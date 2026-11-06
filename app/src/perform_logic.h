// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PERFORM's pure logic (docs/UI-DESIGN.md): the words the screen shows and the small arithmetic of its F keys, apart from the screen so a host
// test (tests/perform_logic_test.cpp) holds them. Nothing here allocates or reads the engine.

#include <o3_voc03er/chords.h>
#include <o3_voc03er/pad_map.h>
#include <o3_voc03er/units/vocoder/scale.h>

#include <o3sdk/core/audio_clock.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace o3_voc03er::perform {

inline constexpr const char* kKeyNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline constexpr const char* kScaleNames[kScaleTableCount] = {"CHROM", "MAJOR", "MINOR", "MAJ-P", "MIN-P", "BLUES", "DORIAN"};
inline constexpr const char* kCarrierNames[9] = {"FOLLOW", "SYNTH", "NOISE", "LOOP1", "LOOP2", "LOOP3", "LOOP4", "LIVE", "VOICE"};  // Vocoder::CarSrc 0..8
inline constexpr const char* kKindNames[4] = {"TRIAD", "7TH", "OPEN", "POWER"};                                    // ChordKind
inline constexpr const char* kModeNames[4] = {"BANDS", "TALK", "TUNE", "ROBOT"};
inline constexpr const char* kRowNames[4] = {"POWER", "OPEN", "7TH", ""};  // the bottom row's name is the kind's

inline constexpr unsigned kLabelChars = 11;  // a pad's label: a chord's name fits kChordNameMax (12) with its terminator

// Samples in one 4/4 bar at `mbpm` milli-BPM (a stopped or odd tempo reads as 20 BPM at the slowest). Shared by the two capture screens.
inline float bar_samples(unsigned mbpm) noexcept {
  if (mbpm < 20000u) {
    mbpm = 20000u;
  }
  return o3sdk::core::kRefRateHz * 240000.0f / static_cast<float>(mbpm);  // the engine's rate (44.1 kHz), 240 s of beats a bar at 1 BPM
}

// The recorded length at which Rec 0 goes out for a take of `bars` bars: the transport running, the middle of the last bar (the unit then stops
// at its end line); stopped, the unit stops at once, so the whole length.
inline float capture_stop_at(unsigned bars, float bar, bool playing) noexcept {
  return playing ? (static_cast<float>(bars) - 0.5f) * bar : static_cast<float>(bars) * bar;
}

// The mode index of the unit's Mode value (0 BANDS, 1 TALK, 2 TUNE, 3 ROBOT): it names the mode (kModeNames). The app's own mode_index (app.cpp) still
// knows three.
constexpr unsigned mode_index(float v) noexcept { return v >= 2.5f ? 3u : (v >= 1.5f ? 2u : (v >= 0.5f ? 1u : 0u)); }
// What a mode draws and which F8 value it has: ROBOT is BANDS with a compressor after it, so it draws BANDS' page.
constexpr unsigned mode_page(unsigned mode) noexcept { return mode == 3u ? 0u : mode; }

// `value` stepped by `delta` and held inside lo..hi (no wrap).
constexpr int step_clamped(int value, int delta, int lo, int hi) noexcept {
  const int v = value + delta;
  return v < lo ? lo : (v > hi ? hi : v);
}
// `value` stepped by `delta`, round (0..count - 1).
constexpr unsigned step_wrapped(unsigned value, int delta, unsigned count) noexcept {
  const int c = static_cast<int>(count);
  const int v = (static_cast<int>(value) + delta) % c;
  return static_cast<unsigned>(v < 0 ? v + c : v);
}

// Bit n set: the pad column n (root pitch class n) is in SCALE of KEY.
constexpr std::uint16_t scale_cols(unsigned key, unsigned scale) noexcept {
  if (scale >= kScaleTableCount) {
    scale = 0;
  }
  std::uint16_t m = 0;
  for (unsigned c = 0; c < 12; ++c) {
    if ((kScaleMask[scale] >> ((c + 12u - key % 12u) % 12u) & 1u) != 0u) {
      m = static_cast<std::uint16_t>(m | (1u << c));
    }
  }
  return m;
}

// A chord's name for a pad label, at most kLabelChars characters, into out[kLabelChars + 1].
inline void chord_label(const Chord& c, char (&out)[kLabelChars + 1]) noexcept {
  std::size_t n = 0;
  while (c.name[n] != '\0' && n < kLabelChars) {
    out[n] = c.name[n];
    ++n;
  }
  out[n] = '\0';
}

// The notes two chords share, so a release of a pad does not cut a note the latched chord still holds.
constexpr bool chord_has(const Chord& c, std::uint8_t note) noexcept {
  for (unsigned i = 0; i < c.count && i < kChordMaxNotes; ++i) {
    if (c.notes[i] == note) {
      return true;
    }
  }
  return false;
}

// Decimal text of v (0..9999) into out; returns its length.
inline unsigned format_uint(unsigned v, char (&out)[12]) noexcept {
  char tmp[6] = {};
  unsigned n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + v % 10u);
    v /= 10u;
  } while (v != 0u && n < 5u);
  for (unsigned i = 0; i < n; ++i) {
    out[i] = tmp[n - 1u - i];
  }
  out[n] = '\0';
  return n;
}

// The three ranges of F8 (BANDS SPREAD = Detune cents, TALK QUALITY = LPC order, TUNE SPEED = ms) as a 0..100 bar.
constexpr int bar_percent(float value, float lo, float hi) noexcept {
  const float t = (value - lo) / (hi - lo);
  const float c = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return static_cast<int>(c * 100.0f + 0.5f);
}

// A linear peak as the 0..255 of the ladders: -60 dB is 0, 0 dBFS is 255 (the unit's View scale). Main loop only (a float log2).
inline std::uint8_t level_byte(float linear) noexcept {
  if (!(linear > 0.001f)) {  // -60 dB, and NaN
    return 0;
  }
  if (linear >= 1.0f) {
    return 255;
  }
  const float db = 6.0206f * std::log2(linear);  // 20 log10(x), in float
  const float b = (db + 60.0f) * (255.0f / 60.0f);
  return static_cast<std::uint8_t>(b < 0.0f ? 0.0f : (b > 255.0f ? 255.0f : b));
}

// ---- F7 PADS and the bend -----------------------------------------------------------------------------------------------------------
// BEND ROW (SHIFT + F7), on either F7 page: the bottom row of the grid is a bend strip.
inline constexpr unsigned kStripRow = 3;  // the bend strip: the bottom row of the grid
inline constexpr unsigned kBendFocus = 8;  // PerformScreen's focus value when no F key is hot: the encoder bends
inline constexpr float kBendMaxSt = 12.0f;  // the unit's Bend range (Vocoder::Bend)
inline constexpr std::uint64_t kBendSpringMs = 120;  // the encoder's bend returns to 0 this long after the last detent
inline constexpr unsigned kBendRanges[3] = {2, 5, 12};  // BEND RANGE, semitones either way

// The semitones of a strip column: 5 and 6 are 0, each pad outwards is one (-5 at column 0, +5 at column 11).
constexpr int strip_semitone(unsigned col) noexcept {
  return col <= 5u ? static_cast<int>(col) - 5 : static_cast<int>(col) - 6;
}
// Whether `pad` is on the strip (bottom row) and the strip is up.
constexpr bool on_strip(unsigned pad, bool strip) noexcept { return strip && o3sdk::app::pad_row(pad) == kStripRow; }

// The KEYS page is a piano (PERFORM only; PLAY keeps its chromatic grid). Rows count from the top (pad_row): row 2 the white keys from C of
// the current octave, row 1 the black keys on the column of the white key to their right, row 0 the controls, row 3 the strip (BEND ROW on;
// dark and silent otherwise).
enum class PianoFn : std::uint8_t { None, White, Black, OctDown, OctUp, VolDown, VolUp, RootDown, RootUp, Mod, BndDown, BndUp, Strip, Formant };
struct PianoPad {
  PianoFn fn = PianoFn::None;
  int offset = 0;  // semitones above C of the octave (White, Black); the semitones of the pad (Formant)
};
inline constexpr int kWhiteOffset[12] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19};          // C D E F G A B C D E F G
inline constexpr int kBlackOffset[12] = {-1, 1, 3, -1, 6, 8, 10, -1, 13, 15, -1, 18};       // C# D# . F# G# A# . C# D# . F#; -1 no key
// Row 0, by column: 0 OCT-, 1 OCT+, 3 VOL-, 4 VOL+, 6 ROOT-, 7 ROOT+, 9 MOD, 10 BND-, 11 BND+; 2, 5 and 8 are gaps.
inline constexpr PianoFn kControlFn[12] = {PianoFn::OctDown, PianoFn::OctUp,   PianoFn::None,    PianoFn::VolDown, PianoFn::VolUp,  PianoFn::None,
                                           PianoFn::RootDown, PianoFn::RootUp, PianoFn::None,    PianoFn::Mod,     PianoFn::BndDown, PianoFn::BndUp};
inline constexpr const char* kControlName[12] = {"OCT-", "OCT+", "", "VOL-", "VOL+", "", "ROOT-", "ROOT+", "", "MOD", "BND-", "BND+"};
// The formant strip (KEYS, BEND ROW off, row 3): the Vocoder::Formant semitones of each column; both middle pads are 0.
inline constexpr int kFormantStrip[12] = {-12, -9, -6, -4, -2, 0, 0, 2, 4, 6, 9, 12};
constexpr PianoPad piano_pad(unsigned row, unsigned col, bool strip) noexcept {
  PianoPad p;
  if (col >= 12u) {
    return p;
  }
  if (row == 2u) {
    p.fn = PianoFn::White;
    p.offset = kWhiteOffset[col];
  } else if (row == 1u && kBlackOffset[col] >= 0) {
    p.fn = PianoFn::Black;
    p.offset = kBlackOffset[col];
  } else if (row == 0u) {
    p.fn = kControlFn[col];
  } else if (row == kStripRow && strip) {
    p.fn = PianoFn::Strip;
  } else if (row == kStripRow) {  // BEND ROW off: the formant strip
    p.fn = PianoFn::Formant;
    p.offset = kFormantStrip[col];
  }
  return p;
}
// A control pad plays no note; BND and MOD act while held, the others on the press.
constexpr bool is_control(PianoFn f) noexcept { return f != PianoFn::None && f != PianoFn::White && f != PianoFn::Black && f != PianoFn::Strip && f != PianoFn::Formant; }
constexpr bool is_held_control(PianoFn f) noexcept { return f == PianoFn::Mod || f == PianoFn::BndDown || f == PianoFn::BndUp; }

// The time bend (BND- and BND+) and the vibrato (MOD): linear in the time held, full after kRampMs; the vibrato falls the same way on release.
inline constexpr std::uint64_t kRampMs = 400;
// The bend in semitones after `held_ms` of BND+ (dir +1) or BND- (dir -1) at `range`.
constexpr float bend_time_semitones(std::uint64_t held_ms, int dir, unsigned range) noexcept {
  const std::uint64_t t = held_ms < kRampMs ? held_ms : kRampMs;
  return static_cast<float>(dir) * static_cast<float>(range) * static_cast<float>(t) / static_cast<float>(kRampMs);
}
// The vibrato depth 0..1 after `held_ms` of MOD, starting from 0, and `ms` after the release from depth `from`.
constexpr float vib_rise(std::uint64_t held_ms) noexcept {
  return static_cast<float>(held_ms < kRampMs ? held_ms : kRampMs) / static_cast<float>(kRampMs);
}
constexpr float vib_fall(float from, std::uint64_t ms) noexcept {
  const float v = from - static_cast<float>(ms < kRampMs ? ms : kRampMs) / static_cast<float>(kRampMs);
  return v > 0.0f ? v : 0.0f;
}
// The unit's Vib: the pad's depth and the mod wheel's (CC1, 0..127), the larger.
constexpr float vib_depth(float pad, unsigned cc1) noexcept {
  const float w = static_cast<float>(cc1 < 127u ? cc1 : 127u) * (1.0f / 127.0f);
  return pad > w ? pad : w;
}

// A level as text with one decimal ("-6.0", "0.5"), into out; the hint line's VOL.
inline void format_db(float db, char (&out)[12]) noexcept {
  const int t = static_cast<int>(db < 0.0f ? db * 10.0f - 0.5f : db * 10.0f + 0.5f);
  unsigned n = 0;
  if (t < 0) {
    out[n++] = '-';
  }
  const unsigned a = static_cast<unsigned>(t < 0 ? -t : t);
  if (a >= 100u) {
    out[n++] = static_cast<char>('0' + (a / 100u) % 10u);
  }
  out[n++] = static_cast<char>('0' + (a / 10u) % 10u);
  out[n++] = '.';
  out[n++] = static_cast<char>('0' + a % 10u);
  out[n] = '\0';
}

// The MIDI note of a key pad at `octave` (C of octave 0 is kBottomLeftNote), or -1 for a pad that is no key or past 0..127.
constexpr int piano_note(const PianoPad& p, int octave) noexcept {
  if (p.fn != PianoFn::White && p.fn != PianoFn::Black) {
    return -1;
  }
  const int n = kBottomLeftNote + 12 * octave + p.offset;
  return n >= 0 && n <= 127 ? n : -1;
}

// With BEND ROW on in CHORDS the chord rows move up one: the row above the strip (2) plays `bottom` (F6 CHORD's kind), the two above it 7TH and
// OPEN, or the next ones (POWER, TRIAD) when `bottom` is one of them. Row 3 is the strip (it returns `bottom`, unused).
constexpr ChordKind bend_row_kind(unsigned row, ChordKind bottom) noexcept {
  if (row >= 2u) {
    return bottom;
  }
  const ChordKind cand[4] = {ChordKind::Seventh, ChordKind::Open, ChordKind::Power, ChordKind::Triad};
  unsigned seen = 0;
  const unsigned want = row == 1u ? 0u : 1u;  // row 1 takes the first candidate, row 0 the second
  for (unsigned i = 0; i < 4; ++i) {
    if (cand[i] == bottom) {
      continue;
    }
    if (seen == want) {
      return cand[i];
    }
    ++seen;
  }
  return bottom;
}
// The ChordKind of a chord row (the strip up or not).
constexpr ChordKind row_kind(unsigned row, ChordKind bottom, bool strip) noexcept { return strip ? bend_row_kind(row, bottom) : kind_for_row(row, bottom); }

// The local bend (semitones) after one encoder detent: range / 8, SHIFT coarse range / 2, held inside +-range.
constexpr float bend_step(float st, int delta, bool coarse, unsigned range) noexcept {
  const float r = static_cast<float>(range);
  const float v = st + static_cast<float>(delta) * (coarse ? r * 0.5f : r * 0.125f);
  return v < -r ? -r : (v > r ? r : v);
}
// The Bend value: the local and the wheel's semitones added, held inside +-12.
constexpr float bend_sum(float local, float wheel) noexcept {
  const float v = local + wheel;
  return v < -kBendMaxSt ? -kBendMaxSt : (v > kBendMaxSt ? kBendMaxSt : v);
}
// A 14-bit pitch bend (lsb, msb: 7 bits each; 8192 is centre) into semitones for a BEND RANGE.
constexpr float wheel_semitones(unsigned lsb, unsigned msb, unsigned range) noexcept {
  const int v = static_cast<int>(((msb & 0x7Fu) << 7) | (lsb & 0x7Fu)) - 8192;
  const float n = static_cast<float>(v) * (1.0f / 8192.0f);
  const float c = n > 1.0f ? 1.0f : (n < -1.0f ? -1.0f : n);
  return c * static_cast<float>(range);
}
// Strip text: "-5", "0", "+5".
inline void strip_label(int st, char (&out)[kLabelChars + 1]) noexcept {
  unsigned n = 0;
  if (st < 0) {
    out[n++] = '-';
  } else if (st > 0) {
    out[n++] = '+';
  }
  const unsigned a = static_cast<unsigned>(st < 0 ? -st : st);
  if (a >= 10u) {
    out[n++] = static_cast<char>('0' + a / 10u);
  }
  out[n++] = static_cast<char>('0' + a % 10u);
  out[n] = '\0';
}

// ---- the SHIFT row (docs/UI-DESIGN.md "The SHIFT row of PERFORM") -------------------------------------------------------------------
// What SHIFT + F1..F8 are. Toggle: an action, no focus (FREEZE). Value: takes the encoder (sfocus), a second tap steps it. BendRow: an action.
enum class ShiftFn : std::uint8_t { Freeze, Value, BendRow };
struct ShiftKey {
  const char* name;  // on the F row (the Label face fits 8) and, upper case, HELP's Shift label (9 at most)
  ShiftFn fn;
};
inline constexpr ShiftKey kShiftKeys[8] = {{"FREEZE", ShiftFn::Freeze}, {"GATE", ShiftFn::Value},      {"ARP", ShiftFn::Value},        {"GLIDE", ShiftFn::Value},
                                           {"FORMANT", ShiftFn::Value}, {"THRU", ShiftFn::Value},    {"BEND ROW", ShiftFn::BendRow}, {"PRESET", ShiftFn::Value}};
inline constexpr unsigned kNoShiftFocus = 8;  // PerformScreen's sfocus_ when no SHIFT-row value has the encoder

inline constexpr int kFormantMin = -12;
inline constexpr int kFormantMax = 12;
// Formant (semitones) after one detent: one, SHIFT coarse 4, held to -12..+12.
constexpr int formant_step(int st, int delta, bool coarse) noexcept { return step_clamped(st, delta * (coarse ? 4 : 1), kFormantMin, kFormantMax); }
// Thru (percent) after one detent: 5, SHIFT coarse 20, held to 0..100. The unit's Thru is 0..1 (percent / 100, as `voc thru` writes it).
constexpr int thru_step(int percent, int delta, bool coarse) noexcept { return step_clamped(percent, delta * (coarse ? 20 : 5), 0, 100); }
// GATE and ARP are the unit's enums (Vocoder::Gate 0..4, Vocoder::Arp 0..4); a detent or a second tap steps round.
inline constexpr unsigned kGateCount = 5;
inline constexpr unsigned kArpCount = 5;
inline constexpr const char* kGateNames[kGateCount] = {"OFF", "1/4", "1/8", "1/16", "1/32"};
inline constexpr const char* kArpNames[kArpCount] = {"OFF", "UP", "DOWN", "UP-DOWN", "RANDOM"};
// Glide (ms) after one detent: 10, SHIFT coarse 50, held to 0..500.
constexpr int glide_step(int ms, int delta, bool coarse) noexcept { return step_clamped(ms, delta * (coarse ? 50 : 10), 0, 500); }
// The preset after one detent, round; from none (kNoPreset) a turn up picks the first and a turn down the last. `count` is kPresetCount.
inline constexpr unsigned kNoPreset = 255;
constexpr unsigned preset_step(unsigned cur, int delta, unsigned count) noexcept {
  if (cur >= count) {
    return delta >= 0 ? 0u : count - 1u;
  }
  return step_wrapped(cur, delta, count);
}
// The pad of the formant strip in force: `col` holds `formant` (both middle pads at 0).
constexpr bool formant_lit(int formant, unsigned col) noexcept { return col < 12u && kFormantStrip[col] == formant; }

// The line under PERFORM's grid: what the F key under the encoder does (`focus` 0 to 7). `mode` (0 BANDS, 1 TALK, 2 TUNE, 3 ROBOT) picks F8's word.
inline constexpr unsigned kKeyHintMax = 40;  // characters of the Micro face that fit beside the pads' hint
// With a picker open the line right of the grid is empty and the left one says what its keys do (perform_screen.cpp).
inline const char* key_hint(unsigned focus, unsigned mode, unsigned sfocus = kNoShiftFocus) noexcept {
  if (sfocus == 4u) {
    return "SHIFT F5 FORMANT: THE VOWEL'S PITCH";
  }
  if (sfocus == 5u) {
    return "SHIFT F6 THRU: SIBILANTS COME THROUGH";
  }
  if (sfocus == 1u) {
    return "SHIFT F2 GATE: CUTS THE CARRIER IN TIME";
  }
  if (sfocus == 2u) {
    return "SHIFT F3 ARP: HELD NOTES ONE AT A TIME";
  }
  if (sfocus == 3u) {
    return "SHIFT F4 GLIDE: SLIDE BETWEEN NOTES";
  }
  if (sfocus == 7u) {
    return "SHIFT F8 PRESET: A FACTORY SOUND";
  }
  if (focus >= kBendFocus) {
    return "ENCODER FREE: IT BENDS THE PITCH";
  }
  static constexpr const char* kHints[7] = {
      "F1 MODE: BANDS, TALK, TUNE OR ROBOT",    "F2 SPEAKS: A LIST OF WHAT TALKS", "F3 SOUNDS: A LIST OF THE CARRIERS",
      "F4 KEY: THE ROOT OF THE PADS AND OF TUNE", "F5 SCALE: THE COLUMNS THAT LIGHT",       "F6 CHORD: THE KIND ON THE BOTTOM ROW",
      "F7 PADS: CHORDS OR KEYS. SHIFT: BEND ROW",
  };
  static constexpr const char* kF8[3] = {"F8 SPREAD: THE CARRIER'S DETUNE", "F8 QUALITY: THE LPC ORDER", "F8 SPEED: HOW FAST THE PITCH MOVES"};
  return focus < 7u ? kHints[focus] : kF8[mode_page(mode < 4u ? mode : 0u)];
}

}  // namespace o3_voc03er::perform
