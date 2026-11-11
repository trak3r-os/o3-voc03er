// SPDX-License-Identifier: GPL-3.0-or-later
// The pads' layout (app/src/pads_logic.h): the PLAYS values and the rows that offer them, the piano join and split, each row's octave, what a pad plays.
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
  CHECK(static_cast<int>(Plays::PianoNote) == 6);
  CHECK(static_cast<int>(Plays::PianoPower) == 10);
  CHECK(static_cast<int>(Plays::Formant) == 11);
  CHECK(static_cast<int>(Plays::Bend) == 12);
  CHECK(std::strcmp(pd::kPlaysNames[0], "NOTE") == 0);
  CHECK(std::strcmp(pd::kPlaysNames[2], "7TH") == 0);
  CHECK(std::strcmp(pd::kPlaysNames[6], "PIANO NOTE") == 0);
  CHECK(std::strcmp(pd::kPlaysNames[12], "BEND") == 0);
  CHECK(std::strcmp(pd::kRowNames[2], "C") == 0);
  // 0..4 are the step's stored kind: a note, then the ChordKind + 1.
  CHECK(static_cast<int>(Plays::Triad) == static_cast<int>(ChordKind::Triad) + 1);
  CHECK(static_cast<int>(Plays::Power) == static_cast<int>(ChordKind::Power) + 1);
  const pd::Layout def;
  CHECK(def.row[0] == Plays::Note);
  CHECK(def.row[1] == Plays::Triad);
  CHECK(def.row[2] == Plays::Formant);
  CHECK(def.last[0] == Plays::Note);
  CHECK(def.last[1] == Plays::Triad);
  CHECK_FALSE(pd::is_piano(def));
}

TEST_CASE("pads: which row offers which value") {
  for (unsigned v = 0; v < pd::kPlaysCount; ++v) {
    const Plays p = static_cast<Plays>(v);
    CAPTURE(v);
    CHECK(pd::offered(Row::A, p) == (v <= 10));
    CHECK(pd::offered(Row::B, p) == (v <= 10));
    CHECK(pd::offered(Row::C, p) == (v <= 5 || v >= 11));
  }
}

TEST_CASE("pads: a tap steps and wraps, the encoder steps and stops at the ends") {
  // A and B: NOTE TRIAD 7TH OPEN POWER PIANO NOTE PIANO TRIAD PIANO 7TH PIANO OPEN PIANO POWER OFF.
  const Plays ab[11] = {Plays::Note,       Plays::Triad,      Plays::Seventh,     Plays::Open,       Plays::Power,    Plays::PianoNote,
                        Plays::PianoTriad, Plays::PianoSeventh, Plays::PianoOpen, Plays::PianoPower, Plays::Off};
  for (Row r : {Row::A, Row::B}) {
    for (unsigned i = 0; i < 11; ++i) {
      CAPTURE(i);
      CHECK(pd::plays_tap(r, ab[i]) == ab[(i + 1) % 11]);
      CHECK(pd::plays_step(r, ab[i], 1) == ab[i < 10 ? i + 1 : 10]);
      CHECK(pd::plays_step(r, ab[i], -1) == ab[i > 0 ? i - 1 : 0]);
    }
    CHECK(pd::plays_step(r, Plays::Note, 40) == Plays::Off);
    CHECK(pd::plays_step(r, Plays::Off, -40) == Plays::Note);
    CHECK(pd::plays_step(r, Plays::Triad, 4) == Plays::PianoNote);
  }
  // C: NOTE TRIAD 7TH OPEN POWER FORMANT BEND OFF.
  const Plays c[8] = {Plays::Note, Plays::Triad, Plays::Seventh, Plays::Open, Plays::Power, Plays::Formant, Plays::Bend, Plays::Off};
  for (unsigned i = 0; i < 8; ++i) {
    CAPTURE(i);
    CHECK(pd::plays_tap(Row::C, c[i]) == c[(i + 1) % 8]);
    CHECK(pd::plays_step(Row::C, c[i], 1) == c[i < 7 ? i + 1 : 7]);
    CHECK(pd::plays_step(Row::C, c[i], -1) == c[i > 0 ? i - 1 : 0]);
  }
  CHECK(pd::plays_step(Row::C, Plays::Power, 1) == Plays::Formant);
  // A value the row does not offer is taken as the first.
  CHECK(pd::plays_tap(Row::C, Plays::PianoNote) == Plays::Triad);
  // Every value either step reaches is one the row offers.
  for (Row r : {Row::A, Row::B, Row::C}) {
    Plays p = Plays::Note;
    for (int i = 0; i < 12; ++i) {
      CHECK(pd::offered(r, p));
      p = pd::plays_tap(r, p);
    }
  }
}

TEST_CASE("pads: a piano value joins A and B, a grid value splits them and `last` comes back") {
  pd::Layout l;  // A NOTE, B TRIAD, C FORMANT
  CHECK(pd::set_plays(l, Row::B, Plays::PianoTriad));
  CHECK(pd::is_piano(l));
  CHECK(l.row[0] == Plays::PianoTriad);
  CHECK(l.row[1] == Plays::PianoTriad);
  CHECK(l.last[0] == Plays::Note);
  CHECK(l.last[1] == Plays::Triad);
  // Another piano value changes both and does not overwrite `last` with piano values.
  CHECK(pd::set_plays(l, Row::A, Plays::PianoOpen));
  CHECK(l.row[0] == Plays::PianoOpen);
  CHECK(l.row[1] == Plays::PianoOpen);
  CHECK(l.last[0] == Plays::Note);
  CHECK(l.last[1] == Plays::Triad);
  // A grid value on A: A takes it, B gets its own back.
  CHECK(pd::set_plays(l, Row::A, Plays::Power));
  CHECK_FALSE(pd::is_piano(l));
  CHECK(l.row[0] == Plays::Power);
  CHECK(l.row[1] == Plays::Triad);
  // The same on B, from another start: A NOTE, B 7TH, joined from A, then B splits.
  l = pd::Layout{};
  l.row[1] = Plays::Seventh;
  CHECK(pd::set_plays(l, Row::A, Plays::PianoNote));
  CHECK(l.last[1] == Plays::Seventh);
  CHECK(pd::set_plays(l, Row::B, Plays::Off));
  CHECK(l.row[1] == Plays::Off);
  CHECK(l.row[0] == Plays::Note);
  // C is never part of the join, and a grid value on C leaves a piano alone.
  l = pd::Layout{};
  pd::set_plays(l, Row::A, Plays::PianoNote);
  CHECK(pd::set_plays(l, Row::C, Plays::Off));
  CHECK(pd::is_piano(l));
  CHECK(l.row[2] == Plays::Off);
  // A value the row does not offer changes nothing.
  const pd::Layout before = l;
  CHECK_FALSE(pd::set_plays(l, Row::C, Plays::PianoNote));
  CHECK_FALSE(pd::set_plays(l, Row::A, Plays::Bend));
  CHECK_FALSE(pd::set_plays(l, Row::B, Plays::Formant));
  CHECK(std::memcmp(&l, &before, sizeof l) == 0);
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
  // A piano with C TRIAD: C +0, the piano +1, on both halves.
  const pd::Layout piano = make(Plays::PianoNote, Plays::PianoNote, Plays::Triad);
  CHECK(pd::row_octave(piano, Row::C) == 0);
  CHECK(pd::row_octave(piano, Row::B) == 1);
  CHECK(pd::row_octave(piano, Row::A) == 1);
  // A piano over a C that plays nothing: both +0.
  const pd::Layout piano_off = make(Plays::PianoTriad, Plays::PianoTriad, Plays::Bend);
  CHECK(pd::row_octave(piano_off, Row::B) == 0);
  CHECK(pd::row_octave(piano_off, Row::A) == 0);
  // C OFF and B OFF: A +0. B OFF alone lets A sit on C's octave plus one.
  CHECK(pd::row_octave(make(Plays::Note, Plays::Off, Plays::Off), Row::A) == 0);
  CHECK(pd::row_octave(make(Plays::Note, Plays::Off, Plays::Note), Row::A) == 1);
  CHECK(pd::row_octave(make(Plays::Note, Plays::Triad, Plays::Bend), Row::A) == 1);
  CHECK(pd::row_octave(make(Plays::Off, Plays::Note, Plays::Note), Row::B) == 1);
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

TEST_CASE("pads: the piano is B for the white keys and A for the black ones") {
  const pd::Layout l = make(Plays::PianoTriad, Plays::PianoTriad, Plays::Triad);  // the piano is at +1
  const int white[12] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19};
  const int black[12] = {-1, 1, 3, -1, 6, 8, 10, -1, 13, 15, -1, 18};
  for (unsigned col = 0; col < 12; ++col) {
    CAPTURE(col);
    const pd::PadPlay w = pd::pad_play(l, Row::B, col, 0);
    CHECK(w.note == 60 + white[col]);
    CHECK(w.kind == 1);  // TRIAD
    const pd::PadPlay k = pd::pad_play(l, Row::A, col, 0);
    if (black[col] < 0) {
      CHECK(k.note == -1);  // a gap
    } else {
      CHECK(k.note == 60 + black[col]);
      CHECK(k.kind == 1);
    }
  }
  // PIANO NOTE stores a note; PIANO POWER stores POWER.
  CHECK(pd::pad_play(make(Plays::PianoNote, Plays::PianoNote, Plays::Off), Row::B, 0, 0).kind == 0);
  CHECK(pd::pad_play(make(Plays::PianoPower, Plays::PianoPower, Plays::Off), Row::B, 0, 0).kind == 4);
}

TEST_CASE("pads: OFF, FORMANT and BEND play nothing, and notes stay in 1..127 and never 0") {
  const pd::Layout l = make(Plays::Off, Plays::Off, Plays::Formant);
  for (unsigned col = 0; col < 12; ++col) {
    for (Row r : {Row::A, Row::B, Row::C}) CHECK(pd::pad_play(l, r, col, 0).note == -1);
  }
  CHECK(pd::pad_play(make(Plays::Note, Plays::Note, Plays::Bend), Row::C, 0, 0).note == -1);
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
  // KEYS: a piano on A and B, the formant strip on C; with BEND ROW, BEND on C. `last` is the default's.
  pd::Layout k = pd::layout_from_legacy(true, ChordKind::Triad, false);
  CHECK(k.row[0] == Plays::PianoNote);
  CHECK(k.row[1] == Plays::PianoNote);
  CHECK(k.row[2] == Plays::Formant);
  CHECK(k.last[0] == Plays::Note);
  CHECK(k.last[1] == Plays::Triad);
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
