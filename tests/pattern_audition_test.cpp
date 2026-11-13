// SPDX-License-Identifier: GPL-3.0-or-later
// PATTERN's audition and held steps (Q3 to Q5): a palette pad sounds while it is held. The observable is the output energy: the vocoder speaks while it hears the
// modulator (a tone on the fake capture) and a carrier note is held, and falls silent once the note-off arrives.

#include <doctest/doctest.h>

#include "tract_skip.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/app/runtime.h>
#include <o3_voc03er/units/vocoder/vocoder.h>
#include <o3sdk/platform/host/fake_capture.h>
#include <o3sdk/sim/harness.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

using o3_voc03er::Vocoder;
namespace app = o3sdk::app;
namespace sim = o3sdk::sim;

namespace {

void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(40);
}

struct Rig {
  o3sdk::platform::host::FakeCapture cap;
  std::unique_ptr<sim::Harness> h;
  Rig() {
    cap.input_tone(220.0f, -12.0f);
    h = std::make_unique<sim::Harness>(::o3sdk_app(), nullptr, nullptr, nullptr, nullptr, &cap);
    REQUIRE(h->start());
    h->record(true);
    h->run_ms(100);
    tap(*h, app::Button::Performance);  // PERFORM first, as from home
    tap(*h, app::Button::Pattern);
    h->run_ms(60);
  }
  unsigned led(unsigned p) const { return h->leds().level(p); }
  std::size_t mark() const { return h->audio().size(); }
  // The rms (fraction of full scale) of the samples recorded since `from`.
  double rms_since(std::size_t from) const {
    const std::vector<std::int16_t>& a = h->audio();
    double s = 0.0;
    std::size_t n = 0;
    for (std::size_t i = from; i < a.size(); i += 2) {
      s += static_cast<double>(a[i]) * static_cast<double>(a[i]);
      ++n;
    }
    return n != 0 ? std::sqrt(s / static_cast<double>(n)) / 32768.0 : 0.0;
  }
  // The rms of the next `ms` milliseconds.
  double listen(unsigned ms) {
    const std::size_t from = mark();
    h->run_ms(ms);
    return rms_since(from);
  }
};

constexpr double kSound = 1e-4;  // above: a note is sounding
constexpr unsigned kNoteA = 5;   // a palette pad of row 0 (A NOTE, a single note)
constexpr unsigned kNoteB = 7;
constexpr unsigned kChordPad = 17;  // row 1 (B TRIAD): a chord

}  // namespace

TEST_CASE("audition: a palette pad sounds while held and lets go on the release, and its LED is on") {
  Rig rig;
  CHECK(rig.listen(200) < kSound);  // nothing sounds before
  rig.h->pad_down(kNoteA);
  CHECK(rig.listen(300) > kSound);
  CHECK(rig.led(kNoteA) == app::kLedOn);
  CHECK(rig.listen(300) > kSound);  // it holds
  rig.h->pad_up(kNoteA);
  rig.h->run_ms(40);
  CHECK(rig.led(kNoteA) != app::kLedOn);
  rig.h->run_ms(700);  // the release tail
  CHECK(rig.listen(200) < kSound);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("audition: a chord pad holds its chord, and the step it wrote is a chord") {
  Rig rig;
  rig.h->pad_down(kChordPad);
  CHECK(rig.listen(300) > kSound);
  rig.h->pad_up(kChordPad);
  rig.h->run_ms(900);
  CHECK(rig.listen(200) < kSound);
  CHECK(rig.led(24) == app::kLedDim);  // step 1 holds the chord (the press wrote it)
  CHECK(rig.led(25) == app::kLedMid);  // and the cursor moved on
}

TEST_CASE("audition: SHIFT + a palette pad sounds and writes nothing") {
  Rig rig;
  rig.h->press(app::Button::Shift);
  rig.h->run_ms(5);
  rig.h->pad_down(kNoteA);
  CHECK(rig.listen(300) > kSound);
  CHECK(rig.led(24) == app::kLedMid);  // the cursor stays on step 1, which stays empty
  CHECK(rig.led(25) == app::kLedOff);
  rig.h->pad_up(kNoteA);
  rig.h->release(app::Button::Shift);
  rig.h->run_ms(900);
  CHECK(rig.listen(200) < kSound);
  CHECK(rig.led(24) == app::kLedMid);
}

TEST_CASE("audition: leaving PATTERN with a pad held lets it go") {
  Rig rig;
  rig.h->pad_down(kNoteA);
  CHECK(rig.listen(300) > kSound);
  tap(*rig.h, app::Button::Performance);  // a screen button leaves at any moment
  rig.h->run_ms(900);
  CHECK(rig.listen(200) < kSound);
  rig.h->pad_up(kNoteA);  // the late release is harmless
  rig.h->run_ms(100);
  CHECK(rig.listen(200) < kSound);
  tap(*rig.h, app::Button::Pattern);  // and PATTERN comes back with no pad lit
  CHECK(rig.led(kNoteA) != app::kLedOn);
}

TEST_CASE("audition: with the transport playing a step's note-off does not cut a held pad") {
  Rig rig;
  tap(*rig.h, app::Button::Performance);
  tap(*rig.h, app::Button::Pattern);
  rig.h->pad_down(kNoteA);  // step 1 gets a note; the cursor moves to step 2, which stays empty
  rig.h->run_ms(20);
  rig.h->pad_up(kNoteA);
  rig.h->run_ms(900);
  tap(*rig.h, app::Button::Play);
  // Step 1 sounds at the start of each bar and its note-off comes before the bar's second half: a quiet window of the bar.
  const double first = rig.listen(150);
  CHECK(first > kSound);  // the step's note
  rig.h->run_ms(550);
  const double quiet = rig.listen(500);  // nothing sounds in this window of the bar
  CHECK(quiet < first * 0.1);
  rig.h->pad_down(kNoteB);  // a different note held through the next bar's start and its note-off
  rig.h->run_ms(100);
  const double held = rig.listen(1500);
  CHECK(held > quiet * 10.0 + kSound);
  rig.h->pad_up(kNoteB);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

// NO1: the audition holds the very note the pattern plays. The unit holds a note until its last owner lets go, so the sequencer's gate off and its
// Stop do not cut the pad. (Before NO1 both cut it: the voice was one per note.)
TEST_CASE("audition: a pad on the very note the pattern plays sounds through the step's gate off and through Stop") {
  Rig rig;
  tap(*rig.h, app::Button::Performance);
  tap(*rig.h, app::Button::Pattern);
  rig.h->pad_down(kNoteA);  // step 1 gets note A
  rig.h->run_ms(20);
  rig.h->pad_up(kNoteA);
  rig.h->run_ms(900);
  tap(*rig.h, app::Button::Play);
  const double first = rig.listen(150);
  CHECK(first > kSound);  // the step's note
  rig.h->run_ms(550);
  rig.h->press(app::Button::Shift);  // audition: sounds, writes nothing
  rig.h->run_ms(5);
  rig.h->pad_down(kNoteA);  // the same note the step plays, held across the next bar's start and its gate off
  rig.h->run_ms(100);
  for (int w = 0; w < 8; ++w) {  // 4 s: more than a bar, whose release tail would otherwise leave a silent window
    CHECK(rig.listen(500) > kSound);
  }
  tap(*rig.h, app::Button::Play);  // Stop: the sequencer's notes let go, the pad's does not
  rig.h->run_ms(900);              // past the release tail of a note that had been cut
  CHECK(rig.listen(300) > kSound);
  rig.h->pad_up(kNoteA);
  rig.h->release(app::Button::Shift);
  rig.h->run_ms(900);
  CHECK(rig.listen(200) < kSound);  // the last holder let go: silent
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

// Q4. A step pad held while a palette pad is pressed: every held step takes it and the cursor stays.
TEST_CASE("held steps: a palette pad writes every held step and the cursor does not advance") {
  Rig rig;
  rig.h->pad_down(24);  // steps 1 and 2 held; the cursor is on the last, step 2
  rig.h->pad_down(25);
  rig.h->run_ms(40);
  rig.h->pad_down(kNoteA);
  CHECK(rig.listen(300) > kSound);  // it still sounds
  rig.h->pad_up(kNoteA);
  rig.h->run_ms(40);
  CHECK(rig.led(24) == app::kLedDim);  // step 1 holds a note
  CHECK(rig.led(25) == app::kLedMid);  // step 2 too, and the cursor is still on it
  CHECK(rig.led(26) == app::kLedOff);  // it did not move on
  rig.h->pad_up(24);
  rig.h->pad_up(25);
  rig.h->run_ms(40);
  rig.h->pad_down(kNoteB);  // nothing held now: the old behaviour, a write on the cursor (step 2) and the cursor moves on
  rig.h->run_ms(40);
  rig.h->pad_up(kNoteB);
  rig.h->run_ms(40);
  CHECK(rig.led(26) == app::kLedMid);
  CHECK(rig.led(25) == app::kLedDim);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

// UP and DOWN are PERFORM's octave: the palette's notes move by 12 (a step written at octave N matches pad 5 only at octave N), and they stop at its clamp.
TEST_CASE("octave: UP and DOWN move the palette by 12 and stop at PERFORM's clamp") {
  Rig rig;
  const auto select = [&](unsigned step_pad) {
    rig.h->pad_down(step_pad);
    rig.h->run_ms(20);
    rig.h->pad_up(step_pad);
    rig.h->run_ms(40);
  };
  const auto matches = [&](unsigned step_pad) {  // the cursor on that step: does the palette pad 5 light as its note?
    select(step_pad);
    return rig.led(kNoteA) == app::kLedMid;
  };
  rig.h->pad_down(kNoteA);  // step 1 at octave 0
  rig.h->run_ms(20);
  rig.h->pad_up(kNoteA);
  rig.h->run_ms(40);
  CHECK(matches(24));
  tap(*rig.h, app::Button::Up);
  CHECK_FALSE(matches(24));  // the palette went up: pad 5 is 12 above step 1's note
  tap(*rig.h, app::Button::Down);
  CHECK(matches(24));
  // Up to the top: octave 2. A step written there matches while UP stays pressed on; one more UP changes nothing.
  for (int i = 0; i < 5; ++i) {
    tap(*rig.h, app::Button::Up);
  }
  select(25);
  rig.h->pad_down(kNoteA);  // step 2 at octave 2
  rig.h->run_ms(20);
  rig.h->pad_up(kNoteA);
  rig.h->run_ms(40);
  CHECK(matches(25));
  tap(*rig.h, app::Button::Up);
  CHECK(matches(25));  // clamped at +2
  tap(*rig.h, app::Button::Down);
  CHECK_FALSE(matches(25));
  tap(*rig.h, app::Button::Up);
  CHECK(matches(25));
  // And the bottom: -3, five octaves below +2.
  for (int i = 0; i < 6; ++i) {
    tap(*rig.h, app::Button::Down);
  }
  select(26);
  rig.h->pad_down(kNoteA);  // step 3 at octave -3
  rig.h->run_ms(20);
  rig.h->pad_up(kNoteA);
  rig.h->run_ms(40);
  CHECK(matches(26));
  tap(*rig.h, app::Button::Down);
  CHECK(matches(26));
  tap(*rig.h, app::Button::Up);
  CHECK_FALSE(matches(26));
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

// Q5. LEVEL: the palette pad writes the level and sounds the cursor step's note at it; SLICE: it fires the slice on the SPEAKS slot.
TEST_CASE("level: a palette pad sounds the cursor step's note at that level, louder when higher; a rest sounds nothing") {
  Rig rig;
  rig.h->pad_down(kNoteA);  // step 1 holds a note, the cursor moves on to step 2
  rig.h->run_ms(20);
  rig.h->pad_up(kNoteA);
  rig.h->run_ms(900);
  tap(*rig.h, app::Button::Vol);
  const auto select = [&](unsigned step_pad) {
    rig.h->pad_down(step_pad);
    rig.h->run_ms(20);
    rig.h->pad_up(step_pad);
    rig.h->run_ms(40);
  };
  const auto hear = [&](unsigned level_pad) {
    select(24);
    rig.h->pad_down(level_pad);
    const double v = rig.listen(300);
    CHECK(rig.led(level_pad) == app::kLedOn);
    rig.h->pad_up(level_pad);
    rig.h->run_ms(900);
    return v;
  };
  const double low = hear(0);
  const double high = hear(23);
  CHECK(low > kSound);
  CHECK(high > low);
  select(26);  // step 3 is a rest: the level is written and nothing sounds
  rig.h->pad_down(12);
  CHECK(rig.listen(300) < kSound);
  rig.h->pad_up(12);
  rig.h->run_ms(100);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

namespace {
constexpr std::uint16_t kTrigReader0 = Vocoder::region_param(0, Vocoder::RegionField::Trig);
constexpr std::uint16_t kStartReader0 = Vocoder::region_param(0, Vocoder::RegionField::Start);
float unit_param(sim::Harness& h, std::uint16_t id) {
  float best = 0.0f;
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(h, u)) continue;
    const float v = h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
}
}  // namespace

TEST_CASE("slice: a palette pad writes the slice and fires it on the SPEAKS slot; with SPEAKS on LIVE nothing fires") {
  Rig rig;
  // A 1 BAR take into slot 1 (even cuts), SPEAKS on that slot, as SAMPLE does it.
  tap(*rig.h, app::Button::SampleRec);
  rig.h->run_ms(60);
  tap(*rig.h, app::Button::F3);
  tap(*rig.h, app::Button::F4);
  tap(*rig.h, app::Button::F4);
  tap(*rig.h, app::Button::F4);
  tap(*rig.h, app::Button::Rec);
  rig.h->run_ms(9000);
  tap(*rig.h, app::Button::Pattern);
  rig.h->run_ms(60);
  tap(*rig.h, app::Button::Fx);
  CHECK(unit_param(*rig.h, kTrigReader0) == doctest::Approx(0.0f));
  rig.h->pad_down(2);  // SPEAKS is LIVE: slice 3 has nothing to fire
  rig.h->run_ms(100);
  rig.h->pad_up(2);
  rig.h->run_ms(60);
  CHECK(unit_param(*rig.h, kTrigReader0) == doctest::Approx(0.0f));
  tap(*rig.h, app::Button::SamplePlay);
  rig.h->run_ms(60);
  tap(*rig.h, app::Button::F2);  // SPEAKS: this slot (a second tap would go back to LIVE)
  rig.h->run_ms(60);
  tap(*rig.h, app::Button::Pattern);
  rig.h->run_ms(60);  // the lane is still SLICE (a second FX would reach MOUTH)
  rig.h->pad_down(1);  // slice 2
  rig.h->run_ms(60);
  CHECK(unit_param(*rig.h, kTrigReader0) == doctest::Approx(1.0f));
  const float s2 = unit_param(*rig.h, kStartReader0);
  CHECK(rig.led(1) == app::kLedOn);
  rig.h->pad_up(1);
  rig.h->run_ms(300);
  rig.h->pad_down(5);  // slice 6 starts later in the slot
  rig.h->run_ms(60);
  CHECK(unit_param(*rig.h, kStartReader0) > s2);
  rig.h->pad_up(5);
  rig.h->run_ms(60);
  CHECK(rig.h->runtime().frame_failures() == 0u);
}

TEST_CASE("slice: PATTERN's fire_slice stays one shot with the slot's LOOP ON (a repeat is the SAMPLE pad's)") {
  Rig rig;
  tap(*rig.h, app::Button::SampleRec);
  rig.h->run_ms(60);
  tap(*rig.h, app::Button::F3);
  tap(*rig.h, app::Button::F4);
  tap(*rig.h, app::Button::F4);
  tap(*rig.h, app::Button::F4);
  tap(*rig.h, app::Button::Rec);
  rig.h->run_ms(9000);
  tap(*rig.h, app::Button::SamplePlay);
  rig.h->run_ms(60);
  for (unsigned i = 0; i < 3; ++i) tap(*rig.h, app::Button::Up);  // SAMPLE keeps its cursor across the harnesses of the process: slot 1
  // SAMPLE REC's AFTER also persists, so the take may already play (SPEAKS or SOUNDS) or sit on PADS: get to "speaks, and LOOP ON" from either.
  const std::uint16_t play0 = Vocoder::loop_param(0, Vocoder::LoopField::Play);
  if (unit_param(*rig.h, play0) != 1.0f) {
    tap(*rig.h, app::Button::F5);  // LOOP ON (on a PADS slot it makes the slot speak first)
    rig.h->run_ms(300);
  }
  if (unit_param(*rig.h, 47) != 3.0f) {
    tap(*rig.h, app::Button::F2);  // SPEAKS: this slot
    rig.h->run_ms(300);
  }
  REQUIRE(unit_param(*rig.h, 47) == doctest::Approx(3.0f));  // ModSrc: slot 1 speaks
  REQUIRE(unit_param(*rig.h, play0) == doctest::Approx(1.0f));  // and its loop plays
  rig.h->run_ms(2500);
  tap(*rig.h, app::Button::Pattern);
  rig.h->run_ms(60);
  tap(*rig.h, app::Button::Fx);  // the SLICE lane
  rig.h->pad_down(1);
  rig.h->run_ms(60);
  // The slot's own reader plays the loop, so the slice runs on another reader: look at the Trig of all four. One shot is 1, a repeat would be 2.
  float most = 0.0f;
  float seen = 0.0f;
  for (unsigned k = 0; k < 4; ++k) {
    const float t = unit_param(*rig.h, Vocoder::region_param(k, Vocoder::RegionField::Trig));
    most = std::max(most, t);
    seen += t > 0.0f ? 1.0f : 0.0f;
  }
  CHECK(seen >= 1.0f);                       // the slice sounds
  CHECK(most == doctest::Approx(1.0f));      // one shot, not Trig 2
  rig.h->pad_up(1);
  rig.h->run_ms(200);
  for (unsigned k = 0; k < 4; ++k) {
    CHECK(unit_param(*rig.h, Vocoder::region_param(k, Vocoder::RegionField::Trig)) < 1.5f);
  }
  CHECK(rig.h->runtime().frame_failures() == 0u);
}
