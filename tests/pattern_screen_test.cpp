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
#include "rec_take.h"
#include "tract_skip.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace pt = o3_voc03er::pattern;
namespace pd = o3_voc03er::pads;

TEST_CASE("pattern: the pad map: the steps and patterns on the top two rows, the lane's palette on the lower two") {
  CHECK(pt::step_of_pad(0) == 0);
  CHECK(pt::step_of_pad(7) == 7);
  CHECK(pt::step_of_pad(12) == 8);
  CHECK(pt::step_of_pad(19) == 15);
  CHECK(pt::step_of_pad(8) == -1);   // a pattern pad
  CHECK(pt::step_of_pad(24) == -1);  // the palette
  CHECK(pt::pattern_of_pad(8) == 0);
  CHECK(pt::pattern_of_pad(11) == 3);
  CHECK(pt::pattern_of_pad(20) == 4);
  CHECK(pt::pattern_of_pad(23) == 7);
  CHECK(pt::pattern_of_pad(0) == -1);   // a step pad
  CHECK(pt::pattern_of_pad(24) == -1);  // the palette
  for (unsigned p = 0; p < 24; ++p) {  // the top two rows are steps or patterns, never the palette
    CHECK(pt::palette_of_pad(p) == -1);
    CHECK((pt::step_of_pad(p) >= 0) != (pt::pattern_of_pad(p) >= 0));
  }
  for (unsigned p = 24; p < 48; ++p) {  // the lower two rows are the palette, index 0..23
    CHECK(pt::palette_of_pad(p) == static_cast<int>(p) - 24);
    CHECK(pt::step_of_pad(p) == -1);
    CHECK(pt::pattern_of_pad(p) == -1);
    CHECK(pt::pad_of_palette(p - 24u) == p);
  }
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
  CHECK(pt::step_value(p, 4, pt::Lane::Level, -1));  // (the default 100 is the top outside TRACT: UX2)
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


TEST_CASE("pattern: a TIE is left alone by the VALUE key, and a rest starts at C3") {
  o3_voc03er::PatternData p{};
  REQUIRE(o3_voc03er::set_tie(p, 2));
  CHECK_FALSE(pt::step_value(p, 2, pt::Lane::Note, 1));
  CHECK_FALSE(pt::step_value(p, 2, pt::Lane::Note, -1));
  CHECK(o3_voc03er::is_tie(o3_voc03er::get_step(p, 2)));
  CHECK(pt::step_value(p, 3, pt::Lane::Note, 1));
  CHECK(o3_voc03er::get_step(p, 3).note == 48u);
}

TEST_CASE("pattern: the hand a palette pad gives, lane by lane, and the hand each lane starts with (the logic)") {
  const pd::Layout def;  // A NOTE, B TRIAD
  pt::Hand h;
  const auto pick = [&](pt::Lane lane, unsigned idx, unsigned slices = 0, unsigned voicing = 0, const pd::Layout& l = pd::Layout{}) {
    return pt::hand_of_pad(h, lane, idx, 0, l, voicing, pt::MouthPage::Vowels, 100, slices);
  };
  REQUIRE(pick(pt::Lane::Note, 3));  // A: a note, an octave above B
  CHECK(h.s.note == 63u);
  CHECK(h.s.kind == 0u);
  CHECK(h.s.voicing == 0u);
  REQUIRE(pick(pt::Lane::Note, 15, 0, 2));  // B: a triad, with the voicing
  CHECK(h.s.note == 51u);
  CHECK(h.s.kind == 1u);
  CHECK(h.s.voicing == 2u);
  pd::Layout off;
  pd::set_plays(off, pd::Row::B, pd::Plays::Off);
  CHECK_FALSE(pick(pt::Lane::Note, 15, 0, 0, off));  // OFF plays nothing: no hand
  CHECK_FALSE(pick(pt::Lane::Note, 24));             // past the palette
  REQUIRE(pick(pt::Lane::Carrier, 2));
  CHECK(h.s.carrier == 2u);
  CHECK(pick(pt::Lane::Carrier, 7));  // LIVE
  CHECK_FALSE(pick(pt::Lane::Carrier, 8));
  REQUIRE(pick(pt::Lane::Level, 23));
  CHECK(h.s.level == pt::level_of_pad(23, 100));
  CHECK(h.s.level == 100u);
  REQUIRE(pick(pt::Lane::Slice, 3, 4));
  CHECK(h.s.slice == 4u);
  CHECK_FALSE(pick(pt::Lane::Slice, 4, 4));  // past the SPEAKS slot's cuts
  CHECK_FALSE(pick(pt::Lane::Slice, 0, 0));
  REQUIRE(pick(pt::Lane::Mouth, 0));
  CHECK(h.mouth == 1u);
  CHECK_FALSE(pick(pt::Lane::Mouth, 21));  // the strip's dark pads
  // The first hands.
  CHECK(pt::first_hand(pt::Lane::Note, 60, 100).s.note == 60u);
  CHECK(pt::first_hand(pt::Lane::Note, 0, 100).s.note == 1u);
  CHECK(pt::first_hand(pt::Lane::Note, 200, 100).s.note == 127u);
  CHECK(pt::first_hand(pt::Lane::Carrier, 60, 100).s.carrier == 0u);  // FOLLOW
  CHECK(pt::first_hand(pt::Lane::Level, 60, 100).s.level == pt::level_of_pad(0, 100));
  CHECK(pt::first_hand(pt::Lane::Slice, 60, 100).s.slice == 1u);
  CHECK(pt::first_hand(pt::Lane::Mouth, 60, 100).mouth == 1u);
}

TEST_CASE("pattern: a step press puts the hand on an empty step, chooses a full one and clears the one under the cursor (the logic)") {
  using o3_voc03er::get_mouth;
  using o3_voc03er::get_step;
  o3_voc03er::PatternData p{};
  REQUIRE(o3_voc03er::set_length(p, 16));
  pt::Hand hand = pt::first_hand(pt::Lane::Note, 60, 100);
  CHECK(pt::press_step(p, 3, 0, pt::Lane::Note, hand) == pt::Press::Put);
  CHECK(get_step(p, 3).note == 60u);
  CHECK(pt::holds_hand(p, 3, pt::Lane::Note, hand));
  // A full step that is not under the cursor is chosen: nothing is written, the hand becomes its value.
  pt::Hand other{};
  other.s.note = 70;
  CHECK(pt::press_step(p, 3, 0, pt::Lane::Note, other) == pt::Press::Chose);
  CHECK(other.s.note == 60u);
  CHECK(get_step(p, 3).note == 60u);
  // Under the cursor it is cleared, the hand is left alone.
  other.s.note = 70;
  CHECK(pt::press_step(p, 3, 3, pt::Lane::Note, other) == pt::Press::Cleared);
  CHECK(get_step(p, 3).note == 0u);
  CHECK(other.s.note == 70u);
  // An empty hand puts nothing; a step past LENGTH is nothing.
  pt::Hand none{};
  CHECK(pt::press_step(p, 1, 0, pt::Lane::Note, none) == pt::Press::Nothing);
  REQUIRE(o3_voc03er::set_length(p, 4));
  CHECK(pt::press_step(p, 5, 0, pt::Lane::Note, hand) == pt::Press::Nothing);
  REQUIRE(o3_voc03er::set_length(p, 16));
  // A TIE is full, yet chosen it leaves the hand as it was.
  REQUIRE(o3_voc03er::set_tie(p, 2));
  pt::Hand keep = hand;
  CHECK(pt::press_step(p, 2, 0, pt::Lane::Note, keep) == pt::Press::Chose);
  CHECK(keep.s.note == hand.s.note);
  // The other lanes.
  pt::Hand c = pt::first_hand(pt::Lane::Carrier, 60, 100);
  c.s.carrier = 2;
  CHECK(pt::press_step(p, 4, 0, pt::Lane::Carrier, c) == pt::Press::Put);
  CHECK(get_step(p, 4).carrier == 2u);
  CHECK(pt::press_step(p, 4, 4, pt::Lane::Carrier, c) == pt::Press::Cleared);
  CHECK(get_step(p, 4).carrier == 0u);
  pt::Hand lv = pt::first_hand(pt::Lane::Level, 60, 100);
  lv.s.level = 77;
  CHECK(pt::press_step(p, 5, 0, pt::Lane::Level, lv) == pt::Press::Put);
  CHECK(get_step(p, 5).level == 77u);
  pt::Hand sl = pt::first_hand(pt::Lane::Slice, 60, 100);
  sl.s.slice = 2;
  CHECK(pt::press_step(p, 6, 0, pt::Lane::Slice, sl) == pt::Press::Put);
  CHECK(get_step(p, 6).slice == 2u);
  pt::Hand mo = pt::first_hand(pt::Lane::Mouth, 60, 100);
  mo.mouth = 5;
  CHECK(pt::press_step(p, 7, 0, pt::Lane::Mouth, mo) == pt::Press::Put);
  CHECK(get_mouth(p, 7) == 5u);
  mo.mouth = 9;
  CHECK(pt::press_step(p, 7, 0, pt::Lane::Mouth, mo) == pt::Press::Chose);
  CHECK(mo.mouth == 5u);
}

TEST_CASE("pattern: SHIFT + a step pad on NOTE is a TIE, and again a rest; the lane's other values stay (the logic)") {
  using o3_voc03er::get_step;
  using o3_voc03er::is_tie;
  o3_voc03er::PatternData p{};
  REQUIRE(o3_voc03er::set_length(p, 16));
  CHECK(pt::tie_toggle(p, 2) == pt::Press::Put);
  CHECK(is_tie(get_step(p, 2)));
  CHECK(pt::tie_toggle(p, 2) == pt::Press::Cleared);
  CHECK_FALSE(is_tie(get_step(p, 2)));
  CHECK(get_step(p, 2).note == 0u);
  REQUIRE(o3_voc03er::set_note(p, 4, 60));  // a note becomes a TIE
  CHECK(pt::tie_toggle(p, 4) == pt::Press::Put);
  CHECK(is_tie(get_step(p, 4)));
  REQUIRE(o3_voc03er::set_carrier(p, 5, 3));
  CHECK(pt::tie_toggle(p, 5) == pt::Press::Put);
  CHECK(get_step(p, 5).carrier == 3u);
  REQUIRE(o3_voc03er::set_length(p, 4));
  CHECK(pt::tie_toggle(p, 9) == pt::Press::Nothing);
}

TEST_CASE("pattern: what a lane holds: lane_full, hand_of_step and holds_hand (the logic)") {
  using o3_voc03er::PatternData;
  PatternData p{};
  REQUIRE(o3_voc03er::set_length(p, 16));
  pt::Hand h;
  for (unsigned l = 0; l < pt::kLanes; ++l) {
    CHECK_FALSE(pt::lane_full(p, 0, static_cast<pt::Lane>(l)));
    CHECK_FALSE(pt::hand_of_step(h, p, 0, static_cast<pt::Lane>(l)));
  }
  REQUIRE(o3_voc03er::set_chord(p, 1, 48, o3_voc03er::ChordKind::Seventh, o3_voc03er::Voicing::Open));
  CHECK(pt::lane_full(p, 1, pt::Lane::Note));
  CHECK_FALSE(pt::lane_full(p, 1, pt::Lane::Carrier));
  REQUIRE(pt::hand_of_step(h, p, 1, pt::Lane::Note));
  CHECK(h.s.note == 48u);
  CHECK(h.s.kind == 2u);  // the seventh
  CHECK(h.s.voicing == 1u);
  CHECK(pt::holds_hand(p, 1, pt::Lane::Note, h));
  h.s.voicing = 2;
  CHECK_FALSE(pt::holds_hand(p, 1, pt::Lane::Note, h));
  REQUIRE(o3_voc03er::set_tie(p, 2));
  CHECK(pt::lane_full(p, 2, pt::Lane::Note));  // a TIE is full ...
  CHECK_FALSE(pt::hand_of_step(h, p, 2, pt::Lane::Note));  // ... and gives no hand
  REQUIRE(o3_voc03er::set_carrier(p, 3, 3));
  REQUIRE(pt::hand_of_step(h, p, 3, pt::Lane::Carrier));
  CHECK(h.s.carrier == 3u);
  REQUIRE(o3_voc03er::set_level(p, 4, 50));
  REQUIRE(pt::hand_of_step(h, p, 4, pt::Lane::Level));
  CHECK(h.s.level == 50u);
  REQUIRE(o3_voc03er::set_slice(p, 5, 2));
  REQUIRE(pt::hand_of_step(h, p, 5, pt::Lane::Slice));
  CHECK(h.s.slice == 2u);
  REQUIRE(o3_voc03er::set_mouth(p, 6, 7));
  REQUIRE(pt::hand_of_step(h, p, 6, pt::Lane::Mouth));
  CHECK(h.mouth == 7u);
  // write_hand and hand_of_step round trip.
  PatternData q{};
  REQUIRE(pt::hand_of_step(h, p, 1, pt::Lane::Note));
  REQUIRE(pt::write_hand(q, 9, pt::Lane::Note, h));
  CHECK(o3_voc03er::get_step(q, 9).note == 48u);
  CHECK(o3_voc03er::get_step(q, 9).kind == 2u);
}

TEST_CASE("pattern: F4 CHORD steps the palette's lower row: a tap wraps, a detent stops; UP and DOWN stop at NOTE and MOUTH (the logic)") {
  static_assert(pt::kChordKinds == 4u, "TRIAD, 7TH, OPEN, POWER");
  CHECK(pt::chord_step(0, 1, true) == 1u);
  CHECK(pt::chord_step(3, 1, true) == 0u);   // a tap wraps
  CHECK(pt::chord_step(3, 1, false) == 3u);  // a detent stops
  CHECK(pt::chord_step(0, -1, false) == 0u);
  CHECK(pt::chord_step(0, -1, true) == 3u);
  CHECK(pt::chord_step(1, 30, false) == 3u);
  CHECK(pt::chord_step(2, -30, false) == 0u);
  for (unsigned i = 0; i < pt::kChordKinds; ++i) {
    CHECK(pt::chord_index(pt::kChordPlays[i]) == i);
    CHECK(pt::is_chord_plays(pt::kChordPlays[i]));
  }
  CHECK_FALSE(pt::is_chord_plays(pd::Plays::Note));
  CHECK_FALSE(pt::is_chord_plays(pd::Plays::Off));
  const pd::Layout def;
  CHECK(pt::palette_normal(def));
  CHECK(pt::palette_chord(def) == 0u);
  pd::Layout l;
  pd::set_plays(l, pd::Row::A, pd::Plays::Note);
  pd::set_plays(l, pd::Row::B, pd::Plays::Note);  // NOTE + NOTE: not a palette, the chord falls back to the triad
  CHECK_FALSE(pt::palette_normal(l));
  CHECK(pt::palette_chord(l) == 0u);
  pd::set_plays(l, pd::Row::A, pd::Plays::Open);
  CHECK_FALSE(pt::palette_normal(l));
  CHECK(pt::palette_chord(l) == 2u);  // the kind a foreign row A kept
  pd::set_plays(l, pd::Row::A, pd::Plays::Note);
  pd::set_plays(l, pd::Row::B, pd::Plays::Power);
  CHECK(pt::palette_normal(l));
  CHECK(pt::palette_chord(l) == 3u);
  // UP goes towards NOTE, DOWN towards MOUTH, neither wraps.
  static_assert(pt::kLanes == 5u, "NOTE, CARRIER, LEVEL, SLICE, MOUTH");
  CHECK(pt::lane_step(0, true) == 0u);
  CHECK(pt::lane_step(0, false) == 1u);
  CHECK(pt::lane_step(2, true) == 1u);
  CHECK(pt::lane_step(3, false) == 4u);
  CHECK(pt::lane_step(4, false) == 4u);
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

TEST_CASE("pattern: CLEAR LANE's question names the lane as the screen does") {
  for (unsigned l = 0; l < pt::kLanes; ++l) {
    const std::string q = pt::kClearPrompt[l];
    CHECK(q == std::string("CLEAR ") + pt::kLaneNames[l] + " LANE: F1 CONFIRMS");
  }
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
unsigned sp(unsigned i) { return pt::pad_of_step(i); }
unsigned pp(unsigned idx) { return pt::pad_of_palette(idx); }
// How many of the sixteen step pads show `level`.
unsigned steps_count(const Rig& rig, unsigned level) {
  unsigned n = 0;
  for (unsigned i = 0; i < 16; ++i) {
    n += rig.led(sp(i)) == level ? 1u : 0u;
  }
  return n;
}
// The hand on the first n steps (the cursor ends on the last one).
void fill(Rig& rig, unsigned n) {
  for (unsigned i = 0; i < n; ++i) {
    pad(*rig.h, sp(i));
  }
}
void open_help(sim::Harness& h) {
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Config);
  h.release(app::Button::Shift);
  h.run_ms(60);
}
}  // namespace

// ---- the hand: the palette on the lower two rows, the steps and the patterns on the top two ---------------------------------------------------

TEST_CASE("pattern: a palette pad sounds while held and becomes the hand; it writes nothing") {
  Rig rig;
  CHECK(rig.led(pp(0)) == app::kLedMid);  // the NOTE lane starts on the key's root
  CHECK(rig.led(sp(0)) == app::kLedMid);  // the cursor, nothing in it
  rig.h->pad_down(pp(5));
  rig.h->run_ms(60);
  CHECK(rig.led(pp(5)) == app::kLedOn);  // held and sounding
  rig.h->pad_up(pp(5));
  rig.h->run_ms(60);
  CHECK(rig.led(pp(5)) == app::kLedMid);  // in hand
  CHECK(rig.led(pp(0)) != app::kLedMid);
  CHECK(steps_count(rig, app::kLedDim) == 0u);  // no step holds anything
  CHECK(rig.led(sp(0)) == app::kLedMid);        // the cursor did not move
  CHECK(rig.led(sp(1)) == app::kLedOff);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: a step pad puts the hand on an empty step and the cursor goes there") {
  Rig rig;
  pad(*rig.h, pp(5));
  pad(*rig.h, sp(3));
  CHECK(rig.led(sp(3)) == app::kLedMid);  // the cursor, with the step in it
  CHECK(rig.led(sp(0)) == app::kLedOff);  // the cursor left an empty step
  pad(*rig.h, sp(6));
  CHECK(rig.led(sp(3)) == app::kLedDim);
  CHECK(rig.led(sp(6)) == app::kLedMid);
  CHECK(rig.led(pp(5)) == app::kLedMid);  // the hand is the same
  CHECK(rig.h->runtime().frame_failures() == 0u);
  CHECK(rig.h->display().peak_frame_words() < 2048u);
}

TEST_CASE("pattern: a step pad on a full step chooses it: the cursor goes there, its value becomes the hand, nothing is written") {
  Rig rig;
  pad(*rig.h, sp(0));    // the key's root, C4, on step 1
  pad(*rig.h, pp(5));    // another hand
  pad(*rig.h, sp(2));    // put on step 3: the cursor is there
  CHECK(rig.led(pp(5)) == app::kLedMid);
  pad(*rig.h, sp(0));  // full and not under the cursor: chosen
  CHECK(rig.led(pp(0)) == app::kLedMid);
  CHECK(rig.led(pp(5)) != app::kLedMid);
  CHECK(rig.led(sp(0)) == app::kLedMid);  // the cursor
  CHECK(rig.led(sp(2)) == app::kLedDim);  // untouched
  CHECK(steps_count(rig, app::kLedDim) == 1u);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: a step pad on the full step under the cursor clears it, and the hand stays") {
  Rig rig;
  pad(*rig.h, pp(5));
  pad(*rig.h, sp(0));  // put; the cursor was there already
  pad(*rig.h, sp(0));  // again: cleared
  shift_tap(*rig.h, app::Button::Right);  // the cursor away: the pad shows the step
  CHECK(rig.led(sp(0)) == app::kLedOff);
  CHECK(rig.led(pp(5)) == app::kLedMid);  // the hand was not touched
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: a step pad held with a palette pad writes every held step and changes the hand") {
  Rig rig;
  rig.h->pad_down(sp(1));  // an empty step: the hand goes in, and stays held
  rig.h->run_ms(20);
  rig.h->pad_down(sp(3));
  rig.h->run_ms(20);
  pad(*rig.h, pp(6));  // F#4 over both
  rig.h->pad_up(sp(3));
  rig.h->run_ms(20);
  rig.h->pad_up(sp(1));
  rig.h->run_ms(40);
  CHECK(rig.led(sp(1)) == app::kLedDim);
  CHECK(rig.led(sp(3)) == app::kLedMid);  // the cursor stays on the last step pressed
  CHECK(rig.led(sp(2)) == app::kLedOff);
  CHECK(rig.led(pp(6)) == app::kLedMid);
  pad(*rig.h, pp(0));  // the hand away
  pad(*rig.h, sp(1));  // chosen: the pad that plays its note is in hand
  CHECK(rig.led(pp(6)) == app::kLedMid);
  pad(*rig.h, pp(0));
  pad(*rig.h, sp(3));
  CHECK(rig.led(pp(6)) == app::kLedMid);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: SHIFT + a step pad on NOTE is a TIE, and again a rest; the hand stays and a chosen TIE leaves it alone") {
  Rig rig;
  pad(*rig.h, pp(5));
  pad(*rig.h, sp(2), true);  // a TIE
  shift_tap(*rig.h, app::Button::Right);
  CHECK(rig.led(sp(2)) == app::kLedDim);
  CHECK(rig.led(pp(5)) == app::kLedMid);
  pad(*rig.h, pp(0));
  pad(*rig.h, sp(2));  // chosen: a TIE has no pitch to hand over
  CHECK(rig.led(pp(0)) == app::kLedMid);
  CHECK(rig.led(sp(2)) == app::kLedMid);
  pad(*rig.h, sp(2), true);  // again: a rest
  shift_tap(*rig.h, app::Button::Right);
  CHECK(rig.led(sp(2)) == app::kLedOff);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: SHIFT + a step pad on another lane empties the step's lane") {
  Rig rig;
  tap(*rig.h, app::Button::Instr);  // CARRIER
  pad(*rig.h, pp(2));
  pad(*rig.h, sp(0));  // NOISE put
  shift_tap(*rig.h, app::Button::Right);
  CHECK(rig.led(sp(0)) == app::kLedDim);
  pad(*rig.h, sp(0), true);  // cleared, the cursor goes there
  CHECK(rig.led(sp(0)) == app::kLedMid);
  shift_tap(*rig.h, app::Button::Right);
  CHECK(rig.led(sp(0)) == app::kLedOff);
  CHECK(rig.led(pp(2)) == app::kLedMid);  // the hand stays
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

// ---- REC ---------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("pattern: REC with the transport stopped makes a palette pad write at the cursor and move on; REC off, it only sounds") {
  Rig rig;
  tap(*rig.h, app::Button::Rec);
  pad(*rig.h, pp(5));  // step 1
  CHECK(rig.led(sp(0)) == app::kLedDim);
  CHECK(rig.led(sp(1)) == app::kLedMid);  // the cursor moved on
  pad(*rig.h, pp(6));                     // step 2
  CHECK(rig.led(sp(1)) == app::kLedDim);
  CHECK(rig.led(sp(2)) == app::kLedMid);
  tap(*rig.h, app::Button::Rec);  // off
  pad(*rig.h, pp(7));
  shift_tap(*rig.h, app::Button::Right);
  CHECK(rig.led(sp(2)) == app::kLedOff);  // nothing was written on step 3
  CHECK(rig.led(pp(7)) == app::kLedMid);  // yet it is the hand
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: REC with the transport running writes on the step that plays") {
  Rig rig;
  tap(*rig.h, app::Button::Rec);
  tap(*rig.h, app::Button::Play);
  rig.h->run_ms(300);  // the bar is past its first two steps
  pad(*rig.h, pp(5));
  tap(*rig.h, app::Button::Play);  // stop
  rig.h->run_ms(100);
  shift_tap(*rig.h, app::Button::Right);  // the cursor followed the take: away from it
  CHECK(rig.led(sp(0)) == app::kLedOff);
  CHECK(rig.led(sp(1)) == app::kLedOff);
  CHECK(steps_count(*&rig, app::kLedDim) == 1u);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: REC goes off when the screen is left") {
  Rig rig;
  tap(*rig.h, app::Button::Rec);
  tap(*rig.h, app::Button::Performance);
  tap(*rig.h, app::Button::Pattern);
  rig.h->run_ms(60);
  pad(*rig.h, pp(5));
  CHECK(steps_count(rig, app::kLedDim) == 0u);
  CHECK(rig.led(sp(0)) == app::kLedMid);  // the cursor did not move
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

// ---- the arrows and the F row ---------------------------------------------------------------------------------------------------------------

TEST_CASE("pattern: UP and DOWN go across the lanes, NOTE at the top and MOUTH at the end, no wrap; each lane's palette") {
  Rig rig;
  const unsigned note_dark = rig.count(24, 47, app::kLedOff);
  CHECK(note_dark > 0u);
  CHECK(note_dark < 24u);
  tap(*rig.h, app::Button::Up);  // NOTE is the first
  CHECK(rig.count(24, 47, app::kLedOff) == note_dark);
  tap(*rig.h, app::Button::Down);  // CARRIER: eight values
  CHECK(rig.count(24, 47, app::kLedOff) == 16u);
  CHECK(rig.led(pp(0)) == app::kLedMid);  // FOLLOW is what an empty step stores
  tap(*rig.h, app::Button::Down);         // LEVEL: all 24 pads lit
  CHECK(rig.count(24, 47, app::kLedOff) == 0u);
  pad(*rig.h, pp(23));
  CHECK(rig.led(pp(23)) == app::kLedMid);  // the top is 100
  CHECK(rig.led(pp(0)) != app::kLedMid);
  tap(*rig.h, app::Button::Down);  // SLICE: drawn, dark with no slices
  CHECK(rig.count(24, 47, app::kLedOff) == 24u);
  tap(*rig.h, app::Button::Down);  // MOUTH: twenty-one pads, three dark
  CHECK(rig.count(24, 47, app::kLedOff) == 3u);
  tap(*rig.h, app::Button::Down);  // the end
  CHECK(rig.count(24, 47, app::kLedOff) == 3u);
  tap(*rig.h, app::Button::Up);  // SLICE
  CHECK(rig.count(24, 47, app::kLedOff) == 24u);
  tap(*rig.h, app::Button::Up);
  tap(*rig.h, app::Button::Up);
  tap(*rig.h, app::Button::Up);  // NOTE
  CHECK(rig.count(24, 47, app::kLedOff) == note_dark);
  tap(*rig.h, app::Button::Up);
  CHECK(rig.count(24, 47, app::kLedOff) == note_dark);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: the NOTE, INSTR, VOL and FX buttons pick the lane; FX goes SLICE, MOUTH, SLICE") {
  Rig rig;
  set_mode(*rig.h, 4.0f);
  tap(*rig.h, app::Button::Instr);
  CHECK(rig.count(24, 47, app::kLedOff) == 16u);
  tap(*rig.h, app::Button::Vol);
  CHECK(rig.count(24, 47, app::kLedOff) == 0u);
  tap(*rig.h, app::Button::Fx);  // SLICE
  CHECK(rig.count(24, 47, app::kLedOff) == 24u);
  tap(*rig.h, app::Button::Fx);  // MOUTH
  CHECK(rig.count(24, 47, app::kLedOff) == 3u);
  CHECK(rig.led(pp(21)) == app::kLedOff);
  CHECK(rig.led(pp(22)) == app::kLedOff);
  CHECK(rig.led(pp(23)) == app::kLedOff);
  CHECK(rig.led(pp(20)) == app::kLedDim);
  tap(*rig.h, app::Button::Fx);  // back to SLICE
  CHECK(rig.count(24, 47, app::kLedOff) == 24u);
  tap(*rig.h, app::Button::Note);
  const unsigned tract_dark = rig.count(24, 47, app::kLedOff);
  CHECK(tract_dark > 0u);
  CHECK(tract_dark < 24u);  // the notes palette is lit in TRACT
  set_mode(*rig.h, 0.0f);   // BANDS: the same palette
  CHECK(rig.count(24, 47, app::kLedOff) == tract_dark);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: SHIFT + UP and DOWN move the octave and the NOTE hand with it") {
  Rig rig;
  pad(*rig.h, sp(0));  // C4 on step 1
  CHECK(rig.led(pp(0)) == app::kLedMid);
  shift_tap(*rig.h, app::Button::Up);
  CHECK(rig.led(pp(0)) == app::kLedMid);  // the hand moved with the palette: C5 is its root now
  pad(*rig.h, sp(1));                     // C5 put on step 2
  pad(*rig.h, sp(0));                     // chosen: C4, an octave below the palette now
  CHECK(rig.led(pp(0)) != app::kLedMid);
  shift_tap(*rig.h, app::Button::Down);   // the palette back, the hand a octave down: C3
  pad(*rig.h, sp(1));                     // chosen: C5, off the palette
  pad(*rig.h, sp(0));                     // chosen: C4, the root again
  CHECK(rig.led(pp(0)) == app::kLedMid);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: LEFT and RIGHT are one encoder step of the lit F key") {
  Rig rig;
  pad(*rig.h, sp(0));
  tap(*rig.h, app::Button::Right);  // F3 VALUE is lit on entry
  tap(*rig.h, app::Button::Right);
  pad(*rig.h, sp(1));  // the hand away from step 1
  pad(*rig.h, sp(0));  // chosen: C4 + 2
  CHECK(rig.led(pp(2)) == app::kLedMid);
  tap(*rig.h, app::Button::Left);
  pad(*rig.h, sp(1));
  pad(*rig.h, sp(0));
  CHECK(rig.led(pp(1)) == app::kLedMid);
  // LENGTH
  fill(rig, 16);
  tap(*rig.h, app::Button::F1);  // lights LENGTH
  tap(*rig.h, app::Button::Left);
  CHECK(rig.led(sp(14)) == app::kLedMid);  // the cursor was cut back onto the last step
  CHECK(rig.led(sp(15)) == app::kLedOff);
  tap(*rig.h, app::Button::Right);
  CHECK(rig.led(sp(15)) == app::kLedDim);
  // TEMPO
  tap(*rig.h, app::Button::F2);
  const std::uint32_t t0 = rig.h->engine().own_tempo();
  tap(*rig.h, app::Button::Right);
  CHECK(rig.h->engine().own_tempo() == t0 + 1000u);
  tap(*rig.h, app::Button::Left);
  tap(*rig.h, app::Button::Left);
  CHECK(rig.h->engine().own_tempo() == t0 - 1000u);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: SHIFT + LEFT and RIGHT move the cursor along the steps and wrap") {
  Rig rig;
  shift_tap(*rig.h, app::Button::Left);
  CHECK(rig.led(sp(15)) == app::kLedMid);
  shift_tap(*rig.h, app::Button::Right);
  CHECK(rig.led(sp(0)) == app::kLedMid);
  shift_tap(*rig.h, app::Button::Right);
  CHECK(rig.led(sp(1)) == app::kLedMid);
  CHECK(rig.led(sp(0)) == app::kLedOff);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: F4 CHORD steps the palette's lower row on the first tap, a tap wraps and the encoder stops at the ends") {
  Rig rig;
  pad(*rig.h, pp(15));  // a triad
  CHECK(rig.led(pp(15)) == app::kLedMid);
  tap(*rig.h, app::Button::F4);  // 7TH: acts on the first tap
  CHECK(rig.led(pp(15)) != app::kLedMid);
  rig.h->turn(-10);
  rig.h->run_ms(60);
  CHECK(rig.led(pp(15)) == app::kLedMid);  // the encoder stops at TRIAD
  rig.h->turn(10);
  rig.h->run_ms(60);
  CHECK(rig.led(pp(15)) != app::kLedMid);  // and at POWER
  tap(*rig.h, app::Button::F4);            // a tap wraps
  CHECK(rig.led(pp(15)) == app::kLedMid);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: F4 offers note kinds only, and PERFORM's layout and the palette do not touch each other") {
  Rig rig;
  tap(*rig.h, app::Button::F4);
  rig.h->turn(30);  // the last kind the palette offers: POWER
  rig.h->run_ms(300);
  CHECK(rig.count(24, 35, app::kLedOff) < 12u);  // never OFF, never a strip
  CHECK(rig.count(36, 47, app::kLedOff) < 12u);
  rig.h->turn(-30);
  rig.h->run_ms(300);
  const unsigned lit_a = rig.count(24, 35, app::kLedOff);
  const unsigned lit_b = rig.count(36, 47, app::kLedOff);
  CHECK(lit_a < 12u);
  set_mode(*rig.h, 4.0f);  // TRACT: PERFORM's rows are NOTE, TONGUE, MOUTH
  CHECK(rig.count(24, 35, app::kLedOff) == lit_a);
  CHECK(rig.count(36, 47, app::kLedOff) == lit_b);  // B is a note row here, not TONGUE
  tap(*rig.h, app::Button::Performance);
  rig.h->run_ms(300);
  CHECK(rig.led(24 + 0) != app::kLedOff);  // TRACT's B on PERFORM: the TONGUE strip, lit
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: F4 is blank off the NOTE and MOUTH lanes: it takes no focus there") {
  Rig rig;
  pad(*rig.h, sp(0));
  rig.h->turn(1);  // still VALUE
  rig.h->run_ms(60);
  pad(*rig.h, sp(1));
  pad(*rig.h, sp(0));
  CHECK(rig.led(pp(1)) == app::kLedMid);  // C4 + 1
  tap(*rig.h, app::Button::Instr);        // CARRIER
  pad(*rig.h, pp(2));
  pad(*rig.h, sp(0));
  tap(*rig.h, app::Button::F4);  // blank here
  rig.h->turn(1);
  rig.h->run_ms(60);
  pad(*rig.h, sp(1));
  pad(*rig.h, sp(0));
  CHECK(rig.led(pp(3)) == app::kLedMid);  // the encoder still stepped the value
  CHECK(rig.led(pp(2)) != app::kLedMid);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: SHIFT + F6 asks to clear the lane: F1 confirms, any other key cancels; the cursor stays") {
  Rig rig;
  fill(rig, 3);  // the cursor on step 3
  shift_tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F2);  // cancels
  CHECK(rig.led(sp(0)) == app::kLedDim);
  CHECK(rig.led(sp(1)) == app::kLedDim);
  shift_tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F1);  // confirms
  CHECK(rig.led(sp(0)) == app::kLedOff);
  CHECK(rig.led(sp(1)) == app::kLedOff);
  CHECK(rig.led(sp(2)) == app::kLedMid);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: LENGTH cuts the steps, a step past it is dark and CLEAR LANE empties the lane") {
  Rig rig;
  fill(rig, 16);
  CHECK(rig.led(sp(8)) == app::kLedDim);
  tap(*rig.h, app::Button::F1);  // focus LENGTH
  rig.h->turn(-8);
  rig.h->run_ms(60);
  CHECK(rig.led(sp(7)) == app::kLedMid);  // the cursor is cut back onto step 8, and it is still in
  CHECK(rig.led(sp(6)) == app::kLedDim);
  CHECK(rig.led(sp(8)) == app::kLedOff);  // out
  CHECK(rig.led(sp(15)) == app::kLedOff);
  pad(*rig.h, sp(10));  // a step past LENGTH: nothing
  CHECK(rig.led(sp(7)) == app::kLedMid);
  CHECK(rig.led(sp(10)) == app::kLedOff);
  shift_tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F1);
  CHECK(rig.led(sp(6)) == app::kLedOff);  // the lane is empty
  CHECK(rig.led(sp(7)) == app::kLedMid);  // the cursor stays
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

// ---- the pattern pads -------------------------------------------------------------------------------------------------------------------------

TEST_CASE("pattern: pattern pads show the pattern and the one playing; FOLLOW rides the playhead") {
  Rig rig;
  CHECK(rig.led(8) == app::kLedMid);  // A
  CHECK(rig.led(20) == app::kLedDim);
  pad(*rig.h, 20);  // E
  CHECK(rig.led(20) == app::kLedMid);
  CHECK(rig.led(8) == app::kLedDim);
  pad(*rig.h, 8);
  pad(*rig.h, sp(5));
  shift_tap(*rig.h, app::Button::F7);  // FOLLOW
  tap(*rig.h, app::Button::Play);
  rig.h->run_ms(900);
  CHECK(rig.led(8) == app::kLedOn);                 // A is playing
  CHECK(steps_count(rig, app::kLedOn) == 1u);       // the playhead
  CHECK(steps_count(rig, app::kLedMid) == 0u);      // the cursor is on it
  shift_tap(*rig.h, app::Button::F7);               // FOLLOW off: the cursor stays where it was
  rig.h->run_ms(300);
  CHECK(rig.h->runtime().frame_failures() == 0u);
  CHECK(rig.h->display().peak_frame_words() < 2048u);
}

// ---- COPY, INSERT, DELETE ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("pattern: COPY + INSERT moves the steps right, DELETE empties one, SHIFT + DELETE asks F1 for the pattern") {
  Rig rig;
  sim::Harness& h = *rig.h;
  auto shifted = [&h](app::Button b) { shift_tap(h, b); };
  fill(rig, 2);        // steps 1 and 2
  pad(h, sp(0));       // chosen: the cursor on step 1
  tap(h, app::Button::Copy);
  tap(h, app::Button::Insert);  // step 1's copy at step 1: steps 1 to 3 hold notes now
  pad(h, sp(7));                // the cursor away, on step 8 (it takes the hand too)
  CHECK(rig.led(sp(0)) == app::kLedDim);
  CHECK(rig.led(sp(1)) == app::kLedDim);
  CHECK(rig.led(sp(2)) == app::kLedDim);
  CHECK(rig.led(sp(3)) == app::kLedOff);
  pad(h, sp(1));
  tap(h, app::Button::Delete);  // step 2 empty
  pad(h, sp(7));
  CHECK(rig.led(sp(1)) == app::kLedOff);
  CHECK(rig.led(sp(0)) == app::kLedDim);
  CHECK(rig.led(sp(2)) == app::kLedDim);
  shifted(app::Button::Delete);  // asks first
  tap(h, app::Button::F2);       // any other key cancels
  CHECK(rig.led(sp(0)) == app::kLedDim);
  shifted(app::Button::Delete);
  tap(h, app::Button::F1);  // F1 empties the pattern
  CHECK(rig.led(sp(0)) == app::kLedOff);
  CHECK(rig.led(sp(2)) == app::kLedOff);
  CHECK(h.runtime().frame_failures() == 0u);
}

// ---- the other lanes ----------------------------------------------------------------------------------------------------------------------------

TEST_CASE("pattern: with SPEAKS on LIVE the SLICE lane is dark and writes nothing") {
  Rig rig;
  tap(*rig.h, app::Button::Fx);
  CHECK(rig.count(24, 47, app::kLedOff) == 24u);
  pad(*rig.h, pp(3));  // a palette pad: SPEAKS HAS NO SLICES
  CHECK(rig.led(sp(0)) == app::kLedMid);
  rig.h->turn(3);  // the encoder on VALUE: the same
  rig.h->run_ms(60);
  CHECK(rig.led(sp(0)) == app::kLedMid);
  pad(*rig.h, sp(2));  // a step press says so, and puts nothing
  CHECK(steps_count(rig, app::kLedDim) == 0u);
  shift_tap(*rig.h, app::Button::F6);  // CLEAR LANE is allowed and changes nothing here
  tap(*rig.h, app::Button::F1);
  CHECK(steps_count(rig, app::kLedDim) == 0u);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: a LIVE pad is a hand and lights; the pads after it are dark and ignored") {
  Rig rig;
  tap(*rig.h, app::Button::Instr);  // CARRIER
  CHECK(rig.led(pp(7)) != app::kLedOff);  // the LIVE pad is lit, and
  CHECK(rig.led(pp(8)) == app::kLedOff);  // the next is dark
  pad(*rig.h, pp(7));                     // LIVE in hand
  CHECK(rig.led(pp(7)) == app::kLedMid);
  pad(*rig.h, pp(8));  // a dark pad changes nothing
  CHECK(rig.led(pp(7)) == app::kLedMid);
  pad(*rig.h, sp(0));  // LIVE put
  pad(*rig.h, sp(1));
  pad(*rig.h, pp(2));
  pad(*rig.h, sp(0));  // chosen: LIVE is in hand again
  CHECK(rig.led(pp(7)) == app::kLedMid);
  CHECK(rig.led(pp(2)) != app::kLedMid);
  CHECK(rig.led(sp(1)) == app::kLedDim);
  CHECK(rig.count(24, 47, app::kLedOff) == 16u);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

// ---- the MOUTH lane --------------------------------------------------------------------------------------------------------------------------

namespace {
// The MOUTH lane with the vocoder in TRACT, palette page VOWELS.
void to_mouth(Rig& rig) {
  set_mode(*rig.h, 4.0f);
  tap(*rig.h, app::Button::Fx);
  tap(*rig.h, app::Button::Fx);
}
}  // namespace

TEST_CASE("pattern: a MOUTH pad is a hand, a step pad puts it; GLD is row 1; a dark pad is ignored") {
  Rig rig;
  to_mouth(rig);
  CHECK(rig.led(pp(0)) == app::kLedMid);  // the first vowel is in hand
  pad(*rig.h, pp(2));                     // E
  pad(*rig.h, sp(0));
  pad(*rig.h, pp(14));  // E with GLD
  pad(*rig.h, sp(1));
  CHECK(rig.led(sp(0)) == app::kLedDim);
  CHECK(rig.led(sp(1)) == app::kLedMid);
  pad(*rig.h, pp(21));  // dark: nothing, the hand stays
  CHECK(rig.led(pp(14)) == app::kLedMid);
  pad(*rig.h, sp(0));  // chosen: the strip's E, not the GLD row's
  CHECK(rig.led(pp(2)) == app::kLedMid);
  CHECK(rig.led(pp(14)) == app::kLedDim);
  pad(*rig.h, sp(1));
  CHECK(rig.led(pp(14)) == app::kLedMid);
  CHECK(rig.led(pp(2)) == app::kLedDim);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: with steps held a MOUTH pad writes them; SHIFT + a palette pad only sounds; SHIFT + a step clears it") {
  Rig rig;
  to_mouth(rig);
  const unsigned s5 = sp(4), s7 = sp(6);
  rig.h->pad_down(s5);
  rig.h->run_ms(20);
  rig.h->pad_down(s7);
  rig.h->run_ms(20);
  pad(*rig.h, pp(9));  // LIPS into both held steps
  rig.h->pad_up(s7);
  rig.h->run_ms(20);
  rig.h->pad_up(s5);
  rig.h->run_ms(20);
  CHECK(rig.led(s5) == app::kLedDim);
  CHECK(rig.led(s7) == app::kLedMid);  // the cursor stays on the last step pressed, which holds the mouth too
  CHECK(rig.led(sp(5)) == app::kLedOff);
  pad(*rig.h, pp(0));
  pad(*rig.h, s5);
  CHECK(rig.led(pp(9)) == app::kLedMid);
  pad(*rig.h, s7);
  CHECK(rig.led(pp(9)) == app::kLedMid);
  // SHIFT + a palette pad: the sound, never the hand
  pad(*rig.h, pp(3), true);
  CHECK(rig.led(pp(3)) == app::kLedDim);
  CHECK(rig.led(pp(9)) == app::kLedMid);
  // SHIFT + a step pad: the lane emptied there
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
  pad(*rig.h, pp(8));  // U, not in TRACT: nothing sounds
  CHECK(tract_value(*rig.h, TractUnit::Vowel) == doctest::Approx(v0));
  set_mode(*rig.h, 4.0f);
  rig.h->pad_down(pp(4));  // I, the fifth vowel: (5 - 1) / 8
  rig.h->run_ms(60);
  CHECK(tract_value(*rig.h, TractUnit::Vowel) == doctest::Approx(0.5f));
  rig.h->pad_up(pp(4));
  rig.h->run_ms(20);
  rig.h->pad_down(pp(10));  // TIP: held
  rig.h->run_ms(60);
  CHECK(tract_value(*rig.h, TractUnit::Close) == doctest::Approx(2.0f));
  rig.h->pad_up(pp(10));
  rig.h->run_ms(60);
  CHECK(tract_value(*rig.h, TractUnit::Close) == doctest::Approx(0.0f));
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: the MOUTH lane draws inside the display list's limit, in and out of TRACT") {
  Rig rig;
  tap(*rig.h, app::Button::Fx);
  tap(*rig.h, app::Button::Fx);
  for (unsigned i = 0; i < 6; ++i) {
    pad(*rig.h, pp(i % 12));  // a line of vowels
    pad(*rig.h, sp(i));
  }
  rig.h->run_ms(100);
  CHECK(rig.h->display().peak_frame_words() < 2048u);
  set_mode(*rig.h, 4.0f);
  rig.h->run_ms(100);
  CHECK(rig.h->display().peak_frame_words() < 2048u);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: F4 SET flips the MOUTH palette VOWELS and WORDS") {
  Rig rig;
  to_mouth(rig);
  CHECK(rig.count(24, 47, app::kLedOff) == 3u);  // VOWELS
  tap(*rig.h, app::Button::F4);
  CHECK(rig.count(24, 47, app::kLedOff) == 10u);  // WORDS: 8 + 6 lit
  CHECK(rig.led(pp(7)) == app::kLedDim);
  CHECK(rig.led(pp(8)) == app::kLedOff);
  CHECK(rig.led(pp(17)) == app::kLedDim);
  CHECK(rig.led(pp(18)) == app::kLedOff);
  tap(*rig.h, app::Button::F4);
  CHECK(rig.count(24, 47, app::kLedOff) == 3u);  // back
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: a WORDS pad is a hand that starts the word; a WOB pad leaves VOWEL alone; a dark pad is ignored") {
  using o3_voc03er::TractUnit;
  Rig rig;
  to_mouth(rig);
  tap(*rig.h, app::Button::F4);
  pad(*rig.h, pp(5));  // BA
  pad(*rig.h, sp(0));
  CHECK(rig.led(sp(0)) == app::kLedMid);
  rig.h->run_ms(500);  // the audition runs its course
  const float v0 = tract_value(*rig.h, TractUnit::Vowel);
  pad(*rig.h, pp(14));  // the 1/16 WOB: sounds nothing
  pad(*rig.h, sp(1));
  rig.h->run_ms(100);
  CHECK(tract_value(*rig.h, TractUnit::Vowel) == doctest::Approx(v0));
  CHECK(rig.led(sp(0)) == app::kLedDim);
  CHECK(rig.led(sp(1)) == app::kLedMid);
  pad(*rig.h, sp(0));  // chosen: BA is in hand on the WORDS page
  CHECK(rig.led(pp(5)) == app::kLedMid);
  CHECK(rig.led(pp(14)) != app::kLedMid);
  pad(*rig.h, sp(1));
  CHECK(rig.led(pp(14)) == app::kLedMid);
  CHECK(rig.led(pp(5)) != app::kLedMid);
  pad(*rig.h, pp(9));  // dark on this page: the hand stays
  CHECK(rig.led(pp(14)) == app::kLedMid);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

// ---- HELP -----------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("pattern: HELP gives a row to every button the screen answers and names the F keys and pads") {
  Rig rig;
  sim::Harness& h = *rig.h;
  open_help(h);
  REQUIRE(h.runtime().help_open());
  const app::Help& help = h.runtime().help_overlay().help();
  app::HelpProblem p;
  const bool ok = app::help_check(help, &p);
  CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
  for (const app::Button b : {app::Button::Up, app::Button::Down, app::Button::Left, app::Button::Right, app::Button::Rec, app::Button::Shift,
                              app::Button::Copy, app::Button::Insert, app::Button::Delete,
                              app::Button::Params, app::Button::Performance, app::Button::Play, app::Button::Master, app::Button::File,
                              app::Button::SampleLoad, app::Button::SamplePlay, app::Button::SampleEdit, app::Button::SampleRec,
                              app::Button::Song, app::Button::Pattern, app::Button::Note, app::Button::Instr, app::Button::Vol,
                              app::Button::Fx}) {
    INFO(std::string(app::button_name(b)));
    CHECK(has_row(help, b));
  }
  for (unsigned k = 0; k < 8; ++k) {
    CHECK(help.fkey[k] != nullptr);  // on the NOTE lane every F key has a text (F6 STYLE too)
    CHECK((help.fkey_shift[k] != nullptr) == (k == 0u || k >= 4u));  // UNDO, ALL LANES, CLR LANE, FOLLOW, CAPTURE
  }
  CHECK(help.pad_group[pp(0)] != 0u);  // the palette
  CHECK(help.pad_group[sp(0)] != 0u);  // the steps
  CHECK(help.pad_group[8] != 0u);      // the patterns
  CHECK(help.pad_group[pp(0)] != help.pad_group[sp(0)]);
  CHECK(help.pad_group[sp(0)] != help.pad_group[8]);
  CHECK(help.pad_label[pp(0)] != nullptr);
  CHECK(help.pad_label[sp(0)] != nullptr);
  CHECK(help.pad_label[8] != nullptr);
  // F4 is CHORD on NOTE, SET on MOUTH, blank on the others.
  Rig carrier;
  tap(*carrier.h, app::Button::Instr);
  open_help(*carrier.h);
  REQUIRE(carrier.h->runtime().help_open());
  CHECK(carrier.h->runtime().help_overlay().help().fkey[3] == nullptr);
  Rig mouth;
  tap(*mouth.h, app::Button::Fx);
  tap(*mouth.h, app::Button::Fx);
  open_help(*mouth.h);
  REQUIRE(mouth.h->runtime().help_open());
  CHECK(mouth.h->runtime().help_overlay().help().fkey[3] != nullptr);
}

// ---- RANDOM (F5), STYLE (F6), DENSITY (F7), RANDOM ALL and UNDO ------------------------------------------------------------------------------
// The tests cannot read the bank, so the oracle is the console's `pat` text (the bank as the sequencer reads it): one line per pattern, a token per step.

namespace {
// The console's command with up to four words after its name; what it printed.
std::string console_words(std::initializer_list<const char*> words) {
  const app::DiagCommand* rows = nullptr;
  const std::size_t n = ::o3sdk_app().diag_commands(rows);
  const char* argv[8] = {};
  unsigned argc = 0;
  for (const char* w : words) {
    if (argc < 8u) argv[argc++] = w;
  }
  std::string text;
  for (std::size_t i = 0; i < n; ++i) {
    if (std::string(rows[i].name) != argv[0]) continue;
    const app::DiagArgs args{argc, argv};
    app::DiagOut out(app::DiagSink{[](void* c, const char* d, std::size_t k) { static_cast<std::string*>(c)->append(d, k); }, &text});
    rows[i].run(args, out);
    return text;
  }
  text = "(no such command)";
  return text;
}

// One step of a `pat` line.
struct SV {
  std::string note = "-";  // "-" a rest, "T" a tie, else the MIDI number (a trailing c: a chord)
  unsigned car = 0, lvl = 0, slc = 0, mou = 0;
  bool playing() const { return note != "-"; }
  bool operator==(const SV& o) const { return note == o.note && car == o.car && lvl == o.lvl && slc == o.slc && mou == o.mou; }
  bool operator!=(const SV& o) const { return !(*this == o); }
};

// Pattern `c` (A..H) as the console prints it: the steps up to its LENGTH.
std::vector<SV> pat_of(char c) {
  const char name[2] = {c, 0};
  const std::string text = console_words({"pat", name});
  std::vector<SV> out;
  const std::size_t colon = text.find(':');
  if (colon == std::string::npos) return out;
  std::size_t i = colon + 1;
  while (i < text.size()) {
    while (i < text.size() && text[i] == ' ') ++i;
    std::size_t e = i;
    while (e < text.size() && text[e] != ' ' && text[e] != '\n' && text[e] != '\r') ++e;
    if (e == i) break;
    const std::string tok = text.substr(i, e - i);
    SV sv;
    std::size_t k = 0;
    while (k < tok.size() && std::strchr("/vsm", tok[k]) == nullptr) ++k;
    sv.note = tok.substr(0, k);
    while (k < tok.size()) {
      const char f = tok[k++];
      unsigned v = 0;
      while (k < tok.size() && tok[k] >= '0' && tok[k] <= '9') v = v * 10u + static_cast<unsigned>(tok[k++] - '0');
      (f == '/' ? sv.car : f == 'v' ? sv.lvl : f == 's' ? sv.slc : sv.mou) = v;
    }
    out.push_back(sv);
    i = e;
  }
  return out;
}

// The part of a step that belongs to lane 0 NOTE, 1 CARRIER, 2 LEVEL, 3 SLICE, 4 MOUTH.
std::string lane_part(const SV& s, unsigned lane) {
  switch (lane) {
    case 0: return s.note;
    case 1: return std::to_string(s.car);
    case 2: return std::to_string(s.lvl);
    case 3: return std::to_string(s.slc);
    default: return std::to_string(s.mou);
  }
}
// The steps [from, to) of `p`, all lanes but `skip` (5: all lanes), as one string.
std::string sig(const std::vector<SV>& p, std::size_t from, std::size_t to, unsigned skip) {
  std::string out;
  for (std::size_t i = from; i < to && i < p.size(); ++i) {
    for (unsigned l = 0; l < 5; ++l) {
      if (l != skip) out += lane_part(p[i], l) + ",";
    }
    out += ";";
  }
  return out;
}
std::string lane_sig(const std::vector<SV>& p, unsigned lane) {
  std::string out;
  for (const SV& s : p) out += lane_part(s, lane) + ",";
  return out;
}
bool any_playing(const std::vector<SV>& p) {
  for (const SV& s : p) {
    if (s.playing() || s.car != 0u || s.lvl != 0u || s.slc != 0u || s.mou != 0u) return true;
  }
  return false;
}

void to_lane(Rig& rig, unsigned lane) {
  switch (lane) {
    case 0: tap(*rig.h, app::Button::Note); break;
    case 1: tap(*rig.h, app::Button::Instr); break;
    case 2: tap(*rig.h, app::Button::Vol); break;
    case 3:
      tap(*rig.h, app::Button::Note);
      tap(*rig.h, app::Button::Fx);
      break;
    default:
      tap(*rig.h, app::Button::Note);
      tap(*rig.h, app::Button::Fx);
      tap(*rig.h, app::Button::Fx);
      break;
  }
}

// Every lane of steps 1..16 of pattern A holds a mark, so a roll's reach shows.
void mark_pattern_a() {
  for (unsigned st = 1; st <= 16; ++st) {
    const std::string s = std::to_string(st);
    const std::string note = std::to_string(40u + st);
    const std::string lvl = std::to_string(50u + st);
    (void)console_words({"pat", "A", s.c_str(), "note", note.c_str()});
    (void)console_words({"pat", "A", s.c_str(), "car", (st % 2u) != 0u ? "noise" : "synth"});
    (void)console_words({"pat", "A", s.c_str(), "lvl", lvl.c_str()});
    (void)console_words({"pat", "A", s.c_str(), "mou", "2"});
  }
}
void f5(Rig& r) { tap(*r.h, app::Button::F5); }
void f6(Rig& r) { tap(*r.h, app::Button::F6); }
void f7(Rig& r) { tap(*r.h, app::Button::F7); }
void undo(Rig& r) { shift_tap(*r.h, app::Button::F1); }
}  // namespace

TEST_CASE("pattern: RANDOM on each lane writes that lane over steps 1..LENGTH and nothing else") {
  for (unsigned lane = 0; lane < 5; ++lane) {
    if (lane == 3) continue;  // SLICE has its own test (it needs a take)
    INFO("lane " << lane);
    Rig rig;
    mark_pattern_a();
    const std::vector<SV> full = pat_of('A');
    REQUIRE(full.size() == 16u);
    (void)console_words({"pat", "A", "len", "8"});
    to_lane(rig, lane);
    const std::vector<SV> before = pat_of('A');
    REQUIRE(before.size() == 8u);
    f5(rig);
    const std::vector<SV> after = pat_of('A');
    REQUIRE(after.size() == 8u);
    CHECK(lane_sig(after, lane) != lane_sig(before, lane));      // the lane was written
    CHECK(sig(after, 0, 8, lane) == sig(before, 0, 8, lane));    // the other lanes of those steps stayed
    (void)console_words({"pat", "A", "len", "16"});
    const std::vector<SV> wide = pat_of('A');
    REQUIRE(wide.size() == 16u);
    CHECK(sig(wide, 8, 16, 9) == sig(full, 8, 16, 9));  // steps 9..16 were not touched in any lane
    CHECK(rig.h->runtime().frame_failures() == 0u);
  }
}

TEST_CASE("pattern: ORIGINAL is the lane as it was before the first roll, and the seeds scroll with the arrows and the encoder") {
  Rig rig;
  mark_pattern_a();
  const std::vector<SV> s0 = pat_of('A');
  f5(rig);  // seed 1, F5 lit
  const std::vector<SV> p1 = pat_of('A');
  CHECK(lane_sig(p1, 0) != lane_sig(s0, 0));
  tap(*rig.h, app::Button::Right);  // seed 2
  const std::vector<SV> p2 = pat_of('A');
  CHECK(lane_sig(p2, 0) != lane_sig(p1, 0));
  tap(*rig.h, app::Button::Left);  // seed 1 again: the same pattern
  CHECK(pat_of('A') == p1);
  tap(*rig.h, app::Button::Left);  // ORIGINAL
  CHECK(pat_of('A') == s0);
  tap(*rig.h, app::Button::Left);  // the arrows stop at ORIGINAL
  CHECK(pat_of('A') == s0);
  rig.h->turn(1);  // the encoder is the same scroll
  rig.h->run_ms(40);
  CHECK(pat_of('A') == p1);
  rig.h->turn(1);
  rig.h->run_ms(40);
  CHECK(pat_of('A') == p2);  // arrow and encoder reach the same seed 2
  rig.h->turn(-2);
  rig.h->run_ms(40);
  CHECK(pat_of('A') == s0);
  // a roll does not touch the other lanes
  CHECK(sig(p2, 0, 16, 0) == sig(s0, 0, 16, 0));
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: STYLE and DENSITY re-roll a live run with the same seed, and do nothing once the run is over") {
  Rig rig;
  const std::vector<SV> s0 = pat_of('A');
  f5(rig);
  const std::vector<SV> p1 = pat_of('A');  // seed 1, BASS, DENSITY 4
  REQUIRE(any_playing(p1));
  f6(rig);  // STYLE takes the light and the run stays live
  rig.h->turn(1);
  rig.h->run_ms(40);
  const std::vector<SV> lead = pat_of('A');
  CHECK(lane_sig(lead, 0) != lane_sig(p1, 0));
  rig.h->turn(-1);
  rig.h->run_ms(40);
  CHECK(pat_of('A') == p1);  // back to BASS: the same seed gives the same pattern
  f7(rig);                   // DENSITY
  rig.h->turn(2);
  rig.h->run_ms(40);
  const std::vector<SV> dense = pat_of('A');
  CHECK(dense != p1);
  for (std::size_t i = 0; i < p1.size(); ++i) {
    CHECK((!p1[i].playing() || dense[i].playing()));  // a higher density only adds steps
  }
  rig.h->turn(-2);
  rig.h->run_ms(40);
  CHECK(pat_of('A') == p1);
  // F5 is still the run: its seed scrolls again after a tap, and ORIGINAL is the lane before the run
  f5(rig);  // F5 takes the light back and goes to seed 2
  const std::vector<SV> p2 = pat_of('A');
  CHECK(p2 != p1);
  rig.h->turn(-2);
  rig.h->run_ms(40);
  CHECK(pat_of('A') == s0);
  // the run ends when another of the first four keys takes the light: STYLE and DENSITY then write nothing
  f5(rig);
  const std::vector<SV> q1 = pat_of('A');
  tap(*rig.h, app::Button::F3);  // VALUE takes the light: the run is over
  f6(rig);
  rig.h->turn(1);
  rig.h->run_ms(40);
  CHECK(pat_of('A') == q1);
  f7(rig);
  rig.h->turn(1);
  rig.h->run_ms(40);
  CHECK(pat_of('A') == q1);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: CARRIER and LEVEL have one style: F6 takes no light, so a live run keeps F5") {
  for (unsigned lane = 1; lane <= 2; ++lane) {
    INFO("lane " << lane);
    Rig rig;
    to_lane(rig, lane);
    const std::vector<SV> s0 = pat_of('A');
    f5(rig);
    const std::vector<SV> p1 = pat_of('A');
    CHECK(lane_sig(p1, lane) != lane_sig(s0, lane));
    f6(rig);  // nothing to step, no light
    rig.h->turn(-1);  // F5 is still lit: the seed goes to ORIGINAL
    rig.h->run_ms(40);
    CHECK(pat_of('A') == s0);
  }
}

TEST_CASE("pattern: RANDOM on SLICE with SPEAKS on LIVE writes nothing and starts no run") {
  Rig rig;
  to_lane(rig, 3);
  const std::string before = console_words({"pat"});
  f5(rig);
  CHECK(console_words({"pat"}) == before);
  rig.h->turn(1);  // F5 is not lit: the encoder is VALUE, which has no slice to write either
  rig.h->run_ms(40);
  CHECK(console_words({"pat"}) == before);
  // RANDOM ALL rolls the other lanes and leaves SLICE alone
  shift_tap(*rig.h, app::Button::F5);
  const std::vector<SV> all = pat_of('A');
  CHECK(any_playing(all));
  CHECK(lane_sig(all, 3) == lane_sig(pat_of('B'), 3));  // (no slice anywhere: both are all zeros)
  for (const SV& s : all) CHECK(s.slc == 0u);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: RANDOM ALL rolls every lane with one seed and one UNDO puts all of them back; a second one redoes") {
  Rig rig;
  const std::vector<SV> s0 = pat_of('A');
  REQUIRE_FALSE(any_playing(s0));
  shift_tap(*rig.h, app::Button::F5);
  const std::vector<SV> all = pat_of('A');
  CHECK(lane_sig(all, 0) != lane_sig(s0, 0));  // NOTE
  CHECK(lane_sig(all, 1) != lane_sig(s0, 1));  // CARRIER
  CHECK(lane_sig(all, 2) != lane_sig(s0, 2));  // LEVEL
  CHECK(lane_sig(all, 4) != lane_sig(s0, 4));  // MOUTH
  undo(rig);
  CHECK(pat_of('A') == s0);
  undo(rig);  // REDO
  CHECK(pat_of('A') == all);
  undo(rig);
  CHECK(pat_of('A') == s0);
  // it is the same pattern again for the same seed: a second RANDOM ALL takes a new seed
  shift_tap(*rig.h, app::Button::F5);
  CHECK(pat_of('A') != all);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: UNDO after a pad write, a cleared step, SHIFT + step and a held-step overwrite; a second UNDO redoes") {
  Rig rig;
  const std::vector<SV> s0 = pat_of('A');
  undo(rig);  // nothing to undo: nothing changes
  CHECK(pat_of('A') == s0);
  pad(*rig.h, sp(0));  // the hand's note on step 1
  const std::vector<SV> s1 = pat_of('A');
  CHECK(s1 != s0);
  undo(rig);
  CHECK(pat_of('A') == s0);
  undo(rig);
  CHECK(pat_of('A') == s1);  // REDONE
  pad(*rig.h, sp(0));        // a full step under the cursor: cleared
  CHECK(pat_of('A') == s0);
  undo(rig);
  CHECK(pat_of('A') == s1);
  pad(*rig.h, sp(0), true);  // SHIFT + step: a TIE
  const std::vector<SV> s2 = pat_of('A');
  CHECK(s2 != s1);
  undo(rig);
  CHECK(pat_of('A') == s1);
  // a held step overwritten by a palette pad: the cursor is elsewhere, so the held full step is only chosen, and the pad's write is the one act
  pad(*rig.h, sp(3));
  const std::vector<SV> s2b = pat_of('A');
  rig.h->pad_down(sp(0));
  rig.h->run_ms(20);
  rig.h->pad_down(pp(5));
  rig.h->run_ms(20);
  rig.h->pad_up(pp(5));
  rig.h->pad_up(sp(0));
  rig.h->run_ms(40);
  const std::vector<SV> s3 = pat_of('A');
  CHECK(s3 != s2b);
  undo(rig);
  CHECK(pat_of('A') == s2b);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: UNDO after CLEAR LANE, a RANDOM run (one copy for the whole run), a REC pass and VALUE") {
  Rig rig;
  fill(rig, 3);
  const std::vector<SV> filled = pat_of('A');
  // CLEAR LANE (SHIFT + F6, F1 confirms)
  shift_tap(*rig.h, app::Button::F6);
  tap(*rig.h, app::Button::F1);
  const std::vector<SV> cleared = pat_of('A');
  CHECK(cleared != filled);
  undo(rig);
  CHECK(pat_of('A') == filled);
  // a RANDOM run: four seeds, one UNDO back to the lane before the run, a second redoes the last one
  f5(rig);
  rig.h->turn(1);
  rig.h->turn(1);
  rig.h->run_ms(40);
  rig.h->turn(1);
  rig.h->run_ms(40);
  const std::vector<SV> last = pat_of('A');
  CHECK(last != filled);
  undo(rig);
  CHECK(pat_of('A') == filled);
  undo(rig);
  CHECK(pat_of('A') == last);
  undo(rig);  // back to the lane before the run
  CHECK(pat_of('A') == filled);
  // VALUE: a first change on a step takes a copy, the next ones on the same step do not
  tap(*rig.h, app::Button::F3);  // VALUE lit (the run is over)
  tap(*rig.h, app::Button::Right);
  const std::vector<SV> v1 = pat_of('A');
  CHECK(v1 != filled);
  tap(*rig.h, app::Button::Right);
  CHECK(pat_of('A') != v1);
  undo(rig);
  CHECK(pat_of('A') == filled);  // both steps of the value went at once
  // a REC pass: the first write takes the copy, the next ones of the pass do not
  tap(*rig.h, app::Button::Rec);
  pad(*rig.h, pp(3));
  pad(*rig.h, pp(5));
  const std::vector<SV> rec = pat_of('A');
  CHECK(rec != filled);
  undo(rig);
  CHECK(pat_of('A') == filled);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("pattern: UNDO after INSERT, DELETE and a pasted pattern; the copy goes with a change of pattern") {
  Rig rig;
  fill(rig, 3);
  const std::vector<SV> filled = pat_of('A');
  tap(*rig.h, app::Button::Copy);  // the cursor's step
  tap(*rig.h, app::Button::Insert);
  const std::vector<SV> inserted = pat_of('A');
  CHECK(inserted != filled);
  undo(rig);
  CHECK(pat_of('A') == filled);
  tap(*rig.h, app::Button::Delete);  // the cursor's step emptied
  const std::vector<SV> deleted = pat_of('A');
  CHECK(deleted != filled);
  undo(rig);
  CHECK(pat_of('A') == filled);
  // the whole pattern pasted over B
  shift_tap(*rig.h, app::Button::Copy);
  pad(*rig.h, 9);  // pattern B
  const std::vector<SV> b0 = pat_of('B');
  shift_tap(*rig.h, app::Button::Insert);
  const std::vector<SV> b1 = pat_of('B');
  CHECK(b1 != b0);
  undo(rig);
  CHECK(pat_of('B') == b0);
  // the copy is dropped by a change of pattern: A's last edit stays when B is shown
  pad(*rig.h, 8);
  pad(*rig.h, sp(5));  // A: a note on step 6
  const std::vector<SV> a_edit = pat_of('A');
  CHECK(a_edit != filled);
  pad(*rig.h, 9);  // B shown: the copy goes
  undo(rig);
  CHECK(pat_of('A') == a_edit);
  CHECK(pat_of('B') == b0);
  // leaving the screen drops it too
  pad(*rig.h, 8);
  pad(*rig.h, sp(6));
  const std::vector<SV> a_edit2 = pat_of('A');
  tap(*rig.h, app::Button::Performance);
  tap(*rig.h, app::Button::Pattern);
  undo(rig);
  CHECK(pat_of('A') == a_edit2);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

namespace {
// A 1 BAR take into slot 1 (even cuts) that SPEAKS, as tests/pattern_audition_test.cpp: back on PATTERN with the SLICE lane chosen. AUTOPLAY is off, so
// SPEAKS is set by SAMPLE's F2.
void speaks_take(Rig& rig) {
  sim::Harness& h = *rig.h;
  tap(h, app::Button::SampleRec);
  h.run_ms(60);
  o3_voc03er::test::rec_length_one_bar(h);
  o3_voc03er::test::rec_autoplay(h, false);
  o3_voc03er::test::rec_slot(h, 0);
  tap(h, app::Button::Rec);
  h.run_ms(9000);
  tap(h, app::Button::SamplePlay);
  h.run_ms(60);
  tap(h, app::Button::F2);  // SPEAKS: this slot
  h.run_ms(60);
  tap(h, app::Button::Pattern);
  h.run_ms(60);
  tap(h, app::Button::Note);
  tap(h, app::Button::Fx);  // the SLICE lane
}
}  // namespace

TEST_CASE("pattern: RANDOM on SLICE rolls the SPEAKS slot's cuts: CHOP on the playing steps, ORDER in order, UNDO and ORIGINAL back") {
  Rig rig;
  speaks_take(rig);
  (void)console_words({"pat", "A", "len", "8"});
  const std::vector<SV> s0 = pat_of('A');
  REQUIRE_FALSE(any_playing(s0));
  f5(rig);
  const std::vector<SV> chop = pat_of('A');
  REQUIRE(chop.size() == 8u);
  CHECK(chop[0].slc >= 1u);  // CHOP always plays the first step
  unsigned playing = 0, top = 0;
  for (const SV& s : chop) {
    playing += s.slc != 0u ? 1u : 0u;
    top = s.slc > top ? s.slc : top;
  }
  CHECK(playing >= 1u);
  CHECK(top <= 24u);
  (void)console_words({"pat", "A", "len", "16"});
  const std::vector<SV> wide = pat_of('A');
  for (std::size_t i = 8; i < wide.size(); ++i) CHECK(wide[i].slc == 0u);  // only 1..LENGTH
  (void)console_words({"pat", "A", "len", "8"});
  f6(rig);  // STYLE: CHOP, ORDER
  rig.h->turn(1);
  rig.h->run_ms(40);
  const std::vector<SV> order = pat_of('A');
  unsigned last = 0;
  bool ascending = true;
  for (const SV& s : order) {
    if (s.slc == 0u) continue;
    ascending = ascending && s.slc > last;
    last = s.slc;
  }
  CHECK(ascending);  // slice k + 1 on step floor(k * len / N), in order
  CHECK(order[0].slc == 1u);
  f5(rig);
  rig.h->turn(-5);  // ORIGINAL (the arrows and the encoder stop there)
  rig.h->run_ms(40);
  CHECK(pat_of('A') == s0);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

namespace {
double listen_rms(sim::Harness& h, unsigned ms) {
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
}  // namespace

TEST_CASE("pattern: the SLICE lane's palette pad alone brings the note: it sounds while held and stops on the release and on leave") {
  constexpr double kSound = 1e-4;
  // The observable is the output energy: BANDS speaks while it hears the tone on the fake capture and a carrier note is held, so a slice with no note is silence.
  {
    Rig rig;
    rig.h->record(true);
    speaks_take(rig);
    CHECK(listen_rms(*rig.h, 300) < kSound);  // nothing held: the tone alone does not speak
    rig.h->pad_down(pp(2));
    CHECK(listen_rms(*rig.h, 120) > kSound);  // the slice and its note
    rig.h->pad_up(pp(2));
    rig.h->run_ms(3000);  // the slice ends by itself (ONE SHOT): the note is gone
    CHECK(listen_rms(*rig.h, 300) < kSound);
  }
  {
    Rig rig;
    rig.h->record(true);
    speaks_take(rig);
    rig.h->pad_down(pp(2));
    CHECK(listen_rms(*rig.h, 120) > kSound);
    tap(*rig.h, app::Button::Performance);  // leave with the pad held: the note ends with the screen
    rig.h->run_ms(3000);
    CHECK(listen_rms(*rig.h, 300) < kSound);
    rig.h->pad_up(pp(2));  // the late release is harmless
    rig.h->run_ms(100);
    CHECK(rig.h->runtime().frame_failures() == 0u);
  }
}
