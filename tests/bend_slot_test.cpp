// SPDX-License-Identifier: GPL-3.0-or-later
// SB1, bend a slot: the Loop's continuous speed (Loop::set_bend, driven by the Vocoder's Bend). Synthetic signals only.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/loop.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>
#include <o3sdk/dsp/transport.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace o3_voc03er;

namespace {

constexpr float kFs = 44100.0f;
constexpr float kPi2 = 6.2831853f;

struct Fnv {
  std::uint32_t h = 2166136261u;
  void add(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    for (int k = 0; k < 4; ++k) {
      h = (h ^ ((u >> (8 * k)) & 0xFFu)) * 16777619u;
    }
  }
};

// A loop of `n` samples of noise-like integers (a fixed LCG), sealed, so every sample is distinct.
void make_noise_loop(Loop& l, std::vector<std::int16_t>& mem, std::uint32_t n) {
  mem.assign(Loop::kCount * static_cast<std::size_t>(n) + 8u, 0);
  l.bind(mem.data(), n);
  std::vector<float> in(n);
  std::uint32_t s = 12345u;
  for (std::uint32_t k = 0; k < n; ++k) {
    s = s * 1664525u + 1013904223u;
    in[k] = 0.4f * (static_cast<float>(static_cast<std::int32_t>(s >> 8) & 0xFFFF) / 32768.0f - 1.0f) +
            0.3f * std::sin(kPi2 * static_cast<float>(k) / 777.0f);
  }
  l.begin_record(0);
  REQUIRE(l.record(0, in.data(), n) == n);
  if (l.recording(0)) {
    l.end_record(0);
  }
}

// What the integer path (no bend) does, folded into one digest: every speed and way over odd chunk sizes, a level change, bar lines (snaps,
// jumps with a ghost), a stop tail, regions in both directions with a release and a retrigger. `bend_one`: set_bend(1.0f) is called too.
std::uint32_t bend0_digest(bool bend_one) {
  Loop l;
  std::vector<std::int16_t> mem;
  make_noise_loop(l, mem, 9000);
  if (bend_one) {
    l.set_bend(1.0f);
  }
  Fnv d;
  std::vector<float> out(64);
  const std::uint32_t chunks[] = {64, 37, 17, 64, 1, 50};
  std::uint32_t ci = 0;
  auto run = [&](unsigned i, std::uint32_t total, bool region) {
    for (std::uint32_t k = 0; k < total;) {
      const std::uint32_t m = chunks[ci++ % 6];
      if (region) {
        l.play(i, out.data(), m, (k & 1u) != 0u);
      } else {
        l.mix(i, out.data(), m, (k & 1u) != 0u);
      }
      for (std::uint32_t j = 0; j < m; ++j) {
        d.add(out[j]);
      }
      k += m;
    }
  };
  for (unsigned sp = 0; sp < 3; ++sp) {
    for (int rv = 0; rv < 2; ++rv) {
      l.set_speed(0, sp);
      l.set_reverse(0, rv != 0);
      REQUIRE(l.start(0));
      run(0, 30000, false);
      l.set_gain(0, 0.5f);
      run(0, 300, false);
      l.set_gain(0, 1.0f);
      l.stop(0);
      run(0, 200, false);
    }
  }
  // bars
  l.set_speed(0, 1);
  l.set_reverse(0, false);
  l.set_bars(0, 2);
  REQUIRE(l.start(0, 0));
  std::uint32_t bar = 1;
  for (int line = 0; line < 12; ++line) {
    l.bar_line(0, 5, bar++);
    run(0, 3000 + 111u * static_cast<std::uint32_t>(line), false);
  }
  l.stop(0);
  run(0, 100, false);
  l.set_bars(0, 0);
  // regions
  for (unsigned sp = 0; sp < 3; ++sp) {
    for (int rv = 0; rv < 2; ++rv) {
      l.set_speed(0, sp);
      l.set_reverse(0, rv != 0);
      l.set_region_start(0, 1000);
      l.set_region_len(0, 5000);
      REQUIRE(l.trig_region(0, 3, 100));
      run(0, 400, true);
      l.set_region_start(0, 2500);
      l.set_region_len(0, 90);
      REQUIRE(l.trig_region(0, 0, 80));
      run(0, 300, true);
      l.release_region(0, 10);
      run(0, 300, true);
      l.set_region_start(0, 500);
      l.set_region_len(0, 7000);
      REQUIRE(l.trig_region(0, 0, 100));
      run(0, 30000, true);
    }
  }
  return d.h;
}

constexpr double kStepOf[3] = {0.5, 1.0, 2.0};  // samples a sample at the ratio 1: speed 0, 1, 2

// The ratio as set_bend stores it (Q24), in double.
double quant(float ratio) { return static_cast<double>(static_cast<std::uint32_t>(ratio * 16777216.0f + 0.5f)) / 16777216.0; }

// The double-precision oracle: the linear interpolation of the stored samples at `pos`, wrapping mod len.
double ref_at(const std::int16_t* s, std::uint32_t len, double pos) {
  double p = std::fmod(pos, static_cast<double>(len));
  if (p < 0.0) {
    p += static_cast<double>(len);
  }
  const auto i = static_cast<std::uint32_t>(p);
  const double fr = p - static_cast<double>(i);
  const double a = s[i];
  const double b = s[(i + 1u) % len];
  return (a + fr * (b - a)) / 32768.0;
}

// A sine of `period` samples (amplitude 0.5) in a loop of `n` samples.
void make_sine_loop(Loop& l, std::vector<std::int16_t>& mem, std::uint32_t n, float period) {
  mem.assign(Loop::kCount * static_cast<std::size_t>(n) + 8u, 0);
  l.bind(mem.data(), n);
  std::vector<float> in(n);
  for (std::uint32_t k = 0; k < n; ++k) {
    in[k] = 0.5f * std::sin(kPi2 * static_cast<float>(k) / period);
  }
  l.begin_record(0);
  REQUIRE(l.record(0, in.data(), n) == n);
  if (l.recording(0)) {
    l.end_record(0);
  }
}

// `n` samples of slot `i` through play() (the region if any, else mix()), in blocks that cycle through odd sizes.
std::vector<float> run_play(Loop& l, unsigned i, std::uint32_t n, std::uint32_t* ci = nullptr) {
  static const std::uint32_t chunks[] = {64, 37, 17, 64, 1, 50, 23, 64};
  std::uint32_t local = 0;
  std::uint32_t& c = ci != nullptr ? *ci : local;
  std::vector<float> out(n);
  for (std::uint32_t k = 0; k < n;) {
    std::uint32_t m = chunks[c++ % 8u];
    m = m < n - k ? m : n - k;
    l.play(i, out.data() + k, m, false);
    k += m;
  }
  return out;
}

// The frequency of a sine in cycles a sample, by its upward zero crossings (linearly interpolated) over out[from, to).
double zero_cross_freq(const std::vector<float>& y, std::size_t from, std::size_t to) {
  double first = 0.0;
  double last = 0.0;
  int count = 0;
  for (std::size_t k = from; k + 1 < to; ++k) {
    if (y[k] <= 0.0f && y[k + 1] > 0.0f) {
      const double t = static_cast<double>(k) + static_cast<double>(-y[k]) / static_cast<double>(y[k + 1] - y[k]);
      if (count == 0) {
        first = t;
      }
      last = t;
      ++count;
    }
  }
  REQUIRE(count > 10);
  return static_cast<double>(count - 1) / (last - first);
}

bool all_finite_within(const std::vector<float>& v, float bound) {
  for (float x : v) {
    if (!std::isfinite(x) || std::fabs(x) > bound) {
      return false;
    }
  }
  return true;
}

float max_step(const std::vector<float>& v, std::size_t from, std::size_t to) {
  float m = 0.0f;
  for (std::size_t k = from; k + 1 < to; ++k) {
    m = std::fmax(m, std::fabs(v[k + 1] - v[k]));
  }
  return m;
}

}  // namespace

TEST_CASE("bend slot: bend 0 is bit for bit the integer path of the build before the bend") {
  // The digest of the playback of the build before SB1 (every speed and way, odd chunks, levels, bar lines, regions): pinned.
  constexpr std::uint32_t kHeadDigest = 0x5451E258u;
  CHECK(bend0_digest(false) == kHeadDigest);
  CHECK(bend0_digest(true) == kHeadDigest);
}

TEST_CASE("bend slot: set_bend away from 1 and back before any sample is played changes nothing") {
  Loop l;
  std::vector<std::int16_t> mem;
  make_noise_loop(l, mem, 5000);
  l.set_bend(1.3f);
  l.set_bend(std::nanf(""));  // NaN is 1
  REQUIRE(l.start(0));
  std::vector<float> a = run_play(l, 0, 3000);
  for (std::uint32_t k = 0; k < 3000; ++k) {
    CHECK(a[k] == static_cast<float>(mem[k]) * (1.0f / 32768.0f));
  }
}

TEST_CASE("bend slot: a bent reader matches a double-precision oracle (speeds, ways, ratios, a short loop's seam, odd blocks)") {
  const float ratios[] = {0.5f, 0.7071f, 0.8409f, 1.0595f, 1.3f, 2.0f};
  double worst = 0.0;
  for (unsigned sp = 0; sp < 3; ++sp) {
    for (int rv = 0; rv < 2; ++rv) {
      for (float r : ratios) {
        Loop l;
        std::vector<std::int16_t> mem;
        make_noise_loop(l, mem, 300);
        l.set_bend(r);
        l.set_speed(0, sp);
        l.set_reverse(0, rv != 0);
        REQUIRE(l.start(0));
        const std::uint32_t n = 4000;
        const std::vector<float> y = run_play(l, 0, n);
        const double step = kStepOf[sp] * quant(r);
        for (std::uint32_t k = 0; k < n; ++k) {
          const double pos = rv != 0 ? 299.0 - static_cast<double>(k) * step : static_cast<double>(k) * step;
          const double e = std::fabs(static_cast<double>(y[k]) - ref_at(mem.data(), 300, pos));
          worst = std::fmax(worst, e);
          REQUIRE(e < 1e-5);
        }
      }
    }
  }
  std::fprintf(stderr, "SB1 oracle worst abs error %.3g\n", worst);
}

TEST_CASE("bend slot: a sine bent +12, -12 and +2 st plays at the right frequency") {
  const float period = 100.0f;
  const float sts[] = {12.0f, -12.0f, 2.0f, -5.0f, 7.0f};
  for (float st : sts) {
    Loop l;
    std::vector<std::int16_t> mem;
    make_sine_loop(l, mem, 20000, period);
    const float ratio = std::exp2(st / 12.0f);
    l.set_bend(ratio);
    REQUIRE(l.start(0));
    const std::vector<float> y = run_play(l, 0, 4000);
    const double want = quant(ratio) / static_cast<double>(period);
    const double got = zero_cross_freq(y, 300, 3700);
    const double rel = std::fabs(got / want - 1.0);
    std::fprintf(stderr, "SB1 sine %+.0f st: %.3f Hz want %.3f Hz, error %.2e\n", static_cast<double>(st), got * 44100.0, want * 44100.0, rel);
    CHECK(rel < 1e-4);
  }
}

TEST_CASE("bend slot: a bend started in play glides to the new rate, and the interpolation is clean (SNR on a sine)") {
  const float period = 100.0f;
  Loop l;
  std::vector<std::int16_t> mem;
  make_sine_loop(l, mem, 20000, period);
  REQUIRE(l.start(0));
  std::vector<float> y = run_play(l, 0, 640);  // at 1x
  const float ratio = std::exp2(2.0f / 12.0f);
  l.set_bend(ratio);
  const std::vector<float> z = run_play(l, 0, 6000);
  y.insert(y.end(), z.begin(), z.end());
  // the rate after the glide
  const double got = zero_cross_freq(y, 1200, 6000);
  CHECK(std::fabs(got / (quant(ratio) / static_cast<double>(period)) - 1.0) < 1e-4);
  // SNR against the ideal sine at the bent phase: phase of sample k (k >= 704) is p0 + (k - 704) * r
  const double r = quant(ratio);
  const double p0 = 704.0 + (r - 1.0) * 64.0 * 65.0 / 128.0;  // after the 64-sample glide chunk: the speed is 1 + (r - 1) * (k + 1) / 64 at its sample k
  double es = 0.0;
  double en = 0.0;
  for (std::size_t k = 704; k < 6000; ++k) {
    const double pos = p0 + static_cast<double>(k - 704u) * r;
    const double ideal = 0.5 * std::sin(6.283185307179586 * pos / static_cast<double>(period));
    const double d = static_cast<double>(y[k]) - ideal;
    es += ideal * ideal;
    en += d * d;
  }
  const double snr = 10.0 * std::log10(es / en);
  std::fprintf(stderr, "SB1 interpolation SNR at 441 Hz, +2 st: %.1f dB\n", snr);
  CHECK(snr > 50.0);
  // the glide is a ramp of one chunk: the output is continuous
  CHECK(max_step(y, 0, y.size()) < 0.5f * 6.2832f / period * 1.2f * 1.2f);
}

TEST_CASE("bend slot: the interpolation SNR of a higher tone, for the record") {
  Loop l;
  std::vector<std::int16_t> mem;
  make_sine_loop(l, mem, 20000, 22.05f);  // 2 kHz
  const float ratio = std::exp2(1.0f / 12.0f);
  l.set_bend(ratio);
  REQUIRE(l.start(0));
  const std::vector<float> y = run_play(l, 0, 4000);
  const double r = quant(ratio);
  double es = 0.0;
  double en = 0.0;
  for (std::size_t k = 200; k < 4000; ++k) {
    const double ideal = 0.5 * std::sin(6.283185307179586 * static_cast<double>(k) * r / 22.05);
    const double d = static_cast<double>(y[k]) - ideal;
    es += ideal * ideal;
    en += d * d;
  }
  const double snr = 10.0 * std::log10(es / en);
  std::fprintf(stderr, "SB1 interpolation SNR at 2 kHz, +1 st: %.1f dB\n", snr);
  CHECK(snr > 25.0);
}

TEST_CASE("bend slot: back at 0 the reader settles on the grid and the integer path plays on, at the nominal rate") {
  // Many histories (the ratio, how long it was held, the block sizes) for every speed and way. The integer paths play on whole samples at 1x
  // and 2x (a half sample left over would be a two-sample average for good: a permanent lowpass), so after the return the output must be the
  // stored samples exactly; at 0.5x the position alternates anyway, so either half bit is allowed there.
  const float ratios[] = {1.2f, 0.7f, 1.5f, 0.55f, 1.93f, 1.0594631f, 0.9439f, 1.31f};
  for (int rv = 0; rv < 2; ++rv) {
    for (unsigned sp = 0; sp < 3; ++sp) {
      for (int hist = 0; hist < 40; ++hist) {
        Loop l;
        std::vector<std::int16_t> mem;
        make_noise_loop(l, mem, 4000);
        l.set_speed(0, sp);
        l.set_reverse(0, rv != 0);
        REQUIRE(l.start(0));
        l.set_bend(ratios[hist % 8]);
        std::uint32_t ci = static_cast<std::uint32_t>(hist);
        (void)run_play(l, 0, 300u + 37u * static_cast<std::uint32_t>(hist), &ci);
        l.set_bend(1.0f);
        (void)run_play(l, 0, 192, &ci);  // a glide chunk, then a settle chunk of 32 or more (the odd block sizes may need a couple more)
        (void)run_play(l, 0, 192, &ci);
        const std::uint32_t pos0 = l.position(0);
        REQUIRE(pos0 < 4000);
        const std::vector<float> y = run_play(l, 0, 900, &ci);
        double best = 1e9;
        for (int hb = 0; hb < (sp == 0u ? 2 : 1); ++hb) {
          double worst = 0.0;
          for (std::uint32_t k = 0; k < 900; ++k) {
            const double d = static_cast<double>(k) * kStepOf[sp];
            const double pos = static_cast<double>(pos0) + 0.5 * hb + (rv != 0 ? -d : d);
            worst = std::fmax(worst, std::fabs(static_cast<double>(y[k]) - ref_at(mem.data(), 4000, pos)));
          }
          best = std::fmin(best, worst);
        }
        CHECK(best < 1e-6);
        if (sp == 1u && rv == 0) {  // the fast path: whole samples, exactly the stored ones
          for (std::uint32_t k = 0; k < 900; ++k) {
            CHECK(y[k] == static_cast<float>(mem[(pos0 + k) % 4000u]) * (1.0f / 32768.0f));
          }
        }
      }
    }
  }
}

TEST_CASE("bend slot: a bent whole-bar loop drifts, and back at 0 the first bar line puts it right with no step above the crossfade") {
  constexpr std::uint32_t kBar = 4416;  // 69 blocks of 64
  constexpr std::uint32_t kLen = 2u * kBar;
  Loop l;
  std::vector<std::int16_t> mem;
  make_sine_loop(l, mem, kLen, static_cast<float>(kLen) / 32.0f);  // 276 samples a cycle, whole cycles in the loop
  l.set_bars(0, 2);
  REQUIRE(l.start(0, 0));
  const float ratio = std::exp2(3.0f / 12.0f);
  std::vector<float> y;
  std::vector<float> blk(64);
  std::uint32_t bar = 1;
  std::uint32_t at = 0;
  std::uint32_t settled_bar = 0;
  std::uint32_t drift = 0;
  for (std::uint32_t b = 0; b < 20u * kBar / 64u; ++b) {
    if (at % kBar == 0 && at != 0) {
      l.bar_line(0, 0, bar++);
    }
    if (at == 3u * kBar + 1024u) {
      l.set_bend(ratio);
    }
    if (at == 9u * kBar + 1920u) {
      l.set_bend(1.0f);
      settled_bar = bar;  // the next bar line is the first with the bend back at 0
    }
    l.mix(0, blk.data(), 64, false);
    y.insert(y.end(), blk.begin(), blk.end());
    at += 64;
    if (settled_bar != 0u && bar > settled_bar && at % kBar == 64) {
      // the first block after a bar line once the bend was back: the loop is where the transport says (j bars in, plus the block)
      const std::uint32_t j = bar - 1u;
      CHECK(l.position(0) == (j * kBar + 64u) % kLen);
    }
    if (at % kBar == 64 && at > kBar) {
      const std::uint32_t want = ((bar - 1u) * kBar + 64u) % kLen;
      const std::uint32_t got = l.position(0);
      const std::uint32_t off = (got + kLen - want) % kLen;
      if (bar <= 3u) {
        CHECK(off == 0u);  // before the bend: bar lines change nothing
      } else if (settled_bar == 0u) {
        drift = std::max(drift, std::min(off, kLen - off));  // bent: the loop is not where the bars say
      }
    }
  }
  CHECK(drift > 500u);  // it did drift while bent (about 0.19 of a bar a bar)
  const float slope = 0.5f * 6.2832f / 276.0f * ratio;
  const float limit = slope + 1.0f / 64.0f + 0.01f;  // a signal's own slope, plus two sines fading across 64 samples
  const float worst = max_step(y, 0, y.size());
  std::fprintf(stderr, "SB1 re-sync: worst step %.4f, limit %.4f\n", static_cast<double>(worst), static_cast<double>(limit));
  CHECK(worst < limit);
  CHECK(all_finite_within(y, 1.0f));
}

TEST_CASE("bend slot: long runs at odd rates read only valid samples (loops, short and long, both ways, an exact-size bank)") {
  const float ratios[] = {0.5f, 0.50000012f, 0.6180339f, 0.7071f, 0.9999f, 1.0001f, 1.4142f, 1.9999f, 2.0f};
  const std::uint32_t lens[] = {100, 130, 9000};
  for (std::uint32_t n : lens) {
    for (unsigned sp = 0; sp < 3; ++sp) {
      for (int rv = 0; rv < 2; ++rv) {
        for (float r : ratios) {
          Loop l;
          std::vector<std::int16_t> mem(static_cast<std::size_t>(Loop::kCount) * n);  // exact: ASAN sees any read past it
          l.bind(mem.data(), n);
          std::vector<float> in(n);
          for (std::uint32_t k = 0; k < n; ++k) {
            in[k] = 0.5f * std::sin(0.37f * static_cast<float>(k));
          }
          // the last slot: its memory ends the bank
          l.begin_record(3);
          REQUIRE(l.record(3, in.data(), n) == n);
          if (l.recording(3)) {
            l.end_record(3);
          }
          l.set_speed(3, sp);
          l.set_reverse(3, rv != 0);
          l.set_bend(r);
          REQUIRE(l.start(3));
          const std::vector<float> y = run_play(l, 3, 30000);
          REQUIRE(all_finite_within(y, 0.51f));
          CHECK(l.position(3) < n);
        }
      }
    }
  }
}

TEST_CASE("bend slot: a bent region never leaves its bounds, ends by position and fades out") {
  const float ratios[] = {0.5f, 0.6180339f, 0.9999f, 1.0001f, 1.4142f, 2.0f};
  const std::uint32_t lens[] = {1, 2, 3, 70, 130, 1000};
  constexpr std::uint32_t kPer = 3000;
  for (std::uint32_t len : lens) {
    for (unsigned sp = 0; sp < 3; ++sp) {
      for (int rv = 0; rv < 2; ++rv) {
        for (float r : ratios) {
          Loop l;
          std::vector<std::int16_t> mem(static_cast<std::size_t>(Loop::kCount) * kPer, 32767);  // loud outside
          l.bind(mem.data(), kPer);
          std::vector<float> in(kPer, 0.1f);
          l.begin_record(0);
          REQUIRE(l.record(0, in.data(), kPer) == kPer);
          if (l.recording(0)) {
            l.end_record(0);
          }
          const std::uint32_t start = kPer + 500u;  // inside slot 1's memory: a region reads the whole bank
          for (std::uint32_t k = 0; k < kPer; ++k) {
            mem[k] = 32767;
          }
          for (std::uint32_t k = 0; k < len; ++k) {
            mem[start + k] = 1000;  // quiet, a DC
          }
          // slot 0 is empty of meaning now but still has its length: the region reads the bank at `start`
          l.set_speed(0, sp);
          l.set_reverse(0, rv != 0);
          l.set_bend(r);
          l.set_region_start(0, start);
          l.set_region_len(0, len);
          REQUIRE(l.trig_region(0, 0, 100));
          std::uint32_t total = 0;
          std::uint32_t ci = 0;
          float peak = 0.0f;
          float last_nonzero = 0.0f;
          std::uint32_t gone = 0;
          while (l.region(0) && total < 20000u) {
            const std::vector<float> y = run_play(l, 0, 64, &ci);
            for (float v : y) {
              peak = std::fmax(peak, std::fabs(v));
              if (v != 0.0f) {
                last_nonzero = std::fabs(v);
              }
            }
            total += 64;
            if (!l.region(0) && gone == 0u) {
              gone = total;
            }
          }
          REQUIRE_FALSE(l.region(0));
          CHECK(peak <= 1000.0f / 32768.0f + 1e-6f);  // never a loud sample from outside the region
          if (len >= 1000u) {
            // it ends near where the speed says, and the last sample it plays is nearly faded out
            const double expect = static_cast<double>(len) / (kStepOf[sp] * quant(r));
            CHECK(std::fabs(static_cast<double>(total) - expect) < 64.0 + 64.0 + 70.0);
            CHECK(last_nonzero < 0.3f * (1000.0f / 32768.0f));
          }
        }
      }
    }
  }
}

TEST_CASE("bend slot: a bend that returns to 0 inside a region, and near its end, has no step above the fade") {
  for (std::uint32_t bend_off_at : {3000u, 5200u, 5600u}) {  // output sample where the bend goes back to 0 (the region is 6000 at 1.2x: ~5000)
    Loop l;
    constexpr std::uint32_t kPer = 8000;
    std::vector<std::int16_t> mem(static_cast<std::size_t>(Loop::kCount) * kPer, 1000);
    l.bind(mem.data(), kPer);
    std::vector<float> in(kPer, 0.03f);
    l.begin_record(0);
    REQUIRE(l.record(0, in.data(), kPer) == kPer);
    if (l.recording(0)) {
      l.end_record(0);
    }
    for (std::uint32_t k = 0; k < kPer; ++k) {
      mem[k] = 1000;
    }
    l.set_region_start(0, 0);
    l.set_region_len(0, 6000);
    l.set_bend(1.2f);
    REQUIRE(l.trig_region(0, 0, 100));
    std::vector<float> y;
    std::uint32_t at = 0;
    while (l.region(0) && at < 12000u) {
      if (at >= bend_off_at && at < bend_off_at + 64u) {
        l.set_bend(1.0f);
      }
      std::vector<float> b(64);
      l.play(0, b.data(), 64, false);
      y.insert(y.end(), b.begin(), b.end());
      at += 64;
    }
    REQUIRE_FALSE(l.region(0));
    CHECK(all_finite_within(y, 1000.0f / 32768.0f + 1e-6f));
    CHECK(max_step(y, 0, y.size()) < (1000.0f / 32768.0f) / 40.0f);  // a 64-sample ramp is 1/64 of the level a sample; a step of 1/40 would be a click
  }
}

TEST_CASE("bend slot: a region whose position fade runs out on the hand-off to the integer path ends there, with no wrapped count") {
  // Bent at 2.0 the position lands on whole samples, so the last glide sample (one sample long) lands exactly on the region's last sample:
  // the position fade there is 0, the hand-off count truncates to 0 going down, and the integer path used to wrap it (a gain of ~6.7e7).
  Loop l;
  constexpr std::uint32_t kPer = 8000;
  std::vector<std::int16_t> mem(static_cast<std::size_t>(Loop::kCount) * kPer, 1000);
  l.bind(mem.data(), kPer);
  std::vector<float> in(kPer, 0.03f);
  l.begin_record(0);
  REQUIRE(l.record(0, in.data(), kPer) == kPer);
  if (l.recording(0)) {
    l.end_record(0);
  }
  for (std::uint32_t k = 0; k < kPer; ++k) {
    mem[k] = 1000;
  }
  l.set_region_start(0, 0);
  l.set_region_len(0, 2002);
  l.set_bend(2.0f);
  REQUIRE(l.trig_region(0, 0, 100));
  std::vector<float> y;
  std::vector<float> b(40);
  for (int k = 0; k < 25; ++k) {  // 1000 samples at 2.0: the position is 2000 of 2001
    l.play(0, b.data(), 40, false);
    y.insert(y.end(), b.begin(), b.end());
  }
  REQUIRE(l.region(0));
  l.set_bend(1.0f);
  l.play(0, b.data(), 1, false);  // the glide sample: one whole sample on, at the region's last
  y.push_back(b[0]);
  for (int k = 0; k < 4; ++k) {
    l.play(0, b.data(), 40, false);
    y.insert(y.end(), b.begin(), b.begin() + 40);
  }
  CHECK(all_finite_within(y, 1000.0f / 32768.0f + 1e-6f));
  CHECK_FALSE(l.region(0));
}

TEST_CASE("bend slot: stop() while bent starts its tail where the bend was, with no sample-sized step") {
  for (int rv = 0; rv < 2; ++rv) {
    for (unsigned sp = 0; sp < 3; ++sp) {
      for (int hist = 0; hist < 12; ++hist) {
        Loop a;
        Loop b;
        std::vector<std::int16_t> ma;
        std::vector<std::int16_t> mb;
        make_noise_loop(a, ma, 4000);
        make_noise_loop(b, mb, 4000);
        for (Loop* l : {&a, &b}) {
          l->set_speed(0, sp);
          l->set_reverse(0, rv != 0);
          REQUIRE(l->start(0));
          l->set_bend(0.6f + 0.13f * static_cast<float>(hist));
        }
        const std::uint32_t n = 640u + 29u * static_cast<std::uint32_t>(hist);
        std::uint32_t ca = 0;
        std::uint32_t cb = 0;
        (void)run_play(a, 0, n, &ca);
        (void)run_play(b, 0, n, &cb);
        const std::vector<float> want = run_play(a, 0, 8, &ca);  // what the bent loop plays next
        b.stop(0);
        REQUIRE(b.sounding(0));
        std::vector<float> tail(8);
        b.mix(0, tail.data(), 8, false);
        // the first tail sample is the next bent one (weight 1); the rest fades out under its own slope, so only the first is compared
        CHECK(std::fabs(tail[0] - want[0]) < 1e-6f);
      }
    }
  }
}

TEST_CASE("bend slot: on_bar goes false while a bend has the loop off its bar cycle, and the first bar line after the bend puts it back") {
  constexpr std::uint32_t kBar = 4416;
  constexpr std::uint32_t kLen = 2u * kBar;
  Loop l;
  std::vector<std::int16_t> mem;
  make_sine_loop(l, mem, kLen, static_cast<float>(kLen) / 32.0f);
  l.set_bars(0, 2);
  REQUIRE(l.start(0, 0));
  CHECK(l.on_bar(0));
  std::vector<float> blk(64);
  std::uint32_t bar = 1;
  std::uint32_t at = 0;
  bool seen_off = false;
  bool returned = false;
  for (std::uint32_t k = 0; k < 14u * kBar / 64u; ++k) {
    if (at % kBar == 0 && at != 0) {
      l.bar_line(0, 0, bar++);
      if (at > 4u * kBar + 1920u) {
        returned = true;
        CHECK(l.on_bar(0));  // the line is the re-sync
      }
    }
    if (at == kBar + 1024u) {
      l.set_bend(1.2f);
    }
    if (at == 4u * kBar + 1920u) {
      l.set_bend(1.0f);
    }
    l.mix(0, blk.data(), 64, false);
    at += 64;
    if (at <= kBar + 1024u) {
      CHECK(l.on_bar(0));
    } else if (at > kBar + 1088u && !returned) {
      CHECK_FALSE(l.on_bar(0));  // bent, and after the bend is back at 0 too: the loop is still where the bend left it
      seen_off = true;
    }
  }
  CHECK(seen_off);
  CHECK(returned);
  CHECK(l.on_bar(0));
}

TEST_CASE("bend slot: sweeping the bend in every state stays finite, bounded and inside the loop (NaN, inf, out of range, odd blocks)") {
  std::uint32_t rng = 777u;
  const auto next = [&] {
    rng = rng * 1664525u + 1013904223u;
    return rng >> 8;
  };
  Loop l;
  std::vector<std::int16_t> mem;
  make_noise_loop(l, mem, 2500);
  const float odd[] = {std::nanf(""), INFINITY, -INFINITY, -3.0f, 0.0f, 7.0f, 0.5f, 2.0f, 1.0f};
  std::vector<float> blk(64);
  for (int round = 0; round < 3000; ++round) {
    const std::uint32_t r = next();
    if (r % 7u == 0u) {
      l.set_bend(odd[(r >> 3) % 9u]);
    } else if (r % 3u == 0u) {
      l.set_bend(0.5f + static_cast<float>((r >> 4) & 0xFFFFu) * (1.5f / 65535.0f));
    }
    if (r % 97u == 0u) {
      l.set_speed(0, (r >> 5) % 3u);
      l.set_reverse(0, ((r >> 7) & 1u) != 0u);
    }
    if (r % 211u == 0u) {
      l.set_region_start(0, (r >> 4) % 2000u);
      l.set_region_len(0, 1u + (r >> 9) % 3000u);
      (void)l.trig_region(0, (r >> 12) % 64u, 100);
    }
    if (r % 301u == 0u) {
      l.release_region(0, 0);
    }
    if (!l.playing(0)) {
      (void)l.start(0);
    }
    const std::uint32_t m = 1u + (r >> 10) % 64u;
    const bool add = (r & 1u) != 0u;
    for (std::uint32_t k = 0; k < m; ++k) {
      blk[k] = 0.0f;  // add: on top of silence
    }
    l.play(0, blk.data(), m, add);
    for (std::uint32_t k = 0; k < m; ++k) {
      REQUIRE(std::isfinite(blk[k]));
      REQUIRE(std::fabs(blk[k]) < 2.0f);
    }
    REQUIRE(l.position(0) < 2500u);
  }
}

namespace {

// A Vocoder with a slot recorded (a loop of `n` samples) and playing, free (no transport); returns the position after each block.
struct SlotRig {
  Vocoder v;
  std::vector<std::int16_t> mem;
  o3sdk::dsp::Transport tr;
  explicit SlotRig(std::uint32_t per_loop) : mem(static_cast<std::size_t>(Loop::kCount) * per_loop) {
    v.bind_loops(mem.data(), per_loop);
    v.prepare(44100.0f);
  }
  void block(const float* in, std::vector<o3sdk::dsp::Event> ev = {}) {
    std::vector<float> out(64), r(64), zero(64, 0.0f);
    o3sdk::dsp::ProcessArgs a{};
    a.events = ev.data();
    a.event_count = static_cast<std::uint32_t>(ev.size());
    a.in_l = in != nullptr ? in : zero.data();
    a.in_r = a.in_l;
    a.out_l = out.data();
    a.out_r = r.data();
    a.frames = 64;
    tr.frames = 64;
    a.transport = &tr;
    v.process(a);
  }
};

std::uint32_t advance_of(SlotRig& rig, std::uint32_t len) {
  const std::uint32_t p0 = rig.v.loops().position(0);
  rig.block(nullptr);
  return (rig.v.loops().position(0) + len - p0) % len;
}

}  // namespace

TEST_CASE("bend slot: the Vocoder's Bend moves a playing slot's speed, and 0 gives it back") {
  using LF = Vocoder::LoopField;
  SlotRig rig(4000);
  std::vector<float> x(1280, 0.3f);
  rig.block(x.data(), {o3sdk::dsp::param_change(Vocoder::loop_param(0, LF::Rec), 1.0f)});
  for (std::size_t p = 64; p < x.size(); p += 64) {
    rig.block(x.data() + p);
  }
  rig.block(nullptr, {o3sdk::dsp::param_change(Vocoder::loop_param(0, LF::Rec), 0.0f)});
  const std::uint32_t len = rig.v.loops().length(0);
  REQUIRE(len == 1280);
  rig.block(nullptr, {o3sdk::dsp::param_change(Vocoder::loop_param(0, LF::Play), 1.0f)});
  rig.block(nullptr);
  REQUIRE(rig.v.loops().playing(0));
  CHECK(advance_of(rig, len) == 64);
  rig.block(nullptr, {o3sdk::dsp::param_change(Vocoder::Bend, 12.0f)});
  for (int k = 0; k < 40; ++k) {  // the 20 ms glide is 882 samples
    rig.block(nullptr);
  }
  CHECK(advance_of(rig, len) == 128);
  rig.block(nullptr, {o3sdk::dsp::param_change(Vocoder::Bend, -12.0f)});
  for (int k = 0; k < 40; ++k) {
    rig.block(nullptr);
  }
  CHECK(advance_of(rig, len) == 32);
  rig.block(nullptr, {o3sdk::dsp::param_change(Vocoder::Bend, 0.0f)});
  for (int k = 0; k < 40; ++k) {
    rig.block(nullptr);
  }
  CHECK(advance_of(rig, len) == 64);
  CHECK(advance_of(rig, len) == 64);
}

TEST_CASE("bend slot: a wheel a hair off centre (below 0.02 st) is no bend for the slots: bit-exact path, no drift") {
  using LF = Vocoder::LoopField;
  for (float st : {0.0003f, -0.0003f, 0.01f, -0.0199f}) {
    SlotRig rig(4000);
    std::vector<float> x(1280, 0.3f);
    rig.block(x.data(), {o3sdk::dsp::param_change(Vocoder::loop_param(0, LF::Rec), 1.0f)});
    for (std::size_t p = 64; p < x.size(); p += 64) {
      rig.block(x.data() + p);
    }
    rig.block(nullptr, {o3sdk::dsp::param_change(Vocoder::loop_param(0, LF::Rec), 0.0f)});
    const std::uint32_t len = rig.v.loops().length(0);
    REQUIRE(len == 1280);
    rig.block(nullptr, {o3sdk::dsp::param_change(Vocoder::loop_param(0, LF::Play), 1.0f)});
    rig.block(nullptr);
    REQUIRE(rig.v.loops().playing(0));
    rig.block(nullptr, {o3sdk::dsp::param_change(Vocoder::Bend, st)});
    for (int k = 0; k < 40; ++k) {  // past the 20 ms glide
      rig.block(nullptr);
    }
    const std::uint32_t p0 = rig.v.loops().position(0);
    for (int k = 1; k <= 300; ++k) {  // 300 blocks: a bend of 0.0003 st would have moved the loop by a sample or more
      rig.block(nullptr);
      CHECK(rig.v.loops().position(0) == (p0 + 64u * static_cast<std::uint32_t>(k)) % len);
    }
  }
  // just outside the zone the slot does bend (a position that drifts off the nominal one)
  SlotRig rig(4000);
  std::vector<float> x(1280, 0.3f);
  rig.block(x.data(), {o3sdk::dsp::param_change(Vocoder::loop_param(0, LF::Rec), 1.0f)});
  for (std::size_t p = 64; p < x.size(); p += 64) {
    rig.block(x.data() + p);
  }
  rig.block(nullptr, {o3sdk::dsp::param_change(Vocoder::loop_param(0, LF::Rec), 0.0f)});
  const std::uint32_t len = rig.v.loops().length(0);
  rig.block(nullptr, {o3sdk::dsp::param_change(Vocoder::loop_param(0, LF::Play), 1.0f)});
  rig.block(nullptr);
  rig.block(nullptr, {o3sdk::dsp::param_change(Vocoder::Bend, 0.05f)});
  for (int k = 0; k < 40; ++k) {
    rig.block(nullptr);
  }
  const std::uint32_t p0 = rig.v.loops().position(0);
  for (int k = 0; k < 300; ++k) {
    rig.block(nullptr);
  }
  CHECK(rig.v.loops().position(0) != (p0 + 64u * 300u) % len);
}
