// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/bands.h>

#include <cmath>

#include "nofma.h"

namespace o3_voc03er {

namespace {

constexpr float kLowHz = 125.0f;
constexpr float kOctavesPerBand = 0.4f;  // centre i = 125 x 2^(0.4 i)
constexpr float kBandQ = 2.3f;           // of each biquad; two cascaded
constexpr float kPreEmphasis = 0.7f;  // y = x - 0.7 x[-1]: about -10 dB at 100 Hz, +4.6 dB at 8 kHz
constexpr float kRectToPeak = 1.5707964f;  // pi / 2: the follower of a sine reads its amplitude
constexpr float kFlush = 1.0e-15f;

constexpr float kHighpassHz = 80.0f;
constexpr float kAgcTarget = 0.1f;     // mean-abs level the analysis path is brought to
constexpr float kAgcGate = 0.002f;     // mean-abs level (about -54 dB) at and under which the gain is 1
constexpr float kAgcKnee = 0.008f;     // the full gain from here up: between the two it fades to 1, so noise at the gate does not flutter
constexpr float kAgcMaxGain = 20.0f;
constexpr float kAgcMinGain = 0.05f;
constexpr float kAgcAttackMs = 5.0f;
constexpr float kAgcReleaseMs = 100.0f;

constexpr unsigned kUvFirstBand = 11;    // noise goes into the carrier of bands 11..15
constexpr unsigned kUvHiFirst = 13;      // the ratio looks at bands 13..15
constexpr float kUvOpen = 0.35f;
constexpr float kUvClose = 0.28f;        // hysteresis
constexpr float kUvMinSum = 0.003f;      // followers' sum under which there is nothing to decide on
constexpr float kUvFadeMs = 5.0f;
constexpr float kUvNoiseGain = 0.5f;

inline float flush(float s) noexcept { return (s > kFlush || s < -kFlush) ? s : 0.0f; }
inline float one_pole(float ms, float fs) noexcept { return 1.0f - std::exp(-1000.0f / (ms * fs)); }

}  // namespace

void Bands::prepare(float sample_rate) noexcept {
  fs_ = sample_rate;
  for (unsigned b = 0; b < kBands; ++b) {
    const float f = kLowHz * std::exp2(kOctavesPerBand * static_cast<float>(b));
    center_[b] = f;
    const float w0 = 6.2831853f * f / fs_;
    const float alpha = std::sin(w0) / (2.0f * kBandQ);
    const float a0 = 1.0f + alpha;
    b0_[b] = alpha / a0;
    a1_[b] = -2.0f * std::cos(w0) / a0;
    a2_[b] = (1.0f - alpha) / a0;
    const float sign = (b & 1u) ? -1.0f : 1.0f;
    const float b02 = b0_[b] * b0_[b];
    rect_gain_[b] = kRectToPeak * b02;  // the sections run with b0 folded out: the cascade's output is y / b0^2
    syn_gain_[b] = sign * b02;
  }
  // 80 Hz high-pass, Butterworth (RBJ, Q = 1/sqrt 2)
  const float w = 6.2831853f * kHighpassHz / fs_;
  const float cw = std::cos(w);
  const float alpha = std::sin(w) * 0.70710678f;
  const float a0 = 1.0f + alpha;
  hp_b0_ = (1.0f + cw) * 0.5f / a0;
  hp_b1_ = -(1.0f + cw) / a0;
  hp_b2_ = hp_b0_;
  hp_a1_ = -2.0f * cw / a0;
  hp_a2_ = (1.0f - alpha) / a0;
  lvl_att_ = one_pole(kAgcAttackMs, fs_);
  lvl_rel_ = one_pole(kAgcReleaseMs, fs_);
  uv_step_ = 1.0f / (kUvFadeMs * 0.001f * fs_);
  set_envelope(2.0f, 25.0f, 40.0f);
  reset();
}

void Bands::hold_envelopes(float level) noexcept {
  for (unsigned b = 0; b < kBands; ++b) {
    atk_[b] = 0.0f;
    rel_[b] = 0.0f;
    env_[b] = level;
  }
}

void Bands::reset() noexcept {
  restart();
  level_ = kAgcTarget;  // starts at the target: unity gain, no blast at the first loud sample
  gain_ = 1.0f;
}

void Bands::restart() noexcept {
  for (unsigned b = 0; b < kBands; ++b) {
    env_[b] = 0.0f;
    for (unsigned k = 0; k < 4; ++k) {
      mod_state_[b][k] = 0.0f;
      car_state_[b][k] = 0.0f;
    }
  }
  pre_prev_ = 0.0f;
  hp_s1_ = 0.0f;
  hp_s2_ = 0.0f;
  uv_ = 0.0f;
  uv_open_ = false;
}

void Bands::set_envelope(float attack_ms, float release_ms, float low_release_ms) noexcept {
  attack_ms = attack_ms < 0.05f ? 0.05f : attack_ms;
  release_ms = release_ms < 0.05f ? 0.05f : release_ms;
  low_release_ms = low_release_ms < 0.05f ? 0.05f : low_release_ms;
  for (unsigned b = 0; b < kBands; ++b) {
    atk_[b] = one_pole(attack_ms, fs_);
    rel_[b] = one_pole(b < 3 ? low_release_ms : release_ms, fs_);
  }
}

void Bands::process(const float* modulator, const float* carrier, float* out, std::uint32_t n, const float* gate) noexcept {
  if (n > kMaxFrames) {
    n = kMaxFrames;
  }
  // front end: high-pass, then the AGC level; the gain is applied in the second pass, with the pre-emphasis
  float hp[kMaxFrames];
  float pre[kMaxFrames];
  float lvl = level_;
  float h1 = hp_s1_, h2 = hp_s2_;
  for (std::uint32_t i = 0; i < n; ++i) {
    const float x = clean(modulator[i]);
    const float y = hp_b0_ * x + h1;
    h1 = hp_b1_ * x - hp_a1_ * y + h2;
    h2 = hp_b2_ * x - hp_a2_ * y;
    hp[i] = y;
    const float a = y < 0.0f ? -y : y;
    lvl += (a - lvl) * (a > lvl ? lvl_att_ : lvl_rel_);
  }
  hp_s1_ = flush(h1);
  hp_s2_ = flush(h2);
  level_ = flush(lvl);
  const bool agc_on = lvl > kAgcGate;
  float gt = 1.0f;  // gated: under the gate the gain is 1, silence and noise floor pass as they are
  if (agc_on) {
    gt = kAgcTarget / lvl;
    gt = gt > kAgcMaxGain ? kAgcMaxGain : (gt < kAgcMinGain ? kAgcMinGain : gt);
    if (lvl < kAgcKnee) {
      gt = 1.0f + (gt - 1.0f) * (lvl - kAgcGate) * (1.0f / (kAgcKnee - kAgcGate));
    }
  }
  float g = gain_;
  const float gstep = (gt - g) / static_cast<float>(n);
  float prev = pre_prev_;
  for (std::uint32_t i = 0; i < n; ++i) {
    g += gstep;
    const float x = hp[i] * g;
    pre[i] = x - kPreEmphasis * prev;
    prev = x;
    out[i] = 0.0f;
  }
  gain_ = gt;
  pre_prev_ = prev;

  // unvoiced decision (per block, from the followers as they stand) and the per-sample noise mix
  float sum = 0.0f, hi = 0.0f;
  for (unsigned b = 0; b < kBands; ++b) {
    sum += env_[b];
    hi += b >= kUvHiFirst ? env_[b] : 0.0f;
  }
  if (agc_on && sum > kUvMinSum) {
    const float ratio = hi / sum;
    uv_open_ = uv_open_ ? ratio > kUvClose : ratio > kUvOpen;
  } else {
    uv_open_ = false;
  }
  const float uv_target = uv_open_ ? 1.0f : 0.0f;
  const bool uv_live = uv_target > 0.0f || uv_ > 0.0f;
  float uvn[kMaxFrames];
  float carrier_uv[kMaxFrames];
  if (uv_live) {
    (noise_src_ != nullptr ? noise_src_ : &own_noise_)->fill(uvn, n);
    float u = uv_;
    for (std::uint32_t i = 0; i < n; ++i) {
      const float d = uv_target - u;
      u += d > uv_step_ ? uv_step_ : (d < -uv_step_ ? -uv_step_ : d);
      uvn[i] *= u * kUvNoiseGain * (gate != nullptr ? gate[i] : 1.0f);
    }
    uv_ = u;
  }

  for (unsigned b = 0; b < kBands; ++b) {
    const float a1 = a1_[b];
    const float a2 = a2_[b];
    const float atk = atk_[b];
    const float rel = rel_[b];
    const float gr = rect_gain_[b];
    const float gs = syn_gain_[b];
    const float* cin = carrier;
    if (uv_live && b >= kUvFirstBand) {
      for (std::uint32_t i = 0; i < n; ++i) {
        carrier_uv[i] = carrier[i] + uvn[i];
      }
      cin = carrier_uv;
    }
    float m1 = mod_state_[b][0], m2 = mod_state_[b][1], m3 = mod_state_[b][2], m4 = mod_state_[b][3];
    float c1 = car_state_[b][0], c2 = car_state_[b][1], c3 = car_state_[b][2], c4 = car_state_[b][3];
    float env = env_[b];
    for (std::uint32_t i = 0; i < n; ++i) {
      // analysis: two bandpass sections, transposed direct form II with b1 = 0, b2 = -b0, b0 folded out and the state negated:
      // w = x - n1; n1' = n2 + a1 w; n2' = x + a2 w. The cascade's output is y / b0^2; gr (rectifier) and gs (synthesis) restore it.
      const float x = pre[i];
      const float w1 = x - m1;
      m1 = m2 + mulb(a1, w1);
      m2 = x + mulb(a2, w1);
      const float w2 = w1 - m3;
      m3 = m4 + mulb(a1, w2);
      m4 = w1 + mulb(a2, w2);
      const float r = std::fabs(w2) * gr;
      env += mulb(r - env, r > env ? atk : rel);
      // synthesis
      const float u = cin[i];
      const float z1 = u - c1;
      c1 = c2 + mulb(a1, z1);
      c2 = u + mulb(a2, z1);
      const float z2 = z1 - c3;
      c3 = c4 + mulb(a1, z2);
      c4 = z1 + mulb(a2, z2);
      out[i] += mulb(z2 * env, gs);
    }
    mod_state_[b][0] = flush(m1);
    mod_state_[b][1] = flush(m2);
    mod_state_[b][2] = flush(m3);
    mod_state_[b][3] = flush(m4);
    car_state_[b][0] = flush(c1);
    car_state_[b][1] = flush(c2);
    car_state_[b][2] = flush(c3);
    car_state_[b][3] = flush(c4);
    env_[b] = flush(env);
  }
}

}  // namespace o3_voc03er
