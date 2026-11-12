// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/mouth_live.h>

#include <o3_voc03er/units/vocoder/talk.h>

#include <cmath>

namespace o3_voc03er {

void MouthLive::start(Lpc& lpc) noexcept {
  h1_ = Halfband{};
  h2_ = Halfband{};
  odd1_ = odd2_ = false;
  have_ = false;
  held_ = 0;
  lpc.reset(kOrder);
}

void MouthLive::process(Lpc& lpc, const float* mod, std::uint32_t n, MouthLink& link) noexcept {
  n = n > 64u ? 64u : n;
  float m1[33];
  float m2[17];
  const std::uint32_t n1 = decimate(h1_, mod, n, odd1_, m1);
  odd1_ = ((static_cast<unsigned>(odd1_) + n) & 1u) != 0u;
  const std::uint32_t n2 = decimate(h2_, m1, n1, odd2_, m2);
  odd2_ = ((static_cast<unsigned>(odd2_) + n1) & 1u) != 0u;
  for (std::uint32_t i = 0; i < n2; ++i) {
    if (lpc.push(m2[i], kOrder)) shape(lpc, link);
  }
  lpc.flush_state();
}

void MouthLive::shape(Lpc& lpc, MouthLink& link) noexcept {
  const float power = lpc.power();
  if (!(power >= kGate) || lpc.order() != kOrder) {  // silence, a NaN, or a hop that was started at another order: hold
    ++held_;
    return;
  }
  float kk[Lpc::kMaxOrder + 2] = {};
  lpc.levinson(kk);
  float area[Talk::kMaxStages + 1] = {};
  Talk::areas_from_reflection(kk, kOrder, area);
  float widest = 0.0f;
  for (std::uint32_t i = 0; i < kOrder; ++i) widest = area[i] > widest ? area[i] : widest;  // index kOrder is the termination: not a section
  if (!(widest > 0.0f)) {
    ++held_;
    return;
  }
  const float inv = 1.0f / widest;
  float d[kOrder];
  for (std::uint32_t i = 0; i < kOrder; ++i) {
    float x = std::sqrt(area[i] * inv);
    x = x < kFloor ? kFloor : (x > 1.0f ? 1.0f : x);   // the areas are clamped positive and finite: this also keeps a NaN out
    d[i] = x == x ? x : kFloor;
  }
  if (!have_) {
    for (std::uint32_t i = 0; i < kOrder; ++i) d_[i] = d[i];
    have_ = true;
  } else {
    for (std::uint32_t i = 0; i < kOrder; ++i) d_[i] += kSmooth * (d[i] - d_[i]);
  }
  for (std::uint32_t i = 0; i < kOrder; ++i) {
    const float a = d_[i] * d_[i];
    link.area[2u * i] = a;
    link.area[2u * i + 1u] = a;
  }
  link.n = kAreas;
  link.valid = true;
  held_ = 0;
  ++shapes_;
}

}  // namespace o3_voc03er
