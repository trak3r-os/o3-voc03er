// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PATTERN (docs/UI-DESIGN.md "PATTERN"), in the OR NOIR skin: the pattern letter A..H large at the left, sixteen step cells with four lanes under each
// (NOTE, CARRIER, LEVEL, SLICE, MOUTH: what each step stores), the playhead in the voice ink, the cursor framed, the lane in force lit.
//
//   F1 PATTERN  F2 LANE  F3 LENGTH  F4 TEMPO  F5 VALUE  F6 ROW  F7 PLAYS  F8 SAVE      (SHIFT: F6 CLR LANE, F7 FOLLOW, F8 CAPTURE)
//   A tap on F1..F7 points the encoder at that key (it stays lit); a second tap steps it on. F6 ROW picks which of PERFORM's two upper pad rows (A or B)
//   F7 changes, F7 PLAYS is that row's value (NOTE, TRIAD, 7TH, OPEN, POWER, OFF or a piano value: pads::Layout, PERFORM holds it); both are dim off the
//   NOTE lane. SHIFT + F6 clears the lane of the pattern (F1 confirms), SHIFT + F7 toggles the cursor riding the playhead, F8 saves the bank like FILE does.
//   The encoder starts on F5, the cursor step's value in the lane. SHIFT + F8 CAPTURE (MOUTH lane only, mouth_capture.h): reads the slot SAMPLE has selected
//   and writes the nearest of TRACT's nine vowels (a closure where the level dips) into the MOUTH lane, a step at a time from update(); a second press
//   cancels, leaving the screen cancels, and nothing is written before the analysis ends.
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

  // loop_mem: the loop memory (Loop::kCount slots of Loop::kMaxSamples int16), which SHIFT + F8 CAPTURE reads; nullptr leaves CAPTURE with nothing to read.
  void init(o3sdk::dsp::UnitId voice, Bank* bank, StepSeq* seq, PerformScreen* perform, SaveFn save, void* user,
            SampleScreen* sample = nullptr, const std::int16_t* loop_mem = nullptr) noexcept;
  // The console's `mcap`: reads `slot` (0..3) to the end in this call and writes pattern's (0..7) MOUTH lane. nullptr when written, else the reason.
  const char* capture_now(o3sdk::app::Context& ctx, unsigned pattern, unsigned slot) noexcept;

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
  // V3b, the MOUTH lane (F6 SET flips the palette page, F7 FILL writes a random walk; main loop only).
  void fill_press(o3sdk::app::Context& ctx) noexcept;  // F7: fills, or asks first when the lane holds something that is not the last fill's own output
  void fill_now(o3sdk::app::Context& ctx) noexcept;
  void word_start(o3sdk::app::Context& ctx, unsigned word) noexcept;  // a WORD pad sounds: its points are played from update() (word_pump)
  void word_pump(o3sdk::app::Context& ctx) noexcept;
  void word_end(o3sdk::app::Context& ctx) noexcept;
  // MC1, SHIFT + F8 on the MOUTH lane: capture_press starts (or cancels) the analysis of the selected slot, capture_pump is one pass of it from update(),
  // capture_finish writes the lane. The Capture itself is one object in the SDRAM (g_cap in the .cpp). Main loop only.
  const char* capture_begin(o3sdk::app::Context& ctx, unsigned pattern, unsigned slot) noexcept;  // nullptr: running; else why not
  void capture_press(o3sdk::app::Context& ctx) noexcept;
  void capture_pump(o3sdk::app::Context& ctx) noexcept;
  void capture_finish(o3sdk::app::Context& ctx) noexcept;
  bool capture_slot_same() const noexcept;
  pattern::MouthPage mouth_page() const noexcept { return static_cast<pattern::MouthPage>(page_); }
  const pads::Layout& layout_now() const noexcept;  // PERFORM's (the default one when no PERFORM is wired)
  PatternData& pattern_now() const noexcept { return bank_->pattern[shown_ & 7u]; }

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  Bank* bank_ = nullptr;
  StepSeq* seq_ = nullptr;
  PerformScreen* perform_ = nullptr;
  SampleScreen* sample_ = nullptr;  // the SLICE audition fires a slice as SAMPLE's pads do
  const std::int16_t* loop_mem_ = nullptr;  // CAPTURE reads the selected slot from it (read only)
  std::uint8_t cap_pattern_ = 0;  // the pattern a running capture writes (the one shown when it started)
  std::uint8_t cap_slot_ = 0;     // the slot it reads
  std::uint32_t cap_length_ = 0;  // that slot's length when it started (a change ends the capture)
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
  std::uint8_t armed_ = 0;   // waiting for F1 (main loop only): 0 nothing, kArmLane CLEAR LANE, kArmPattern SHIFT + DELETE, kArmFill FILL over a lane that holds something
  std::uint8_t page_ = 0;    // F6 SET on the MOUTH lane: the palette's page, pattern::MouthPage (VOWELS 0, WORDS 1)
  // FILL (F7): the screen's count of fills (it seeds them with the pattern's index, so every press differs) and, per pattern, the 16 bytes the last fill left,
  // which is how a second press tells its own output (no question asked) from a lane the hands wrote (F1 confirms). Main loop only.
  std::uint32_t fill_count_ = 0;
  std::uint8_t fill_has_ = 0;  // bit pattern: fill_out_[pattern] holds a fill's output
  std::uint8_t fill_out_[kBankPatterns][kPatternSteps] = {};
  // The WORD audition (a WORD pad pressed in TRACT): the word, the next point to play and the uptime the pad was pressed; main loop only (update() plays it).
  std::uint8_t word_ = 0xFF;  // 0xFF: none
  std::uint8_t word_next_ = 0;
  std::uint64_t word_t0_ = 0;
  bool wob_said_ = false;  // the strip said "WOB PLAYS WITH THE LINE" since the screen was entered
  bool key_tap_ = false;     // turn() came from a second tap on an F key, not from the encoder
};

}  // namespace o3_voc03er
