// SPDX-License-Identifier: GPL-3.0-or-later
// Lpc: the analysis half of Talk (lpc.h). DSP steps after mda Talkbox, Copyright (c) 1999-2000 Paul Kellett (maxim digital audio), used under
// the MIT option; see THIRD_PARTY.md. The code here is original, and is Talk's own, moved: no sample of Talk's output changed.
#include <o3_voc03er/units/vocoder/lpc.h>

#include <o3_voc03er/units/vocoder/nofma.h>

#include <cmath>

namespace o3_voc03er {

namespace {
constexpr float kPreEmph = 0.9f;
constexpr float kR0Boost = 1.0001f;
constexpr float kLagHz = 60.0f;
constexpr float kErrFloor = 1.0e-7f;  // of r0: about float's resolution, below it Levinson's error is rounding
constexpr float kTiny = 1.0e-15f;
constexpr float kPi2 = 6.2831853f;
constexpr float kWinRise = 0.8f;      // the window rises over this part (sin^2) and falls over the rest (cos^2)
}  // namespace

void Lpc::prepare(float sample_rate) noexcept {
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
  const float fl = sample_rate * 0.25f;
  for (std::uint32_t l = 0; l <= kMaxOrder; ++l) {
    const float x = kPi2 * kLagHz * static_cast<float>(l) / fl;
    lagw_[l] = std::exp(-0.5f * x * x);
  }
  reset(12);
}

void Lpc::reset(std::uint32_t order) noexcept {
  for (auto& v : ring_) v = 0.0f;
  for (auto& v : win_) v = 0.0f;
  for (auto& v : r_) v = 0.0f;
  for (auto& v : acc_) v = 0.0f;
  wpos_ = filled_ = hop_pos_ = end_ = 0;
  pa_ = order < 1u ? 1u : (order > kMaxOrder ? kMaxOrder : order);
  analysing_ = false;
  stage_ = cpos_ = lag_ = total_ = done_ = 0;
  emph_prev_ = 0.0f;
  macs_ = 0;
}

// Runs `units` of the sliced work, a unit being four samples: first the window and r0, then the lags, four in one pass. Every sample loaded serves
// four lags and the four sums do not wait on each other; one lag at a time cost two loads a multiply-add and stalled on its own sum. The
// window's zeros past kWin let every lag run the same length, and the lags past pa_ of the last pass are never read.
void Lpc::slice(std::uint32_t units) noexcept {
  macs_ += units * 16u;
  while (units != 0u && stage_ < 2u) {
    std::uint32_t c = kQuads - cpos_;
    c = c < units ? c : units;
    units -= c;
    if (stage_ == 0u) {
      float r0 = acc_[0];
      const std::uint32_t last = (cpos_ + c) * 4u;
      for (std::uint32_t i = cpos_ * 4u; i < last; ++i) {
        const float x = ring_[(end_ - kWin + i) & (kRing - 1u)] * wtab_[i];
        win_[i] = x;
        r0 += mulb(x, x);
      }
      acc_[0] = r0;
    } else {
      const float* a = win_ + cpos_ * 4u;
      const float* b = a + lag_;
      float s0 = acc_[0], s1 = acc_[1], s2 = acc_[2], s3 = acc_[3];
      float b0 = b[0], b1 = b[1], b2 = b[2];
      for (std::uint32_t q = 0; q < c; ++q) {
        const float a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
        const float b3 = b[3], b4 = b[4], b5 = b[5], b6 = b[6];
        s0 += mulb(a0, b0);
        s1 += mulb(a0, b1);
        s2 += mulb(a0, b2);
        s3 += mulb(a0, b3);
        s0 += mulb(a1, b1);
        s1 += mulb(a1, b2);
        s2 += mulb(a1, b3);
        s3 += mulb(a1, b4);
        s0 += mulb(a2, b2);
        s1 += mulb(a2, b3);
        s2 += mulb(a2, b4);
        s3 += mulb(a2, b5);
        s0 += mulb(a3, b3);
        s1 += mulb(a3, b4);
        s2 += mulb(a3, b5);
        s3 += mulb(a3, b6);
        b0 = b4;
        b1 = b5;
        b2 = b6;
        a += 4;
        b += 4;
      }
      acc_[0] = s0;
      acc_[1] = s1;
      acc_[2] = s2;
      acc_[3] = s3;
    }
    cpos_ += c;
    if (cpos_ == kQuads) {
      cpos_ = 0;
      if (stage_ == 0u) {
        r_[0] = acc_[0];
        stage_ = 1;
        lag_ = 1;
      } else {
        for (std::uint32_t i = 0; i < 4u; ++i) r_[lag_ + i] = acc_[i];
        lag_ += 4u;
        if (lag_ > pa_) stage_ = 2;
      }
      for (auto& v : acc_) v = 0.0f;
    }
  }
}

// One low-rate sample: into the ring, and this sample's share of the analysis. The order is read at a hop's start, so a change is applied
// at the next hop and never mid-analysis.
bool Lpc::push(float m, std::uint32_t order) noexcept {
  if (hop_pos_ == 0u) {
    // the window is the kWin samples before this one; a ring of 512 keeps it stable through the hop
    analysing_ = filled_ >= kWin;
    if (analysing_) {
      end_ = wpos_ & (kRing - 1u);
      pa_ = order < 1u ? 1u : (order > kMaxOrder ? kMaxOrder : order);
      stage_ = cpos_ = lag_ = 0;
      for (auto& v : acc_) v = 0.0f;
      total_ = kQuads * (1u + (pa_ + 3u) / 4u);  // the window, and a pass for every four lags
      done_ = 0;
    }
  }
  ring_[wpos_ & (kRing - 1u)] = m - kPreEmph * emph_prev_;
  emph_prev_ = m;
  ++wpos_;
  if (filled_ < kRing) ++filled_;
  bool ended = false;
  if (analysing_ && (hop_pos_ & 3u) == 3u) {
    // an even share of the work every four samples: the cumulative target is total x (pos + 1) / kHop
    const std::uint32_t upto = total_ * (hop_pos_ + 1u) / kHop;
    slice(upto - done_);
    done_ = upto;
    ended = hop_pos_ == kHop - 1u;
  }
  hop_pos_ = hop_pos_ == kHop - 1u ? 0u : hop_pos_ + 1u;
  return ended;
}

void Lpc::flush_state() noexcept {
  if (!(emph_prev_ > kTiny || emph_prev_ < -kTiny)) emph_prev_ = 0.0f;
}

float Lpc::levinson(float* kk) noexcept {
  const std::uint32_t p = pa_;
  r_[0] *= kR0Boost;
  for (std::uint32_t l = 1; l <= p; ++l) r_[l] *= lagw_[l];
  float a[kMaxOrder + 2];
  float t[kMaxOrder + 2];
  for (auto& v : a) v = 0.0f;
  float err = r_[0];
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
  macs_ += macs;
  return err;
}

}  // namespace o3_voc03er
