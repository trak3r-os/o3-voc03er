// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Carrier: the vocoder's internal sound source. Eight paraphonic voices (polyBLEP saw or 50 % pulse), a white-noise mix gated by the
// held notes, a short attack and a release per voice, oldest-voice stealing. Silent with no note held. A plain class: no engine knowledge.
//
// Cost [est]: about 20 cycles a sample a sounding voice (a phase step, a polyBLEP test, an envelope step), 160 with all eight.

#include <cstdint>

namespace o3_voc03er {

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
  void set_attack_ms(float ms) noexcept;
  void set_release_ms(float ms) noexcept;

  // Writes `n` samples (replaces the buffer's contents).
  void render(float* out, std::uint32_t n) noexcept;

  // For tests and screens.
  unsigned sounding_voices() const noexcept;   // voices not idle (held or releasing)
  bool holds(std::uint8_t note) const noexcept;  // a voice is held (key down) on this note

 private:
  enum class Stage : std::uint8_t { Idle, Attack, Hold, Release };
  struct Voice {
    float phase = 0.0f;
    float inc = 0.0f;
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
  Wave wave_ = Wave::Saw;
  std::uint32_t counter_ = 0;
  std::uint32_t rng_ = 0x2545F491u;
};

}  // namespace o3_voc03er
