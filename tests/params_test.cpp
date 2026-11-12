// SPDX-License-Identifier: GPL-3.0-or-later
// PARAMS: the pure logic (pages, focus, stepping, text, bars, the over-budget test) and the screen on the engine (a page opens, the encoder moves a
// value through the engine's parameter, VOICING is the app's own key, PARAMS / DOWN page it, HELP).
#include <doctest/doctest.h>

#include "tract_skip.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/sim/harness.h>

#include <o3_voc03er/units/tract/tract.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include "../app/src/params_logic.h"
#include "../app/src/perform_screen.h"

#include <cmath>
#include <cstring>
#include <string>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace pm = o3_voc03er::params;
using o3_voc03er::Vocoder;

TEST_CASE("params: four pages of eight, the live keys name a parameter the unit has") {
  CHECK(pm::kPages == 4u);
  CHECK(pm::kKeys == 8u);
  CHECK(std::strcmp(pm::key_def(0, 0).name, "SHAPE") == 0);
  CHECK(pm::key_def(0, 0).kind == pm::Kind::Value);
  CHECK(std::strcmp(pm::key_def(0, 2).name, "SUB") == 0);
  CHECK(pm::key_def(0, 2).kind == pm::Kind::Value);
  CHECK(pm::key_def(0, 2).param == Vocoder::Sub);
  CHECK(pm::key_def(0, 3).param == Vocoder::Tilt);
  CHECK(std::strcmp(pm::key_def(1, 4).name, "VOICING") == 0);
  CHECK(pm::key_def(1, 4).kind == pm::Kind::App);
  CHECK(pm::key_def(1, 4).fmt == pm::Fmt::Voicing);
  CHECK(std::strcmp(pm::key_def(1, 1).name, "QUALITY") == 0);
  CHECK(pm::key_def(1, 1).param == Vocoder::Quality);
  CHECK(pm::key_def(2, 4).param == Vocoder::NotesIn);
  CHECK(pm::key_def(1, 7).kind == pm::Kind::None);
  for (unsigned p = 0; p < pm::kPages; ++p) {
    for (unsigned k = 0; k < pm::kKeys; ++k) {
      const pm::KeyDef& d = pm::key_def(p, k);
      CHECK(pm::focusable(p, k) == (d.kind == pm::Kind::Value || d.kind == pm::Kind::App || d.kind == pm::Kind::Tract));
      if (d.kind == pm::Kind::Value) {
        CHECK(d.lo < d.hi);
        CHECK(d.step > 0);
        CHECK(d.param < Vocoder::kParamCount);
      }
    }
  }
  CHECK_FALSE(pm::focusable(0, 8));
}

TEST_CASE("params: a page change is round, and a page opens on its first live key") {
  CHECK(pm::next_page(0) == 1u);
  CHECK(pm::next_page(2) == 3u);
  CHECK(pm::next_page(3) == 0u);
  CHECK(pm::next_page(0, -1) == 3u);
  CHECK(pm::next_page(1, -1) == 0u);
  CHECK(pm::first_focus(0) == 0u);
  CHECK(pm::first_focus(1) == 0u);
  CHECK(pm::first_focus(2) == 0u);
  CHECK(pm::first_focus(3) == 0u);
}

TEST_CASE("params: the TRACT page is the tract unit's eight keys, signed percents and a size by name, and four tabs of at most 196 px") {
  using o3_voc03er::TractUnit;
  CHECK(pm::kTractPage == 3u);
  CHECK(std::string(pm::kPageNames[3]) == "TRACT");
  const char* const names[8] = {"VOWEL", "TONGUE", "JAW", "LIPS", "GLIDE", "SIZE", "RES", "HISS"};
  const std::uint16_t ids[8] = {TractUnit::Vowel, TractUnit::Tongue, TractUnit::Jaw, TractUnit::Lips,
                                TractUnit::Glide, TractUnit::Size,   TractUnit::Res, TractUnit::Hiss};
  for (unsigned k = 0; k < pm::kKeys; ++k) {
    const pm::KeyDef& d = pm::key_def(3, k);
    INFO(names[k]);
    CHECK(std::strcmp(d.name, names[k]) == 0);
    CHECK(d.kind == pm::Kind::Tract);
    CHECK(d.param == ids[k]);
    CHECK(d.param < TractUnit::kParamCount);
    CHECK(d.lo < d.hi);
    CHECK(d.step > 0);
    CHECK(pm::focusable(3, k));
    CHECK(pm::key_live(d, 0u));
    CHECK(pm::key_live(d, o3_voc03er::kModeTract));
  }
  char t[12];
  pm::format_value(pm::key_def(3, 1), -35, t);
  CHECK(std::string(t) == "-35%");
  pm::format_value(pm::key_def(3, 1), 20, t);
  CHECK(std::string(t) == "+20%");
  pm::format_value(pm::key_def(3, 1), 0, t);
  CHECK(std::string(t) == "0%");
  for (int d = 0; d < 5; ++d) {
    pm::format_value(pm::key_def(3, 5), d, t);
    CHECK(std::string(t) == pm::kSizeNames[d]);
    CHECK(pm::to_display(pm::Fmt::Size, static_cast<float>(d)) == d);
  }
  CHECK(pm::to_display(pm::Fmt::SPct, -0.35f) == -35);
  CHECK(pm::to_param(pm::Fmt::SPct, -35) == doctest::Approx(-0.35f));
  // a signed key steps through zero and stops at both ends; VOWEL goes in 1 % detents and 10 % coarse
  const pm::KeyDef& tongue = pm::key_def(3, 1);
  CHECK(pm::step_display(tongue, 0, -1, false) == -2);
  CHECK(pm::step_display(tongue, -98, -3, false) == -100);
  CHECK(pm::step_display(tongue, 95, 1, true) == 100);
  const pm::KeyDef& vowel = pm::key_def(3, 0);
  CHECK(pm::step_display(vowel, 50, 1, false) == 51);
  CHECK(pm::step_display(vowel, 50, -1, true) == 40);
  CHECK(pm::step_display(vowel, 100, 1, false) == 100);
  // the tabs: four, each at most 196 px, in order and inside the 800 px screen
  for (unsigned i = 0; i < pm::kPages; ++i) {
    CHECK(pm::kTabW <= 196);
    CHECK(pm::tab_x(i) >= 0);
    CHECK(pm::tab_x(i) + pm::kTabW <= 800);
    if (i > 0u) {
      CHECK(pm::tab_x(i) >= pm::tab_x(i - 1u) + pm::kTabW);
    }
  }
}

TEST_CASE("params: VOICING is an app key, 0..3 by name, clamped at its ends") {
  const pm::KeyDef& v = pm::key_def(1, 4);
  CHECK(v.kind == pm::Kind::App);
  CHECK(pm::focusable(1, 4));
  CHECK(v.lo == 0);
  CHECK(v.hi == 3);
  CHECK(static_cast<int>(o3_voc03er::Voicing::Smooth) == v.hi);  // the default is the last: SMOOTH
  const char* const names[4] = {"CLOSE", "OPEN", "WIDE", "SMOOTH"};
  for (int d = v.lo; d <= v.hi; ++d) {
    char txt[12];
    pm::format_value(v, d, txt);
    CHECK(std::strcmp(txt, names[d]) == 0);
    CHECK(std::strcmp(o3_voc03er::perform::kVoicingNames[d], names[d]) == 0);
    CHECK(pm::bar_of(v, d) == -1);  // a word, no bar
  }
  CHECK(pm::step_display(v, 0, 1, false) == 1);
  CHECK(pm::step_display(v, 1, 1, false) == 2);
  CHECK(pm::step_display(v, 2, 1, false) == 3);
  CHECK(pm::step_display(v, 3, 1, false) == 3);    // no wrap at the top
  CHECK(pm::step_display(v, 0, -1, false) == 0);   // nor at the bottom
  CHECK(pm::step_display(v, 3, -2, false) == 1);
  CHECK(pm::step_display(v, 1, 9, true) == 3);     // SHIFT (coarse) is the same step
  CHECK(pm::step_display(v, 2, 0, false) == 2);
  CHECK(pm::step_display(v, 3, 1, false, true) == 0);  // a tap at the end goes round, as on the other keys
  // PerformScreen holds it: SMOOTH until set, and a value past the range stops at SMOOTH
  o3_voc03er::PerformScreen ps;
  CHECK(ps.voicing() == 3u);
  ps.set_voicing(1);
  CHECK(ps.voicing() == 1u);
  ps.set_voicing(9);
  CHECK(ps.voicing() == 3u);
}

TEST_CASE("params: display values and parameter values convert both ways") {
  CHECK(pm::to_display(pm::Fmt::Pct, 0.5f) == 50);
  CHECK(pm::to_display(pm::Fmt::Db, -6.0f) == -60);
  CHECK(pm::to_display(pm::Fmt::Db, -0.04f) == 0);
  CHECK(pm::to_display(pm::Fmt::Int, 24.0f) == 24);
  CHECK(pm::to_param(pm::Fmt::Pct, 25) == doctest::Approx(0.25f));
  CHECK(pm::to_param(pm::Fmt::Db, -60) == doctest::Approx(-6.0f));
  CHECK(pm::to_param(pm::Fmt::Int, 12) == doctest::Approx(12.0f));
  for (int d = 0; d <= 100; d += 7) {
    CHECK(pm::to_display(pm::Fmt::Pct, pm::to_param(pm::Fmt::Pct, d)) == d);
  }
}

TEST_CASE("params: stepping clamps the linear keys and wraps the round ones") {
  const pm::KeyDef& noise = pm::key_def(0, 4);
  CHECK(pm::step_display(noise, 50, 1, false) == 52);
  CHECK(pm::step_display(noise, 50, -2, false) == 46);
  CHECK(pm::step_display(noise, 50, 1, true) == 60);
  CHECK(pm::step_display(noise, 99, 1, false) == 100);
  CHECK(pm::step_display(noise, 100, 1, false) == 100);
  CHECK(pm::step_display(noise, 0, -1, false) == 0);
  CHECK(pm::step_display(noise, 100, 1, false, true) == 0);  // a tap at the end goes to the other end
  CHECK(pm::step_display(noise, 0, -1, false, true) == 100);
  CHECK(pm::step_display(noise, 40, 0, false) == 40);
  // LEVEL's tap stops at +6 dB: from the default -6 dB, taps go 0, +6, +6, never round to -48 dB (Fable's finding 11)
  const pm::KeyDef& tap_level = pm::key_def(0, 7);
  REQUIRE(tap_level.param == Vocoder::Level);
  CHECK(pm::step_display(tap_level, -60, 1, true, true) == 0);
  CHECK(pm::step_display(tap_level, 0, 1, true, true) == 60);
  CHECK(pm::step_display(tap_level, 60, 1, true, true) == 60);
  const pm::KeyDef& key = pm::key_def(2, 0);
  CHECK(pm::step_display(key, 11, 1, false) == 0);
  CHECK(pm::step_display(key, 0, -1, false) == 11);
  CHECK(pm::step_display(key, 5, 14, false) == 7);
  const pm::KeyDef& scale = pm::key_def(2, 1);
  CHECK(pm::step_display(scale, static_cast<int>(o3_voc03er::kScaleTableCount) - 1, 1, false) == 0);
  const pm::KeyDef& notes = pm::key_def(2, 4);
  CHECK(pm::step_display(notes, 0, 1, false) == 1);
  CHECK(pm::step_display(notes, 1, 1, false) == 0);

  const pm::KeyDef& quality = pm::key_def(1, 1);
  CHECK(pm::step_display(quality, 8, -1, false) == 8);
  CHECK(pm::step_display(quality, 20, 1, true) == 24);
  CHECK(pm::step_display(quality, 24, 1, false) == 24);
  const pm::KeyDef& mode = pm::key_def(1, 0);
  CHECK(pm::step_display(mode, 2, 1, false) == 3);  // ROBOT
  CHECK(pm::step_display(mode, 3, 1, false) == 4);  // TRACT, the last mode
  CHECK(pm::step_display(mode, 4, 1, false) == 4);

  const pm::KeyDef& level = pm::key_def(0, 7);
  CHECK(pm::step_display(level, 0, 1, false) == 5);
  CHECK(pm::step_display(level, 58, 1, true) == 60);
  CHECK(pm::step_display(level, -478, -1, true) == -480);

  const pm::KeyDef& speed = pm::key_def(2, 2);
  CHECK(pm::step_display(speed, 0, 1, false) == 5);
  CHECK(pm::step_display(speed, 490, 1, true) == 500);

  // the log keys step a tenth of the value (a third coarse), at least 1, and stay inside their ends
  const pm::KeyDef& attack = pm::key_def(0, 5);
  CHECK(pm::step_display(attack, 1, 1, false) == 2);
  CHECK(pm::step_display(attack, 100, 1, false) == 110);
  CHECK(pm::step_display(attack, 100, 1, true) == 133);
  CHECK(pm::step_display(attack, 500, 1, false) == 500);
  CHECK(pm::step_display(attack, 1, -1, false) == 1);
  int v = 1;
  for (int i = 0; i < 200; ++i) {
    v = pm::step_display(attack, v, 1, false);
  }
  CHECK(v == 500);
}

TEST_CASE("params: QUALITY is live only in TALK, MOUTH in TRACT, every other key in every mode") {
  REQUIRE(std::string(o3_voc03er::kModeNames[1]) == "TALK");
  const pm::KeyDef& quality = pm::key_def(1, 1);
  CHECK(pm::key_live(quality, 1u));
  for (unsigned m = 0; m < o3_voc03er::kModeCount; ++m) {
    CHECK(pm::key_live(quality, m) == (m == 1u));
    for (unsigned page = 0; page < pm::kPages; ++page) {
      for (unsigned k = 0; k < pm::kKeys; ++k) {
        const pm::KeyDef& d = pm::key_def(page, k);
        if (d.fmt == pm::Fmt::Source) {
          CHECK(pm::key_live(d, m) == (m == o3_voc03er::kModeTract));
        } else if (&d != &quality) {
          CHECK(pm::key_live(d, m));
        }
      }
    }
  }
}

TEST_CASE("params: MOUTH (LT1) is TRACT's SOURCE on the second page: PADS or LIVE by name, a round key") {
  using o3_voc03er::TractUnit;
  const pm::KeyDef& k = pm::key_def(1, 5);
  CHECK(std::string(k.name) == "MOUTH");
  CHECK(k.kind == pm::Kind::Tract);
  CHECK(k.param == TractUnit::Source);
  CHECK(k.param < TractUnit::kParamCount);
  CHECK(k.fmt == pm::Fmt::Source);
  CHECK(pm::focusable(1, 5));
  char t[12];
  pm::format_value(k, 0, t);
  CHECK(std::string(t) == "PADS");
  pm::format_value(k, 1, t);
  CHECK(std::string(t) == "LIVE");
  CHECK(pm::to_display(k.fmt, 1.0f) == 1);
  CHECK(pm::to_param(k.fmt, 1) == 1.0f);
  CHECK(pm::step_display(k, 0, 1, false) == 1);
  CHECK(pm::step_display(k, 1, 1, false) == 0);   // round: a detent past LIVE is PADS
  CHECK(pm::step_display(k, 0, -1, true) == 1);
  CHECK(pm::bar_of(k, 1) == -1);                  // a word: no bar
  CHECK(pm::key_def(1, 6).kind == pm::Kind::None);
}

TEST_CASE("params: values read as short text") {
  char t[12];
  pm::format_value(pm::key_def(0, 4), 50, t);
  CHECK(std::string(t) == "50%");
  pm::format_value(pm::key_def(0, 5), 120, t);
  CHECK(std::string(t) == "120MS");
  pm::format_value(pm::key_def(0, 7), -60, t);
  CHECK(std::string(t) == "-6.0");
  pm::format_value(pm::key_def(1, 0), 1, t);
  CHECK(std::string(t) == "TALK");
  pm::format_value(pm::key_def(1, 1), 16, t);
  CHECK(std::string(t) == "16");
  pm::format_value(pm::key_def(1, 2), 1, t);
  CHECK(std::string(t) == "ON");
  pm::format_value(pm::key_def(1, 2), 0, t);
  CHECK(std::string(t) == "OFF");
  pm::format_value(pm::key_def(2, 0), 3, t);
  CHECK(std::string(t) == "D#");
  pm::format_value(pm::key_def(2, 1), 2, t);
  CHECK(std::string(t) == "MINOR");
  for (unsigned p = 0; p < pm::kPages; ++p) {
    for (unsigned k = 0; k < pm::kKeys; ++k) {
      const pm::KeyDef& d = pm::key_def(p, k);
      if (d.kind == pm::Kind::Value) {
        pm::format_value(d, d.hi, t);
        CHECK(std::strlen(t) <= 11u);
        pm::format_value(d, d.lo, t);
        CHECK(std::strlen(t) <= 11u);
      }
    }
  }
}

TEST_CASE("params: a bar goes 0..100 for a number and is absent for a word") {
  CHECK(pm::bar_of(pm::key_def(0, 4), 0) == 0);
  CHECK(pm::bar_of(pm::key_def(0, 4), 50) == 50);
  CHECK(pm::bar_of(pm::key_def(0, 4), 100) == 100);
  CHECK(pm::bar_of(pm::key_def(1, 1), 16) == 50);
  CHECK(pm::bar_of(pm::key_def(1, 0), 1) == -1);
  CHECK(pm::bar_of(pm::key_def(2, 0), 4) == -1);
  CHECK(pm::bar_of(pm::key_def(1, 2), 1) == -1);
  const pm::KeyDef& attack = pm::key_def(0, 5);
  const int lo = pm::bar_of(attack, 1), mid = pm::bar_of(attack, 20), hi = pm::bar_of(attack, 500);
  CHECK(lo <= mid);
  CHECK(mid < hi);
  CHECK(hi == 100);
}

namespace {
void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(20);
}

// The voice is one of the engine's units: the one that answers (the largest value, as the other tests read it).
float param(sim::Harness& h, std::uint16_t id) {
  float best = 0.0f;
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(h, u)) continue;
    const float v = h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
}

// The tract unit's own parameter (the unit named TRACT; `param` skips it).
float tract_param(sim::Harness& h, std::uint16_t id) {
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(h, u)) {
      return h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    }
  }
  return -1.0e9f;
}

bool has_row(const app::Help& help, app::Button b) {
  for (std::size_t i = 0; i < help.button_count; ++i) {
    if (help.buttons[i].button == b) {
      return help.buttons[i].text != nullptr;
    }
  }
  return false;
}
}  // namespace

TEST_CASE("params: the screen moves a value through the engine, VOICING takes the encoder, PARAMS and DOWN page it, HELP is valid") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  tap(h, app::Button::Params);
  h.run_ms(60);

  // CARRIER opens on SHAPE; NOISE is F5, and a detent is a step of 2 %
  const float noise0 = param(h, Vocoder::Noise);
  tap(h, app::Button::F5);
  h.turn(5);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(noise0 + 0.10f).epsilon(0.02));

  // SUB is live, a detent a step of 2 %; the encoder has left NOISE
  const float noise1 = param(h, Vocoder::Noise);
  tap(h, app::Button::F3);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(noise1).epsilon(0.02));
  CHECK(param(h, Vocoder::Sub) == doctest::Approx(0.02f).epsilon(0.02));

  // DOWN pages to SHAPE (MODE under the encoder), PARAMS pages on to TUNE (KEY), UP goes back
  tap(h, app::Button::Down);
  h.run_ms(60);
  const float mode0 = param(h, Vocoder::Mode);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Mode) != doctest::Approx(mode0));
  // VOICING is the app's key: a tap points the encoder at it, so a turn moves nothing in the unit (the value is PerformScreen's)
  const float mode1 = param(h, Vocoder::Mode);
  tap(h, app::Button::F5);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Mode) == doctest::Approx(mode1));
  tap(h, app::Button::Params);
  h.run_ms(60);
  const float key0 = param(h, Vocoder::Key);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Key) == doctest::Approx(std::fmod(key0 + 1.0f, 12.0f)));
  tap(h, app::Button::Up);
  h.run_ms(60);
  const float quality0 = param(h, Vocoder::Quality);
  tap(h, app::Button::F2);  // QUALITY: the first tap points the encoder at it
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Quality) == doctest::Approx(std::fmin(quality0 + 1.0f, 24.0f)));

  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Config);
  h.release(app::Button::Shift);
  h.run_ms(60);
  REQUIRE(h.runtime().help_open());
  const app::Help& help = h.runtime().help_overlay().help();
  app::HelpProblem p;
  const bool ok = app::help_check(help, &p);
  CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
  for (const app::Button b : {app::Button::Up, app::Button::Down, app::Button::Params, app::Button::Performance}) {
    INFO(std::string(app::button_name(b)));
    CHECK(has_row(help, b));
  }
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("params: the TRACT page writes the tract unit in every mode and its HELP is valid") {
  using o3_voc03er::TractUnit;
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  REQUIRE(tract_param(h, TractUnit::Hiss) > -1.0e8f);  // the unit is in the engine
  tap(h, app::Button::Params);
  h.run_ms(60);
  tap(h, app::Button::Up);  // CARRIER, one page back: TRACT (round)
  h.run_ms(60);
  const float mode0 = param(h, Vocoder::Mode);
  // VOWEL is F1, the page's first key and so under the encoder as the page opens: a detent 1 %
  h.turn(7);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.07f).epsilon(0.02));
  // JAW is signed: a detent 2 %, down through zero
  tap(h, app::Button::F3);
  h.turn(-3);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Jaw) == doctest::Approx(-0.06f).epsilon(0.02));
  // SIZE by name: BIG..TINY, written as 0..4
  tap(h, app::Button::F6);
  h.turn(1);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Size) == doctest::Approx(3.0f));
  // the vocoder is untouched, and so is the vowel while the encoder points elsewhere
  CHECK(param(h, Vocoder::Mode) == doctest::Approx(mode0));
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.07f).epsilon(0.02));

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

TEST_CASE("header: a slot that speaks is named the same on every screen, not the live source") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  o3sdk::dsp::UnitId voice = o3sdk::dsp::kNoUnit;
  for (unsigned u = 0; u < 8 && voice == o3sdk::dsp::kNoUnit; ++u) {
    const auto id = static_cast<o3sdk::dsp::UnitId>(u);
    (void)h.engine().set_param(id, Vocoder::ModSrc, static_cast<float>(o3_voc03er::route::kFirstSlot));
    h.run_ms(20);
    voice = h.engine().param(id, Vocoder::ModSrc) == static_cast<float>(o3_voc03er::route::kFirstSlot) ? id : voice;
  }
  REQUIRE(voice != o3sdk::dsp::kNoUnit);
  o3_voc03er::ornoir::HeaderState hs;
  o3_voc03er::header_route(h.engine(), voice, "USB", true, hs);
  CHECK(std::strcmp(hs.speaks, "SLOT 1") == 0);
  (void)h.engine().set_param(voice, Vocoder::ModSrc, static_cast<float>(o3_voc03er::route::kSpeaksLive));
  h.run_ms(20);
  o3_voc03er::header_route(h.engine(), voice, "LINE", false, hs);
  CHECK(std::strcmp(hs.speaks, "LINE") == 0);
}
