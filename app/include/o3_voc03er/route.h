// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The routing of docs/UI-DESIGN.md sections 1 and 2, pure (no screen, no engine): what SPEAKS and what SOUNDS, which pairs are allowed, the
// roles that follow, the unit values that carry them, and the rows of a picker with the ones the one-LIVE rule dims. The two pickers, the
// header and the console all read this; tests/route_test.cpp covers it.
//
// Values are the unit's: SPEAKS is Vocoder::ModSrc (0 LIVE, 3..6 SLOT 1..4; NONE (1) is not a picker choice), SOUNDS is Vocoder::CarSrc
// (1 SYNTH, 2 NOISE, 3..6 SLOT 1..4, 7 LIVE, 8 VOICE: the synth plays the pitch that is sung; FOLLOW (0) is the pattern's "leave it").

#include <cstdint>

namespace o3_voc03er::route {

inline constexpr std::uint8_t kSpeaksLive = 0;
inline constexpr std::uint8_t kSoundsSynth = 1;
inline constexpr std::uint8_t kSoundsNoise = 2;
inline constexpr std::uint8_t kFirstSlot = 3;  // slot n (0..3) is kFirstSlot + n on both sides
inline constexpr std::uint8_t kSoundsLive = 7;
inline constexpr std::uint8_t kSoundsVoice = 8;
inline constexpr unsigned kSlots = 4;

enum class Side : std::uint8_t { Speaks, Sounds };

// The one live source of the ADC (section 2): the order of the AudioIn sources the design lists.
enum class LiveSrc : std::uint8_t { Line, Left, Right, MicLow, MicHigh, Radio, Usb };
inline constexpr unsigned kLiveSrcCount = 7;

struct Route {
  std::uint8_t speaks = kSpeaksLive;      // ModSrc value
  std::uint8_t sounds = kSoundsSynth;     // CarSrc value
};

constexpr bool is_slot(std::uint8_t v) noexcept { return v >= kFirstSlot && v < kFirstSlot + kSlots; }

constexpr bool speaks_valid(std::uint8_t v) noexcept { return v == kSpeaksLive || is_slot(v); }
constexpr bool sounds_valid(std::uint8_t v) noexcept { return v == kSoundsSynth || v == kSoundsNoise || is_slot(v) || v == kSoundsLive || v == kSoundsVoice; }

// A pair is allowed when both values are picker choices and no slot is on both sides (a slot is one reader: it cannot be the modulator and
// the carrier at once). Every other pair is allowed, LIVE on both sides included: it is the same source on the modulator and the carrier bus.
constexpr bool allowed(Route r) noexcept {
  return speaks_valid(r.speaks) && sounds_valid(r.sounds) && !(is_slot(r.speaks) && r.speaks == r.sounds);
}

enum class SlotRole : std::uint8_t { Free, Speaks, Sounds };  // Free: on no side, it speaks only when its slices are played

struct Roles {
  SlotRole slot[kSlots] = {SlotRole::Free, SlotRole::Free, SlotRole::Free, SlotRole::Free};
  bool live_speaks = false;
  bool live_sounds = false;
  bool adc = false;       // the ADC is in use: a live source is on a side
  bool synth = false;     // the synth plays
  bool noise_only = false;  // the synth plays noise alone (SOUNDS NOISE)
};

constexpr Roles roles(Route r) noexcept {
  Roles o;
  o.live_speaks = r.speaks == kSpeaksLive;
  o.live_sounds = r.sounds == kSoundsLive;
  o.adc = o.live_speaks || o.live_sounds;
  o.synth = r.sounds == kSoundsSynth || r.sounds == kSoundsNoise || r.sounds == kSoundsVoice;
  o.noise_only = r.sounds == kSoundsNoise;
  for (unsigned i = 0; i < kSlots; ++i) {
    const auto v = static_cast<std::uint8_t>(kFirstSlot + i);
    o.slot[i] = r.speaks == v ? SlotRole::Speaks : (r.sounds == v ? SlotRole::Sounds : SlotRole::Free);
  }
  return o;
}

// The ADC feeds a side's LIVE row; with USB the two sides take its two channels (SPEAKS USB L, SOUNDS USB R). 0 left, 1 right.
constexpr unsigned usb_channel(Side s) noexcept { return s == Side::Speaks ? 0u : 1u; }
// The name of a side's LIVE: the source's label, and with USB the channel that side takes.
constexpr const char* live_name(const char* label, bool usb, Side s) noexcept { return !usb ? label : (usb_channel(s) == 0u ? "USB L" : "USB R"); }

// A picker row: what it offers. Live is the LIVE row with the name of the source in force; Source is any other live source.
enum class Kind : std::uint8_t { Synth, Noise, Voice, Live, Source, Slot };
struct Row {
  Kind kind = Kind::Synth;
  std::uint8_t arg = 0;  // Source: a LiveSrc; Slot: 0..3
};

enum class Reason : std::uint8_t { None, CaptureFirst, SlotInUse };

struct RowState {
  bool dimmed = false;
  Reason reason = Reason::None;
  // A dimmed Source row is a pre-arm (section 2): picking it captures the live source first, then moves the ADC to this one for this side.
  bool pre_arm() const noexcept { return reason == Reason::CaptureFirst; }
};

constexpr const char* reason_text(Reason r) noexcept {
  return r == Reason::CaptureFirst ? "CAPTURE FIRST" : (r == Reason::SlotInUse ? "IN USE" : "");
}

// The state of `row` in the picker of `side` while `r` is in force and `cur` is the live source of the ADC. The one rule: the other side
// holds LIVE, so any source but the one in force cannot be taken without freeing it by a capture (CAPTURE FIRST). The LIVE row of the source
// in force is never dimmed (both sides may hold it). A slot the other side holds is dimmed (IN USE). SYNTH, NOISE and VOICE are never dimmed.
constexpr RowState row_state(Side side, Route r, Row row) noexcept {
  RowState s;
  const Roles o = roles(r);
  if (row.kind == Kind::Source) {
    const bool other_live = side == Side::Speaks ? o.live_sounds : o.live_speaks;
    if (other_live) {
      s.dimmed = true;
      s.reason = Reason::CaptureFirst;
    }
  } else if (row.kind == Kind::Slot) {
    const auto v = static_cast<std::uint8_t>(kFirstSlot + row.arg);
    const std::uint8_t other = side == Side::Speaks ? r.sounds : r.speaks;
    if (other == v) {
      s.dimmed = true;
      s.reason = Reason::SlotInUse;
    }
  }
  return s;
}

// The rows of a picker, in the order the screen lists them: SOUNDS gets SYNTH, NOISE, VOICE first; both get the LIVE row, the other live sources
// (all but `cur`, 6 rows), then the 4 slots. Writes at most `cap` rows and returns how many the picker has (up to kMaxRows).
inline constexpr unsigned kMaxRows = 3 + 1 + (kLiveSrcCount - 1) + kSlots;

constexpr unsigned rows(Side side, LiveSrc cur, Row* out, unsigned cap) noexcept {
  unsigned n = 0;
  const auto put = [&](Kind k, unsigned arg) {
    if (n < cap) {
      out[n] = Row{k, static_cast<std::uint8_t>(arg)};
    }
    ++n;
  };
  if (side == Side::Sounds) {
    put(Kind::Synth, 0);
    put(Kind::Noise, 0);
    put(Kind::Voice, 0);
  }
  put(Kind::Live, 0);
  for (unsigned s = 0; s < kLiveSrcCount; ++s) {
    if (s != static_cast<unsigned>(cur)) {
      put(Kind::Source, s);
    }
  }
  for (unsigned i = 0; i < kSlots; ++i) {
    put(Kind::Slot, i);
  }
  return n;
}

// The route that picking `row` makes on `side` (the live source changes elsewhere). Returns false, leaving `r` as it was, when the pick is
// dimmed or the result is not allowed. A Source row stands for LIVE on that side (the caller moves the ADC).
constexpr bool pick(Side side, Route& r, Row row) noexcept {
  if (row_state(side, r, row).dimmed) {
    return false;
  }
  std::uint8_t v = 0;
  switch (row.kind) {
    case Kind::Synth: v = kSoundsSynth; break;
    case Kind::Noise: v = kSoundsNoise; break;
    case Kind::Voice: v = kSoundsVoice; break;
    case Kind::Live:
    case Kind::Source: v = side == Side::Speaks ? kSpeaksLive : kSoundsLive; break;
    case Kind::Slot: v = static_cast<std::uint8_t>(kFirstSlot + row.arg); break;
  }
  Route n = r;
  (side == Side::Speaks ? n.speaks : n.sounds) = v;
  if (!allowed(n)) {
    return false;
  }
  r = n;
  return true;
}

static_assert(allowed(Route{kSpeaksLive, kSoundsSynth}) && allowed(Route{kSpeaksLive, kSoundsLive}), "REALTIME and live-to-live are allowed");
static_assert(!allowed(Route{kFirstSlot, kFirstSlot}), "a slot is not on both sides");

}  // namespace o3_voc03er::route
