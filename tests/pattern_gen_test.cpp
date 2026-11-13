// SPDX-License-Identifier: GPL-3.0-or-later
// The pattern generator (app/src/pattern_gen.h): determinism, lane isolation, the density subset rule, the pitch rules per style, every length.

#include <doctest/doctest.h>

#include "../app/src/pattern_gen.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace pg = o3_voc03er::pattern_gen;
using namespace o3_voc03er;

namespace {

constexpr pg::NoteStyle kStyles[4] = {pg::NoteStyle::Bass, pg::NoteStyle::Lead, pg::NoteStyle::Chords, pg::NoteStyle::Long};
constexpr std::uint32_t kSeeds[5] = {1u, 2u, 0x1234567u, 0xDEADBEEFu, 99u};
constexpr unsigned kLens[8] = {16, 15, 12, 10, 8, 7, 4, 1};
constexpr unsigned kRoots[3] = {12, 60, 115};

pg::NoteArgs args(unsigned len, pg::NoteStyle style, unsigned density, std::uint32_t seed, unsigned key = 0, unsigned scale = 1, unsigned root = 60) {
  return pg::NoteArgs{len, style, density, seed, key, scale, root, ChordKind::Triad, Voicing::Close};
}

// A pattern whose every lane holds a recognisable value (and whose steps past 8 are distinct), so a function that touches a lane it does not own shows.
PatternData marked() {
  PatternData p{};
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    Step s;
    s.note = static_cast<std::uint8_t>(20 + i);
    s.carrier = static_cast<std::uint8_t>(1 + i % 7);
    s.level = static_cast<std::uint8_t>(10 + i);
    s.slice = static_cast<std::uint8_t>(1 + i % 24);
    REQUIRE(set_step(p, i, s));
    REQUIRE(set_mouth(p, i, 1 + i % 9));
  }
  return p;
}

bool same(const PatternData& a, const PatternData& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

enum Lane { kNote, kCarrier, kLevel, kSlice, kMouth };
// Does the lane of every step equal in a and b? (NOTE: note, kind, voicing.)
bool lane_same(const PatternData& a, const PatternData& b, Lane lane, unsigned from, unsigned to) {
  for (unsigned i = from; i < to; ++i) {
    const Step x = get_step(a, i);
    const Step y = get_step(b, i);
    bool eq = true;
    switch (lane) {
      case kNote: eq = x.note == y.note && x.kind == y.kind && x.voicing == y.voicing; break;
      case kCarrier: eq = x.carrier == y.carrier; break;
      case kLevel: eq = x.level == y.level; break;
      case kSlice: eq = x.slice == y.slice; break;
      case kMouth: eq = get_mouth(a, i) == get_mouth(b, i); break;
    }
    if (!eq) {
      return false;
    }
  }
  return true;
}
// Every lane but `own` of steps 0..len-1 and every lane of the steps from len on are as they were.
bool others_kept(const PatternData& before, const PatternData& after, Lane own, unsigned len) {
  for (unsigned l = kNote; l <= kMouth; ++l) {
    if (l != own && !lane_same(before, after, static_cast<Lane>(l), 0, kPatternSteps)) {
      return false;
    }
  }
  return lane_same(before, after, own, len, kPatternSteps);
}

unsigned scale_mask(unsigned scale) { return kScaleMask[scale >= 1u && scale < kScaleTableCount ? scale : chords_detail::kParent[0]]; }
unsigned parent_mask(unsigned scale) { return kScaleMask[scale < kScaleTableCount ? chords_detail::kParent[scale] : chords_detail::kParent[0]]; }
bool in_mask(unsigned mask, unsigned note, unsigned key) { return (mask >> ((note + 12u - key % 12u) % 12u) & 1u) != 0u; }

bool plays(const Step& s) { return s.note != 0u; }
unsigned play_set(const PatternData& p, unsigned len) {
  unsigned m = 0;
  for (unsigned i = 0; i < len; ++i) {
    m |= (plays(get_step(p, i)) ? 1u : 0u) << i;
  }
  return m;
}

}  // namespace

TEST_CASE("pattern_gen: the names and the density range") {
  CHECK(std::strcmp(pg::kNoteStyleNames[static_cast<unsigned>(pg::NoteStyle::Chords)], "CHORDS") == 0);
  CHECK(std::strcmp(pg::kSliceStyleNames[1], "ORDER") == 0);
  CHECK(std::strcmp(pg::kMouthStyleNames[1], "WORDS") == 0);
  CHECK(pg::kDensityMin == 1u);
  CHECK(pg::kDensityMax == 8u);
  CHECK(pg::kDensityDefault == 4u);
}

TEST_CASE("pattern_gen: the same arguments give the same pattern, another seed another pattern") {
  for (const pg::NoteStyle st : kStyles) {
    PatternData a = marked(), b = marked(), c = marked();
    pg::roll_notes(a, args(16, st, 5, 7u));
    pg::roll_notes(b, args(16, st, 5, 7u));
    CHECK(same(a, b));
    if (st != pg::NoteStyle::Long) {  // LONG with these arguments has the same sections for many seeds; check it over several
      pg::roll_notes(c, args(16, st, 5, 8u));
      CHECK_FALSE(same(a, c));
    }
  }
  bool differs = false;
  for (std::uint32_t s = 1; s < 20u && !differs; ++s) {
    PatternData a = marked(), b = marked();
    pg::roll_notes(a, args(16, pg::NoteStyle::Long, 5, 1u));
    pg::roll_notes(b, args(16, pg::NoteStyle::Long, 5, s + 1u));
    differs = !same(a, b);
  }
  CHECK(differs);
  {
    PatternData a = marked(), b = marked(), c = marked();
    pg::roll_slices(a, 16, pg::SliceStyle::Chop, 4, 3u, 8);
    pg::roll_slices(b, 16, pg::SliceStyle::Chop, 4, 3u, 8);
    pg::roll_slices(c, 16, pg::SliceStyle::Chop, 4, 4u, 8);
    CHECK(same(a, b));
    CHECK_FALSE(same(a, c));
  }
  for (const pg::MouthStyle ms : {pg::MouthStyle::Vowels, pg::MouthStyle::Words}) {
    PatternData a = marked(), b = marked(), c = marked();
    pg::roll_mouth(a, 16, ms, 4, 3u);
    pg::roll_mouth(b, 16, ms, 4, 3u);
    pg::roll_mouth(c, 16, ms, 4, 4u);
    CHECK(same(a, b));
    CHECK_FALSE(same(a, c));
  }
  {
    PatternData a = marked(), b = marked(), c = marked();
    pg::roll_level(a, 16, 4, 3u, 127);
    pg::roll_level(b, 16, 4, 3u, 127);
    pg::roll_level(c, 16, 4, 4u, 127);
    CHECK(same(a, b));
    CHECK_FALSE(same(a, c));
  }
  {
    PatternData a = marked(), b = marked();
    pg::roll_carrier(a, 16, 4, 3u);
    pg::roll_carrier(b, 16, 4, 3u);
    CHECK(same(a, b));
    bool noise_differs = false;  // the NOISE weights leave little room at density 4: look at 6 over several seeds
    for (std::uint32_t s = 4; s < 40u && !noise_differs; ++s) {
      PatternData x = marked(), y = marked();
      pg::roll_carrier(x, 16, 6, 3u);
      pg::roll_carrier(y, 16, 6, s);
      noise_differs = !same(x, y);
    }
    CHECK(noise_differs);
  }
  {  // ORDER does not read the seed
    PatternData a = marked(), b = marked();
    pg::roll_slices(a, 16, pg::SliceStyle::Order, 4, 3u, 8);
    pg::roll_slices(b, 16, pg::SliceStyle::Order, 4, 4u, 8);
    CHECK(same(a, b));
  }
}

TEST_CASE("pattern_gen: a function writes only its own lane of steps 0..len-1") {
  for (const unsigned len : kLens) {
    for (const std::uint32_t seed : kSeeds) {
      for (const pg::NoteStyle st : kStyles) {
        const PatternData before = marked();
        PatternData p = before;
        pg::roll_notes(p, args(len, st, 6, seed));
        CHECK(others_kept(before, p, kNote, len));
      }
      for (const pg::SliceStyle st : {pg::SliceStyle::Chop, pg::SliceStyle::Order}) {
        const PatternData before = marked();
        PatternData p = before;
        CHECK(pg::roll_slices(p, len, st, 6, seed, 8));
        CHECK(others_kept(before, p, kSlice, len));
      }
      for (const pg::MouthStyle st : {pg::MouthStyle::Vowels, pg::MouthStyle::Words}) {
        const PatternData before = marked();
        PatternData p = before;
        pg::roll_mouth(p, len, st, 6, seed);
        CHECK(others_kept(before, p, kMouth, len));
      }
      {
        const PatternData before = marked();
        PatternData p = before;
        pg::roll_level(p, len, 6, seed, 110);
        CHECK(others_kept(before, p, kLevel, len));
        p = before;
        pg::roll_carrier(p, len, 6, seed);
        CHECK(others_kept(before, p, kCarrier, len));
      }
    }
  }
  // The pattern's LENGTH is not touched either.
  PatternData p = marked();
  REQUIRE(set_length(p, 11));
  pg::roll_notes(p, args(16, pg::NoteStyle::Lead, 4, 5u));
  CHECK(length_of(p) == 11u);
}

TEST_CASE("pattern_gen: a higher density only adds steps") {
  for (const std::uint32_t seed : kSeeds) {
    for (const unsigned len : kLens) {
      auto check_style = [&](auto roll, auto read) {
        unsigned last = 0;
        for (unsigned d = 1; d <= 8; ++d) {
          PatternData p{};
          roll(p, d);
          const unsigned m = read(p);
          CHECK((last & ~m) == 0u);  // the set at d - 1 is inside the set at d
          CHECK(m != 0u);
          last = m;
        }
      };
      for (const pg::NoteStyle st : {pg::NoteStyle::Bass, pg::NoteStyle::Lead, pg::NoteStyle::Chords}) {
        check_style([&](PatternData& p, unsigned d) { pg::roll_notes(p, args(len, st, d, seed)); }, [&](const PatternData& p) { return play_set(p, len); });
      }
      check_style([&](PatternData& p, unsigned d) { (void)pg::roll_slices(p, len, pg::SliceStyle::Chop, d, seed, 8); },
                  [&](const PatternData& p) {
                    unsigned m = 0;
                    for (unsigned i = 0; i < len; ++i) m |= (get_step(p, i).slice != 0u ? 1u : 0u) << i;
                    return m;
                  });
      check_style([&](PatternData& p, unsigned d) { pg::roll_mouth(p, len, pg::MouthStyle::Words, d, seed); },
                  [&](const PatternData& p) {
                    unsigned m = 0;
                    for (unsigned i = 0; i < len; ++i) m |= (get_mouth(p, i) != 0u ? 1u : 0u) << i;
                    return m;
                  });
      check_style([&](PatternData& p, unsigned d) { pg::roll_level(p, len, d, seed, 120); },
                  [&](const PatternData& p) {
                    unsigned m = 0;
                    for (unsigned i = 0; i < len; ++i) m |= (get_step(p, i).level != 0u ? 1u : 0u) << i;
                    return m;
                  });
      check_style([&](PatternData& p, unsigned d) { pg::roll_carrier(p, len, d, seed); },
                  [&](const PatternData& p) {
                    unsigned m = 0;
                    for (unsigned i = 0; i < len; ++i) m |= (get_step(p, i).carrier != 0u ? 1u : 0u) << i;
                    return m;
                  });
    }
  }
}

TEST_CASE("pattern_gen: the density sets the count of playing steps") {
  for (const std::uint32_t seed : kSeeds) {
    PatternData p{};
    pg::roll_notes(p, args(16, pg::NoteStyle::Bass, 8, seed));
    CHECK(pg::detail::popcount(play_set(p, 16)) == 16u);  // A: 8 of 8, the repeat, the tail: 4 of 4
    pg::roll_notes(p, args(16, pg::NoteStyle::Bass, 4, seed));
    const unsigned half = pg::detail::popcount(play_set(p, 8));
    CHECK(half == 4u);  // A plays round(8 * 4 / 8) of its 8
    pg::roll_notes(p, args(16, pg::NoteStyle::Bass, 1, seed));
    CHECK(pg::detail::popcount(play_set(p, 8)) == 1u);
    pg::roll_notes(p, args(16, pg::NoteStyle::Bass, 0, seed));  // clamped to 1
    CHECK(pg::detail::popcount(play_set(p, 8)) == 1u);
    pg::roll_notes(p, args(16, pg::NoteStyle::Bass, 99, seed));  // clamped to 8
    CHECK(pg::detail::popcount(play_set(p, 16)) == 16u);
  }
}

TEST_CASE("pattern_gen: the phrase repeats A and the BASS pitch repeats with it") {
  for (const std::uint32_t seed : kSeeds) {
    PatternData p{};
    pg::roll_notes(p, args(16, pg::NoteStyle::Bass, 4, seed));
    for (unsigned j = 0; j < 4; ++j) {
      CHECK(get_step(p, 8 + j).note == get_step(p, j).note);
    }
    // the tail plays as many steps as A does in its own last 4
    unsigned in_a = 0, in_tail = 0;
    for (unsigned j = 4; j < 8; ++j) in_a += plays(get_step(p, j)) ? 1u : 0u;
    for (unsigned j = 12; j < 16; ++j) in_tail += plays(get_step(p, j)) ? 1u : 0u;
    CHECK(in_tail == in_a);
  }
}

TEST_CASE("pattern_gen: every note is in the scale, every chord root in its parent, everything in 1..127") {
  for (unsigned scale = 0; scale <= 8; ++scale) {  // 7 and 8 are past the table: CHROM's parent
    for (unsigned key = 0; key < 12; ++key) {
      for (const unsigned root : kRoots) {
        for (const std::uint32_t seed : kSeeds) {
          for (const pg::NoteStyle st : kStyles) {
            for (const unsigned len : {16u, 10u, 5u}) {
              PatternData p{};
              pg::roll_notes(p, args(len, st, 1 + seed % 8u, seed, key, scale, root));
              for (unsigned i = 0; i < len; ++i) {
                const Step s = get_step(p, i);
                REQUIRE(step_valid(s));
                if (s.note == 0u) {
                  continue;
                }
                CHECK(s.note >= 1u);
                CHECK(s.note <= 127u);
                if (s.kind == 0u) {
                  CHECK(in_mask(scale_mask(scale), s.note, key));
                } else {
                  CHECK(in_mask(parent_mask(scale), s.note, key));
                }
              }
            }
          }
        }
      }
    }
  }
}

TEST_CASE("pattern_gen: step 0 plays in every NOTE style, and every length works") {
  for (unsigned len = 1; len <= 16; ++len) {
    for (const std::uint32_t seed : kSeeds) {
      for (unsigned d = 1; d <= 8; ++d) {
        for (const pg::NoteStyle st : kStyles) {
          PatternData p{};
          pg::roll_notes(p, args(len, st, d, seed));
          CHECK(plays(get_step(p, 0)));
          for (unsigned i = len; i < kPatternSteps; ++i) {
            CHECK(get_step(p, i).note == 0u);  // nothing past len
          }
        }
        PatternData p{};
        pg::roll_mouth(p, len, pg::MouthStyle::Words, d, seed);
        pg::roll_level(p, len, d, seed, 100);
        pg::roll_carrier(p, len, d, seed);
        CHECK(pg::roll_slices(p, len, pg::SliceStyle::Chop, d, seed, 4));
        CHECK(pg::roll_slices(p, len, pg::SliceStyle::Order, d, seed, 4));
        CHECK(get_step(p, 0).slice == 1u);  // ORDER's first slice, after CHOP's step 0
      }
    }
  }
}

TEST_CASE("pattern_gen: LEAD ends on the root, BASS beats are the root or the fifth") {
  for (unsigned scale = 0; scale <= 6; ++scale) {
    for (unsigned key = 0; key < 12; ++key) {
      for (const std::uint32_t seed : kSeeds) {
        for (const unsigned len : kLens) {
          for (unsigned d = 1; d <= 8; d += 3) {
            PatternData p{};
            pg::roll_notes(p, args(len, pg::NoteStyle::Lead, d, seed, key, scale, 60 + key));
            unsigned last = len;
            for (unsigned i = 0; i < len; ++i) {
              if (get_step(p, i).kind == 0u && get_step(p, i).note != 0u) last = i;
            }
            REQUIRE(last < len);
            CHECK(get_step(p, last).note % 12u == key);

            PatternData b{};
            pg::roll_notes(b, args(len, pg::NoteStyle::Bass, d, seed, key, scale, 60 + key));
            const bool shaped = len % 2u == 0u && len >= 8u;  // a copied step keeps the pitch rule of its source, not of its own place
            for (unsigned i = 0; i < len; i += 4) {
              if (shaped && i >= len / 2u && i < len - (len / 4u > 1u ? len / 4u : 1u)) {
                continue;
              }
              const Step s = get_step(b, i);
              if (s.note != 0u) {
                const unsigned rel = (s.note + 12u - key) % 12u;
                CHECK((rel == 0u || rel == 7u));
              }
            }
            CHECK(get_step(b, 0).note % 12u == key);  // step 0 is the root
          }
        }
      }
    }
  }
}

TEST_CASE("pattern_gen: BASS sits an octave under root_note, LEAD at it") {
  PatternData p{};
  pg::roll_notes(p, args(16, pg::NoteStyle::Bass, 8, 5u, 0, 1, 60));
  CHECK(get_step(p, 0).note == 48u);
  pg::roll_notes(p, args(16, pg::NoteStyle::Lead, 8, 5u, 0, 1, 60));
  CHECK(get_step(p, 0).note >= 60u);
  CHECK(get_step(p, 0).note <= 67u);  // root, third or fifth
  // a root_note whose class is not the key: the nearest note of the key's class is the root
  pg::roll_notes(p, args(16, pg::NoteStyle::Bass, 8, 5u, 7, 1, 60));
  CHECK(get_step(p, 0).note == 43u);  // G: 7 over 60 is farther than 5 under it, so the root is 55 and the bass 43
  pg::roll_notes(p, args(16, pg::NoteStyle::Bass, 8, 5u, 5, 1, 60));
  CHECK(get_step(p, 0).note == 53u);  // F: 5 over 60 is nearer than 7 under it, so the root is 65 and the bass an octave under
}

TEST_CASE("pattern_gen: CHORDS and LONG, the section starts and the ties") {
  for (const std::uint32_t seed : kSeeds) {
    for (const unsigned len : kLens) {
      for (unsigned d = 1; d <= 8; ++d) {
        for (const pg::NoteStyle st : {pg::NoteStyle::Chords, pg::NoteStyle::Long}) {
          PatternData p{};
          pg::roll_notes(p, args(len, st, d, seed, 2, 1, 62));
          const unsigned secs = pg::section_count(st, len, d, seed);
          REQUIRE(secs >= 1u);
          REQUIRE(secs <= 4u);
          unsigned chords = 0;
          for (unsigned k = 0; k < secs; ++k) {
            const Step s = get_step(p, k * len / secs);
            CHECK(s.note != 0u);
            CHECK(s.kind == static_cast<unsigned>(ChordKind::Triad) + 1u);
          }
          for (unsigned i = 0; i < len; ++i) {
            const Step s = get_step(p, i);
            const Step prev = i > 0u ? get_step(p, i - 1u) : Step{};
            if (s.note != 0u) {
              ++chords;
              CHECK(s.kind >= 1u);
              CHECK(s.kind <= 4u);
            }
            if (is_tie(s)) {
              CHECK((prev.note != 0u || is_tie(prev)));  // never after a rest
            }
            if (st == pg::NoteStyle::Chords && is_tie(s)) {
              CHECK(prev.note != 0u);  // a tie follows a chord directly
            }
            if (st == pg::NoteStyle::Long) {
              bool start = false;
              for (unsigned k = 0; k < secs; ++k) start = start || i == k * len / secs;
              CHECK((start ? s.note != 0u : is_tie(s)));
            }
          }
          CHECK(chords >= secs);
          if (st == pg::NoteStyle::Long) {
            CHECK(chords == secs);
          }
        }
      }
    }
  }
  // the section counts
  CHECK(pg::section_count(pg::NoteStyle::Long, 16, 1, 1u) == 1u);
  CHECK(pg::section_count(pg::NoteStyle::Long, 16, 2, 1u) == 1u);
  CHECK(pg::section_count(pg::NoteStyle::Long, 16, 3, 1u) == 2u);
  CHECK(pg::section_count(pg::NoteStyle::Long, 16, 5, 1u) == 2u);
  CHECK(pg::section_count(pg::NoteStyle::Long, 16, 6, 1u) == 4u);
  CHECK(pg::section_count(pg::NoteStyle::Long, 16, 8, 1u) == 4u);
  CHECK(pg::section_count(pg::NoteStyle::Long, 2, 8, 1u) == 1u);
  CHECK(pg::section_count(pg::NoteStyle::Long, 1, 8, 1u) == 1u);
  CHECK(pg::section_count(pg::NoteStyle::Chords, 12, 4, 1u) == 2u);
  CHECK(pg::section_count(pg::NoteStyle::Chords, 7, 4, 1u) == 1u);
  bool two = false, four = false;
  for (std::uint32_t s = 1; s < 64u; ++s) {
    const unsigned n = pg::section_count(pg::NoteStyle::Chords, 16, 4, s);
    two = two || n == 2u;
    four = four || n == 4u;
    CHECK((n == 2u || n == 4u));
  }
  CHECK(two);
  CHECK(four);
}

TEST_CASE("pattern_gen: a chord keeps its kind and voicing") {
  PatternData p{};
  pg::NoteArgs a = args(16, pg::NoteStyle::Chords, 8, 3u);
  a.chord = ChordKind::Power;
  a.voicing = Voicing::Wide;
  pg::roll_notes(p, a);
  for (unsigned i = 0; i < 16; ++i) {
    const Step s = get_step(p, i);
    if (s.note != 0u) {
      CHECK(s.kind == static_cast<unsigned>(ChordKind::Power) + 1u);
      CHECK(s.voicing == static_cast<unsigned>(Voicing::Wide));
    }
  }
}

TEST_CASE("pattern_gen: a roll over a roll leaves no tie or chord behind") {
  PatternData p{};
  pg::roll_notes(p, args(16, pg::NoteStyle::Long, 8, 3u));
  pg::roll_notes(p, args(16, pg::NoteStyle::Bass, 8, 3u));
  for (unsigned i = 0; i < 16; ++i) {
    const Step s = get_step(p, i);
    CHECK(s.kind == 0u);
    CHECK(step_valid(s));
  }
}

TEST_CASE("pattern_gen: the slice lane") {
  PatternData p{};
  CHECK_FALSE(pg::roll_slices(p, 16, pg::SliceStyle::Chop, 4, 1u, 0));
  CHECK_FALSE(pg::roll_slices(p, 16, pg::SliceStyle::Order, 4, 1u, 0));
  PatternData z{};
  CHECK(same(p, z));  // nothing written
  for (const std::uint32_t seed : kSeeds) {
    for (const unsigned slices : {1u, 3u, 8u, 24u, 40u}) {
      for (unsigned d = 1; d <= 8; ++d) {
        PatternData c{};
        CHECK(pg::roll_slices(c, 16, pg::SliceStyle::Chop, d, seed, slices));
        CHECK(get_step(c, 0).slice == 1u);
        const unsigned cap = slices > kSliceMax ? kSliceMax : slices;
        for (unsigned i = 0; i < 16; ++i) {
          CHECK(get_step(c, i).slice <= cap);
        }
      }
    }
  }
  // ORDER: slice k + 1 on step floor(k * len / N)
  PatternData o{};
  CHECK(pg::roll_slices(o, 16, pg::SliceStyle::Order, 8, 1u, 4));  // N = min(16, 4)
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(get_step(o, i).slice == (i % 4u == 0u ? i / 4u + 1u : 0u));
  }
  PatternData o2{};
  CHECK(pg::roll_slices(o2, 16, pg::SliceStyle::Order, 4, 1u, 24));  // N = 8: every other step
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(get_step(o2, i).slice == (i % 2u == 0u ? i / 2u + 1u : 0u));
  }
  PatternData o3{};
  CHECK(pg::roll_slices(o3, 10, pg::SliceStyle::Order, 1, 1u, 24));  // N = 1
  CHECK(get_step(o3, 0).slice == 1u);
  for (unsigned i = 1; i < 10; ++i) CHECK(get_step(o3, i).slice == 0u);
}

TEST_CASE("pattern_gen: the mouth, level and carrier lanes") {
  for (const std::uint32_t seed : kSeeds) {
    for (unsigned d = 1; d <= 8; ++d) {
      PatternData p{};
      pg::roll_mouth(p, 16, pg::MouthStyle::Words, d, seed);
      for (unsigned i = 0; i < 16; ++i) {
        const std::uint8_t m = get_mouth(p, i);
        CHECK(mouth_valid(m));
        CHECK((m == 0u || mouth_is_word(m)));
      }
      pg::roll_mouth(p, 16, pg::MouthStyle::Vowels, d, seed);
      for (unsigned i = 0; i < 16; ++i) {
        CHECK(mouth_valid(get_mouth(p, i)));
        CHECK_FALSE(mouth_is_word(get_mouth(p, i)));
      }
      PatternData l{};
      pg::roll_level(l, 16, d, seed, 127);
      unsigned n = 0;
      for (unsigned i = 0; i < 16; ++i) {
        const Step s = get_step(l, i);
        CHECK(step_valid(s));
        CHECK((s.level == 0u || s.level == 127u));
        n += s.level != 0u ? 1u : 0u;
      }
      CHECK(n >= 1u);
      pg::roll_level(l, 16, d, seed, 0);  // top clamps to 1
      pg::roll_level(l, 16, d, seed, 500);  // and to 127
      for (unsigned i = 0; i < 16; ++i) CHECK((get_step(l, i).level == 0u || get_step(l, i).level == 127u));
      PatternData c{};
      pg::roll_carrier(c, 16, d, seed);
      unsigned hits = 0;
      for (unsigned i = 0; i < 16; ++i) {
        const unsigned car = get_step(c, i).carrier;
        CHECK((car == 0u || car == 2u));
        hits += car == 2u ? 1u : 0u;
      }
      CHECK(hits >= 1u);
      CHECK(hits <= 8u);
      CHECK(hits == (16u * d + 8u) / 16u);  // round(len * d / 16), under the cap of len / 2
    }
  }
  PatternData one{};
  pg::roll_carrier(one, 1, 8, 1u);
  CHECK(get_step(one, 0).carrier == 2u);  // at least one hit, even at len 1
}
