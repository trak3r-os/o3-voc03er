// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The vocoder's modes as the app knows them: one count, the screens' names, the console's words, and one reading of the unit's Mode value.
// Every screen, the console and its parser use these, so a new mode is an entry in each table and a larger kModeCount;
// tests/modes_test.cpp holds kModeCount to the unit's own Mode range and names. Pure: no engine, no state.

namespace o3_voc03er {

// TRACT (4) is the carrier through the tract unit (units/tract): app.cpp apply_mode() raises the tract's MIX with it and drops it with any other mode.
inline constexpr unsigned kModeCount = 5;
inline constexpr unsigned kModeTract = 4;  // the unit's Mode value of TRACT
inline constexpr const char* kModeNames[kModeCount] = {"BANDS", "TALK", "TUNE", "ROBOT", "TRACT"};  // the screens' words (the unit's Mode names)
inline constexpr const char* kModeWords[kModeCount] = {"bands", "talk", "tune", "robot", "tract"};  // the console's words (`voc`, the status line)

// The mode index of the unit's Mode value (0 BANDS, 1 TALK, 2 TUNE, 3 ROBOT, 4 TRACT): the nearest whole value, held inside 0..kModeCount - 1 (a NaN
// reads as 0).
constexpr unsigned mode_index(float v) noexcept {
  unsigned m = 0;
  while (m + 1u < kModeCount && v >= static_cast<float>(m) + 0.5f) {
    ++m;
  }
  return m;
}

}  // namespace o3_voc03er
