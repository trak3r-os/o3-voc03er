// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE LOAD (docs/UI-DESIGN.md, "SAMPLE LOAD"), slice U8a, in the OR NOIR skin: a WAV from the card (Apps/o3_voc03er/, `.wav` only) into a loop slot.
//
//   The slot is named at the top: UP / DOWN change it (the one SAMPLE and SAMPLE EDIT show). The kit's file picker lists the WAVs with their sizes (encoder,
//   LEFT / RIGHT a page, a pad a row). F1 OPEN loads the file under the cursor into the slot, F8 BACK returns to the screen this one was opened from.
//   PCM of 8, 16, 24 or 32 bits or 32-bit float, at 22.05 to 96 kHz, mono or stereo (the mean of the two): converted on the way in to the slot's mono 16-bit 44.1 kHz
//   (sample_load_logic.h, wav_convert.h); anything else, or a damaged file, is refused on the strip with its reason. A file over 16 s (after the conversion) gives its
//   first 16 s and the strip says so, and what was converted ("48K 24B TO 44.1K 16B"). A slot that is recording or sounds is refused (STOP THE SLOT FIRST); a slot that
//   holds a sample asks first (REPLACES SLOT n, a second F1 confirms, any other key cancels).
//
// The read is one of the kit's card jobs (Files::load into a sink), a slice of the main loop a pass; the sink walks the file's chunks and converts the data as it comes
// into a stage in the SDRAM (one loop of mono 16-bit), with a bounded amount of converter work a pass (the sink answers WouldBlock past it). The stage is checked, and written
// into the slot memory in bounded slices, a 64-sample fade at both ends. A slot that held a sample is
// cleared first (the unit's own Rec CLEAR, as SAMPLE EDIT's empty does), so a replacement of the same length is a new length to every screen too. Then Load
// goes to the unit and the screen waits for the slot to show the length. SAMPLE's service() then cuts the slot evenly (F2 dim: not whole bars, unless it is).
// Main loop only; the state is in the SDRAM (g_sl in the .cpp), so this object is the picker, the sink and a few pointers.

#include "sample_load_logic.h"
#include "sample_screen.h"

#include <o3sdk/app/file_picker.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>

#include <cstdint>

namespace o3_voc03er {

class SampleLoadScreen final : public o3sdk::app::Screen {
 public:
  SampleLoadScreen() noexcept;

  // `sample` is SAMPLE (the slot, the tables, the pads); `mem` the loop bank (slot n starts at n * Loop::kMaxSamples), or nullptr.
  void init(SampleScreen* sample, std::int16_t* mem) noexcept;
  // The screen F8 returns to (the one that opened this); nullptr (and the default) is SAMPLE.
  void set_back(o3sdk::app::Screen* back) noexcept { back_ = back; }

  const char* title() const noexcept override { return "SAMPLE LOAD"; }
  o3sdk::app::Chrome chrome(const o3sdk::app::Context&) const noexcept override { return o3sdk::app::Chrome::None; }
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context&) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& out) noexcept override;
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& help) const noexcept override;

  // True while a file is being read or written into a slot (the tests).
  bool loading() const noexcept;

  // FILE's entry (U9c3): the same read, clear, copy and wait for the unit, without this screen being up, of `name` in the project folder `folder` into `slot`. The driver
  // calls tick_external() once a pass and reads external(); Done and Failed stay until the next begin_external() or this screen's enter(). The slot must be stopped (no
  // take, no play); a slot that holds a sample is replaced without asking (a project load is the ask). Main loop only.
  enum class External : std::uint8_t { Idle, Running, Done, Failed };
  bool begin_external(o3sdk::app::Context& ctx, unsigned slot, const char* folder, const char* name) noexcept;
  void tick_external(o3sdk::app::Context& ctx) noexcept;
  External external() const noexcept;
  const char* external_text() const noexcept;  // why it failed (upper case, static or this screen's own buffer); "" otherwise
  // Gives the job up as leave() does: a read is cancelled, a copy in hand is finished and sent. Leaves external() Failed "CANCELLED".
  void abort_external(o3sdk::app::Context& ctx) noexcept;

 private:
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  void open_picker(o3sdk::app::Context& ctx) noexcept;
  void chosen(o3sdk::app::Context& ctx) noexcept;
  // What keeps the slot from taking a load (None: it may); the refusal line names it (sample_load::busy_text).
  sample_load::Busy busy(unsigned slot) const noexcept;
  void begin(o3sdk::app::Context& ctx) noexcept;
  void end_job(o3sdk::app::Context& ctx) noexcept;
  void tick(o3sdk::app::Context& ctx) noexcept;
  void tick_read(o3sdk::app::Context& ctx) noexcept;
  void tick_clear(o3sdk::app::Context& ctx) noexcept;
  void tick_copy(o3sdk::app::Context& ctx) noexcept;
  void tick_wait(o3sdk::app::Context& ctx) noexcept;
  void finish_copy(o3sdk::app::Context& ctx) noexcept;
  void send_load(o3sdk::app::Context& ctx) noexcept;
  void go_back(o3sdk::app::Context& ctx) noexcept;

  SampleScreen* sample_ = nullptr;
  std::int16_t* mem_ = nullptr;
  o3sdk::app::Screen* back_ = nullptr;
  o3sdk::app::FilePicker picker_;
};

}  // namespace o3_voc03er
