// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/bands.h>

#include <cmath>

namespace o3_voc03er {

namespace {

constexpr float kLowHz = 100.0f;
constexpr float kHighHz = 8000.0f;
constexpr float kBandQ = 3.0f;       // of each biquad; two cascaded: a band about 0.4 of the spacing wide at -3 dB
constexpr float kPreEmphasis = 0.7f;  // y = x - 0.7 x[-1]: about -10 dB at 100 Hz, +4.6 dB at 8 kHz
constexpr float kRectToPeak = 1.5707964f;  // pi / 2: the follower of a sine reads its amplitude
constexpr float kFlush = 1.0e-15f;

inline float flush(float s) noexcept { return (s > kFlush || s < -kFlush) ? s : 0.0f; }

}  // namespace

void Bands::prepare(float sample_rate) noexcept {
  fs_ = sample_rate;
  const float ratio = std::pow(kHighHz / kLowHz, 1.0f / static_cast<float>(kBands - 1u));
  float f = kLowHz;
  for (unsigned b = 0; b < kBands; ++b) {
    center_[b] = f;
    const float w0 = 6.2831853f * f / fs_;
    const float alpha = std::sin(w0) / (2.0f * kBandQ);
    const float a0 = 1.0f + alpha;
    b0_[b] = alpha / a0;
    a1_[b] = -2.0f * std::cos(w0) / a0;
    a2_[b] = (1.0f - alpha) / a0;
    f *= ratio;
  }
  set_envelope(3.0f, 30.0f);
  reset();
}

void Bands::reset() noexcept {
  for (unsigned b = 0; b < kBands; ++b) {
    env_[b] = 0.0f;
    for (unsigned k = 0; k < 4; ++k) {
      mod_state_[b][k] = 0.0f;
      car_state_[b][k] = 0.0f;
    }
  }
  pre_prev_ = 0.0f;
}

void Bands::set_envelope(float attack_ms, float release_ms) noexcept {
  attack_ms = attack_ms < 0.05f ? 0.05f : attack_ms;
  release_ms = release_ms < 0.05f ? 0.05f : release_ms;
  attack_ = 1.0f - std::exp(-1000.0f / (attack_ms * fs_));
  release_ = 1.0f - std::exp(-1000.0f / (release_ms * fs_));
}

void Bands::process(const float* modulator, const float* carrier, float* out, std::uint32_t n) noexcept {
  if (n > kMaxFrames) {
    n = kMaxFrames;
  }
  float pre[kMaxFrames];
  float prev = pre_prev_;
  for (std::uint32_t i = 0; i < n; ++i) {
    const float x = clean(modulator[i]);
    pre[i] = x - kPreEmphasis * prev;
    prev = x;
    out[i] = 0.0f;
  }
  pre_prev_ = prev;

  const float atk = attack_;
  const float rel = release_;
  for (unsigned b = 0; b < kBands; ++b) {
    const float b0 = b0_[b];
    const float a1 = a1_[b];
    const float a2 = a2_[b];
    float m1 = mod_state_[b][0], m2 = mod_state_[b][1], m3 = mod_state_[b][2], m4 = mod_state_[b][3];
    float c1 = car_state_[b][0], c2 = car_state_[b][1], c3 = car_state_[b][2], c4 = car_state_[b][3];
    float env = env_[b];
    for (std::uint32_t i = 0; i < n; ++i) {
      // analysis: two bandpass sections, transposed direct form II with b1 = 0, b2 = -b0
      const float x = pre[i];
      const float y1 = b0 * x + m1;
      m1 = m2 - a1 * y1;
      m2 = -b0 * x - a2 * y1;
      const float y2 = b0 * y1 + m3;
      m3 = m4 - a1 * y2;
      m4 = -b0 * y1 - a2 * y2;
      const float r = (y2 < 0.0f ? -y2 : y2) * kRectToPeak;
      env += (r - env) * (r > env ? atk : rel);
      // synthesis
      const float u = carrier[i];
      const float z1 = b0 * u + c1;
      c1 = c2 - a1 * z1;
      c2 = -b0 * u - a2 * z1;
      const float z2 = b0 * z1 + c3;
      c3 = c4 - a1 * z2;
      c4 = -b0 * z1 - a2 * z2;
      out[i] += z2 * env;
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
