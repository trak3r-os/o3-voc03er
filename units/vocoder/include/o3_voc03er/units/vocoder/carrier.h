// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Carrier: the vocoder's internal sound source. Eight paraphonic voices of two oscillators each (polyBLEP; SHAPE morphs saw, 50 % pulse and
// a narrow pulse; the second is SPREAD cents sharp of the first), plus up to a third a voice at -SPREAD cents (unison), a SUB square an
// octave down, a TILT shelf (about 700 Hz) once on the sum of the voices, a white-noise mix gated by the held notes, a short attack and a
// release per voice, oldest-voice stealing. Silent with no note held. A plain class: no engine knowledge. The noise comes from a
// NoiseSource that Bands can share (set_noise_source), so the carrier and the unvoiced path draw from one generator.
//
// Fast path: with SHAPE exactly 0 (saw) or 1 (pulse), SUB 0, no third oscillator on the voice (SPREAD 0, or the plan has none for it) the sample
// loop is the one of before, bit for bit, at its cost; TILT works on the sum and adds nothing to that loop. A feature that is off adds nothing.
// Which voices get the third oscillator and which oscillators morph is decided by plan_oscillators (carrier_plan.h) under a cycle budget,
// once a block, from the number of sounding voices; a voice that gains or loses its third ramps its level over about 3 ms.
//
// Cost [est]: about 34 cycles a sample an oscillator (measured on the unit for two), 16 oscillators with all eight voices: 16 x 34 x 64 =
// 34,816 a 64-frame block; the plan adds at most 8,000 for the third oscillators, the morph (+9 a sample an oscillator), SUB (5 a voice) and
// TILT (about 12 a sample on the sum). The constants are in carrier_plan.h; the unit measures them (U11).

#include <cstdint>

namespace o3_voc03er {

// White noise, xorshift32, uniform in [-1, 1). One generator may feed several users (single thread: the audio block).
class NoiseSource {
 public:
  float next() noexcept {
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return static_cast<float>(static_cast<std::int32_t>(s_)) * 4.6566129e-10f;  // 2^-31
  }
  void fill(float* out, std::uint32_t n) noexcept {
    for (std::uint32_t i = 0; i < n; ++i) {
      out[i] = next();
    }
  }

 private:
  std::uint32_t s_ = 0x2545F491u;
};

class Carrier {
 public:
  static constexpr unsigned kVoices = 8;

  void prepare(float sample_rate) noexcept;
  void note_on(std::uint8_t note) noexcept;
  void note_off(std::uint8_t note) noexcept;
  void all_notes_off() noexcept;

  // SHAPE 0..2: 0 saw, 1 50 % pulse, 2 a narrow pulse (10 %); between the ends a morph (saw(p) - m x saw(p + width), level held), with no jump
  // in level or polarity through 1. Exactly 0 and 1 are the specialised paths.
  void set_shape(float shape) noexcept { shape_ = shape < 0.0f ? 0.0f : (shape > 2.0f ? 2.0f : shape); }
  void set_sub(float amount) noexcept { sub_ = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount); }  // SUB 0..1: the square an octave below
  void set_tilt_db(float db) noexcept;  // TILT -12..+12 dB: the high side +db/2, the low side -db/2 (a one-pole shelf at about 700 Hz, asymptotes 1.15 x so that 100 Hz and 10 kHz land within 1 dB)
  void set_noise(float amount) noexcept { noise_ = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount); }  // 0 oscillators .. 1 noise
  void set_detune_cents(float cents) noexcept;  // SPREAD 0..100: the second oscillator +cents of the first, the third -cents; 0: one oscillator
  // The pitch ratio of every sounding oscillator (2^(bend / 12)), set by the owner once a block; 1 is the exact unbent path. SYNTH only.
  void set_bend_ratio(float ratio) noexcept { bend_ratio_ = ratio; }
  // GLIDE: the per-run slew of each voice's increment toward its note, `coef` = 1 - exp(-n / tau) for the run's n samples (the owner computes it
  // once a run); 0 turns the glide off and every voice returns to its target, the exact path of before. A new voice starts from the pitch of the
  // newest sounding one. Nothing is added to the sample loop.
  void set_glide(float coef) noexcept;
  void step_glide() noexcept;
  void set_noise_source(NoiseSource* src) noexcept { noise_src_ = src; }  // null: the carrier's own
  void set_attack_ms(float ms) noexcept;
  void set_release_ms(float ms) noexcept;

  // Writes `n` samples (replaces the buffer's contents). `gate`, when given, gets the held-notes gate a sample, 0..1 (the voices'
  // envelopes summed and clipped): what gates the noise, and what Bands uses to gate its unvoiced noise.
  void render(float* out, std::uint32_t n, float* gate = nullptr) noexcept;

  // For tests and screens.
  unsigned sounding_voices() const noexcept;   // voices not idle (held or releasing)
  bool holds(std::uint8_t note) const noexcept;  // a voice is held (key down) on this note
  void set_force_ext(bool on) noexcept { force_ext_ = on; }  // test hook: every voice on the generic path (run_ext), to compare it with the specialised ones

 private:
  enum class Stage : std::uint8_t { Idle, Attack, Hold, Release };
  struct Voice {
    float phase = 0.0f;
    float inc = 0.0f;  // the note's increment: the glide's target
    float cur = 0.0f;  // the increment the oscillators use while GLIDE is on
    float phase2 = 0.0f;
    float phase3 = 0.0f;  // the third oscillator, started at an offset phase when its level leaves 0
    float r3 = 0.0f;      // the third oscillator's level, 0..1, ramped (the block's plan sets its target)
    float sub_sign = 1.0f;  // the SUB square's level, flipped at oscillator 1's wrap (only while the voice runs SUB; it starts at +1)
    float rs = 0.0f;        // this voice's SUB level, 0..1, stepped once a block toward its goal (the plan gives SUB to the lowest notes)
    float env = 0.0f;
    std::uint32_t age = 0;  // note-on counter: the smallest is the oldest
    Stage stage = Stage::Idle;
    std::uint8_t note = 0;
  };

  Voice voices_[kVoices];
  float fs_ = 44100.0f;
  float attack_step_ = 0.005f;
  float release_coef_ = 0.999f;
  float noise_ = 0.0f;
  float detune_ratio_ = 1.0f;  // 2^(cents / 1200)
  float detune_down_ = 1.0f;   // 2^(-cents / 1200): the third oscillator
  float shape_ = 0.0f;
  float sub_ = 0.0f;           // the target, 0..1
  float sub_cur_ = 0.0f;       // the level the last block ended on (ramped to the target over the next block)
  float tilt_target_lo_ = 1.0f;  // the shelf's gains (linear): the low side and the high side, smoothed per block toward the target
  float tilt_target_hi_ = 1.0f;
  float tilt_lo_ = 1.0f;
  float tilt_hi_ = 1.0f;
  float tilt_lp_ = 0.0f;       // the shelf's one-pole state
  float tilt_a_ = 0.14f;       // the one-pole coefficient for about 700 Hz
  float ramp_step_ = 0.0076f;  // a third oscillator's level change a sample (about 3 ms)
  float glide_ = 0.0f;         // the per-run slew coefficient, 0 off
  float bend_ratio_ = 1.0f;    // 2^(semitones / 12), from the unit's Bend
  NoiseSource own_noise_;
  NoiseSource* noise_src_ = nullptr;
  std::uint32_t counter_ = 0;
  bool force_ext_ = false;
};

}  // namespace o3_voc03er
