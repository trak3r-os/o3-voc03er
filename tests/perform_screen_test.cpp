// SPDX-License-Identifier: GPL-3.0-or-later
// PERFORM on the engine: a chord pad sounds a chord, SHIFT latches it, DELETE lets it go; a frame fits the display list.
#include <doctest/doctest.h>

#include "tract_skip.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/platform/host/fake_capture.h>
#include <o3sdk/sim/harness.h>

#include <o3_voc03er/route.h>
#include <o3_voc03er/units/tract/tract.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;

namespace {
void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(5);
}

void open_help(sim::Harness& h) {
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Config);
  h.release(app::Button::Shift);
  h.run_ms(60);
  REQUIRE(h.runtime().help_open());
}

bool has_row(const app::Help& help, app::Button b) {
  for (std::size_t i = 0; i < help.button_count; ++i) {
    if (help.buttons[i].button == b) {
      return help.buttons[i].text != nullptr;
    }
  }
  return false;
}

// The buttons app.cpp's input() answers on PERFORM. A button with no row in HELP draws as a hollow cap, which says it does nothing.
constexpr app::Button kAnswered[] = {app::Button::Params,     app::Button::Performance, app::Button::Play,       app::Button::Master,
                                     app::Button::File,       app::Button::SampleLoad,  app::Button::SamplePlay, app::Button::SampleEdit,
                                     app::Button::SampleRec,  app::Button::Song,        app::Button::Pattern,    app::Button::Note,
                                     app::Button::Instr,      app::Button::Vol,         app::Button::Fx};
}  // namespace

TEST_CASE("perform: a held chord pad lights, SHIFT latches it, DELETE releases it") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  h->pad_down(26);
  h->run_ms(100);
  CHECK(h->leds().level(26) == 255);
  tap(*h, app::Button::Shift);
  h->pad_up(26);
  h->run_ms(100);
  CHECK(h->leds().level(26) == 140);  // latched: Mid
  tap(*h, app::Button::Delete);
  h->run_ms(100);
  CHECK(h->leds().level(26) != 140);
  CHECK(h->runtime().frame_failures() == 0u);
  CHECK(h->display().peak_frame_words() < 2048u);
}

TEST_CASE("perform: HELP names every control, and its legend is the page in force") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);

  open_help(*h);
  {
    const app::Help& help = h->runtime().help_overlay().help();
    app::HelpProblem p;
    const bool ok = app::help_check(help, &p);
    CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
    for (const app::Button b : kAnswered) {
      INFO(std::string(app::button_name(b)));
      CHECK(has_row(help, b));
    }
    for (const app::Button b : {app::Button::Rec, app::Button::Shift, app::Button::Delete, app::Button::Up, app::Button::Down, app::Button::Left, app::Button::Right}) {
      INFO(std::string(app::button_name(b)));
      CHECK(has_row(help, b));
    }
    for (unsigned k = 0; k < 8; ++k) {
      CHECK(help.fkey[k] != nullptr);
      CHECK(help.fkey_shift[k] != nullptr);  // the SHIFT row: all eight keys are live (F7 is the OCTAVE)
    }
    CHECK(help.encoder != nullptr);
    CHECK(help.group_count == 5u);  // the controls, rows A, B and C, the LEDs
    for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
      CHECK(help.pad_group[pad] == app::pad_row(pad) + 1u);
      // Every pad has a label but the gaps of the controls row (columns 2, 5 and 8), which are dark and silent.
      CHECK((help.pad_label[pad] != nullptr) == (pad >= 12u || (app::pad_col(pad) != 2u && app::pad_col(pad) != 5u && app::pad_col(pad) != 8u)));
    }
  }
  tap(*h, app::Button::Config);
  REQUIRE_FALSE(h->runtime().help_open());

  // A as a parameter strip (F7 takes the focus, the encoder steps A past OFF to the first strip), then C to BEND (F6 to row C, F7 steps it): the legend follows.
  tap(*h, app::Button::F7);
  h->turn(6);
  tap(*h, app::Button::F6);
  tap(*h, app::Button::F6);
  tap(*h, app::Button::F6);
  tap(*h, app::Button::F7);
  tap(*h, app::Button::F7);
  h->run_ms(200);
  open_help(*h);
  {
    const app::Help& help = h->runtime().help_overlay().help();
    app::HelpProblem p;
    const bool ok = app::help_check(help, &p);
    CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
    CHECK(help.group_count == 5u);
    for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
      CHECK(help.pad_group[pad] == app::pad_row(pad) + 1u);
    }
  }
  tap(*h, app::Button::Config);
}

TEST_CASE("perform: the controls row lights while held and plays no note; a gap is dark; row C is the formant strip by default") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  CHECK(h->leds().level(7) == 24);   // ROOT+: dim
  CHECK(h->leds().level(2) == 0);    // a gap
  CHECK(h->leds().level(36) == 24);  // row C FORMANT: -12 dim
  h->pad_down(7);
  h->run_ms(60);
  CHECK(h->leds().level(7) == 255);
  h->pad_up(7);
  h->run_ms(60);
  CHECK(h->leds().level(7) == 24);
  h->pad_down(2);  // a gap: nothing
  h->run_ms(60);
  CHECK(h->leds().level(2) == 0);
  h->pad_up(2);
  h->pad_down(9);  // MOD, held for the ramp
  h->run_ms(500);
  CHECK(h->leds().level(9) == 255);
  h->pad_up(9);
  h->run_ms(500);
  CHECK(h->runtime().frame_failures() == 0u);
}

namespace {
// A unit parameter by id, read off whichever of the first units holds it (as app_test.cpp's peak_param).
float unit_param(sim::Harness& h, std::uint16_t id) {
  float best = 0.0f;
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(h, u)) continue;
    const float v = h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
}
void shift_tap(sim::Harness& h, app::Button b) {
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, b);
  h.release(app::Button::Shift);
  h.run_ms(20);
}
constexpr std::uint16_t kWave = 0, kFreeze = 9, kDetune = 6, kFormant = 40, kThru = 41, kGlide = 44, kGate = 45, kArp = 46;
}  // namespace

TEST_CASE("perform: the SHIFT row focuses FORMANT, THRU, GATE, ARP, GLIDE and PRESET, and FREEZE toggles") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  shift_tap(*h, app::Button::F1);  // FREEZE on, off
  h->run_ms(20);
  CHECK(unit_param(*h, kFreeze) == doctest::Approx(1.0f));
  shift_tap(*h, app::Button::F1);
  h->run_ms(20);
  CHECK(unit_param(*h, kFreeze) == doctest::Approx(0.0f));

  shift_tap(*h, app::Button::F5);  // takes the focus: the encoder moves FORMANT a semitone a detent, SHIFT coarse 4
  h->turn(3);
  h->run_ms(20);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(3.0f));
  h->press(app::Button::Shift);
  h->run_ms(2);
  h->turn(-1);
  h->release(app::Button::Shift);
  h->run_ms(20);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(-1.0f));
  shift_tap(*h, app::Button::F5);  // a second SHIFT + tap steps it
  h->run_ms(20);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(0.0f));

  shift_tap(*h, app::Button::F6);  // THRU: 5 a detent in 0..100, the unit's 0..1
  h->turn(2);
  h->run_ms(20);
  CHECK(unit_param(*h, kThru) == doctest::Approx(0.10f));

  shift_tap(*h, app::Button::F2);  // GATE: the encoder steps the enum, a second SHIFT + tap steps it on
  h->turn(2);
  h->run_ms(20);
  CHECK(unit_param(*h, kThru) == doctest::Approx(0.10f));
  CHECK(unit_param(*h, kGate) == doctest::Approx(2.0f));
  shift_tap(*h, app::Button::F2);
  h->run_ms(20);
  CHECK(unit_param(*h, kGate) == doctest::Approx(3.0f));
  h->turn(-3);
  h->run_ms(20);
  CHECK(unit_param(*h, kGate) == doctest::Approx(0.0f));
  shift_tap(*h, app::Button::F3);  // ARP: OFF, UP, DOWN, UP-DOWN, RANDOM, round
  h->turn(-1);
  h->run_ms(20);
  CHECK(unit_param(*h, kArp) == doctest::Approx(4.0f));
  shift_tap(*h, app::Button::F3);
  h->run_ms(20);
  CHECK(unit_param(*h, kArp) == doctest::Approx(0.0f));
  shift_tap(*h, app::Button::F4);  // GLIDE: 10 ms a detent, SHIFT coarse 50
  h->turn(3);
  h->run_ms(20);
  CHECK(unit_param(*h, kGlide) == doctest::Approx(30.0f));
  h->press(app::Button::Shift);
  h->run_ms(2);
  h->turn(2);
  h->release(app::Button::Shift);
  h->run_ms(20);
  CHECK(unit_param(*h, kGlide) == doctest::Approx(130.0f));
  shift_tap(*h, app::Button::F4);
  h->run_ms(20);
  CHECK(unit_param(*h, kGlide) == doctest::Approx(140.0f));

  shift_tap(*h, app::Button::F6);  // back to THRU
  h->turn(1);
  h->run_ms(20);
  CHECK(unit_param(*h, kThru) == doctest::Approx(0.15f));

  // PRESET: a turn applies the sound at once and leaves KEY alone; the second SHIFT + tap goes to the next
  const float key_before = unit_param(*h, 35);
  shift_tap(*h, app::Button::F8);
  h->turn(1);  // from none the first turn picks ROBOT
  h->run_ms(20);
  CHECK(unit_param(*h, kGlide) == doctest::Approx(0.0f));
  CHECK(unit_param(*h, kThru) == doctest::Approx(0.0f));
  CHECK(unit_param(*h, kDetune) == doctest::Approx(0.0f));
  h->turn(3);  // GIANT: formant -9, glide 250
  h->run_ms(20);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(-9.0f));
  CHECK(unit_param(*h, kGlide) == doctest::Approx(250.0f));
  shift_tap(*h, app::Button::F8);  // ELF: nothing of GIANT stays
  h->run_ms(20);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(9.0f));
  CHECK(unit_param(*h, kGlide) == doctest::Approx(0.0f));
  CHECK(unit_param(*h, 35) == doctest::Approx(key_before));

  const float thru_elf = unit_param(*h, kThru);
  tap(*h, app::Button::F3);  // a plain tap goes back: the encoder no longer moves THRU (nor the preset)
  h->turn(1);
  h->run_ms(20);
  CHECK(unit_param(*h, kThru) == doctest::Approx(thru_elf));
  CHECK(unit_param(*h, kFormant) == doctest::Approx(9.0f));
  CHECK(h->runtime().frame_failures() == 0u);
  CHECK(h->display().peak_frame_words() < 2048u);
}

TEST_CASE("perform: the formant strip sets Formant, stays lit, and SHIFT + F5 shows the same value") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  h->run_ms(60);  // row C is FORMANT by default
  CHECK(h->leds().level(36 + 5) == 255);  // 0: both middle pads on
  CHECK(h->leds().level(36 + 6) == 255);
  CHECK(h->leds().level(36 + 0) == 24);
  h->pad_down(36 + 8);  // +4
  h->run_ms(60);
  h->pad_up(36 + 8);
  h->run_ms(60);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(4.0f));
  CHECK(h->leds().level(36 + 8) == 255);  // it stays
  CHECK(h->leds().level(36 + 5) == 24);
  shift_tap(*h, app::Button::F5);
  h->turn(1);
  h->run_ms(20);
  CHECK(unit_param(*h, kFormant) == doctest::Approx(5.0f));  // the same value; 5 has no pad: all dim
  CHECK(h->leds().level(36 + 8) == 24);
  CHECK(h->runtime().frame_failures() == 0u);
}

// ---- the F2 SPEAKS and F3 SOUNDS pickers, and the REC capture (slice U3b) ----------------------------------------------------------------

namespace {
constexpr std::uint16_t kCarSrc = 34, kModSrc = 47;

struct Rig {
  o3sdk::platform::host::FakeCapture cap;
  std::unique_ptr<sim::Harness> h;
  Rig() {
    cap.input_tone(220.0f, -12.0f);
    h = std::make_unique<sim::Harness>(::o3sdk_app(), nullptr, nullptr, nullptr, nullptr, &cap);
    REQUIRE(h->start());
    h->record(true);
    h->run_ms(100);
  }
  std::size_t mark() const { return h->audio().size(); }
  // The rms (fraction of full scale) of the next `ms` milliseconds.
  double listen(unsigned ms) {
    const std::size_t from = mark();
    h->run_ms(ms);
    const std::vector<std::int16_t>& a = h->audio();
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = from; i < a.size(); i += 2) {
      sum += static_cast<double>(a[i]) * static_cast<double>(a[i]);
      ++n;
    }
    return n != 0 ? std::sqrt(sum / static_cast<double>(n)) / 32768.0 : 0.0;
  }
  // SOUNDS on the live input, so the SPEAKS picker's other sources are dimmed (CAPTURE FIRST).
  void sounds_live() {
    tap(*h, app::Button::F3);
    tap(*h, app::Button::Down);
    tap(*h, app::Button::Down);
    tap(*h, app::Button::Down);  // SYNTH, NOISE, VOICE, LIVE
    tap(*h, app::Button::F1);
    h->run_ms(20);
  }
};
}  // namespace

TEST_CASE("perform: F2 and F3 open a list: the cursor moves, the key again takes the row, F8 backs out") {
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::F3);  // opens: nothing changes
  h.run_ms(20);
  const float car0 = unit_param(h, kCarSrc);
  tap(h, app::Button::Down);
  tap(h, app::Button::F8);  // backs out: the cursor's row is not taken
  h.run_ms(20);
  CHECK(unit_param(h, kCarSrc) == doctest::Approx(car0));
  tap(h, app::Button::F3);
  tap(h, app::Button::Down);  // NOISE
  tap(h, app::Button::F3);    // the key again takes it
  h.run_ms(20);
  CHECK(unit_param(h, kCarSrc) == doctest::Approx(2.0f));
  tap(h, app::Button::F3);  // the encoder moves the cursor, F1 takes it
  h.turn(-1);
  tap(h, app::Button::F1);
  h.run_ms(20);
  CHECK(unit_param(h, kCarSrc) == doctest::Approx(1.0f));
  CHECK(h.display().peak_frame_words() < 2048u);
}

TEST_CASE("perform: a dimmed LIVE source pre-arms a capture; the ADC moves only when the slot has taken over") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.sounds_live();
  REQUIRE(unit_param(h, kCarSrc) == doctest::Approx(7.0f));
  const auto src0 = rig.cap.source();
  // The dimmed row the design says: another source while the other side holds LIVE.
  namespace route = o3_voc03er::route;
  route::Route r;
  r.sounds = route::kSoundsLive;
  REQUIRE(route::row_state(route::Side::Speaks, r, route::Row{route::Kind::Source, 0}).pre_arm());
  tap(h, app::Button::F2);
  tap(h, app::Button::Down);  // the first other source: dimmed
  tap(h, app::Button::F1);
  h.run_ms(500);
  CHECK(rig.cap.source() == src0);  // picking only arms
  tap(h, app::Button::Rec);
  h.run_ms(1500);  // recording, not finished
  CHECK(rig.cap.source() == src0);
  CHECK(unit_param(h, kCarSrc) == doctest::Approx(7.0f));
  h.run_ms(9000);  // two bars at the default tempo, with room
  CHECK(unit_param(h, kCarSrc) == doctest::Approx(3.0f));  // SOUNDS took SLOT 1 ...
  CHECK(rig.cap.source() != src0);                         // ... and the ADC moved to the picked source, for SPEAKS
  CHECK(unit_param(h, kModSrc) == doctest::Approx(0.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: REC captures the live source into the first empty slot and the side that held LIVE takes it; REC again while armed cancels") {
  Rig rig;
  sim::Harness& h = *rig.h;
  const auto src0 = rig.cap.source();
  tap(h, app::Button::Rec);
  h.run_ms(300);
  tap(h, app::Button::Rec);  // a second tap before the end of the bar wait or the record: stops or cancels; either leaves a consistent route
  h.run_ms(9000);
  CHECK(rig.cap.source() == src0);  // no pre-arm: the ADC never moves
  const float mod = unit_param(h, kModSrc);
  CHECK((mod == doctest::Approx(0.0f) || mod == doctest::Approx(3.0f)));
  tap(h, app::Button::Rec);
  h.run_ms(9000);
  const float mod2 = unit_param(h, kModSrc);
  CHECK(mod2 == doctest::Approx(3.0f));  // SPEAKS holds SLOT 1 (the cancelled one stayed empty, or was the first)
  CHECK(h.runtime().frame_failures() == 0u);
}

// Finding 2 of the review: the kit hands App::input the key first, so a screen button can leave PERFORM with a take in hand. The capture's tick is App::update's
// (PerformScreen::service), not PERFORM's update(): the take ends on PATTERN as it does on PERFORM (the same run as the test above, with the screen left).
TEST_CASE("perform: a REC take goes on to its end when PERFORM is left (the capture is the app's service, not the screen's update)") {
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::Rec);
  h.run_ms(300);
  tap(h, app::Button::Pattern);  // leaves PERFORM with the take armed
  h.run_ms(9000);
  CHECK(unit_param(h, kModSrc) == doctest::Approx(3.0f));  // SPEAKS holds SLOT 1: the take finished with no PERFORM under it
  CHECK(h.runtime().frame_failures() == 0u);
}

// Finding 10: a second tap on F1 MODE steps round (TRACT, the fifth mode since V1, to BANDS); the encoder stops at the ends.
TEST_CASE("perform: F1 MODE taps wrap round, the encoder clamps") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  constexpr std::uint16_t kMode = o3_voc03er::Vocoder::Mode;
  tap(*h, app::Button::F1);  // the first tap takes the focus and changes nothing
  h->run_ms(20);
  h->turn(-8);  // the encoder clamps at the first mode ...
  h->run_ms(20);
  CHECK(unit_param(*h, kMode) == doctest::Approx(0.0f));
  h->turn(8);  // ... and at the last (TRACT)
  h->run_ms(20);
  CHECK(unit_param(*h, kMode) == doctest::Approx(4.0f));
  h->turn(1);
  h->run_ms(20);
  CHECK(unit_param(*h, kMode) == doctest::Approx(4.0f));
  tap(*h, app::Button::F1);  // a tap at TRACT goes round to the first mode, where it used to stop
  h->run_ms(20);
  CHECK(unit_param(*h, kMode) == doctest::Approx(0.0f));
  tap(*h, app::Button::F1);
  h->run_ms(20);
  CHECK(unit_param(*h, kMode) == doctest::Approx(1.0f));
}

// ---- Q6: PERFORM plays the layout; F6 ROW and F7 PLAYS --------------------------------------------------------------------------------------

namespace {
constexpr double kSound = 1e-4;  // above: a note is sounding (the vocoder speaks the tone on the fake capture)

// Which of rows A, B and C (bits 0, 1, 2) are dark on column 5, a pad that is lit in every default row: the F6 flash.
unsigned dark_rows(sim::Harness& h) {
  unsigned m = 0;
  for (unsigned r = 1; r <= 3; ++r) {
    if (h.leds().level(r * app::kPadCols + 5u) == 0) {
      m |= 1u << (r - 1u);
    }
  }
  return m;
}
bool row_dark(sim::Harness& h, unsigned pad_row) {
  for (unsigned c = 0; c < app::kPadCols; ++c) {
    if (h.leds().level(pad_row * app::kPadCols + c) != 0) {
      return false;
    }
  }
  return true;
}
}  // namespace

TEST_CASE("perform: F6 steps the row and wraps on a tap, the encoder stops at the ends, and the chosen row flashes") {
  Rig rig;
  sim::Harness& h = *rig.h;
  CHECK(dark_rows(h) == 0u);  // F6 holds the focus at start, row A
  tap(h, app::Button::F6);    // A to B
  h.run_ms(40);
  CHECK(dark_rows(h) == 2u);
  h.run_ms(300);
  CHECK(dark_rows(h) == 0u);  // the flash is short
  tap(h, app::Button::F6);    // B to C
  h.run_ms(40);
  CHECK(dark_rows(h) == 4u);
  h.run_ms(300);
  tap(h, app::Button::F6);  // C wraps to A
  h.run_ms(40);
  CHECK(dark_rows(h) == 1u);
  h.run_ms(300);
  h.turn(5);  // the encoder stops at C ...
  h.run_ms(40);
  CHECK(dark_rows(h) == 4u);
  h.run_ms(300);
  h.turn(1);  // ... and does not wrap: nothing changes, nothing flashes
  h.run_ms(40);
  CHECK(dark_rows(h) == 0u);
  h.turn(-5);
  h.run_ms(40);
  CHECK(dark_rows(h) == 1u);
  h.run_ms(300);
  h.turn(-1);  // stops at A
  h.run_ms(40);
  CHECK(dark_rows(h) == 0u);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: F7 sets what the chosen row plays: OFF is dark and silent, BEND takes C, a parameter strip sets its parameter from its pads") {
  Rig rig;
  sim::Harness& h = *rig.h;
  // Defaults: A NOTE, B TRIAD, C FORMANT (the major scale is the rig's: the root 70, in-scale pads 24, the others dark).
  CHECK(h.leds().level(12 + 0) == 70);
  CHECK(h.leds().level(24 + 0) == 70);
  CHECK(h.leds().level(36 + 5) == 255);

  tap(h, app::Button::F6);  // F6 holds the focus at start: A to B
  tap(h, app::Button::F7);  // F7 takes the focus and changes nothing
  h.run_ms(300);
  CHECK(h.leds().level(24 + 0) == 70);
  h.turn(4);  // B: TRIAD to OFF
  h.run_ms(300);
  CHECK(row_dark(h, 2));
  CHECK(h.leds().level(12 + 0) == 70);  // A is as it was
  h.pad_down(24 + 5);
  CHECK(rig.listen(300) < kSound);  // silent
  h.pad_up(24 + 5);
  h.run_ms(100);

  tap(h, app::Button::F6);  // focus
  tap(h, app::Button::F6);  // B to C
  tap(h, app::Button::F7);  // focus
  h.turn(1);                // FORMANT to BEND: the strip's middle pair is dim, the rest dark
  h.run_ms(300);
  CHECK(h.leds().level(36 + 5) == 24);
  CHECK(h.leds().level(36 + 6) == 24);
  CHECK(h.leds().level(36 + 0) == 0);

  tap(h, app::Button::F6);  // focus
  tap(h, app::Button::F6);  // C wraps to A
  tap(h, app::Button::F7);  // focus
  h.turn(6);                // A: NOTE past OFF to the first parameter strip, SHAPE: twelve pads, none dark
  h.run_ms(300);
  for (unsigned pad = 12; pad < 24; ++pad) {
    CHECK(h.leds().level(pad) != 0);
  }
  h.pad_down(12 + 11);  // the top of the range: sets SHAPE, and nothing sounds
  CHECK(rig.listen(300) < kSound);
  h.pad_up(12 + 11);
  h.run_ms(100);
  CHECK(unit_param(h, kWave) == doctest::Approx(2.0f));
  CHECK(h.leds().level(12 + 11) == 255);  // the nearest pad to the value is the bright one
  h.turn(-1);  // back to OFF: A is dark again, and B (OFF) too
  h.run_ms(300);
  CHECK(row_dark(h, 1));
  CHECK(row_dark(h, 2));
  CHECK(h.runtime().frame_failures() == 0u);
}

namespace {
// The tract unit's own parameter (the unit named TRACT; `unit_param` skips it).
float tract_param(sim::Harness& h, std::uint16_t id) {
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(h, u)) {
      return h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    }
  }
  return -1.0e9f;
}
}  // namespace

TEST_CASE("perform: each mode keeps its own rows, TRACT starts on NOTE, TONGUE and MOUTH, and a switch shows the layout of the mode it lands on") {
  using o3_voc03er::TractUnit;
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::F7);  // row A is the chosen one at start; F7 takes the focus
  h.turn(6);                // BANDS, row A: the SHAPE strip
  h.run_ms(300);
  h.pad_down(12 + 11);
  h.run_ms(60);
  h.pad_up(12 + 11);
  h.run_ms(60);
  CHECK(unit_param(h, kWave) == doctest::Approx(2.0f));

  tap(h, app::Button::F1);
  h.turn(8);  // TRACT: its own layout, A NOTE, B TONGUE, C MOUTH, whatever BANDS was set to
  h.run_ms(300);
  REQUIRE(unit_param(h, o3_voc03er::Vocoder::Mode) == doctest::Approx(4.0f));
  h.pad_down(12 + 4);  // A is a note row: it sounds and writes no parameter
  h.run_ms(60);
  h.pad_up(12 + 4);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.0f));
  h.pad_down(24 + 0);
  h.run_ms(60);
  h.pad_up(24 + 0);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Tongue) == doctest::Approx(-1.0f));
  CHECK(unit_param(h, kWave) == doctest::Approx(2.0f));  // the pads of TRACT did not touch BANDS' SHAPE
  h.pad_down(36 + 4);  // C: MOUTH, the vowel I (column 4 of 8 eighths)
  h.run_ms(60);
  h.pad_up(36 + 4);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.5f));
  h.pad_down(36 + 10);  // C: MOUTH, TIP held
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Close) == doctest::Approx(2.0f));
  h.pad_up(36 + 10);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Close) == doctest::Approx(0.0f));

  h.turn(-80);  // BANDS again (F1 still has the focus): row A is the SHAPE strip as it was left, the other rows the defaults
  h.run_ms(300);
  REQUIRE(unit_param(h, o3_voc03er::Vocoder::Mode) == doctest::Approx(0.0f));
  h.pad_down(12 + 0);
  h.run_ms(60);
  h.pad_up(12 + 0);
  h.run_ms(60);
  CHECK(unit_param(h, kWave) == doctest::Approx(0.0f));
  CHECK(h.leds().level(24 + 0) == 70);  // B is the default TRIAD
  h.turn(1);  // TALK: the default layout, row A plays notes
  h.run_ms(300);
  CHECK(h.leds().level(12 + 0) == 70);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: every row value plays what it says, and row C FORMANT holds nothing") {
  Rig rig;
  sim::Harness& h = *rig.h;
  // A NOTE and B TRIAD sound.
  h.pad_down(12 + 4);
  CHECK(rig.listen(300) > kSound);
  h.pad_up(12 + 4);
  h.run_ms(900);
  CHECK(rig.listen(200) < kSound);
  h.pad_down(24 + 4);
  CHECK(rig.listen(300) > kSound);
  h.pad_up(24 + 4);
  h.run_ms(900);
  // C FORMANT: a tap sets the value and flashes it, and nothing sounds or stays held.
  h.pad_down(36 + 8);
  CHECK(rig.listen(300) < kSound);
  h.pad_up(36 + 8);
  h.run_ms(100);
  CHECK(unit_param(h, kFormant) == doctest::Approx(4.0f));
  // A as a 7TH: a chord on a pad, the stored kind is the row's.
  tap(h, app::Button::F7);
  h.turn(2);  // NOTE, TRIAD, 7TH
  h.run_ms(100);
  h.pad_down(12 + 1);
  CHECK(rig.listen(300) > kSound);
  h.pad_up(12 + 1);
  h.run_ms(900);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: a held pad's row takes F7 at once, and ROW stays where F6 put it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  h.pad_down(24 + 5);  // a pad of row B, held (the chosen row is A)
  h.run_ms(100);
  CHECK(h.leds().level(24 + 5) == 255);
  tap(h, app::Button::F7);  // B steps now, TRIAD to 7TH, and its notes end; F7 takes the focus
  h.run_ms(40);
  CHECK(h.leds().level(24 + 5) != 255);
  h.pad_up(24 + 5);
  h.run_ms(300);
  tap(h, app::Button::F6);  // takes the focus back
  h.run_ms(40);
  CHECK(dark_rows(h) == 0u);
  tap(h, app::Button::F6);  // steps from A, the row F6 showed all along (FX1: the pad's row did not take it), to B
  h.run_ms(40);
  CHECK(dark_rows(h) == 2u);
  h.run_ms(300);

  // The encoder does the same while F7 holds the focus: a pad of A is held, the encoder takes A to OFF.
  tap(h, app::Button::F7);  // focus
  h.pad_down(12 + 0);
  h.run_ms(100);
  h.turn(5);
  h.run_ms(300);
  CHECK(row_dark(h, 1));
  CHECK(h.leds().level(24 + 0) == 70);  // B is as it was
  h.pad_up(12 + 0);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: SHIFT latches a chord pad, DELETE lets it go, and a strip pad has nothing to latch") {
  Rig rig;
  sim::Harness& h = *rig.h;
  h.pad_down(24 + 2);  // row B: a triad
  h.run_ms(100);
  CHECK(h.leds().level(24 + 2) == 255);
  tap(h, app::Button::Shift);
  h.pad_up(24 + 2);
  h.run_ms(100);
  CHECK(h.leds().level(24 + 2) == 140);
  CHECK(rig.listen(300) > kSound);
  tap(h, app::Button::Delete);
  h.run_ms(100);
  CHECK(h.leds().level(24 + 2) != 140);
  // A parameter strip on A: SHIFT with a strip pad held latches nothing and sets the value as a plain press does.
  tap(h, app::Button::F7);
  h.turn(6);
  h.run_ms(100);
  h.pad_down(12 + 11);
  tap(h, app::Button::Shift);
  h.pad_up(12 + 11);
  h.run_ms(100);
  CHECK(h.leds().level(12 + 11) != 140);
  CHECK(unit_param(h, kWave) == doctest::Approx(2.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

namespace {
// The app's octave as the console's state line says it.
int state_octave(sim::Harness& h) {
  char line[400];
  const std::size_t n = h.runtime().state_line(line, sizeof line);
  const std::string text(line, n);
  const std::size_t at = text.find("octave=");
  return at == std::string::npos ? -99 : std::atoi(text.c_str() + at + 7);
}
}  // namespace

TEST_CASE("perform: SHIFT + F7 is OCTAVE, a focused value the encoder and the arrows step, and a layout's OCT and VOL pads keep working on row 0") {
  Rig rig;
  sim::Harness& h = *rig.h;
  CHECK(state_octave(h) == 0);
  tap(h, app::Button::Left);  // nothing focused: the arrows do nothing now (they were the octave)
  tap(h, app::Button::Right);
  CHECK(state_octave(h) == 0);
  shift_tap(h, app::Button::F7);  // SHIFT + F7: OCTAVE takes the focus and changes nothing
  CHECK(state_octave(h) == 0);
  h.turn(-1);
  h.run_ms(20);
  CHECK(state_octave(h) == -1);
  tap(h, app::Button::Left);
  CHECK(state_octave(h) == -2);
  tap(h, app::Button::Right);
  tap(h, app::Button::Right);
  CHECK(state_octave(h) == 0);
  for (unsigned i = 0; i < 4; ++i) {  // the range is -3..2 and stops at its ends
    tap(h, app::Button::Right);
  }
  CHECK(state_octave(h) == 2);
  for (unsigned i = 0; i < 7; ++i) {
    tap(h, app::Button::Left);
  }
  CHECK(state_octave(h) == -3);
  h.run_ms(60);
  CHECK(h.leds().level(36 + 5) == 255);
  CHECK(h.leds().level(0) == 0);   // OCT- at the lowest octave: dark
  h.pad_down(1);                   // OCT+
  h.pad_up(1);
  h.run_ms(60);
  CHECK(h.leds().level(1) == 24);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: the arrows step the focused F value as a detent does, and UP and DOWN do too") {
  Rig rig;
  sim::Harness& h = *rig.h;
  // F4 is GLIDE's key on the plain row (it holds the focus after a tap): focus F6 THRU is not there in BANDS? Use SHIFT + F4 GLIDE, 10 ms a detent.
  shift_tap(h, app::Button::F4);
  const float g0 = unit_param(h, kGlide);
  tap(h, app::Button::Right);
  CHECK(unit_param(h, kGlide) == doctest::Approx(g0 + 10.0f));
  tap(h, app::Button::Up);
  CHECK(unit_param(h, kGlide) == doctest::Approx(g0 + 20.0f));
  tap(h, app::Button::Left);
  tap(h, app::Button::Down);
  CHECK(unit_param(h, kGlide) == doctest::Approx(g0));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: an arrow does not change F1 MODE, QUALITY in TALK or the PRESET; the encoder still does") {
  Rig rig;
  sim::Harness& h = *rig.h;
  constexpr std::uint16_t kMode = o3_voc03er::Vocoder::Mode, kQuality = o3_voc03er::Vocoder::Quality;
  tap(h, app::Button::F1);  // the first tap takes the focus and changes nothing
  tap(h, app::Button::Right);
  tap(h, app::Button::Up);
  h.run_ms(20);
  CHECK(unit_param(h, kMode) == doctest::Approx(0.0f));
  h.turn(1);  // TALK
  h.run_ms(20);
  CHECK(unit_param(h, kMode) == doctest::Approx(1.0f));
  tap(h, app::Button::Left);
  tap(h, app::Button::Down);
  h.run_ms(20);
  CHECK(unit_param(h, kMode) == doctest::Approx(1.0f));
  tap(h, app::Button::F8);  // QUALITY on TALK's page
  const float q = unit_param(h, kQuality);
  tap(h, app::Button::Right);
  tap(h, app::Button::Up);
  CHECK(unit_param(h, kQuality) == doctest::Approx(q));
  h.turn(1);  // the encoder is the deliberate way
  h.run_ms(20);
  CHECK(unit_param(h, kQuality) != doctest::Approx(q));
  float before[o3_voc03er::Vocoder::kParamCount];
  shift_tap(h, app::Button::F8);  // PRESET
  for (unsigned i = 0; i < o3_voc03er::Vocoder::kParamCount; ++i) {
    before[i] = unit_param(h, static_cast<std::uint16_t>(i));
  }
  tap(h, app::Button::Right);
  tap(h, app::Button::Up);
  h.run_ms(20);
  for (unsigned i = 0; i < o3_voc03er::Vocoder::kParamCount; ++i) {
    CHECK(unit_param(h, static_cast<std::uint16_t>(i)) == doctest::Approx(before[i]));
  }
  CHECK(h.runtime().frame_failures() == 0u);
}

// ---- V2: the mouth strip, F8 VOWEL and the voiceprint in TRACT ----------------------------------------------------------------------------

namespace {
constexpr unsigned kRowC = 36;  // the pad of row C's column 0

// The mode `mode_turn` steps from BANDS under F1, then row C of that mode's layout to MOUTH (F6 takes the focus and steps A, B, C; F7 turned to the
// top of the list is NOTE, and MOUTH is the eighth value). Each mode has its own layout (TP1), so the mode comes first.
void mouth_setup(sim::Harness& h, int mode_turn) {
  tap(h, app::Button::F1);
  h.turn(mode_turn);
  h.run_ms(300);
  tap(h, app::Button::F6);  // takes the focus (F1 had it)
  tap(h, app::Button::F6);  // B
  tap(h, app::Button::F6);  // C
  tap(h, app::Button::F7);
  h.turn(-80);
  h.turn(7);
  h.run_ms(300);
}
}  // namespace

TEST_CASE("perform: in TRACT a MOUTH row writes VOWEL (sticky) and CLOSE (momentary, the last pressed wins), and its LEDs follow") {
  using o3_voc03er::TractUnit;
  Rig rig;
  sim::Harness& h = *rig.h;
  REQUIRE(tract_param(h, TractUnit::Close) > -1.0e8f);
  mouth_setup(h, 8);  // TRACT
  REQUIRE(unit_param(h, o3_voc03er::Vocoder::Mode) == doctest::Approx(4.0f));
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.0f));
  CHECK(h.leds().level(kRowC + 0) == 255);  // the nearest column to A
  CHECK(h.leds().level(kRowC + 2) == 140);  // a pure vowel
  CHECK(h.leds().level(kRowC + 1) == 24);
  CHECK(h.leds().level(kRowC + 9) == 24);   // the closures idle dim
  CHECK(h.leds().level(kRowC + 11) == 24);

  // a vowel pad sets VOWEL and it stays after the release; the LEDs follow
  h.pad_down(kRowC + 4);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.5f));
  h.pad_up(kRowC + 4);
  h.run_ms(300);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.5f));
  CHECK(h.leds().level(kRowC + 4) == 255);
  CHECK(h.leds().level(kRowC + 0) == 140);
  CHECK(h.leds().level(kRowC + 3) == 24);
  h.pad_down(kRowC + 3);  // an in-between column: 3/8
  h.run_ms(40);
  h.pad_up(kRowC + 3);
  h.run_ms(40);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.375f));

  // closures: momentary, two held, the last pressed wins, a release gives back the one still held
  CHECK(tract_param(h, TractUnit::Close) == doctest::Approx(0.0f));
  h.pad_down(kRowC + 9);  // LIPS
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Close) == doctest::Approx(1.0f));
  CHECK(h.leds().level(kRowC + 9) == 255);
  h.pad_down(kRowC + 10);  // TIP
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Close) == doctest::Approx(2.0f));
  h.pad_up(kRowC + 10);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Close) == doctest::Approx(1.0f));
  CHECK(h.leds().level(kRowC + 10) == 24);
  h.pad_up(kRowC + 9);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Close) == doctest::Approx(0.0f));
  CHECK(h.leds().level(kRowC + 9) == 24);

  // SHIFT does not latch a closure, and SHIFT + a vowel pad is a plain press
  h.pad_down(kRowC + 11);  // BACK
  h.run_ms(40);
  tap(h, app::Button::Shift);
  h.pad_up(kRowC + 11);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Close) == doctest::Approx(0.0f));
  h.press(app::Button::Shift);
  h.run_ms(2);
  h.pad_down(kRowC + 8);
  h.run_ms(40);
  h.pad_up(kRowC + 8);
  h.release(app::Button::Shift);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(1.0f));

  // the strip writes the tract unit only: the vocoder's MODE stays
  CHECK(unit_param(h, o3_voc03er::Vocoder::Mode) == doctest::Approx(4.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: outside TRACT a MOUTH row is dark and writes nothing") {
  using o3_voc03er::TractUnit;
  Rig rig;
  sim::Harness& h = *rig.h;
  mouth_setup(h, 0);  // BANDS
  REQUIRE(unit_param(h, o3_voc03er::Vocoder::Mode) == doctest::Approx(0.0f));
  CHECK(row_dark(h, 3));
  const float vowel0 = tract_param(h, TractUnit::Vowel);
  h.pad_down(kRowC + 4);
  h.run_ms(60);
  h.pad_up(kRowC + 4);
  h.pad_down(kRowC + 9);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Close) == doctest::Approx(0.0f));
  h.pad_up(kRowC + 9);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(vowel0));
  CHECK(row_dark(h, 3));
  // a closure held while the mode leaves TRACT still ends with its pad (OPEN)
  tap(h, app::Button::F1);
  h.turn(8);
  h.run_ms(100);
  h.turn(-80);  // TRACT has its own layout (C is TIP): the focus is still on PLAYS of row C, so the encoder walks C to MOUTH
  h.turn(7);
  h.run_ms(60);
  h.pad_down(kRowC + 10);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Close) == doctest::Approx(2.0f));
  tap(h, app::Button::F1);
  h.turn(-8);
  h.run_ms(60);
  h.pad_up(kRowC + 10);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Close) == doctest::Approx(0.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: F8 is VOWEL in TRACT, 1 percent a detent, and the strip follows; HELP names it") {
  using o3_voc03er::TractUnit;
  Rig rig;
  sim::Harness& h = *rig.h;
  mouth_setup(h, 8);
  tap(h, app::Button::F8);  // the first tap takes the focus
  h.run_ms(40);
  h.turn(7);
  h.run_ms(300);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.07f).epsilon(0.02));
  CHECK(h.leds().level(kRowC + 1) == 255);  // 7 % is nearest the column 1 (12.5 %)
  h.turn(-3);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.04f).epsilon(0.02));
  h.turn(-30);  // the encoder stops at A
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.0f));
  h.turn(130);  // and at U
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(1.0f));

  open_help(h);
  const app::Help& help = h.runtime().help_overlay().help();
  app::HelpProblem p;
  const bool ok = app::help_check(help, &p);
  CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
  REQUIRE(help.fkey[7] != nullptr);
  CHECK(std::string(help.fkey[7]).find("MOUTH") != std::string::npos);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: SHIFT + F8 in TRACT picks one of TRACT's eight sounds, writes all twelve tract values and leaves the vocoder's alone") {
  using o3_voc03er::TractUnit;
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::F1);
  h.turn(8);  // clamps at TRACT
  h.run_ms(300);
  const float vocoder_glide = unit_param(h, kGlide);
  shift_tap(h, app::Button::F8);
  h.turn(1);  // from none the first turn picks AH
  h.run_ms(40);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.0f));
  CHECK(tract_param(h, TractUnit::Jaw) == doctest::Approx(0.3f));
  CHECK(tract_param(h, TractUnit::Glide) == doctest::Approx(30.0f));
  h.turn(3);  // OO, EE, then WOW: vowel O, a slow glide, a large tube, a jaw that accents
  h.run_ms(40);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.75f));
  CHECK(tract_param(h, TractUnit::Glide) == doctest::Approx(200.0f));
  CHECK(tract_param(h, TractUnit::Size) == doctest::Approx(1.0f));
  CHECK(tract_param(h, TractUnit::Accent) == doctest::Approx(0.3f));
  h.turn(1);  // BREATH: nothing of WOW stays
  h.run_ms(40);
  CHECK(tract_param(h, TractUnit::Accent) == doctest::Approx(0.0f));
  CHECK(tract_param(h, TractUnit::Size) == doctest::Approx(2.0f));
  CHECK(tract_param(h, TractUnit::Hiss) == doctest::Approx(0.9f));
  CHECK(tract_param(h, TractUnit::Mix) == doctest::Approx(0.7f));
  CHECK(unit_param(h, kGlide) == doctest::Approx(vocoder_glide));  // the vocoder's GLIDE is not TRACT's
  CHECK(unit_param(h, kFormant) == doctest::Approx(0.0f));
  h.turn(-1);  // back one: WOW
  h.run_ms(40);
  CHECK(tract_param(h, TractUnit::Glide) == doctest::Approx(200.0f));
  h.turn(-3);
  h.turn(-1);  // AH, then the table wraps to the last: BIG
  h.run_ms(40);
  CHECK(tract_param(h, TractUnit::Size) == doctest::Approx(0.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: the display list of PERFORM in TRACT (the mouth's profile, a held closure) fits next to the busiest mode") {
  std::size_t busiest = 0;
  for (int turn : {0, 1, 2, 3}) {  // BANDS, TALK, ROBOT, SING
    auto h = std::make_unique<sim::Harness>(::o3sdk_app());
    REQUIRE(h->start());
    h->run_ms(100);
    tap(*h, app::Button::F1);
    h->turn(-8);
    h->turn(turn);
    h->run_ms(400);
    busiest = std::max(busiest, h->display().peak_frame_words());
  }
  Rig rig;
  sim::Harness& h = *rig.h;
  mouth_setup(h, 8);
  h.pad_down(kRowC + 9);  // a closure held: the profile pinches
  h.run_ms(400);
  tap(h, app::Button::F8);
  h.turn(40);
  h.run_ms(400);
  const std::size_t tract = h.display().peak_frame_words();
  CHECK_MESSAGE(tract < 2048u, "TRACT " << tract << " words, busiest other mode " << busiest);
  CHECK(h.runtime().frame_failures() == 0u);
}

// FX1 (the maintainer's unit report): with ROW on C, F7 held or tapped and the encoder turned, the selected row went back to A. The chosen row stays C
// through F7's press, the encoder, F7's release and a second turn; the next F6 steps from C (a wrap to A), not from A.
TEST_CASE("perform: row C stays chosen through F7 held, the encoder and F7's release") {
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::F6);  // focus (F6 holds it at start): A to B
  tap(h, app::Button::F6);  // B to C
  h.run_ms(300);
  h.press(app::Button::F7);  // held: F7 takes the focus
  h.run_ms(40);
  h.turn(1);  // C: FORMANT to BEND
  h.run_ms(300);
  CHECK(h.leds().level(36 + 5) == 24);  // row C changed, not A
  CHECK(h.leds().level(12 + 0) == 70);  // A is as it was
  h.release(app::Button::F7);
  h.run_ms(300);
  CHECK(h.leds().level(36 + 5) == 24);
  CHECK(h.leds().level(12 + 0) == 70);
  tap(h, app::Button::F6);  // focus
  tap(h, app::Button::F6);  // C wraps to A: it was C
  h.run_ms(40);
  CHECK(dark_rows(h) == 1u);

  // The same with a tap of F7 and the encoder after it.
  h.run_ms(300);
  tap(h, app::Button::F6);
  tap(h, app::Button::F6);  // A to B
  tap(h, app::Button::F6);  // B to C
  h.run_ms(300);
  tap(h, app::Button::F7);
  h.run_ms(300);
  CHECK(h.leds().level(12 + 0) == 70);
  h.turn(-1);
  h.run_ms(300);
  CHECK(h.leds().level(12 + 0) == 70);
  CHECK(h.runtime().frame_failures() == 0u);
}

// FX1 and a latched pad: a pad that SHIFT latched is not under a finger, so F7 does not follow it. With ROW on C, F7 and the encoder step row C's value, row A
// keeps its meaning and ROW is still C after F7's release.
// SP1: PARAMS forwards its pads to PERFORM's surface, and leaving PARAMS does not call PERFORM::leave: held_ keeps a bit with no down_ bit behind it.
TEST_CASE("perform: a stale held bit (a pad held on PARAMS, PERFORM entered) does not take F7; ROW stays where F6 shows it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::Params);
  h.run_ms(100);
  h.pad_down(24 + 3);  // PARAMS hands it to PERFORM: a row B note, held (and down)
  h.run_ms(60);
  tap(h, app::Button::Performance);  // enter() forgets down_; the held bit stays
  h.run_ms(100);
  CHECK(h.leds().level(24 + 0) == 70);  // row B: TRIAD
  tap(h, app::Button::F7);  // focus only: no pad is down here
  h.run_ms(40);
  h.turn(5);  // the chosen row (A) goes to OFF, not row B's TRIAD
  h.run_ms(300);
  CHECK(row_dark(h, 1));
  CHECK(h.leds().level(24 + 0) == 70);  // B is as it was
  CHECK_FALSE(row_dark(h, 2));
  h.pad_up(24 + 3);
  h.run_ms(100);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("perform: a latched row A pad does not take F7, ROW stays on C") {
  Rig rig;
  sim::Harness& h = *rig.h;
  tap(h, app::Button::F6);  // F6 holds the focus at start: A to B
  tap(h, app::Button::F6);  // B to C
  h.run_ms(300);
  h.pad_down(12 + 3);  // a row A note, latched, then let go
  h.run_ms(60);
  tap(h, app::Button::Shift);
  h.pad_up(12 + 3);
  h.run_ms(300);
  CHECK(h.leds().level(12 + 3) == 140);  // latched: Mid
  unsigned a_before[12];
  for (unsigned c = 0; c < 12; ++c) {
    a_before[c] = h.leds().level(12 + c);
  }
  CHECK(h.leds().level(36 + 5) == 255);  // row C is FORMANT: the pad of the current value is on
  h.press(app::Button::F7);
  h.run_ms(40);
  h.turn(1);  // row C: FORMANT to BEND, not row A's NOTE to TRIAD
  h.run_ms(300);
  CHECK(h.leds().level(36 + 5) == 24);
  // Row A keeps its NOTE meaning (every pad but the latched one, which a change of layout lets go, as it always did).
  bool a_same = true;
  for (unsigned c = 0; c < 12; ++c) {
    a_same = a_same && (c == 3u || h.leds().level(12 + c) == a_before[c]);
  }
  CHECK(a_same);
  h.release(app::Button::F7);
  h.run_ms(300);
  CHECK(h.leds().level(36 + 5) == 24);
  tap(h, app::Button::F6);
  tap(h, app::Button::F6);  // C wraps to A: ROW was still C
  h.run_ms(40);
  CHECK(dark_rows(h) == 1u);
  CHECK(h.runtime().frame_failures() == 0u);
}

// FX1 sweep: in every mode, with ROW on C, the encoder through the whole of F7's list (held and released) never moves row A or B and ROW stays C.
TEST_CASE("perform: FX1 sweep, row C stays chosen in every mode") {
  Rig rig;
  sim::Harness& h = *rig.h;
  for (unsigned m = 0; m < 9u; ++m) {
    if (m != 0u) {
      tap(h, app::Button::F1);
      tap(h, app::Button::F1);  // focus, then a tap steps the mode on
    }
    h.run_ms(300);
    tap(h, app::Button::F6);
    h.run_ms(40);
    // ROW to C by the encoder
    h.turn(5);
    h.run_ms(300);
    unsigned a_before[12];
    for (unsigned c = 0; c < 12; ++c) {
      a_before[c] = h.leds().level(12 + c);
    }
    h.press(app::Button::F7);
    h.run_ms(40);
    for (int i = 0; i < 40; ++i) {
      h.turn(1);
      h.run_ms(20);
    }
    h.release(app::Button::F7);
    h.run_ms(400);
    bool a_same = true;
    for (unsigned c = 0; c < 12; ++c) {
      a_same = a_same && h.leds().level(12 + c) == a_before[c];
    }
    CHECK_MESSAGE(a_same, "mode " << m << ": row A changed");
    tap(h, app::Button::F6);
    tap(h, app::Button::F6);  // C wraps to A
    h.run_ms(40);
    // Row C's own column 5 may be dark after the sweep (its function changed): only A (the flash) and B are judged.
    CHECK_MESSAGE((dark_rows(h) & 3u) == 1u, "mode " << m << ": dark " << dark_rows(h));
    h.run_ms(400);
  }
}
