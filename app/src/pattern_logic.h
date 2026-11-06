// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pure part of the PATTERN screen (docs/UI-DESIGN.md "PATTERN"): which pad is what, the LEVEL palette, the palette's value for a pad, the text of a
// step cell. No engine, no screen: the host test (tests/pattern_screen_test.cpp) calls these directly.

#include <o3_voc03er/stepseq.h>

#include <o3sdk/app/panel.h>

#include <cstdint>

namespace o3_voc03er::pattern {

enum class Lane : std::uint8_t { Note, Carrier, Level, Slice };
inline constexpr unsigned kLanes = 4;
inline constexpr const char* kLaneNames[kLanes] = {"NOTE", "CARRIER", "LEVEL", "SLICE"};

inline constexpr unsigned kPalettePads = 24;  // pads 0..23: rows 0 and 1 (row 0 is the top)
inline constexpr unsigned kFirstStepPad = 24;  // rows 2 and 3
inline constexpr unsigned kCarrierPads = kCarrierMax + 1u;  // FOLLOW, SYNTH, NOISE, SLOT 1..4

// A pad of rows 2 and 3: columns 0..7 are steps 1..16 (row 2 the first eight), columns 8..11 are patterns A..H (row 2 A..D). -1: not that kind.
constexpr int step_of_pad(unsigned pad) noexcept {
  if (pad < kFirstStepPad || pad >= o3sdk::app::kPadCount) {
    return -1;
  }
  const unsigned row = o3sdk::app::pad_row(pad) - 2u;
  const unsigned col = o3sdk::app::pad_col(pad);
  return col < 8u ? static_cast<int>(row * 8u + col) : -1;
}
constexpr int pattern_of_pad(unsigned pad) noexcept {
  if (pad < kFirstStepPad || pad >= o3sdk::app::kPadCount) {
    return -1;
  }
  const unsigned row = o3sdk::app::pad_row(pad) - 2u;
  const unsigned col = o3sdk::app::pad_col(pad);
  return col >= 8u ? static_cast<int>(row * 4u + (col - 8u)) : -1;
}
constexpr unsigned pad_of_step(unsigned step) noexcept { return o3sdk::app::pad_at(2u + step / 8u, step % 8u); }
constexpr unsigned pad_of_pattern(unsigned p) noexcept { return o3sdk::app::pad_at(2u + p / 4u, 8u + p % 4u); }

// The LEVEL palette: 24 steps, pad 0 the lowest, pad 23 the full 100 (the synth takes 100 and up as full, so nothing above is offered). The step
// is 4.17, so no two pads give the same velocity.
constexpr unsigned level_of_pad(unsigned pad) noexcept { return ((pad % kPalettePads) + 1u) * 100u / kPalettePads; }

// The NOTE palette's note for pad 0..23: CHORDS: the root of PERFORM's octave, column = pitch class; KEYS: two chromatic octaves, row 0 the upper.
// Returns 1..127 (clamped).
constexpr unsigned note_of_pad(unsigned pad, int octave, bool keys) noexcept {
  const unsigned row = o3sdk::app::pad_row(pad);
  const unsigned col = o3sdk::app::pad_col(pad);
  const int up = keys ? (row == 0u ? 12 : 0) : 0;
  const int n = 48 + 12 * octave + up + static_cast<int>(col);
  return n < 1 ? 1u : n > 127 ? 127u : static_cast<unsigned>(n);
}
// The chord kind of the NOTE palette's row (the chord grid's two lower rows: SEVENTH then the bottom row's kind).
constexpr ChordKind kind_of_pad(unsigned pad, ChordKind bottom) noexcept { return o3sdk::app::pad_row(pad) == 0u ? ChordKind::Seventh : bottom; }

// Does the palette pad hold what the step stores? (LED mid on it.)
constexpr bool palette_matches(Lane lane, unsigned pad, const Step& s, int octave, bool keys, ChordKind bottom) noexcept {
  switch (lane) {
    case Lane::Note:
      if (s.note == 0u) {
        return false;
      }
      if (keys) {
        return s.kind == 0u && s.note == note_of_pad(pad, octave, true);
      }
      return s.kind == static_cast<unsigned>(kind_of_pad(pad, bottom)) + 1u && s.note == note_of_pad(pad, octave, false);
    case Lane::Carrier:
      return pad < kCarrierPads && s.carrier == pad;
    case Lane::Level:
      return level_of_pad(pad) == step_velocity(s);
    case Lane::Slice:
      return pad < kSliceMax && pad + 1u == s.slice;
  }
  return false;
}

// A palette pad of the SLICE lane: pads 0..23 are slices 1..24 of the SPEAKS slot, lit (dim) up to its slice count `count`.
enum class SliceWrite : std::uint8_t { Written, NoSlices, Dark };
inline SliceWrite slice_write(PatternData& p, unsigned i, unsigned pad, unsigned count) noexcept {
  if (count == 0u) {
    return SliceWrite::NoSlices;  // SPEAKS has no slices: nothing is written
  }
  if (pad >= count || pad >= kSliceMax) {
    return SliceWrite::Dark;
  }
  return set_slice(p, i, pad + 1u) ? SliceWrite::Written : SliceWrite::Dark;
}

// The next step of the cursor after a palette write: one on, wrapping at LENGTH.
constexpr unsigned next_cursor(unsigned cursor, unsigned length) noexcept { return length == 0u ? 0u : (cursor + 1u) % length; }

inline constexpr const char* kCellCarrier[kCarrierPads] = {"FLW", "SYN", "NSE", "S1", "S2", "S3", "S4"};
inline constexpr const char* kPadCarrier[kCarrierPads] = {"FOLLW", "SYNTH", "NOISE", "SLOT1", "SLOT2", "SLOT3", "SLOT4"};
inline constexpr const char* kCellKind[4] = {"TRI", "7TH", "OPN", "PWR"};
inline constexpr const char* kPitchClass[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// "C#3": the MIDI convention of the kit's help_note_label (60 is C4, 48 is C3). Up to 4 characters and the terminator.
inline void note_name(char (&out)[6], unsigned note) noexcept {
  const char* pc = kPitchClass[note % 12u];
  unsigned n = 0;
  while (*pc != '\0') {
    out[n++] = *pc++;
  }
  const int oct = static_cast<int>(note / 12u) - 1;
  if (oct < 0) {
    out[n++] = '-';
  }
  out[n++] = static_cast<char>('0' + (oct < 0 ? -oct : oct) % 10);
  out[n] = '\0';
}

// The value the VALUE key steps, by lane: NOTE a semitone (a rest starts at 48, a chord keeps its kind), CARRIER 0..6, LEVEL 1..127. A step with
// no change (the end of a range, the SLICE lane with no slices) returns false; SLICE steps 0..`slice_count` (0 is none).
inline bool step_value(PatternData& p, unsigned i, Lane lane, int delta, unsigned slice_count = 0) noexcept {
  const Step s = get_step(p, i);
  switch (lane) {
    case Lane::Note: {
      if (s.note == 0u) {
        return delta > 0 && set_note(p, i, 48u);
      }
      const int n = static_cast<int>(s.note) + delta;
      if (n < 1 || n > 127) {
        return false;
      }
      Step t = s;
      t.note = static_cast<std::uint8_t>(n);
      return set_step(p, i, t);
    }
    case Lane::Carrier: {
      const int c = static_cast<int>(s.carrier) + delta;
      return c >= 0 && c <= static_cast<int>(kCarrierMax) && set_carrier(p, i, static_cast<unsigned>(c));
    }
    case Lane::Level: {
      const int v = static_cast<int>(step_velocity(s)) + delta;
      return v >= 1 && v <= 127 && set_level(p, i, static_cast<unsigned>(v));
    }
    case Lane::Slice: {
      if (slice_count == 0u) {
        return false;
      }
      int v = static_cast<int>(s.slice) + delta;
      v = v < 0 ? 0 : (v > static_cast<int>(slice_count) ? static_cast<int>(slice_count) : v);
      return v != static_cast<int>(s.slice) && set_slice(p, i, static_cast<unsigned>(v));
    }
  }
  return false;
}

// A rest in one lane of a step (SHIFT + step pad, CLEAR LANE): NOTE a rest, CARRIER FOLLOW, LEVEL the default, SLICE none.
inline bool rest_lane(PatternData& p, unsigned i, Lane lane) noexcept {
  switch (lane) {
    case Lane::Note:
      return set_rest(p, i);
    case Lane::Carrier:
      return set_carrier(p, i, 0u);
    case Lane::Level:
      return set_level(p, i, 0u);
    case Lane::Slice:
      return set_slice(p, i, 0u);
  }
  return false;
}

}  // namespace o3_voc03er::pattern
