// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MASTER's pure logic (docs/UI-DESIGN.md, "MASTER"): the eight keys (SOURCE, GAIN, MONITOR, DRY, LEVEL, RETURN, SEEK DOWN, SEEK UP), which of them
// the board can answer, the line a dim key says when it is tapped, the radio's frequency as text, and the follower that gives the two tall meters
// their fall and held peak. Apart from the screen so a host test (tests/master_test.cpp) holds it. Nothing here allocates or reads the engine.
//
// The steppable keys reuse PARAMS' KeyDef (params_logic.h), so DRY and LEVEL step and read exactly as PARAMS writes them. SOURCE is a round key
// over input.h's kSources (a Scale-like wrap); GAIN, MONITOR and RETURN are percent keys the kit holds (AudioIn, UsbAudio), not unit parameters.

#include "params_logic.h"

#include <o3_voc03er/input.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/audio_in.h>

#include <cstddef>
#include <cstdint>

namespace o3_voc03er::master {

inline constexpr unsigned kKeys = 8;
enum Key : std::uint8_t { Source, Gain, Monitor, Dry, Level, Return, SeekDown, SeekUp };

// F1..F6 step a value; F7 and F8 are the radio's seek (the table has no value for them).
inline constexpr params::KeyDef kDefs[6] = {
    {"SOURCE", params::Kind::Value, 0, params::Fmt::Scale, 0, static_cast<int>(kSourceCount) - 1, 1, 1},
    {"GAIN", params::Kind::Value, 0, params::Fmt::Pct, 0, 100, 2, 10},
    {"MONITOR", params::Kind::Value, 0, params::Fmt::Pct, 0, 100, 2, 10},
    {"DRY", params::Kind::Value, Vocoder::Dry, params::Fmt::Pct, 0, 100, 2, 10},
    {"LEVEL", params::Kind::Value, Vocoder::Level, params::Fmt::Db, -480, 60, 5, 60},
    {"RETURN", params::Kind::Value, 0, params::Fmt::Pct, 0, 100, 2, 10},
};
inline constexpr const char* kSeekNames[2] = {"SEEK DOWN", "SEEK UP"};

inline constexpr const char* key_name(unsigned k) noexcept { return k < 6u ? kDefs[k].name : kSeekNames[(k - 6u) & 1u]; }
inline constexpr bool steppable(unsigned k) noexcept { return k < 6u; }

// What the board and the source in force let the keys do. Filled from the kit by the screen.
struct Avail {
  bool audio_in = false;      // a capture device: select, gain
  bool usb_source = false;    // the source in force is USB (no analogue gain)
  bool radio_source = false;  // the source in force is the radio
  bool radio_usable = false;  // the receiver answered
  bool usb_device = false;    // a USB audio device: RETURN
};

inline constexpr bool live(unsigned k, const Avail& a) noexcept {
  switch (k) {
    case Source:
      return a.audio_in;
    case Gain:
      return a.audio_in && !a.usb_source;
    case Return:
      return a.usb_device;
    case SeekDown:
    case SeekUp:
      return a.radio_source && a.radio_usable;
    default:  // MONITOR works without a capture device too (the kit says so), DRY and LEVEL are the unit's
      return k < kKeys;
  }
}

// The line a tap on a dim key says (a static string).
inline constexpr const char* dim_note(unsigned k, const Avail& a) noexcept {
  switch (k) {
    case Source:
      return "NO AUDIO INPUT ON THIS BOARD";
    case Gain:
      return a.audio_in ? "NO GAIN ON USB" : "NO AUDIO INPUT ON THIS BOARD";
    case Return:
      return "NO USB AUDIO ON THIS BOARD";
    case SeekDown:
    case SeekUp:
      return a.radio_source ? "THE RADIO DID NOT ANSWER" : "PICK RADIO AS THE SOURCE";
    default:
      return "";
  }
}

// ---- source ---------------------------------------------------------------------------------------------------------------------------

inline int source_index(o3sdk::hal::capture::Source s) noexcept {
  for (std::size_t i = 0; i < kSourceCount; ++i) {
    if (kSources[i].source == s) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

// The source `delta` detents from index `cur`, round.
inline const SourceName& source_after(int cur, int delta) noexcept {
  const int v = params::step_display(kDefs[Source], cur, delta, false, true);
  return kSources[static_cast<std::size_t>(v) % kSourceCount];
}

// A second tap on a key: MONITOR toggles between off and the kit's "on" (70), the rest step coarse and wrap at the end.
inline int tap_value(unsigned k, int cur) noexcept {
  if (k == Monitor) {
    return cur == 0 ? static_cast<int>(o3sdk::app::AudioIn::kMonitorOn) : 0;
  }
  return k < 6u ? params::step_display(kDefs[k], cur, 1, true, true) : cur;
}

// ---- radio ----------------------------------------------------------------------------------------------------------------------------

// A frequency in 10 kHz units as MHz with two decimals ("98.50", "108.00"), at most 6 characters and the terminator.
inline void format_freq(unsigned f, char (&out)[12]) noexcept {
  const unsigned mhz = f / 100u;
  const unsigned frac = f % 100u;
  std::size_t n = 0;
  if (mhz >= 100u) {
    out[n++] = static_cast<char>('0' + (mhz / 100u) % 10u);
  }
  out[n++] = static_cast<char>('0' + (mhz / 10u) % 10u);
  out[n++] = static_cast<char>('0' + mhz % 10u);
  out[n++] = '.';
  out[n++] = static_cast<char>('0' + frac / 10u);
  out[n++] = static_cast<char>('0' + frac % 10u);
  out[n] = '\0';
}

// ---- meters ---------------------------------------------------------------------------------------------------------------------------

// The fall and the held peak of a 0..255 ladder, PERFORM's rule: attack at once, release by a quarter of the distance; a peak holds for
// kPeakHoldFrames, then falls two a frame. The state is three bytes.
struct Follower {
  static constexpr std::uint8_t kPeakHoldFrames = 20;
  std::uint8_t level = 0;
  std::uint8_t peak = 0;
  std::uint8_t age = 0;

  void feed(std::uint8_t raw) noexcept {
    if (raw >= level) {
      level = raw;
    } else {
      level = static_cast<std::uint8_t>(level - (static_cast<unsigned>(level) - raw + 3u) / 4u);
    }
    if (level >= peak) {
      peak = level;
      age = 0;
    } else if (age < kPeakHoldFrames) {
      ++age;
    } else {
      peak = static_cast<std::uint8_t>(peak > 2u ? peak - 2u : 0u);
    }
  }
};

}  // namespace o3_voc03er::master
