// SPDX-License-Identifier: GPL-3.0-or-later
// Tune: the YIN pitch detector and the voicing gate. See tune.h for the design, the schedule and the cost.
#include <o3_voc03er/units/vocoder/tune.h>

#include <o3_voc03er/units/vocoder/nofma.h>

#include <cmath>

namespace o3_voc03er {

namespace {

constexpr unsigned kNMask = 2047;
constexpr unsigned kDMask = 511;
constexpr unsigned kS1Mask = 31;
constexpr float kPi = 3.14159265f;
constexpr unsigned kHoldSamples = Tune::kHoldMs * 44100u / 1000u;  // 2646
constexpr unsigned kMaxNativeLag = 1000;

// The refine reads back from the snapshot's newest sample: kRefineWindow + the largest native lag + the samples that arrive before the
// refine ends (a hop) must fit the ring.
static_assert(Tune::kRefineWindow + 8 * Tune::kBassMaxLag + Tune::kRefineSpan + Tune::kHop + 1 <= 2048, "bass refine fits the native ring");
static_assert(Tune::kRefineWindow + 4 * Tune::kMaxLag + Tune::kRefineSpan + Tune::kHop + 1 <= 2048, "normal refine fits the native ring");
static_assert(8 * Tune::kBassMaxLag + Tune::kRefineSpan + 1 <= kMaxNativeLag, "native lag limit");
static_assert(Tune::kWindow + Tune::kMaxLag <= 416, "snapshot size");
static_assert(Tune::kWindow % 8 == 0, "the difference loop is unrolled by 8");
static_assert(Tune::kHop % Tune::kTick == 0 && Tune::kHop / Tune::kTick == 32, "32 ticks a hop, the phase is a 5-bit field");
static_assert((Tune::kHop / Tune::kTick - 1) * Tune::kUnitsPerTick >= Tune::kMaxLag + 2 * (2 * Tune::kRefineSpan + 1), "the slices cover the worst case");
static_assert(Tune::kHop % 8 == 0 && (Tune::kHop & (Tune::kHop - 1)) == 0, "hop is a power of two");

// Sum over the window of (a[j] - b[j])^2. Unrolled by 8 into four accumulators (the Cortex-M4F is single issue: fewer loop instructions,
// and the adds do not wait for each other); pointers advance, no indexing arithmetic. Fixed order: the result is deterministic.
inline float diff_sum(const float* a, const float* b) noexcept {
  float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
  for (unsigned j = 0; j < Tune::kWindow; j += 8) {
    const float d0 = a[0] - b[0], d1 = a[1] - b[1], d2 = a[2] - b[2], d3 = a[3] - b[3];
    const float d4 = a[4] - b[4], d5 = a[5] - b[5], d6 = a[6] - b[6], d7 = a[7] - b[7];
    s0 += mulb(d0, d0);
    s1 += mulb(d1, d1);
    s2 += mulb(d2, d2);
    s3 += mulb(d3, d3);
    s0 += mulb(d4, d4);
    s1 += mulb(d5, d5);
    s2 += mulb(d6, d6);
    s3 += mulb(d7, d7);
    a += 8;
    b += 8;
  }
  return (s0 + s1) + (s2 + s3);
}

inline float clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

// The vertex offset of a parabola through three points (a at -1, b at 0, c at +1), 0 when it is not a minimum.
inline float vertex(float a, float b, float c) noexcept {
  const float den = a - 2.0f * b + c;
  if (!(den > 1e-12f)) return 0.0f;
  return 0.5f * (a - c) / den;
}

}  // namespace

Tune::Tune() {
  // Stage 1: 32 taps, Hamming-windowed sinc, cutoff 0.075 of 44.1 kHz (3.3 kHz). Stage 2: 16 taps, cutoff 0.15 of 11.025 kHz (1.65 kHz).
  // Unity gain at DC. Built once, here (sinf, cosf: not in a sample loop).
  float sum = 0.0f;
  for (unsigned k = 0; k < kS1Taps; ++k) {
    const float t = static_cast<float>(k) - 15.5f;
    const float x = 2.0f * 0.075f * t;
    const float sinc = std::sin(kPi * x) / (kPi * x);  // x is never 0: the centre is between two taps
    const float w = 0.54f - 0.46f * std::cos(2.0f * kPi * static_cast<float>(k) / 31.0f);
    h1_[k] = sinc * w;
    sum += h1_[k];
  }
  for (unsigned k = 0; k < kS1Taps; ++k) h1_[k] /= sum;
  sum = 0.0f;
  for (unsigned k = 0; k < kS2Taps; ++k) {
    const float t = static_cast<float>(k) - 7.5f;
    const float x = 2.0f * 0.15f * t;
    const float sinc = std::sin(kPi * x) / (kPi * x);
    const float w = 0.54f - 0.46f * std::cos(2.0f * kPi * static_cast<float>(k) / 15.0f);
    h2_[k] = sinc * w;
    sum += h2_[k];
  }
  for (unsigned k = 0; k < kS2Taps; ++k) h2_[k] /= sum;
  reset();
}

void Tune::reset() {
  for (float& v : xn_) v = 0.0f;
  for (float& v : dec_) v = 0.0f;
  for (float& v : snap_) v = 0.0f;
  for (float& v : dp_) v = 1.0f;
  for (float& v : s1_) v = 0.0f;
  for (float& v : dn_) v = 0.0f;
  n_ = 0;
  dn_count_ = 0;
  s1n_ = 0;
  en_ = ed_ = 0.0f;
  hop_pow_ = hop_ratio_ = 0.0f;
  snap_native_ = 0;
  stage_ = Stage::Idle;
  lag_ = 1;
  cum_ = 0.0f;
  found_ = false;
  dip_done_ = false;
  cand_ = best_ = 0;
  best_v_ = 2.0f;
  cand_a_ = 1.0f;
  tau_f_ = 0.0f;
  ref_lo_ = ref_i_ = 0;
  freq_ = conf_ = 0.0f;
  voiced_ = false;
  bad_samples_ = 0;
  pending_ = 0.0f;
}

void Tune::set_range(Range r) {
  range_ = r;
  reset();
}

void Tune::push(const float* x, unsigned n) {
  for (unsigned i = 0; i < n; ++i) {
    float v = x[i];
    if (!(v >= -8.0f && v <= 8.0f)) v = 0.0f;  // NaN and infinities fail both comparisons
    xn_[n_ & kNMask] = v;
    en_ += mulb(v, v);
    ++n_;
    if ((n_ & 3u) == 0) decimate_stage1();
    if ((n_ & (kTick - 1)) == 0) tick();
  }
}

void Tune::dec_push(float y) {
  dec_[dn_count_ & kDMask] = y;
  ++dn_count_;
  ed_ += mulb(y, y);
}

void Tune::decimate_stage1() {
  const unsigned i0 = (n_ - 1) & kNMask;
  float y = 0.0f;
  if (i0 >= kS1Taps - 1) {
    const float* p = xn_ + i0;
    for (unsigned k = 0; k < kS1Taps; ++k) y += mulb(h1_[k], p[-static_cast<int>(k)]);
  } else {
    for (unsigned k = 0; k < kS1Taps; ++k) y += mulb(h1_[k], xn_[(i0 - k) & kNMask]);
  }
  if (range_ == Range::Normal) {
    dec_push(y);
    return;
  }
  s1_[s1n_ & kS1Mask] = y;
  ++s1n_;
  if ((s1n_ & 1u) == 0) {
    float y2 = 0.0f;
    for (unsigned k = 0; k < kS2Taps; ++k) y2 += mulb(h2_[k], s1_[(s1n_ - 1 - k) & kS1Mask]);
    dec_push(y2);
  }
}

void Tune::tick() {
  const unsigned phase = (n_ / kTick) & 31u;
  if (phase == 0) {
    hop_start();
  } else if (stage_ == Stage::Diff || stage_ == Stage::Refine) {
    work();
  }
}

void Tune::hop_start() {
  snap_native_ = n_;
  hop_pow_ = en_ * (1.0f / static_cast<float>(kHop));
  hop_ratio_ = en_ > 0.0f ? ed_ * static_cast<float>(decim()) / en_ : 0.0f;
  en_ = ed_ = 0.0f;

  const unsigned len = kWindow + max_lag();
  const unsigned start = (dn_count_ - len) & kDMask;
  const unsigned first = kDecRing - start < len ? kDecRing - start : len;
  for (unsigned i = 0; i < first; ++i) snap_[i] = dec_[start + i];
  for (unsigned i = first; i < len; ++i) snap_[i] = dec_[i - first];

  stage_ = Stage::Diff;
  lag_ = 1;
  cum_ = 0.0f;
  found_ = false;
  dip_done_ = false;
  cand_ = best_ = 0;
  best_v_ = 2.0f;
}

void Tune::work() {
  unsigned budget = kUnitsPerTick;
  while (budget > 0 && (stage_ == Stage::Diff || stage_ == Stage::Refine)) {
    if (stage_ == Stage::Diff) {
      --budget;
      if (diff_step()) diff_end();
    } else {
      budget = budget >= 2 ? budget - 2 : 0;
      refine_step();
    }
  }
}

bool Tune::diff_step() {
  const unsigned t = lag_;
  const float* a = snap_ + max_lag();  // the newest window; its lag-t copy starts t earlier
  const float d = diff_sum(a, a - t);
  cum_ += d;
  const float dp = cum_ > 0.0f ? d * static_cast<float>(t) / cum_ : 1.0f;
  dp_[t] = dp;
  const unsigned lo = min_lag();
  if (t >= lo && dp < best_v_) {
    best_v_ = dp;
    best_ = t;
  }
  bool over = false;
  if (!found_) {
    if (t >= lo && dp < kThreshold) {
      found_ = true;
      cand_ = t;
    }
  } else if (!dip_done_) {
    if (dp < dp_[cand_]) cand_ = t;
    else dip_done_ = true;  // dp rose past the dip: cand_ is its minimum
  }
  ++lag_;
  // After the dip, go on to one lag past its double: the sub-octave check in diff_end needs d' there.
  if (dip_done_) {
    const unsigned stop = 2 * cand_ + 1 < max_lag() ? 2 * cand_ + 1 : max_lag();
    over = t >= stop;
  }
  return over || lag_ > max_lag();
}

void Tune::diff_end() {
  unsigned c = found_ ? cand_ : best_;
  if (!found_ && c != 0) {  // the first local minimum near the lowest, not the lowest: a noisy period's multiples dip a little lower
    for (unsigned i = min_lag(); i < max_lag(); ++i) {
      if (i >= 2 && dp_[i] <= best_v_ + kSlack && dp_[i] < dp_[i - 1] && dp_[i] <= dp_[i + 1]) {
        c = i;
        break;
      }
    }
  }
  if (c == 0) {  // nothing computed above the minimum lag: cannot happen with a full pass, kept for safety
    cand_a_ = 1.0f;
    tau_f_ = 0.0f;
    finish(0.0f, false);
    return;
  }
  // The octave-up guard: a strong second harmonic (a formant on it) can make the half period dip below the threshold first. If d' at the
  // double (its minimum within one lag) is clearly lower than at c, the period is the double.
  if (2 * c + 1 < lag_) {
    unsigned m = 2 * c - 1;
    for (unsigned i = 2 * c; i <= 2 * c + 1; ++i)
      if (dp_[i] < dp_[m]) m = i;
    if (dp_[m] + kSubMargin < dp_[c] && dp_[m] < kSubRatio * dp_[c]) c = m;
  }
  cand_a_ = dp_[c];
  float off = 0.0f;
  if (c >= 2 && c + 1 < lag_) off = clampf(vertex(dp_[c - 1], dp_[c], dp_[c + 1]), -1.0f, 1.0f);
  tau_f_ = static_cast<float>(c) + off;

  const unsigned r = decim();
  if (!gate_ok(cand_a_)) {  // nothing to refine: the estimate will not be used
    finish(tau_f_ * static_cast<float>(r), false);
    return;
  }
  const unsigned l0 = static_cast<unsigned>(tau_f_ * static_cast<float>(r) + 0.5f);
  if (l0 <= kRefineSpan || l0 + kRefineSpan > kMaxNativeLag) {
    finish(tau_f_ * static_cast<float>(r), true);
    return;
  }
  ref_lo_ = l0 - kRefineSpan;
  ref_i_ = 0;
  stage_ = Stage::Refine;
}

void Tune::refine_step() {
  const unsigned lag = ref_lo_ + ref_i_;
  const unsigned b = snap_native_ - 1;
  float s0 = 0.0f, s1 = 0.0f;
  for (unsigned j = 0; j < kRefineWindow; j += 2) {
    const float d0 = xn_[(b - j) & kNMask] - xn_[(b - j - lag) & kNMask];
    const float d1 = xn_[(b - j - 1) & kNMask] - xn_[(b - j - 1 - lag) & kNMask];
    s0 += mulb(d0, d0);
    s1 += mulb(d1, d1);
  }
  dn_[ref_i_] = s0 + s1;
  if (++ref_i_ < kRefineCount) return;

  unsigned m = 0;
  for (unsigned i = 1; i < kRefineCount; ++i)
    if (dn_[i] < dn_[m]) m = i;
  float l = static_cast<float>(ref_lo_ + m);
  if (m > 0 && m + 1 < kRefineCount) l += clampf(vertex(dn_[m - 1], dn_[m], dn_[m + 1]), -0.5f, 0.5f);
  finish(l, true);
}

bool Tune::gate_ok(float a) const {
  const float gate = voiced_ ? kGateExit : kGateEnter;
  const float pow_min = voiced_ ? kLevelExitPow : kLevelEnterPow;
  return a < gate && hop_pow_ > pow_min && hop_ratio_ > kLowBandMin;
}

void Tune::finish(float lag_native, bool good) {
  stage_ = Stage::Done;
  conf_ = clampf(1.0f - cand_a_, 0.0f, 1.0f);
  if (!good || !(lag_native >= 1.0f)) {
    bad_samples_ += kHop;
    if (voiced_ && bad_samples_ + kHop / 2 >= kHoldSamples) {
      voiced_ = false;
      pending_ = 0.0f;
    }
    return;
  }
  bad_samples_ = 0;
  const float f = kFs / lag_native;
  if (voiced_ && freq_ > 0.0f) {
    const float r = f / freq_;
    if (r > kJumpRatio || r < 1.0f / kJumpRatio) {
      const float q = pending_ > 0.0f ? f / pending_ : 0.0f;
      if (q > 1.0f / kJumpAgree && q < kJumpAgree) {  // the repeat: accept
        freq_ = f;
        pending_ = 0.0f;
      } else {
        pending_ = f;  // keep the held pitch, remember the candidate
      }
      return;
    }
  }
  pending_ = 0.0f;
  freq_ = f;
  voiced_ = true;
}

}  // namespace o3_voc03er
