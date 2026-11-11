// SPDX-License-Identifier: GPL-3.0-or-later
// MASTER: the pure logic (the eight keys, what the board lets them do, the dim keys' lines, the source round, the radio's text, the meters'
// follower) and one screen test on the engine (the keys open on SOURCE, DRY moves through the unit, a dim key says why, HELP is valid).
#include <doctest/doctest.h>

#include "tract_skip.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/sim/harness.h>

#include <o3_voc03er/units/vocoder/vocoder.h>

#include "../app/src/master_logic.h"

#include <cmath>
#include <cstring>
#include <string>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace ms = o3_voc03er::master;
using o3_voc03er::Vocoder;
using Src = o3sdk::hal::capture::Source;

TEST_CASE("master: eight keys, the six that step name a value the unit or the kit has") {
  CHECK(ms::kKeys == 8u);
  CHECK(std::strcmp(ms::key_name(ms::Source), "SOURCE") == 0);
  CHECK(std::strcmp(ms::key_name(ms::Return), "RETURN") == 0);
  CHECK(std::strcmp(ms::key_name(ms::SeekDown), "SEEK DOWN") == 0);
  CHECK(std::strcmp(ms::key_name(ms::SeekUp), "SEEK UP") == 0);
  CHECK(ms::kDefs[ms::Dry].param == Vocoder::Dry);
  CHECK(ms::kDefs[ms::Level].param == Vocoder::Level);
  for (unsigned k = 0; k < 6u; ++k) {
    CHECK(ms::steppable(k));
    CHECK(ms::kDefs[k].lo < ms::kDefs[k].hi);
  }
  CHECK_FALSE(ms::steppable(ms::SeekDown));
  CHECK_FALSE(ms::steppable(ms::SeekUp));
}

TEST_CASE("master: which keys the board answers, and what a dim key says") {
  ms::Avail none;  // no capture device, no radio, no USB audio
  CHECK_FALSE(ms::live(ms::Source, none));
  CHECK_FALSE(ms::live(ms::Gain, none));
  CHECK(ms::live(ms::Monitor, none));
  CHECK(ms::live(ms::Dry, none));
  CHECK(ms::live(ms::Level, none));
  CHECK_FALSE(ms::live(ms::Return, none));
  CHECK_FALSE(ms::live(ms::SeekUp, none));
  CHECK(std::strcmp(ms::dim_note(ms::Source, none), "NO AUDIO INPUT ON THIS BOARD") == 0);

  ms::Avail line;
  line.audio_in = true;
  CHECK(ms::live(ms::Source, line));
  CHECK(ms::live(ms::Gain, line));
  CHECK_FALSE(ms::live(ms::SeekDown, line));
  CHECK(std::strcmp(ms::dim_note(ms::SeekDown, line), "PICK RADIO AS THE SOURCE") == 0);

  ms::Avail usb = line;
  usb.usb_source = true;
  usb.usb_device = true;
  CHECK_FALSE(ms::live(ms::Gain, usb));
  CHECK(ms::live(ms::Return, usb));
  CHECK(std::strcmp(ms::dim_note(ms::Gain, usb), "NO GAIN ON USB") == 0);

  ms::Avail radio = line;
  radio.radio_source = true;
  CHECK_FALSE(ms::live(ms::SeekUp, radio));  // the receiver has not answered
  CHECK(std::strcmp(ms::dim_note(ms::SeekUp, radio), "THE RADIO DID NOT ANSWER") == 0);
  radio.radio_usable = true;
  CHECK(ms::live(ms::SeekUp, radio));
  CHECK(ms::live(ms::SeekDown, radio));
}

TEST_CASE("master: the source is a round key, a second tap steps coarse, MONITOR toggles") {
  const int usb = ms::source_index(Src::Usb);
  CHECK(usb == static_cast<int>(o3_voc03er::kSourceCount) - 1);
  CHECK(ms::source_after(usb, 1).source == o3_voc03er::kSources[0].source);  // wraps to the first
  CHECK(ms::source_after(0, -1).source == Src::Usb);
  CHECK(ms::source_after(0, 1).source == o3_voc03er::kSources[1].source);
  CHECK(ms::source_index(Src::Radio) == 5);

  CHECK(ms::tap_value(ms::Monitor, 0) == static_cast<int>(o3sdk::app::AudioIn::kMonitorOn));
  CHECK(ms::tap_value(ms::Monitor, 40) == 0);
  CHECK(ms::tap_value(ms::Dry, 100) == 0);  // coarse, wraps at the end
  CHECK(ms::tap_value(ms::Dry, 0) == 10);
  CHECK(ms::tap_value(ms::Gain, 100) == 100);  // GAIN and LEVEL stop at the top: a wrap would be full to silence in one tap
  CHECK(ms::tap_value(ms::Gain, 95) == 100);
  CHECK(ms::tap_value(ms::Level, 60) == 60);
  CHECK(ms::tap_value(ms::SeekUp, 7) == 7);
}

TEST_CASE("master: the radio's frequency reads as MHz with two decimals") {
  char t[12];
  ms::format_freq(9850u, t);
  CHECK(std::strcmp(t, "98.50") == 0);
  ms::format_freq(10800u, t);
  CHECK(std::strcmp(t, "108.00") == 0);
  ms::format_freq(8750u, t);
  CHECK(std::strcmp(t, "87.50") == 0);
  ms::format_freq(10005u, t);
  CHECK(std::strcmp(t, "100.05") == 0);
}

TEST_CASE("master: a meter rises at once, falls by a quarter, and holds its peak") {
  ms::Follower f;
  f.feed(200);
  CHECK(f.level == 200);
  CHECK(f.peak == 200);
  f.feed(0);
  CHECK(f.level == 150);  // a quarter of the distance
  CHECK(f.peak == 200);
  for (unsigned i = 1; i < ms::Follower::kPeakHoldFrames; ++i) {
    f.feed(0);
  }
  CHECK(f.peak == 200);  // still held
  f.feed(0);
  CHECK(f.peak < 200);  // then falls
  for (unsigned i = 0; i < 400u; ++i) {
    f.feed(0);
  }
  CHECK(f.level == 0);
  CHECK(f.peak == 0);
}

namespace {
void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(20);
}

float param(sim::Harness& h, std::uint16_t id) {
  float best = 0.0f;
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(h, u)) continue;
    const float v = h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
}
}  // namespace

TEST_CASE("master: the button opens the screen, DRY moves through the unit, a dim key changes nothing, HELP is valid") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  tap(h, app::Button::Master);
  h.run_ms(60);

  // DRY is F4: the first tap points the encoder at it, a detent is 2 %
  const float dry0 = param(h, Vocoder::Dry);
  tap(h, app::Button::F4);
  h.turn(5);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Dry) == doctest::Approx(dry0 + 0.10f).epsilon(0.02));

  // SEEK UP is dim unless the source is RADIO: a tap moves nothing
  const float dry1 = param(h, Vocoder::Dry);
  tap(h, app::Button::F8);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Dry) == doctest::Approx(dry1 + 0.02f).epsilon(0.05));  // the encoder still drives DRY

  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Config);
  h.release(app::Button::Shift);
  h.run_ms(60);
  REQUIRE(h.runtime().help_open());
  app::HelpProblem p;
  const bool ok = app::help_check(h.runtime().help_overlay().help(), &p);
  CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
  CHECK(h.runtime().frame_failures() == 0u);
}
