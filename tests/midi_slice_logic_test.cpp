// SPDX-License-Identifier: GPL-3.0-or-later
// MS1, slices on their own MIDI channel: the pure rules of the SLICE CH setting (SAME, 1..16, OFF) against the user's channel (omni too).
#include <doctest/doctest.h>

#include "../app/src/midi_slice_logic.h"

#include <cstring>
#include <initializer_list>

namespace ms = o3_voc03er::midislice;

TEST_CASE("midi slice logic: SAME fires slices from the route and listens to no other channel") {
  CHECK(ms::route_fires_slices(ms::kSame, 1));
  CHECK(ms::route_fires_slices(ms::kSame, 0));
  CHECK(ms::slice_channel(ms::kSame, 1) == 0u);
  for (unsigned ch = 1; ch <= 16; ++ch) {
    CHECK_FALSE(ms::fires(ms::kSame, 1, ch));
  }
}

TEST_CASE("midi slice logic: OFF fires nothing and the route plays the synth") {
  CHECK_FALSE(ms::route_fires_slices(ms::kOff, 1));
  CHECK_FALSE(ms::route_fires_slices(ms::kOff, 0));
  for (unsigned ch = 1; ch <= 16; ++ch) {
    CHECK_FALSE(ms::fires(ms::kOff, 1, ch));
    CHECK_FALSE(ms::fires(ms::kOff, 0, ch));
  }
  CHECK_FALSE(ms::needs_hint(ms::kOff, 0));
}

TEST_CASE("midi slice logic: a slice channel fires on that channel only and the route stops firing") {
  CHECK_FALSE(ms::route_fires_slices(10, 1));
  CHECK(ms::fires(10, 1, 10));
  CHECK_FALSE(ms::fires(10, 1, 1));
  CHECK_FALSE(ms::fires(10, 1, 9));
  CHECK(ms::slice_channel(10, 1) == 10u);
  CHECK(ms::takes_release(10, 1, 10));
  CHECK_FALSE(ms::takes_release(10, 1, 2));
}

TEST_CASE("midi slice logic: a slice channel equal to the user's is SAME") {
  CHECK(ms::route_fires_slices(3, 3));
  CHECK_FALSE(ms::fires(3, 3, 3));  // the unit takes it unmarked from the route
  CHECK(ms::slice_channel(3, 3) == 0u);
  CHECK_FALSE(ms::needs_hint(3, 3));
}

TEST_CASE("midi slice logic: the user's channel omni cannot reach the slice channel, and the screen says so") {
  CHECK(ms::needs_hint(10, 0));
  CHECK_FALSE(ms::needs_hint(10, 1));
  CHECK_FALSE(ms::needs_hint(ms::kSame, 0));
  CHECK_FALSE(ms::route_fires_slices(10, 0));  // the route takes everything and plays the synth
}

TEST_CASE("midi slice logic: values clamp, step without wrapping, and read back") {
  CHECK(ms::clamp(0) == ms::kSame);
  CHECK(ms::clamp(17) == ms::kOff);
  CHECK(ms::clamp(18) == ms::kSame);
  CHECK(ms::clamp(255) == ms::kSame);
  CHECK(ms::step(ms::kSame, -1) == ms::kSame);
  CHECK(ms::step(ms::kSame, 1) == 1);
  CHECK(ms::step(16, 1) == ms::kOff);
  CHECK(ms::step(ms::kOff, 1) == ms::kOff);
  CHECK(ms::step(ms::kOff, -1) == 16);
  CHECK(ms::step(99, 1) == 1);  // a damaged value is SAME first
  char s[12];
  ms::format(ms::kSame, s);
  CHECK(std::strcmp(s, "SAME") == 0);
  ms::format(ms::kOff, s);
  CHECK(std::strcmp(s, "OFF") == 0);
  ms::format(7, s);
  CHECK(std::strcmp(s, "7") == 0);
  ms::format(16, s);
  CHECK(std::strcmp(s, "16") == 0);
}

TEST_CASE("midi slice logic: the console's words") {
  std::uint8_t v = 99;
  CHECK(ms::parse("same", v));
  CHECK(v == ms::kSame);
  CHECK(ms::parse("off", v));
  CHECK(v == ms::kOff);
  CHECK(ms::parse("1", v));
  CHECK(v == 1);
  CHECK(ms::parse("16", v));
  CHECK(v == 16);
  v = 99;
  for (const char* bad : {"", "0", "17", "-1", "001", "1a", "OFF", "all", "100"}) {
    CHECK_FALSE(ms::parse(bad, v));
  }
  CHECK_FALSE(ms::parse(nullptr, v));
  CHECK(v == 99);
}
