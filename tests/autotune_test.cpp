// SPDX-License-Identifier: GPL-3.0-or-later
// Autotune (TUNE's snap, retune and period-jump shifter) against synthetic voices of known pitch: the nearest scale note in cents at SPEED 0
// and at a slower SPEED, level and formants kept, no click at a period jump or a voiced edge, a held note over the scale and the fall back,
// a vibrato that does not flip the note, unvoiced noise passed bit for bit, block-length independence, NaN, and the Vocoder's mode switches.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/autotune.h>
#include <o3_voc03er/units/vocoder/vocoder.h>
#include <o3sdk/dsp/event.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

namespace {

constexpr double kFs = 44100.0;
constexpr double kPi = 3.14159265358979323846;

static_assert(sizeof(Autotune) <= 14 * 1024, "the tuner is about 13 KiB in SRAM");

struct Reson {
  double a1 = 0, a2 = 0, g = 0, y1 = 0, y2 = 0;
  void set(double f, double bw, double gain) {
    const double r = std::exp(-kPi * bw / kFs);
    a1 = 2.0 * r * std::cos(2.0 * kPi * f / kFs);
    a2 = -r * r;
    g = gain * (1.0 - r);
  }
  double run(double x) {
    const double y = g * x + a1 * y1 + a2 * y2;
    y2 = y1;
    y1 = y;
    return y;
  }
};

// A saw (band-limited step) through three formant resonators (vowel A), peak about 0.3.
struct Voice {
  Reson r[3];
  double phase = 0;
  Voice() {
    const double f[3] = {700, 1220, 2600}, bw[3] = {90, 110, 170}, g[3] = {1.0, 0.7, 0.4};
    for (int i = 0; i < 3; ++i) r[i].set(f[i], bw[i], g[i]);
  }
  static double blep(double p, double dt) {
    if (p < dt) { const double x = p / dt; return x + x - x * x - 1.0; }
    if (p > 1.0 - dt) { const double x = (p - 1.0) / dt; return x * x + x + x + 1.0; }
    return 0.0;
  }
  float next(double f0) {
    phase += f0 / kFs;
    if (phase >= 1.0) phase -= 1.0;
    const double s = 2.0 * phase - 1.0 - blep(phase, f0 / kFs);
    double y = 0;
    for (auto& q : r) y += q.run(s);
    return static_cast<float>(y * 0.9);
  }
};

double hz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }
double cents(double f, double f0) { return 1200.0 * std::log2(f / f0); }

// Runs `n` samples of gen(i) through the tuner in blocks of `blk`.
template <class Gen>
std::vector<float> run(Autotune& a, std::size_t n, Gen gen, unsigned blk = 64) {
  std::vector<float> in(n), out(n);
  for (std::size_t i = 0; i < n; ++i) in[i] = gen(i);
  for (std::size_t i = 0; i < n; i += blk) a.process(in.data() + i, out.data() + i, static_cast<unsigned>(std::min<std::size_t>(blk, n - i)));
  return out;
}

// The pitch of out[from, to) in Hz (a fresh detector), 0 when unvoiced.
double pitch_of(const std::vector<float>& x, std::size_t from, std::size_t to) {
  Tune t;
  for (std::size_t i = from; i + 64 <= to; i += 64) t.push(x.data() + i, 64);
  return t.voiced() ? static_cast<double>(t.frequency()) : 0.0;
}

float max_step(const std::vector<float>& x, std::size_t from, std::size_t to) {
  float s = 0.0f;
  for (std::size_t i = from + 1; i < to; ++i) s = std::max(s, std::fabs(x[i] - x[i - 1]));
  return s;
}

double rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
  double s = 0;
  for (std::size_t i = from; i < to; ++i) s += static_cast<double>(x[i]) * static_cast<double>(x[i]);
  return std::sqrt(s / static_cast<double>(to - from));
}

// Spectral centroid over the harmonics of f0 up to 6 kHz (Goertzel at k * f0, over a whole number of periods).
double centroid(const std::vector<float>& x, std::size_t from, std::size_t to, double f0) {
  double num = 0, den = 0;
  for (double f = f0; f <= 6000; f += f0) {
    const double w = 2.0 * kPi * f / kFs, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (std::size_t i = from; i < to; ++i) {
      const double s = static_cast<double>(x[i]) + c * s1 - s2;
      s2 = s1;
      s1 = s;
    }
    const double e = s1 * s1 + s2 * s2 - c * s1 * s2;
    num += f * e;
    den += e;
  }
  return num / den;
}

std::vector<float> steady(Autotune& a, double f, double secs, unsigned blk = 64) {
  Voice v;
  return run(a, static_cast<std::size_t>(secs * kFs), [&](std::size_t) { return v.next(f); }, blk);
}

Autotune& fresh() {
  static Autotune a;
  a.reset();
  a.all_notes_off();
  a.set_key(0);
  a.set_scale(Autotune::Chromatic);
  a.set_speed_ms(0.0f);
  a.set_amount(1.0f);
  a.set_notes_in(true);
  return a;
}

}  // namespace

TEST_CASE("autotune: a voice off the scale ends on the nearest scale note, at SPEED 0 and at a slower SPEED") {
  struct Case { double midi; unsigned scale; double expect; };
  // F4 + 30 cents in C major -> F4; C#4 + 40 cents in C major (C# is not in it; D is 60 cents away, C 140) -> D4; A4 - 35 chromatic -> A4.
  const Case cs[] = {{65.30, Autotune::Major, 65.0}, {61.40, Autotune::Major, 62.0}, {68.65, Autotune::Chromatic, 69.0}};
  for (const Case& c : cs) {
    for (float speed : {0.0f, 120.0f}) {
      Autotune& a = fresh();
      a.set_scale(c.scale);
      a.set_speed_ms(speed);
      const double secs = speed == 0.0f ? 1.2 : 2.5;
      const auto out = steady(a, hz(c.midi), secs);
      const std::size_t n = out.size();
      const double f = pitch_of(out, n - 20000, n);
      INFO("midi " << c.midi << " speed " << speed << " out " << f << " Hz");
      REQUIRE(f > 0.0);
      const double err = cents(f, hz(c.expect));
      std::printf("autotune: in %.2f speed %.0f -> %.2f cents off the target\n", c.midi, static_cast<double>(speed), err);
      CHECK(std::fabs(err) < (speed == 0.0f ? 8.0 : 12.0));
      CHECK(a.target_note() == static_cast<int>(c.expect));
    }
  }
}

TEST_CASE("autotune: shifting keeps the level and the formants' rough place") {
  Autotune& a = fresh();
  a.note_on(70);  // A#4 for a 440 Hz voice: +100 cents
  const auto out = steady(a, 440.0, 1.5);
  const std::size_t n = out.size();
  Autotune& b = fresh();
  b.set_amount(0.0f);
  const auto ref = steady(b, 440.0, 1.5);
  const double lv = rms(out, n - 20000, n) / rms(ref, n - 20000, n);
  const double cr = centroid(out, n - 20000, n, hz(70)) / centroid(ref, n - 20000, n, 440.0);
  std::printf("autotune: +100 cents: level ratio %.3f, centroid ratio %.3f (a semitone is 1.059)\n", lv, cr);
  CHECK(lv > 0.8);
  CHECK(lv < 1.25);
  CHECK(cr > 0.97);
  CHECK(cr < 1.12);  // the formants move with the shift in a period-jump shifter: at most about the ratio itself
  CHECK(std::fabs(cents(pitch_of(out, n - 20000, n), hz(70))) < 8.0);
}

TEST_CASE("autotune: no click at a period jump or at a voiced edge") {
  for (double f0 : {110.0, 220.0, 600.0}) {
    Autotune& a = fresh();
    a.note_on(static_cast<std::uint8_t>(std::lround(69 + 12 * std::log2(f0 / 440.0)) + 3));  // +3 semitones: a jump every few periods
    Voice v;
    // 0.3 s silence, 1.0 s voice, 0.3 s silence, 0.8 s voice, 0.3 s silence.
    const auto gen = [&](std::size_t i) {
      const double t = static_cast<double>(i) / kFs;
      const bool on = (t >= 0.3 && t < 1.3) || (t >= 1.6 && t < 2.4);
      const float x = v.next(f0);
      return on ? x : 0.0f;
    };
    std::vector<float> in(static_cast<std::size_t>(2.7 * kFs));
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = gen(i);
    std::vector<float> out(in.size());
    for (std::size_t i = 0; i < in.size(); i += 64) a.process(in.data() + i, out.data() + i, static_cast<unsigned>(std::min<std::size_t>(64, in.size() - i)));
    const float own = max_step(in, 0, in.size());
    const float got = max_step(out, 0, out.size());
    std::printf("autotune: f0 %.0f: largest step in %.4f out %.4f\n", f0, static_cast<double>(own), static_cast<double>(got));
    // +3 semitones raise every slope by up to 1.19; the cubic and the crossfade add no step beyond that and a small margin.
    CHECK(got <= 1.3f * own);
    // Edges only: the steady part of the shifted voice, then the 20 ms around each edge, against the signal's own largest step.
    CHECK(max_step(out, static_cast<std::size_t>(1.25 * kFs), static_cast<std::size_t>(1.8 * kFs)) <= 1.3f * own);
  }
}

TEST_CASE("autotune: a held note overrides the scale; with none held it falls back to KEY and SCALE") {
  Autotune& a = fresh();
  a.set_scale(Autotune::Major);
  a.set_speed_ms(0.0f);
  a.note_on(64);  // E4
  auto out = steady(a, hz(65.30), 1.2);
  CHECK(a.target_note() == 64);
  CHECK(std::fabs(cents(pitch_of(out, out.size() - 20000, out.size()), hz(64))) < 8.0);
  a.note_on(52);  // E3: the latest held note wins, in the octave nearest the voice
  out = steady(a, hz(65.30), 0.8);
  CHECK(a.target_note() == 64);
  a.note_off(52);  // back to the E4 still held
  a.note_off(64);
  a.reset();
  out = steady(a, hz(65.30), 1.0);
  CHECK(a.target_note() == 65);  // none held: F4 of C major
  CHECK(std::fabs(cents(pitch_of(out, out.size() - 20000, out.size()), hz(65))) < 8.0);
  // NOTES-IN off: a held note is ignored.
  a.reset();
  a.set_notes_in(false);
  a.note_on(64);
  out = steady(a, hz(65.30), 1.0);
  CHECK(a.target_note() == 65);
  // All notes off clears the stack.
  a.set_notes_in(true);
  a.all_notes_off();
  a.reset();
  steady(a, hz(65.30), 1.0);
  CHECK(a.target_note() == 65);
}

TEST_CASE("autotune: a vibrato of +-50 cents does not flip the note") {
  Autotune& a = fresh();
  Voice v;
  const std::size_t n = static_cast<std::size_t>(2.5 * kFs);
  std::vector<float> in(n), out(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / kFs;
    in[i] = v.next(440.0 * std::pow(2.0, 0.5 / 12.0 * std::sin(2.0 * kPi * 5.5 * t)));
  }
  unsigned flips = 0, seen = 0;
  int last = -1;
  for (std::size_t i = 0; i < n; i += 64) {
    a.process(in.data() + i, out.data() + i, static_cast<unsigned>(std::min<std::size_t>(64, n - i)));
    if (i > static_cast<std::size_t>(0.4 * kFs) && a.target_note() >= 0) {
      ++seen;
      if (last >= 0 && a.target_note() != last) ++flips;
      last = a.target_note();
    }
  }
  CHECK(seen > 100u);
  CHECK(flips == 0u);
  CHECK(last == 69);
}

TEST_CASE("autotune: unvoiced noise passes unchanged, only delayed") {
  Autotune& a = fresh();
  std::uint32_t s = 777;
  std::vector<float> in(static_cast<std::size_t>(1.5 * kFs));
  for (float& x : in) {
    s = s * 1664525u + 1013904223u;
    x = 0.1f * (static_cast<float>(s >> 8) / 8388608.0f - 1.0f);
  }
  std::vector<float> out(in.size());
  for (std::size_t i = 0; i < in.size(); i += 64) a.process(in.data() + i, out.data() + i, static_cast<unsigned>(std::min<std::size_t>(64, in.size() - i)));
  CHECK_FALSE(a.voiced());
  const int d = static_cast<int>(a.delay());
  CHECK(static_cast<float>(d) == a.delay());
  double worst = 0;
  for (std::size_t i = 2000; i + static_cast<std::size_t>(d) < in.size(); ++i) {
    worst = std::max(worst, static_cast<double>(std::fabs(out[i + static_cast<std::size_t>(d)] - in[i])));
  }
  CHECK(worst == 0.0);
}

TEST_CASE("autotune: the output is bit-identical for any block length") {
  Autotune& a = fresh();
  a.set_scale(Autotune::Minor);
  const auto ref = steady(a, hz(66.4), 0.6, 64);
  for (unsigned blk : {1u, 7u, 37u, 16u}) {
    Autotune& b = fresh();
    b.set_scale(Autotune::Minor);
    const auto o = steady(b, hz(66.4), 0.6, blk);
    CHECK(o == ref);
  }
}

TEST_CASE("autotune: NaN and infinity do not poison it") {
  Autotune& a = fresh();
  Voice v;
  std::vector<float> in(static_cast<std::size_t>(0.8 * kFs)), out(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) in[i] = v.next(hz(65.3));
  in[10000] = std::nanf("");
  in[10001] = INFINITY;
  in[20000] = -INFINITY;
  for (std::size_t i = 0; i < in.size(); i += 64) a.process(in.data() + i, out.data() + i, static_cast<unsigned>(std::min<std::size_t>(64, in.size() - i)));
  for (float x : out) REQUIRE(std::isfinite(x));
  CHECK(a.voiced());
  CHECK(std::isfinite(a.shift_cents()));
  CHECK(std::fabs(a.shift_cents()) <= 600.0f);
}

namespace {

std::vector<float> run_unit(Vocoder& vo, std::size_t blocks, const std::vector<std::pair<std::size_t, float>>& modes, bool notes) {
  Voice v;
  std::vector<float> out(blocks * 64);
  dsp::Transport tr;
  for (std::size_t b = 0; b < blocks; ++b) {
    float m[64], r[64];
    for (float& x : m) x = v.next(hz(65.3));
    dsp::Event ev[4];
    std::uint32_t n = 0;
    if (b == 0 && notes) {
      ev[n++] = dsp::note_on(48, 100);
      ev[n++] = dsp::note_on(55, 100);
    }
    for (const auto& p : modes) {
      if (p.first == b) ev[n++] = dsp::param_change(Vocoder::Mode, p.second);
    }
    dsp::ProcessArgs a{};
    a.events = ev;
    a.event_count = n;
    a.in_l = m;
    a.in_r = m;
    a.out_l = out.data() + b * 64;
    a.out_r = r;
    a.frames = 64;
    a.transport = &tr;
    vo.process(a);
  }
  return out;
}

}  // namespace

TEST_CASE("vocoder: TUNE is mode 2 after TALK, the new parameters are appended, and TUNE tunes") {
  static Vocoder vo;
  vo.prepare(static_cast<float>(kFs));
  CHECK(Vocoder::kParamCount == 40);
  CHECK(Vocoder::Mode == 7);
  CHECK(Vocoder::CarSrc == 34);
  CHECK(Vocoder::Key == 35);
  CHECK(Vocoder::NotesIn == 39);
  const auto& info = vo.info();
  CHECK(info.params[Vocoder::Mode].max == 2.0f);
  CHECK(std::string(info.params[Vocoder::Mode].names[2]) == "TUNE");
  CHECK(std::string(info.params[Vocoder::Mode].names[1]) == "TALK");
  CHECK(std::string(info.params[Vocoder::Key].name) == "KEY");
  CHECK(std::string(info.params[Vocoder::NotesIn].name) == "NOTES-IN");
  // In TUNE the voice itself is the output, corrected: F4 + 30 cents with a held F# (the carrier's notes are not heard).
  const auto out = run_unit(vo, 1400, {{100, 2.0f}}, false);
  REQUIRE(!out.empty());
  for (float x : out) REQUIRE(std::isfinite(x));
  CHECK(rms(out, 1000 * 64, 1400 * 64) > 0.01);
  CHECK(vo.tune().voiced());
  CHECK(vo.tune().target_note() == 65);
}

TEST_CASE("vocoder: MODE switches into and out of TUNE, in every pair, are click-free") {
  static Vocoder vo;
  vo.prepare(static_cast<float>(kFs));
  // BANDS -> TUNE -> BANDS -> TALK -> TUNE -> TALK, one switch every 500 blocks, with the carrier playing.
  const std::vector<std::pair<std::size_t, float>> sw = {{500, 2.0f}, {1000, 0.0f}, {1500, 1.0f}, {2000, 2.0f}, {2500, 1.0f}};
  const auto out = run_unit(vo, 3000, sw, true);
  for (float x : out) REQUIRE(std::isfinite(x));
  CHECK(*std::max_element(out.begin(), out.end()) < 1.0f);
  float own = 0.0f;  // the modes' own steps: the stretch before each switch, once the mode has settled
  for (std::size_t k = 0; k < 5; ++k) own = std::max(own, max_step(out, (sw[k].first - 150) * 64, sw[k].first * 64));
  CHECK(own > 0.0f);
  for (const auto& s : sw) {
    const float st = max_step(out, s.first * 64 - 64, (s.first + 40) * 64);
    std::printf("autotune: switch at block %zu: step %.4f (own %.4f)\n", s.first, static_cast<double>(st), static_cast<double>(own));
    CHECK(st <= 1.5f * own);
  }
  // Back in BANDS after TUNE the output is Bands' again (not silent, not the dry voice).
  CHECK(rms(out, 1400 * 64, 1500 * 64) > 0.01);
}
