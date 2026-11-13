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
#include <cstring>

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

// ---- CUT HITS (F3): an onset detector over 256-sample hop energies --------------------------------------------------------------------------

inline constexpr std::uint32_t kHop = 256;                 // samples a hop energy covers (5.8 ms)
inline constexpr unsigned kMaxHops = 2756;                 // the table's size a slot: 2,756 x 256 = 705,536 samples of the slot's 705,600 (the last 64 are not scanned)
inline constexpr std::uint32_t kHopSlice = 4096;           // samples one hop_scan_step reads (16 hops: a frame's share of the main loop)
inline constexpr unsigned kHitMeanHops = 16;               // the running mean: the 16 hops before a hop (93 ms)
inline constexpr unsigned kHitGapHops = 9;                 // 50 ms apart at least: 9 x 256 samples = 52 ms at 44.1 kHz
inline constexpr float kHitRise = 1.25f;                   // a hit is louder by this than two hops before it, the sustain of a burst is not
inline constexpr unsigned kHitFloor = 32;                  // a hop under this mean of |sample| (-60 dBFS) is never a hit: silence and the noise floor give none
inline constexpr unsigned kHitsMax = 100;                  // F3's value: 0 off, 1..100 the sensitivity
inline constexpr unsigned kHitCandidates = kMaxHops / kHitGapHops + 2u;  // most hits a table can hold 50 ms apart
inline constexpr unsigned kHitKeep = sm::kMaxSlices - 1u;  // cut 1 is always at sample 0, so at most 23 hits make the other cuts of the 24

constexpr unsigned hop_count(std::uint32_t length) noexcept {
  const std::uint32_t n = (length + kHop - 1u) / kHop;
  return n > kMaxHops ? kMaxHops : static_cast<unsigned>(n);
}

// The scan: one pass over the slot, kHopSlice samples at a time, the mean of |sample| of each hop (the last hop of a slot is as long as what is left).
struct HopScan {
  bool on = false;
  std::uint32_t pos = 0, end = 0;  // the next hop's first sample; the end of what is scanned
};
inline void hop_scan_begin(HopScan& s, std::uint32_t length) noexcept {
  s.end = length < kMaxHops * kHop ? length : kMaxHops * kHop;
  s.pos = 0;
  s.on = s.end != 0u;
}
// One slice of data[0, end) into e[0, hop_count); true once the scan is over (also when it was not on). 0..65535 holds the mean (|sample| <= 32768).
inline bool hop_scan_step(HopScan& s, const std::int16_t* data, std::uint16_t* e) noexcept {
  if (!s.on) {
    return true;
  }
  std::uint32_t stop = s.pos + kHopSlice;
  if (stop > s.end) {
    stop = s.end;
  }
  while (s.pos < stop) {
    const std::uint32_t n = s.end - s.pos < kHop ? s.end - s.pos : kHop;
    std::uint32_t sum = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
      const std::int32_t v = data[s.pos + i];
      sum += static_cast<std::uint32_t>(v < 0 ? -v : v);
    }
    const std::uint32_t m = sum / n;
    e[s.pos / kHop] = static_cast<std::uint16_t>(m > 65535u ? 65535u : m);
    s.pos += n;
  }
  if (s.pos >= s.end) {
    s.on = false;
  }
  return !s.on;
}
constexpr unsigned hop_scan_percent(const HopScan& s) noexcept {
  return s.end == 0u ? 0u : static_cast<unsigned>(static_cast<std::uint64_t>(s.pos) * 100u / s.end);
}

// The sensitivity (1..100) as the two tests a hop must pass. RATIO: the hop is above the running mean by this factor, 4.0 at 1 to 1.5 at 100 (linear
// in the factor). LEVEL: the hop is at least this fraction of the loudest hop of the slot, 0.60 at 1 to 0.02 at 100 (linear): the ratio alone finds
// a soft hit after silence as easily as a loud one, so the level is what makes 1 keep only the loudest and 100 the softest. Both loosen together.
constexpr float hit_ratio(unsigned sens) noexcept {
  const unsigned s = sens < 1u ? 1u : (sens > kHitsMax ? kHitsMax : sens);
  return 4.0f - 2.5f * static_cast<float>(s - 1u) / 99.0f;
}
constexpr float hit_level(unsigned sens) noexcept {
  const unsigned s = sens < 1u ? 1u : (sens > kHitsMax ? kHitsMax : sens);
  return 0.60f - 0.58f * static_cast<float>(s - 1u) / 99.0f;
}

struct HitCut {
  unsigned hits = 0;    // hops taken as hits (cuts made, less the first); 0: the table is untouched
  unsigned count = 0;   // cuts made
  bool capped = false;  // more hits than the 24 cuts hold: the weakest went
};

// The hits of e[0, hops) (hop_scan_step's energies; `length` the slot's) at `sens` as cuts of `t`: cut 1 at sample 0, then one at the first hop of each
// hit, in time order. A hop is a hit when its energy passes the level and the ratio of hit_ratio / hit_level against the mean of the kHitMeanHops
// before it, still rises (kHitRise), and it is at least kHitGapHops after the last hit (the first one to cross wins; the hit's strength is the loudest hop in the gap after
// it). Hop 0 is never a hit (cut 1 is there); a hit closer than kMinCutSamples to the end is not one. Over 24 cuts the weakest hits go and `capped`
// is set. Refuses (hits 0, the table untouched) a slot with no hits, sens 0, or fewer than two hops.
inline HitCut hit_cuts(sm::SliceTable& t, const std::uint16_t* e, unsigned hops, std::uint32_t length, unsigned sens) noexcept {
  HitCut r;
  if (sens == 0u || hops < 2u || length == 0u) {
    return r;
  }
  unsigned gmax = 0;
  for (unsigned i = 0; i < hops; ++i) {
    gmax = e[i] > gmax ? e[i] : gmax;
  }
  if (gmax < kHitFloor) {
    return r;
  }
  const float ratio = hit_ratio(sens);
  float level = hit_level(sens) * static_cast<float>(gmax);
  if (level < static_cast<float>(kHitFloor)) {
    level = static_cast<float>(kHitFloor);
  }
  struct Cand {
    std::uint16_t hop, strength;
  };
  Cand cand[kHitCandidates];
  unsigned n = 0;
  std::uint32_t sum = e[0];  // the energies of the hops before hop i, at most kHitMeanHops of them
  unsigned next_ok = 0;
  for (unsigned i = 1; i < hops; ++i) {
    if (static_cast<std::uint32_t>(i) * kHop + kMinCutSamples > length) {
      break;
    }
    const unsigned back = i < kHitMeanHops ? i : kHitMeanHops;
    const float mean = static_cast<float>(sum) / static_cast<float>(back);
    const float v = static_cast<float>(e[i]);
    const float before = static_cast<float>(e[i >= 2u ? i - 2u : 0u]);  // a held sound is above the mean for a while, but it no longer rises
    if (i >= next_ok && n < kHitCandidates && v >= level && v > ratio * mean && v > kHitRise * before) {
      unsigned peak = e[i];
      for (unsigned j = i + 1u; j < hops && j < i + kHitGapHops; ++j) {
        peak = e[j] > peak ? e[j] : peak;
      }
      cand[n++] = {static_cast<std::uint16_t>(i), static_cast<std::uint16_t>(peak)};
      next_ok = i + kHitGapHops;
    }
    sum += e[i];
    if (i >= kHitMeanHops) {
      sum -= e[i - kHitMeanHops];
    }
  }
  if (n == 0u) {
    return r;
  }
  while (n > kHitKeep) {  // drop the weakest (the earliest of equals), keeping time order
    unsigned w = 0;
    for (unsigned k = 1; k < n; ++k) {
      if (cand[k].strength < cand[w].strength) {
        w = k;
      }
    }
    for (unsigned k = w + 1u; k < n; ++k) {
      cand[k - 1u] = cand[k];
    }
    --n;
    r.capped = true;
  }
  t.length = length;
  t.count = static_cast<std::uint8_t>(n + 1u);
  t.cut[0] = {0u, 0u};
  for (unsigned k = 0; k < n; ++k) {
    t.cut[k + 1u].start = static_cast<std::uint32_t>(cand[k].hop) * kHop;
  }
  for (unsigned k = 0; k <= n; ++k) {
    const std::uint32_t end = k < n ? t.cut[k + 1u].start : length;
    t.cut[k].len = end - t.cut[k].start;
  }
  r.hits = n;
  r.count = n + 1u;
  return r;
}

// F3's value `code` (0 off, 1..100) `delta` steps on: a detent moves one and stops at the ends, a second tap steps by 10 (to the next multiple of 10, 100 at
// most; from 0 it lands at 50, a useful start) and wraps from 100 to 0.
constexpr unsigned step_hits(unsigned code, int delta, bool tap) noexcept {
  const unsigned c = code > kHitsMax ? kHitsMax : code;
  if (tap) {
    return c >= kHitsMax ? 0u : (c == 0u ? 50u : (c / 10u + 1u) * 10u);
  }
  const int v = static_cast<int>(c) + delta;
  return v < 0 ? 0u : (v > static_cast<int>(kHitsMax) ? kHitsMax : static_cast<unsigned>(v));
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

// ---- TRIM (F4 START, F5 END) and COPY: the brackets, the cuts after a trim, and the sliced move ---------------------------------------------

inline constexpr std::uint32_t kJobSlice = 8192;  // samples one job_step moves: 16 KiB read and written in the SDRAM, a frame's share of the main loop
inline constexpr std::uint32_t kTrimFade = 64;    // the seam on the new start: Loop::kFadeSamples (the .cpp asserts it)

// The trim's two brackets over a slot: [start, end) is what stays. A slot's are {0, length} until the hand moves one.
struct Brackets {
  std::uint32_t start = 0, end = 0;
};

// A bracket `detents` x `step` samples on; START stays in [0, end - kMinCutSamples], END in [start + kMinCutSamples, length]. A slot too short for two
// brackets that far apart keeps them where they are.
inline Brackets bracket_move(Brackets b, bool end_side, int detents, std::uint32_t step, std::uint32_t length) noexcept {
  if (length < 2u * kMinCutSamples || b.end > length || b.end < b.start + kMinCutSamples) {
    return b;
  }
  const std::int64_t d = static_cast<std::int64_t>(detents) * static_cast<std::int64_t>(step);
  if (end_side) {
    const std::int64_t v = static_cast<std::int64_t>(b.end) + d;
    const std::int64_t lo = static_cast<std::int64_t>(b.start) + static_cast<std::int64_t>(kMinCutSamples);
    b.end = static_cast<std::uint32_t>(v < lo ? lo : (v > static_cast<std::int64_t>(length) ? static_cast<std::int64_t>(length) : v));
  } else {
    const std::int64_t v = static_cast<std::int64_t>(b.start) + d;
    const std::int64_t hi = static_cast<std::int64_t>(b.end) - static_cast<std::int64_t>(kMinCutSamples);
    b.start = static_cast<std::uint32_t>(v < 0 ? 0 : (v > hi ? hi : v));
  }
  return b;
}

// A bracket is off its end: F1 (with one focused) would trim.
constexpr bool trim_moved(const Brackets& b, std::uint32_t length) noexcept { return length != 0u && (b.start != 0u || b.end != length); }

// The table after a trim to [start, end): every cut shifted by -start, those wholly outside dropped, the first and last clipped so the cuts tile the
// new length (end - start); at least one stays. `out` is a copy of `in` edited; false (out untouched) for a range that is empty or beyond `in`.
inline bool trim_cuts(const sm::SliceTable& in, std::uint32_t start, std::uint32_t end, sm::SliceTable& out) noexcept {
  if (end <= start || end > in.length) {
    return false;
  }
  sm::SliceTable t;
  t.length = end - start;
  for (unsigned i = 0; i < in.count && t.count < sm::kMaxSlices; ++i) {
    const std::uint32_t a = in.cut[i].start > start ? in.cut[i].start : start;
    const std::uint64_t e_full = static_cast<std::uint64_t>(in.cut[i].start) + in.cut[i].len;
    const std::uint32_t b = e_full < end ? static_cast<std::uint32_t>(e_full) : end;
    if (b <= a) {
      continue;
    }
    t.cut[t.count].start = a - start;
    t.cut[t.count].len = b - a;
    ++t.count;
  }
  if (t.count == 0u) {  // a table that did not tile: one cut over the new length
    sm::even_cuts(t, t.length, 1u);
  }
  out = t;
  return true;
}

// COPY's target: the first slot after `from` (wrapping, never `from`) that is not `used`; kReaders when every other slot is.
inline unsigned next_empty_slot(const bool (&used)[sm::kReaders], unsigned from) noexcept {
  for (unsigned k = 1; k < sm::kReaders; ++k) {
    const unsigned j = (from + k) % sm::kReaders;
    if (!used[j]) {
      return j;
    }
  }
  return sm::kReaders;
}

enum class JobPhase : std::uint8_t { Idle, Move, Fade, Done };  // zeroed is Idle (the SDRAM's start state)

// A slice cursor for both jobs: Move copies `count` samples from src to dst a kJobSlice at a time, then Fade ramps the first `fade` samples of dst in
// from zero (a trim's seam; COPY has none). src may overlap dst when dst is below it (the trim: memmove per slice, ascending, never writes what is
// still to be read).
struct Job {
  JobPhase phase = JobPhase::Idle;
  std::uint32_t pos = 0, count = 0, fade = 0;
};

inline void job_begin(Job& j, std::uint32_t count, bool fade_in) noexcept {
  j = Job{};
  j.count = count;
  j.fade = fade_in ? (count / 2u < kTrimFade ? count / 2u : kTrimFade) : 0u;
  j.phase = count == 0u ? JobPhase::Done : JobPhase::Move;
}

// One slice; true once the job is done. Calling it again after that changes nothing.
inline bool job_step(Job& j, std::int16_t* dst, const std::int16_t* src) noexcept {
  if (j.phase == JobPhase::Move) {
    std::uint32_t n = j.count - j.pos;
    n = n < kJobSlice ? n : kJobSlice;
    std::memmove(dst + j.pos, src + j.pos, static_cast<std::size_t>(n) * sizeof(std::int16_t));
    j.pos += n;
    if (j.pos >= j.count) {
      j.phase = j.fade != 0u ? JobPhase::Fade : JobPhase::Done;
    }
  } else if (j.phase == JobPhase::Fade) {
    const float step = 1.0f / static_cast<float>(j.fade);
    for (std::uint32_t k = 0; k < j.fade; ++k) {
      dst[k] = static_cast<std::int16_t>(static_cast<float>(dst[k]) * (static_cast<float>(k) * step));
    }
    j.phase = JobPhase::Done;
  }
  return j.phase == JobPhase::Done;
}

// 0..100 of the move.
constexpr unsigned job_percent(const Job& j) noexcept {
  return j.phase == JobPhase::Done ? 100u : (j.count == 0u ? 0u : static_cast<unsigned>(static_cast<std::uint64_t>(j.pos) * 100u / j.count));
}

// ---- SE1: a slice picked on its pad, its start placed by the encoder, the picture zoomed on it -----------------------------------------------

inline constexpr std::uint32_t kSliceStep = 32;               // samples a detent moves a slice start (about a pixel of the zoom); the kit's acceleration multiplies the detents
inline constexpr std::uint32_t kSliceSnap = kSnapRadius;      // how far past the target the quiet-crossing search looks, in the direction of the move
inline constexpr std::uint32_t kZoomSpan = kRateHz / 2u;      // the most samples one zoom rebuild reads (about): half a second, the old fixed window
inline constexpr unsigned kZoomColW = 4;                      // px a zoom column is wide: 186 columns, 2 words each in the display list
inline constexpr unsigned kZoomCols = kPicW / kZoomColW;
static_assert(kPicW % kZoomColW == 0u, "the zoom columns tile the picture");

// The sample in [from, from + radius] (towards the end when `forward`, else towards the start, never past [1, len - 1]) where the signal crosses zero
// most quietly: s[i - 1] and s[i] on different sides of zero (zero counting as positive), and the larger of the two |values| the smallest; a tie goes to the
// one nearest `from`. `from` itself when none is there. A search that starts at the target and goes the way the encoder turns never lands behind the
// target, so the start cannot stick on a crossing (tests/sample_edit_test.cpp).
inline std::uint32_t snap_quiet(const std::int16_t* s, std::uint32_t len, std::uint32_t from, bool forward, std::uint32_t radius = kSliceSnap) noexcept {
  if (s == nullptr || len < 2u) {
    return from;
  }
  std::uint32_t best = from;
  std::int32_t best_mag = INT32_MAX;
  for (std::uint32_t d = 0; d <= radius; ++d) {
    if (!forward && d > from) {
      break;
    }
    const std::uint32_t i = forward ? from + d : from - d;
    if (i < 1u || i >= len) {
      if (forward) {
        break;
      }
      continue;
    }
    if ((s[i - 1u] < 0) == (s[i] < 0)) {
      continue;
    }
    const std::int32_t a = s[i - 1u] < 0 ? -static_cast<std::int32_t>(s[i - 1u]) : s[i - 1u];
    const std::int32_t b = s[i] < 0 ? -static_cast<std::int32_t>(s[i]) : s[i];
    const std::int32_t mag = a > b ? a : b;
    if (mag < best_mag) {
      best_mag = mag;
      best = i;
    }
  }
  return best;
}

// Where a slice start may be: [lo, hi] samples. Slice i (> 0) stays kMinCutSamples clear of the cut before it and of the cut after it (the slot's end for the
// last one); slice 0 is the trim START, so it starts at 0 and stops short of cut 2 and of `cap` (the END bracket) by the same margin. False when there is no room.
inline bool slice_limits(const sm::SliceTable& t, unsigned i, std::uint32_t cap, std::uint32_t& lo, std::uint32_t& hi) noexcept {
  lo = hi = 0;
  if (i >= t.count) {
    return false;
  }
  std::uint32_t top = i + 1u < t.count ? t.cut[i + 1u].start : t.length;
  if (i == 0u && cap < top) {
    top = cap;
  }
  if (top < kMinCutSamples) {
    return false;
  }
  hi = top - kMinCutSamples;
  lo = i == 0u ? 0u : t.cut[i - 1u].start + kMinCutSamples;
  return lo <= hi;
}

// A start `detents` on, `step` samples a detent, snapped to the quietest zero crossing ahead of the target (the way it moves) when `snap`. Held inside
// [lo, hi]; a target the clamp moved is not snapped. `s` nullptr: no snap. A `pos` already outside [lo, hi] (the START bracket F4 left past cut 2) is not
// thrown back to the edge: the window widens to take it in, so a detent away from the range stays put and one towards it walks in by steps (0 detents
// alone brings it into the range).
inline std::uint32_t slice_move_step(const std::int16_t* s, std::uint32_t len, std::uint32_t pos, int detents, std::uint32_t step, bool snap, std::uint32_t lo,
                                     std::uint32_t hi) noexcept {
  if (hi < lo || detents == 0) {
    return pos < lo ? lo : (pos > hi ? hi : pos);
  }
  const std::uint32_t lo2 = pos < lo ? pos : lo;
  const std::uint32_t hi2 = pos > hi ? pos : hi;
  const std::int64_t want = static_cast<std::int64_t>(pos) + static_cast<std::int64_t>(detents) * static_cast<std::int64_t>(step == 0u ? 1u : step);
  if (want <= static_cast<std::int64_t>(lo2)) {
    return lo2;
  }
  if (want >= static_cast<std::int64_t>(hi2)) {
    return hi2;
  }
  std::uint32_t at = static_cast<std::uint32_t>(want);
  if (snap) {
    at = snap_quiet(s, len, at, detents > 0);
    at = at < lo2 ? lo2 : (at > hi2 ? hi2 : at);
  }
  return at;
}

// The SE1 step: kSliceStep a detent and snapped, or one sample a detent and no snap when `fine` (SHIFT).
inline std::uint32_t slice_move(const std::int16_t* s, std::uint32_t len, std::uint32_t pos, int detents, bool fine, std::uint32_t lo, std::uint32_t hi) noexcept {
  return slice_move_step(s, len, pos, detents, fine ? 1u : kSliceStep, !fine, lo, hi);
}

// Cut `i` (> 0) starts at `pos`: the cut before it ends there and this one ends where it did. Refused (false, nothing changed) for cut 0, a cut past the
// count, or a `pos` outside slice_limits. The table keeps tiling.
inline bool set_slice_start(sm::SliceTable& t, unsigned i, std::uint32_t pos) noexcept {
  std::uint32_t lo = 0, hi = 0;
  if (i == 0u || !slice_limits(t, i, t.length, lo, hi) || pos < lo || pos > hi) {
    return false;
  }
  const std::uint32_t end = t.cut[i].start + t.cut[i].len;
  t.cut[i - 1u].len = pos - t.cut[i - 1u].start;
  t.cut[i].start = pos;
  t.cut[i].len = end - pos;
  return true;
}

// ---- SE2: the zoom ladder, the window, the stride build ---------------------------------------------------------------------------------

// The zoom rungs, widest first: rung 0 is the whole slot (the overview picture); each rung after it halves the span, 4 s down to 344 samples (7.8 ms) across,
// about a pixel to every second sample. A rung at least as wide as the slot is the whole slot as well (zoom_step skips it).
inline constexpr unsigned kZoomRungs = 11;
inline constexpr std::uint32_t kZoomRung[kZoomRungs] = {0u, 176400u, 88200u, 44100u, 22050u, 11025u, 5512u, 2756u, 1378u, 689u, 344u};
static_assert(kZoomRung[kZoomRungs - 1u] >= kZoomCols, "the deepest zoom has a sample for every column");
inline constexpr std::uint32_t kZoomReadCol = kZoomSpan / kZoomCols + 1u;  // the most samples a column reads: a wider one reads every `stride`-th sample
inline constexpr std::uint32_t kSnapMinStep = 24;                          // under this many samples a detent the quiet-crossing snap would be bigger than the step

// The span in samples of `rung` over a slot of `len` samples: the slot itself for rung 0 or a rung at least as wide as the slot.
constexpr std::uint32_t zoom_span(unsigned rung, std::uint32_t len) noexcept {
  return rung == 0u || rung >= kZoomRungs || kZoomRung[rung] >= len ? len : kZoomRung[rung];
}

// True when `rung` shows the slot whole.
constexpr bool zoom_whole(unsigned rung, std::uint32_t len) noexcept { return zoom_span(rung, len) >= len; }

// The rung one step in (dir > 0) or out (dir < 0): in skips the rungs that are as wide as the slot and stays where it is at the deepest (or when the slot is
// shorter than the deepest rung); out goes back to the whole slot from the first real rung.
constexpr unsigned zoom_step(unsigned rung, std::uint32_t len, int dir) noexcept {
  if (rung >= kZoomRungs) {
    rung = kZoomRungs - 1u;
  }
  if (dir > 0) {
    if (zoom_whole(rung, len)) {
      rung = 0;
    }
    unsigned r = rung + 1u;
    while (r < kZoomRungs && kZoomRung[r] >= len) {
      ++r;
    }
    return r < kZoomRungs ? r : rung;
  }
  if (dir < 0) {
    if (rung <= 1u) {
      return 0;
    }
    return kZoomRung[rung - 1u] >= len ? 0u : rung - 1u;
  }
  return rung;
}

// The window's first sample for a view `span` samples wide centred on `centre`: held inside the slot ([0, len - span]); 0 for the whole slot.
constexpr std::uint32_t zoom_lo_for(std::uint32_t centre, std::uint32_t span, std::uint32_t len) noexcept {
  if (span >= len) {
    return 0;
  }
  const std::uint32_t half = span / 2u;
  const std::uint32_t lo = centre > half ? centre - half : 0u;
  return lo > len - span ? len - span : lo;
}

// x (0 .. kPicW - 1) of `pos` in the window [lo, lo + span); -1 when `pos` is outside it.
constexpr int zoom_x(std::uint32_t pos, std::uint32_t lo, std::uint32_t span) noexcept {
  if (span == 0u || pos < lo || pos - lo >= span) {
    return -1;
  }
  return static_cast<int>(static_cast<std::uint64_t>(pos - lo) * kPicW / span);
}

// The samples a picture pixel stands for at this zoom (the slot's `len` when whole), at least 1: what a detent of the encoder moves, so a detent is about a
// pixel and the deepest zoom steps a single sample.
constexpr std::uint32_t zoom_detent(std::uint32_t span) noexcept { return span / kPicW > 0u ? span / kPicW : 1u; }

// The window's columns: the lowest and highest sample of each (span / kZoomCols samples), from the slot's `s[0 .. len - 1]`. A column wholly outside the slot
// is blank (hi < lo). A column wider than kZoomReadCol samples reads every stride-th one, so a rebuild reads at most about kZoomSpan samples whatever the
// span; never outside the slot.
inline void zoom_build(const std::int16_t* s, std::uint32_t len, std::uint32_t lo, std::uint32_t span, std::int16_t (&mn)[kZoomCols],
                       std::int16_t (&mx)[kZoomCols]) noexcept {
  for (unsigned c = 0; c < kZoomCols; ++c) {
    mn[c] = 1;
    mx[c] = 0;
    if (s == nullptr || span == 0u) {
      continue;
    }
    const std::uint64_t a = static_cast<std::uint64_t>(lo) + static_cast<std::uint64_t>(span) * c / kZoomCols;
    const std::uint64_t b0 = static_cast<std::uint64_t>(lo) + static_cast<std::uint64_t>(span) * (c + 1u) / kZoomCols;
    const std::uint64_t b = b0 > len ? len : b0;
    if (a >= b) {
      continue;
    }
    const std::uint64_t stride = (b - a) / kZoomReadCol + 1u;
    std::int16_t lo_v = INT16_MAX, hi_v = INT16_MIN;
    for (std::uint64_t i = a; i < b; i += stride) {
      const std::int16_t v = s[i];
      lo_v = v < lo_v ? v : lo_v;
      hi_v = v > hi_v ? v : hi_v;
    }
    mn[c] = lo_v;
    mx[c] = hi_v;
  }
}

// The zoom label: WHOLE SLOT, else the span as a time, `ZOOM 4 S` / `ZOOM 0.5 S` from 500 ms up, `ZOOM 125 MS` under it.
inline void zoom_text(unsigned rung, std::uint32_t len, char (&out)[16], unsigned rate = kRateHz) noexcept {
  const std::uint32_t span = zoom_span(rung, len);
  unsigned n = 0;
  const auto put = [&](const char* p) {
    while (*p != '\0' && n + 1u < sizeof out) {
      out[n++] = *p++;
    }
  };
  const auto put_uint = [&](std::uint32_t v) {
    char d[11];
    unsigned k = 0;
    do {
      d[k++] = static_cast<char>('0' + v % 10u);
      v /= 10u;
    } while (v != 0u && k < sizeof d);
    while (k != 0u && n + 1u < sizeof out) {
      out[n++] = d[--k];
    }
  };
  if (rung == 0u || span >= len || rate == 0u) {
    put("WHOLE SLOT");
  } else {
    const std::uint32_t ms = static_cast<std::uint32_t>(static_cast<std::uint64_t>(span) * 1000u / rate);
    put("ZOOM ");
    if (ms >= 500u) {
      const std::uint32_t tenths = (ms + 50u) / 100u;
      put_uint(tenths / 10u);
      if (tenths % 10u != 0u) {
        put(".");
        put_uint(tenths % 10u);
      }
      put(" S");
    } else {
      put_uint(ms);
      put(" MS");
    }
  }
  out[n] = '\0';
}

// The cut nearest `pos` among the table's cuts 1.. (never the one at 0) whose start is inside [lo, hi): its index, or -1 when there is none. A tie goes to
// the earlier. Used by DELETE with no slice picked: the cut the cursor points at in the view.
inline int nearest_cut(const sm::SliceTable& t, std::uint32_t pos, std::uint32_t lo, std::uint32_t hi) noexcept {
  int best = -1;
  std::uint32_t best_d = UINT32_MAX;
  for (unsigned i = 1; i < t.count; ++i) {
    const std::uint32_t a = t.cut[i].start;
    if (a < lo || a >= hi) {
      continue;
    }
    const std::uint32_t d = a > pos ? a - pos : pos - a;
    if (d < best_d) {
      best_d = d;
      best = static_cast<int>(i);
    }
  }
  return best;
}

// Where INSERT splits slice `i` (of `t`, its start as drawn `start`, which is the trim START for slice 0): the middle of it, snapped to a zero crossing,
// kept kMinCutSamples clear of both ends. 0 when the slice is too short to split.
inline std::uint32_t slice_middle(const sm::SliceTable& t, unsigned i, std::uint32_t start, const std::int16_t* s) noexcept {
  if (i >= t.count) {
    return 0;
  }
  const std::uint32_t end = t.cut[i].start + t.cut[i].len;
  if (end < start + 2u * kMinCutSamples) {
    return 0;
  }
  const std::uint32_t mid = start + (end - start) / 2u;
  std::uint32_t at = s != nullptr ? snap_zero(s, t.length, mid) : mid;
  const std::uint32_t lo = start + kMinCutSamples;
  const std::uint32_t hi = end - kMinCutSamples;
  at = at < lo ? lo : (at > hi ? hi : at);
  return at;
}

// ---- the confirmations: F6 and SHIFT + DELETE ask, F1 confirms ---------------------------------------------------------------------------

enum class Pending : std::uint8_t { None, Normalise, Empty, Copy };  // zeroed is None

// ---- the REPLACES warning ---------------------------------------------------------------------------------------------------------------

// F1 (CUT EVEN) and SHIFT + F8 (CLR CUTS) replace the table. When the table holds the hand's work (`edited`), the first attempt on a key only warns and a
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
