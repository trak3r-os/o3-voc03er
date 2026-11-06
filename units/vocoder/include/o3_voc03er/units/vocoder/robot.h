// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// Internal to the vocoder unit. ROBOT is BANDS (the same Bands engine, the same wet sum) followed by this fast compressor, so a voice that
// comes and goes in level comes out at a nearly flat level, the "robot voice" of a leveled vocoder. A feed-forward peak compressor with the
// gain computed at control rate: every kSeg samples the peak of the block drives a one-pole envelope (attack kAttackMs, release kReleaseMs),
// the gain is (kThreshold / envelope)^(1 - 1/ratio) above the threshold (ratio 4: the exponent 0.75, from two square roots) times the
// make-up, and it ramps linearly across the block. Per sample: one abs, one compare, one add and one multiply; per block: two divides, three
// square roots. No libm, no heap, no state beyond four floats. It works on the wet sum before the unit's kWetGain, so the levels below are
// that sum's own.

namespace o3_voc03er {

class RobotComp {
 public:
  static constexpr std::uint32_t kSeg = 16;       // control period in samples (0.36 ms at 44.1 kHz)
  static constexpr float kAttackMs = 1.0f;
  static constexpr float kReleaseMs = 50.0f;
  static constexpr float kThreshold = 0.0008f;    // peak of the wet sum (kWetGain 128 turns it into about 0.1 of full scale)
  static constexpr float kMakeup = 3.0f;          // at and below the threshold the signal gets this; above it the gain falls by 0.75 dB per dB

  void prepare(float sample_rate) noexcept;
  void reset() noexcept;
  void process(float* x, std::uint32_t n) noexcept;  // in place

 private:
  float att_ = 0.7f, rel_ = 0.999f;  // envelope coefficients per control step
  float env_ = 0.0f;                 // peak envelope, from the block peaks
  float gain_ = kMakeup;             // the gain at the end of the last block
};

}  // namespace o3_voc03er
