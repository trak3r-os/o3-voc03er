// SPDX-License-Identifier: GPL-3.0-or-later
// The sample-rate converter of SAMPLE LOAD (app/src/wav_convert.h), measured on the host: frequency and level of a tone through 48 to 44.1 kHz (the pass band is flat), the
// rejection of what lies above the new Nyquist (aliasing, going down) and of the images (going up), the weight sum that keeps an edge from drooping, the exact output length, the
// integer conversions' rounding and the cost per output. The measuring maths is double (a host test); the converter itself is float and integer.

#include <doctest/doctest.h>

#include "../app/src/wav_convert.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace wav = o3_voc03er::wav;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kAmp = 20000.0;

wav::Resampler& rs() {
  static wav::Resampler r;
  return r;
}

struct Fit {
  double amp;    // the amplitude of the tone at the fitted frequency, in the input's units
  double resid;  // the RMS of what is left after taking that tone away
};

// Least-squares sine and cosine at `fq` (Hz) over the middle of `o` (`skip` samples are left out at each end).
Fit fit(const std::vector<std::int16_t>& o, double fq, double rate, std::size_t skip) {
  double sc = 0, ss = 0;
  std::size_t n = 0;
  for (std::size_t i = skip; i + skip < o.size(); ++i) {
    const double ph = 2.0 * kPi * fq * static_cast<double>(i) / rate;
    sc += o[i] * std::cos(ph);
    ss += o[i] * std::sin(ph);
    ++n;
  }
  const double a = 2.0 * sc / static_cast<double>(n), b = 2.0 * ss / static_cast<double>(n);
  double e = 0;
  for (std::size_t i = skip; i + skip < o.size(); ++i) {
    const double ph = 2.0 * kPi * fq * static_cast<double>(i) / rate;
    const double d = o[i] - (a * std::cos(ph) + b * std::sin(ph));
    e += d * d;
  }
  return {std::sqrt(a * a + b * b), std::sqrt(e / static_cast<double>(n))};
}

// One second of a tone of `f` Hz at `fin`, converted to `fout`; returns the output.
std::vector<std::int16_t> convert_tone(double f, std::uint32_t fin, std::uint32_t fout, double amp = kAmp) {
  std::vector<std::int16_t> out(200000);
  REQUIRE(rs().begin(fin, fout, out.data(), static_cast<std::uint32_t>(out.size())));
  for (std::uint32_t i = 0; i < fin; ++i) {
    rs().push(static_cast<float>(amp * std::sin(2.0 * kPi * f * i / fin)));
  }
  rs().finish();
  out.resize(rs().out());
  return out;
}

double db(double ratio) { return 20.0 * std::log10(ratio + 1e-12); }

// The tone's level in dB (re its input amplitude) at its own (aliased) frequency, and the residual in dB re the amplitude.
double tone_db(double f, std::uint32_t fin, std::uint32_t fout, double* resid_db = nullptr) {
  const auto o = convert_tone(f, fin, fout);
  double fa = std::fmod(f, static_cast<double>(fout));
  if (fa > fout / 2.0) {
    fa = fout - fa;
  }
  const Fit r = fit(o, fa, static_cast<double>(fout), 3000);
  if (resid_db != nullptr) {
    *resid_db = db(r.resid / kAmp);
  }
  return db(r.amp / kAmp);
}

}  // namespace

TEST_CASE("wav_convert: 1 kHz and 15 kHz through 48 to 44.1 kHz keep their frequency and their level") {
  // The level is within 0.02 dB (measured: 0.002 dB) and what is not the tone is under -70 dB (measured: -82 dB and below): a wrong frequency would leave a residual of the
  // tone's own size.
  for (double f : {1000.0, 15000.0}) {
    double resid = 0;
    const double level = tone_db(f, 48000, 44100, &resid);
    INFO("tone " << f << " Hz: " << level << " dB, residual " << resid << " dB");
    CHECK(std::abs(level) < 0.02);
    CHECK(resid < -70.0);
  }
}

TEST_CASE("wav_convert: the pass band is flat from 1 kHz to 15 kHz for the rates a file may have") {
  for (std::uint32_t fin : {48000u, 96000u, 88200u, 32000u, 22050u}) {
    for (double f = 1000.0; f <= 15000.0; f += 1400.0) {
      if (fin < 44100u && f >= fin * 0.33) {
        break;  // a lower rate's own pass band is what its Nyquist leaves
      }
      double resid = 0;
      const double level = tone_db(f, fin, 44100, &resid);
      INFO(fin << " Hz in, tone " << f << " Hz: " << level << " dB, residual " << resid << " dB");
      CHECK(std::abs(level) < 0.03);
      CHECK(resid < -65.0);
    }
  }
}

TEST_CASE("wav_convert: what lies above the new Nyquist is rejected, going down: no alias") {
  // A tone above 22.05 kHz would fold into the pass band. 23 kHz at 48 kHz folds to 21.1 kHz (a rate of 48 kHz has little room: -60 dB measured at 23 kHz and beyond);
  // from 96 kHz the room is wide and every tone above the Nyquist is down by 70 dB or more.
  CHECK(tone_db(23000.0, 48000, 44100) < -60.0);
  CHECK(tone_db(23500.0, 48000, 44100) < -60.0);
  for (double f : {23000.0, 26000.0, 30000.0, 40000.0, 47000.0}) {
    const double level = tone_db(f, 96000, 44100);
    INFO("96 kHz in, tone " << f << " Hz folds to " << std::fabs(f - 44100.0) << " Hz: " << level << " dB");
    CHECK(level < -70.0);
  }
  for (double f : {23000.0, 30000.0, 43000.0}) {
    CHECK(tone_db(f, 88200, 44100) < -70.0);
  }
}

TEST_CASE("wav_convert: the images are rejected, going up: 22.05 and 32 kHz to 44.1 kHz") {
  // 5 kHz at 22.05 kHz has its image at 22 050 - 5 000 = 17 050 Hz of the output; the filter must take it away.
  const auto o = convert_tone(5000.0, 22050, 44100);
  const Fit tone = fit(o, 5000.0, 44100.0, 3000);
  const Fit image = fit(o, 17050.0, 44100.0, 3000);
  CHECK(std::abs(db(tone.amp / kAmp)) < 0.03);
  CHECK(db(image.amp / kAmp) < -70.0);
  // 10 kHz at 32 kHz: image at 22 kHz
  const auto p = convert_tone(10000.0, 32000, 44100);
  const Fit tone2 = fit(p, 10000.0, 44100.0, 3000);
  const Fit image2 = fit(p, 22000.0, 44100.0, 3000);
  CHECK(std::abs(db(tone2.amp / kAmp)) < 0.03);
  CHECK(db(image2.amp / kAmp) < -70.0);
}

TEST_CASE("wav_convert: the transition: the response at the edge of the new band, for the record") {
  // 48 to 44.1 kHz: the kernel is down by 6 dB around 0.86 of the Nyquist (19 kHz) and by 60 dB or more past 22.05 kHz. These are the measured bounds, with a margin.
  CHECK(tone_db(17000.0, 48000, 44100) > -0.5);
  CHECK(tone_db(19000.0, 48000, 44100) < -2.0);
  CHECK(tone_db(19000.0, 48000, 44100) > -9.0);
  CHECK(tone_db(21000.0, 48000, 44100) < -20.0);
}

TEST_CASE("wav_convert: a constant stays constant to the first and the last sample") {
  for (std::uint32_t fin : {48000u, 96000u, 22050u, 32000u}) {
    std::vector<std::int16_t> out(100000, 0x5A5A);
    REQUIRE(rs().begin(fin, 44100, out.data(), static_cast<std::uint32_t>(out.size())));
    const std::uint32_t n = fin / 10u;
    for (std::uint32_t i = 0; i < n; ++i) {
      rs().push(-12345.0f);
    }
    rs().finish();
    const std::uint32_t want = static_cast<std::uint32_t>((static_cast<std::uint64_t>(n) * 44100u + fin - 1u) / fin);
    REQUIRE(rs().out() == want);
    for (std::uint32_t i = 0; i < want; ++i) {
      INFO("rate " << fin << " output " << i);
      REQUIRE(std::abs(out[i] + 12345) <= 1);
    }
    CHECK(out[want] == 0x5A5A);
  }
}

TEST_CASE("wav_convert: the output length is ceil(frames * 44100 / rate), and never past the capacity") {
  for (std::uint32_t fin : {48000u, 96000u, 22050u, 32000u, 88200u, 44100u}) {
    for (std::uint32_t frames : {1u, 2u, 7u, 4800u, 48000u, 12345u}) {
      std::vector<std::int16_t> out(200000, 0);
      REQUIRE(rs().begin(fin, 44100, out.data(), static_cast<std::uint32_t>(out.size())));
      for (std::uint32_t i = 0; i < frames; ++i) {
        rs().push(1000.0f);
      }
      rs().finish();
      INFO("rate " << fin << " frames " << frames);
      CHECK(rs().out() == static_cast<std::uint32_t>((static_cast<std::uint64_t>(frames) * 44100u + fin - 1u) / fin));
    }
  }
  // the capacity: the converter stops at it, and says it is full
  std::vector<std::int16_t> out(100, 0);
  REQUIRE(rs().begin(48000, 44100, out.data(), 100));
  for (std::uint32_t i = 0; i < 48000u; ++i) {
    rs().push(500.0f);
  }
  rs().finish();
  CHECK(rs().out() == 100u);
  CHECK(rs().full());
  CHECK_FALSE(rs().begin(0, 44100, out.data(), 100));
  CHECK_FALSE(rs().begin(48000, 0, out.data(), 100));
}

TEST_CASE("wav_convert: the cost: multiply-adds per output follow the kernel's width") {
  // taps() is the kernel's width in input samples (2 * the taps a side); work() / out() stays at or under it. Going down the kernel is wider, going up it is 32 samples.
  struct Case { std::uint32_t fin; unsigned lo, hi; };
  for (const Case c : {Case{48000, 30, 40}, Case{96000, 64, 74}, Case{88200, 60, 70}, Case{32000, 30, 34}, Case{22050, 32, 34}}) {
    std::vector<std::int16_t> out(100000, 0);
    REQUIRE(rs().begin(c.fin, 44100, out.data(), static_cast<std::uint32_t>(out.size())));
    for (std::uint32_t i = 0; i < c.fin / 4u; ++i) {
      rs().push(100.0f);
    }
    rs().finish();
    const double per = static_cast<double>(rs().work()) / static_cast<double>(rs().out());
    INFO("rate " << c.fin << ": taps " << rs().taps() << ", " << per << " multiply-adds an output");
    CHECK(rs().taps() >= c.lo);
    CHECK(rs().taps() <= c.hi);
    CHECK(per <= static_cast<double>(rs().taps()) + 1.0);
    CHECK(per >= static_cast<double>(rs().taps()) * 0.8);
  }
}

TEST_CASE("wav_convert: the integer paths round to nearest, a tie up; the float path rounds a tie away from zero and clips") {
  CHECK(wav::clip16(40000) == 32767);
  CHECK(wav::clip16(-40000) == -32768);
  CHECK(wav::clip16(5) == 5);
  CHECK(wav::round16(0.5f) == 1);
  CHECK(wav::round16(-0.5f) == -1);
  CHECK(wav::round16(0.49f) == 0);
  CHECK(wav::round16(-0.49f) == 0);
  CHECK(wav::round16(1.5f) == 2);
  CHECK(wav::round16(40000.0f) == 32767);
  CHECK(wav::round16(-40000.0f) == -32768);
  CHECK(wav::round16(std::nanf("")) == 0);
  // 24-bit PCM: three bytes, little-endian, signed
  const std::uint8_t b24[3] = {0x80, 0x00, 0x00};  // 128
  CHECK(wav::frame_to_16(b24, 24, wav::Coding::Pcm, 1) == 1);
  const std::uint8_t c24[3] = {0x7F, 0x00, 0x00};  // 127
  CHECK(wav::frame_to_16(c24, 24, wav::Coding::Pcm, 1) == 0);
  const std::uint8_t d24[3] = {0xFF, 0xFF, 0x7F};  // 0x7FFFFF
  CHECK(wav::frame_to_16(d24, 24, wav::Coding::Pcm, 1) == 32767);
  const std::uint8_t e24[3] = {0x00, 0x00, 0x80};  // -0x800000
  CHECK(wav::frame_to_16(e24, 24, wav::Coding::Pcm, 1) == -32768);
  const std::uint8_t f24[3] = {0x7F, 0xFF, 0xFF};  // -129
  CHECK(wav::frame_to_16(f24, 24, wav::Coding::Pcm, 1) == -1);
}
