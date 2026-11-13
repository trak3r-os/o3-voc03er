// SPDX-License-Identifier: GPL-3.0-or-later
// UX2, the liveness table: (parameter, mode) -> heard, the one table PERFORM's SHIFT row, the strips and PARAMS read. The table itself and its line, the
// three readers agreeing, FREEZE refused outside TALK and cleared when TALK is left, the FORMANT pads dark and refused where FORMANT is not heard, the
// PATTERN lane names, masks and LEVEL range, and MASTER opening with nothing focused. What the screens print is read off the scenario's PNGs
// (app/scenarios/liveness.txt); here the effects on the engine are.
#include <doctest/doctest.h>

#include "tract_skip.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/sim/harness.h>

#include <o3_voc03er/units/vocoder/vocoder.h>

#include "../app/src/liveness.h"
#include "../app/src/params_logic.h"
#include "../app/src/pattern_logic.h"
#include "../app/src/strip_logic.h"

#include <cmath>
#include <cstring>
#include <memory>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace lv = o3_voc03er::live;
namespace params = o3_voc03er::params;
namespace st = o3_voc03er::strip;
namespace pt = o3_voc03er::pattern;
using o3_voc03er::Vocoder;
using o3_voc03er::kModeCount;

namespace {
constexpr unsigned kBands = 0, kTalk = 1, kTune = 2, kRobot = 3, kTract = 4;

void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(5);
}

void shift_tap(sim::Harness& h, app::Button b) {
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, b);
  h.release(app::Button::Shift);
  h.run_ms(20);
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

// PERFORM's F1 MODE: a tap takes the focus (or steps round when it has it), the encoder clamps at BANDS and then goes to `mode`.
void set_mode(sim::Harness& h, unsigned mode) {
  tap(h, app::Button::Performance);
  h.run_ms(60);
  tap(h, app::Button::F1);
  h.run_ms(20);
  h.turn(-8);
  h.run_ms(20);
  h.turn(static_cast<int>(mode));
  h.run_ms(300);  // the LEDs are repainted a pass later
  REQUIRE(static_cast<unsigned>(unit_param(h, Vocoder::Mode) + 0.5f) == mode);
}
}  // namespace

TEST_CASE("liveness: the table says what the vocoder does in each mode") {
  // FREEZE and QUALITY are TALK's; FORMANT and THRU act on BANDS, TALK and ROBOT
  for (unsigned m = 0; m < kModeCount; ++m) {
    CHECK(lv::voice_heard(Vocoder::Freeze, m) == (m == kTalk));
    CHECK(lv::voice_heard(Vocoder::Quality, m) == (m == kTalk));
    CHECK(lv::voice_heard(Vocoder::Formant, m) == (m == kBands || m == kTalk || m == kRobot));
    CHECK(lv::voice_heard(Vocoder::Thru, m) == (m == kBands || m == kTalk || m == kRobot));
    // the carrier page, GLIDE, GATE, ARP: everything but TUNE (it has no carrier)
    for (const std::uint16_t p : {Vocoder::Wave, Vocoder::Detune, Vocoder::Sub, Vocoder::Tilt, Vocoder::Noise, Vocoder::Attack, Vocoder::Release,
                                  Vocoder::Glide, Vocoder::Gate, Vocoder::Arp}) {
      CHECK(lv::voice_heard(p, m) == (m != kTune));
    }
    // SPEED, AMOUNT and NOTES IN are TUNE's (SPEED also with the VOICE carrier, below)
    CHECK(lv::voice_heard(Vocoder::Speed, m) == (m == kTune));
    CHECK(lv::voice_heard(Vocoder::Amount, m) == (m == kTune));
    CHECK(lv::voice_heard(Vocoder::NotesIn, m) == (m == kTune));
    // DRY, LEVEL, KEY, SCALE and MODE itself: every mode
    CHECK(lv::voice_heard(Vocoder::Dry, m));
    CHECK(lv::voice_heard(Vocoder::Level, m));
    CHECK(lv::voice_heard(Vocoder::Key, m));
    CHECK(lv::voice_heard(Vocoder::Mode, m));
  }
  CHECK_FALSE(lv::heard(lv::kAllModes, kModeCount));  // a mode out of range is heard nowhere
  CHECK(lv::heard(lv::kTractOnly, kTract));
  CHECK_FALSE(lv::heard(lv::kTractOnly, kTalk));
}

TEST_CASE("liveness: SPEED is heard in every mode with the VOICE carrier, AMOUNT and NOTES IN stay TUNE's") {
  static_assert(lv::kCarrierVoice == Vocoder::kCarVoice, "the table reads the unit's VOICE source");
  for (unsigned m = 0; m < kModeCount; ++m) {
    CHECK(lv::voice_heard(Vocoder::Speed, m, Vocoder::kCarVoice));                   // voice_step smooths the sung note with SPEED's alpha
    CHECK(lv::voice_heard(Vocoder::Speed, m, 1) == (m == kTune));                      // SYNTH
    CHECK(lv::voice_heard(Vocoder::Speed, m, 7) == (m == kTune));                      // LIVE: a step's carrier lane tops out at 7
    CHECK(lv::voice_heard(Vocoder::Amount, m, Vocoder::kCarVoice) == (m == kTune));  // voice_step ignores both
    CHECK(lv::voice_heard(Vocoder::NotesIn, m, Vocoder::kCarVoice) == (m == kTune));
    CHECK(lv::voice_heard(Vocoder::Wave, m, Vocoder::kCarVoice) == (m != kTune));    // no other row reads the source
  }
  char t[40];
  lv::voice_heard_text(Vocoder::Speed, 1, t);
  CHECK(std::strcmp(t, "HEARD IN TUNE, OR WITH CARRIER VOICE") == 0);
  lv::voice_heard_text(Vocoder::Speed, Vocoder::kCarVoice, t);
  CHECK(std::strcmp(t, "HEARD IN EVERY MODE") == 0);
  lv::voice_heard_text(Vocoder::Amount, 1, t);
  CHECK(std::strcmp(t, "HEARD IN TUNE ONLY") == 0);
  // the PARAMS key follows the table with the source
  for (unsigned p = 0; p < params::kPages; ++p) {
    for (unsigned k = 0; k < params::kKeys; ++k) {
      const params::KeyDef& e = params::key_def(p, k);
      if (e.kind == params::Kind::Value && e.param == Vocoder::Speed) {
        CHECK(params::key_live(e, kBands, Vocoder::kCarVoice));
        CHECK_FALSE(params::key_live(e, kBands, 1));
        CHECK(params::key_live(e, kTune, 1));
      }
    }
  }
}

TEST_CASE("liveness: the line that says where a value is heard") {
  char t[40];
  lv::heard_text(lv::kTalkOnly, t);
  CHECK(std::strcmp(t, "HEARD IN TALK ONLY") == 0);
  lv::heard_text(lv::kTractOnly, t);
  CHECK(std::strcmp(t, "HEARD IN TRACT ONLY") == 0);
  lv::heard_text(lv::kNotTune, t);
  CHECK(std::strcmp(t, "NOT HEARD IN TUNE") == 0);
  lv::heard_text(lv::kBandsTalkRobot, t);
  CHECK(std::strcmp(t, "HEARD IN BANDS, TALK, ROBOT") == 0);
  lv::heard_text(lv::kAllModes, t);
  CHECK(std::strcmp(t, "HEARD IN EVERY MODE") == 0);
  lv::heard_text(0, t);
  CHECK(std::strcmp(t, "NOT HEARD ANYWHERE") == 0);
  // every mask there is fits and ends
  for (unsigned mask = 0; mask < 256; ++mask) {
    char u[40];
    std::memset(u, 'x', sizeof u);
    lv::heard_text(static_cast<std::uint8_t>(mask), u);
    CHECK(std::strlen(u) < sizeof u);
  }
}

TEST_CASE("liveness: the strips and PARAMS read the same table") {
  // a strip's modes are the table's modes for its parameter (the voice strips); PARAMS' keys the same
  unsigned strips = 0;
  for (const st::StripDef& d : st::kStrips) {
    if (d.key.kind == params::Kind::Value) {
      CHECK(d.modes == lv::voice_modes(d.key.param));
      ++strips;
    }
  }
  CHECK(strips > 10u);
  for (unsigned p = 0; p < params::kPages; ++p) {
    for (unsigned k = 0; k < params::kKeys; ++k) {
      const params::KeyDef& e = params::key_def(p, k);
      for (unsigned m = 0; m < kModeCount; ++m) {
        if (e.kind == params::Kind::Value) {
          CHECK(params::key_live(e, m) == lv::voice_heard(e.param, m));
        } else if (e.kind == params::Kind::Tract) {
          CHECK(params::key_live(e, m) == (m == kTract));
        }
      }
    }
  }
  // the keys UX2 added are on PARAMS, and learnable
  const auto find = [](std::uint16_t param) {
    for (unsigned p = 0; p < params::kPages; ++p) {
      for (unsigned k = 0; k < params::kKeys; ++k) {
        const params::KeyDef& e = params::key_def(p, k);
        if (e.kind == params::Kind::Value && e.param == param) {
          return params::learnable(p, k);
        }
      }
    }
    return false;
  };
  CHECK(find(Vocoder::Formant));
  CHECK(find(Vocoder::Thru));
  CHECK(find(Vocoder::Glide));
  CHECK(find(Vocoder::Gate));
  CHECK(find(Vocoder::Arp));
}

TEST_CASE("liveness: FREEZE is refused outside TALK, works in TALK, and a mode change clears it") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  REQUIRE(unit_param(h, Vocoder::Mode) == doctest::Approx(0.0f));  // BANDS
  shift_tap(h, app::Button::F1);  // FREEZE
  h.run_ms(40);
  CHECK(unit_param(h, Vocoder::Freeze) == doctest::Approx(0.0f));  // refused in BANDS

  set_mode(h, kTalk);
  shift_tap(h, app::Button::F1);
  h.run_ms(40);
  CHECK(unit_param(h, Vocoder::Freeze) == doctest::Approx(1.0f));  // TALK hears it

  set_mode(h, kTune);  // leaving TALK clears the latch
  h.run_ms(100);
  CHECK(unit_param(h, Vocoder::Freeze) == doctest::Approx(0.0f));
  shift_tap(h, app::Button::F1);
  h.run_ms(40);
  CHECK(unit_param(h, Vocoder::Freeze) == doctest::Approx(0.0f));  // and TUNE refuses it

  set_mode(h, kTalk);  // TALK is entered unfrozen
  h.run_ms(100);
  CHECK(unit_param(h, Vocoder::Freeze) == doctest::Approx(0.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

namespace {
// F7's list for F6's row: open it, one entry on, take it.
void plays_next(sim::Harness& h) {
  tap(h, app::Button::F7);
  h.run_ms(20);
  h.turn(1);
  h.run_ms(20);
  tap(h, app::Button::F7);
  h.run_ms(60);
}

void press_pad(sim::Harness& h, unsigned pad) {
  h.pad_down(pad);
  h.run_ms(60);
  h.pad_up(pad);
  h.run_ms(60);
}
}  // namespace

TEST_CASE("liveness: the FORMANT pads of row C are lit and set where FORMANT is heard") {
  for (const unsigned mode : {kBands, kTalk, kRobot}) {
    sim::Harness h(::o3sdk_app());
    REQUIRE(h.start());
    h.run_ms(100);
    if (mode != kBands) {
      set_mode(h, mode);
    }
    CHECK(h.leds().level(36 + 5) == 255);  // 0: both middle pads on
    press_pad(h, 36 + 8);                  // +4
    CHECK(unit_param(h, Vocoder::Formant) == doctest::Approx(4.0f));
    CHECK(h.leds().level(36 + 8) == 255);
    CHECK(h.leds().level(36 + 0) == 24);
  }
}

// F7's list offers FORMANT only where it is heard (strip::plays_live): in TUNE and TRACT, row C round its whole list never writes FORMANT from a pad.
TEST_CASE("liveness: where FORMANT is not heard, F7's list does not offer it and no row C value writes it") {
  for (const unsigned mode : {kTune, kTract}) {
    CHECK_FALSE(st::plays_live(o3_voc03er::pads::Row::C, o3_voc03er::pads::Plays::Formant, mode));
    sim::Harness h(::o3sdk_app());
    REQUIRE(h.start());
    h.run_ms(100);
    set_mode(h, mode);
    tap(h, app::Button::F6);  // A to B
    tap(h, app::Button::F6);  // B to C
    const unsigned n = st::entry_count(o3_voc03er::pads::Row::C, mode);
    for (unsigned i = 0; i < n; ++i) {
      plays_next(h);
      press_pad(h, 36 + 8);
      CHECK_MESSAGE(unit_param(h, Vocoder::Formant) == doctest::Approx(0.0f), "mode " << mode << " entry " << i);
    }
    CHECK(h.runtime().frame_failures() == 0u);
  }
}

TEST_CASE("liveness: PATTERN names, masks and the LEVEL range") {
  // the LEVEL lane is the tract's ACCENT in TRACT
  CHECK(std::strcmp(pt::lane_name(pt::Lane::Level, false), "LEVEL") == 0);
  CHECK(std::strcmp(pt::lane_name(pt::Lane::Level, true), "ACCENT") == 0);
  CHECK(std::strcmp(pt::lane_name(pt::Lane::Note, true), "NOTE") == 0);
  // what each lane is heard in
  CHECK(pt::lane_modes(pt::Lane::Note) == lv::kAllModes);
  CHECK(pt::lane_modes(pt::Lane::Slice) == lv::kAllModes);
  CHECK(pt::lane_modes(pt::Lane::Carrier) == lv::kNotTune);
  CHECK(pt::lane_modes(pt::Lane::Level) == lv::kNotTune);
  CHECK(pt::lane_modes(pt::Lane::Mouth) == lv::kTractOnly);
  CHECK(std::strcmp(pt::kSongPlays, "SONG PLAYS THE CHAIN") == 0);

  // the palette's top pad and VALUE's end are the same number, in each range
  for (const bool tract : {false, true}) {
    const unsigned top = pt::level_top(tract);
    CHECK(top == (tract ? 127u : 100u));
    CHECK(pt::level_of_pad(pt::kPalettePads - 1u, top) == top);
    unsigned prev = 0;
    for (unsigned pad = 0; pad < pt::kPalettePads; ++pad) {
      const unsigned v = pt::level_of_pad(pad, top);
      CHECK(v > prev);  // rising, and never zero
      CHECK(v <= top);
      prev = v;
    }
    o3_voc03er::PatternData p{};
    REQUIRE(o3_voc03er::set_level(p, 0, 1));
    CHECK(pt::step_value(p, 0, pt::Lane::Level, 500, 0, top));  // VALUE runs to the top and stops
    CHECK(o3_voc03er::step_velocity(o3_voc03er::get_step(p, 0)) == top);
    CHECK_FALSE(pt::step_value(p, 0, pt::Lane::Level, 1, 0, top));
    // the palette lights the pad whose value the step has
    CHECK(pt::palette_matches(pt::Lane::Level, pt::kPalettePads - 1u, o3_voc03er::get_step(p, 0), 0, o3_voc03er::pads::Layout{}, 0,
                              pt::MouthPage::Vowels, top));
  }
  // a stored 127 (written in TRACT) outside TRACT comes down to the 100 a detent away
  o3_voc03er::PatternData q{};
  REQUIRE(o3_voc03er::set_level(q, 3, 127));
  CHECK(pt::step_value(q, 3, pt::Lane::Level, -1, 0, pt::kLevelTop));
  CHECK(o3_voc03er::step_velocity(o3_voc03er::get_step(q, 3)) == 100u);
}

namespace {
float param_of(sim::Harness& h, std::uint16_t id) { return unit_param(h, id); }
}  // namespace

TEST_CASE("liveness: MASTER opens with nothing focused, a tap points the encoder, leaving forgets it") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  tap(h, app::Button::Master);
  h.run_ms(60);
  const float dry0 = param_of(h, Vocoder::Dry);
  const float lvl0 = param_of(h, Vocoder::Level);
  h.turn(5);  // nothing is focused: the detents move nothing
  h.run_ms(60);
  CHECK(param_of(h, Vocoder::Dry) == doctest::Approx(dry0));
  CHECK(param_of(h, Vocoder::Level) == doctest::Approx(lvl0));

  tap(h, app::Button::F4);  // DRY
  h.turn(5);
  h.run_ms(60);
  CHECK(param_of(h, Vocoder::Dry) == doctest::Approx(dry0 + 0.10f).epsilon(0.02));

  tap(h, app::Button::Performance);  // away and back: nothing focused again
  h.run_ms(60);
  tap(h, app::Button::Master);
  h.run_ms(60);
  const float dry1 = param_of(h, Vocoder::Dry);
  h.turn(5);
  h.run_ms(60);
  CHECK(param_of(h, Vocoder::Dry) == doctest::Approx(dry1));
  CHECK(h.runtime().frame_failures() == 0u);
}
