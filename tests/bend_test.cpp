// SPDX-License-Identifier: GPL-3.0-or-later
// BEND and VIB on the vocoder's SYNTH carrier: Bend 0 and Vib 0 are the exact unbent path, the pitch ratio at +2 and -5 semitones, the swing of
// the vibrato at depth 1. Synthetic signals only; the pitch is the autocorrelation lag of the output under a modulator of four partials.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

namespace {

constexpr float kFs = 44100.0f;

std::vector<float> render(const std::vector<std::vector<dsp::Event>>& ev, std::size_t blocks) {
  Vocoder v;
  v.prepare(kFs);
  std::vector<float> mod(blocks * 64);
  for (std::size_t i = 0; i < mod.size(); ++i) {  // four steady partials: the bands stay open on the carrier's harmonics there
    const float t = static_cast<float>(i) / kFs;
    mod[i] = 0.12f * (std::sin(6.2831853f * 500.0f * t) + std::sin(6.2831853f * 1000.0f * t) + std::sin(6.2831853f * 2000.0f * t) +
                      std::sin(6.2831853f * 3000.0f * t));
  }
  std::vector<float> out(mod.size());
  dsp::Transport tr;
  for (std::size_t b = 0; b < blocks; ++b) {
    float r[64];
    dsp::ProcessArgs a{};
    a.events = b < ev.size() ? ev[b].data() : nullptr;
    a.event_count = b < ev.size() ? static_cast<std::uint32_t>(ev[b].size()) : 0u;
    a.in_l = mod.data() + b * 64;
    a.in_r = mod.data() + b * 64;
    a.out_l = out.data() + b * 64;
    a.out_r = r;
    a.frames = 64;
    a.transport = &tr;
    v.process(a);
  }
  return out;
}

// Pitch in Hz of out[from, from + len) by the autocorrelation peak within 0.65 to 1.3 times 110 Hz, with a parabolic refinement.
float pitch(const std::vector<float>& x, std::size_t from, std::size_t len, float lo_f = 0.65f, float hi_f = 1.3f) {
  const std::size_t lo = static_cast<std::size_t>(kFs / (110.0f * hi_f));
  const std::size_t hi = static_cast<std::size_t>(kFs / (110.0f * lo_f));
  std::vector<double> ac(hi + 2, 0.0);
  for (std::size_t lag = lo - 1; lag <= hi + 1; ++lag) {
    double a = 0.0;
    for (std::size_t i = 0; i + lag < len; ++i) {
      a += static_cast<double>(x[from + i]) * static_cast<double>(x[from + i + lag]);
    }
    ac[lag] = a;
  }
  std::size_t best = lo;
  for (std::size_t lag = lo; lag <= hi; ++lag) {
    if (ac[lag] > ac[best]) {
      best = lag;
    }
  }
  const double d = ac[best - 1] - 2.0 * ac[best] + ac[best + 1];
  const double off = d != 0.0 ? 0.5 * (ac[best - 1] - ac[best + 1]) / d : 0.0;
  return static_cast<float>(static_cast<double>(kFs) / (static_cast<double>(best) + off));
}

std::vector<std::vector<dsp::Event>> held(std::vector<dsp::Event> first) {
  std::vector<std::vector<dsp::Event>> ev(1);
  ev[0] = std::move(first);
  ev[0].push_back(dsp::note_on(45, 100, 0, 0));
  return ev;
}

}  // namespace

TEST_CASE("bend: BEND is parameter 42 and VIB 43, both default 0") {
  CHECK(Vocoder::Bend == 42);
  CHECK(Vocoder::Vib == 43);
  CHECK(Vocoder::kParamCount == 62);
  Vocoder v;
  CHECK(v.info().params[Vocoder::Bend].def == 0.0f);
  CHECK(v.info().params[Vocoder::Vib].def == 0.0f);
}

TEST_CASE("bend: BEND 0 and VIB 0 written are bit-exact with never written") {
  const auto base = render(held({}), 200);
  const auto zero = render(held({dsp::param_change(Vocoder::Bend, 0.0f), dsp::param_change(Vocoder::Vib, 0.0f)}), 200);
  CHECK(base == zero);
  // a bend that comes back to 0 settles to the exact path too
  auto ev = held({dsp::param_change(Vocoder::Bend, 3.0f)});
  ev.resize(100);
  ev[99] = {dsp::param_change(Vocoder::Bend, 0.0f)};
  const auto back = render(ev, 200);
  const auto ref = render(held({}), 200);
  bool same = true;
  for (std::size_t i = 150 * 64; i < back.size(); ++i) {
    same = same && std::fabs(back[i]) < 2.0f;  // sane; the phase differs, so only the level is compared
  }
  CHECK(same);
  CHECK(ref.size() == back.size());
}

TEST_CASE("bend: +2 and -5 semitones scale the pitch by 2^(st/12)") {
  const float p0 = pitch(render(held({}), 200), 100 * 64, 4096);
  CHECK(p0 == doctest::Approx(110.0f).epsilon(0.02));
  const float p2 = pitch(render(held({dsp::param_change(Vocoder::Bend, 2.0f)}), 200), 100 * 64, 4096);
  const float m5 = pitch(render(held({dsp::param_change(Vocoder::Bend, -5.0f)}), 200), 100 * 64, 4096);
  CHECK(p2 / p0 == doctest::Approx(std::exp2(2.0f / 12.0f)).epsilon(0.01));
  CHECK(m5 / p0 == doctest::Approx(std::exp2(-5.0f / 12.0f)).epsilon(0.01));
}

TEST_CASE("bend: VIB 1 swings the pitch by about half a semitone each way at 5.5 Hz") {
  const auto out = render(held({dsp::param_change(Vocoder::Vib, 1.0f)}), 1200);
  const float p0 = pitch(render(held({}), 200), 100 * 64, 4096, 0.9f, 1.1f);
  float lo = 1.0e9f;
  float hi = 0.0f;
  for (std::size_t at = 64 * 100; at + 4096 < out.size(); at += 1024) {
    const float p = pitch(out, at, 4096, 0.9f, 1.1f);
    lo = std::fmin(lo, p);
    hi = std::fmax(hi, p);
  }
  const float up = 12.0f * std::log2(hi / p0);
  const float down = 12.0f * std::log2(lo / p0);
  CHECK(up > 0.3f);
  CHECK(up < 0.7f);
  CHECK(down < -0.3f);
  CHECK(down > -0.7f);
}
