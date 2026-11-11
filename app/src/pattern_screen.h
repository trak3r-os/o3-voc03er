// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PATTERN (docs/UI-DESIGN.md "PATTERN"), in the OR NOIR skin: the pattern letter A..H large at the left, sixteen step cells with four lanes under each
// (NOTE, CARRIER, LEVEL, SLICE, MOUTH: what each step stores), the playhead in the voice ink, the cursor framed, the lane in force lit.
//
//   F1 PATTERN  F2 LANE  F3 LENGTH  F4 TEMPO  F5 VALUE  F6 ROW  F7 PLAYS  F8 SAVE      (SHIFT: F6 CLR LANE, F7 FOLLOW)
//   A tap on F1..F7 points the encoder at that key (it stays lit); a second tap steps it on. F6 ROW picks which of PERFORM's two upper pad rows (A or B)
//   F7 changes, F7 PLAYS is that row's value (NOTE, TRIAD, 7TH, OPEN, POWER, OFF or a piano value: pads::Layout, PERFORM holds it); both are dim off the
//   NOTE lane. SHIFT + F6 clears the lane of the pattern (F1 confirms), SHIFT + F7 toggles the cursor riding the playhead, F8 saves the bank like FILE does.
//   The encoder starts on F5, the cursor step's value in the lane.
//   UP / DOWN: the octave (PERFORM's and SAMPLE's, one press one step, the palette's notes move by 12). LEFT / RIGHT: the cursor (they repeat when
//   held). NOTE, INSTR, VOL, FX and F2: the lane (app.cpp opens the screen on it; FX: SLICE, then MOUTH, then SLICE).
//   Pads: rows 2 and 3, columns 0..7: steps 1..16 (a press moves the cursor; SHIFT + press writes a rest in the lane); columns 8..11: patterns A..H (the
//   one shown, and the next to play: it starts when the playing one ends). Rows 0 and 1: the lane's palette (NOTE: the pads of PERFORM's rows A and B, what
//   they play there, at PERFORM's octave, lit like PERFORM's; CARRIER: FOLLOW, SYNTH, NOISE, SLOT 1..4; LEVEL: 24 steps up to 100; SLICE: the SPEAKS
//   slot's cuts 1..24, lit up to its count; MOUTH: row 0 PERFORM's mouth strip, the nine vowels then LIPS, TIP, BACK, row 1 the vowels with GLD, the
//   slide into them; the lane speaks in TRACT only, dim elsewhere, and a pad sounds the mouth live there). A palette pad writes into the cursor's step and moves the cursor on; a pad that plays nothing writes nothing.
//   On the NOTE lane a palette pad also sounds while it is held (PERFORM's local-note path, so the bar's note-offs do not cut it) and is lit; SHIFT +
//   a palette pad sounds and writes nothing. With one or more step pads held a palette pad writes every held step and the cursor stays.
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

class SampleScreen;
class SongScreen;

class PatternScreen final : public o3sdk::app::Screen {
 public:
  // F8: the app's save of the bank (the same job FILE starts); it says what it did through the screen's line when it cannot (the card is busy).
  using SaveFn = bool (*)(void* user, o3sdk::app::Context& ctx) noexcept;

  void init(o3sdk::dsp::UnitId voice, Bank* bank, StepSeq* seq, PerformScreen* perform, SaveFn save, void* user,
            SampleScreen* sample = nullptr) noexcept;

  const char* title() const noexcept override { return "PATTERN"; }
  o3sdk::app::Chrome chrome(const o3sdk::app::Context&) const noexcept override { return o3sdk::app::Chrome::None; }
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context&) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;  // a palette pad still held lets go (a screen button can leave at any moment)
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
  void show(o3sdk::app::Context& ctx, unsigned p) noexcept { show_pattern(ctx, p, false); }  // FILE (U9c2): the pattern a loaded project was on
  // LE3: the SONG screen, whose chain owns what plays while it walks: a pattern picked by hand then only changes what PATTERN shows and edits.
  void set_song(const SongScreen* song) noexcept { song_ = song; }
  bool follow() const noexcept { return follow_; }
  void set_follow(bool on) noexcept { follow_ = on; }  // FILE (P3) puts a loaded project's FOLLOW back
  // M3 (main loop): a MIDI note of the carrier, recorded as the pads do (the note and its velocity as LEVEL, a single note). While the transport plays it
  // lands on the quantised step of the pattern that plays; stopped it is the cursor's step and the cursor moves on (midi_pattern_logic.h).
  void record_midi(o3sdk::app::Context& ctx, const Vocoder::NoteIn& n) noexcept;

  // A refusal App::input makes for a key it handled itself (SAMPLE LOAD while FILE runs a job): said on this screen's own line.
  void notice(o3sdk::app::Context& ctx, const char* text) noexcept { say(ctx, text); }

 private:
  void turn(o3sdk::app::Context& ctx, unsigned key, int delta) noexcept;
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  void lane_chosen(o3sdk::app::Context& ctx) noexcept;  // the lane changed: the MOUTH lane outside TRACT says where it speaks
  bool tract_on(const o3sdk::app::Context& ctx) const noexcept;  // the vocoder is in TRACT mode
  void show_pattern(o3sdk::app::Context& ctx, unsigned p, bool by_hand) noexcept;  // by_hand: a chain that plays keeps its own request
  void pad(o3sdk::app::Context& ctx, unsigned pad, bool shift) noexcept;
  void write_palette(o3sdk::app::Context& ctx, unsigned pad, bool shift) noexcept;
  void audition(o3sdk::app::Context& ctx, unsigned pad, unsigned note, unsigned kind, std::uint8_t velocity = PerformScreen::kAuditionVelocity,
                unsigned voicing = PerformScreen::kCurrentVoicing) noexcept;
  void audition_step(o3sdk::app::Context& ctx, unsigned pad, unsigned step) noexcept;  // LEVEL: the step's note or chord at the step's level
  void audition_slice(o3sdk::app::Context& ctx, unsigned pad) noexcept;                // SLICE: fires the slice on the SPEAKS slot
  const pads::Layout& layout_now() const noexcept;  // PERFORM's (the default one when no PERFORM is wired)
  PatternData& pattern_now() const noexcept { return bank_->pattern[shown_ & 7u]; }

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  Bank* bank_ = nullptr;
  StepSeq* seq_ = nullptr;
  PerformScreen* perform_ = nullptr;
  SampleScreen* sample_ = nullptr;  // the SLICE audition fires a slice as SAMPLE's pads do
  SaveFn save_ = nullptr;
  void* save_user_ = nullptr;
  const char* note_ = nullptr;  // a moment's line (a static string), until note_until_
  std::uint64_t note_until_ = 0;
  std::uint8_t cursor_ = 0;  // 0..15
  std::uint8_t lane_ = 0;    // pattern::Lane
  std::uint8_t focus_ = 4;   // the F key the encoder drives (F5, VALUE)
  const SongScreen* song_ = nullptr;  // read on the main loop only
  std::uint8_t shown_ = 0;   // the pattern on the screen, 0..7
  bool follow_ = false;
  std::uint8_t row_ = 0;     // F6: the row F7 changes, pads::Row A (0) or B (1) (the palette's two rows)
  std::uint64_t flash_until_ = 0;  // the chosen row's LEDs are off until then, a short off-on when F6 changes it (leds() checks it; main loop only)
  std::uint16_t steps_held_ = 0;  // the step pads held (bit step 0..15): a palette pad writes all of them and the cursor stays; main loop only
  char oct_say_[12] = {};         // "OCTAVE +1", the strip's line for UP and DOWN (say() keeps the pointer)
  std::uint32_t mouth_aud_ = 0;  // the MOUTH palette's closure pads held and sounding (bit pad; a subset of aud_); main loop only
  std::uint32_t aud_ = 0;    // the palette pads held and sounding (bit pad 0..23, PerformScreen::audition_press); main loop only
  std::uint8_t armed_ = 0;   // waiting for F1 (main loop only): 0 nothing, kArmLane CLEAR LANE, kArmPattern SHIFT + DELETE
  bool key_tap_ = false;     // turn() came from a second tap on an F key, not from the encoder
};

}  // namespace o3_voc03er
