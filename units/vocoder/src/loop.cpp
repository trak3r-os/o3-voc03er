// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/loop.h>

#include <cstdint>
#include <cstring>

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
    st_s_[i] = 0;
    st_l_[i] = 0;
    region_off(i);
  }
}

void Loop::forget(unsigned i) noexcept {
  if (i >= kCount) {
    return;
  }
  playing_[i] = false;
  rec_[i] = false;
  h_[i] = 0;
  len_[i] = 0;
  region_off(i);
}

void Loop::begin_record(unsigned i) noexcept {
  if (mem_ == nullptr || i >= kCount) {
    return;
  }
  playing_[i] = false;
  h_[i] = 0;
  len_[i] = 0;
  rec_[i] = true;
  region_off(i);
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

// Loop memory is SDRAM on the system bus (uncached): read aligned 32-bit words, two samples a bus access.
// The address decides, never the index: at most one halfword head, whole words, at most one halfword tail,
// so nothing outside the segment is read. Target and host are little-endian: the low half of a word is the
// sample at the lower address.
inline bool word_aligned(const std::int16_t* p) noexcept {
  return (reinterpret_cast<std::uintptr_t>(p) & 2u) == 0u;
}
inline std::uint32_t load_word(const std::int16_t* p) noexcept {  // p word-aligned
  std::uint32_t w;
  std::memcpy(&w, p, sizeof w);
  return w;
}
inline std::int16_t lo_half(std::uint32_t w) noexcept {
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(w & 0xFFFFu));
}
inline std::int16_t hi_half(std::uint32_t w) noexcept {
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(w >> 16));
}

// out[k] = src[k] for k < seg.
void read_forward(std::int16_t* out, const std::int16_t* src, std::uint32_t seg) noexcept {
  if (seg != 0u && !word_aligned(src)) {
    *out++ = *src++;
    --seg;
  }
  for (std::uint32_t k = seg >> 1; k != 0u; --k) {
    const std::uint32_t w = load_word(src);
    out[0] = lo_half(w);
    out[1] = hi_half(w);
    out += 2;
    src += 2;
  }
  if ((seg & 1u) != 0u) {
    *out = *src;
  }
}

// out[k] = src[-k] for k < seg (src is the highest sample read).
void read_backward(std::int16_t* out, const std::int16_t* src, std::uint32_t seg) noexcept {
  if (seg != 0u && word_aligned(src)) {  // src is the low half of its word: the high half is outside
    *out++ = *src--;
    --seg;
  }
  for (std::uint32_t k = seg >> 1; k != 0u; --k) {  // src is a high half, src - 1 the word's base
    const std::uint32_t w = load_word(src - 1);
    out[0] = hi_half(w);
    out[1] = lo_half(w);
    out += 2;
    src -= 2;
  }
  if ((seg & 1u) != 0u) {
    *out = *src;
  }
}

// dst[k] (+)= src[k] * scale for k < seg.
template <bool Add>
void scale_forward(float* dst, const std::int16_t* src, std::uint32_t seg, float scale) noexcept {
  if (seg != 0u && !word_aligned(src)) {
    const float v = static_cast<float>(*src++) * scale;
    dst[0] = Add ? dst[0] + v : v;
    ++dst;
    --seg;
  }
  for (std::uint32_t k = seg >> 1; k != 0u; --k) {
    const std::uint32_t w = load_word(src);
    const float a = static_cast<float>(lo_half(w)) * scale;
    const float b = static_cast<float>(hi_half(w)) * scale;
    dst[0] = Add ? dst[0] + a : a;
    dst[1] = Add ? dst[1] + b : b;
    dst += 2;
    src += 2;
  }
  if ((seg & 1u) != 0u) {
    const float v = static_cast<float>(*src) * scale;
    dst[0] = Add ? dst[0] + v : v;
  }
}

// buf[j] = s[(start + j) mod len], one sequential pass (start < len, len >= 1).
void copy_forward(std::int16_t* buf, const std::int16_t* s, std::uint32_t len, std::uint32_t start, std::uint32_t count) noexcept {
  while (count != 0u) {
    const std::uint32_t seg = len - start < count ? len - start : count;
    read_forward(buf, s + start, seg);
    buf += seg;
    count -= seg;
    start = 0;
  }
}

// buf[j] = s[(start - j) mod len], reading backwards.
void copy_backward(std::int16_t* buf, const std::int16_t* s, std::uint32_t len, std::uint32_t start, std::uint32_t count) noexcept {
  while (count != 0u) {
    const std::uint32_t seg = start + 1u < count ? start + 1u : count;
    read_backward(buf, s + start, seg);
    buf += seg;
    count -= seg;
    start = len - 1u;
  }
}

// tmp[k] = the loop's sample at half position h -/+ k * step (linear between two), one sequential copy into buf first.
inline void fill_chunk(float* tmp, std::int16_t* buf, const std::int16_t* s, std::uint32_t len, std::uint32_t h, bool rev, std::uint32_t step,
                       std::uint32_t nn) noexcept {
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
        scale_forward<true>(dst, src, seg, scale);
      } else {
        scale_forward<false>(dst, src, seg, scale);
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
    fill_chunk(tmp, buf, s, len, h, rev, step, nn);
    if (!rev) {
      h = (h + nn * step) % len2;
    } else {
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

// ---- Regions ----

void Loop::region_off(unsigned i) noexcept {
  act_[i] = false;
  rst_[i] = false;
  cmd_[i] = Cmd::None;
  fn_[i] = 0;
  dir_[i] = 0;
}

void Loop::set_region_start(unsigned i, std::uint32_t start) noexcept {
  if (i < kCount) {
    st_s_[i] = start;
  }
}

void Loop::set_region_len(unsigned i, std::uint32_t len) noexcept {
  if (i < kCount) {
    st_l_[i] = len;
  }
}

bool Loop::trig_region(unsigned i, std::uint32_t frame) noexcept {
  if (mem_ == nullptr || i >= kCount || rec_[i] || st_l_[i] == 0u || st_s_[i] >= bank_samples()) {
    return false;
  }
  cmd_[i] = Cmd::Start;
  cf_[i] = static_cast<std::uint16_t>(frame);
  return true;
}

void Loop::release_region(unsigned i, std::uint32_t frame) noexcept {
  if (i >= kCount) {
    return;
  }
  if (!act_[i]) {
    cmd_[i] = Cmd::None;  // a start of this block that was not applied yet is cancelled
    return;
  }
  cmd_[i] = Cmd::Release;
  cf_[i] = static_cast<std::uint16_t>(frame);
}

// Begins the staged region (the memory clamps it); false when it is empty.
bool Loop::begin_region(unsigned i) noexcept {
  const std::uint32_t bank = bank_samples();
  const std::uint32_t a = st_s_[i];
  if (a >= bank || st_l_[i] == 0u) {
    return false;
  }
  const std::uint32_t l = st_l_[i] < bank - a ? st_l_[i] : bank - a;
  rs_[i] = a;
  rl_[i] = l;
  rh_[i] = rev_[i] ? 2u * (l - 1u) : 0u;
  rg_[i] = gain_[i];
  fn_[i] = 0;
  dir_[i] = 1;
  rst_[i] = false;
  act_[i] = true;
  return true;
}

// One chunk (up to kChunk samples) of the sounding region at dst[0..n): returns how many it wrote. The fade count is a whole number of
// samples, so the ramps are exact; the natural end starts its fade-out when as many samples are left as the fade is high, so it ends at zero.
std::uint32_t Loop::region_chunk(unsigned i, float* dst, std::uint32_t n, bool add) noexcept {
  const bool rev = rev_[i];
  const unsigned sp = speed_[i];
  const std::uint32_t sh = sp == 0u ? 0u : (sp == 1u ? 1u : 2u);
  const std::uint32_t step = 1u << sh;  // half samples a sample
  const std::uint32_t len = rl_[i];
  std::uint32_t h = rh_[i];
  const std::uint32_t last = 2u * (len - 1u);
  if (h > last) {
    h = last;
  }
  const std::uint32_t outs = ((rev ? h : last - h) >> sh) + 1u;  // samples left, this one included
  std::uint32_t fn = fn_[i];
  std::int32_t dir = dir_[i];
  std::uint32_t nn = n < kChunk ? n : kChunk;
  if (nn > outs) {
    nn = outs;
  }
  if (dir >= 0 && outs <= fn) {
    dir = -1;  // the end is near: ramp down so that it reaches zero on the last sample
  }
  if (dir > 0) {
    const std::uint32_t room = kFadeSamples - fn;
    const std::uint32_t half = ((outs - fn) >> 1) + 1u;  // do not run past the point where the end's ramp must begin
    nn = nn < room ? nn : room;
    nn = nn < half ? nn : half;
  } else if (dir < 0) {
    nn = nn < fn ? nn : (fn != 0u ? fn : 1u);
  } else if (outs > kFadeSamples && nn > outs - kFadeSamples) {
    nn = outs - kFadeSamples;
  }
  const std::int16_t* s = mem_ + rs_[i];
  alignas(4) std::int16_t buf[kSpanMax + 2];
  float tmp[kChunk];
  fill_chunk(tmp, buf, s, len, h, rev, step, nn);
  const float g1 = gain_[i];
  float g = rg_[i];
  const float dg = (g1 - g) / static_cast<float>(nn);
  if (dir == 0) {
    for (std::uint32_t k = 0; k < nn; ++k) {
      g += dg;
      tmp[k] *= kToFloat * g;
    }
  } else {
    const std::int32_t d = dir;
    for (std::uint32_t k = 0; k < nn; ++k) {
      g += dg;
      fn = static_cast<std::uint32_t>(static_cast<std::int32_t>(fn) + d);
      tmp[k] *= kToFloat * g * (static_cast<float>(fn) * (1.0f / static_cast<float>(kFadeSamples)));
    }
  }
  rg_[i] = g1;
  if (add) {
    for (std::uint32_t k = 0; k < nn; ++k) {
      dst[k] += tmp[k];
    }
  } else {
    for (std::uint32_t k = 0; k < nn; ++k) {
      dst[k] = tmp[k];
    }
  }
  const std::uint32_t adv = nn * step;
  rh_[i] = rev ? (h >= adv ? h - adv : 0u) : h + adv;
  if (dir > 0 && fn >= kFadeSamples) {
    dir = 0;
  }
  fn_[i] = static_cast<std::uint8_t>(fn);
  dir_[i] = static_cast<std::int8_t>(dir);
  if (nn == outs || (dir < 0 && fn == 0u)) {  // the region is over
    act_[i] = false;
    if (rst_[i]) {
      (void)begin_region(i);  // a retrigger: the new region follows the fade-out at once
    }
    if (!act_[i]) {
      region_off(i);
    }
  }
  return nn;
}

// Plays [from, to) of dst: the region while there is one, the loop (a no-op when it is not playing) once there is none.
std::uint32_t Loop::advance(unsigned i, float* dst, std::uint32_t from, std::uint32_t to, bool add) noexcept {
  while (from < to) {
    if (!act_[i]) {
      mix(i, dst + from, to - from, add);
      return to;
    }
    from += region_chunk(i, dst + from, to - from, add);
  }
  return to;
}

void Loop::mix_region(unsigned i, float* dst, std::uint32_t n, bool add) noexcept {
  const Cmd c = cmd_[i];
  std::uint32_t at = 0;
  if (c != Cmd::None) {
    const std::uint32_t cf = cf_[i] < n ? cf_[i] : n;
    at = advance(i, dst, 0, cf, add);
    cmd_[i] = Cmd::None;
    if (c == Cmd::Release) {
      if (act_[i] && fn_[i] == 0u) {  // not audible yet
        region_off(i);
      } else if (act_[i]) {
        dir_[i] = -1;
        rst_[i] = false;
      }
    } else if (act_[i]) {  // a retrigger: fade the old one out, the new one starts when it is silent
      if (fn_[i] == 0u) {
        (void)begin_region(i);
      } else {
        dir_[i] = -1;
        rst_[i] = true;
      }
    } else {
      // The loop that was playing ducks over the first ramp (its own level ramp to zero, then back at its resume) while the region fades in.
      const std::uint32_t nd = n - at < kFadeSamples ? n - at : kFadeSamples;
      const bool duck = playing_[i] && nd != 0u && st_l_[i] >= 4u * kFadeSamples;
      if (begin_region(i) && duck) {
        const float g = gain_[i];
        gain_[i] = 0.0f;
        mix(i, dst + at, nd, add);
        gain_[i] = g;
        at = advance(i, dst, at, at + nd, true);
      }
    }
  }
  advance(i, dst, at, n, add);
}

void Loop::play(unsigned i, float* dst, std::uint32_t n, bool add) noexcept {
  if (i < kCount && (act_[i] || cmd_[i] != Cmd::None)) {
    mix_region(i, dst, n, add);
  } else {
    mix(i, dst, n, add);
  }
}

std::uint32_t Loop::bank_position(unsigned i) const noexcept {
  if (i >= kCount) {
    return 0;
  }
  return act_[i] ? rs_[i] + (rh_[i] >> 1) : static_cast<std::uint32_t>(i) * cap_ + (h_[i] >> 1);
}

}  // namespace o3_voc03er
