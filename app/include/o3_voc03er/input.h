// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The vocoder's input side, the pure parts (host test: tests/input_test.cpp): the audio sources and their names, the gain a source
// starts at, the input peak meter that forgets the previous source after a switch, and the console's `voc` command.

#include <o3_voc03er/modes.h>

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

// `voc <bands|talk|tune|robot> [notes 0..8]`: the mode and how many notes the console holds (the unit round has no person at the pads).
inline constexpr unsigned kVocMaxNotes = 8;
struct VocCommand {
  unsigned mode = 0;  // Vocoder::Mode: the index of its word in kModeWords (modes.h)
  unsigned notes = 0;
};
// false (out untouched) for a word that is not a mode, a count that is not 0..8 or an extra word. No count means 0 (release all).
// `voc formant <-12..12>`, `voc thru <0..100>`, `voc glide <0..500 ms>`, `voc gate <0..4>` and `voc arp <0..4>`: which one and its value.
struct VocShape {
  bool thru = false;
  bool glide = false;
  bool gate = false;
  bool arp = false;
  bool speaks = false;  // `speaks <0..6>`: Vocoder::ModSrc (0 LIVE, 1 NONE, 3..6 SLOT 1..4; 2 acts as NONE)
  bool sounds = false;  // `sounds <0..7>`: Vocoder::CarSrc (0 FOLLOW, 1 SYNTH, 2 NOISE, 3..6 SLOT 1..4, 7 LIVE)
  bool range = false;  // `bendrange`: value is 2, 5 or 12
  int value = 0;
};
inline bool parse_voc_shape(const char* what, const char* value, VocShape& out) noexcept {
  if (what == nullptr || value == nullptr) {
    return false;
  }
  VocShape v;
  if (std::strcmp(what, "bendrange") == 0) {
    if (std::strcmp(value, "2") != 0 && std::strcmp(value, "5") != 0 && std::strcmp(value, "12") != 0) {
      return false;
    }
    v.range = true;
    v.value = value[1] == '\0' ? value[0] - '0' : 12;
    out = v;
    return true;
  }
  int max = 12;
  bool signed_ok = false;
  if (std::strcmp(what, "thru") == 0) {
    v.thru = true;
    max = 100;
  } else if (std::strcmp(what, "glide") == 0) {
    v.glide = true;
    max = 500;
  } else if (std::strcmp(what, "gate") == 0) {
    v.gate = true;
    max = 4;
  } else if (std::strcmp(what, "arp") == 0) {
    v.arp = true;
    max = 4;
  } else if (std::strcmp(what, "speaks") == 0) {
    v.speaks = true;
    max = 6;
  } else if (std::strcmp(what, "sounds") == 0) {
    v.sounds = true;
    max = 7;
  } else if (std::strcmp(what, "formant") == 0) {
    signed_ok = true;
  } else {
    return false;
  }
  const bool negative = value[0] == '-' && signed_ok;
  std::size_t i = negative ? 1u : 0u;
  const std::size_t first = i;
  int n = 0;
  for (; value[i] >= '0' && value[i] <= '9' && i < first + 3u; ++i) {
    n = n * 10 + (value[i] - '0');
  }
  if (i == first || value[i] != '\0' || n > max) {
    return false;
  }
  v.value = negative ? -n : n;
  out = v;
  return true;
}

inline bool parse_voc(const char* mode, const char* count, VocCommand& out) noexcept {
  VocCommand v;
  unsigned m = 0;
  while (m < kModeCount && (mode == nullptr || std::strcmp(mode, kModeWords[m]) != 0)) {
    ++m;
  }
  if (m == kModeCount) {
    return false;
  }
  v.mode = m;
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
// `tract <param> <value>`: a parameter of the tract unit (units/tract) by its word (the unit's table order is the id: vowel 0 .. slide 10) and a
// value, a decimal number ("0.5", "-1", "30") or, for CLOSE and SIZE, the name of the choice (open lips tip back; big large normal small tiny).
// The range is the unit's own: the app checks it against the table (param_in_range) and refuses what is outside.
inline constexpr unsigned kTractParamCount = 11;
inline constexpr const char* kTractWords[kTractParamCount] = {"vowel", "tongue", "jaw", "lips", "close", "glide", "size", "res", "hiss", "mix", "slide"};
inline constexpr const char* kTractCloseWords[4] = {"open", "lips", "tip", "back"};
inline constexpr const char* kTractSizeWords[5] = {"big", "large", "normal", "small", "tiny"};
struct TractCommand {
  unsigned id = 0;
  float value = 0.0f;
};
// A plain decimal: an optional '-', digits, an optional '.' and digits, nothing else, at most 8 characters (no exponent, no NaN, no infinity).
inline bool parse_decimal(const char* s, float& out) noexcept {
  if (s == nullptr) {
    return false;
  }
  std::size_t i = 0;
  const bool negative = s[0] == '-';
  if (negative) {
    ++i;
  }
  float whole = 0.0f;
  std::size_t digits = 0;
  for (; s[i] >= '0' && s[i] <= '9'; ++i, ++digits) {
    whole = whole * 10.0f + static_cast<float>(s[i] - '0');
  }
  float frac = 0.0f;
  if (s[i] == '.') {
    ++i;
    float scale = 0.1f;
    for (; s[i] >= '0' && s[i] <= '9'; ++i, ++digits) {
      frac += scale * static_cast<float>(s[i] - '0');
      scale *= 0.1f;
    }
  }
  if (digits == 0 || s[i] != '\0' || i > 8u) {
    return false;
  }
  out = negative ? -(whole + frac) : whole + frac;
  return true;
}
inline bool parse_tract(const char* name, const char* value, TractCommand& out) noexcept {
  if (name == nullptr || value == nullptr) {
    return false;
  }
  unsigned id = 0;
  while (id < kTractParamCount && std::strcmp(name, kTractWords[id]) != 0) {
    ++id;
  }
  if (id == kTractParamCount) {
    return false;
  }
  TractCommand c;
  c.id = id;
  const bool is_close = id == 4u;
  const bool is_size = id == 6u;
  if (is_close || is_size) {
    const unsigned n = is_close ? 4u : 5u;
    for (unsigned i = 0; i < n; ++i) {
      if (std::strcmp(value, is_close ? kTractCloseWords[i] : kTractSizeWords[i]) == 0) {
        c.value = static_cast<float>(i);
        out = c;
        return true;
      }
    }
  }
  if (!parse_decimal(value, c.value)) {
    return false;
  }
  out = c;
  return true;
}

// The i-th held note: C3 major seventh and up, spread over two octaves (a chord that exercises the carrier's voices).
inline constexpr std::uint8_t kVocChord[kVocMaxNotes] = {48, 52, 55, 59, 60, 64, 67, 71};

// `loop <1-4> <rec|stop|play|halt|mod|car>`: rec arms the recording (it starts at the next bar line), stop ends it at the next bar line,
// play and halt start and stop playback, mod and car set the role, half x1 x2 the speed, fwd and rev the direction, `lvl <dB>` the level
// (a whole number, -24..12).
enum class LoopAct : std::uint8_t { Rec, StopRec, Play, Halt, Clear, Mod, Car, Half, X1, X2, Fwd, Rev, Level };
struct LoopCommand {
  unsigned loop = 0;  // 0..3
  LoopAct act = LoopAct::Rec;
  int db = 0;         // Level only
};
// false (out untouched) for a loop that is not 1..4, a word that is not an action or a missing or extra word (`val` is for lvl only).
inline bool parse_loop(const char* num, const char* act, LoopCommand& out, const char* val = nullptr) noexcept {
  static constexpr struct {
    const char* word;
    LoopAct act;
  } kActs[] = {{"rec", LoopAct::Rec}, {"stop", LoopAct::StopRec}, {"play", LoopAct::Play},
               {"halt", LoopAct::Halt}, {"clear", LoopAct::Clear}, {"mod", LoopAct::Mod}, {"car", LoopAct::Car}, {"half", LoopAct::Half},
               {"x1", LoopAct::X1}, {"x2", LoopAct::X2}, {"fwd", LoopAct::Fwd}, {"rev", LoopAct::Rev}, {"lvl", LoopAct::Level}};
  if (num == nullptr || act == nullptr || num[0] < '1' || num[0] > '4' || num[1] != '\0') {
    return false;
  }
  for (const auto& a : kActs) {
    if (std::strcmp(act, a.word) == 0) {
      int db = 0;
      if (a.act == LoopAct::Level) {
        if (val == nullptr) {
          return false;
        }
        const bool neg = val[0] == '-';
        const char* d = val + (neg ? 1 : 0);
        if (d[0] < '0' || d[0] > '9' || (d[1] != '\0' && (d[1] < '0' || d[1] > '9' || d[2] != '\0'))) {
          return false;
        }
        db = (d[0] - '0') * (d[1] != '\0' ? 10 : 1) + (d[1] != '\0' ? d[1] - '0' : 0);
        db = neg ? -db : db;
        if (db < -24 || db > 12) {
          return false;
        }
      } else if (val != nullptr) {
        return false;
      }
      out.loop = static_cast<unsigned>(num[0] - '1');
      out.act = a.act;
      out.db = db;
      return true;
    }
  }
  return false;
}

// `pat [A-H] [len <n>] | [A-H <step 1-16> note <1-127|rest> | chord <root> <tri|sev|open|pow> [close|open|wide|smooth] | car <follow|synth|noise|s1..s4> |
// lvl <0-127> | slc <0-24>]`: the words after `pat`. Kind and voicing are the numbers of ChordKind and Voicing (chords.h); the root is a MIDI note 1..127.
enum class PatAct : std::uint8_t { Show, Length, Note, Chord, Carrier, Level, Slice };
struct PatCommand {
  unsigned pattern = 0;  // 0..7
  PatAct act = PatAct::Show;
  unsigned step = 0;     // 0..15 (every act but Show and Length)
  unsigned value = 0;    // Length 1..16; Note 0..127 (0 a rest); Chord the root; Carrier 0..6; Level 0..127; Slice 0..24 (0 none)
  unsigned kind = 0;     // Chord: ChordKind
  unsigned voicing = 0;  // Chord: Voicing
};
// false (out untouched) for a pattern that is not A..H (or 1..8), a step that is not 1..16, a word that is not an action, a value out of range
// or a missing or extra word.
inline bool parse_pat(const char* const* w, unsigned n, PatCommand& out) noexcept {
  auto number = [](const char* t, unsigned max, unsigned& v) noexcept {
    if (t == nullptr || t[0] == '\0') {
      return false;
    }
    unsigned acc = 0;
    for (; *t != '\0'; ++t) {
      if (*t < '0' || *t > '9' || acc > max) {
        return false;
      }
      acc = acc * 10u + static_cast<unsigned>(*t - '0');
    }
    if (acc > max) {
      return false;
    }
    v = acc;
    return true;
  };
  auto word = [](const char* t, const char* const* names, unsigned count, unsigned& v) noexcept {
    for (unsigned i = 0; i < count; ++i) {
      if (t != nullptr && std::strcmp(t, names[i]) == 0) {
        v = i;
        return true;
      }
    }
    return false;
  };
  if (w == nullptr || n < 1u || n > 6u || w[0] == nullptr || w[0][1] != '\0') {
    return false;
  }
  PatCommand c;
  const char l = w[0][0];
  if (l >= 'A' && l <= 'H') {
    c.pattern = static_cast<unsigned>(l - 'A');
  } else if (l >= 'a' && l <= 'h') {
    c.pattern = static_cast<unsigned>(l - 'a');
  } else if (l >= '1' && l <= '8') {
    c.pattern = static_cast<unsigned>(l - '1');
  } else {
    return false;
  }
  if (n == 1u) {
    out = c;
    return true;
  }
  if (std::strcmp(w[1], "len") == 0) {
    c.act = PatAct::Length;
    if (n != 3u || !number(w[2], 16u, c.value) || c.value < 1u) {
      return false;
    }
    out = c;
    return true;
  }
  unsigned step = 0;
  if (n < 4u || !number(w[1], 16u, step) || step < 1u) {
    return false;
  }
  c.step = step - 1u;
  static const char* const kCar[] = {"follow", "synth", "noise", "s1", "s2", "s3", "s4"};
  static const char* const kKind[] = {"tri", "sev", "open", "pow"};
  static const char* const kVoice[] = {"close", "open", "wide", "smooth"};
  if (std::strcmp(w[2], "note") == 0 && n == 4u) {
    c.act = PatAct::Note;
    if (std::strcmp(w[3], "rest") == 0) {
      c.value = 0;
    } else if (!number(w[3], 127u, c.value) || c.value < 1u) {
      return false;
    }
  } else if (std::strcmp(w[2], "chord") == 0 && (n == 5u || n == 6u)) {
    c.act = PatAct::Chord;
    if (!number(w[3], 127u, c.value) || c.value < 1u || !word(w[4], kKind, 4u, c.kind) || (n == 6u && !word(w[5], kVoice, 4u, c.voicing))) {
      return false;
    }
  } else if (std::strcmp(w[2], "car") == 0 && n == 4u) {
    c.act = PatAct::Carrier;
    if (!word(w[3], kCar, 7u, c.value)) {
      return false;
    }
  } else if (std::strcmp(w[2], "lvl") == 0 && n == 4u) {
    c.act = PatAct::Level;
    if (!number(w[3], 127u, c.value)) {
      return false;
    }
  } else if (std::strcmp(w[2], "slc") == 0 && n == 4u) {
    c.act = PatAct::Slice;
    if (!number(w[3], 24u, c.value)) {
      return false;
    }
  } else {
    return false;
  }
  out = c;
  return true;
}

}  // namespace o3_voc03er
