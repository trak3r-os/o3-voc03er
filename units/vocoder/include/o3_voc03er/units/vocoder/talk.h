// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Talk: the LPC talkbox mode of the vocoder. The modulator's spectral envelope is estimated by linear prediction and imposed on the
// carrier by an all-pole lattice filter. Its DSP steps (pre-emphasis, Hann window, autocorrelation, Levinson-Durbin, reflection
// coefficient clamp, lattice, gain = sqrt(error), allpass paths that spread the carrier) follow mda Talkbox by Paul Kellett, used under
// its MIT option (THIRD_PARTY.md). The code is written here, as a streaming design, and stays GPL-3.0-or-later.
//
// Streaming: the lattice runs on every carrier sample, so the carrier is NOT delayed (mda filters a captured window and overlap-adds).
// The analysis only moves coefficients: every kHop = 384 samples (6 blocks of 64) it reads the last kWin = 768 modulator samples
// from a ring of 2048 floats (>= window + hop + block, so the window is stable while samples keep arriving: no snapshot copy), and
// the autocorrelation is sliced evenly over the 6 blocks by a cursor, Levinson-Durbin runs at the end of the sixth. The coefficients
// ramp sample by sample in the reflection domain (stable while |k| < 1), never in direct form.
//
// Numerics: r0 x 1.0001, Gaussian lag window (60 Hz), |k| <= 0.995, silence gate on r0. FREEZE stops k from updating (the gain
// still follows). An order change is applied at the next publish; the lattice runs the larger of the old and new order until the
// ramp has taken the extra stages to zero.
//
// Cost [est], order 16, per 64-sample block: autocorrelation and window 2,176 MAC x 3 = 6,528; Levinson 1,500 (last block of a hop);
// lattice 16 x 6 x 64 = 6,144; coefficient ramp 1,024; ring and pre-emphasis 512; allpass paths 640; gain 256 = about 16,600 cycles,
// 6.4 % of 261,224; about 8.5 % at order 24. Not measured on a unit.
// Memory: ring 8 KiB + window 3 KiB + Hann table 3 KiB, in the object (SRAM).

#include <cstdint>

namespace o3_voc03er {

class Talk {
 public:
  static constexpr std::uint32_t kMaxFrames = 64;
  static constexpr std::uint32_t kWin = 768;
  static constexpr std::uint32_t kHop = 384;
  static constexpr std::uint32_t kStep = 8;  // the coefficient ramp moves every kStep samples; a power of 2 dividing kHop
  static constexpr std::uint32_t kRing = 2048;
  static constexpr std::uint32_t kMinOrder = 8;
  static constexpr std::uint32_t kMaxOrder = 24;
  static constexpr float kMaxK = 0.995f;

  void prepare(float sample_rate) noexcept;  // tables, clears all state
  void reset() noexcept;                     // clears the state, keeps the settings (a mode switch into Talk)
  void set_order(std::uint32_t order) noexcept;  // clamped to 8..24; safe at any moment
  void set_freeze(bool on) noexcept { freeze_ = on; }

  // out[i] = the carrier shaped by the modulator's envelope. `n` at most kMaxFrames. out must not alias the inputs.
  void process(const float* mod, const float* car, float* out, std::uint32_t n) noexcept;

  // Test seam: multiply-adds of the last process() call (one addition a slice, not one a MAC), and the lattice's state.
  std::uint32_t macs_last_block() const noexcept { return macs_last_; }
  float reflection_target(std::uint32_t m) const noexcept { return kt_[m]; }  // m 1..kMaxOrder
  float reflection(std::uint32_t m) const noexcept { return k_[m]; }
  std::uint32_t order() const noexcept { return order_; }
  std::uint32_t lattice_order() const noexcept { return run_; }

 private:
  void analyse(std::uint32_t block) noexcept;
  void slice(std::uint32_t units) noexcept;
  void publish() noexcept;

  // Single thread: everything runs in the audio context (events are applied in process() order).
  alignas(8) float ring_[kRing];
  alignas(8) float win_[kWin];
  alignas(8) float hann_[kWin];
  alignas(8) float r_[kMaxOrder + 1];
  alignas(8) float lagw_[kMaxOrder + 1];
  alignas(8) float k_[kMaxOrder + 2];   // current reflection coefficients, index 1..order
  alignas(8) float kt_[kMaxOrder + 2];  // targets
  alignas(8) float dk_[kMaxOrder + 2];
  alignas(8) float b_[kMaxOrder + 2];   // lattice backward states

  std::uint32_t wpos_ = 0;
  std::uint32_t filled_ = 0;
  std::uint32_t block_ = 0;     // 0..5 in the hop
  std::uint32_t end_ = 0;       // ring index just past the analysis window
  std::uint32_t order_ = 16;    // requested
  std::uint32_t pa_ = 16;       // order of the analysis in progress
  std::uint32_t run_ = 16;      // lattice order running
  std::uint32_t tgt_order_ = 16;
  bool analysing_ = false;
  bool freeze_ = false;
  std::uint32_t stage_ = 0;     // cursor: 0 window, 1 lags
  std::uint32_t cpos_ = 0;
  std::uint32_t lag_ = 0;
  float acc_ = 0.0f;
  std::uint32_t total_ = 0;
  std::uint32_t done_ = 0;
  std::uint32_t ramp_left_ = 0;
  std::uint32_t macs_last_ = 0;
  float sumw2_ = 1.0f;
  float emph_prev_ = 0.0f;
  float gain_ = 0.0f, gain_t_ = 0.0f, dgain_ = 0.0f;
  float ap_[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // two allpass states before the lattice, two after
};

}  // namespace o3_voc03er
