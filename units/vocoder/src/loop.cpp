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
    st_v_[i] = 100;
    st_r_[i] = false;
    rv_[i] = 100;
    bars_[i] = 0;
    k0_[i] = 0;
    drift_[i] = false;
    hf_[i] = 0;
    rq_[i] = kRatioOne;
    ghost_off(i);
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
  hf_[i] = 0;
  len_[i] = 0;
  bars_[i] = 0;
  ghost_off(i);
  region_off(i);
}

bool Loop::adopt(unsigned i, std::uint32_t length) noexcept {
  if (mem_ == nullptr || i >= kCount || rec_[i] || playing_[i] || region(i) || length > cap_) {
    return false;
  }
  if (length != 0u && length < len_[i]) {  // the seam end_record() baked is cut off: ramp the new end to zero, or the wrap clicks
    std::int16_t* s = mem_ + static_cast<std::size_t>(i) * cap_;
    const std::uint32_t f = length / 2u < kFadeSamples ? length / 2u : kFadeSamples;
    const float step = f != 0u ? 1.0f / static_cast<float>(f) : 0.0f;
    for (std::uint32_t k = 0; k < f; ++k) {
      s[length - 1u - k] = static_cast<std::int16_t>(static_cast<float>(s[length - 1u - k]) * (static_cast<float>(k) * step));
    }
  }
  h_[i] = 0;
  hf_[i] = 0;
  len_[i] = length;
  bars_[i] = 0;
  ghost_off(i);
  return true;
}

void Loop::begin_record(unsigned i) noexcept {
  if (mem_ == nullptr || i >= kCount) {
    return;
  }
  playing_[i] = false;
  h_[i] = 0;
  hf_[i] = 0;
  len_[i] = 0;
  rec_[i] = true;
  bars_[i] = 0;
  ghost_off(i);
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

bool Loop::start(unsigned i, std::uint32_t bar) noexcept {
  if (mem_ == nullptr || i >= kCount || rec_[i] || len_[i] == 0u) {
    return false;
  }
  h_[i] = rev_[i] ? 2u * (len_[i] - 1u) : 0u;
  hf_[i] = 0;
  rq_[i] = bq_;  // a loop that starts while bent plays at the bent speed from its first sample
  gcur_[i] = gain_[i];
  k0_[i] = bar;
  drift_[i] = false;
  sf_[i] = -1;
  playing_[i] = true;
  return true;
}

// The loop does not cut: from where it is, the ghost carries it on for kFadeSamples more, fading to zero (mix_slow). Not when a region is
// sounding over it (the loop is paused under the region: mix() is not what is heard), nor when a jump fade is still running.
void Loop::stop(unsigned i) noexcept {
  if (i >= kCount) {
    return;
  }
  if (playing_[i] && !region(i) && gn_[i] == 0u) {
    const unsigned sp = speed_[i];
    gh_[i] = (sp == 1u && !rev_[i]) ? (h_[i] & ~1u) : h_[i];
    gs_[i] = static_cast<std::uint8_t>(sp == 0u ? 1u : (sp == 1u ? 2u : 4u));
    gv_[i] = rev_[i];
    gm_[i] = false;
    gc_[i] = 0.0f;
    if (hf_[i] != 0u || gh_[i] != h_[i]) {  // a bent loop sits between two half samples: the tail starts from what it was about to play
      const std::int16_t* s = mem_ + static_cast<std::size_t>(i) * cap_;
      const std::uint32_t len = len_[i];
      const std::uint32_t gb = gh_[i] >> 1;
      const std::uint32_t b = h_[i] >> 1;
      const float va = static_cast<float>(s[gb]);
      const float v0 = (gh_[i] & 1u) != 0u ? (va + static_cast<float>(s[gb + 1u < len ? gb + 1u : 0u])) * 0.5f : va;
      const std::uint64_t u = (static_cast<std::uint64_t>(h_[i] & 1u) << 32) | hf_[i];
      const float fr = static_cast<float>(static_cast<std::uint32_t>(u >> 9) & 0xFFFFFFu) * (1.0f / 16777216.0f);
      const float a = static_cast<float>(s[b]);
      const float c = static_cast<float>(s[b + 1u < len ? b + 1u : 0u]);
      gc_[i] = a + fr * (c - a) - v0;
    }
    gn_[i] = static_cast<std::uint8_t>(kFadeSamples);
  }
  playing_[i] = false;
  sf_[i] = -1;
}

void Loop::set_bars(unsigned i, std::uint32_t bars) noexcept {
  if (i < kCount) {
    bars_[i] = static_cast<std::uint16_t>(bars < 0xFFFFu ? bars : 0xFFFFu);
    drift_[i] = false;
  }
}

// Where the cycle stands at bar `bar` (see loop.h), staged as a jump at `frame` unless the loop is already there.
void Loop::bar_line(unsigned i, std::uint32_t frame, std::uint32_t bar) noexcept {
  if (i >= kCount) {
    return;
  }
  if (bar == 0u) {
    reanchor(i);
  }
  const std::uint64_t n = bars_[i];
  const std::uint32_t len = len_[i];
  if (n == 0u || len == 0u || !playing_[i] || gn_[i] != 0u || region(i)) {
    return;
  }
  if (loop_bent(i)) {
    return;  // a bent loop drifts from the bars by design: the first line after the bend is back at 0 puts it right
  }
  const unsigned sp = speed_[i];
  const bool rev = rev_[i];
  const std::uint64_t step = sp == 0u ? 1u : (sp == 1u ? 2u : 4u);
  const std::uint64_t len2 = 2ull * len;
  const std::uint64_t m = 2ull * n;
  const std::uint64_t j = (bar % m + m - k0_[i] % m) % m;
  const std::uint64_t x = (j * step * len + n / 2u) / n % len2;
  std::uint64_t e = rev ? (2ull * (len - 1u) + len2 - x) % len2 : x;
  std::uint64_t cur = h_[i];
  if (sp == 1u && !rev) {
    e &= ~1ull;  // the plain pass plays whole samples
    cur &= ~1ull;
  }
  const std::uint64_t adv = static_cast<std::uint64_t>(frame) * step % len2;
  cur = rev ? (cur + len2 - adv) % len2 : (cur + adv) % len2;
  const std::uint64_t d = (cur + len2 - e) % len2;
  drift_[i] = false;  // the line puts the loop on the cycle (or finds it there)
  if (d <= 4u || len2 - d <= 4u) {
    return;  // already on the grid: nothing to change, nothing to hear
  }
  // A seam line (the grid says the first sample) with the loop a few samples off: a bar that is not a whole number of samples (123 BPM) makes
  // the loop slip a fraction of a sample a cycle, and once the slip is past the tolerance it is put back here, where the baked fades sit at
  // about zero: a snap at `frame`, no ghost, nothing to hear. Anywhere else, or when the error is larger, the jump crossfades.
  sn_[i] = x == 0u && (d <= kSnap || len2 - d <= kSnap);
  sf_[i] = static_cast<std::int32_t>(frame);
  sj_[i] = static_cast<std::uint32_t>(e);
}

void Loop::reanchor(unsigned i) noexcept {
  if (i < kCount) {
    k0_[i] = 0;
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

// The ratio in Q24, 0.5 .. 2 (NaN is 1): 1.0f is exactly kRatioOne, so bend 0 is the integer paths, bit for bit.
void Loop::set_bend(float ratio) noexcept {
  if (ratio != ratio) {
    ratio = 1.0f;
  }
  ratio = ratio < 0.5f ? 0.5f : (ratio > 2.0f ? 2.0f : ratio);
  bq_ = static_cast<std::uint32_t>(ratio * 16777216.0f + 0.5f);
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

// ---- The bend: a fixed-point phase ----
// A bent reader keeps its position as a Q33 phase u (whole samples in the bits above 33, 2^-33 of a sample below; one uint64) relative to
// the first sample of a span copied into buf, and the speed as qs, half samples a sample in Q24 (step * ratio): the phase moves by qs << 8
// a sample, and qs moves by dqs a sample (a linear glide of the ratio over the chunk). A reverse reader is mirrored: its span is copied
// backwards from the sample above its base, so the phase grows in both directions and one loop serves both.
constexpr std::uint32_t kBentSpan = kChunk * 4u + 4u;  // 4 samples a sample at most (2x times an octave up), plus the interpolator's guard
constexpr std::uint32_t kSettleMin = 32;               // the shortest chunk that may land on the grid (the speed then stays within 2 %)

inline float lerp_at(const std::int16_t* buf, std::uint64_t u) noexcept {
  const std::uint32_t idx = static_cast<std::uint32_t>(u >> 33);
  const float fr = static_cast<float>(static_cast<std::uint32_t>(u >> 9) & 0xFFFFFFu) * (1.0f / 16777216.0f);  // 24 bits: exact
  const float a = static_cast<float>(buf[idx]);
  const float b = static_cast<float>(buf[idx + 1u]);
  return a + fr * (b - a);
}

// tmp[k] = buf at the phase, k < nn; u ends one step past the last sample.
void run_bent(float* tmp, const std::int16_t* buf, std::uint64_t& u, std::uint32_t qs, std::int32_t dqs, std::uint32_t nn) noexcept {
  std::uint64_t p = u;
  for (std::uint32_t k = 0; k < nn; ++k) {
    tmp[k] = lerp_at(buf, p);
    qs = static_cast<std::uint32_t>(static_cast<std::int32_t>(qs) + dqs);
    p += static_cast<std::uint64_t>(qs) << 8;
  }
  u = p;
}

// The same for a region near its end: stops at the first sample past `lim` (returns how many it wrote), and fades the samples within `zone`
// of the end (distance in Q33 samples, the fade's length at the nominal speed) by their distance: 1 at the zone's edge, 0 at the end.
std::uint32_t run_bent_end(float* tmp, const std::int16_t* buf, std::uint64_t& u, std::uint32_t qs, std::int32_t dqs, std::uint32_t nn,
                           std::uint64_t lim, std::uint64_t zone, float zinv) noexcept {
  std::uint64_t p = u;
  std::uint32_t k = 0;
  for (; k < nn && p <= lim; ++k) {
    float v = lerp_at(buf, p);
    const std::uint64_t rem = lim - p;
    if (rem < zone) {
      v *= static_cast<float>(static_cast<std::uint32_t>(rem >> 16)) * zinv;  // < 2^24: exact
    }
    tmp[k] = v;
    qs = static_cast<std::uint32_t>(static_cast<std::int32_t>(qs) + dqs);
    p += static_cast<std::uint64_t>(qs) << 8;
  }
  u = p;
  return k;
}

// Where a phase u (relative to base sample b, see above) is, as a loop's half position and the fraction past it (wrapping mod len).
inline void phase_to_loop(std::uint64_t u, bool rev, std::uint32_t b, std::uint32_t len, std::uint32_t& h, std::uint32_t& hf) noexcept {
  const std::uint32_t di = static_cast<std::uint32_t>(u >> 33);
  const std::uint64_t df = u & ((1ull << 33) - 1u);
  if (!rev) {
    h = ((b + di) % len) * 2u + static_cast<std::uint32_t>(df >> 32);
    hf = static_cast<std::uint32_t>(df);
    return;
  }
  const std::uint32_t w = (b + 1u + len - di % len) % len;  // the sample below the one the phase is in, +1
  if (df == 0u) {
    h = w * 2u;
    hf = 0;
    return;
  }
  const std::uint64_t pf = (1ull << 33) - df;
  h = ((w + len - 1u) % len) * 2u + static_cast<std::uint32_t>(pf >> 32);
  hf = static_cast<std::uint32_t>(pf);
}

}  // namespace

void Loop::mix(unsigned i, float* dst, std::uint32_t n, bool add) noexcept {
  if (i >= kCount) {
    return;
  }
  if (gn_[i] == 0u && sf_[i] < 0) {  // the common block: no ghost, no jump
    if (playing_[i]) {
      mix_plain(i, dst, n, add);
    }
    return;
  }
  mix_slow(i, dst, n, add);
}

void Loop::mix_plain(unsigned i, float* dst, std::uint32_t n, bool add) noexcept {
  if (loop_bent(i)) {
    const std::uint32_t done = mix_bent(i, dst, n, add);  // until the bend is settled: the integer paths take the rest
    dst += done;
    n -= done;
    if (n == 0u) {
      return;
    }
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

// The bent loop (see loop.h): chunks of at most kChunk from the fixed-point phase, until `n` is done or the bend is settled (the target ratio
// is 1, the speed has got there and the phase is on the grid), which it reports so that the integer paths play the rest. When the target is 1
// and the speed already is, one chunk of kSettleMin or more lands exactly on the grid at a speed within 2 % of the nominal one. The grid is
// the one the integer paths play on: a whole sample at 1x and 2x (forward or reverse: the plain pass keeps the position's parity, so a half
// sample left over would stay a two-sample average for good), a half sample at 0.5x (where the position alternates anyway).
std::uint32_t Loop::mix_bent(unsigned i, float* dst, std::uint32_t n, bool add) noexcept {
  drift_[i] = true;  // off the bar cycle (only read for a loop with bars)
  const std::int16_t* s = mem_ + static_cast<std::size_t>(i) * cap_;
  const std::uint32_t len = len_[i];
  const bool rev = rev_[i];
  const unsigned sp = speed_[i];
  const std::uint32_t step = sp == 0u ? 1u : (sp == 1u ? 2u : 4u);  // half samples a sample at the ratio 1
  const std::uint64_t grid = step == 1u ? (1ull << 32) : (1ull << 33);  // 0.5x plays half samples; 1x and 2x whole ones (see below)
  const float g1 = gain_[i];
  const std::uint32_t tgt = bq_;
  std::uint32_t cur = rq_[i];
  std::uint32_t h = h_[i];
  std::uint32_t hf = hf_[i];
  float g = gcur_[i];
  alignas(4) std::int16_t buf[kBentSpan];
  static_assert(kBentSpan % 2u == 0u, "the span buffer is whole words");
  float tmp[kChunk];
  std::uint32_t done = 0;
  while (done < n && (tgt != kRatioOne || cur != kRatioOne || hf != 0u || (sp != 0u && (h & 1u) != 0u))) {
    const std::uint32_t nn = n - done < kChunk ? n - done : kChunk;
    const std::uint32_t b = h >> 1;
    const std::uint64_t f0 = (static_cast<std::uint64_t>(h & 1u) << 32) | hf;
    std::uint64_t u = rev ? (1ull << 33) - f0 : f0;
    std::uint32_t qs = step * cur;
    std::int32_t dqs = 0;
    std::uint32_t qmax = qs;
    bool settle = false;
    std::uint64_t uend = 0;
    if (tgt == kRatioOne && cur == kRatioOne) {
      if (nn >= kSettleMin) {  // off the grid here: land on it
        const std::uint64_t anom = static_cast<std::uint64_t>(nn * step) << 32;  // the nominal advance of the chunk
        uend = (u + anom + (grid >> 1)) & ~(grid - 1u);
        qs = (static_cast<std::uint32_t>((uend - u) >> 14) / nn) << 6;  // (uend - u) / nn, in qs units; 2^-13 of a sample of error at most
        qmax = qs;
        settle = true;
      }
    } else {
      const std::uint32_t qt = step * tgt;
      dqs = (static_cast<std::int32_t>(qt) - static_cast<std::int32_t>(qs)) / static_cast<std::int32_t>(nn);
      qmax = qt > qs ? qt : qs;
    }
    const std::uint64_t reach = u + ((static_cast<std::uint64_t>(qmax) * nn) << 8);  // no sample is read past this phase
    const std::uint32_t count = static_cast<std::uint32_t>(reach >> 33) + 3u;  // <= kBentSpan: qmax <= 2^27, nn <= 64
    if (!rev) {
      copy_forward(buf, s, len, b, count);
    } else {
      copy_backward(buf, s, len, b + 1u < len ? b + 1u : 0u, count);
    }
    run_bent(tmp, buf, u, qs, dqs, nn);
    if (settle) {
      u = uend;
    }
    phase_to_loop(u, rev, b, len, h, hf);
    cur = settle ? cur : tgt;  // the glide ends on the target
    const float dg = (g1 - g) / static_cast<float>(nn);
    for (std::uint32_t k = 0; k < nn; ++k) {
      g += dg;
      tmp[k] *= kToFloat * g;
    }
    g = g1;
    if (add) {
      for (std::uint32_t k = 0; k < nn; ++k) {
        dst[done + k] += tmp[k];
      }
    } else {
      for (std::uint32_t k = 0; k < nn; ++k) {
        dst[done + k] = tmp[k];
      }
    }
    done += nn;
  }
  gcur_[i] = g;
  rq_[i] = cur;
  h_[i] = h;
  hf_[i] = hf;
  return done;
}

// The blocks with a jump staged or a ghost running (a bar line the loop was off, or 64 samples after a stop), in chunks of at most kChunk
// that end where something changes: a staged jump at its frame (the old position becomes the ghost, the new one the main), the ghost's end.
// The ghost's weight w falls (gn - k) / kFadeSamples from 1; the main is full under a stop tail and 1 - w under a jump. One more span copy
// a chunk, for at most two chunks a bar line.
void Loop::mix_slow(unsigned i, float* dst, std::uint32_t n, bool add) noexcept {
  const std::int16_t* s = mem_ + static_cast<std::size_t>(i) * cap_;
  const std::uint32_t len = len_[i];
  const std::uint32_t len2 = 2u * len;
  const bool rev = rev_[i];
  const unsigned sp = speed_[i];
  const float g1 = gain_[i];
  const bool on = playing_[i];
  std::uint32_t h = h_[i];
  if (sp == 1u && !rev) {
    h &= ~1u;
  }
  const std::uint32_t step = sp == 0u ? 1u : (sp == 1u ? 2u : 4u);
  alignas(4) std::int16_t buf[kSpanMax + 2];
  float tmp[kChunk];
  float gt[kChunk];
  float g = gcur_[i];
  std::uint32_t done = 0;
  while (done < n) {
    if (sf_[i] >= 0 && static_cast<std::uint32_t>(sf_[i]) <= done) {
      if (on && gn_[i] == 0u && !sn_[i]) {
        gh_[i] = h;
        gs_[i] = static_cast<std::uint8_t>(step);
        gv_[i] = rev;
        gm_[i] = true;
        gc_[i] = 0.0f;
        gn_[i] = static_cast<std::uint8_t>(kFadeSamples);
      }
      h = sj_[i];
      sf_[i] = -1;
    }
    std::uint32_t nn = n - done < kChunk ? n - done : kChunk;
    if (sf_[i] >= 0 && static_cast<std::uint32_t>(sf_[i]) - done < nn) {
      nn = static_cast<std::uint32_t>(sf_[i]) - done;
    }
    if (gn_[i] != 0u && gn_[i] < nn) {
      nn = gn_[i];
    }
    if (on) {
      fill_chunk(tmp, buf, s, len, h, rev, step, nn);
      h = rev ? (h + len2 - (nn * step) % len2) % len2 : (h + nn * step) % len2;
    } else {
      for (std::uint32_t k = 0; k < nn; ++k) {
        tmp[k] = 0.0f;
      }
    }
    if (gn_[i] != 0u) {
      const std::uint32_t gstep = gs_[i];
      fill_chunk(gt, buf, s, len, gh_[i], gv_[i], gstep, nn);
      gh_[i] = gv_[i] ? (gh_[i] + len2 - (nn * gstep) % len2) % len2 : (gh_[i] + nn * gstep) % len2;
      if (gc_[i] != 0.0f) {  // the tail of a bent loop: its first sample is where the bend was, and the offset fades out with the tail
        for (std::uint32_t k = 0; k < nn; ++k) {
          gt[k] += gc_[i];
        }
      }
      constexpr float kInvFade = 1.0f / static_cast<float>(kFadeSamples);  // a power of two: exact
      for (std::uint32_t k = 0; k < nn; ++k) {
        const float w = static_cast<float>(gn_[i] - k) * kInvFade;
        tmp[k] = (gm_[i] ? tmp[k] * (1.0f - w) : tmp[k]) + gt[k] * w;
      }
      gn_[i] = static_cast<std::uint8_t>(gn_[i] - nn);
    }
    const float dg = (g1 - g) / static_cast<float>(nn);
    for (std::uint32_t k = 0; k < nn; ++k) {
      g += dg;
      tmp[k] *= kToFloat * g;
    }
    g = g1;
    if (add) {
      for (std::uint32_t k = 0; k < nn; ++k) {
        dst[done + k] += tmp[k];
      }
    } else {
      for (std::uint32_t k = 0; k < nn; ++k) {
        dst[done + k] = tmp[k];
      }
    }
    done += nn;
  }
  sf_[i] = -1;
  gcur_[i] = g;
  if (on) {
    h_[i] = h;
    hf_[i] = 0;  // the main played at the nominal speed on whole half samples under the ghost
  }
}

// ---- Regions ----

void Loop::region_off(unsigned i) noexcept {
  act_[i] = false;
  rst_[i] = false;
  rep_[i] = false;
  cmd_[i] = Cmd::None;
  fn_[i] = 0;
  dir_[i] = 0;
  rhf_[i] = 0;
  rrq_[i] = kRatioOne;
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

bool Loop::trig_region(unsigned i, std::uint32_t frame, std::uint8_t level, bool repeat) noexcept {
  if (mem_ == nullptr || i >= kCount || rec_[i] || st_l_[i] == 0u || st_s_[i] >= bank_samples()) {
    return false;
  }
  st_v_[i] = level < 100u ? level : std::uint8_t{100};
  st_r_[i] = repeat;
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

// Begins the staged region (the memory clamps it); false when it is empty. `again` is a repeat's next pass: it begins the span (rs_, rl_) and the level the
// trigger latched, not what is staged now, so a Start / Len written for the next pad between two passes never plays (the staged values are for the next Trig).
bool Loop::begin_region(unsigned i, bool again) noexcept {
  const std::uint32_t bank = bank_samples();
  const std::uint32_t a = again ? rs_[i] : st_s_[i];
  const std::uint32_t sl = again ? rl_[i] : st_l_[i];
  if (a >= bank || sl == 0u) {
    return false;
  }
  const std::uint32_t l = sl < bank - a ? sl : bank - a;
  rs_[i] = a;
  rl_[i] = l;
  rh_[i] = rev_[i] ? 2u * (l - 1u) : 0u;
  rhf_[i] = 0;
  rrq_[i] = bq_;  // a region that starts while bent plays at the bent speed from its first sample
  if (!again) {
    rv_[i] = st_v_[i];
    rep_[i] = st_r_[i];
  }
  rg_[i] = rv_[i] >= 100u ? gain_[i] : gain_[i] * (static_cast<float>(rv_[i]) * 0.01f);
  ghost_off(i);  // the loop's own stop tail or jump fade is not carried under a region
  fn_[i] = 0;
  dir_[i] = 1;
  rst_[i] = false;
  act_[i] = true;
  return true;
}

// One chunk (up to kChunk samples) of the sounding region at dst[0..n): returns how many it wrote. The fade count is a whole number of
// samples, so the ramps are exact; the natural end starts its fade-out when as many samples are left as the fade is high, so it ends at zero.
std::uint32_t Loop::region_chunk(unsigned i, float* dst, std::uint32_t n, bool add) noexcept {
  if (region_bent(i)) {
    return region_chunk_bent(i, dst, n, add);
  }
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
  // The region's level folds into the chunk's target: at full it is gain_ itself, bit for bit.
  const float g1 = rv_[i] >= 100u ? gain_[i] : gain_[i] * (static_cast<float>(rv_[i]) * 0.01f);
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
    if (rst_[i] || rep_[i]) {
      (void)begin_region(i, !rst_[i]);  // a retrigger (the new region follows the fade-out at once) or a repeat (the span the trigger latched, again)
    }
    if (!act_[i]) {
      region_off(i);
    }
  }
  return nn;
}

// One chunk of the sounding region from the fixed-point phase (see mix_bent). The fades in, out and release are counts of samples as in
// region_chunk; the natural end is by position: the region is over at the first sample past its last one, and the samples within
// kFadeSamples' worth of distance at the nominal speed of it fade by that distance. Reads stay inside the region: the span is clamped to
// it and padded with its last valid sample. Returns how many samples it wrote (at least one).
std::uint32_t Loop::region_chunk_bent(unsigned i, float* dst, std::uint32_t n, bool add) noexcept {
  const bool rev = rev_[i];
  const unsigned sp = speed_[i];
  const std::uint32_t step = sp == 0u ? 1u : (sp == 1u ? 2u : 4u);
  const std::uint32_t len = rl_[i];
  const std::uint32_t last = 2u * (len - 1u);
  const std::uint64_t grid = step == 1u ? (1ull << 32) : (1ull << 33);  // 0.5x plays half samples; 1x and 2x whole ones (see below)
  const std::uint32_t tgt = bq_;
  const std::uint32_t cur = rrq_[i];
  std::uint32_t h = rh_[i];
  std::uint32_t hf = rhf_[i];
  if (h >= last) {
    h = last;
    hf = 0;
  }
  std::uint32_t fn = fn_[i];
  std::int32_t dir = dir_[i];
  std::uint32_t nn = n < kChunk ? n : kChunk;
  if (dir > 0) {
    nn = nn < kFadeSamples - fn ? nn : kFadeSamples - fn;
  } else if (dir < 0) {
    nn = nn < fn ? nn : (fn != 0u ? fn : 1u);
  }
  const std::uint32_t b = h >> 1;
  const std::uint64_t f0 = (static_cast<std::uint64_t>(h & 1u) << 32) | hf;
  std::uint64_t u = rev ? (1ull << 33) - f0 : f0;
  const std::uint64_t lim = rev ? static_cast<std::uint64_t>(b + 1u) << 33 : static_cast<std::uint64_t>(len - 1u - b) << 33;
  const std::uint64_t zone = static_cast<std::uint64_t>(step) << 38;  // kFadeSamples at the nominal speed, in Q33 samples
  std::uint32_t qs = step * cur;
  std::int32_t dqs = 0;
  std::uint32_t qmax = qs;
  bool settle = false;
  std::uint64_t uend = 0;
  if (tgt == kRatioOne && cur == kRatioOne) {
    if (nn >= kSettleMin && lim >= u && lim - u >= 2u * zone) {  // well before the end, hf != 0 here: land on the grid
      const std::uint64_t anom = static_cast<std::uint64_t>(nn * step) << 32;
      uend = (u + anom + (grid >> 1)) & ~(grid - 1u);
      qs = (static_cast<std::uint32_t>((uend - u) >> 14) / nn) << 6;
      qmax = qs;
      settle = true;
    }
  } else {
    const std::uint32_t qt = step * tgt;
    dqs = (static_cast<std::int32_t>(qt) - static_cast<std::int32_t>(qs)) / static_cast<std::int32_t>(nn);
    qmax = qt > qs ? qt : qs;
  }
  const std::uint64_t reach = u + ((static_cast<std::uint64_t>(qmax) * nn) << 8);
  std::uint32_t count = static_cast<std::uint32_t>(reach >> 33) + 3u;
  count = count < kBentSpan ? count : kBentSpan;
  const std::int16_t* s = mem_ + rs_[i];
  alignas(4) std::int16_t buf[kBentSpan];
  float tmp[kChunk];
  if (!rev) {
    const std::uint32_t valid = count < len - b ? count : len - b;  // b < len
    read_forward(buf, s + b, valid);
    for (std::uint32_t k = valid; k < count; ++k) {
      buf[k] = buf[valid - 1u];
    }
  } else {
    std::uint32_t off = 0;
    std::uint32_t c0 = b + 1u;
    if (c0 >= len) {  // the sample above the region's last one is not ours: the span starts on a copy of the last
      c0 = len - 1u;
      buf[0] = s[c0];
      off = 1;
    }
    const std::uint32_t valid = count - off < c0 + 1u ? count - off : c0 + 1u;
    read_backward(buf + off, s + c0, valid);
    for (std::uint32_t k = off + valid; k < count; ++k) {
      buf[k] = buf[off + valid - 1u];
    }
  }
  const float zinv = 1.0f / static_cast<float>(step << 22);  // a power of two: exact
  std::uint32_t m = nn;
  const bool near_end = reach + zone > lim;
  if (near_end) {
    m = run_bent_end(tmp, buf, u, qs, dqs, nn, lim, zone, zinv);
  } else {
    run_bent(tmp, buf, u, qs, dqs, nn);
  }
  bool over = m < nn || u > lim;
  if (settle) {
    u = uend;
  }
  const float g1 = rv_[i] >= 100u ? gain_[i] : gain_[i] * (static_cast<float>(rv_[i]) * 0.01f);
  float g = rg_[i];
  const float dg = (g1 - g) / static_cast<float>(m);
  if (dir == 0) {
    for (std::uint32_t k = 0; k < m; ++k) {
      g += dg;
      tmp[k] *= kToFloat * g;
    }
  } else {
    const std::int32_t d = dir;
    for (std::uint32_t k = 0; k < m; ++k) {
      g += dg;
      fn = static_cast<std::uint32_t>(static_cast<std::int32_t>(fn) + d);
      tmp[k] *= kToFloat * g * (static_cast<float>(fn) * (1.0f / static_cast<float>(kFadeSamples)));
    }
  }
  rg_[i] = g1;
  if (add) {
    for (std::uint32_t k = 0; k < m; ++k) {
      dst[k] += tmp[k];
    }
  } else {
    for (std::uint32_t k = 0; k < m; ++k) {
      dst[k] = tmp[k];
    }
  }
  if (dir > 0 && fn >= kFadeSamples) {
    dir = 0;
  }
  if (dir < 0 && fn == 0u) {
    over = true;
  }
  if (!over) {
    const std::uint32_t di = static_cast<std::uint32_t>(u >> 33);
    const std::uint64_t df = u & ((1ull << 33) - 1u);
    if (!rev) {
      rh_[i] = (b + di) * 2u + static_cast<std::uint32_t>(df >> 32);
      rhf_[i] = static_cast<std::uint32_t>(df);
    } else if (df == 0u) {
      rh_[i] = (b + 1u - di) * 2u;
      rhf_[i] = 0;
    } else {
      const std::uint64_t pf = (1ull << 33) - df;
      rh_[i] = (b - di) * 2u + static_cast<std::uint32_t>(pf >> 32);
      rhf_[i] = static_cast<std::uint32_t>(pf);
    }
    rrq_[i] = settle ? cur : tgt;
    if (!region_bent(i) && lim - u < zone) {  // the integer path takes the end from here: it picks the position fade up as a count
      const float e = static_cast<float>(static_cast<std::uint32_t>((lim - u) >> 16)) * zinv;
      fn = static_cast<std::uint32_t>(e * static_cast<float>(fn != 0u ? fn : kFadeSamples));
      dir = -1;
      if (fn == 0u) {  // the position fade has run out: the region is over (a count of 0 going down would wrap in the integer path)
        over = true;
      }
    }
  }
  fn_[i] = static_cast<std::uint8_t>(fn);
  dir_[i] = static_cast<std::int8_t>(dir);
  if (over) {
    act_[i] = false;
    if (rst_[i] || rep_[i]) {
      (void)begin_region(i, !rst_[i]);
    }
    if (!act_[i]) {
      region_off(i);
    }
  }
  return m;
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
        rep_[i] = false;  // the fade-out is the end: a repeating region lets go
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
