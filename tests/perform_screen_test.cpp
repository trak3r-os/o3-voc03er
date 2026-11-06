// SPDX-License-Identifier: GPL-3.0-or-later
// PERFORM on the engine: a chord pad sounds a chord, SHIFT latches it, DELETE lets it go; a frame fits the display list.
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/sim/harness.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;

namespace {
void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(5);
}

void open_help(sim::Harness& h) {
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Config);
  h.release(app::Button::Shift);
  h.run_ms(60);
  REQUIRE(h.runtime().help_open());
}

bool has_row(const app::Help& help, app::Button b) {
  for (std::size_t i = 0; i < help.button_count; ++i) {
    if (help.buttons[i].button == b) {
      return help.buttons[i].text != nullptr;
    }
  }
  return false;
}

// The buttons app.cpp's input() answers on PERFORM and on a stub. A button with no row in HELP draws as a hollow cap, which says it does nothing.
constexpr app::Button kAnswered[] = {app::Button::Params,     app::Button::Performance, app::Button::Play,       app::Button::Master,
                                     app::Button::File,       app::Button::SampleLoad,  app::Button::SamplePlay, app::Button::SampleEdit,
                                     app::Button::SampleRec,  app::Button::Song,        app::Button::Pattern,    app::Button::Note,
                                     app::Button::Instr,      app::Button::Vol,         app::Button::Fx};
}  // namespace

TEST_CASE("perform: a held chord pad lights, SHIFT latches it, DELETE releases it") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  h->pad_down(26);
  h->run_ms(100);
  CHECK(h->leds().level(26) == 255);
  tap(*h, app::Button::Shift);
  h->pad_up(26);
  h->run_ms(100);
  CHECK(h->leds().level(26) == 140);  // latched: Mid
  tap(*h, app::Button::Delete);
  h->run_ms(100);
  CHECK(h->leds().level(26) != 140);
  CHECK(h->runtime().frame_failures() == 0u);
  CHECK(h->display().peak_frame_words() < 2048u);
}

TEST_CASE("perform: HELP names every control, and its legend is the page in force") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);

  open_help(*h);
  {
    const app::Help& help = h->runtime().help_overlay().help();
    app::HelpProblem p;
    const bool ok = app::help_check(help, &p);
    CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
    for (const app::Button b : kAnswered) {
      INFO(std::string(app::button_name(b)));
      CHECK(has_row(help, b));
    }
    for (const app::Button b : {app::Button::Shift, app::Button::Delete, app::Button::Up, app::Button::Down, app::Button::Left, app::Button::Right}) {
      INFO(std::string(app::button_name(b)));
      CHECK(has_row(help, b));
    }
    for (unsigned k = 0; k < 8; ++k) {
      CHECK(help.fkey[k] != nullptr);
      CHECK(help.fkey_shift[k] != nullptr);  // the SHIFT row: eight labels
    }
    CHECK(help.encoder != nullptr);
    CHECK(help.group_count == 5u);  // the three fixed rows, the bottom row, the LEDs
    for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
      CHECK(help.pad_group[pad] == app::pad_row(pad) + 1u);
      CHECK(help.pad_label[pad] != nullptr);
    }
  }
  tap(*h, app::Button::Config);
  REQUIRE_FALSE(h->runtime().help_open());

  tap(*h, app::Button::F7);  // the focus, then a second tap: KEYS
  tap(*h, app::Button::F7);
  open_help(*h);
  {
    const app::Help& help = h->runtime().help_overlay().help();
    app::HelpProblem p;
    const bool ok = app::help_check(help, &p);
    CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
    CHECK(help.group_count == 4u);  // white keys, black keys, controls, and the FORMANT row: none of the chord rows
    for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
      CHECK(help.pad_group[pad] <= 4u);
    }
    for (unsigned col = 0; col < app::kPadCols; ++col) {
      CHECK(help.pad_group[3u * app::kPadCols + col] == 4u);
    }
  }
  tap(*h, app::Button::Config);

  h->press(app::Button::Shift);  // SHIFT + F7: BEND ROW on, over KEYS: row 3 is the strip, a fourth group
  h->run_ms(2);
  tap(*h, app::Button::F7);
  h->release(app::Button::Shift);
  h->run_ms(20);
  open_help(*h);
  {
    const app::Help& help = h->runtime().help_overlay().help();
    app::HelpProblem p;
    const bool ok = app::help_check(help, &p);
    CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
    CHECK(help.group_count == 4u);
    for (unsigned col = 0; col < app::kPadCols; ++col) {
      CHECK(help.pad_group[3u * app::kPadCols + col] == 4u);
    }
  }
  tap(*h, app::Button::Config);
}

TEST_CASE("perform: KEYS controls light while held and play no note; a gap is dark") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  tap(*h, app::Button::F7);
  tap(*h, app::Button::F7);  // KEYS
  h->run_ms(60);
  CHECK(h->leds().level(7) == 24);   // ROOT+: dim
  CHECK(h->leds().level(2) == 0);    // a gap
  CHECK(h->leds().level(36) == 24);  // row 3 without BEND ROW: the formant strip, -12 dim
  h->pad_down(7);
  h->run_ms(60);
  CHECK(h->leds().level(7) == 255);
  h->pad_up(7);
  h->run_ms(60);
  CHECK(h->leds().level(7) == 24);
  h->pad_down(2);  // a gap: nothing
  h->run_ms(60);
  CHECK(h->leds().level(2) == 0);
  h->pad_up(2);
  h->pad_down(9);  // MOD, held for the ramp
  h->run_ms(500);
  CHECK(h->leds().level(9) == 255);
  h->pad_up(9);
  h->run_ms(500);
  CHECK(h->runtime().frame_failures() == 0u);
}

namespace {
// A unit parameter by id, read off whichever of the first units holds it (as app_test.cpp's peak_param).
float unit_param(sim::Harness& h, std::uint16_t id) {
  float best = 0.0f;
  for (unsigned u = 0; u < 8; ++u) {
    const float v = h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
}
void shift_tap(sim::Harness& h, app::Button b) {
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, b);
  h.release(app::Button::Shift);
  h.run_ms(20);
}
constexpr std::uint16_t kFreeze = 9, kFormant = 40, kThru = 41;
}  // namespace

TEST_CASE("perform: the SHIFT row focuses FORMANT and THRU, FREEZE toggles, a waiting key does nothing") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  shift_tap(*h, app::Button::F1);  // FREEZE on, off
  h->run_ms(20);
  CHECK(unit_param(*h, kFreeze) == doctest::Approx(1.0f));
  shift_tap(*h, app::Button::F1);
  h->run_ms(20);
  CHECK(unit_param(*h, kFreeze) == doctest::Approx(0.0f));

  shift_tap(*h, app::Button::F5);  // takes the focus: the encoder moves FORMANT a semitone a detent, SHIFT coarse 4
  h->turn(3);
  h->run_ms(20);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(3.0f));
  h->press(app::Button::Shift);
  h->run_ms(2);
  h->turn(-1);
  h->release(app::Button::Shift);
  h->run_ms(20);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(-1.0f));
  shift_tap(*h, app::Button::F5);  // a second SHIFT + tap steps it
  h->run_ms(20);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(0.0f));

  shift_tap(*h, app::Button::F6);  // THRU: 5 a detent in 0..100, the unit's 0..1
  h->turn(2);
  h->run_ms(20);
  CHECK(unit_param(*h, kThru) == doctest::Approx(0.10f));

  shift_tap(*h, app::Button::F2);  // GATE waits for P3: nothing moves, the focus stays on THRU
  h->turn(1);
  h->run_ms(20);
  CHECK(unit_param(*h, kThru) == doctest::Approx(0.15f));

  tap(*h, app::Button::F3);  // a plain tap goes back: the encoder no longer moves THRU
  h->turn(1);
  h->run_ms(20);
  CHECK(unit_param(*h, kThru) == doctest::Approx(0.15f));
  CHECK(h->runtime().frame_failures() == 0u);
  CHECK(h->display().peak_frame_words() < 2048u);
}

TEST_CASE("perform: the formant strip sets Formant, stays lit, and SHIFT + F5 shows the same value") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  tap(*h, app::Button::F7);
  tap(*h, app::Button::F7);  // KEYS, BEND ROW off
  h->run_ms(60);
  CHECK(h->leds().level(36 + 5) == 255);  // 0: both middle pads on
  CHECK(h->leds().level(36 + 6) == 255);
  CHECK(h->leds().level(36 + 0) == 24);
  h->pad_down(36 + 8);  // +4
  h->run_ms(60);
  h->pad_up(36 + 8);
  h->run_ms(60);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(4.0f));
  CHECK(h->leds().level(36 + 8) == 255);  // it stays
  CHECK(h->leds().level(36 + 5) == 24);
  shift_tap(*h, app::Button::F5);
  h->turn(1);
  h->run_ms(20);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(5.0f));  // the same value; 5 has no pad: all dim
  CHECK(h->leds().level(36 + 8) == 24);
  CHECK(h->runtime().frame_failures() == 0u);
}

TEST_CASE("stub: HELP gives a row to every button the stub answers") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  tap(*h, app::Button::Master);
  h->run_ms(100);
  open_help(*h);
  const app::Help& help = h->runtime().help_overlay().help();
  for (const app::Button b : kAnswered) {
    INFO(std::string(app::button_name(b)));
    CHECK(has_row(help, b));
  }
  tap(*h, app::Button::Config);
}
