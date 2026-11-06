// SPDX-License-Identifier: GPL-3.0-or-later
// SAMPLE: the pure logic (SIDE against route::allowed, SPEED, LEVEL, the pad split) and the screen on the engine (a take, then SIDE, SPEED, DIR,
// LEVEL and LOOP reach the unit; HELP).
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/platform/host/fake_capture.h>
#include <o3sdk/sim/harness.h>

#include <o3_voc03er/route.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include "../app/src/sample_logic.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace sm = o3_voc03er::sample;
namespace route = o3_voc03er::route;
using o3_voc03er::Vocoder;

TEST_CASE("sample: SIDE moves a slot between SPEAKS, SOUNDS and NONE, refuses an empty one") {
  route::Route cur;
  sm::SidePlan p = sm::plan_side(cur, sm::Side::Speaks, 1, true);
  CHECK(p.refused);
  CHECK_FALSE(p.change);
  CHECK(p.to.speaks == cur.speaks);
  p = sm::plan_side(cur, sm::Side::Speaks, 1, false);
  CHECK(p.change);
  CHECK(p.to.speaks == route::kFirstSlot + 1);
  CHECK(route::allowed(p.to));
  route::Route on_speaks = p.to;
  p = sm::plan_side(on_speaks, sm::Side::Sounds, 1, false);  // a move: SPEAKS goes back to LIVE
  CHECK(p.change);
  CHECK(p.to.speaks == route::kSpeaksLive);
  CHECK(p.to.sounds == route::kFirstSlot + 1);
  p = sm::plan_side(on_speaks, sm::Side::None, 1, true);  // leaving needs no audio
  CHECK(p.change);
  CHECK_FALSE(p.refused);
  CHECK(p.to.speaks == route::kSpeaksLive);
  p = sm::plan_side(on_speaks, sm::Side::Speaks, 1, false);  // already there
  CHECK_FALSE(p.change);
  CHECK_FALSE(p.refused);
  CHECK(sm::side_of(on_speaks, 1) == sm::Side::Speaks);
  CHECK(sm::side_of(on_speaks, 0) == sm::Side::None);
  CHECK(sm::step_side(sm::Side::None, 1) == sm::Side::Speaks);
  CHECK(sm::step_side(sm::Side::Speaks, -1) == sm::Side::None);
}

TEST_CASE("sample: SPEED, LEVEL and the pad split") {
  CHECK(sm::step_speed(0, -1, false) == 0u);
  CHECK(sm::step_speed(2, 1, false) == 2u);
  CHECK(sm::step_speed(2, 1, true) == 0u);
  CHECK(sm::step_speed(0, -1, true) == 2u);
  CHECK(sm::step_level(0.0f, 1) == doctest::Approx(0.5f));
  CHECK(sm::step_level(-24.0f, -3) == doctest::Approx(-24.0f));
  CHECK(sm::step_level(11.5f, 6) == doctest::Approx(12.0f));
  char t[8];
  sm::level_text(0.0f, t);
  CHECK(std::strcmp(t, "0") == 0);
  sm::level_text(3.5f, t);
  CHECK(std::strcmp(t, "+3.5") == 0);
  sm::level_text(-24.0f, t);
  CHECK(std::strcmp(t, "-24") == 0);
  sm::level_text(-0.5f, t);
  CHECK(std::strcmp(t, "-0.5") == 0);
  CHECK(sm::level_percent(-24.0f) == 0);
  CHECK(sm::level_percent(12.0f) == 100);
  CHECK_FALSE(sm::chord_pad(0));
  CHECK_FALSE(sm::chord_pad(23));
  CHECK(sm::chord_pad(24));
  CHECK(sm::chord_pad(47));
}

namespace {
void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(5);
}

float unit_param(sim::Harness& h, std::uint16_t id) {
  float best = 0.0f;
  for (unsigned u = 0; u < 8; ++u) {
    const float v = h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
}

constexpr std::uint16_t kCarSrc = 34, kModSrc = 47;

// SAMPLE REC takes 1 BAR into slot 1 (AFTER NONE: it is not placed), then SAMPLE opens.
struct Rig {
  o3sdk::platform::host::FakeCapture cap;
  std::unique_ptr<sim::Harness> h;
  Rig() {
    cap.input_tone(220.0f, -12.0f);
    h = std::make_unique<sim::Harness>(::o3sdk_app(), nullptr, nullptr, nullptr, nullptr, &cap);
    REQUIRE(h->start());
    h->run_ms(100);
  }
  void take() {
    tap(*h, app::Button::SampleRec);
    h->run_ms(60);
    tap(*h, app::Button::F3);  // LENGTH: 1 BAR
    tap(*h, app::Button::F4);  // AFTER
    tap(*h, app::Button::F4);
    tap(*h, app::Button::F4);  // NONE
    tap(*h, app::Button::Rec);
    h->run_ms(9000);
  }
  void open() {
    tap(*h, app::Button::SamplePlay);
    h->run_ms(60);
  }
};

bool has_row(const app::Help& help, app::Button b) {
  for (std::size_t i = 0; i < help.button_count; ++i) {
    if (help.buttons[i].button == b) {
      return help.buttons[i].text != nullptr;
    }
  }
  return false;
}
}  // namespace

TEST_CASE("sample: SIDE refuses an empty slot and changes nothing") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.open();
  tap(h, app::Button::F2);  // focus SIDE
  tap(h, app::Button::F2);  // step: SPEAKS, but the slot is empty
  h.run_ms(60);
  CHECK(unit_param(h, kModSrc) != doctest::Approx(3.0f));
  CHECK(unit_param(h, kCarSrc) != doctest::Approx(3.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: SIDE, SPEED, DIR, LEVEL and LOOP reach the unit") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  const auto slot1 = [&h](Vocoder::LoopField f) { return unit_param(h, Vocoder::loop_param(0, f)); };
  CHECK(slot1(Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  tap(h, app::Button::F5);  // LOOP on, from the bar
  h.run_ms(5000);
  CHECK(slot1(Vocoder::LoopField::Play) == doctest::Approx(1.0f));
  tap(h, app::Button::F5);  // and off
  h.run_ms(300);
  CHECK(slot1(Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  tap(h, app::Button::F2);  // focus SIDE
  tap(h, app::Button::F2);  // SPEAKS
  h.run_ms(60);
  CHECK(unit_param(h, kModSrc) == doctest::Approx(3.0f));
  CHECK(slot1(Vocoder::LoopField::Play) == doctest::Approx(1.0f));  // a slot on a side plays
  tap(h, app::Button::F2);  // SOUNDS: a move
  h.run_ms(60);
  CHECK(unit_param(h, kCarSrc) == doctest::Approx(3.0f));
  CHECK(unit_param(h, kModSrc) != doctest::Approx(3.0f));
  tap(h, app::Button::F2);  // NONE
  h.run_ms(60);
  CHECK(unit_param(h, kCarSrc) != doctest::Approx(3.0f));
  CHECK(slot1(Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  tap(h, app::Button::F3);  // focus SPEED
  tap(h, app::Button::F3);  // a tap wraps: 1X to 2X
  h.run_ms(60);
  CHECK(slot1(Vocoder::LoopField::Speed) == doctest::Approx(2.0f));
  tap(h, app::Button::F4);  // DIR
  h.run_ms(60);
  CHECK(slot1(Vocoder::LoopField::Dir) == doctest::Approx(1.0f));
  tap(h, app::Button::F8);  // focus LEVEL
  tap(h, app::Button::F8);  // a tap moves 3 dB
  h.run_ms(60);
  CHECK(slot1(Vocoder::LoopField::Level) == doctest::Approx(3.0f));
  CHECK(h.runtime().frame_failures() == 0u);
  CHECK(h.display().peak_frame_words() < 2048u);
}

TEST_CASE("sample: UP and DOWN pick the slot the keys write to") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.open();
  tap(h, app::Button::Down);
  tap(h, app::Button::F4);  // DIR of slot 2
  h.run_ms(60);
  CHECK(unit_param(h, Vocoder::loop_param(1, Vocoder::LoopField::Dir)) == doctest::Approx(1.0f));
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Dir)) == doctest::Approx(0.0f));
}

TEST_CASE("sample: HELP gives a row to every button the screen answers") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.open();
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
  for (const app::Button b : {app::Button::Up, app::Button::Down, app::Button::Left, app::Button::Right, app::Button::Shift, app::Button::Delete,
                              app::Button::Rec, app::Button::Params, app::Button::Performance, app::Button::Play, app::Button::Master,
                              app::Button::File, app::Button::SampleLoad, app::Button::SamplePlay, app::Button::SampleEdit,
                              app::Button::SampleRec, app::Button::Song, app::Button::Pattern, app::Button::Note, app::Button::Instr,
                              app::Button::Vol, app::Button::Fx}) {
    INFO(std::string(app::button_name(b)));
    CHECK(has_row(help, b));
  }
  for (unsigned k : {0u, 1u, 2u, 3u, 4u, 7u}) {
    CHECK(help.fkey[k] != nullptr);
  }
  CHECK(help.fkey[5] == nullptr);
  CHECK(help.fkey[6] == nullptr);
}
