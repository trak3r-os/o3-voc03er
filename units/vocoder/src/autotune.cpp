// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/autotune.h>
#include <o3_voc03er/units/vocoder/scale.h>

#include "nofma.h"

#include <cmath>
#include <cstring>
#include <initializer_list>

namespace o3_voc03er {

static_assert(kScaleTableCount == Autotune::kScaleCount, "the scale table and the Scale enum agree");

namespace {

constexpr float kMinPeriod = 32.0f;
constexpr float kMaxPeriod = 700.0f;
constexpr float kRelax = 0.012f;        // per segment: about 30 ms to forget the shift when unvoiced
constexpr float kSmooth = 0.35f;        // the decision pitch follows the estimates with this weight per hop
constexpr float kInvXf = 1.0f / static_cast<float>(Autotune::kXfade + 1u);
constexpr float kInvSeg = 1.0f / static_cast<float>(Autotune::kSeg);

inline float clean(float x) noexcept { return (x == x && x <= 8.0f && x >= -8.0f) ? x : 0.0f; }

}  // namespace

Autotune::Autotune() { reset(); }

void Autotune::reset() {
  tune_.reset();
  std::memset(seg_, 0, sizeof seg_);
  seg_n_ = 0;
  w_ = 0;
  d_ = d2_ = kRestDelay;
  xf_ = 0;
  ratio_ = r1_ = 1.0f;
  dr_ = 0.0f;
  was_voiced_ = false;
  f_last_ = 0.0f;
  m_ = m_s_ = 0.0f;
  cur_ = -1;
  e_ = c_ = 0.0f;
}

void Autotune::set_key(unsigned k) { key_ = k % 12u; }

void Autotune::set_scale(unsigned s) {
  scale_ = s < kScaleCount ? s : 0u;
  mask_ = kScaleMask[scale_];
}

void Autotune::set_speed_ms(float ms) {
  alpha_ = ms <= 0.5f ? 1.0f : 1.0f - std::exp(-static_cast<float>(kSeg) / (Tune::kFs * 0.001f * ms));
}

void Autotune::note_on(std::uint8_t n) {
  note_off(n);
  if (held_n_ == kMaxHeld) {
    for (unsigned i = 1; i < kMaxHeld; ++i) held_[i - 1] = held_[i];
    --held_n_;
  }
  held_[held_n_++] = n;
}

void Autotune::note_off(std::uint8_t n) {
  unsigned j = 0;
  for (unsigned i = 0; i < held_n_; ++i) {
    if (held_[i] != n) held_[j++] = held_[i];
  }
  held_n_ = j;
}

// One cubic read at the delay d (>= kMinRead) behind the newest sample. The history is the detector's own native ring (Tune::native),
// which lags the newest sample by seg_n_ <= 15 samples (Tune is pushed once a segment): a read needs d - 1 >= seg_n_ + 1, hence kMinRead.
float Autotune::read(float d) const {
  const int id = static_cast<int>(d);
  const float f = d - static_cast<float>(id);
  const std::uint32_t b = w_ - static_cast<std::uint32_t>(id);  // the sample d_int behind the newest (newest = w_)
  const float p2 = tune_.native(b);
  if (f == 0.0f) return p2;
  const float p0 = tune_.native(b - 2u);
  const float p1 = tune_.native(b - 1u);
  const float p3 = tune_.native(b + 1u);
  const float t = 1.0f - f;  // from p1 (t = 0) to p2 (t = 1)
  const float c3 = mulb(3.0f, p1 - p2) + p3 - p0;
  const float c2 = mulb(2.0f, p0) - mulb(5.0f, p1) + mulb(4.0f, p2) - p3;
  return p1 + mulb(0.5f * t, (p2 - p0) + mulb(t, c2 + mulb(t, c3)));
}

void Autotune::start_xfade(float d2) {
  d2_ = d2 < kMinRead ? kMinRead : (d2 > kMaxRead ? kMaxRead : d2);
  xf_ = 1;
}

// The delay left [kDelayLo, kDelayLo + period): a whole number of periods back inside.
void Autotune::jump() {
  const float over = d_ - kDelayLo;
  int k;
  if (over < 0.0f) {
    k = static_cast<int>(-over * inv_period_) + 1;
  } else {
    k = static_cast<int>(over * inv_period_);
    if (k < 1) k = 1;
    k = -k;
  }
  start_xfade(d_ + static_cast<float>(k) * period_);
}

int Autotune::pick(float m) const {
  const int base = static_cast<int>(std::floor(m + 0.5f));
  int best = -1;
  float bd = 1.0e9f;
  for (int n = base - 7; n <= base + 7; ++n) {
    if (!in_scale(n)) continue;
    const float d = std::fabs(m - static_cast<float>(n));
    if (d < bd) {
      bd = d;
      best = n;
    }
  }
  if (best < 0) return base;
  if (cur_ >= 0 && in_scale(cur_) && best != cur_ && bd + kSnapBias >= std::fabs(m - static_cast<float>(cur_))) {
    return cur_;
  }
  return best;
}

void Autotune::new_estimate(float f) {
  period_ = Tune::kFs / f;
  period_ = period_ < kMinPeriod ? kMinPeriod : (period_ > kMaxPeriod ? kMaxPeriod : period_);
  inv_period_ = 1.0f / period_;
  m_ = 69.0f + 12.0f * std::log2(f * (1.0f / 440.0f));
  m_s_ = cur_ < 0 || !was_voiced_ ? m_ : m_s_ + kSmooth * (m_ - m_s_);
  float target;
  if (notes_in_ && held_n_ > 0) {
    const float nt = static_cast<float>(held_[held_n_ - 1]);
    target = nt + 12.0f * std::floor((m_s_ - nt) * (1.0f / 12.0f) + 0.5f);
    cur_ = static_cast<int>(target);
  } else {
    if (!was_voiced_) cur_ = -1;
    cur_ = pick(m_s_);
    target = static_cast<float>(cur_);
  }
  float e = (target - m_) * 100.0f;
  e_ = e < -kMaxCents ? -kMaxCents : (e > kMaxCents ? kMaxCents : e);
}

void Autotune::control() {
  const bool v = tune_.voiced();
  if (v) {
    const float f = tune_.frequency();
    if (f > 0.0f && (f != f_last_ || !was_voiced_)) {
      f_last_ = f;
      new_estimate(f);
      if (!was_voiced_) c_ = 0.0f;
    }
    c_ += alpha_ * (e_ * amount_ - c_);
  } else {
    c_ -= c_ * kRelax;
    if (c_ < 0.01f && c_ > -0.01f) c_ = 0.0f;
    e_ = 0.0f;
  }
  was_voiced_ = v;
  if (!(c_ == c_)) c_ = 0.0f;
  c_ = c_ < -kMaxCents ? -kMaxCents : (c_ > kMaxCents ? kMaxCents : c_);
  ratio_ = r1_;
  r1_ = c_ == 0.0f ? 1.0f : std::exp2(c_ * (1.0f / 1200.0f));
  dr_ = (r1_ - ratio_) * kInvSeg;
  // At rest the delay becomes a whole number of samples, so the output is the input, delayed.
  if (!v && c_ == 0.0f && ratio_ == 1.0f && r1_ == 1.0f && xf_ == 0) {
    const float r = static_cast<float>(static_cast<int>(d_ + 0.5f));
    if (r != d_) start_xfade(r);
  }
}

void Autotune::process(const float* in, float* out, unsigned n) {
  for (unsigned i = 0; i < n; ++i) {
    const float x = clean(in[i]);
    seg_[seg_n_] = x;
    float y;
    if (xf_ == 0) {
      y = read(d_);
    } else {
      const float t = static_cast<float>(xf_) * kInvXf;
      const float g = t * t * (3.0f - 2.0f * t);
      const float a = read(d_);
      const float b = read(d2_);
      y = a + mulb(g, b - a);
    }
    out[i] = y;
    ++w_;
    ratio_ += dr_;
    const float step = 1.0f - ratio_;
    d_ += step;
    if (xf_ != 0) {
      d2_ += step;
      if (++xf_ > kXfade) {
        d_ = d2_;
        xf_ = 0;
      }
    } else if (d_ < kDelayLo || d_ > kDelayLo + period_) {
      jump();
    }
    if (++seg_n_ == kSeg) {
      seg_n_ = 0;
      tune_.push(seg_, kSeg);
      control();
    }
  }
}

}  // namespace o3_voc03er
