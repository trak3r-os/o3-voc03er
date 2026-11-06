// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The chord engine of PADS = CHORDS (docs/UI-DESIGN.md section 7). Pure: no engine, no screen, no state, no heap; a host test covers it
// (tests/chords_test.cpp). The caller keeps the previous chord and hands it back for SMOOTH.
//
// The pad: the column is the root (pitch class 0 = C at the left; the MIDI root is kChordBaseNote + 12 * octave + column, the pad map's
// bottom row), the row is the kind. KEY and SCALE are the vocoder's own (units/vocoder/scale.h, the table Autotune snaps to).
//
// Quality: the stack of thirds of the scale on the root's degree. The 7-note scales (MAJOR, MINOR, DORIAN) are their own parent; the others
// take the diatonic parent of their notes (CHROM and MAJ-P: MAJOR; MIN-P and BLUES: MINOR), so every kind has a quality. A root that is
// not in the parent plays the quality of the nearest note of it (a tie goes to the note below), still on its own root. `in_scale` is about
// the SCALE value itself (the LEDs: a root of the scale is dim, one outside it is off); with CHROM every root is in scale.
//
// Kinds: TRIAD (root, 3rd, 5th of the scale), SEVENTH (and the 7th), OPEN (root, fifth, ninth: "sus2"), POWER (root, fifth, octave: "5").
// Voicings, on the notes stacked upward from the root: CLOSE as stacked; OPEN the second note up an octave; WIDE the root, the notes above the
// second, the second up an octave and the root two octaves up (one note more, two octaves wide); SMOOTH the inversion nearest the previous
// chord (see smooth_cost), CLOSE when there is none; OPEN and POWER kinds keep their shape in SMOOTH (only the octave moves).
// Range: a chord that leaves 0..127 moves whole by octaves until it is inside, so its shape stays.

#include <o3_voc03er/pad_map.h>
#include <o3_voc03er/units/vocoder/scale.h>

#include <cstddef>
#include <cstdint>

namespace o3_voc03er {

enum class ChordKind : std::uint8_t { Triad, Seventh, Open, Power };
enum class Voicing : std::uint8_t { Close, Open, Wide, Smooth };

inline constexpr int kChordBaseNote = kBottomLeftNote;  // the root C at octave 0
inline constexpr std::size_t kChordMaxNotes = 5;
inline constexpr std::size_t kChordNameMax = 12;

struct Chord {
  std::uint8_t notes[kChordMaxNotes] = {};  // MIDI, ascending, the first `count`
  std::uint8_t count = 0;
  bool in_scale = false;  // the root is a note of SCALE (for the LEDs)
  char name[kChordNameMax] = {};  // "Dm7", "C5", "Bm7b5", "C#sus2"
};

// Which kind a pad row plays: row 0 (top) POWER, row 1 OPEN, row 2 SEVENTH, row 3 (bottom) `bottom` (the UP / DOWN choice).
constexpr ChordKind kind_for_row(unsigned row, ChordKind bottom) noexcept {
  return row == 0u ? ChordKind::Power : row == 1u ? ChordKind::Open : row == 2u ? ChordKind::Seventh : bottom;
}

namespace chords_detail {

// The diatonic parent of each SCALE value (an index into kScaleMask).
inline constexpr std::uint8_t kParent[kScaleTableCount] = {1, 1, 2, 1, 2, 2, 6};
inline constexpr char kRootName[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct Name {
  char* p;
  std::size_t n = 0;
  void add(const char* s) noexcept {
    while (*s != '\0' && n + 1 < kChordNameMax) p[n++] = *s++;
    p[n] = '\0';
  }
};

inline void sort_up(int* v, std::size_t n) noexcept {
  for (std::size_t i = 1; i < n; ++i) {
    const int x = v[i];
    std::size_t j = i;
    while (j > 0 && v[j - 1] > x) {
      v[j] = v[j - 1];
      --j;
    }
    v[j] = x;
  }
}

// The voice-leading distance between two ascending chords: voice i of one against voice i of the other, the summed semitones; the voices
// one chord has more than the other are measured against the other's top note. 0 = the same notes.
inline int smooth_cost(const int* a, std::size_t na, const std::uint8_t* b, std::size_t nb) noexcept {
  const std::size_t m = na < nb ? na : nb;
  int c = 0;
  auto d = [](int x, int y) { return x > y ? x - y : y - x; };
  for (std::size_t i = 0; i < m; ++i) c += d(a[i], b[i]);
  for (std::size_t i = m; i < na; ++i) c += d(a[i], b[nb - 1]);
  for (std::size_t i = m; i < nb; ++i) c += d(b[i], a[na - 1]);
  return c;
}


// The name suffix of the stacked thirds (`i3`, `i5`, `i7` semitones above the root).
inline const char* triad_suffix(int i3, int i5) noexcept {
  if (i3 == 3) return i5 == 6 ? "dim" : i5 == 8 ? "m#5" : "m";
  return i5 == 8 ? "+" : i5 == 6 ? "b5" : "";
}
inline const char* seventh_suffix(int i3, int i5, int i7) noexcept {
  if (i3 == 3) {
    if (i5 == 6) return i7 == 9 ? "dim7" : i7 == 10 ? "m7b5" : "mM7b5";
    return i7 == 11 ? "mM7" : "m7";
  }
  if (i5 == 8) return i7 == 11 ? "+M7" : "+7";
  return i7 == 11 ? "M7" : i7 == 10 ? "7" : "7b5";
}

}  // namespace chords_detail

// One chord. `root_pc` 0..11 (C = 0, any value is taken mod 12), `key` 0..11 (the KEY value), `scale` the SCALE value (past the table: CHROM,
// as the unit does). `prev` is the chord before, for SMOOTH only (null: none).
inline Chord make_chord(ChordKind kind, unsigned root_pc, int octave, unsigned key, unsigned scale, Voicing voicing,
                        const Chord* prev = nullptr) noexcept {
  using namespace chords_detail;
  Chord out;
  const int pc = static_cast<int>(root_pc % 12u);
  const int rel = (pc + 12 - static_cast<int>(key % 12u)) % 12;
  if (scale >= kScaleTableCount) scale = 0;
  out.in_scale = ((kScaleMask[scale] >> rel) & 1u) != 0u;

  // The degree whose quality the root takes: itself when it is a note of the parent, else the nearest (below wins a tie).
  const unsigned dm = kScaleMask[kParent[scale]];
  int deg = rel;
  for (int d = 0; d < 12; ++d) {
    const int lo = (rel + 12 - d) % 12;
    const int hi = (rel + d) % 12;
    if (((dm >> lo) & 1u) != 0u) {
      deg = lo;
      break;
    }
    if (((dm >> hi) & 1u) != 0u) {
      deg = hi;
      break;
    }
  }
  int degrees[12] = {};  // the parent's notes above the key, ascending
  int nd = 0;
  int at = 0;
  for (int b = 0; b < 12; ++b) {
    if (((dm >> b) & 1u) != 0u) {
      if (b == deg) at = nd;
      degrees[nd++] = b;
    }
  }
  auto above = [&](int steps) { return (degrees[(at + steps) % nd] - deg + 12) % 12; };  // 7 notes: thirds are 2 steps apart
  const int i3 = above(2);
  const int i5 = above(4);
  const int i7 = above(6);

  int s[kChordMaxNotes] = {};  // the stack, semitones above the root
  std::size_t n = 3;
  const char* suffix = "";
  switch (kind) {
    case ChordKind::Triad:
      s[1] = i3;
      s[2] = i5;
      suffix = triad_suffix(i3, i5);
      break;
    case ChordKind::Seventh:
      s[1] = i3;
      s[2] = i5;
      s[3] = i7;
      n = 4;
      suffix = seventh_suffix(i3, i5, i7);
      break;
    case ChordKind::Open:
      s[1] = 7;
      s[2] = 14;
      suffix = "sus2";
      break;
    case ChordKind::Power:
      s[1] = 7;
      s[2] = 12;
      suffix = "5";
      break;
  }

  Name nm{out.name};
  nm.add(kRootName[pc]);
  nm.add(suffix);

  // The shape in semitones above the root of the lowest note (voicings other than SMOOTH), or the absolute notes (SMOOTH).
  int v[kChordMaxNotes] = {};
  std::size_t vn = n;
  const int root = kChordBaseNote + 12 * octave + pc;
  bool smooth = voicing == Voicing::Smooth && prev != nullptr && prev->count > 0;
  if (smooth) {
    const bool rotates = kind == ChordKind::Triad || kind == ChordKind::Seventh;
    const std::size_t turns = rotates ? n : 1;
    int best = 0x7fffffff;
    for (std::size_t k = 0; k < turns; ++k) {
      int r[kChordMaxNotes] = {};
      for (std::size_t j = 0; j < n; ++j) r[j] = s[(k + j) % n] + (k + j >= n ? 12 : 0);
      sort_up(r, n);
      for (int q = 0; q < 11; ++q) {
        int c[kChordMaxNotes] = {};
        for (std::size_t j = 0; j < n; ++j) c[j] = pc + 12 * q + r[j];
        if (c[n - 1] > 127) break;
        const int cost = smooth_cost(c, n, prev->notes, prev->count);
        if (cost < best) {
          best = cost;
          for (std::size_t j = 0; j < n; ++j) v[j] = c[j];
        }
      }
    }
    smooth = best != 0x7fffffff;
  }
  if (!smooth) {
    switch (voicing) {
      case Voicing::Open:
        s[1] += 12;
        sort_up(s, n);
        break;
      case Voicing::Wide: {
        int w[kChordMaxNotes] = {};
        std::size_t m = 0;
        w[m++] = s[0];
        for (std::size_t j = 2; j < n; ++j) w[m++] = s[j];
        w[m++] = s[1] + 12;
        w[m++] = s[0] + 24;
        sort_up(w, m);
        for (std::size_t j = 0; j < m; ++j) s[j] = w[j];
        n = m;
        break;
      }
      case Voicing::Close:
      case Voicing::Smooth:
        break;
    }
    vn = n;
    for (std::size_t j = 0; j < n; ++j) v[j] = root + s[j];
  }

  // Into 0..127 as a whole, by octaves.
  int lo = v[0];
  int hi = v[vn - 1];
  while (hi > 127 && lo >= 12) {
    for (std::size_t j = 0; j < vn; ++j) v[j] -= 12;
    lo -= 12;
    hi -= 12;
  }
  while (lo < 0 && hi <= 115) {
    for (std::size_t j = 0; j < vn; ++j) v[j] += 12;
    lo += 12;
    hi += 12;
  }
  for (std::size_t j = 0; j < vn; ++j) out.notes[j] = static_cast<std::uint8_t>(v[j] < 0 ? 0 : v[j] > 127 ? 127 : v[j]);  // only if the span itself does not fit
  out.count = static_cast<std::uint8_t>(vn);
  return out;
}

// The chord of a pad (row 0..3, column 0..11); `bottom` is what the bottom row plays.
inline Chord chord_for_pad(unsigned row, unsigned col, int octave, unsigned key, unsigned scale, Voicing voicing,
                           const Chord* prev = nullptr, ChordKind bottom = ChordKind::Triad) noexcept {
  return make_chord(kind_for_row(row, bottom), col, octave, key, scale, voicing, prev);
}

}  // namespace o3_voc03er
