// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Bands: a 16-band channel vocoder. Band centres are 125 x 2^(0.4 i) Hz, i = 0..15 (125 Hz to 8 kHz, 2.5 bands an octave). Per band: two
// cascaded RBJ bandpass biquads (Q 2.3 each) on the modulator, a rectifier and a one-pole follower (attack 2 ms, release 25 ms, 40 ms on
// the three lowest bands), and two cascaded bandpass biquads on the carrier whose output is scaled by the follower and summed. Two
// cascaded biquads at Q 2.3 are about 0.28 octave wide at -3 dB, narrower than the 0.4-octave spacing, and neighbouring bands cancel at the crossover unless the synthesis polarity of alternate bands is flipped (sign_): with the flip the swept sum is flat within +-1 dB from 200 Hz to 6 kHz, without it 16 dB of ripple (tests/vocoder_test.cpp).
//
// Front end on the analysis path only (the dry modulator is the caller's): an 80 Hz high-pass, a slow AGC (the level is a mean-abs
// follower, attack 5 ms, release 100 ms; the gain is target / level, per block, ramped over the block, clamped to 0.05..20, fading to 1 as the level falls from 0.008 to 0.002) that is gated:
// below -54 dB the gain goes to 1, so a noise floor is never lifted, then a first-order pre-emphasis so consonants survive.
//
// Unvoiced path: when (followers of bands 13..15) / (sum of all followers) passes 0.35 (closes under 0.28) and the AGC level is above its
// gate, white noise from the shared NoiseSource is added to the carrier of bands 11..15. The decision is per block, the mix is a per-sample
// ramp of 5 ms, so it does not click. The noise is multiplied by the carrier's gate (Carrier::render's `gate`, passed to process), so with no
// note held there is no noise; without a gate (nullptr) the noise is not gated.
//
// Real time: coefficients are computed in prepare() and set_envelope(), never in process(). process() runs one band at a time over the
// block, so the biquad and follower state lives in registers; no virtual calls, no branches on the signal in the inner loop. A NaN or Inf
// in the modulator is replaced by 0 before it reaches any state. State below 1e-15 is flushed to 0 at the end of each block (no denormals).
//
// Cost [est]: 4 biquads (4 mul, 4 add each) + a follower (about 6 ops) a band a sample, about 55 cycles x 16 = 870 cycles a sample on the
// Cortex-M4F, 56,000 a 64-frame block; the front end (high-pass, level, gain, pre-emphasis, about 25 cycles a sample) 1,600 and the
// unvoiced mix (noise, ramp, five bands' adds, about 12 cycles a sample) 800: 58,400 a block, 22 % of the 261,224 it has (the review's
// figure). Not measured on a unit (o3-sdk skill rt-optimize).

#include <o3_voc03er/units/vocoder/carrier.h>

#include <cstdint>

namespace o3_voc03er {

class Bands {
 public:
  static constexpr unsigned kBands = 16;
  static constexpr std::uint32_t kMaxFrames = 64;

  void prepare(float sample_rate) noexcept;  // coefficients, clears all state
  void reset() noexcept;                     // clears state only
  void set_envelope(float attack_ms, float release_ms, float low_release_ms) noexcept;  // the low release is for bands 0..2
  void set_noise_source(NoiseSource* src) noexcept { noise_src_ = src; }  // null: the bands' own
  float unvoiced() const noexcept { return uv_; }  // 0..1, the noise mix now
  // Test seam: every follower holds `level` (coefficients 0), so the synthesis bands alone can be swept. Not for the unit.
  void hold_envelopes(float level) noexcept;

  // out[i] = sum over bands of carrier_band * follower(modulator_band). `n` at most kMaxFrames. out must not alias the inputs.
  // `gate` (optional, n values 0..1) gates the unvoiced noise.
  void process(const float* modulator, const float* carrier, float* out, std::uint32_t n, const float* gate = nullptr) noexcept;

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
  alignas(16) float atk_[kBands] = {};  // follower coefficients a band
  alignas(16) float rel_[kBands] = {};
  alignas(16) float sign_[kBands] = {};  // +-1: the synthesis polarity
  alignas(16) float mod_state_[kBands][4] = {};  // two biquads: s1, s2 each
  alignas(16) float car_state_[kBands][4] = {};
  float fs_ = 44100.0f;
  float pre_prev_ = 0.0f;
  float hp_b0_ = 0.0f, hp_b1_ = 0.0f, hp_b2_ = 0.0f, hp_a1_ = 0.0f, hp_a2_ = 0.0f;  // 80 Hz high-pass
  float hp_s1_ = 0.0f, hp_s2_ = 0.0f;
  float level_ = 0.0f;  // AGC: mean-abs follower of the high-passed modulator
  float lvl_att_ = 0.0f, lvl_rel_ = 0.0f;
  float gain_ = 1.0f;
  float uv_ = 0.0f;
  float uv_step_ = 0.0f;
  bool uv_open_ = false;
  NoiseSource own_noise_;
  NoiseSource* noise_src_ = nullptr;
};

static_assert(alignof(Bands) >= 16, "the state arrays are 16-byte aligned");

}  // namespace o3_voc03er
