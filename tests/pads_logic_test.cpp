// SPDX-License-Identifier: GPL-3.0-or-later
// The pads' layout (app/src/pads_logic.h): the PLAYS values and the rows that offer them (the piano is gone: its saved bytes read as the plain value), the strip value, each row's octave,
// what a pad plays.
#include <doctest/doctest.h>

#include "../app/src/pads_logic.h"

#include <cstring>
#include <initializer_list>

using namespace o3_voc03er;
namespace pd = o3_voc03er::pads;
using pd::Plays;
using pd::Row;

namespace {

pd::Layout make(Plays a, Plays b, Plays c) {
  pd::Layout l;
  l.row[0] = a;
  l.row[1] = b;
  l.row[2] = c;
  return l;
}

}  // namespace

TEST_CASE("pads: the values keep their saved numbers and names") {
  CHECK(static_cast<int>(Plays::Note) == 0);
  CHECK(static_cast<int>(Plays::Power) == 4);
  CHECK(static_cast<int>(Plays::Off) == 5);
  CHECK(static_cast<int>(Plays::Formant) == 11);
  CHECK(static_cast<int>(Plays::Bend) == 12);
  CHECK(static_cast<int>(Plays::Mouth) == 13);
  CHECK(static_cast<int>(Plays::Strip) == 14);
  CHECK(pd::kPlaysCount == 15u);
  CHECK(std::strcmp(pd::kPlaysNames[14], "PARAM") == 0);
  CHECK(std::strcmp(pd::kPlaysNames[13], "MOUTH") == 0);
  CHECK(std::strcmp(pd::kPlaysShort[13], "MOUTH") == 0);
  CHECK(std::strcmp(pd::kPlaysNames[0], "NOTE") == 0);
  CHECK(std::strcmp(pd::kPlaysNames[2], "7TH") == 0);
  for (unsigned v = 6; v <= 10; ++v) {  // the retired piano values have no name
    CHECK(std::strcmp(pd::kPlaysNames[v], "") == 0);
  }
  CHECK(std::strcmp(pd::kPlaysNames[12], "BEND") == 0);
  CHECK(std::strcmp(pd::kRowNames[2], "C") == 0);
  // 0..4 are the step's stored kind: a note, then the ChordKind + 1.
  CHECK(static_cast<int>(Plays::Triad) == static_cast<int>(ChordKind::Triad) + 1);
  CHECK(static_cast<int>(Plays::Power) == static_cast<int>(ChordKind::Power) + 1);
  const pd::Layout def;
  CHECK(def.row[0] == Plays::Note);
  CHECK(def.row[1] == Plays::Triad);
  CHECK(def.row[2] == Plays::Formant);
  CHECK(def.strip[0] == pd::kNoStrip);
  CHECK(def.strip[2] == pd::kNoStrip);
}

TEST_CASE("pads: which row offers which value") {
  for (unsigned v = 0; v < pd::kPlaysCount; ++v) {
    const Plays p = static_cast<Plays>(v);
    CAPTURE(v);
    const bool shared = v <= 5 || v == 14;  // NOTE..OFF and STRIP are every row's
    CHECK(pd::offered(Row::A, p) == shared);
    CHECK(pd::offered(Row::B, p) == shared);
    CHECK(pd::offered(Row::C, p) == (shared || (v >= 11 && v <= 13)));  // FORMANT, BEND and MOUTH are row C's only
  }
}

TEST_CASE("pads: a tap steps and wraps, the encoder steps and stops at the ends") {
  // A and B: NOTE TRIAD 7TH OPEN POWER OFF.
  const Plays ab[6] = {Plays::Note, Plays::Triad, Plays::Seventh, Plays::Open, Plays::Power, Plays::Off};
  for (Row r : {Row::A, Row::B}) {
    for (unsigned i = 0; i < 6; ++i) {
      CAPTURE(i);
      CHECK(pd::plays_tap(r, ab[i]) == ab[(i + 1) % 6]);
      CHECK(pd::plays_step(r, ab[i], 1) == ab[i < 5 ? i + 1 : 5]);
      CHECK(pd::plays_step(r, ab[i], -1) == ab[i > 0 ? i - 1 : 0]);
    }
    CHECK(pd::plays_step(r, Plays::Note, 40) == Plays::Off);
    CHECK(pd::plays_step(r, Plays::Off, -40) == Plays::Note);
    CHECK(pd::plays_step(r, Plays::Triad, 4) == Plays::Off);
  }
  // C: NOTE TRIAD 7TH OPEN POWER FORMANT BEND MOUTH OFF.
  const Plays c[9] = {Plays::Note, Plays::Triad, Plays::Seventh, Plays::Open, Plays::Power, Plays::Formant, Plays::Bend, Plays::Mouth, Plays::Off};
  for (unsigned i = 0; i < 9; ++i) {
    CAPTURE(i);
    CHECK(pd::plays_tap(Row::C, c[i]) == c[(i + 1) % 9]);
    CHECK(pd::plays_step(Row::C, c[i], 1) == c[i < 8 ? i + 1 : 8]);
    CHECK(pd::plays_step(Row::C, c[i], -1) == c[i > 0 ? i - 1 : 0]);
  }
  CHECK(pd::plays_step(Row::C, Plays::Bend, 1) == Plays::Mouth);
  CHECK(pd::plays_step(Row::C, Plays::Mouth, 1) == Plays::Off);
  CHECK(pd::plays_step(Row::C, Plays::Mouth, -1) == Plays::Bend);
  CHECK(pd::plays_step(Row::C, Plays::Power, 1) == Plays::Formant);
  // A value the row does not offer is taken as the first.
  CHECK(pd::plays_tap(Row::C, Plays::Strip) == Plays::Triad);
  // Every value either step reaches is one the row offers.
  for (Row r : {Row::A, Row::B, Row::C}) {
    Plays p = Plays::Note;
    for (int i = 0; i < 14; ++i) {
      CHECK(pd::offered(r, p));
      p = pd::plays_tap(r, p);
    }
  }
}

TEST_CASE("pads: set_plays and set_strip") {
  pd::Layout l;  // A NOTE, B TRIAD, C FORMANT
  CHECK(pd::set_plays(l, Row::B, Plays::Power));
  CHECK(l.row[1] == Plays::Power);
  CHECK(l.row[0] == Plays::Note);  // the rows are independent
  // A strip names its parameter by a stable id; a plain value drops it.
  CHECK(pd::set_strip(l, Row::A, pd::strip_id(2, 0)));
  CHECK(l.row[0] == Plays::Strip);
  CHECK(l.strip[0] == pd::strip_id(2, 0));
  CHECK(l.strip[1] == pd::kNoStrip);
  CHECK(pd::set_plays(l, Row::A, Plays::Off));
  CHECK(l.row[0] == Plays::Off);
  CHECK(l.strip[0] == pd::kNoStrip);
  CHECK_FALSE(pd::set_strip(l, Row::A, pd::kNoStrip));
  CHECK(l.row[0] == Plays::Off);
  // set_plays never makes a strip (it has no id), and a value the row does not offer changes nothing.
  const pd::Layout before = l;
  CHECK_FALSE(pd::set_plays(l, Row::A, Plays::Strip));
  CHECK_FALSE(pd::set_plays(l, Row::A, Plays::Bend));
  CHECK_FALSE(pd::set_plays(l, Row::B, Plays::Formant));
  CHECK_FALSE(pd::set_plays(l, Row::C, static_cast<Plays>(6)));  // a retired piano byte is no value
  CHECK(std::memcmp(&l, &before, sizeof l) == 0);
}

TEST_CASE("pads: a saved byte, the retired piano values load as the plain value of the same kind") {
  for (unsigned v = 0; v <= 14; ++v) {
    CHECK(pd::valid_saved(static_cast<std::uint8_t>(v)));
  }
  CHECK_FALSE(pd::valid_saved(15));
  CHECK_FALSE(pd::valid_saved(255));
  CHECK(pd::plays_from_saved(0) == Plays::Note);
  CHECK(pd::plays_from_saved(5) == Plays::Off);
  CHECK(pd::plays_from_saved(6) == Plays::Note);     // PIANO NOTE
  CHECK(pd::plays_from_saved(7) == Plays::Triad);    // PIANO TRIAD
  CHECK(pd::plays_from_saved(8) == Plays::Seventh);  // PIANO 7TH
  CHECK(pd::plays_from_saved(9) == Plays::Open);     // PIANO OPEN
  CHECK(pd::plays_from_saved(10) == Plays::Power);   // PIANO POWER
  CHECK(pd::plays_from_saved(11) == Plays::Formant);
  CHECK(pd::plays_from_saved(14) == Plays::Strip);
  // Whatever it loads as, rows A and B offer it (C offers every kind too).
  for (unsigned v = 6; v <= 10; ++v) {
    for (Row r : {Row::A, Row::B, Row::C}) {
      CHECK(pd::offered(r, pd::plays_from_saved(static_cast<std::uint8_t>(v))));
    }
  }
}

TEST_CASE("pads: strip ids pack a tag and a parameter byte, and 0 is none") {
  CHECK(pd::kNoStrip == 0);
  CHECK(pd::strip_id(1, 5) == 0x0105);
  CHECK(pd::strip_id(4, 2) == 0x0402);
  CHECK(pd::strip_id(2, 0x1FF) == 0x02FF);  // the parameter is one byte
}

TEST_CASE("pads: a row plays one octave above each playing row below it") {
  // The default: A NOTE, B TRIAD, C FORMANT (C plays no note): B +0, A +1.
  const pd::Layout def;
  CHECK(pd::row_octave(def, Row::C) == 0);
  CHECK(pd::row_octave(def, Row::B) == 0);
  CHECK(pd::row_octave(def, Row::A) == 1);
  // Three note rows: C +0, B +1, A +2.
  const pd::Layout notes = make(Plays::Note, Plays::Note, Plays::Note);
  CHECK(pd::row_octave(notes, Row::C) == 0);
  CHECK(pd::row_octave(notes, Row::B) == 1);
  CHECK(pd::row_octave(notes, Row::A) == 2);
  // A parameter strip plays no note, so it gives the rows above no octave (the same as BEND and MOUTH).
  pd::Layout strip_c = make(Plays::Note, Plays::Triad, Plays::Strip);
  strip_c.strip[2] = pd::strip_id(2, 0);
  CHECK(pd::row_octave(strip_c, Row::B) == 0);
  CHECK(pd::row_octave(strip_c, Row::A) == 1);
  strip_c.row[1] = Plays::Strip;
  strip_c.strip[1] = pd::strip_id(2, 1);
  CHECK(pd::row_octave(strip_c, Row::A) == 0);
  CHECK_FALSE(pd::plays_notes(Plays::Strip));
  // C OFF and B OFF: A +0. B OFF alone lets A sit on C's octave plus one.
  CHECK(pd::row_octave(make(Plays::Note, Plays::Off, Plays::Off), Row::A) == 0);
  CHECK(pd::row_octave(make(Plays::Note, Plays::Off, Plays::Note), Row::A) == 1);
  CHECK(pd::row_octave(make(Plays::Note, Plays::Triad, Plays::Bend), Row::A) == 1);
  CHECK(pd::row_octave(make(Plays::Off, Plays::Note, Plays::Note), Row::B) == 1);
  // C MOUTH plays no note, so it gives the rows above no octave: the same as C BEND.
  CHECK(pd::row_octave(make(Plays::Note, Plays::Triad, Plays::Mouth), Row::B) == 0);
  CHECK(pd::row_octave(make(Plays::Note, Plays::Triad, Plays::Mouth), Row::A) == 1);
  CHECK_FALSE(pd::plays_notes(Plays::Mouth));
}

TEST_CASE("pads: a grid pad is the root of its column, the kind is the stored one") {
  const pd::Layout l = make(Plays::Note, Plays::Triad, Plays::Seventh);  // octaves: C +0, B +1, A +2
  const pd::PadPlay c = pd::pad_play(l, Row::C, 3, 0);
  CHECK(c.note == 48 + 3);
  CHECK(c.kind == 2);  // 7TH
  const pd::PadPlay b = pd::pad_play(l, Row::B, 0, 0);
  CHECK(b.note == 60);
  CHECK(b.kind == 1);
  const pd::PadPlay a = pd::pad_play(l, Row::A, 11, 0);
  CHECK(a.note == 72 + 11);
  CHECK(a.kind == 0);
  // The screen's octave shifts every row alike.
  CHECK(pd::pad_play(l, Row::B, 5, -2).note == 60 + 5 - 24);
  CHECK(pd::pad_play(l, Row::B, 5, 1).note == 60 + 5 + 12);
  // The same note on the same row whatever else the screen is: the layout alone decides.
  for (unsigned col = 0; col < 12; ++col) {
    CHECK(pd::pad_play(l, Row::A, col, 0).note == pd::pad_play(l, Row::B, col, 1).note);
  }
  // Past the grid.
  CHECK(pd::pad_play(l, Row::C, 12, 0).note == -1);
}

TEST_CASE("pads: the chord kinds store their own kind, a strip row plays no note") {
  CHECK(pd::pad_play(make(Plays::Note, Plays::Note, Plays::Off), Row::B, 0, 0).kind == 0);
  CHECK(pd::pad_play(make(Plays::Power, Plays::Power, Plays::Off), Row::B, 0, 0).kind == 4);
  pd::Layout l = make(Plays::Strip, Plays::Note, Plays::Note);
  l.strip[0] = pd::strip_id(2, 0);
  for (unsigned col = 0; col < 12; ++col) {
    CHECK(pd::pad_play(l, Row::A, col, 0).note == -1);
  }
}

TEST_CASE("pads: OFF, FORMANT, BEND and MOUTH play nothing, and notes stay in 1..127 and never 0") {
  const pd::Layout l = make(Plays::Off, Plays::Off, Plays::Formant);
  for (unsigned col = 0; col < 12; ++col) {
    for (Row r : {Row::A, Row::B, Row::C}) CHECK(pd::pad_play(l, r, col, 0).note == -1);
  }
  CHECK(pd::pad_play(make(Plays::Note, Plays::Note, Plays::Bend), Row::C, 0, 0).note == -1);
  for (unsigned col = 0; col < 12; ++col) CHECK(pd::pad_play(make(Plays::Note, Plays::Note, Plays::Mouth), Row::C, col, 0).note == -1);
  // The range: every note is 1..127, the edges are exact, one past is -1.
  const pd::Layout n = make(Plays::Note, Plays::Note, Plays::Note);
  for (int oct = -8; oct <= 8; ++oct) {
    for (Row r : {Row::A, Row::B, Row::C}) {
      for (unsigned col = 0; col < 12; ++col) {
        const int note = pd::pad_play(n, r, col, oct).note;
        CHECK(note != 0);
        CHECK(note >= -1);
        CHECK(note <= 127);
      }
    }
  }
  // C at octave -4 is 0..11: note 0 is a rest in a step, so column 0 is -1 and column 1 is 1.
  CHECK(pd::pad_play(n, Row::C, 0, -4).note == -1);
  CHECK(pd::pad_play(n, Row::C, 1, -4).note == 1);
  // C at octave 6: 120..131: 127 plays, 128 does not.
  CHECK(pd::pad_play(n, Row::C, 7, 6).note == 127);
  CHECK(pd::pad_play(n, Row::C, 8, 6).note == -1);
}

TEST_CASE("pads: the layout an older file meant") {
  // KEYS: two note rows on A and B (the piano is gone), the formant strip on C; with BEND ROW, BEND on C.
  pd::Layout k = pd::layout_from_legacy(true, ChordKind::Triad, false);
  CHECK(k.row[0] == Plays::Note);
  CHECK(k.row[1] == Plays::Note);
  CHECK(k.row[2] == Plays::Formant);
  CHECK(pd::layout_from_legacy(true, ChordKind::Power, true).row[2] == Plays::Bend);
  // CHORDS: C is the bottom kind, B the next, A the one after, whatever F6 was.
  for (unsigned bk = 0; bk < 4; ++bk) {
    const ChordKind bottom = static_cast<ChordKind>(bk);
    const pd::Layout c = pd::layout_from_legacy(false, bottom, false);
    CAPTURE(bk);
    CHECK(c.row[2] == pd::plays_of_kind(bottom));
    CHECK(c.row[1] == pd::plays_of_kind(next_kind(bottom, 1)));
    CHECK(c.row[0] == pd::plays_of_kind(next_kind(bottom, 2)));
    // The kinds are the ones kind_for_row gave PERFORM's rows 3, 2 and 1.
    CHECK(static_cast<unsigned>(c.row[2]) == static_cast<unsigned>(kind_for_row(3, bottom)) + 1u);
    CHECK(static_cast<unsigned>(c.row[1]) == static_cast<unsigned>(kind_for_row(2, bottom)) + 1u);
    CHECK(static_cast<unsigned>(c.row[0]) == static_cast<unsigned>(kind_for_row(1, bottom)) + 1u);
    // BEND ROW: the strip is row 3, so B is the bottom kind, A the next, C BEND.
    const pd::Layout b = pd::layout_from_legacy(false, bottom, true);
    CHECK(b.row[2] == Plays::Bend);
    CHECK(b.row[1] == pd::plays_of_kind(bottom));
    CHECK(b.row[0] == pd::plays_of_kind(next_kind(bottom, 1)));
    CHECK(static_cast<unsigned>(b.row[1]) == static_cast<unsigned>(kind_for_row(2, bottom, 2)) + 1u);
    CHECK(static_cast<unsigned>(b.row[0]) == static_cast<unsigned>(kind_for_row(1, bottom, 2)) + 1u);
  }
  // Each value a legacy layout gives is one its row offers.
  for (unsigned i = 0; i < 16; ++i) {
    const pd::Layout l = pd::layout_from_legacy((i & 1u) != 0, static_cast<ChordKind>((i >> 1) & 3u), (i & 8u) != 0);
    for (Row r : {Row::A, Row::B, Row::C}) CHECK(pd::offered(r, l.row[pd::idx(r)]));
  }
}

TEST_CASE("pads: legacy_fields derives the 1.0 settings of a layout (keys, the lowest chord row's kind, BEND ROW)") {
  pd::Layout l;  // A NOTE, B TRIAD, C FORMANT
  pd::LegacyFields f = pd::legacy_fields(l);
  CHECK(f.keys == 0);
  CHECK(f.chord == 0);  // B's TRIAD is the lowest chord row
  CHECK(f.bend_row == 0);

  l.row[2] = Plays::Seventh;  // C is the lowest row and a chord: its kind wins
  CHECK(pd::legacy_fields(l).chord == 1);
  l.row[2] = Plays::Bend;
  f = pd::legacy_fields(l);
  CHECK(f.bend_row == 1);
  CHECK(f.chord == 0);  // B's TRIAD again
  l.row[2] = Plays::Mouth;  // a mouth is not a chord, a key row nor a bend row
  f = pd::legacy_fields(l);
  CHECK(f.bend_row == 0);
  CHECK(f.chord == 0);
  CHECK(f.keys == 0);
  l.row[2] = Plays::Bend;

  l.row[1] = Plays::Off;
  l.row[0] = Plays::Power;  // only A is a chord
  CHECK(pd::legacy_fields(l).chord == 3);
  l.row[0] = Plays::Note;  // no chord at all
  CHECK(pd::legacy_fields(l).chord == 0);

  // `keys` is always 0 now: no layout is a piano.
  pd::Layout p;
  REQUIRE(pd::set_plays(p, Row::A, Plays::Open));
  f = pd::legacy_fields(p);
  CHECK(f.keys == 0);
  CHECK(f.chord == 0);  // B's TRIAD is still the lowest chord row
  CHECK(f.bend_row == 0);
  p.row[1] = Plays::Off;
  CHECK(pd::legacy_fields(p).chord == 2);  // only A, OPEN
  // A strip row is no chord and no bend row.
  REQUIRE(pd::set_strip(p, Row::C, pd::strip_id(2, 0)));
  CHECK(pd::legacy_fields(p).bend_row == 0);

  // A layout an older file meant gives its bend and chord settings back.
  for (unsigned i = 0; i < 16; ++i) {
    const bool keys = (i & 1u) != 0;
    const ChordKind bottom = static_cast<ChordKind>((i >> 1) & 3u);
    const bool bend = (i & 8u) != 0;
    const pd::LegacyFields g = pd::legacy_fields(pd::layout_from_legacy(keys, bottom, bend));
    CAPTURE(i);
    CHECK(g.keys == 0);
    CHECK((g.bend_row != 0) == bend);
    if (!keys) {
      CHECK(g.chord == static_cast<unsigned>(bottom));
    }
  }
}
