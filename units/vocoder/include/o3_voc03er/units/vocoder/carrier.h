// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Carrier: the vocoder's internal sound source. Eight paraphonic voices of two oscillators each (polyBLEP saw or 50 % pulse; the second
// is detuned against the first by 0..20 cents), a white-noise mix gated by the held notes, a short attack and a release per voice,
// oldest-voice stealing. Silent with no note held. A plain class: no engine knowledge. The noise comes from a NoiseSource that Bands can
// share (set_noise_source), so the carrier and the unvoiced path draw from one generator.
//
// Cost [est]: about 30 cycles a sample an oscillator (a phase step and the polyBLEP tests, the voice's envelope step shared), 16 with all
// eight voices: 16 x 30 x 64 = 30,720 a 64-frame block, 11.8 % of the 261,224 it has; half of that with DETUNE 0, when the second
// oscillator is skipped.

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
  enum class Wave : std::uint8_t { Saw, Pulse };

  void prepare(float sample_rate) noexcept;
  void note_on(std::uint8_t note) noexcept;
  void note_off(std::uint8_t note) noexcept;
  void all_notes_off() noexcept;

  void set_wave(Wave w) noexcept { wave_ = w; }
  void set_noise(float amount) noexcept { noise_ = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount); }  // 0 oscillators .. 1 noise
  void set_detune_cents(float cents) noexcept;  // the second oscillator of each voice, 0..20 cents sharp of the first
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

 private:
  enum class Stage : std::uint8_t { Idle, Attack, Hold, Release };
  struct Voice {
    float phase = 0.0f;
    float inc = 0.0f;  // the note's increment: the glide's target
    float cur = 0.0f;  // the increment the oscillators use while GLIDE is on
    float phase2 = 0.0f;
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
  float glide_ = 0.0f;         // the per-run slew coefficient, 0 off
  float bend_ratio_ = 1.0f;    // 2^(semitones / 12), from the unit's Bend
  NoiseSource own_noise_;
  NoiseSource* noise_src_ = nullptr;
  Wave wave_ = Wave::Saw;
  std::uint32_t counter_ = 0;
};

}  // namespace o3_voc03er
