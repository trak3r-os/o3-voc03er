// SPDX-License-Identifier: GPL-3.0-or-later
// The PATTERN screen (U6b): the pad map and the LEVEL palette as pure logic, then the screen on the simulator, read through the pad LEDs.

#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/platform/host/fake_capture.h>
#include <o3sdk/sim/harness.h>

#include "../app/src/pattern_logic.h"

#include <cstddef>
#include <memory>
#include <string>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace pt = o3_voc03er::pattern;

TEST_CASE("pattern: the pad map") {
  CHECK(pt::step_of_pad(24) == 0);
  CHECK(pt::step_of_pad(31) == 7);
  CHECK(pt::step_of_pad(36) == 8);
  CHECK(pt::step_of_pad(43) == 15);
  CHECK(pt::step_of_pad(32) == -1);  // a pattern pad
  CHECK(pt::step_of_pad(5) == -1);   // the palette
  CHECK(pt::pattern_of_pad(32) == 0);
  CHECK(pt::pattern_of_pad(35) == 3);
  CHECK(pt::pattern_of_pad(44) == 4);
  CHECK(pt::pattern_of_pad(47) == 7);
  CHECK(pt::pattern_of_pad(24) == -1);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(pt::step_of_pad(pt::pad_of_step(i)) == static_cast<int>(i));
  }
  for (unsigned q = 0; q < 8; ++q) {
    CHECK(pt::pattern_of_pad(pt::pad_of_pattern(q)) == static_cast<int>(q));
  }
}

TEST_CASE("pattern: the SLICE lane's palette, value and clear") {
  using o3_voc03er::get_step;
  o3_voc03er::PatternData p{};
  // a palette pad writes slice pad + 1, only while SPEAKS has that many cuts; with none it says so and writes nothing
  CHECK(pt::slice_write(p, 0, 0, 0) == pt::SliceWrite::NoSlices);
  CHECK(pt::slice_write(p, 0, 23, 0) == pt::SliceWrite::NoSlices);
  CHECK(get_step(p, 0).slice == 0u);
  CHECK(pt::slice_write(p, 0, 4, 8) == pt::SliceWrite::Written);
  CHECK(get_step(p, 0).slice == 5u);
  CHECK(pt::slice_write(p, 1, 8, 8) == pt::SliceWrite::Dark);  // a dark pad does nothing
  CHECK(get_step(p, 1).slice == 0u);
  CHECK(pt::slice_write(p, 1, 7, 8) == pt::SliceWrite::Written);
  CHECK(get_step(p, 1).slice == 8u);
  CHECK(pt::slice_write(p, 2, 23, 24) == pt::SliceWrite::Written);
  CHECK(get_step(p, 2).slice == 24u);
  // the palette's mid pad is the step's own slice
  CHECK(pt::palette_matches(pt::Lane::Slice, 4, get_step(p, 0), 0, false, o3_voc03er::ChordKind::Triad));
  CHECK_FALSE(pt::palette_matches(pt::Lane::Slice, 5, get_step(p, 0), 0, false, o3_voc03er::ChordKind::Triad));
  CHECK_FALSE(pt::palette_matches(pt::Lane::Slice, 0, get_step(p, 5), 0, false, o3_voc03er::ChordKind::Triad));  // no slice: nothing matches
  // VALUE steps 0..count; 0 is none; an out of range stored slice steps down to the count
  CHECK(pt::step_value(p, 0, pt::Lane::Slice, 1, 8));
  CHECK(get_step(p, 0).slice == 6u);
  CHECK(pt::step_value(p, 0, pt::Lane::Slice, 10, 8));
  CHECK(get_step(p, 0).slice == 8u);
  CHECK_FALSE(pt::step_value(p, 0, pt::Lane::Slice, 1, 8));  // the end of the range
  CHECK(pt::step_value(p, 0, pt::Lane::Slice, -20, 8));
  CHECK(get_step(p, 0).slice == 0u);
  CHECK_FALSE(pt::step_value(p, 0, pt::Lane::Slice, -1, 8));
  CHECK(pt::step_value(p, 0, pt::Lane::Slice, 1, 8));
  CHECK(get_step(p, 0).slice == 1u);
  CHECK(pt::step_value(p, 2, pt::Lane::Slice, 1, 3));  // a stored 24 with 3 cuts: it comes down to 3
  CHECK(get_step(p, 2).slice == 3u);
  CHECK_FALSE(pt::step_value(p, 3, pt::Lane::Slice, 1, 0));
  // CLEAR LANE and SHIFT + step: slice 0, the other lanes stay
  REQUIRE(set_note(p, 1, 60));
  CHECK(pt::rest_lane(p, 1, pt::Lane::Slice));
  CHECK(get_step(p, 1).slice == 0u);
  CHECK(get_step(p, 1).note == 60u);
  // LEVEL does not touch a slice
  CHECK(pt::slice_write(p, 4, 2, 8) == pt::SliceWrite::Written);
  CHECK(pt::step_value(p, 4, pt::Lane::Level, 1));
  CHECK(get_step(p, 4).slice == 3u);
}

TEST_CASE("pattern: the LEVEL palette is 24 distinct steps up to 100, not 127") {
  CHECK(pt::level_of_pad(23) == 100u);
  CHECK(pt::level_of_pad(0) >= 1u);
  for (unsigned p = 1; p < 24; ++p) {
    CHECK(pt::level_of_pad(p) > pt::level_of_pad(p - 1));
  }
}

TEST_CASE("pattern: value steps and rests by lane") {
  o3_voc03er::PatternData p{};
  CHECK(pt::step_value(p, 0, pt::Lane::Note, 1));  // a rest starts at C3
  CHECK(o3_voc03er::get_step(p, 0).note == 48u);
  CHECK(pt::step_value(p, 0, pt::Lane::Note, 2));
  CHECK(o3_voc03er::get_step(p, 0).note == 50u);
  CHECK_FALSE(pt::step_value(p, 1, pt::Lane::Note, -1));  // a rest does not go down
  CHECK_FALSE(pt::step_value(p, 0, pt::Lane::Carrier, -1));
  CHECK(pt::step_value(p, 0, pt::Lane::Carrier, 3));
  CHECK(o3_voc03er::get_step(p, 0).carrier == 3u);
  CHECK_FALSE(pt::step_value(p, 0, pt::Lane::Carrier, 9));
  CHECK_FALSE(pt::step_value(p, 0, pt::Lane::Slice, 1));  // SPEAKS has no slices
  CHECK(pt::rest_lane(p, 0, pt::Lane::Note));
  CHECK(o3_voc03er::get_step(p, 0).note == 0u);
  CHECK(o3_voc03er::get_step(p, 0).carrier == 3u);  // the other lanes stay
  CHECK(pt::rest_lane(p, 0, pt::Lane::Carrier));
  CHECK(o3_voc03er::get_step(p, 0).carrier == 0u);
}

namespace {
void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(40);  // the LEDs refresh on the main loop's tick
}
void pad(sim::Harness& h, unsigned p, bool shift = false) {
  if (shift) {
    h.press(app::Button::Shift);
    h.run_ms(5);
  }
  h.pad_down(p);
  h.run_ms(20);
  h.pad_up(p);
  h.run_ms(20);
  if (shift) {
    h.release(app::Button::Shift);
    h.run_ms(5);
  }
}

struct Rig {
  o3sdk::platform::host::FakeCapture cap;
  std::unique_ptr<sim::Harness> h;
  Rig() {
    cap.input_tone(220.0f, -12.0f);
    h = std::make_unique<sim::Harness>(::o3sdk_app(), nullptr, nullptr, nullptr, nullptr, &cap);
    REQUIRE(h->start());
    h->run_ms(100);
    tap(*h, app::Button::Performance);  // PERFORM first, as from home
    tap(*h, app::Button::Pattern);
    h->run_ms(60);
  }
  unsigned led(unsigned p) const { return h->leds().level(p); }
  unsigned count(unsigned from, unsigned to, unsigned level) const {
    unsigned n = 0;
    for (unsigned p = from; p <= to; ++p) {
      n += led(p) == level ? 1u : 0u;
    }
    return n;
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

TEST_CASE("pattern: a palette pad writes the cursor's step and moves on; a step pad moves the cursor") {
  Rig rig;
  CHECK(rig.led(24) == app::kLedMid);  // step 1: the cursor, nothing in it
  CHECK(rig.led(25) == app::kLedOff);
  pad(*rig.h, 5);  // a NOTE
  CHECK(rig.led(24) == app::kLedDim);  // content now
  CHECK(rig.led(25) == app::kLedMid);  // the cursor moved on
  CHECK(rig.led(5) == app::kLedDim);   // the palette shows the step the cursor is on: empty, so no match
  pad(*rig.h, 30);                     // step 7
  CHECK(rig.led(30) == app::kLedMid);
  CHECK(rig.led(25) == app::kLedOff);
  pad(*rig.h, 24);  // back on step 1: the palette's mid is the note it holds
  CHECK(rig.led(5) == app::kLedMid);
  CHECK(rig.h->runtime().frame_failures() == 0u);
  CHECK(rig.h->display().peak_frame_words() < 2048u);
}

TEST_CASE("pattern: SHIFT + a step pad writes a rest in the lane") {
  Rig rig;
  pad(*rig.h, 5);
  pad(*rig.h, 7);
  CHECK(rig.led(24) == app::kLedDim);
  CHECK(rig.led(25) == app::kLedDim);
  pad(*rig.h, 24, true);
  CHECK(rig.led(24) == app::kLedMid);  // the cursor is there, and the step holds nothing
  pad(*rig.h, 26);
  CHECK(rig.led(24) == app::kLedOff);
  CHECK(rig.led(25) == app::kLedDim);
}

TEST_CASE("pattern: LANE (the buttons and F2) picks what the palette writes and the LEDs show") {
  Rig rig;
  tap(*rig.h, app::Button::Instr);  // CARRIER: seven values
  CHECK(rig.count(0, 23, app::kLedOff) == 17u);
  CHECK(rig.led(0) == app::kLedMid);  // FOLLOW is what an empty step stores
  pad(*rig.h, 2);                     // NOISE into step 1
  pad(*rig.h, 24);
  CHECK(rig.led(2) == app::kLedMid);
  CHECK(rig.led(24) == app::kLedMid);
  tap(*rig.h, app::Button::Vol);  // LEVEL: all 24 pads lit
  CHECK(rig.count(0, 23, app::kLedOff) == 0u);
  pad(*rig.h, 23);
  pad(*rig.h, 24);
  CHECK(rig.led(23) == app::kLedMid);  // the top is 100
  tap(*rig.h, app::Button::Fx);        // SLICE: drawn, dark
  CHECK(rig.count(0, 23, app::kLedOff) == 24u);
  tap(*rig.h, app::Button::Note);
  CHECK(rig.count(0, 23, app::kLedOff) == 0u);
  tap(*rig.h, app::Button::F2);  // focus LANE
  tap(*rig.h, app::Button::F2);  // a tap steps it: CARRIER
  CHECK(rig.count(0, 23, app::kLedOff) == 17u);
  tap(*rig.h, app::Button::Down);
  tap(*rig.h, app::Button::Down);
  CHECK(rig.count(0, 23, app::kLedOff) == 24u);  // UP and DOWN move the lane too
  tap(*rig.h, app::Button::Up);
  CHECK(rig.count(0, 23, app::kLedOff) == 0u);
}

TEST_CASE("pattern: with SPEAKS on LIVE the SLICE lane is dark and writes nothing") {
  Rig rig;
  tap(*rig.h, app::Button::Fx);
  CHECK(rig.count(0, 23, app::kLedOff) == 24u);
  pad(*rig.h, 3);  // a palette pad: SPEAKS HAS NO SLICES
  CHECK(rig.led(24) == app::kLedMid);  // the cursor did not move
  CHECK(rig.led(25) == app::kLedOff);
  rig.h->turn(3);  // the encoder on F5: the same
  rig.h->run_ms(60);
  CHECK(rig.led(24) == app::kLedMid);
  tap(*rig.h, app::Button::F6);  // CLEAR LANE is allowed and changes nothing here
  CHECK(rig.led(24) == app::kLedMid);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: LENGTH cuts the steps, CLEAR LANE empties the lane") {
  Rig rig;
  for (unsigned i = 0; i < 16; ++i) {
    pad(*rig.h, 5);  // fill all sixteen steps; the cursor wraps back to step 1
  }
  CHECK(rig.led(43) == app::kLedDim);
  tap(*rig.h, app::Button::F3);  // focus LENGTH
  rig.h->turn(-8);
  rig.h->run_ms(60);
  CHECK(rig.led(24 + 7) != app::kLedOff);  // step 8 is still in
  CHECK(rig.led(36) == app::kLedOff);      // step 9 is out
  CHECK(rig.led(43) == app::kLedOff);
  tap(*rig.h, app::Button::F6);  // CLEAR LANE asks first
  CHECK(rig.led(25) != app::kLedOff);  // nothing cleared yet
  tap(*rig.h, app::Button::F2);        // any other key cancels
  CHECK(rig.led(25) != app::kLedOff);
  tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F1);  // F1 confirms
  CHECK(rig.led(25) == app::kLedOff);  // the lane is empty
  CHECK(rig.led(24) == app::kLedMid);  // the cursor stays
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: COPY, INSERT and DELETE on a step and on a pattern (the logic)") {
  using o3_voc03er::PatternData;
  PatternData p{};
  (void)o3_voc03er::set_length(p, 4);
  for (unsigned i = 0; i < 4; ++i) {
    REQUIRE(o3_voc03er::set_note(p, i, 60u + i));
  }
  pt::Clip c;
  CHECK_FALSE(pt::insert_step(p, 0, c));  // nothing copied: nothing changes
  CHECK_FALSE(pt::paste_whole(p, c));
  CHECK(o3_voc03er::get_step(p, 0).note == 60u);
  pt::copy_step(c, p, 3);
  CHECK_FALSE(pt::paste_whole(p, c));  // a step is not a pattern
  CHECK_FALSE(pt::insert_step(p, 4, c));  // past LENGTH
  REQUIRE(pt::insert_step(p, 1, c));
  const unsigned want[4] = {60, 63, 61, 62};  // the old last step (63) fell off LENGTH; step 5 was not touched
  for (unsigned i = 0; i < 4; ++i) {
    CHECK(o3_voc03er::get_step(p, i).note == want[i]);
  }
  CHECK(o3_voc03er::get_step(p, 4).note == 0u);
  REQUIRE(pt::clear_step(p, 2));
  const o3_voc03er::Step e = o3_voc03er::get_step(p, 2);
  CHECK((e.note == 0u && e.kind == 0u && e.carrier == 0u && e.level == 0u && e.slice == 0u));
  pt::Clip whole;
  pt::copy_whole(whole, p);
  PatternData q{};
  REQUIRE(pt::paste_whole(q, whole));
  CHECK(o3_voc03er::same_pattern(p, q));  // LENGTH too
  pt::clear_steps(q);
  CHECK(o3_voc03er::notes_used(q) == 0u);
  CHECK(o3_voc03er::length_of(q) == 4u);  // emptied, LENGTH kept
}

TEST_CASE("pattern: COPY + INSERT moves the steps right, DELETE empties one, SHIFT + DELETE asks F1 for the pattern") {
  Rig rig;
  sim::Harness& h = *rig.h;
  auto shifted = [&h](app::Button b) {
    h.press(app::Button::Shift);
    h.run_ms(5);
    tap(h, b);
    h.release(app::Button::Shift);
    h.run_ms(5);
  };
  pad(h, 5);   // step 1
  pad(h, 7);   // step 2
  pad(h, 24);  // the cursor on step 1
  tap(h, app::Button::Copy);
  tap(h, app::Button::Insert);  // step 1's copy at step 1: steps 1 to 3 hold notes now
  pad(h, 31);                   // the cursor away, on step 8
  CHECK(rig.led(24) == app::kLedDim);
  CHECK(rig.led(25) == app::kLedDim);
  CHECK(rig.led(26) == app::kLedDim);
  CHECK(rig.led(27) == app::kLedOff);
  pad(h, 25);
  tap(h, app::Button::Delete);  // step 2 empty
  pad(h, 31);
  CHECK(rig.led(25) == app::kLedOff);
  CHECK(rig.led(24) == app::kLedDim);
  CHECK(rig.led(26) == app::kLedDim);
  shifted(app::Button::Delete);  // asks first
  tap(h, app::Button::F2);       // any other key cancels
  CHECK(rig.led(24) == app::kLedDim);
  shifted(app::Button::Delete);
  tap(h, app::Button::F1);  // F1 empties the pattern
  CHECK(rig.led(24) == app::kLedOff);
  CHECK(rig.led(26) == app::kLedOff);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: CLEAR LANE's question names the lane as the screen does") {
  for (unsigned l = 0; l < pt::kLanes; ++l) {
    const std::string q = pt::kClearPrompt[l];
    CHECK(q == std::string("CLEAR ") + pt::kLaneNames[l] + " LANE: F1 CONFIRMS");
  }
}

TEST_CASE("pattern: pattern pads show the pattern and the one playing; FOLLOW rides the playhead") {
  Rig rig;
  CHECK(rig.led(32) == app::kLedMid);  // A
  CHECK(rig.led(44) == app::kLedDim);
  pad(*rig.h, 44);  // E
  CHECK(rig.led(44) == app::kLedMid);
  CHECK(rig.led(32) == app::kLedDim);
  pad(*rig.h, 32);
  pad(*rig.h, 5);
  tap(*rig.h, app::Button::F7);  // FOLLOW
  tap(*rig.h, app::Button::Play);
  rig.h->run_ms(900);
  CHECK(rig.led(32) == app::kLedOn);  // A is playing
  CHECK(rig.count(24, 31, app::kLedOn) + rig.count(36, 43, app::kLedOn) == 1u);  // the playhead
  CHECK(rig.count(24, 31, app::kLedMid) + rig.count(36, 43, app::kLedMid) == 0u);  // the cursor is on it
  tap(*rig.h, app::Button::F7);                 // FOLLOW off: the cursor stays where it was
  rig.h->run_ms(300);
  CHECK(rig.h->runtime().frame_failures() == 0u);
  CHECK(rig.h->display().peak_frame_words() < 2048u);
}

TEST_CASE("pattern: HELP gives a row to every button the screen answers") {
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
  for (const app::Button b : {app::Button::Up, app::Button::Down, app::Button::Left, app::Button::Right, app::Button::Shift, app::Button::Copy,
                              app::Button::Insert, app::Button::Delete,
                              app::Button::Params, app::Button::Performance, app::Button::Play, app::Button::Master, app::Button::File,
                              app::Button::SampleLoad, app::Button::SamplePlay, app::Button::SampleEdit, app::Button::SampleRec,
                              app::Button::Song, app::Button::Pattern, app::Button::Note, app::Button::Instr, app::Button::Vol,
                              app::Button::Fx}) {
    INFO(std::string(app::button_name(b)));
    CHECK(has_row(help, b));
  }
  for (unsigned k = 0; k < 8; ++k) {
    CHECK(help.fkey[k] != nullptr);
  }
  CHECK(help.pad_group[0] != 0u);   // the palette
  CHECK(help.pad_group[24] != 0u);  // the steps
  CHECK(help.pad_group[32] != 0u);  // the patterns
  CHECK(help.pad_group[24] != help.pad_group[32]);
  CHECK(help.pad_label[24] != nullptr);
  CHECK(help.pad_label[32] != nullptr);
  CHECK(help.pad_label[5] != nullptr);
}
