// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PERFORM's pure logic (docs/UI-DESIGN.md): the words the screen shows and the small arithmetic of its F keys, apart from the screen so a host
// test (tests/perform_logic_test.cpp) holds them. Nothing here allocates or reads the engine.

#include <o3_voc03er/chords.h>
#include <o3_voc03er/pad_map.h>
#include <o3_voc03er/units/vocoder/scale.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace o3_voc03er::perform {

inline constexpr const char* kKeyNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline constexpr const char* kScaleNames[kScaleTableCount] = {"CHROM", "MAJOR", "MINOR", "MAJ-P", "MIN-P", "BLUES", "DORIAN"};
inline constexpr const char* kCarrierNames[7] = {"FOLLOW", "SYNTH", "NOISE", "LOOP1", "LOOP2", "LOOP3", "LOOP4"};  // Vocoder::CarSrc 0..6
inline constexpr const char* kKindNames[4] = {"TRIAD", "7TH", "OPEN", "POWER"};                                    // ChordKind
inline constexpr const char* kModeNames[3] = {"BANDS", "TALK", "TUNE"};
inline constexpr const char* kRowNames[4] = {"POWER", "OPEN", "7TH", ""};  // the bottom row's name is the kind's

inline constexpr unsigned kLabelChars = 11;  // a pad's label: a chord's name fits kChordNameMax (12) with its terminator

// The mode index of the unit's Mode value (the app's mode_index).
constexpr unsigned mode_index(float v) noexcept { return v >= 1.5f ? 2u : (v >= 0.5f ? 1u : 0u); }

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

}  // namespace o3_voc03er::perform
