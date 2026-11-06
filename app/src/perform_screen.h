// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PERFORM, the home screen (docs/UI-DESIGN.md), in the OR NOIR skin (skin/ornoir.h): what speaks (the audio input) on the left, the voiceprint
// of the vocoder's 16 bands in the middle, what sounds (the carrier) on the right, and the pads as a chord grid under them.
//
//   F1 MODE  F2 SPEAKS  F3 SOUNDS  F4 KEY  F5 SCALE  F6 CHORD  F7 PADS  F8 by mode (BANDS SPREAD, TALK QUALITY, TUNE SPEED)
//   A tap on an F key points the encoder at it (the key stays lit); a second tap steps it on. The encoder changes the value.
//   pads          CHORDS: a chord a pad (rows: POWER, OPEN, 7TH, and the bottom row's kind); KEYS: a chromatic keyboard
//   SHIFT         latches the held pad's chord (it keeps sounding); DELETE releases it
//   UP / DOWN     the bottom row's chord kind;  LEFT / RIGHT  the octave (they repeat when held)
//   PLAY          the transport (the app's); the other screen buttons open their screen (app.cpp), MODE is F1
//
// Everything runs in the main loop (o3-sdk app/screen.h). The tables (labels, the chord of each pad, the peak holds) are one O3SDK_BIG struct
// in perform_screen.cpp; the screen keeps a cursor and a few flags in the SRAM.

#include <o3_voc03er/input.h>
#include <o3_voc03er/pad_map.h>

#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_voc03er {

class PerformScreen final : public o3sdk::app::Screen {
 public:
  // The app asks the screen to step the audio source (F2): it owns the input and its meter (app.cpp's select_source).
  using SourceStep = void (*)(void* user) noexcept;

  void init(o3sdk::dsp::UnitId voice, const InputMeter* meter, const Vocoder* voc, SourceStep step, void* user) noexcept;

  const char* title() const noexcept override { return "PERFORM"; }
  o3sdk::app::Chrome chrome(const o3sdk::app::Context&) const noexcept override { return o3sdk::app::Chrome::None; }
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context&) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  std::uint64_t repeat_buttons(const o3sdk::app::Context&) const noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& help) const noexcept override;

  int octave() const noexcept { return octave_; }
  unsigned focus() const noexcept { return focus_; }
  bool keys_mode() const noexcept { return keys_; }
  int latched() const noexcept { return latched_; }

 private:
  void release_all(o3sdk::app::Context& ctx) noexcept;
  void press_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void release_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void latch(o3sdk::app::Context& ctx, bool on) noexcept;
  void turn(o3sdk::app::Context& ctx, int delta, bool coarse) noexcept;
  void bottom_kind(o3sdk::app::Context& ctx, int delta) noexcept;
  void refresh_labels(const o3sdk::dsp::Engine& e) noexcept;
  bool note_in_use(std::uint8_t note, unsigned except_pad) const noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  const InputMeter* meter_ = nullptr;
  const Vocoder* voc_ = nullptr;
  SourceStep step_source_ = nullptr;
  void* source_user_ = nullptr;
  unsigned focus_ = 5;       // the F key the encoder drives (default F6, CHORD)
  int octave_ = 0;
  unsigned bottom_ = 0;      // ChordKind of the bottom row
  bool keys_ = false;        // PADS: false CHORDS, true KEYS
  int latched_ = -1;         // the latched pad, or -1
  std::uint64_t held_ = 0;   // the pads this screen holds notes for (bit pad)
  std::uint32_t label_sig_ = 0xFFFFFFFFu;  // what the labels were built for
  std::uint32_t frame_ = 0;
  mutable o3sdk::app::HelpPadNames help_names_ = {};
};

}  // namespace o3_voc03er
