// SPDX-License-Identifier: GPL-3.0-or-later
// Carrier, Bands and the Vocoder unit, against synthetic voices only (no files): a 110 Hz saw through formant resonators.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>

#include <cfloat>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

namespace {

constexpr float kFs = 44100.0f;
constexpr float kPi2 = 6.2831853f;

// A voice: a saw at f0 through up to three two-pole resonators in parallel (a vowel); formants can change between calls.
class FormantVoice {
 public:
  struct Formant {
    float hz;
    float bw;
    float gain;
  };
  void set_vowel(const Formant* f, unsigned n) {
    n_ = n;
    for (unsigned k = 0; k < n; ++k) {
      const float r = std::exp(-3.1415927f * f[k].bw / kFs);
      c1_[k] = 2.0f * r * std::cos(kPi2 * f[k].hz / kFs);
      c2_[k] = -r * r;
      g_[k] = f[k].gain * (1.0f - r);
    }
  }
  void render(float* out, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      phase_ += 110.0f / kFs;
      phase_ -= phase_ >= 1.0f ? 1.0f : 0.0f;
      const float x = 2.0f * phase_ - 1.0f;
      float y = 0.0f;
      for (unsigned k = 0; k < n_; ++k) {
        const float s = g_[k] * x + c1_[k] * y1_[k] + c2_[k] * y2_[k];
        y2_[k] = y1_[k];
        y1_[k] = s;
        y += s;
      }
      out[i] = y;
    }
  }

 private:
  unsigned n_ = 0;
  float c1_[3] = {}, c2_[3] = {}, g_[3] = {}, y1_[3] = {}, y2_[3] = {};
  float phase_ = 0.0f;
};

const FormantVoice::Formant kVowelA[] = {{750.0f, 90.0f, 1.0f}, {1300.0f, 110.0f, 0.7f}, {2800.0f, 200.0f, 0.1f}};
const FormantVoice::Formant kVowelI[] = {{300.0f, 60.0f, 1.0f}, {2400.0f, 150.0f, 0.9f}, {3200.0f, 200.0f, 0.2f}};

unsigned band_near(const Bands& b, float hz) {
  unsigned best = 0;
  for (unsigned k = 1; k < Bands::kBands; ++k) {
    if (std::fabs(std::log(b.center_hz(k) / hz)) < std::fabs(std::log(b.center_hz(best) / hz))) {
      best = k;
    }
  }
  return best;
}

float peak(const std::vector<float>& v, std::size_t from = 0) {
  float p = 0.0f;
  for (std::size_t i = from; i < v.size(); ++i) {
    p = std::fmax(p, std::fabs(v[i]));
  }
  return p;
}

float rms(const std::vector<float>& v, std::size_t from, std::size_t to) {
  float s = 0.0f;
  for (std::size_t i = from; i < to; ++i) {
    s += v[i] * v[i];
  }
  return std::sqrt(s / static_cast<float>(to - from));
}

// Energy at one frequency (Goertzel), normalised by the length.
float tone_energy(const std::vector<float>& v, std::size_t from, std::size_t to, float hz) {
  const float w = kPi2 * hz / kFs;
  const float c = 2.0f * std::cos(w);
  float s1 = 0.0f, s2 = 0.0f;
  for (std::size_t i = from; i < to; ++i) {
    const float s = v[i] + c * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  const float n = static_cast<float>(to - from);
  return (s1 * s1 + s2 * s2 - c * s1 * s2) / (n * n);
}

// Drives a Vocoder block by block (64 frames) from a modulator signal; `events(block)` returns the events of that block.
struct Rig {
  Vocoder v;
  Rig() { v.prepare(kFs); }
  void set(Vocoder::Param p, float val, dsp::Event* store, unsigned& n) { store[n++] = dsp::param_change(p, val); }

  std::vector<float> run(const std::vector<float>& mod, const std::vector<std::vector<dsp::Event>>& ev = {}) {
    std::vector<float> out(mod.size());
    dsp::Transport tr;
    for (std::size_t at = 0, b = 0; at < mod.size(); at += 64, ++b) {
      float r[64];
      dsp::ProcessArgs a{};
      const std::size_t blk = b < ev.size() ? b : ev.size();
      a.events = blk < ev.size() ? ev[blk].data() : nullptr;
      a.event_count = blk < ev.size() ? static_cast<std::uint32_t>(ev[blk].size()) : 0u;
      a.in_l = mod.data() + at;
      a.in_r = mod.data() + at;
      a.out_l = out.data() + at;
      a.out_r = r;
      a.frames = static_cast<std::uint32_t>(mod.size() - at < 64 ? mod.size() - at : 64);
      a.transport = &tr;
      v.process(a);
    }
    return out;
  }
};

std::vector<std::vector<dsp::Event>> at_block0(std::vector<dsp::Event> e) { return {std::move(e)}; }

std::vector<float> speech(const FormantVoice::Formant* f, std::size_t n) {
  FormantVoice fv;
  fv.set_vowel(f, 3);
  std::vector<float> m(n);
  fv.render(m.data(), n);
  const float p = peak(m);
  for (float& x : m) {
    x *= 0.5f / p;
  }
  return m;
}

}  // namespace

TEST_CASE("vocoder: the parameter table is valid and ids are stable") {
  Vocoder v;
  const dsp::UnitInfo& info = v.info();
  CHECK(info.kind == dsp::UnitKind::Instrument);
  REQUIRE(info.param_count == Vocoder::kParamCount);
  for (unsigned p = 0; p < info.param_count; ++p) {
    INFO(info.params[p].name);
    CHECK(o3sdk::core::param_desc_valid(info.params[p]));
  }
  CHECK(Vocoder::Wave == 0);
  CHECK(Vocoder::Level == 5);
  CHECK(Vocoder::Detune == 6);
  CHECK(Vocoder::Mode == 7);  // Talk: MODE, QUALITY and FREEZE appended
  CHECK(Vocoder::Freeze == 9);  // the first ten never move
  CHECK(Vocoder::kParamCount == 62);  // V2f: FORMANT, THRU (40, 41); V5a: the loops appended (ids 10..21), V5b: their speed, direction and level (22..33), V5c: CARRIER (34), V6b: KEY, SCALE, SPEED, AMOUNT, NOTES-IN (35..39)
}

TEST_CASE("bands: a band responds more to a sine at its centre than at a neighbour's centre") {
  Bands bands;
  bands.prepare(kFs);
  const std::vector<unsigned> picks = {3, 8, 12};
  for (const unsigned b : picks) {
    float resp[2] = {};
    const float freqs[2] = {bands.center_hz(b), bands.center_hz(b + 1)};
    for (int k = 0; k < 2; ++k) {
      bands.reset();
      float ph = 0.0f;
      float mod[64], car[64] = {}, out[64];
      for (int blk = 0; blk < 200; ++blk) {
        for (float& x : mod) {
          x = 0.5f * std::sin(ph);
          ph += kPi2 * freqs[k] / kFs;
        }
        bands.process(mod, car, out, 64);
      }
      resp[k] = bands.envelope(b);
    }
    INFO("band " << b);
    CHECK(resp[0] > 1.8f * resp[1]);
  }
}

TEST_CASE("bands: no modulator and no carrier are both silent") {
  Rig rig;
  const std::size_t n = 44100;
  // a carrier with no modulator
  std::vector<float> zero(n, 0.0f);
  auto out = rig.run(zero, at_block0({dsp::note_on(48, 100), dsp::note_on(55, 100), dsp::note_on(60, 100)}));
  CHECK(peak(out) == 0.0f);
  // a modulator burst, then the followers release with the carrier still held: -80 dBFS (1e-4) once they are done
  Rig rig2;
  std::vector<float> burst = speech(kVowelA, n);
  for (std::size_t i = 4410; i < n; ++i) {
    burst[i] = 0.0f;
  }
  out = rig2.run(burst, at_block0({dsp::note_on(48, 100), dsp::note_on(55, 100)}));
  CHECK(peak(out) > 0.01f);
  CHECK(peak(out, 4410 + 22050) < 1.0e-4f);
  // a modulator with no carrier
  Rig rig3;
  out = rig3.run(speech(kVowelA, n));
  CHECK(peak(out) == 0.0f);
}

TEST_CASE("vocoder: with noise as the carrier the output follows the modulator's formants") {
  const std::size_t n = 44100;
  float e[2][3];  // [vowel][probe]: energy at 750 Hz, at 2400 Hz and at 300 Hz
  const FormantVoice::Formant* vowels[2] = {kVowelA, kVowelI};
  for (int v = 0; v < 2; ++v) {
    Rig rig;
    const auto out = rig.run(speech(vowels[v], n), at_block0({dsp::param_change(Vocoder::Noise, 1.0f), dsp::note_on(48, 100)}));
    e[v][0] = tone_energy(out, 8820, n, 750.0f) + tone_energy(out, 8820, n, 800.0f);
    e[v][1] = tone_energy(out, 8820, n, 2400.0f) + tone_energy(out, 8820, n, 2300.0f);
    e[v][2] = tone_energy(out, 8820, n, 330.0f) + tone_energy(out, 8820, n, 220.0f);
    CHECK(peak(out) > 0.05f);
  }
  CHECK(e[1][2] / e[1][0] > 4.0f * (e[0][2] / e[0][0]));  // the low vowel I: more at 300 Hz than at 750 Hz, unlike A
  const float ratio_a = e[0][0] / e[0][1];
  const float ratio_i = e[1][0] / e[1][1];
  CHECK(ratio_a > 4.0f * ratio_i);
  CHECK(ratio_a > 2.0f);

  // The bands themselves: the strongest follower sits in the band of the vowel's first or second formant.
  Bands b;
  b.prepare(kFs);
  const std::vector<float> m = speech(kVowelI, 20000);
  std::vector<float> car(20000);
  uint32_t s = 12345u;
  for (float& x : car) {
    s = s * 1664525u + 1013904223u;
    x = static_cast<float>(static_cast<std::int32_t>(s)) * 4.6566129e-10f;
  }
  float out[64];
  for (std::size_t at = 0; at + 64 <= m.size(); at += 64) {
    b.process(m.data() + at, car.data() + at, out, 64);
  }
  CHECK(b.envelope(band_near(b, 300.0f)) > 2.0f * b.envelope(band_near(b, 750.0f)));
  // The second formant: the 2.4 kHz band against the 1.3 kHz band is at least 3 times higher for the vowel I than for the vowel A (the
  // old absolute "twice" was written for the 100 Hz..8 kHz geometry; with Q 2.3 the 300 Hz formant's skirt reaches the 1.3 kHz band).
  auto hi_to_mid = [&](const FormantVoice::Formant* vowel) {
    Bands bb;
    bb.prepare(kFs);
    const std::vector<float> mm = speech(vowel, 20000);
    for (std::size_t at = 0; at + 64 <= mm.size(); at += 64) {
      bb.process(mm.data() + at, car.data() + at, out, 64);
    }
    return bb.envelope(band_near(bb, 2400.0f)) / bb.envelope(band_near(bb, 1300.0f));
  };
  CHECK(hi_to_mid(kVowelI) > 3.0f * hi_to_mid(kVowelA));
}

TEST_CASE("vocoder: the output never exceeds 1.0 at full-scale input") {
  Rig rig;
  const std::size_t n = 32768;
  std::vector<float> mod(n);
  uint32_t s = 99u;
  for (std::size_t i = 0; i < n; ++i) {
    s = s * 1664525u + 1013904223u;
    mod[i] = (i / 50) % 2 == 0 ? 1.0f : -1.0f;
    mod[i] = 0.5f * mod[i] + 0.5f * static_cast<float>(static_cast<std::int32_t>(s)) * 4.6566129e-10f;
  }
  std::vector<dsp::Event> ev = {dsp::param_change(Vocoder::Level, 6.0f), dsp::param_change(Vocoder::Dry, 1.0f)};
  for (std::uint8_t k = 0; k < 8; ++k) {
    ev.push_back(dsp::note_on(static_cast<std::uint8_t>(40 + 3 * k), 127));
  }
  const auto out = rig.run(mod, at_block0(ev));
  CHECK(peak(out) > 0.5f);
  CHECK(peak(out) <= 1.0f);
  for (const float x : out) {
    REQUIRE(std::isfinite(x));
  }
}

TEST_CASE("vocoder: NaN and Inf at the input do not stick in the state") {
  const std::size_t n = 64 * 400;
  std::vector<float> clean = speech(kVowelA, n);
  std::vector<float> dirty = clean;
  for (std::size_t i = 64 * 20; i < 64 * 22; ++i) {
    dirty[i] = (i % 3 == 0) ? std::nanf("") : ((i % 3 == 1) ? INFINITY : -INFINITY);
  }
  const auto ev = at_block0({dsp::note_on(48, 100), dsp::note_on(55, 100)});
  Rig a, b;
  const auto oa = a.run(clean, ev);
  const auto ob = b.run(dirty, ev);
  for (const float x : ob) {
    REQUIRE(std::isfinite(x));
  }
  CHECK(peak(ob) <= 1.0f);
  for (std::size_t i = 64 * 150; i < n; ++i) {  // long after the bad block: the same as a clean run
    REQUIRE(ob[i] == doctest::Approx(oa[i]).epsilon(0.01).scale(1.0));
  }
}

TEST_CASE("carrier: eight notes are held and a ninth steals the oldest") {
  Carrier c;
  c.prepare(kFs);
  for (std::uint8_t k = 0; k < 8; ++k) {
    c.note_on(static_cast<std::uint8_t>(60 + k));
  }
  CHECK(c.sounding_voices() == 8);
  c.note_on(80);
  CHECK(c.sounding_voices() == 8);
  CHECK_FALSE(c.holds(60));
  for (std::uint8_t k = 1; k < 8; ++k) {
    CHECK(c.holds(static_cast<std::uint8_t>(60 + k)));
  }
  CHECK(c.holds(80));
  c.note_on(81);  // now 61 is the oldest
  CHECK_FALSE(c.holds(61));
  CHECK(c.holds(62));
}

TEST_CASE("carrier: silent with no note, saw and pulse stay bounded, release ends the voices") {
  Carrier c;
  c.prepare(kFs);
  float buf[64];
  c.render(buf, 64);
  for (const float x : buf) {
    CHECK(x == 0.0f);
  }
  for (int w = 0; w < 2; ++w) {
    c.set_wave(w == 0 ? Carrier::Wave::Saw : Carrier::Wave::Pulse);
    for (std::uint8_t k = 0; k < 8; ++k) {
      c.note_on(static_cast<std::uint8_t>(36 + 7 * k));
    }
    float p = 0.0f;
    for (int b = 0; b < 100; ++b) {
      c.render(buf, 64);
      for (const float x : buf) {
        p = std::fmax(p, std::fabs(x));
      }
    }
    CHECK(p > 0.3f);
    CHECK(p < 2.1f);
    c.all_notes_off();
    for (int b = 0; b < 400; ++b) {
      c.render(buf, 64);
    }
    CHECK(c.sounding_voices() == 0);
    c.render(buf, 64);
    CHECK(buf[63] == 0.0f);
  }
}

TEST_CASE("vocoder: a note event lands on its frame") {
  Rig rig;
  const std::size_t n = 64 * 8;
  // a steady broadband modulator (noise), so the followers are settled before the note
  std::vector<float> mod(n);
  uint32_t s = 7u;
  for (float& x : mod) {
    s = s * 1664525u + 1013904223u;
    x = 0.5f * static_cast<float>(static_cast<std::int32_t>(s)) * 4.6566129e-10f;
  }
  auto onset = [&](std::uint16_t frame, unsigned block) {
    Rig r;
    std::vector<std::vector<dsp::Event>> ev(block + 1);
    ev[block] = {dsp::note_on(57, 100, 0, frame)};
    const auto out = r.run(mod, ev);
    std::size_t first = 0;
    while (first < out.size() && out[first] == 0.0f) {
      ++first;
    }
    CHECK(rms(out, first, n) > 1.0e-3f);
    return first;
  };
  const std::size_t at32 = onset(32, 4);
  const std::size_t at0 = onset(0, 4);
  CHECK(at32 - at0 == 32);
  CHECK(at0 >= 64 * 4);  // nothing before the block of the event
  CHECK(at0 <= 64 * 4 + 2);
}

TEST_CASE("vocoder: after a long silence no sample is a denormal") {
  Rig rig;
  std::vector<float> mod = speech(kVowelA, 44100);
  mod.resize(44100 * 3, 0.0f);
  const auto out = rig.run(mod, at_block0({dsp::note_on(48, 100)}));
  for (std::size_t i = 44100 * 2; i < out.size(); ++i) {
    const float a = std::fabs(out[i]);
    REQUIRE((a == 0.0f || a >= FLT_MIN));
  }
  for (unsigned b = 0; b < Bands::kBands; ++b) {
    const float e = rig.v.bands().envelope(b);
    REQUIRE((e == 0.0f || e >= FLT_MIN));
  }
}

namespace {

std::vector<float> noise_vec(std::size_t n, float amp) {
  std::vector<float> v(n);
  NoiseSource ns;
  for (float& x : v) {
    x = amp * ns.next();
  }
  return v;
}

// Peak-to-peak of the summed synthesis bands (all followers held at 1) over a tone sweep, in dB around the mean.
float sweep_ripple_db(float* worst_lo, float* worst_hi) {
  Bands b;
  b.prepare(kFs);
  b.hold_envelopes(1.0f);
  std::vector<float> db;
  for (float f = 200.0f; f <= 6000.0f; f *= 1.03f) {
    b.reset();
    b.hold_envelopes(1.0f);
    float ph = 0.0f, acc = 0.0f;
    unsigned cnt = 0;
    float mod[64] = {}, car[64], out[64];
    for (int blk = 0; blk < 120; ++blk) {
      for (float& x : car) {
        x = std::sin(ph);
        ph += kPi2 * f / kFs;
        ph -= ph > kPi2 ? kPi2 : 0.0f;
      }
      b.process(mod, car, out, 64);
      if (blk >= 60) {
        for (const float y : out) {
          acc += y * y;
          ++cnt;
        }
      }
    }
    db.push_back(20.0f * std::log10(std::sqrt(2.0f * acc / static_cast<float>(cnt))));
  }
  float mean = 0.0f;
  for (const float d : db) {
    mean += d;
  }
  mean /= static_cast<float>(db.size());
  float lo = 1e9f, hi = -1e9f;
  for (const float d : db) {
    lo = std::fmin(lo, d - mean);
    hi = std::fmax(hi, d - mean);
  }
  *worst_lo = lo;
  *worst_hi = hi;
  return hi - lo;
}

}  // namespace

TEST_CASE("bands: the summed synthesis bands are flat within 3 dB from 200 Hz to 6 kHz") {
  float lo = 0.0f, hi = 0.0f;
  const float pp = sweep_ripple_db(&lo, &hi);
  MESSAGE("ripple " << pp << " dB, " << lo << " .. " << hi);
  CHECK(lo > -3.0f);
  CHECK(hi < 3.0f);
}


namespace {

// Drives Bands alone block by block; the carrier is a quiet noise, the gate absent.
struct BandsRun {
  Bands b;
  std::vector<float> car;
  BandsRun() { b.prepare(kFs); }
  void feed(const std::vector<float>& mod, std::vector<float>* uv_trace = nullptr) {
    if (car.size() < mod.size()) {
      car = noise_vec(mod.size(), 0.2f);
    }
    float out[64];
    for (std::size_t at = 0; at + 64 <= mod.size(); at += 64) {
      b.process(mod.data() + at, car.data() + at, out, 64);
      if (uv_trace != nullptr) {
        uv_trace->push_back(b.unvoiced());
      }
    }
  }
};

}  // namespace

TEST_CASE("bands: a high-shaped noise burst opens the unvoiced path, a vowel does not, and it closes without a jump") {
  const std::size_t n = 44100 / 2;
  // a vowel, a burst of noise tilted high (twice differenced), then the vowel again
  std::vector<float> sig = speech(kVowelA, n * 3);
  std::vector<float> nz = noise_vec(n, 0.3f);
  float p1 = 0.0f, p2 = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    const float d1 = nz[i] - p1;
    const float d2 = d1 - p2;
    p1 = nz[i];
    p2 = d1;
    sig[n + i] = 0.4f * d2;
  }
  BandsRun r;
  std::vector<float> tr;
  r.feed(sig, &tr);
  const std::size_t bl = n / 64;  // blocks a third
  float vowel1 = 0.0f, burst = 1.0f, vowel2 = 0.0f, max_step = 0.0f;
  int flips = 0;
  for (std::size_t k = 1; k < tr.size(); ++k) {
    max_step = std::fmax(max_step, std::fabs(tr[k] - tr[k - 1]));
    flips += (tr[k] >= 0.5f) != (tr[k - 1] >= 0.5f) ? 1 : 0;
  }
  for (std::size_t k = 0; k < bl; ++k) {
    vowel1 = std::fmax(vowel1, tr[k]);
  }
  for (std::size_t k = bl + 40; k < 2 * bl; ++k) {
    burst = std::fmin(burst, tr[k]);
  }
  for (std::size_t k = 2 * bl + 40; k < 3 * bl; ++k) {
    vowel2 = std::fmax(vowel2, tr[k]);
  }
  CHECK(vowel1 == 0.0f);
  CHECK(burst == 1.0f);
  CHECK(vowel2 == 0.0f);
  CHECK(flips == 2);                          // opens once, closes once: the hysteresis does not chatter
  CHECK(max_step <= 64.0f * (1.0f / (0.005f * kFs)) + 1.0e-6f);  // the crossfade moves at most 5 ms^-1: no jump
}

TEST_CASE("bands: the unvoiced noise follows the carrier's gate") {
  const std::size_t n = 44100 / 4;
  std::vector<float> nz = noise_vec(n, 0.3f);
  std::vector<float> sig(n);
  float p1 = 0.0f, p2 = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    const float d1 = nz[i] - p1;
    sig[i] = 0.4f * (d1 - p2);
    p1 = nz[i];
    p2 = d1;
  }
  const std::vector<float> zero(n, 0.0f);
  float out[64], gate[64] = {};
  Bands b;
  b.prepare(kFs);
  float pk = 0.0f;
  for (std::size_t at = 0; at + 64 <= n; at += 64) {  // no carrier, gate 0: silent although the path is open
    b.process(sig.data() + at, zero.data() + at, out, 64, gate);
    for (const float y : out) {
      pk = std::fmax(pk, std::fabs(y));
    }
  }
  CHECK(b.unvoiced() == 1.0f);
  CHECK(pk == 0.0f);
}

TEST_CASE("vocoder: the AGC levels the modulator, and silence or a noise floor is not lifted") {
  const std::size_t n = 44100 * 2;
  float level[2];
  const float amps[2] = {0.5f, 0.05f};  // -6 dB and -26 dB peak
  for (int k = 0; k < 2; ++k) {
    Rig rig;
    auto m = speech(kVowelA, n);
    for (float& x : m) {
      x *= amps[k] / 0.5f;
    }
    const auto out = rig.run(m, at_block0({dsp::note_on(48, 100), dsp::note_on(55, 100), dsp::note_on(60, 100)}));
    level[k] = rms(out, 44100, n);
    CHECK(level[k] > 0.01f);
  }
  CHECK(std::fabs(20.0f * std::log10(level[0] / level[1])) < 3.0f);

  // a noise floor at -70 dB with a chord held, from the start: a floor lifted by the AGC's maximum gain (x20) would peak near 1; unlifted it is 0.07 (the floor times the wet gain)
  Rig rig;
  const auto floor_out = rig.run(noise_vec(n, 3.0e-4f), at_block0({dsp::note_on(48, 100), dsp::note_on(55, 100)}));
  CHECK(peak(floor_out) < 0.15f);
  // silence after a loud passage: silent
  Rig rig2;
  auto m = speech(kVowelA, n);
  for (std::size_t i = n / 2; i < n; ++i) {
    m[i] = 0.0f;
  }
  const auto tail = rig2.run(m, at_block0({dsp::note_on(48, 100)}));
  CHECK(peak(tail, n - 4410) < 1.0e-4f);
}

namespace {

// Power at one frequency over a Hann-windowed span: resolves tones 2.5 Hz apart in two seconds.
float hann_power(const std::vector<float>& v, std::size_t from, std::size_t to, float hz) {
  float re = 0.0f, im = 0.0f;
  const float len = static_cast<float>(to - from);
  for (std::size_t i = from; i < to; ++i) {
    const float t = static_cast<float>(i - from);
    const float w = 0.5f - 0.5f * std::cos(kPi2 * t / len);
    const float a = std::fmod(kPi2 * hz * t / kFs, kPi2);
    re += w * v[i] * std::cos(a);
    im += w * v[i] * std::sin(a);
  }
  return (re * re + im * im) / (len * len);
}

}  // namespace

TEST_CASE("carrier: the two oscillators of a voice beat at the detune; DETUNE 0 is one pitch") {
  const float f1 = 220.0f;  // note 57
  const float cents[2] = {20.0f, 0.0f};
  float p_f1[2], p_f2[2], p_mid[2];
  for (int k = 0; k < 2; ++k) {
    Carrier c;
    c.prepare(kFs);
    c.set_detune_cents(cents[k]);
    c.note_on(57);
    std::vector<float> buf(44100 * 2 + 4410);
    for (std::size_t at = 0; at + 64 <= buf.size(); at += 64) {
      c.render(buf.data() + at, 64);
    }
    const float f2 = f1 * std::exp2(20.0f / 1200.0f);  // 222.55 Hz: a beat of 2.55 Hz
    p_f1[k] = hann_power(buf, 4410, buf.size(), f1);
    p_f2[k] = hann_power(buf, 4410, buf.size(), f2);
    p_mid[k] = hann_power(buf, 4410, buf.size(), 0.5f * (f1 + f2));
  }
  // detuned: a second line at f1 + 2.55 Hz as strong as the first, and a dip between them
  CHECK(p_f2[0] > 0.5f * p_f1[0]);
  CHECK(p_f2[0] < 2.0f * p_f1[0]);
  CHECK(p_mid[0] < 0.5f * p_f1[0]);
  // DETUNE 0: one line, nothing 2.55 Hz above it
  CHECK(p_f2[1] < 0.02f * p_f1[1]);
}

TEST_CASE("bands: the AGC has no step at its gate") {
  // A level just under the gate and one just over it read about the same: a step there would flutter on a noise floor.
  const auto settle = [](float mean_abs) {
    Bands bands;
    bands.prepare(44100.0f);
    const float amp = mean_abs * 1.5707964f;  // a sine's mean-abs is 2/pi of its amplitude
    float mod[64];
    const float car[64] = {};
    float out[64];
    float phase = 0.0f;
    for (unsigned block = 0; block < 1400; ++block) {  // 2 s: the AGC's release has settled
      for (float& m : mod) {
        m = amp * std::sin(6.2831853f * phase);
        phase += 1000.0f / 44100.0f;
        phase -= phase >= 1.0f ? 1.0f : 0.0f;
      }
      bands.process(mod, car, out, 64);
    }
    float sum = 0.0f;
    for (unsigned b = 0; b < Bands::kBands; ++b) {
      sum += bands.envelope(b);
    }
    return sum;
  };
  const float under = settle(0.0019f);
  const float over = settle(0.0021f);
  CHECK(over > 0.0f);
  CHECK(over < 1.5f * under);
}

// ---- the half-rate low bands (survey item 1): bands 0..12 run at 22.05 kHz between halfband filters; the response must be today's.

namespace {

struct BandShape {
  float peak_hz;
  float width_oct;  // the -3 dB width of the follower's response, in octaves
};

// Each band's follower response to a sine, swept over 1/24-octave steps from 100 Hz to 16 kHz with the AGC at unity (a level of 0.1 mean-abs
// settles to gain 1); the peak is parabolic-interpolated in log frequency, the width is read at 0.7071 of the peak.
void measure_shapes(BandShape* shapes) {
  std::vector<float> freq;
  for (float f = 100.0f; f < 16000.0f; f *= 1.0293022f) {  // 2^(1/24)
    freq.push_back(f);
  }
  std::vector<std::vector<float>> resp(Bands::kBands, std::vector<float>(freq.size()));
  Bands b;
  b.prepare(kFs);
  for (std::size_t k = 0; k < freq.size(); ++k) {
    b.reset();
    float mod[64], out[64];
    const float car[64] = {};
    float ph = 0.0f;
    float acc[Bands::kBands] = {};
    for (int blk = 0; blk < 450; ++blk) {
      for (float& x : mod) {
        x = 0.157f * std::sin(ph);
        ph += kPi2 * freq[k] / kFs;
        ph -= ph > kPi2 ? kPi2 : 0.0f;
      }
      b.process(mod, car, out, 64);
      if (blk >= 400) {
        for (unsigned i = 0; i < Bands::kBands; ++i) {
          acc[i] += b.envelope(i) / 50.0f;
        }
      }
    }
    for (unsigned i = 0; i < Bands::kBands; ++i) {
      resp[i][k] = acc[i];
    }
  }
  for (unsigned i = 0; i < Bands::kBands; ++i) {
    const std::vector<float>& r = resp[i];
    std::size_t m = 0;
    for (std::size_t k = 1; k < r.size(); ++k) {
      m = r[k] > r[m] ? k : m;
    }
    const float step = std::log2(1.0293022f);
    const float l = std::log(r[m - 1]), c = std::log(r[m]), h = std::log(r[m + 1]);
    const float off = 0.5f * (l - h) / (l - 2.0f * c + h);
    const float peak_oct = std::log2(freq[m]) + off * step;
    const float level = r[m] * 0.70710678f;
    std::size_t lo = m, hi = m;
    while (lo > 0 && r[lo] > level) --lo;
    while (hi + 1 < r.size() && r[hi] > level) ++hi;
    const float f_lo = std::log2(freq[lo]) + step * (level - r[lo]) / (r[lo + 1] - r[lo]);
    const float f_hi = std::log2(freq[hi - 1]) + step * (r[hi - 1] - level) / (r[hi - 1] - r[hi]);
    shapes[i].peak_hz = std::exp2(peak_oct);
    shapes[i].width_oct = f_hi - f_lo;
  }
}

// Today's (full-rate) shapes, measured by this same test on the code before the half-rate bands (e3f675c).
const BandShape kShapeFullRate[Bands::kBands] = {
    {125.027f, 0.403625f}, {164.919f, 0.404102f}, {217.671f, 0.402546f}, {287.335f, 0.405329f},
    {379.166f, 0.402906f}, {500.446f, 0.40341f}, {660.915f, 0.404684f}, {872.801f, 0.402826f},
    {1153.44f, 0.404266f}, {1525.13f, 0.404154f}, {2018.14f, 0.401229f}, {2670.76f, 0.39884f},
    {3531.63f, 0.393373f}, {4665.87f, 0.378167f}, {6152.67f, 0.357297f}, {8096.7f, 0.319692f},
};
constexpr float kRippleFullRateDb = 1.39393f;  // sweep_ripple_db() on e3f675c

}  // namespace

TEST_CASE("bands: every band keeps its peak frequency and -3 dB width") {
  BandShape s[Bands::kBands];
  measure_shapes(s);
  for (unsigned i = 0; i < Bands::kBands; ++i) {
    MESSAGE("band " << i << " peak " << s[i].peak_hz << " Hz width " << s[i].width_oct << " oct");
  }
  for (unsigned i = 0; i < Bands::kBands; ++i) {
    INFO("band " << i);
    CHECK(std::fabs(std::log2(s[i].peak_hz / kShapeFullRate[i].peak_hz)) < 0.003f);  // 0.003 octave, 5 cents (seen: 0.0003)
    CHECK(std::fabs(s[i].width_oct - kShapeFullRate[i].width_oct) < 0.01f);  // seen: 0.0042 at band 12
  }
}

TEST_CASE("bands: the summed response is no more than 1 dB less flat than at full rate") {
  float lo = 0.0f, hi = 0.0f;
  const float pp = sweep_ripple_db(&lo, &hi);
  MESSAGE("ripple " << pp << " dB");
  CHECK(pp < kRippleFullRateDb + 1.0f);
}

TEST_CASE("bands: a carrier tone above 11 kHz that would alias onto a low band does not sound there") {
  // The half-rate bands see f_in and 22.05 kHz - f_in as the same frequency: a tone at 22.05 kHz - c would fold onto the centre c of a band
  // if the halfband let it through. The product is measured at c (Goertzel), where the full-rate bands' own leakage of the tone does not
  // fall (it sits at the tone's frequency); the reference is the tone at c itself, same amplitude, measured at c.
  const auto level = [](float hz, float at_hz) {
    Bands b;
    b.prepare(kFs);
    b.hold_envelopes(1.0f);
    float mod[64] = {}, car[64], out[64];
    float ph = 0.0f;
    std::vector<float> y;
    for (int blk = 0; blk < 150; ++blk) {
      for (float& x : car) {
        x = std::sin(ph);
        ph += kPi2 * hz / kFs;
        ph -= ph > kPi2 ? kPi2 : 0.0f;
      }
      b.process(mod, car, out, 64);
      if (blk >= 80) {
        y.insert(y.end(), out, out + 64);
      }
    }
    return tone_energy(y, 0, y.size(), at_hz);
  };
  float worst = -1000.0f;
  for (unsigned i = 0; i < 13; ++i) {
    const float c = 125.0f * std::exp2(0.4f * static_cast<float>(i));
    const float in_band = level(c, c);
    const float alias = level(22050.0f - c, c);
    const float db = 10.0f * std::log10(alias / in_band);
    MESSAGE("band " << i << " centre " << c << " Hz: tone at " << 22050.0f - c << " Hz is " << db << " dB against the tone in band");
    worst = std::fmax(worst, db);
  }
  MESSAGE("worst " << worst << " dB");
  CHECK(worst < -50.0f);
}

namespace {

const float kVoiceEnvFullRate[Bands::kBands] = {
    0.0173853f, 0.0077841f, 0.0148367f, 0.0107291f, 0.0125046f, 0.0172781f, 0.025903f, 0.0215385f,
    0.0101047f, 0.00775739f, 0.00299845f, 0.00142848f, 0.000851399f, 0.000451527f, 0.00024683f, 0.000138929f};
// vowel A, mean of the last 300 blocks. Re-derived at the follower rates of the realism round (1 ms attack, 15 ms release mid-range; bands.cpp set_envelope) in a one-off run with
// kHalfBands = 0, so every band runs at the full rate; the tolerance (1 dB) is unchanged.

void voice_levels(float* env) {
  Bands b;
  b.prepare(kFs);
  const std::vector<float> m = speech(kVowelA, 64 * 600);
  const std::vector<float> car = noise_vec(m.size(), 0.2f);
  float out[64];
  for (unsigned i = 0; i < Bands::kBands; ++i) {
    env[i] = 0.0f;
  }
  for (std::size_t at = 0, blk = 0; at + 64 <= m.size(); at += 64, ++blk) {
    b.process(m.data() + at, car.data() + at, out, 64);
    if (blk >= 300) {
      for (unsigned i = 0; i < Bands::kBands; ++i) {
        env[i] += b.envelope(i) / 300.0f;
      }
    }
  }
}

}  // namespace

TEST_CASE("bands: the followers of a synthetic voice read within 1 dB of the full-rate bands") {
  float env[Bands::kBands];
  voice_levels(env);
  float top = 0.0f;
  for (unsigned i = 0; i < Bands::kBands; ++i) {
    top = std::fmax(top, kVoiceEnvFullRate[i]);
    MESSAGE("band " << i << " env " << env[i]);
  }
  for (unsigned i = 0; i < Bands::kBands; ++i) {
    if (kVoiceEnvFullRate[i] > top * 0.01f) {  // within 40 dB of the strongest; under that the band only holds a skirt
      INFO("band " << i);
      CHECK(std::fabs(20.0f * std::log10(env[i] / kVoiceEnvFullRate[i])) < 1.0f);
    }
  }
}

TEST_CASE("bands: any frame count from 1 to 64 gives the output and followers of blocks of 64") {
  // A level under the AGC gate (gain 1 whatever the block size) and the unvoiced path shut: the result must not depend on the partition.
  const std::size_t total = 64 * 40;
  std::vector<float> mod(total);
  for (std::size_t i = 0; i < total; ++i) {
    mod[i] = 0.001f * std::sin(0.05f * static_cast<float>(i)) + 0.0005f * std::sin(0.9f * static_cast<float>(i));
  }
  const std::vector<float> car = noise_vec(total, 0.3f);
  const auto run = [&](const std::vector<std::uint32_t>& pattern, std::vector<float>& out, float* env) {
    Bands b;
    b.prepare(kFs);
    float lead[64] = {}, lead_out[64];
    for (int blk = 0; blk < 2000; ++blk) {  // the AGC level starts at its target; 2000 silent blocks (500 ms release) bring it under the gate
      b.process(lead, lead, lead_out, 64);
    }
    out.assign(total, 0.0f);
    std::size_t at = 0;
    for (std::size_t k = 0; at < total; ++k) {
      std::uint32_t n = pattern[k % pattern.size()];
      n = n > total - at ? static_cast<std::uint32_t>(total - at) : n;
      b.process(mod.data() + at, car.data() + at, out.data() + at, n);
      at += n;
    }
    for (unsigned i = 0; i < Bands::kBands; ++i) {
      env[i] = b.envelope(i);
    }
  };
  std::vector<float> ref, odd;
  float env_ref[Bands::kBands], env_odd[Bands::kBands];
  run({64}, ref, env_ref);
  CHECK(peak(ref) > 1.0e-5f);
  for (const auto& pattern : {std::vector<std::uint32_t>{1}, {3, 7, 64, 5, 33, 2, 1, 63}, {13}, {2}}) {
    run(pattern, odd, env_odd);
    bool same = true;
    for (std::size_t i = 0; i < total; ++i) {
      same = same && odd[i] == ref[i];
    }
    CHECK(same);
    for (unsigned i = 0; i < Bands::kBands; ++i) {
      CHECK(env_odd[i] == env_ref[i]);
    }
  }
}

// ---- V2f: graded follower rates, FORMANT, THRU ----

namespace {

// One resonance at 1 kHz (the other two formants silent): the follower row has one hump, so a shift moves one peak.
const FormantVoice::Formant kVowelOne[] = {{1000.0f, 120.0f, 1.0f}, {1000.0f, 120.0f, 0.0f}, {1000.0f, 120.0f, 0.0f}};

// Runs `mod` and the carrier through a Bands at `formant` semitones and THRU `thru`, in blocks of `blk` frames.
std::vector<float> bands_run(const std::vector<float>& mod, const std::vector<float>& car, float formant, float thru = 0.0f, std::uint32_t blk = 64,
                              unsigned lead_blocks = 0) {
  Bands b;
  b.prepare(kFs);
  float lead[64] = {}, lead_out[64];
  for (unsigned k = 0; k < lead_blocks; ++k) {  // silent blocks first: the AGC level settles under its gate
    b.process(lead, lead, lead_out, 64);
  }
  b.set_formant(formant);
  b.set_thru(thru);
  std::vector<float> out(mod.size());
  for (std::size_t at = 0; at < mod.size();) {
    const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::size_t>(blk, mod.size() - at));
    b.process(mod.data() + at, car.data() + at, out.data() + at, n);
    at += n;
  }
  return out;
}

// The frequency of the strongest of 48 log-spaced probes from 200 Hz to 7 kHz, each the mean of Goertzel energies over 32 segments of 1024 samples.
float peak_hz(const std::vector<float>& v, std::size_t from) {
  float best = 0.0f, best_hz = 0.0f;
  for (int k = 0; k < 48; ++k) {
    const float hz = 200.0f * std::pow(35.0f, static_cast<float>(k) / 47.0f);
    float e = 0.0f;
    for (int s = 0; s < 32; ++s) {
      const std::size_t a = from + static_cast<std::size_t>(s) * 1024;
      e += tone_energy(v, a, a + 1024, hz);
    }
    if (e > best) {
      best = e;
      best_hz = hz;
    }
  }
  return best_hz;
}

}  // namespace

TEST_CASE("bands: the follower times are a smooth gradient, slower at the bottom and faster at the top") {
  Bands b;
  b.prepare(kFs);
  // default 15 ms release, 25 ms low release: neighbours differ by 2^(log2(25/15)/7.5) = 1.070; the bound is 1.10
  for (unsigned i = 0; i + 1 < Bands::kBands; ++i) {
    INFO("band " << i);
    CHECK(b.release_ms(i) > b.release_ms(i + 1));
    CHECK(b.attack_ms(i) > b.attack_ms(i + 1));
    CHECK(b.release_ms(i) / b.release_ms(i + 1) < 1.10f);
    CHECK(b.attack_ms(i) / b.attack_ms(i + 1) < 1.10f);
  }
  CHECK(b.release_ms(0) == doctest::Approx(25.0f).epsilon(0.02));
  CHECK(b.release_ms(15) == doctest::Approx(9.0f).epsilon(0.03));
  // the middle of the range is 15 ms and 1 ms: the two middle bands straddle them
  CHECK(b.release_ms(7) > 15.0f);
  CHECK(b.release_ms(8) < 15.0f);
  CHECK(std::sqrt(b.release_ms(7) * b.release_ms(8)) == doctest::Approx(15.0f).epsilon(0.02));
  CHECK(std::sqrt(b.attack_ms(7) * b.attack_ms(8)) == doctest::Approx(1.0f).epsilon(0.02));
}

TEST_CASE("bands: FORMANT 0 stays on the merged path; two passes only away from 0 and for one block after it") {
  Bands b;
  b.prepare(kFs);
  CHECK_FALSE(b.shifted());
  b.set_formant(0.0f);
  CHECK_FALSE(b.shifted());
  float mod[64] = {}, car[64] = {}, out[64];
  b.set_formant(5.0f);
  CHECK(b.shifted());
  b.process(mod, car, out, 64);
  b.set_formant(0.0f);
  CHECK(b.shifted());  // the gains come back to the followers in one more two-pass block
  b.process(mod, car, out, 64);
  CHECK_FALSE(b.shifted());
}

TEST_CASE("bands: FORMANT 0 set explicitly is bit-exact with FORMANT never touched") {
  const std::vector<float> mod = speech(kVowelA, 64 * 120);
  const std::vector<float> car = noise_vec(mod.size(), 0.2f);
  Bands b;
  b.prepare(kFs);
  std::vector<float> ref(mod.size());
  for (std::size_t at = 0; at < mod.size(); at += 64) {
    b.process(mod.data() + at, car.data() + at, ref.data() + at, 64);
  }
  const std::vector<float> z = bands_run(mod, car, 0.0f);
  bool same = true;
  for (std::size_t i = 0; i < mod.size(); ++i) {
    same = same && z[i] == ref[i];
  }
  CHECK(same);
}

TEST_CASE("bands: FORMANT +12 and -12 move the spectral peak by about an octave") {
  const std::size_t total = 64 * 400 + 32 * 1024;
  const std::vector<float> mod = speech(kVowelOne, total);
  const std::vector<float> car = noise_vec(total, 0.2f);
  const float p0 = peak_hz(bands_run(mod, car, 0.0f), 64 * 400);
  const float pu = peak_hz(bands_run(mod, car, 12.0f), 64 * 400);
  const float pd = peak_hz(bands_run(mod, car, -12.0f), 64 * 400);
  MESSAGE("peak at 0: " << p0 << " Hz, +12: " << pu << " Hz, -12: " << pd << " Hz");
  CHECK(p0 > 800.0f);
  CHECK(p0 < 1250.0f);
  // an octave, tolerance a ratio of 1.7 to 2.4 (a band is 0.4 octave wide, the follower row is interpolated between bands)
  CHECK(pu / p0 > 1.7f);
  CHECK(pu / p0 < 2.4f);
  CHECK(p0 / pd > 1.7f);
  CHECK(p0 / pd < 2.4f);
}

TEST_CASE("bands: moving FORMANT between 0 and other values, and sweeping it, makes no jump") {
  // A steady 1 kHz sine carrier: a gain step shows as a jump in the second difference, which a sine keeps small (amplitude * w^2).
  const std::size_t total = 64 * 260;
  const std::vector<float> mod = speech(kVowelA, total);
  std::vector<float> car(total);
  for (std::size_t i = 0; i < total; ++i) {
    car[i] = 0.3f * std::sin(kPi2 * 1000.0f * static_cast<float>(i) / kFs);
  }
  const auto worst = [&](int mode) {
    Bands b;
    b.prepare(kFs);
    std::vector<float> out(total);
    for (std::size_t blk = 0; blk < total / 64; ++blk) {
      if (mode == 1 && blk >= 100) {  // 0 <-> 7 semitones every 3 blocks
        b.set_formant(((blk / 3) % 2) != 0 ? 7.0f : 0.0f);
      }
      if (mode == 2 && blk >= 100) {  // a sweep from -12 to +12, one semitone every 5 blocks, then back
        const long k = static_cast<long>((blk - 100) / 5) % 48;
        b.set_formant(static_cast<float>(k < 24 ? k - 12 : 36 - k));
      }
      b.process(mod.data() + blk * 64, car.data() + blk * 64, out.data() + blk * 64, 64);
    }
    float w = 0.0f;
    for (std::size_t i = 102 * 64; i + 1 < total; ++i) {
      w = std::fmax(w, std::fabs(out[i + 1] - 2.0f * out[i] + out[i - 1]));
    }
    return w;
  };
  const float steady = worst(0);
  const float toggle = worst(1);
  const float sweep = worst(2);
  MESSAGE("worst second difference: steady " << steady << ", toggle " << toggle << ", sweep " << sweep);
  CHECK(steady > 0.0f);
  // the bound: 2x the steady case
  CHECK(toggle < 2.0f * steady);
  CHECK(sweep < 2.0f * steady);
}

TEST_CASE("bands: FORMANT away from 0 gives blocks of any size the output of blocks of 64 within -50 dB") {
  // The gains ramp across each block from the last block's follower, so a block size moves the gain's timing by up to one block: a steady
  // modulator under the AGC gate (as in the chunking test above) isolates the maths from that lag (a speech-like one reads about -32 dB; this one -55 to -57 dB: the follower ripple times the lag),
  // and the first 250 blocks, where the followers still rise, are left out of the comparison.
  const std::size_t total = 64 * 400;
  std::vector<float> mod(total);
  for (std::size_t i = 0; i < total; ++i) {
    mod[i] = 0.001f * std::sin(0.3f * static_cast<float>(i)) + 0.0005f * std::sin(0.9f * static_cast<float>(i));
  }
  const std::vector<float> car = noise_vec(total, 0.2f);
  const std::vector<float> ref = bands_run(mod, car, 6.0f, 0.0f, 64, 2000);
  float sig = 0.0f;
  for (std::size_t i = 64 * 250; i < total; ++i) {
    sig += ref[i] * ref[i];
  }
  CHECK(sig > 0.0f);
  for (const std::uint32_t blk : {1u, 7u, 13u, 33u}) {
    const std::vector<float> o = bands_run(mod, car, 6.0f, 0.0f, blk, 2000);
    float err = 0.0f;
    for (std::size_t i = 64 * 250; i < total; ++i) {
      err += (o[i] - ref[i]) * (o[i] - ref[i]);
    }
    const float db = 10.0f * std::log10(err / sig + 1.0e-30f);
    MESSAGE("block " << blk << ": error " << db << " dB");
    CHECK(db < -50.0f);
  }
}

TEST_CASE("bands: THRU 0 is bit-exact, THRU 100 adds the modulator only while the high followers are open") {
  const std::size_t total = 64 * 200;
  const std::vector<float> zero(total, 0.0f);
  const std::vector<float> noise = noise_vec(total, 0.3f);
  const std::vector<float> vowel = speech(kVowelA, total);
  const std::vector<float> car = noise_vec(total, 0.2f);
  // THRU 0 (also set to 100 and back before the first block): identical to the code being absent, which is the default
  Bands b;
  b.prepare(kFs);
  std::vector<float> ref(total);
  for (std::size_t at = 0; at < total; at += 64) {
    b.process(vowel.data() + at, car.data() + at, ref.data() + at, 64);
  }
  Bands c;
  c.prepare(kFs);
  c.set_thru(1.0f);
  c.set_thru(0.0f);
  std::vector<float> a(total);
  for (std::size_t at = 0; at < total; at += 64) {
    c.process(vowel.data() + at, car.data() + at, a.data() + at, 64);
  }
  bool same = true;
  for (std::size_t i = 0; i < total; ++i) {
    same = same && a[i] == ref[i];
  }
  CHECK(same);
  // THRU only adds to the output (it touches no state), so the difference of two runs is exactly its contribution; above 6 kHz it is the
  // modulator's, scaled by the followers of bands 13..15
  const auto contribution = [&](const std::vector<float>& mod) {
    const std::vector<float> o0 = bands_run(mod, car, 0.0f, 0.0f);
    const std::vector<float> o1 = bands_run(mod, car, 0.0f, 1.0f);
    std::vector<float> d(total);
    for (std::size_t i = 0; i < total; ++i) {
      d[i] = o1[i] - o0[i];
    }
    float e = 0.0f;
    for (const float hz : {6500.0f, 7500.0f, 8500.0f, 9500.0f}) {
      e += tone_energy(d, 64 * 100, total, hz);
    }
    MESSAGE("THRU contribution rms " << rms(d, 64 * 100, total) << " against the output rms " << rms(o0, 64 * 100, total));
    return e;
  };
  const float noisy = contribution(noise);
  const float voiced = contribution(vowel);
  MESSAGE("above 6 kHz: THRU 100 on noise " << noisy << ", on a vowel " << voiced);
  CHECK(noisy > 1.0e-12f);
  CHECK(voiced < noisy * 1.0e-3f);  // 30 dB under: the followers of the top bands are closed on a vowel
}

TEST_CASE("vocoder: FORMANT and THRU are parameters 40 and 41, appended after NOTES-IN") {
  CHECK(Vocoder::NotesIn == 39);
  CHECK(Vocoder::Formant == 40);
  CHECK(Vocoder::Thru == 41);
  Vocoder v;
  const dsp::UnitInfo& info = v.info();
  REQUIRE(info.param_count > 41);
  CHECK(std::string(info.params[40].name) == "FORMANT");
  CHECK(info.params[40].min == -12.0f);
  CHECK(info.params[40].max == 12.0f);
  CHECK(info.params[40].def == 0.0f);
  CHECK(std::string(info.params[41].name) == "THRU");
  CHECK(info.params[41].min == 0.0f);
  CHECK(info.params[41].max == 1.0f);
  CHECK(info.params[41].def == 0.0f);
}

// The unit end to end at FORMANT -12/0/+12 and THRU 0/100 on a vowel with an "s" every half second. Always checked (finite, and the settings
// change the sound); with O3_VOC03ER_WAV_DIR set, the five runs are written there as 16-bit mono WAVs for listening (the scenarios cannot set a
// parameter that has no screen row yet, so this is the V2f listening render: formant_m12.wav, formant_0.wav, formant_p12.wav, thru_0.wav, thru_100.wav).
namespace {

void write_wav16(const std::string& path, const std::vector<float>& v) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) {
    return;
  }
  const std::uint32_t bytes = static_cast<std::uint32_t>(v.size() * 2);
  const auto u32 = [&](std::uint32_t x) {
    const unsigned char b[4] = {static_cast<unsigned char>(x), static_cast<unsigned char>(x >> 8), static_cast<unsigned char>(x >> 16), static_cast<unsigned char>(x >> 24)};
    std::fwrite(b, 1, 4, f);
  };
  const auto u16 = [&](std::uint16_t x) {
    const unsigned char b[2] = {static_cast<unsigned char>(x), static_cast<unsigned char>(x >> 8)};
    std::fwrite(b, 1, 2, f);
  };
  std::fwrite("RIFF", 1, 4, f);
  u32(36 + bytes);
  std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16);
  u16(1);
  u16(1);
  u32(44100);
  u32(88200);
  u16(2);
  u16(16);
  std::fwrite("data", 1, 4, f);
  u32(bytes);
  for (const float x : v) {
    const float c = std::fmax(-1.0f, std::fmin(1.0f, x));
    u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(c * 32767.0f)));
  }
  std::fclose(f);
}

}  // namespace

TEST_CASE("vocoder: FORMANT and THRU through the unit change the sound, and the five renders are written on request") {
  const std::size_t total = 64 * 1380;  // 2 s
  std::vector<float> mod = speech(kVowelA, total);
  const std::vector<float> hiss = noise_vec(total, 0.35f);
  for (std::size_t i = 0; i < total; ++i) {
    // an "s" every half second, 120 ms long: noise through a one-pole high-pass-ish difference (bright)
    if ((i % 22050) < 5292 && i > 0) {
      mod[i] += hiss[i] - hiss[i - 1];
    }
  }
  const auto render = [&](float formant, float thru) {
    Rig rig;
    return rig.run(mod, at_block0({dsp::param_change(Vocoder::Formant, formant), dsp::param_change(Vocoder::Thru, thru), dsp::note_on(48, 100),
                                   dsp::note_on(55, 100), dsp::note_on(60, 100)}));
  };
  const std::vector<float> f_m12 = render(-12.0f, 0.0f);
  const std::vector<float> f_0 = render(0.0f, 0.0f);
  const std::vector<float> f_p12 = render(12.0f, 0.0f);
  const std::vector<float> t_100 = render(0.0f, 1.0f);
  for (const auto* v : {&f_m12, &f_0, &f_p12, &t_100}) {
    bool finite = true;
    for (const float x : *v) {
      finite = finite && std::isfinite(x);
    }
    CHECK(finite);
    CHECK(peak(*v) < 1.0f);
  }
  const auto diff_rms = [&](const std::vector<float>& a, const std::vector<float>& b) {
    std::vector<float> d(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
      d[i] = a[i] - b[i];
    }
    return rms(d, 64 * 200, d.size());
  };
  MESSAGE("rms " << rms(f_0, 64 * 200, total) << "; difference to FORMANT 0: -12 " << diff_rms(f_m12, f_0) << ", +12 " << diff_rms(f_p12, f_0)
                 << ", THRU 100 " << diff_rms(t_100, f_0));
  CHECK(diff_rms(f_m12, f_0) > 0.1f * rms(f_0, 64 * 200, total));
  CHECK(diff_rms(f_p12, f_0) > 0.1f * rms(f_0, 64 * 200, total));
  CHECK(diff_rms(t_100, f_0) > 0.0f);
  const char* dir = std::getenv("O3_VOC03ER_WAV_DIR");
  if (dir != nullptr) {
    const std::string d = dir;
    write_wav16(d + "/formant_m12.wav", f_m12);
    write_wav16(d + "/formant_0.wav", f_0);
    write_wav16(d + "/formant_p12.wav", f_p12);
    write_wav16(d + "/thru_0.wav", f_0);
    write_wav16(d + "/thru_100.wav", t_100);
  }
}
