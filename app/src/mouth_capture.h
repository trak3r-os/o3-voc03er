// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Capture the mouth (MC1, docs/REVIEW-2026-11-11.md idea 7): a recorded slot (a voice take, or a WAV in a loop slot) is read offline, in the main loop,
// and the vowel the voice was making in each step of the pattern is written into the MOUTH lane, so TRACT "speaks" the phrase with the sequencer.
//
// Per step, two passes, each one small piece of work a call of Capture::step() (the PATTERN screen calls it once per update):
//   LEVEL  the RMS of the step's span of the take (in chunks of kRmsChunk samples). A step more than 30 dB under the take's loudest step is a closure.
//   SHAPE  the vocal tract of the other steps, the way TALK reads it at Quality 16: a window of 1,024 samples centred in the step (256 at fs/4 through the
//          unit's two halfbands), pre-emphasis 0.9, TALK's window (sin^2 rise over 0.8, cos^2 fall), 12 stages of LPC (autocorrelation, r0 * 1.0001, the
//          60 Hz lag window, Levinson-Durbin with |k| <= 0.995), and TALK's reflection-to-area map (Talk::areas_from_reflection: area[0] is the glottis
//          end, the direction of TRACT's diameters). The 13 areas become log-areas, the mean removed (the overall size of an LPC tube means nothing); the
//          nearest of TRACT's nine vowel places (A, A-E ... U: VOWEL k / 8) by squared distance wins.
// The nine references (phase REFS, nine passes, once per sample rate) are what this same analysis reads from TRACT's own output: for each place k the
// extras/tract Processor (the one the TRACT unit wraps, so the app has one source of the vowels) is buzzed by a 130 Hz saw at VOWEL k / 8, settled
// 1,536 frames, and 1,152 frames are read by SHAPE like a take. Why not the tube's diameters, resampled from 22 sections to 13: tried first, and the
// all-pole areas of a 12-stage LPC are not the tube's areas (the lips' radiation and the lossy ends are in the signal), so the nearest of nine by
// area distance found 5 of 20 primary vowels on TRACT's own output. Reading both with the same estimator makes the distance a distance between
// spectral envelopes: 97 of 108 exact and 108 of 108 within one place in the same experiment (4 pitches, +-0.03 of VOWEL, noise to -40 dBFS).
//
// What it writes, per step (code -> MOUTH byte, byte_of): a vowel place 0..8 -> 1..9; a closure (code 9) -> LIPS (10), the lane's closures being
// LIPS, TIP and BACK and a level dip telling none of the three apart; keep (0xFF: a step the LPC could not read, a window under TALK's own gate) ->
// 0, FOLLOW. A take that is silent altogether (the loudest step under -70 dBFS RMS) writes nothing: silent() is true and the screen leaves the lane.
//
// The mapping of the take to steps (plan): a slot of a whole number of bars at the current tempo (as SAMPLE shows it, "2.0 BARS") has 16 steps a
// bar, the first bar filling a pattern of up to 16 steps (the pattern's length counts: LENGTH 8 takes the first half bar); any other slot is spread
// evenly over the pattern's length. A slot shorter than one window (1,024 samples) is refused.
//
// Real-time and memory: main loop only, never the audio path. No heap, float only (no double, no 64-bit conversions), no exceptions. A Capture holds
// about 5.2 KB, all zero at construction (an O3SDK_BIG object goes to the SDRAM, where it is .bss), and begin() builds its tables. A REFS pass puts a
// Processor (under 1.5 KB) and 0.5 KB of buffers on the stack. Cost [est] of a step() pass: a REFS pass is 2,688 tract frames at about 256 cycles
// (0.7M cycles, 4 ms at 180 MHz) plus one SHAPE read; nine of them, only the first capture after boot (or a rate change). A SHAPE pass is about 1,152 samples through two halfbands (about 8k cycles), 13 lags over 256 (3.3k MACs, about 7k cycles), a
// Levinson of 12 (about 0.3k MACs), 13 logs and 9 x 13 distances (about 6k cycles): 20 to 30k cycles, 0.15 ms at 180 MHz; a LEVEL pass is at most
// 4,096 int16 squares (about 10k cycles, 0.06 ms). A 16-step capture is 16 SHAPE and 16 to 170 LEVEL passes, over a fraction of a second of updates.
// A host test is not that measurement: the pass is measured on the unit.

#include <o3_voc03er/stepseq.h>
#include <o3_voc03er/units/vocoder/halfband.h>
#include <o3_voc03er/units/vocoder/talk.h>
#include <o3sdk/extras/tract/processor.h>

#include "sample_rec_logic.h"

#include <cmath>
#include <cstdint>

namespace o3_voc03er::mouthcap {

inline constexpr unsigned kPlaces = 9;                       // TRACT's vowel places: VOWEL k / 8, k = 0..8 (MOUTH values 1..9)
inline constexpr unsigned kStages = 12;                      // Talk::stages_for(16): TALK at Quality 16
inline constexpr unsigned kAreas = kStages + 1;              // areas 0..12, the glottis end first
inline constexpr unsigned kWin = Talk::kWin;                 // 256 samples at fs/4
inline constexpr std::uint32_t kSpan = 4u * kWin;            // the window in input samples: 1,024
inline constexpr std::uint32_t kPreroll = 128;               // input samples the halfbands run before the window (a multiple of 4)
inline constexpr std::uint32_t kChunk = 64;                  // input samples a halfband call takes (a multiple of 4: every call keeps the phase even)
inline constexpr std::uint32_t kRmsChunk = 4096;             // samples of the level pass a step() takes
inline constexpr std::uint32_t kMinLength = kSpan;           // a slot shorter than one window is refused
inline constexpr unsigned kMaxSteps = kPatternSteps;         // the steps a capture analyses: the pattern's
inline constexpr float kClosureRatio = 0.031622777f;         // 10^(-30/20): a step under this fraction of the loudest step's RMS is a closure
inline constexpr float kSilentRms = 3.1622777e-4f;           // -70 dBFS: a take whose loudest step is under it is silent
inline constexpr float kGate = 2.5e-10f;                     // Talk's gate on the window's mean power (talk.cpp kGate): under it the window is silence
inline constexpr float kPreEmph = 0.9f;                      // Talk's pre-emphasis
inline constexpr float kWinRise = 0.8f;                      // Talk's window: sin^2 over this part, cos^2 over the rest
inline constexpr float kR0Boost = 1.0001f;                   // Talk's white-noise correction of r0
inline constexpr float kLagHz = 60.0f;                       // Talk's lag window
inline constexpr float kErrFloor = 1.0e-7f;                  // Talk's floor of the Levinson error, of r0
inline constexpr float kLogClamp = 3.0f;                     // |ln area - mean| limit: the nine shapes reach about 2.5; an LPC tube can run to +-11
inline constexpr float kPi2 = 6.2831853f;
inline constexpr std::uint32_t kRefSettle = 1536;            // the reference tube's frames discarded, then
inline constexpr std::uint32_t kRefKeep = 1152;              //  these kept: the window's kSpan and 128 for the halfbands
inline constexpr float kRefHz = 130.0f;                      // the reference's buzz (a saw)
inline constexpr float kRefAmp = 0.3f;

inline constexpr std::uint8_t kClosure = 9;                  // a step's code: 0..8 a vowel place, 9 a closure,
inline constexpr std::uint8_t kKeep = 0xFF;                  //  or nothing read

// The MOUTH byte of a code.
constexpr std::uint8_t byte_of(std::uint8_t code) noexcept {
  return code < kPlaces ? static_cast<std::uint8_t>(1u + code) : (code == kClosure ? static_cast<std::uint8_t>(kMouthLips) : std::uint8_t{0});
}

// Bars in a slot as SAMPLE shows them (a whole number at the current tempo, `bar` samples), else 0.
inline unsigned bars_of(std::uint32_t length, float bar) noexcept {
  const unsigned t = sample_rec::tenths_bars(length, bar);
  return t >= 10u && t % 10u == 0u ? t / 10u : 0u;
}

struct Plan {
  std::uint32_t total = 0;  // steps the whole slot is cut into
  std::uint32_t used = 0;   // steps analysed: the first `used`, the pattern's length
};

// 16 steps a bar when the slot has bars, else the pattern's length over the whole slot.
inline Plan plan(std::uint32_t length, float bar, unsigned pattern_len) noexcept {
  Plan p;
  const unsigned len = pattern_len < 1u ? 1u : (pattern_len > kMaxSteps ? kMaxSteps : pattern_len);
  const unsigned bars = bars_of(length, bar);
  p.total = bars > 0u ? bars * 16u : len;
  p.used = p.total < len ? p.total : len;
  return p;
}

// A running capture's slot is still the one it started on: idle (not recording) and the same length.
constexpr bool slot_same(bool idle, std::uint32_t length_now, std::uint32_t length_then) noexcept { return idle && length_now == length_then; }

// The first sample of step i of `total` over `length` samples (i = total gives the end). Integer, exact: length = q * total + r, and i * r fits.
constexpr std::uint32_t span_edge(std::uint32_t length, std::uint32_t total, std::uint32_t i) noexcept {
  return total == 0u ? 0u : i * (length / total) + (i * (length % total)) / total;
}

// The nine references: mean-removed, clamped log-areas of what this same analysis reads from TRACT's own output for each vowel place (see the header).
struct Refs {
  alignas(8) float la[kPlaces][kAreas] = {};
};

// log-areas in place: the mean removed, clamped to +-kLogClamp. The same for the references and the measure.
inline void normalise(float (&la)[kAreas]) noexcept {
  float mean = 0.0f;
  for (unsigned j = 0; j < kAreas; ++j) {
    mean += la[j];
  }
  mean *= 1.0f / static_cast<float>(kAreas);
  for (unsigned j = 0; j < kAreas; ++j) {
    const float v = la[j] - mean;
    la[j] = v > kLogClamp ? kLogClamp : (v < -kLogClamp ? -kLogClamp : v);
  }
}

// What the nearest place is: its code (0..8), the squared distance of the best and of the second best (the margin the tests read).
struct Pick {
  std::uint8_t code = 0;
  float dist = 0.0f;
  float next = 0.0f;
};

// The nearest of the nine references to the mean-removed, clamped log-areas `la`.
inline Pick nearest(const Refs& r, const float (&la)[kAreas]) noexcept {
  Pick p;
  float best = 1.0e30f, second = 1.0e30f;
  unsigned at = 0;
  for (unsigned k = 0; k < kPlaces; ++k) {
    float d = 0.0f;
    for (unsigned j = 0; j < kAreas; ++j) {
      const float e = la[j] - r.la[k][j];
      d += e * e;
    }
    if (d < best) {
      second = best;
      best = d;
      at = k;
    } else if (d < second) {
      second = d;
    }
  }
  p.code = static_cast<std::uint8_t>(at);
  p.dist = best;
  p.next = second;
  return p;
}

class Capture {
 public:
  enum class Phase : std::uint8_t { Idle, Refs, Level, Shape, Done };

  // Starts a capture of `mem[0..length)` (mono int16 at `rate` Hz) cut into `total` steps, the first `used` analysed. False (and Idle) for a slot
  // under kMinLength, no steps, or more than kMaxSteps. Builds the window, the lag window and the references: a few hundred sin, cos, log.
  bool begin(const std::int16_t* mem, std::uint32_t length, std::uint32_t total, std::uint32_t used, float rate) noexcept {
    phase_ = Phase::Idle;
    if (mem == nullptr || length < kMinLength || total == 0u || used == 0u || used > total || used > kMaxSteps || !(rate >= 8000.0f)) {
      return false;
    }
    mem_ = mem;
    length_ = length;
    total_ = total;
    used_ = used;
    i_ = 0;
    pos_ = 0;
    acc_ = 0.0f;
    peak_ = 0.0f;
    sum_ = 0.0f;
    mean_ = 0.0f;
    mean_done_ = false;
    silent_ = false;
    const std::uint32_t rise = static_cast<std::uint32_t>(static_cast<float>(kWin) * kWinRise);
    const std::uint32_t fall = kWin - rise;
    float s2 = 0.0f;
    for (std::uint32_t i = 0; i < kWin; ++i) {
      float h;
      if (i < rise) {
        h = std::sin(0.25f * kPi2 * (static_cast<float>(i) + 0.5f) / static_cast<float>(rise));
      } else {
        h = std::cos(0.25f * kPi2 * (static_cast<float>(i - rise) + 0.5f) / static_cast<float>(fall));
      }
      wtab_[i] = h * h;
      s2 += wtab_[i] * wtab_[i];
    }
    sumw2_ = s2;
    const float fl = rate * 0.25f;
    for (unsigned l = 0; l <= kStages; ++l) {
      const float x = kPi2 * kLagHz * static_cast<float>(l) / fl;
      lagw_[l] = std::exp(-0.5f * x * x);
    }
    for (unsigned i = 0; i < kMaxSteps; ++i) {
      pick_[i] = Pick{};
      pick_[i].code = kKeep;
      rms_[i] = 0.0f;
    }
    refs_i_ = 0;
    rate_ = rate;
    with_refs_ = !(refs_ok_ && refs_rate_ == rate);
    phase_ = with_refs_ ? Phase::Refs : Phase::Level;
    return true;
  }

  void cancel() noexcept { phase_ = Phase::Idle; }

  // One piece of work. True while more is left (the phase is Level or Shape afterwards).
  bool step() noexcept {
    if (phase_ == Phase::Refs) {
      refs_step();
    } else if (phase_ == Phase::Level) {
      level_chunk();
    } else if (phase_ == Phase::Shape) {
      shape_step();
    }
    return running();
  }

  Phase phase() const noexcept { return phase_; }
  bool running() const noexcept { return phase_ == Phase::Refs || phase_ == Phase::Level || phase_ == Phase::Shape; }
  bool done() const noexcept { return phase_ == Phase::Done; }
  bool silent() const noexcept { return silent_; }   // Done: the loudest step is under kSilentRms, nothing to write
  unsigned used() const noexcept { return used_; }
  // 0..100: the units of work done (one a reference, a measured step, a read step) over all of them.
  unsigned percent() const noexcept {
    const unsigned nrefs = with_refs_ ? kPlaces : 0u;
    const unsigned all = nrefs + 2u * used_;
    switch (phase_) {
      case Phase::Refs: return (refs_i_ * 100u) / all;
      case Phase::Level: {  // the mean sweep and the RMS sweep: half of the phase's work each, by samples
        const std::uint32_t e = span_edge(length_, total_, used_);
        const float f = e == 0u ? 0.0f : (static_cast<float>(mean_done_ ? e + (i_ < used_ ? span_edge(length_, total_, i_) : e) : pos_) / (2.0f * static_cast<float>(e)));
        return static_cast<unsigned>((static_cast<float>(nrefs) + static_cast<float>(used_) * f) * 100.0f / static_cast<float>(all));
      }
      case Phase::Shape: return ((nrefs + used_ + i_) * 100u) / all;
      case Phase::Done: return 100u;
      case Phase::Idle: break;
    }
    return 0u;
  }
  // The result of step i (< used): the code and the distances. Valid once Done.
  const Pick& pick(unsigned i) const noexcept { return pick_[i < kMaxSteps ? i : kMaxSteps - 1u]; }
  float rms(unsigned i) const noexcept { return rms_[i < kMaxSteps ? i : kMaxSteps - 1u]; }
  float peak_rms() const noexcept { return peak_; }
  const Refs& refs() const noexcept { return refs_; }

  // The window of step `i` read as areas: false for a window under Talk's gate or a Levinson that cannot start. The 13 areas, glottis end first.
  bool areas(const std::int16_t* m, std::uint32_t len, std::uint32_t a, std::uint32_t b, float (&area)[kAreas]) noexcept {
    if (!window(m, len, a, b)) {
      return false;
    }
    float r[kStages + 1];
    for (unsigned l = 0; l <= kStages; ++l) {
      float s = 0.0f;
      for (unsigned i = 0; i + l < kWin; ++i) {
        s += win_[i] * win_[i + l];
      }
      r[l] = s;
    }
    r[0] *= kR0Boost;
    if (!(r[0] / sumw2_ > kGate)) {
      return false;
    }
    for (unsigned l = 1; l <= kStages; ++l) {
      r[l] *= lagw_[l];
    }
    float aa[kStages + 1] = {}, t[kStages + 1] = {}, k[kStages + 2] = {};
    float err = r[0];
    const float floor = r[0] * kErrFloor;
    for (unsigned q = 1; q <= kStages; ++q) {
      if (!(err > floor)) {
        break;
      }
      float s = r[q];
      for (unsigned j = 1; j < q; ++j) {
        s += aa[j] * r[q - j];
      }
      float km = -s / err;
      km = km > Talk::kMaxK ? Talk::kMaxK : (km < -Talk::kMaxK ? -Talk::kMaxK : km);
      for (unsigned j = 1; j < q; ++j) {
        t[j] = aa[j] + km * aa[q - j];
      }
      for (unsigned j = 1; j < q; ++j) {
        aa[j] = t[j];
      }
      aa[q] = km;
      k[q] = km;
      err *= 1.0f - km * km;
    }
    Talk::areas_from_reflection(k, kStages, area);
    return true;
  }

 private:
  // Fills win_ with the pre-emphasised, windowed, fs/4 samples around the span [a, b): kSpan input samples centred in it (clamped inside the slot),
  // from an even start, after up to kPreroll samples that warm the halfbands up. Every call of a halfband gets a multiple of 4 samples, so the
  // phase flag stays false and the output counts stay exact.
  bool window(const std::int16_t* m, std::uint32_t len, std::uint32_t a, std::uint32_t b) noexcept {
    const std::uint32_t centre = a + (b > a ? (b - a) / 2u : 0u);
    std::uint32_t start = centre > kSpan / 2u ? centre - kSpan / 2u : 0u;
    if (start > len - kSpan) {
      start = len - kSpan;
    }
    start &= ~1u;
    const std::uint32_t pre = (start < kPreroll ? start : kPreroll) & ~3u;
    const std::uint32_t skip = pre / 4u;
    const std::uint32_t end = start + kSpan;
    Halfband h0, h1;
    float prev = 0.0f;
    std::uint32_t seen = 0, w = 0;
    for (std::uint32_t pos = start - pre; pos < end;) {
      const std::uint32_t n = end - pos < kChunk ? end - pos : kChunk;
      float x[kChunk], y[kChunk / 2u], z[kChunk / 4u];
      for (std::uint32_t j = 0; j < n; ++j) {
        x[j] = static_cast<float>(m[pos + j]) * (1.0f / 32768.0f);
      }
      const std::uint32_t n1 = decimate(h0, x, n, false, y);
      const std::uint32_t n2 = decimate(h1, y, n1, false, z);
      for (std::uint32_t j = 0; j < n2; ++j) {
        const float e = z[j] - kPreEmph * prev;
        prev = z[j];
        if (seen++ >= skip && w < kWin) {
          win_[w] = e * wtab_[w];
          ++w;
        }
      }
      pos += n;
    }
    return w == kWin;
  }

  // One reference: the tract at vowel place refs_i_ buzzed by a saw, read by areas() like any take. kRefSettle frames settle the tube (its glide, its
  // ringing; the level trim is slower but does not change the shape), then kRefKeep are kept as the "take". The Processor lives on the stack for the pass.
  void refs_step() noexcept {
    namespace tr = o3sdk::extras::tract;
    tr::Processor p(tr::Processor::Quality::Full);
    p.prepare(rate_);
    p.set_mix(1.0f);
    p.set_glide_ms(5.0f);
    p.set_vowel(static_cast<float>(refs_i_) * (1.0f / 8.0f));
    float in[kChunk], out[kChunk];
    float phase = 0.0f;
    for (std::uint32_t pos = 0; pos < kRefSettle + kRefKeep; pos += kChunk) {
      for (std::uint32_t j = 0; j < kChunk; ++j) {
        phase += kRefHz / rate_;
        phase -= std::floor(phase);
        in[j] = kRefAmp * (2.0f * phase - 1.0f);
      }
      p.process(in, out, kChunk);
      if (pos >= kRefSettle) {
        for (std::uint32_t j = 0; j < kChunk; ++j) {
          float v = out[j] * 32767.0f;
          v = v > 32767.0f ? 32767.0f : (v < -32767.0f ? -32767.0f : v);
          synth_[pos - kRefSettle + j] = static_cast<std::int16_t>(v);
        }
      }
    }
    float area[kAreas];
    if (areas(synth_, kRefKeep, 0u, kRefKeep, area)) {
      for (unsigned j = 0; j < kAreas; ++j) {
        refs_.la[refs_i_][j] = std::log(area[j]);
      }
      normalise(refs_.la[refs_i_]);
    } else {
      for (unsigned j = 0; j < kAreas; ++j) {  // cannot happen (the tube is not silent); a flat shape no take is near
        refs_.la[refs_i_][j] = 0.0f;
      }
    }
    if (++refs_i_ >= kPlaces) {
      refs_i_ = 0;
      refs_ok_ = true;
      refs_rate_ = rate_;
      phase_ = Phase::Level;
    }
  }

  // The level pass: first the mean of the whole analysed take (a DC offset would otherwise sit in every step's RMS and hide the closures), then
  // the RMS of each step with that mean removed. Chunks of kRmsChunk samples. The chunk sums are exact 32-bit integers, added in float.
  void level_chunk() noexcept {
    if (!mean_done_) {
      const std::uint32_t e = span_edge(length_, total_, used_);
      const std::uint32_t n = e - pos_ < kRmsChunk ? e - pos_ : kRmsChunk;
      std::int32_t t = 0;  // |t| <= kRmsChunk * 32768 = 2^27
      for (std::uint32_t j = 0; j < n; ++j) {
        t += static_cast<std::int32_t>(mem_[pos_ + j]);
      }
      sum_ += static_cast<float>(t);
      pos_ += n;
      if (pos_ >= e) {
        mean_ = e > 0u ? sum_ / static_cast<float>(e) : 0.0f;
        mean_done_ = true;
        pos_ = 0;
      }
      return;
    }
    const std::uint32_t a = span_edge(length_, total_, i_), b = span_edge(length_, total_, i_ + 1u);
    if (pos_ < a) {
      pos_ = a;
      acc_ = 0.0f;
    }
    const std::uint32_t n = b - pos_ < kRmsChunk ? b - pos_ : kRmsChunk;
    float s = 0.0f;
    for (std::uint32_t j = 0; j < n; ++j) {
      const float v = static_cast<float>(mem_[pos_ + j]) - mean_;
      s += v * v;
    }
    acc_ += s;
    pos_ += n;
    if (pos_ < b) {
      return;
    }
    const float mean = b > a ? acc_ / static_cast<float>(b - a) : 0.0f;
    const float r = std::sqrt(mean) * (1.0f / 32768.0f);
    rms_[i_] = r;
    peak_ = r > peak_ ? r : peak_;
    pos_ = 0;
    if (++i_ < used_) {
      return;
    }
    i_ = 0;
    if (!(peak_ >= kSilentRms)) {  // nothing to read (also a NaN or an all-zero slot)
      silent_ = true;
      phase_ = Phase::Done;
      return;
    }
    const float thr = peak_ * kClosureRatio;
    for (unsigned i = 0; i < used_; ++i) {
      if (rms_[i] < thr) {
        pick_[i].code = kClosure;
      }
    }
    phase_ = Phase::Shape;
  }

  void shape_step() noexcept {
    if (pick_[i_].code != kClosure) {
      float area[kAreas];
      if (areas(mem_, length_, span_edge(length_, total_, i_), span_edge(length_, total_, i_ + 1u), area)) {
        float la[kAreas];
        for (unsigned j = 0; j < kAreas; ++j) {
          la[j] = std::log(area[j]);
        }
        normalise(la);
        pick_[i_] = nearest(refs_, la);
      }
    }
    if (++i_ >= used_) {
      phase_ = Phase::Done;
    }
  }

  float rate_ = 0.0f, refs_rate_ = 0.0f;
  unsigned refs_i_ = 0;
  bool with_refs_ = false, refs_ok_ = false;
  const std::int16_t* mem_ = nullptr;
  std::uint32_t length_ = 0, total_ = 0, pos_ = 0;
  unsigned used_ = 0, i_ = 0;
  float acc_ = 0.0f, peak_ = 0.0f, sumw2_ = 0.0f, sum_ = 0.0f, mean_ = 0.0f;
  bool mean_done_ = false;
  Phase phase_ = Phase::Idle;
  bool silent_ = false;
  Pick pick_[kMaxSteps] = {};
  float rms_[kMaxSteps] = {};
  float lagw_[kStages + 1] = {};
  alignas(8) float wtab_[kWin] = {};
  alignas(8) float win_[kWin] = {};
  alignas(8) std::int16_t synth_[kRefKeep] = {};
  Refs refs_;
};

static_assert(alignof(Capture) >= 8u && alignof(Refs) >= 8u, "the window, its table and the references are alignas(8)");
static_assert(kRefKeep >= kSpan && kRefSettle % kChunk == 0u && kRefKeep % kChunk == 0u, "the reference take holds a window, in whole chunks");
static_assert(kPreroll % 4u == 0u && kChunk % 4u == 0u && kSpan % 4u == 0u, "every halfband call takes a multiple of 4 samples");
static_assert(Talk::stages_for(16) == kStages, "TALK at Quality 16");

// The whole capture in one go (the host tests' reference: begin and a loop of step() are the same work cut in pieces). Fills `out[0..used)`.
// False when begin() refuses or the slot is silent (out untouched then).
inline bool analyse(Capture& c, const std::int16_t* mem, std::uint32_t length, std::uint32_t total, std::uint32_t used, float rate, Pick* out) noexcept {
  if (!c.begin(mem, length, total, used, rate)) {
    return false;
  }
  while (c.step()) {
  }
  if (c.silent()) {
    return false;
  }
  for (unsigned i = 0; i < used; ++i) {
    out[i] = c.pick(i);
  }
  return true;
}

// Writes a finished capture into the MOUTH lane of `p`: the `used` steps it analysed (a step it could not read is FOLLOW). `used` is the pattern's
// length when the capture began, and the pattern is the one it began on, so no other step is touched. Nothing is written for a capture that is not
// Done or is silent. True when the lane was written.
inline bool apply(const Capture& c, PatternData& p) noexcept {
  if (!c.done() || c.silent()) {
    return false;
  }
  for (unsigned i = 0; i < c.used(); ++i) {
    set_mouth(p, i, byte_of(c.pick(i).code));
  }
  return true;
}

}  // namespace o3_voc03er::mouthcap
