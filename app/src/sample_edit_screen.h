// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE EDIT (docs/UI-DESIGN.md, "SAMPLE EDIT"), first cut (slice U7a), in the OR NOIR skin: the selected slot as a bar the full width of its length,
// the cuts as numbered marks, the cursor as a bright line with its time under it. The overview waveform (U7b) sits in the rectangle above the bar.
//
//   Encoder: the cursor, one sample a detent (the kit's acceleration applies); SHIFT: a 64th of the slot. LEFT / RIGHT: the previous / next cut's
//   start. UP / DOWN: the slot (the one SAMPLE shows). INSERT: a cut at the cursor, snapped to a zero crossing within 64 samples. DELETE: the cut
//   that starts at the cursor (LEFT / RIGHT put the cursor on one). F1 CUT EVEN: OFF, 8, 16, 24: a tap points the encoder at it, a second tap
//   steps it, turning re-cuts the slot. SHIFT + F8 (CLR CUTS): one cut over the slot (F8 alone only says so). F2 CUT BEATS: OFF, 4, 8, 16 a bar, on the bars a take was recorded on (dim for
//   a loaded WAV), at most 24 cuts. F6 NORMALISE: the slot's peak to -1 dBFS, a slice of samples a pass. F3 CUT HITS: 0 to 100, the onset detector's sensitivity (0 off; a
//   second tap steps by 10): one scan of the slot, then the cuts follow the value from the stored hop energies. F7 SAVE WAV writes the slot to the card's Samples/ folder. F4 START and F5 END point the encoder at a trim bracket (SHIFT: coarse); with one moved off its end F1 TRIMS the slot to the
//   brackets (destructive, no undo; a slice of samples a pass, then Load). COPY copies the slot, cuts included, to the next empty slot. F6, SHIFT + DELETE
//   (empty the slot) and COPY ask; F1 confirms. A table the hand edited is replaced only by a second attempt (REPLACES n CUTS). The pads are SAMPLE's.
//
// SE1, beat slices by hand: a tap on a top pad picks that slice (and plays it, SAMPLE's pad), lit high; the picked slice takes the encoder ahead of the lit F key:
//   its start moves between its neighbours with the quiet-crossing snap (SHIFT: one sample a detent, no snap); slice 1's start is the trim START, the same
//   bracket. LEFT / RIGHT pick the previous / next slice. The same pad again, an F key or UP / DOWN lets it go (that key counts as a first tap). A pad past
//   the last slice splits nothing: INSERT does. The waveform zooms to about 0.5 s around the start (cached min / max columns). While something waits for its
//   confirm F1 reads CONFIRM (the F key a REPLACES warning waits on reads it instead), and a trim and a NORMALISE say NO UNDO.
// The cut tables are SAMPLE's (SampleScreen::table, in the SDRAM) and live in RAM until U9 saves them (SAMPLE LOAD, U8a, resets a slot's through slot_replaced). Main loop only; the cursor of each
// slot, the arm and the line are in the SDRAM (g_se in the .cpp), so this object is two pointers and the vtable.

#include "sample_screen.h"

#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>

#include <cstdint>

namespace o3_voc03er {

class SampleEditScreen final : public o3sdk::app::Screen {
 public:
  // `sample` is SAMPLE (the slot, the tables, the pads); `mem` the loop bank (slot n starts at n * Loop::kMaxSamples), or nullptr; read, and written only by NORMALISE.
  void init(SampleScreen* sample, std::int16_t* mem) noexcept;

  // A load (SAMPLE LOAD) put new samples into `slot`: its cached energies, beats, hits, brackets and cursor were the old samples'.
  static void slot_replaced(unsigned slot) noexcept;

  // The trim's brackets of `slot` (SAMPLE EDIT, F4 START and F5 END) for a project: `start`, `end` when a bracket is off its slot's end (`length` is the slot's), else
  // false and 0, 0. set_trim_brackets puts a saved pair back on a slot of that length (0, 0: back to the slot's own ends; false: it does not fit the slot, nothing changes). Main loop only.
  static bool trim_brackets(unsigned slot, std::uint32_t length, std::uint32_t& start, std::uint32_t& end) noexcept;
  static bool set_trim_brackets(unsigned slot, std::uint32_t length, std::uint32_t start, std::uint32_t end) noexcept;

  const char* title() const noexcept override { return "SAMPLE EDIT"; }
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

  // The cursor of the selected slot, in samples (the tests).
  std::uint32_t cursor() const noexcept;

  // A refusal App::input makes for a key it handled itself (SAMPLE LOAD while FILE runs a job): said on this screen's own line.
  void notice(o3sdk::app::Context& ctx, const char* text) noexcept { say(ctx, text); }

 private:
  void turn(o3sdk::app::Context& ctx, int delta) noexcept;
  void recut(o3sdk::app::Context& ctx, unsigned code) noexcept;
  void clear_cuts(o3sdk::app::Context& ctx) noexcept;
  void insert(o3sdk::app::Context& ctx) noexcept;
  void remove(o3sdk::app::Context& ctx) noexcept;
  bool refuse(o3sdk::app::Context& ctx, bool needs_cuts) noexcept;
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  void set_cursor(o3sdk::app::Context& ctx, std::uint32_t pos) noexcept;
  const std::int16_t* samples() const noexcept;
  void recut_beats(o3sdk::app::Context& ctx, unsigned code) noexcept;
  void turn_beats(o3sdk::app::Context& ctx, int delta) noexcept;
  void cut_hits(o3sdk::app::Context& ctx, unsigned slot, unsigned sens) noexcept;
  void recut_hits(o3sdk::app::Context& ctx, unsigned code) noexcept;
  void turn_hits(o3sdk::app::Context& ctx, int delta) noexcept;
  void scan_tick(o3sdk::app::Context& ctx) noexcept;
  bool beats_dim(const o3sdk::app::Context& ctx) const noexcept;
  void ask_normalise(o3sdk::app::Context& ctx) noexcept;
  void save_wav(o3sdk::app::Context& ctx) noexcept;
  void ask_empty(o3sdk::app::Context& ctx) noexcept;
  void confirm(o3sdk::app::Context& ctx) noexcept;
  void norm_tick(o3sdk::app::Context& ctx) noexcept;
  void norm_finish(o3sdk::app::Context& ctx) noexcept;
  bool busy(o3sdk::app::Context& ctx) noexcept;
  void fit_brackets(unsigned slot) noexcept;
  bool slot_stopped(unsigned slot) const noexcept;
  void send_load(o3sdk::app::Context& ctx) noexcept;
  void tap_bracket(o3sdk::app::Context& ctx, bool end_side, bool first) noexcept;
  void turn_bracket(o3sdk::app::Context& ctx, int delta, bool shift) noexcept;
  void run_trim(o3sdk::app::Context& ctx) noexcept;
  void ask_copy(o3sdk::app::Context& ctx) noexcept;
  unsigned copy_target() const noexcept;
  void run_copy(o3sdk::app::Context& ctx) noexcept;
  void job_tick(o3sdk::app::Context& ctx) noexcept;
  void job_finish(o3sdk::app::Context& ctx) noexcept;
  void warn_replaces(o3sdk::app::Context& ctx, unsigned key, unsigned count) noexcept;
  // SE1: one slice picked on its pad.
  unsigned selected() const noexcept;  // the picked slice's index plus one, 0 when none or when the table is no longer the one it was picked in
  void select_slice(o3sdk::app::Context& ctx, unsigned index) noexcept;
  void clear_sel(o3sdk::app::Context& ctx) noexcept;
  void turn_slice(o3sdk::app::Context& ctx, int delta, bool fine) noexcept;
  void step_slice(o3sdk::app::Context& ctx, int dir) noexcept;
  void zoom_refresh(unsigned slot, std::uint32_t centre) noexcept;
  void pad_tap(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void audition_pad(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;  // SE1: SAMPLE's pads, but pad 1 plays from the trim START
  bool trim_armed() noexcept;  // F1 would trim: a bracket is off its end and no slice is picked

  SampleScreen* sample_ = nullptr;
  std::int16_t* mem_ = nullptr;
};

}  // namespace o3_voc03er
