// SPDX-License-Identifier: GPL-3.0-or-later
// The arithmetic of the TALK and TUNE voiceprints (app/src/voiceprint_logic.h): the step-up from reflection coefficients, the envelope, the ladder.

#include <doctest/doctest.h>

#include "../app/src/voiceprint_logic.h"

#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace vp = o3_voc03er::voiceprint;

namespace {

constexpr float kPi = 3.14159265f;

// The two reflection coefficients of a resonator with poles at r e^(+-j theta): a1 = -2 r cos(theta), a2 = r^2, and k2 = a2, k1 = a1 / (1 + a2).
void two_pole(float hz, float r, float (&k)[vp::kMaxOrder]) {
  const float th = 2.0f * kPi * hz / vp::kLpcRate;
  const float a1 = -2.0f * r * std::cos(th);
  const float a2 = r * r;
  for (float& x : k) x = 0.0f;
  k[0] = a1 / (1.0f + a2);
  k[1] = a2;
}

unsigned peak_index(const std::uint8_t (&line)[vp::kPoints]) {
  unsigned best = 0;
  for (unsigned j = 1; j < vp::kPoints; ++j) {
    if (line[j] > line[best]) best = j;
  }
  return best;
}

}  // namespace

TEST_CASE("voiceprint: the step-up of a two-pole filter gives its direct form") {
  float k[vp::kMaxOrder];
  two_pole(2000.0f, 0.97f, k);
  float a[vp::kMaxOrder] = {};
  vp::step_up(k, 2, a);
  const float th = 2.0f * kPi * 2000.0f / vp::kLpcRate;
  CHECK(a[0] == doctest::Approx(-2.0f * 0.97f * std::cos(th)).epsilon(1e-4));
  CHECK(a[1] == doctest::Approx(0.97f * 0.97f).epsilon(1e-4));
}

TEST_CASE("voiceprint: the envelope of a two-pole filter peaks at its frequency") {
  vp::Axis ax{};
  vp::axis_init(ax);
  for (const float hz : {1000.0f, 2000.0f, 3500.0f}) {
    float k[vp::kMaxOrder];
    two_pole(hz, 0.97f, k);
    std::uint8_t line[vp::kPoints];
    vp::envelope(ax, k, 2, line);
    const unsigned pk = peak_index(line);
    CHECK(line[pk] == 255);
    CHECK(vp::point_hz(pk) == doctest::Approx(hz).epsilon(0.08));
    CHECK(line[0] < 255);  // 125 Hz is well under the resonance
  }
}

TEST_CASE("voiceprint: flat coefficients give a flat line") {
  vp::Axis ax{};
  vp::axis_init(ax);
  float k[vp::kMaxOrder] = {};
  std::uint8_t line[vp::kPoints];
  vp::envelope(ax, k, vp::kMaxOrder, line);
  for (const std::uint8_t v : line) CHECK(v == 255);
  vp::envelope(ax, k, 0, line);  // order 0 as well
  for (const std::uint8_t v : line) CHECK(v == 255);
}

TEST_CASE("voiceprint: a bad value stays finite and drawable") {
  vp::Axis ax{};
  vp::axis_init(ax);
  const float bad[] = {std::nanf(""), INFINITY, -INFINITY, 1.0e30f, -1.0e30f, 0.9999f};
  for (const float b : bad) {
    float k[vp::kMaxOrder];
    for (unsigned i = 0; i < vp::kMaxOrder; ++i) k[i] = (i % 3 == 0) ? b : 0.3f;
    float a[vp::kMaxOrder] = {};
    vp::step_up(k, 40, a);  // an order past the maximum is held to it
    for (const float x : a) CHECK(std::isfinite(x));
    std::uint8_t line[vp::kPoints];
    vp::envelope(ax, k, 40, line);
    bool any_top = false;
    for (const std::uint8_t v : line) any_top = any_top || v == 255;
    CHECK(any_top);  // normalised to its own peak, whatever came in
  }
  CHECK(vp::sane(std::nanf("")) == 0.0f);
  CHECK(vp::sane(5.0f) == vp::kMaxK);
  CHECK(vp::sane(-5.0f) == -vp::kMaxK);
}

TEST_CASE("voiceprint: the axis points run from 125 Hz to 8 kHz") {
  CHECK(vp::point_hz(0) == doctest::Approx(125.0f));
  CHECK(vp::point_hz(vp::kPoints - 1) == doctest::Approx(8000.0f).epsilon(1e-3));
}

TEST_CASE("voiceprint: the ladder puts a note on the right rung") {
  constexpr int h = 174;
  constexpr std::uint16_t cmajor = 0xAB5;  // C D E F G A B
  CHECK(vp::ladder_y(0, h) == 87);
  CHECK(vp::ladder_y(100, h) < vp::ladder_y(0, h));  // up is toward the top
  CHECK(vp::ladder_y(-100, h) > vp::ladder_y(0, h));
  CHECK(vp::ladder_y(5000, h) == vp::ladder_y(1250, h));  // held at the edge

  vp::Rung rung[vp::kSpan];
  const unsigned n = vp::ladder_rungs(60, cmajor, 0, h, rung);
  CHECK(n == 15);  // 25 semitones of a seven-note scale: 48..72 holds 15 C-major notes
  unsigned roots = 0;
  bool found_centre = false;
  for (unsigned i = 0; i < n; ++i) {
    if (i != 0) CHECK(rung[i].y < rung[i - 1].y);  // from the lowest up, so y falls
    if (rung[i].root) ++roots;
    if (rung[i].y == 87) {
      found_centre = true;
      CHECK(rung[i].root);  // middle C is the KEY's root
    }
  }
  CHECK(found_centre);
  CHECK(roots == 3);  // C48, C60, C72
}

TEST_CASE("voiceprint: the detected pitch and the ladder's centre") {
  CHECK(vp::detected_cents(60, 62, 30) == 170);
  CHECK(vp::detected_cents(60, -1, 30) == 0);
  CHECK(vp::ladder_follow(0, -1) == 60);
  CHECK(vp::ladder_follow(60, 64) == 60);  // within six semitones: stays
  CHECK(vp::ladder_follow(60, 67) == 67);  // past six: moves to the target
  CHECK(vp::ladder_follow(60, -1) == 60);  // no target: stays
}
