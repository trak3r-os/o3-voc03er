// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Bands: a 16-band channel vocoder. Bands are log-spaced from 100 Hz to 8 kHz. Per band: two cascaded bandpass biquads on the modulator
// (after a first-order pre-emphasis, so consonants survive), a rectifier and a one-pole follower with separate attack and release, and two
// cascaded bandpass biquads on the carrier whose output is scaled by the follower and summed. A plain class: no engine knowledge.
//
// Real time: coefficients are computed in prepare() and set_envelope(), never in process(). process() runs one band at a time over the
// block, so the biquad and follower state lives in registers; no virtual calls, no branches on the signal in the inner loop. A NaN or Inf
// in the modulator is replaced by 0 before it reaches any state. State below 1e-15 is flushed to 0 at the end of each block (no denormals).
//
// Cost [est]: 4 biquads (4 mul, 4 add each) + a follower (about 6 ops) a band a sample, about 55 cycles x 16 = 870 cycles a sample on the
// Cortex-M4F, 56,000 a 64-frame block (21 % of the 261,224 it has). Not measured on a unit (o3-sdk skill rt-optimize).

#include <cstdint>

namespace o3_voc03er {

class Bands {
 public:
  static constexpr unsigned kBands = 16;
  static constexpr std::uint32_t kMaxFrames = 64;

  void prepare(float sample_rate) noexcept;  // coefficients, clears all state
  void reset() noexcept;                     // clears state only
  void set_envelope(float attack_ms, float release_ms) noexcept;

  // out[i] = sum over bands of carrier_band * follower(modulator_band). `n` at most kMaxFrames. out must not alias the inputs.
  void process(const float* modulator, const float* carrier, float* out, std::uint32_t n) noexcept;

  float center_hz(unsigned band) const noexcept { return band < kBands ? center_[band] : 0.0f; }
  float envelope(unsigned band) const noexcept { return band < kBands ? env_[band] : 0.0f; }  // the follower, for tests and meters

  // A finite sample, the sample itself, anything else 0.
  static float clean(float x) noexcept { return (x == x && x < 1.0e6f && x > -1.0e6f) ? x : 0.0f; }

 private:
  alignas(16) float b0_[kBands] = {};  // bandpass: b1 = 0, b2 = -b0
  alignas(16) float a1_[kBands] = {};
  alignas(16) float a2_[kBands] = {};
  alignas(16) float center_[kBands] = {};
  alignas(16) float env_[kBands] = {};
  alignas(16) float mod_state_[kBands][4] = {};  // two biquads: s1, s2 each
  alignas(16) float car_state_[kBands][4] = {};
  float fs_ = 44100.0f;
  float attack_ = 0.1f;   // one-pole coefficients
  float release_ = 0.001f;
  float pre_prev_ = 0.0f;
};

static_assert(alignof(Bands) >= 16, "the state arrays are 16-byte aligned");

}  // namespace o3_voc03er
