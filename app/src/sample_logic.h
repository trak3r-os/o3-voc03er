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

// The top two rows (pads 0..23) are the slices of the selected slot.
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

// ---- slices (U5b) -----------------------------------------------------------------------------------------------------------------------

inline constexpr unsigned kMaxSlices = kTopPads;
inline constexpr unsigned kDefaultCuts = 8;
inline constexpr unsigned kReaders = 4;

struct Cut {
  std::uint32_t start = 0;  // samples from the start of the slot
  std::uint32_t len = 0;
};

// A slot's cuts. Until SAMPLE EDIT (U7) they are even; `length` is the slot's length they were built for (0: none yet).
struct SliceTable {
  std::uint32_t length = 0;
  std::uint8_t count = 0;
  Cut cut[kMaxSlices];
};

// `want` equal cuts of a slot of `length` samples (one slot is at most Loop::kMaxSamples, so length * 24 stays inside 32 bits). A slot shorter
// than `want` gets one cut a sample; an empty slot none. The last cut ends at the slot's end, so the cuts tile it.
inline void even_cuts(SliceTable& t, std::uint32_t length, unsigned want = kDefaultCuts) noexcept {
  const unsigned cap = want < kMaxSlices ? want : kMaxSlices;
  const unsigned n = length < cap ? static_cast<unsigned>(length) : cap;
  t.length = length;
  t.count = static_cast<std::uint8_t>(n);
  for (unsigned i = 0; i < n; ++i) {
    const std::uint32_t a = length * i / n;
    const std::uint32_t b = length * (i + 1u) / n;
    t.cut[i] = {a, b - a};
  }
}

// The Start the unit takes: the bank's samples, slot after slot of `per_loop` (the size bind_loops was given).
constexpr std::uint32_t region_start(unsigned slot, std::uint32_t per_loop, const Cut& c) noexcept { return slot * per_loop + c.start; }

// A slice sounds on the side of its slot; a slot on no side speaks (design section 1). true: the reader's Role is CAR.
constexpr bool slice_is_carrier(Side s) noexcept { return s == Side::Sounds; }

// What pick_reader needs of each reader (= the slot of the same number).
struct ReaderState {
  bool recording = false;  // a take is in it: never touched
  bool loop_on = false;    // its own slot's whole loop plays (or waits for the bar)
  bool region = false;     // a slice sounds on it
  bool sided = false;      // its slot is on a side: the unit routes that reader by CarSrc or ModSrc, not by Role, so a borrowed slice would land there
  std::uint32_t seq = 0;   // when that slice started (higher: newer)
};

// The reader a slice of `slot` plays on; -1 when none can. CHOKE ON: the slot's own reader, which a new slice retriggers. CHOKE OFF: the own
// reader if no slice sounds on it, else a free one, else the oldest slice voice is stolen. A reader that records is never taken, nor one whose
// whole loop plays while it is another slot's (a slice would pause it), nor one whose slot is on a side.
constexpr int pick_reader(const ReaderState (&r)[kReaders], unsigned slot, bool choke) noexcept {
  if (slot >= kReaders || r[slot].recording) {
    return -1;
  }
  if (choke || !r[slot].region) {
    return static_cast<int>(slot);
  }
  int oldest = -1;
  for (unsigned k = 0; k < kReaders; ++k) {
    if (k == slot || r[k].recording || r[k].loop_on || r[k].sided) {
      continue;
    }
    if (!r[k].region) {
      return static_cast<int>(k);
    }
    if (oldest < 0 || r[k].seq < r[static_cast<unsigned>(oldest)].seq) {
      oldest = static_cast<int>(k);
    }
  }
  if (oldest < 0 || r[slot].seq < r[static_cast<unsigned>(oldest)].seq) {
    oldest = static_cast<int>(slot);  // the own reader sounds a slice too: it competes for the steal
  }
  return oldest;
}

// A slice that has not shown in LoopStatus::region yet has been asked at most this long ago (the render context publishes once a block).
inline constexpr std::uint64_t kStartGraceMs = 60;

// A slice voice has ended: it was seen sounding and no longer is, or it was never seen within the grace.
constexpr bool region_ended(bool seen, bool region_now, std::uint64_t age_ms) noexcept {
  return seen ? !region_now : (!region_now && age_ms >= kStartGraceMs);
}

// x of `offset` along a bar `w` wide for a slot of `length` samples (offset past the end clamps).
constexpr int bar_x(std::uint32_t offset, std::uint32_t length, int w) noexcept {
  return length == 0u ? 0 : static_cast<int>((offset < length ? offset : length) * static_cast<std::uint32_t>(w) / length);
}

}  // namespace o3_voc03er::sample
