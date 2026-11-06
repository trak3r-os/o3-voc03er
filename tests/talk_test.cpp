// SPDX-License-Identifier: GPL-3.0-or-later
// Talk (the LPC talkbox) and the Vocoder's MODE switch, against synthetic signals only: saw voices through formant resonators.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/robot.h>
#include <o3_voc03er/units/vocoder/talk.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

namespace {

constexpr float kFs = 44100.0f;
constexpr float kPi2 = 6.2831853f;

struct Formant {
  float hz, bw, gain;
};

// A saw at f0 through up to three two-pole resonators in parallel: a vowel. The formants may change between calls.
class Voice {
 public:
  explicit Voice(float f0) : f0_(f0) {}
  void set_vowel(const Formant* f, unsigned n) {
    n_ = n;
    for (unsigned k = 0; k < n; ++k) {
      const float r = std::exp(-3.1415927f * f[k].bw / kFs);
      c1_[k] = 2.0f * r * std::cos(kPi2 * f[k].hz / kFs);
      c2_[k] = -r * r;
      g_[k] = f[k].gain * (1.0f - r);
    }
  }
  float next() {
    phase_ += f0_ / kFs;
    phase_ -= phase_ >= 1.0f ? 1.0f : 0.0f;
    const float x = 2.0f * phase_ - 1.0f;
    float y = 0.0f;
    for (unsigned k = 0; k < n_; ++k) {
      const float s = g_[k] * x + c1_[k] * y1_[k] + c2_[k] * y2_[k];
      y2_[k] = y1_[k];
      y1_[k] = s;
      y += s;
    }
    return y;
  }

 private:
  float f0_;
  unsigned n_ = 0;
  float c1_[3] = {}, c2_[3] = {}, g_[3] = {}, y1_[3] = {}, y2_[3] = {};
  float phase_ = 0.0f;
};

class Saw {
 public:
  explicit Saw(float f0) : f0_(f0) {}
  float next() {
    phase_ += f0_ / kFs;
    phase_ -= phase_ >= 1.0f ? 1.0f : 0.0f;
    return 0.5f * (2.0f * phase_ - 1.0f);
  }

 private:
  float f0_, phase_ = 0.0f;
};

const Formant kA[] = {{500.0f, 80.0f, 6.0f}, {1800.0f, 120.0f, 25.0f}};
const Formant kB[] = {{300.0f, 60.0f, 6.0f}, {2600.0f, 150.0f, 60.0f}};

// Goertzel power at `hz` over [from, to) of x, per sample squared.
float power_at(const std::vector<float>& x, std::size_t from, std::size_t to, float hz) {
  const float w = kPi2 * hz / kFs;
  const float c = 2.0f * std::cos(w);
  float s1 = 0.0f, s2 = 0.0f;
  for (std::size_t i = from; i < to; ++i) {
    const float s = x[i] + c * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  const float n = static_cast<float>(to - from);
  return (s1 * s1 + s2 * s2 - c * s1 * s2) / (n * n);
}

// Feeds a Talk block by block; mod and car come from generators; the block hook runs before each block.
template <class M, class C, class H>
std::vector<float> run_talk(Talk& t, M mod, C car, std::size_t blocks, H hook) {
  std::vector<float> out(blocks * 64);
  for (std::size_t b = 0; b < blocks; ++b) {
    hook(b);
    float m[64], c[64];
    for (int i = 0; i < 64; ++i) {
      m[i] = mod();
      c[i] = car();
    }
    t.process(m, c, out.data() + b * 64, 64);
  }
  return out;
}

// The shaping gain at hz: output power over the plain carrier's power (the saw's own tilt cancels).
float shape_gain(const std::vector<float>& out, std::size_t from, std::size_t to, float hz) {
  Saw cs(100.0f);
  std::vector<float> cv(out.size());
  for (auto& x : cv) x = cs.next();
  return power_at(out, from, to, hz) / power_at(cv, from, to, hz);
}

bool finite(const std::vector<float>& v) {
  for (float x : v) {
    if (!std::isfinite(x)) return false;
  }
  return true;
}

float peak(const std::vector<float>& v, std::size_t from = 0) {
  float p = 0.0f;
  for (std::size_t i = from; i < v.size(); ++i) p = std::max(p, std::fabs(v[i]));
  return p;
}

Talk* make_talk() {
  static Talk t;  // 14 KiB: not on the stack
  t.prepare(kFs);
  return &t;
}

}  // namespace

TEST_CASE("talk: the carrier is not delayed, an onset shows in the same block") {
  Talk& t = *make_talk();
  Voice v(110.0f);
  v.set_vowel(kA, 2);
  Saw saw(100.0f);
  bool on = false;
  const std::size_t onset = 120;
  std::vector<float> out(160 * 64);
  for (std::size_t b = 0; b < 160; ++b) {
    float m[64], c[64];
    for (int i = 0; i < 64; ++i) {
      m[i] = v.next();
      c[i] = (b >= onset) ? saw.next() : 0.0f;
    }
    on = b >= onset;
    t.process(m, c, out.data() + b * 64, 64);
  }
  CHECK(on);
  float before = 0.0f, first8 = 0.0f;
  for (int i = 0; i < 64; ++i) before = std::max(before, std::fabs(out[(onset - 1) * 64 + static_cast<std::size_t>(i)]));
  for (int i = 0; i < 8; ++i) first8 = std::max(first8, std::fabs(out[onset * 64 + static_cast<std::size_t>(i)]));
  CHECK(before == 0.0f);
  CHECK(first8 > 1.0e-4f);
}

TEST_CASE("talk: no block carries a spike, at every order") {
  for (std::uint32_t order : {8u, 16u, 24u}) {
    Talk& t = *make_talk();
    t.set_order(order);
    Voice v(110.0f);
    v.set_vowel(kA, 2);
    Saw saw(100.0f);
    std::vector<std::uint32_t> macs;
    float m[64], c[64], o[64];
    for (int b = 0; b < 36 * 2; ++b) {
      for (int i = 0; i < 64; ++i) {
        m[i] = v.next();
        c[i] = saw.next();
      }
      t.process(m, c, o, 64);
      macs.push_back(t.macs_last_block());
    }
    // Hops after the window has filled (the first 256 low-rate samples, 16 blocks), each aligned: a hop is 2 blocks of 64 frames.
    for (std::size_t h = 12; h < 36; ++h) {
      const std::uint32_t mx = std::max(macs[h * 2], macs[h * 2 + 1]);
      CHECK(static_cast<double>(mx) <= 1.5 * 0.5 * (macs[h * 2] + macs[h * 2 + 1]));
    }
  }
}

TEST_CASE("talk: a vowel modulator puts the carrier's peaks at the vowel's formants") {
  Talk& t = *make_talk();
  Voice v(110.0f);
  v.set_vowel(kA, 2);
  Saw saw(100.0f);  // a harmonic every 100 Hz: the probes sit on harmonics
  auto out = run_talk(t, [&] { return v.next(); }, [&] { return saw.next(); }, 400, [](std::size_t) {});
  const std::size_t from = 200 * 64, to = out.size();
  const auto g = [&](const std::vector<float>& o, float hz) { return shape_gain(o, from, to, hz); };
  const float f1 = g(out, 500.0f), f2 = g(out, 1800.0f);
  const float valley = g(out, 1000.0f);  // between the formants
  const float high = g(out, 4000.0f);    // above both
  // 6 dB (a factor 4 in power) over the valley and over the far band.
  CHECK(f1 > 4.0f * valley);
  CHECK(f2 > 4.0f * valley);
  CHECK(f1 > 4.0f * high);
  CHECK(f2 > 4.0f * high);
  // The other vowel moves them: 300 Hz and 2600 Hz win over 500 and 1800 (which sit between them).
  Talk& t2 = *make_talk();
  Voice w(110.0f);
  w.set_vowel(kB, 2);
  Saw saw2(100.0f);
  auto out2 = run_talk(t2, [&] { return w.next(); }, [&] { return saw2.next(); }, 400, [](std::size_t) {});
  CHECK(g(out2, 300.0f) > 4.0f * g(out2, 800.0f));
  CHECK(g(out2, 2600.0f) > 4.0f * g(out2, 1400.0f));
}

TEST_CASE("talk: a 90 Hz vowel at order 24 for 5 s stays bounded, |k| <= 0.995, no NaN") {
  Talk& t = *make_talk();
  t.set_order(24);
  Voice v(90.0f);
  const Formant f[] = {{600.0f, 50.0f, 6.0f}, {1200.0f, 60.0f, 4.0f}, {2500.0f, 100.0f, 2.0f}};
  v.set_vowel(f, 3);
  Saw saw(90.0f);
  float kmax = 0.0f;
  auto out = run_talk(t, [&] { return v.next(); }, [&] { return saw.next(); }, 5 * 44100 / 64, [&](std::size_t) {
    for (std::uint32_t m = 1; m <= Talk::kMaxStages; ++m) {
      kmax = std::max(kmax, std::fabs(t.reflection(m)));
      kmax = std::max(kmax, std::fabs(t.reflection_target(m)));
    }
  });
  CHECK(finite(out));
  CHECK(kmax <= 0.995f);
  CHECK(kmax > 0.5f);        // the analysis did run and found resonances
  CHECK(peak(out) < 20.0f);  // the carrier is 0.5 peak; a 26 dB rise through the resonances would already be a fault
  CHECK(peak(out) > 0.01f);
}

TEST_CASE("talk: silence in the modulator gives silence, no NaN, no denormals") {
  Talk& t = *make_talk();
  Voice v(110.0f);
  v.set_vowel(kA, 2);
  Saw saw(100.0f);
  std::size_t sample = 0;
  auto out = run_talk(t, [&] { return (sample++ < 44100u) ? v.next() : 0.0f; }, [&] { return saw.next(); }, 3 * 44100 / 64,
                      [](std::size_t) {});
  CHECK(finite(out));
  CHECK(peak(out, 44100) > 0.0f);  // it sounded
  const std::size_t from = 44100 + 2 * 44100 - 128 * 64;  // the last blocks, 2 s after the voice stopped
  for (std::size_t i = from; i < out.size(); ++i) {
    CHECK(out[i] == 0.0f);
  }
  for (std::size_t i = 0; i < out.size(); ++i) CHECK(std::fpclassify(out[i]) != FP_SUBNORMAL);
  // from the start
  Talk& t2 = *make_talk();
  std::vector<float> z(64, 0.0f), o(64);
  for (int b = 0; b < 200; ++b) t2.process(z.data(), z.data(), o.data(), 64);
  CHECK(peak(o) == 0.0f);
}

TEST_CASE("talk: FREEZE holds k while the modulator changes") {
  Talk& t = *make_talk();
  Voice v(110.0f);
  v.set_vowel(kA, 2);
  Saw saw(100.0f);
  run_talk(t, [&] { return v.next(); }, [&] { return saw.next(); }, 200, [](std::size_t) {});
  float held[Talk::kMaxStages + 1];
  for (std::uint32_t m = 1; m <= 12; ++m) held[m] = t.reflection_target(m);
  CHECK(std::fabs(held[1]) > 0.1f);
  t.set_freeze(true);
  v.set_vowel(kB, 2);
  auto out = run_talk(t, [&] { return v.next(); }, [&] { return saw.next(); }, 200, [](std::size_t) {});
  for (std::uint32_t m = 1; m <= 12; ++m) {
    CHECK(t.reflection_target(m) == held[m]);
    CHECK(t.reflection(m) == held[m]);
  }
  CHECK(peak(out, 100 * 64) > 0.01f);  // still sounding
  t.set_freeze(false);
  run_talk(t, [&] { return v.next(); }, [&] { return saw.next(); }, 200, [](std::size_t) {});
  float moved = 0.0f;
  for (std::uint32_t m = 1; m <= 12; ++m) moved = std::max(moved, std::fabs(t.reflection_target(m) - held[m]));
  CHECK(moved > 0.05f);
}

TEST_CASE("talk: an order change at any moment is safe, every order 8..24 runs") {
  Talk& t = *make_talk();
  Voice v(110.0f);
  v.set_vowel(kA, 2);
  Saw saw(100.0f);
  std::uint32_t o = 8;
  auto out = run_talk(t, [&] { return v.next(); }, [&] { return saw.next(); }, 1500, [&](std::size_t b) {
    if (b % 7 == 3) {  // not aligned with the hop
      o = o >= 24 ? 8 : o + 1;
      t.set_order(o);
    }
  });
  CHECK(finite(out));
  CHECK(peak(out) < 20.0f);
  CHECK(peak(out, 600 * 64) > 0.01f);
  for (std::uint32_t order = 8; order <= 24; ++order) {
    Talk& u = *make_talk();
    u.set_order(order);
    Voice w(110.0f);
    w.set_vowel(kA, 2);
    Saw s2(100.0f);
    auto r = run_talk(u, [&] { return w.next(); }, [&] { return s2.next(); }, 300, [](std::size_t) {});
    CHECK(finite(r));
    CHECK(peak(r, 150 * 64) > 0.01f);
    CHECK(peak(r) < 20.0f);
    CHECK(u.order() == order);
  }
}

namespace {

// The Vocoder with a vowel modulator and two held notes; `hook(block, events)` may add events.
std::vector<float> run_unit(Vocoder& vo, std::size_t blocks, std::size_t switch_a, std::size_t switch_b, float level = 0.3f) {
  Voice v(110.0f);
  v.set_vowel(kA, 2);
  std::vector<float> out(blocks * 64);
  dsp::Transport tr;
  for (std::size_t b = 0; b < blocks; ++b) {
    float m[64], r[64];
    for (float& x : m) x = level * v.next();
    dsp::Event ev[3];
    std::uint32_t n = 0;
    if (b == 0) {
      ev[n++] = dsp::note_on(48, 100);
      ev[n++] = dsp::note_on(55, 100);
    }
    if (b == switch_a) ev[n++] = dsp::param_change(Vocoder::Mode, 1.0f);
    if (b == switch_b) ev[n++] = dsp::param_change(Vocoder::Mode, 0.0f);
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

float max_step(const std::vector<float>& x, std::size_t from, std::size_t to) {
  float s = 0.0f;
  for (std::size_t i = from + 1; i < to; ++i) s = std::max(s, std::fabs(x[i] - x[i - 1]));
  return s;
}

}  // namespace

TEST_CASE("vocoder: MODE switches Bands and Talk during sound with no click, and Talk sounds") {
  static Vocoder vo;
  vo.prepare(kFs);
  CHECK(Vocoder::kParamCount == 62);
  const std::size_t sw_a = 700, sw_b = 1100;
  const auto out = run_unit(vo, 1500, sw_a, sw_b);
  CHECK(finite(out));
  CHECK(peak(out) < 1.0f);
  const std::size_t a0 = sw_a * 64, b0 = sw_b * 64;
  CHECK(peak(out, a0 + 20 * 64) > 0.01f);  // Talk sounds
  // The click threshold: the largest sample step the two modes make by themselves (Bands before the first switch, Talk before the
  // second), 1.5 times. The crossfade is 10 ms, so a sum of the two signals changes by no more than their own steps plus the
  // mix slope times the difference of the signals (<= 2 x peak / 441), which this bound leaves room for.
  const float bands_step = max_step(out, 300 * 64, a0);
  const float talk_step = max_step(out, a0 + 20 * 64, b0);
  const float limit = 1.5f * std::max(bands_step, talk_step);
  CHECK(bands_step > 0.0f);
  CHECK(max_step(out, a0 - 64, a0 + 40 * 64) <= limit);
  CHECK(max_step(out, b0 - 64, b0 + 40 * 64) <= limit);
  // The way back keeps the AGC: a full reset to unity gain returned at 0.97 against a steady 0.60, a bump no step limit sees as such.
  float back = 0.0f;
  for (std::size_t i = b0; i < b0 + 40 * 64; ++i) back = std::max(back, std::fabs(out[i]));
  CHECK(back <= 1.1f * peak(out, b0 + 300 * 64));
}

TEST_CASE("vocoder: QUALITY and FREEZE are parameters 8 and 9, the defaults are BANDS, 16, off") {
  static Vocoder vo;
  vo.prepare(kFs);
  const auto& info = vo.info();
  CHECK(info.params[Vocoder::Mode].def == 0.0f);
  CHECK(info.params[Vocoder::Quality].def == 16.0f);
  CHECK(info.params[Vocoder::Quality].min == 8.0f);
  CHECK(info.params[Vocoder::Quality].max == 24.0f);
  CHECK(info.params[Vocoder::Freeze].def == 0.0f);
  CHECK(Vocoder::Mode == 7);
  CHECK(Vocoder::Quality == 8);
  CHECK(Vocoder::Freeze == 9);
  CHECK(vo.talk().order() == 16);
}

TEST_CASE("talk: a pure tone at full scale, the near-singular case, stays finite at order 24") {
  Talk* t = make_talk();
  t->set_order(24);
  float mod[64];
  float car[64];
  float out[64];
  float phase = 0.0f;
  float carrier = -1.0f;
  bool all_finite = true;
  for (unsigned block = 0; block < 3500; ++block) {  // 5 s
    for (unsigned i = 0; i < 64; ++i) {
      mod[i] = std::sin(6.2831853f * phase);
      phase += 1000.0f / 44100.0f;
      phase -= phase >= 1.0f ? 1.0f : 0.0f;
      car[i] = carrier;
      carrier = carrier >= 1.0f ? -1.0f : carrier + 0.02f;
    }
    t->process(mod, car, out, 64);
    for (float v : out) {
      all_finite = all_finite && std::isfinite(v);
    }
    for (std::uint32_t m = 1; m <= Talk::kMaxStages; ++m) {
      all_finite = all_finite && std::fabs(t->reflection(m)) <= Talk::kMaxK;  // false for a NaN too
    }
  }
  CHECK(all_finite);
}

TEST_CASE("vocoder: back from TALK, the bands hold nothing of the voice before it") {
  static Vocoder v;
  v.prepare(44100.0f);
  float in[64];
  float l[64];
  float r[64];
  dsp::Transport tr;
  const auto block = [&](float amp, const dsp::Event* ev, std::uint32_t count) {
    for (unsigned i = 0; i < 64; ++i) {
      in[i] = amp * std::sin(6.2831853f * 1000.0f * static_cast<float>(i) / 44100.0f);
    }
    dsp::ProcessArgs a{};
    a.events = ev;
    a.event_count = count;
    a.in_l = in;
    a.in_r = in;
    a.out_l = l;
    a.out_r = r;
    a.frames = 64;
    a.transport = &tr;
    v.process(a);
  };
  for (unsigned i = 0; i < 200; ++i) {
    block(0.5f, nullptr, 0);  // BANDS, a loud voice
  }
  float held = 0.0f;
  for (unsigned b = 0; b < Bands::kBands; ++b) {
    held += v.bands().envelope(b);
  }
  REQUIRE(held > 0.01f);
  const dsp::Event to_talk = dsp::param_change(Vocoder::Mode, 1.0f);
  block(0.0f, &to_talk, 1);
  for (unsigned i = 0; i < 100; ++i) {
    block(0.0f, nullptr, 0);  // TALK, silent: Bands do not run, their followers stand still
  }
  const dsp::Event to_bands = dsp::param_change(Vocoder::Mode, 0.0f);
  block(0.0f, &to_bands, 1);
  float left = 0.0f;
  for (unsigned b = 0; b < Bands::kBands; ++b) {
    left += v.bands().envelope(b);
  }
  CHECK(left < 0.01f * held);
}

// ---- fs/4 design: formants, any frame count, breath, bad input, quality ----

namespace {

// A pulse train at f0 through two known resonators in cascade: an all-pole vowel with formants at f1 and f2.
class Buzz {
 public:
  Buzz(float f0, float f1, float bw1, float f2, float bw2) : period_(kFs / f0) {
    const float r1 = std::exp(-3.1415927f * bw1 / kFs), r2 = std::exp(-3.1415927f * bw2 / kFs);
    c1_[0] = 2.0f * r1 * std::cos(kPi2 * f1 / kFs);
    c1_[1] = -r1 * r1;
    c2_[0] = 2.0f * r2 * std::cos(kPi2 * f2 / kFs);
    c2_[1] = -r2 * r2;
    g_ = (1.0f - r1) * (1.0f - r2) * 8.0f;
  }
  float next() {
    float x = 0.0f;
    if (t_ >= period_) {
      t_ -= period_;
      x = g_;
    }
    t_ += 1.0f;
    const float a = x + c1_[0] * a1_ + c1_[1] * a2_;
    a2_ = a1_;
    a1_ = a;
    const float b = a + c2_[0] * b1_ + c2_[1] * b2_;
    b2_ = b1_;
    b1_ = b;
    return b;
  }

 private:
  float period_, t_ = 0.0f, g_;
  float c1_[2], c2_[2];
  float a1_ = 0.0f, a2_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f;
};

// The lattice's all-pole envelope 1/|A| at `hz` (the lattice runs at fs/4), from the reflection targets of stages 1..p.
float lattice_env(const Talk& t, std::uint32_t p, float hz) {
  float a[Talk::kMaxStages + 1] = {1.0f}, tmp[Talk::kMaxStages + 1];
  for (std::uint32_t m = 1; m <= p; ++m) {
    const float k = t.reflection_target(m);
    for (std::uint32_t j = 1; j < m; ++j) tmp[j] = a[j] + k * a[m - j];
    for (std::uint32_t j = 1; j < m; ++j) a[j] = tmp[j];
    a[m] = k;
  }
  const float w = kPi2 * hz / (kFs * 0.25f);
  float re = 1.0f, im = 0.0f;
  for (std::uint32_t j = 1; j <= p; ++j) {
    re += a[j] * std::cos(w * static_cast<float>(j));
    im -= a[j] * std::sin(w * static_cast<float>(j));
  }
  return 1.0f / std::sqrt(re * re + im * im);
}

// Power of x[from, to) above `lo` Hz: the sum of Goertzel powers on a grid of `lo` + 250 Hz steps up to 10 kHz.
float power_above(const std::vector<float>& x, std::size_t from, std::size_t to, float lo) {
  float p = 0.0f;
  for (float hz = lo; hz <= 10000.0f; hz += 250.0f) p += power_at(x, from, to, hz);
  return p;
}

// Noise generator (xorshift), -0.3..0.3
struct Noise {
  std::uint32_t s = 12345u;
  float next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (static_cast<float>(s & 0xFFFFu) * (1.0f / 32768.0f) - 1.0f) * 0.3f;
  }
};

}  // namespace

TEST_CASE("talk: the AGC keeps the level of a voice 12 dB down, and leaves a noise floor alone") {
  const auto level = [](float gain) {
    static Talk t;
    t.prepare(kFs);
    Voice v(110.0f);
    v.set_vowel(kA, 2);
    Saw saw(100.0f);
    const auto out = run_talk(t, [&] { return gain * v.next(); }, [&] { return saw.next(); }, 3000, [](std::size_t) {});
    float s = 0.0f;
    for (std::size_t i = 2500 * 64; i < out.size(); ++i) s += out[i] * out[i];
    return std::sqrt(s / static_cast<float>(out.size() - 2500 * 64));
  };
  const float loud = level(0.1f);  // a hotter voice meets the AGC's lowest gain
  const float quiet = level(0.025f);
  std::printf("talk: AGC rms loud %.5f quiet %.5f\n", static_cast<double>(loud), static_cast<double>(quiet));
  CHECK(loud > 0.0f);
  CHECK(quiet / loud > 0.8f);  // 12 dB less at the input, within 2 dB at the output
  CHECK(quiet / loud < 1.25f);
  const float floor = level(0.00003f);  // under the gate: unity gain, so it stays about 40 dB under the others
  CHECK(floor < 0.1f * loud);
}

TEST_CASE("vocoder: a change of MODE keeps the level within 6 dB on a voice and a chord") {
  static Vocoder vo;
  vo.prepare(kFs);
  const std::size_t sw = 2000;
  const auto out = run_unit(vo, 4000, sw, 1000000);
  const auto rms = [&](std::size_t from, std::size_t to) {
    float s = 0.0f;
    for (std::size_t i = from; i < to; ++i) s += out[i] * out[i];
    return std::sqrt(s / static_cast<float>(to - from));
  };
  const float bands = rms(1500 * 64, sw * 64);
  const float talk = rms(3500 * 64, 4000 * 64);
  std::printf("talk: unit rms bands %.5f talk %.5f ratio %.4f\n", static_cast<double>(bands), static_cast<double>(talk), static_cast<double>(talk / bands));
  CHECK(talk / bands > 0.5f);
  CHECK(talk / bands < 2.0f);
}

// The whole unit, not one engine: a unit round read the OUT meter against the input level and got no clear answer.
TEST_CASE("vocoder: BANDS and TALK hold their level with the voice 12 and 24 dB down") {
  static Vocoder vo;
  const auto level = [&](float in, bool talk) {
    vo.prepare(kFs);
    const std::size_t blocks = 3000;
    const auto out = run_unit(vo, blocks, talk ? 0 : 1000000, 1000000, in);
    float s = 0.0f;
    for (std::size_t i = 2000 * 64; i < blocks * 64; ++i) s += out[i] * out[i];
    return std::sqrt(s / static_cast<float>((blocks - 2000) * 64));
  };
  for (bool talk : {false, true}) {
    // This voice runs hot: at 0.3 it already meets the AGC's lowest gain, so the three levels start under that.
    const float a = level(0.075f, talk), b = level(0.019f, talk), c = level(0.0047f, talk);
    MESSAGE("talk " << talk << ": " << a << " " << b << " " << c);
    std::printf("talk: unit 12/24 dB rms talk %d: %.5f %.5f %.5f\n", talk ? 1 : 0, static_cast<double>(a), static_cast<double>(b), static_cast<double>(c));
    CHECK(a > 0.01f);
    CHECK(b / a > 0.7f);
    CHECK(b / a < 1.42f);
    CHECK(c / a > 0.7f);
    CHECK(c / a < 1.42f);
  }
}

TEST_CASE("talk: the lattice's envelope peaks land within 60 Hz of a known vowel's F1 and F2 at Quality 16") {
  Talk& t = *make_talk();
  t.set_order(16);
  Buzz v(110.0f, 650.0f, 90.0f, 1700.0f, 120.0f);
  Saw saw(100.0f);
  run_talk(t, [&] { return v.next(); }, [&] { return saw.next(); }, 300, [](std::size_t) {});
  float best1 = 0.0f, best2 = 0.0f, e1 = 0.0f, e2 = 0.0f;
  for (float hz = 200.0f; hz < 3500.0f; hz += 5.0f) {
    const float e = lattice_env(t, 12, hz);
    if (hz < 1150.0f && e > e1) {
      e1 = e;
      best1 = hz;
    }
    if (hz >= 1150.0f && e > e2) {
      e2 = e;
      best2 = hz;
    }
  }
  MESSAGE("F1 " << best1 << " Hz, F2 " << best2 << " Hz");
  CHECK(std::fabs(best1 - 650.0f) <= 60.0f);
  CHECK(std::fabs(best2 - 1700.0f) <= 60.0f);
}

TEST_CASE("talk: any frame count gives the same output as calls of 64") {
  const std::size_t total = 64 * 85;  // 85 x 64 = 64 x (1 + 7 + 13 + 64)
  std::vector<float> m(total), c(total);
  {
    Voice v(110.0f);
    v.set_vowel(kA, 2);
    Saw saw(100.0f);
    for (std::size_t i = 0; i < total; ++i) {
      m[i] = 0.3f * v.next();
      c[i] = 0.3f + 0.2f * saw.next();  // never under 0.1: the breath's gate sees the same carrier in every call
    }
  }
  Talk& a = *make_talk();
  a.set_order(16);
  std::vector<float> ref(total);
  for (std::size_t i = 0; i < total; i += 64) a.process(&m[i], &c[i], &ref[i], 64);
  static Talk b;
  b.prepare(kFs);
  b.set_order(16);
  std::vector<float> got(total);
  const std::uint32_t pattern[] = {1, 7, 13, 64};
  std::size_t pos = 0;
  unsigned k = 0;
  while (pos < total) {
    std::size_t n = pattern[k++ % 4];
    n = std::min(n, total - pos);
    b.process(&m[pos], &c[pos], &got[pos], static_cast<std::uint32_t>(n));
    pos += n;
  }
  float worst = 0.0f;
  for (std::size_t i = 0; i < total; ++i) worst = std::max(worst, std::fabs(ref[i] - got[i]));
  MESSAGE("worst difference " << worst << " on a peak of " << peak(ref));
  CHECK(peak(ref) > 0.01f);
  CHECK(worst <= 1.0e-5f);
  // single frames too
  static Talk d;
  d.prepare(kFs);
  d.set_order(16);
  std::vector<float> one(total);
  for (std::size_t i = 0; i < total; ++i) d.process(&m[i], &c[i], &one[i], 1);
  worst = 0.0f;
  for (std::size_t i = 0; i < total; ++i) worst = std::max(worst, std::fabs(ref[i] - one[i]));
  CHECK(worst <= 1.0e-5f);
}

TEST_CASE("talk: the breath is the modulator above 5 kHz, only while the carrier sounds, and THRU doubles it") {
  const std::size_t blocks = 400;
  const auto run = [&](bool carrier_on, float thru) {
    Talk& t = *make_talk();
    t.set_thru(thru);
    Noise n;
    Saw saw(100.0f);
    return run_talk(t, [&] { return n.next(); }, [&] { return carrier_on ? saw.next() : 0.0f; }, blocks, [](std::size_t) {});
  };
  const std::size_t from = 200 * 64, to = blocks * 64;
  const auto on0 = run(true, 0.0f);
  const auto on1 = run(true, 1.0f);
  const auto off = run(false, 1.0f);
  const float p0 = power_above(on0, from, to, 6000.0f);
  const float p1 = power_above(on1, from, to, 6000.0f);
  CHECK(p0 > 1.0e-9f);  // the carrier is below 5.5 kHz after the decimators: this is the breath
  CHECK(peak(off) == 0.0f);
  CHECK(power_above(off, from, to, 6000.0f) == 0.0f);
  const float ratio = std::sqrt(p1 / p0);
  MESSAGE("THRU 1 over THRU 0, amplitude " << ratio);
  CHECK(ratio > 1.9f);
  CHECK(ratio < 2.1f);
}

TEST_CASE("talk: a NaN and an Inf in the modulator and in the carrier leave the output finite") {
  Talk& t = *make_talk();
  Voice v(110.0f);
  v.set_vowel(kA, 2);
  Saw saw(100.0f);
  const float nan = std::nanf(""), inf = INFINITY;
  std::vector<float> out;
  for (std::size_t b = 0; b < 300; ++b) {
    float m[64], c[64], o[64];
    for (int i = 0; i < 64; ++i) {
      m[i] = 0.3f * v.next();
      c[i] = saw.next();
    }
    if (b == 50) m[10] = nan;
    if (b == 60) m[20] = inf;
    if (b == 70) c[30] = nan;
    if (b == 80) c[40] = -inf;
    t.process(m, c, o, 64);
    out.insert(out.end(), o, o + 64);
  }
  CHECK(finite(out));
  CHECK(peak(out, 200 * 64) > 0.01f);
  for (std::uint32_t m = 1; m <= Talk::kMaxStages; ++m) CHECK(std::isfinite(t.reflection(m)));
}

TEST_CASE("talk: both inputs silent after a vowel give exact silence, no denormals; reset leaves no state") {
  Talk& t = *make_talk();
  Voice v(110.0f);
  v.set_vowel(kA, 2);
  Saw saw(100.0f);
  std::size_t sample = 0;
  auto out = run_talk(t, [&] { return (sample < 44100u) ? v.next() : 0.0f; },
                      [&] { return (sample++ < 44100u) ? saw.next() : 0.0f; }, 3 * 44100 / 64, [](std::size_t) {});
  CHECK(peak(out, 0) > 0.0f);
  for (std::size_t i = 0; i < out.size(); ++i) CHECK(std::fpclassify(out[i]) != FP_SUBNORMAL);
  for (std::size_t i = out.size() - 64 * 64; i < out.size(); ++i) CHECK(out[i] == 0.0f);
  // after reset(), silence in gives silence out at once: no lattice, filter or decimator state left over
  Voice w(110.0f);
  w.set_vowel(kA, 2);
  Saw s2(100.0f);
  run_talk(t, [&] { return w.next(); }, [&] { return s2.next(); }, 200, [](std::size_t) {});
  t.reset();
  std::vector<float> z(64, 0.0f), o(64, 1.0f);
  for (int b = 0; b < 4; ++b) {
    t.process(z.data(), z.data(), o.data(), 64);
    CHECK(peak(o) == 0.0f);
  }
}

TEST_CASE("talk: Quality maps to the lattice order (q x 3 + 2) / 4 and stays in 8..24") {
  CHECK(Talk::stages_for(8) == 6);
  CHECK(Talk::stages_for(16) == 12);
  CHECK(Talk::stages_for(24) == 18);
  CHECK(Talk::kMaxStages == 18);
  for (std::uint32_t q = 8; q <= 24; ++q) {
    Talk& t = *make_talk();
    t.set_order(q);
    CHECK(t.order() == q);
    Voice v(110.0f);
    v.set_vowel(kA, 2);
    Saw saw(100.0f);
    run_talk(t, [&] { return v.next(); }, [&] { return saw.next(); }, 100, [](std::size_t) {});
    CHECK(t.lattice_order() == (q * 3 + 2) / 4);
    CHECK(t.reflection_target((q * 3 + 2) / 4) != 0.0f);
  }
  Talk& t = *make_talk();
  t.set_order(3);
  CHECK(t.order() == 8);
  t.set_order(99);
  CHECK(t.order() == 24);
}

// The carrier's driver (a one-pole high-pass at 250 Hz and a cubic soft clip), alone.
namespace {

float drive_rms(float hz, float amp) {
  CarDrive d;
  d.prepare(kFs);
  std::vector<float> x(8192);
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = amp * std::sin(kPi2 * hz * static_cast<float>(i) / kFs);
  for (std::size_t i = 0; i < x.size(); i += 64) d.process(x.data() + i, 64);
  float s = 0.0f;
  for (std::size_t i = 4096; i < x.size(); ++i) s += x[i] * x[i];
  return std::sqrt(s / 4096.0f);
}

}  // namespace

TEST_CASE("talk: the carrier driver attenuates below its corner and keeps a small signal above it") {
  const float low = drive_rms(60.0f, 0.05f), mid = drive_rms(1000.0f, 0.05f), at = drive_rms(250.0f, 0.05f);
  const float in_rms = 0.05f * 0.70710678f * CarDrive::kDriveGain;  // the small-signal gain is the make-up
  std::printf("talk: driver gain at 60 Hz %.3f, 250 Hz %.3f, 1 kHz %.3f (small signal)\n", static_cast<double>(low / in_rms),
              static_cast<double>(at / in_rms), static_cast<double>(mid / in_rms));
  CHECK(low / in_rms < 0.3f);                  // 60 Hz: about 0.24 for the one pole
  CHECK(at / in_rms > 0.6f);                   // the corner: about -3 dB
  CHECK(at / in_rms < 0.8f);
  CHECK(mid / in_rms > 0.95f);
  CHECK(mid / in_rms < 1.0f + 1.0e-3f);
  // A DC offset does not get through.
  CarDrive d;
  d.prepare(kFs);
  std::vector<float> dc(4096, 0.3f);
  for (std::size_t i = 0; i < dc.size(); i += 64) d.process(dc.data() + i, 64);
  CHECK(std::fabs(dc.back()) < 1.0e-6f);
}

TEST_CASE("talk: a hot carrier through the driver stays bounded and finite") {
  CarDrive d;
  d.prepare(kFs);
  std::vector<float> x(16384);
  std::uint32_t s = 12345u;
  for (std::size_t i = 0; i < x.size(); ++i) {
    s = s * 1664525u + 1013904223u;
    // +-1e6 noise, a +-50 square at 300 Hz and a sine, in turn
    const float r = static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
    x[i] = i < 4096 ? 1.0e6f * r : (i < 8192 ? ((i / 73) % 2 == 0 ? 50.0f : -50.0f) : 20.0f * std::sin(kPi2 * 700.0f * static_cast<float>(i) / kFs));
  }
  for (std::size_t i = 0; i < x.size(); i += 64) d.process(x.data() + i, 64);
  float mx = 0.0f;
  for (float v : x) {
    REQUIRE(std::isfinite(v));
    mx = std::max(mx, std::fabs(v));
  }
  std::printf("talk: driver peak out for a hot carrier %.4f (bound %.4f)\n", static_cast<double>(mx), static_cast<double>(CarDrive::kDriveOut));
  CHECK(mx <= CarDrive::kDriveOut * (1.0f + 1.0e-6f));
  CHECK(mx > 0.9f * CarDrive::kDriveOut);  // and it does saturate
}

// BANDS must come out of the unit exactly as it did before ROBOT existed: an FNV-1a digest of the bit patterns of the BANDS stretches of a run
// that goes to TALK and back (so the crossfade's mixing path is in it too). The value was taken from the build before ROBOT was added.
TEST_CASE("vocoder: the BANDS output is bit-identical to what it was before ROBOT") {
  static Vocoder vo;
  vo.prepare(kFs);
  const auto out = run_unit(vo, 1500, 700, 1100);
  std::uint32_t d = 2166136261u;
  const auto eat = [&](std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
      std::uint32_t u;
      std::memcpy(&u, &out[i], sizeof u);
      d = (d ^ u) * 16777619u;
    }
  };
  eat(0, 690 * 64);
  eat(1300 * 64, 1500 * 64);
  std::printf("vocoder: BANDS digest %08X\n", d);
  CHECK(d == 0xE55118E5u);
}

// ---- ROBOT: BANDS followed by a fast compressor on the wet sum ------------------------------------------------------------------------

namespace {

// The Vocoder in `mode` with the vowel modulator at level(block) and two held notes.
template <typename LevelFn>
std::vector<float> run_mode(Vocoder& vo, std::size_t blocks, float mode, std::size_t switch_to, float mode2, LevelFn level) {
  Voice v(110.0f);
  v.set_vowel(kA, 2);
  std::vector<float> out(blocks * 64);
  dsp::Transport tr;
  for (std::size_t b = 0; b < blocks; ++b) {
    float m[64], r[64];
    const float lv = level(b);
    for (float& x : m) x = lv * v.next();
    dsp::Event ev[3];
    std::uint32_t n = 0;
    if (b == 0) {
      ev[n++] = dsp::param_change(Vocoder::Mode, mode);
      ev[n++] = dsp::note_on(48, 100);
      ev[n++] = dsp::note_on(55, 100);
    }
    if (b == switch_to) ev[n++] = dsp::param_change(Vocoder::Mode, mode2);
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

float rms_blocks(const std::vector<float>& x, std::size_t b0, std::size_t b1) {
  float s = 0.0f;
  for (std::size_t i = b0 * 64; i < b1 * 64; ++i) s += x[i] * x[i];
  return std::sqrt(s / static_cast<float>((b1 - b0) * 64));
}

constexpr std::size_t kSeg = 120;  // blocks per level segment: 174 ms, faster than the BANDS AGC's 500 ms release

}  // namespace

TEST_CASE("robot: MODE has four values, ROBOT is the fourth and takes no new parameter") {
  static Vocoder vo;
  vo.prepare(kFs);
  const auto& p = vo.info().params[Vocoder::Mode];
  CHECK(p.max == 3.0f);
  CHECK(p.def == 0.0f);
  CHECK(Vocoder::kParamCount == 62);
}

TEST_CASE("robot: the compressor flattens a level that alternates 12 dB, and BANDS does not as much") {
  static Vocoder vo;
  const auto alt = [](std::size_t b) { return (b / kSeg) % 2 == 0 ? 0.075f : 0.019f; };
  float ratio[2] = {};
  for (int robot = 0; robot < 2; ++robot) {
    vo.prepare(kFs);
    const std::size_t blocks = kSeg * 12;
    const auto out = run_mode(vo, blocks, robot ? 3.0f : 0.0f, 1000000, 0.0f, alt);
    CHECK(finite(out));
    float loud = 0.0f, quiet = 0.0f;
    unsigned n = 0;
    for (std::size_t s = 6; s + 1 < 12; s += 2) {  // later cycles, the last 60 blocks of each segment
      loud += rms_blocks(out, s * kSeg + 60, (s + 1) * kSeg);
      quiet += rms_blocks(out, (s + 1) * kSeg + 60, (s + 2) * kSeg);
      ++n;
    }
    loud /= static_cast<float>(n);
    quiet /= static_cast<float>(n);
    ratio[robot] = quiet / loud;
    std::printf("robot: alternating 12 dB, mode %d: loud %.5f quiet %.5f ratio %.3f (the input's is 0.253)\n", robot ? 3 : 0, static_cast<double>(loud),
                static_cast<double>(quiet), static_cast<double>(ratio[robot]));
    CHECK(loud > 0.01f);
  }
  // 1 would be flat. ROBOT must be much nearer to it than BANDS: the 12 dB the input falls by comes out as under half of that.
  CHECK(ratio[1] > ratio[0] + 0.15f);
  CHECK(ratio[1] > 0.6f);
}

TEST_CASE("robot: BANDS to ROBOT and back, during sound, makes no click and no level jump") {
  static Vocoder vo;
  vo.prepare(kFs);
  const std::size_t sw_a = 700, sw_b = 1100;
  const auto level = [](std::size_t) { return 0.075f; };
  // two runs would need two switches: the helper has one, so go to ROBOT first and back in a second run of the same length
  const auto to_robot = run_mode(vo, 1000, 0.0f, sw_a, 3.0f, level);
  CHECK(finite(to_robot));
  CHECK(peak(to_robot) < 1.0f);
  const std::size_t a0 = sw_a * 64;
  const float step_before = max_step(to_robot, 300 * 64, a0);
  CHECK(step_before > 0.0f);
  CHECK(max_step(to_robot, a0 - 64, a0 + 40 * 64) <= 1.5f * step_before + 0.0f);
  float mid = 0.0f;
  for (std::size_t i = a0 - 40 * 64; i < a0; ++i) mid = std::max(mid, std::fabs(to_robot[i]));
  float after = 0.0f;
  for (std::size_t i = a0 + 40 * 64; i < to_robot.size(); ++i) after = std::max(after, std::fabs(to_robot[i]));
  std::printf("robot: peak before the switch %.4f, after %.4f, max step before %.5f across %.5f\n", static_cast<double>(mid), static_cast<double>(after),
              static_cast<double>(step_before), static_cast<double>(max_step(to_robot, a0 - 64, a0 + 40 * 64)));

  vo.prepare(kFs);
  const auto back = run_mode(vo, 1500, 3.0f, sw_b, 0.0f, level);
  CHECK(finite(back));
  CHECK(peak(back) < 1.0f);
  const std::size_t b0 = sw_b * 64;
  const float step_robot = max_step(back, 600 * 64, b0);
  CHECK(step_robot > 0.0f);
  CHECK(max_step(back, b0 - 64, b0 + 40 * 64) <= 1.5f * std::max(step_robot, max_step(back, b0 + 100 * 64, 1500 * 64)));
}

TEST_CASE("robot: a hot voice, NaN and Inf at the input leave ROBOT finite and bounded") {
  static Vocoder vo;
  vo.prepare(kFs);
  const auto hot = run_mode(vo, 600, 3.0f, 1000000, 0.0f, [](std::size_t) { return 1.0f; });
  CHECK(finite(hot));
  CHECK(peak(hot) < 1.5f);

  vo.prepare(kFs);
  dsp::Transport tr;
  float m[64], r[64], o[64];
  bool ok = true;
  for (std::size_t b = 0; b < 400; ++b) {
    for (std::size_t i = 0; i < 64; ++i) m[i] = 0.1f * std::sin(0.05f * static_cast<float>(b * 64 + i));
    if (b == 100) m[3] = std::numeric_limits<float>::quiet_NaN();
    if (b == 120) m[9] = std::numeric_limits<float>::infinity();
    dsp::Event ev[3];
    std::uint32_t n = 0;
    if (b == 0) {
      ev[n++] = dsp::param_change(Vocoder::Mode, 3.0f);
      ev[n++] = dsp::note_on(48, 100);
    }
    dsp::ProcessArgs a{};
    a.events = ev;
    a.event_count = n;
    a.in_l = m;
    a.in_r = m;
    a.out_l = o;
    a.out_r = r;
    a.frames = 64;
    a.transport = &tr;
    vo.process(a);
    if (b >= 300)
      for (float x : o) ok = ok && std::isfinite(x);
  }
  CHECK(ok);
}

TEST_CASE("robot: the compressor alone, 4:1 above its threshold, make-up below, silence stays silence") {
  RobotComp c;
  c.prepare(kFs);
  const auto run = [&](float amp) {
    c.reset();
    std::vector<float> x(4096);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = amp * std::sin(0.3f * static_cast<float>(i));
    c.process(x.data(), static_cast<std::uint32_t>(x.size()));
    float s = 0.0f;
    for (std::size_t i = 2048; i < x.size(); ++i) s += x[i] * x[i];
    return std::sqrt(s / 2048.0f) / (amp * 0.70710678f);  // the gain it settled at
  };
  CHECK(run(RobotComp::kThreshold * 0.1f) == doctest::Approx(RobotComp::kMakeup).epsilon(0.02));
  const float g1 = run(RobotComp::kThreshold * 4.0f);
  const float g2 = run(RobotComp::kThreshold * 16.0f);
  // 12 dB more in comes out 9 dB more (the exponent 0.75): the gain falls by 9 dB, a factor of 4^-0.75 = 0.3536
  CHECK(g2 / g1 == doctest::Approx(0.35355f).epsilon(0.05));
  c.reset();
  std::vector<float> z(256, 0.0f);
  c.process(z.data(), 256);
  for (float v : z) CHECK(v == 0.0f);
  // any block length, any alignment
  std::vector<float> y(37, 0.01f);
  c.process(y.data(), 37);
  for (float v : y) CHECK(std::isfinite(v));
}

TEST_CASE("robot: the whole run is deterministic, two runs give the same bits") {
  static Vocoder vo;
  const auto alt = [](std::size_t b) { return (b / kSeg) % 2 == 0 ? 0.075f : 0.019f; };
  vo.prepare(kFs);
  const auto a = run_mode(vo, 400, 3.0f, 1000000, 0.0f, alt);
  vo.prepare(kFs);
  const auto b = run_mode(vo, 400, 3.0f, 1000000, 0.0f, alt);
  CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}
