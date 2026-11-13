// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pattern generator of the PATTERN screen: seeded, deterministic rolls for one lane at a time (NOTE, SLICE, MOUTH, LEVEL = ACCENT, CARRIER = NOISE hits).
// Pure logic, no screen and no engine: tests/pattern_gen_test.cpp calls it directly. Every function writes only its own lane of steps 0..len-1 and keeps the
// other lanes of each step and every step from len on. The same arguments give the same pattern: all randomness is pattern::xorshift32 through h(seed, step,
// salt), with no state.
//
// Which steps play: a weight table of 16 a style (index step % 16) plus a jitter of 0..49 from h; in a segment of n steps the N best play (a tie goes to the
// earlier step), N = round(n * density / 8) kept in 1..n, so with one seed a higher DENSITY only adds steps. For len even and >= 8 the pattern has a phrase
// shape: segment A is steps [0, len / 2), ranked; steps [len / 2, len - q) repeat A (q = max(1, len / 4)); the tail [len - q, len) is ranked anew and plays
// as many steps as A plays in its own last q. Otherwise the whole pattern is one segment. The NOTE styles and CHOP always play step 0.

#include <o3_voc03er/chords.h>
#include <o3_voc03er/route.h>
#include <o3_voc03er/stepseq.h>

#include "pattern_logic.h"

#include <cstdint>

namespace o3_voc03er::pattern_gen {

enum class NoteStyle : std::uint8_t { Bass, Lead, Chords, Long };
inline constexpr unsigned kNoteStyles = 4;
inline constexpr const char* kNoteStyleNames[kNoteStyles] = {"BASS", "LEAD", "CHORDS", "LONG"};
enum class SliceStyle : std::uint8_t { Chop, Order };
inline constexpr unsigned kSliceStyles = 2;
inline constexpr const char* kSliceStyleNames[kSliceStyles] = {"CHOP", "ORDER"};
enum class MouthStyle : std::uint8_t { Vowels, Words };
inline constexpr unsigned kMouthStyles = 2;
inline constexpr const char* kMouthStyleNames[kMouthStyles] = {"VOWELS", "WORDS"};
inline constexpr unsigned kDensityMin = 1, kDensityMax = 8, kDensityDefault = 4;

// `len` is the span written (1..16, the pattern's LENGTH; roll_notes does not change LENGTH). `root_note` is the MIDI note of the key's root in the octave the
// pads play; when its pitch class is not `key` the nearest note of that class is the root (a tie goes down), so the notes always sit in the key. `scale` is the
// SCALE value (CHROM and any value past the table: MAJOR's notes).
struct NoteArgs {
  unsigned len;
  NoteStyle style;
  unsigned density;
  std::uint32_t seed;
  unsigned key;
  unsigned scale;
  unsigned root_note;
  ChordKind chord;
  Voicing voicing;
};

namespace detail {

namespace pt = o3_voc03er::pattern;

// The weights of "which steps play", index step % 16.
inline constexpr std::uint8_t kWBass[16] = {100, 20, 45, 55, 70, 20, 60, 35, 85, 20, 45, 55, 70, 25, 60, 40};
inline constexpr std::uint8_t kWLead[16] = {100, 30, 60, 40, 70, 30, 65, 45, 80, 30, 60, 40, 70, 35, 65, 50};
inline constexpr std::uint8_t kWChords[16] = {100, 10, 30, 60, 40, 10, 70, 20, 50, 10, 65, 30, 60, 10, 45, 35};
inline constexpr std::uint8_t kWWords[16] = {100, 10, 40, 20, 80, 10, 40, 20, 90, 10, 40, 20, 80, 10, 40, 30};
inline constexpr std::uint8_t kWAccent[16] = {90, 10, 40, 30, 100, 10, 50, 30, 90, 10, 40, 30, 100, 10, 50, 40};
inline constexpr std::uint8_t kWNoise[16] = {20, 30, 100, 30, 20, 30, 100, 40, 20, 30, 100, 30, 20, 30, 100, 60};

// One salt a use of h, so the rolls of a seed do not move together. The tail of a phrase ranks with kTailSalt added.
enum Salt : unsigned {
  kSaltBass = 1,
  kSaltLead = 2,
  kSaltChords = 3,
  kSaltChop = 4,
  kSaltWords = 5,
  kSaltAccent = 6,
  kSaltNoise = 7,
  kSaltBassPitch = 11,
  kSaltLeadStart = 12,
  kSaltLeadMove = 13,
  kSaltLeadTie = 14,
  kSaltSections = 15,
  kSaltRow = 16,
  kSaltChordTie = 17,
  kSaltSlice = 18,
  kSaltWord = 19,
  kTailSalt = 64
};

// h(seed, step, salt): a 32-bit hash, two xorshift32 rounds over a mix of the three (xorshift32 must not start at 0).
constexpr std::uint32_t h(std::uint32_t seed, unsigned step, unsigned salt) noexcept {
  std::uint32_t x = (seed * 2654435761u) ^ ((step + 1u) * 0x9E3779B1u) ^ ((salt + 1u) * 0x85EBCA6Bu);
  if (x == 0u) {
    x = 0x9E3779B9u;
  }
  return pt::xorshift32(pt::xorshift32(x));
}

constexpr unsigned clamp_len(unsigned len) noexcept { return len < 1u ? 1u : (len > kPatternSteps ? kPatternSteps : len); }
constexpr unsigned clamp_density(unsigned d) noexcept { return d < kDensityMin ? kDensityMin : (d > kDensityMax ? kDensityMax : d); }
// round(n * density / 8) in 1..n
constexpr unsigned play_count(unsigned n, unsigned density) noexcept {
  const unsigned c = (n * density + 4u) / 8u;
  return c < 1u ? 1u : (c > n ? n : c);
}

constexpr unsigned popcount(unsigned m) noexcept {
  unsigned c = 0;
  for (; m != 0u; m &= m - 1u) {
    ++c;
  }
  return c;
}

// The phrase shape of a pattern of `len` steps.
struct Phrase {
  bool shaped;
  unsigned half;  // segment A is [0, half)
  unsigned q;     // the tail is [len - q, len)
};
constexpr Phrase phrase_of(unsigned len) noexcept {
  const unsigned q = len / 4u < 1u ? 1u : len / 4u;
  return Phrase{len % 2u == 0u && len >= 8u, len / 2u, q};
}
// The steps whose rhythm a step repeats (half + j is j inside the repeat); the step itself elsewhere.
constexpr unsigned source_of(const Phrase& ph, unsigned len, unsigned i) noexcept { return ph.shaped && i >= ph.half && i < len - ph.q ? i - ph.half : i; }

// The `count` best of the n steps from `from` (bit = the step index) by weight + jitter; a tie goes to the earlier step. `first` puts step `from` on top.
inline unsigned rank_mask(const std::uint8_t (&w)[16], std::uint32_t seed, unsigned salt, unsigned from, unsigned n, unsigned count, bool first) noexcept {
  unsigned score[kPatternSteps] = {};
  for (unsigned j = 0; j < n; ++j) {
    score[j] = w[(from + j) % 16u] + h(seed, from + j, salt) % 50u + (first && j == 0u ? 1000u : 0u);
  }
  unsigned picked = 0;
  unsigned mask = 0;
  for (unsigned k = 0; k < count && k < n; ++k) {
    unsigned best = n;
    for (unsigned j = 0; j < n; ++j) {
      if ((picked >> j & 1u) == 0u && (best == n || score[j] > score[best])) {
        best = j;
      }
    }
    picked |= 1u << best;
    mask |= 1u << (from + best);
  }
  return mask;
}

// The steps that play, as a mask over 0..len-1, with the phrase shape; `first`: step 0 always plays.
inline unsigned play_mask(const std::uint8_t (&w)[16], std::uint32_t seed, unsigned salt, unsigned len, unsigned density, bool first) noexcept {
  const Phrase ph = phrase_of(len);
  if (!ph.shaped) {
    return rank_mask(w, seed, salt, 0, len, play_count(len, density), first);
  }
  const unsigned a = rank_mask(w, seed, salt, 0, ph.half, play_count(ph.half, density), first);
  unsigned m = a;
  for (unsigned i = ph.half; i < len - ph.q; ++i) {
    if ((a >> (i - ph.half) & 1u) != 0u) {
      m |= 1u << i;
    }
  }
  const unsigned in_last_q = (a >> (ph.half - ph.q)) & ((1u << ph.q) - 1u);
  m |= rank_mask(w, seed, salt + kTailSalt, len - ph.q, ph.q, popcount(in_last_q), false);
  return m;
}

// ---- pitch ------------------------------------------------------------------------------------------------------------------------

// The key's notes: the scale's intervals ascending (5 to 7) and the 7-note parent's.
struct Scale {
  int root;
  unsigned n;
  int iv[7];
  int piv[7];
};
inline void intervals_of(unsigned mask, int (&out)[7], unsigned& n) noexcept {
  n = 0;
  for (int b = 0; b < 12 && n < 7u; ++b) {
    if ((mask >> b & 1u) != 0u) {
      out[n++] = b;
    }
  }
}
inline Scale make_scale(unsigned key, unsigned scale, unsigned root_note) noexcept {
  Scale s{};
  const int rn = root_note > 127u ? 127 : static_cast<int>(root_note);
  const int diff = (static_cast<int>(key % 12u) - rn % 12 + 12) % 12;  // up from the given note to the key's class
  s.root = diff == 0 ? rn : (diff < 6 ? rn + diff : rn - (12 - diff));
  const unsigned own = scale >= 1u && scale < kScaleTableCount ? scale : unsigned{chords_detail::kParent[0]};
  const unsigned par = scale < kScaleTableCount ? unsigned{chords_detail::kParent[scale]} : unsigned{chords_detail::kParent[0]};
  intervals_of(kScaleMask[own], s.iv, s.n);
  unsigned pn = 0;
  intervals_of(kScaleMask[par], s.piv, pn);
  return s;
}
constexpr int fold_range(int n) noexcept {
  while (n < 1) {
    n += 12;
  }
  while (n > 127) {
    n -= 12;
  }
  return n;
}
constexpr int floor_div(int a, int b) noexcept { return a >= 0 ? a / b : -((-a + b - 1) / b); }
// The note of degree d (negative or past n allowed): root + 12 * floor(d / n) + interval[d mod n], moved into 1..127 by octaves.
inline int degree_note(const Scale& s, int base, int d) noexcept {
  const int n = static_cast<int>(s.n);
  const int q = floor_div(d, n);
  return fold_range(base + 12 * q + s.iv[d - q * n]);
}
inline bool is_anchor(const Scale& s, int d) noexcept {
  const int n = static_cast<int>(s.n);
  const int iv = s.iv[d - floor_div(d, n) * n];
  return iv == 0 || iv == 3 || iv == 4 || iv == 7;
}
// The nearest anchor degree (a tie goes down).
inline int nearest_anchor(const Scale& s, int d) noexcept {
  for (int k = 0; k <= static_cast<int>(s.n); ++k) {
    if (is_anchor(s, d - k)) {
      return d - k;
    }
    if (is_anchor(s, d + k)) {
      return d + k;
    }
  }
  return d;
}
// The nearest degree that is the root (a multiple of n; a tie goes down).
inline int nearest_root(const Scale& s, int d) noexcept {
  const int n = static_cast<int>(s.n);
  const int r = floor_div(d, n) * n;
  return (d - r) * 2 > n ? r + n : r;
}

// What a step of the roll holds before it is written.
enum class Ev : std::uint8_t { Rest, Note, Chord, Tie };
struct Event {
  Ev kind = Ev::Rest;
  int note = 0;
  int degree = 0;  // LEAD
};

inline constexpr std::int8_t kLeadMoves[8] = {-2, -1, -1, -1, 1, 1, 1, 2};
// The rows of the CHORDS table, as degrees of the 7-note parent scale.
inline constexpr std::uint8_t kChordRows[8][4] = {{0, 5, 3, 4}, {0, 4, 5, 3}, {5, 3, 0, 4}, {0, 3, 0, 4}, {0, 2, 3, 4}, {0, 5, 1, 4}, {0, 6, 5, 4}, {0, 3, 5, 4}};

// The number of chord sections of CHORDS and LONG.
constexpr unsigned section_count(NoteStyle style, unsigned len, unsigned density, std::uint32_t seed) noexcept {
  len = clamp_len(len);
  density = clamp_density(density);
  if (style == NoteStyle::Long) {
    unsigned s = density <= 2u ? 1u : (density <= 5u ? 2u : 4u);
    s = s > len / 2u ? len / 2u : s;
    return s < 1u ? 1u : s;
  }
  if (len >= 16u) {
    return h(seed, 0, kSaltSections) % 4u == 3u ? 4u : 2u;
  }
  return len >= 8u ? 2u : 1u;
}

// The chord root of section k: root_note plus the parent scale's interval of the table's degree, folded into root - 5 .. root + 6, then into 1..127.
inline unsigned chord_root(const Scale& s, std::uint32_t seed, unsigned sections, unsigned k) noexcept {
  const unsigned degree = kChordRows[h(seed, 1, kSaltRow) % 8u][sections == 1u ? 0u : k % 4u];
  int r = s.root + s.piv[degree % 7u];
  while (r > s.root + 6) {
    r -= 12;
  }
  while (r < s.root - 5) {
    r += 12;
  }
  return static_cast<unsigned>(fold_range(r));
}

inline void roll_notes_impl(PatternData& p, const NoteArgs& a) noexcept {
  const unsigned len = clamp_len(a.len);
  const unsigned density = clamp_density(a.density);
  const Scale sc = make_scale(a.key, a.scale, a.root_note);
  const Phrase ph = phrase_of(len);
  Event ev[kPatternSteps];
  unsigned play = 0;

  if (a.style == NoteStyle::Long || a.style == NoteStyle::Chords) {
    const unsigned secs = section_count(a.style, len, density, a.seed);
    unsigned start[4] = {};
    for (unsigned k = 0; k < secs; ++k) {
      start[k] = k * len / secs;
    }
    if (a.style == NoteStyle::Chords) {
      play = play_mask(kWChords, a.seed, kSaltChords, len, density, true);
    }
    for (unsigned k = 0; k < secs; ++k) {
      play |= 1u << start[k];
    }
    unsigned k = 0;
    for (unsigned i = 0; i < len; ++i) {
      while (k + 1u < secs && i >= start[k + 1u]) {
        ++k;
      }
      const bool is_start = i == start[k];
      const bool plays = a.style == NoteStyle::Long ? is_start : (play >> i & 1u) != 0u;
      if (plays) {
        ev[i].kind = Ev::Chord;
        ev[i].note = static_cast<int>(chord_root(sc, a.seed, secs, k));
      } else if (a.style == NoteStyle::Long) {
        ev[i].kind = Ev::Tie;  // LONG holds from the section start to the next
      } else if (i > 0u && ev[i - 1u].kind == Ev::Chord && h(a.seed, source_of(ph, len, i), kSaltChordTie) % 2u == 0u) {
        ev[i].kind = Ev::Tie;
      }
    }
  } else {
    const bool bass = a.style == NoteStyle::Bass;
    play = bass ? play_mask(kWBass, a.seed, kSaltBass, len, density, true) : play_mask(kWLead, a.seed, kSaltLead, len, density, true);
    const int base = bass ? sc.root - 12 : sc.root;
    // The steps that are generated: A (or everything), then the tail after the repeat has been copied.
    const unsigned a_end = ph.shaped ? ph.half : len;
    for (unsigned pass = 0; pass < 2u; ++pass) {
      unsigned from = 0;
      unsigned to = a_end;
      if (pass == 1u) {
        if (!ph.shaped) {
          break;
        }
        for (unsigned i = ph.half; i < len - ph.q; ++i) {
          ev[i] = ev[i - ph.half];
        }
        from = len - ph.q;
        to = len;
      }
      for (unsigned i = from; i < to; ++i) {
        if ((play >> i & 1u) == 0u) {
          const bool tie = !bass && i > 0u && ev[i - 1u].kind == Ev::Note && h(a.seed, i, kSaltLeadTie) % 3u == 0u;
          ev[i].kind = tie ? Ev::Tie : Ev::Rest;
          continue;
        }
        ev[i].kind = Ev::Note;
        if (bass) {
          const unsigned r = h(a.seed, i, kSaltBassPitch) % 100u;
          int note = base;  // the root; step 0 keeps it
          if (i != 0u) {
            if (i % 4u == 0u) {
              note = r < 70u ? base : base + 7;
            } else if (r >= 85u) {
              note = base - 12 + sc.iv[sc.n - 1u];  // the scale note under the root
            } else if (r >= 70u) {
              note = base + 12;
            } else if (r >= 50u) {
              note = base + 7;
            }
          }
          ev[i].note = fold_range(note);
          continue;
        }
        int prev = 0;
        bool have = false;
        for (unsigned j = i; j-- > 0u;) {
          if (ev[j].kind == Ev::Note) {
            prev = ev[j].degree;
            have = true;
            break;
          }
        }
        int d = 0;
        if (!have) {
          int anchors[7];
          unsigned na = 0;
          for (unsigned k = 0; k < sc.n; ++k) {
            if (is_anchor(sc, static_cast<int>(k))) {
              anchors[na++] = static_cast<int>(k);
            }
          }
          d = anchors[h(a.seed, i, kSaltLeadStart) % na];
        } else {
          const int mv = kLeadMoves[h(a.seed, i, kSaltLeadMove) % 8u];
          d = prev + mv;
          if (d < -2 || d > static_cast<int>(sc.n) + 2) {
            d = prev - mv;
          }
          if (i % 4u == 0u) {
            d = nearest_anchor(sc, d);
          }
        }
        ev[i].degree = d;
        ev[i].note = degree_note(sc, base, d);
      }
    }
    if (!bass) {  // the last playing step lands on the nearest root
      for (unsigned i = len; i-- > 0u;) {
        if (ev[i].kind == Ev::Note) {
          ev[i].degree = nearest_root(sc, ev[i].degree);
          ev[i].note = degree_note(sc, base, ev[i].degree);
          break;
        }
      }
    }
  }

  for (unsigned i = 0; i < len; ++i) {
    switch (ev[i].kind) {
      case Ev::Rest:
        (void)set_rest(p, i);
        break;
      case Ev::Note:
        (void)set_note(p, i, static_cast<unsigned>(ev[i].note));
        break;
      case Ev::Chord:
        (void)set_chord(p, i, static_cast<unsigned>(ev[i].note), a.chord, a.voicing);
        break;
      case Ev::Tie:
        (void)set_tie(p, i);
        break;
    }
  }
}

}  // namespace detail

// The NOTE lane of steps 0..len-1: BASS (root and fifth, an octave under root_note), LEAD (a walk on the scale that lands on the root), CHORDS (a section
// chord on the playing steps, ties after them) or LONG (one chord a section, ties between). The step's other lanes stay.
inline void roll_notes(PatternData& p, const NoteArgs& a) noexcept { detail::roll_notes_impl(p, a); }
// The number of chord sections a CHORDS or LONG roll of these arguments has (the first step of each is a chord).
constexpr unsigned section_count(NoteStyle style, unsigned len, unsigned density, std::uint32_t seed) noexcept {
  return detail::section_count(style, len, density, seed);
}

// The SLICE lane. CHOP: the playing steps take slice 1 + h % slices (step 0 slice 1), the others none. ORDER: slice k + 1 on step floor(k * len / N), N
// slices in order, the seed unused. `slices` is the SPEAKS slot's slice count (above kSliceMax it counts as kSliceMax); 0: nothing is written, false.
inline bool roll_slices(PatternData& p, unsigned len, SliceStyle style, unsigned density, std::uint32_t seed, unsigned slices) noexcept {
  using namespace detail;
  if (slices == 0u) {
    return false;
  }
  slices = slices > kSliceMax ? kSliceMax : slices;
  len = clamp_len(len);
  density = clamp_density(density);
  if (style == SliceStyle::Chop) {
    const unsigned play = play_mask(kWLead, seed, kSaltChop, len, density, true);
    for (unsigned i = 0; i < len; ++i) {
      const bool on = (play >> i & 1u) != 0u;
      (void)set_slice(p, i, !on ? 0u : (i == 0u ? 1u : 1u + h(seed, i, kSaltSlice) % slices));
    }
    return true;
  }
  const unsigned cap = len < slices ? len : slices;
  unsigned n = (len * density + 4u) / 8u;
  n = n < 1u ? 1u : (n > cap ? cap : n);
  for (unsigned i = 0; i < len; ++i) {
    (void)set_slice(p, i, 0u);
  }
  for (unsigned k = 0; k < n; ++k) {
    (void)set_slice(p, k * len / n, k + 1u);
  }
  return true;
}

// The MOUTH lane. VOWELS: pattern::fill_mouth (density unused). WORDS: the playing steps take a word (mouth_word_byte), the others FOLLOW.
inline void roll_mouth(PatternData& p, unsigned len, MouthStyle style, unsigned density, std::uint32_t seed) noexcept {
  using namespace detail;
  len = clamp_len(len);
  if (style == MouthStyle::Vowels) {
    pattern::fill_mouth(p, len, seed);
    return;
  }
  const unsigned play = play_mask(kWWords, seed, kSaltWords, len, clamp_density(density), false);
  for (unsigned i = 0; i < len; ++i) {
    (void)set_mouth(p, i, (play >> i & 1u) != 0u ? mouth_word_byte(h(seed, i, kSaltWord) % kMouthWords) : std::uint8_t{0});
  }
}

// The LEVEL lane as ACCENT: the playing steps take `top` (1..127), the others the default level (0).
inline void roll_level(PatternData& p, unsigned len, unsigned density, std::uint32_t seed, unsigned top) noexcept {
  using namespace detail;
  len = clamp_len(len);
  top = top < 1u ? 1u : (top > 127u ? 127u : top);
  const unsigned play = play_mask(kWAccent, seed, kSaltAccent, len, clamp_density(density), false);
  for (unsigned i = 0; i < len; ++i) {
    (void)set_level(p, i, (play >> i & 1u) != 0u ? top : 0u);
  }
}

// The CARRIER lane as NOISE hits: N = round(len * density / 16) kept in 1..max(1, len / 2) steps (the NOISE weights, one segment) take NOISE, the others FOLLOW.
inline void roll_carrier(PatternData& p, unsigned len, unsigned density, std::uint32_t seed) noexcept {
  using namespace detail;
  len = clamp_len(len);
  density = clamp_density(density);
  const unsigned cap = len / 2u < 1u ? 1u : len / 2u;
  unsigned n = (len * density + 8u) / 16u;
  n = n < 1u ? 1u : (n > cap ? cap : n);
  const unsigned hits = rank_mask(kWNoise, seed, kSaltNoise, 0, len, n, false);
  for (unsigned i = 0; i < len; ++i) {
    (void)set_carrier(p, i, (hits >> i & 1u) != 0u ? route::kSoundsNoise : 0u);
  }
}

}  // namespace o3_voc03er::pattern_gen
