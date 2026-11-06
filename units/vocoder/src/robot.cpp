// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/robot.h>

#include <o3_voc03er/units/vocoder/nofma.h>

#include <cmath>

namespace o3_voc03er {

namespace {
constexpr float kTiny = 1.0e-30f;
constexpr float kPeakMax = 1.0e6f;  // a non-finite or absurd peak is read as this, so the gain stays finite
}  // namespace

void RobotComp::prepare(float sample_rate) noexcept {
  att_ = std::exp(-static_cast<float>(kSeg) / (kAttackMs * 0.001f * sample_rate));
  rel_ = std::exp(-static_cast<float>(kSeg) / (kReleaseMs * 0.001f * sample_rate));
  reset();
}

void RobotComp::reset() noexcept {
  env_ = 0.0f;
  gain_ = kMakeup;
}

void RobotComp::process(float* x, std::uint32_t n) noexcept {
  float env = env_, g = gain_;
  for (std::uint32_t i = 0; i < n; i += kSeg) {
    const std::uint32_t len = n - i < kSeg ? n - i : kSeg;
    float peak = 0.0f;
    for (std::uint32_t k = 0; k < len; ++k) {
      const float a = std::fabs(x[i + k]);
      peak = a > peak ? a : peak;
    }
    peak = peak < kPeakMax ? peak : kPeakMax;  // false for NaN as well
    env = peak > env ? peak + mulb(att_, env - peak) : peak + mulb(rel_, env - peak);
    env = env > kTiny ? env : 0.0f;
    float target = kMakeup;
    if (env > kThreshold) {
      const float q = kThreshold / env;  // < 1
      const float r = std::sqrt(q);
      target = mulb(kMakeup, mulb(r, std::sqrt(r)));  // q^0.75
    }
    const float step = (target - g) / static_cast<float>(len);
    for (std::uint32_t k = 0; k < len; ++k) {
      g += step;
      x[i + k] = mulb(x[i + k], g);
    }
    g = target;  // the ramp lands on the target exactly, whatever the rounding
  }
  env_ = env;
  gain_ = g;
}

}  // namespace o3_voc03er
