// SPDX-License-Identifier: GPL-3.0-or-later
// SAMPLE: the pure logic (SIDE against route::allowed, SPEED, LEVEL, the pad split) and the screen on the engine (a take, then SIDE, SPEED, DIR,
// LEVEL and LOOP reach the unit; HELP).
#include <doctest/doctest.h>

#include "rec_take.h"
#include "tract_skip.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/platform/host/fake_capture.h>
#include <o3sdk/sim/harness.h>

#include <o3_voc03er/route.h>
#include <o3_voc03er/units/tract/tract.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include "../app/src/sample_logic.h"
#include "../app/src/strip_logic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace sm = o3_voc03er::sample;
namespace route = o3_voc03er::route;
using o3_voc03er::Vocoder;

TEST_CASE("sample: SPEAKS moves a slot between SPEAKS, SOUNDS and PADS ONLY, refuses an empty one") {
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
}

TEST_CASE("sample: F2 SPEAKS positions, the tap target and the encoder step over the slots that hold audio") {
  route::Route r;
  CHECK(sm::speaks_pos(r) == 0u);
  r.speaks = route::kFirstSlot + 2;
  CHECK(sm::speaks_pos(r) == 3u);
  CHECK(sm::tap_speaks_target(r, 2) == sm::Side::None);  // it speaks already: back to LIVE
  CHECK(sm::tap_speaks_target(r, 1) == sm::Side::Speaks);
  CHECK(sm::tap_speaks_target(route::Route{}, 0) == sm::Side::Speaks);
  const unsigned audio = 0b0101u;  // slots 1 and 3 hold audio
  CHECK(sm::step_speaks(0, 1, audio) == 1u);
  CHECK(sm::step_speaks(1, 1, audio) == 3u);  // slot 2 is empty: skipped
  CHECK(sm::step_speaks(3, 1, audio) == 3u);  // stops at the end
  CHECK(sm::step_speaks(3, -1, audio) == 1u);
  CHECK(sm::step_speaks(1, -1, audio) == 0u);
  CHECK(sm::step_speaks(0, -1, audio) == 0u);  // stops at LIVE
  CHECK(sm::step_speaks(0, 1, 0u) == 0u);      // nothing to speak
}

TEST_CASE("sample: F5's value names the repeating slice") {
  char t[12];
  sm::loop_text(false, 0, t);
  CHECK(std::strcmp(t, "OFF") == 0);
  sm::loop_text(true, 0, t);
  CHECK(std::strcmp(t, "ON") == 0);
  sm::loop_text(true, 5, t);
  CHECK(std::strcmp(t, "ON SL 5") == 0);
  sm::loop_text(true, 12, t);
  CHECK(std::strcmp(t, "ON SL 12") == 0);
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
  CHECK_FALSE(sm::layout_pad(0));
  CHECK_FALSE(sm::layout_pad(23));
  CHECK(sm::layout_pad(24));
  CHECK(sm::layout_pad(47));
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

TEST_CASE("sample: rule 1 (a slice pad brings the note) and rule 2 (a lower pad starts the picked slice) are pure decisions") {
  using sm::Side;
  for (unsigned mode = 0; mode < o3_voc03er::kModeCount; ++mode) {
    const bool bands_talk_robot = std::strcmp(o3_voc03er::kModeNames[mode], "BANDS") == 0 || std::strcmp(o3_voc03er::kModeNames[mode], "TALK") == 0 ||
                                  std::strcmp(o3_voc03er::kModeNames[mode], "ROBOT") == 0;
    CHECK(sm::mode_needs_note(mode) == bands_talk_robot);  // not TUNE, not TRACT
    for (const Side side : {Side::Speaks, Side::None}) {
      CHECK(sm::slice_press_brings_note(mode, side, false, false) == bands_talk_robot);
      CHECK_FALSE(sm::slice_press_brings_note(mode, side, true, false));   // a lower pad (or a latched note) is held
      CHECK_FALSE(sm::slice_press_brings_note(mode, side, false, true));   // the sequencer plays its own notes
    }
    CHECK_FALSE(sm::slice_press_brings_note(mode, Side::Sounds, false, false));  // a SOUNDS slot is not a voice
  }
  CHECK_FALSE(sm::mode_needs_note(o3_voc03er::kModeCount));
  // Rule 2: every flag is a veto.
  CHECK(sm::lower_press_starts_slice(Side::Speaks, true, false, false, false));
  CHECK(sm::lower_press_starts_slice(Side::None, true, false, false, false));
  CHECK_FALSE(sm::lower_press_starts_slice(Side::Sounds, true, false, false, false));
  CHECK_FALSE(sm::lower_press_starts_slice(Side::Speaks, false, false, false, false));  // an empty slot
  CHECK_FALSE(sm::lower_press_starts_slice(Side::Speaks, true, true, false, false));    // a slice pad is held
  CHECK_FALSE(sm::lower_press_starts_slice(Side::Speaks, true, false, true, false));    // the loop plays
  CHECK_FALSE(sm::lower_press_starts_slice(Side::Speaks, true, false, false, true));    // a slice repeats
  // The picked slice follows a top pad that exists, and falls back to slice 1.
  CHECK(sm::picked_after(0, 3, 8) == 3u);
  CHECK(sm::picked_after(2, 9, 8) == 2u);  // no such slice: the pick stays
  CHECK(sm::picked_after(2, 0, 0) == 2u);
  CHECK(sm::picked_valid(3, 8) == 3u);
  CHECK(sm::picked_valid(8, 8) == 0u);
  CHECK(sm::picked_valid(5, 0) == 0u);
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
    if (o3_voc03er::test::is_tract_unit(h, u)) continue;
    const float v = h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
}

constexpr std::uint16_t kCarSrc = 34, kModSrc = 47;

// SAMPLE REC takes 1 BAR into slot 1 with AUTOPLAY OFF (the take waits in the slot, on no side), then SAMPLE opens.
struct Rig {
  o3sdk::platform::host::FakeCapture cap;
  std::unique_ptr<sim::Harness> h;
  Rig() {
    cap.input_tone(220.0f, -12.0f);
    h = std::make_unique<sim::Harness>(::o3sdk_app(), nullptr, nullptr, nullptr, nullptr, &cap);
    REQUIRE(h->start());
    h->run_ms(100);
  }
  void take(bool autoplay = false) {
    tap(*h, app::Button::SampleRec);
    h->run_ms(60);
    o3_voc03er::test::rec_length_one_bar(*h);
    o3_voc03er::test::rec_autoplay(*h, autoplay);
    o3_voc03er::test::rec_slot(*h, 0);
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
  tap(h, app::Button::F2);  // SPEAKS acts on the first tap: SLOT 1 is empty, so it refuses
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
  tap(h, app::Button::F5);  // LOOP on, from the bar: a slot on no side is made to speak first (the same route change as a tap on F2)
  h.run_ms(5000);
  CHECK(slot1(Vocoder::LoopField::Play) == doctest::Approx(1.0f));
  CHECK(unit_param(h, kModSrc) == doctest::Approx(3.0f));
  tap(h, app::Button::F5);  // and off: the loop stops, the slot goes on speaking
  h.run_ms(300);
  CHECK(slot1(Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  CHECK(unit_param(h, kModSrc) == doctest::Approx(3.0f));
  tap(h, app::Button::F2);  // it speaks already: SPEAKS goes back to LIVE
  h.run_ms(60);
  CHECK(unit_param(h, kModSrc) != doctest::Approx(3.0f));
  tap(h, app::Button::F2);  // SPEAKS: the first tap makes SLOT 1 speak
  h.run_ms(60);
  CHECK(unit_param(h, kModSrc) == doctest::Approx(3.0f));
  CHECK(slot1(Vocoder::LoopField::Play) == doctest::Approx(1.0f));  // a slot on a side plays
  tap(h, app::Button::F2);  // it speaks already: SPEAKS goes back to LIVE (SOUNDS is PERFORM's)
  h.run_ms(60);
  CHECK(unit_param(h, kModSrc) != doctest::Approx(3.0f));
  CHECK(unit_param(h, kCarSrc) != doctest::Approx(3.0f));
  CHECK(slot1(Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  tap(h, app::Button::F3);  // focus SPEED
  tap(h, app::Button::F3);  // a tap wraps: 1X to 2X
  h.run_ms(60);
  CHECK(slot1(Vocoder::LoopField::Speed) == doctest::Approx(2.0f));
  tap(h, app::Button::F4);  // lights the key (H2)
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
  tap(h, app::Button::F4);  // lights the key (H2)
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
  for (const app::Button b : {app::Button::Up, app::Button::Down, app::Button::Left, app::Button::Right, app::Button::Shift,
                              app::Button::Params, app::Button::Performance, app::Button::Play, app::Button::Master,
                              app::Button::File, app::Button::SampleLoad, app::Button::SamplePlay, app::Button::SampleEdit,
                              app::Button::SampleRec, app::Button::Song, app::Button::Pattern, app::Button::Note, app::Button::Instr,
                              app::Button::Vol, app::Button::Fx}) {
    INFO(std::string(app::button_name(b)));
    CHECK(has_row(help, b));
  }
  for (unsigned k : {0u, 1u, 2u, 3u, 4u, 5u, 7u}) {
    CHECK(help.fkey[k] != nullptr);
  }
  CHECK(help.fkey[6] == nullptr);  // F7 is blank and inert
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
  tap(h, app::Button::F6);  // lights the key (H2)
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
  h.pad_down(2);  // the screen is always MONO: a hit fades every other slice of the slot, only the new one is held
  h.run_ms(40);
  h.pad_up(2);
  h.pad_down(3);
  h.run_ms(40);
  h.pad_up(3);
  h.pad_down(4);
  h.run_ms(40);
  h.pad_up(4);
  unsigned live = 0;
  for (unsigned k = 0; k < 4u; ++k) {
    live += rp(h, k, Vocoder::RegionField::Trig) > 0.5f ? 1u : 0u;
  }
  CHECK(live == 1u);
  CHECK(rp(h, 0, Vocoder::RegionField::Start) == doctest::Approx(len * 4.0f).epsilon(0.01));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: with LOOP off a slice turns nothing on; the loop does not come back when the slice ends") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  h.pad_down(5);
  h.run_ms(40);
  h.pad_up(5);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(1.0f));  // a plain slice: once, never a repeat
  h.run_ms(3000);                                                         // the slice ended long ago
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  tap(h, app::Button::F5);  // F5 starts the loop
  h.run_ms(2500);
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(1.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: LOOP ON and a slice pad repeats that slice; GATE goes back to the whole loop on the release; F5 OFF stops it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  tap(h, app::Button::F5);  // LOOP on
  h.run_ms(2500);
  REQUIRE(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(1.0f));
  h.pad_down(5);  // ONE SHOT: it repeats until another pad or F5
  h.run_ms(40);
  h.pad_up(5);
  h.run_ms(40);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(2.0f));  // REPEAT, and the release does not end it
  const float start5 = rp(h, 0, Vocoder::RegionField::Start);
  h.pad_down(2);  // another pad: the repeat moves there, one repeating slice per slot
  h.run_ms(40);
  h.pad_up(2);
  h.run_ms(40);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(2.0f));
  CHECK(rp(h, 0, Vocoder::RegionField::Start) != doctest::Approx(start5));
  tap(h, app::Button::F5);  // OFF: everything of the slot stops
  h.run_ms(60);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(0.0f));
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  tap(h, app::Button::F5);  // ON again
  h.run_ms(2500);
  REQUIRE(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(1.0f));
  tap(h, app::Button::F6);  // lights the key (H2)
  tap(h, app::Button::F6);  // TRIG: GATE
  h.pad_down(3);
  h.run_ms(40);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(2.0f));  // it repeats while held
  h.pad_up(3);
  h.run_ms(60);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(0.0f));  // the release lets go ...
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(1.0f));    // ... and the slot's whole loop goes on
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: the screen is always MONO: a second slice cuts the first on the slot's own reader; slot k's edits stay") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  const float role1 = lp(h, 1, Vocoder::LoopField::Role);
  tap(h, app::Button::F3);  // focus SPEED
  tap(h, app::Button::F3);  // 2X
  tap(h, app::Button::F3);  // 0.5X: the slice lasts 500 ms, long enough for the steps below
  h.run_ms(60);
  h.pad_down(0);
  h.run_ms(20);
  h.pad_down(1);  // MONO: the own reader again, never a borrowed one
  h.run_ms(60);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(1.0f));  // slot 1's own reader
  CHECK(rp(h, 1, Vocoder::RegionField::Trig) == doctest::Approx(0.0f));  // reader 2 untouched
  CHECK(lp(h, 0, Vocoder::LoopField::Speed) == doctest::Approx(0.0f));
  tap(h, app::Button::Down);  // slot 2
  tap(h, app::Button::F4);  // lights the key (H2)
  tap(h, app::Button::F4);    // DIR of slot 2 (the slot is empty: the key still reaches the unit's field)
  h.run_ms(60);
  CHECK(lp(h, 1, Vocoder::LoopField::Dir) == doctest::Approx(1.0f));  // REV: slot 2's own reader takes it at once (MONO never borrows it)
  tap(h, app::Button::Up);
  h.pad_up(0);
  h.pad_up(1);
  h.run_ms(3000);
  CHECK(lp(h, 1, Vocoder::LoopField::Speed) == doctest::Approx(1.0f));
  CHECK(lp(h, 1, Vocoder::LoopField::Role) == doctest::Approx(role1));
  CHECK(lp(h, 0, Vocoder::LoopField::Speed) == doctest::Approx(0.0f));  // slot 1 untouched
  CHECK(h.runtime().frame_failures() == 0u);
  CHECK(h.display().peak_frame_words() < 2048u);
}

// ---- the lower pad rows play PERFORM's layout rows A and B (Q7) -------------------------------------------------------------------
// The observable is the output energy (as tests/pattern_audition_test.cpp): the vocoder speaks while it hears the tone on the fake capture and a carrier note is held.

namespace {
constexpr double kSound = 1e-4;     // above: a note is sounding
constexpr unsigned kRowA = 24;      // the first pad of layout row A on SAMPLE (pads 24..35); row B is 36..47
constexpr unsigned kRowB = 36;

struct LayoutRig {
  Rig rig;
  sim::Harness& h;
  LayoutRig() : h(*rig.h) {
    h.record(true);
    rig.open();
  }
  unsigned led(unsigned p) const { return h.leds().level(p); }
  unsigned count(unsigned from, unsigned to, unsigned level) const {
    unsigned n = 0;
    for (unsigned p = from; p <= to; ++p) {
      n += led(p) == level ? 1u : 0u;
    }
    return n;
  }
  double listen(unsigned ms) {
    const std::size_t from = h.audio().size();
    h.run_ms(ms);
    const std::vector<std::int16_t>& a = h.audio();
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = from; i < a.size(); i += 2) {
      sum += static_cast<double>(a[i]) * static_cast<double>(a[i]);
      ++n;
    }
    return n != 0 ? std::sqrt(sum / static_cast<double>(n)) / 32768.0 : 0.0;
  }
};
}  // namespace

TEST_CASE("sample: rows 2 and 3 play layout rows A (a note) and B (a triad) while held, and let go on the release") {
  LayoutRig r;
  CHECK(r.listen(200) < kSound);
  r.h.pad_down(kRowA + 5);  // row A: NOTE
  CHECK(r.listen(300) > kSound);
  CHECK(r.led(kRowA + 5) == app::kLedOn);
  r.h.pad_up(kRowA + 5);
  r.h.run_ms(900);
  CHECK(r.led(kRowA + 5) != app::kLedOn);
  CHECK(r.listen(200) < kSound);
  r.h.pad_down(kRowB + 5);  // row B: TRIAD
  CHECK(r.listen(300) > kSound);
  CHECK(r.led(kRowB + 5) == app::kLedOn);
  r.h.pad_up(kRowB + 5);
  r.h.run_ms(900);
  CHECK(r.listen(200) < kSound);
  CHECK(r.h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: the lower rows are lit as the notes palette and the encoder (F1, the slot) never darkens them") {
  LayoutRig r;
  CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) < 12u);
  CHECK(r.count(kRowB, kRowB + 11, app::kLedOff) < 12u);
  r.h.turn(1);
  r.h.run_ms(300);
  CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) < 12u);
  CHECK(r.count(kRowB, kRowB + 11, app::kLedOff) < 12u);
  r.h.pad_down(kRowB + 5);  // row B plays a chord with the default palette
  CHECK(r.listen(300) > kSound);
  r.h.pad_up(kRowB + 5);
  r.h.run_ms(900);
  r.h.pad_down(kRowA + 5);  // row A a note
  CHECK(r.listen(300) > kSound);
  r.h.pad_up(kRowA + 5);
  r.h.run_ms(900);
  CHECK(r.h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: two rows of the same kind play the same notes an octave apart") {
  o3_voc03er::pads::Layout l;
  CHECK(l.row[0] == o3_voc03er::pads::Plays::Note);  // the palette's own default: A NOTE, B TRIAD
  CHECK(l.row[1] == o3_voc03er::pads::Plays::Triad);
  l.row[0] = o3_voc03er::pads::Plays::Note;
  l.row[1] = o3_voc03er::pads::Plays::Note;
  for (unsigned col = 0; col < 12; ++col) {
    const auto a = o3_voc03er::pads::pad_play(l, o3_voc03er::pads::Row::A, col, 0);
    const auto b = o3_voc03er::pads::pad_play(l, o3_voc03er::pads::Row::B, col, 0);
    CHECK(a.kind == b.kind);
    CHECK(a.note == b.note + 12);
  }
}

TEST_CASE("sample: leaving SAMPLE with a layout pad held lets it go, and the late release is harmless") {
  LayoutRig r;
  r.h.pad_down(kRowA + 5);
  CHECK(r.listen(300) > kSound);
  tap(r.h, app::Button::Performance);
  r.h.run_ms(900);
  CHECK(r.listen(200) < kSound);
  r.h.pad_up(kRowA + 5);
  r.h.run_ms(100);
  CHECK(r.listen(200) < kSound);
  tap(r.h, app::Button::SamplePlay);
  r.h.run_ms(60);
  CHECK(r.led(kRowA + 5) != app::kLedOn);  // SAMPLE comes back with no pad lit
}

// ---- SAMPLE has its own two rows, whatever PERFORM's mode (SP1) -----------------------------------------------------------------------

namespace {
float tract_param(sim::Harness& h, std::uint16_t id) {
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(h, u)) {
      return h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    }
  }
  return -1.0e9f;
}
}  // namespace

TEST_CASE("sample: in TRACT the lower pads still play notes, and PERFORM's strips are untouched") {
  using o3_voc03er::TractUnit;
  LayoutRig r;
  tap(r.h, app::Button::Performance);
  tap(r.h, app::Button::F1);  // MODE
  r.h.turn(8);                // TRACT: PERFORM's rows A NOTE, B TONGUE, C MOUTH
  r.h.run_ms(300);
  const float vowel = tract_param(r.h, TractUnit::Vowel);
  const float tongue = tract_param(r.h, TractUnit::Tongue);
  const unsigned perform_dark = r.count(kRowA, kRowB + 11, app::kLedOff);
  tap(r.h, app::Button::SamplePlay);
  r.h.run_ms(60);
  // the lower rows light as notes (the scale's pads dim, the root low), not as a strip's twelve values
  CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) < 12u);
  r.h.pad_down(kRowA + 5);  // a note: it lights as sounding, and writes no parameter
  r.h.run_ms(60);
  CHECK(r.led(kRowA + 5) == app::kLedOn);
  CHECK(tract_param(r.h, TractUnit::Vowel) == doctest::Approx(vowel));
  r.h.pad_up(kRowA + 5);
  r.h.run_ms(900);
  tap(r.h, app::Button::Performance);  // PERFORM keeps its own layout: row B is still the TONGUE strip, row C the MOUTH strip
  r.h.run_ms(300);
  CHECK(r.count(kRowA, kRowB + 11, app::kLedOff) == perform_dark);
  r.h.pad_down(35);  // PERFORM's row B is pads 24..35 (its top row is the controls): the top of the TONGUE strip
  r.h.run_ms(60);
  CHECK(tract_param(r.h, TractUnit::Tongue) != doctest::Approx(tongue));  // the strip still sets TONGUE here
  r.h.pad_up(35);
  CHECK(r.h.runtime().frame_failures() == 0u);
}

// ---- UX3: the mode cue, the slices line, the reader refusal -------------------------------------------------------------------------------

TEST_CASE("sample: UX3 mode cue: TUNE does not hear SOUNDS, TRACT hears SPEAKS and PADS ONLY through DRY, and moves the mouth with MOUTH LIVE") {
  CHECK(std::strcmp(o3_voc03er::kModeNames[sm::kModeTune], "TUNE") == 0);
  for (unsigned mode = 0; mode < o3_voc03er::kModeCount; ++mode) {
    for (const bool live : {false, true}) {
      CHECK(sm::mode_cue(mode, sm::Side::Sounds, live) == (mode == sm::kModeTune ? sm::Cue::TuneSilent : sm::Cue::Heard));
      const sm::Cue speaks = mode == o3_voc03er::kModeTract ? (live ? sm::Cue::TractLive : sm::Cue::TractDry) : sm::Cue::Heard;
      CHECK(sm::mode_cue(mode, sm::Side::Speaks, live) == speaks);
      CHECK(sm::mode_cue(mode, sm::Side::None, live) == speaks);
    }
  }
  CHECK(sm::cue_reason(sm::Cue::Heard) == nullptr);
  CHECK(std::strlen(sm::cue_reason(sm::Cue::TuneSilent)) <= 40u);
  CHECK(std::strlen(sm::cue_reason(sm::Cue::TractDry)) <= 40u);
  CHECK(std::strlen(sm::cue_reason(sm::Cue::TractLive)) <= 40u);
  // The two TRACT lines differ and neither is the other's claim: with the mouth LIVE a SPEAKS slot is not "only through DRY", and it is not dimmed (it is heard).
  CHECK(std::strcmp(sm::cue_reason(sm::Cue::TractDry), sm::cue_reason(sm::Cue::TractLive)) != 0);
  CHECK(std::strstr(sm::cue_reason(sm::Cue::TractLive), "ONLY") == nullptr);
  CHECK(sm::cue_dim(sm::Cue::TuneSilent));
  CHECK(sm::cue_dim(sm::Cue::TractDry));
  CHECK_FALSE(sm::cue_dim(sm::Cue::TractLive));
  CHECK_FALSE(sm::cue_dim(sm::Cue::Heard));
  // The cue line: one plain line for the selected slot, short enough for the Label face in 800 px (52 characters).
  char line[64];
  for (unsigned slot = 0; slot < 4; ++slot) {
    for (const sm::Side side : {sm::Side::Speaks, sm::Side::Sounds, sm::Side::None}) {
      for (const sm::Cue cue : {sm::Cue::Heard, sm::Cue::TuneSilent, sm::Cue::TractDry, sm::Cue::TractLive}) {
        for (const bool loop : {false, true}) {
          sm::cue_line(line, slot, side, cue, loop, route::kFirstSlot);
          CHECK(std::strlen(line) <= 52u);
          CHECK(line[5] == static_cast<char>('1' + slot));
        }
      }
    }
  }
  sm::cue_line(line, 1, sm::Side::None, sm::Cue::Heard, false, route::kSpeaksLive);
  CHECK(std::strcmp(line, "SLOT 2: ITS PADS SPEAK OVER LIVE") == 0);
  sm::cue_line(line, 1, sm::Side::None, sm::Cue::Heard, true, route::kFirstSlot + 2);
  CHECK(std::strcmp(line, "SLOT 2: LOOP NOT HEARD, PADS SPEAK WITH SLOT 3") == 0);
  sm::cue_line(line, 0, sm::Side::Speaks, sm::Cue::TractDry, false, route::kFirstSlot);
  CHECK(std::strstr(line, "ONLY THROUGH DRY") != nullptr);
  sm::cue_line(line, 2, sm::Side::Speaks, sm::Cue::Heard, false, route::kFirstSlot + 2);
  CHECK(std::strcmp(line, "SLOT 3 SPEAKS: THE VOCODER HEARS IT, NOT THE INPUT") == 0);
  // The reader refusal says Recording or Armed, as it is.
  CHECK(sm::no_reader_line(false) == sm::kNoReaderRecLine);
  CHECK(sm::no_reader_line(true) == sm::kNoReaderArmedLine);
  CHECK(std::strstr(sm::kNoReaderRecLine, "RECORDING") != nullptr);
  CHECK(std::strstr(sm::kNoReaderArmedLine, "ARMED") != nullptr);
  CHECK(std::strlen(sm::kNoReaderRecLine) <= 40u);
  CHECK(std::strlen(sm::kNoReaderArmedLine) <= 40u);
  CHECK(std::strlen(sm::kCutsResetLine) <= 40u);
}

TEST_CASE("sample: UX3 tract_live follows the TRACT unit's Source on the engine") {
  using o3_voc03er::TractUnit;
  Rig rig;
  CHECK_FALSE(sm::tract_live(rig.h->engine()));  // PADS by default
  bool found = false;
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(*rig.h, u)) {
      found = true;
      (void)rig.h->engine().set_param(static_cast<o3sdk::dsp::UnitId>(u), TractUnit::Source, 1.0f);
      rig.h->run_ms(60);
      CHECK(sm::tract_live(rig.h->engine()));
      (void)rig.h->engine().set_param(static_cast<o3sdk::dsp::UnitId>(u), TractUnit::Source, 0.0f);
      rig.h->run_ms(60);
      CHECK_FALSE(sm::tract_live(rig.h->engine()));
      break;
    }
  }
  CHECK(found);
}

TEST_CASE("sample: UX3 slices_line says how many slices there are") {
  char b[32];
  sm::slices_line(0, b);
  CHECK(std::string(b) == "NO SLICES: SLOT IS EMPTY");
  sm::slices_line(1, b);
  CHECK(std::string(b) == "1 SLICE");
  sm::slices_line(5, b);
  CHECK(std::string(b) == "5 SLICES");
  sm::slices_line(24, b);
  CHECK(std::string(b) == "24 SLICES");
}

// ---- FOLD: LOOP ON makes a slot speak, the repeat survives leaving SAMPLE, SLICES POLY with LOOP ON ----------------------------------------------

namespace {
// The pixels of the F5 key's value window (x 400..499, y 424..447 of the 800 x 480 frame): what the key reads, as bytes.
std::string f5_window(const sim::Harness& h) {
  const std::uint8_t* px = h.raster().rgba();
  std::string out;
  for (int y = 424; y < 448; ++y) {
    out.append(reinterpret_cast<const char*>(px + (static_cast<std::size_t>(y) * 800u + 400u) * 4u), 100u * 4u);
  }
  return out;
}

float mod_src(sim::Harness& h) { return unit_param(h, kModSrc); }
}  // namespace

TEST_CASE("sample: LOOP ON on a slot that neither speaks nor sounds makes it speak first; on a SOUNDS slot the route stays") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();  // slot 1, PADS ONLY
  rig.open();
  CHECK(mod_src(h) != doctest::Approx(3.0f));
  CHECK(unit_param(h, kCarSrc) != doctest::Approx(3.0f));
  tap(h, app::Button::F5);  // LOOP ON
  h.run_ms(300);
  CHECK(mod_src(h) == doctest::Approx(3.0f));  // the slot speaks now (the route of a tap on F2) ...
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(1.0f));  // ... and the loop plays
  tap(h, app::Button::F5);  // OFF: the loop stops, the slot goes on speaking
  h.run_ms(300);
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  CHECK(mod_src(h) == doctest::Approx(3.0f));
  // A take into slot 2 that SOUNDS holds (CarSrc 4, as PERFORM's F3 list leaves it): AUTOPLAY ON plays it where it is, the route stays,
  // and its LOOP ON changes nothing in it.
  for (unsigned u = 0; u < 8; ++u) {
    if (!o3_voc03er::test::is_tract_unit(h, u)) {
      (void)h.engine().set_param(static_cast<o3sdk::dsp::UnitId>(u), kCarSrc, 4.0f);
    }
  }
  h.run_ms(60);
  tap(h, app::Button::SampleRec);
  h.run_ms(60);
  o3_voc03er::test::rec_slot(h, 1);
  o3_voc03er::test::rec_autoplay(h, true);
  tap(h, app::Button::Rec);
  h.run_ms(9000);
  REQUIRE(unit_param(h, kCarSrc) == doctest::Approx(4.0f));  // SOUNDS still holds slot 2
  rig.open();
  for (unsigned i = 0; i < 3; ++i) tap(h, app::Button::Up);  // the cursor may sit on slot 2 already (a take selects its slot): first the top ...
  tap(h, app::Button::Down);                                  // ... then slot 2
  const float mod_before = mod_src(h);
  if (lp(h, 1, Vocoder::LoopField::Play) == 1.0f) {  // AUTOPLAY played the SOUNDS slot with its take: F5 ends it, and the second tap is the LOOP ON under test
    tap(h, app::Button::F5);
    h.run_ms(300);
    CHECK(lp(h, 1, Vocoder::LoopField::Play) == doctest::Approx(0.0f));
    CHECK(unit_param(h, kCarSrc) == doctest::Approx(4.0f));
  }
  tap(h, app::Button::F5);
  h.run_ms(500);
  CHECK(lp(h, 1, Vocoder::LoopField::Play) == doctest::Approx(1.0f));
  CHECK(unit_param(h, kCarSrc) == doctest::Approx(4.0f));
  CHECK(mod_src(h) == doctest::Approx(mod_before));
  CHECK(h.runtime().frame_failures() == 0u);
  // SAMPLE REC keeps its SLOT and AUTOPLAY across the harnesses of the process: back to what Rig::take expects (SLOT 1, AUTOPLAY OFF).
  tap(h, app::Button::SampleRec);
  h.run_ms(60);
  o3_voc03er::test::rec_slot(h, 0);
  o3_voc03er::test::rec_autoplay(h, false);
}

TEST_CASE("sample: a repeating slice goes on after SAMPLE is left, F5 reads ON SL n from the unit again, F5 OFF and a NEW project end it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  tap(h, app::Button::F5);  // LOOP ON
  h.run_ms(2500);
  h.pad_down(5);
  h.run_ms(40);
  h.pad_up(5);
  h.run_ms(200);
  REQUIRE(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(2.0f));
  const std::string reads = f5_window(h);  // "ON SL 6"
  tap(h, app::Button::Performance);
  h.run_ms(500);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(2.0f));  // kept: PERFORM's pads play over it
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(1.0f));
  rig.open();
  h.run_ms(200);
  CHECK(f5_window(h) == reads);  // the way back: the key still reads the repeat (the unit's status, not the screen's memory)
  tap(h, app::Button::F5);       // OFF ends it
  h.run_ms(200);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(0.0f));
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  CHECK(f5_window(h) != reads);
  // A repeat again, then a NEW project: the slot is emptied, nothing repeats, and F5 does not read a slice.
  tap(h, app::Button::F5);
  h.run_ms(2500);
  h.pad_down(2);
  h.run_ms(40);
  h.pad_up(2);
  h.run_ms(200);
  REQUIRE(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(2.0f));
  rig.take(true);  // a new take into slot 1 (on SPEAKS) while it repeats, AUTOPLAY ON: the repeat ends with the old audio and the new one plays
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(0.0f));
  CHECK_FALSE(f5_window(h) == reads);
  rig.open();
  if (lp(h, 0, Vocoder::LoopField::Play) != 1.0f) {  // the new take may be playing already (the slot speaks)
    tap(h, app::Button::F5);
    h.run_ms(2500);
  }
  REQUIRE(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(1.0f));
  h.pad_down(4);
  h.run_ms(40);
  h.pad_up(4);
  h.run_ms(200);
  REQUIRE(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(2.0f));
  tap(h, app::Button::File);
  h.run_ms(60);
  tap(h, app::Button::F4);  // NEW asks
  tap(h, app::Button::F1);  // confirmed
  h.run_ms(600);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(0.0f));
  CHECK(lp(h, 0, Vocoder::LoopField::Play) == doctest::Approx(0.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: LOOP ON repeats one slice per slot: the next pad takes the repeat over on the slot's own reader") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  tap(h, app::Button::F5);  // LOOP ON
  h.run_ms(2500);
  h.pad_down(5);
  h.run_ms(40);
  h.pad_up(5);
  h.run_ms(40);
  REQUIRE(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(2.0f));
  const float start5 = rp(h, 0, Vocoder::RegionField::Start);
  h.pad_down(2);
  h.run_ms(40);
  h.pad_up(2);
  h.run_ms(40);
  CHECK(rp(h, 0, Vocoder::RegionField::Trig) == doctest::Approx(2.0f));
  CHECK(rp(h, 0, Vocoder::RegionField::Start) != doctest::Approx(start5));
  for (unsigned reader = 1; reader < 4; ++reader) {  // no other reader took a slice
    CHECK(rp(h, reader, Vocoder::RegionField::Trig) == doctest::Approx(0.0f));
  }
  CHECK(h.runtime().frame_failures() == 0u);
}

// ---- the one-finger redesign: a slice pad alone brings the note (rule 1), a lower pad alone starts the picked slice (rule 2) -------------------
// The observable of the note is the output energy: BANDS speaks while it hears the tone on the fake capture and a carrier note is held, so a slice pad that brought no
// note would give silence. A slot that holds a take and neither speaks nor sounds is HELD.

namespace {
struct OneFinger {
  Rig rig;
  sim::Harness& h;
  OneFinger() : h(*rig.h) {
    rig.take();
    h.record(true);
    rig.open();
  }
  void slow() {  // SPEED 0.5X: a slice lasts 500 ms, long enough to hold and release inside it
    tap(h, app::Button::F3);
    tap(h, app::Button::F3);
    tap(h, app::Button::F3);
    h.run_ms(60);
  }
  double listen(unsigned ms) {
    const std::size_t from = h.audio().size();
    h.run_ms(ms);
    const std::vector<std::int16_t>& a = h.audio();
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = from; i < a.size(); i += 2) {
      sum += static_cast<double>(a[i]) * static_cast<double>(a[i]);
      ++n;
    }
    return n != 0 ? std::sqrt(sum / static_cast<double>(n)) / 32768.0 : 0.0;
  }
  float trig() { return rp(h, 0, Vocoder::RegionField::Trig); }
  float start() { return rp(h, 0, Vocoder::RegionField::Start); }
  float len() { return rp(h, 0, Vocoder::RegionField::Len); }
};
}  // namespace

TEST_CASE("sample: rule 1: a slice pad alone brings the note, and ONE SHOT's end takes it away again") {
  OneFinger r;
  r.slow();
  CHECK(r.listen(300) < kSound);  // nothing held: the tone alone does not speak
  r.h.pad_down(2);
  CHECK(r.listen(150) > kSound);
  r.h.pad_up(2);  // ONE SHOT: the release does not end the slice, nor the note
  CHECK(r.listen(100) > kSound);
  r.h.run_ms(2500);  // the slice ended by itself (service() saw it): the note let go
  CHECK(r.listen(300) < kSound);
  CHECK(r.h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: rule 1: TRIG GATE: the slice's release takes the note away") {
  OneFinger r;
  tap(r.h, app::Button::F6);  // lights the key (H2)
  tap(r.h, app::Button::F6);  // GATE
  r.slow();
  r.h.pad_down(2);
  CHECK(r.listen(150) > kSound);
  r.h.pad_up(2);
  r.h.run_ms(1200);
  CHECK(r.listen(300) < kSound);
  CHECK(r.h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: rule 1: another slice replaces the slice and the note stays for it; the old pad's release does not end it") {
  OneFinger r;
  tap(r.h, app::Button::F6);  // lights the key (H2)
  tap(r.h, app::Button::F6);  // GATE
  r.slow();
  r.h.pad_down(2);
  r.h.run_ms(100);
  r.h.pad_down(3);  // MONO: slice 4 cuts slice 3
  r.h.pad_up(2);
  CHECK(r.listen(150) > kSound);
  CHECK(r.trig() == doctest::Approx(1.0f));
  r.h.pad_up(3);
  r.h.run_ms(1200);
  CHECK(r.listen(300) < kSound);
}

TEST_CASE("sample: rule 1: a lower pad takes the note over, and its own note ends with its release") {
  OneFinger r;
  tap(r.h, app::Button::F6);  // lights the key (H2)
  tap(r.h, app::Button::F6);  // GATE
  r.slow();
  r.h.pad_down(2);
  r.h.run_ms(100);
  r.h.pad_down(kRowA + 5);
  r.h.pad_up(2);  // the slice's release: the lower pad's note carries on
  r.h.run_ms(600);
  CHECK(r.listen(200) > kSound);
  r.h.pad_up(kRowA + 5);
  r.h.run_ms(1200);
  CHECK(r.listen(300) < kSound);
}

TEST_CASE("sample: rule 1: leaving SAMPLE with the slice pad held ends the note, and the late release is harmless") {
  OneFinger r;
  tap(r.h, app::Button::F6);  // lights the key (H2)
  tap(r.h, app::Button::F6);  // GATE
  r.slow();
  r.h.pad_down(2);
  CHECK(r.listen(150) > kSound);
  tap(r.h, app::Button::Performance);
  r.h.run_ms(1200);
  CHECK(r.listen(300) < kSound);
  r.h.pad_up(2);
  r.h.run_ms(100);
  CHECK(r.listen(300) < kSound);
  tap(r.h, app::Button::SamplePlay);
  r.h.run_ms(60);
  CHECK(r.listen(300) < kSound);  // coming back plays nothing
}

TEST_CASE("sample: rule 1: LOOP ON repeats the slice with the note, and LOOP OFF takes both away") {
  OneFinger r;
  tap(r.h, app::Button::F5);  // LOOP ON (the slot speaks first)
  r.h.run_ms(2500);
  r.h.pad_down(5);
  r.h.run_ms(40);
  r.h.pad_up(5);
  r.h.run_ms(40);
  REQUIRE(r.trig() == doctest::Approx(2.0f));
  CHECK(r.listen(300) > kSound);
  tap(r.h, app::Button::F5);  // OFF
  r.h.run_ms(1500);
  CHECK(r.listen(300) < kSound);
  CHECK(r.trig() == doctest::Approx(0.0f));
}

TEST_CASE("sample: rule 2: a lower pad alone plays its note and starts the picked slice; the last release stops it in GATE") {
  OneFinger r;
  tap(r.h, app::Button::F6);  // lights the key (H2)
  tap(r.h, app::Button::F6);  // GATE
  r.slow();
  r.h.pad_down(kRowA + 5);  // the picked slice is slice 1 at first
  r.h.run_ms(60);
  CHECK(r.trig() == doctest::Approx(1.0f));
  CHECK(r.start() == doctest::Approx(0.0f));
  CHECK(r.listen(100) > kSound);
  r.h.pad_up(kRowA + 5);
  r.h.run_ms(60);
  CHECK(r.trig() == doctest::Approx(0.0f));
  // The picked slice follows the top pads.
  r.h.pad_down(4);
  r.h.run_ms(40);
  const float len = r.len();
  CHECK(r.start() == doctest::Approx(len * 4.0f).epsilon(0.01));
  r.h.pad_up(4);
  r.h.run_ms(60);
  CHECK(r.trig() == doctest::Approx(0.0f));
  r.h.pad_down(kRowA + 5);
  r.h.run_ms(60);
  CHECK(r.trig() == doctest::Approx(1.0f));
  CHECK(r.start() == doctest::Approx(len * 4.0f).epsilon(0.01));
  // The slice lets go with the last lower pad, not the first.
  r.h.pad_down(kRowB + 3);
  r.h.pad_up(kRowA + 5);
  r.h.run_ms(60);
  CHECK(r.trig() == doctest::Approx(1.0f));
  r.h.pad_up(kRowB + 3);
  r.h.run_ms(60);
  CHECK(r.trig() == doctest::Approx(0.0f));
  CHECK(r.h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: rule 2: ONE SHOT plays the picked slice out after the lower pad's release") {
  OneFinger r;
  r.slow();
  r.h.pad_down(kRowB + 5);
  r.h.run_ms(60);
  r.h.pad_up(kRowB + 5);
  r.h.run_ms(60);
  CHECK(r.trig() == doctest::Approx(1.0f));
  r.h.run_ms(2500);  // the region ends by itself (the written Trig stays 1 until the next pad)
  CHECK(r.h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: rule 2 does not apply with a slice pad held, with LOOP ON, or on a SOUNDS slot") {
  OneFinger r;
  tap(r.h, app::Button::F6);  // lights the key (H2)
  tap(r.h, app::Button::F6);  // GATE
  r.slow();
  r.h.pad_down(1);  // a slice pad held: the lower pad only plays its note
  r.h.run_ms(60);
  const float start1 = r.start();
  CHECK(start1 > 0.0f);
  r.h.pad_down(kRowA + 5);
  r.h.run_ms(60);
  CHECK(r.start() == doctest::Approx(start1));  // slice 2 still, not restarted from slice 1
  r.h.pad_up(1);
  r.h.pad_up(kRowA + 5);
  r.h.run_ms(1200);
  CHECK(r.trig() == doctest::Approx(0.0f));
  tap(r.h, app::Button::F5);  // LOOP ON
  r.h.run_ms(2500);
  r.h.pad_down(kRowA + 5);
  r.h.run_ms(60);
  CHECK(r.trig() == doctest::Approx(0.0f));  // the whole loop plays, no slice starts
  r.h.pad_up(kRowA + 5);
  tap(r.h, app::Button::F5);  // OFF
  r.h.run_ms(600);
  tap(r.h, app::Button::F2);  // SPEAKS -> the slot speaks
  tap(r.h, app::Button::F2);  // SOUNDS
  r.h.run_ms(60);
  r.h.pad_down(kRowA + 5);
  r.h.run_ms(60);
  CHECK(r.trig() == doctest::Approx(0.0f));
  r.h.pad_up(kRowA + 5);
}

TEST_CASE("sample: F1 is the slot: the first tap lights it, the second steps and wraps; the encoder and the arrows stop at the ends") {
  OneFinger r;
  sim::Harness& h = r.h;
  const auto dir = [&](unsigned slot) { return lp(h, slot, Vocoder::LoopField::Dir); };
  // The selected slot is seen through F4 DIR (H2: the first tap lights, the second sets REV), then back to FWD.
  const auto probe = [&](unsigned want) {
    tap(h, app::Button::F4);
    tap(h, app::Button::F4);
    h.run_ms(60);
    for (unsigned s = 0; s < 4; ++s) {
      CHECK(dir(s) == doctest::Approx(s == want ? 1.0f : 0.0f));
    }
    tap(h, app::Button::F4);  // FWD again
    h.run_ms(60);
  };
  h.turn(10);  // F1 is lit when SAMPLE opens: the encoder stops at slot 4
  h.run_ms(20);
  probe(3);
  tap(h, app::Button::F1);  // the first tap lights F1 and changes nothing
  h.turn(-1);               // so this is slot 3
  probe(2);
  tap(h, app::Button::F1);  // lights
  tap(h, app::Button::F1);  // slot 4
  tap(h, app::Button::F1);  // wraps to slot 1
  probe(0);
  tap(h, app::Button::F1);  // lights
  h.turn(-5);               // the encoder stops at slot 1
  probe(0);
  tap(h, app::Button::F1);  // lights
  tap(h, app::Button::Right);  // one encoder step: slot 2
  probe(1);
  tap(h, app::Button::F1);    // lights F1 again (the probe left F4 lit)
  tap(h, app::Button::Down);  // UP and DOWN still choose the slot
  probe(2);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: F7 is blank: it takes no focus and changes nothing") {
  OneFinger r;
  sim::Harness& h = r.h;
  tap(h, app::Button::F3);  // SPEED lit, 1X
  const float before = lp(h, 0, Vocoder::LoopField::Speed);
  tap(h, app::Button::F7);
  tap(h, app::Button::F7);
  h.turn(1);  // the encoder is still on SPEED
  h.run_ms(60);
  CHECK(lp(h, 0, Vocoder::LoopField::Speed) != doctest::Approx(before));
  CHECK(lp(h, 0, Vocoder::LoopField::Dir) == doctest::Approx(0.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

// ---- why nothing sounds on SAMPLE (silence.h): the line is the app's silence_ watch (state_line `silence=`: 0 none, 1 NO NOTE, 2 NOTHING SPEAKS) ------
// SAMPLE tries to be heard with a slice pad down and the slice still sounding; the line needs half a second of it, and goes with the pad.

namespace {
int silence_answer(sim::Harness& h) {
  char line[1500];
  const std::string text(line, h.runtime().state_line(line, sizeof line));
  const std::size_t at = text.find("silence=");
  return at == std::string::npos ? -1 : std::atoi(text.c_str() + at + 8);
}
}  // namespace

TEST_CASE("sample: why nothing sounds: a slot on SOUNDS with nothing speaking says NOTHING SPEAKS after half a second of a held slice, and clears with the pad") {
  OneFinger r;
  sim::Harness& h = r.h;
  r.rig.cap.input_off();  // the take is in; now nobody talks into the input
  for (unsigned u = 0; u < 8; ++u) {
    if (!o3_voc03er::test::is_tract_unit(h, u)) {
      (void)h.engine().set_param(static_cast<o3sdk::dsp::UnitId>(u), kCarSrc, 3.0f);  // slot 1 SOUNDS (SPEAKS stays LIVE)
    }
  }
  h.run_ms(2500);  // the input meter lets go of the tone
  CHECK(silence_answer(h) == 0);  // not trying: nothing is said
  tap(h, app::Button::F5);  // LOOP ON: on a SOUNDS slot the route stays, and a slice pad repeats its slice
  h.run_ms(300);
  CHECK(silence_answer(h) == 0);
  h.pad_down(2);
  h.run_ms(300);
  CHECK(silence_answer(h) == 0);  // under the half second
  h.run_ms(600);
  CHECK(silence_answer(h) == 2);
  h.pad_up(2);
  h.run_ms(60);
  CHECK(silence_answer(h) == 0);  // the moment the player lets go
  tap(h, app::Button::F5);        // OFF: back to what Rig::take expects
  h.run_ms(100);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: why nothing sounds: a one-shot slice that ended under a held pad, on a slot on no side, says nothing") {
  OneFinger r;
  sim::Harness& h = r.h;
  r.rig.cap.input_off();  // a line that would say NOTHING SPEAKS if the slice were not the voice
  h.run_ms(2500);
  r.slow();
  h.pad_down(2);
  for (unsigned i = 0; i < 30; ++i) {  // 1.5 s: through the slice (500 ms) and past its end, the pad held all the way
    h.run_ms(50);
    CHECK_MESSAGE(silence_answer(h) == 0, "at " << (i + 1) * 50 << " ms");
  }
  h.pad_up(2);
  h.run_ms(100);
  CHECK(silence_answer(h) == 0);
  CHECK(h.runtime().frame_failures() == 0u);
}
