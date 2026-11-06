// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE (docs/UI-DESIGN.md, "SAMPLE (SAMPLE PLAY): the slots"), in the OR NOIR skin: the four slots as a column (the selected one framed: name,
// length, speed, direction, its side in that side's ink), the slot's slices and play line in the middle, SOUNDS and the chord on the right.
//
//   F1 CHORD  F2 SIDE  F3 SPEED  F4 DIR  F5 LOOP  F6 TRIG  F7 CHOKE  F8 LEVEL
//   A tap on an F key points the encoder at it (it stays lit); a second tap steps it on (DIR and LOOP flip at once). UP / DOWN: the slot.
//   LEFT / RIGHT: the octave. The bottom two pad rows play PERFORM's chords (its code); the top two rows are the slot's slices 1..24 (even cuts,
//   8 by default, until SAMPLE EDIT): a press plays a slice on a loop reader (see trigger()), TRIG GATE stops it at the release, ONE SHOT lets it end.
//   TRIG and CHOKE are per slot; CHOKE ON (default) gives a slot one voice, OFF lets its slices overlap on the free readers (or steal the oldest).
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
  void cuts_changed() noexcept;  // after any edit of a table: the next service() pass publishes the SPEAKS slot's cuts again
  Vocoder::LoopStatus status(unsigned slot) const noexcept { return view(slot); }
  bool pad_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;  // a pad press or release; false when `in` is no pad
  void pad_leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept;
  void pad_leave(o3sdk::app::Context& ctx) noexcept;  // the slice voices and the held chord pads let go
  void pad_update(o3sdk::app::Context& ctx) noexcept;
  void fill_header(o3sdk::app::Context& ctx, ornoir::HeaderState& h) const noexcept;

  // Once a pass of the main loop, whichever screen is up (App::update): ends the slice voices that finished (a borrowed reader gets its own slot's
  // Speed, Dir, Level and Role back), rebuilds a slot's cuts when its length changed and republishes the SPEAKS slot's slices to the step
  // sequencer when the slot or its cuts changed.
  void service(o3sdk::app::Context& ctx) noexcept;

  // The side's ink of a slot (SPEAKS, SOUNDS, or the neutral rule colour): the tint of its overview picture.
  o3sdk::ui::Rgb slot_ink(o3sdk::app::Context& ctx, unsigned slot) const noexcept;

 private:
  void turn(o3sdk::app::Context& ctx, unsigned key, int delta) noexcept;
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  void write(o3sdk::app::Context& ctx, Vocoder::LoopField f, float v) noexcept;
  void set_side(o3sdk::app::Context& ctx, int delta) noexcept;
  void trigger(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void release_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void release_all(o3sdk::app::Context& ctx) noexcept;
  void draw_line(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, unsigned slot, std::uint32_t length, int x, int y, int w) const noexcept;
  Vocoder::LoopStatus view(unsigned loop) const noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  const Vocoder* voc_ = nullptr;
  PerformScreen* perform_ = nullptr;
  StepSeq* seq_ = nullptr;
  std::uint8_t slot_ = 0;       // 0..3
  std::uint8_t focus_ = 0;      // the F key the encoder drives (F1, CHORD)
  bool key_tap_ = false;        // turn() came from a second tap on an F key, not from the encoder
  const char* note_ = nullptr;  // a moment's line (a static string), until note_until_
  std::uint64_t note_until_ = 0;
};

}  // namespace o3_voc03er
