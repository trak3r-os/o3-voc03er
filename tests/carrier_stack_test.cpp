// SPDX-License-Identifier: GPL-3.0-or-later
// The carrier's oscillator stack (U10a): the plan under its cycle budget, the old path kept bit for bit, aliasing, SUB, TILT, the SHAPE morph
// and the step when the plan changes.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/carrier.h>
#include <o3_voc03er/units/vocoder/carrier_plan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace o3_voc03er;

namespace {

constexpr float kFs = 44100.0f;

// The scenario whose hashes were taken on the Carrier of before this slice (set_wave, set_detune_cents(0)): three notes, a fourth, two released.
std::uint64_t scenario_hash(int wave, float cents, float noise) {
  Carrier c;
  c.prepare(kFs);
  c.set_attack_ms(2.0f);
  c.set_release_ms(30.0f);
  c.set_shape(static_cast<float>(wave));
  c.set_detune_cents(cents);
  c.set_noise(noise);
  std::uint64_t h = 1469598103934665603ull;
  float buf[64], gate[64];
  for (int b = 0; b < 40; ++b) {
    if (b == 0) {
      c.note_on(48);
      c.note_on(55);
      c.note_on(64);
    }
    if (b == 8) {
      c.note_on(100);
    }
    if (b == 20) {
      c.note_off(55);
      c.note_off(48);
    }
    c.render(buf, 64, gate);
    for (int i = 0; i < 64; ++i) {
      std::uint32_t u;
      std::memcpy(&u, &buf[i], 4);
      h = (h ^ u) * 1099511628211ull;
      std::memcpy(&u, &gate[i], 4);
      h = (h ^ u) * 1099511628211ull;
    }
  }
  return h;
}

struct Cfg {
  float shape = 0.0f;
  float spread = 0.0f;
  float sub = 0.0f;
  float tilt = 0.0f;
};

void setup(Carrier& c, const Cfg& g) {
  c.prepare(kFs);
  c.set_attack_ms(0.1f);
  c.set_release_ms(30.0f);
  c.set_shape(g.shape);
  c.set_detune_cents(g.spread);
  c.set_sub(g.sub);
  c.set_tilt_db(g.tilt);
}

// `skip` blocks are rendered and dropped (the tilt settles in a few), then `n` samples are kept.
std::vector<float> render_notes(const Cfg& g, const std::vector<int>& notes, unsigned skip, unsigned n) {
  Carrier c;
  setup(c, g);
  for (int k : notes) {
    c.note_on(static_cast<std::uint8_t>(k));
  }
  std::vector<float> out;
  float buf[64];
  for (unsigned b = 0; b < skip + n / 64; ++b) {
    c.render(buf, 64);
    if (b >= skip) {
      out.insert(out.end(), buf, buf + 64);
    }
  }
  return out;
}

// Hann-windowed power spectrum, N = x.size() (a power of two or not), bins 0..N/2.
std::vector<double> spectrum(const std::vector<float>& x) {
  const std::size_t n = x.size();
  std::vector<double> tw_c(n), tw_s(n), w(n);
  const double pi = 3.14159265358979323846;
  for (std::size_t i = 0; i < n; ++i) {
    tw_c[i] = std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(n));
    tw_s[i] = std::sin(2.0 * pi * static_cast<double>(i) / static_cast<double>(n));
    w[i] = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(n));
  }
  std::vector<double> p(n / 2 + 1);
  for (std::size_t k = 0; k <= n / 2; ++k) {
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t j = (k * i) % n;
      const double v = static_cast<double>(x[i]) * w[i];
      re += v * tw_c[j];
      im -= v * tw_s[j];
    }
    p[k] = re * re + im * im;
  }
  return p;
}

double peak_near(const std::vector<double>& p, double bin, int span) {
  double m = 0.0;
  const long c = std::lround(bin);
  for (long k = c - span; k <= c + span; ++k) {
    if (k >= 0 && k < static_cast<long>(p.size())) {
      m = std::max(m, p[static_cast<std::size_t>(k)]);
    }
  }
  return m;
}

double hz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// The share of the energy that is not on a harmonic of f0 (a Hann main lobe is +-2 bins), in dB: what the aliasing and the BLEP's error leave.
double nonharmonic_db(const std::vector<float>& x, double f0) {
  const auto p = spectrum(x);
  const double n = static_cast<double>(x.size());
  std::vector<bool> harm(p.size(), false);
  for (std::size_t k = 0; k < std::min<std::size_t>(3, p.size()); ++k) {
    harm[k] = true;  // DC
  }
  for (double f = f0; f < static_cast<double>(kFs) * 0.5; f += f0) {
    const long c = std::lround(f / static_cast<double>(kFs) * n);
    for (long k = c - 2; k <= c + 2; ++k) {
      if (k >= 0 && k < static_cast<long>(p.size())) {
        harm[static_cast<std::size_t>(k)] = true;
      }
    }
  }
  double tot = 0.0, non = 0.0;
  for (std::size_t k = 0; k < p.size(); ++k) {
    tot += p[k];
    non += harm[k] ? 0.0 : p[k];
  }
  return 10.0 * std::log10(non / tot);
}

}  // namespace

TEST_CASE("carrier plan: the table, the budget and the stated rows") {
  using namespace carrier_cost;
  for (unsigned n = 0; n <= 9; ++n) {
    for (unsigned f = 0; f < 16; ++f) {
      const bool spread = (f & 1u) != 0, morph = (f & 2u) != 0, sub = (f & 4u) != 0, tilt = (f & 8u) != 0;
      const CarrierPlan p = plan_oscillators(n, spread, morph, sub, tilt);
      const unsigned nn = n > 8 ? 8 : n;
      const unsigned t = tilt ? kTilt : 0u;
      CHECK(p.third <= nn);
      CHECK(p.morph1 <= nn);
      CHECK(p.sub <= nn);
      CHECK((spread ? true : p.third == 0));
      CHECK((morph ? true : (p.morph1 == 0 && !p.full)));
      CHECK((sub ? true : p.sub == 0));
      const unsigned expect = nn == 0 ? 0u
                              : p.full ? nn * kExt + t
                                       : nn * kVoice + t + p.sub * kSub + p.morph1 * kMorph1 + p.third * kThird;
      CHECK(p.cycles == expect);
      CHECK(p.cycles <= kBudget);  // the plan never goes past the budget
      if (p.full) {
        CHECK(nn * kExt + t <= kBudget);
      }
      if (n > 0 && n < 8 && spread && !morph) {
        const CarrierPlan q = plan_oscillators(n + 1, spread, morph, sub, tilt);
        CHECK(n - p.third <= n + 1 - q.third);  // a voice more never serves more of the older ones
      }
    }
  }
  // The stated rows: SPREAD only: no third oscillator at 8 notes, 2 at 7, 5 at 6; 5 notes or fewer: all voices. SUB only at 8 notes: 4 voices.
  CHECK(plan_oscillators(8, true, false, false, false).third == 0);
  CHECK(plan_oscillators(7, true, false, false, false).third == 2);
  CHECK(plan_oscillators(6, true, false, false, false).third == 5);
  for (unsigned n = 1; n <= 5; ++n) {
    CHECK(plan_oscillators(n, true, false, false, false).third == n);
  }
  CHECK(plan_oscillators(8, false, false, true, false).sub == 4);
  CHECK(plan_oscillators(8, false, true, false, false).morph1 == 2);
  CHECK(!plan_oscillators(8, false, true, false, false).full);
  CHECK(plan_oscillators(3, false, true, false, true).full);   // 3 x 11,500 + 770 fits
  CHECK(!plan_oscillators(4, false, true, false, true).full);  // 4 x 11,500 + 770 does not
  CHECK(plan_oscillators(4, false, true, false, false).full);  // 46,000 does
  CHECK(!plan_oscillators(3, true, false, true, true).full);   // no morph, never full
  for (unsigned n = 6; n <= 8; ++n) {
    const struct { const char* name; bool s, m, u, t; } rows[] = {{"spread", true, false, false, false}, {"sub", false, false, true, false},
                                                                  {"morph", false, true, false, false}, {"tilt", false, false, false, true},
                                                                  {"spread+sub", true, false, true, false}, {"all", true, true, true, true}};
    for (const auto& r : rows) {
      const CarrierPlan p = plan_oscillators(n, r.s, r.m, r.u, r.t);
      std::printf("plan n=%u %-11s sub=%u morph1=%u third=%u full=%d cycles=%u\n", n, r.name, unsigned{p.sub}, unsigned{p.morph1}, unsigned{p.third},
                  p.full ? 1 : 0, p.cycles);
    }
  }
}

TEST_CASE("carrier stack: SPREAD, SUB and TILT at 0 with SHAPE 0 or 1 are the old path, bit for bit") {
  // Hashes taken on the Carrier before U10a (the same scenario, set_wave).
  CHECK(scenario_hash(0, 0.0f, 0.0f) == 0xcf05b7523239e764ull);
  CHECK(scenario_hash(0, 0.0f, 0.3f) == 0xa5d09b4b0f5e15c9ull);
  CHECK(scenario_hash(1, 0.0f, 0.0f) == 0x05ba60a92267c143ull);
  CHECK(scenario_hash(1, 0.0f, 0.3f) == 0xaaf624419242d5b1ull);
  // The two-oscillator hashes of before (cents 8: 0xf22e8d941d401e69, 0x670086553b58603d) do not hold: SPREAD > 0 now adds the third oscillator
  // on every voice the plan serves (4 notes here), by design. A voice the plan does not serve is the two-oscillator loop of before.
  CHECK(scenario_hash(0, 8.0f, 0.0f) != 0xf22e8d941d401e69ull);
}

TEST_CASE("carrier stack: aliasing at the top notes for each SHAPE stays under its bound") {
  const float shapes[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
  double worst = -200.0;
  for (int note : {84, 96, 108}) {
    for (float s : shapes) {
      Cfg g;
      g.shape = s;
      const double db = nonharmonic_db(render_notes(g, {note}, 8, 4096), hz(note));
      std::printf("aliasing note %d shape %.1f: %.1f dB below the whole\n", note, static_cast<double>(s), db);
      worst = std::max(worst, db);
    }
  }
  CHECK(worst < -18.0);  // polyBLEP at 1 to 4 kHz (the saw and pulse of before: -25.6 dB at note 108); the narrow pulse at 4.2 kHz is the worst, -19.2 dB:
                         // its two edges fall within a sample of each other. The measured values are printed above.
}

TEST_CASE("carrier stack: SUB sounds an octave below") {
  Cfg off, on;
  on.sub = 1.0f;
  const std::vector<int> note{57};  // 220 Hz
  const auto p0 = spectrum(render_notes(off, note, 8, 4096));
  const auto p1 = spectrum(render_notes(on, note, 8, 4096));
  const double bin110 = 110.0 / static_cast<double>(kFs) * 4096.0;
  const double bin220 = 220.0 / static_cast<double>(kFs) * 4096.0;
  CHECK(peak_near(p1, bin110, 2) > 1000.0 * peak_near(p0, bin110, 2));  // nothing there without SUB
  // The highest peak below the note's own fundamental is the sub's: 110 Hz.
  std::size_t best = 4;
  for (std::size_t k = 4; k < static_cast<std::size_t>(bin220 * 0.8); ++k) {
    if (p1[k] > p1[best]) {
      best = k;
    }
  }
  CHECK(std::abs(static_cast<double>(best) - bin110) <= 1.5);
}

TEST_CASE("carrier stack: TILT is within 1 dB of its claim at 100 Hz and 10 kHz") {
  for (float t : {12.0f, -12.0f, 6.0f}) {
    Cfg flat, tilted;
    tilted.tilt = t;
    // note 43 is 98 Hz (its fundamental is the 100 Hz point); the 13th harmonic of note 79 is 10.2 kHz.
    const auto lo0 = spectrum(render_notes(flat, {43}, 40, 4096));
    const auto lo1 = spectrum(render_notes(tilted, {43}, 40, 4096));
    const auto hi0 = spectrum(render_notes(flat, {79}, 40, 4096));
    const auto hi1 = spectrum(render_notes(tilted, {79}, 40, 4096));
    const double bl = hz(43) / static_cast<double>(kFs) * 4096.0;
    const double bh = 13.0 * hz(79) / static_cast<double>(kFs) * 4096.0;
    const double d_lo = 10.0 * std::log10(peak_near(lo1, bl, 2) / peak_near(lo0, bl, 2));
    const double d_hi = 10.0 * std::log10(peak_near(hi1, bh, 2) / peak_near(hi0, bh, 2));
    std::printf("tilt %+.0f dB: at 98 Hz %+.2f dB (claim %+.1f), at 10.2 kHz %+.2f dB (claim %+.1f)\n", static_cast<double>(t), d_lo,
                static_cast<double>(-t / 2), d_hi, static_cast<double>(t / 2));
    CHECK(std::abs(d_lo - static_cast<double>(-t / 2)) < 1.0);
    CHECK(std::abs(d_hi - static_cast<double>(t / 2)) < 1.0);
  }
}

TEST_CASE("carrier stack: SHAPE has no jump in level or polarity through the ends of the morph") {
  struct Pair { float a, b; };
  for (const Pair pr : {Pair{0.0f, 0.0005f}, Pair{0.9995f, 1.0f}, Pair{1.0f, 1.0005f}}) {
    Cfg ga, gb;
    ga.shape = pr.a;
    gb.shape = pr.b;
    const auto a = render_notes(ga, {45}, 2, 2048);
    const auto b = render_notes(gb, {45}, 2, 2048);
    double worst = 0.0, peak = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
      worst = std::max(worst, static_cast<double>(std::fabs(a[i] - b[i])));
      peak = std::max(peak, static_cast<double>(std::fabs(a[i])));
    }
    std::printf("shape %.4f vs %.4f: worst sample difference %.4f of a peak %.3f\n", static_cast<double>(pr.a), static_cast<double>(pr.b), worst, peak);
    CHECK(worst < 0.2 * peak);  // 1.0005 moves the pulse edges by 0.08 sample at this note (a 0.04 step, 15 % of the peak); a jump in level or polarity would be 100 % or 200 %
  }
}

TEST_CASE("carrier stack: a change of plan (a voice starts) steps the output by no more than the bound") {
  // SHAPE between the ends, SPREAD and SUB on: six notes, then a seventh and an eighth change the plan (third oscillators and morphs drop
  // out of the older voices), then two are released. The largest sample-to-sample step is compared with the steady state's.
  Carrier c;
  Cfg g;
  g.shape = 0.5f;
  g.spread = 30.0f;
  g.sub = 0.5f;
  g.tilt = 3.0f;
  setup(c, g);
  c.set_attack_ms(4.0f);
  const int notes[] = {40, 44, 47, 52, 56, 59, 64, 67};
  for (int i = 0; i < 6; ++i) {
    c.note_on(static_cast<std::uint8_t>(notes[i]));
  }
  float prev = 0.0f, buf[64];
  double steady = 0.0, change = 0.0;
  for (int b = 0; b < 60; ++b) {
    if (b == 20) {
      c.note_on(static_cast<std::uint8_t>(notes[6]));
    }
    if (b == 30) {
      c.note_on(static_cast<std::uint8_t>(notes[7]));
    }
    c.render(buf, 64);
    double m = 0.0;
    for (int i = 0; i < 64; ++i) {
      m = std::max(m, static_cast<double>(std::fabs(buf[i] - prev)));
      prev = buf[i];
    }
    if (b >= 4 && b < 20) {
      steady = std::max(steady, m);
    }
    if (b >= 20 && b < 40) {
      change = std::max(change, m);
    }
    if (b >= 40) {
      steady = std::max(steady, m);  // the eight-voice steady state too: eight voices step more than six by chance alone, plan or no plan
    }
  }
  std::printf("plan change: steady max step %.4f, max step around the changes %.4f\n", steady, change);
  CHECK(change < 1.5);  // the output is within about +-2 before the soft clip; the step of the plan's switch stays under this
  CHECK(change <= 1.1 * steady + 0.05);  // measured (U10c): 0.77 around the changes against 0.72 in the eight-voice steady state
}

TEST_CASE("carrier stack: the specialised paths (third, SUB, morph on oscillator 1) match the generic path within 1e-5") {
  struct Row { Cfg g; std::vector<int> notes; };
  Cfg a;  // a steady third
  a.spread = 8.0f;
  Cfg b;  // a third and SUB on four voices
  b.spread = 30.0f;
  b.sub = 0.6f;
  Cfg c;  // the morph on oscillator 1 (3 of 6 voices), SUB on all of them, TILT
  c.shape = 0.5f;
  c.sub = 1.0f;
  c.tilt = 3.0f;
  Cfg d;  // the narrow-pulse side of the morph with a third
  d.shape = 1.5f;
  d.spread = 20.0f;
  d.sub = 0.3f;
  const Row rows[] = {{a, {48, 55, 64, 71}}, {b, {45, 52, 57, 64}}, {c, {40, 44, 47, 52, 56, 59}}, {d, {43, 50, 55, 62, 67, 74}}};
  int k = 0;
  for (const Row& r : rows) {
    Carrier fast, ext;
    setup(fast, r.g);
    setup(ext, r.g);
    ext.set_force_ext(true);
    fast.set_attack_ms(3.0f);
    ext.set_attack_ms(3.0f);
    for (int n : r.notes) {
      fast.note_on(static_cast<std::uint8_t>(n));
      ext.note_on(static_cast<std::uint8_t>(n));
    }
    double worst = 0.0, peak = 0.0;
    float x[64], y[64];
    for (int blk = 0; blk < 60; ++blk) {
      if (blk == 40) {
        fast.note_off(static_cast<std::uint8_t>(r.notes[0]));
        ext.note_off(static_cast<std::uint8_t>(r.notes[0]));
      }
      fast.render(x, 64);
      ext.render(y, 64);
      for (int i = 0; i < 64; ++i) {
        worst = std::max(worst, static_cast<double>(std::fabs(x[i] - y[i])));
        peak = std::max(peak, static_cast<double>(std::fabs(x[i])));
      }
    }
    std::printf("specialised vs generic, row %d: worst difference %.2e of a peak %.3f\n", k, worst, peak);
    CHECK(peak > 0.1);
    CHECK(worst < 1.0e-5);
    ++k;
  }
}

TEST_CASE("carrier stack: SUB moving between voices (a ninth note arrives and leaves) steps no more than the bound") {
  Carrier c;
  Cfg g;
  g.sub = 1.0f;
  g.spread = 0.0f;
  setup(c, g);
  c.set_attack_ms(4.0f);
  const int chord[] = {48, 50, 53, 55, 60, 62, 65, 67};
  for (int n : chord) {
    c.note_on(static_cast<std::uint8_t>(n));
  }
  float prev = 0.0f, buf[64];
  double steady = 0.0, change = 0.0;
  for (int b = 0; b < 90; ++b) {
    if (b == 30) {
      c.note_on(36);  // the lowest note: it steals the oldest voice, and SUB moves to it
    }
    if (b == 45) {
      c.note_off(36);  // and leaves: when its release ends there are seven voices, and SUB moves again
    }
    c.render(buf, 64);
    double m = 0.0;
    for (int i = 0; i < 64; ++i) {
      m = std::max(m, static_cast<double>(std::fabs(buf[i] - prev)));
      prev = buf[i];
    }
    if (b >= 8 && b < 30) {
      steady = std::max(steady, m);
    }
    if (b >= 30) {
      change = std::max(change, m);
    }
  }
  std::printf("SUB between voices: steady max step %.4f, around the changes %.4f\n", steady, change);
  CHECK(change < 1.5);
  CHECK(change <= 1.1 * steady + 0.05);
}

TEST_CASE("carrier stack: SUB goes to the lowest notes of a held chord") {
  Cfg off, on;
  on.sub = 1.0f;
  const std::vector<int> chord{45, 48, 52, 55, 59, 63, 67, 91};  // 8 voices: the plan gives SUB to 4, the lowest
  const auto p0 = spectrum(render_notes(off, chord, 12, 8192));
  const auto p1 = spectrum(render_notes(on, chord, 12, 8192));
  const double bins = 8192.0 / static_cast<double>(kFs);
  const double low = hz(45) * 0.5 * bins;   // an octave below the lowest note
  const double top = hz(91) * 0.5 * bins;   // an octave below the top note
  const double r_low = peak_near(p1, low, 2) / peak_near(p0, low, 2);
  const double r_top = peak_near(p1, top, 2) / peak_near(p0, top, 2);
  std::printf("SUB on the lowest notes: the octave below the lowest note x%.1f, below the top note x%.2f\n", r_low, r_top);
  CHECK(r_low > 1000.0);
  CHECK(r_top < 1.5);
}
