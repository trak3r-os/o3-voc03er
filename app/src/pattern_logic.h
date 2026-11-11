// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pure part of the PATTERN screen (docs/UI-DESIGN.md "PATTERN"): which pad is what, the LEVEL palette, the palette's value for a pad, the text of a
// step cell. No engine, no screen: the host test (tests/pattern_screen_test.cpp) calls these directly.

#include <o3_voc03er/stepseq.h>

#include "pads_logic.h"
#include "perform_logic.h"

#include <o3sdk/app/panel.h>

#include <cstdint>

namespace o3_voc03er::pattern {

enum class Lane : std::uint8_t { Note, Carrier, Level, Slice, Mouth };
inline constexpr unsigned kLanes = 5;
inline constexpr const char* kLaneNames[kLanes] = {"NOTE", "CARRIER", "LEVEL", "SLICE", "MOUTH"};
// CLEAR LANE's question (SHIFT + F6), one line per lane (the strip shows it until a key answers).
inline constexpr const char* kClearPrompt[kLanes] = {"CLEAR NOTE LANE: F1 CONFIRMS", "CLEAR CARRIER LANE: F1 CONFIRMS", "CLEAR LEVEL LANE: F1 CONFIRMS",
                                                     "CLEAR SLICE LANE: F1 CONFIRMS", "CLEAR MOUTH LANE: F1 CONFIRMS"};

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

// The NOTE palette is the layout's: palette row 0 is row A, row 1 is row B, so a pad plays what pads::pad_play says of it.
inline constexpr pads::Row kPaletteRow[2] = {pads::Row::A, pads::Row::B};
constexpr pads::PadPlay palette_play(unsigned pad, int octave, const pads::Layout& layout) noexcept {
  return pads::pad_play(layout, kPaletteRow[o3sdk::app::pad_row(pad) % 2u], o3sdk::app::pad_col(pad), octave);
}

// The MOUTH palette has two pages (F6 SET flips them, TRACT V3b). VOWELS (V3): row 0 is PERFORM's mouth strip (columns 0..8 the vowels A..U = values
// 1..9, columns 9..11 the closures LIPS, TIP, BACK = 10..12); row 1 columns 0..8 are the same vowels with GLD (the slide into it), columns 9..11 are dark.
// WORDS: row 0 columns 0..7 are the 8 words (WOW YEAH YOI WAH OW BA DA GA), row 1 columns 0..5 the 6 WOB rates (1/4 1/8 1/16 1/32 1/8T 1/16T); the rest is
// dark. The mouth byte a pad writes; 0: a dark pad.
enum class MouthPage : std::uint8_t { Vowels, Words };
constexpr std::uint8_t mouth_of_pad(unsigned pad, MouthPage page = MouthPage::Vowels) noexcept {
  const unsigned col = o3sdk::app::pad_col(pad);
  const unsigned row = o3sdk::app::pad_row(pad);
  if (pad >= kPalettePads || col >= 12u) {
    return 0;
  }
  if (page == MouthPage::Words) {
    if (row == 0u) {
      return col < kMouthWords ? mouth_word_byte(col) : std::uint8_t{0};
    }
    return col < kMouthWobRates ? mouth_wob_byte(col) : std::uint8_t{0};
  }
  if (row == 0u) {
    return static_cast<std::uint8_t>(col + 1u);
  }
  return col < kMouthVowels ? static_cast<std::uint8_t>((col + 1u) | kMouthGld) : std::uint8_t{0};
}

// Does the palette pad hold what the step stores? (LED mid on it.) On the NOTE lane the step's note and kind equal the pad's play (a rest, and a pad
// that plays nothing, never match); the other lanes do not depend on the layout. `mouth` is the step's mouth byte (the MOUTH lane's).
constexpr bool palette_matches(Lane lane, unsigned pad, const Step& s, int octave, const pads::Layout& layout, std::uint8_t mouth = 0,
                               MouthPage page = MouthPage::Vowels) noexcept {
  switch (lane) {
    case Lane::Note: {
      const pads::PadPlay p = palette_play(pad, octave, layout);
      return s.note != 0u && p.note > 0 && s.note == static_cast<unsigned>(p.note) && s.kind == p.kind;
    }
    case Lane::Carrier:
      return pad < kCarrierPads && s.carrier == pad;
    case Lane::Level:
      return level_of_pad(pad) == step_velocity(s);
    case Lane::Slice:
      return pad < kSliceMax && pad + 1u == s.slice;
    case Lane::Mouth:
      return mouth != 0u && mouth_of_pad(pad, page) == mouth;
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

// A palette pad of the MOUTH lane writes `m`; a dark pad writes nothing (false).
inline bool mouth_write(PatternData& p, unsigned i, unsigned pad, MouthPage page = MouthPage::Vowels) noexcept {
  const std::uint8_t m = mouth_of_pad(pad, page);
  return m != 0u && set_mouth(p, i, m);
}

// The next step of the cursor after a palette write: one on, wrapping at LENGTH.
constexpr unsigned next_cursor(unsigned cursor, unsigned length) noexcept { return length == 0u ? 0u : (cursor + 1u) % length; }

inline constexpr const char* kCellCarrier[kCarrierPads] = {"FLW", "SYN", "NSE", "S1", "S2", "S3", "S4"};
inline constexpr const char* kPadCarrier[kCarrierPads] = {"FOLLW", "SYNTH", "NOISE", "SLOT1", "SLOT2", "SLOT3", "SLOT4"};
// The MOUTH lane's cell: the vowel's letters (PERFORM's labels), "LIP", "TIP", "BCK" for the closures, a ">" before a vowel that slides in (GLD), the word's
// three letters ("WOW" "YEA" "YOI" "WAH" "OW" "BA" "DA" "GA") or the WOB's rate ("~4" "~8" "~16" "~32" "~8T" "~16T"); empty for FOLLOW. Up to 4 characters
// and the terminator.
inline constexpr const char* kCellWord[kMouthWords] = {"WOW", "YEA", "YOI", "WAH", "OW", "BA", "DA", "GA"};
inline constexpr const char* kCellWob[kMouthWobRates] = {"~4", "~8", "~16", "~32", "~8T", "~16T"};
inline void mouth_text(char (&out)[6], std::uint8_t m) noexcept {
  static constexpr const char* kClosure[3] = {"LIP", "TIP", "BCK"};
  out[0] = '\0';
  const bool word = mouth_is_word(m) && mouth_arg(m) < kMouthWords;
  const bool wob = mouth_is_wob(m) && mouth_arg(m) < kMouthWobRates;
  if (!mouth_is_vowel(m) && !mouth_is_closure(m) && !word && !wob) {
    return;
  }
  unsigned n = 0;
  if (mouth_is_vowel(m) && mouth_gld(m)) {
    out[n++] = '>';
  }
  const char* t = word ? kCellWord[mouth_arg(m)] : (wob ? kCellWob[mouth_arg(m)] : (mouth_is_vowel(m) ? perform::kMouthLabel[mouth_value(m) - 1u] : kClosure[mouth_close(m) - 1u]));
  while (*t != '\0' && n < 4u) {
    out[n++] = *t++;
  }
  out[n] = '\0';
}
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

// The MOUTH lane's VALUE walk: FOLLOW, A..U, LIP TIP BCK, 8 words, 6 WOBs (27 values, the last is 26).
inline constexpr int kMouthWalkMax = static_cast<int>(kMouthWord + kMouthWords + kMouthWobRates) - 1;

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
    case Lane::Mouth: {  // the 27 values: 0 FOLLOW, 1..9 the vowels, 10..12 the closures, 13..20 the words, 21..26 the WOB rates; a vowel keeps its GLD
      const std::uint8_t m = get_mouth(p, i);
      const int at = mouth_is_word(m) ? static_cast<int>(kMouthWord + mouth_arg(m)) : (mouth_is_wob(m) ? static_cast<int>(kMouthWord + kMouthWords + mouth_arg(m)) : static_cast<int>(mouth_value(m)));
      int v = at + delta;
      v = v < 0 ? 0 : (v > kMouthWalkMax ? kMouthWalkMax : v);
      if (v == at) {
        return false;
      }
      const unsigned u = static_cast<unsigned>(v);
      if (u >= kMouthWord + kMouthWords) {
        return set_mouth(p, i, mouth_wob_byte(u - kMouthWord - kMouthWords));
      }
      if (u >= kMouthWord) {
        return set_mouth(p, i, mouth_word_byte(u - kMouthWord));
      }
      const bool vowel = u >= 1u && u <= kMouthVowels;
      return set_mouth(p, i, u | (vowel && mouth_gld(m) ? kMouthGld : 0u));
    }
  }
  return false;
}

// ---- FILL on the MOUTH lane (F7): a seeded random walk over the vowels ----------------------------------------------------------------
constexpr std::uint32_t xorshift32(std::uint32_t x) noexcept {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}
// The seed of the n-th FILL of pattern `pattern`: the screen's counter plus the pattern index, spread so that neighbouring counters differ in every bit
// (xorshift32 must not start at 0).
constexpr std::uint32_t fill_seed(std::uint32_t counter, unsigned pattern) noexcept {
  const std::uint32_t s = (counter + pattern + 1u) * 2654435761u;
  return s != 0u ? s : 0x9E3779B9u;
}
// Steps 0..len-1 of the lane: start on a random vowel, then about 1 step in 4 stays FOLLOW and the others move -2..+2 along A..U (clamped), about 1 in 6
// of those slides (GLD). No closures, words or WOBs. Steps from len on are left alone. The same seed gives the same line.
inline void fill_mouth(PatternData& p, unsigned len, std::uint32_t seed) noexcept {
  std::uint32_t r = seed != 0u ? seed : 1u;
  r = xorshift32(r);
  int v = static_cast<int>((r >> 8) % kMouthVowels);  // 0..8: A..U
  for (unsigned i = 0; i < len && i < kPatternSteps; ++i) {
    r = xorshift32(r);
    if ((r >> 4) % 4u == 0u) {
      (void)set_mouth(p, i, 0u);
      continue;
    }
    v += static_cast<int>((r >> 8) % 5u) - 2;
    v = v < 0 ? 0 : (v > static_cast<int>(kMouthVowels) - 1 ? static_cast<int>(kMouthVowels) - 1 : v);
    const bool slide = (r >> 16) % 6u == 0u;
    (void)set_mouth(p, i, static_cast<unsigned>(v + 1) | (slide ? kMouthGld : 0u));
  }
}
// Is the lane empty (all 16 bytes FOLLOW)?
inline bool mouth_lane_empty(const PatternData& p) noexcept {
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    if (get_mouth(p, i) != 0u) {
      return false;
    }
  }
  return true;
}
// Does the lane equal `out` (the 16 bytes the last FILL left)?
inline bool mouth_lane_is(const PatternData& p, const std::uint8_t (&out)[kPatternSteps]) noexcept {
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    if (get_mouth(p, i) != out[i]) {
      return false;
    }
  }
  return true;
}
inline constexpr const char* kFillPrompt = "FILL MOUTH LANE: F1 CONFIRMS";
inline constexpr const char* kFilled = "MOUTH FILLED: F7 AGAIN, SHIFT + F6 CLEARS";
inline constexpr const char* kWobPlays = "WOB PLAYS WITH THE LINE";
inline constexpr const char* kMouthTract = "MOUTH SPEAKS IN TRACT";

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
    case Lane::Mouth:
      return set_mouth(p, i, 0u);
  }
  return false;
}

// ---- COPY, INSERT, DELETE (docs/UI-DESIGN.md section 3): the step under the cursor, or with SHIFT the whole pattern ----------------------
// One clipboard holds the last COPY: a step (every lane) or a pattern (its LENGTH with it).
enum class ClipKind : std::uint8_t { Empty, Step, Pattern };
struct Clip {
  ClipKind kind = ClipKind::Empty;
  std::uint32_t word = 0;  // the step (pack_step)
  std::uint8_t mouth = 0;  // its mouth byte (V3)
  PatternData pattern{};
};
inline void copy_step(Clip& c, const PatternData& p, unsigned i) noexcept {
  if (i < kPatternSteps) {
    c.kind = ClipKind::Step;
    c.word = load_word(p.step[i]);
    c.mouth = get_mouth(p, i);
  }
}
inline void copy_whole(Clip& c, const PatternData& p) noexcept {
  c.kind = ClipKind::Pattern;
  copy_pattern(c.pattern, p);
}
// INSERT: the copied step at `i`, the steps from `i` to the end of LENGTH one step right (the last one falls off). False, changing nothing, with
// no step copied or `i` past LENGTH.
inline bool insert_step(PatternData& p, unsigned i, const Clip& c) noexcept {
  const unsigned len = length_of(p);
  if (c.kind != ClipKind::Step || i >= len) {
    return false;
  }
  for (unsigned j = len - 1u; j > i; --j) {
    store_word(p.step[j], load_word(p.step[j - 1u]));
    (void)set_mouth(p, j, get_mouth(p, j - 1u));
  }
  store_word(p.step[i], c.word);
  (void)set_mouth(p, i, c.mouth);
  return true;
}
// SHIFT + INSERT: the copied pattern over this one, LENGTH and all. False with no pattern copied.
inline bool paste_whole(PatternData& p, const Clip& c) noexcept {
  if (c.kind != ClipKind::Pattern) {
    return false;
  }
  copy_pattern(p, c.pattern);
  return true;
}
// DELETE: the step empty in every lane (a rest, FOLLOW, the default level, no slice), as rest_lane leaves each lane.
inline bool clear_step(PatternData& p, unsigned i) noexcept {
  const bool ok = set_step(p, i, Step{});
  return set_mouth(p, i, 0u) && ok;
}
// SHIFT + DELETE, once F1 confirms: every step empty; LENGTH stays.
inline void clear_steps(PatternData& p) noexcept {
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    (void)clear_step(p, i);
  }
}
inline constexpr const char* kClearPatternPrompt = "EMPTY THE PATTERN: F1 CONFIRMS";

}  // namespace o3_voc03er::pattern
