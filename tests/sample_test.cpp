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

TEST_CASE("sample: even cuts tile the slot, 8 by default, fewer for a slot shorter than that") {
  sm::SliceTable t;
  sm::even_cuts(t, 8000);
  CHECK(t.count == 8u);
  CHECK(t.length == 8000u);
  std::uint32_t end = 0;
  for (unsigned i = 0; i < t.count; ++i) {
    CHECK(t.cut[i].start == end);  // no gap, no overlap
    CHECK(t.cut[i].len > 0u);
    end = t.cut[i].start + t.cut[i].len;
  }
  CHECK(end == 8000u);
  sm::even_cuts(t, 5);  // shorter than the cut count: one sample each
  CHECK(t.count == 5u);
  CHECK(t.cut[4].start == 4u);
  CHECK(t.cut[4].len == 1u);
  sm::even_cuts(t, 1003, 30);  // never past the table
  CHECK(t.count == sm::kMaxSlices);
  CHECK(t.cut[t.count - 1].start + t.cut[t.count - 1].len == 1003u);
  sm::even_cuts(t, 0);
  CHECK(t.count == 0u);
  CHECK(sm::region_start(2, 705600u, t.cut[0]) == 2u * 705600u);
  CHECK(sm::bar_x(50, 100, 200) == 100u);
  CHECK(sm::bar_x(500, 100, 200) == 200u);
  CHECK(sm::bar_x(1, 0, 200) == 0u);
  // not seen yet: the unit's flag lags, so a grace
  CHECK_FALSE(sm::region_ended(false, false, 10));
  CHECK(sm::region_ended(false, false, 100));
  CHECK(sm::region_ended(true, false, 0));
  CHECK_FALSE(sm::region_ended(true, true, 500));
}

TEST_CASE("sample: pick_reader, CHOKE ON and OFF") {
  sm::ReaderState r[4] = {};
  CHECK(sm::pick_reader(r, 1, true) == 1);
  CHECK(sm::pick_reader(r, 1, false) == 1);
  r[1].region = true;
  CHECK(sm::pick_reader(r, 1, true) == 1);  // CHOKE ON: the own reader, always
  CHECK(sm::pick_reader(r, 1, false) == 0);  // OFF: a free one
  r[0].loop_on = true;  // a whole loop of another slot is never taken
  r[2].recording = true;
  CHECK(sm::pick_reader(r, 1, false) == 3);
  r[3].region = true;
  r[3].seq = 5;
  r[1].seq = 9;
  CHECK(sm::pick_reader(r, 1, false) == 3);  // all busy: the oldest slice voice goes
  r[1].seq = 2;
  CHECK(sm::pick_reader(r, 1, false) == 1);
  r[1].recording = true;  // a recording reader is never taken, the own one included
  CHECK(sm::pick_reader(r, 1, true) == -1);
  CHECK(sm::pick_reader(r, 1, false) == -1);
  CHECK(sm::pick_reader(r, 7, true) == -1);
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

TEST_CASE("sample: a slice never borrows a reader whose slot is on a side, even a stopped one") {
  sm::ReaderState r[sm::kReaders];
  r[1].region = true;  // CHOKE OFF, the own reader is busy, so the slice must borrow
  r[0].sided = true;   // the unit routes reader 0 by CarSrc or ModSrc: the slice would land on the wrong bus
  CHECK(sm::pick_reader(r, 1, false) == 2);
  r[2].sided = true;
  r[3].sided = true;
  CHECK(sm::pick_reader(r, 1, false) == 1);  // nothing to borrow: the own slice is stolen
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
                              app::Button::Params, app::Button::Performance, app::Button::Play, app::Button::Master,
                              app::Button::File, app::Button::SampleLoad, app::Button::SamplePlay, app::Button::SampleEdit,
                              app::Button::SampleRec, app::Button::Song, app::Button::Pattern, app::Button::Note, app::Button::Instr,
                              app::Button::Vol, app::Button::Fx}) {
    INFO(std::string(app::button_name(b)));
    CHECK(has_row(help, b));
  }
  for (unsigned k : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u}) {
    CHECK(help.fkey[k] != nullptr);
  }
  CHECK(help.fkey[5] != nullptr);
  CHECK(help.fkey[6] != nullptr);
  CHECK(help.pad_group[0] != 0u);  // the slices have a group and a label
  CHECK(help.pad_label[23] != nullptr);
}

namespace {
float rp(sim::Harness& h, unsigned reader, Vocoder::RegionField f) { return unit_param(h, Vocoder::region_param(reader, f)); }
float lp(sim::Harness& h, unsigned reader, Vocoder::LoopField f) { return unit_param(h, Vocoder::loop_param(reader, f)); }
}  // namespace

TEST_CASE("sample: a slice pad triggers its cut and plays it to the end (ONE SHOT, the default); GATE stops at the release") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  h.pad_down(2);  // slice 3 of slot 1, on its own reader (CHOKE ON)
  h.run_ms(40);
  const float len = rp(h, 0, Vocoder::RegionField::Len);
  CHECK(len > 0.0f);
  CHECK(rp(h, 0, Vocoder::RegionField::Start) == doctest::Approx(len * 2.0f).epsilon(0.01));  // even cuts: the third starts at 2 lengths
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(1.0f));
  h.pad_up(2);
  h.run_ms(40);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(1.0f));  // ONE SHOT: the release is ignored
  h.run_ms(600);
  tap(h, app::Button::F6);  // TRIG: GATE
  h.pad_down(3);
  h.run_ms(40);
  h.pad_up(3);
  h.run_ms(40);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(0.0f));  // the release stops it
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: a retrigger restarts from the slice's start cut, and a hit fades the slot's other slice") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  h.pad_down(5);
  h.run_ms(40);
  h.pad_up(5);
  const float len = rp(h, 0, Vocoder::RegionField::Len);
  CHECK(rp(h, 0, Vocoder::RegionField::Start) == doctest::Approx(len * 5.0f).epsilon(0.01));
  h.run_ms(60);  // mid-slice
  h.pad_down(1);  // another slice of the slot: it takes the reader
  h.run_ms(40);
  h.pad_up(1);
  CHECK(rp(h, 0, Vocoder::RegionField::Start) == doctest::Approx(len).epsilon(0.01));
  h.run_ms(60);
  h.pad_down(5);  // slice 6 again, mid-slice of slice 2: restarts at slice 6's own start
  h.run_ms(40);
  CHECK(rp(h, 0, Vocoder::RegionField::Start) == doctest::Approx(len * 5.0f).epsilon(0.01));
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(1.0f));
  h.pad_up(5);
  tap(h, app::Button::F7);  // CHOKE OFF: the slices overlap on two readers
  h.pad_down(2);
  h.run_ms(40);
  h.pad_up(2);
  h.pad_down(3);
  h.run_ms(40);
  h.pad_up(3);
  unsigned live = 0;
  for (unsigned k = 0; k < 4u; ++k) {
    live += rp(h, k, Vocoder::RegionField::Trig) > 0.5f ? 1u : 0u;
  }
  CHECK(live >= 2u);
  tap(h, app::Button::F7);  // CHOKE ON: a hit fades every other slice of the slot, only the new one is held
  h.pad_down(4);
  h.run_ms(40);
  h.pad_up(4);
  live = 0;
  for (unsigned k = 0; k < 4u; ++k) {
    live += rp(h, k, Vocoder::RegionField::Trig) > 0.5f ? 1u : 0u;
  }
  CHECK(live == 1u);
  CHECK(rp(h, 0, Vocoder::RegionField::Start) == doctest::Approx(len * 4.0f).epsilon(0.01));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: a slice turns the slot's loop off, and the loop does not come back when the slice ends") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  tap(h, app::Button::F5);  // LOOP on (a flip)
  h.run_ms(2500);           // the bar comes and the loop plays
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(1.0f));
  h.pad_down(5);
  h.run_ms(40);
  h.pad_up(5);
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(0.0f));  // LOOP reads OFF: the pads own the slot
  h.run_ms(3000);  // the slice ended long ago
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  tap(h, app::Button::F5);  // F5 starts it again
  h.run_ms(60);
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(1.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: a borrowed reader gets its own values and role back; SAMPLE keeps slot k's edits") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  const float role1 = lp(h, 1, Vocoder::LoopField::Role);
  tap(h, app::Button::F7);  // CHOKE OFF
  tap(h, app::Button::F3);  // focus SPEED
  tap(h, app::Button::F3);  // 2X
  tap(h, app::Button::F3);  // 0.5X: the slice lasts 500 ms, long enough for the steps below
  h.run_ms(60);
  h.pad_down(0);  // the own reader is busy with a slice, so the next one goes to a free reader
  h.run_ms(20);
  h.pad_down(1);
  h.run_ms(60);
  CHECK(rp(h, 1, Vocoder::RegionField::Trig) == doctest::Approx(1.0f));  // reader 2, not reader 1
  CHECK(lp(h, 1, Vocoder::LoopField::Speed) == doctest::Approx(0.0f));   // the source slot's speed, written first
  CHECK(rp(h, 1, Vocoder::RegionField::Start) > 0.0f);
  tap(h, app::Button::Down);  // slot 2, whose reader is borrowed
  tap(h, app::Button::F4);    // DIR of slot 2: waits, the reader plays the slice forward
  h.run_ms(60);
  CHECK(lp(h, 1, Vocoder::LoopField::Dir) == doctest::Approx(0.0f));
  tap(h, app::Button::Up);
  h.pad_up(0);
  h.pad_up(1);
  h.run_ms(3000);  // the region ends and the reader comes back
  CHECK(lp(h, 1, Vocoder::LoopField::Speed) == doctest::Approx(1.0f));
  CHECK(lp(h, 1, Vocoder::LoopField::Dir) == doctest::Approx(1.0f));  // slot 2's edit, kept
  CHECK(lp(h, 1, Vocoder::LoopField::Role) == doctest::Approx(role1));
  CHECK(lp(h, 0, Vocoder::LoopField::Speed) == doctest::Approx(0.0f));  // slot 1 untouched
  CHECK(h.runtime().frame_failures() == 0u);
  CHECK(h.display().peak_frame_words() < 2048u);
}
