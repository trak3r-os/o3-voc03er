// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Autotune: the TUNE mode (docs/VOCODER-DESIGN.md section 4, docs/PLAN.md V6b). The voice itself goes in, a pitch-shifted voice comes out:
// Tune (the detector, tune.h) says what the pitch is, Snap picks the note it should be, Retune moves the correction toward it, the
// period-jump shifter applies it. A plain class, given the samples a block at a time (process); it knows nothing of the engine.
//
// Written fresh: no code of any other shifter or tuner was read or copied, and there is no FFT.
//
// Shifter: the detector's own native history ring (Tune::native, 2048 samples, no second copy) and one reader at a delay d (samples behind the newest), read with a Catmull-Rom
// cubic. A ratio r (1 = no shift) moves d by 1 - r a sample: r > 1 (up) shortens the delay, r < 1 lengthens it. The delay must stay in a
// window [kDelayLo, kDelayLo + P) one detected period wide. When it leaves, a second reader starts at d plus or minus a WHOLE number of
// periods, back inside the window; the voice repeats itself every period, so the two readers carry the same waveform, and a
// kXfade-sample smoothstep crossfade between them is in phase (no comb, no click). Afterwards the second reader is the reader. Formants
// move with the ratio (the spectrum envelope is shifted too): the usual limit of a time-domain shifter; the correction is at most +-6
// semitones.
// Unvoiced input: the correction relaxes to none over about 30 ms and, once the ratio is exactly 1, the delay is rounded to a whole
// number of samples by one more crossfade: the output is the input delayed by that many samples, bit for bit.
//
// Snap: the target is the scale note nearest the (smoothed) detected pitch, with a kSnapBias-cents bias in favour of the note already
// chosen (a vibrato at the edge of two notes does not flip it). KEY and SCALE give the notes; with NOTES-IN on and a note held, the most
// recent held note wins (folded to the octave nearest the voice); with none held the scale is the fallback.
// Retune: the shift (cents) follows the error with a one-pole per 16-sample chunk; SPEED 0 is alpha 1: the shift is the whole error at
// once, a step at every new estimate (the hard sound). AMOUNT scales the error.
//
// Schedule: by sample count: the control step (Tune slice, snap, retune, ratio ramp) runs every kSeg (16) input samples, so the output
// is bit-identical for any block length. The ratio is interpolated linearly per sample across a segment. Nothing in a sample loop
// divides or calls libm; log2f and exp2f run once per estimate and once per segment (exp2f only while the shift is not 0).
// Latency: the delay (about 1 ms at rest) plus the detector's (voiced 11 to 48 ms after an onset, tune.h).
// Memory: sizeof about 13 KiB in SRAM (Tune and the control state; no history of its own). All in the render context. No allocation. NaN and infinite input -> 0.

#include <o3_voc03er/units/vocoder/tune.h>

#include <cstdint>

namespace o3_voc03er {

class Autotune {
 public:
  enum Scale : std::uint8_t { Chromatic, Major, Minor, MajorPent, MinorPent, Blues, Dorian, kScaleCount };
  static constexpr float kMinRead = 20.0f;     // the nearest read: the detector's ring lags by up to kSeg - 1 samples, the cubic reads 2 on
  static constexpr float kMaxRead = 1016.0f;   // the farthest: kDelayLo + the longest period (630) + the drift of a crossfade fits
  static constexpr unsigned kSeg = Tune::kTick;
  static constexpr unsigned kXfade = 64;       // crossfade length, samples
  static constexpr float kDelayLo = 48.0f;     // the window's lower edge, samples (covers the drift of a crossfade at +-6 semitones)
  static constexpr float kRestDelay = 48.0f;   // the delay at reset, a whole number
  // At +600 cents the old reader falls 0.4143 samples a sample for the whole crossfade, and must still end at or above the nearest read.
  static_assert(kDelayLo >= kMinRead + 0.4143f * static_cast<float>(kXfade), "the crossfade's old reader would read unwritten samples");
  static constexpr float kSnapBias = 0.20f;    // semitones
  static constexpr float kMaxCents = 600.0f;
  static constexpr unsigned kMaxHeld = 8;

  Autotune();
  void reset();  // forget the voice (the held notes and the settings stay)

  void set_key(unsigned k);          // 0..11, C = 0
  void set_scale(unsigned s);        // Scale
  void set_speed_ms(float ms);       // 0 = hard
  void set_amount(float a) { amount_ = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a); }
  void set_notes_in(bool on) { notes_in_ = on; }
  void note_on(std::uint8_t n);
  void note_off(std::uint8_t n);
  void all_notes_off() { held_n_ = 0; }

  void process(const float* in, float* out, unsigned n);  // in may equal out

  const Tune& tuner() const { return tune_; }
  float shift_cents() const { return c_; }
  float delay() const { return d_; }
  int target_note() const { return cur_; }  // the MIDI note being aimed at, -1 before the first voiced estimate
  bool voiced() const { return tune_.voiced(); }

  // VOICE (the vocoder's CarSrc 8): the sung note, for the synth carrier to play. set_voice(true) makes the control step (process or detect)
  // also decide it once a segment: the detected pitch snapped by KEY and SCALE with pick() (kSnapBias, SPEED 0 unsmoothed, as TUNE decides
  // it), through its own hysteresis state (a held NOTES-IN note does not steer it). Off, nothing of it runs and every output is as before.
  // detect() is process() without the shifter: the detector alone, no output, nothing read back. Unvoiced, the note is held kVoiceHoldMs
  // (counted in segments, so block-length independent) before voice_note() gives -1: a consonant does not chop the note.
  static constexpr float kVoiceHoldMs = 60.0f;
  static constexpr unsigned kVoiceHoldSegs = static_cast<unsigned>(kVoiceHoldMs * Tune::kFs * 0.001f / static_cast<float>(Tune::kTick));
  void set_voice(bool on);
  void detect(const float* in, unsigned n);
  int voice_note() const { return vnote_; }  // the sung MIDI note, -1 when none (before the first voiced estimate, or after the hold)

 private:
  void voice_step();
  void control();
  void new_estimate(float f);
  int pick(float m, int cur) const;
  float read(float d) const;
  void start_xfade(float d2);
  void jump();
  bool in_scale(int n) const { return ((mask_ >> (((n - key_) % 12 + 12) % 12)) & 1u) != 0u; }

  Tune tune_;
  float seg_[kSeg];
  unsigned seg_n_ = 0;
  std::uint32_t w_ = 0;      // index of the next sample to write

  float d_ = kRestDelay, d2_ = kRestDelay;
  unsigned xf_ = 0;          // 0: no crossfade; else 1..kXfade
  float ratio_ = 1.0f, dr_ = 0.0f, r1_ = 1.0f;
  float period_ = 100.0f, inv_period_ = 0.01f;

  unsigned key_ = 0;
  unsigned mask_ = 0xFFFu;
  unsigned scale_ = Chromatic;
  float alpha_ = 1.0f;
  float amount_ = 1.0f;
  bool notes_in_ = true;
  std::uint8_t held_[kMaxHeld] = {};
  unsigned held_n_ = 0;

  bool was_voiced_ = false;
  float f_last_ = 0.0f;
  float m_ = 0.0f, m_s_ = 0.0f;  // MIDI pitch of the last estimate and its smoothed value
  int cur_ = -1;
  float e_ = 0.0f;               // the wanted shift, cents
  float c_ = 0.0f;               // the shift now, cents

  bool voice_on_ = false;        // VOICE: the render context only, with every member below
  bool vvoiced_ = false;
  int vnote_ = -1;
  unsigned vhold_ = 0;           // unvoiced segments since the note was last sung
  float vm_s_ = 0.0f;            // the voice's smoothed decision pitch, MIDI
  float vf_last_ = 0.0f;
};

}  // namespace o3_voc03er
