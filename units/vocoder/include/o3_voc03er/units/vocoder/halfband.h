// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the vocoder unit. The halfband pair shared by Bands (fs/2) and Talk (twice, fs/4): the textbook polyphase pair of first-order
// allpass sections in z^-2, H = (A0(z^2) + z^-1 A1(z^2)) / 2, coefficients 0.127 and 0.566 (passband 0..0.105 fs flat to 1e-6 dB, stopband
// from 0.395 fs at -70 dB). Any frame count works: the caller carries the sample phase (`odd`: in[0] is odd-indexed, counted from reset)
// and the state carries the half pair across calls.

#include <cstdint>

#include <o3_voc03er/units/vocoder/nofma.h>

namespace o3_voc03er {

// A halfband's state: x[n-1] and y[n-1] of the section of each branch, and `pend`, the value the next call needs (the odd-sample branch's
// output in a decimator, the second output of the pair in the interpolator).
struct Halfband {
  float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f, pend = 0.0f;
};

constexpr float kHbC0 = 0.12701027f;  // allpass coefficients: branch of the current sample, branch of the previous one
constexpr float kHbC1 = 0.5664591f;

// First-order allpass in z^-2 on the low-rate grid, one multiply: y = a (x - y[-1]) + x[-1].
inline float allpass(float a, float& x1, float& y1, float x) noexcept {
  const float y = mulb(a, x - y1) + x1;
  x1 = x;
  y1 = y;
  return y;
}

// Returns the number of outputs (one for each even-indexed input, `odd` says whether in[0] is odd-indexed).
inline std::uint32_t decimate(Halfband& h, const float* in, std::uint32_t n, bool odd, float* out) noexcept {
  std::uint32_t i = 0, k = 0;
  float pend = h.pend;
  if (odd && n > 0) {  // in[0] is the earlier sample of a pair: it goes through the z^-1 branch
    pend = allpass(kHbC1, h.x1, h.y1, in[0]);
    i = 1;
  }
  for (; i + 1 < n; i += 2) {
    out[k++] = 0.5f * (allpass(kHbC0, h.x0, h.y0, in[i]) + pend);
    pend = allpass(kHbC1, h.x1, h.y1, in[i + 1]);
  }
  if (i < n) {  // a last even sample: its pair completes in the next call
    out[k++] = 0.5f * (allpass(kHbC0, h.x0, h.y0, in[i]) + pend);
  }
  h.pend = pend;
  return k;
}

// Adds the interpolated samples into out[0..n) (the first output at an even-indexed sample; `in` holds one value for each pair).
inline void interpolate(Halfband& h, const float* in, std::uint32_t n, bool odd, float* out) noexcept {
  std::uint32_t i = 0;
  if (odd && n > 0) {  // the second output of the last call's last pair
    out[0] += h.pend;
    i = 1;
  }
  for (std::uint32_t m = 0; i < n; ++m, i += 2) {
    out[i] += allpass(kHbC0, h.x0, h.y0, in[m]);
    const float o1 = allpass(kHbC1, h.x1, h.y1, in[m]);
    if (i + 1 < n) {
      out[i + 1] += o1;
    } else {
      h.pend = o1;
    }
  }
}

inline void settle(Halfband& h) noexcept {
  constexpr float kFlush = 1.0e-15f;
  auto f = [](float s) { return (s > kFlush || s < -kFlush) ? s : 0.0f; };
  h.x0 = f(h.x0);
  h.y0 = f(h.y0);
  h.x1 = f(h.x1);
  h.y1 = f(h.y1);
  h.pend = f(h.pend);
}

}  // namespace o3_voc03er
