// SPDX-License-Identifier: GPL-3.0-or-later
// Why nothing sounds (app/src/silence.h): what each mode needs, the note and speech facts from the route and the sources, the answer (the note first),
// the half second and the clearing, and the text lengths. Pure; the screens' lines are read off app/scenarios/arrows_everywhere.txt and tested in
// perform_test.cpp and pattern_test.cpp.
#include <doctest/doctest.h>

#include "../app/src/silence.h"

#include <cstring>
#include <initializer_list>

namespace sl = o3_voc03er::silence;
namespace route = o3_voc03er::route;
using o3_voc03er::kModeCount;
using o3_voc03er::kModeTract;

namespace {
constexpr unsigned kBands = 0, kTalk = 1, kTune = 2, kRobot = 3;

sl::Sources src(unsigned mode) {
  sl::Sources s;
  s.mode = mode;
  return s;
}
}  // namespace

TEST_CASE("silence: each mode's needs come from the mode table") {
  for (unsigned m : {kBands, kTalk, kRobot}) {
    CHECK(sl::needs_note(m));
    CHECK(sl::needs_speech(m));
  }
  CHECK_FALSE(sl::needs_note(kTune));  // TUNE retunes what speaks and plays no note
  CHECK(sl::needs_speech(kTune));
  CHECK(sl::needs_note(kModeTract));  // TRACT is the carrier alone: a note, and nobody to speak
  CHECK_FALSE(sl::needs_speech(kModeTract));
  CHECK_FALSE(sl::needs_note(kModeCount));  // outside the table: nothing is asked
  CHECK_FALSE(sl::needs_speech(kModeCount));
}

TEST_CASE("silence: with nothing playing the note is missing first, then the speech") {
  for (unsigned m : {kBands, kTalk, kRobot}) {
    CHECK(sl::why(sl::facts(src(m))) == sl::Why::NoNote);
  }
  sl::Sources s = src(kTalk);
  s.pad_note = true;
  CHECK(sl::why(sl::facts(s)) == sl::Why::NothingSpeaks);  // a note, and the live input is silent
  s.input_live = true;
  s.input_peak = 0.001f;  // under the floor
  CHECK(sl::why(sl::facts(s)) == sl::Why::NothingSpeaks);
  s.input_peak = 0.2f;
  CHECK(sl::why(sl::facts(s)) == sl::Why::None);
}

TEST_CASE("silence: TUNE asks for speech only, TRACT for a note only") {
  sl::Sources t = src(kTune);
  CHECK(sl::why(sl::facts(t)) == sl::Why::NothingSpeaks);  // no note needed
  t.input_live = true;
  t.input_peak = 0.1f;
  CHECK(sl::why(sl::facts(t)) == sl::Why::None);

  sl::Sources r = src(kModeTract);
  CHECK(sl::why(sl::facts(r)) == sl::Why::NoNote);  // the silent input does not matter
  r.midi_note = true;
  CHECK(sl::why(sl::facts(r)) == sl::Why::None);
}

TEST_CASE("silence: a note comes from a pad, a MIDI key or the sequencer, and a carrier that is not the synth needs none") {
  sl::Sources s = src(kBands);
  s.input_live = true;
  s.input_peak = 0.5f;
  CHECK(sl::why(sl::facts(s)) == sl::Why::NoNote);
  s.pad_note = true;
  CHECK(sl::why(sl::facts(s)) == sl::Why::None);
  s.pad_note = false;
  s.midi_note = true;
  CHECK(sl::why(sl::facts(s)) == sl::Why::None);
  s.midi_note = false;
  s.seq_note = true;
  CHECK(sl::why(sl::facts(s)) == sl::Why::None);
  s.seq_note = false;
  for (std::uint8_t car : {route::kSoundsNoise, route::kFirstSlot, route::kSoundsLive, route::kSoundsVoice}) {
    s.sounds = car;
    CHECK(sl::why(sl::facts(s)) == sl::Why::None);
  }
  s.sounds = route::kSoundsSynth;
  CHECK(sl::why(sl::facts(s)) == sl::Why::NoNote);
}

TEST_CASE("silence: a slot speaks when it holds audio that plays, NONE speaks to nobody, the input does not count for a slot") {
  sl::Sources s = src(kTalk);
  s.pad_note = true;
  s.input_live = true;
  s.input_peak = 0.8f;  // a loud input that is not what SPEAKS
  s.speaks = route::kFirstSlot + 1u;
  CHECK(sl::why(sl::facts(s)) == sl::Why::NothingSpeaks);
  s.slot_audio = true;
  CHECK(sl::why(sl::facts(s)) == sl::Why::None);
  s.speaks = 1;  // ModSrc NONE
  CHECK(sl::why(sl::facts(s)) == sl::Why::NothingSpeaks);
  s.speaks = route::kSpeaksLive;
  CHECK(sl::why(sl::facts(s)) == sl::Why::None);
  s.input_live = false;  // no audio input on the unit
  CHECK(sl::why(sl::facts(s)) == sl::Why::NothingSpeaks);
}

TEST_CASE("silence: a slice that plays as the voice speaks, whatever the route says") {
  sl::Sources s = src(kTalk);
  s.pad_note = true;
  s.input_live = false;  // SPEAKS = LIVE with no input: nobody speaks
  s.speaks = route::kSpeaksLive;
  CHECK(sl::why(sl::facts(s)) == sl::Why::NothingSpeaks);
  s.slice_speaks = true;  // a region of a slot with no side (HELD) sounds as the voice
  CHECK(sl::why(sl::facts(s)) == sl::Why::None);
  s.pad_note = false;  // the note is still asked first
  CHECK(sl::why(sl::facts(s)) == sl::Why::NoNote);
}

TEST_CASE("silence: the watch shows an answer after half a second of trying and clears at once") {
  sl::Watch w;
  CHECK(w.update(1000, true, sl::Why::NoNote) == sl::Why::None);  // first sight
  CHECK(w.update(1499, true, sl::Why::NoNote) == sl::Why::None);
  CHECK(w.update(1500, true, sl::Why::NoNote) == sl::Why::NoNote);
  CHECK(w.shown() == sl::Why::NoNote);
  CHECK(w.update(2500, true, sl::Why::NoNote) == sl::Why::NoNote);  // it stays while it holds
  CHECK(w.update(2510, true, sl::Why::None) == sl::Why::None);      // the sound is back
  CHECK(w.shown() == sl::Why::None);
  CHECK(w.update(2520, true, sl::Why::NoNote) == sl::Why::None);    // and the half second starts over
  CHECK(w.update(3019, true, sl::Why::NoNote) == sl::Why::None);
  CHECK(w.update(3020, true, sl::Why::NoNote) == sl::Why::NoNote);
}

TEST_CASE("silence: not trying clears the watch, and a changed answer waits its own half second") {
  sl::Watch w;
  (void)w.update(0, true, sl::Why::NothingSpeaks);
  CHECK(w.update(600, true, sl::Why::NothingSpeaks) == sl::Why::NothingSpeaks);
  CHECK(w.update(610, false, sl::Why::NothingSpeaks) == sl::Why::None);  // the pad is up and the transport stopped
  CHECK(w.update(620, true, sl::Why::NothingSpeaks) == sl::Why::None);   // trying again: from the start
  CHECK(w.update(1120, true, sl::Why::NothingSpeaks) == sl::Why::NothingSpeaks);
  CHECK(w.update(1130, true, sl::Why::NoNote) == sl::Why::None);         // another answer: its own half second
  CHECK(w.update(1630, true, sl::Why::NoNote) == sl::Why::NoNote);
  w.reset();
  CHECK(w.shown() == sl::Why::None);
}

TEST_CASE("silence: the texts fit a line, name the way out, and the answer None has none") {
  CHECK(std::strlen(sl::text(sl::Why::NoNote)) < 80);
  CHECK(std::strlen(sl::text(sl::Why::NothingSpeaks)) < 80);
  CHECK(std::strstr(sl::text(sl::Why::NoNote), "NO NOTE") != nullptr);
  CHECK(std::strstr(sl::text(sl::Why::NothingSpeaks), "SAMPLE F2") != nullptr);
  CHECK(sl::text(sl::Why::None)[0] == '\0');
}
