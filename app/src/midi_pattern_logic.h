// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// M3, patterns by MIDI (docs/UI-DESIGN.md 7c): the pure part. A program change picks a pattern at the next landing of the step sequencer; the
// carrier's MIDI notes record into PATTERN as the pads do, quantised. No state here: app.cpp and the PATTERN screen call these.

#include <o3_voc03er/stepseq.h>

#include <cstdint>

namespace o3_voc03er::midipat {

// Program 0..7 is pattern A..H. Above 7 there is no pattern: it is ignored (not wrapped, so a sequencer that sends a bank of 128 programs
// does not play pattern A on program 8). -1: ignored.
constexpr int pattern_of_program(unsigned program) noexcept { return program < kBankPatterns ? static_cast<int>(program) : -1; }

// What a program change does. The landing is the one every pattern change has (StepSeq::request_pattern: the hook takes it when the playing
// pattern reaches its end, and at the next Play), which is the bar line for the default LENGTH 16 and the pattern's own end for a shorter one.
//   Request  asks that landing for the pattern.
//   Range    program above 7: nothing.
//   Chain    a SONG chain is playing: the chain owns the pattern while it plays (it asks for the next one at each lap and a second request would
//            cut its walk), so the program change is ignored.
enum class Program : std::uint8_t { Request, Range, Chain };
constexpr Program program_action(unsigned program, bool chain_playing) noexcept {
  return pattern_of_program(program) < 0 ? Program::Range : (chain_playing ? Program::Chain : Program::Request);
}

// Where a recorded note goes. `playing` is the stamp of the note (Vocoder::NoteIn): while the transport played, the nearest sixteenth edge `k`
// of the note picks the step of the pattern that PLAYS, from what the hook last published (`now`: its edge, step and pattern, StepSeq::position()):
// the note lands on the step whose edge is nearest, which is the one that just played or the next one (a note in the second half of a step
// belongs to the next). While stopped it is the pads' step entry: the cursor of the shown pattern, which then moves on.
struct Target {
  bool at_cursor = true;
  std::uint8_t pattern = 0;
  std::uint8_t step = 0;
};
// `length` is the playing pattern's LENGTH (1..16). The hook may have run a few steps on since the note was stamped (the main loop is late at
// the worst load): the difference of the edges, as a signed 16-bit, walks back or forward from the hook's step, wrapping at LENGTH. A pattern
// change in between is not seen (one note in a million lands a step off).
inline Target target_of(bool playing, std::uint16_t note_edge, const StepSeq::Position& now, unsigned length) noexcept {
  Target t;
  if (!playing || !now.playing || length == 0u) {
    return t;
  }
  const int delta = static_cast<std::int16_t>(static_cast<std::uint16_t>(note_edge - now.edge));
  const int len = static_cast<int>(length);
  int step = (static_cast<int>(now.step) + delta) % len;
  step = step < 0 ? step + len : step;
  t.at_cursor = false;
  t.pattern = now.pattern;
  t.step = static_cast<std::uint8_t>(step);
  return t;
}

// What one recorded note writes: the note and its velocity as the step's level (127 and down to 1; a velocity of 0 is a note off and never gets here).
constexpr unsigned level_of_velocity(unsigned velocity) noexcept { return velocity < 1u ? 1u : (velocity > 127u ? 127u : velocity); }

// The channel test App::midi() applies: 0 is omni.
constexpr bool channel_ok(unsigned user_channel, unsigned message_channel) noexcept { return user_channel == 0u || user_channel == message_channel; }

}  // namespace o3_voc03er::midipat
