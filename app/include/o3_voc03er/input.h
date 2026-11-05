// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The vocoder's input side, the pure parts (host test: tests/input_test.cpp): the audio sources and their names, the gain a source
// starts at, the input peak meter that forgets the previous source after a switch, and the console's `voc` command.

#include <o3sdk/hal/capture.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace o3_voc03er {

struct SourceName {
  const char* name;   // the console's word
  const char* label;  // the screen's
  o3sdk::hal::capture::Source source;
};
inline constexpr SourceName kSources[] = {
    {"line", "LINE", o3sdk::hal::capture::Source::LineMix},   {"left", "LEFT", o3sdk::hal::capture::Source::LineLeft},
    {"right", "RIGHT", o3sdk::hal::capture::Source::LineRight}, {"miclow", "MIC LOW", o3sdk::hal::capture::Source::MicLow},
    {"michigh", "MIC HIGH", o3sdk::hal::capture::Source::MicHigh}, {"radio", "RADIO", o3sdk::hal::capture::Source::Radio},
    {"usb", "USB", o3sdk::hal::capture::Source::Usb},
};
inline constexpr std::size_t kSourceCount = sizeof kSources / sizeof kSources[0];

inline const SourceName* find_source(o3sdk::hal::capture::Source s) noexcept {
  for (const SourceName& n : kSources) {
    if (n.source == s) {
      return &n;
    }
  }
  return nullptr;
}
inline const char* source_name(o3sdk::hal::capture::Source s) noexcept {
  const SourceName* n = find_source(s);
  return n != nullptr ? n->name : "?";
}
// The next source of the key that cycles them (line, left, right, ... usb, line).
inline o3sdk::hal::capture::Source next_source(o3sdk::hal::capture::Source s) noexcept {
  for (std::size_t i = 0; i < kSourceCount; ++i) {
    if (kSources[i].source == s) {
      return kSources[(i + 1u) % kSourceCount].source;
    }
  }
  return kSources[0].source;
}

// Gain 0 is the ADC's 0 dB, not mute, and the ADC has no attenuation: a line source starts there (a hot one can only be turned down at
// the source). The others (microphones, radio, USB) start at 50.
constexpr unsigned default_gain(o3sdk::hal::capture::Source s) noexcept {
  using S = o3sdk::hal::capture::Source;
  return s == S::LineMix || s == S::LineLeft || s == S::LineRight ? 0u : 50u;
}

// Per-mille of full scale, 0..100000 (saturating far above full scale).
constexpr unsigned permille(float peak) noexcept {
  const float v = peak * 1000.0f + 0.5f;
  return v <= 0.0f ? 0u : (v > 100000.0f ? 100000u : static_cast<unsigned>(v));
}
inline constexpr unsigned kClipPermille = 990;

// The input peak as the screen and `state` show it (main loop only). The engine's peaks() are fed every pass; each pass's maxima fill
// two 500 ms buckets and the shown value is the larger of the two (0.5..1 s). The kit does not clear anything at a source switch, so
// reset() does: it empties the buckets and drops the reads of the next `holdoff_ms` (the gate is shut for the settling time and the
// first blocks after it can still carry the previous source's tail).
class InputMeter {
 public:
  static constexpr std::uint64_t kBucketMs = 500;
  static constexpr std::uint64_t kSelectHoldoffMs = 300;  // after `select`: the 200 ms settling and a margin
  static constexpr std::uint64_t kSettleHoldoffMs = 100;  // after the kit says settling is over

  void reset(std::uint64_t now_ms, std::uint64_t holdoff_ms) noexcept {
    cur_l_ = cur_r_ = prev_l_ = prev_r_ = 0.0f;
    bucket_start_ = now_ms;
    ignore_until_ = now_ms + holdoff_ms;
  }
  void feed(float l, float r, std::uint64_t now_ms) noexcept {
    if (now_ms < ignore_until_) {
      return;
    }
    if (now_ms - bucket_start_ >= kBucketMs) {
      const bool stale = now_ms - bucket_start_ >= 2u * kBucketMs;
      prev_l_ = stale ? 0.0f : cur_l_;
      prev_r_ = stale ? 0.0f : cur_r_;
      cur_l_ = cur_r_ = 0.0f;
      bucket_start_ = now_ms;
    }
    cur_l_ = l > cur_l_ ? l : cur_l_;
    cur_r_ = r > cur_r_ ? r : cur_r_;
  }
  float left() const noexcept { return cur_l_ > prev_l_ ? cur_l_ : prev_l_; }
  float right() const noexcept { return cur_r_ > prev_r_ ? cur_r_ : prev_r_; }
  float peak() const noexcept { return left() > right() ? left() : right(); }
  bool clipping() const noexcept { return permille(peak()) >= kClipPermille; }

 private:
  float cur_l_ = 0.0f, cur_r_ = 0.0f, prev_l_ = 0.0f, prev_r_ = 0.0f;
  std::uint64_t bucket_start_ = 0;
  std::uint64_t ignore_until_ = 0;
};

// `voc <bands|talk> [notes 0..8]`: the mode and how many notes the console holds (the unit round has no person at the pads).
inline constexpr unsigned kVocMaxNotes = 8;
struct VocCommand {
  bool talk = false;
  unsigned notes = 0;
};
// false (out untouched) for a word that is not a mode, a count that is not 0..8 or an extra word. No count means 0 (release all).
inline bool parse_voc(const char* mode, const char* count, VocCommand& out) noexcept {
  VocCommand v;
  if (mode != nullptr && std::strcmp(mode, "bands") == 0) {
    v.talk = false;
  } else if (mode != nullptr && std::strcmp(mode, "talk") == 0) {
    v.talk = true;
  } else {
    return false;
  }
  if (count != nullptr) {
    unsigned n = 0;
    std::size_t i = 0;
    for (; count[i] >= '0' && count[i] <= '9' && i < 3u; ++i) {
      n = n * 10u + static_cast<unsigned>(count[i] - '0');
    }
    if (i == 0u || count[i] != '\0' || n > kVocMaxNotes) {
      return false;
    }
    v.notes = n;
  }
  out = v;
  return true;
}
// The i-th held note: C3 major seventh and up, spread over two octaves (a chord that exercises the carrier's voices).
inline constexpr std::uint8_t kVocChord[kVocMaxNotes] = {48, 52, 55, 59, 60, 64, 67, 71};

}  // namespace o3_voc03er
