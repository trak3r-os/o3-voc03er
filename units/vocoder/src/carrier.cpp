// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/carrier.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

#include <o3_voc03er/units/vocoder/carrier_plan.h>

#include <o3_voc03er/units/vocoder/nofma.h>

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
  float clear;  // 0.25 - dt, less a margin: see osc<false>
};

__attribute__((always_inline)) inline Edge make_edge(float dt) noexcept { return Edge{dt, 1.0f / dt, 1.0f - dt, 0.25f - dt - 1.0e-6f}; }

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
    // One test for the common case: farther than dt from the edges at 0, 0.5 and 1, where both residuals are exactly 0. The margin keeps it inside the
    // windows blep() tests, so the output is the same as without it; a dt of 0.25 or more never takes it.
    const float square = phase < 0.5f ? 1.0f : -1.0f;
    if (__builtin_expect(__builtin_fabsf(__builtin_fabsf(phase - 0.5f) - 0.25f) < e.clear, 1)) {
      return square;
    }
    float p2 = phase + 0.5f;
    p2 -= p2 >= 1.0f ? 1.0f : 0.0f;
    return square + blep(phase, e) - blep(p2, e);
  }
}

// x = a * saw(p) + b * saw(p + c): between saw (SHAPE 0) and the 50 % pulse (1) a = 1 - 2m, b = m, c = 0.5 (a crossfade, which is a phase-shifted saw
// of half the size at m = 0.5, so it is normalised); between the pulse and the narrow one (2) a = -b, c grows from 0.5 to 0.9 (duty 0.5 to 0.1).
struct Morph {
  float a = 0.0f;
  float b = 0.0f;
  float c = 0.5f;
};

Morph make_morph(float s) noexcept {
  constexpr float kSawRms = 0.57735027f;
  if (s < 1.0f) {
    const float r2 = (1.0f - 5.0f * s + 7.0f * s * s) * (1.0f / 3.0f);  // the mean square of a x saw(p) + b x saw(p + 0.5), always > 0
    const float g = (kSawRms + (1.0f - kSawRms) * s) / std::sqrt(r2);   // the level runs from the saw's to the pulse's: no jump at either end
    return Morph{(1.0f - 2.0f * s) * g, s * g, 0.5f};
  }
  const float c = 0.5f + 0.4f * (s - 1.0f);
  const float d = 1.0f - c;
  const float g = 0.5f / std::sqrt(d * (1.0f - d));  // the zero-mean pulse of duty d has the mean square 4 d (1 - d): rms 1 at every width
  return Morph{-g, g, c};
}

__attribute__((always_inline)) inline float morph_osc(float phase, const Edge& e, const Morph& m) noexcept {
  float q = phase + m.c;
  q -= q >= 1.0f ? 1.0f : 0.0f;
  return m.a * osc<true>(phase, e) + m.b * osc<true>(q, e);
}

// The SUB square (level s = +-1, flipped at oscillator 1's wrap) with its edge smoothed by the BLEP residual: the step is to -s before the wrap
// (the sign has not flipped yet) and to +s after it.
__attribute__((always_inline)) inline float sub_osc(float s, float phase, const Edge& e) noexcept {
  if (phase < e.dt) {
    return s * (1.0f + blep(phase, e));
  }
  if (phase > e.hi) {
    return s * (1.0f - blep(phase, e));
  }
  return s;
}

enum class Seg { Attack, Hold, Release };

constexpr float kThirdGain = 0.1835f;  // 2 -> 3 oscillators at equal power: 1/sqrt(6) = 0.5 x (1 - 0.1835)

// One voice for one block on a specialised path (the features are template parameters, so the sample loops hold no branch on them).
struct VoiceRun {
  float phase, phase2, env;
  Edge e1, e2;
  float attack_step, release_coef;
  float phase3 = 0.0f;  // Third only
  Edge e3 = {};
  float sub_sign = 1.0f;  // Sub only: the square's level, flipped at oscillator 1's wrap
  float sub_g = 0.0f;     // Sub only: the level a sample (voice gain included) at the start of the block, ramped by sub_dg a sample
  float sub_dg = 0.0f;
  Morph mo = {};          // M only: oscillator 1 runs the morph (oscillators 2 and 3 run the nearer end)
};

// One sample's oscillators: x (the sum of the oscillators), s (the sub square, before its level) and the phases advanced.
template <bool Two, bool Saw, bool Third, bool Sub, bool M>
__attribute__((always_inline)) inline void voice_step(const VoiceRun& v, float& phase, float& phase2, float& phase3, float& sub_sign, float& sub_g,
                                                      float& x, float& s) noexcept {
  const float p = phase;
  if constexpr (M) {
    x = morph_osc(p, v.e1, v.mo);
  } else {
    x = osc<Saw>(p, v.e1);
  }
  phase += v.e1.dt;
  const bool wrapped = phase >= 1.0f;
  phase -= wrapped ? 1.0f : 0.0f;
  if constexpr (Two) {
    x += osc<Saw>(phase2, v.e2);
    phase2 += v.e2.dt;
    phase2 -= phase2 >= 1.0f ? 1.0f : 0.0f;
  }
  if constexpr (Third) {
    x += osc<Saw>(phase3, v.e3);
    phase3 += v.e3.dt;
    phase3 -= phase3 >= 1.0f ? 1.0f : 0.0f;
  }
  if constexpr (Sub) {
    sub_g += v.sub_dg;
    s = sub_osc(sub_sign, p, v.e1);
    sub_sign = wrapped ? -sub_sign : sub_sign;
  } else {
    (void)sub_sign;
    (void)sub_g;
    s = 0.0f;
  }
}

// Samples i..n of one voice in one envelope segment; Gate: accumulate the held-notes gate. Returns the index it stopped at. Attack stops
// after the sample that reaches 1 (`done`), Release after the sample that falls under kIdle (`done`), Hold runs to n.
template <bool Two, bool Saw, bool Gate, Seg S, bool Third = false, bool Sub = false, bool M = false>
inline std::uint32_t run_segment(VoiceRun& v, float* __restrict out, float* __restrict gate, std::uint32_t i, std::uint32_t n, float g1, bool& done) noexcept {
  const VoiceRun k = v;  // a local whose address never escapes: the edges stay in registers, where a store to out[] through `v` would force a reload a sample
  float phase = v.phase;
  float phase2 = v.phase2;
  float phase3 = v.phase3;
  float sub_sign = v.sub_sign;
  float sub_g = v.sub_g;
  float env = v.env;
  done = false;
  if constexpr (S == Seg::Hold) {
    const float eg = env * g1;
    for (; i < n; ++i) {
      float x, s;
      voice_step<Two, Saw, Third, Sub, M>(k, phase, phase2, phase3, sub_sign, sub_g, x, s);
      out[i] += mulb(x, eg);
      if constexpr (Sub) {
        out[i] += mulb(s, sub_g * env);
      }
      if constexpr (Gate) {
        gate[i] += env;
      }
    }
  } else {
    for (; i < n;) {
      float x, s;
      voice_step<Two, Saw, Third, Sub, M>(k, phase, phase2, phase3, sub_sign, sub_g, x, s);
      out[i] += mulb(x * env, g1);
      if constexpr (Sub) {
        out[i] += mulb(s, sub_g * env);
      }
      if constexpr (Gate) {
        gate[i] += env;
      }
      ++i;
      if constexpr (S == Seg::Attack) {
        env += k.attack_step;
        if (env >= 1.0f) {
          env = 1.0f;
          done = true;
          break;
        }
      } else {
        env *= k.release_coef;
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
  v.phase3 = phase3;
  v.sub_sign = sub_sign;
  v.sub_g = sub_g;
  v.env = env;
  return i;
}

template <bool Two, bool Saw, bool Gate, bool Third = false, bool Sub = false, bool M = false>
inline void run_voice(VoiceRun& v, std::uint8_t& stage_in, float* out, float* gate, std::uint32_t n, float g1) noexcept {
  // stage_in: 1 Attack, 2 Hold, 3 Release (the Stage values); written back as the stage the voice ends in
  std::uint32_t i = 0;
  bool done = false;
  if (stage_in == 1) {
    i = run_segment<Two, Saw, Gate, Seg::Attack, Third, Sub, M>(v, out, gate, i, n, g1, done);
    if (!done) {
      return;
    }
    stage_in = 2;
  }
  if (stage_in == 2) {
    run_segment<Two, Saw, Gate, Seg::Hold, Third, Sub, M>(v, out, gate, i, n, g1, done);
    return;
  }
  run_segment<Two, Saw, Gate, Seg::Release, Third, Sub, M>(v, out, gate, i, n, g1, done);
  if (done) {
    stage_in = 0;
  }
}

// The table of the specialised paths with the gate: one indirect call a voice a block, none in a sample loop. Index = Two | Saw << 1 | Third << 2 |
// Sub << 3 | M << 4; a Third without Two is not a voice (it aliases the one without).
using VoiceFn = void (*)(VoiceRun&, std::uint8_t&, float*, float*, std::uint32_t, float);

template <unsigned I>
constexpr VoiceFn voice_fn() noexcept {
  constexpr bool two = (I & 1u) != 0;
  return &run_voice<two, (I & 2u) != 0, true, two && (I & 4u) != 0, (I & 8u) != 0, (I & 16u) != 0>;
}

template <std::size_t... Is>
constexpr std::array<VoiceFn, sizeof...(Is)> make_voice_table(std::index_sequence<Is...>) noexcept {
  return {{voice_fn<static_cast<unsigned>(Is)>()...}};
}

constexpr std::array<VoiceFn, 32> kVoiceTable = make_voice_table(std::make_index_sequence<32>{});

// ---- The generic path (run_ext): what cannot be specialised, a third oscillator in its gain ramp and the plan's `full` morph. It has branches on
// the features in the sample loop: about 11,500 cycles a block a voice against about 5,000 on a specialised one (measured on the unit, U10b).

struct ExtRun {
  float phase, phase2, phase3, r3, sub_sign, env;
  Edge e1, e2, e3;
  float attack_step, release_coef, osc_gain;
  float sub_g, sub_dg;     // SUB's level a sample (including the voice gain), ramped over the block
  float r3_target, ramp;   // the third oscillator's level goal and its step a sample
  Morph mo;
  bool m1, m2, m3, two, third, sub, saw;
};

void run_ext(ExtRun& v, std::uint8_t& stage, float* out, float* gate, std::uint32_t n) noexcept {
  float phase = v.phase;
  float phase2 = v.phase2;
  float phase3 = v.phase3;
  float r3 = v.r3;
  float sub_sign = v.sub_sign;
  float sub_g = v.sub_g;
  float env = v.env;
  for (std::uint32_t i = 0; i < n && stage != 0; ++i) {
    const float p = phase;
    float x = v.m1 ? morph_osc(p, v.e1, v.mo) : (v.saw ? osc<true>(p, v.e1) : osc<false>(p, v.e1));
    phase += v.e1.dt;
    const bool wrapped = phase >= 1.0f;
    phase -= wrapped ? 1.0f : 0.0f;
    float g = v.osc_gain;
    if (v.two) {
      x += v.m2 ? morph_osc(phase2, v.e2, v.mo) : (v.saw ? osc<true>(phase2, v.e2) : osc<false>(phase2, v.e2));
      phase2 += v.e2.dt;
      phase2 -= phase2 >= 1.0f ? 1.0f : 0.0f;
      g *= 0.5f;
      if (v.third) {
        if (r3 < v.r3_target) {
          r3 += v.ramp;
          r3 = r3 > v.r3_target ? v.r3_target : r3;
        } else if (r3 > v.r3_target) {
          r3 -= v.ramp;
          r3 = r3 < v.r3_target ? v.r3_target : r3;
        }
        if (r3 > 0.0f) {
          x += r3 * (v.m3 ? morph_osc(phase3, v.e3, v.mo) : (v.saw ? osc<true>(phase3, v.e3) : osc<false>(phase3, v.e3)));
          phase3 += v.e3.dt;
          phase3 -= phase3 >= 1.0f ? 1.0f : 0.0f;
          g *= 1.0f - kThirdGain * r3;
        }
      }
    }
    float y = x * g;
    if (v.sub) {
      sub_g += v.sub_dg;
      y += sub_osc(sub_sign, p, v.e1) * sub_g;
      sub_sign = wrapped ? -sub_sign : sub_sign;
    }
    out[i] += y * env;
    gate[i] += env;
    if (stage == 1) {
      env += v.attack_step;
      if (env >= 1.0f) {
        env = 1.0f;
        stage = 2;
      }
    } else if (stage == 3) {
      env *= v.release_coef;
      if (env < kIdle) {
        env = 0.0f;
        stage = 0;
      }
    }
  }
  v.phase = phase;
  v.phase2 = phase2;
  v.phase3 = phase3;
  v.r3 = r3;
  v.sub_sign = sub_sign;
  v.env = env;
}

}  // namespace

void Carrier::set_detune_cents(float cents) noexcept {
  cents = cents < 0.0f ? 0.0f : (cents > 100.0f ? 100.0f : cents);
  detune_ratio_ = cents > 0.0f ? std::exp2(cents * (1.0f / 1200.0f)) : 1.0f;
  detune_down_ = cents > 0.0f ? std::exp2(cents * (-1.0f / 1200.0f)) : 1.0f;
}

void Carrier::set_tilt_db(float db) noexcept {
  db = db < -12.0f ? -12.0f : (db > 12.0f ? 12.0f : db);
  tilt_target_hi_ = db != 0.0f ? std::exp2(db * 0.0955054f) : 1.0f;   // asymptotes 1.15 x 10^(db / 40): about +db / 2 on the high side at 10 kHz (a one-pole shelf leaks)
  tilt_target_lo_ = db != 0.0f ? std::exp2(db * -0.0955054f) : 1.0f;  // and -db / 2 on the low side
}

void Carrier::prepare(float sample_rate) noexcept {
  fs_ = sample_rate;
  for (Voice& v : voices_) {
    v = Voice{};
  }
  counter_ = 0;
  sub_cur_ = 0.0f;
  tilt_lo_ = tilt_target_lo_;
  tilt_hi_ = tilt_target_hi_;
  tilt_lp_ = 0.0f;
  tilt_a_ = 1.0f - std::exp(-6.2831853f * 700.0f / fs_);
  ramp_step_ = 1.0f / (0.003f * fs_);
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
    pick->phase3 = 0.0f;
    pick->r3 = 0.0f;
    pick->sub_sign = 1.0f;
    pick->rs = 0.0f;
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
  const bool two = detune_ratio_ != 1.0f;  // SPREAD > 0: oscillators 2 and 3 exist
  const bool morph = shape_ != 0.0f && shape_ != 1.0f;
  const bool saw = shape_ < 0.5f;  // the oscillator a voice runs when the plan has no morph for it: the nearer end
  const bool sub_on = sub_ > 0.0f || sub_cur_ > 0.0f;
  const bool tilt_on = tilt_target_hi_ != 1.0f || tilt_hi_ != 1.0f;
  const bool want_gate = gate_out != nullptr || noise_ > 0.0f;
  const float osc_gain = kVoiceGain * (1.0f - noise_);
  const float g1 = two ? 0.5f * osc_gain : osc_gain;  // two oscillators keep the voice's level
  // The plan, once a block, from the sounding voices (it changes when a voice starts or ends): the morph and the third go to the newest voices,
  // SUB to the lowest notes.
  const CarrierPlan plan = plan_oscillators(sounding_voices(), two, morph, sub_on, tilt_on);
  const Morph mo = morph ? make_morph(shape_) : Morph{};
  const float rn = n > 0 ? 1.0f / static_cast<float>(n) : 0.0f;
  const float sub_goal = sub_ * osc_gain * 0.5f;
  const float sub_from = sub_cur_ * osc_gain * 0.5f;
  const float rs_step = static_cast<float>(n) * ramp_step_;  // a voice's SUB level moves 0..1 in about 3 ms
  for (Voice& v : voices_) {
    if (v.stage == Stage::Idle) {
      continue;
    }
    unsigned rank = 0;   // 0 is the newest sounding voice
    unsigned lower = 0;  // 0 is the lowest note (a tie: the lower voice slot)
    for (const Voice& o : voices_) {
      if (o.stage != Stage::Idle) {
        rank += o.age > v.age ? 1u : 0u;
        lower += (o.note < v.note || (o.note == v.note && &o < &v)) ? 1u : 0u;
      }
    }
    const bool m1 = morph && (plan.full || rank < plan.morph1);
    const bool want3 = two && rank < plan.third;
    if (!two) {
      v.r3 = 0.0f;  // no third oscillator without a second
    }
    // SUB: this voice's level steps toward its goal once a block; a voice that is not on SUB starts it from level 0 and the sign +1.
    float rs = v.rs;
    if (sub_on) {
      const float goal = lower < plan.sub ? 1.0f : 0.0f;
      rs = rs < goal ? (rs + rs_step < goal ? rs + rs_step : goal) : (rs - rs_step > goal ? rs - rs_step : goal);
    } else {
      rs = 0.0f;
    }
    if (v.rs <= 0.0f) {
      v.sub_sign = 1.0f;
    }
    const bool sub_v = rs > 0.0f || v.rs > 0.0f;
    const float sg0 = v.rs * sub_from;
    const float sg1 = rs * sub_goal;
    const float sub_dg = (sg1 - sg0) * rn;
    v.rs = rs;
    const float base = glide_ > 0.0f ? v.cur : v.inc;
    const float dt1 = base * bend_ratio_ > 0.45f ? 0.45f : base * bend_ratio_;  // x 1.0f is exact: Bend 0 is the unbent path
    float dt2 = dt1 * detune_ratio_;
    dt2 = dt2 > 0.45f ? 0.45f : dt2;
    float dt3 = dt1 * detune_down_;
    dt3 = dt3 > 0.45f ? 0.45f : dt3;
    std::uint8_t stage = static_cast<std::uint8_t>(v.stage);
    static_assert(static_cast<unsigned>(Stage::Attack) == 1 && static_cast<unsigned>(Stage::Hold) == 2 &&
                      static_cast<unsigned>(Stage::Release) == 3, "run_voice reads the Stage values");
    const bool third_steady = want3 && v.r3 == 1.0f;  // a settled third oscillator: a specialised path with one more, no ramp in the loop
    const bool third_off = !want3 && v.r3 == 0.0f;
    if (force_ext_ || plan.full || !(third_steady || third_off)) {  // the generic path: a third in its ramp, or every oscillator morphing
      if (want3 && v.r3 == 0.0f) {  // the third enters at an offset phase, from level 0
        v.phase3 = v.phase2 + 0.37f;
        v.phase3 -= v.phase3 >= 1.0f ? 1.0f : 0.0f;
      }
      ExtRun r{v.phase, two ? v.phase2 : v.phase, v.phase3, v.r3, v.sub_sign, v.env, make_edge(dt1), make_edge(dt2), make_edge(dt3),
               attack_step_, release_coef_, osc_gain, sg0, sub_dg, want3 ? 1.0f : 0.0f, ramp_step_, mo,
               m1, morph && plan.full, morph && plan.full, two, want3 || v.r3 > 0.0f, sub_v, saw};
      run_ext(r, stage, out, gate, n);
      v.phase = r.phase;
      v.phase2 = two ? r.phase2 : r.phase;
      v.phase3 = r.phase3;
      v.r3 = r.r3;
      v.sub_sign = r.sub_sign;
      v.env = r.env;
    } else if (!third_steady && !sub_v && !m1) {  // the plain path: the loop of before
      VoiceRun r{v.phase, two ? v.phase2 : v.phase, v.env, make_edge(dt1), make_edge(dt2), attack_step_, release_coef_};
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
    } else {  // a specialised path with a third, SUB and/or the morph on oscillator 1: one indirect call, with the gate
      VoiceRun r{v.phase, two ? v.phase2 : v.phase, v.env, make_edge(dt1), make_edge(dt2), attack_step_, release_coef_,
                 v.phase3, third_steady ? make_edge(dt3) : Edge{}, v.sub_sign, sg0, sub_dg, mo};
      const unsigned idx = (two ? 1u : 0u) | (saw ? 2u : 0u) | (third_steady ? 4u : 0u) | (sub_v ? 8u : 0u) | (m1 ? 16u : 0u);
      kVoiceTable[idx](r, stage, out, gate, n, third_steady ? g1 * (1.0f - kThirdGain) : g1);
      v.phase = r.phase;
      v.phase2 = two ? r.phase2 : r.phase;
      v.phase3 = r.phase3;
      v.sub_sign = r.sub_sign;
      v.env = r.env;
    }
    v.stage = static_cast<Stage>(stage);
  }
  sub_cur_ = sub_;
  if (gate_out != nullptr) {
    for (std::uint32_t i = 0; i < n; ++i) {
      gate_out[i] = gate[i] < 1.0f ? gate[i] : 1.0f;
    }
  }
  if (tilt_on) {  // the shelf on the voice sum, before the noise: lp + (x - lp) with the two sides' gains, smoothed a block at a time
    tilt_lo_ += (tilt_target_lo_ - tilt_lo_) * 0.35f;
    tilt_hi_ += (tilt_target_hi_ - tilt_hi_) * 0.35f;
    if (tilt_hi_ - tilt_target_hi_ < 1.0e-4f && tilt_target_hi_ - tilt_hi_ < 1.0e-4f) {
      tilt_lo_ = tilt_target_lo_;
      tilt_hi_ = tilt_target_hi_;
    }
    float lp = tilt_lp_;
    for (std::uint32_t i = 0; i < n; ++i) {
      lp += tilt_a_ * (out[i] - lp);
      out[i] = tilt_lo_ * lp + tilt_hi_ * (out[i] - lp);
    }
    tilt_lp_ = lp;
  } else {
    tilt_lp_ = 0.0f;
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
