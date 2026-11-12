// SPDX-License-Identifier: GPL-3.0-or-later
// Capture the mouth (MC1): a slot of audio becomes the PATTERN's MOUTH lane. The analysis (app/src/mouth_capture.h) is tested against the TRACT
// processor itself as an independent synthesiser: each of the nine vowel places, at two pitches, with noise and a perturbation, must come back as
// itself (or, for the in-between places, as a neighbour); silent gaps are closures; short and empty slots are refused; the step mapping is exact; the
// state machine, stepped in pieces, gives the one-shot result.
#include <doctest/doctest.h>

#include "../app/src/mouth_capture.h"

#include <o3sdk/extras/tract/processor.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace mc = o3_voc03er::mouthcap;
namespace tr = o3sdk::extras::tract;

namespace {

constexpr float kFs = 44100.0f;
constexpr std::uint32_t kSpanLen = 4410;  // 0.1 s a step

struct Rng {
  std::uint32_t s = 12345u;
  float next() {  // -1..1
    s = s * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<std::int32_t>(s >> 8) - (1 << 23)) * (1.0f / static_cast<float>(1 << 23));
  }
};

// One place through the tract: `n` samples of a saw at f0 (vibrato +-3 % at 5 Hz when `wobble`), the vowel at place/8 plus `shift`.
void synth_place(tr::Processor& p, unsigned place, float shift, float f0, bool wobble, float amp, std::uint32_t n, std::vector<float>& out, float& phase,
                 float& t, float fs) {
  p.set_vowel(static_cast<float>(place) * 0.125f + shift);
  std::vector<float> in(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    const float f = f0 * (wobble ? 1.0f + 0.03f * std::sin(6.2831853f * 5.0f * t) : 1.0f);
    phase += f / fs;
    phase -= std::floor(phase);
    in[i] = amp * (2.0f * phase - 1.0f);
    t += 1.0f / fs;
  }
  std::vector<float> o(n);
  p.process(in.data(), o.data(), n);
  out.insert(out.end(), o.begin(), o.end());
}

std::int16_t to16(float v) {
  v = v > 0.99f ? 0.99f : (v < -0.99f ? -0.99f : v);
  return static_cast<std::int16_t>(std::lround(v * 32767.0f));
}

// A take of the nine places in a row, a step each (the window is centred in its step), at f0.
std::vector<std::int16_t> nine_places(float f0, float noise_db, float shift, bool wobble, float fs = kFs) {
  tr::Processor p(tr::Processor::Quality::Full);
  p.prepare(fs);
  const std::uint32_t span = static_cast<std::uint32_t>(fs * 0.1f);  // 0.1 s a step
  p.set_mix(1.0f);
  p.set_glide_ms(5.0f);
  std::vector<float> w;
  float phase = 0.0f, t = 0.0f;
  for (unsigned k = 0; k < 9; ++k) {
    synth_place(p, k, shift, f0, wobble, 0.3f, span, w, phase, t, fs);
  }
  Rng r;
  const float na = std::pow(10.0f, noise_db / 20.0f);
  std::vector<std::int16_t> s(w.size());
  for (std::size_t i = 0; i < w.size(); ++i) {
    s[i] = to16(w[i] + na * r.next());
  }
  return s;
}

// A throw-away analysis that fills the references (a begin() alone leaves them zero: they are built by the first step() passes).
void warm(mc::Capture& c) {
  std::vector<std::int16_t> s(4096, 0);
  for (std::size_t i = 0; i < s.size(); ++i) {
    s[i] = static_cast<std::int16_t>(2000.0f * std::sin(0.05f * static_cast<float>(i)));
  }
  mc::Pick out[1];
  REQUIRE(mc::analyse(c, s.data(), 4096, 1, 1, kFs, out));
}

mc::Capture& cap() {
  static mc::Capture c;
  return c;
}

}  // namespace

TEST_CASE("mouthcap: the byte of a code, the references") {
  CHECK(mc::byte_of(0) == 1);
  CHECK(mc::byte_of(8) == 9);
  CHECK(mc::byte_of(mc::kClosure) == o3_voc03er::kMouthLips);
  CHECK(mc::byte_of(mc::kKeep) == 0);
  CHECK(o3_voc03er::mouth_valid(mc::byte_of(mc::kClosure)));
  mc::Capture& c = cap();
  warm(c);
  const mc::Refs& r = c.refs();
  for (unsigned k = 0; k < mc::kPlaces; ++k) {
    float mean = 0.0f;
    for (unsigned j = 0; j < mc::kAreas; ++j) {
      CHECK(std::fabs(r.la[k][j]) <= mc::kLogClamp + 1e-6f);
      mean += r.la[k][j];
    }
    CHECK(std::fabs(mean) < 0.2f);  // mean removed (the clamp may move it a little)
  }
  // the nine shapes are distinct: every reference is nearest to itself, with room
  float min_next = 1e9f;
  for (unsigned k = 0; k < mc::kPlaces; ++k) {
    float la[mc::kAreas];
    for (unsigned j = 0; j < mc::kAreas; ++j) {
      la[j] = r.la[k][j];
    }
    const mc::Pick p = mc::nearest(r, la);
    CHECK(p.code == k);
    CHECK(p.dist < 1e-6f);
    CHECK(p.next > 0.05f);
    min_next = p.next < min_next ? p.next : min_next;
  }
  std::printf("mouthcap references: smallest squared distance between two shapes %.3f\n", static_cast<double>(min_next));
}

TEST_CASE("mouthcap: the nine places, pitches, noise and perturbations") {
  mc::Capture& c = cap();
  struct Case {
    float f0, noise_db, shift;
    bool wobble;
  };
  // 110 and 180 Hz clean; both noisy (-50 dBFS) with the vowel moved 0.02 of the axis and a 5 Hz vibrato of 3 %; the extremes, 90 and 250 Hz at -40 dBFS.
  const Case cases[] = {{110.0f, -90.0f, 0.0f, false}, {180.0f, -90.0f, 0.0f, false}, {110.0f, -50.0f, 0.02f, true},
                        {180.0f, -50.0f, -0.02f, true}, {90.0f, -40.0f, 0.0f, true},   {250.0f, -40.0f, 0.0f, false}};
  constexpr unsigned kCases = sizeof(cases) / sizeof(cases[0]);
  unsigned exact[mc::kPlaces] = {}, near_[mc::kPlaces] = {};
  int confusions[mc::kPlaces][mc::kPlaces] = {};
  float worst[mc::kPlaces];  // the worst (dist / next) over the cases: under 1 the pick is a clear one
  for (float& w : worst) {
    w = 0.0f;
  }
  for (const Case& cs : cases) {
    const std::vector<std::int16_t> s = nine_places(cs.f0, cs.noise_db, cs.shift, cs.wobble);
    mc::Pick out[9];
    REQUIRE(mc::analyse(c, s.data(), static_cast<std::uint32_t>(s.size()), 9, 9, kFs, out));
    for (unsigned k = 0; k < 9; ++k) {
      REQUIRE(out[k].code < mc::kPlaces);
      ++confusions[k][out[k].code];
      if (out[k].code == k) {
        ++exact[k];
      }
      const int d = static_cast<int>(out[k].code) - static_cast<int>(k);
      if (d >= -1 && d <= 1) {
        ++near_[k];
      }
      const float ratio = out[k].dist / out[k].next;
      worst[k] = ratio > worst[k] ? ratio : worst[k];
    }
  }
  unsigned all_exact = 0;
  for (unsigned k = 0; k < mc::kPlaces; ++k) {
    std::printf("mouthcap place %u: exact %u/%u, within one %u/%u, worst dist/next %.2f; picks:", k, exact[k], kCases, near_[k], kCases,
                static_cast<double>(worst[k]));
    for (unsigned j = 0; j < mc::kPlaces; ++j) {
      std::printf(" %d", confusions[k][j]);
    }
    std::printf("\n");
    all_exact += exact[k];
    CHECK(near_[k] == kCases);                    // never farther than a neighbour
    if (k % 2u == 0u) {
      CHECK(exact[k] >= kCases - 1u);             // the five primary vowels: at most one miss in six
    }
  }
  CHECK(all_exact * 10u >= 9u * 9u * kCases);     // 90 % exact over all nine places
}

TEST_CASE("mouthcap: silent gaps and quiet steps are closures") {
  mc::Capture& c = cap();
  std::vector<std::int16_t> s = nine_places(140.0f, -90.0f, 0.0f, false);
  Rng r;
  const auto scale_step = [&](unsigned k, float g) {
    for (std::uint32_t i = 0; i < kSpanLen; ++i) {
      std::int16_t& v = s[k * kSpanLen + i];
      v = to16(static_cast<float>(v) * (1.0f / 32768.0f) * g + 3e-5f * r.next());
    }
  };
  scale_step(2, 0.0f);    // silence
  scale_step(5, 0.0f);
  scale_step(3, 0.1f);    // -20 dB: quiet, still a vowel
  scale_step(7, 0.02f);   // -34 dB: under the threshold, a closure
  mc::Pick out[9];
  REQUIRE(mc::analyse(c, s.data(), static_cast<std::uint32_t>(s.size()), 9, 9, kFs, out));
  const bool closure[9] = {false, false, true, false, false, true, false, true, false};
  for (unsigned k = 0; k < 9; ++k) {
    CHECK((out[k].code == mc::kClosure) == closure[k]);
    if (!closure[k]) {
      const int d = static_cast<int>(out[k].code) - static_cast<int>(k);
      CHECK(d >= -1);
      CHECK(d <= 1);
    }
  }
  CHECK(c.peak_rms() > 0.01f);
  CHECK(c.rms(2) < c.peak_rms() * mc::kClosureRatio);
  CHECK(c.rms(3) > c.peak_rms() * mc::kClosureRatio);
  // into the lane: a closure is LIPS, a vowel its place
  o3_voc03er::PatternData pd;
  REQUIRE(o3_voc03er::set_length(pd, 9));
  REQUIRE(mc::apply(c, pd));
  CHECK(o3_voc03er::get_mouth(pd, 2) == o3_voc03er::kMouthLips);
  CHECK(o3_voc03er::get_mouth(pd, 5) == o3_voc03er::kMouthLips);
  CHECK(o3_voc03er::get_mouth(pd, 0) == 1u);
}

TEST_CASE("mouthcap: empty, short, silent and bad requests") {
  mc::Capture& c = cap();
  std::vector<std::int16_t> s(8192, 0);
  mc::Pick out[16];
  CHECK_FALSE(c.begin(nullptr, 8192, 4, 4, kFs));
  CHECK(c.phase() == mc::Capture::Phase::Idle);
  CHECK_FALSE(c.begin(s.data(), 0, 4, 4, kFs));
  CHECK_FALSE(c.begin(s.data(), mc::kMinLength - 1u, 4, 4, kFs));  // under one window
  CHECK_FALSE(c.begin(s.data(), 8192, 0, 0, kFs));
  CHECK_FALSE(c.begin(s.data(), 8192, 4, 5, kFs));                 // more used than total
  CHECK_FALSE(c.begin(s.data(), 8192, 32, 17, kFs));               // more than the pattern holds
  CHECK_FALSE(c.begin(s.data(), 8192, 4, 4, 4000.0f));
  CHECK_FALSE(c.step());                                           // Idle: nothing to do
  CHECK(c.begin(s.data(), mc::kMinLength, 1, 1, kFs));             // exactly one window is enough
  c.cancel();
  // a slot of zeros: the run ends silent, nothing is written
  CHECK_FALSE(mc::analyse(c, s.data(), 8192, 4, 4, kFs, out));
  CHECK(c.done());
  CHECK(c.silent());
  o3_voc03er::PatternData pd;
  const std::uint8_t before = o3_voc03er::get_mouth(pd, 0);
  CHECK_FALSE(mc::apply(c, pd));
  CHECK(o3_voc03er::get_mouth(pd, 0) == before);
  // a take of a tenth of a LSB of noise: also silent (-70 dBFS floor)
  Rng r;
  for (std::int16_t& v : s) {
    v = static_cast<std::int16_t>(r.next() * 3.0f);
  }
  CHECK_FALSE(mc::analyse(c, s.data(), 8192, 4, 4, kFs, out));
  CHECK(c.silent());
  // apply() before the end of a run writes nothing
  CHECK(c.begin(s.data(), 8192, 4, 4, kFs));
  CHECK_FALSE(mc::apply(c, pd));
  c.cancel();
}

TEST_CASE("mouthcap: the step mapping") {
  const float bar = 88200.0f;  // 120 BPM at 44.1 kHz
  CHECK(mc::bars_of(88200, bar) == 1u);
  CHECK(mc::bars_of(176400, bar) == 2u);
  CHECK(mc::bars_of(88200u * 4u + 300u, bar) == 4u);  // a few hundred samples off a bar still reads 4.0
  CHECK(mc::bars_of(88200u + 44100u, bar) == 0u);     // 1.5 bars: no bars
  CHECK(mc::bars_of(100000, bar) == 0u);
  CHECK(mc::bars_of(88200, 0.0f) == 0u);
  // a bar: 16 steps; the pattern's length cuts the analysed part
  mc::Plan p = mc::plan(88200, bar, 16);
  CHECK(p.total == 16u);
  CHECK(p.used == 16u);
  p = mc::plan(88200, bar, 8);
  CHECK(p.total == 16u);
  CHECK(p.used == 8u);
  p = mc::plan(176400, bar, 16);  // two bars: 32 steps, the first 16 land in the pattern
  CHECK(p.total == 32u);
  CHECK(p.used == 16u);
  p = mc::plan(176400, bar, 5);
  CHECK(p.total == 32u);
  CHECK(p.used == 5u);
  // no bars: the take is spread over the pattern's length
  p = mc::plan(100000, bar, 12);
  CHECK(p.total == 12u);
  CHECK(p.used == 12u);
  p = mc::plan(100000, bar, 1);
  CHECK(p.total == 1u);
  CHECK(p.used == 1u);
  p = mc::plan(100000, bar, 99);  // a length over the pattern's maximum is clamped
  CHECK(p.total == 16u);
  CHECK(p.used == 16u);
  p = mc::plan(100000, 0.0f, 0);  // a length of 0 reads 1
  CHECK(p.total == 1u);
  // the edges: exact, monotonic, cover the slot, the spans differ by at most one sample (uneven lengths)
  const std::uint32_t lens[] = {1024u, 1025u, 4410u, 88200u, 100003u, 705600u};
  const std::uint32_t tots[] = {1u, 3u, 7u, 16u, 32u};
  for (std::uint32_t len : lens) {
    for (std::uint32_t t : tots) {
      CHECK(mc::span_edge(len, t, 0) == 0u);
      CHECK(mc::span_edge(len, t, t) == len);
      std::uint32_t lo = 0xFFFFFFFFu, hi = 0;
      for (std::uint32_t i = 0; i < t; ++i) {
        const std::uint32_t a = mc::span_edge(len, t, i), b = mc::span_edge(len, t, i + 1u);
        CHECK(b > a);
        lo = b - a < lo ? b - a : lo;
        hi = b - a > hi ? b - a : hi;
      }
      CHECK(hi - lo <= 1u);
    }
  }
  CHECK(mc::span_edge(1000, 0, 0) == 0u);
  // a step lands on its own samples: eight places in a slot of a bar of 8 steps, uneven (length 8 * 2205 + 3)
  mc::Capture& c = cap();
  std::vector<std::int16_t> s = nine_places(140.0f, -90.0f, 0.0f, false);
  s.resize(8u * 4410u + 3u, 0);  // eight places and 3 stray samples
  mc::Pick out[16];
  REQUIRE(mc::analyse(c, s.data(), static_cast<std::uint32_t>(s.size()), 8, 8, kFs, out));
  for (unsigned k = 0; k < 8; ++k) {
    CHECK(out[k].code == k);
  }
}

TEST_CASE("mouthcap: the state machine in pieces is the one-shot result") {
  static mc::Capture one, many;
  const std::vector<std::int16_t> s = nine_places(150.0f, -60.0f, 0.01f, true);
  const std::uint32_t len = static_cast<std::uint32_t>(s.size());
  mc::Pick ref[9];
  REQUIRE(mc::analyse(one, s.data(), len, 9, 9, kFs, ref));
  REQUIRE(many.begin(s.data(), len, 9, 9, kFs));
  CHECK(many.phase() == mc::Capture::Phase::Refs);  // a fresh object builds the references first
  unsigned last = many.percent();
  unsigned passes = 0;
  unsigned seen_level = 0, seen_shape = 0, seen_refs = 0;
  while (many.running()) {
    seen_refs += many.phase() == mc::Capture::Phase::Refs ? 1u : 0u;
    seen_level += many.phase() == mc::Capture::Phase::Level ? 1u : 0u;
    seen_shape += many.phase() == mc::Capture::Phase::Shape ? 1u : 0u;
    many.step();
    CHECK(many.percent() >= last);
    last = many.percent();
    REQUIRE(++passes < 1000u);  // bounded
  }
  CHECK(last == 100u);
  CHECK(seen_refs == mc::kPlaces);                  // one reference a pass
  CHECK(seen_level >= 9u);                          // at least one chunk a step
  CHECK(seen_shape == 9u);                          // one read a step
  CHECK(many.done());
  CHECK_FALSE(many.silent());
  for (unsigned k = 0; k < 9; ++k) {
    CHECK(many.pick(k).code == ref[k].code);
    CHECK(many.pick(k).dist == doctest::Approx(ref[k].dist));
  }
  // stepping a Done capture changes nothing
  CHECK_FALSE(many.step());
  CHECK(many.done());
  // the references are kept for the rate: the second run skips them, a new rate builds them again, same picks
  REQUIRE(many.begin(s.data(), len, 9, 9, kFs));
  CHECK(many.phase() == mc::Capture::Phase::Level);
  while (many.step()) {
  }
  for (unsigned k = 0; k < 9; ++k) {
    CHECK(many.pick(k).code == ref[k].code);
  }
  REQUIRE(many.begin(s.data(), len, 9, 9, 48000.0f));
  CHECK(many.phase() == mc::Capture::Phase::Refs);
  many.cancel();
  CHECK(many.phase() == mc::Capture::Phase::Idle);
  CHECK(many.percent() == 0u);
  CHECK_FALSE(many.running());
  // a long slot: the level pass takes several chunks a step, still bounded
  std::vector<std::int16_t> big(mc::kRmsChunk * 5u + 17u);
  for (std::size_t i = 0; i < big.size(); ++i) {
    big[i] = static_cast<std::int16_t>(3000.0f * std::sin(0.03f * static_cast<float>(i)));
  }
  REQUIRE(many.begin(big.data(), static_cast<std::uint32_t>(big.size()), 1, 1, kFs));
  passes = 0;
  while (many.step()) {
    REQUIRE(++passes < 100u);
  }
  CHECK(many.done());
  CHECK(passes >= 10u + 1u);  // 5 chunks of the mean sweep and 5 of the RMS sweep (the references of 44.1 kHz are kept: the cancelled 48 kHz run built none), one read
}

TEST_CASE("mouthcap: apply writes the MOUTH lane only, within the pattern's length") {
  mc::Capture& c = cap();
  const std::vector<std::int16_t> s = nine_places(140.0f, -90.0f, 0.0f, false);
  const std::uint32_t len = static_cast<std::uint32_t>(s.size());
  o3_voc03er::PatternData pd;
  REQUIRE(o3_voc03er::set_length(pd, 6));
  // something before: a mouth past the length, a GLD flag on step 1, a WORD at step 2
  REQUIRE(o3_voc03er::set_mouth(pd, 12, 4));
  REQUIRE(o3_voc03er::set_mouth(pd, 1, 1u | 0x80u));
  const o3_voc03er::PatternData before = pd;
  mc::Pick out[16];
  REQUIRE(mc::analyse(c, s.data(), len, 9, 6, kFs, out));
  REQUIRE(c.done());
  REQUIRE(mc::apply(c, pd));
  for (unsigned i = 0; i < 6; ++i) {
    CHECK(o3_voc03er::get_mouth(pd, i) == 1u + i);  // the place of the step, a plain vowel (GLD gone with the old value)
  }
  CHECK(o3_voc03er::get_mouth(pd, 12) == 4u);          // past the length: untouched
  CHECK(o3_voc03er::length_of(pd) == 6u);
  // nothing else of the pattern moved
  CHECK(std::memcmp(&pd, &before, sizeof(pd)) != 0);
  o3_voc03er::PatternData expect = before;
  for (unsigned i = 0; i < 6; ++i) {
    REQUIRE(o3_voc03er::set_mouth(expect, i, 1u + i));
  }
  CHECK(std::memcmp(&pd, &expect, sizeof(pd)) == 0);
}

TEST_CASE("mouthcap: a DC offset does not hide the closures") {
  mc::Capture& c = cap();
  const std::vector<std::int16_t> clean = nine_places(140.0f, -90.0f, 0.0f, false);
  const bool closure[9] = {false, false, true, false, false, true, false, true, false};
  // the same take with steps 2, 5 and 7 silent, then an offset of 0.01 of full scale (-40 dBFS) on every sample: the silence must still be a closure
  std::vector<std::int16_t> s = clean;
  for (const unsigned k : {2u, 5u, 7u}) {
    for (std::uint32_t i = 0; i < kSpanLen; ++i) {
      s[k * kSpanLen + i] = 0;
    }
  }
  mc::Pick ref[9];
  REQUIRE(mc::analyse(c, s.data(), static_cast<std::uint32_t>(s.size()), 9, 9, kFs, ref));
  for (unsigned k = 0; k < 9; ++k) {
    CHECK((ref[k].code == mc::kClosure) == closure[k]);
  }
  for (std::int16_t& v : s) {
    v = static_cast<std::int16_t>(v + 328);  // 0.01 * 32768 = -40 dBFS
  }
  mc::Pick out[9];
  REQUIRE(mc::analyse(c, s.data(), static_cast<std::uint32_t>(s.size()), 9, 9, kFs, out));
  for (unsigned k = 0; k < 9; ++k) {
    CHECK((out[k].code == mc::kClosure) == closure[k]);
    if (!closure[k]) {
      const int d = static_cast<int>(out[k].code) - static_cast<int>(ref[k].code);
      CHECK(d >= -1);
      CHECK(d <= 1);
    }
  }
  CHECK(c.rms(2) < 0.002f);  // the offset is gone from the level of a silent step (it would read 0.01)
  // a take that is only an offset is silent
  std::vector<std::int16_t> flat(8192, 500);
  CHECK_FALSE(mc::analyse(c, flat.data(), 8192, 4, 4, kFs, out));
  CHECK(c.silent());
}

TEST_CASE("mouthcap: the nine places at 48 kHz (the references are per rate)") {
  static mc::Capture c48;
  constexpr float kFs48 = 48000.0f;
  unsigned exact = 0, near_ = 0, all = 0;
  for (const float f0 : {110.0f, 180.0f}) {
    const std::vector<std::int16_t> s = nine_places(f0, -60.0f, 0.0f, false, kFs48);
    mc::Pick out[9];
    REQUIRE(mc::analyse(c48, s.data(), static_cast<std::uint32_t>(s.size()), 9, 9, kFs48, out));
    for (unsigned k = 0; k < 9; ++k) {
      ++all;
      exact += out[k].code == k ? 1u : 0u;
      const int d = static_cast<int>(out[k].code) - static_cast<int>(k);
      near_ += (d >= -1 && d <= 1) ? 1u : 0u;
    }
  }
  std::printf("mouthcap 48 kHz: exact %u/%u, within one %u/%u\n", exact, all, near_, all);
  CHECK(near_ == all);
  CHECK(exact * 10u >= 9u * all);
  // the rate changed under a kept capture: the references are built again
  const std::vector<std::int16_t> again = nine_places(110.0f, -60.0f, 0.0f, false, kFs48);
  REQUIRE(c48.begin(again.data(), static_cast<std::uint32_t>(again.size()), 9, 9, kFs));
  CHECK(c48.phase() == mc::Capture::Phase::Refs);
  c48.cancel();
}

TEST_CASE("mouthcap: the slot a capture started on") {
  CHECK(mc::slot_same(true, 88200u, 88200u));
  CHECK_FALSE(mc::slot_same(false, 88200u, 88200u));  // recording
  CHECK_FALSE(mc::slot_same(true, 44100u, 88200u));   // a new take or a load
  CHECK_FALSE(mc::slot_same(true, 0u, 88200u));       // cleared
}
