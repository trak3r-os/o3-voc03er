// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Lpc: the analysis half of Talk, on its own so that a second user can run it (the TRACT mode's live voice, mouth_live.h). It takes the
// modulator at the low rate (fs/4: the caller decimates), one sample at a time. Its steps are Talk's, unchanged: pre-emphasis 0.9, a ring of
// 512 floats, the asymmetric window (sin^2 over the first 80 %, cos^2 over the last 20 %) of kWin = 256 samples, an autocorrelation sliced
// evenly over the hop of kHop = 32 samples (a share every four samples, four lags a pass, window and lag zeros so every lag runs the same
// length), and, once a hop, Levinson-Durbin on the r0 x 1.0001, Gaussian-lag-windowed (kLagHz = 60) autocorrelation with |k| <= kMaxK. Talk's
// numerics are in talk.h; moving them here changed no sample of Talk's output (tests/talk_test.cpp pins the digests).
//
// Cost [est]: the autocorrelation about 16 multiply-adds per four samples and lag pass, 3.5 cycles each (talk.h): about 7,000 cycles a block
// of 64 frames at order 12, the same whoever runs it. Levinson at order 12: about 300 multiply-adds and 12 divisions a hop.
// Memory: ring 2 KiB + window 1 KiB and 80 B of zeros + window table 1 KiB + lag window and r about 200 B, in the object.
// One context: everything runs in the audio context.

#include <cstdint>

namespace o3_voc03er {

class Lpc {
 public:
  static constexpr std::uint32_t kWin = 256;   // low-rate samples
  static constexpr std::uint32_t kHop = 32;    // low-rate samples
  static constexpr std::uint32_t kRing = 512;
  static constexpr std::uint32_t kMaxOrder = 18;
  static constexpr std::uint32_t kQuads = kWin / 4u;                     // the analysis' unit, four samples
  static constexpr std::uint32_t kLagTop = (kMaxOrder + 3u) / 4u * 4u;   // the last lag a pass of four reaches
  static constexpr float kMaxK = 0.995f;

  void prepare(float sample_rate) noexcept;         // the window and the lag window; then reset(12)
  void reset(std::uint32_t order) noexcept;         // clears the state; `order` is the order of an analysis before the first push sets it

  // One low-rate sample m, analysed at `order` (1..kMaxOrder; read at the start of each hop). Returns true when this sample ends a hop that
  // was analysing: r is ready, call power() and, unless silent, levinson(). false for every other sample and before the window has filled.
  bool push(float m, std::uint32_t order) noexcept;

  std::uint32_t order() const noexcept { return pa_; }   // the order of the analysis in progress (and of r)
  // The window's mean power, r0 / sum(w^2). valid after push() returned true.
  float power() const noexcept { return r_[0] / sumw2_; }
  float sumw2() const noexcept { return sumw2_; }
  float r0() const noexcept { return r_[0]; }            // r0 as it is now: boosted once levinson() has run
  // Levinson-Durbin at order(): kk[1..order()] = the reflection coefficients (|k| <= kMaxK; the stages left after an error floor stay 0), and
  // the prediction error is returned. Modifies r (the boost and the lag window): call it once a hop, and only when power() is not silence.
  // kk needs kMaxOrder + 2 floats, zeroed by the caller.
  float levinson(float* kk) noexcept;

  void flush_state() noexcept;   // denormals: the pre-emphasis' state, once a block

  // Multiply-adds since clear_macs(), one addition a slice and 2 m a stage of Levinson (a test seam: the cost counter Talk reports).
  std::uint32_t macs() const noexcept { return macs_; }
  void clear_macs() noexcept { macs_ = 0; }

 private:
  void slice(std::uint32_t units) noexcept;

  alignas(8) float ring_[kRing];
  alignas(8) float win_[kWin + kLagTop];  // the window, then zeros that are never written: a pass reads up to kLagTop past it
  alignas(8) float wtab_[kWin];
  alignas(8) float r_[kLagTop + 1];
  alignas(8) float lagw_[kMaxOrder + 1];
  float acc_[4] = {};
  std::uint32_t wpos_ = 0;      // low-rate samples written (wraps with the ring mask)
  std::uint32_t filled_ = 0;
  std::uint32_t hop_pos_ = 0;   // 0..kHop-1
  std::uint32_t end_ = 0;       // ring index just past the analysis window
  std::uint32_t pa_ = 12;       // order of the analysis in progress
  bool analysing_ = false;
  std::uint32_t stage_ = 0;     // cursor: 0 window, 1 lags
  std::uint32_t cpos_ = 0;      // in units of four samples
  std::uint32_t lag_ = 0;       // the first of the four lags of this pass
  std::uint32_t total_ = 0;
  std::uint32_t done_ = 0;
  std::uint32_t macs_ = 0;
  float sumw2_ = 1.0f;
  float emph_prev_ = 0.0f;
};

static_assert(alignof(Lpc) >= 8u, "the buffers are alignas(8)");
static_assert(Lpc::kWin + Lpc::kHop <= Lpc::kRing, "the window must stay stable through a hop");
static_assert((Lpc::kRing & (Lpc::kRing - 1u)) == 0u, "the ring wraps with a mask");
static_assert(Lpc::kWin % 4u == 0u && Lpc::kHop % 4u == 0u, "the analysis goes four samples at a time, and its share every four low-rate samples");

}  // namespace o3_voc03er
