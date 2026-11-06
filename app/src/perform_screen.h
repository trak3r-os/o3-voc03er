// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PERFORM, the home screen (docs/UI-DESIGN.md), in the OR NOIR skin (skin/ornoir.h): what speaks (the audio input) on the left, the voiceprint
// of the vocoder's 16 bands in the middle, what sounds (the carrier) on the right, and the pads as a chord grid under them.
//
//   F1 MODE  F2 SPEAKS  F3 SOUNDS  F4 KEY  F5 SCALE  F6 CHORD  F7 PADS  F8 by mode (BANDS SPREAD, TALK QUALITY, TUNE SPEED)
//   A tap on an F key points the encoder at it (the key stays lit); a second tap steps it on. The encoder changes the value.
//   pads          F7: CHORDS (rows POWER, OPEN, 7TH and the bottom row's kind) or KEYS (a chromatic keyboard). SHIFT + F7 toggles BEND ROW (kept when F7
//                 moves): the bottom row is a bend strip; the chord rows move up one, or the keyboard keeps the three rows above it
//   bend          PERFORMANCE while PERFORM is up frees the encoder (no F key hot): it bends the carrier, range / 8 a detent, and springs back to 0
//                 about 120 ms after the last one. A tap on an F key takes the encoder back. MIDI pitch bend adds to it (set_wheel)
//   SHIFT row     while SHIFT is held the F row shows a second row (FREEZE, GATE, ARP, GLIDE, FORMANT, THRU, BEND ROW, PRESET); SHIFT + F key focuses
//                 its value, shown while it has the focus; a plain F tap goes back (perform_logic.h kShiftKeys)
//   SHIFT         latches the held pad's chord (it keeps sounding); DELETE releases it
//   UP / DOWN     the bottom row's chord kind;  LEFT / RIGHT  the octave (they repeat when held)
//   PLAY          the transport (the app's); the other screen buttons open their screen (app.cpp), MODE is F1
//
// Everything runs in the main loop (o3-sdk app/screen.h). The tables (labels, the chord of each pad, the peak holds) are one O3SDK_BIG struct
// in perform_screen.cpp; the screen keeps a cursor and a few flags in the SRAM.

#include "perform_logic.h"

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
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& out) noexcept override;
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& help) const noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;

  // PERFORMANCE pressed while this screen is up: the encoder is free (it bends) / app.cpp's pitch bend: the wheel's semitones.
  void free_encoder(o3sdk::app::Context& ctx) noexcept;
  void set_wheel(o3sdk::app::Context& ctx, unsigned lsb, unsigned msb) noexcept;
  // app.cpp's CC1 (the mod wheel, 0..127): the vibrato depth, held apart from the MOD pad (the larger wins).
  void set_mod_wheel(o3sdk::app::Context& ctx, unsigned cc) noexcept;
  float vib_pad() const noexcept { return vib_pad_; }
  const char* flash() const noexcept { return flash_; }
  // BEND RANGE: 2, 5 or 12 semitones either way (other values are refused, false).
  bool set_bend_range(unsigned semitones) noexcept;
  unsigned bend_range() const noexcept { return range_; }
  float bend_local() const noexcept { return local_; }

  int octave() const noexcept { return octave_; }
  unsigned focus() const noexcept { return focus_; }
  bool keys_mode() const noexcept { return keys_; }
  bool strip_mode() const noexcept { return strip_; }
  int latched() const noexcept { return latched_; }

 private:
  void release_all(o3sdk::app::Context& ctx) noexcept;
  void press_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void release_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void latch(o3sdk::app::Context& ctx, bool on) noexcept;
  void turn(o3sdk::app::Context& ctx, int delta, bool coarse) noexcept;
  void shift_key(o3sdk::app::Context& ctx, unsigned key) noexcept;
  void toggle_strip(o3sdk::app::Context& ctx) noexcept;
  void apply_bend(o3sdk::app::Context& ctx) noexcept;
  void apply_vib(o3sdk::app::Context& ctx) noexcept;
  void control_press(o3sdk::app::Context& ctx, unsigned pad, perform::PianoFn fn) noexcept;
  void control_release(o3sdk::app::Context& ctx, unsigned pad, perform::PianoFn fn) noexcept;
  void show_flash(o3sdk::app::Context& ctx, const char* name, const char* value) noexcept;
  int key_pad_note(unsigned pad) const noexcept;
  void bottom_kind(o3sdk::app::Context& ctx, int delta) noexcept;
  void refresh_labels(const o3sdk::dsp::Engine& e) noexcept;
  bool note_in_use(std::uint8_t note, unsigned except_pad) const noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  const InputMeter* meter_ = nullptr;
  const Vocoder* voc_ = nullptr;
  SourceStep step_source_ = nullptr;
  void* source_user_ = nullptr;
  unsigned preset_ = perform::kNoPreset;  // the last factory sound chosen (SHIFT + F8), or kNoPreset: the window shows ---
  unsigned sfocus_ = perform::kNoShiftFocus;  // the SHIFT-row value the encoder drives (1 GATE, 2 ARP, 3 GLIDE, 4 FORMANT, 5 THRU, 7 PRESET), or kNoShiftFocus; the row stays shown while set
  unsigned focus_ = 5;       // the F key the encoder drives (default F6, CHORD); perform::kBendFocus (8): none, the encoder bends
  int octave_ = 0;
  unsigned bottom_ = 0;      // ChordKind of the bottom row
  bool keys_ = false;        // F7 PADS: false CHORDS, true KEYS
  bool strip_ = false;       // BEND ROW (SHIFT + F7): the bottom row is the bend strip
  unsigned strip_last_ = 0;  // the strip pad pressed last
  unsigned range_ = 2;       // BEND RANGE, semitones either way (2, 5, 12)
  float local_ = 0.0f;       // the encoder's and the strip's bend, semitones (main loop only)
  float wheel_ = 0.0f;       // MIDI pitch bend, semitones (main loop only); the unit's Bend is their sum, held to +-12
  std::uint64_t spring_at_ = 0;  // uptime_ms at which the encoder's bend returns to 0; 0 none
  // The held control pads of KEYS (MOD, BND-, BND+; bit pad), apart from held_ (they play no note). Main loop only.
  std::uint64_t ctl_held_ = 0;
  int bnd_dir_ = 0;                  // the time bend in force: -1 BND-, +1 BND+, 0 none (the last pressed of the strip and the BND pads wins)
  std::uint64_t bnd_at_[2] = {0, 0}; // uptime_ms at which BND- and BND+ were pressed
  bool mod_held_ = false;            // MOD down: the depth rises; up: it falls from mod_from_ since mod_t_
  std::uint64_t mod_t_ = 0;          // uptime_ms the depth's present ramp began (as if from 0 while rising)
  float mod_from_ = 0.0f;
  float vib_pad_ = 0.0f;             // the pad's vibrato depth 0..1
  unsigned cc1_ = 0;                 // the mod wheel, 0..127
  float vib_sent_ = 0.0f;            // the last Vib written (only a change is written)
  char flash_[24] = {};              // a moment's text on the hint line (VOL, ROOT, OCT), until flash_until_
  std::uint64_t flash_until_ = 0;
  int latched_ = -1;         // the latched pad, or -1
  std::uint64_t held_ = 0;   // the pads this screen holds notes for (bit pad)
  std::uint32_t label_sig_ = 0xFFFFFFFFu;  // what the labels were built for
  std::uint32_t frame_ = 0;
  mutable o3sdk::app::HelpPadGroup help_groups_[5] = {};
};

}  // namespace o3_voc03er
