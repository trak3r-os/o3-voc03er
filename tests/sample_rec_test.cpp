// SPDX-License-Identifier: GPL-3.0-or-later
// SAMPLE REC: the pure logic (LENGTH, AFTER against route::allowed, bars and seconds) and the screen on the engine (arm, record, stop, the slot
// plays, DELETE, HELP, the display list).
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/platform/host/fake_capture.h>
#include <o3sdk/sim/harness.h>

#include <o3_voc03er/route.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include "../app/src/perform_logic.h"
#include "../app/src/sample_rec_logic.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace sr = o3_voc03er::sample_rec;
namespace route = o3_voc03er::route;
namespace pl = o3_voc03er::perform;
using o3_voc03er::Vocoder;

TEST_CASE("sample_rec: LENGTH steps stop at FREE and 8 BARS") {
  CHECK(sr::step_length(0, -1) == 0u);
  CHECK(sr::step_length(0, 1) == 1u);
  CHECK(sr::step_length(3, 1) == 4u);
  CHECK(sr::step_length(4, 1) == 4u);
  CHECK(sr::step_length(4, -9) == 0u);
  CHECK_FALSE(sr::auto_stops(0));
  CHECK(sr::auto_stops(1));
  CHECK(sr::kLengthBars[4] == 8u);
}

TEST_CASE("sample_rec: AFTER places the slot where route::allowed lets it, and says when it cannot") {
  route::Route cur;
  sr::AfterPlan p = sr::plan_after(cur, sr::After::Speaks, 1);
  CHECK(p.place);
  CHECK(p.to.speaks == route::kFirstSlot + 1);
  p = sr::plan_after(cur, sr::After::Sounds, 0);
  CHECK(p.place);
  CHECK(p.to.sounds == route::kFirstSlot);
  p = sr::plan_after(cur, sr::After::None, 0);
  CHECK_FALSE(p.place);
  CHECK_FALSE(p.refused);
  route::Route held;
  held.sounds = route::kFirstSlot + 2;  // SOUNDS holds slot 3: SPEAKS cannot have it
  p = sr::plan_after(held, sr::After::Speaks, 2);
  CHECK_FALSE(p.place);
  CHECK(p.refused);
  CHECK(p.to.sounds == held.sounds);
  CHECK(p.to.speaks == held.speaks);
}

TEST_CASE("sample_rec: bars to samples, bars done, seconds and the transport's bar") {
  const float bar = pl::bar_samples(120000);  // 2 s a bar at 120 BPM
  CHECK(bar == doctest::Approx(88200.0f).epsilon(0.001));
  CHECK(pl::capture_stop_at(2, bar, false) == doctest::Approx(2.0f * bar));
  CHECK(pl::capture_stop_at(2, bar, true) == doctest::Approx(1.5f * bar));
  CHECK(sr::bars_done(0, bar) == 0u);
  CHECK(sr::bars_done(88210, bar) == 1u);
  CHECK(sr::bars_done(88190, bar) == 0u);
  CHECK(sr::tenths_seconds(44100) == 10u);
  CHECK(sr::tenths_bars(132300, bar) == 15u);
  CHECK(sr::tenths_bars(100, 0.0f) == 0u);
  const sr::BarBeat b = sr::bar_beat(std::uint64_t{5u * 960u} << 32);
  CHECK(b.bar == 2u);
  CHECK(b.beat == 2u);
}

namespace {
void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(5);
}

struct Rig {
  o3sdk::platform::host::FakeCapture cap;
  std::unique_ptr<sim::Harness> h;
  Rig() {
    cap.input_tone(220.0f, -12.0f);
    h = std::make_unique<sim::Harness>(::o3sdk_app(), nullptr, nullptr, nullptr, nullptr, &cap);
    REQUIRE(h->start());
    h->run_ms(100);
    tap(*h, app::Button::SampleRec);
    h->run_ms(60);
  }
};

float unit_param(sim::Harness& h, std::uint16_t id) {
  float best = 0.0f;
  for (unsigned u = 0; u < 8; ++u) {
    const float v = h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
}

constexpr std::uint16_t kCarSrc = 34, kModSrc = 47;

bool has_row(const app::Help& help, app::Button b) {
  for (std::size_t i = 0; i < help.button_count; ++i) {
    if (help.buttons[i].button == b) {
      return help.buttons[i].text != nullptr;
    }
  }
  return false;
}
}  // namespace

TEST_CASE("sample_rec: a 1 BAR take arms, records, stops by itself and plays the slot looped on SPEAKS") {
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::F3);  // LENGTH is the lit key at start: a tap steps it to 1 BAR
  tap(h, app::Button::Rec);
  h.run_ms(300);
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(1.0f));
  h.run_ms(9000);
  CHECK(unit_param(h, kModSrc) == doctest::Approx(3.0f));  // SPEAKS holds SLOT 1
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Play)) == doctest::Approx(1.0f));
  CHECK(h.runtime().frame_failures() == 0u);
  CHECK(h.display().peak_frame_words() < 2048u);
}

TEST_CASE("sample_rec: REC again stops a free take, AFTER NONE leaves the slot unplaced, DELETE empties it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  // LENGTH down to FREE with LEFT
  tap(h, app::Button::Left);
  tap(h, app::Button::Left);
  tap(h, app::Button::Left);
  tap(h, app::Button::F4);  // focus AFTER
  tap(h, app::Button::F4);  // SOUNDS
  tap(h, app::Button::F4);  // NONE
  tap(h, app::Button::Rec);
  h.run_ms(2500);
  tap(h, app::Button::Rec);  // stops at the bar line
  h.run_ms(5000);
  CHECK(unit_param(h, kModSrc) != doctest::Approx(3.0f));
  CHECK(unit_param(h, kCarSrc) != doctest::Approx(3.0f));
  tap(h, app::Button::F4);  // AFTER back to SPEAKS
  tap(h, app::Button::F7);  // SLOT: focus, then DOWN / UP move the slot
  tap(h, app::Button::Down);
  tap(h, app::Button::Up);
  const auto loop1 = [&h] {  // what the console's state says of slot 1: the unit's own view, the one every screen reads
    char line[600];
    const std::string text(line, h.runtime().state_line(line, sizeof line));
    const std::size_t at = text.find("loop1=");
    return at == std::string::npos ? std::string() : text.substr(at, text.find(' ', at) - at);
  };
  REQUIRE_FALSE(loop1().empty());
  CHECK(loop1() != "loop1=0");  // the take is there
  tap(h, app::Button::Delete);  // nothing placed, so nothing to take off a side
  h.run_ms(100);
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Play)) == doctest::Approx(0.0f));
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(0.0f));  // CLEAR does not stay in a saved project
  CHECK(loop1() == "loop1=0");
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample_rec: HELP gives a row to every button the screen answers") {
  Rig rig;
  sim::Harness& h = *rig.h;
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
  for (const app::Button b : {app::Button::Rec, app::Button::Delete, app::Button::Up, app::Button::Down, app::Button::Left, app::Button::Right,
                              app::Button::Params, app::Button::Performance, app::Button::Play, app::Button::Master, app::Button::File,
                              app::Button::SampleLoad, app::Button::SamplePlay, app::Button::SampleEdit, app::Button::SampleRec,
                              app::Button::Song, app::Button::Pattern, app::Button::Note, app::Button::Instr, app::Button::Vol, app::Button::Fx}) {
    INFO(std::string(app::button_name(b)));
    CHECK(has_row(help, b));
  }
  for (unsigned k = 0; k < 7; ++k) {
    CHECK(help.fkey[k] != nullptr);
  }
}

TEST_CASE("sample_rec: a 2 BAR take with the transport stopped is exactly 2 bars long, not a block past it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  REQUIRE_FALSE(h.engine().playing());
  REQUIRE(h.engine().stats().tempo_mbpm == 120000u);
  tap(h, app::Button::Right);  // LENGTH: 1 BAR
  h.run_ms(60);
  tap(h, app::Button::Right);  // 2 BARS
  h.run_ms(60);
  tap(h, app::Button::Rec);
  h.run_ms(6500);  // 4 s of audio plus the stop and the trim
  const auto loop1 = [&h] {
    char line[1200];
    const std::string text(line, h.runtime().state_line(line, sizeof line));
    const std::size_t at = text.find("loop1=");
    return at == std::string::npos ? std::string() : text.substr(at, text.find(' ', at) - at);
  };
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Play)) == doctest::Approx(1.0f));
  CHECK(unit_param(h, Vocoder::Load) == doctest::Approx(0.0f));  // back to 0: a saved project does not hold it
  CHECK(loop1() == "loop1=176400");
  CHECK(h.runtime().frame_failures() == 0u);
}

namespace {
// What the console's state says of slot 1: the samples it holds ("loop1=0" is empty), the unit's own view, the one every screen reads.
std::string slot1_state(sim::Harness& h) {
  char line[1200];
  const std::string text(line, h.runtime().state_line(line, sizeof line));
  const std::size_t at = text.find("loop1=");
  return at == std::string::npos ? std::string() : text.substr(at, text.find(' ', at) - at);
}
}  // namespace

TEST_CASE("sample_rec: DELETE while a take only waits for the bar line cancels it and keeps the audio, a second DELETE empties the slot") {
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::F3);  // LENGTH is the lit key at start: a tap steps it to 1 BAR
  h.run_ms(60);
  tap(h, app::Button::Rec);    // transport stopped: records at once, stops by itself at the bar
  h.run_ms(4500);
  const std::string held = slot1_state(h);
  REQUIRE_FALSE(held.empty());
  REQUIRE(held != "loop1=0");  // the slot holds a take
  tap(h, app::Button::Play);   // the transport runs: the next take waits for the bar line
  h.run_ms(100);
  REQUIRE(h.engine().playing());
  tap(h, app::Button::Rec);  // ARMED: nothing is recorded yet
  h.run_ms(40);
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(1.0f));
  tap(h, app::Button::Delete);  // cancels as REC does
  h.run_ms(40);
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(0.0f));
  CHECK(slot1_state(h) == held);  // the audio stays
  h.run_ms(5000);                 // past any bar line: nothing records, the take is not kept as a new one
  CHECK(slot1_state(h) == held);
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(0.0f));
  tap(h, app::Button::Delete);  // idle now: empties the slot
  h.run_ms(100);
  CHECK(slot1_state(h) == "loop1=0");
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(0.0f));  // CLEAR does not stay in a saved project
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample_rec: DELETE while recording stops the take at the bar line and throws it away") {
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::F3);  // LENGTH is the lit key at start: a tap steps it to 1 BAR
  h.run_ms(60);
  tap(h, app::Button::Rec);
  h.run_ms(600);  // recording
  REQUIRE(slot1_state(h) != "loop1=0");
  tap(h, app::Button::Delete);
  h.run_ms(4500);
  CHECK(slot1_state(h) == "loop1=0");
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Play)) == doctest::Approx(0.0f));
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(0.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}
