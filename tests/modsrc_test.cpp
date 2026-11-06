// SPDX-License-Identifier: GPL-3.0-or-later
// ModSrc (what speaks) and CarSrc LIVE (the live input as the carrier), U3a: LIVE is today's modulator, NONE and a slot take the live input
// out of the modulator sum, a slot speaks in its place, and the live input can sound as the carrier. Synthetic signals only.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/loop.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>
#include <o3sdk/dsp/transport.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

namespace {

constexpr float kFs = 44100.0f;
constexpr float kPi2 = 6.2831853f;

struct Rig {
  Vocoder v;
  std::vector<std::int16_t> mem;
  dsp::Transport tr;
  Rig() : mem(static_cast<std::size_t>(Loop::kCount) * 8000u) {
    v.bind_loops(mem.data(), 8000u);
    v.prepare(kFs);
    tr.playing = false;
    tr.tempo_mbpm = 120'000;
  }
  std::vector<float> block(const float* in, std::vector<dsp::Event> ev = {}, const float* right = nullptr) {
    constexpr std::uint32_t n = 64;
    std::vector<float> out(n), r(n), zero(n, 0.0f);
    dsp::ProcessArgs a{};
    a.events = ev.data();
    a.event_count = static_cast<std::uint32_t>(ev.size());
    a.in_l = in != nullptr ? in : zero.data();
    a.in_r = right != nullptr ? right : a.in_l;
    a.out_l = out.data();
    a.out_r = r.data();
    a.frames = n;
    tr.frames = n;
    a.transport = &tr;
    v.process(a);
    return out;
  }
};

std::vector<float> tone(std::size_t n, float hz, float amp) {
  std::vector<float> x(n);
  for (std::size_t i = 0; i < n; ++i) x[i] = amp * std::sin(kPi2 * hz * static_cast<float>(i) / kFs);
  return x;
}

float rms(const std::vector<float>& v) {
  float s = 0.0f;
  for (float f : v) s += f * f;
  return std::sqrt(s / static_cast<float>(v.size()));
}

// Runs `blocks` blocks of `x` (cycled) after the events, returns the last block's output.
std::vector<float> run(Rig& r, const std::vector<float>& x, unsigned blocks, std::vector<dsp::Event> ev = {}) {
  std::vector<float> out;
  for (unsigned b = 0; b < blocks; ++b) {
    const std::size_t off = (static_cast<std::size_t>(b) * 64u) % (x.size() - 64u);
    out = r.block(x.data() + off, b == 0 ? ev : std::vector<dsp::Event>{});
  }
  return out;
}

float energy(const std::vector<float>& v, float hz) {
  const float c = 2.0f * std::cos(kPi2 * hz / kFs);
  float s1 = 0.0f, s2 = 0.0f;
  for (float f : v) {
    const float s = f + c * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  return s1 * s1 + s2 * s2 - c * s1 * s2;
}

}  // namespace

TEST_CASE("ModSrc: the ids and the table") {
  static_assert(Vocoder::ModSrc == 47 && Vocoder::LiveCh == 48 && Vocoder::kParamCount == 61, "append only");
  Vocoder u;
  const auto& info = u.info();
  CHECK(info.params[Vocoder::ModSrc].max == 6.0f);
  CHECK(info.params[Vocoder::CarSrc].max == 7.0f);
}

TEST_CASE("ModSrc: LIVE is bit-identical to a unit that never heard of it") {
  const auto x = tone(4096, 300.0f, 0.4f);
  Rig c, d;
  const std::vector<dsp::Event> ev = {dsp::param_change(Vocoder::Dry, 1.0f), dsp::param_change(Vocoder::ModSrc, 0.0f)};
  const auto ya = run(c, x, 40, {dsp::param_change(Vocoder::Dry, 1.0f)});
  const auto yb = run(d, x, 40, ev);
  REQUIRE(ya.size() == yb.size());
  for (std::size_t i = 0; i < ya.size(); ++i) CHECK(ya[i] == yb[i]);
}

TEST_CASE("ModSrc: NONE takes the live input out of the modulator, no bleed in DRY") {
  const auto x = tone(4096, 300.0f, 0.4f);
  Rig live, none;
  const auto yl = run(live, x, 60, {dsp::param_change(Vocoder::Dry, 1.0f)});
  const auto yn = run(none, x, 60, {dsp::param_change(Vocoder::Dry, 1.0f), dsp::param_change(Vocoder::ModSrc, 1.0f)});
  CHECK(rms(yl) > 0.1f);
  CHECK(rms(yn) < 1.0e-4f);
}

TEST_CASE("ModSrc: a slot speaks in place of the live input, which a recording still takes") {
  Rig rig;
  const auto voice = tone(64 * 40, 440.0f, 0.4f);
  const auto mic = tone(64 * 40, 1500.0f, 0.4f);
  // Record the 440 Hz into loop 1 (transport stopped: it starts at once and ends at the next request).
  (void)run(rig, voice, 1, {dsp::param_change(Vocoder::loop_param(0, Vocoder::LoopField::Rec), 1.0f)});
  for (unsigned b = 1; b < 30; ++b) (void)rig.block(voice.data() + b * 64u);
  (void)rig.block(nullptr, {dsp::param_change(Vocoder::loop_param(0, Vocoder::LoopField::Rec), 0.0f)});
  // Now the mic is 1500 Hz and slot 1 speaks, heard in DRY.
  (void)run(rig, mic, 1,
            {dsp::param_change(Vocoder::Dry, 1.0f), dsp::param_change(Vocoder::ModSrc, 3.0f),
             dsp::param_change(Vocoder::loop_param(0, Vocoder::LoopField::Play), 1.0f)});
  std::vector<float> got;
  for (unsigned b = 0; b < 60; ++b) {
    const auto o = rig.block(mic.data() + (b % 30u) * 64u);
    got.insert(got.end(), o.begin(), o.end());
  }
  CHECK(rms(got) > 0.05f);
  CHECK(energy(got, 440.0f) > 20.0f * energy(got, 1500.0f));  // the slot, not the microphone
}

TEST_CASE("CarSrc LIVE: the live input is the carrier, the synth is not") {
  const auto x = tone(4096, 300.0f, 0.4f);
  Rig syn, lv;
  const auto ys = run(syn, x, 80, {dsp::param_change(Vocoder::CarSrc, 1.0f)});
  const auto yl = run(lv, x, 80, {dsp::param_change(Vocoder::CarSrc, 7.0f)});
  CHECK(rms(ys) < 1.0e-4f);  // no note held: the synth is silent
  CHECK(rms(yl) > 1.0e-3f);  // the voice through itself
}

TEST_CASE("LiveCh: USB speaks on the left and sounds on the right") {
  // The voice is on the left; the right channel is silent or carries the same tone. Only LiveCh R makes the carrier follow the right one.
  const auto x = tone(8192, 300.0f, 0.4f);
  const std::vector<float> zero(8192, 0.0f);
  const auto level = [&](float ch, const std::vector<float>& right) {
    Rig r;
    float peak = 0.0f;
    for (unsigned b = 0; b < 100; ++b) {
      const std::size_t off = static_cast<std::size_t>(b) * 64u;
      const std::vector<dsp::Event> ev = b == 0 ? std::vector<dsp::Event>{dsp::param_change(Vocoder::CarSrc, 7.0f), dsp::param_change(Vocoder::LiveCh, ch)}
                                                : std::vector<dsp::Event>{};
      const float now = rms(r.block(x.data() + off, ev, right.data() + off));
      peak = b >= 60 && now > peak ? now : peak;
    }
    return peak;
  };
  CHECK(level(0.0f, zero) > 1.0e-3f);  // L: the left channel through itself, whatever the right one holds
  CHECK(level(1.0f, zero) < 1.0e-5f);  // R and a silent right channel: no carrier, no sound
  CHECK(level(1.0f, x) > 1.0e-3f);     // R and a tone on the right: the voice speaks through it
}

TEST_CASE("CarSrc LIVE: GATE cuts it like any carrier") {
  const auto x = tone(4096, 300.0f, 0.4f);
  Rig g;
  (void)run(g, x, 300, {dsp::param_change(Vocoder::CarSrc, 7.0f), dsp::param_change(Vocoder::Gate, 2.0f)});  // 1/8 at 120: 125 ms open, 125 shut
  float lo = 1.0e9f, hi = 0.0f;
  for (unsigned b = 0; b < 400u; ++b) {  // the followers settled, a block at a time over two whole steps
    const float r = rms(g.block(x.data() + (static_cast<std::size_t>(b) * 64u) % (x.size() - 64u)));
    lo = r < lo ? r : lo;
    hi = r > hi ? r : hi;
  }
  CHECK(hi > 1.0e-3f);
  CHECK(lo < 0.1f * hi);
}
