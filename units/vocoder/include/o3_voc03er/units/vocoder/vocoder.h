// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// VOCODER: the Instrument unit. `in_l` is the modulator (`in_r` is unused for now), notes play the Carrier, both go through Bands, the
// result (plus the dry modulator, when asked) leaves mono on both planes through a soft clip whose peak is below 1.0.
//
// Parameters (the id is the index in the table: append, never insert):
//   0 Wave     0 SAW, 1 PULSE
//   1 Noise    0..1: the carrier's oscillators fade to white noise (gated by the held notes)
//   2 Attack   ms, 1..500: the carrier voices' attack
//   3 Release  ms, 5..2000: the carrier voices' release
//   4 Dry      0..1: the dry modulator mixed into the output (default 0)
//   5 Level    dB, -48..+6
//   6 Detune   cents, 0..20 (default 8): the second oscillator of each carrier voice, sharp of the first
//   7 Mode     0 BANDS, 1 TALK (default BANDS): the Carrier goes through Bands or through Talk (LPC talkbox); a 10 ms crossfade, and
//              only the active mode runs, except during the crossfade
//   8 Quality  LPC order 8..24 (default 16), Talk only
//   9 Freeze   0 off, 1 on: Talk stops updating its reflection coefficients (a held vowel)
//  10..21    Loops, three for each of the four loops n = 0..3 (ids 10 + 3n + field; Vocoder::loop_param):
//            +0 Rec   0 OFF, 1 ARM: records `in_l` into the loop. ARM starts at the next bar line of the transport (sample-exact, 4/4: four
//                     quarters, Transport::at); 0 stops at the next bar line. With the transport stopped both happen at once. A full loop
//                     (16 s) stops by itself. Recording empties the loop. The engine's stored value stays 1 after such a stop: send 0 then 1
//                     to record again (every Param event is delivered).
//            +1 Play  0 OFF, 1 ON: plays the loop at 1x from its first sample, from the next bar line (at once when the transport is
//                     stopped); OFF stops at once. An empty loop does not play.
//            +2 Role  0 MOD, 1 CAR: a playing MOD loop is the modulator in place of the live input (several are summed); a playing CAR loop is
//                     summed with what the internal synth plays. Both feed Bands or Talk as usual; DRY mixes the modulator that is used.
//            The loop memory comes from the app (bind_loops); without it the loops do nothing.
// The bands' followers are fixed (2 ms attack, 25 ms release, 40 ms on the three lowest bands; Bands::set_envelope).
//
// Cost [est]: Bands 58,400 a 64-frame block (front end and unvoiced path included) + Carrier up to 30,720 + mixing 1,300: 90,400 a
// block, 35 % of the 261,224 a block has at 44.1 kHz. TALK replaces Bands: Talk about 16,600 at order 16 (talk.h) + the same Carrier
// and mixing. Not measured on a unit.

#include <o3_voc03er/units/vocoder/bands.h>
#include <o3_voc03er/units/vocoder/carrier.h>
#include <o3_voc03er/units/vocoder/loop.h>
#include <o3_voc03er/units/vocoder/talk.h>

#include <o3sdk/dsp/smooth.h>
#include <o3sdk/dsp/unit.h>

#include <atomic>
#include <cstdint>

namespace o3_voc03er {

class Vocoder final : public o3sdk::dsp::Unit {
 public:
  enum Param : std::uint16_t { Wave, Noise, Attack, Release, Dry, Level, Detune, Mode, Quality, Freeze,
                            Loop1Rec, Loop1Play, Loop1Role, Loop2Rec, Loop2Play, Loop2Role, Loop3Rec, Loop3Play, Loop3Role, Loop4Rec, Loop4Play, Loop4Role,
                            kParamCount };
  enum class LoopField : std::uint16_t { Rec, Play, Role };
  static constexpr std::uint16_t loop_param(unsigned loop, LoopField f) noexcept {
    return static_cast<std::uint16_t>(Loop1Rec + 3u * loop + static_cast<unsigned>(f));
  }

  // What a loop is doing, for the screen and the console: written by the render context once a block, read by the main loop (one atomic
  // word per loop, so a read is never torn).
  struct LoopStatus {
    enum class Rec : std::uint8_t { Idle, Armed, Recording, Stopping };  // Armed: waiting for the bar line; Stopping: until the bar line
    Rec rec = Rec::Idle;
    bool playing = false;
    bool pending = false;  // Play asked, waiting for the bar line
    bool carrier = false;  // the role
    std::uint32_t length = 0;  // samples recorded so far (while recording) or in the loop
  };
  LoopStatus loop_status(unsigned loop) const noexcept;
  // Main loop, before the engine starts: the loop memory, Loop::kCount * per_loop int16 (per_loop up to Loop::kMaxSamples), 2-byte aligned,
  // that outlives the unit (the app's O3SDK_BIG array). Not touched by prepare().
  void bind_loops(std::int16_t* mem, std::uint32_t per_loop) noexcept { loops_.bind(mem, per_loop); }
  const Loop& loops() const noexcept { return loops_; }

  const o3sdk::dsp::UnitInfo& info() const noexcept override;
  void prepare(float sample_rate) noexcept override;
  void process(const o3sdk::dsp::ProcessArgs& args) noexcept override;

  const Carrier& carrier() const noexcept { return carrier_; }
  const Bands& bands() const noexcept { return bands_; }
  const Talk& talk() const noexcept { return talk_; }

 private:
  void apply(const o3sdk::dsp::Event& e) noexcept;
  void set(std::uint16_t id, float v) noexcept;
  void loop_param_event(std::uint16_t id, float v, std::uint32_t frame) noexcept;
  void run_loops(const o3sdk::dsp::Transport& t, std::uint32_t n, float* mod, float* car) noexcept;
  void publish_loops() noexcept;

  // One loop's control state (render context only; the main loop reads status_).
  struct Slot {
    enum class Rec : std::uint8_t { Idle, Armed, Recording };
    Rec rec = Rec::Idle;
    bool want_rec = false;    // the Rec parameter; false while Recording means stop at the bar line
    bool want_play = false;   // Play asked and not started yet
    bool carrier = false;
    std::uint16_t req = 0;    // the frame of the event that asked, this block only (0 afterwards): a bar line before it does not count
  };

  Carrier carrier_;
  Bands bands_;
  Talk talk_;
  NoiseSource noise_;
  o3sdk::dsp::Ramp level_;
  o3sdk::dsp::Ramp dry_;
  o3sdk::dsp::Ramp talk_mix_;  // 0 Bands .. 1 Talk
  Loop loops_;
  Slot slot_[Loop::kCount];
  std::atomic<std::uint32_t> status_[Loop::kCount] = {};  // packed LoopStatus: single writer (the render context), word-sized reads
};

}  // namespace o3_voc03er
