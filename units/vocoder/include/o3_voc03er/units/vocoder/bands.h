// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Bands: a 16-band channel vocoder. Band centres are 125 x 2^(0.4 i) Hz, i = 0..15 (125 Hz to 8 kHz, 2.5 bands an octave). Per band: two
// cascaded RBJ bandpass biquads (Q 2.3 each) on the modulator, a rectifier and a one-pole follower (attack 1 ms, release 15 ms in the middle of the
// range, graded across the bands: slower at the bottom, faster at the top, see set_envelope), and two cascaded bandpass biquads on the carrier whose output is scaled by the follower and summed. Two
// cascaded biquads at Q 2.3 are about 0.28 octave wide at -3 dB, narrower than the 0.4-octave spacing, and neighbouring bands cancel at the crossover unless the synthesis polarity of alternate bands is flipped (syn_gain_): with the flip the swept sum is flat within +-1 dB from 200 Hz to 6 kHz, without it 16 dB of ripple (tests/vocoder_test.cpp).
//
// Front end on the analysis path only (the dry modulator is the caller's): an 80 Hz high-pass, a slow AGC (the level is a mean-abs
// follower, attack 50 ms, release 500 ms; the gain is target / level, per block, ramped over the block, clamped to 0.05..20, fading to 1 as the level falls from 0.008 to 0.002) that is gated:
// below -54 dB the gain goes to 1, so a noise floor is never lifted, then a first-order pre-emphasis so consonants survive.
//
// Unvoiced path: when (followers of bands 13..15) / (sum of all followers) passes 0.35 (closes under 0.28) and the AGC level is above its
// gate, white noise from the shared NoiseSource is added to the carrier of bands 11..15. The decision is per block, the mix is a per-sample
// ramp of 5 ms, so it does not click. The noise is multiplied by the carrier's gate (Carrier::render's `gate`, passed to process), so with no
// note held there is no noise; without a gate (nullptr) the noise is not gated.
//
// Half rate: bands 0..12 (125 Hz to 3.5 kHz) run at fs/2 = 22.05 kHz between a halfband decimator on the pre-emphasised modulator, one on the
// carrier (and one on the unvoiced noise while it is live), and a halfband interpolator on the sum of those 13 bands; bands 13..15 stay at full
// rate. A halfband is the textbook polyphase pair of first-order allpass sections in z^-2, H = (A0(z^2) + z^-1 A1(z^2)) / 2, coefficients 0.127 and
// 0.566 (designed for passband 0..0.105 fs flat to 1e-6 dB, stopband from 0.395 fs at -70 dB). The half-rate bands' biquads are designed for
// 22.05 kHz, so centres and bandwidths are unchanged; the followers update once a half-rate sample, as the full-rate ones update every second sample,
// so their time constants are unchanged. The band biquads' Q is scaled by cos(w) so the bilinear warping at twice the angle keeps the -3 dB widths.
// The halfbands delay the half-rate group by about 3 full-rate samples more than the bands left at full rate; those three are delayed by kFullDelay
// samples before the sum (without it the summed ripple was 3.1 dB, against 1.4 dB at full rate). The sample phase (odd or even) and the interpolator's second output carry across calls: any frame count works.
//
// FORMANT and THRU (V2f): see set_formant and set_thru. FORMANT 0 runs the loop above unchanged; any other value runs two passes (all followers,
// then the carrier at a gain ramped across the block), and one more block after returning to 0, so the gains never jump.
//
// Real time: coefficients are computed in prepare() and set_envelope(), never in process(). process() runs one band at a time over the
// block, so the biquad and follower state lives in registers; no virtual calls, no branches on the signal in the inner loop. A NaN or Inf
// in the modulator is replaced by 0 before it reaches any state. State below 1e-15 is flushed to 0 at the end of each block (no denormals).
//
// Cost: V2's loop measured 97 cycles a band-sample, 38 % of a block (docs/PLAN.md, section 4b). After V2c, about 47 by its instructions [est].

#include <o3_voc03er/units/vocoder/carrier.h>
#include <o3_voc03er/units/vocoder/halfband.h>

#include <cstdint>

namespace o3_voc03er {

class Bands {
 public:
  static constexpr unsigned kBands = 16;
  static constexpr std::uint32_t kMaxFrames = 64;

  void prepare(float sample_rate) noexcept;  // coefficients, clears all state
  void reset() noexcept;                     // clears state only
  void restart() noexcept;                   // the same, keeping the AGC: its estimate of the input beats unity gain after a pause
  // Follower times: a geometric gradient across the bands. Band 0 has the low release, band 7.5 (the middle of the range) the release, band 15
  // the release squared over the low release; the attack follows the same factor.
  void set_envelope(float attack_ms, float release_ms, float low_release_ms) noexcept;
  float attack_ms(unsigned band) const noexcept;   // the time constant a band has now (tests, meters)
  float release_ms(unsigned band) const noexcept;
  // FORMANT: the spectral envelope moved up (+) or down (-) relative to the carrier, -12..12 semitones (0.4 octave a band: st / 4.8 bands).
  void set_formant(float semitones) noexcept;
  // THRU: 0..1, the high-passed modulator added to the output, scaled by the followers of bands 13..15 (BANDS only).
  void set_thru(float level) noexcept;
  void set_noise_source(NoiseSource* src) noexcept { noise_src_ = src; }  // null: the bands' own
  float unvoiced() const noexcept { return uv_; }  // 0..1, the noise mix now
  // Test seam: every follower holds `level` (coefficients 0), so the synthesis bands alone can be swept. Not for the unit.
  void hold_envelopes(float level) noexcept;

  // out[i] = sum over bands of carrier_band * follower(modulator_band). `n` at most kMaxFrames. out must not alias the inputs.
  // `gate` (optional, n values 0..1) gates the unvoiced noise.
  void process(const float* modulator, const float* carrier, float* out, std::uint32_t n, const float* gate = nullptr) noexcept;

  float center_hz(unsigned band) const noexcept { return band < kBands ? center_[band] : 0.0f; }
  float envelope(unsigned band) const noexcept { return band < kBands ? env_[band] : 0.0f; }  // the follower, for tests and meters
  bool shifted() const noexcept { return formant_off_ != 0.0f || shifted_last_; }  // the next block takes the two-pass path (tests)

  // A finite sample, the sample itself, anything else 0.
  static float clean(float x) noexcept { return (x == x && x < 1.0e6f && x > -1.0e6f) ? x : 0.0f; }

 private:
  alignas(16) float b0_[kBands] = {};  // bandpass: b1 = 0, b2 = -b0
  alignas(16) float a1_[kBands] = {};
  alignas(16) float a2_[kBands] = {};
  alignas(16) float center_[kBands] = {};
  alignas(16) float env_[kBands] = {};
  alignas(16) float atk_[kBands] = {};  // follower coefficients a band, for an update every second sample
  alignas(16) float rel_[kBands] = {};
  alignas(16) float rect_gain_[kBands] = {};  // kRectToPeak x b0^2: the rectifier's gain (the sections run with b0 folded out)
  alignas(16) float syn_gain_[kBands] = {};   // +-1 (the synthesis polarity) x b0^2
  // Two biquads, n1 and n2 each, as -s/b0 of the transposed form (b0 folded out, see process): up to 1/b0^2 = 7e4 times the plain state.
  alignas(16) float mod_state_[kBands][4] = {};
  alignas(16) float car_state_[kBands][4] = {};
  Halfband dec_mod_, dec_car_, dec_uv_, interp_;
  static constexpr unsigned kFullDelay = 3;  // samples the full-rate bands' sum is delayed by
  float fr_dly_[kFullDelay] = {};
  // FORMANT other than 0 (and the block after it) runs process_shifted(): all followers first, then the carrier sections at a gain ramped
  // across the block. gain_prev_ is the gain each synthesis band ended the last block on in both paths (the follower, in the merged one).
  void process_shifted(const float* pre, const float* md, const float* cd, const float* cd_uv, const float* carrier, const float* carrier_uv,
                       bool uv_live, float* sh, float* fr, float* out, float* tgt, std::uint32_t n, std::uint32_t nh) noexcept;
  alignas(16) float gain_prev_[kBands] = {};
  float formant_off_ = 0.0f;  // bands, st / 4.8; written by set_formant, read by process (the render context calls both)
  float thru_ = 0.0f;
  float thru_s_ = 0.0f;       // the THRU scale the last block ended on
  bool shifted_last_ = false;
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
  bool fol_odd_ = false;  // sample phase for the followers and the halfbands: the next sample has an odd index (counted from reset)
  NoiseSource own_noise_;
  NoiseSource* noise_src_ = nullptr;
};

static_assert(alignof(Bands) >= 16, "the state arrays are 16-byte aligned");

}  // namespace o3_voc03er
