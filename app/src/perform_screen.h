// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PERFORM, the home screen (docs/UI-DESIGN.md), in the OR NOIR skin (skin/ornoir.h): what speaks (the audio input) on the left, the voiceprint
// of the vocoder's 16 bands in the middle, what sounds (the carrier) on the right, and the pads as a chord grid under them.
//
//   F1 MODE  F2 SPEAKS  F3 SOUNDS  F4 KEY  F5 SCALE  F6 ROW  F7 PLAYS  F8 by mode (BANDS and ROBOT SPREAD, TALK QUALITY, TUNE SPEED, TRACT VOWEL)
//   A tap on an F key points the encoder at it (the key stays lit); a second tap steps it on. The encoder changes the value. F6 steps on its first tap,
//               F2, F3 and F7 open their list on it.
//   F2, F3      a tap opens a list over the column (SPEAKS: sources and slots; SOUNDS: SYNTH, NOISE, LIVE, sources, slots): UP / DOWN or the encoder move the
//               cursor, the key again or F1 takes the row, F8 backs out; F4..F7, SHIFT, PERFORMANCE and the screen buttons close it, the pads sound as ever.
//               A dimmed row (a second LIVE source) arms a capture. REC records the live source for two bars into the first empty slot, which then takes
//               the side that held LIVE (route.h; the capture is a state machine run by update()).
//   pads          row 0 is KEYS' controls in every layout (OCT, VOL, ROOT, MOD, BND); rows 1, 2 and 3 are the layout's A, B and C (pads_logic.h), each with its PLAYS
//                 value: a note, a chord kind, OFF, on C the FORMANT strip, the BEND strip or the MOUTH strip (TRACT: nine vowel pads and the closures LIPS, TIP,
//                 BACK), or a PARAMETER STRIP on any row (strip_logic.h: twelve pads over one parameter's range, or a closure held while pressed). There is one
//                 layout per mode (BANDS, TALK, TUNE, ROBOT, TRACT), kept when the mode changes and saved in the project. F6 ROW picks
//                 A, B or C (every tap steps and wraps, the encoder and LEFT / RIGHT stop at the ends, UP goes to the row above and DOWN to the row below; the
//                 row's LEDs flash once and its name beside the pads stays marked while F6 or F7 has the focus). F7 PLAYS acts on that row and no other: a tap
//                 opens the row's list over the SOUNDS column (the notes values, then the mode's parameter strips under their group headings, only what acts in
//                 this mode), the cursor on the value in force; UP / DOWN or the encoder move it, SHIFT + the encoder jumps a group, F7 again or F1 takes it,
//                 F8 backs out. A row above plays one octave higher per playing row below it
//   bend          PERFORMANCE while PERFORM is up frees the encoder (no F key hot): it bends the carrier, range / 8 a detent, and springs back to 0
//                 about 120 ms after the last one. A tap on an F key takes the encoder back. MIDI pitch bend adds to it (set_wheel)
//   SHIFT row     while SHIFT is held the F row shows a second row (FREEZE, GATE, ARP, GLIDE, FORMANT, THRU, OCTAVE, PRESET); SHIFT + F key focuses
//                 its value, shown while it has the focus; a plain F tap goes back (perform_logic.h kShiftKeys)
//   SHIFT         latches the held pad's note or chord (it keeps sounding); DELETE releases it
//   LEFT / RIGHT  one step down / up of the focused F value (the encoder's detent; they repeat when held); UP / DOWN the same when no list is open (on F6
//                 UP is the row above, DOWN the row below, as the pads lie)
//   PLAY          the transport (the app's); the other screen buttons open their screen (app.cpp), MODE is F1
//
// Everything runs in the main loop (o3-sdk app/screen.h). The tables (labels, the chord of each pad, the peak holds) are one O3SDK_BIG struct
// in perform_screen.cpp; the screen keeps a cursor and a few flags in the SRAM.

#include "pads_logic.h"
#include "perform_logic.h"
#include "skin/ornoir.h"
#include "strip_logic.h"

#include <o3_voc03er/input.h>
#include <o3_voc03er/modes.h>
#include <o3_voc03er/pad_map.h>
#include <o3_voc03er/presets.h>
#include <o3_voc03er/route.h>

#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_voc03er {

class StepSeq;

// The header's pair, as every screen shows it: the slot or the live source that speaks, and the slot, the live source or the carrier that sounds.
// `live` is the live source's label and `usb` whether it is the USB in.
void header_route(const o3sdk::dsp::Engine& e, o3sdk::dsp::UnitId voice, const char* live, bool usb, ornoir::HeaderState& h) noexcept;

class PerformScreen final : public o3sdk::app::Screen {
 public:
  // The app switches the audio input for the screen (a pick in the SPEAKS or SOUNDS list, the end of a capture): it owns the input and its meter
  // (app.cpp's select_source).
  using SourceStep = void (*)(void* user, o3sdk::hal::capture::Source src, unsigned gain) noexcept;

  // `tract` is the tract unit (TRACT mode's mouth: F8 VOWEL and row C set to MOUTH write its parameters, the voiceprint reads them); kNoUnit: none.
  void init(o3sdk::dsp::UnitId voice, o3sdk::dsp::UnitId tract, const InputMeter* meter, const Vocoder* voc, SourceStep step, void* user) noexcept;

  // The step sequencer hook (V3): while a closure pad of the mouth strip is held the hook writes no mouth events (it sets the hook's relaxed flag). Null: none.
  void set_mouth_hold_sink(StepSeq* seq) noexcept { seq_ = seq; }

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
  // Every pass of the main loop, from App::update, whichever screen is up: advances a REC capture (a screen button can leave PERFORM at any moment).
  void service(o3sdk::app::Context& ctx) noexcept;

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

  // SAMPLE REC plays the pads and moves a slot with this screen's code: a pad press or release of `in`, the route the engine holds, and a route
  // change that stops or starts the slots it frees or takes (set_route).
  void pad_event(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  route::Route route_now(const o3sdk::app::Context& ctx) const noexcept;
  void apply_route(o3sdk::app::Context& ctx, route::Side side, route::Route from, route::Route to) noexcept { set_route(ctx, side, from, to); }

  void step_octave(o3sdk::app::Context& ctx, int delta) noexcept;

  // PARAMS draws PERFORM's voiceprint small: the sixteen bands followed as draw() follows them (same smoothing, held peaks, the busy rule),
  // copied out, 0..255 each. Call once a frame, from the main loop, on the screen that shows it.
  void follow_bands(o3sdk::app::Context& ctx, std::uint8_t (&level)[16], std::uint8_t (&peak)[16]) noexcept;

  // TALK and TUNE: fills what `vp` needs for the mode in force (kind, the envelope, the ladder) from the unit's View; nothing in the other modes.
  // The envelope is recomputed at most every 40 ms (80 above 60 % CPU). Call once a frame from the main loop, when `vp.bars` is false.
  void follow_print(o3sdk::app::Context& ctx, ornoir::Voiceprint& vp) noexcept;

  int octave() const noexcept { return octave_; }
  // The chords' voicing (0..3, the Voicing enum; SMOOTH by default): PARAMS' VOICING key sets it, the chord pads play it and the pattern records it.
  // Main loop only. Saved with the octave (the config file).
  unsigned voicing() const noexcept { return voicing_; }
  void set_voicing(unsigned v) noexcept { voicing_ = v > 3u ? 3u : v; }
  // FILE (U9c2) restores these: the octave is clamped as the keys clamp it.
  void set_octave(int o) noexcept { octave_ = clamp_octave(o); }
  // FILE (P3) restores these too. The layouts (one per mode) are taken as they are (the caller has checked them with strip::layout_valid; PERFORM is
  // not on screen, so no pad is held); the preset is an index of presets.h, anything past the table is none (the window shows ---).
  void set_layouts(const pads::Layout (&l)[kModeCount]) noexcept {
    for (unsigned m = 0; m < kModeCount; ++m) {
      layouts_[m] = l[m];
    }
  }
  const pads::Layout (&layouts() const noexcept)[kModeCount] { return layouts_; }
  unsigned preset() const noexcept { return preset_; }
  void set_preset(unsigned index) noexcept { preset_ = index < kPresetCount ? index : perform::kNoPreset; }
  // TRACT's own table (kTractPresets): SHIFT + F8 in TRACT picks from it and keeps its index apart from the vocoder's. `preset_is_tract()` says which of
  // the two the project's one saved name belongs to (the last SHIFT + F8 choice); FILE's load sets both, at most one of them is not none.
  unsigned tract_preset() const noexcept { return tract_preset_; }
  void set_tract_preset(unsigned index) noexcept { tract_preset_ = index < kTractPresetCount ? index : perform::kNoPreset; }
  bool preset_is_tract() const noexcept { return preset_is_tract_; }
  void set_preset_is_tract(bool t) noexcept { preset_is_tract_ = t; }
  // FILE's load marks the slots whose WAV is still on its way (bit n: slot n): PERFORM and FILE draw them grey (ornoir::kSlotWaiting). Main loop only.
  void set_slots_waiting(unsigned mask) noexcept { slots_waiting_ = static_cast<std::uint8_t>(mask & 0xFu); }
  unsigned slots_waiting() const noexcept { return slots_waiting_; }
  unsigned focus() const noexcept { return focus_; }
  // The pads' layout (docs/REVIEW-2026-11-11.md section 3): what rows A, B and C play, the current mode's of the five (`layouts()`). PERFORM's own pads
  // (rows 1, 2 and 3) follow it and nothing else does (strips live on PERFORM only). Main loop only. Saved in the project (PADS); init() sets the defaults
  // (strip::default_layout: one per mode, see strip_logic.h). The mode is read in service() (every main-loop pass),
  // in enter() and on a pad press. PERFORM's own F7 lets the held pads go first (take_plays).
  const pads::Layout& layout() const noexcept { return layouts_[mode_]; }
  unsigned layout_mode() const noexcept { return mode_; }
  // The notes palette (SP1): what the pad rows A and B play on PATTERN, SAMPLE, SAMPLE EDIT, FILE and SAMPLE LOAD, the same in every mode. Notes only
  // (pads::kOrderAB), default A NOTE, B TRIAD. It lives here only because this is the screen the others already reach; PERFORM never reads or writes it,
  // and a PERFORM layout change does not touch it. RAM, no chunk (docs/CHANGELOG.md, SP1): init(), a project load and F4 NEW put the defaults back (notes_reset). Main loop only.
  // notes_set is false, changing nothing, for a row or value the palette does not offer (row C, a strip).
  const pads::Layout& notes() const noexcept { return notes_; }
  void notes_reset() noexcept { notes_ = {}; }  // A NOTE, B TRIAD
  bool notes_set(pads::Row row, pads::Plays value) noexcept { return row != pads::Row::C && pads::set_plays(notes_, row, value); }
  // SAMPLE's pads of a parameter strip (rows A and B there): the same press and release as PERFORM's pad of that row and column. False when the row
  // does not hold a strip (nothing done).
  // `why`, when given, receives the text of a dark strip's refusal ("... IN <MODE>", valid until the next refusal) for the caller to show with its own say();
  // it stays null on any other press.
  bool strip_pad(o3sdk::app::Context& ctx, pads::Row row, unsigned col, unsigned pad_id, bool press, const char** why = nullptr) noexcept;
  // Every held closure (the MOUTH strip's and the CL strips', from PERFORM or SAMPLE) opens: CLOSE goes back to OPEN and the sequencer's mouth line is free.
  // A screen that plays strips calls it when it is left (SAMPLE's pad_leave); PERFORM's own release_all does it too.
  void release_closures(o3sdk::app::Context& ctx) noexcept;
  // The LED level of that pad (the value's pad on, the others dim, those past the choices dark; a held closure's pad on); dark for a row without a strip.
  std::uint8_t strip_led(const o3sdk::dsp::Engine& e, pads::Row row, unsigned col, unsigned pad_id) const noexcept;
  // The row F6 points at (A, B or C): the one F7 sets, whose LEDs flash and whose name is marked.
  pads::Row row() const noexcept { return row_; }
  int latched() const noexcept { return latched_; }
  // The audition (Q3): another screen (PATTERN) plays a palette pad through PERFORM's local-note path, so the sequencer's note-offs do not cut it.
  // `pad` 0..23 (it takes that pad's slot of the tables and its bit of held_; PERFORM is not on screen meanwhile, and its leave() lets the pads go),
  // `note` a MIDI note, `kind` 0 a single note, 1..4 a chord of that kind on the note (root pc = note % 12, the octave of kChordBaseNote, PERFORM's
  // voicing and the SMOOTH memory as press_pad uses them). No latch. Main loop only.
  void audition_press(o3sdk::app::Context& ctx, unsigned pad, unsigned note, unsigned kind, std::uint8_t velocity = kAuditionVelocity,
                      unsigned voicing = kCurrentVoicing) noexcept;
  void audition_release(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void audition_release_all(o3sdk::app::Context& ctx) noexcept;
  static constexpr std::uint8_t kAuditionVelocity = 110;
  static constexpr unsigned kCurrentVoicing = 4;  // audition_press: the chord uses PERFORM's voicing (0..3 is a voicing of its own, a step's)  // what PERFORM's pads play

  // V3: PATTERN's MOUTH palette sounds a mouth byte live in TRACT (a vowel sets VOWEL, a closure sets CLOSE until mouth_sound_release); nothing outside TRACT.
  // Main loop only; the sequencer's own line is not held back (the strip's closure pads do that).
  bool mouth_active(const o3sdk::dsp::Engine& e) const noexcept;  // the unit is in TRACT mode
  void mouth_sound(o3sdk::app::Context& ctx, std::uint8_t mouth) noexcept;
  void mouth_sound_release(o3sdk::app::Context& ctx) noexcept;
  // V3b: one point of a WORD's audition (stepseq.h kWords): SLIDE, then VOWEL, and CLOSE; `step_ms` is a sixteenth. mouth_word_end puts SLIDE back to 0.
  void mouth_word_point(o3sdk::app::Context& ctx, float step_ms, const WordPoint& w) noexcept;
  void mouth_word_end(o3sdk::app::Context& ctx) noexcept;

  // A refusal App::input makes for a key it handled itself (SAMPLE LOAD while FILE runs a job): the hint line's moment of text.
  void notice(o3sdk::app::Context& ctx, const char* text) noexcept { show_flash(ctx, text, ""); }

 private:
  void release_all(o3sdk::app::Context& ctx) noexcept;
  void press_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void release_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void latch(o3sdk::app::Context& ctx, bool on) noexcept;
  void turn(o3sdk::app::Context& ctx, int delta, bool coarse, bool tap = false) noexcept;
  void shift_key(o3sdk::app::Context& ctx, unsigned key) noexcept;
  void apply_bend(o3sdk::app::Context& ctx) noexcept;
  void apply_vib(o3sdk::app::Context& ctx) noexcept;
  void mouth_press(o3sdk::app::Context& ctx, unsigned pad, const perform::PadSpec& sp) noexcept;
  void mouth_release(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void close_press(o3sdk::app::Context& ctx, unsigned pad, unsigned close) noexcept;
  void param_press(o3sdk::app::Context& ctx, unsigned pad, const perform::PadSpec& sp, char* why = nullptr) noexcept;
  void sync_mode(o3sdk::app::Context& ctx) noexcept;
  pads::Layout& lay() noexcept { return layouts_[mode_]; }
  int strip_current(const o3sdk::dsp::Engine& e, const strip::StripDef& d) const noexcept;
  void write_close(o3sdk::app::Context& ctx) noexcept;
  void sync_mouth_hold() noexcept;
  void control_press(o3sdk::app::Context& ctx, unsigned pad, perform::PadFn fn) noexcept;
  void control_release(o3sdk::app::Context& ctx, unsigned pad, perform::PadFn fn) noexcept;
  void choose_row(o3sdk::app::Context& ctx, pads::Row row) noexcept;
  void step_focused(o3sdk::app::Context& ctx, int delta, bool coarse) noexcept;
  void show_flash(o3sdk::app::Context& ctx, const char* name, const char* value) noexcept;
  void refresh_labels(const o3sdk::dsp::Engine& e) noexcept;
  bool note_in_use(std::uint8_t note, unsigned except_pad) const noexcept;
  // The pickers and the capture (perform_screen.cpp). F7's list (pick_ 3) is the same picker over the SOUNDS column, its rows strip::list_rows.
  void open_picker(o3sdk::app::Context& ctx, route::Side side) noexcept;
  void open_plays(o3sdk::app::Context& ctx) noexcept;
  void build_plays(o3sdk::app::Context& ctx) noexcept;
  void take_plays(o3sdk::app::Context& ctx) noexcept;
  unsigned pick_key() const noexcept { return pick_ == 3u ? 6u : pick_; }  // the F key that opened the list (and takes its cursor)
  void close_picker(o3sdk::app::Context& ctx) noexcept;
  void build_picker(o3sdk::app::Context& ctx) noexcept;
  void take_choice(o3sdk::app::Context& ctx) noexcept;
  void set_route(o3sdk::app::Context& ctx, route::Side side, route::Route from, route::Route to) noexcept;
  void switch_source(unsigned live_index) noexcept;
  void rec_press(o3sdk::app::Context& ctx) noexcept;
  void capture_tick(o3sdk::app::Context& ctx) noexcept;
  void finish_capture(o3sdk::app::Context& ctx) noexcept;
  void abort_capture(o3sdk::app::Context& ctx, const char* why) noexcept;
  void loop_rec(o3sdk::app::Context& ctx, unsigned slot, bool on) noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId tract_ = o3sdk::dsp::kNoUnit;
  // The mouth strip's closure pads held (row C set to MOUTH, TRACT): pad bits and the order they went down in. Layout-independent, so a row changed
  // under a held pad still lets it go. Main loop only.
  std::uint64_t mouth_pads_ = 0;
  StepSeq* seq_ = nullptr;  // told whether a mouth pad is held (set_mouth_hold_sink)
  char why_[40] = {};  // strip_pad's refusal text
  perform::CloseHold mouth_;  // the held closures (the MOUTH strip's and the CL strips'), by pad
  const InputMeter* meter_ = nullptr;
  const Vocoder* voc_ = nullptr;
  SourceStep step_source_ = nullptr;
  void* source_user_ = nullptr;
  unsigned tract_preset_ = perform::kNoPreset;  // the last TRACT sound chosen (SHIFT + F8 in TRACT), or kNoPreset
  bool preset_is_tract_ = false;                // the last choice came from kTractPresets: it is the name a project saves
  unsigned preset_ = perform::kNoPreset;  // the last factory sound chosen (SHIFT + F8), or kNoPreset: the window shows ---
  unsigned sfocus_ = perform::kNoShiftFocus;  // the SHIFT-row value the encoder drives (1 GATE, 2 ARP, 3 GLIDE, 4 FORMANT, 5 THRU, 6 OCTAVE, 7 PRESET), or kNoShiftFocus; the row stays shown while set
  unsigned focus_ = 5;       // the F key the encoder drives (default F6, ROW); perform::kBendFocus (8): none, the encoder bends
  int octave_ = 0;
  unsigned voicing_ = 3;     // Voicing of the chords (Smooth)
  pads::Layout layouts_[kModeCount] = {};  // see layout(); init() fills the defaults
  pads::Layout notes_ = {};  // see notes(): rows A and B, notes only
  unsigned mode_ = 0;        // the mode whose layout is in force (sync_mode)
  pads::Row row_ = pads::Row::A;  // F6: the row F7's list sets, the only one (main loop only)
  std::uint64_t row_flash_until_ = 0;  // the chosen row's LEDs are dark until then, a short off-on when F6 changes the row (leds() checks it)
  std::uint8_t slots_waiting_ = 0;  // see set_slots_waiting
  unsigned strip_last_ = 0;  // the strip pad pressed last
  unsigned range_ = 2;       // BEND RANGE, semitones either way (2, 5, 12)
  float local_ = 0.0f;       // the encoder's and the strip's bend, semitones (main loop only)
  float wheel_ = 0.0f;       // MIDI pitch bend, semitones (main loop only); the unit's Bend is their sum, held to +-12
  std::uint64_t spring_at_ = 0;  // uptime_ms at which the encoder's bend returns to 0; 0 none
  // The held control pads of row 0 (MOD, BND-, BND+; bit pad), apart from held_ (they play no note). Main loop only.
  std::uint64_t ctl_held_ = 0;
  int bnd_dir_ = 0;                  // the time bend in force: -1 BND-, +1 BND+, 0 none (the last pressed of the strip and the BND pads wins)
  std::uint64_t bnd_at_[2] = {0, 0}; // uptime_ms at which BND- and BND+ were pressed
  bool mod_held_ = false;            // MOD down: the depth rises; up: it falls from mod_from_ since mod_t_
  std::uint64_t mod_t_ = 0;          // uptime_ms the depth's present ramp began (as if from 0 while rising)
  float mod_from_ = 0.0f;
  float vib_pad_ = 0.0f;             // the pad's vibrato depth 0..1
  unsigned cc1_ = 0;                 // the mod wheel, 0..127
  float vib_sent_ = 0.0f;            // the last Vib written (only a change is written)
  char flash_[40] = {};              // a moment's text on the hint line (VOL, ROOT, OCT), until flash_until_
  std::uint64_t flash_until_ = 0;
  // The pickers: 0 closed, 1 SPEAKS, 2 SOUNDS, 3 F7's PLAYS list for row_ (its rows are g_pd.pick). Main loop only.
  std::uint8_t pick_ = 0;
  std::uint8_t pick_cursor_ = 0;  // SPEAKS and SOUNDS: the row; PLAYS: the entry of strip::entry_at (headings never take it)
  // The capture (REC): Starting (Rec written, waiting for the unit to show it), Active (armed or recording). Main loop only, advanced by service() (App::update, every pass).
  enum class Cap : std::uint8_t { Idle, Starting, Active };
  Cap cap_ = Cap::Idle;
  std::uint8_t cap_slot_ = 0;
  bool cap_stop_ = false;      // Rec 0 has been written: the loop ends at its bar line
  bool cap_pre_ = false;       // armed by a dimmed source row: REC captures, then that source goes to cap_pre_side_
  std::uint8_t cap_pre_src_ = 0;   // the row's LiveSrc
  std::uint8_t cap_pre_side_ = 0;  // 1 SPEAKS, 2 SOUNDS: the side whose picker armed it
  std::uint64_t cap_t0_ = 0;
  int latched_ = -1;         // the latched pad, or -1
  std::uint64_t held_ = 0;   // the pads this screen holds notes for (bit pad)
  std::uint32_t label_sig_ = 0xFFFFFFFFu;  // what the labels were built for
  std::uint32_t frame_ = 0;
  mutable o3sdk::app::HelpPadGroup help_groups_[5] = {};
};

}  // namespace o3_voc03er
