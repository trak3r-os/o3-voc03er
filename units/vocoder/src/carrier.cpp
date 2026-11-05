// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/carrier.h>

#include <cmath>

namespace o3_voc03er {

namespace {

constexpr float kVoiceGain = 0.25f;  // eight voices stay inside about +-2: the soft clip at the output takes the rest
constexpr float kNoiseGain = 1.0f;
constexpr float kIdle = 1.0e-4f;  // -80 dB: a releasing voice ends below it
constexpr std::uint32_t kMaxBlock = 64;

// PolyBLEP residual for a unit step at phase 0, `dt` the phase increment.
inline float blep(float t, float dt) noexcept {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

inline float osc(float phase, float dt, bool saw) noexcept {
  if (saw) {
    return 2.0f * phase - 1.0f - blep(phase, dt);
  }
  float p2 = phase + 0.5f;
  p2 -= p2 >= 1.0f ? 1.0f : 0.0f;
  return (phase < 0.5f ? 1.0f : -1.0f) + blep(phase, dt) - blep(p2, dt);
}

}  // namespace

void Carrier::set_detune_cents(float cents) noexcept {
  cents = cents < 0.0f ? 0.0f : (cents > 20.0f ? 20.0f : cents);
  detune_ratio_ = cents > 0.0f ? std::exp2(cents * (1.0f / 1200.0f)) : 1.0f;
}

void Carrier::prepare(float sample_rate) noexcept {
  fs_ = sample_rate;
  for (Voice& v : voices_) {
    v = Voice{};
  }
  counter_ = 0;
  set_attack_ms(5.0f);
  set_release_ms(80.0f);
}

void Carrier::set_attack_ms(float ms) noexcept {
  ms = ms < 0.1f ? 0.1f : ms;
  attack_step_ = 1000.0f / (ms * fs_);
  attack_step_ = attack_step_ > 1.0f ? 1.0f : attack_step_;
}

void Carrier::set_release_ms(float ms) noexcept {
  ms = ms < 0.5f ? 0.5f : ms;
  release_coef_ = std::exp(-4000.0f / (ms * fs_));  // four time constants over `ms`
}

void Carrier::note_on(std::uint8_t note) noexcept {
  Voice* pick = nullptr;
  for (Voice& v : voices_) {  // the same note again keeps its voice
    if (v.stage != Stage::Idle && v.note == note) {
      pick = &v;
      break;
    }
  }
  if (pick == nullptr) {
    for (Voice& v : voices_) {
      if (v.stage == Stage::Idle) {
        pick = &v;
        break;
      }
    }
  }
  if (pick == nullptr) {  // steal the oldest
    pick = &voices_[0];
    for (Voice& v : voices_) {
      if (v.age < pick->age) {
        pick = &v;
      }
    }
  }
  if (pick->stage == Stage::Idle) {
    pick->phase = 0.0f;
    pick->phase2 = 0.0f;
    pick->env = 0.0f;
  }
  pick->note = note;
  pick->inc = 440.0f * std::exp2((static_cast<float>(note) - 69.0f) / 12.0f) / fs_;
  pick->inc = pick->inc > 0.45f ? 0.45f : pick->inc;
  pick->age = ++counter_;
  pick->stage = Stage::Attack;  // from where the envelope stands: a steal does not click
}

void Carrier::note_off(std::uint8_t note) noexcept {
  for (Voice& v : voices_) {
    if ((v.stage == Stage::Attack || v.stage == Stage::Hold) && v.note == note) {
      v.stage = Stage::Release;
    }
  }
}

void Carrier::all_notes_off() noexcept {
  for (Voice& v : voices_) {
    if (v.stage == Stage::Attack || v.stage == Stage::Hold) {
      v.stage = Stage::Release;
    }
  }
}

unsigned Carrier::sounding_voices() const noexcept {
  unsigned c = 0;
  for (const Voice& v : voices_) {
    c += v.stage != Stage::Idle ? 1u : 0u;
  }
  return c;
}

bool Carrier::holds(std::uint8_t note) const noexcept {
  for (const Voice& v : voices_) {
    if ((v.stage == Stage::Attack || v.stage == Stage::Hold) && v.note == note) {
      return true;
    }
  }
  return false;
}

void Carrier::render(float* out, std::uint32_t n, float* gate_out) noexcept {
  if (n > kMaxBlock) {
    n = kMaxBlock;
  }
  float gate[kMaxBlock];
  for (std::uint32_t i = 0; i < n; ++i) {
    out[i] = 0.0f;
    gate[i] = 0.0f;
  }
  const bool two = detune_ratio_ != 1.0f;
  const bool saw = wave_ == Wave::Saw;
  const float osc_gain = kVoiceGain * (1.0f - noise_);
  const float g1 = two ? 0.5f * osc_gain : osc_gain;  // two oscillators keep the voice's level
  for (Voice& v : voices_) {
    if (v.stage == Stage::Idle) {
      continue;
    }
    float phase = v.phase;
    float phase2 = two ? v.phase2 : phase;
    float env = v.env;
    Stage stage = v.stage;
    const float dt = v.inc;
    float dt2 = dt * detune_ratio_;
    dt2 = dt2 > 0.45f ? 0.45f : dt2;
    for (std::uint32_t i = 0; i < n; ++i) {
      float x = osc(phase, dt, saw);
      phase += dt;
      phase -= phase >= 1.0f ? 1.0f : 0.0f;
      if (two) {
        x += osc(phase2, dt2, saw);
        phase2 += dt2;
        phase2 -= phase2 >= 1.0f ? 1.0f : 0.0f;
      }
      out[i] += x * env * g1;
      gate[i] += env;
      if (stage == Stage::Attack) {
        env += attack_step_;
        if (env >= 1.0f) {
          env = 1.0f;
          stage = Stage::Hold;
        }
      } else if (stage == Stage::Release) {
        env *= release_coef_;
        if (env < kIdle) {
          env = 0.0f;
          stage = Stage::Idle;
          break;
        }
      }
    }
    v.phase = phase;
    v.phase2 = phase2;
    v.env = env;
    v.stage = stage;
  }
  if (gate_out != nullptr) {
    for (std::uint32_t i = 0; i < n; ++i) {
      gate_out[i] = gate[i] < 1.0f ? gate[i] : 1.0f;
    }
  }
  if (noise_ > 0.0f) {
    float nz[kMaxBlock];
    (noise_src_ != nullptr ? noise_src_ : &own_noise_)->fill(nz, n);
    const float g = kNoiseGain * noise_;
    for (std::uint32_t i = 0; i < n; ++i) {
      out[i] += nz[i] * g * (gate[i] < 1.0f ? gate[i] : 1.0f);
    }
  }
}

}  // namespace o3_voc03er
