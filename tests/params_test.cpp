// SPDX-License-Identifier: GPL-3.0-or-later
// PARAMS: the pure logic (pages, focus, stepping, text, bars, the over-budget test) and the screen on the engine (a page opens, the encoder moves a
// value through the engine's parameter, a key that comes later changes nothing, PARAMS / DOWN page it, HELP).
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/sim/harness.h>

#include <o3_voc03er/units/vocoder/vocoder.h>

#include "../app/src/params_logic.h"

#include <cmath>
#include <cstring>
#include <string>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace pm = o3_voc03er::params;
using o3_voc03er::Vocoder;

TEST_CASE("params: three pages of eight, the live keys name a parameter the unit has") {
  CHECK(pm::kPages == 3u);
  CHECK(pm::kKeys == 8u);
  CHECK(std::strcmp(pm::key_def(0, 0).name, "SHAPE") == 0);
  CHECK(pm::key_def(0, 0).kind == pm::Kind::Value);
  CHECK(std::strcmp(pm::key_def(0, 2).name, "SUB") == 0);
  CHECK(pm::key_def(0, 2).kind == pm::Kind::Later);
  CHECK(std::strcmp(pm::key_def(1, 1).name, "QUALITY") == 0);
  CHECK(pm::key_def(1, 1).param == Vocoder::Quality);
  CHECK(pm::key_def(2, 4).param == Vocoder::NotesIn);
  CHECK(pm::key_def(1, 7).kind == pm::Kind::None);
  for (unsigned p = 0; p < pm::kPages; ++p) {
    for (unsigned k = 0; k < pm::kKeys; ++k) {
      const pm::KeyDef& d = pm::key_def(p, k);
      CHECK(pm::focusable(p, k) == (d.kind == pm::Kind::Value));
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
  CHECK(pm::next_page(2) == 0u);
  CHECK(pm::next_page(0, -1) == 2u);
  CHECK(pm::next_page(1, -1) == 0u);
  CHECK(pm::first_focus(0) == 0u);
  CHECK(pm::first_focus(1) == 0u);
  CHECK(pm::first_focus(2) == 0u);
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
  CHECK(pm::step_display(mode, 2, 1, false) == 2);

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
    const float v = h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
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

TEST_CASE("params: the screen moves a value through the engine, a key that comes later changes nothing, PARAMS and DOWN page it, HELP is valid") {
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

  // SUB is a key that comes later: a tap moves nothing in the unit
  const float noise1 = param(h, Vocoder::Noise);
  tap(h, app::Button::F3);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(noise1).epsilon(0.02));

  // DOWN pages to SHAPE (MODE under the encoder), PARAMS pages on to TUNE (KEY), UP goes back
  tap(h, app::Button::Down);
  h.run_ms(60);
  const float mode0 = param(h, Vocoder::Mode);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Mode) != doctest::Approx(mode0));
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
