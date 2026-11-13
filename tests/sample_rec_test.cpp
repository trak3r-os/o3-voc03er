// SPDX-License-Identifier: GPL-3.0-or-later
// SAMPLE REC: the pure logic (LENGTH, the take end against route::allowed, bars and seconds) and the screen on the engine (arm, record, stop, the slot
// plays, DELETE, HELP, the display list).
#include <doctest/doctest.h>

#include "rec_take.h"
#include "tract_skip.h"

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
#include <cstdlib>
#include <cstring>
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

TEST_CASE("sample_rec: a take ends as AUTOPLAY and the route say, and a take never goes to SOUNDS by itself") {
  route::Route cur;  // SPEAKS and SOUNDS on their defaults, no slot on either side
  CHECK(sr::take_end(cur, true, 1) == sr::TakeEnd::SpeaksNow);
  CHECK(sr::take_end(cur, false, 1) == sr::TakeEnd::Waits);
  CHECK(sr::take_end(cur, true, route::kSlots) == sr::TakeEnd::Waits);  // no such slot
  route::Route on_speaks = cur;
  on_speaks.speaks = route::kFirstSlot + 2;
  CHECK(sr::take_end(on_speaks, true, 2) == sr::TakeEnd::PlaysSpeaks);
  CHECK(sr::take_end(on_speaks, false, 2) == sr::TakeEnd::Waits);
  route::Route on_sounds = cur;
  on_sounds.sounds = route::kFirstSlot + 3;
  CHECK(sr::take_end(on_sounds, true, 3) == sr::TakeEnd::PlaysSounds);  // plays where it is: SOUNDS stays
  CHECK(sr::take_end(on_sounds, true, 0) == sr::TakeEnd::SpeaksNow);    // another slot is free to speak
  // The route a take leaves: only SpeaksNow moves it, and never SOUNDS.
  route::Route to = sr::take_end_route(cur, sr::TakeEnd::SpeaksNow, 1);
  CHECK(to.speaks == route::kFirstSlot + 1);
  CHECK(to.sounds == cur.sounds);
  for (const auto e : {sr::TakeEnd::Waits, sr::TakeEnd::PlaysSpeaks, sr::TakeEnd::PlaysSounds, sr::TakeEnd::Refused}) {
    to = sr::take_end_route(on_sounds, e, 3);
    CHECK(to.speaks == on_sounds.speaks);
    CHECK(to.sounds == on_sounds.sounds);
  }
}

TEST_CASE("sample_rec: a LENGTH of bars fits the 16 s buffer from its tempo up, FREE always") {
  CHECK(pl::fits(8, 120000));  // 8 bars at 120 BPM are exactly 16 s: the stop line is the last sample
  CHECK_FALSE(pl::fits(8, 119999));
  CHECK_FALSE(pl::fits(8, 100000));
  CHECK(pl::fits(8, 300000));
  CHECK(pl::fits(4, 60000));
  CHECK_FALSE(pl::fits(4, 59999));
  CHECK(pl::fits(4, 120000));
  CHECK(pl::fits(2, 30000));
  CHECK_FALSE(pl::fits(2, 29999));
  CHECK(pl::fits(1, 20000));
  CHECK(pl::fits(0, 20000));  // FREE: there is no length to fit
  CHECK(pl::fits(0, 0));
  CHECK(pl::fits(1, 0));  // a stopped or odd tempo reads 20 BPM, as bar_samples does
  CHECK_FALSE(pl::fits(2, 0));
  for (unsigned i = 0; i < sr::kLengthCount; ++i) {  // agrees with the float bar the screen stops at
    const unsigned bars = sr::kLengthBars[i];
    for (unsigned bpm = 20; bpm <= 300; ++bpm) {
      const float len = static_cast<float>(bars) * pl::bar_samples(bpm * 1000u);
      if (bars != 0u && std::fabs(len - static_cast<float>(o3_voc03er::Loop::kMaxSamples)) > 2.0f) {
        CHECK(pl::fits(bars, bpm * 1000u) == (len < static_cast<float>(o3_voc03er::Loop::kMaxSamples)));
      }
    }
  }
}

TEST_CASE("sample_rec: a take in flight is given up only a real margin under the edge, REC arms at the exact edge (FOLLOW jitter at LENGTH 8 and 120.000 BPM)") {
  CHECK(pl::fits(8, 120000));        // REC arms at the exact edge
  CHECK(pl::still_fits(8, 119999));  // jitter below it does not cancel an arm or stop a take
  CHECK(pl::still_fits(8, 119500));
  CHECK_FALSE(pl::still_fits(8, 119499));  // 0.5 BPM under the edge: given up
  CHECK(pl::still_fits(4, 59500));
  CHECK_FALSE(pl::still_fits(4, 59499));
  CHECK(pl::still_fits(2, 29500));
  CHECK_FALSE(pl::still_fits(2, 29499));
  CHECK(pl::still_fits(0, 0));  // FREE always
  // a jittering tempo around the edge: no verdict flips, whichever way it wanders within the margin
  unsigned flips = 0;
  bool was = pl::still_fits(8, 120000);
  for (unsigned mbpm = 119600u; mbpm <= 120400u; mbpm += 7u) {
    const bool now = pl::still_fits(8, mbpm);
    flips += now != was ? 1u : 0u;
    was = now;
  }
  CHECK(flips == 0u);
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
    if (o3_voc03er::test::is_tract_unit(h, u)) continue;
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
  o3_voc03er::test::rec_length_one_bar(h);  // F3 lights LENGTH, LEFT to FREE, RIGHT to 1 BAR: the same whatever the test before left
  tap(h, app::Button::Rec);
  h.run_ms(300);
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(1.0f));
  h.run_ms(9000);
  CHECK(unit_param(h, kModSrc) == doctest::Approx(3.0f));  // SPEAKS holds SLOT 1
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Play)) == doctest::Approx(1.0f));
  CHECK(h.runtime().frame_failures() == 0u);
  CHECK(h.display().peak_frame_words() < 2048u);
}

namespace {
// The app is one static object for the whole test process, so the screen keeps LENGTH and the lit key from the test before: "fresh" is a tap on F3 (LENGTH lit)
// and LEFT, which stops at FREE.
void length_to_free(sim::Harness& h) {
  o3_voc03er::test::rec_length_free(h);
}
}  // namespace

TEST_CASE("sample_rec: one RIGHT from FREE sets LENGTH to 1 BAR, as the F3 tap does (LE7: the press works, the screen state outlives a test)") {
  Rig rig;
  sim::Harness& h = *rig.h;
  REQUIRE_FALSE(h.engine().playing());
  length_to_free(h);
  tap(h, app::Button::Right);  // the first press, no pause before REC: LENGTH 1 BAR, not FREE and not 2
  tap(h, app::Button::Rec);
  h.run_ms(300);
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(1.0f));
  h.run_ms(9000);  // a FREE take would still be recording: only a bar-length take ends by itself
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(0.0f));
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Play)) == doctest::Approx(1.0f));
  char line[1200];
  const std::string text(line, h.runtime().state_line(line, sizeof line));
  const std::size_t at = text.find("loop1=");
  REQUIRE(at != std::string::npos);
  CHECK(text.substr(at, text.find(' ', at) - at) == "loop1=88200");  // 1 bar at 120 BPM, 44.1 kHz
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample_rec: LEFT / RIGHT step the lit key: LENGTH after F3, and nothing else moves (the encoder's step, as the encoder turn makes it)") {
  Rig rig;
  sim::Harness& h = *rig.h;
  REQUIRE_FALSE(h.engine().playing());
  length_to_free(h);  // F3 lit, FREE
  const unsigned gain = rig.cap.gain();
  tap(h, app::Button::Right);
  tap(h, app::Button::Right);  // 2 BARS
  tap(h, app::Button::Left);   // 1 BAR
  CHECK(rig.cap.gain() == gain);  // the arrows are not GAIN's while F3 is lit
  tap(h, app::Button::Rec);
  h.run_ms(9500);  // only a bar-length take ends by itself; 2 BARS would still be recording
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(0.0f));
  char line[1200];
  const std::string text(line, h.runtime().state_line(line, sizeof line));
  const std::size_t at = text.find("loop1=");
  REQUIRE(at != std::string::npos);
  CHECK(text.substr(at, text.find(' ', at) - at) == "loop1=88200");
}

TEST_CASE("sample_rec: after a tap on F2 the arrows step GAIN by 1, SHIFT by 5, and the tap itself changes nothing the first time") {
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::F1);  // lights SOURCE (or steps it on, when it was lit): a known other key
  tap(h, app::Button::F2);  // lights GAIN: a tap on a key that is not lit changes nothing
  const unsigned g0 = rig.cap.gain();
  REQUIRE(g0 <= 90u);
  tap(h, app::Button::Right);
  CHECK(rig.cap.gain() == g0 + 1u);
  tap(h, app::Button::Right);
  CHECK(rig.cap.gain() == g0 + 2u);
  tap(h, app::Button::Left);
  CHECK(rig.cap.gain() == g0 + 1u);
  h.press(app::Button::Shift);
  h.run_ms(5);
  tap(h, app::Button::Right);  // SHIFT + RIGHT: the coarse step
  h.release(app::Button::Shift);
  h.run_ms(5);
  CHECK(rig.cap.gain() == g0 + 6u);
  tap(h, app::Button::F2);  // a second tap on the lit key steps it by 5, as it did
  CHECK(rig.cap.gain() == g0 + 11u);
  tap(h, app::Button::Up);  // UP / DOWN stay the slot: GAIN does not move
  tap(h, app::Button::Down);
  CHECK(rig.cap.gain() == g0 + 11u);
  o3_voc03er::test::rec_slot(h, 0);  // the slot outlives the test: back to the first
}

TEST_CASE("sample_rec: after a tap on F5 the arrows set MONITOR on (RIGHT) and off (LEFT)") {
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::F1);
  tap(h, app::Button::F5);  // lights MONITOR, changes nothing
  tap(h, app::Button::Left);
  h.run_ms(20);
  CHECK(h.engine().input_monitor() == doctest::Approx(0.0f));
  tap(h, app::Button::Right);
  h.run_ms(20);
  CHECK(h.engine().input_monitor() > 0.0f);
  tap(h, app::Button::Right);  // already on: stays on
  h.run_ms(20);
  CHECK(h.engine().input_monitor() > 0.0f);
  tap(h, app::Button::Left);
  h.run_ms(20);
  CHECK(h.engine().input_monitor() == doctest::Approx(0.0f));
}

TEST_CASE("sample_rec: F4 AUTOPLAY takes the light as MONITOR does: a tap lights it, a second switches it, RIGHT sets it on and LEFT off") {
  Rig rig;
  sim::Harness& h = *rig.h;
  const auto autoplay = [&h] {
    char line[1500];
    const std::string text(line, h.runtime().state_line(line, sizeof line));
    const std::size_t at = text.find("rec_autoplay=");
    return at == std::string::npos ? -1 : std::atoi(text.c_str() + at + 13);
  };
  tap(h, app::Button::F1);  // the light is elsewhere
  const int was = autoplay();
  REQUIRE(was >= 0);
  tap(h, app::Button::F4);  // lights AUTOPLAY, changes nothing
  CHECK(autoplay() == was);
  tap(h, app::Button::F4);  // a second tap switches it
  CHECK(autoplay() == 1 - was);
  tap(h, app::Button::Left);
  CHECK(autoplay() == 0);
  tap(h, app::Button::Left);  // already off: stays off
  CHECK(autoplay() == 0);
  tap(h, app::Button::Right);
  CHECK(autoplay() == 1);
  tap(h, app::Button::Right);  // already on: stays on
  CHECK(autoplay() == 1);
  h.press(app::Button::Shift);  // SHIFT passes as the encoder passes it: AUTOPLAY has no coarse step
  tap(h, app::Button::Left);
  h.release(app::Button::Shift);
  CHECK(autoplay() == 0);
  h.turn(1);  // the encoder sets it as the arrows do
  h.run_ms(20);
  CHECK(autoplay() == 1);
  h.turn(-1);
  h.run_ms(20);
  CHECK(autoplay() == 0);
  o3_voc03er::test::rec_autoplay(h, true);  // the screen outlives the test
  CHECK(autoplay() == 1);
}

TEST_CASE("sample_rec: a held arrow repeats on the lit key (GAIN climbs by more than one step), and the release stops it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::F1);
  tap(h, app::Button::F2);  // lights GAIN
  const unsigned g0 = rig.cap.gain();
  const bool up = g0 < 50u;
  const app::Button b = up ? app::Button::Right : app::Button::Left;
  h.press(b);
  h.run_ms(1500);  // 400, 300, 200, 150, 100 ms, then 80 ms apart
  h.release(b);
  h.run_ms(5);
  const unsigned g1 = rig.cap.gain();
  CHECK((up ? g1 - g0 : g0 - g1) >= 5u);
  h.run_ms(1000);
  CHECK(rig.cap.gain() == g1);  // released: nothing more
}

TEST_CASE("sample_rec: REC again stops a free take, AUTOPLAY OFF leaves the slot on no side, DELETE empties it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  length_to_free(h);  // FREE
  o3_voc03er::test::rec_autoplay(h, false);  // AUTOPLAY OFF: the take waits in the slot
  tap(h, app::Button::Rec);
  h.run_ms(2500);
  tap(h, app::Button::Rec);  // stops at the bar line
  h.run_ms(5000);
  CHECK(unit_param(h, kModSrc) != doctest::Approx(3.0f));
  CHECK(unit_param(h, kCarSrc) != doctest::Approx(3.0f));
  o3_voc03er::test::rec_autoplay(h, true);  // AUTOPLAY back ON (the screen outlives the test)
  tap(h, app::Button::F6);  // SLOT: focus, then DOWN / UP move the slot
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
  for (unsigned k = 0; k < 6; ++k) {
    CHECK(help.fkey[k] != nullptr);
  }
}

TEST_CASE("sample_rec: a 2 BAR take with the transport stopped is exactly 2 bars long, not a block past it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  REQUIRE_FALSE(h.engine().playing());
  REQUIRE(h.engine().stats().tempo_mbpm == 120000u);
  length_to_free(h);
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
  o3_voc03er::test::rec_length_one_bar(h);  // F3 lights LENGTH, LEFT to FREE, RIGHT to 1 BAR: the same whatever the test before left
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
  o3_voc03er::test::rec_length_one_bar(h);  // F3 lights LENGTH, LEFT to FREE, RIGHT to 1 BAR: the same whatever the test before left
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

TEST_CASE("sample_rec: DELETE while a take is stopping (REC pressed, the bar line not reached) still throws it away") {
  Rig rig;
  sim::Harness& h = *rig.h;
  o3_voc03er::test::rec_length_one_bar(h);  // F3 lights LENGTH, LEFT to FREE, RIGHT to 1 BAR: the same whatever the test before left
  h.run_ms(60);
  tap(h, app::Button::Play);  // the transport runs: a stop waits for the bar line (up to 2 s)
  h.run_ms(100);
  REQUIRE(h.engine().playing());
  tap(h, app::Button::Rec);  // armed, then records from the next bar line
  h.run_ms(2300);            // past that bar line, inside the 1 BAR take
  REQUIRE(slot1_state(h) != "loop1=0");
  tap(h, app::Button::Rec);  // stop: the unit ends the take at the bar line
  h.run_ms(40);
  tap(h, app::Button::Delete);  // before the bar line: the take is thrown away, not kept
  h.run_ms(4500);
  CHECK(slot1_state(h) == "loop1=0");
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Play)) == doctest::Approx(0.0f));
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(0.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample_rec: a LENGTH that does not fit the buffer at the tempo is refused at REC, and fits again at 120 BPM") {
  Rig rig;
  sim::Harness& h = *rig.h;
  REQUIRE(h.engine().set_tempo(100000u));  // 8 bars at 100 BPM are 19.2 s: more than the 16 s a slot holds
  h.run_ms(60);
  length_to_free(h);
  for (int i = 0; i < 4; ++i) {
    tap(h, app::Button::Right);  // 1, 2, 4, 8 BARS
  }
  tap(h, app::Button::Rec);
  h.run_ms(300);
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(0.0f));  // nothing armed
  CHECK(slot1_state(h) == "loop1=0");
  tap(h, app::Button::Left);  // 4 BARS: 9.6 s at 100 BPM
  tap(h, app::Button::Rec);
  h.run_ms(300);
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(1.0f));  // armed
  tap(h, app::Button::Rec);  // cancel
  h.run_ms(100);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample_rec: a tempo change under a recording take that no longer fits ends it at the next bar line, shorter than LENGTH") {
  Rig rig;
  sim::Harness& h = *rig.h;
  length_to_free(h);
  for (int i = 0; i < 3; ++i) {
    tap(h, app::Button::Right);  // 1, 2, 4 BARS
  }
  tap(h, app::Button::Play);
  h.run_ms(100);
  REQUIRE(h.engine().playing());
  tap(h, app::Button::Rec);  // armed, then records from the next bar line (at 2 s)
  h.run_ms(2500);
  REQUIRE(slot1_state(h) != "loop1=0");
  REQUIRE(h.engine().set_tempo(50000u));  // 4 bars at 50 BPM are 19.2 s
  h.run_ms(7000);                         // the take ends at the next line, 2.4 s on, instead of at 4 bars
  CHECK(unit_param(h, Vocoder::loop_param(0, Vocoder::LoopField::Rec)) == doctest::Approx(0.0f));
  const std::string s = slot1_state(h);
  REQUIRE(s.size() > 6u);
  const unsigned long len = std::stoul(s.substr(6));
  CHECK(len > 88200u / 2u);
  CHECK(len < 2u * 88200u + 88200u / 2u);  // a bar and a half of audio, nowhere near four bars or the full buffer
  CHECK(h.runtime().frame_failures() == 0u);
}

// What the screen says of a take: before it (the plan) and when it ends (the done line); the strings are the screen's and fit its width.
TEST_CASE("sample rec: the take's lines say what will happen and what did") {
  CHECK(std::string(sr::take_end_plan_text(sr::TakeEnd::SpeaksNow)) == "THE TAKE WILL SPEAK");
  CHECK(std::string(sr::take_end_plan_text(sr::TakeEnd::PlaysSpeaks)) == "THE TAKE WILL SPEAK");
  CHECK(std::string(sr::take_end_plan_text(sr::TakeEnd::PlaysSounds)) == "THE TAKE WILL PLAY ON SOUNDS");
  CHECK(std::string(sr::take_end_plan_text(sr::TakeEnd::Waits)) == "THE TAKE WILL WAIT IN THE SLOT");
  CHECK(std::string(sr::take_end_done_text(sr::TakeEnd::SpeaksNow)) == "PLAYING ON SPEAKS");
  CHECK(std::string(sr::take_end_done_text(sr::TakeEnd::PlaysSpeaks)) == "PLAYING ON SPEAKS");
  CHECK(std::string(sr::take_end_done_text(sr::TakeEnd::PlaysSounds)) == "PLAYING ON SOUNDS");
  CHECK(std::string(sr::take_end_done_text(sr::TakeEnd::Waits)) == "TAKEN, NOT PLAYING");
  for (const auto e : {sr::TakeEnd::Waits, sr::TakeEnd::PlaysSpeaks, sr::TakeEnd::PlaysSounds, sr::TakeEnd::SpeaksNow, sr::TakeEnd::Refused}) {
    CHECK(std::strlen(sr::take_end_plan_text(e)) <= 40u);
    CHECK(std::strlen(sr::take_end_done_text(e)) <= 40u);
  }
}
