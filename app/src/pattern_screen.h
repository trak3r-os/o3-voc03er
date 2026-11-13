// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PATTERN (docs/UI-DESIGN.md "PATTERN"), in the OR NOIR skin: the pattern letter A..H large at the left, sixteen step cells with five lanes under each
// (NOTE, CARRIER, LEVEL, SLICE, MOUTH: what each step stores), the playhead in the voice ink, the cursor framed, the lane in force lit.
//
// The model: pick below, put above (the house rule: the lower two pad rows are the keyboard, as SAMPLE's are; the top two rows are the screen's own things).
// A palette pad (pads 24..47, the lane's palette) sounds what it is while it is held and becomes the lane's HAND; it writes nothing by itself. A step pad
// (rows 0 and 1, columns 0..7) on an empty step puts the hand there and moves the cursor; on a full step that is not under the cursor it chooses it (the
// cursor goes there, it sounds, its value becomes the hand, nothing is written; a TIE leaves the hand alone); on a full step under the cursor it clears it
// (pattern_logic.h press_step). SHIFT + a step pad on the NOTE lane is a TIE (the step holds the notes before it; again ends it), on the other lanes it clears the
// step. With a step pad held a palette pad writes every held step at once (and is picked). REC (the button) arms writing from the palette pads: with the
// transport stopped a pad writes the cursor's step and the cursor moves on, while it plays the step that plays (midi_pattern_logic.h, the rule MIDI notes
// use); REC goes off when the screen is left. Each lane keeps its own hand (hand_[]); the NOTE hand moves with the octave.
//
//   F1 LENGTH  F2 TEMPO  F3 VALUE  F4 CHORD (MOUTH: SET)  F5 F6 F7 (blank)  F8 SAVE      (SHIFT: F5 blank, F6 CLR LANE, F7 FOLLOW, F8 CAPTURE)
//   A tap on F1..F3 points the encoder at that key (it stays lit); a second tap steps it on. F3 is lit when the screen is entered. F4 CHORD (NOTE lane) is the
//   palette's lower row: TRIAD, 7TH, OPEN, POWER (a tap steps and wraps, a detent stops at the ends); the upper row is always the notes, so the palette is
//   rows A NOTES and B CHORDS whatever PERFORM left (enter() and F4 write it). On the MOUTH lane F4 is SET, the palette's page (VOWELS or WORDS: an action,
//   no focus). On the other lanes F4 is blank. F5, F6 and F7 are blank in every lane and take no focus (the RANDOM keys will use them). SHIFT + F6 clears
//   the lane of the pattern (F1 confirms), SHIFT + F7 toggles the cursor riding the playhead, F8 saves the bank like FILE does. F3 changes the cursor step's value in
//   the lane (a TIE is left alone). SHIFT + F8 CAPTURE (MOUTH lane only, mouth_capture.h): reads the slot SAMPLE has selected and writes the nearest of
//   TRACT's nine vowels (a closure where the level dips) into the MOUTH lane, a step at a time from update(); a second press cancels, leaving the screen
//   cancels, and nothing is written before the analysis ends.
//   Arrows: LEFT / RIGHT one encoder step of the lit F key (they repeat when held); SHIFT + LEFT / RIGHT the cursor; UP / DOWN the lane (NOTE, CARRIER, LEVEL,
//   SLICE, MOUTH, no wrap); SHIFT + UP / DOWN the octave (PERFORM's and SAMPLE's, one press one step, the palette's notes move by 12). NOTE, INSTR, VOL and FX
//   also choose the lane (FX: SLICE, then MOUTH, then SLICE).
//   Pads: rows 0 and 1, columns 0..7 steps 1..16 (past LENGTH dark), columns 8..11 patterns A..H (the one shown, and the next to play: it starts when the playing
//   one ends). Rows 2 and 3: the lane's palette (NOTE: rows A and B of the notes palette, at PERFORM's octave, pad for pad what SAMPLE's lower rows play; CARRIER:
//   FOLLOW, SYNTH, NOISE, SLOT 1..4, LIVE; LEVEL: 24 steps up to 100 (127 in TRACT); SLICE: the SPEAKS slot's cuts 1..24, lit up to its count; MOUTH: row 0
//   PERFORM's mouth strip, the nine vowels then LIPS, TIP, BACK, row 1 the vowels with GLD, the slide into them; the lane speaks in TRACT only, dim elsewhere, and
//   a pad sounds the mouth live there). A pad that plays nothing is not picked. A pad sounds while it is held (PERFORM's local-note path, so the bar's note-offs
//   do not cut it); SHIFT + a palette pad sounds it and picks nothing. The palette is addressed by its index 0..23 (pt::palette_of_pad).
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
  unsigned mode_of(const o3sdk::app::Context& ctx) const noexcept;  // the vocoder's mode, 0..kModeCount - 1
  void say_heard(o3sdk::app::Context& ctx, std::uint8_t modes) noexcept;  // the line that says where `modes` hear a value ("HEARD IN TRACT ONLY")
  void show_pattern(o3sdk::app::Context& ctx, unsigned p, bool by_hand) noexcept;  // by_hand: a chain that plays keeps its own request
  void pad(o3sdk::app::Context& ctx, unsigned pad, bool shift) noexcept;
  void pick(o3sdk::app::Context& ctx, unsigned pad, bool shift) noexcept;  // a palette pad (the lower rows, by index): sounds, becomes the hand, writes held steps (and with REC the step it targets)
  void audition_chosen(o3sdk::app::Context& ctx, unsigned step) noexcept;
  void step_press(o3sdk::app::Context& ctx, unsigned step, bool shift) noexcept;  // a step pad: puts the hand, chooses a full step, clears the cursor's, or with SHIFT a TIE
  void normalise_palette() noexcept;  // rows A NOTES and B CHORDS (kept kind)
  void init_hands(const o3sdk::app::Context& ctx) noexcept;
  void step_octave_hand(o3sdk::app::Context& ctx, int delta) noexcept;
  // Where a recording write lands (REC pads, MIDI notes): the step that plays, or with the transport stopped the cursor's; rec_moved then moves the cursor.
  struct RecTarget {
    PatternData* p = nullptr;
    unsigned at = 0;
    bool at_cursor = true;
    unsigned pattern = 0;
  };
  RecTarget rec_target(bool playing, std::uint16_t edge) noexcept;
  void rec_moved(const RecTarget& t) noexcept;
  void audition(o3sdk::app::Context& ctx, unsigned pad, unsigned note, unsigned kind, std::uint8_t velocity = PerformScreen::kAuditionVelocity,
                unsigned voicing = PerformScreen::kCurrentVoicing) noexcept;
  void audition_step(o3sdk::app::Context& ctx, unsigned pad, unsigned step, std::uint8_t velocity) noexcept;  // LEVEL: the step's note or chord at `velocity`
  void audition_slice(o3sdk::app::Context& ctx, unsigned pad) noexcept;                // SLICE: fires the slice on the SPEAKS slot
  // V3b, the MOUTH lane (F4 SET flips the palette page; main loop only).
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
  const pads::Layout& layout_now() const noexcept;  // the notes palette (PERFORM::notes(); the default one when no PERFORM is wired)
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
  const char* note_ = nullptr;  // a moment's line (a static string, or where_), until note_until_
  char where_[40] = {};         // the "HEARD IN ..." line say_heard builds (main loop only)
  std::uint64_t note_until_ = 0;
  std::uint8_t cursor_ = 0;  // 0..15
  std::uint8_t lane_ = 0;    // pattern::Lane
  std::uint8_t focus_ = 2;   // the F key the encoder drives (F3, VALUE)
  const SongScreen* song_ = nullptr;  // read on the main loop only
  std::uint8_t shown_ = 0;   // the pattern on the screen, 0..7
  bool follow_ = false;
  // The hands, one per lane (pattern::Hand), and whether they have their first values yet (the first enter()). Main loop only.
  pattern::Hand hand_[pattern::kLanes] = {};
  bool hands_ready_ = false;
  bool rec_ = false;  // REC: the lower pads also write (off on leave); main loop only
  std::uint16_t steps_held_ = 0;  // the step pads held (bit step 0..15): a palette pad writes all of them and the cursor stays; main loop only
  std::uint16_t step_aud_ = 0;    // the chosen steps sounding under a held step pad (bit step, PerformScreen audition id = the step); main loop only
  char oct_say_[12] = {};         // "OCTAVE +1", the strip's line for UP and DOWN (say() keeps the pointer)
  std::uint32_t mouth_aud_ = 0;  // the MOUTH palette's closure pads held and sounding (bit pad; a subset of aud_); main loop only
  std::uint32_t aud_ = 0;    // the palette pads held and sounding (bit pad 0..23, PerformScreen::audition_press); main loop only
  std::uint8_t armed_ = 0;   // waiting for F1 (main loop only): 0 nothing, kArmLane CLEAR LANE, kArmPattern SHIFT + DELETE
  std::uint8_t page_ = 0;    // F4 SET on the MOUTH lane: the palette's page, pattern::MouthPage (VOWELS 0, WORDS 1)
  // The WORD audition (a WORD pad pressed in TRACT): the word, the next point to play and the uptime the pad was pressed; main loop only (update() plays it).
  std::uint8_t word_ = 0xFF;  // 0xFF: none
  std::uint8_t word_next_ = 0;
  std::uint64_t word_t0_ = 0;
  bool wob_said_ = false;  // the strip said "WOB PLAYS WITH THE LINE" since the screen was entered
  bool key_tap_ = false;     // turn() came from a second tap on an F key, not from the encoder
};

}  // namespace o3_voc03er
