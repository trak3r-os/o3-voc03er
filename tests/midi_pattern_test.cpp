// SPDX-License-Identifier: GPL-3.0-or-later
// M3, patterns by MIDI: the pure rules (program to pattern, above 7, the chain, the quantised step of a recorded note), the unit's notes-in
// ring, the hook's position word, and a program change through the app's own message path in the harness. Synthetic only; no card.
#include <doctest/doctest.h>

#include "../app/src/midi_pattern_logic.h"

#include <o3_voc03er/stepseq.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/engine.h>

#include <cstdint>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

namespace {

StepSeq::Position at(std::uint16_t edge, unsigned step, unsigned pattern = 0, bool playing = true) {
  StepSeq::Position p;
  p.playing = playing;
  p.edge = edge;
  p.step = static_cast<std::uint8_t>(step);
  p.pattern = static_cast<std::uint8_t>(pattern);
  return p;
}

}  // namespace

TEST_CASE("midi pattern: program 0..7 is pattern A..H and above 7 is nothing") {
  for (unsigned p = 0; p < 8; ++p) {
    CHECK(midipat::pattern_of_program(p) == static_cast<int>(p));
  }
  CHECK(midipat::pattern_of_program(8) == -1);
  CHECK(midipat::pattern_of_program(127) == -1);
}

TEST_CASE("midi pattern: a SONG chain that plays owns the pattern, a program above 7 is ignored") {
  CHECK(midipat::program_action(3, false) == midipat::Program::Request);
  CHECK(midipat::program_action(3, true) == midipat::Program::Chain);
  CHECK(midipat::program_action(8, false) == midipat::Program::Range);
  CHECK(midipat::program_action(8, true) == midipat::Program::Range);
}

TEST_CASE("midi pattern: the user's channel, 0 is omni") {
  CHECK(midipat::channel_ok(0, 5));
  CHECK(midipat::channel_ok(5, 5));
  CHECK_FALSE(midipat::channel_ok(5, 6));
}

TEST_CASE("midi pattern: the quantised step of a note while the transport plays") {
  // the note's nearest edge is the hook's own: the step that just played
  midipat::Target t = midipat::target_of(true, 100, at(100, 4), 16);
  CHECK_FALSE(t.at_cursor);
  CHECK(t.step == 4);
  // a note in the second half of a step rounds to the next edge: the next step
  t = midipat::target_of(true, 101, at(100, 4, 2), 16);
  CHECK(t.step == 5);
  CHECK(t.pattern == 2);
  // the hook ran on by two steps before the main loop took the note
  t = midipat::target_of(true, 100, at(102, 6), 16);
  CHECK(t.step == 4);
  // wraps at LENGTH, forward and back
  t = midipat::target_of(true, 101, at(100, 15), 16);
  CHECK(t.step == 0);
  t = midipat::target_of(true, 99, at(100, 0), 16);
  CHECK(t.step == 15);
  t = midipat::target_of(true, 101, at(100, 5), 6);
  CHECK(t.step == 0);
  // the 16-bit edge wraps: 0xFFFF then 0
  t = midipat::target_of(true, 0, at(0xFFFF, 7), 16);
  CHECK(t.step == 8);
  t = midipat::target_of(true, 0xFFFF, at(0, 8), 16);
  CHECK(t.step == 7);
}

TEST_CASE("midi pattern: stopped, or no step published yet, is the pads' step entry at the cursor") {
  CHECK(midipat::target_of(false, 10, at(10, 3), 16).at_cursor);
  CHECK(midipat::target_of(true, 10, at(10, 3, 0, false), 16).at_cursor);
  CHECK(midipat::target_of(true, 10, at(10, 3), 0).at_cursor);
}

TEST_CASE("midi pattern: the velocity is the level, 1..127") {
  CHECK(midipat::level_of_velocity(0) == 1);
  CHECK(midipat::level_of_velocity(64) == 64);
  CHECK(midipat::level_of_velocity(127) == 127);
  CHECK(midipat::level_of_velocity(200) == 127);
}

TEST_CASE("midi pattern: nearest sixteenth and the ring word") {
  CHECK(Vocoder::nearest_sixteenth(0) == 0);
  CHECK(Vocoder::nearest_sixteenth(119) == 0);
  CHECK(Vocoder::nearest_sixteenth(120) == 1);
  CHECK(Vocoder::nearest_sixteenth(239) == 1);
  CHECK(Vocoder::nearest_sixteenth(240 * 3 + 100) == 3);
  Vocoder::NoteIn n;
  n.note = 127;
  n.velocity = 99;
  n.playing = true;
  n.sixteenth = 0xABCD;
  const Vocoder::NoteIn u = Vocoder::note_in_unpack(Vocoder::note_in_pack(n));
  CHECK(u.note == 127);
  CHECK(u.velocity == 99);
  CHECK(u.playing);
  CHECK(u.sixteenth == 0xABCD);
}

TEST_CASE("midi pattern: the unit hands the carrier's MIDI notes to the main loop, stamped on the grid") {
  dsp::Engine e;
  Vocoder v;
  const dsp::UnitId id = e.attach(v);
  REQUIRE(e.set_source(0, id));
  e.start();
  std::vector<float> l(64), r(64);
  auto block = [&] { e.render(nullptr, nullptr, l.data(), r.data(), 64); };
  Vocoder::NoteIn n;
  block();
  CHECK_FALSE(v.take_note_in(n));
  // stopped: a note is stamped as not playing; the app's own marked note is not recorded
  REQUIRE(e.inject(id, dsp::note_on(60, 90, 0, 0)));
  REQUIRE(e.inject(id, dsp::note_on(61, 90, Vocoder::kNoteLocal, 0)));
  block();
  REQUIRE(v.take_note_in(n));
  CHECK(n.note == 60);
  CHECK(n.velocity == 90);
  CHECK_FALSE(n.playing);
  CHECK_FALSE(v.take_note_in(n));
  // playing at 120 BPM: a sixteenth is 5,512.5 samples; 90 blocks of 64 is 5,760 samples, past the first edge
  REQUIRE(e.play());
  for (unsigned i = 0; i < 90; ++i) {
    block();
  }
  REQUIRE(e.inject(id, dsp::note_on(62, 100, 0, 0)));
  block();
  REQUIRE(v.take_note_in(n));
  CHECK(n.playing);
  CHECK(n.note == 62);
  CHECK((n.sixteenth == 1 || n.sixteenth == 2));
  // a reader that lags loses the oldest, in order
  for (unsigned i = 0; i < Vocoder::kNoteInDepth + 3u; ++i) {
    REQUIRE(e.inject(id, dsp::note_on(static_cast<std::uint8_t>(40 + i), 100, 0, 0)));
    block();
  }
  unsigned got = 0;
  std::uint8_t first = 0;
  while (v.take_note_in(n)) {
    first = got == 0u ? n.note : first;
    ++got;
  }
  CHECK(got == Vocoder::kNoteInDepth);
  CHECK(first == 43);
}

TEST_CASE("midi pattern: the hook publishes its edge, step and pattern in one word") {
  CHECK_FALSE(StepSeq{}.position().playing);
  Bank bank = {};
  set_length(bank.pattern[3], 5);
  StepSeq seq;
  dsp::Engine e;
  Vocoder v;
  const dsp::UnitId id = e.attach(v);
  seq.init(id, &bank);
  REQUIRE(e.set_source(0, id));
  REQUIRE(e.add_hook(seq));
  e.start();
  seq.request_pattern(3);
  std::vector<float> l(64), r(64);
  REQUIRE(e.play());
  for (unsigned i = 0; i < 200; ++i) {  // 12,800 samples: edges 0, 1 and 2 passed
    e.render(nullptr, nullptr, l.data(), r.data(), 64);
  }
  const StepSeq::Position p = seq.position();
  CHECK(p.playing);
  CHECK(p.pattern == 3);
  CHECK(p.edge == 2);
  CHECK(p.step == 2);
  REQUIRE(e.stop());
  e.render(nullptr, nullptr, l.data(), r.data(), 64);
  CHECK_FALSE(seq.position().playing);
}
