// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The arithmetic behind the TALK and TUNE voiceprints (docs/UI-DESIGN.md section 5.3). Pure, float only, no allocation: the main loop runs it.
//
// TALK: the lattice's reflection coefficients are stepped up to direct form (A(z) = 1 + sum a[i] z^-i, the sign convention of Talk's lattice, which
// is a[i] += k[m] a[m - i], a[m] = k[m]) and the envelope 1 / |A| is evaluated at 32 frequencies on the bands' axis, 125 Hz to 8 kHz in a log
// scale (point 0 and point 31 are the two ends, 1 kHz is between points 15 and 16). The lattice runs at fs / 4, so above its Nyquist (5.5 kHz at
// 44.1 kHz) there is no envelope: those points hold the value at Nyquist. Normalised to the line's own peak, 48 dB across the panel, so a quiet
// frame draws as large as a loud one (the filter has no gain of its own: Talk's level is a separate gain). Cost per call: the step-up is
// 18 x 17 / 2 multiply-adds, the evaluation 32 x 18 x 6 floating operations and 32 logarithms of a few operations each: about 5,000 operations,
// some 50,000 cycles [est] on the M4F, at 25 Hz about 0.7 % of the core, in the main loop.
//
// TUNE: the ladder shows 25 semitones, the centre note in the middle; geometry in cents so the detected pitch (which is between rungs) and the
// rungs share one scale.

#include <cmath>
#include <cstdint>
#include <cstring>

namespace o3_voc03er::voiceprint {

inline constexpr unsigned kPoints = 32;
inline constexpr unsigned kMaxOrder = 18;      // Talk::kMaxStages
inline constexpr float kLpcRate = 11025.0f;    // the lattice's rate at 44.1 kHz
inline constexpr float kMaxK = 0.995f;         // Talk::kMaxK: what the drawing trusts of a coefficient
inline constexpr float kRangeDb = 48.0f;       // across the panel
inline constexpr float kLoHz = 125.0f;
inline constexpr float kOctaves = 6.0f;        // 125 Hz to 8 kHz

// Cosines and sines of the 32 frequencies' angles at the lattice's rate, computed once (`axis_init`). Zero-initialised (it lives in the SDRAM).
struct Axis {
  float c[kPoints];
  float s[kPoints];
  bool ready;
};

// The frequency of point j on the bands' axis.
inline float point_hz(unsigned j) noexcept { return kLoHz * std::exp2(kOctaves * static_cast<float>(j) / static_cast<float>(kPoints - 1u)); }

inline void axis_init(Axis& a, float lpc_rate = kLpcRate) noexcept {
  for (unsigned j = 0; j < kPoints; ++j) {
    float w = 6.2831853f * point_hz(j) / lpc_rate;
    w = w > 3.1415927f ? 3.1415927f : w;  // past the lattice's Nyquist: hold the value there
    a.c[j] = std::cos(w);
    a.s[j] = std::sin(w);
  }
  a.ready = true;
}

// A coefficient the drawing can trust: NaN is 0, anything else is held to +-kMaxK.
inline float sane(float k) noexcept { return (k > -kMaxK) ? (k < kMaxK ? k : kMaxK) : (k <= -kMaxK ? -kMaxK : 0.0f); }

// Direct form from reflection: k[0..order-1] are the coefficients 1..order, a[0..order-1] come out the coefficients 1..order of A(z).
// order is held to kMaxOrder.
inline void step_up(const float* k, unsigned order, float* a) noexcept {
  order = order > kMaxOrder ? kMaxOrder : order;
  for (unsigned m = 0; m < order; ++m) {
    const float km = sane(k[m]);
    // a[0..m-1] are the previous stage's: a_new[i] = a[i] + km a[m - 1 - i], done in pairs so it runs in place.
    for (unsigned i = 0; i < (m + 1u) / 2u; ++i) {
      const unsigned j = m - 1u - i;
      const float ai = a[i], aj = a[j];
      a[i] = ai + km * aj;
      if (j != i) a[j] = aj + km * ai;
    }
    a[m] = km;
  }
}

// log2 of x > 0 to about 0.005 (0.015 dB of power): the exponent and a quadratic in the mantissa.
inline float fast_log2(float x) noexcept {
  std::uint32_t b;
  std::memcpy(&b, &x, sizeof b);
  const int e = static_cast<int>((b >> 23) & 0xFFu) - 127;
  const std::uint32_t mb = (b & 0x7FFFFFu) | 0x3F800000u;
  float m;
  std::memcpy(&m, &mb, sizeof m);
  const float f = m - 1.0f;
  return static_cast<float>(e) + f * (1.3465f - 0.3465f * f);
}

// The envelope of the filter 1 / A(z) at the 32 points, as 0..255 (255 the line's peak, 0 is 48 dB under it or lower). `order` coefficients
// k[0..order-1] in the reflection domain; order 0, or anything non-finite, gives a flat line.
inline void envelope(const Axis& ax, const float* k, unsigned order, std::uint8_t (&line)[kPoints]) noexcept {
  float a[kMaxOrder] = {};
  step_up(k, order, a);
  order = order > kMaxOrder ? kMaxOrder : order;
  float db[kPoints];
  float peak = -1.0e30f;
  for (unsigned j = 0; j < kPoints; ++j) {
    const float c = ax.c[j], s = ax.s[j];
    float cr = c, sr = s;  // cos and sin of i w, from i = 1
    float re = 1.0f, im = 0.0f;
    for (unsigned i = 0; i < order; ++i) {
      re += a[i] * cr;
      im -= a[i] * sr;
      const float nc = cr * c - sr * s;
      sr = sr * c + cr * s;
      cr = nc;
    }
    float p = re * re + im * im;  // |A|^2
    p = p > 1.0e-12f ? p : (p == p ? 1.0e-12f : 1.0f);
    p = p < 1.0e12f ? p : 1.0e12f;
    db[j] = -3.0103f * fast_log2(p);  // 10 log10 (1 / |A|^2)
    peak = db[j] > peak ? db[j] : peak;
  }
  for (unsigned j = 0; j < kPoints; ++j) {
    const float v = 1.0f - (peak - db[j]) * (1.0f / kRangeDb);
    line[j] = v <= 0.0f ? std::uint8_t{0} : (v >= 1.0f ? std::uint8_t{255} : static_cast<std::uint8_t>(v * 255.0f + 0.5f));
  }
}

// ---- TUNE's ladder ------------------------------------------------------------------------------------------------------------------------

inline constexpr int kSpan = 25;       // semitones shown: centre - 12 .. centre + 12
inline constexpr int kHalfSpan = 12;
inline constexpr int kDefaultCentre = 60;

// Where the ladder is centred: it stays until the target is more than six semitones away, then it moves to the target (the rungs do not
// jump with every note). centre 0 is "not set yet" (the screen's state starts at zero) and means middle C.
inline int ladder_follow(int centre, int target) noexcept {
  if (centre <= 0) centre = kDefaultCentre;
  if (target >= 0 && (target - centre > 6 || centre - target > 6)) centre = target;
  return centre;
}

// The y of a pitch that many cents above the centre, in a panel h high, 0 at the top: the centre is the middle, +-12 semitones are 6 px from
// the edges. A pitch outside is held at the edge.
inline int ladder_y(int cents_from_centre, int h) noexcept {
  cents_from_centre = cents_from_centre > 1250 ? 1250 : (cents_from_centre < -1250 ? -1250 : cents_from_centre);
  return h / 2 - (cents_from_centre * (h - 12)) / 2400;
}

struct Rung {
  std::int16_t y;
  bool root;
};

// The rungs of the notes in the scale (pitch classes in `mask`, bit 0 = C) in view, from the lowest up; `root` the KEY's. Returns how many.
inline unsigned ladder_rungs(int centre, std::uint16_t mask, unsigned key, int h, Rung (&out)[kSpan]) noexcept {
  unsigned n = 0;
  for (int d = -kHalfSpan; d <= kHalfSpan; ++d) {
    const int note = centre + d;
    if (note < 0 || note > 127) continue;
    const unsigned pc = static_cast<unsigned>(note) % 12u;
    if (((mask >> pc) & 1u) == 0u) continue;
    out[n].y = static_cast<std::int16_t>(ladder_y(d * 100, h));
    out[n].root = pc == key % 12u;
    ++n;
  }
  return n;
}

// The detected pitch, in cents from the centre: the target's note less the correction (the unit aims at the target; at AMOUNT 100 % the
// correction is exactly the way there). Unvoiced or no target yet: the centre.
inline int detected_cents(int centre, int target, int shift_cents) noexcept {
  return target < 0 ? 0 : (target - centre) * 100 - shift_cents;
}

}  // namespace o3_voc03er::voiceprint
