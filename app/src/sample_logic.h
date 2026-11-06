// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE's pure logic (docs/UI-DESIGN.md, "SAMPLE (SAMPLE PLAY): the slots"): SIDE against route::allowed, the SPEED and LEVEL steps, the
// dB text, the pads' split. Apart from the screen so tests/sample_test.cpp holds them. Nothing here allocates or reads the engine.

#include <o3_voc03er/route.h>

#include <cstdint>

namespace o3_voc03er::sample {

inline constexpr unsigned kSpeedCount = 3;
inline constexpr const char* kSpeedNames[kSpeedCount] = {"0.5X", "1X", "2X"};  // LoopField::Speed 0..2
inline constexpr const char* kDirNames[2] = {"FWD", "REV"};                    // LoopField::Dir 0..1

inline constexpr float kLevelMinDb = -24.0f;
inline constexpr float kLevelMaxDb = 12.0f;

// F2 SIDE: which picker the slot goes to.
enum class Side : std::uint8_t { Speaks, Sounds, None };
inline constexpr unsigned kSideCount = 3;
inline constexpr const char* kSideNames[kSideCount] = {"SPEAKS", "SOUNDS", "NONE"};

// The top two rows (pads 0..23) are slices, a later slice's; this one leaves them dark and silent.
inline constexpr unsigned kTopPads = 24;
constexpr bool chord_pad(unsigned pad) noexcept { return pad >= kTopPads; }

constexpr std::uint8_t slot_value(unsigned slot) noexcept { return static_cast<std::uint8_t>(route::kFirstSlot + slot); }

// The side `slot` is on in `r` (a slot is one reader, so at most one).
constexpr Side side_of(route::Route r, unsigned slot) noexcept {
  const std::uint8_t v = slot_value(slot);
  return r.speaks == v ? Side::Speaks : (r.sounds == v ? Side::Sounds : Side::None);
}

// What F2 does: `to` the route after it, `change` when it differs, `refused` with `why` (a static string) when nothing may change.
// A slot sits on one side: moving it frees the side it leaves (LIVE on SPEAKS, SYNTH on SOUNDS). An empty slot cannot be placed.
struct SidePlan {
  route::Route to;
  bool change = false;
  bool refused = false;
  const char* why = "";
};

constexpr SidePlan plan_side(route::Route cur, Side target, unsigned slot, bool empty) noexcept {
  SidePlan p;
  p.to = cur;
  if (slot >= route::kSlots || side_of(cur, slot) == target) {
    return p;
  }
  const std::uint8_t v = slot_value(slot);
  if (target != Side::None && empty) {
    p.refused = true;
    p.why = "EMPTY: NOTHING TO PLAY";
    return p;
  }
  route::Route n = cur;
  if (n.speaks == v) {
    n.speaks = route::kSpeaksLive;
  }
  if (n.sounds == v) {
    n.sounds = route::kSoundsSynth;
  }
  if (target == Side::Speaks) {
    n.speaks = v;
  } else if (target == Side::Sounds) {
    n.sounds = v;
  }
  if (!route::allowed(n)) {
    p.refused = true;
    p.why = "THE OTHER SIDE HOLDS IT";
    return p;
  }
  p.to = n;
  p.change = true;
  return p;
}

// The next side by `delta`, wrapping SPEAKS, SOUNDS, NONE.
constexpr Side step_side(Side s, int delta) noexcept {
  const int n = static_cast<int>(kSideCount);
  return static_cast<Side>(((static_cast<int>(s) + delta) % n + n) % n);
}

// SPEED held inside 0.5X..2X by a detent; a tap wraps.
constexpr unsigned step_speed(unsigned index, int delta, bool wrap) noexcept {
  const int n = static_cast<int>(kSpeedCount);
  const int v = static_cast<int>(index) + delta;
  if (wrap) {
    return static_cast<unsigned>((v % n + n) % n);
  }
  return v < 0 ? 0u : (v >= n ? kSpeedCount - 1u : static_cast<unsigned>(v));
}

// LEVEL by `delta_half` half-dB, held inside -24..+12 dB.
constexpr float step_level(float db, int delta_half) noexcept {
  const float v = db + static_cast<float>(delta_half) * 0.5f;
  return v < kLevelMinDb ? kLevelMinDb : (v > kLevelMaxDb ? kLevelMaxDb : v);
}

// The level in half-dB, rounded (-48..24).
constexpr int level_halves(float db) noexcept { return static_cast<int>(db * 2.0f + (db >= 0.0f ? 0.5f : -0.5f)); }

// "+3.5", "-12", "0" into out[8] (the unit's dB, without the unit).
inline void level_text(float db, char (&out)[8]) noexcept {
  const int h = level_halves(db);
  const unsigned a = static_cast<unsigned>(h < 0 ? -h : h);
  unsigned n = 0;
  out[n++] = h < 0 ? '-' : (h > 0 ? '+' : '0');
  if (h != 0) {
    const unsigned w = a / 2u;
    if (w >= 10u) {
      out[n++] = static_cast<char>('0' + w / 10u);
    }
    out[n++] = static_cast<char>('0' + w % 10u);
    if ((a & 1u) != 0u) {
      out[n++] = '.';
      out[n++] = '5';
    }
  }
  out[n] = '\0';
}

// 0..100 for the little bar under the LEVEL value.
constexpr int level_percent(float db) noexcept { return static_cast<int>((db - kLevelMinDb) * 100.0f / (kLevelMaxDb - kLevelMinDb) + 0.5f); }

}  // namespace o3_voc03er::sample
