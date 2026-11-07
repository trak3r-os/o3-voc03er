// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The carrier's oscillator plan (U10a): how many of the sounding voices get the third (unison) oscillator and how many oscillators run
// the SHAPE morph, for the number of sounding voices and the features that are on. A pure function with its costs in one place
// (carrier_cost), so a measurement on the unit replaces the constants and nothing else.
//
// Every figure is an estimate [est] in cycles a 64-frame block: only the unit measures them (U11). The budget is what 16 oscillators cost
// today (2 on each of 8 voices) plus a room of 8,000 (the 3.5 % of the BANDS row's headroom is 9,143, which leaves about 1,100 for ROBOT).
//
// The rules, in priority order: every voice has oscillators 1 and 2; then SUB and TILT when on; then the morph on oscillator 1 of each voice;
// then the morph on oscillator 2; then third oscillators (each costs its morph too when the morph is on). What does not fit is left out,
// the newest voices served first: voice rank 0 is the newest. The plan is recomputed when a voice starts or ends, never per sample.

#include <cstdint>

namespace o3_voc03er {

namespace carrier_cost {
inline constexpr unsigned kOsc = 2176;    // an oscillator, 34 cycles a sample x 64 (measured at 34 on the unit, 2026-11)
inline constexpr unsigned kMorph = 576;   // the morph's second BLEP and shifts on an oscillator, 9 a sample [est]
inline constexpr unsigned kSub = 320;     // SUB, a voice, 5 a sample [est]
inline constexpr unsigned kTilt = 770;    // TILT, once on the sum, 12 a sample [est]
inline constexpr unsigned kMaxVoices = 8;
inline constexpr unsigned kRoom = 8000;
inline constexpr unsigned kBudget = 2u * kMaxVoices * kOsc + kRoom;  // 16 oscillators today + the room: 42,816
}  // namespace carrier_cost

struct CarrierPlan {
  std::uint8_t third;   // voices (the newest) that have the third oscillator
  std::uint8_t morph1;  // voices whose oscillator 1 runs the morph
  std::uint8_t morph2;  // voices whose oscillator 2 runs the morph
  std::uint8_t morph3;  // voices whose third oscillator runs the morph (the same as `third` when the morph is on, else 0)
  unsigned cycles;      // the estimated cost of the whole carrier with this plan
  constexpr unsigned morphed() const noexcept { return unsigned{morph1} + morph2 + morph3; }  // oscillators that morph
};

// n sounding voices (clamped to 8); `spread`: a third oscillator is asked for; `morph`: SHAPE is strictly between its ends; `sub`, `tilt`: on.
constexpr CarrierPlan plan_oscillators(unsigned n, bool spread, bool morph, bool sub, bool tilt) noexcept {
  using namespace carrier_cost;
  CarrierPlan p{0, 0, 0, 0, 0};
  n = n > kMaxVoices ? kMaxVoices : n;
  if (n == 0) {
    return p;
  }
  unsigned cost = 2u * n * kOsc + (sub ? n * kSub : 0u) + (tilt ? kTilt : 0u);
  unsigned rem = cost < kBudget ? kBudget - cost : 0u;
  if (morph) {
    unsigned m1 = rem / kMorph;
    m1 = m1 > n ? n : m1;
    rem -= m1 * kMorph;
    unsigned m2 = rem / kMorph;
    m2 = m2 > n ? n : m2;
    rem -= m2 * kMorph;
    p.morph1 = static_cast<std::uint8_t>(m1);
    p.morph2 = static_cast<std::uint8_t>(m2);
    cost += (m1 + m2) * kMorph;
  }
  if (spread) {
    const unsigned each = kOsc + (morph ? kMorph : 0u);
    unsigned t = rem / each;
    t = t > n ? n : t;
    p.third = static_cast<std::uint8_t>(t);
    p.morph3 = morph ? p.third : std::uint8_t{0};
    cost += t * each;
  }
  p.cycles = cost;
  return p;
}

}  // namespace o3_voc03er
