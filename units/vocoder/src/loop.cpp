// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/loop.h>

namespace o3_voc03er {

namespace {
constexpr float kToInt = 32767.0f;
constexpr float kToFloat = 1.0f / 32768.0f;
}  // namespace

void Loop::bind(std::int16_t* mem, std::uint32_t per_loop) noexcept {
  mem_ = per_loop != 0u ? mem : nullptr;
  cap_ = mem_ == nullptr ? 0u : (per_loop < kMaxSamples ? per_loop : kMaxSamples);
  clear();
}

void Loop::clear() noexcept {
  for (unsigned i = 0; i < kCount; ++i) {
    len_[i] = 0;
    h_[i] = 0;
    rec_[i] = false;
    playing_[i] = false;
    speed_[i] = 1;
    rev_[i] = false;
    gain_[i] = 1.0f;
    gcur_[i] = 1.0f;
  }
}

void Loop::begin_record(unsigned i) noexcept {
  if (mem_ == nullptr || i >= kCount) {
    return;
  }
  playing_[i] = false;
  h_[i] = 0;
  len_[i] = 0;
  rec_[i] = true;
}

std::uint32_t Loop::record(unsigned i, const float* in, std::uint32_t n) noexcept {
  if (i >= kCount || !rec_[i]) {
    return 0;
  }
  const std::uint32_t room = cap_ - len_[i];
  const std::uint32_t take = n < room ? n : room;
  std::int16_t* dst = mem_ + static_cast<std::size_t>(i) * cap_ + len_[i];
  for (std::uint32_t k = 0; k < take; ++k) {
    float x = in[k];
    x = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    dst[k] = static_cast<std::int16_t>(x * kToInt + (x >= 0.0f ? 0.5f : -0.5f));
  }
  len_[i] += take;
  if (len_[i] >= cap_) {
    end_record(i);
  }
  return take;
}

void Loop::end_record(unsigned i) noexcept {
  if (i >= kCount || !rec_[i]) {
    return;
  }
  rec_[i] = false;
  const std::uint32_t len = len_[i];
  std::int16_t* s = mem_ + static_cast<std::size_t>(i) * cap_;
  const std::uint32_t f = len / 2u < kFadeSamples ? len / 2u : kFadeSamples;
  if (f != 0u) {
    const float step = 1.0f / static_cast<float>(f);  // once a recording, not per sample
    for (std::uint32_t k = 0; k < f; ++k) {
      const float g = static_cast<float>(k) * step;  // 0 at the loop's first and last sample
      s[k] = static_cast<std::int16_t>(static_cast<float>(s[k]) * g);
      s[len - 1u - k] = static_cast<std::int16_t>(static_cast<float>(s[len - 1u - k]) * g);
    }
  }
}

bool Loop::start(unsigned i) noexcept {
  if (mem_ == nullptr || i >= kCount || rec_[i] || len_[i] == 0u) {
    return false;
  }
  h_[i] = rev_[i] ? 2u * (len_[i] - 1u) : 0u;
  gcur_[i] = gain_[i];
  playing_[i] = true;
  return true;
}

void Loop::stop(unsigned i) noexcept {
  if (i < kCount) {
    playing_[i] = false;
  }
}

void Loop::set_speed(unsigned i, unsigned speed) noexcept {
  if (i < kCount) {
    speed_[i] = static_cast<std::uint8_t>(speed <= 2u ? speed : 1u);
  }
}

void Loop::set_reverse(unsigned i, bool reverse) noexcept {
  if (i < kCount) {
    rev_[i] = reverse;
  }
}

void Loop::set_gain(unsigned i, float gain) noexcept {
  if (i < kCount) {
    gain_[i] = gain;
  }
}

namespace {

constexpr std::uint32_t kChunk = 64;                       // output samples a span copy serves
constexpr std::uint32_t kSpanMax = kChunk * 2u + 2u;       // 2x: two samples each, plus the interpolator's guard

// buf[j] = s[(start + j) mod len], one sequential pass (start < len, len >= 1).
void copy_forward(std::int16_t* buf, const std::int16_t* s, std::uint32_t len, std::uint32_t start, std::uint32_t count) noexcept {
  while (count != 0u) {
    const std::uint32_t seg = len - start < count ? len - start : count;
    for (std::uint32_t k = 0; k < seg; ++k) {
      buf[k] = s[start + k];
    }
    buf += seg;
    count -= seg;
    start = 0;
  }
}

// buf[j] = s[(start - j) mod len], reading backwards.
void copy_backward(std::int16_t* buf, const std::int16_t* s, std::uint32_t len, std::uint32_t start, std::uint32_t count) noexcept {
  while (count != 0u) {
    const std::uint32_t seg = start + 1u < count ? start + 1u : count;
    for (std::uint32_t k = 0; k < seg; ++k) {
      buf[k] = s[start - k];
    }
    buf += seg;
    count -= seg;
    start = len - 1u;
  }
}

}  // namespace

void Loop::mix(unsigned i, float* dst, std::uint32_t n, bool add) noexcept {
  if (i >= kCount || !playing_[i]) {
    return;
  }
  const std::int16_t* s = mem_ + static_cast<std::size_t>(i) * cap_;
  const std::uint32_t len = len_[i];
  const std::uint32_t len2 = 2u * len;
  const bool rev = rev_[i];
  const unsigned sp = speed_[i];
  const float g1 = gain_[i];
  std::uint32_t h = h_[i];
  if (sp == 1u && !rev) {
    h &= ~1u;  // the plain pass needs a whole sample
  }
  if (sp == 1u && !rev && gcur_[i] == g1) {  // the fast path: 1x forward, a settled level
    const float scale = kToFloat * g1;
    std::uint32_t p = h >> 1;
    while (n != 0u) {
      const std::uint32_t seg = len - p < n ? len - p : n;
      const std::int16_t* src = s + p;
      if (add) {
        for (std::uint32_t k = 0; k < seg; ++k) {
          dst[k] += static_cast<float>(src[k]) * scale;
        }
      } else {
        for (std::uint32_t k = 0; k < seg; ++k) {
          dst[k] = static_cast<float>(src[k]) * scale;
        }
      }
      dst += seg;
      n -= seg;
      p += seg;
      if (p >= len) {
        p = 0;
      }
    }
    h_[i] = p << 1;
    return;
  }
  const std::uint32_t step = sp == 0u ? 1u : (sp == 1u ? 2u : 4u);  // half samples a sample
  alignas(4) std::int16_t buf[kSpanMax + 2];
  static_assert((kSpanMax + 2u) % 2u == 0u, "the span buffer is whole words");
  float tmp[kChunk];
  float g = gcur_[i];
  while (n != 0u) {
    const std::uint32_t nn = n < kChunk ? n : kChunk;
    const std::int32_t last = static_cast<std::int32_t>((nn - 1u) * step);
    const std::uint32_t frac0 = h & 1u;
    const std::uint32_t b = h >> 1;
    if (!rev) {
      copy_forward(buf, s, len, b, static_cast<std::uint32_t>((static_cast<std::int32_t>(frac0) + last) >> 1) + 2u);
      std::uint32_t o = frac0;
      for (std::uint32_t k = 0; k < nn; ++k) {
        const std::uint32_t a = o >> 1;
        tmp[k] = (o & 1u) != 0u ? static_cast<float>(buf[a] + buf[a + 1]) * 0.5f : static_cast<float>(buf[a]);
        o += step;
      }
      h = (h + nn * step) % len2;
    } else {
      const std::uint32_t c = b + 1u < len ? b + 1u : 0u;  // the sample after b, where the backward copy starts
      const std::uint32_t mmax = static_cast<std::uint32_t>(-((static_cast<std::int32_t>(frac0) - last) >> 1));
      copy_backward(buf, s, len, c, mmax + 2u);
      std::int32_t o = static_cast<std::int32_t>(frac0);
      for (std::uint32_t k = 0; k < nn; ++k) {
        const std::uint32_t m = static_cast<std::uint32_t>(-(o >> 1));
        tmp[k] = (o & 1) != 0 ? static_cast<float>(buf[m + 1u] + buf[m]) * 0.5f : static_cast<float>(buf[m + 1u]);
        o -= static_cast<std::int32_t>(step);
      }
      h = (h + len2 - (nn * step) % len2) % len2;
    }
    const float dg = (g1 - g) / static_cast<float>(nn);  // once a chunk
    for (std::uint32_t k = 0; k < nn; ++k) {
      g += dg;
      tmp[k] *= kToFloat * g;
    }
    g = g1;  // the ramp ends on the target, exactly
    if (add) {
      for (std::uint32_t k = 0; k < nn; ++k) {
        dst[k] += tmp[k];
      }
    } else {
      for (std::uint32_t k = 0; k < nn; ++k) {
        dst[k] = tmp[k];
      }
    }
    dst += nn;
    n -= nn;
  }
  gcur_[i] = g;
  h_[i] = h;
}

}  // namespace o3_voc03er
