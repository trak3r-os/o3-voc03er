// SPDX-License-Identifier: GPL-3.0-or-later
// Talk (the LPC talkbox) and the Vocoder's MODE switch, against synthetic signals only: saw voices through formant resonators.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/talk.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
    for (int b = 0; b < 12 * 6; ++b) {
      for (int i = 0; i < 64; ++i) {
        m[i] = v.next();
        c[i] = saw.next();
      }
      t.process(m, c, o, 64);
      macs.push_back(t.macs_last_block());
    }
    // Hops after the ring has filled (12 blocks), each aligned: blocks 6k .. 6k+5.
    for (std::size_t h = 3; h < 12; ++h) {
      std::uint32_t mx = 0;
      double sum = 0.0;
      for (std::size_t j = 0; j < 6; ++j) {
        mx = std::max(mx, macs[h * 6 + j]);
        sum += macs[h * 6 + j];
      }
      CHECK(static_cast<double>(mx) <= 1.5 * (sum / 6.0));
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
    for (std::uint32_t m = 1; m <= 24; ++m) {
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
  float held[25];
  for (std::uint32_t m = 1; m <= 16; ++m) held[m] = t.reflection_target(m);
  CHECK(std::fabs(held[1]) > 0.1f);
  t.set_freeze(true);
  v.set_vowel(kB, 2);
  auto out = run_talk(t, [&] { return v.next(); }, [&] { return saw.next(); }, 200, [](std::size_t) {});
  for (std::uint32_t m = 1; m <= 16; ++m) {
    CHECK(t.reflection_target(m) == held[m]);
    CHECK(t.reflection(m) == held[m]);
  }
  CHECK(peak(out, 100 * 64) > 0.01f);  // still sounding
  t.set_freeze(false);
  run_talk(t, [&] { return v.next(); }, [&] { return saw.next(); }, 200, [](std::size_t) {});
  float moved = 0.0f;
  for (std::uint32_t m = 1; m <= 16; ++m) moved = std::max(moved, std::fabs(t.reflection_target(m) - held[m]));
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
std::vector<float> run_unit(Vocoder& vo, std::size_t blocks, std::size_t switch_a, std::size_t switch_b) {
  Voice v(110.0f);
  v.set_vowel(kA, 2);
  std::vector<float> out(blocks * 64);
  dsp::Transport tr;
  for (std::size_t b = 0; b < blocks; ++b) {
    float m[64], r[64];
    for (float& x : m) x = 0.3f * v.next();
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
  CHECK(Vocoder::kParamCount == 47);
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
    for (std::uint32_t m = 1; m <= Talk::kMaxOrder; ++m) {
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
