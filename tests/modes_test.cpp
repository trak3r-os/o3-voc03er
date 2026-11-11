// SPDX-License-Identifier: GPL-3.0-or-later
// The one table of modes (app/include/o3_voc03er/modes.h): its count and names agree with the unit's Mode parameter, mode_index reads the
// unit's value as the screens always did, and the console's parser takes every mode word.
#include <doctest/doctest.h>

#include <o3_voc03er/input.h>
#include <o3_voc03er/modes.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <cmath>
#include <string>

using namespace o3_voc03er;

TEST_CASE("modes: kModeCount and the names are the unit's Mode range and names") {
  static Vocoder vo;
  vo.prepare(44100.0f);
  const auto& d = vo.info().params[Vocoder::Mode];
  CHECK(static_cast<unsigned>(d.max) + 1u == kModeCount);
  REQUIRE(d.names != nullptr);
  for (unsigned m = 0; m < kModeCount; ++m) {
    CHECK(std::string(d.names[m]) == kModeNames[m]);
  }
}

TEST_CASE("modes: mode_index rounds to the nearest mode and holds the ends") {
  // The thresholds the screens used before the table: 0.5, 1.5, 2.5.
  for (int t = -20; t <= 60; ++t) {
    const float v = static_cast<float>(t) * 0.1f;
    const unsigned old = v >= 2.5f ? 3u : (v >= 1.5f ? 2u : (v >= 0.5f ? 1u : 0u));
    CHECK(mode_index(v) == old);
  }
  CHECK(mode_index(1e9f) == kModeCount - 1u);
  CHECK(mode_index(-1e9f) == 0u);
  CHECK(mode_index(std::nanf("")) == 0u);
}

TEST_CASE("modes: the console's voc takes every mode word, and nothing else") {
  for (unsigned m = 0; m < kModeCount; ++m) {
    VocCommand c;
    CHECK(parse_voc(kModeWords[m], nullptr, c));
    CHECK(c.mode == m);
  }
  VocCommand c;
  CHECK_FALSE(parse_voc("BANDS", nullptr, c));
  CHECK_FALSE(parse_voc("", nullptr, c));
  CHECK_FALSE(parse_voc(nullptr, nullptr, c));
}
