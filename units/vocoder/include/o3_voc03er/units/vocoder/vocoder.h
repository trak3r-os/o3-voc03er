// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// VOCODER: the Instrument unit. `in_l` is the modulator (`in_r` is unused for now), notes play the Carrier, both go through Bands, the
// result (plus the dry modulator, when asked) leaves mono on both planes through a soft clip whose peak is below 1.0.
//
// Parameters (the id is the index in the table: append, never insert; a MODE parameter will be appended):
//   0 Wave     0 SAW, 1 PULSE
//   1 Noise    0..1: the carrier's oscillators fade to white noise (gated by the held notes)
//   2 Attack   ms, 1..500: the carrier voices' attack
//   3 Release  ms, 5..2000: the carrier voices' release
//   4 Dry      0..1: the dry modulator mixed into the output (default 0)
//   5 Level    dB, -48..+6
//   6 Detune   cents, 0..20 (default 8): the second oscillator of each carrier voice, sharp of the first
// The bands' followers are fixed (2 ms attack, 25 ms release, 40 ms on the three lowest bands; Bands::set_envelope).
//
// Cost [est]: Bands 58,400 a 64-frame block (front end and unvoiced path included) + Carrier up to 30,720 + mixing 1,300: 90,400 a
// block, 35 % of the 261,224 a block has at 44.1 kHz. Not measured on a unit.

#include <o3_voc03er/units/vocoder/bands.h>
#include <o3_voc03er/units/vocoder/carrier.h>

#include <o3sdk/dsp/smooth.h>
#include <o3sdk/dsp/unit.h>

#include <cstdint>

namespace o3_voc03er {

class Vocoder final : public o3sdk::dsp::Unit {
 public:
  enum Param : std::uint16_t { Wave, Noise, Attack, Release, Dry, Level, Detune, kParamCount };

  const o3sdk::dsp::UnitInfo& info() const noexcept override;
  void prepare(float sample_rate) noexcept override;
  void process(const o3sdk::dsp::ProcessArgs& args) noexcept override;

  const Carrier& carrier() const noexcept { return carrier_; }
  const Bands& bands() const noexcept { return bands_; }

 private:
  void apply(const o3sdk::dsp::Event& e) noexcept;
  void set(std::uint16_t id, float v) noexcept;

  Carrier carrier_;
  Bands bands_;
  NoiseSource noise_;
  o3sdk::dsp::Ramp level_;
  o3sdk::dsp::Ramp dry_;
};

}  // namespace o3_voc03er
