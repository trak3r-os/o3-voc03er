// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE (docs/UI-DESIGN.md, "SAMPLE (SAMPLE PLAY): the slots"), in the OR NOIR skin: the four slots as a column (the selected one framed: name,
// length, speed, direction, its side in that side's ink), the slot's slices and play line in the middle, SOUNDS and the pad row on the right.
//
//   F1 SLOT  F2 SPEAKS  F3 SPEED  F4 DIR  F5 LOOP  F6 TRIG  F7 (blank)  F8 LEVEL        SHIFT: F2 SLICE CH, F3 OCTAVE
//   A tap on a value key (F1 SLOT, F3 SPEED, F8 LEVEL) lights it and changes nothing; a second tap steps a list; the encoder changes the lit key's value and LEFT / RIGHT do one encoder step of it.
//   F2 SPEAKS, F4 DIR, F5 LOOP and F6 TRIG act on the tap. UP / DOWN: the slot. F7 is blank: no name, no value, no focus, inert.
//   The pads are one finger each. The top two rows are the selected slot's slices 1..24 (even cuts, 8 by default, until SAMPLE EDIT): a press plays a slice on a loop reader (see
//   trigger()), it plays its cut once and stops at the end cut (TRIG ONE SHOT, the default; GATE also stops it at the release). A slice turns the slot's whole loop off (LOOP reads OFF)
//   until F5 starts it again; a new hit restarts the slice from its start cut and the screen is always MONO (a new slice fades the one before out). With LOOP ON a pad does not turn it off:
//   it repeats that slice (the unit's Trig 2) on the slot's own reader, until another pad or F5 OFF (TRIG GATE: until the release, then the whole loop goes on). A repeat (LOOP ON, TRIG ONE
//   SHOT) goes on when the screen is left, like the loop it narrows (PERFORM's pads play over it); back on SAMPLE F5 still reads ON SL n, F5 OFF ends it, and a project load or a new take in
//   the slot does too. LOOP ON on a slot that neither speaks nor sounds makes it speak first (the F2 route), so the loop is heard.
//   The bottom two rows (here, on SAMPLE EDIT, FILE and SAMPLE LOAD) are the keyboard: they play notes from the notes palette PATTERN shares (SP1: PerformScreen::notes(), not PERFORM's layout,
//   which is per mode and may hold parameter strips), A NOTE and B TRIAD at start, through PERFORM's audition entry at PERFORM's octave; the palette lives in RAM.
//   Rule 1, a slice pad alone is heard: in BANDS, TALK and ROBOT a slice is heard only while a synth note sounds, so a slice pressed with no lower pad held, a slot that does not sound as
//   the SOUNDS source and no note in the sequencer's pattern also plays "the note" (the last note or chord a lower pad played here, at first the root chord of row B): it ends with the slice
//   pad's release (GATE), the slice's end (ONE SHOT, service()), a lower pad (its own note takes over) and pad_leave. A new slice keeps the note.
//   Rule 2, a lower pad alone is heard: with no slice pad held, a slot that holds audio and is not the SOUNDS source, no loop (ON or waiting for the bar) and no repeat, a lower pad plays its
//   note as always and also starts "the picked slice" (the last slice a top pad fired on the slot, slice 1 at first and after the slot or its cuts change); the release of the last held
//   lower pad releases it (stops in GATE, plays out in ONE SHOT).
//   TRIG is per slot. F2 SPEAKS is what the vocoder listens to, as PERFORM's SPEAKS chip names it: LIVE or SLOT 1..4. A tap makes the selected slot speak (again: back to LIVE), the
//   encoder steps LIVE, SLOT 1..4 over the slots that hold audio; an empty slot is refused and the screen says why. The line under the panes says what the selected slot does now.
//   LOOP is the unit's Play: the whole slot plays round from the bar. SPEED, DIR and LEVEL are the slot's own parameters (Vocoder::loop_param).
//
// Main loop only; the cursor is a few bytes, the tables are in the SDRAM (g_sd in the .cpp).

#include "perform_screen.h"
#include "sample_logic.h"
#include "skin/ornoir.h"

#include <o3_voc03er/input.h>
#include <o3_voc03er/stepseq.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_voc03er {

class SampleScreen final : public o3sdk::app::Screen {
 public:
  // `perform` is the app's PERFORM (the pads, the chord and the route code).
  // `seq` is the step sequencer: service() publishes the SPEAKS slot's slices to it (U6c), and the pad LEDs show the slice it plays. `bank` is its patterns: rule 1 reads
  // whether the pattern that plays holds a note (nullptr: a playing sequencer counts as playing notes).
  void init(o3sdk::dsp::UnitId voice, const Vocoder* voc, PerformScreen* perform, StepSeq* seq = nullptr, const Bank* bank = nullptr) noexcept;
  // The map service() fills (in the SDRAM): the app hands it to StepSeq::init.
  SliceMap* slice_map() noexcept;

  const char* title() const noexcept override { return "SAMPLE"; }
  o3sdk::app::Chrome chrome(const o3sdk::app::Context&) const noexcept override { return o3sdk::app::Chrome::None; }
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context&) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  std::uint64_t repeat_buttons(const o3sdk::app::Context&) const noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& out) noexcept override;
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& help) const noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;

  unsigned slot() const noexcept { return slot_; }
  // MS1: the MIDI slice channel (midi_slice_logic.h: 0 SAME, 1..16, 17 OFF), set here by SHIFT + F2 and kept in config.o3a by the app. Main loop.
  unsigned slice_ch() const noexcept { return slice_ch_; }
  void set_slice_ch(unsigned v) noexcept;
  o3sdk::dsp::UnitId voice() const noexcept { return voice_; }  // the loop unit: SAMPLE EDIT's SHIFT + DELETE writes its Rec CLEAR

  // What SAMPLE EDIT (sample_edit_screen.h) shares with this screen: the selected slot, the cut tables (g_sd), the pads and the header.
  void set_slot(unsigned slot) noexcept;
  sample::SliceTable& table(unsigned slot) noexcept;
  bool edited(unsigned slot) const noexcept;  // the slot's cuts are the hand's or a re-cut's; a new length (a new take) clears it
  void set_edited(unsigned slot, bool v) noexcept;
  unsigned even_code(unsigned slot) const noexcept;  // F1 CUT EVEN's value (sample_edit_logic.h)
  void set_even_code(unsigned slot, unsigned code) noexcept;
  // FILE's load puts a slot's SPEED (0 0.5X, 1 1X, 2 2X), DIR and LEVEL back by the path the screen's own keys take (write): the unit's Loop n Speed, Dir and Level.
  void set_values(o3sdk::app::Context& ctx, unsigned loop, unsigned speed, bool reverse, float level_db) noexcept;
  // TRIG (true GATE, false ONE SHOT) of a slot 0..3, as the key flips it (P3: the project keeps it per slot). The project's choke_off field is read and written as 0: the screen is always MONO.
  bool gate(unsigned slot) const noexcept;
  void set_gate(unsigned slot, bool on) noexcept;
  void cuts_changed() noexcept;  // after any edit of a table: the next service() pass publishes the SPEAKS slot's cuts again
  Vocoder::LoopStatus status(unsigned slot) const noexcept { return view(slot); }
  bool pad_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in, bool repeat_ok = true) noexcept;  // a pad press or release; false when `in` is no pad. SAMPLE EDIT passes false: its pad auditions the slice once, whatever LOOP says
  void pad_leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept;
  void pad_leave(o3sdk::app::Context& ctx) noexcept;  // the slice voices and the held layout pads let go
  void pad_update(o3sdk::app::Context& ctx) noexcept;
  void fill_header(o3sdk::app::Context& ctx, ornoir::HeaderState& h) const noexcept;

  // Once a pass of the main loop, whichever screen is up (App::update): ends the slice voices that finished (a borrowed reader gets its own slot's
  // Speed, Dir, Level and Role back), rebuilds a slot's cuts when its length changed and republishes the SPEAKS slot's slices to the step
  // sequencer when the slot or its cuts changed.
  void service(o3sdk::app::Context& ctx) noexcept;

  // PATTERN's SLICE audition (Q5): slice `pad` (0..23) of the slot that SPEAKS, fired as a pad of this screen fires it (reader, TRIG, SLICES and the
  // slot's loop as there). Nothing when SPEAKS is LIVE or the slot has no such slice. release_slice is the pad's release (it ends a GATE slice).
  void fire_slice(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void release_slice(o3sdk::app::Context& ctx, unsigned pad) noexcept { release_pad(ctx, pad); }

  // What a tap on F2 does for the selected slot (it speaks; again: SPEAKS back to LIVE), through the same function. SAMPLE EDIT's F8 calls it, so the two
  // keys cannot route differently. Returns the note the tap said (a string that outlives the call: SAMPLE EDIT shows it on its own line), or nullptr
  // when it said nothing.
  const char* speaks_tap(o3sdk::app::Context& ctx) noexcept;

  // The side's ink of a slot (SPEAKS, SOUNDS, or the neutral rule colour): the tint of its overview picture.
  o3sdk::ui::Rgb slot_ink(o3sdk::app::Context& ctx, unsigned slot) const noexcept;
  // The slot neither speaks nor sounds (HELD): its loop is off and only its pads play it (UX3-5: SAMPLE LOAD says so after a load).
  bool on_no_side(o3sdk::app::Context& ctx, unsigned slot) const noexcept;

  // A refusal App::input makes for a key it handled itself (SAMPLE LOAD while FILE runs a job): said on this screen's own line.
  void notice(o3sdk::app::Context& ctx, const char* text) noexcept { say(ctx, text); }

  // True while the slot's WAV is still on its way after a project open (FILE's landing window): the slot looks empty to the unit but is not free.
  // REC, COPY and DELETE refuse such a slot, and the screens label it LOADING.
  bool waiting(unsigned slot) const noexcept { return perform_ != nullptr && slot < 4u && ((perform_->slots_waiting() >> slot) & 1u) != 0u; }

 private:
  void turn(o3sdk::app::Context& ctx, unsigned key, int delta) noexcept;
  void step_value(o3sdk::app::Context& ctx, bool shift, int delta) noexcept;  // the encoder or an arrow: the SHIFT-row value in focus, else the F key's
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  void write(o3sdk::app::Context& ctx, Vocoder::LoopField f, float v) noexcept;
  void write_slot(o3sdk::app::Context& ctx, unsigned loop, Vocoder::LoopField f, float v) noexcept;
  void set_speaks(o3sdk::app::Context& ctx, int delta, bool tap) noexcept;  // F2: the selected slot speaks (a tap), or the encoder steps LIVE, SLOT 1..4
  void move_slot(o3sdk::app::Context& ctx, unsigned slot, const route::Route& cur, const sample::SidePlan& plan) noexcept;  // writes a plan: leave a side, then enter one
  // slice `pad` of `slot` (SAMPLE: the selected slot; fire_slice: SPEAKS). `pressed`: a pad of this screen (not the SLICE lane or PATTERN's audition): with the slot's LOOP on
  // it repeats that slice instead of turning the loop off.
  // Returns true when a slice was fired (a reader took it).
  bool trigger(o3sdk::app::Context& ctx, unsigned slot, unsigned pad, bool pressed = false) noexcept;
  void stop_repeat(o3sdk::app::Context& ctx, unsigned slot) noexcept;  // the slot's repeating slice lets go (F5 OFF)
  bool release_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;  // true when it ended a GATE voice
  void stop_loop(o3sdk::app::Context& ctx, unsigned slot) noexcept;  // Play 0 on the slot's loop when it plays or waits (a slice owns the slot)
  void release_all(o3sdk::app::Context& ctx) noexcept;
  const pads::Layout& lay() const noexcept;  // the notes palette (PERFORM::notes(): rows A and B, note kinds only), the defaults when no PERFORM is wired
  bool play_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;     // pad 24..47: what this screen's row A (24..35) or B (36..47) plays there, held until the release; true when it plays
  void release_play(o3sdk::app::Context& ctx, unsigned pad) noexcept;  // its release (a pad that is not held does nothing)
  void step_slot(o3sdk::app::Context& ctx, int delta, bool tap) noexcept;  // F1: the selected slot 1..4 (a tap wraps, the encoder and the arrows stop at the ends)
  // Rule 1 and rule 2 (see the header comment). note_start brings the note for a slice of `pad`; note_stop lets it go; note_check (service) lets it go when no voice holds it.
  bool seq_plays_notes() const noexcept;
  void note_start(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void note_stop(o3sdk::app::Context& ctx) noexcept;
  void note_check(o3sdk::app::Context& ctx) noexcept;
  void lower_press(o3sdk::app::Context& ctx, unsigned pad) noexcept;  // pad 24..47: play_pad, then rule 2
  void lower_release(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void draw_line(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, unsigned slot, std::uint32_t length, int x, int y, int w) const noexcept;
  Vocoder::LoopStatus view(unsigned loop) const noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  const Vocoder* voc_ = nullptr;
  PerformScreen* perform_ = nullptr;
  StepSeq* seq_ = nullptr;
  std::uint8_t slot_ = 0;       // 0..3
  std::uint8_t focus_ = 0;      // the F key the encoder drives (F1, SLOT)
  std::uint8_t slice_ch_ = 0;   // the MIDI slice channel (SLICE CH): 0 SAME, 1..16, 17 OFF
  bool shift_slice_ = false;    // SHIFT + F2 was the last SHIFT key: the encoder steps SLICE CH while SHIFT is held
  bool shift_oct_ = false;      // SHIFT + F3 was the last SHIFT key: the encoder and the arrows step PERFORM's octave while SHIFT is held (AK1)
  const Bank* bank_ = nullptr;
  // Rule 1's note: sounds as PerformScreen::kAuditionNoteId while a slice of note_slot_ fired from top pad note_pad_ sounds. Main loop only.
  bool note_on_ = false;
  std::uint8_t note_pad_ = 0;
  std::uint8_t note_slot_ = 0;
  std::int16_t last_note_ = -1;  // the last note a lower pad played on this screen (-1: none yet, the root chord of row B stands in); its kind with it
  std::uint8_t last_kind_ = 0;
  std::uint32_t top_held_ = 0;  // the top pads held (bit = pad 0..23)
  std::uint8_t r2_pad_ = 0xFF;  // rule 2: the slice (a top pad index) the lower pads fired and hold, 0xFF none
  std::uint32_t aud_ = 0;       // the layout pads sounding (bit pad - 24): the audition entry's pad ids; main loop only
  bool key_tap_ = false;        // turn() came from a second tap on an F key, not from the encoder
  const char* note_ = nullptr;  // a moment's line (a static string), until note_until_
  std::uint64_t note_until_ = 0;
};

}  // namespace o3_voc03er
