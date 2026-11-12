// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What SAMPLE LOAD does to the samples of a WAV on the way into a slot (a slot is mono, 16-bit, 44.1 kHz): the decode of 8, 16, 24 and 32-bit PCM and 32-bit
// float, the stereo mix, the rounding to 16 bits, and a streaming sample-rate converter (one windowed-sinc kernel, any rate from 22.05 to 96 kHz).
// Pure: no kit, no engine, no heap, float only (no double, no float <-> 64-bit integer conversion). Host tests in tests/sample_load_test.cpp.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace o3_voc03er::wav {

inline constexpr std::uint32_t kEngineRate = 44100;  // a slot's rate
inline constexpr std::uint32_t kMinRate = 22050;     // the rates a file may have
inline constexpr std::uint32_t kMaxRate = 96000;

// ---- a sample's value -------------------------------------------------------------------------------------------------------------------

// A sample as the file stores it, to 16-bit: `p` is its first byte. The integers are exact; the float is rounded to the nearest and clipped.
enum class Coding : std::uint8_t { Pcm, Float };

constexpr std::uint32_t bytes_per_sample(unsigned bits) noexcept { return (bits + 7u) / 8u; }

inline std::int32_t rd_le(const std::uint8_t* p, unsigned bytes) noexcept {  // little-endian, sign-extended from `bytes` (1 to 4); 1 byte is unsigned
  switch (bytes) {
    case 1: return static_cast<std::int32_t>(p[0]);
    case 2: return static_cast<std::int32_t>(static_cast<std::int16_t>(static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)));
    case 3: {
      const std::uint32_t u = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16);
      return static_cast<std::int32_t>(u << 8) >> 8;  // the arithmetic shift of a negative value is implementation-defined before C++20 but arithmetic on every target here
    }
    default:
      return static_cast<std::int32_t>(static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
                                       (static_cast<std::uint32_t>(p[3]) << 24));
  }
}

inline std::int16_t clip16(std::int32_t v) noexcept { return static_cast<std::int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); }

// A float in 16-bit units to the nearest 16-bit integer (a tie goes away from zero), clipped; NaN is 0. No dither: the rounding is the whole conversion.
inline std::int16_t round16(float x) noexcept {
  if (x != x) {
    return 0;
  }
  if (x >= 32767.0f) {
    return 32767;
  }
  if (x <= -32768.0f) {
    return -32768;
  }
  return static_cast<std::int16_t>(static_cast<std::int32_t>(x + (x >= 0.0f ? 0.5f : -0.5f)));
}

// One frame of `channels` (1 or 2) samples of `bits` and `coding` to one 16-bit mono sample, in integers where the file's are (a stereo pair is the mean). A 16-bit file
// comes out as its own samples (stereo: (L + R) / 2 truncated toward zero, which is what 16-bit files have always got); an 8, 24 or 32-bit one is rounded to nearest (a tie goes up).
inline std::int16_t frame_to_16(const std::uint8_t* p, unsigned bits, Coding coding, unsigned channels) noexcept {
  const unsigned bps = bytes_per_sample(bits);
  if (coding == Coding::Float) {
    float a;
    std::memcpy(&a, p, sizeof a);
    float v = a;
    if (channels == 2u) {
      float b;
      std::memcpy(&b, p + 4, sizeof b);
      v = (a + b) * 0.5f;  // an inf and its opposite make NaN: round16 turns it into 0
    }
    return round16(v * 32768.0f);
  }
  const std::int32_t a = rd_le(p, bps);
  const std::int32_t b = channels == 2u ? rd_le(p + bps, bps) : 0;
  switch (bits) {
    case 8: return static_cast<std::int16_t>(channels == 2u ? (a + b - 256) * 128 : (a - 128) * 256);  // unsigned 8-bit, exact in 16
    case 16: return static_cast<std::int16_t>(channels == 2u ? (a + b) / 2 : a);
    case 24: return clip16(channels == 2u ? (a + b + 256) >> 9 : (a + 128) >> 8);
    default: {  // 32
      const std::int32_t x = a >> 8;  // the top 24 bits; the rest is below half a 16-bit step of the sum
      const std::int32_t y = b >> 8;
      return clip16(channels == 2u ? (x + y + 256) >> 9 : (x + 128) >> 8);
    }
  }
}

// The same frame as a float in 16-bit units (what the converter takes); no rounding at all.
inline float frame_to_float(const std::uint8_t* p, unsigned bits, Coding coding, unsigned channels) noexcept {
  const unsigned bps = bytes_per_sample(bits);
  float scale = 1.0f;
  float a;
  float b = 0.0f;
  if (coding == Coding::Float) {
    std::memcpy(&a, p, sizeof a);
    if (channels == 2u) {
      std::memcpy(&b, p + 4, sizeof b);
    }
    scale = 32768.0f;
    // NaN and the infinities are not signal; a value far past full scale is clipped before it is mixed (a finite pair cannot overflow then)
    a = a != a ? 0.0f : (a > 2.0f ? 2.0f : (a < -2.0f ? -2.0f : a));
    b = b != b ? 0.0f : (b > 2.0f ? 2.0f : (b < -2.0f ? -2.0f : b));
  } else {
    std::int32_t ia = rd_le(p, bps);
    std::int32_t ib = channels == 2u ? rd_le(p + bps, bps) : 0;
    if (bits == 8) {
      ia = (ia - 128) * 256;
      ib = channels == 2u ? (ib - 128) * 256 : 0;
    }
    a = static_cast<float>(ia);
    b = static_cast<float>(ib);
    scale = bits == 24 ? 1.0f / 256.0f : (bits == 32 ? 1.0f / 65536.0f : 1.0f);
  }
  return (channels == 2u ? (a + b) * 0.5f : a) * scale;
}

// ---- the sample-rate converter -----------------------------------------------------------------------------------------------------------

// Windowed sinc, one table for every ratio: T(u) = sinc(kCut u) * Kaiser(u / kHalf) for u from 0 to kHalf, kPhases entries per unit of u, 16-bit (Q15), linear between
// entries. A kernel is that table stretched: u = x * g, where x is the distance in input samples from the output's position and g = fout / fin when the rate comes down (so
// the cut-off follows the new Nyquist) and 1 when it goes up. Every output is the weighted mean of the input around its position (the weights are divided by their sum, so
// the gain is 1 at every phase). The input is kept in a ring; the output position is a Q32 count of input samples, advanced by an integer step.
class Resampler {
 public:
  static constexpr unsigned kHalf = 16;                      // zero crossings of the kernel either side (in u)
  static constexpr unsigned kPhases = 64;                    // table entries per unit of u
  static constexpr unsigned kTableLen = kHalf * kPhases + 2;  // the last one is the 0 that the interpolation reads past the end
  static constexpr unsigned kRing = 128;                     // input samples kept: a power of two above 2 * (kHalf * 96000 / 44100 rounded up) + 2
  static constexpr float kCut = 0.86f;                       // the kernel's -6 dB point, as a fraction of the Nyquist of the lower rate
  static constexpr float kBeta = 7.0f;                       // Kaiser's beta: about 70 dB of stop band
  static_assert((kRing & (kRing - 1u)) == 0u && kRing >= 2u * 36u + 2u, "the ring holds a whole kernel at 96 kHz in");
  static_assert(static_cast<std::uint64_t>(kHalf) * kPhases * 65536u < (1ull << 31), "the table index fits a Q16 in 32 bits");

  // The table, built once (the first conversion after boot; idempotent). It runs in the main loop, inside the first write() of a converted file's data chunk. Cost [estimate]:
  // 1,026 entries of one sinf (about 200 cycles on the M4F with its single-precision FPU), one sqrt and a Bessel series of about 20 terms (a divide each, about 25 cycles a term),
  // so about 0.9 M cycles, 5 ms at 180 MHz, once. To be measured on the unit.
  void build_table() noexcept {
    if (built_) {
      return;
    }
    const float i0_beta = bessel_i0(kBeta);  // the Kaiser window's normaliser: the same for every entry
    for (unsigned j = 0; j < kTableLen; ++j) {
      const float u = static_cast<float>(j) / static_cast<float>(kPhases);
      float v = 0.0f;
      if (j <= kHalf * kPhases) {
        const float t = kCut * u;
        const float s = t < 1e-6f ? 1.0f : std::sin(kPi * t) / (kPi * t);
        const float x = u / static_cast<float>(kHalf);
        const float r = 1.0f - x * x;
        const float w = bessel_i0(kBeta * std::sqrt(r > 0.0f ? r : 0.0f)) / i0_beta;
        v = s * w;
      }
      table_[j] = round16(v * 32767.0f);
    }
    built_ = true;
  }
  bool built() const noexcept { return built_; }

  // Starts a conversion of `fin` to `fout` into `dst` (at most `cap` samples). false for a rate that is 0 (the caller has checked the range).
  bool begin(std::uint32_t fin, std::uint32_t fout, std::int16_t* dst, std::uint32_t cap) noexcept {
    if (fin == 0u || fout == 0u || dst == nullptr) {
      return false;
    }
    build_table();
    dst_ = dst;
    cap_ = cap;
    out_ = 0;
    frames_in_ = 0;
    pushed_ = 0;
    pos_ = 0;
    work_ = 0;
    step_pos_ = ((static_cast<std::uint64_t>(fin) << 32) + fout - 1u) / fout;  // rounded up: a whole second in is a whole second out, never one sample more
    // table index per input sample, Q16: kPhases * g
    const std::uint64_t g16 = fin > fout ? (static_cast<std::uint64_t>(fout) << 16) / fin : 65536u;  // g in Q16
    step_idx_ = static_cast<std::uint32_t>((g16 * kPhases));
    const std::uint32_t limit = kHalf * kPhases * 65536u;
    side_ = (limit + step_idx_ - 1u) / step_idx_;  // taps a side: the most that fit under the end of the table
    std::memset(ring_, 0, sizeof ring_);
    return true;
  }

  // One input sample, in 16-bit units (a mono mix). Produces what it makes possible.
  void push(float x) noexcept {
    ring_[pushed_ & (kRing - 1u)] = x;
    ++pushed_;
    ++frames_in_;
    produce(false);
  }
  // The end of the input: the output is flushed up to the end of the signal, the kernel cut at the last sample and renormalised (ceil(frames * fout / fin) samples in all).
  void finish() noexcept { produce(true); }

  std::uint32_t out() const noexcept { return out_; }
  bool full() const noexcept { return out_ >= cap_; }
  std::uint32_t work() const noexcept { return work_; }  // multiply-adds so far (the cost: about a dozen cycles each)
  std::uint32_t taps() const noexcept { return 2u * side_; }

 private:
  static constexpr float kPi = 3.14159265358979f;

  static float bessel_i0(float x) noexcept {
    float sum = 1.0f;
    float term = 1.0f;
    const float q = x * x * 0.25f;
    for (unsigned k = 1; k < 48u; ++k) {  // bounded: x is at most kBeta, and the terms are below 1e-9 of the sum long before
      term *= q / (static_cast<float>(k) * static_cast<float>(k));
      sum += term;
      if (term < 1e-9f * sum) {
        break;
      }
    }
    return sum;
  }

  // Outputs while the ring holds all the input the next one needs (or, flushing, while the next one is inside the signal).
  void produce(bool flush) noexcept {
    for (;;) {
      if (out_ >= cap_) {
        return;
      }
      const auto i0 = static_cast<std::uint32_t>(pos_ >> 32);
      if (flush) {
        if (pos_ >= (static_cast<std::uint64_t>(frames_in_) << 32)) {
          return;
        }
      } else if (pushed_ <= i0 + side_) {
        return;
      }
      dst_[out_++] = round16(one(i0, static_cast<std::uint32_t>(pos_ >> 16) & 0xFFFFu));
      pos_ += step_pos_;
    }
  }

  // The output at input position i0 + frac16 / 65536.
  float one(std::uint32_t i0, std::uint32_t frac16) noexcept {
    const std::uint32_t limit = kHalf * kPhases * 65536u;
    const std::uint32_t step = step_idx_;
    const auto d0 = static_cast<std::uint32_t>((static_cast<std::uint64_t>(frac16) * step) >> 16);  // table index of the sample at i0 (to its left)
    float acc = 0.0f;
    float wsum = 0.0f;
    unsigned taps = 0;
    // the samples at i0, i0 - 1, ... (none before the first input sample)
    std::uint32_t idx = d0;
    for (std::uint32_t m = i0 + 1u; m != 0u && idx < limit; --m, idx += step) {
      const float w = weight(idx);
      acc += w * ring_[(m - 1u) & (kRing - 1u)];
      wsum += w;
      ++taps;
    }
    // the samples at i0 + 1, i0 + 2, ... (none past the last input sample: flushing, the weights of the missing ones are left out of the sum too, so the end does not droop)
    idx = step - d0;
    for (std::uint32_t m = i0 + 1u; idx < limit && m < frames_in_; ++m, idx += step) {
      const float w = weight(idx);
      acc += w * ring_[m & (kRing - 1u)];
      wsum += w;
      ++taps;
    }
    work_ += taps;
    return wsum > 1.0f ? acc / wsum : 0.0f;
  }

  // The kernel at table index `idx` (Q16), as a number that is only worth something in proportion to the others: Q31 before the float.
  float weight(std::uint32_t idx) const noexcept {
    const std::uint32_t i = idx >> 16;
    const std::int32_t f = static_cast<std::int32_t>(idx & 0xFFFFu);
    const std::int32_t a = table_[i];
    const std::int32_t b = table_[i + 1u];
    return static_cast<float>(a * (65536 - f) + b * f);  // a convex mix of two 16-bit numbers times 65536: inside int32
  }

  alignas(4) std::int16_t table_[kTableLen] = {};
  alignas(4) float ring_[kRing] = {};
  bool built_ = false;
  std::int16_t* dst_ = nullptr;
  std::uint32_t cap_ = 0, out_ = 0, frames_in_ = 0, pushed_ = 0, step_idx_ = 0, side_ = 0;
  std::uint64_t pos_ = 0, step_pos_ = 0;
  std::uint32_t work_ = 0;
};

}  // namespace o3_voc03er::wav
