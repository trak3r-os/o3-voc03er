// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Talk: the LPC talkbox mode of the vocoder. The modulator's spectral envelope is estimated by linear prediction and imposed on the
// carrier by an all-pole lattice filter. Its DSP steps (pre-emphasis, window, autocorrelation, Levinson-Durbin, reflection coefficient
// clamp, lattice, gain = sqrt(error)) follow mda Talkbox by Paul Kellett, used under its MIT option (THIRD_PARTY.md). The code is written
// here, as a streaming design, and stays GPL-3.0-or-later.
//
// Rates: the analysis and the lattice run at fs/4 (11,025 Hz at 44.1 kHz): the modulator and the carrier each go through two halfband
// decimators (halfband.h, as Bands uses them), the lattice's output through two halfband interpolators back to fs. A voice's formants lie
// under 5 kHz, and at this rate a window of 256 samples is 23.2 ms and the lattice order that models them is 12 (Quality 16), not 16.
//
// Streaming: the lattice runs on every low-rate carrier sample, so the carrier is delayed only by the halfbands, about 9 samples (mda filters
// a captured window and overlap-adds).
// The analysis only moves coefficients: every kHop = 32 low-rate samples (2.9 ms = 128 frames; counted in low-rate samples, so any frame
// count works, and a process() call may produce none) it reads the last kWin = 256 pre-emphasised (0.9) modulator samples from a ring of
// 512 floats (>= window + hop, so the window is stable while samples keep arriving). The window is asymmetric, sin^2 over the first 80 %
// and cos^2 over the last 20 %, so the newest samples count and the onset is not smeared. The autocorrelation is sliced evenly over the
// hop by a cursor (a share every 4 low-rate samples), Levinson-Durbin runs at the end of the hop. The coefficients ramp linearly over the hop,
// one step per low-rate sample, in the reflection domain (stable while |k| < 1), never in direct form.
//
// The carrier gets a first difference (c - 0.2 c[-1]) before the lattice, which lifts its highs so the vowel's upper formants are not
// buried under the saw's own tilt.
//
// Breath: a real talkbox gets its sibilants from the mouth's own air. The modulator at full rate through a 2nd-order Butterworth
// high-pass at 5 kHz is added to the output, scaled by kBreath x (0.5 + 0.5 x thru) x open. `open` follows the carrier (1 while its
// peak over a call exceeds 1e-3, a linear ramp of 5 ms), so with no carrier sounding there is no breath.
//
// Level: the slow AGC Bands has (agc.h), measured on the decimated modulator, scales the lattice's gain at each publish and the breath, so a
// quiet microphone and a hot line sound alike and a change of MODE keeps the level. level_ survives reset(), as in Bands::restart, and a
// change of MODE hands it over (adopt_level): without that a hot input came in 20 dB too loud until the follower caught up.
//
// Numerics: r0 x 1.0001, Gaussian lag window (60 Hz at the low rate), |k| <= 0.995, silence gate on r0. FREEZE stops k from updating
// (the gain still follows). Quality 8..24 is the parameter's range; the lattice order is (q x 3 + 2) / 4 (16 gives 12, 8 gives 6,
// 24 gives 18). An order change is applied at the next publish; the lattice runs the larger of the old and new order until the ramp
// has taken the extra stages to zero. A NaN or Inf in either input is replaced by 0 on entry (the decimators are IIR).
//
// Cost: each stage of the lattice cost about 2,000 cycles a block on the unit in 6a19f36 (Quality 8 to 16 to 24, 8 notes and 4 loops: 147k,
// 160k, 171k), most of it the autocorrelation, whose one-lag dot product ran at about 8 cycles a multiply-add [est]. It now runs four lags in
// a pass, 8 loads and 16 multiplies and adds for 16 multiply-adds, about 3.5 cycles each [est, from the disassembly].
// Memory: ring 2 KiB + window 1 KiB and 80 B of zeros + window table 1 KiB, in the object (SRAM).

#include <o3_voc03er/units/vocoder/halfband.h>

#include <cstdint>

namespace o3_voc03er {

class Talk {
 public:
  static constexpr std::uint32_t kMaxFrames = 64;
  static constexpr std::uint32_t kWin = 256;   // low-rate samples
  static constexpr std::uint32_t kHop = 32;    // low-rate samples
  static constexpr std::uint32_t kRing = 512;
  static constexpr std::uint32_t kMinOrder = 8;   // Quality's limits
  static constexpr std::uint32_t kMaxOrder = 24;
  static constexpr std::uint32_t kMaxStages = 18;  // the lattice's order at Quality 24
  static constexpr std::uint32_t kQuads = kWin / 4u;  // the analysis' unit, four samples
  static constexpr std::uint32_t kLagTop = (kMaxStages + 3u) / 4u * 4u;  // the last lag a pass of four reaches
  static constexpr float kMaxK = 0.995f;

  // The lattice order Quality q runs.
  static constexpr std::uint32_t stages_for(std::uint32_t q) noexcept { return (q * 3u + 2u) / 4u; }

  void prepare(float sample_rate) noexcept;  // tables, clears all state
  void reset() noexcept;                     // clears the state, keeps the settings (a mode switch into Talk)
  void set_order(std::uint32_t order) noexcept;  // Quality, clamped to 8..24; safe at any moment
  void set_freeze(bool on) noexcept { freeze_ = on; }
  // The AGC's level, handed over by a change of MODE (see Bands::agc_level).
  float agc_level() const noexcept { return level_; }
  void adopt_level(float level) noexcept;
  void set_thru(float level) noexcept { thru_ = level > 1.0f ? 1.0f : (level > 0.0f ? level : 0.0f); }  // 0..1, the breath's strength

  // out[i] = the carrier shaped by the modulator's envelope. `n` at most kMaxFrames. out must not alias the inputs.
  void process(const float* mod, const float* car, float* out, std::uint32_t n) noexcept;

  // Test seam: multiply-adds of the last process() call (one addition a slice, not one a MAC), and the lattice's state.
  std::uint32_t macs_last_block() const noexcept { return macs_last_; }
  float reflection_target(std::uint32_t m) const noexcept { return kt_[m]; }  // m 1..kMaxStages
  float reflection(std::uint32_t m) const noexcept { return k_[m]; }
  std::uint32_t order() const noexcept { return order_; }  // Quality
  std::uint32_t lattice_order() const noexcept { return run_; }
  float breath_open() const noexcept { return open_; }

 private:
  void slice(std::uint32_t units) noexcept;
  void publish() noexcept;
  void tick(float m, float c, float& y) noexcept;  // one low-rate sample: analysis share, ramp, lattice

  // Single thread: everything runs in the audio context (events are applied in process() order).
  alignas(8) float ring_[kRing];
  alignas(8) float win_[kWin + kLagTop];  // the window, then zeros that are never written: a pass reads up to kLagTop past it
  alignas(8) float wtab_[kWin];
  alignas(8) float r_[kLagTop + 1];
  alignas(8) float lagw_[kMaxStages + 1];
  alignas(8) float k_[kMaxStages + 2];   // current reflection coefficients, index 1..order
  alignas(8) float kt_[kMaxStages + 2];  // targets
  alignas(8) float dk_[kMaxStages + 2];
  alignas(8) float b_[kMaxStages + 2];   // lattice backward states
  Halfband dec_mod_[2], dec_car_[2], interp_[2];  // stage 1 at fs -> fs/2, stage 2 fs/2 -> fs/4

  std::uint32_t wpos_ = 0;      // low-rate samples written (wraps with the ring mask)
  std::uint32_t filled_ = 0;
  std::uint32_t hop_pos_ = 0;   // 0..kHop-1
  std::uint32_t end_ = 0;       // ring index just past the analysis window
  std::uint32_t order_ = 16;    // requested (Quality)
  std::uint32_t pa_ = 12;       // order of the analysis in progress
  std::uint32_t run_ = 12;      // lattice order running
  std::uint32_t tgt_order_ = 12;
  bool analysing_ = false;
  bool freeze_ = false;
  bool odd1_ = false, odd2_ = false;  // sample phases of the two halfband stages (the next input has an odd index), as Bands::fol_odd_
  std::uint32_t stage_ = 0;     // cursor: 0 window, 1 lags
  std::uint32_t cpos_ = 0;      // in units of four samples
  std::uint32_t lag_ = 0;       // the first of the four lags of this pass
  float acc_[4] = {};
  std::uint32_t total_ = 0;
  std::uint32_t done_ = 0;
  std::uint32_t ramp_left_ = 0;
  std::uint32_t macs_last_ = 0;
  float sumw2_ = 1.0f;
  float emph_prev_ = 0.0f;
  float car_prev_ = 0.0f;
  float gain_ = 0.0f, gain_t_ = 0.0f, dgain_ = 0.0f;
  // Breath: high-pass biquad (transposed direct form II), `open` and its ramp. thru_ is written by set_thru, read by process (one context).
  float hp_b0_ = 0.0f, hp_b1_ = 0.0f, hp_b2_ = 0.0f, hp_a1_ = 0.0f, hp_a2_ = 0.0f;
  float hp_s1_ = 0.0f, hp_s2_ = 0.0f;
  float open_ = 0.0f, open_step_ = 0.0f;
  float thru_ = 0.0f;
  float breath_ = 0.0f, breath_step_ = 0.0f;  // the breath's level and its ramp over a hop
  std::uint32_t breath_pos_ = 0;              // frames into that ramp
  float agc_ = 1.0f;     // the AGC's gain at the last publish
  float level_ = 0.1f;   // AGC: mean-abs follower of the decimated modulator
  float lvl_att_ = 0.0f, lvl_rel_ = 0.0f;
};

}  // namespace o3_voc03er
