// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE (docs/UI-DESIGN.md, "SAMPLE (SAMPLE PLAY): the slots"), in the OR NOIR skin: the four slots as a column (the selected one framed: name,
// length, speed, direction, its side in that side's ink), the slot's state in the middle, SOUNDS and the chord on the right. No slices yet (U5).
//
//   F1 CHORD  F2 SIDE  F3 SPEED  F4 DIR  F5 LOOP  F6 -  F7 -  F8 LEVEL
//   A tap on an F key points the encoder at it (it stays lit); a second tap steps it on (DIR and LOOP flip at once). UP / DOWN: the slot.
//   LEFT / RIGHT: the octave. The bottom two pad rows play PERFORM's chords (its code); the top two rows are dark and silent until the slices.
//   SIDE moves the slot between SPEAKS, SOUNDS and NONE; an empty slot is refused and the screen says why. LOOP is the unit's Play: the whole
//   slot plays round from the bar. SPEED, DIR and LEVEL are the slot's own parameters (Vocoder::loop_param).
//
// Main loop only; the state is a few bytes, nothing is big.

#include "perform_screen.h"

#include <o3_voc03er/input.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_voc03er {

class SampleScreen final : public o3sdk::app::Screen {
 public:
  // `perform` is the app's PERFORM (the pads, the chord and the route code).
  void init(o3sdk::dsp::UnitId voice, const Vocoder* voc, PerformScreen* perform) noexcept;

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

 private:
  void turn(o3sdk::app::Context& ctx, unsigned key, int delta) noexcept;
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  void write(o3sdk::app::Context& ctx, Vocoder::LoopField f, float v) noexcept;
  void set_side(o3sdk::app::Context& ctx, int delta) noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  const Vocoder* voc_ = nullptr;
  PerformScreen* perform_ = nullptr;
  std::uint8_t slot_ = 0;       // 0..3
  std::uint8_t focus_ = 0;      // the F key the encoder drives (F1, CHORD)
  bool key_tap_ = false;        // turn() came from a second tap on an F key, not from the encoder
  const char* note_ = nullptr;  // a moment's line (a static string), until note_until_
  std::uint64_t note_until_ = 0;
};

}  // namespace o3_voc03er
