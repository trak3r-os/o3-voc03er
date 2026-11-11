// SPDX-License-Identifier: GPL-3.0-or-later
// The tests' parameter readers look at the engine's first units and take the value of largest magnitude (the vocoder is the one that answers). Since V1 the
// TRACT unit is one of them, and its ten ids (RES 0.5, GLIDE 30, ...) collide with the vocoder's low ids: the readers skip it.
#pragma once

#include <o3sdk/sim/harness.h>

#include <cstring>

namespace o3_voc03er::test {

inline bool is_tract_unit(o3sdk::sim::Harness& h, unsigned u) {
  const o3sdk::dsp::UnitInfo* info = h.engine().unit_info(static_cast<o3sdk::dsp::UnitId>(u));
  return info != nullptr && std::strcmp(info->name, "TRACT") == 0;
}

}  // namespace o3_voc03er::test
