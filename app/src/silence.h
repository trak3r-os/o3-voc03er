// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Why nothing sounds (rd-ctl, the maintainer's two test rounds): in BANDS, TALK and ROBOT the voice is heard only while something speaks AND a synth note
// sounds, and no screen said which half was missing. This header answers it, pure (no engine, no screen), for any screen to call:
//
//   facts(Sources)  folds what the main loop already reads (the route, the pads, the sequencer, MIDI, the input meter, the SPEAKS slot) into two
//                   answers: does a note sound, does something speak.
//   why(Facts)      the mode decides what it needs (needs_note, needs_speech: sets of liveness.h's mode bits, so a new mode is one table row) and the
//                   first missing half is the answer: None, NoNote or NothingSpeaks (the note first: a pad or a pattern is the first thing to do).
//   Watch           the line is shown only while the player is trying to be heard (a pad down, or the transport playing) and the same answer has lasted
//                   kHoldMs; it goes at once when the sound returns or the trying stops.
//
// Nothing here touches the audio path: App::update feeds it once per pass from values the screens read already (docs/UI-DESIGN.md, "Say why nothing
// sounds"). Main loop only. tests/silence_test.cpp holds it.

#include "liveness.h"

#include <o3_voc03er/modes.h>
#include <o3_voc03er/route.h>

#include <cstdint>

namespace o3_voc03er::silence {

enum class Why : std::uint8_t { None, NoNote, NothingSpeaks };

// The text of each answer (at most 80 characters: PERFORM's hint line and PATTERN's strip note both take it).
inline constexpr const char* kNoNoteText = "NO NOTE: HOLD A LOWER PAD OR PLAY A PATTERN";
inline constexpr const char* kNothingSpeaksText = "NOTHING SPEAKS: TALK INTO THE INPUT OR MAKE A SLOT SPEAK (SAMPLE F2)";

constexpr const char* text(Why w) noexcept {
  return w == Why::NoNote ? kNoNoteText : (w == Why::NothingSpeaks ? kNothingSpeaksText : "");
}

// What each mode needs before it can be heard (the sets of mode bits, liveness.h's): the carrier runs in every mode but TUNE (TUNE retunes what speaks and
// plays no note), the modulator speaks in every mode but TRACT (the tract's mouth is played, not spoken into).
inline constexpr std::uint8_t kNeedsNote = live::kNotTune;
inline constexpr std::uint8_t kNeedsSpeech = static_cast<std::uint8_t>(live::kAllModes & ~live::kTractOnly);

constexpr bool needs_note(unsigned mode) noexcept { return live::heard(kNeedsNote, mode); }
constexpr bool needs_speech(unsigned mode) noexcept { return live::heard(kNeedsSpeech, mode); }

// The input peak (linear) a live voice must pass to count as speaking: -50 dB, above the noise of a quiet input and below a murmur.
inline constexpr float kSpeechFloor = 0.003f;

struct Sources {
  unsigned mode = 0;                           // the mode index (modes.h)
  std::uint8_t speaks = route::kSpeaksLive;    // Vocoder::ModSrc
  std::uint8_t sounds = route::kSoundsSynth;   // Vocoder::CarSrc
  bool pad_note = false;                       // a pad that plays a note is held, or one is latched
  bool midi_note = false;                      // a MIDI note is held
  bool seq_note = false;                       // the transport runs and the pattern it plays holds a note
  bool input_live = false;                     // the audio input is there and the SPEAKS source is the live one
  float input_peak = 0.0f;                     // the input meter's peak, linear
  bool slot_audio = false;                     // the SPEAKS slot (when it is one) holds audio that plays
  bool slice_speaks = false;                   // a slice plays as the voice: a region of any slot, whatever the route (SAMPLE's slices of a slot on no side speak)
};

struct Facts {
  unsigned mode = 0;
  bool note = false;
  bool speech = false;
};

// A carrier that is not the synth (noise, a slot, LIVE, VOICE) sounds without a note: only the synth waits for one.
constexpr bool note_sounds(const Sources& s) noexcept { return s.sounds != route::kSoundsSynth || s.pad_note || s.midi_note || s.seq_note; }

constexpr bool speech_sounds(const Sources& s) noexcept {
  if (s.slice_speaks) {
    return true;
  }
  if (s.speaks == route::kSpeaksLive) {
    return s.input_live && s.input_peak >= kSpeechFloor;
  }
  return route::is_slot(s.speaks) && s.slot_audio;  // NONE (1) speaks to nobody
}

constexpr Facts facts(const Sources& s) noexcept { return Facts{s.mode, note_sounds(s), speech_sounds(s)}; }

constexpr Why why(const Facts& f) noexcept {
  if (needs_note(f.mode) && !f.note) {
    return Why::NoNote;
  }
  if (needs_speech(f.mode) && !f.speech) {
    return Why::NothingSpeaks;
  }
  return Why::None;
}

// The half second and the clearing. `update` is called every pass with the answer of that pass; it returns what the screen shows now.
class Watch {
 public:
  static constexpr std::uint64_t kHoldMs = 500;

  // `trying`: a pad is down or the transport plays. The same candidate for kHoldMs, while trying, is shown; None or no trying clears at once.
  Why update(std::uint64_t now_ms, bool trying, Why candidate) noexcept {
    if (!trying || candidate == Why::None) {
      cand_ = Why::None;
      shown_ = Why::None;
      return shown_;
    }
    if (candidate != cand_) {
      cand_ = candidate;
      since_ = now_ms;
      shown_ = Why::None;
    } else if (now_ms - since_ >= kHoldMs) {
      shown_ = cand_;
    }
    return shown_;
  }

  Why shown() const noexcept { return shown_; }
  void reset() noexcept {
    cand_ = shown_ = Why::None;
    since_ = 0;
  }

 private:
  Why cand_ = Why::None;
  Why shown_ = Why::None;
  std::uint64_t since_ = 0;
};

}  // namespace o3_voc03er::silence
