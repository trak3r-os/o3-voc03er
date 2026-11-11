// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The mouth the voiceprint draws in TRACT mode (docs/TRACT-KNOBS-PLAN.md 4.2): the tube's diameters from the app's own copy of the tract's
// parameters (VOWEL, TONGUE, JAW, LIPS and CLOSE, read from the engine on the main loop), through the public area function of o3-sdk's
// extras/tract. Never the unit's state: this is the same arithmetic the unit does, on the values the app wrote. Pure, float, no allocation; the main
// loop runs it at the UI rate (22 sections: a few hundred operations).

#include <o3sdk/extras/tract/shape.h>

#include <cstdint>

namespace o3_voc03er::mouth {

inline constexpr unsigned kTubeN = 22;         // the unit's tube sections (units/tract: Processor's N at 22,050 Hz)
inline constexpr float kDiameterMax = 2.0f;    // the widest section the profile can hold (the lips open to 1.9 in the area model)

struct Params {
  float vowel = 0.0f;   // TractUnit::Vowel, 0..1
  float tongue = 0.0f;  // -1..+1
  float jaw = 0.0f;     // -1..+1
  float lips = 0.0f;    // -1..+1
  unsigned close = 0;   // TractUnit::Close: 0 OPEN, 1 LIPS, 2 TIP, 3 BACK
};

constexpr float clamp01(float v) noexcept { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }

// The shape for the parameters, as Processor::set_bend documents it: the bend is added to the vowel's shape (the jaw lowers the height), clamped.
inline o3sdk::extras::tract::Shape shape_of(const Params& p) noexcept {
  o3sdk::extras::tract::Shape s = o3sdk::extras::tract::vowel_shape(clamp01(p.vowel));
  s.tongue = clamp01(s.tongue + p.tongue);
  s.height = clamp01(s.height - p.jaw);
  s.lips = clamp01(s.lips + p.lips);
  return s;
}

// The diameters of the kTubeN sections, glottis first, lips last, each as 0..255 for 0..kDiameterMax.
inline void profile(const Params& p, std::uint8_t (&out)[kTubeN]) noexcept {
  const float closure[3] = {p.close == 1u ? 1.0f : 0.0f, p.close == 2u ? 1.0f : 0.0f, p.close == 3u ? 1.0f : 0.0f};
  float d[kTubeN];
  o3sdk::extras::tract::diameters(shape_of(p), closure, kTubeN, d);
  for (unsigned i = 0; i < kTubeN; ++i) {
    const float t = d[i] * (255.0f / kDiameterMax);
    out[i] = static_cast<std::uint8_t>(t < 0.0f ? 0.0f : (t > 255.0f ? 255.0f : t));
  }
}

}  // namespace o3_voc03er::mouth
