// SPDX-License-Identifier: GPL-3.0-or-later
// TRACT, the vocoder's fifth mode (V1): the carrier through TractUnit (a vocal tract, o3-sdk's extras/tract). The four old modes sound as they did (a
// digest of the unit's output per mode, taken in the tree before TRACT existed); in TRACT the carrier, whatever CarSrc picked, speaks through the
// tube (formants: A and U differ in their spectra); a mode change makes no step; the chain does not depend on the block size; the tube sleeps on
// silence and wakes cleanly; the app keeps the tube's MIX in step with the Mode and the console reaches the tube.
#include <doctest/doctest.h>

#include "../app/src/perform_logic.h"

#include <o3_voc03er/modes.h>
#include <o3_voc03er/units/tract/tract.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/app.h>
#include <o3sdk/dsp/event.h>
#include <o3sdk/sim/harness.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;
namespace app = o3sdk::app;
namespace sim = o3sdk::sim;

namespace {

constexpr float kFs = 44100.0f;
constexpr float kPi2 = 6.2831853f;

// ---- the old modes, sample-identical ------------------------------------------------------------------------------------------------------
// Digest of the unit's output (both planes, FNV-1a of the bit patterns) for a saw plus noise at the input and two held notes. The harness is the
// same one that ran in the tree of cdff2a4 (before TRACT), and the values in the test below were taken there.
std::uint32_t digest_run(const unsigned* modes, const std::size_t* at, std::size_t switches, std::size_t blocks) {
  static Vocoder vo;
  vo.prepare(kFs);
  std::uint32_t d = 2166136261u;
  std::uint32_t seed = 777u;
  float ph = 0.0f;
  dsp::Transport tr;
  std::size_t next = 0;
  for (std::size_t b = 0; b < blocks; ++b) {
    float m[64], l[64], r[64];
    for (float& x : m) {
      seed = seed * 1664525u + 1013904223u;
      ph += 110.0f / kFs;
      ph -= std::floor(ph);
      x = 0.25f * (2.0f * ph - 1.0f) + 0.02f * (static_cast<float>(seed >> 8) * (1.0f / 8388608.0f) - 1.0f);
    }
    dsp::Event ev[4];
    std::uint32_t n = 0;
    if (b == 0) {
      ev[n++] = dsp::note_on(48, 100);
      ev[n++] = dsp::note_on(55, 100);
    }
    while (next < switches && at[next] == b) {
      ev[n++] = dsp::param_change(Vocoder::Mode, static_cast<float>(modes[next]));
      ++next;
    }
    dsp::ProcessArgs a{};
    a.events = ev;
    a.event_count = n;
    a.in_l = m;
    a.in_r = m;
    a.out_l = l;
    a.out_r = r;
    a.frames = 64;
    a.transport = &tr;
    vo.process(a);
    for (std::size_t i = 0; i < 64; ++i) {
      std::uint32_t u, w;
      std::memcpy(&u, &l[i], sizeof u);
      std::memcpy(&w, &r[i], sizeof w);
      d = (d ^ u) * 16777619u;
      d = (d ^ w) * 16777619u;
    }
  }
  return d;
}

// ---- the chain: the Vocoder, then the TractUnit in place on its output (slot 0, insert 0) -----------------------------------------------------

struct Ev {
  std::size_t frame;  // a multiple of 64 (an event is delivered at the start of the block it is in)
  bool tract;         // for the TractUnit, else the Vocoder
  std::uint16_t id;
  float value;
};

enum class In { Silence, Voice, Noise };

std::uint32_t g_seed = 1u;

float rnd() {
  g_seed = g_seed * 1664525u + 1013904223u;
  return static_cast<float>(g_seed >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

void fill_in(float* m, std::size_t n, In in, float& ph) {
  for (std::size_t i = 0; i < n; ++i) {
    if (in == In::Voice) {
      ph += 110.0f / kFs;
      ph -= std::floor(ph);
      m[i] = 0.25f * (2.0f * ph - 1.0f) + 0.01f * rnd();
    } else if (in == In::Noise) {
      m[i] = 0.3f * rnd();
    } else {
      m[i] = 0.0f;
    }
  }
}

// `block` frames at a time (the last may be shorter); events only at multiples of `block` when block is 64. Returns the left plane.
std::vector<float> render(std::size_t frames, std::size_t block, const std::vector<Ev>& evs, In in, bool notes = true) {
  static Vocoder vo;
  static TractUnit tu;
  vo.prepare(kFs);
  tu.prepare(kFs);
  g_seed = 99u;
  float ph = 0.0f;
  std::vector<float> out(frames);
  dsp::Transport tr;
  std::size_t pos = 0;
  while (pos < frames) {
    const std::size_t n = std::min(block, frames - pos);
    float m[64], r[64];
    fill_in(m, n, in, ph);
    dsp::Event ve[8], te[8];
    std::uint32_t nv = 0, nt = 0;
    if (pos == 0 && notes) {
      ve[nv++] = dsp::note_on(48, 100);
    }
    for (const Ev& e : evs) {
      if (e.frame >= pos && e.frame < pos + block) {
        (e.tract ? te[nt++] : ve[nv++]) = dsp::param_change(e.id, e.value);
      }
    }
    float* l = out.data() + pos;
    dsp::ProcessArgs a{};
    a.events = ve;
    a.event_count = nv;
    a.in_l = m;
    a.in_r = m;
    a.out_l = l;
    a.out_r = r;
    a.frames = static_cast<std::uint32_t>(n);
    a.transport = &tr;
    vo.process(a);
    dsp::ProcessArgs b{};
    b.events = te;
    b.event_count = nt;
    b.in_l = l;
    b.in_r = r;
    b.out_l = l;
    b.out_r = r;
    b.frames = static_cast<std::uint32_t>(n);
    b.transport = &tr;
    tu.process(b);
    pos += n;
  }
  return out;
}

float max_step(const std::vector<float>& x, std::size_t from, std::size_t to) {
  float s = 0.0f;
  for (std::size_t i = from + 1; i < to; ++i) s = std::max(s, std::fabs(x[i] - x[i - 1]));
  return s;
}

float peak(const std::vector<float>& x, std::size_t from, std::size_t to) {
  float s = 0.0f;
  for (std::size_t i = from; i < to; ++i) s = std::max(s, std::fabs(x[i]));
  return s;
}

// The energy of x[from, from + n) between lo and hi Hz: a Hann window, a Goertzel every 20 Hz.
float band(const std::vector<float>& x, std::size_t from, std::size_t n, float lo, float hi) {
  std::vector<float> w(n);
  for (std::size_t i = 0; i < n; ++i) {
    w[i] = x[from + i] * (0.5f - 0.5f * std::cos(kPi2 * static_cast<float>(i) / static_cast<float>(n)));
  }
  float e = 0.0f;
  for (float f = lo; f <= hi; f += 20.0f) {
    const float c = 2.0f * std::cos(kPi2 * f / kFs);
    float s1 = 0.0f, s2 = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
      const float s = w[i] + c * s1 - s2;
      s2 = s1;
      s1 = s;
    }
    e += s1 * s1 + s2 * s2 - c * s1 * s2;
  }
  return e;
}

// Where the energy sits: the share of 500..1100 Hz (A's two formants) over 150..450 Hz (U's first), in dB. A is high, U is low.
float tilt_db(const std::vector<float>& x, std::size_t from, std::size_t n) {
  const float hi = band(x, from, n, 500.0f, 1100.0f);
  const float lo = band(x, from, n, 150.0f, 450.0f);
  return 10.0f * std::log10((hi + 1e-20f) / (lo + 1e-20f));
}

constexpr std::uint16_t kVMode = Vocoder::Mode;
constexpr std::uint16_t kMix = TractUnit::Mix;

}  // namespace

TEST_CASE("tract mode: modes 0..3 are sample-identical to the tree before TRACT") {
  // Taken in cdff2a4: one mode for 500 blocks each, then a walk TALK, TUNE, ROBOT, BANDS, TUNE through the crossfades.
  const std::uint32_t want[4] = {0x07A03409u, 0x75A4070Bu, 0x8292FDF5u, 0x89FF34D1u};
  for (unsigned m = 0; m < 4; ++m) {
    const std::size_t at[1] = {0};
    const unsigned mm[1] = {m};
    const std::uint32_t d = digest_run(mm, at, 1, 500);
    std::printf("tract mode: digest of mode %u is %08X\n", m, d);
    CHECK(d == want[m]);
  }
  const unsigned wm[5] = {1, 2, 3, 0, 2};
  const std::size_t wa[5] = {100, 200, 300, 400, 450};
  const std::uint32_t d = digest_run(wm, wa, 5, 600);
  std::printf("tract mode: digest of the walk is %08X\n", d);
  CHECK(d == 0xA9A804A5u);
}

TEST_CASE("tract mode: the tables agree: five modes, TRACT is the fifth, the tube's table is eleven ids") {
  static Vocoder vo;
  vo.prepare(kFs);
  static TractUnit tu;
  tu.prepare(kFs);
  CHECK(kModeCount == 5u);
  CHECK(kModeTract == 4u);
  CHECK(std::string(kModeNames[kModeTract]) == "TRACT");
  CHECK(vo.info().params[Vocoder::Mode].max == 4.0f);
  CHECK(Vocoder::kParamCount == 64);  // the kit's limit: the tube's knobs are the new unit's
  CHECK(tu.info().param_count == TractUnit::kParamCount);
  CHECK(TractUnit::kParamCount == 11);
  CHECK(tu.info().kind == dsp::UnitKind::Effect);
  CHECK(tu.info().params[TractUnit::Mix].def == 0.0f);
  CHECK(tu.info().params[TractUnit::Glide].def == 30.0f);
  CHECK(tu.info().params[TractUnit::Slide].def == 30.0f);
}

TEST_CASE("tract mode: PERFORM has a page for TRACT: it keeps the first page's F8 until V2 gives it VOWEL") {
  namespace pl = o3_voc03er::perform;
  CHECK(pl::mode_page(kModeTract) == pl::mode_page(0u));
  CHECK(pl::mode_page(3u) == pl::mode_page(0u));  // ROBOT
  for (unsigned m = 0; m < kModeCount; ++m) CHECK(pl::mode_page(m) < pl::kModePages);
}

TEST_CASE("tract mode: the carrier speaks through the tube: A and U differ in where their energy sits") {
  const std::size_t frames = 30000, from = frames - 16384;
  const std::vector<Ev> a = {{0, false, kVMode, 4.0f}, {0, true, kMix, 1.0f}, {0, true, TractUnit::Vowel, 0.0f}};
  const std::vector<Ev> u = {{0, false, kVMode, 4.0f}, {0, true, kMix, 1.0f}, {0, true, TractUnit::Vowel, 1.0f}};
  const auto xa = render(frames, 64, a, In::Voice);
  const auto xu = render(frames, 64, u, In::Voice);
  const float ta = tilt_db(xa, from, 16384), tu_ = tilt_db(xu, from, 16384);
  std::printf("tract mode: tilt A %.1f dB, U %.1f dB; peaks %.3f %.3f\n", static_cast<double>(ta), static_cast<double>(tu_),
              static_cast<double>(peak(xa, from, frames)), static_cast<double>(peak(xu, from, frames)));
  CHECK(peak(xa, from, frames) > 0.01f);
  CHECK(peak(xu, from, frames) > 0.01f);
  CHECK(ta - tu_ > 6.0f);
  // With MIX 0 the same chain is the bare carrier: the tube is what changed the sound, and the bare carrier does not depend on the vowel.
  const std::vector<Ev> bare = {{0, false, kVMode, 4.0f}, {0, true, TractUnit::Vowel, 1.0f}};
  const auto xb = render(frames, 64, bare, In::Voice);
  CHECK(std::fabs(tilt_db(xb, from, 16384) - tu_) > 3.0f);
}

TEST_CASE("tract mode: Bands, Talk and Tune idle: the modulator does not reach the output, the carrier does") {
  const std::vector<Ev> on = {{0, false, kVMode, 4.0f}, {0, true, kMix, 1.0f}};
  const auto v = render(12000, 64, on, In::Voice);
  const auto s = render(12000, 64, on, In::Silence);
  // Bit for bit: the input is not read. (From the end of the 10 ms crossfade out of BANDS, the default mode, which does read it.)
  CHECK(std::equal(v.begin() + 2000, v.end(), s.begin() + 2000));
  CHECK(peak(v, 6000, 12000) > 0.01f);   // and the carrier plays
  const auto none = render(12000, 64, on, In::Voice, false);
  CHECK(peak(none, 6000, 12000) < 1.0e-4f);  // no note, no sound (the carrier is gated by the notes)
}

TEST_CASE("tract mode: any carrier source speaks through the tube: NOISE and LIVE (the bus after CarSrc)") {
  const std::size_t frames = 30000, from = frames - 16384;
  const struct {
    float src;
    In in;
    const char* name;
  } cases[2] = {{2.0f, In::Silence, "NOISE"}, {7.0f, In::Noise, "LIVE"}};
  for (const auto& c : cases) {
    const std::vector<Ev> a = {{0, false, kVMode, 4.0f}, {0, false, Vocoder::CarSrc, c.src}, {0, true, kMix, 1.0f}, {0, true, TractUnit::Vowel, 0.0f}};
    const std::vector<Ev> u = {{0, false, kVMode, 4.0f}, {0, false, Vocoder::CarSrc, c.src}, {0, true, kMix, 1.0f}, {0, true, TractUnit::Vowel, 1.0f}};
    const auto xa = render(frames, 64, a, c.in);
    const auto xu = render(frames, 64, u, c.in);
    const float ta = tilt_db(xa, from, 16384), tu_ = tilt_db(xu, from, 16384);
    std::printf("tract mode: CarSrc %s tilt A %.1f dB, U %.1f dB, peak %.3f\n", c.name, static_cast<double>(ta), static_cast<double>(tu_),
                static_cast<double>(peak(xa, from, frames)));
    CHECK(peak(xa, from, frames) > 0.005f);
    CHECK(ta - tu_ > 4.0f);
  }
}

TEST_CASE("tract mode: a mode change, to TRACT and back, makes no step over a bound") {
  // What the app writes: Mode and the tube's MIX together, one block.
  const std::size_t to = 400 * 64, back = 900 * 64;
  const std::vector<Ev> sw = {{to, false, kVMode, 4.0f}, {to, true, kMix, 1.0f}, {back, false, kVMode, 0.0f}, {back, true, kMix, 0.0f}};
  const auto x = render(1400 * 64, 64, sw, In::Voice);
  const float steady_bands = max_step(x, 300 * 64, 390 * 64);
  const float steady_tract = max_step(x, 800 * 64, 890 * 64);
  // The bare carrier is what the 12 ms hold lets through before the tube fades in: its steps count too.
  const std::vector<Ev> bare = {{0, false, kVMode, 4.0f}};
  const auto xb = render(30000, 64, bare, In::Voice);
  const float steady_bare = max_step(xb, 20000, 30000);
  const float bound = 1.25f * std::max(steady_bands, std::max(steady_tract, steady_bare)) + 0.01f;
  const float over_to = max_step(x, to - 64, to + 64 * 120);
  const float over_back = max_step(x, back - 64, back + 64 * 120);
  std::printf("tract mode: steps bands %.4f tract %.4f bare %.4f; across the change %.4f, back %.4f (bound %.4f)\n", static_cast<double>(steady_bands),
              static_cast<double>(steady_tract), static_cast<double>(steady_bare), static_cast<double>(over_to), static_cast<double>(over_back),
              static_cast<double>(bound));
  CHECK(over_to <= bound);
  CHECK(over_back <= bound);
  CHECK(peak(x, to + 64 * 120, back) > 0.01f);  // TRACT sounds after the change
}

TEST_CASE("tract mode: the chain does not depend on the block size") {
  const std::vector<Ev> on = {{0, false, kVMode, 4.0f}, {0, true, kMix, 1.0f}, {0, true, TractUnit::Vowel, 0.4f}};
  const auto ref = render(8192, 64, on, In::Voice);
  for (const std::size_t block : {29u, 7u, 1u}) {
    const auto x = render(8192, block, on, In::Voice);
    float worst = 0.0f;
    for (std::size_t i = 0; i < ref.size(); ++i) worst = std::max(worst, std::fabs(x[i] - ref[i]));
    std::printf("tract mode: block %zu differs from block 64 by at most %.3g (peak %.3f)\n", block, static_cast<double>(worst),
                static_cast<double>(peak(ref, 0, ref.size())));
    CHECK(worst < 1.0e-3f);
  }
}

// ---- the TractUnit alone ------------------------------------------------------------------------------------------------------------------

namespace {

// A saw at 130 Hz, amplitude 0.3, continuing across calls through `ph`.
void saw(float* x, std::size_t n, float& ph) {
  for (std::size_t i = 0; i < n; ++i) {
    ph += 130.0f / kFs;
    ph -= std::floor(ph);
    x[i] = 0.3f * (2.0f * ph - 1.0f);
  }
}

struct Tube {
  TractUnit u;
  float ph = 0.0f;
  Tube() { u.prepare(kFs); }
  // One block of `n` (<= 64) frames: the input is a saw or silence; returns the left plane in `out`.
  void block(float* out, std::size_t n, bool sound, const dsp::Event* ev = nullptr, std::uint32_t nev = 0) {
    float in[64], r[64];
    if (sound) {
      saw(in, n, ph);
    } else {
      std::fill(in, in + n, 0.0f);
    }
    std::memcpy(out, in, n * sizeof(float));
    std::memcpy(r, in, n * sizeof(float));
    dsp::ProcessArgs a{};
    a.events = ev;
    a.event_count = nev;
    a.in_l = out;
    a.in_r = r;
    a.out_l = out;
    a.out_r = r;
    a.frames = static_cast<std::uint32_t>(n);
    u.process(a);
  }
};

}  // namespace

TEST_CASE("tract unit: MIX 0 is a bit-exact bypass, left and right apart, in place or not") {
  static TractUnit u;
  u.prepare(kFs);
  float l[64], r[64], ol[64], orr[64];
  std::uint32_t s = 5u;
  for (int b = 0; b < 40; ++b) {
    for (int i = 0; i < 64; ++i) {
      s = s * 1664525u + 1013904223u;
      l[i] = static_cast<float>(s >> 8) * (1.0f / 8388608.0f) - 1.0f;
      r[i] = -0.5f * l[i] + 0.1f;
    }
    dsp::ProcessArgs a{};
    a.in_l = l;
    a.in_r = r;
    a.out_l = ol;
    a.out_r = orr;
    a.frames = 64;
    u.process(a);
    CHECK(std::memcmp(l, ol, sizeof l) == 0);
    CHECK(std::memcmp(r, orr, sizeof r) == 0);
    float il[64], ir[64];  // in place
    std::memcpy(il, l, sizeof il);
    std::memcpy(ir, r, sizeof ir);
    dsp::ProcessArgs p{};
    p.in_l = il;
    p.in_r = ir;
    p.out_l = il;
    p.out_r = ir;
    p.frames = 64;
    u.process(p);
    CHECK(std::memcmp(l, il, sizeof l) == 0);
    CHECK(std::memcmp(r, ir, sizeof r) == 0);
  }
  CHECK(u.processed_frames() == 0u);  // and nothing ran
}

TEST_CASE("tract unit: MIX waits out the hold (12 ms of bypass), then the tube speaks, and a drop to 0 comes back to the bypass") {
  Tube t;
  const dsp::Event on = dsp::param_change(TractUnit::Mix, 1.0f);
  float out[64], in[64];
  float ph = 0.0f;
  bool same = true;
  t.block(out, 64, true, &on, 1);
  saw(in, 64, ph);
  same = same && std::memcmp(in, out, sizeof in) == 0;
  CHECK(t.u.holding());
  std::size_t frames = 64;
  while (frames < 480) {  // 7.5 blocks: still inside the 529-frame hold
    t.block(out, 64, true);
    saw(in, 64, ph);
    same = same && std::memcmp(in, out, sizeof in) == 0;
    frames += 64;
  }
  CHECK(same);
  CHECK(t.u.processed_frames() == 0u);
  for (int b = 0; b < 40; ++b) t.block(out, 64, true);
  CHECK_FALSE(t.u.holding());
  CHECK(t.u.processed_frames() > 0u);
  float wet = 0.0f, dry = 0.0f;
  saw(in, 64, ph);
  t.block(out, 64, true);
  saw(in, 64, ph);
  for (int i = 0; i < 64; ++i) {
    wet += std::fabs(out[i]);
    dry += std::fabs(in[i]);
  }
  CHECK(wet > 0.0f);
  CHECK(std::fabs(wet - dry) > 0.05f * dry);  // not the bare input: it went through the tube
  const dsp::Event off = dsp::param_change(TractUnit::Mix, 0.0f);
  t.block(out, 64, true, &off, 1);
  for (int b = 0; b < 20; ++b) t.block(out, 64, true);  // the Processor ramps its mix down in 5 ms; then it is the bypass again
  const std::uint32_t n0 = t.u.processed_frames();
  t.block(out, 64, true);
  saw(in, 64, ph);
  for (int b = 0; b < 3; ++b) {
    t.block(out, 64, true);
    saw(in, 64, ph);
  }
  CHECK(t.u.processed_frames() == n0);
}

TEST_CASE("tract unit: the block size does not change the tube") {
  const dsp::Event on[2] = {dsp::param_change(TractUnit::Mix, 1.0f), dsp::param_change(TractUnit::Vowel, 0.7f)};
  std::vector<float> ref;
  for (const std::size_t size : {64u, 29u, 5u}) {
    Tube t;
    std::vector<float> x;
    float out[64];
    std::size_t done = 0;
    bool first = true;
    while (done < 12000) {
      const std::size_t n = std::min<std::size_t>(size, 12000 - done);
      t.block(out, n, true, first ? on : nullptr, first ? 2u : 0u);
      first = false;
      x.insert(x.end(), out, out + n);
      done += n;
    }
    if (ref.empty()) {
      ref = x;
      continue;
    }
    float worst = 0.0f;
    for (std::size_t i = 0; i < ref.size(); ++i) worst = std::max(worst, std::fabs(x[i] - ref[i]));
    std::printf("tract unit: block %zu differs from 64 by %.3g\n", size, static_cast<double>(worst));
    CHECK(worst < 1.0e-5f);
  }
}

TEST_CASE("tract unit: the sleep: silence costs nothing, the first block after waking has no step over a bound") {
  Tube t;
  const dsp::Event on = dsp::param_change(TractUnit::Mix, 1.0f);
  float out[64];
  std::vector<float> run;
  t.block(out, 64, true, &on, 1);
  for (int b = 0; b < 200; ++b) {
    t.block(out, 64, true);
    run.insert(run.end(), out, out + 64);
  }
  CHECK_FALSE(t.u.asleep());
  const float steady = max_step(run, 6400, run.size());
  const float level = peak(run, 6400, run.size());
  CHECK(level > 0.01f);
  // Silence: the tube rings down, then sleeps.
  int blocks = 0;
  while (!t.u.asleep() && blocks < 2000) {
    t.block(out, 64, false);
    ++blocks;
  }
  std::printf("tract unit: asleep after %d silent blocks (%.0f ms)\n", blocks, static_cast<double>(blocks) * 64.0 * 1000.0 / static_cast<double>(kFs));
  REQUIRE(t.u.asleep());
  CHECK(blocks < 1000);
  const std::uint32_t ran = t.u.processed_frames();
  bool zeros = true;
  for (int b = 0; b < 500; ++b) {
    t.block(out, 64, false);
    for (float v : out) zeros = zeros && v == 0.0f;
  }
  CHECK(zeros);
  CHECK(t.u.processed_frames() == ran);  // a second of idle: not one frame of the Processor
  // Wake.
  std::vector<float> w;
  for (int b = 0; b < 40; ++b) {
    t.block(out, 64, true);
    w.insert(w.end(), out, out + 64);
  }
  CHECK_FALSE(t.u.asleep());
  CHECK(t.u.processed_frames() > ran);
  const float first_step = std::max(std::fabs(w[0]), max_step(w, 0, 64));
  const float wake_step = max_step(w, 0, w.size());
  std::printf("tract unit: steady step %.4f, level %.4f; first block step %.4f, wake step %.4f\n", static_cast<double>(steady),
              static_cast<double>(level), static_cast<double>(first_step), static_cast<double>(wake_step));
  CHECK(first_step <= 1.25f * steady + 0.01f);
  CHECK(wake_step <= 1.25f * steady + 0.01f);
  CHECK(peak(w, 0, w.size()) <= 1.5f * level);
  for (float v : w) REQUIRE(std::isfinite(v));
}

// ---- the app: apply_mode and the console -------------------------------------------------------------------------------------------------

namespace {

struct Ids {
  dsp::UnitId voc = dsp::kNoUnit;
  dsp::UnitId tube = dsp::kNoUnit;
};

Ids find_units(sim::Harness& h) {
  Ids ids;
  for (unsigned u = 0; u < h.engine().unit_count(); ++u) {
    const auto* info = h.engine().unit_info(static_cast<dsp::UnitId>(u));
    if (info != nullptr && std::string(info->name) == "VOCODER") ids.voc = static_cast<dsp::UnitId>(u);
    if (info != nullptr && std::string(info->name) == "TRACT") ids.tube = static_cast<dsp::UnitId>(u);
  }
  return ids;
}

// The console's command `name` with up to two words; the text it prints.
std::string console(const char* name, const char* a1 = nullptr, const char* a2 = nullptr) {
  const app::DiagCommand* rows = nullptr;
  const std::size_t n = o3sdk_app().diag_commands(rows);
  std::string text;
  for (std::size_t i = 0; i < n; ++i) {
    if (std::string(rows[i].name) != name) continue;
    const char* argv[3] = {name, a1, a2};
    const app::DiagArgs args{a1 == nullptr ? 1u : (a2 == nullptr ? 2u : 3u), argv};
    app::DiagOut out(app::DiagSink{[](void* c, const char* d, std::size_t k) { static_cast<std::string*>(c)->append(d, k); }, &text});
    rows[i].run(args, out);
    return text;
  }
  text = "(no such command)";
  return text;
}

}  // namespace

TEST_CASE("tract mode: the app keeps the tube's MIX with the Mode, whoever writes the Mode") {
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, nullptr, nullptr, nullptr);
  REQUIRE(h->start());
  h->run_ms(200);
  const Ids id = find_units(*h);
  REQUIRE(id.voc != dsp::kNoUnit);
  REQUIRE(id.tube != dsp::kNoUnit);
  CHECK(h->engine().param(id.tube, TractUnit::Mix) == 0.0f);

  // The console.
  const std::string said = console("voc", "tract");
  CHECK(said.find("tract") != std::string::npos);
  h->run_ms(100);
  CHECK(h->engine().param(id.voc, Vocoder::Mode) == 4.0f);
  CHECK(h->engine().param(id.tube, TractUnit::Mix) == 1.0f);
  (void)console("voc", "bands");
  h->run_ms(100);
  CHECK(h->engine().param(id.voc, Vocoder::Mode) == 0.0f);
  CHECK(h->engine().param(id.tube, TractUnit::Mix) == 0.0f);

  // Any other writer: the engine's parameter itself (PARAMS, a project, a preset all end here).
  REQUIRE(h->engine().set_param(id.voc, Vocoder::Mode, 4.0f));
  h->run_ms(100);
  CHECK(h->engine().param(id.tube, TractUnit::Mix) == 1.0f);
  REQUIRE(h->engine().set_param(id.voc, Vocoder::Mode, 1.0f));
  h->run_ms(100);
  CHECK(h->engine().param(id.tube, TractUnit::Mix) == 0.0f);
  CHECK(h->runtime().frame_failures() == 0u);
}

TEST_CASE("tract mode: the console's tract command sets the ids, by name, and refuses what is out of range") {
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, nullptr, nullptr, nullptr);
  REQUIRE(h->start());
  h->run_ms(200);
  const Ids id = find_units(*h);
  REQUIRE(id.tube != dsp::kNoUnit);
  const auto get = [&](TractUnit::Param p) { return h->engine().param(id.tube, p); };
  (void)console("tract", "vowel", "0.5");
  CHECK(get(TractUnit::Vowel) == doctest::Approx(0.5f));
  (void)console("tract", "tongue", "-0.25");
  CHECK(get(TractUnit::Tongue) == doctest::Approx(-0.25f));
  (void)console("tract", "jaw", "1");
  CHECK(get(TractUnit::Jaw) == doctest::Approx(1.0f));
  (void)console("tract", "lips", "-1");
  CHECK(get(TractUnit::Lips) == doctest::Approx(-1.0f));
  (void)console("tract", "close", "tip");
  CHECK(get(TractUnit::Close) == 2.0f);
  (void)console("tract", "close", "open");
  CHECK(get(TractUnit::Close) == 0.0f);
  (void)console("tract", "size", "tiny");
  CHECK(get(TractUnit::Size) == 4.0f);
  (void)console("tract", "size", "2");
  CHECK(get(TractUnit::Size) == 2.0f);
  (void)console("tract", "glide", "120");
  CHECK(get(TractUnit::Glide) == 120.0f);
  (void)console("tract", "res", "0.8");
  CHECK(get(TractUnit::Res) == doctest::Approx(0.8f));
  (void)console("tract", "hiss", "0");
  CHECK(get(TractUnit::Hiss) == 0.0f);
  (void)console("tract", "slide", "200");
  CHECK(get(TractUnit::Slide) == 200.0f);
  (void)console("tract", "mix", "1");
  CHECK(get(TractUnit::Mix) == 1.0f);
  // Out of range, an unknown name, a word that is no number: usage, nothing written.
  const std::string bad = console("tract", "vowel", "2");
  CHECK(bad.find("usage") != std::string::npos);
  CHECK(get(TractUnit::Vowel) == doctest::Approx(0.5f));
  CHECK(console("tract", "bogus", "1").find("usage") != std::string::npos);
  CHECK(console("tract", "glide", "abc").find("usage") != std::string::npos);
  CHECK(console("tract", "glide", "4").find("usage") != std::string::npos);
  CHECK(get(TractUnit::Glide) == 120.0f);
  CHECK(console("tract").find("usage") != std::string::npos);
}
