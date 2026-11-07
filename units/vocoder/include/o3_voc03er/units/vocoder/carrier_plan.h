// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The carrier's feature plan (U10c): how many of the sounding voices get SUB, the SHAPE morph on oscillator 1 and the third (unison)
// oscillator, for the number of sounding voices and the features that are on. A pure function with its costs in one place (carrier_cost), so a
// measurement on the unit replaces the constants and nothing else. Recomputed once a block, never per sample.
//
// Figures are cycles a 64-frame block. [measured] ones were read on the unit (candidate 4ce71cd, BANDS, 8 notes + 4 loops, `render last`); [est]
// ones are estimates that the unit has yet to confirm (the check owed after U10c: each feature's cost at 8 and at 6 notes). A voice on a
// specialised path (run_voice, features as template parameters) costs a few thousand; a voice on the generic path (run_ext, branches on the
// features in the loop) costs more than twice that, so the plan keeps voices off run_ext whenever it can.
//
// The rules, in this order, each taking what is left (`rem`) of the budget after the n voices and TILT: SUB, then the morph on oscillator 1, then
// the third oscillator. `full`: when the morph is on and every voice could run on the generic path inside the budget (n x kExt + TILT), every
// voice does, with all its oscillators morphing; the SUB and third of the plan stay as they are. Who gets what is decided by the caller:
// SUB goes to the lowest notes, the morph and the third to the newest voices.

#include <cstdint>

namespace o3_voc03er {

namespace carrier_cost {
inline constexpr unsigned kVoice = 5000;   // a voice on the specialised path, two oscillators [measured, about: BANDS at SPREAD 0, 164.8k with 8 notes]
inline constexpr unsigned kThird = 2000;   // a third oscillator on the specialised path [est]
inline constexpr unsigned kSub = 1300;     // SUB on a specialised voice [est]
inline constexpr unsigned kMorph1 = 2200;  // the morph on oscillator 1 of a specialised voice [est]
inline constexpr unsigned kExt = 11500;    // a voice on the generic path [measured: about 11,200 on top of nothing at 4 to 8 notes]
inline constexpr unsigned kTilt = 770;     // TILT, once on the sum, 12 a sample [est]
inline constexpr unsigned kRoom = 6000;    // what stays free for the rest of the unit: the limit 69 % peak is 180,200 and BANDS at SPREAD 0 reads 164.8k
inline constexpr unsigned kMaxVoices = 8;
inline constexpr unsigned kBudget = kMaxVoices * kVoice + kRoom;  // 46,000
}  // namespace carrier_cost

struct CarrierPlan {
  std::uint8_t sub;     // voices (the lowest notes) that sound the SUB
  std::uint8_t morph1;  // voices (the newest) whose oscillator 1 runs the morph
  std::uint8_t third;   // voices (the newest) that have the third oscillator
  bool full;            // every voice runs the generic path with all its oscillators morphing
  unsigned cycles;      // the estimated cost of the whole carrier with this plan
};

// n sounding voices (clamped to 8); `spread`: a third oscillator is asked for; `morph`: SHAPE is strictly between its ends; `sub`, `tilt`: on.
constexpr CarrierPlan plan_oscillators(unsigned n, bool spread, bool morph, bool sub, bool tilt) noexcept {
  using namespace carrier_cost;
  CarrierPlan p{0, 0, 0, false, 0};
  n = n > kMaxVoices ? kMaxVoices : n;
  if (n == 0) {
    return p;
  }
  const unsigned base = n * kVoice + (tilt ? kTilt : 0u);
  unsigned rem = base < kBudget ? kBudget - base : 0u;
  unsigned cost = base;
  if (sub) {
    unsigned k = rem / kSub;
    k = k > n ? n : k;
    p.sub = static_cast<std::uint8_t>(k);
    rem -= k * kSub;
    cost += k * kSub;
  }
  if (morph) {
    unsigned k = rem / kMorph1;
    k = k > n ? n : k;
    p.morph1 = static_cast<std::uint8_t>(k);
    rem -= k * kMorph1;
    cost += k * kMorph1;
  }
  if (spread) {
    unsigned k = rem / kThird;
    k = k > n ? n : k;
    p.third = static_cast<std::uint8_t>(k);
    rem -= k * kThird;
    cost += k * kThird;
  }
  if (morph && n * kExt + (tilt ? kTilt : 0u) <= kBudget) {
    p.full = true;
    cost = n * kExt + (tilt ? kTilt : 0u);  // the generic path does it all: its cost replaces the specialised ones
  }
  p.cycles = cost;
  return p;
}

}  // namespace o3_voc03er
