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
// 6..10 are the same on the piano, kind = value - 6. MOUTH (13, with the tract) is appended later.
enum class Plays : std::uint8_t {
  Note = 0,
  Triad = 1,
  Seventh = 2,
  Open = 3,
  Power = 4,
  Off = 5,
  PianoNote = 6,
  PianoTriad = 7,
  PianoSeventh = 8,
  PianoOpen = 9,
  PianoPower = 10,
  Formant = 11,
  Bend = 12,
};
inline constexpr unsigned kPlaysCount = 13;
inline constexpr const char* kPlaysNames[kPlaysCount] = {"NOTE",        "TRIAD",       "7TH",         "OPEN",       "POWER",   "OFF", "PIANO NOTE",
                                                         "PIANO TRIAD", "PIANO 7TH",   "PIANO OPEN",  "PIANO POWER", "FORMANT", "BEND"};

// Row A is the top of the three, C the bottom (the lowest, the one that plays at the pad's own octave).
enum class Row : std::uint8_t { A, B, C };
inline constexpr unsigned kRows = 3;
inline constexpr const char* kRowNames[kRows] = {"A", "B", "C"};

constexpr unsigned idx(Row r) noexcept { return static_cast<unsigned>(r); }
constexpr unsigned idx(Plays p) noexcept { return static_cast<unsigned>(p); }

// `last` holds the grid values of A and B while they are joined into a piano, for the split.
struct Layout {
  Plays row[kRows] = {Plays::Note, Plays::Triad, Plays::Formant};
  Plays last[2] = {Plays::Note, Plays::Triad};
};

constexpr bool is_piano_value(Plays p) noexcept { return p >= Plays::PianoNote && p <= Plays::PianoPower; }
// A value that sounds from its pads (a note, a chord or a piano key): not OFF, FORMANT or BEND.
constexpr bool plays_notes(Plays p) noexcept { return p <= Plays::Power || is_piano_value(p); }
constexpr bool is_piano(const Layout& l) noexcept { return is_piano_value(l.row[idx(Row::A)]) && is_piano_value(l.row[idx(Row::B)]); }

// Every row offers NOTE..OFF; A and B also the piano values; only C FORMANT and BEND.
constexpr bool offered(Row r, Plays p) noexcept {
  if (p <= Plays::Off) {
    return true;
  }
  if (is_piano_value(p)) {
    return r != Row::C;
  }
  return (p == Plays::Formant || p == Plays::Bend) && r == Row::C;
}

// The order PLAYS steps through, per row.
inline constexpr Plays kOrderAB[11] = {Plays::Note,       Plays::Triad,      Plays::Seventh,     Plays::Open,       Plays::Power,    Plays::PianoNote,
                                       Plays::PianoTriad, Plays::PianoSeventh, Plays::PianoOpen, Plays::PianoPower, Plays::Off};
inline constexpr Plays kOrderC[8] = {Plays::Note, Plays::Triad, Plays::Seventh, Plays::Open, Plays::Power, Plays::Formant, Plays::Bend, Plays::Off};

constexpr const Plays* order_of(Row r) noexcept { return r == Row::C ? kOrderC : kOrderAB; }
constexpr unsigned order_count(Row r) noexcept { return r == Row::C ? 8u : 11u; }
// The place of `p` in the row's order; a value the row does not offer is taken as the first (NOTE).
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

// Sets a row's value. A piano value on A or B joins them (both take it; the grid values they had are kept in `last`), a grid value on A or B
// while they are joined splits them (the other row gets its `last` value back). False, and nothing changed, when the row does not offer it.
constexpr bool set_plays(Layout& l, Row r, Plays p) noexcept {
  if (!offered(r, p)) {
    return false;
  }
  const unsigned a = idx(Row::A);
  const unsigned b = idx(Row::B);
  if (is_piano_value(p)) {  // only A and B offer it
    if (!is_piano(l)) {
      l.last[0] = l.row[a];
      l.last[1] = l.row[b];
    }
    l.row[a] = p;
    l.row[b] = p;
  } else if (r != Row::C && is_piano(l)) {
    const unsigned other = r == Row::A ? b : a;
    l.row[idx(r)] = p;
    l.row[other] = l.last[other];
  } else {
    l.row[idx(r)] = p;
  }
  return true;
}

// The octaves above the base a row plays: one above each playing row below it (C is the lowest, then B, then A), the joined piano counting as
// one row and both its halves reporting its octave. Defined on the layout, so a pad gives the same note on every screen.
constexpr int row_octave(const Layout& l, Row r) noexcept {
  const int c = plays_notes(l.row[idx(Row::C)]) ? 1 : 0;
  if (r == Row::C) {
    return 0;
  }
  if (is_piano(l)) {
    return c;
  }
  if (r == Row::B) {
    return c;
  }
  return c + (plays_notes(l.row[idx(Row::B)]) ? 1 : 0);
}

// The piano's keys, by column, in semitones above the C of the octave: B holds the white keys, A the black ones (-1: no key).
inline constexpr int kWhiteOffset[12] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19};     // C D E F G A B C D E F G
inline constexpr int kBlackOffset[12] = {-1, 1, 3, -1, 6, 8, 10, -1, 13, 15, -1, 18};  // C# D# . F# G# A# . C# D# . F#; -1 no key

// What a pad plays: `note` 1..127 (0 is a rest in a step, so never returned; -1 plays nothing: OFF, FORMANT, BEND, a piano gap, a note out of
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
  int offset = static_cast<int>(col);
  unsigned kind = idx(p);
  if (is_piano_value(p)) {
    if (r == Row::C) {
      return out;
    }
    offset = r == Row::B ? kWhiteOffset[col] : kBlackOffset[col];
    if (offset < 0) {
      return out;
    }
    kind -= idx(Plays::PianoNote);
  }
  const int note = kChordBaseNote + 12 * (octave + row_octave(l, r)) + offset;
  if (note < 1 || note > 127) {
    return out;
  }
  out.note = note;
  out.kind = static_cast<std::uint8_t>(kind);
  return out;
}

constexpr Plays plays_of_kind(ChordKind k) noexcept { return static_cast<Plays>(static_cast<unsigned>(k) + 1u); }

// The layout an older file's settings meant (Q8): PADS = KEYS is a piano on A and B with the formant strip (BEND with BEND ROW) on C; PADS = CHORDS
// gave PERFORM's rows 1, 2 and 3 the kinds kind_for_row chose: C `bottom` and the next kinds above, or, with BEND ROW (the strip on row 3), B
// `bottom`, A the next and C BEND.
constexpr Layout layout_from_legacy(bool keys, ChordKind bottom, bool bend_row) noexcept {
  Layout l;
  if (keys) {
    l.row[idx(Row::A)] = Plays::PianoNote;
    l.row[idx(Row::B)] = Plays::PianoNote;
    l.row[idx(Row::C)] = bend_row ? Plays::Bend : Plays::Formant;
    return l;
  }
  const unsigned lowest = bend_row ? 2u : 3u;
  l.row[idx(Row::A)] = plays_of_kind(kind_for_row(1u, bottom, lowest));
  l.row[idx(Row::B)] = plays_of_kind(kind_for_row(2u, bottom, lowest));
  l.row[idx(Row::C)] = bend_row ? Plays::Bend : plays_of_kind(kind_for_row(3u, bottom, lowest));
  return l;
}

}  // namespace o3_voc03er::pads
