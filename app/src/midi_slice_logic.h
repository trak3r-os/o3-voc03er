// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MS1, MIDI slices on their own channel (docs/UI-DESIGN.md 7c): the pure part. The setting SLICE CH is one byte: 0 SAME, 1..16 a channel, 17 OFF.
// No state here: app.cpp (App::midi() and the push to the unit) and the SAMPLE screen call these.
//   SAME  the notes of the user's channel from 36 up fire slices (the unit's own rule, today's behaviour); nothing else does.
//   1..16 the notes of that channel fire slices; the unmarked notes of the user's channel always play the synth. Those of the slice channel never
//         play the synth and never record into PATTERN's NOTE lane (the app sends them to the unit as Vocoder::kNoteSlice).
//   OFF   MIDI never fires a slice: every routed note plays the synth.
// A slice channel equal to the user's channel is SAME. With the user's channel omni (0) the kit's route takes every channel, so a note of the
// slice channel never reaches App::midi(): it plays the synth, and the screen says "SET MIDI CH".

#include <cstdint>
#include <cstring>

namespace o3_voc03er::midislice {

inline constexpr std::uint8_t kSame = 0;
inline constexpr std::uint8_t kOff = 17;
inline constexpr std::uint8_t kValues = 18;  // 0 .. 17

// A value out of range (a damaged file, a console typo) is SAME.
constexpr std::uint8_t clamp(unsigned v) noexcept { return v < kValues ? static_cast<std::uint8_t>(v) : kSame; }

constexpr bool is_channel(unsigned setting) noexcept { return setting >= 1u && setting <= 16u; }

// The slice channel the app listens to, 1..16, or 0 for none: SAME and OFF have none, and a slice channel equal to the user's is SAME.
constexpr unsigned slice_channel(unsigned setting, unsigned user_channel) noexcept {
  return is_channel(setting) && setting != user_channel ? setting : 0u;
}

// Whether the unmarked notes of the route (the user's channel) may fire slices: SAME, or a slice channel that is the user's own.
constexpr bool route_fires_slices(unsigned setting, unsigned user_channel) noexcept {
  return setting == kSame || (is_channel(setting) && setting == user_channel);
}

// App::midi(): a NoteOn (velocity above 0) of the slice channel is sent to the unit as a slice note. With the user's channel omni the message
// never gets here, so nothing is asked of it.
constexpr bool fires(unsigned setting, unsigned user_channel, unsigned message_channel) noexcept {
  const unsigned ch = slice_channel(setting, user_channel);
  return ch != 0u && ch == message_channel;
}

// A note off, or a note on at velocity 0 (the other spelling), of the slice channel: the app takes it and does nothing (a slice is one shot).
constexpr bool takes_release(unsigned setting, unsigned user_channel, unsigned message_channel) noexcept {
  return fires(setting, user_channel, message_channel);
}

// The screen's hint: a slice channel is set and the user's channel is omni, so the setting cannot work.
constexpr bool needs_hint(unsigned setting, unsigned user_channel) noexcept { return is_channel(setting) && user_channel == 0u; }

// The value the screen and the console show, into `out` (at least 6 bytes): SAME, 1..16, OFF.
inline void format(unsigned setting, char (&out)[12]) noexcept {
  if (setting == kOff) {
    out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = '\0';
  } else if (is_channel(setting)) {
    unsigned n = 0;
    if (setting >= 10u) {
      out[n++] = '1';
    }
    out[n++] = static_cast<char>('0' + setting % 10u);
    out[n] = '\0';
  } else {
    out[0] = 'S'; out[1] = 'A'; out[2] = 'M'; out[3] = 'E'; out[4] = '\0';
  }
}

// The next value a click of the encoder gives: SAME, 1, 2 .. 16, OFF, and no wrap at either end.
constexpr std::uint8_t step(unsigned setting, int delta) noexcept {
  const int v = static_cast<int>(clamp(setting)) + delta;
  return static_cast<std::uint8_t>(v < 0 ? 0 : (v > static_cast<int>(kOff) ? kOff : v));
}

// The console's word: "same", "off" or a channel "1".."16" (no sign, no spaces); false and `out` untouched for anything else.
inline bool parse(const char* word, std::uint8_t& out) noexcept {
  if (word == nullptr) {
    return false;
  }
  if (std::strcmp(word, "same") == 0) {
    out = kSame;
    return true;
  }
  if (std::strcmp(word, "off") == 0) {
    out = kOff;
    return true;
  }
  unsigned n = 0;
  unsigned digits = 0;
  for (const char* c = word; *c != '\0'; ++c, ++digits) {
    if (*c < '0' || *c > '9' || digits >= 2u) {
      return false;
    }
    n = n * 10u + static_cast<unsigned>(*c - '0');
  }
  if (!is_channel(n)) {
    return false;
  }
  out = static_cast<std::uint8_t>(n);
  return true;
}

}  // namespace o3_voc03er::midislice
