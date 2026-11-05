// SPDX-License-Identifier: GPL-3.0-or-later
// Talk: LPC talkbox, streaming. DSP steps after mda Talkbox, Copyright (c) 1999-2000 Paul Kellett (maxim digital audio), used under
// the MIT option; see THIRD_PARTY.md. The code here is original.
#include <o3_voc03er/units/vocoder/talk.h>

#include <cmath>

namespace o3_voc03er {

static_assert(Talk::kWin + Talk::kHop + Talk::kMaxFrames <= Talk::kRing, "the window must stay stable through a hop");
static_assert((Talk::kRing & (Talk::kRing - 1u)) == 0u, "the ring wraps with a mask");
static_assert(alignof(Talk) >= 8u, "the buffers are alignas(8)");

namespace {
constexpr float kPreEmph = 0.9f;
constexpr float kR0Boost = 1.0001f;
constexpr float kLagHz = 60.0f;
constexpr float kGate = 1.0e-9f;  // mean power of the pre-emphasised modulator below which the analysis calls it silence (-90 dB)
constexpr float kTiny = 1.0e-15f;
constexpr float kErrFloor = 1.0e-7f;  // of r0: about float's resolution, below it Levinson's error is rounding
// Two-path allpass spread of the carrier (coefficients from the card: 0.3 and 0.77), applied before and after the lattice.
constexpr float kA1 = 0.3f;
constexpr float kA2 = 0.77f;
constexpr float kPi2 = 6.2831853f;

inline float flush(float x) noexcept { return (x > kTiny || x < -kTiny) ? x : 0.0f; }
}  // namespace

void Talk::prepare(float sample_rate) noexcept {
  float s2 = 0.0f;
  for (std::uint32_t i = 0; i < kWin; ++i) {
    const float h = 0.5f - 0.5f * std::cos(kPi2 * static_cast<float>(i) / static_cast<float>(kWin));
    hann_[i] = h;
    s2 += h * h;
  }
  sumw2_ = s2;
  for (std::uint32_t l = 0; l <= kMaxOrder; ++l) {
    const float x = kPi2 * kLagHz * static_cast<float>(l) / sample_rate;
    lagw_[l] = std::exp(-0.5f * x * x);
  }
  reset();
}

void Talk::reset() noexcept {
  for (auto& v : ring_) v = 0.0f;
  for (auto& v : win_) v = 0.0f;
  for (auto& v : r_) v = 0.0f;
  for (std::uint32_t m = 0; m < kMaxOrder + 2; ++m) {
    k_[m] = 0.0f;
    kt_[m] = 0.0f;
    dk_[m] = 0.0f;
    b_[m] = 0.0f;
  }
  for (auto& v : ap_) v = 0.0f;
  wpos_ = filled_ = block_ = end_ = 0;
  pa_ = run_ = tgt_order_ = order_;
  analysing_ = false;
  stage_ = cpos_ = lag_ = total_ = done_ = ramp_left_ = 0;
  acc_ = emph_prev_ = gain_ = gain_t_ = dgain_ = 0.0f;
  macs_last_ = 0;
}

void Talk::set_order(std::uint32_t order) noexcept {
  order_ = order < kMinOrder ? kMinOrder : (order > kMaxOrder ? kMaxOrder : order);
}

// Runs `units` of the sliced work: windowing, then the lag sums.
void Talk::slice(std::uint32_t units) noexcept {
  macs_last_ += units;
  while (units != 0u && stage_ < 2u) {
    if (stage_ == 0u) {
      std::uint32_t c = kWin - cpos_;
      c = c < units ? c : units;
      for (std::uint32_t i = cpos_; i < cpos_ + c; ++i) {
        win_[i] = ring_[(end_ - kWin + i) & (kRing - 1u)] * hann_[i];
      }
      cpos_ += c;
      units -= c;
      if (cpos_ == kWin) {
        stage_ = 1;
        cpos_ = 0;
        lag_ = 0;
        acc_ = 0.0f;
      }
    } else {
      const std::uint32_t len = kWin - lag_;
      std::uint32_t c = len - cpos_;
      c = c < units ? c : units;
      const float* a = win_ + cpos_;
      const float* b = win_ + cpos_ + lag_;
      float s = 0.0f;
      for (std::uint32_t i = 0; i < c; ++i) s += a[i] * b[i];
      acc_ += s;
      cpos_ += c;
      units -= c;
      if (cpos_ == len) {
        r_[lag_] = acc_;
        acc_ = 0.0f;
        cpos_ = 0;
        if (++lag_ > pa_) stage_ = 2;
      }
    }
  }
}

// Levinson-Durbin on r_[0..pa_]; sets the targets and the gain target, and starts the ramp.
void Talk::publish() noexcept {
  const std::uint32_t p = pa_;
  const float power = r_[0] / sumw2_;
  const bool silent = !(power > kGate);
  float err = 0.0f;
  float kk[kMaxOrder + 2] = {};
  if (!silent && !freeze_) {
    r_[0] *= kR0Boost;
    for (std::uint32_t l = 1; l <= p; ++l) r_[l] *= lagw_[l];
    float a[kMaxOrder + 2];
    float t[kMaxOrder + 2];
    for (auto& v : a) v = 0.0f;
    err = r_[0];
    std::uint32_t macs = 0;
    const float err_floor = r_[0] * kErrFloor;
    for (std::uint32_t m = 1; m <= p; ++m) {
      if (!(err > err_floor)) {
        // A safety net, reached by no signal tried: the boost on r0 and the lag window keep err far above it. If err ever got to 0,
        // -s/err would be a NaN no clamp catches and the lattice would stay poisoned. The stages left stay at 0.
        break;
      }
      float s = r_[m];
      for (std::uint32_t j = 1; j < m; ++j) s += a[j] * r_[m - j];
      float k = -s / err;
      k = k > kMaxK ? kMaxK : (k < -kMaxK ? -kMaxK : k);
      for (std::uint32_t j = 1; j < m; ++j) t[j] = a[j] + k * a[m - j];
      for (std::uint32_t j = 1; j < m; ++j) a[j] = t[j];
      a[m] = k;
      kk[m] = k;
      err *= 1.0f - k * k;
      macs += 2u * m;
    }
    macs_last_ += macs;
  } else if (!silent) {
    err = r_[0] * kR0Boost;  // frozen: k holds, the gain follows the level through the frozen filter's own error
    const std::uint32_t q = p < order_ ? p : order_;
    for (std::uint32_t m = 1; m <= q; ++m) err *= 1.0f - kt_[m] * kt_[m];
  }
  if (!silent) {
    gain_t_ = std::sqrt((err > 0.0f ? err : 0.0f) / sumw2_);
  } else {
    gain_t_ = 0.0f;
  }
  const std::uint32_t use = p < order_ ? p : order_;
  const std::uint32_t prev_run = run_;
  if (!silent && !freeze_) {
    for (std::uint32_t m = 1; m <= kMaxOrder; ++m) kt_[m] = m <= use ? kk[m] : 0.0f;
  } else {
    for (std::uint32_t m = use + 1; m <= kMaxOrder; ++m) kt_[m] = 0.0f;
  }
  tgt_order_ = use;
  run_ = prev_run > use ? prev_run : use;
  ramp_left_ = kHop;
  const float inv = static_cast<float>(kStep) / static_cast<float>(kHop);  // k and the gain move every kStep samples
  for (std::uint32_t m = 1; m <= kMaxOrder; ++m) dk_[m] = (kt_[m] - k_[m]) * inv;
  dgain_ = (gain_t_ - gain_) * inv;
}

void Talk::analyse(std::uint32_t block) noexcept {
  if (block == 0u) {
    analysing_ = filled_ >= kWin;
    if (!analysing_) return;
    end_ = wpos_ & (kRing - 1u);  // before this block's samples: the window is the 768 before them
    pa_ = order_;
    stage_ = cpos_ = lag_ = 0;
    acc_ = 0.0f;
    std::uint32_t t = kWin;
    for (std::uint32_t l = 0; l <= pa_; ++l) t += kWin - l;
    total_ = t;
    done_ = 0;
  }
  if (!analysing_) return;
  std::uint32_t u = total_ / 6u;
  if (block == 5u) u = total_ - done_;
  done_ += u;
  slice(u);
  if (block == 5u) publish();
}

void Talk::process(const float* mod, const float* car, float* out, std::uint32_t n) noexcept {
  macs_last_ = 0;
  if (n > kMaxFrames) n = kMaxFrames;
  // The window of this hop ends before this block's samples: capture it at block 0 first.
  if (block_ == 0u) analyse(0);
  for (std::uint32_t i = 0; i < n; ++i) {
    const float x = mod[i];
    ring_[wpos_ & (kRing - 1u)] = x - kPreEmph * emph_prev_;
    emph_prev_ = x;
    ++wpos_;
  }
  filled_ = filled_ + n > kRing ? kRing : filled_ + n;
  if (block_ != 0u) analyse(block_);  // block 0 ran its slice with its capture
  block_ = block_ == 5u ? 0u : block_ + 1u;

  const std::uint32_t run = run_;
  macs_last_ += n * (2u * run + run + 4u);
  std::uint32_t left = ramp_left_;
  float gain = gain_;
  float ap0 = ap_[0], ap1 = ap_[1], ap2 = ap_[2], ap3 = ap_[3];
  for (std::uint32_t i = 0; i < n; ++i) {
    if (left != 0u) {
      // k and the gain step every kStep samples (left is a multiple of kStep at the first of each group); the last sample lands on the target
      if ((left & (kStep - 1u)) == 0u) {
        for (std::uint32_t m = 1; m <= run; ++m) k_[m] += dk_[m];
        gain += dgain_;
      }
      if (--left == 0u) {
        for (std::uint32_t m = 1; m <= run; ++m) k_[m] = kt_[m];
        gain = gain_t_;
      }
    }
    // carrier through the two-path allpass spread
    const float c = car[i];
    const float y1 = -kA1 * c + ap0;
    ap0 = c + kA1 * y1;
    const float y2 = -kA2 * c + ap1;
    ap1 = c + kA2 * y2;
    float f = 0.5f * (y1 + y2) * gain;
    // all-pole lattice, stages run..1
    for (std::uint32_t m = run; m >= 1u; --m) {
      f -= k_[m] * b_[m - 1u];
      b_[m] = b_[m - 1u] + k_[m] * f;
    }
    b_[0] = f;
    const float z1 = -kA1 * f + ap2;
    ap2 = f + kA1 * z1;
    const float z2 = -kA2 * f + ap3;
    ap3 = f + kA2 * z2;
    out[i] = 0.5f * (z1 + z2);
  }
  ramp_left_ = left;
  gain_ = gain;
  ap_[0] = ap0;
  ap_[1] = ap1;
  ap_[2] = ap2;
  ap_[3] = ap3;
  // Denormals: states decay to zero under silence; flush them once a block.
  for (std::uint32_t m = 0; m <= run; ++m) b_[m] = flush(b_[m]);
  for (auto& v : ap_) v = flush(v);
  emph_prev_ = flush(emph_prev_);
  gain_ = flush(gain_);
  if (ramp_left_ == 0u && run_ != tgt_order_) {
    run_ = tgt_order_;
    for (std::uint32_t m = run_ + 1; m <= kMaxOrder; ++m) {
      b_[m] = 0.0f;
      k_[m] = 0.0f;
    }
  }
}

}  // namespace o3_voc03er
