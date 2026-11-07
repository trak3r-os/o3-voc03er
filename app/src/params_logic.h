// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PARAMS' pure logic (docs/UI-DESIGN.md, "PARAMS"): the three pages of eight keys, which key the encoder drives, how a key steps and how its value
// reads, and the over-budget test of QUALITY. Apart from the screen so a host test (tests/params_test.cpp) holds it. Nothing here allocates or
// reads the engine; values go through the unit's own parameter ids, in the units PERFORM writes them (so the two screens never disagree).
//
// A key is blank (None), drawn dim with no value (Later: a parameter the unit does not have yet; a tap says COMES LATER), or live (Value). A live
// key has a "display value", an int: a percent for a 0..1 parameter, tenths of a dB for the level, otherwise the parameter itself.

#include "perform_logic.h"

#include <o3_voc03er/units/vocoder/vocoder.h>

#include <cstddef>
#include <cstdint>

namespace o3_voc03er::params {

inline constexpr unsigned kPages = 3;
inline constexpr unsigned kKeys = 8;

enum class Kind : std::uint8_t { None, Later, Value };
// How a live key reads and steps.
enum class Fmt : std::uint8_t {
  Pct,      // a 0..1 parameter as 0..100 %
  Wave,     // SHAPE, in hundredths of Wave's 0..2: SAW, PULSE and NARROW by name, the morph between them as a number
  MsLog,    // milliseconds on a log curve: a step is a tenth of the value (a third coarse), at least 1
  MsLin,    // milliseconds, a fixed step
  Db,       // tenths of a dB
  Int,      // the parameter itself, clamped
  Mode,     // 0..3 (BANDS, TALK, TUNE, ROBOT), clamped
  Key,      // 0..11, round
  Scale,    // 0..kScaleTableCount - 1, round
  OnOff,    // 0 / 1
};

struct KeyDef {
  const char* name;
  Kind kind;
  std::uint16_t param;  // Vocoder::Param (Value only)
  Fmt fmt;
  int lo, hi;           // display values
  int step, coarse;     // per detent, and per detent with SHIFT (and a second tap on the key)
};

inline constexpr KeyDef kBlank{"", Kind::None, 0, Fmt::Int, 0, 0, 0, 0};
inline constexpr KeyDef later(const char* name) noexcept { return KeyDef{name, Kind::Later, 0, Fmt::Int, 0, 0, 0, 0}; }

inline constexpr const char* kPageNames[kPages] = {"CARRIER", "SHAPE", "TUNE"};

// The pages (docs/UI-DESIGN.md, "PARAMS"). SHAPE is Wave as a morph, SPREAD is Detune, SUB and TILT the carrier's (U10). VOICING comes later.
inline constexpr KeyDef kTable[kPages][kKeys] = {
    {{"SHAPE", Kind::Value, Vocoder::Wave, Fmt::Wave, 0, 200, 5, 25},
     {"SPREAD", Kind::Value, Vocoder::Detune, Fmt::Int, 0, 100, 1, 10},
     {"SUB", Kind::Value, Vocoder::Sub, Fmt::Pct, 0, 100, 2, 10},
     {"TILT", Kind::Value, Vocoder::Tilt, Fmt::Db, -120, 120, 5, 20},
     {"NOISE", Kind::Value, Vocoder::Noise, Fmt::Pct, 0, 100, 2, 10},
     {"ATTACK", Kind::Value, Vocoder::Attack, Fmt::MsLog, 1, 500, 1, 1},
     {"RELEASE", Kind::Value, Vocoder::Release, Fmt::MsLog, 5, 2000, 1, 1},
     {"LEVEL", Kind::Value, Vocoder::Level, Fmt::Db, -480, 60, 5, 60}},
    {{"MODE", Kind::Value, Vocoder::Mode, Fmt::Mode, 0, 3, 1, 1},
     {"QUALITY", Kind::Value, Vocoder::Quality, Fmt::Int, 8, 24, 1, 4},
     {"FREEZE", Kind::Value, Vocoder::Freeze, Fmt::OnOff, 0, 1, 1, 1},
     {"DRY", Kind::Value, Vocoder::Dry, Fmt::Pct, 0, 100, 2, 10},
     later("VOICING"), kBlank, kBlank, kBlank},
    {{"KEY", Kind::Value, Vocoder::Key, Fmt::Key, 0, 11, 1, 1},
     {"SCALE", Kind::Value, Vocoder::Scale, Fmt::Scale, 0, static_cast<int>(kScaleTableCount) - 1, 1, 1},
     {"SPEED", Kind::Value, Vocoder::Speed, Fmt::MsLin, 0, 500, 5, 25},
     {"AMOUNT", Kind::Value, Vocoder::Amount, Fmt::Pct, 0, 100, 2, 10},
     {"NOTES IN", Kind::Value, Vocoder::NotesIn, Fmt::OnOff, 0, 1, 1, 1}, kBlank, kBlank, kBlank},
};

inline constexpr const KeyDef& key_def(unsigned page, unsigned key) noexcept { return kTable[page % kPages][key % kKeys]; }

// PARAMS pressed again, or DOWN: the next page; UP: the one before. Round.
inline constexpr unsigned next_page(unsigned page, int delta = 1) noexcept { return perform::step_wrapped(page % kPages, delta, kPages); }

inline constexpr bool focusable(unsigned page, unsigned key) noexcept { return key < kKeys && key_def(page, key).kind == Kind::Value; }

// The key the encoder points at when a page opens: its first live one.
inline constexpr unsigned first_focus(unsigned page) noexcept {
  for (unsigned k = 0; k < kKeys; ++k) {
    if (focusable(page, k)) {
      return k;
    }
  }
  return 0;
}

// ---- values ---------------------------------------------------------------------------------------------------------------------------

// The unit's parameter value of a display value, and back.
inline int to_display(Fmt f, float v) noexcept {
  switch (f) {
    case Fmt::Pct:
    case Fmt::Wave:
      return static_cast<int>(v * 100.0f + (v < 0.0f ? -0.5f : 0.5f));
    case Fmt::Db:
      return static_cast<int>(v * 10.0f + (v < 0.0f ? -0.5f : 0.5f));
    default:
      return static_cast<int>(v + (v < 0.0f ? -0.5f : 0.5f));
  }
}

inline float to_param(Fmt f, int d) noexcept {
  switch (f) {
    case Fmt::Pct:
    case Fmt::Wave:
      return static_cast<float>(d) * 0.01f;
    case Fmt::Db:
      return static_cast<float>(d) * 0.1f;
    default:
      return static_cast<float>(d);
  }
}

// `cur` stepped by `delta` detents (SHIFT or a second tap: `coarse`). Round keys wrap; the rest stop at their ends, unless `wrap` (a tap) is set
// and the value already stands at the end it moves towards, which sends it to the other end.
inline int step_display(const KeyDef& k, int cur, int delta, bool coarse, bool wrap = false) noexcept {
  if (delta == 0 || k.kind != Kind::Value) {
    return cur;
  }
  const int n = k.hi - k.lo + 1;
  if (k.fmt == Fmt::Key || k.fmt == Fmt::Scale || k.fmt == Fmt::OnOff) {
    int v = (cur - k.lo + delta) % n;
    return k.lo + (v < 0 ? v + n : v);
  }
  int next = cur;
  if (k.fmt == Fmt::MsLog) {
    int mag = cur / (coarse ? 3 : 10);
    mag = mag < 1 ? 1 : mag;
    next = cur + (delta > 0 ? mag : -mag) * (delta > 0 ? delta : -delta);
  } else {
    next = cur + delta * (coarse ? k.coarse : k.step);
  }
  if (wrap && delta > 0 && cur >= k.hi) {
    return k.lo;
  }
  if (wrap && delta < 0 && cur <= k.lo) {
    return k.hi;
  }
  return next < k.lo ? k.lo : (next > k.hi ? k.hi : next);
}

// The key's value as text (at most 11 characters), into out.
inline void format_value(const KeyDef& k, int d, char (&out)[12]) noexcept {
  out[0] = '\0';
  const auto put = [&out](const char* s) noexcept {
    std::size_t n = 0;
    while (out[n] != '\0') {
      ++n;
    }
    while (*s != '\0' && n + 1u < sizeof out) {
      out[n++] = *s++;
    }
    out[n] = '\0';
  };
  char num[12] = {};
  const unsigned u = static_cast<unsigned>(d < 0 ? 0 : d);
  switch (k.fmt) {
    case Fmt::Pct:
      (void)perform::format_uint(u, num);
      put(num);
      put("%");
      break;
    case Fmt::MsLog:
    case Fmt::MsLin:
      (void)perform::format_uint(u, num);
      put(num);
      put("MS");
      break;
    case Fmt::Db:
      perform::format_db(static_cast<float>(d) * 0.1f, num);
      put(num);
      break;
    case Fmt::Int:
      (void)perform::format_uint(u, num);
      put(num);
      break;
    case Fmt::Wave:
      if (u % 100u == 0u) {
        put(u == 0u ? "SAW" : (u == 100u ? "PULSE" : "NARROW"));
      } else {
        num[0] = static_cast<char>('0' + u / 100u);
        num[1] = '.';
        num[2] = static_cast<char>('0' + u / 10u % 10u);
        num[3] = static_cast<char>('0' + u % 10u);
        num[4] = '\0';
        put(num);
      }
      break;
    case Fmt::Mode:
      put(perform::kModeNames[u < 4u ? u : 0u]);
      break;
    case Fmt::Key:
      put(perform::kKeyNames[u % 12u]);
      break;
    case Fmt::Scale:
      put(perform::kScaleNames[u < kScaleTableCount ? u : 0u]);
      break;
    case Fmt::OnOff:
      put(d != 0 ? "ON" : "OFF");
      break;
  }
}

// The value's bar under its window, 0..100, or -1 for a key that is a word.
inline int bar_of(const KeyDef& k, int d) noexcept {
  switch (k.fmt) {
    case Fmt::Pct:
    case Fmt::Wave:
    case Fmt::MsLin:
    case Fmt::Db:
    case Fmt::Int:
      return perform::bar_percent(static_cast<float>(d), static_cast<float>(k.lo), static_cast<float>(k.hi));
    case Fmt::MsLog: {  // the bar is the position in steps, so it moves evenly under the encoder
      int steps = 0, at = 0;
      for (int probe = k.lo; probe < k.hi && steps < 200; ++steps) {
        probe += probe / 10 < 1 ? 1 : probe / 10;
        if (probe <= d) {
          ++at;
        }
      }
      return steps > 0 ? at * 100 / steps : 0;
    }
    default:
      return -1;
  }
}

}  // namespace o3_voc03er::params
