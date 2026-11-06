// SPDX-License-Identifier: GPL-3.0-or-later
// PERFORM on the engine: a chord pad sounds a chord, SHIFT latches it, DELETE lets it go; a frame fits the display list.
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/sim/harness.h>

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
    CHECK(help.group_count == 2u);  // white and black keys, none of the chord rows
    for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
      CHECK(help.pad_group[pad] <= 2u);
    }
  }
  tap(*h, app::Button::Config);
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
