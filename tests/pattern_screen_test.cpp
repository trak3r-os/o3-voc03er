// SPDX-License-Identifier: GPL-3.0-or-later
// The PATTERN screen (U6b): the pad map and the LEVEL palette as pure logic, then the screen on the simulator, read through the pad LEDs.

#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/platform/host/fake_capture.h>
#include <o3sdk/sim/harness.h>
#include <o3_voc03er/units/tract/tract.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include "../app/src/pattern_logic.h"
#include "tract_skip.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace pt = o3_voc03er::pattern;
namespace pd = o3_voc03er::pads;

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
  CHECK(pt::palette_matches(pt::Lane::Slice, 4, get_step(p, 0), 0, o3_voc03er::pads::Layout{}));
  CHECK_FALSE(pt::palette_matches(pt::Lane::Slice, 5, get_step(p, 0), 0, o3_voc03er::pads::Layout{}));
  CHECK_FALSE(pt::palette_matches(pt::Lane::Slice, 0, get_step(p, 5), 0, o3_voc03er::pads::Layout{}));  // no slice: nothing matches
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

TEST_CASE("pattern: the palette plays what the layout says, row 0 is A and row 1 is B") {
  const pd::Layout def;  // A NOTE, B TRIAD, C FORMANT: B +0, A +1
  const unsigned a_pad = o3sdk::app::pad_at(0, 3);
  const unsigned b_pad = o3sdk::app::pad_at(1, 3);
  CHECK(pt::palette_play(a_pad, 0, def).note == 48 + 12 + 3);
  CHECK(pt::palette_play(a_pad, 0, def).kind == 0);
  CHECK(pt::palette_play(b_pad, 0, def).note == 48 + 3);
  CHECK(pt::palette_play(b_pad, 0, def).kind == 1);
  CHECK(pt::palette_play(b_pad, 2, def).note == 48 + 24 + 3);
  // A parameter strip row plays nothing and writes nothing, and the rows left alone keep their octave (a strip gives none).
  pd::Layout strip;
  pd::set_strip(strip, pd::Row::A, pd::strip_id(2, 0));
  CHECK(pt::palette_play(a_pad, 0, strip).note == -1);
  CHECK(pt::palette_play(b_pad, 0, strip).note == 48 + 3);
  // OFF plays nothing.
  pd::Layout off;
  pd::set_plays(off, pd::Row::B, pd::Plays::Off);
  CHECK(pt::palette_play(b_pad, 0, off).note == -1);
}

TEST_CASE("pattern: a step matches the palette pad whose note and kind it stores") {
  const pd::Layout def;
  const unsigned a_pad = o3sdk::app::pad_at(0, 3);
  const unsigned b_pad = o3sdk::app::pad_at(1, 3);
  o3_voc03er::Step s;
  s.note = 63;  // A's pad 3
  CHECK(pt::palette_matches(pt::Lane::Note, a_pad, s, 0, def));
  CHECK_FALSE(pt::palette_matches(pt::Lane::Note, b_pad, s, 0, def));
  CHECK_FALSE(pt::palette_matches(pt::Lane::Note, a_pad, s, 1, def));  // another octave
  s.note = 51;  // B's pad 3 is a triad
  CHECK_FALSE(pt::palette_matches(pt::Lane::Note, b_pad, s, 0, def));  // a note, not the triad
  s.kind = 1;
  CHECK(pt::palette_matches(pt::Lane::Note, b_pad, s, 0, def));
  CHECK_FALSE(pt::palette_matches(pt::Lane::Note, a_pad, s, 0, def));
  // A rest matches nothing, and a pad that plays nothing matches nothing.
  CHECK_FALSE(pt::palette_matches(pt::Lane::Note, b_pad, o3_voc03er::Step{}, 0, def));
  pd::Layout off;
  pd::set_plays(off, pd::Row::B, pd::Plays::Off);
  CHECK_FALSE(pt::palette_matches(pt::Lane::Note, b_pad, s, 0, off));
  // The other lanes are as they were.
  o3_voc03er::Step lv;
  lv.level = static_cast<std::uint8_t>(pt::level_of_pad(5));
  CHECK(pt::palette_matches(pt::Lane::Level, 5, lv, 0, def));
  CHECK_FALSE(pt::palette_matches(pt::Lane::Level, 6, lv, 0, def));
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
  CHECK(pt::step_value(p, 0, pt::Lane::Carrier, 4));  // LC1: 3 + 4 is LIVE (7), the last value; one more is refused
  CHECK(o3_voc03er::get_step(p, 0).carrier == 7u);
  CHECK_FALSE(pt::step_value(p, 0, pt::Lane::Carrier, 1));
  CHECK(pt::step_value(p, 0, pt::Lane::Carrier, -4));
  CHECK(o3_voc03er::get_step(p, 0).carrier == 3u);
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
void shift_tap(sim::Harness& h, app::Button b) {
  h.press(app::Button::Shift);
  h.run_ms(5);
  tap(h, b);
  h.release(app::Button::Shift);
  h.run_ms(5);
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

namespace {
unsigned step_pad(unsigned i) { return i < 8u ? 24u + i : 36u + (i - 8u); }
}  // namespace

TEST_CASE("pattern: the palette writes the layout: a note on A, a triad on B, the cursor moves on") {
  Rig rig;
  pad(*rig.h, 3);   // A, a note, step 1
  pad(*rig.h, 15);  // B, a TRIAD, step 2
  pad(*rig.h, step_pad(0));
  CHECK(rig.led(3) == app::kLedMid);  // step 1 holds A's note: the pad that plays it is mid
  CHECK(rig.led(15) != app::kLedMid);
  pad(*rig.h, step_pad(1));
  CHECK(rig.led(15) == app::kLedMid);  // step 2 holds B's triad
  CHECK(rig.led(3) != app::kLedMid);
  CHECK(rig.led(27) == app::kLedOff);  // two steps written, the cursor is on step 2
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: F6 ROW picks A or B and F7 PLAYS steps that row through the notes and then the mode's parameters") {
  Rig rig;
  tap(*rig.h, app::Button::F6);  // focus ROW
  tap(*rig.h, app::Button::F6);  // a second tap: B
  tap(*rig.h, app::Button::F7);  // focus PLAYS
  tap(*rig.h, app::Button::F7);  // B: TRIAD to 7TH
  pad(*rig.h, 15);               // step 1: B's 7TH
  pad(*rig.h, step_pad(0));
  CHECK(rig.led(15) == app::kLedMid);
  tap(*rig.h, app::Button::F6);  // focus ROW again
  tap(*rig.h, app::Button::F6);  // a second tap: back to A
  tap(*rig.h, app::Button::F7);
  tap(*rig.h, app::Button::F7);  // A: NOTE to TRIAD
  pad(*rig.h, step_pad(1));
  pad(*rig.h, 3);  // step 2: A's triad now (a kind-1 step on A's pad 3)
  pad(*rig.h, step_pad(1));
  CHECK(rig.led(3) == app::kLedMid);
  CHECK(rig.led(15) != app::kLedMid);  // B plays 7TH: the step's kind 1 is not its pad's
  // The encoder on PLAYS steps and stops at the ends: the last parameter strip at the top (it writes nothing here, so the row is dark), NOTE at the bottom.
  rig.h->turn(30);
  rig.h->run_ms(60);
  CHECK(rig.count(0, 11, app::kLedOff) == 12u);  // A plays a strip: its row is dark
  tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F6);  // ROW to B
  rig.h->turn(30);
  rig.h->run_ms(60);
  CHECK(rig.count(12, 23, app::kLedOff) == 12u);  // B plays a strip too
  rig.h->turn(-30);
  rig.h->run_ms(60);
  CHECK(rig.count(12, 23, app::kLedOff) < 12u);  // and NOTE at the bottom: lit again
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: a parameter strip or an OFF row writes nothing and its pads are dark") {
  Rig rig;
  tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F6);  // ROW B
  tap(*rig.h, app::Button::F7);  // focus PLAYS
  rig.h->turn(30);               // B to the last parameter strip
  rig.h->run_ms(60);
  CHECK(rig.count(12, 23, app::kLedOff) == 12u);
  CHECK(rig.led(24) == app::kLedMid);
  pad(*rig.h, 14);  // a strip row's pad writes nothing: the cursor stays on step 1
  CHECK(rig.led(24) == app::kLedMid);
  CHECK(rig.led(25) == app::kLedOff);
  pad(*rig.h, 3);  // A is still NOTE: it writes step 1 and the cursor moves on
  pad(*rig.h, step_pad(0));
  CHECK(rig.led(3) == app::kLedMid);
  // B to OFF (the sixth entry): the same, dark and silent.
  rig.h->turn(-30);
  rig.h->run_ms(60);
  for (unsigned i = 0; i < 5; ++i) {
    rig.h->turn(1);
  }
  rig.h->run_ms(60);
  CHECK(rig.count(12, 23, app::kLedOff) == 12u);
  pad(*rig.h, step_pad(5));
  pad(*rig.h, 14);
  CHECK(rig.led(step_pad(5)) == app::kLedMid);
  CHECK(rig.led(step_pad(6)) == app::kLedOff);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: SHIFT + a palette pad writes nothing yet; a step pad still writes a rest") {
  Rig rig;
  pad(*rig.h, 3, true);
  CHECK(rig.led(24) == app::kLedMid);  // the cursor did not move
  CHECK(rig.led(25) == app::kLedOff);
  pad(*rig.h, step_pad(4));
  CHECK(rig.led(24) == app::kLedOff);  // step 1 is still empty
  pad(*rig.h, 3);                      // a note at step 5
  pad(*rig.h, step_pad(4), true);      // SHIFT + step: a rest
  CHECK(rig.led(step_pad(4)) == app::kLedMid);
  pad(*rig.h, step_pad(0));
  CHECK(rig.led(step_pad(4)) == app::kLedOff);
}

TEST_CASE("pattern: F6 and F7 do nothing on LEVEL, CARRIER and SLICE") {
  Rig rig;
  tap(*rig.h, app::Button::Vol);
  tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F7);
  tap(*rig.h, app::Button::F7);
  tap(*rig.h, app::Button::Note);
  // the layout was not touched: A NOTE (an octave up), B TRIAD: a write from A and from B still matches A's and B's default pads
  pad(*rig.h, 3);
  pad(*rig.h, 15);
  pad(*rig.h, step_pad(0));
  CHECK(rig.led(3) == app::kLedMid);
  pad(*rig.h, step_pad(1));
  CHECK(rig.led(15) == app::kLedMid);
  tap(*rig.h, app::Button::Instr);
  tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F7);
  tap(*rig.h, app::Button::F7);
  tap(*rig.h, app::Button::Note);
  pad(*rig.h, step_pad(2));
  pad(*rig.h, 3);
  pad(*rig.h, step_pad(2));
  CHECK(rig.led(3) == app::kLedMid);  // still A's note, unchanged
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: changing F6's row flashes that row's LEDs once") {
  Rig rig;
  tap(*rig.h, app::Button::F6);  // focus
  CHECK(rig.count(12, 23, app::kLedOff) < 12u);
  tap(*rig.h, app::Button::F6);  // B: its row goes dark for a moment
  CHECK(rig.count(12, 23, app::kLedOff) == 12u);
  rig.h->run_ms(300);
  CHECK(rig.count(12, 23, app::kLedOff) < 12u);
  CHECK(rig.count(0, 11, app::kLedOff) < 12u);
}

TEST_CASE("pattern: SHIFT + F6 asks to clear the lane and F1 confirms; SHIFT + F7 is FOLLOW") {
  Rig rig;
  pad(*rig.h, 3);
  pad(*rig.h, 5);
  CHECK(rig.led(24) == app::kLedDim);
  shift_tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F1);
  CHECK(rig.led(24) == app::kLedOff);
  CHECK(rig.led(25) == app::kLedOff);
  // F6 alone only focuses ROW, it clears nothing.
  pad(*rig.h, step_pad(0));
  pad(*rig.h, 3);
  tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F1);
  CHECK(rig.led(24) == app::kLedDim);
  // FOLLOW is SHIFT + F7 (its own test case plays the pattern).
  shift_tap(*rig.h, app::Button::F7);
  shift_tap(*rig.h, app::Button::F7);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: LANE (the buttons and F2) picks what the palette writes and the LEDs show") {
  Rig rig;
  tap(*rig.h, app::Button::Instr);  // CARRIER: eight values
  CHECK(rig.count(0, 23, app::kLedOff) == 16u);
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
  CHECK(rig.count(0, 23, app::kLedOff) > 0u);  // NOTE mirrors PERFORM's rows: the scale lit, the rest dark
  CHECK(rig.count(0, 23, app::kLedOff) < 24u);
  tap(*rig.h, app::Button::F2);  // focus LANE
  tap(*rig.h, app::Button::F2);  // a tap steps it: CARRIER
  CHECK(rig.count(0, 23, app::kLedOff) == 16u);
  tap(*rig.h, app::Button::Down);
  tap(*rig.h, app::Button::Up);
  CHECK(rig.count(0, 23, app::kLedOff) == 16u);  // UP and DOWN are the octave now: the lane stays CARRIER
}

// LC1: the CARRIER palette is FOLLOW, SYNTH, NOISE, SLOT 1..4 and LIVE on pads 0..7; the other 16 are dark.
TEST_CASE("pattern: the CARRIER palette ends with LIVE, its cell and pad name, and the step's match (the logic)") {
  static_assert(pt::kCarrierPads == 8u, "FOLLOW, SYNTH, NOISE, four slots and LIVE");
  CHECK(std::strcmp(pt::kPadCarrier[7], "LIVE") == 0);
  CHECK(std::strcmp(pt::kCellCarrier[7], "LIV") == 0);
  CHECK(std::strcmp(pt::kPadCarrier[6], "SLOT4") == 0);  // the ids before it did not move
  o3_voc03er::PatternData p{};
  REQUIRE(o3_voc03er::set_carrier(p, 0, 7));
  const o3_voc03er::Step s = o3_voc03er::get_step(p, 0);
  const pd::Layout layout{};
  for (unsigned pad = 0; pad < pt::kPalettePads; ++pad) {
    CHECK(pt::palette_matches(pt::Lane::Carrier, pad, s, 0, layout) == (pad == 7u));
  }
}

TEST_CASE("pattern: a LIVE pad writes the step and lights; the pads after it are dark") {
  Rig rig;
  tap(*rig.h, app::Button::Instr);  // CARRIER
  CHECK(rig.led(7) != app::kLedOff);  // the LIVE pad is lit, and
  CHECK(rig.led(8) == app::kLedOff);  // the next is dark
  pad(*rig.h, 7);                     // LIVE into step 1; the cursor moves on
  pad(*rig.h, 8);                     // a dark pad writes nothing
  pad(*rig.h, 24);                    // back on step 1
  CHECK(rig.led(7) == app::kLedMid);  // the palette shows what the step holds
  CHECK(rig.led(0) == app::kLedDim);
  CHECK(rig.count(0, 23, app::kLedOff) == 16u);
  CHECK(rig.h->runtime().frame_failures() == 0u);
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
  shift_tap(*rig.h, app::Button::F6);  // CLEAR LANE is allowed and changes nothing here
  tap(*rig.h, app::Button::F1);
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
  shift_tap(*rig.h, app::Button::F6);  // CLEAR LANE asks first
  CHECK(rig.led(25) != app::kLedOff);  // nothing cleared yet
  tap(*rig.h, app::Button::F2);        // any other key cancels
  CHECK(rig.led(25) != app::kLedOff);
  shift_tap(*rig.h, app::Button::F6);
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
  auto shifted = [&h](app::Button b) { shift_tap(h, b); };
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
  shift_tap(*rig.h, app::Button::F7);  // FOLLOW
  tap(*rig.h, app::Button::Play);
  rig.h->run_ms(900);
  CHECK(rig.led(32) == app::kLedOn);  // A is playing
  CHECK(rig.count(24, 31, app::kLedOn) + rig.count(36, 43, app::kLedOn) == 1u);  // the playhead
  CHECK(rig.count(24, 31, app::kLedMid) + rig.count(36, 43, app::kLedMid) == 0u);  // the cursor is on it
  shift_tap(*rig.h, app::Button::F7);           // FOLLOW off: the cursor stays where it was
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
  CHECK(help.fkey_shift[5] != nullptr);  // CLEAR LANE
  CHECK(help.fkey_shift[6] != nullptr);  // FOLLOW
  CHECK(help.pad_group[0] != 0u);   // the palette
  CHECK(help.pad_group[24] != 0u);  // the steps
  CHECK(help.pad_group[32] != 0u);  // the patterns
  CHECK(help.pad_group[24] != help.pad_group[32]);
  CHECK(help.pad_label[24] != nullptr);
  CHECK(help.pad_label[32] != nullptr);
  CHECK(help.pad_label[5] != nullptr);
}

// ---- V3: the MOUTH lane (the fifth) --------------------------------------------------------------------------------------------------------

TEST_CASE("pattern: MOUTH is the fifth lane; its palette is PERFORM's strip and the same with GLD") {
  using o3_voc03er::kMouthGld;
  CHECK(pt::kLanes == 5u);
  CHECK(static_cast<unsigned>(pt::Lane::Mouth) == 4u);  // appended: the others keep their numbers
  CHECK(std::string(pt::kLaneNames[4]) == "MOUTH");
  CHECK(std::string(pt::kClearPrompt[4]) == "CLEAR MOUTH LANE: F1 CONFIRMS");
  for (unsigned c = 0; c < 12; ++c) {
    CHECK(pt::mouth_of_pad(c) == c + 1u);  // row 0: nine vowels, LIPS, TIP, BACK
  }
  for (unsigned c = 0; c < 9; ++c) {
    CHECK(pt::mouth_of_pad(12 + c) == (c + 1u) + kMouthGld);  // row 1: the vowels with GLD
  }
  for (unsigned c = 9; c < 12; ++c) {
    CHECK(pt::mouth_of_pad(12 + c) == 0u);  // dark
  }
  CHECK(pt::mouth_of_pad(24) == 0u);
  char t[6];
  pt::mouth_text(t, 0);
  CHECK(std::string(t) == "");
  pt::mouth_text(t, 1);
  CHECK(std::string(t) == "A");
  pt::mouth_text(t, 2);
  CHECK(std::string(t) == "AE");
  pt::mouth_text(t, 9);
  CHECK(std::string(t) == "U");
  pt::mouth_text(t, 3 + kMouthGld);
  CHECK(std::string(t) == ">E");
  pt::mouth_text(t, 10);
  CHECK(std::string(t) == "LIP");
  pt::mouth_text(t, 11);
  CHECK(std::string(t) == "TIP");
  pt::mouth_text(t, 12);
  CHECK(std::string(t) == "BCK");
}

TEST_CASE("pattern: the MOUTH lane's writes, values, rests and the clipboard (the logic)") {
  using o3_voc03er::PatternData;
  using o3_voc03er::get_mouth;
  using o3_voc03er::kMouthGld;
  PatternData p{};
  (void)o3_voc03er::set_length(p, 4);
  CHECK(pt::mouth_write(p, 0, 4));  // E-I
  CHECK(get_mouth(p, 0) == 5u);
  CHECK(pt::mouth_write(p, 1, 12 + 2));  // E with GLD
  CHECK(get_mouth(p, 1) == 3u + kMouthGld);
  CHECK_FALSE(pt::mouth_write(p, 2, 21));  // a dark pad
  CHECK(get_mouth(p, 2) == 0u);
  CHECK(pt::mouth_write(p, 2, 10));  // TIP
  CHECK(get_mouth(p, 2) == 11u);
  CHECK(pt::palette_matches(pt::Lane::Mouth, 4, o3_voc03er::Step{}, 0, pd::Layout{}, 5u));
  CHECK_FALSE(pt::palette_matches(pt::Lane::Mouth, 4, o3_voc03er::Step{}, 0, pd::Layout{}, 0u));  // FOLLOW matches no pad
  // the encoder: a vowel keeps GLD, a closure is not a vowel, the range is 0..12
  CHECK(pt::step_value(p, 1, pt::Lane::Mouth, 1));
  CHECK(get_mouth(p, 1) == 4u + kMouthGld);
  // the walk: 0 FOLLOW, 1..9 vowels, 10..12 closures, 13..20 the words, 21..26 the WOB rates; a vowel keeps GLD, everything else drops it
  CHECK(pt::step_value(p, 1, pt::Lane::Mouth, 8));  // vowel 4 + GLD -> 12 BACK
  CHECK(get_mouth(p, 1) == 12u);
  CHECK(pt::step_value(p, 1, pt::Lane::Mouth, 1));  // 13: the first word, WOW
  CHECK(get_mouth(p, 1) == o3_voc03er::mouth_word_byte(0));
  CHECK(pt::step_value(p, 1, pt::Lane::Mouth, 7));  // 20: GA
  CHECK(get_mouth(p, 1) == o3_voc03er::mouth_word_byte(7));
  CHECK(pt::step_value(p, 1, pt::Lane::Mouth, 1));  // 21: the first WOB, 1/4
  CHECK(get_mouth(p, 1) == o3_voc03er::mouth_wob_byte(0));
  CHECK(pt::step_value(p, 1, pt::Lane::Mouth, 20));  // clamped at 26: 1/16T
  CHECK(get_mouth(p, 1) == o3_voc03er::mouth_wob_byte(5));
  CHECK_FALSE(pt::step_value(p, 1, pt::Lane::Mouth, 1));
  CHECK(pt::step_value(p, 1, pt::Lane::Mouth, -40));
  CHECK(get_mouth(p, 1) == 0u);
  // COPY and INSERT carry the byte, DELETE clears it, a whole pattern carries all four
  pt::Clip c;
  pt::copy_step(c, p, 0);
  CHECK(c.mouth == 5u);
  REQUIRE(pt::insert_step(p, 1, c));
  CHECK(get_mouth(p, 0) == 5u);
  CHECK(get_mouth(p, 1) == 5u);
  CHECK(get_mouth(p, 3) == 11u);  // the old step 3 (TIP) came to the end of LENGTH
  CHECK(get_mouth(p, 2) == 0u);
  REQUIRE(pt::clear_step(p, 3));
  CHECK(get_mouth(p, 3) == 0u);
  CHECK(pt::rest_lane(p, 0, pt::Lane::Mouth));
  CHECK(get_mouth(p, 0) == 0u);
  CHECK(pt::mouth_write(p, 0, 0));
  pt::Clip whole;
  pt::copy_whole(whole, p);
  PatternData q{};
  REQUIRE(pt::paste_whole(q, whole));
  CHECK(get_mouth(q, 1) == 5u);
  CHECK(o3_voc03er::same_pattern(p, q));
  (void)o3_voc03er::set_mouth(q, 9, 1);
  CHECK_FALSE(o3_voc03er::same_pattern(p, q));
  pt::clear_steps(q);
  CHECK(o3_voc03er::mouths_used(q) == 0u);
}

namespace {
// The vocoder's mode set straight on its unit (the tract unit is skipped: its ids collide with the vocoder's low ones).
void set_mode(sim::Harness& h, float mode) {
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(h, u)) {
      continue;
    }
    const auto id = static_cast<o3sdk::dsp::UnitId>(u);
    const o3sdk::dsp::UnitInfo* info = h.engine().unit_info(id);
    if (info != nullptr && info->param_count == o3_voc03er::Vocoder::kParamCount) {
      (void)h.engine().set_param(id, o3_voc03er::Vocoder::Mode, mode);
    }
  }
  h.run_ms(300);
}
float tract_value(sim::Harness& h, std::uint16_t id) {
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(h, u)) {
      return h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    }
  }
  return -1.0e9f;
}
}  // namespace

TEST_CASE("pattern: FX goes SLICE, MOUTH, SLICE; F2 taps through the five lanes") {
  Rig rig;
  set_mode(*rig.h, 4.0f);
  tap(*rig.h, app::Button::Fx);  // SLICE: dark
  CHECK(rig.count(0, 23, app::kLedOff) == 24u);
  tap(*rig.h, app::Button::Fx);  // MOUTH: twenty-one pads lit, three dark
  CHECK(rig.count(0, 23, app::kLedOff) == 3u);
  CHECK(rig.led(21) == app::kLedOff);
  CHECK(rig.led(22) == app::kLedOff);
  CHECK(rig.led(23) == app::kLedOff);
  CHECK(rig.led(20) == app::kLedDim);
  tap(*rig.h, app::Button::Fx);  // back to SLICE
  CHECK(rig.count(0, 23, app::kLedOff) == 24u);
  tap(*rig.h, app::Button::Note);
  tap(*rig.h, app::Button::F2);  // focus LANE
  for (unsigned i = 0; i < 4; ++i) {
    tap(*rig.h, app::Button::F2);  // CARRIER, LEVEL, SLICE, MOUTH
  }
  CHECK(rig.count(0, 23, app::kLedOff) == 3u);
  tap(*rig.h, app::Button::F2);  // wraps to NOTE
  CHECK(rig.count(0, 23, app::kLedOff) == 24u);  // TRACT's own layout is parameter strips (A VOWEL, B TONGUE): they write no notes, so the palette is dark
  set_mode(*rig.h, 0.0f);  // BANDS has its own layout (A NOTE, B TRIAD): the palette is lit
  CHECK(rig.count(0, 23, app::kLedOff) < 24u);
  CHECK(rig.count(0, 23, app::kLedOff) != 3u);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: a MOUTH pad writes the cursor's step and moves on; GLD is row 1; a dark pad does nothing") {
  Rig rig;
  set_mode(*rig.h, 4.0f);
  tap(*rig.h, app::Button::Fx);
  tap(*rig.h, app::Button::Fx);
  CHECK(rig.led(24) == app::kLedMid);  // the cursor, empty: FOLLOW
  pad(*rig.h, 2);                      // E
  CHECK(rig.led(24) == app::kLedDim);  // content
  CHECK(rig.led(25) == app::kLedMid);  // the cursor moved on
  pad(*rig.h, 14);                     // E with GLD, into step 2
  CHECK(rig.led(25) == app::kLedDim);
  CHECK(rig.led(26) == app::kLedMid);
  pad(*rig.h, 21);  // dark: nothing, the cursor stays
  CHECK(rig.led(26) == app::kLedMid);
  CHECK(rig.led(27) == app::kLedOff);
  pad(*rig.h, 24);  // back on step 1: its pad is the strip's E, not the GLD row's
  CHECK(rig.led(2) == app::kLedMid);
  CHECK(rig.led(14) == app::kLedDim);
  pad(*rig.h, 25);
  CHECK(rig.led(14) == app::kLedMid);
  CHECK(rig.led(2) == app::kLedDim);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: with a step held a MOUTH pad writes that step; SHIFT + a palette pad only sounds; SHIFT + a step clears it") {
  Rig rig;
  set_mode(*rig.h, 4.0f);
  tap(*rig.h, app::Button::Fx);
  tap(*rig.h, app::Button::Fx);
  const unsigned s5 = step_pad(4), s7 = step_pad(6);
  rig.h->pad_down(s5);
  rig.h->run_ms(20);
  rig.h->pad_down(s7);
  rig.h->run_ms(20);
  pad(*rig.h, 9);  // LIPS into both held steps
  rig.h->pad_up(s7);
  rig.h->run_ms(20);
  rig.h->pad_up(s5);
  rig.h->run_ms(20);
  CHECK(rig.led(s5) == app::kLedDim);
  CHECK(rig.led(s7) == app::kLedMid);  // the cursor stays on the last step pressed, which holds the mouth too
  CHECK(rig.led(step_pad(5)) == app::kLedOff);
  pad(*rig.h, s5);
  CHECK(rig.led(9) == app::kLedMid);
  pad(*rig.h, s7);
  CHECK(rig.led(9) == app::kLedMid);
  // SHIFT + a palette pad: the sound, never the step
  pad(*rig.h, 3, true);
  CHECK(rig.led(3) == app::kLedDim);
  CHECK(rig.led(9) == app::kLedMid);
  // SHIFT + a step pad: FOLLOW again
  pad(*rig.h, s5, true);
  CHECK(rig.led(s5) == app::kLedMid);  // the cursor is there, and it holds nothing now
  CHECK(rig.led(s7) == app::kLedDim);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: in TRACT a MOUTH pad sounds: a vowel sets VOWEL, a closure holds CLOSE while it is down; elsewhere it is silent") {
  using o3_voc03er::TractUnit;
  Rig rig;
  tap(*rig.h, app::Button::Fx);
  tap(*rig.h, app::Button::Fx);
  const float v0 = tract_value(*rig.h, TractUnit::Vowel);
  pad(*rig.h, 8, true);  // U, not in TRACT: nothing sounds
  CHECK(tract_value(*rig.h, TractUnit::Vowel) == doctest::Approx(v0));
  set_mode(*rig.h, 4.0f);
  rig.h->press(app::Button::Shift);
  rig.h->run_ms(5);
  rig.h->pad_down(4);  // I, the fifth vowel: (5 - 1) / 8
  rig.h->run_ms(60);
  CHECK(tract_value(*rig.h, TractUnit::Vowel) == doctest::Approx(0.5f));
  rig.h->pad_up(4);
  rig.h->run_ms(20);
  rig.h->pad_down(10);  // TIP: held
  rig.h->run_ms(60);
  CHECK(tract_value(*rig.h, TractUnit::Close) == doctest::Approx(2.0f));
  rig.h->pad_up(10);
  rig.h->run_ms(60);
  CHECK(tract_value(*rig.h, TractUnit::Close) == doctest::Approx(0.0f));
  rig.h->release(app::Button::Shift);
  rig.h->run_ms(20);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: the MOUTH lane draws inside the display list's limit, in and out of TRACT") {
  Rig rig;
  tap(*rig.h, app::Button::Fx);
  tap(*rig.h, app::Button::Fx);
  for (unsigned i = 0; i < 6; ++i) {
    pad(*rig.h, i % 12);  // a line of vowels
  }
  rig.h->run_ms(100);
  CHECK(rig.h->display().peak_frame_words() < 2048u);
  set_mode(*rig.h, 4.0f);
  rig.h->run_ms(100);
  CHECK(rig.h->display().peak_frame_words() < 2048u);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

// ---- TRACT V3b: the words, the WOB rates and FILL on the MOUTH lane -------------------------------------------------------------------------

TEST_CASE("pattern: the WORDS page maps row 0 to the words and row 1 to the WOB rates, the rest is dark (the logic)") {
  using o3_voc03er::mouth_word_byte;
  using o3_voc03er::mouth_wob_byte;
  const auto W = pt::MouthPage::Words;
  for (unsigned c = 0; c < 12; ++c) {
    CHECK(pt::mouth_of_pad(c, W) == (c < 8u ? mouth_word_byte(c) : std::uint8_t{0}));
    CHECK(pt::mouth_of_pad(12u + c, W) == (c < 6u ? mouth_wob_byte(c) : std::uint8_t{0}));
  }
  CHECK(pt::mouth_of_pad(24, W) == 0u);  // a step pad is no palette pad
  // the VOWELS page is as it was
  CHECK(pt::mouth_of_pad(0) == 1u);
  CHECK(pt::mouth_of_pad(14) == (3u | o3_voc03er::kMouthGld));
  CHECK(pt::mouth_of_pad(14, pt::MouthPage::Vowels) == pt::mouth_of_pad(14));
  // the pad that holds a word is mid on the WORDS page and on no other
  CHECK(pt::palette_matches(pt::Lane::Mouth, 5, o3_voc03er::Step{}, 0, pd::Layout{}, mouth_word_byte(5), W));
  CHECK_FALSE(pt::palette_matches(pt::Lane::Mouth, 5, o3_voc03er::Step{}, 0, pd::Layout{}, mouth_word_byte(5)));
  CHECK(pt::palette_matches(pt::Lane::Mouth, 12u + 3u, o3_voc03er::Step{}, 0, pd::Layout{}, mouth_wob_byte(3), W));
  // the cells' texts
  char t[6];
  const char* const words[8] = {"WOW", "YEA", "YOI", "WAH", "OW", "BA", "DA", "GA"};
  for (unsigned w = 0; w < 8; ++w) {
    pt::mouth_text(t, mouth_word_byte(w));
    CHECK(std::string(t) == words[w]);
  }
  const char* const rates[6] = {"~4", "~8", "~16", "~32", "~8T", "~16T"};
  for (unsigned r = 0; r < 6; ++r) {
    pt::mouth_text(t, mouth_wob_byte(r));
    CHECK(std::string(t) == rates[r]);
  }
  // writing a word or a WOB through a pad
  o3_voc03er::PatternData p{};
  CHECK(pt::mouth_write(p, 0, 7, W));
  CHECK(o3_voc03er::get_mouth(p, 0) == mouth_word_byte(7));
  CHECK(pt::mouth_write(p, 1, 12u + 5u, W));
  CHECK(o3_voc03er::get_mouth(p, 1) == mouth_wob_byte(5));
  CHECK_FALSE(pt::mouth_write(p, 2, 8, W));  // dark on this page
  CHECK(o3_voc03er::get_mouth(p, 2) == 0u);
  CHECK(std::string(pt::kWobPlays) == "WOB PLAYS WITH THE LINE");
  CHECK(std::string(pt::kFilled) == "MOUTH FILLED: F7 AGAIN, SHIFT + F6 CLEARS");
  CHECK(std::string(pt::kFillPrompt) == "FILL MOUTH LANE: F1 CONFIRMS");
}

TEST_CASE("pattern: the encoder walks the 27 MOUTH values up and back down, GLD only on a vowel") {
  using o3_voc03er::get_mouth;
  o3_voc03er::PatternData p{};
  for (unsigned expect = 1; expect <= 26; ++expect) {
    CHECK(pt::step_value(p, 0, pt::Lane::Mouth, 1));
    const std::uint8_t m = get_mouth(p, 0);
    CHECK(o3_voc03er::mouth_valid(m));
    if (expect >= 21u) {
      CHECK(o3_voc03er::mouth_is_wob(m));
      CHECK(o3_voc03er::mouth_arg(m) == expect - 21u);
    } else if (expect >= 13u) {
      CHECK(o3_voc03er::mouth_is_word(m));
      CHECK(o3_voc03er::mouth_arg(m) == expect - 13u);
    } else {
      CHECK(m == expect);
    }
  }
  CHECK_FALSE(pt::step_value(p, 0, pt::Lane::Mouth, 1));
  for (unsigned down = 0; down < 26; ++down) {
    CHECK(pt::step_value(p, 0, pt::Lane::Mouth, -1));
    CHECK(o3_voc03er::mouth_valid(get_mouth(p, 0)));
  }
  CHECK(get_mouth(p, 0) == 0u);
  CHECK_FALSE(pt::step_value(p, 0, pt::Lane::Mouth, -1));
  // a vowel with GLD loses it on the way to a word and does not get it back
  CHECK(o3_voc03er::set_mouth(p, 0, 2u | o3_voc03er::kMouthGld));
  CHECK(pt::step_value(p, 0, pt::Lane::Mouth, 11));  // 13: WOW
  CHECK(get_mouth(p, 0) == o3_voc03er::mouth_word_byte(0));
  CHECK(pt::step_value(p, 0, pt::Lane::Mouth, -11));
  CHECK(get_mouth(p, 0) == 2u);
}

TEST_CASE("pattern: FILL's random walk is seeded, valid, moves by two at most and stops at LENGTH (the logic)") {
  using o3_voc03er::get_mouth;
  o3_voc03er::PatternData a{}, b{}, c{};
  pt::fill_mouth(a, 16, pt::fill_seed(0, 0));
  pt::fill_mouth(b, 16, pt::fill_seed(0, 0));
  pt::fill_mouth(c, 16, pt::fill_seed(1, 0));
  bool same_ab = true, same_ac = true;
  for (unsigned i = 0; i < 16; ++i) {
    same_ab = same_ab && get_mouth(a, i) == get_mouth(b, i);
    same_ac = same_ac && get_mouth(a, i) == get_mouth(c, i);
  }
  CHECK(same_ab);
  CHECK_FALSE(same_ac);
  // many seeds: every byte is FOLLOW or a vowel (GLD allowed), and the walk is at most two vowels a step across a FOLLOW too
  for (std::uint32_t n = 0; n < 200; ++n) {
    o3_voc03er::PatternData p{};
    pt::fill_mouth(p, 16, pt::fill_seed(n, n % 8u));
    int last = -1;
    for (unsigned i = 0; i < 16; ++i) {
      const std::uint8_t m = get_mouth(p, i);
      REQUIRE(o3_voc03er::mouth_valid(m));
      if (m == 0u) {
        continue;
      }
      REQUIRE(o3_voc03er::mouth_is_vowel(m));
      const int v = static_cast<int>(o3_voc03er::mouth_value(m));
      if (last >= 0) {
        CHECK(std::abs(v - last) <= 2);
      }
      last = v;
    }
  }
  // LENGTH 8: steps 8..15 are left as they were
  o3_voc03er::PatternData q{};
  REQUIRE(o3_voc03er::set_mouth(q, 12, 3));
  pt::fill_mouth(q, 8, pt::fill_seed(4, 2));
  CHECK(get_mouth(q, 12) == 3u);
  for (unsigned i = 8; i < 16; ++i) {
    if (i != 12u) {
      CHECK(get_mouth(q, i) == 0u);
    }
  }
  // the seeds: never 0, and distinct over the first counters of a pattern
  for (std::uint32_t n = 0; n < 64; ++n) {
    CHECK(pt::fill_seed(n, 0) != 0u);
    for (std::uint32_t m = n + 1; m < 64; ++m) {
      CHECK(pt::fill_seed(n, 0) != pt::fill_seed(m, 0));
    }
  }
  // empty / own output
  o3_voc03er::PatternData e{};
  CHECK(pt::mouth_lane_empty(e));
  pt::fill_mouth(e, 16, pt::fill_seed(0, 0));
  CHECK_FALSE(pt::mouth_lane_empty(e));
  std::uint8_t out[16];
  for (unsigned i = 0; i < 16; ++i) {
    out[i] = get_mouth(e, i);
  }
  CHECK(pt::mouth_lane_is(e, out));
  (void)o3_voc03er::set_mouth(e, 3, 9);
  CHECK_FALSE(pt::mouth_lane_is(e, out));
}

namespace {
// The MOUTH lane with the vocoder in TRACT, palette page VOWELS.
void to_mouth(Rig& rig) {
  set_mode(*rig.h, 4.0f);
  tap(*rig.h, app::Button::Fx);
  tap(*rig.h, app::Button::Fx);
}
// Is the step pad's LED as the line `want` says, step 1..n-1 (step 0 is the cursor)?
bool steps_show(Rig& rig, const o3_voc03er::PatternData& want, unsigned from, unsigned to) {
  for (unsigned i = from; i < to; ++i) {
    const std::uint8_t exp = want.mouth[i] != 0u ? app::kLedDim : app::kLedOff;
    if (rig.led(step_pad(i)) != exp) {
      MESSAGE("step " << i << ": led " << static_cast<unsigned>(rig.led(step_pad(i))) << ", want " << static_cast<unsigned>(exp) << " (mouth byte " << static_cast<unsigned>(want.mouth[i]) << ")");
      return false;
    }
  }
  return true;
}
}  // namespace

TEST_CASE("pattern: F6 SET flips the MOUTH palette VOWELS and WORDS") {
  Rig rig;
  to_mouth(rig);
  CHECK(rig.count(0, 23, app::kLedOff) == 3u);  // VOWELS
  tap(*rig.h, app::Button::F6);
  CHECK(rig.count(0, 23, app::kLedOff) == 10u);  // WORDS: 8 + 6 lit
  CHECK(rig.led(7) == app::kLedDim);
  CHECK(rig.led(8) == app::kLedOff);
  CHECK(rig.led(17) == app::kLedDim);
  CHECK(rig.led(18) == app::kLedOff);
  tap(*rig.h, app::Button::F6);
  CHECK(rig.count(0, 23, app::kLedOff) == 3u);  // back
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: a WORDS pad writes the step and moves on; a WOB pad writes it and leaves VOWEL alone") {
  using o3_voc03er::TractUnit;
  Rig rig;
  to_mouth(rig);
  tap(*rig.h, app::Button::F6);
  pad(*rig.h, 5);  // BA into step 1
  CHECK(rig.led(24) == app::kLedDim);
  CHECK(rig.led(25) == app::kLedMid);
  rig.h->run_ms(500);  // the audition runs its course
  const float v0 = tract_value(*rig.h, TractUnit::Vowel);
  pad(*rig.h, 14);  // the 1/16 WOB into step 2: sounds nothing
  rig.h->run_ms(100);
  CHECK(tract_value(*rig.h, TractUnit::Vowel) == doctest::Approx(v0));
  CHECK(rig.led(25) == app::kLedDim);
  CHECK(rig.led(26) == app::kLedMid);
  pad(*rig.h, 24);  // back on step 1: BA is mid on the WORDS page
  CHECK(rig.led(5) == app::kLedMid);
  CHECK(rig.led(14) != app::kLedMid);
  pad(*rig.h, 25);
  CHECK(rig.led(14) == app::kLedMid);
  CHECK(rig.led(5) != app::kLedMid);
  // a dark pad writes nothing
  pad(*rig.h, 26);
  pad(*rig.h, 9);
  CHECK(rig.led(26) == app::kLedMid);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: F7 FILL writes a seeded line at once over an empty lane, again re-rolls it, and LENGTH is respected") {
  Rig rig;
  to_mouth(rig);
  o3_voc03er::PatternData want{};
  pt::fill_mouth(want, 16, pt::fill_seed(0, 0));
  tap(*rig.h, app::Button::F7);
  CHECK(steps_show(rig, want, 1, 16));
  tap(*rig.h, app::Button::F7);  // its own output: no question, a new line
  o3_voc03er::PatternData want2{};
  pt::fill_mouth(want2, 16, pt::fill_seed(1, 0));
  CHECK(steps_show(rig, want2, 1, 16));
  // LENGTH 8: only the first eight are filled
  shift_tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F1);  // the lane is empty
  CHECK(rig.count(36, 43, app::kLedOff) == 8u);
  tap(*rig.h, app::Button::F3);
  rig.h->turn(-8);
  rig.h->run_ms(60);
  o3_voc03er::PatternData want3{};
  pt::fill_mouth(want3, 8, pt::fill_seed(2, 0));
  tap(*rig.h, app::Button::F7);
  CHECK(steps_show(rig, want3, 1, 8));
  CHECK(rig.led(36) == app::kLedOff);
  CHECK(rig.led(43) == app::kLedOff);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: F7 FILL over a hands-written lane asks, F1 fills and another key cancels") {
  Rig rig;
  to_mouth(rig);
  pad(*rig.h, 2);  // E into step 1
  pad(*rig.h, 4);  // I into step 2
  tap(*rig.h, app::Button::F7);  // asks
  CHECK(rig.led(24) == app::kLedDim);
  CHECK(rig.led(26) == app::kLedMid);  // nothing is filled yet: the cursor on the empty step 3, the rest dark
  CHECK(rig.led(27) == app::kLedOff);
  tap(*rig.h, app::Button::F2);  // any other key cancels
  CHECK(rig.led(27) == app::kLedOff);
  tap(*rig.h, app::Button::F7);
  tap(*rig.h, app::Button::F1);  // confirms
  o3_voc03er::PatternData want{};
  pt::fill_mouth(want, 16, pt::fill_seed(0, 0));  // the cancelled ask did not count as a fill
  CHECK(steps_show(rig, want, 3, 16));
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: SHIFT + F6 empties the MOUTH lane and F7 fills it again without asking") {
  Rig rig;
  to_mouth(rig);
  pad(*rig.h, 3);
  tap(*rig.h, app::Button::F7);
  tap(*rig.h, app::Button::F1);
  shift_tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F1);
  CHECK(rig.count(36, 43, app::kLedOff) == 8u);
  CHECK(rig.count(26, 31, app::kLedOff) == 6u);  // the cursor is on step 2
  tap(*rig.h, app::Button::F7);  // empty: at once
  o3_voc03er::PatternData want{};
  pt::fill_mouth(want, 16, pt::fill_seed(1, 0));
  CHECK(steps_show(rig, want, 2, 16));
  CHECK(rig.h->runtime().frame_failures() == 0u);
}
