// SPDX-License-Identifier: GPL-3.0-or-later
// The chord engine is pure: a table of hand-checked chords says what it must do (C = 0, root C3 = 48 at octave 0).
#include <doctest/doctest.h>

#include <o3_voc03er/chords.h>

#include <cstring>
#include <initializer_list>

using namespace o3_voc03er;

namespace {

constexpr unsigned kMajor = 1, kMinor = 2;

struct Case {
  unsigned pc;
  const char* name;
  std::initializer_list<int> notes;
  bool in_scale = true;
};

void expect(const Chord& c, const Case& e) {
  CAPTURE(e.name);
  CHECK(std::strcmp(c.name, e.name) == 0);
  REQUIRE(c.count == e.notes.size());
  std::size_t i = 0;
  for (int n : e.notes) {
    CHECK(c.notes[i] == n);
    ++i;
  }
  CHECK(c.in_scale == e.in_scale);
}

}  // namespace

TEST_CASE("chords: triads of C major, every root") {
  const Case t[] = {{0, "C", {48, 52, 55}},   {1, "C#", {49, 53, 56}, false}, {2, "Dm", {50, 53, 57}},
                    {3, "D#m", {51, 54, 58}, false}, {4, "Em", {52, 55, 59}},  {5, "F", {53, 57, 60}},
                    {6, "F#", {54, 58, 61}, false}, {7, "G", {55, 59, 62}},   {8, "G#", {56, 60, 63}, false},
                    {9, "Am", {57, 60, 64}},  {10, "A#m", {58, 61, 65}, false}, {11, "Bdim", {59, 62, 65}}};
  for (const Case& e : t) expect(make_chord(ChordKind::Triad, e.pc, 0, 0, kMajor, Voicing::Close), e);
}

TEST_CASE("chords: sevenths of C major, every diatonic root") {
  const Case t[] = {{0, "Cmaj7", {48, 52, 55, 59}}, {2, "Dm7", {50, 53, 57, 60}}, {4, "Em7", {52, 55, 59, 62}},
                    {5, "Fmaj7", {53, 57, 60, 64}}, {7, "G7", {55, 59, 62, 65}},   {9, "Am7", {57, 60, 64, 67}},
                    {11, "Bm7b5", {59, 62, 65, 69}}};
  for (const Case& e : t) expect(make_chord(ChordKind::Seventh, e.pc, 0, 0, kMajor, Voicing::Close), e);
}

TEST_CASE("chords: open and power on every root are the same shape") {
  for (unsigned pc = 0; pc < 12; ++pc) {
    const Chord o = make_chord(ChordKind::Open, pc, 0, 0, kMajor, Voicing::Close);
    CHECK(o.count == 3);
    CHECK(o.notes[0] == 48 + pc);
    CHECK(o.notes[1] == 55 + pc);
    CHECK(o.notes[2] == 62 + pc);
    const Chord p = make_chord(ChordKind::Power, pc, 0, 0, kMajor, Voicing::Close);
    CHECK(p.count == 3);
    CHECK(p.notes[1] == 55 + pc);
    CHECK(p.notes[2] == 60 + pc);
  }
  expect(make_chord(ChordKind::Open, 0, 0, 0, kMajor, Voicing::Close), {0, "Csus2", {48, 55, 62}});
  expect(make_chord(ChordKind::Power, 0, 0, 0, kMajor, Voicing::Close), {0, "C5", {48, 55, 60}});
  expect(make_chord(ChordKind::Power, 9, 0, 0, kMajor, Voicing::Close), {9, "A5", {57, 64, 69}});
}

TEST_CASE("chords: triads and sevenths of A minor, every root") {
  const Case tri[] = {{0, "C", {48, 52, 55}},  {2, "Dm", {50, 53, 57}}, {4, "Em", {52, 55, 59}}, {5, "F", {53, 57, 60}},
                      {7, "G", {55, 59, 62}},  {9, "Am", {57, 60, 64}}, {11, "Bdim", {59, 62, 65}}};
  for (const Case& e : tri) expect(make_chord(ChordKind::Triad, e.pc, 0, 9, kMinor, Voicing::Close), e);
  const Case sev[] = {{9, "Am7", {57, 60, 64, 67}},  {11, "Bm7b5", {59, 62, 65, 69}}, {0, "Cmaj7", {48, 52, 55, 59}},
                      {2, "Dm7", {50, 53, 57, 60}},  {4, "Em7", {52, 55, 59, 62}},     {5, "Fmaj7", {53, 57, 60, 64}},
                      {7, "G7", {55, 59, 62, 65}}};
  for (const Case& e : sev) expect(make_chord(ChordKind::Seventh, e.pc, 0, 9, kMinor, Voicing::Close), e);
  // The notes outside the scale: A minor has no C#, D#, F#, G#, A#.
  CHECK(!make_chord(ChordKind::Triad, 1, 0, 9, kMinor, Voicing::Close).in_scale);
  CHECK(make_chord(ChordKind::Triad, 0, 0, 9, kMinor, Voicing::Close).in_scale);
}

TEST_CASE("chords: a root out of the scale takes the quality of the nearest note and says so") {
  // C# in C major: C and D are equally near, the one below (C, major) wins; F# the same (F, major); A# between A (minor) and B: A below wins.
  const Chord cs = make_chord(ChordKind::Triad, 1, 0, 0, kMajor, Voicing::Close);
  CHECK(std::strcmp(cs.name, "C#") == 0);
  CHECK(!cs.in_scale);
  const Chord as = make_chord(ChordKind::Seventh, 10, 0, 0, kMajor, Voicing::Close);
  CHECK(std::strcmp(as.name, "A#m7") == 0);
  CHECK(as.notes[1] - as.notes[0] == 3);
  CHECK(!as.in_scale);
  // The pentatonic's diatonic parent gives the quality, the pentatonic itself the LED: D is not in C MAJ-P? It is (0 2 4 7 9); F is not, and plays F major.
  const Chord f = make_chord(ChordKind::Triad, 5, 0, 0, 3, Voicing::Close);
  CHECK(std::strcmp(f.name, "F") == 0);
  CHECK(!f.in_scale);
  CHECK(make_chord(ChordKind::Triad, 2, 0, 0, 3, Voicing::Close).in_scale);
  // CHROM: every root is in scale.
  CHECK(make_chord(ChordKind::Triad, 6, 0, 0, 0, Voicing::Close).in_scale);
  // The key moves the degrees: D dorian (key 2, scale 6) on D is Dm, on G is G major.
  CHECK(std::strcmp(make_chord(ChordKind::Triad, 2, 0, 2, 6, Voicing::Close).name, "Dm") == 0);
  CHECK(std::strcmp(make_chord(ChordKind::Triad, 7, 0, 2, 6, Voicing::Close).name, "G") == 0);
}

TEST_CASE("chords: the pad's row picks the kind") {
  CHECK(kind_for_row(0, ChordKind::Open) == ChordKind::Power);
  CHECK(kind_for_row(1, ChordKind::Open) == ChordKind::Open);
  CHECK(kind_for_row(2, ChordKind::Open) == ChordKind::Seventh);
  CHECK(kind_for_row(3, ChordKind::Open) == ChordKind::Open);
  CHECK(kind_for_row(3, ChordKind::Triad) == ChordKind::Triad);
  const Chord c = chord_for_pad(2, 2, 0, 0, kMajor, Voicing::Close);
  CHECK(std::strcmp(c.name, "Dm7") == 0);
}

TEST_CASE("chords: OPEN and WIDE voicings") {
  expect(make_chord(ChordKind::Triad, 0, 0, 0, kMajor, Voicing::Open), {0, "C", {48, 55, 64}});
  expect(make_chord(ChordKind::Seventh, 2, 0, 0, kMajor, Voicing::Open), {2, "Dm7", {50, 57, 60, 65}});
  expect(make_chord(ChordKind::Triad, 0, 0, 0, kMajor, Voicing::Wide), {0, "C", {48, 55, 64, 72}});
  expect(make_chord(ChordKind::Seventh, 7, 0, 0, kMajor, Voicing::Wide), {7, "G7", {55, 62, 65, 71, 79}});
}

TEST_CASE("chords: SMOOTH leads the voices through I vi IV V") {
  // Nothing before: the close chord. Then the inversion with the least summed movement; a tie goes to the lower rotation, then the lower octave.
  Chord c = chord_for_pad(3, 0, 0, 0, kMajor, Voicing::Smooth);  // C
  expect(c, {0, "C", {48, 52, 55}});
  c = chord_for_pad(3, 9, 0, 0, kMajor, Voicing::Smooth, &c);  // Am: C E A, 2 semitones
  expect(c, {9, "Am", {48, 52, 57}});
  c = chord_for_pad(3, 5, 0, 0, kMajor, Voicing::Smooth, &c);  // F: C F A, 1 semitone
  expect(c, {5, "F", {48, 53, 57}});
  c = chord_for_pad(3, 7, 0, 0, kMajor, Voicing::Smooth, &c);  // G: B D G and D G B both cost 6; B D G (rotation 1) wins
  expect(c, {7, "G", {47, 50, 55}});
  // The same chord again does not move.
  const Chord again = chord_for_pad(3, 7, 0, 0, kMajor, Voicing::Smooth, &c);
  expect(again, {7, "G", {47, 50, 55}});
  // Four notes after three: the extra voice is measured against the top of the shorter chord.
  const Chord g7 = chord_for_pad(2, 7, 0, 0, kMajor, Voicing::Smooth, &c);
  CHECK(g7.count == 4);
  CHECK(g7.notes[0] == 47);
}

TEST_CASE("chords: the edges of the MIDI range keep the shape") {
  for (int oct = -12; oct <= 12; ++oct) {
    for (unsigned pc = 0; pc < 12; ++pc) {
      for (unsigned k = 0; k < 4; ++k) {
        for (unsigned v = 0; v < 4; ++v) {
          const Chord c = make_chord(static_cast<ChordKind>(k), pc, oct, 0, kMajor, static_cast<Voicing>(v));
          REQUIRE(c.count >= 3);
          REQUIRE(c.count <= kChordMaxNotes);
          for (unsigned i = 1; i < c.count; ++i) REQUIRE(c.notes[i] > c.notes[i - 1]);
          CHECK(c.notes[c.count - 1] <= 127);
          // The shape (the intervals) is the one of the unclamped chord, whole octaves away.
          const Chord mid = make_chord(static_cast<ChordKind>(k), pc, 0, 0, kMajor, static_cast<Voicing>(v));
          REQUIRE(mid.count == c.count);
          for (unsigned i = 1; i < c.count; ++i) CHECK(c.notes[i] - c.notes[0] == mid.notes[i] - mid.notes[0]);
          CHECK((c.notes[0] - mid.notes[0]) % 12 == 0);
        }
      }
    }
  }
  // A chord at the top: B seventh at octave 4 would be past 127 (48 + 48 + 11 = 107 + 11), so it comes down whole.
  const Chord top = make_chord(ChordKind::Seventh, 11, 6, 0, kMajor, Voicing::Close);
  CHECK(top.notes[3] <= 127);
  CHECK(top.notes[3] >= 116);
  const Chord low = make_chord(ChordKind::Triad, 0, -6, 0, kMajor, Voicing::Close);
  CHECK(low.notes[0] == 0);
  CHECK(low.notes[1] == 4);
  CHECK(low.notes[2] == 7);
  // A wide chord that needs its whole span at the top.
  const Chord wide = make_chord(ChordKind::Seventh, 11, 5, 0, kMajor, Voicing::Wide);
  CHECK(wide.count == 5);
  CHECK(wide.notes[4] <= 127);
}
