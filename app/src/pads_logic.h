// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pads' layout (docs/REVIEW-2026-11-11.md section 3), pure and shared by PERFORM, PATTERN and SAMPLE: three rows A, B and C, each with a
// PLAYS value; what a pad plays follows from the layout, so a pad of row B gives the same note and kind on every screen. No engine, no screen,
// no state, no heap; tests/pads_logic_test.cpp holds it. The screens map their own rows to these (PERFORM rows 1, 2, 3; PATTERN rows 0, 1;
// SAMPLE rows 2, 3) in later slices.

#include <o3_voc03er/chords.h>
#include <o3_voc03er/pad_map.h>

#include <cstddef>
#include <cstdint>

namespace o3_voc03er::pads {

// What a row plays. The numbers are saved in the project: append, never renumber. 0..4 are the step's stored kind (0 a note, 1..4 the ChordKind + 1);
// 6..10 were the piano values (removed, NP1): never reused, and a file that holds one loads it as the plain value of the same kind (plays_from_saved);
// 11 FORMANT, 12 BEND and 13 MOUTH (V2: the tract's vowels and closures, TRACT mode) are row C's; 14 STRIP (TP1) is any row's: the row's `strip` id
// names the parameter its twelve pads set.
enum class Plays : std::uint8_t {
  Note = 0,
  Triad = 1,
  Seventh = 2,
  Open = 3,
  Power = 4,
  Off = 5,
  Formant = 11,
  Bend = 12,
  Mouth = 13,
  Strip = 14,
};
inline constexpr unsigned kPlaysCount = 15;
inline constexpr const char* kPlaysNames[kPlaysCount] = {"NOTE", "TRIAD", "7TH", "OPEN", "POWER", "OFF", "", "", "", "", "", "FORMANT", "BEND", "MOUTH", "PARAM"};
// The same for a soft key's value (the Data face holds about eight characters in a 100 px key).
inline constexpr const char* kPlaysShort[kPlaysCount] = {"NOTE", "TRIAD", "7TH", "OPEN", "POWER", "OFF", "", "", "", "", "", "FORMANT", "BEND", "MOUTH", "PARAM"};

// A saved byte as a value. 6..10 are the retired piano values and read as the plain value of the same kind (PIANO 7TH is 7TH); a byte that is no
// value at all (above 14) is not valid (valid_saved).
constexpr bool valid_saved(std::uint8_t v) noexcept { return v <= 14u; }
constexpr Plays plays_from_saved(std::uint8_t v) noexcept { return static_cast<Plays>(v >= 6u && v <= 10u ? v - 6u : v); }

// Row A is the top of the three, C the bottom (the lowest, the one that plays at the pad's own octave).
enum class Row : std::uint8_t { A, B, C };
inline constexpr unsigned kRows = 3;
inline constexpr const char* kRowNames[kRows] = {"A", "B", "C"};

constexpr unsigned idx(Row r) noexcept { return static_cast<unsigned>(r); }
constexpr unsigned idx(Plays p) noexcept { return static_cast<unsigned>(p); }

// A strip's stable id (TP1): (tag << 8) | the parameter id of its owner, tag 1 the vocoder unit, 2 the tract unit, 3 the app's own values; 0 is none.
// It is what the project saves (never a list position), so the parameter tables may grow. strip_logic.h holds the table.
inline constexpr std::uint16_t kNoStrip = 0;
constexpr std::uint16_t strip_id(unsigned tag, unsigned param) noexcept { return static_cast<std::uint16_t>((tag << 8) | (param & 0xFFu)); }

// `strip[r]` is meaningful only while `row[r]` is STRIP. No other state: one layout is three values and three ids.
struct Layout {
  Plays row[kRows] = {Plays::Note, Plays::Triad, Plays::Formant};
  std::uint16_t strip[kRows] = {kNoStrip, kNoStrip, kNoStrip};
};

// A value that sounds from its pads (a note or a chord): not OFF, FORMANT, BEND, MOUTH or a parameter strip.
constexpr bool plays_notes(Plays p) noexcept { return p <= Plays::Power; }

// Every row offers NOTE..OFF and STRIP; only C FORMANT, BEND and MOUTH.
constexpr bool offered(Row r, Plays p) noexcept {
  if (p <= Plays::Off || p == Plays::Strip) {
    return true;
  }
  return (p == Plays::Formant || p == Plays::Bend || p == Plays::Mouth) && r == Row::C;
}

// The notes PLAYS steps through, per row (the parameter strips follow them in strip_logic.h's entry list).
inline constexpr Plays kOrderAB[6] = {Plays::Note, Plays::Triad, Plays::Seventh, Plays::Open, Plays::Power, Plays::Off};
inline constexpr Plays kOrderC[9] = {Plays::Note, Plays::Triad, Plays::Seventh, Plays::Open, Plays::Power, Plays::Formant, Plays::Bend, Plays::Mouth, Plays::Off};

constexpr const Plays* order_of(Row r) noexcept { return r == Row::C ? kOrderC : kOrderAB; }
constexpr unsigned order_count(Row r) noexcept { return r == Row::C ? 9u : 6u; }
// The place of `p` in the row's order; a value the row does not offer there (a STRIP too) is taken as the first (NOTE).
constexpr unsigned order_index(Row r, Plays p) noexcept {
  const Plays* o = order_of(r);
  for (unsigned i = 0; i < order_count(r); ++i) {
    if (o[i] == p) {
      return i;
    }
  }
  return 0;
}

// The encoder: `delta` places along the order, stopping at the ends.
constexpr Plays plays_step(Row r, Plays cur, int delta) noexcept {
  const int n = static_cast<int>(order_count(r));
  int i = static_cast<int>(order_index(r, cur)) + delta;
  i = i < 0 ? 0 : i > n - 1 ? n - 1 : i;
  return order_of(r)[i];
}
// A tap: the next one, wrapping to the first after the last.
constexpr Plays plays_tap(Row r, Plays cur) noexcept { return order_of(r)[(order_index(r, cur) + 1u) % order_count(r)]; }

// Sets a row's value (a note kind, OFF, FORMANT, BEND or MOUTH) and drops its strip. False, and nothing changed, when the row does not offer it.
// A STRIP goes through set_strip.
constexpr bool set_plays(Layout& l, Row r, Plays p) noexcept {
  if (!offered(r, p) || p == Plays::Strip) {
    return false;
  }
  l.row[idx(r)] = p;
  l.strip[idx(r)] = kNoStrip;
  return true;
}
// Makes the row a parameter strip of `id` (not none).
constexpr bool set_strip(Layout& l, Row r, std::uint16_t id) noexcept {
  if (id == kNoStrip) {
    return false;
  }
  l.row[idx(r)] = Plays::Strip;
  l.strip[idx(r)] = id;
  return true;
}

// The octaves above the base a row plays: one above each playing row below it (C is the lowest, then B, then A). Defined on the layout, so a pad gives
// the same note on every screen.
constexpr int row_octave(const Layout& l, Row r) noexcept {
  const int c = plays_notes(l.row[idx(Row::C)]) ? 1 : 0;
  if (r == Row::C) {
    return 0;
  }
  if (r == Row::B) {
    return c;
  }
  return c + (plays_notes(l.row[idx(Row::B)]) ? 1 : 0);
}

// What a pad plays: `note` 1..127 (0 is a rest in a step, so never returned; -1 plays nothing: OFF, FORMANT, BEND, MOUTH, a strip, a note out of
// range) and the step's stored `kind` (0 a note, 1..4 the ChordKind + 1).
struct PadPlay {
  int note = -1;
  std::uint8_t kind = 0;
};

// The pad of row `r`, column `col` (0..11), the screen's octave shift `octave`.
constexpr PadPlay pad_play(const Layout& l, Row r, unsigned col, int octave) noexcept {
  PadPlay out;
  const Plays p = l.row[idx(r)];
  if (col >= 12u || !plays_notes(p)) {
    return out;
  }
  const int offset = static_cast<int>(col);
  const unsigned kind = idx(p);
  const int note = kChordBaseNote + 12 * (octave + row_octave(l, r)) + offset;
  if (note < 1 || note > 127) {
    return out;
  }
  out.note = note;
  out.kind = static_cast<std::uint8_t>(kind);
  return out;
}

constexpr Plays plays_of_kind(ChordKind k) noexcept { return static_cast<Plays>(static_cast<unsigned>(k) + 1u); }

// The layout an older file's settings meant (Q8): PADS = KEYS is two note rows A and B (the piano is gone, NP1) with the formant strip (BEND with BEND ROW) on C; PADS = CHORDS
// gave PERFORM's rows 1, 2 and 3 the kinds kind_for_row chose: C `bottom` and the next kinds above, or, with BEND ROW (the strip on row 3), B
// `bottom`, A the next and C BEND.
constexpr Layout layout_from_legacy(bool keys, ChordKind bottom, bool bend_row) noexcept {
  Layout l;
  if (keys) {
    l.row[idx(Row::A)] = Plays::Note;
    l.row[idx(Row::B)] = Plays::Note;
    l.row[idx(Row::C)] = bend_row ? Plays::Bend : Plays::Formant;
    return l;
  }
  const unsigned lowest = bend_row ? 2u : 3u;
  l.row[idx(Row::A)] = plays_of_kind(kind_for_row(1u, bottom, lowest));
  l.row[idx(Row::B)] = plays_of_kind(kind_for_row(2u, bottom, lowest));
  l.row[idx(Row::C)] = bend_row ? Plays::Bend : plays_of_kind(kind_for_row(3u, bottom, lowest));
  return l;
}

// The chosen row's LEDs go dark this long when F6 changes the row, a short off-on (PATTERN and PERFORM).
inline constexpr std::uint64_t kRowFlashMs = 150;

// The settings of the 1.0 STAT that a 1.1 file still carries, derived from the layout on save so an older reader finds something sensible: `keys` always 0 (no piano
// now), `chord` the ChordKind of the lowest chord row (C, then B, then A; none gives 0), `bend_row` 1 when C is BEND.
// A 1.1 file loads through the layout alone; these three are ignored there.
struct LegacyFields {
  std::uint8_t keys = 0;
  std::uint8_t chord = 0;
  std::uint8_t bend_row = 0;
};
constexpr LegacyFields legacy_fields(const Layout& l) noexcept {
  LegacyFields f;
  f.bend_row = l.row[idx(Row::C)] == Plays::Bend ? 1u : 0u;
  for (unsigned r = kRows; r-- > 0;) {  // C first, then B, then A
    const Plays p = l.row[r];
    if (p >= Plays::Triad && p <= Plays::Power) {
      f.chord = static_cast<std::uint8_t>(idx(p) - idx(Plays::Triad));
      break;
    }
  }
  return f;
}

}  // namespace o3_voc03er::pads
