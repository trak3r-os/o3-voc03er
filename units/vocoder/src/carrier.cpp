// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/carrier.h>

#include <cmath>

#include "nofma.h"

namespace o3_voc03er {

namespace {

constexpr float kVoiceGain = 0.25f;  // eight voices stay inside about +-2: the soft clip at the output takes the rest
constexpr float kNoiseGain = 1.0f;
constexpr float kIdle = 1.0e-4f;  // -80 dB: a releasing voice ends below it
constexpr std::uint32_t kMaxBlock = 64;

// Per-oscillator constants, from the phase increment: the reciprocal (no division in the sample loop) and the upper edge window.
struct Edge {
  float dt;
  float rdt;  // 1 / dt
  float hi;   // 1 - dt
};

__attribute__((always_inline)) inline Edge make_edge(float dt) noexcept { return Edge{dt, 1.0f / dt, 1.0f - dt}; }

// PolyBLEP residual for a unit step at phase 0 (t in [0, 1)); the edge windows are the rare path.
__attribute__((always_inline)) inline float blep(float t, const Edge& e) noexcept {
  if (__builtin_expect(t < e.dt, 0)) {
    t *= e.rdt;
    return t + t - mulb(t, t) - 1.0f;
  }
  if (__builtin_expect(t > e.hi, 0)) {
    t = (t - 1.0f) * e.rdt;
    return mulb(t, t) + t + t + 1.0f;
  }
  return 0.0f;
}

template <bool Saw>
__attribute__((always_inline)) inline float osc(float phase, const Edge& e) noexcept {
  if constexpr (Saw) {
    return (phase + phase - 1.0f) - blep(phase, e);
  } else {
    float p2 = phase + 0.5f;
    p2 -= p2 >= 1.0f ? 1.0f : 0.0f;
    return (phase < 0.5f ? 1.0f : -1.0f) + blep(phase, e) - blep(p2, e);
  }
}

enum class Seg { Attack, Hold, Release };

struct VoiceRun {
  float phase, phase2, env;
  Edge e1, e2;
  float attack_step, release_coef;
};

// Samples i..n of one voice in one envelope segment; Gate: accumulate the held-notes gate. Returns the index it stopped at. Attack stops
// after the sample that reaches 1 (`done`), Release after the sample that falls under kIdle (`done`), Hold runs to n.
template <bool Two, bool Saw, bool Gate, Seg S>
inline std::uint32_t run_segment(VoiceRun& v, float* out, float* gate, std::uint32_t i, std::uint32_t n, float g1, bool& done) noexcept {
  float phase = v.phase;
  float phase2 = v.phase2;
  float env = v.env;
  done = false;
  if constexpr (S == Seg::Hold) {
    const float eg = env * g1;
    for (; i < n; ++i) {
      float x = osc<Saw>(phase, v.e1);
      phase += v.e1.dt;
      phase -= phase >= 1.0f ? 1.0f : 0.0f;
      if constexpr (Two) {
        x += osc<Saw>(phase2, v.e2);
        phase2 += v.e2.dt;
        phase2 -= phase2 >= 1.0f ? 1.0f : 0.0f;
      }
      out[i] += mulb(x, eg);
      if constexpr (Gate) {
        gate[i] += env;
      }
    }
  } else {
    for (; i < n;) {
      float x = osc<Saw>(phase, v.e1);
      phase += v.e1.dt;
      phase -= phase >= 1.0f ? 1.0f : 0.0f;
      if constexpr (Two) {
        x += osc<Saw>(phase2, v.e2);
        phase2 += v.e2.dt;
        phase2 -= phase2 >= 1.0f ? 1.0f : 0.0f;
      }
      out[i] += mulb(x * env, g1);
      if constexpr (Gate) {
        gate[i] += env;
      }
      ++i;
      if constexpr (S == Seg::Attack) {
        env += v.attack_step;
        if (env >= 1.0f) {
          env = 1.0f;
          done = true;
          break;
        }
      } else {
        env *= v.release_coef;
        if (env < kIdle) {
          env = 0.0f;
          done = true;
          break;
        }
      }
    }
  }
  v.phase = phase;
  v.phase2 = phase2;
  v.env = env;
  return i;
}

template <bool Two, bool Saw, bool Gate>
inline void run_voice(VoiceRun& v, std::uint8_t& stage_in, float* out, float* gate, std::uint32_t n, float g1) noexcept {
  // stage_in: 1 Attack, 2 Hold, 3 Release (the Stage values); written back as the stage the voice ends in
  std::uint32_t i = 0;
  bool done = false;
  if (stage_in == 1) {
    i = run_segment<Two, Saw, Gate, Seg::Attack>(v, out, gate, i, n, g1, done);
    if (!done) {
      return;
    }
    stage_in = 2;
  }
  if (stage_in == 2) {
    run_segment<Two, Saw, Gate, Seg::Hold>(v, out, gate, i, n, g1, done);
    return;
  }
  run_segment<Two, Saw, Gate, Seg::Release>(v, out, gate, i, n, g1, done);
  if (done) {
    stage_in = 0;
  }
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
  const Voice* last = nullptr;  // the newest sounding voice: where a glide starts
  for (const Voice& v : voices_) {
    if (&v != pick && v.stage != Stage::Idle && (last == nullptr || v.age > last->age)) {
      last = &v;
    }
  }
  const float from = last != nullptr && pick->stage == Stage::Idle ? last->cur : pick->cur;
  pick->inc = 440.0f * std::exp2((static_cast<float>(note) - 69.0f) / 12.0f) / fs_;
  pick->inc = pick->inc > 0.45f ? 0.45f : pick->inc;
  pick->cur = glide_ > 0.0f && from > 0.0f ? from : pick->inc;
  pick->age = ++counter_;
  pick->stage = Stage::Attack;  // from where the envelope stands: a steal does not click
}

void Carrier::set_glide(float coef) noexcept {
  const bool was = glide_ > 0.0f;
  glide_ = coef < 0.0f ? 0.0f : (coef > 1.0f ? 1.0f : coef);
  if (was && glide_ == 0.0f) {
    for (Voice& v : voices_) {
      v.cur = v.inc;
    }
  }
}

void Carrier::step_glide() noexcept {
  if (glide_ <= 0.0f) {
    return;
  }
  for (Voice& v : voices_) {
    if (v.stage == Stage::Idle) {
      v.cur = v.inc;
      continue;
    }
    const float d = v.inc - v.cur;
    v.cur = (d < 1.0e-6f * v.inc && d > -1.0e-6f * v.inc) ? v.inc : v.cur + d * glide_;
  }
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
  const bool want_gate = gate_out != nullptr || noise_ > 0.0f;
  const float osc_gain = kVoiceGain * (1.0f - noise_);
  const float g1 = two ? 0.5f * osc_gain : osc_gain;  // two oscillators keep the voice's level
  for (Voice& v : voices_) {
    if (v.stage == Stage::Idle) {
      continue;
    }
    const float base = glide_ > 0.0f ? v.cur : v.inc;
    const float dt1 = base * bend_ratio_ > 0.45f ? 0.45f : base * bend_ratio_;  // x 1.0f is exact: Bend 0 is the unbent path
    float dt2 = dt1 * detune_ratio_;
    dt2 = dt2 > 0.45f ? 0.45f : dt2;
    VoiceRun r{v.phase, two ? v.phase2 : v.phase, v.env, make_edge(dt1), make_edge(dt2), attack_step_, release_coef_};
    std::uint8_t stage = static_cast<std::uint8_t>(v.stage);
    static_assert(static_cast<unsigned>(Stage::Attack) == 1 && static_cast<unsigned>(Stage::Hold) == 2 &&
                      static_cast<unsigned>(Stage::Release) == 3, "run_voice reads the Stage values");
    if (want_gate) {
      if (two) {
        saw ? run_voice<true, true, true>(r, stage, out, gate, n, g1) : run_voice<true, false, true>(r, stage, out, gate, n, g1);
      } else {
        saw ? run_voice<false, true, true>(r, stage, out, gate, n, g1) : run_voice<false, false, true>(r, stage, out, gate, n, g1);
      }
    } else {
      if (two) {
        saw ? run_voice<true, true, false>(r, stage, out, gate, n, g1) : run_voice<true, false, false>(r, stage, out, gate, n, g1);
      } else {
        saw ? run_voice<false, true, false>(r, stage, out, gate, n, g1) : run_voice<false, false, false>(r, stage, out, gate, n, g1);
      }
    }
    v.phase = r.phase;
    v.phase2 = two ? r.phase2 : r.phase;  // as before: one oscillator keeps the second in step
    v.env = r.env;
    v.stage = static_cast<Stage>(stage);
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
