// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE REC's pure logic (docs/UI-DESIGN.md, "SAMPLE REC"): the LENGTH steps, where a take goes AFTER it, bars and seconds as the screen shows
// them. Apart from the screen so tests/sample_rec_test.cpp holds them. Nothing here allocates or reads the engine.

#include <o3_voc03er/route.h>

#include <o3sdk/core/audio_clock.h>

#include <cstdint>

namespace o3_voc03er::sample_rec {

// F3 LENGTH: FREE (REC again ends it) or a count of bars the take stops at by itself.
inline constexpr unsigned kLengthCount = 5;
inline constexpr unsigned kLengthBars[kLengthCount] = {0, 1, 2, 4, 8};
inline constexpr const char* kLengthNames[kLengthCount] = {"FREE", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};

// F4 AFTER: the side the finished take plays on.
enum class After : std::uint8_t { Speaks, Sounds, None };
inline constexpr unsigned kAfterCount = 3;
inline constexpr const char* kAfterNames[kAfterCount] = {"SPEAKS", "SOUNDS", "NONE"};

inline constexpr unsigned kSampleRate = static_cast<unsigned>(o3sdk::core::kRefRateHz);  // the loops' rate: the engine's
inline constexpr unsigned kBeatsPerBar = 4;
inline constexpr unsigned kTicksPerBeat = 960;  // transport.h kPpqn

// What AFTER does with the slot: `place` the route `to` on `side`; `refused` when the other side holds the slot (a slot is one reader, route.h),
// then nothing is placed and the screen says why. NONE places nothing and is not a refusal.
struct AfterPlan {
  route::Side side = route::Side::Speaks;
  route::Route to;
  bool place = false;
  bool refused = false;
};

constexpr AfterPlan plan_after(route::Route cur, After after, unsigned slot) noexcept {
  AfterPlan p;
  p.to = cur;
  if (after == After::None || slot >= route::kSlots) {
    return p;
  }
  p.side = after == After::Speaks ? route::Side::Speaks : route::Side::Sounds;
  const route::Row row{route::Kind::Slot, static_cast<std::uint8_t>(slot)};
  if (route::pick(p.side, p.to, row)) {
    p.place = true;
  } else {
    p.to = cur;
    p.refused = true;
  }
  return p;
}

// UX3-6: what AFTER cannot do for a slot, known when REC arms (not after the take). The slot already sits on a side that is not AFTER's: the take
// plays there and AFTER is ignored (a slot is one reader, route.h). `Refused`: the route would not take it. AFTER NONE and an AFTER that agrees say nothing.
enum class AfterNote : std::uint8_t { None, SlotOnSpeaks, SlotOnSounds, Refused };

constexpr AfterNote after_note(route::Route cur, After after, unsigned slot) noexcept {
  if (after == After::None || slot >= route::kSlots) {
    return AfterNote::None;
  }
  const auto v = static_cast<std::uint8_t>(route::kFirstSlot + slot);
  if (cur.speaks == v) {
    return after == After::Speaks ? AfterNote::None : AfterNote::SlotOnSpeaks;
  }
  if (cur.sounds == v) {
    return after == After::Sounds ? AfterNote::None : AfterNote::SlotOnSounds;
  }
  return plan_after(cur, after, slot).refused ? AfterNote::Refused : AfterNote::None;
}

// The line for a note (nullptr for None): said when REC arms, so the player knows before the take.
constexpr const char* after_note_text(AfterNote n) noexcept {
  switch (n) {
    case AfterNote::SlotOnSpeaks: return "SLOT IS ON SPEAKS: AFTER IGNORED";
    case AfterNote::SlotOnSounds: return "SLOT IS ON SOUNDS: AFTER IGNORED";
    case AfterNote::Refused: return "AFTER CANNOT PLACE THE SLOT";
    case AfterNote::None: break;
  }
  return nullptr;
}

// The line when a take ends on a slot that already sits on a side: it names the side it plays on, and says so when AFTER asked for another.
constexpr const char* playing_on_text(bool speaks, AfterNote n) noexcept {
  const bool ignored = n == AfterNote::SlotOnSpeaks || n == AfterNote::SlotOnSounds;
  return speaks ? (ignored ? "PLAYING ON SPEAKS: AFTER IGNORED" : "PLAYING ON SPEAKS") : (ignored ? "PLAYING ON SOUNDS: AFTER IGNORED" : "PLAYING ON SOUNDS");
}

// The length at which Rec 0 goes out is perform::capture_stop_at(); this is whether a take of `bars` stops by itself (0 is FREE).
constexpr bool auto_stops(unsigned length_index) noexcept { return length_index != 0u && length_index < kLengthCount; }

// Bars completed by `length` samples at `bar` samples a bar (0 for a bar of 0), by a float compare (no 64-bit conversion).
constexpr unsigned bars_done(std::uint32_t length, float bar) noexcept {
  unsigned n = 0;
  if (!(bar >= 1.0f)) {
    return 0;
  }
  while (n < 99u && static_cast<float>(length) >= static_cast<float>(n + 1u) * bar) {
    ++n;
  }
  return n;
}

// Tenths of a second, rounded.
constexpr unsigned tenths_seconds(std::uint32_t samples) noexcept { return (samples + kSampleRate / 20u) / (kSampleRate / 10u); }

// Tenths of a bar, rounded (a bar of 0 reads 0).
constexpr unsigned tenths_bars(std::uint32_t length, float bar) noexcept {
  if (!(bar >= 1.0f)) {
    return 0;
  }
  const float t = static_cast<float>(length) * 10.0f / bar + 0.5f;
  return t >= 9999.0f ? 9999u : static_cast<unsigned>(t);
}

// The transport's 1-based bar and beat from the Q32.32 tick position (whole ticks in the high word).
struct BarBeat {
  unsigned bar = 1;
  unsigned beat = 1;
};
constexpr BarBeat bar_beat(std::uint64_t pos) noexcept {
  const std::uint32_t ticks = static_cast<std::uint32_t>(pos >> 32);
  const std::uint32_t beats = ticks / kTicksPerBeat;
  BarBeat b;
  b.bar = beats / kBeatsPerBar + 1u;
  b.beat = beats % kBeatsPerBar + 1u;
  return b;
}

// The pad pulse while a take records: lit on the first quarter of every beat at `mbpm` (0 or a stopped tempo reads 120).
constexpr bool pulse_on(std::uint64_t now_ms, unsigned mbpm) noexcept {
  const std::uint32_t t = mbpm >= 20000u ? mbpm : 120000u;
  const std::uint32_t period = 60000000u / t;  // ms a beat
  return period != 0u && (now_ms % period) < period / 4u;
}

// The slot's length steps by `delta`, held inside 0..kLengthCount-1 (no wrap: the ends are FREE and 8 BARS).
constexpr unsigned step_length(unsigned index, int delta) noexcept {
  const int v = static_cast<int>(index) + delta;
  return v < 0 ? 0u : (v >= static_cast<int>(kLengthCount) ? kLengthCount - 1u : static_cast<unsigned>(v));
}

}  // namespace o3_voc03er::sample_rec
