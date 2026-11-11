// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE (docs/UI-DESIGN.md, "SAMPLE (SAMPLE PLAY): the slots"), in the OR NOIR skin: the four slots as a column (the selected one framed: name,
// length, speed, direction, its side in that side's ink), the slot's slices and play line in the middle, SOUNDS and the pad row on the right.
//
//   F1 PLAYS  F2 SIDE  F3 SPEED  F4 DIR  F5 LOOP  F6 TRIG  F7 CHOKE  F8 LEVEL        SHIFT + F1: ROW (A or B)
//   A tap on an F key points the encoder at it (it stays lit); a second tap steps it on (DIR and LOOP flip at once). UP / DOWN: the slot.
//   LEFT / RIGHT: the octave. The bottom two pad rows play PERFORM's layout rows A and B (Q7: the layout is PERFORM's, F1 PLAYS sets the row SHIFT + F1
//   chose, or the row of a pad that is held), sounded through PERFORM's audition entry like PATTERN's palette; the top two rows are the slot's slices 1..24 (even cuts,
//   8 by default, until SAMPLE EDIT): a press plays a slice on a loop reader (see trigger()), it plays its cut once and stops at the end cut (TRIG ONE SHOT, the default; GATE also stops it at the release).
//   A slice turns the slot's whole loop off (LOOP reads OFF) until F5 starts it again; a new hit restarts the slice from its start cut.
//   TRIG and CHOKE are per slot; CHOKE ON (default) gives a slot one voice (a hit fades the sounding slice out and starts the new one), OFF lets its slices overlap on the free readers (or steal the oldest).
//   SIDE moves the slot between SPEAKS, SOUNDS and NONE; an empty slot is refused and the screen says why. LOOP is the unit's Play: the whole
//   slot plays round from the bar. SPEED, DIR and LEVEL are the slot's own parameters (Vocoder::loop_param).
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
  // `seq` is the step sequencer: service() publishes the SPEAKS slot's slices to it (U6c), and the pad LEDs show the slice it plays.
  void init(o3sdk::dsp::UnitId voice, const Vocoder* voc, PerformScreen* perform, StepSeq* seq = nullptr) noexcept;
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
  // TRIG (true GATE, false ONE SHOT) and CHOKE (true OFF, false ON) of a slot 0..3, as the keys flip them (P3: the project keeps them per slot).
  bool gate(unsigned slot) const noexcept;
  bool choke_off(unsigned slot) const noexcept;
  void set_gate(unsigned slot, bool on) noexcept;
  void set_choke_off(unsigned slot, bool off) noexcept;
  void cuts_changed() noexcept;  // after any edit of a table: the next service() pass publishes the SPEAKS slot's cuts again
  Vocoder::LoopStatus status(unsigned slot) const noexcept { return view(slot); }
  bool pad_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;  // a pad press or release; false when `in` is no pad
  void pad_leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept;
  void pad_leave(o3sdk::app::Context& ctx) noexcept;  // the slice voices and the held layout pads let go
  void pad_update(o3sdk::app::Context& ctx) noexcept;
  void fill_header(o3sdk::app::Context& ctx, ornoir::HeaderState& h) const noexcept;

  // Once a pass of the main loop, whichever screen is up (App::update): ends the slice voices that finished (a borrowed reader gets its own slot's
  // Speed, Dir, Level and Role back), rebuilds a slot's cuts when its length changed and republishes the SPEAKS slot's slices to the step
  // sequencer when the slot or its cuts changed.
  void service(o3sdk::app::Context& ctx) noexcept;

  // PATTERN's SLICE audition (Q5): slice `pad` (0..23) of the slot that SPEAKS, fired as a pad of this screen fires it (reader, TRIG, CHOKE and the
  // slot's loop as there). Nothing when SPEAKS is LIVE or the slot has no such slice. release_slice is the pad's release (it ends a GATE slice).
  void fire_slice(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void release_slice(o3sdk::app::Context& ctx, unsigned pad) noexcept { release_pad(ctx, pad); }

  // The side's ink of a slot (SPEAKS, SOUNDS, or the neutral rule colour): the tint of its overview picture.
  o3sdk::ui::Rgb slot_ink(o3sdk::app::Context& ctx, unsigned slot) const noexcept;

  // A refusal App::input makes for a key it handled itself (SAMPLE LOAD while FILE runs a job): said on this screen's own line.
  void notice(o3sdk::app::Context& ctx, const char* text) noexcept { say(ctx, text); }

 private:
  void turn(o3sdk::app::Context& ctx, unsigned key, int delta) noexcept;
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  void write(o3sdk::app::Context& ctx, Vocoder::LoopField f, float v) noexcept;
  void write_slot(o3sdk::app::Context& ctx, unsigned loop, Vocoder::LoopField f, float v) noexcept;
  void set_side(o3sdk::app::Context& ctx, int delta) noexcept;
  void trigger(o3sdk::app::Context& ctx, unsigned slot, unsigned pad) noexcept;  // slice `pad` of `slot` (SAMPLE: the selected slot; fire_slice: SPEAKS)
  void release_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void stop_loop(o3sdk::app::Context& ctx, unsigned slot) noexcept;  // Play 0 on the slot's loop when it plays or waits (a slice owns the slot)
  void release_all(o3sdk::app::Context& ctx) noexcept;
  void play_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;     // pad 24..47: what PERFORM's row A (24..35) or B (36..47) plays there, held until the release
  void release_play(o3sdk::app::Context& ctx, unsigned pad) noexcept;  // its release (a pad that is not held does nothing)
  void step_plays(o3sdk::app::Context& ctx, int delta, bool tap) noexcept;  // F1: PLAYS of the row of the lowest held layout pad, else of the chosen row
  void flip_row(o3sdk::app::Context& ctx) noexcept;                    // SHIFT + F1: the other row; its pads go dark for a moment
  void draw_line(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, unsigned slot, std::uint32_t length, int x, int y, int w) const noexcept;
  Vocoder::LoopStatus view(unsigned loop) const noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  const Vocoder* voc_ = nullptr;
  PerformScreen* perform_ = nullptr;
  StepSeq* seq_ = nullptr;
  std::uint8_t slot_ = 0;       // 0..3
  std::uint8_t focus_ = 0;      // the F key the encoder drives (F1, PLAYS)
  std::uint8_t row_ = 0;        // the layout row F1 sets: 0 A, 1 B (pads 24..35, 36..47)
  std::uint64_t flash_until_ = 0;  // ROW changed: its pads are dark until then
  std::uint32_t aud_ = 0;       // the layout pads sounding (bit pad - 24): the audition entry's pad ids; main loop only
  bool key_tap_ = false;        // turn() came from a second tap on an F key, not from the encoder
  const char* note_ = nullptr;  // a moment's line (a static string), until note_until_
  std::uint64_t note_until_ = 0;
};

}  // namespace o3_voc03er
