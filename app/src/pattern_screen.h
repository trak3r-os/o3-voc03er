// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PATTERN (docs/UI-DESIGN.md "PATTERN"), in the OR NOIR skin: the pattern letter A..H large at the left, sixteen step cells with four lanes under each
// (NOTE, CARRIER, LEVEL, SLICE: what each step stores), the playhead in the voice ink, the cursor framed, the lane in force lit.
//
//   F1 PATTERN  F2 LANE  F3 LENGTH  F4 TEMPO  F5 VALUE  F6 CLEAR LANE  F7 FOLLOW  F8 SAVE
//   A tap on F1..F5 points the encoder at that key (it stays lit); a second tap steps it on. F6 clears the lane of the pattern at once, F7 toggles the
//   cursor riding the playhead, F8 saves the bank like FILE does. The encoder starts on F5, the cursor step's value in the lane.
//   UP / DOWN: the lane. LEFT / RIGHT: the cursor (they repeat when held). NOTE, INSTR, VOL, FX: the lane (app.cpp opens the screen on it).
//   Pads: rows 2 and 3, columns 0..7: steps 1..16 (a press moves the cursor; SHIFT + press writes a rest in the lane); columns 8..11: patterns A..H (the
//   one shown, and the next to play: it starts when the playing one ends). Rows 0 and 1: the lane's palette (NOTE: PERFORM's two lower chord rows, or two
//   octaves of keys when PERFORM is on KEYS, at PERFORM's octave; CARRIER: FOLLOW, SYNTH, NOISE, SLOT 1..4; LEVEL: 24 steps up to 100; SLICE: the SPEAKS slot's cuts 1..24, lit up to its count).
//   A palette pad writes into the cursor's step and moves the cursor on.
//
// Main loop only. The screen keeps a cursor and a few flags; HELP's labels are in the SDRAM (g_pd in the .cpp).

#include "pattern_logic.h"
#include "perform_screen.h"

#include <o3_voc03er/stepseq.h>

#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_voc03er {

class PatternScreen final : public o3sdk::app::Screen {
 public:
  // F8: the app's save of the bank (the same job FILE starts); it says what it did through the screen's line when it cannot (the card is busy).
  using SaveFn = bool (*)(void* user, o3sdk::app::Context& ctx) noexcept;

  void init(o3sdk::dsp::UnitId voice, Bank* bank, StepSeq* seq, PerformScreen* perform, SaveFn save, void* user) noexcept;

  const char* title() const noexcept override { return "PATTERN"; }
  o3sdk::app::Chrome chrome(const o3sdk::app::Context&) const noexcept override { return o3sdk::app::Chrome::None; }
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context&) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  std::uint64_t repeat_buttons(const o3sdk::app::Context&) const noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& out) noexcept override;
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& help) const noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;

  // app.cpp's NOTE, INSTR, VOL and FX buttons (and the tests).
  void set_lane(o3sdk::app::Context& ctx, pattern::Lane lane) noexcept;
  pattern::Lane lane() const noexcept { return static_cast<pattern::Lane>(lane_); }
  unsigned cursor() const noexcept { return cursor_; }
  unsigned shown() const noexcept { return shown_; }
  void show(o3sdk::app::Context& ctx, unsigned p) noexcept { show_pattern(ctx, p); }  // FILE (U9c2): the pattern a loaded project was on
  bool follow() const noexcept { return follow_; }
  // M3 (main loop): a MIDI note of the carrier, recorded as the pads do (the note and its velocity as LEVEL, a single note). While the transport plays it
  // lands on the quantised step of the pattern that plays; stopped it is the cursor's step and the cursor moves on (midi_pattern_logic.h).
  void record_midi(o3sdk::app::Context& ctx, const Vocoder::NoteIn& n) noexcept;

 private:
  void turn(o3sdk::app::Context& ctx, unsigned key, int delta) noexcept;
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  void show_pattern(o3sdk::app::Context& ctx, unsigned p) noexcept;
  void pad(o3sdk::app::Context& ctx, unsigned pad, bool shift) noexcept;
  void write_palette(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  PatternData& pattern_now() const noexcept { return bank_->pattern[shown_ & 7u]; }

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  Bank* bank_ = nullptr;
  StepSeq* seq_ = nullptr;
  PerformScreen* perform_ = nullptr;
  SaveFn save_ = nullptr;
  void* save_user_ = nullptr;
  const char* note_ = nullptr;  // a moment's line (a static string), until note_until_
  std::uint64_t note_until_ = 0;
  std::uint8_t cursor_ = 0;  // 0..15
  std::uint8_t lane_ = 0;    // pattern::Lane
  std::uint8_t focus_ = 4;   // the F key the encoder drives (F5, VALUE)
  std::uint8_t shown_ = 0;   // the pattern on the screen, 0..7
  bool follow_ = false;
  bool key_tap_ = false;     // turn() came from a second tap on an F key, not from the encoder
};

}  // namespace o3_voc03er
