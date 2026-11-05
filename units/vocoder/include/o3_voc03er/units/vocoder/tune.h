// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Tune: the pitch detector and the voicing gate of TUNE (docs/VOCODER-DESIGN.md section 4, docs/PLAN.md V6a). A plain class: it is given
// the input samples a block at a time (push) and holds the current estimate (frequency, confidence, voiced). It knows nothing of the engine,
// the parameters or the shifter, and it is not yet part of Vocoder::process.
//
// Clean room from the YIN paper (de Cheveigne and Kawahara, JASA 2002); no code of aubio, the Teensy Audio library or any other
// implementation was read or copied. Every threshold below is a named starting point, to be tuned on sung recordings.
//
// Pipeline, all float, all at 44.1 kHz in:
//   1. A 32-tap FIR and a 4:1 decimation to 11.025 kHz; Range::Bass adds a 16-tap FIR and a second 2:1 (5.5125 kHz), which covers 50 Hz
//      to 400 Hz with the same window and lag counts instead of growing them.
//   2. YIN on the decimated signal: the difference function over a window of kWindow (256) samples, the cumulative-mean normalisation,
//      the first lag whose d' is below kThreshold followed down to its minimum (no dip: the first local minimum within kSlack of the lowest), parabolic refinement.
//   3. A refinement at the native rate: seven native lags around the decimated estimate, a 512-sample window, a second parabola.
//   4. The voicing gate, once a hop: the aperiodicity d' at the chosen lag (hysteresis kGateEnter / kGateExit), the signal power
//      (hysteresis), and the low-band to total power ratio of the decimator's low-pass (rejects sibilants); a hold of kHoldMs before the
//      estimate is declared unvoiced; a jump of more than 7 semitones from the held pitch must be repeated by the next hop to be accepted.
//
// The schedule is by SAMPLE COUNT, not by block: the hop is kHop (512) input samples (11.6 ms); every kTick (16) samples one slice of at
// most kUnitsPerTick lag-units runs (one difference lag is 256 terms, one refine lag 512, a unit is 256 terms), so a block of 64 does at
// most 4 slices, and a block of any length 1..64 gives bit-identical estimates. The slice work is the whole hop's work spread evenly:
// 157 lags x 256 terms + 7 x 512 terms = 43,800 terms a hop worst case (about 5,500 terms per 64-sample block), less when the first dip
// comes early. Nothing in a sample loop divides or calls libm; the one division per lag and the one per hop run in the slices.
//
// Latency (measured in tests/tune_test.cpp, synthetic voices): the estimate describes the 23 ms before the hop's start and is published
// within 31 ticks (11.2 ms) after it. A tone after silence is declared voiced 11 to 25 ms after its onset from 300 Hz up and 31 to 48 ms
// at 80 to 150 Hz (the worst 47.5 ms, 80 Hz); the first estimate may be up to 36 cents off (a window part silence) and is within 1 cent
// four hops later. The end of a voice is declared 79 ms after its last sample (the estimate's age plus the 60 ms hold).
//
// Memory: about 13 KiB in SRAM (sizeof), no SDRAM. Threads: all in the render context.
// Real-time: no allocation, no blocking, bounded loops, no double. NaN and infinite input samples are replaced by 0.

#include <cstdint>

namespace o3_voc03er {

class Tune {
 public:
  enum class Range : std::uint8_t { Normal, Bass };

  static constexpr float kFs = 44100.0f;
  static constexpr unsigned kWindow = 256;     // YIN window, decimated samples (23 ms at 11.025 kHz)
  static constexpr unsigned kMaxLag = 157;     // 70 Hz at 11.025 kHz (Normal)
  static constexpr unsigned kBassMaxLag = 110; // 50 Hz at 5.5125 kHz (Bass)
  static constexpr unsigned kMinLag = 12;      // 919 Hz at 11.025 kHz
  static constexpr unsigned kBassMinLag = 13;  // 424 Hz at 5.5125 kHz
  static constexpr unsigned kHop = 512;        // input samples between estimates
  static constexpr unsigned kTick = 16;        // input samples between slices
  static constexpr unsigned kUnitsPerTick = 6; // lag-units a slice may do (31 slices x 6 = 186 >= 157 + 2 x 7)
  static constexpr unsigned kRefineSpan = 3;   // native lags either side of the decimated estimate
  static constexpr unsigned kRefineWindow = 512;

  // Starting points, all to be tuned on recordings.
  static constexpr float kSubMargin = 0.08f;       // the double of a dip replaces it if its d' is lower by this ...
  static constexpr float kSubRatio = 0.5f;         // ... and below this fraction of it
  static constexpr float kSlack = 0.10f;           // no dip below the threshold: the first local minimum within this of the lowest
  static constexpr float kThreshold = 0.15f;       // YIN: first dip of d' below this
  static constexpr float kGateEnter = 0.25f;       // d' at the chosen lag must be below this to become voiced
  static constexpr float kGateExit = 0.35f;        // ... and below this to stay voiced
  static constexpr float kLevelEnterPow = 1.0e-5f; // mean power of the last hop to become voiced (rms 0.0032, -50 dBFS)
  static constexpr float kLevelExitPow = 4.0e-6f;  // ... to stay voiced
  static constexpr float kLowBandMin = 0.4f;       // low-band / total power ratio below which a hop is a fricative
  static constexpr unsigned kHoldMs = 60;          // a failed verdict does not unvoice the estimate for this long
  static constexpr float kJumpRatio = 1.498f;      // 7 semitones: a new pitch beyond it (up or down) needs a repeat
  static constexpr float kJumpAgree = 1.06f;       // the repeat must agree within this ratio (one semitone)

  Tune();
  void reset();                    // forget everything (also the range's state), keeps the range
  void set_range(Range r);         // resets
  Range range() const { return range_; }

  // `n` samples (any n; blocks of 1..64 are the use). x is read once.
  void push(const float* x, unsigned n);

  // The native history: the sample with absolute index i (the i-th pushed since reset; valid for the last kNative - 1 pushed, input already clamped
  // to +-8 and NaN-free). Autotune's shifter reads it instead of keeping a second ring.
  float native(std::uint32_t i) const { return xn_[i & (kNative - 1u)]; }

  float frequency() const { return freq_; }        // Hz of the last accepted voiced estimate; 0 before the first
  float confidence() const { return conf_; }       // 1 - d' of the last verdict, 0..1
  bool voiced() const { return voiced_; }

 private:
  static constexpr unsigned kNative = 2048;        // native history ring (see the static_assert in tune.cpp)
  static constexpr unsigned kDecRing = 512;
  static constexpr unsigned kSnap = 416;           // kWindow + kMaxLag rounded to 8
  static constexpr unsigned kRefineCount = 2 * kRefineSpan + 1;
  static constexpr unsigned kS1Taps = 32;
  static constexpr unsigned kS2Taps = 16;

  enum class Stage : std::uint8_t { Idle, Diff, Refine, Done };

  void tick();
  void hop_start();
  void work();
  bool diff_step();      // one lag; true when the difference stage is over
  void diff_end();
  void refine_step();    // one native lag
  bool gate_ok(float a) const;
  void finish(float lag_native, bool good);
  void decimate_stage1();
  void dec_push(float y);

  unsigned decim() const { return range_ == Range::Bass ? 8u : 4u; }
  unsigned min_lag() const { return range_ == Range::Bass ? kBassMinLag : kMinLag; }
  unsigned max_lag() const { return range_ == Range::Bass ? kBassMaxLag : kMaxLag; }

  Range range_ = Range::Normal;

  // Hot state. The two histories and the snapshot are touched by word loads only.
  alignas(8) float xn_[kNative];      // native history ring
  alignas(8) float dec_[kDecRing];    // decimated history ring
  alignas(8) float snap_[kSnap];      // the decimated window and its lags, frozen at the hop's start
  alignas(8) float dp_[kMaxLag + 3];  // d' by lag (index = lag)
  float s1_[kS1Taps];                 // ring of stage-1 outputs (Bass only)
  float h1_[kS1Taps];
  float h2_[kS2Taps];
  float dn_[kRefineCount];

  std::uint32_t n_ = 0;               // input samples seen
  std::uint32_t dn_count_ = 0;        // decimated samples written
  std::uint32_t s1n_ = 0;             // stage-1 outputs (Bass phase)
  float en_ = 0.0f, ed_ = 0.0f;       // power sums since the last hop start (native, decimated)
  float hop_pow_ = 0.0f, hop_ratio_ = 0.0f;
  std::uint32_t snap_native_ = 0;     // n_ at the snapshot

  Stage stage_ = Stage::Idle;
  unsigned lag_ = 1;                  // next difference lag
  float cum_ = 0.0f;
  bool found_ = false;
  bool dip_done_ = false;
  unsigned cand_ = 0;
  unsigned best_ = 0;
  float best_v_ = 2.0f;
  float cand_a_ = 1.0f;               // d' at the chosen lag
  float tau_f_ = 0.0f;                // refined decimated lag
  unsigned ref_lo_ = 0;               // first native lag of the refine
  unsigned ref_i_ = 0;

  // Output.
  float freq_ = 0.0f, conf_ = 0.0f;
  bool voiced_ = false;
  std::uint32_t bad_samples_ = 0;
  float pending_ = 0.0f;              // a candidate beyond the jump ratio, awaiting its repeat
};

}  // namespace o3_voc03er
