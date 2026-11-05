// SPDX-License-Identifier: GPL-3.0-or-later
// Carrier, Bands and the Vocoder unit, against synthetic voices only (no files): a 110 Hz saw through formant resonators.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>

#include <cfloat>
#include <cmath>
#include <cstdint>
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
  CHECK(Vocoder::kParamCount == 10);
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
