// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE EDIT's pure logic (docs/UI-DESIGN.md, "SAMPLE EDIT"): the cut table's edits, the zero-crossing snap, the cursor's time text and the
// arming of the REPLACES warning. Apart from the screen so tests/sample_edit_test.cpp holds them. Nothing here allocates or reads the engine.
//
// The cuts tile the slot at all times: cut 1 starts at 0, each cut starts where the one before ends, and the lengths add up to the slot's length
// (an empty slot has no cuts). Every function below keeps that, and a refusal changes nothing.

#include "overview_logic.h"
#include "sample_logic.h"
#include "sample_rec_logic.h"

#include <o3sdk/core/audio_clock.h>

#include <cmath>
#include <cstdint>

namespace o3_voc03er::sample_edit {

namespace sm = o3_voc03er::sample;

inline constexpr std::uint32_t kMinCutSamples = 64;  // a cut is never shorter than this: INSERT refuses to make a smaller one
inline constexpr std::uint32_t kSnapRadius = 64;     // snap_zero's reach, in samples, each way
inline constexpr unsigned kRateHz = static_cast<unsigned>(o3sdk::core::kRefRateHz);  // the loops' rate (44,100): the time text's unit

// Where the overview picture and the ruler sit: the picture (kPicW = overview::kCols wide) starts at kPicX, inside the rectangle's 2 px border, and the ruler
// under it spans the same columns. ONE map puts a sample position at an x for the picture's marks, the cursor and the ruler: a cut is at the same x on
// all of them (tests/sample_edit_test.cpp).
inline constexpr int kPicX = 28;
inline constexpr unsigned kPicW = overview::kCols;
inline int mark_x(std::uint32_t pos, std::uint32_t length) noexcept { return kPicX + static_cast<int>(overview::x_of(pos, length, kPicW)); }

// What an edit did: `ok`, or why it did not (a static string the screen says); `pos` is where it landed (the new cut's start).
struct Edit {
  bool ok = false;
  const char* why = nullptr;
  std::uint32_t pos = 0;
};

// The cut that holds sample `pos` (0-based), or -1 when pos is outside the slot.
inline int cut_at(const sm::SliceTable& t, std::uint32_t pos) noexcept {
  for (unsigned i = 0; i < t.count; ++i) {
    if (pos >= t.cut[i].start && pos - t.cut[i].start < t.cut[i].len) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// The start of the nearest cut before `pos` (0 when none is: cut 1 starts at 0).
inline std::uint32_t prev_cut(const sm::SliceTable& t, std::uint32_t pos) noexcept {
  std::uint32_t best = 0;
  for (unsigned i = 0; i < t.count; ++i) {
    if (t.cut[i].start < pos) {
      best = t.cut[i].start;
    }
  }
  return best;
}

// The start of the nearest cut after `pos` (`pos` itself when none is: the cursor stays on the last cut).
inline std::uint32_t next_cut(const sm::SliceTable& t, std::uint32_t pos) noexcept {
  for (unsigned i = 0; i < t.count; ++i) {
    if (t.cut[i].start > pos) {
      return t.cut[i].start;
    }
  }
  return pos;
}

// A cut at `pos`: the cut that holds it is split there. Refused when the table is full or `pos` is less than kMinCutSamples from a cut's start or
// from the slot's end (the end of the last cut), or outside the slot.
inline Edit insert_cut(sm::SliceTable& t, std::uint32_t pos) noexcept {
  Edit r;
  if (t.count >= sm::kMaxSlices) {
    r.why = "NO MORE CUTS";
    return r;
  }
  const int i = cut_at(t, pos);
  if (i < 0) {
    r.why = "TOO CLOSE";
    return r;
  }
  const unsigned at = static_cast<unsigned>(i);
  const std::uint32_t from_start = pos - t.cut[at].start;
  const std::uint32_t to_end = t.cut[at].len - from_start;
  if (from_start < kMinCutSamples || to_end < kMinCutSamples) {
    r.why = "TOO CLOSE";
    return r;
  }
  for (unsigned k = t.count; k > at + 1u; --k) {
    t.cut[k] = t.cut[k - 1u];
  }
  t.cut[at + 1u] = {pos, to_end};
  t.cut[at].len = from_start;
  t.count = static_cast<std::uint8_t>(t.count + 1u);
  r.ok = true;
  r.pos = pos;
  return r;
}

// The cut that starts exactly at `pos` is removed: the cut before it takes its samples. Refused at 0 (cut 1 stays) and where no cut starts.
inline Edit delete_cut(sm::SliceTable& t, std::uint32_t pos) noexcept {
  Edit r;
  if (pos == 0u) {
    r.why = "THE FIRST CUT STAYS";
    return r;
  }
  unsigned at = t.count;
  for (unsigned i = 0; i < t.count; ++i) {
    if (t.cut[i].start == pos) {
      at = i;
      break;
    }
  }
  if (at >= t.count || at == 0u) {
    r.why = "NO CUT HERE";
    return r;
  }
  t.cut[at - 1u].len += t.cut[at].len;
  for (unsigned k = at; k + 1u < t.count; ++k) {
    t.cut[k] = t.cut[k + 1u];
  }
  t.count = static_cast<std::uint8_t>(t.count - 1u);
  r.ok = true;
  r.pos = pos;  // the cursor stays where it was
  return r;
}

// One cut over the whole slot (none for an empty slot).
inline void clear_cuts(sm::SliceTable& t) noexcept {
  if (t.length == 0u) {
    t.count = 0;
    return;
  }
  t.count = 1;
  t.cut[0] = {0, t.length};
}

// The nearest index to `pos` within `radius` where the sign changes (s[i - 1] and s[i] on different sides of zero, zero counting as positive),
// else `pos`. An exact tie goes to the earlier index. Never out of [0, len - 1] (an empty `s` gives 0). Reads only s[0 .. len - 1].
inline std::uint32_t snap_zero(const std::int16_t* s, std::uint32_t len, std::uint32_t pos, std::uint32_t radius = kSnapRadius) noexcept {
  if (s == nullptr || len == 0u) {
    return 0;
  }
  if (pos >= len) {
    pos = len - 1u;
  }
  const auto crosses = [&](std::uint32_t i) { return i >= 1u && i < len && ((s[i - 1u] < 0) != (s[i] < 0)); };
  for (std::uint32_t d = 0; d <= radius; ++d) {
    if (d <= pos && crosses(pos - d)) {
      return pos - d;
    }
    if (d != 0u && crosses(pos + d)) {
      return pos + d;
    }
  }
  return pos;
}

// The cursor `steps` samples on (negative: back), held inside [0, length - 1] (0 for an empty slot).
inline std::uint32_t move_cursor(std::uint32_t pos, int detents, std::uint32_t step, std::uint32_t length) noexcept {
  if (length == 0u) {
    return 0;
  }
  const std::int64_t v = static_cast<std::int64_t>(pos) + static_cast<std::int64_t>(detents) * static_cast<std::int64_t>(step);
  const std::int64_t top = static_cast<std::int64_t>(length) - 1;
  return static_cast<std::uint32_t>(v < 0 ? 0 : (v > top ? top : v));
}

// With SHIFT held the encoder moves a 64th of the slot a detent (at least one sample).
constexpr std::uint32_t coarse_step(std::uint32_t length) noexcept { return length / 64u > 0u ? length / 64u : 1u; }

// A cursor kept inside a slot whose length changed.
constexpr std::uint32_t clamp_cursor(std::uint32_t pos, std::uint32_t length) noexcept {
  return length == 0u ? 0u : (pos < length ? pos : length - 1u);
}

// "1.234": the sample `pos` as seconds with three decimals (rounded to the millisecond), integer arithmetic. Fits char[12] for any 32-bit pos.
inline void time_text(std::uint32_t pos, char (&out)[12], unsigned rate = kRateHz) noexcept {
  std::uint64_t secs = pos / rate;
  std::uint64_t ms = (static_cast<std::uint64_t>(pos % rate) * 1000u + rate / 2u) / rate;
  if (ms >= 1000u) {
    ++secs;
    ms -= 1000u;
  }
  char digits[8];
  unsigned n = 0;
  do {
    digits[n++] = static_cast<char>('0' + secs % 10u);
    secs /= 10u;
  } while (secs != 0u && n < sizeof digits);
  unsigned o = 0;
  while (n != 0u) {
    out[o++] = digits[--n];
  }
  out[o++] = '.';
  out[o++] = static_cast<char>('0' + ms / 100u);
  out[o++] = static_cast<char>('0' + ms / 10u % 10u);
  out[o++] = static_cast<char>('0' + ms % 10u);
  out[o] = '\0';
}

// ---- CUT EVEN (F1): OFF, 8, 16, 24 ------------------------------------------------------------------------------------------------------

// The values in the order the key shows them, and the code a slot keeps (0 = 8, the default a take starts from, so a zeroed table is right).
inline constexpr unsigned kEvenCodes = 4;
constexpr unsigned even_cuts_of(unsigned code) noexcept {
  constexpr unsigned kN[kEvenCodes] = {8, 16, 24, 0};  // 0: OFF, the cuts are the hand's
  return kN[code % kEvenCodes];
}
constexpr const char* even_name(unsigned code) noexcept {
  constexpr const char* kName[kEvenCodes] = {"8", "16", "24", "OFF"};
  return kName[code % kEvenCodes];
}
// The code `delta` steps on in the order OFF, 8, 16, 24: a detent stops at the ends, a tap wraps.
constexpr unsigned step_even(unsigned code, int delta, bool wrap) noexcept {
  const int pos = static_cast<int>((code + 1u) % kEvenCodes);  // OFF 0, 8 1, 16 2, 24 3
  int v = pos + delta;
  const int n = static_cast<int>(kEvenCodes);
  v = wrap ? ((v % n + n) % n) : (v < 0 ? 0 : (v >= n ? n - 1 : v));
  return (static_cast<unsigned>(v) + kEvenCodes - 1u) % kEvenCodes;
}

// ---- CUT BEATS (F2): OFF, 4, 8, 16 a bar ------------------------------------------------------------------------------------------------

inline constexpr unsigned kBeatsCodes = 4;
constexpr unsigned beats_per_bar(unsigned code) noexcept {
  constexpr unsigned kN[kBeatsCodes] = {0, 4, 8, 16};  // 0: OFF, the cuts are not the bar's
  return kN[code % kBeatsCodes];
}
constexpr const char* beats_name(unsigned code) noexcept {
  constexpr const char* kName[kBeatsCodes] = {"OFF", "4", "8", "16"};
  return kName[code % kBeatsCodes];
}
// The code `delta` steps on in the order OFF, 4, 8, 16: a detent stops at the ends, a tap wraps.
constexpr unsigned step_beats(unsigned code, int delta, bool wrap) noexcept {
  int v = static_cast<int>(code % kBeatsCodes) + delta;
  const int n = static_cast<int>(kBeatsCodes);
  v = wrap ? ((v % n + n) % n) : (v < 0 ? 0 : (v >= n ? n - 1 : v));
  return static_cast<unsigned>(v);
}

// Nothing records that a slot came from a take (a loaded WAV is just samples), so a slot counts as a take when its length is a whole number of bars at
// the current tempo, as SAMPLE shows it ("2.0 BARS": sample_rec::tenths_bars). A take at another tempo or a WAV that is not whole bars reads as a WAV.
inline bool is_take(std::uint32_t length, float bar) noexcept {
  const unsigned t = sample_rec::tenths_bars(length, bar);
  return t >= 10u && t % 10u == 0u;
}

struct BeatCut {
  unsigned beats = 0;      // grid steps the slot holds (whole beats; a take of a little less than a bar rounds up)
  unsigned count = 0;      // cuts made
  bool capped = false;     // beats passed kMaxSlices (24): only the first 24 steps are cut and the last cut runs on to the slot's end
};

// `per_bar` cuts a bar (4, 8 or 16) of a slot of `length` samples, a grid step being bar / per_bar; the last cut ends at the slot's end, so the cuts
// tile it. Refuses (count 0, the table untouched) an empty slot, a bar under one grid step, or a step shorter than kMinCutSamples.
inline BeatCut beat_cuts(sm::SliceTable& t, std::uint32_t length, float bar, unsigned per_bar) noexcept {
  BeatCut r;
  if (length == 0u || per_bar == 0u || !(bar >= 1.0f)) {
    return r;
  }
  const float step = bar / static_cast<float>(per_bar);
  if (step < static_cast<float>(kMinCutSamples)) {
    return r;
  }
  const float want = static_cast<float>(length) / step + 0.05f;
  unsigned n = want >= 4096.0f ? 4096u : static_cast<unsigned>(want);
  if (n == 0u) {
    n = 1u;
  }
  r.beats = n;
  if (n > sm::kMaxSlices) {
    n = sm::kMaxSlices;
    r.capped = true;
  }
  t.length = length;
  t.count = static_cast<std::uint8_t>(n);
  std::uint32_t a = 0;
  for (unsigned i = 0; i < n; ++i) {
    std::uint32_t b = length;
    if (i + 1u < n) {
      const float f = step * static_cast<float>(i + 1u) + 0.5f;
      b = f >= static_cast<float>(length) ? length : static_cast<std::uint32_t>(f);
    }
    if (b <= a) {  // never with step >= kMinCutSamples and n steps inside the length, but a table must tile
      b = a + 1u;
    }
    t.cut[i] = {a, b - a};
    a = b;
  }
  t.cut[n - 1u].len = length - t.cut[n - 1u].start;
  r.count = n;
  return r;
}

// ---- NORMALISE (F6): the peak to -1 dBFS, in slices ----------------------------------------------------------------------------------------

inline constexpr std::int32_t kNormTarget = 29204;       // -1 dBFS of 32767: 32767 * 10^(-1/20)
inline constexpr std::uint32_t kNormSlice = 4096;        // samples one norm_step reads or writes (a frame's share of the main loop)

enum class NormPhase : std::uint8_t { Idle, Scan, Apply, Done, Silent };

// A run: Scan finds the peak over the slot, then Apply scales every sample by `gain`. Zeroed is Idle (the SDRAM's start state).
struct Norm {
  NormPhase phase = NormPhase::Idle;
  std::uint32_t pos = 0, len = 0;
  std::int32_t peak = 0;
  float gain = 0.0f;  // set by the scan; zero so a Norm can live in the SDRAM (.bss)
};

inline void norm_begin(Norm& n, std::uint32_t length) noexcept {
  n = Norm{};
  n.len = length;
  n.phase = length == 0u ? NormPhase::Silent : NormPhase::Scan;
}

// One slice of at most kNormSlice samples of data[0, len); true once the run has ended (Done or Silent). Calling it again after that changes nothing.
inline bool norm_step(Norm& n, std::int16_t* data) noexcept {
  if (n.phase == NormPhase::Scan) {
    std::uint32_t stop = n.pos + kNormSlice;
    if (stop > n.len) {
      stop = n.len;
    }
    for (; n.pos < stop; ++n.pos) {
      const std::int32_t v = data[n.pos];
      const std::int32_t a = v < 0 ? -v : v;
      if (a > n.peak) {
        n.peak = a;
      }
    }
    if (n.pos >= n.len) {
      if (n.peak == 0) {
        n.phase = NormPhase::Silent;
      } else {
        n.gain = static_cast<float>(kNormTarget) / static_cast<float>(n.peak);
        n.pos = 0;
        n.phase = NormPhase::Apply;
      }
    }
  } else if (n.phase == NormPhase::Apply) {
    std::uint32_t stop = n.pos + kNormSlice;
    if (stop > n.len) {
      stop = n.len;
    }
    for (; n.pos < stop; ++n.pos) {
      const float x = static_cast<float>(data[n.pos]) * n.gain;
      std::int32_t v = static_cast<std::int32_t>(x + (x >= 0.0f ? 0.5f : -0.5f));
      v = v > 32767 ? 32767 : (v < -32767 ? -32767 : v);
      data[n.pos] = static_cast<std::int16_t>(v);
    }
    if (n.pos >= n.len) {
      n.phase = NormPhase::Done;
    }
  }
  return n.phase == NormPhase::Done || n.phase == NormPhase::Silent;
}

// 0..100: the scan is the first half, the apply the second.
constexpr unsigned norm_percent(const Norm& n) noexcept {
  if (n.len == 0u) {
    return 0;
  }
  const std::uint64_t half = static_cast<std::uint64_t>(n.pos) * 50u / n.len;
  switch (n.phase) {
    case NormPhase::Scan: return static_cast<unsigned>(half);
    case NormPhase::Apply: return 50u + static_cast<unsigned>(half);
    case NormPhase::Done: return 100u;
    default: return 0;
  }
}

// The gain in tenths of a dB, rounded (negative: a loud slot brought down), 0 for a gain of 0 or less.
inline int gain_tenths_db(float gain) noexcept {
  if (!(gain > 0.0f)) {
    return 0;
  }
  const float t = 200.0f * std::log10(gain);
  return static_cast<int>(t + (t >= 0.0f ? 0.5f : -0.5f));
}

// ---- the confirmations: F6 and SHIFT + DELETE ask, F1 confirms ---------------------------------------------------------------------------

enum class Pending : std::uint8_t { None, Normalise, Empty };  // zeroed is None

// ---- the REPLACES warning ---------------------------------------------------------------------------------------------------------------

// F1 (CUT EVEN) and F8 (CLR CUTS) replace the table. When the table holds the hand's work (`edited`), the first attempt on a key only warns and a
// second one on the same key takes it; the arm then stays open for that key, so turning F1 through 8, 16 and 24 warns once. Anything else
// (the cursor, the slot, another key) closes it. Without `edited` there is no warning (the arm still opens, for the next step to come).
struct Arm {
  std::uint8_t key1 = 0;  // the F key it is open for, plus one; 0 closed (so a zeroed one, as in the SDRAM, is closed)
};
enum class Gate : std::uint8_t { Go, Warn };

inline Gate arm_step(Arm& a, unsigned key, bool edited) noexcept {
  if (a.key1 == key + 1u) {
    return Gate::Go;
  }
  a.key1 = static_cast<std::uint8_t>(key + 1u);
  return edited ? Gate::Warn : Gate::Go;
}
inline void arm_cancel(Arm& a) noexcept { a.key1 = 0; }

}  // namespace o3_voc03er::sample_edit
