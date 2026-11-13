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

TEST_CASE("sample: F1 LOW PADS values, steps and the name of a pair outside the list") {
  using o3_voc03er::pads::Plays;
  CHECK(sm::kLowPadsCount == 6u);
  CHECK(std::strcmp(sm::kLowPadsNames[0], "NOTE+TRIAD") == 0);
  CHECK(sm::low_pads_index(Plays::Note, Plays::Triad) == 0);
  CHECK(sm::low_pads_index(Plays::Power, Plays::Power) == 5);
  CHECK(sm::low_pads_index(Plays::Seventh, Plays::Note) == -1);  // not a pair of the list: MIXED
  CHECK(sm::low_pads_index(Plays::Off, Plays::Note) == -1);
  CHECK(sm::low_pads_step(0, 1, false) == 1u);
  CHECK(sm::low_pads_step(5, 1, false) == 5u);  // the encoder stops
  CHECK(sm::low_pads_step(5, 1, true) == 0u);   // a tap wraps
  CHECK(sm::low_pads_step(0, -1, false) == 0u);
  CHECK(sm::low_pads_step(-1, 1, false) == 0u);
  CHECK(std::strcmp(sm::low_pads_name(Plays::Seventh, Plays::Off), "MIXED") == 0);
  CHECK(std::strcmp(sm::low_pads_name(Plays::Note, Plays::Seventh), "MIXED") == 0);  // PATTERN's A NOTE with B 7TH: row B is not NOTE
  CHECK(std::strcmp(sm::low_pads_key(Plays::Note, Plays::Seventh), "MIXED") == 0);
  CHECK(sm::low_pads_step(sm::low_pads_index(Plays::Note, Plays::Seventh), 1, false) == 0u);  // the first step from MIXED: NOTE+TRIAD
  CHECK(sm::low_pads_step(sm::low_pads_index(Plays::Note, Plays::Seventh), -1, true) == 0u);
  CHECK(std::strcmp(sm::low_pads_name(Plays::Off, Plays::Note), "MIXED") == 0);  // the next step repairs it
  CHECK(std::strcmp(sm::low_pads_name(Plays::Note, Plays::Triad), "NOTE+TRIAD") == 0);
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
  for (const app::Button b : {app::Button::Up, app::Button::Down, app::Button::Left, app::Button::Right, app::Button::Shift,
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

TEST_CASE("sample: F1 LOW PADS sets both lower rows at once, steps from the first tap, and never goes OFF") {
  LayoutRig r;
  CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) < 12u);
  CHECK(r.count(kRowB, kRowB + 11, app::kLedOff) < 12u);
  r.h.turn(1);  // NOTE+TRIAD to NOTE: both rows follow
  r.h.run_ms(300);
  CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) < 12u);
  CHECK(r.count(kRowB, kRowB + 11, app::kLedOff) < 12u);
  r.h.pad_down(kRowB + 5);  // row B plays a plain note now: one pitch, not a chord
  CHECK(r.listen(300) > kSound);
  r.h.pad_up(kRowB + 5);
  r.h.run_ms(900);
  for (int i = 0; i < 12; ++i) {  // past the last value, and back: no step is dark
    r.h.turn(1);
    r.h.run_ms(250);
    CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) < 12u);
    CHECK(r.count(kRowB, kRowB + 11, app::kLedOff) < 12u);
  }
  r.h.pad_down(kRowA + 5);
  CHECK(r.listen(300) > kSound);  // POWER on A
  r.h.pad_up(kRowA + 5);
  r.h.run_ms(900);
  r.h.turn(-40);  // stops at NOTE+TRIAD, the first
  r.h.run_ms(300);
  CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) < 12u);
  CHECK(r.count(kRowB, kRowB + 11, app::kLedOff) < 12u);
  tap(r.h, app::Button::F1);  // a tap steps at once (no focus-only first tap) and wraps at the end
  r.h.run_ms(300);
  CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) < 12u);
  CHECK(r.h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample: both lower rows flash dark when F1 steps LOW PADS; SHIFT + F1 does nothing") {
  LayoutRig r;
  r.h.turn(1);
  r.h.run_ms(20);
  CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) == 12u);  // both rows' pads are dark for the flash (150 ms)
  CHECK(r.count(kRowB, kRowB + 11, app::kLedOff) == 12u);
  r.h.run_ms(300);
  CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) < 12u);
  CHECK(r.count(kRowB, kRowB + 11, app::kLedOff) < 12u);
  r.h.press(app::Button::Shift);
  r.h.run_ms(5);
  r.h.press(app::Button::F1);
  r.h.run_ms(20);
  r.h.release(app::Button::F1);
  r.h.run_ms(20);
  CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) < 12u);  // no flash: nothing changed
  CHECK(r.count(kRowB, kRowB + 11, app::kLedOff) < 12u);
  r.h.release(app::Button::Shift);
}

TEST_CASE("sample: a held lower pad lets go when F1 steps LOW PADS") {
  LayoutRig r;
  r.h.pad_down(kRowB + 5);
  r.h.run_ms(60);
  r.h.turn(2);  // the held pad's old meaning ends, its note lets go
  r.h.run_ms(60);
  r.h.pad_up(kRowB + 5);
  r.h.run_ms(900);
  CHECK(r.listen(200) < kSound);
  CHECK(r.count(kRowB, kRowB + 11, app::kLedOff) < 12u);
}

TEST_CASE("sample: two rows of the same kind play the same notes an octave apart") {
  o3_voc03er::pads::Layout l;
  CHECK(sm::low_pads_index(l.row[0], l.row[1]) == 0);
  l.row[0] = o3_voc03er::pads::Plays::Note;
  l.row[1] = o3_voc03er::pads::Plays::Note;
  CHECK(sm::low_pads_index(l.row[0], l.row[1]) == 1);
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

TEST_CASE("sample: in TRACT the lower pads still play notes, F1 is a note kind, and PERFORM's strips are untouched") {
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
  r.h.turn(5);  // F1 LOW PADS: both rows to POWER (a strip name is not on the list)
  r.h.run_ms(300);
  CHECK(r.count(kRowA, kRowA + 11, app::kLedOff) < 12u);
  r.h.pad_down(kRowA + 5);
  r.h.run_ms(60);
  CHECK(tract_param(r.h, TractUnit::Vowel) == doctest::Approx(vowel));
  r.h.pad_up(kRowA + 5);
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

TEST_CASE("sample: SLICES POLY with LOOP ON still repeats one slice per slot: the next pad takes the repeat over on the slot's own reader") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  tap(h, app::Button::F7);  // SLICES: POLY
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
