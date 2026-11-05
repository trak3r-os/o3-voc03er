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
    pos_[i] = 0;
    rec_[i] = false;
    playing_[i] = false;
  }
}

void Loop::begin_record(unsigned i) noexcept {
  if (mem_ == nullptr || i >= kCount) {
    return;
  }
  playing_[i] = false;
  pos_[i] = 0;
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
  pos_[i] = 0;
  playing_[i] = true;
  return true;
}

void Loop::stop(unsigned i) noexcept {
  if (i < kCount) {
    playing_[i] = false;
  }
}

void Loop::mix(unsigned i, float* dst, std::uint32_t n, bool add) noexcept {
  if (i >= kCount || !playing_[i]) {
    return;
  }
  const std::int16_t* s = mem_ + static_cast<std::size_t>(i) * cap_;
  const std::uint32_t len = len_[i];
  std::uint32_t p = pos_[i];
  while (n != 0u) {
    const std::uint32_t seg = len - p < n ? len - p : n;
    const std::int16_t* src = s + p;
    if (add) {
      for (std::uint32_t k = 0; k < seg; ++k) {
        dst[k] += static_cast<float>(src[k]) * kToFloat;
      }
    } else {
      for (std::uint32_t k = 0; k < seg; ++k) {
        dst[k] = static_cast<float>(src[k]) * kToFloat;
      }
    }
    dst += seg;
    n -= seg;
    p += seg;
    if (p >= len) {
      p = 0;
    }
  }
  pos_[i] = p;
}

}  // namespace o3_voc03er
