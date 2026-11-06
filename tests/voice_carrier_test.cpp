// SPDX-License-Identifier: GPL-3.0-or-later
// CarSrc VOICE (E2): the synth carrier plays the pitch the singer sings, snapped by KEY and SCALE. A sung sine outside the scale gives the snapped
// note, silence lets it go after the unvoiced hold, held pads follow the voice, and with VOICE off nothing changes. Synthetic signals only; how
// it sounds, the onset lag and the octave errors on a real microphone are not tested here.
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
constexpr std::uint32_t kBlock = 64;

struct Rig {
  Vocoder v;
  std::vector<std::int16_t> mem;
  dsp::Transport tr;
  std::size_t pos = 0;
  float hz = 0.0f;  // the singer: 0 is silence
  Rig() : mem(static_cast<std::size_t>(Loop::kCount) * 8000u) {
    v.bind_loops(mem.data(), 8000u);
    v.prepare(kFs);
    tr.playing = false;
    tr.tempo_mbpm = 120'000;
  }
  // `blocks` blocks of the singer, the events landing in the first; returns the last block's output.
  std::vector<float> run(unsigned blocks, std::vector<dsp::Event> ev = {}) {
    std::vector<float> out(kBlock), r(kBlock), in(kBlock);
    for (unsigned b = 0; b < blocks; ++b) {
      for (std::uint32_t i = 0; i < kBlock; ++i, ++pos) {
        in[i] = hz > 0.0f ? 0.4f * std::sin(kPi2 * std::fmod(hz * static_cast<float>(pos) / kFs, 1.0f)) : 0.0f;
      }
      dsp::ProcessArgs a{};
      a.events = b == 0 ? ev.data() : nullptr;
      a.event_count = b == 0 ? static_cast<std::uint32_t>(ev.size()) : 0u;
      a.in_l = in.data();
      a.in_r = in.data();
      a.out_l = out.data();
      a.out_r = r.data();
      a.frames = kBlock;
      tr.frames = kBlock;
      a.transport = &tr;
      v.process(a);
    }
    return out;
  }
};

float rms(const std::vector<float>& v) {
  float s = 0.0f;
  for (float f : v) s += f * f;
  return std::sqrt(s / static_cast<float>(v.size()));
}

// 233 Hz is A#3 (58), outside C major pentatonic (C D E G A): the nearest note is A3 (57).
constexpr float kSung = 233.08f;

std::vector<dsp::Event> voice_on() {
  return {dsp::param_change(Vocoder::CarSrc, 8.0f), dsp::param_change(Vocoder::Speed, 0.0f), dsp::param_change(Vocoder::Key, 0.0f),
          dsp::param_change(Vocoder::Scale, 3.0f)};
}

}  // namespace

TEST_CASE("VOICE: a sung sine outside the scale gives the snapped note") {
  static_assert(Vocoder::kCarVoice == 8, "append only");
  Rig r;
  CHECK(r.v.info().params[Vocoder::CarSrc].max == 8.0f);
  r.hz = kSung;
  r.run(300, voice_on());  // about 435 ms: the detector's latency is 11 to 48 ms
  CHECK(r.v.carrier().holds(57));
  CHECK_FALSE(r.v.carrier().holds(58));  // out of the scale
  CHECK_FALSE(r.v.carrier().holds(48));
  CHECK(rms(r.run(8)) > 0.001f);  // it sounds
}

TEST_CASE("VOICE: silence lets the note go after the unvoiced hold, not before") {
  Rig r;
  r.hz = kSung;
  r.run(300, voice_on());
  REQUIRE(r.v.carrier().holds(57));
  r.hz = 0.0f;
  r.run(4);  // about 6 ms: the hold (60 ms) bridges a consonant or a breath
  CHECK(r.v.carrier().holds(57));
  r.run(120);  // about 170 ms
  CHECK_FALSE(r.v.carrier().holds(57));
}

TEST_CASE("VOICE: held pads follow the voice by their intervals, none sounds without a sung note") {
  Rig r;
  r.hz = 0.0f;
  r.run(10, voice_on());
  r.run(2, {dsp::note_on(48, 100), dsp::note_on(52, 100), dsp::note_on(55, 100)});
  CHECK_FALSE(r.v.carrier().holds(48));  // the pads do not sound on their own
  CHECK_FALSE(r.v.carrier().holds(52));
  r.hz = kSung;
  r.run(300);
  CHECK(r.v.carrier().holds(57));  // the lowest held pad takes the sung note
  CHECK(r.v.carrier().holds(61));
  CHECK(r.v.carrier().holds(64));
  CHECK_FALSE(r.v.carrier().holds(48));
  r.run(2, {dsp::note_off(52), dsp::note_off(55)});
  CHECK(r.v.carrier().holds(57));
  CHECK_FALSE(r.v.carrier().holds(61));
  CHECK_FALSE(r.v.carrier().holds(64));
}

TEST_CASE("VOICE off: the pads sound as before, the singer does not play") {
  Rig r;
  r.hz = kSung;
  r.run(300, {dsp::param_change(Vocoder::CarSrc, 1.0f), dsp::param_change(Vocoder::Scale, 3.0f), dsp::note_on(48, 100)});
  CHECK(r.v.carrier().holds(48));
  CHECK_FALSE(r.v.carrier().holds(57));
  // and leaving VOICE hands the pads back
  Rig q;
  q.hz = kSung;
  q.run(300, voice_on());
  q.run(2, {dsp::note_on(48, 100)});
  q.run(2, {dsp::param_change(Vocoder::CarSrc, 1.0f)});
  CHECK(q.v.carrier().holds(48));
  CHECK_FALSE(q.v.carrier().holds(57));
}
