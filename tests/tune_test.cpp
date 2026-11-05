// SPDX-License-Identifier: GPL-3.0-or-later
// Tune (the YIN pitch detector and the voicing gate) against synthetic signals with a known truth: saw and glottal-pulse voices through
// formant resonators, a sweep with octave errors counted, a breathy low voice, vibrato, noise, silence, a voiced-to-unvoiced transition,
// an onset after silence, block-length independence, NaN. No recording is used (the plan's three sung phrases do not exist yet).
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/tune.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace o3_voc03er;

namespace {

constexpr double kFs = 44100.0;
constexpr double kPi = 3.14159265358979323846;

static_assert(sizeof(Tune) <= 14 * 1024, "the detector's hot state is about 14 KiB");

enum class Src { Saw, Glottal };

struct Formants {
  double f[3], bw[3], g[3];
};
constexpr Formants kA{{700, 1220, 2600}, {90, 110, 170}, {1.0, 0.7, 0.4}};
constexpr Formants kI{{270, 2290, 3010}, {60, 100, 120}, {1.0, 0.5, 0.3}};

struct Reson {
  double a1 = 0, a2 = 0, g = 0, y1 = 0, y2 = 0;
  void set(double f, double bw, double gain) {
    const double r = std::exp(-kPi * bw / kFs);
    a1 = 2.0 * r * std::cos(2.0 * kPi * f / kFs);
    a2 = -r * r;
    g = gain * (1.0 - r);
  }
  double run(double x) {
    const double y = g * x + a1 * y1 + a2 * y2;
    y2 = y1;
    y1 = y;
    return y;
  }
};

struct Rng {
  std::uint32_t s = 12345;
  double white() {  // -1..1
    s = s * 1664525u + 1013904223u;
    return static_cast<double>(s >> 8) / 8388608.0 - 1.0;
  }
};

// A voice generator: the source at f0(t), through three formant resonators, scaled to a peak of about `peak`, plus breath noise.
struct Voice {
  Src src;
  Formants fm;
  Reson r[3];
  double phase = 0, prev_g = 0, t = 0, peak, noise;
  Rng rng;
  Voice(Src s, const Formants& f, double pk = 0.3, double noise_amt = 0.0, std::uint32_t seed = 12345) : src(s), fm(f), peak(pk), noise(noise_amt) {
    for (int i = 0; i < 3; ++i) r[i].set(f.f[i], f.bw[i], f.g[i]);
    rng.s = seed;
  }
  static double blep(double p, double dt) {  // the polynomial band-limited step at the wrap, so the saw has no aliasing jitter
    if (p < dt) { const double x = p / dt; return x + x - x * x - 1.0; }
    if (p > 1.0 - dt) { const double x = (p - 1.0) / dt; return x * x + x + x + 1.0; }
    return 0.0;
  }
  double source(double p, double dt) {
    if (src == Src::Saw) return 2.0 * p - 1.0 - blep(p, dt);
    // Rosenberg pulse: open 0..0.4, closing to 0.56; its derivative (the glottal flow derivative) is the source.
    double g = 0.0;
    if (p < 0.4) g = 0.5 * (1.0 - std::cos(kPi * p / 0.4));
    else if (p < 0.56) g = std::cos(0.5 * kPi * (p - 0.4) / 0.16);
    const double d = (g - prev_g) * 20.0;
    prev_g = g;
    return d;
  }
  float next(double f0) {
    phase += f0 / kFs;
    if (phase >= 1.0) phase -= 1.0;
    const double s = source(phase, f0 / kFs) + noise * rng.white();
    double y = 0;
    for (int i = 0; i < 3; ++i) y += r[i].run(s);
    return static_cast<float>(y * peak * 3.0);  // the gain is calibrated loosely; level() below is what the tests rely on
  }
};

double cents(double f, double f0) { return 1200.0 * std::log2(f / f0); }

// Runs `n` samples of `gen(i)` through the detector in blocks of `blk`, calling `each(i_end)` after every block.
template <class Gen, class Each>
void run(Tune& t, std::size_t n, unsigned blk, Gen gen, Each each) {
  std::vector<float> buf(blk);
  std::size_t i = 0;
  while (i < n) {
    const unsigned m = static_cast<unsigned>(std::min<std::size_t>(blk, n - i));
    for (unsigned k = 0; k < m; ++k) buf[k] = gen(i + k);
    t.push(buf.data(), m);
    i += m;
    each(i);
  }
}

struct Track {
  double max_abs_cents = 0;
  unsigned blocks = 0, unvoiced = 0, octave = 0;
};

// A steady voice at f0: after `settle` samples, every block must be voiced; returns the worst error in cents.
Track steady(Src s, const Formants& fm, double f0, double noise, Tune::Range range = Tune::Range::Normal, double peak = 0.3, std::uint32_t seed = 12345) {
  Tune t;
  t.set_range(range);
  Voice v(s, fm, peak, noise, seed);
  Track tr;
  const std::size_t n = 44100 * 6 / 10, settle = 44100 * 25 / 100;
  run(t, n, 64, [&](std::size_t) { return v.next(f0); }, [&](std::size_t i) {
    if (i < settle) return;
    ++tr.blocks;
    if (!t.voiced()) {
      ++tr.unvoiced;
      return;
    }
    const double c = std::fabs(cents(t.frequency(), f0));
    if (c > 500.0) ++tr.octave;
    else if (c > tr.max_abs_cents) tr.max_abs_cents = c;
  });
  return tr;
}

}  // namespace

TEST_CASE("clean voices are tracked within a few cents, 80 to 800 Hz") {
  const double fs[] = {80, 100, 130, 165, 220, 277, 350, 440, 550, 700, 800};
  double worst = 0;
  for (Src s : {Src::Saw, Src::Glottal})
    for (const Formants* fm : {&kA, &kI})
      for (double f0 : fs) {
        const Track tr = steady(s, *fm, f0, 0.0);
        std::printf("tune acc src=%d f0=%.0f max=%.2f cents unv=%u oct=%u\n", static_cast<int>(s), f0, tr.max_abs_cents, tr.unvoiced, tr.octave);
        CHECK(tr.unvoiced == 0);
        CHECK(tr.octave == 0);
        CHECK(tr.max_abs_cents <= 1.0);  // measured worst 0.12 cents
        if (tr.max_abs_cents > worst) worst = tr.max_abs_cents;
      }
  std::printf("tune accuracy worst %.2f cents\n", worst);
}

TEST_CASE("octave errors over a semitone sweep, clean and breathy") {
  unsigned clean_err = 0, clean_unv = 0, clean_n = 0, breathy_err = 0, breathy_unv = 0, breathy_n = 0, breathy_unv_high = 0;
  for (int k = 0; k <= 40; ++k) {
    const double f0 = 70.0 * std::pow(2.0, k / 12.0 * (std::log2(800.0 / 70.0) * 12.0 / 40.0) / 1.0 / 12.0 * 12.0 / 12.0);
    const Track c = steady(Src::Glottal, kA, f0, 0.0);
    clean_err += c.octave;
    clean_unv += c.unvoiced;
    clean_n += c.blocks;
  }
  // The hard case (review 1.5): a low, breathy voice, 70 to 200 Hz, noise at about -10 dB against the source.
  for (int k = 0; k <= 20; ++k) {
    const double f0 = 70.0 * std::pow(2.0, k / 20.0 * std::log2(200.0 / 70.0));
    const Track b = steady(Src::Glottal, kA, f0, 0.35, Tune::Range::Normal, 0.3, 77u + static_cast<unsigned>(k));
    if (f0 >= 80.0) breathy_unv_high += b.unvoiced;
    breathy_err += b.octave;
    breathy_unv += b.unvoiced;
    breathy_n += b.blocks;
  }
  std::printf("tune sweep clean: %u octave/other errors, %u unvoiced of %u blocks\n", clean_err, clean_unv, clean_n);
  std::printf("tune sweep breathy: %u octave/other errors, %u unvoiced of %u blocks\n", breathy_err, breathy_unv, breathy_n);
  CHECK(clean_err == 0);
  CHECK(clean_unv == 0);
  CHECK(breathy_err * 100 <= breathy_n);  // at most 1 % of the blocks (measured 33 of 5082, all at 154 Hz)
  CHECK(breathy_unv_high == 0);           // below 80 Hz the 23 ms window sees under two periods and the noise wins: measured, not claimed
}

TEST_CASE("vibrato of 6 Hz at +-50 cents is followed without flips") {
  Tune t;
  Voice v(Src::Glottal, kA);
  double ph = 0;
  const double fc = 220.0;
  unsigned flips = 0, unv = 0, n = 0;
  double worst = 0, prev = 0;
  run(t, 44100 * 2, 64, [&](std::size_t) {
    ph += 6.0 / kFs;
    return v.next(fc * std::pow(2.0, 50.0 / 1200.0 * std::sin(2.0 * kPi * ph)));
  }, [&](std::size_t i) {
    if (i < 44100 / 4) return;
    ++n;
    if (!t.voiced()) { ++unv; return; }
    const double c = cents(t.frequency(), fc);
    if (std::fabs(c) > worst) worst = std::fabs(c);
    if (prev > 0 && std::fabs(cents(t.frequency(), prev)) > 100.0) ++flips;
    prev = t.frequency();
  });
  std::printf("tune vibrato: worst %.1f cents from centre, %u flips, %u unvoiced of %u blocks\n", worst, flips, unv, n);
  CHECK(flips == 0);
  CHECK(unv == 0);
  CHECK(worst < 75.0);  // +-50 of vibrato and the estimate's age (about 20 ms of a 6 Hz cycle is up to 36 cents)
}

TEST_CASE("noise and silence are unvoiced") {
  Tune t;
  Rng rng;
  unsigned voiced = 0, n = 0;
  run(t, 44100 * 5, 64, [&](std::size_t) { return static_cast<float>(0.2 * rng.white()); }, [&](std::size_t i) {
    if (i < 8192) return;
    ++n;
    if (t.voiced()) ++voiced;
  });
  std::printf("tune white noise: %u of %u blocks voiced\n", voiced, n);
  CHECK(voiced * 100 <= n);  // at most 1 %
  Tune s;
  unsigned sv = 0;
  run(s, 44100, 64, [](std::size_t) { return 0.0f; }, [&](std::size_t) { if (s.voiced()) ++sv; });
  CHECK(sv == 0);
  CHECK(s.frequency() == 0.0f);
  // Low-level noise under the level floor, and a hiss (high-passed noise) of voice level: the low-band ratio rejects it.
  Tune h;
  float prev = 0;
  unsigned hv = 0, hn = 0;
  run(h, 44100 * 3, 64, [&](std::size_t) { const float x = static_cast<float>(0.2 * rng.white()); const float y = x - prev; prev = x; return y; }, [&](std::size_t i) {
    if (i < 8192) return;
    ++hn;
    if (h.voiced()) ++hv;
  });
  std::printf("tune hiss: %u of %u blocks voiced\n", hv, hn);
  CHECK(hv * 100 <= hn);
}

TEST_CASE("voiced to unvoiced: held about 60 ms, one transition, no chatter") {
  for (int mode = 0; mode < 2; ++mode) {  // 0: silence after the voice, 1: breath noise
    Tune t;
    Voice v(Src::Glottal, kA);
    Rng rng;
    const std::size_t end = 44100 / 2;
    unsigned transitions = 0;
    bool last = false;
    long off_at = -1;
    run(t, 44100, 64, [&](std::size_t i) { return i < end ? v.next(200.0) : (mode ? static_cast<float>(0.05 * rng.white()) : 0.0f); }, [&](std::size_t i) {
      if (t.voiced() != last) {
        ++transitions;
        last = t.voiced();
        if (!last && off_at < 0) off_at = static_cast<long>(i);
      }
    });
    const double ms = (static_cast<double>(off_at) - static_cast<double>(end)) / kFs * 1000.0;
    std::printf("tune voiced end mode %d: unvoiced %.1f ms after the last voiced sample, %u transitions\n", mode, ms, transitions);
    CHECK(transitions == 2);  // on once, off once
    CHECK(ms >= 60.0);   // the estimate is 15 to 30 ms old, then the hold
    CHECK(ms <= 100.0);
  }
}

TEST_CASE("a short dropout is bridged by the hold, a long one is not") {
  for (unsigned gap_ms : {20u, 30u, 150u}) {
    Tune t;
    Voice v(Src::Glottal, kA);
    const std::size_t a = 44100 / 3, b = a + static_cast<std::size_t>(gap_ms) * 441 / 10;
    unsigned offs = 0;
    bool last = false;
    run(t, 44100, 64, [&](std::size_t i) { const float x = v.next(180.0); return (i >= a && i < b) ? 0.0f : x; }, [&](std::size_t i) {
      if (i > 8192 && last && !t.voiced()) ++offs;
      last = t.voiced();
    });
    std::printf("tune dropout %u ms: %u falls\n", gap_ms, offs);
    if (gap_ms <= 30) CHECK(offs == 0);
    else CHECK(offs == 1);
  }
}

TEST_CASE("a tone after silence is found, within the latency") {
  const double fs[] = {80, 150, 300, 600};
  double worst_ms = 0;
  for (double f0 : fs)
    for (std::size_t pre : {44100u / 3, 44100u / 3 + 100, 44100u / 3 + 200, 44100u / 3 + 301}) {
      Tune t;
      Voice v(Src::Glottal, kA);
      long on_at = -1;
      double err = 0, err_later = 99.0;
      run(t, 44100, 64, [&](std::size_t i) { return i < pre ? 0.0f : v.next(f0); }, [&](std::size_t i) {
        if (on_at < 0 && t.voiced()) {
          on_at = static_cast<long>(i);
          err = cents(t.frequency(), f0);
        }
        if (on_at >= 0 && i == static_cast<std::size_t>(on_at) + 2048) err_later = cents(t.frequency(), f0);
      });
      REQUIRE(on_at >= 0);
      const double ms = (static_cast<double>(on_at) - static_cast<double>(pre)) / kFs * 1000.0;
      std::printf("tune onset f0=%.0f pre=%zu: voiced after %.1f ms, first estimate %.1f cents off\n", f0, pre, ms, err);
      if (ms > worst_ms) worst_ms = ms;
      CHECK(std::fabs(err) <= 40.0);       // the first hop sees a window that is partly silence
      CHECK(std::fabs(err_later) <= 1.0);  // four hops later it is settled
    }
  std::printf("tune onset latency worst %.1f ms\n", worst_ms);
  CHECK(worst_ms <= 50.0);  // measured 47.5 at 80 Hz, 11 to 25 ms from 300 Hz up
}

TEST_CASE("any block length gives the same estimates") {
  auto sig = [](std::size_t n) {
    std::vector<float> x(n);
    Voice v(Src::Glottal, kA, 0.3, 0.05);
    Rng rng;
    double ph = 0;
    for (std::size_t i = 0; i < n; ++i) {
      ph += 5.0 / kFs;
      const float s = v.next(140.0 * std::pow(2.0, 0.03 * std::sin(2.0 * kPi * ph)) * (i > 30000 ? 1.5 : 1.0));
      x[i] = (i > 60000 && i < 64000) ? static_cast<float>(0.1 * rng.white()) : s;
    }
    return x;
  };
  const std::size_t n = 44100;  // a multiple of 64 and of 1
  const std::vector<float> x = sig(n);
  struct Snap { float f, c; bool v; };
  auto go = [&](unsigned pattern) {
    Tune t;
    std::vector<Snap> out;
    std::size_t i = 0;
    unsigned k = 0;
    static const unsigned mix[] = {1, 7, 64, 33, 2, 63, 17, 5};
    while (i < n) {
      unsigned m = pattern == 0 ? mix[k++ % 8] : pattern;
      if (i + m > n) m = static_cast<unsigned>(n - i);
      t.push(x.data() + i, m);
      i += m;
      if (i % 64 == 0) out.push_back({t.frequency(), t.confidence(), t.voiced()});
    }
    return out;
  };
  // With block = 64 every checkpoint is hit; the others only where i % 64 == 0, so compare at the sample counts they share by a
  // second pass that checkpoints every 64 samples regardless of the push sizes.
  auto go64 = [&](unsigned blk) {
    Tune t;
    std::vector<Snap> out;
    for (std::size_t i = 0; i + 64 <= n; i += 64) {  // whole 64-sample checkpoints only: never read past the data
      for (std::size_t j = 0; j < 64; j += blk) t.push(x.data() + i + j, static_cast<unsigned>(std::min<std::size_t>(blk, 64 - j)));
      out.push_back({t.frequency(), t.confidence(), t.voiced()});
    }
    return out;
  };
  const auto ref = go64(64);
  for (unsigned blk : {1u, 3u, 7u, 16u, 33u}) {
    const auto o = go64(blk);
    REQUIRE(o.size() == ref.size());
    for (std::size_t i = 0; i < o.size(); ++i) {
      CHECK(std::memcmp(&o[i].f, &ref[i].f, sizeof(float)) == 0);
      CHECK(std::memcmp(&o[i].c, &ref[i].c, sizeof(float)) == 0);
      CHECK(o[i].v == ref[i].v);
    }
  }
  const auto m = go(0);
  CHECK(!m.empty());
}

TEST_CASE("NaN and infinity do not poison the detector") {
  Tune t;
  Voice v(Src::Glottal, kA);
  bool finite = true;
  run(t, 44100 * 2, 64, [&](std::size_t i) {
    const float x = v.next(200.0);
    if (i >= 20000 && i < 20100) return std::nanf("");
    if (i >= 30000 && i < 30100) return INFINITY;
    return x;
  }, [&](std::size_t) {
    if (!std::isfinite(t.frequency()) || !std::isfinite(t.confidence())) finite = false;
  });
  CHECK(finite);
  CHECK(t.voiced());
  CHECK(std::fabs(cents(t.frequency(), 200.0)) <= 5.0);
}

TEST_CASE("the bass range tracks 55 to 400 Hz") {
  const double fs[] = {55, 65, 80, 110, 165, 220, 330, 400};
  for (double f0 : fs) {
    const Track tr = steady(Src::Glottal, kA, f0, 0.0, Tune::Range::Bass);
    std::printf("tune bass f0=%.0f max=%.2f cents unv=%u oct=%u\n", f0, tr.max_abs_cents, tr.unvoiced, tr.octave);
    CHECK(tr.unvoiced == 0);
    CHECK(tr.octave == 0);
    CHECK(tr.max_abs_cents <= 6.0);
  }
}
