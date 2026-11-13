// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE REC's pure logic (docs/UI-DESIGN.md, "SAMPLE REC"): the LENGTH steps, what a take does when it ends (AUTOPLAY), bars and seconds as the screen shows
// them. Apart from the screen so tests/sample_rec_test.cpp holds them. Nothing here allocates or reads the engine.

#include <o3_voc03er/route.h>

#include <o3sdk/core/audio_clock.h>

#include <cstdint>

namespace o3_voc03er::sample_rec {

// F3 LENGTH: FREE (REC again ends it) or a count of bars the take stops at by itself.
inline constexpr unsigned kLengthCount = 5;
inline constexpr unsigned kLengthBars[kLengthCount] = {0, 1, 2, 4, 8};
inline constexpr const char* kLengthNames[kLengthCount] = {"FREE", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};

inline constexpr unsigned kSampleRate = static_cast<unsigned>(o3sdk::core::kRefRateHz);  // the loops' rate: the engine's
inline constexpr unsigned kBeatsPerBar = 4;
inline constexpr unsigned kTicksPerBeat = 960;  // transport.h kPpqn

// What a take does when it ends, from the route, F4 AUTOPLAY and the slot (there is no side to pick: SPEAKS and SOUNDS are PERFORM's F2 and F3).
//   Waits        AUTOPLAY OFF: nothing is routed, the take waits in the slot, the live input keeps speaking.
//   PlaysSpeaks  the slot already speaks: it plays there again.
//   PlaysSounds  the slot already sounds: it plays there again.
//   SpeaksNow    the slot is on neither side: it becomes the SPEAKS source and loops.
//   Refused      the route would not take it as the SPEAKS source (the screen then says so; the take stays in the slot).
// A take never goes to SOUNDS by itself: the player picks the slot in PERFORM's F3 SOUNDS list.
enum class TakeEnd : std::uint8_t { Waits, PlaysSpeaks, PlaysSounds, SpeaksNow, Refused };

constexpr TakeEnd take_end(route::Route cur, bool autoplay, unsigned slot) noexcept {
  if (!autoplay || slot >= route::kSlots) {
    return TakeEnd::Waits;
  }
  const auto v = static_cast<std::uint8_t>(route::kFirstSlot + slot);
  if (cur.speaks == v) {
    return TakeEnd::PlaysSpeaks;
  }
  if (cur.sounds == v) {
    return TakeEnd::PlaysSounds;
  }
  route::Route to = cur;
  return route::pick(route::Side::Speaks, to, route::Row{route::Kind::Slot, static_cast<std::uint8_t>(slot)}) ? TakeEnd::SpeaksNow : TakeEnd::Refused;
}

// The route a take that ends as `e` leaves behind: only SpeaksNow changes it (the slot becomes the SPEAKS source); `cur` otherwise.
constexpr route::Route take_end_route(route::Route cur, TakeEnd e, unsigned slot) noexcept {
  route::Route to = cur;
  if (e == TakeEnd::SpeaksNow && slot < route::kSlots) {
    to.speaks = static_cast<std::uint8_t>(route::kFirstSlot + slot);
  }
  return to;
}

// The line under the bar counter before and during a take: what will happen when it ends.
constexpr const char* take_end_plan_text(TakeEnd e) noexcept {
  switch (e) {
    case TakeEnd::PlaysSpeaks:
    case TakeEnd::SpeaksNow: return "THE TAKE WILL SPEAK";
    case TakeEnd::PlaysSounds: return "THE TAKE WILL PLAY ON SOUNDS";
    case TakeEnd::Refused:
    case TakeEnd::Waits: break;
  }
  return "THE TAKE WILL WAIT IN THE SLOT";
}

// The moment's line when the take has ended.
constexpr const char* take_end_done_text(TakeEnd e) noexcept {
  switch (e) {
    case TakeEnd::PlaysSpeaks:
    case TakeEnd::SpeaksNow: return "PLAYING ON SPEAKS";
    case TakeEnd::PlaysSounds: return "PLAYING ON SOUNDS";
    case TakeEnd::Refused: return "TAKEN: THE ROUTE REFUSES IT";
    case TakeEnd::Waits: break;
  }
  return "TAKEN, NOT PLAYING";
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
