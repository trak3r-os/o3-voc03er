// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE EDIT (docs/UI-DESIGN.md, "SAMPLE EDIT"), first cut (slice U7a), in the OR NOIR skin: the selected slot as a bar the full width of its length,
// the cuts as numbered marks, the cursor as a bright line with its time under it. The overview waveform (U7b) sits in the rectangle above the bar.
//
//   Encoder: the cursor, one sample a detent (the kit's acceleration applies); SHIFT: a 64th of the slot. LEFT / RIGHT: the previous / next cut's
//   start. UP / DOWN: the slot (the one SAMPLE shows). INSERT: a cut at the cursor, snapped to a zero crossing within 64 samples. DELETE: the cut
//   that starts at the cursor (LEFT / RIGHT put the cursor on one). F1 CUT EVEN: OFF, 8, 16, 24: a tap points the encoder at it, a second tap
//   steps it, turning re-cuts the slot. F8 CLR CUTS: one cut over the slot. F2 CUT BEATS: OFF, 4, 8, 16 a bar, on the bars a take was recorded on (dim for
//   a loaded WAV), at most 24 cuts. F6 NORMALISE: the slot's peak to -1 dBFS, a slice of samples a pass. F3 CUT HITS: 0 to 100, the onset detector's sensitivity (0 off; a
//   second tap steps by 10): one scan of the slot, then the cuts follow the value from the stored hop energies. F7 (SAVE WAV) is drawn dim and says COMES LATER. F4 START and F5 END point the encoder at a trim bracket (SHIFT: coarse); with one moved off its end F1 TRIMS the slot to the
//   brackets (destructive, no undo; a slice of samples a pass, then Load). COPY copies the slot, cuts included, to the next empty slot. F6, SHIFT + DELETE
//   (empty the slot) and COPY ask; F1 confirms. A table the hand edited is replaced only by a second attempt (REPLACES n CUTS). The pads are SAMPLE's.
//
// The cut tables are SAMPLE's (SampleScreen::table, in the SDRAM) and live in RAM until U8 and U9 save them. Main loop only; the cursor of each
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
  void ask_empty(o3sdk::app::Context& ctx) noexcept;
  void confirm(o3sdk::app::Context& ctx) noexcept;
  void norm_tick(o3sdk::app::Context& ctx) noexcept;
  void norm_finish(o3sdk::app::Context& ctx) noexcept;
  bool busy(o3sdk::app::Context& ctx) noexcept;
  void fit_brackets(unsigned slot) noexcept;
  bool slot_stopped(unsigned slot) const noexcept;
  void send_load(o3sdk::app::Context& ctx) noexcept;
  void tap_bracket(o3sdk::app::Context& ctx, bool end_side) noexcept;
  void turn_bracket(o3sdk::app::Context& ctx, int delta, bool shift) noexcept;
  void run_trim(o3sdk::app::Context& ctx) noexcept;
  void ask_copy(o3sdk::app::Context& ctx) noexcept;
  unsigned copy_target() const noexcept;
  void run_copy(o3sdk::app::Context& ctx) noexcept;
  void job_tick(o3sdk::app::Context& ctx) noexcept;
  void job_finish(o3sdk::app::Context& ctx) noexcept;

  SampleScreen* sample_ = nullptr;
  std::int16_t* mem_ = nullptr;
};

}  // namespace o3_voc03er
