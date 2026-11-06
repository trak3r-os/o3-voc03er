// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the vocoder unit. The slow level control Bands and Talk share: a mean-abs follower of the modulator (attack 50 ms, release
// 500 ms: it levels a singer, not a word, so the voice's own dynamics pass) and the gain that brings it to kAgcTarget, clamped to
// 0.05..20. It is gated: at and below -54 dB the gain is 1, so a noise floor is never lifted, and between kAgcGate and kAgcKnee it fades
// to 1, so noise at the gate does not flutter.

namespace o3_voc03er {

constexpr float kAgcTarget = 0.1f;     // mean-abs level the modulator is brought to
constexpr float kAgcGate = 0.002f;     // mean-abs level (about -54 dB) at and under which the gain is 1
constexpr float kAgcKnee = 0.008f;     // the full gain from here up
constexpr float kAgcMaxGain = 20.0f;
constexpr float kAgcMinGain = 0.05f;
constexpr float kAgcAttackMs = 50.0f;
constexpr float kAgcReleaseMs = 500.0f;

inline float agc_gain(float level) noexcept {
  if (!(level > kAgcGate)) return 1.0f;
  float g = kAgcTarget / level;
  g = g > kAgcMaxGain ? kAgcMaxGain : (g < kAgcMinGain ? kAgcMinGain : g);
  if (level < kAgcKnee) {
    g = 1.0f + (g - 1.0f) * (level - kAgcGate) * (1.0f / (kAgcKnee - kAgcGate));
  }
  return g;
}

}  // namespace o3_voc03er
