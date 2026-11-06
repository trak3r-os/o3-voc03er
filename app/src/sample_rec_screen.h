// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE REC (docs/UI-DESIGN.md, "SAMPLE REC"), in the OR NOIR skin: the slot, its state in big type (EMPTY, ARMED, REC with the bars counted,
// STOPPING, then the take's length), the input's meter, source and gain, and the transport's bar. REC arms (the unit starts at the next bar line, at
// once with the transport stopped); REC again stops at the bar; a LENGTH stops it by itself; DELETE throws the take away.
//
//   F1 SOURCE  F2 GAIN  F3 LENGTH  F4 AFTER  F5 AUTOPLAY  F6 MONITOR  F7 SLOT  F8 -
//   A tap on an F key points the encoder at it (it stays lit); a second tap steps it on. UP / DOWN: the slot. LEFT / RIGHT: the length.
//   AFTER says where a finished take plays, looped and whole: on SPEAKS, on SOUNDS or nowhere. A take whose side is taken by the slot's other
//   side is not placed, and the screen says so. The pads play the chords of PERFORM (its code) so the take can have a chord under it.
//
// DELETE cannot empty a loop in the unit (no parameter does; docs/UI-DESIGN.md U4a): it stops the slot, takes it off its side and marks it
// empty here, and the next take replaces the audio. Main loop only; the state is a few bytes, nothing is big.

#include "perform_screen.h"
#include "sample_rec_logic.h"

#include <o3_voc03er/input.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_voc03er {

class SampleRecScreen final : public o3sdk::app::Screen {
 public:
  // `perform` is the app's PERFORM (the pads and the route code), `step` its source switch (app.cpp's select_source).
  void init(o3sdk::dsp::UnitId voice, const InputMeter* meter, const Vocoder* voc, PerformScreen* perform, PerformScreen::SourceStep step,
            void* user) noexcept;

  const char* title() const noexcept override { return "SAMPLE REC"; }
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
  unsigned length_index() const noexcept { return length_; }
  sample_rec::After after() const noexcept { return after_; }
  bool autoplay() const noexcept { return autoplay_; }
  bool recording() const noexcept { return phase_ != Phase::Idle; }

 private:
  enum class Phase : std::uint8_t { Idle, Starting, Active };

  void turn(o3sdk::app::Context& ctx, unsigned key, int delta) noexcept;
  void rec_press(o3sdk::app::Context& ctx) noexcept;
  void delete_press(o3sdk::app::Context& ctx) noexcept;
  void tick(o3sdk::app::Context& ctx) noexcept;
  void finish(o3sdk::app::Context& ctx) noexcept;
  void write_rec(o3sdk::app::Context& ctx, bool on) noexcept;
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  bool holds_audio(unsigned slot) const noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  const InputMeter* meter_ = nullptr;
  const Vocoder* voc_ = nullptr;
  PerformScreen* perform_ = nullptr;
  PerformScreen::SourceStep step_source_ = nullptr;
  void* source_user_ = nullptr;
  std::uint8_t slot_ = 0;        // 0..3
  std::uint8_t length_ = 0;      // sample_rec::kLengthBars index
  sample_rec::After after_ = sample_rec::After::Speaks;
  bool autoplay_ = true;
  std::uint8_t focus_ = 2;       // the F key the encoder drives (F3, LENGTH)
  Phase phase_ = Phase::Idle;
  bool stop_ = false;            // Rec 0 has been written: the take ends at its bar line
  std::uint32_t trim_ = 0;       // an auto-stopped take with the transport stopped: its exact length in samples (0: none), sent as Load by finish()
  bool drop_ = false;            // DELETE during a take: it ends and is thrown away
  std::uint8_t rec_slot_ = 0;    // the slot being taken
  std::uint64_t t0_ = 0;         // uptime_ms the Rec was written
  const char* note_ = nullptr;   // a moment's line (a static string), until note_until_
  std::uint64_t note_until_ = 0;
  std::uint8_t in_level_ = 0;    // the meter's followed level (attack at once, release by a quarter a frame)
  bool key_tap_ = false;         // turn() came from a second tap on an F key, not from the encoder
};

}  // namespace o3_voc03er
