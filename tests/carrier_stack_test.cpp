// SPDX-License-Identifier: GPL-3.0-or-later
// The carrier's oscillator stack (U10a): the plan under its cycle budget, the old path kept bit for bit, aliasing, SUB, TILT, the SHAPE morph
// and the step when the plan changes.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/carrier.h>
#include <o3_voc03er/units/vocoder/carrier_plan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace o3_voc03er;

namespace {

constexpr float kFs = 44100.0f;

// The scenario whose hashes were taken on the Carrier of before this slice (set_wave, set_detune_cents(0)): three notes, a fourth, two released.
std::uint64_t scenario_hash(int wave, float cents, float noise) {
  Carrier c;
  c.prepare(kFs);
  c.set_attack_ms(2.0f);
  c.set_release_ms(30.0f);
  c.set_shape(static_cast<float>(wave));
  c.set_detune_cents(cents);
  c.set_noise(noise);
  std::uint64_t h = 1469598103934665603ull;
  float buf[64], gate[64];
  for (int b = 0; b < 40; ++b) {
    if (b == 0) {
      c.note_on(48);
      c.note_on(55);
      c.note_on(64);
    }
    if (b == 8) {
      c.note_on(100);
    }
    if (b == 20) {
      c.note_off(55);
      c.note_off(48);
    }
    c.render(buf, 64, gate);
    for (int i = 0; i < 64; ++i) {
      std::uint32_t u;
      std::memcpy(&u, &buf[i], 4);
      h = (h ^ u) * 1099511628211ull;
      std::memcpy(&u, &gate[i], 4);
      h = (h ^ u) * 1099511628211ull;
    }
  }
  return h;
}

struct Cfg {
  float shape = 0.0f;
  float spread = 0.0f;
  float sub = 0.0f;
  float tilt = 0.0f;
};

void setup(Carrier& c, const Cfg& g) {
  c.prepare(kFs);
  c.set_attack_ms(0.1f);
  c.set_release_ms(30.0f);
  c.set_shape(g.shape);
  c.set_detune_cents(g.spread);
  c.set_sub(g.sub);
  c.set_tilt_db(g.tilt);
}

// `skip` blocks are rendered and dropped (the tilt settles in a few), then `n` samples are kept.
std::vector<float> render_notes(const Cfg& g, const std::vector<int>& notes, unsigned skip, unsigned n) {
  Carrier c;
  setup(c, g);
  for (int k : notes) {
    c.note_on(static_cast<std::uint8_t>(k));
  }
  std::vector<float> out;
  float buf[64];
  for (unsigned b = 0; b < skip + n / 64; ++b) {
    c.render(buf, 64);
    if (b >= skip) {
      out.insert(out.end(), buf, buf + 64);
    }
  }
  return out;
}

// Hann-windowed power spectrum, N = x.size() (a power of two or not), bins 0..N/2.
std::vector<double> spectrum(const std::vector<float>& x) {
  const std::size_t n = x.size();
  std::vector<double> tw_c(n), tw_s(n), w(n);
  const double pi = 3.14159265358979323846;
  for (std::size_t i = 0; i < n; ++i) {
    tw_c[i] = std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(n));
    tw_s[i] = std::sin(2.0 * pi * static_cast<double>(i) / static_cast<double>(n));
    w[i] = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(n));
  }
  std::vector<double> p(n / 2 + 1);
  for (std::size_t k = 0; k <= n / 2; ++k) {
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t j = (k * i) % n;
      const double v = static_cast<double>(x[i]) * w[i];
      re += v * tw_c[j];
      im -= v * tw_s[j];
    }
    p[k] = re * re + im * im;
  }
  return p;
}

double peak_near(const std::vector<double>& p, double bin, int span) {
  double m = 0.0;
  const long c = std::lround(bin);
  for (long k = c - span; k <= c + span; ++k) {
    if (k >= 0 && k < static_cast<long>(p.size())) {
      m = std::max(m, p[static_cast<std::size_t>(k)]);
    }
  }
  return m;
}

double hz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// The share of the energy that is not on a harmonic of f0 (a Hann main lobe is +-2 bins), in dB: what the aliasing and the BLEP's error leave.
double nonharmonic_db(const std::vector<float>& x, double f0) {
  const auto p = spectrum(x);
  const double n = static_cast<double>(x.size());
  std::vector<bool> harm(p.size(), false);
  for (std::size_t k = 0; k < std::min<std::size_t>(3, p.size()); ++k) {
    harm[k] = true;  // DC
  }
  for (double f = f0; f < static_cast<double>(kFs) * 0.5; f += f0) {
    const long c = std::lround(f / static_cast<double>(kFs) * n);
    for (long k = c - 2; k <= c + 2; ++k) {
      if (k >= 0 && k < static_cast<long>(p.size())) {
        harm[static_cast<std::size_t>(k)] = true;
      }
    }
  }
  double tot = 0.0, non = 0.0;
  for (std::size_t k = 0; k < p.size(); ++k) {
    tot += p[k];
    non += harm[k] ? 0.0 : p[k];
  }
  return 10.0 * std::log10(non / tot);
}

}  // namespace

TEST_CASE("carrier plan: the table, the budget and the card's check") {
  using carrier_cost::kBudget;
  for (unsigned n = 0; n <= 9; ++n) {
    for (unsigned f = 0; f < 16; ++f) {
      const bool spread = (f & 1u) != 0, morph = (f & 2u) != 0, sub = (f & 4u) != 0, tilt = (f & 8u) != 0;
      const CarrierPlan p = plan_oscillators(n, spread, morph, sub, tilt);
      const unsigned nn = n > 8 ? 8 : n;
      CHECK(p.third <= nn);
      CHECK(p.morph1 <= nn);
      CHECK(p.morph2 <= nn);
      CHECK((spread ? true : p.third == 0));
      CHECK((morph ? true : p.morphed() == 0));
      CHECK(p.morph3 == (morph ? p.third : 0));
      const unsigned expect = nn == 0 ? 0u
                                      : 2u * nn * carrier_cost::kOsc + (sub ? nn * carrier_cost::kSub : 0u) + (tilt ? carrier_cost::kTilt : 0u) +
                                            (p.morph1 + p.morph2) * carrier_cost::kMorph +
                                            p.third * (carrier_cost::kOsc + (morph ? carrier_cost::kMorph : 0u));
      CHECK(p.cycles == expect);
      CHECK(p.cycles <= kBudget);  // the base itself fits (8 voices, SUB and TILT: 38,146), so the plan never goes past the budget
      if (n > 0 && n < 8 && spread && !morph) {
        const CarrierPlan q = plan_oscillators(n + 1, spread, morph, sub, tilt);
        CHECK(n - p.third <= n + 1 - q.third);  // a voice more never serves more of the older ones
      }
    }
  }
  // The card's check: SPREAD and SUB on, no morph: the third oscillator on 2 voices at 8 notes, on all of them at 6 or fewer.
  CHECK(plan_oscillators(8, true, false, true, false).third == 2);
  CHECK(plan_oscillators(8, true, false, true, true).third == 2);
  CHECK(plan_oscillators(7, true, false, true, false).third == 4);
  for (unsigned n = 1; n <= 6; ++n) {
    CHECK(plan_oscillators(n, true, false, true, false).third == n);
  }
  for (unsigned n = 6; n <= 8; ++n) {
    const struct { const char* name; bool s, m, u, t; } rows[] = {{"spread", true, false, false, false}, {"spread+sub", true, false, true, false},
                                                                  {"spread+sub+tilt", true, false, true, true}, {"morph", false, true, false, false},
                                                                  {"morph+spread", true, true, false, false}, {"all", true, true, true, true}};
    for (const auto& r : rows) {
      const CarrierPlan p = plan_oscillators(n, r.s, r.m, r.u, r.t);
      std::printf("plan n=%u %-16s third=%u morph1=%u morph2=%u morph3=%u cycles=%u\n", n, r.name, unsigned{p.third}, unsigned{p.morph1},
                  unsigned{p.morph2}, unsigned{p.morph3}, p.cycles);
    }
  }
}

TEST_CASE("carrier stack: SPREAD, SUB and TILT at 0 with SHAPE 0 or 1 are the old path, bit for bit") {
  // Hashes taken on the Carrier before U10a (the same scenario, set_wave).
  CHECK(scenario_hash(0, 0.0f, 0.0f) == 0xcf05b7523239e764ull);
  CHECK(scenario_hash(0, 0.0f, 0.3f) == 0xa5d09b4b0f5e15c9ull);
  CHECK(scenario_hash(1, 0.0f, 0.0f) == 0x05ba60a92267c143ull);
  CHECK(scenario_hash(1, 0.0f, 0.3f) == 0xaaf624419242d5b1ull);
  // The two-oscillator hashes of before (cents 8: 0xf22e8d941d401e69, 0x670086553b58603d) do not hold: SPREAD > 0 now adds the third oscillator
  // on every voice the plan serves (4 notes here), by design. A voice the plan does not serve is the two-oscillator loop of before.
  CHECK(scenario_hash(0, 8.0f, 0.0f) != 0xf22e8d941d401e69ull);
}

TEST_CASE("carrier stack: aliasing at the top notes for each SHAPE stays under its bound") {
  const float shapes[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
  double worst = -200.0;
  for (int note : {84, 96, 108}) {
    for (float s : shapes) {
      Cfg g;
      g.shape = s;
      const double db = nonharmonic_db(render_notes(g, {note}, 8, 4096), hz(note));
      std::printf("aliasing note %d shape %.1f: %.1f dB below the whole\n", note, static_cast<double>(s), db);
      worst = std::max(worst, db);
    }
  }
  CHECK(worst < -18.0);  // polyBLEP at 1 to 4 kHz (the saw and pulse of before: -25.6 dB at note 108); the narrow pulse at 4.2 kHz is the worst, -19.2 dB:
                         // its two edges fall within a sample of each other. The measured values are printed above.
}

TEST_CASE("carrier stack: SUB sounds an octave below") {
  Cfg off, on;
  on.sub = 1.0f;
  const std::vector<int> note{57};  // 220 Hz
  const auto p0 = spectrum(render_notes(off, note, 8, 4096));
  const auto p1 = spectrum(render_notes(on, note, 8, 4096));
  const double bin110 = 110.0 / static_cast<double>(kFs) * 4096.0;
  const double bin220 = 220.0 / static_cast<double>(kFs) * 4096.0;
  CHECK(peak_near(p1, bin110, 2) > 1000.0 * peak_near(p0, bin110, 2));  // nothing there without SUB
  // The highest peak below the note's own fundamental is the sub's: 110 Hz.
  std::size_t best = 4;
  for (std::size_t k = 4; k < static_cast<std::size_t>(bin220 * 0.8); ++k) {
    if (p1[k] > p1[best]) {
      best = k;
    }
  }
  CHECK(std::abs(static_cast<double>(best) - bin110) <= 1.5);
}

TEST_CASE("carrier stack: TILT is within 1 dB of its claim at 100 Hz and 10 kHz") {
  for (float t : {12.0f, -12.0f, 6.0f}) {
    Cfg flat, tilted;
    tilted.tilt = t;
    // note 43 is 98 Hz (its fundamental is the 100 Hz point); the 13th harmonic of note 79 is 10.2 kHz.
    const auto lo0 = spectrum(render_notes(flat, {43}, 40, 4096));
    const auto lo1 = spectrum(render_notes(tilted, {43}, 40, 4096));
    const auto hi0 = spectrum(render_notes(flat, {79}, 40, 4096));
    const auto hi1 = spectrum(render_notes(tilted, {79}, 40, 4096));
    const double bl = hz(43) / static_cast<double>(kFs) * 4096.0;
    const double bh = 13.0 * hz(79) / static_cast<double>(kFs) * 4096.0;
    const double d_lo = 10.0 * std::log10(peak_near(lo1, bl, 2) / peak_near(lo0, bl, 2));
    const double d_hi = 10.0 * std::log10(peak_near(hi1, bh, 2) / peak_near(hi0, bh, 2));
    std::printf("tilt %+.0f dB: at 98 Hz %+.2f dB (claim %+.1f), at 10.2 kHz %+.2f dB (claim %+.1f)\n", static_cast<double>(t), d_lo,
                static_cast<double>(-t / 2), d_hi, static_cast<double>(t / 2));
    CHECK(std::abs(d_lo - static_cast<double>(-t / 2)) < 1.0);
    CHECK(std::abs(d_hi - static_cast<double>(t / 2)) < 1.0);
  }
}

TEST_CASE("carrier stack: SHAPE has no jump in level or polarity through the ends of the morph") {
  struct Pair { float a, b; };
  for (const Pair pr : {Pair{0.0f, 0.0005f}, Pair{0.9995f, 1.0f}, Pair{1.0f, 1.0005f}}) {
    Cfg ga, gb;
    ga.shape = pr.a;
    gb.shape = pr.b;
    const auto a = render_notes(ga, {45}, 2, 2048);
    const auto b = render_notes(gb, {45}, 2, 2048);
    double worst = 0.0, peak = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
      worst = std::max(worst, static_cast<double>(std::fabs(a[i] - b[i])));
      peak = std::max(peak, static_cast<double>(std::fabs(a[i])));
    }
    std::printf("shape %.4f vs %.4f: worst sample difference %.4f of a peak %.3f\n", static_cast<double>(pr.a), static_cast<double>(pr.b), worst, peak);
    CHECK(worst < 0.2 * peak);  // 1.0005 moves the pulse edges by 0.08 sample at this note (a 0.04 step, 15 % of the peak); a jump in level or polarity would be 100 % or 200 %
  }
}

TEST_CASE("carrier stack: a change of plan (a voice starts) steps the output by no more than the bound") {
  // SHAPE between the ends, SPREAD and SUB on: six notes, then a seventh and an eighth change the plan (third oscillators and morphs drop
  // out of the older voices), then two are released. The largest sample-to-sample step is compared with the steady state's.
  Carrier c;
  Cfg g;
  g.shape = 0.5f;
  g.spread = 30.0f;
  g.sub = 0.5f;
  g.tilt = 3.0f;
  setup(c, g);
  c.set_attack_ms(4.0f);
  const int notes[] = {40, 44, 47, 52, 56, 59, 64, 67};
  for (int i = 0; i < 6; ++i) {
    c.note_on(static_cast<std::uint8_t>(notes[i]));
  }
  float prev = 0.0f, buf[64];
  double steady = 0.0, change = 0.0;
  for (int b = 0; b < 60; ++b) {
    if (b == 20) {
      c.note_on(static_cast<std::uint8_t>(notes[6]));
    }
    if (b == 30) {
      c.note_on(static_cast<std::uint8_t>(notes[7]));
    }
    c.render(buf, 64);
    double m = 0.0;
    for (int i = 0; i < 64; ++i) {
      m = std::max(m, static_cast<double>(std::fabs(buf[i] - prev)));
      prev = buf[i];
    }
    if (b >= 4 && b < 20) {
      steady = std::max(steady, m);
    }
    if (b >= 20 && b < 50) {
      change = std::max(change, m);
    }
  }
  std::printf("plan change: steady max step %.4f, max step around the changes %.4f\n", steady, change);
  CHECK(change < 1.5);  // the output is within about +-2 before the soft clip; the step of the plan's switch stays under this
  CHECK(change <= 1.1 * steady + 0.05);  // measured: 0.45 around the changes against 0.57 in the steady state
}
