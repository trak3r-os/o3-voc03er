// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FILE (docs/UI-DESIGN.md, "FILE"), slice U9c2, in the OR NOIR skin: the project file (`project.o3a` of a project folder of Apps/o3_voc03er/), without the WAVs.
//
//   F1 LOAD (asks, then the kit's picker lists the project folders), F2 SAVE (the current project's folder, the old file kept as .bak; with no project it is
//   SAVE AS), F3 SAVE AS (the kit's NameEntry, a new folder: a name that exists is refused, never overwritten), F4 NEW (asks), F5 SAVE WAVS (dim: COMES LATER),
//   F8 BACK. Refused while a card job or a take runs. A project is explicit: the screen never saves by itself (pattern.o3a and config.o3a keep their own paths).
//
//   LOAD reads the file whole and decodes it into a copy: a damaged file changes nothing. Then it sends the unit's values a few a frame through the normal
//   parameter path, and last the patterns, the route, the slots' cuts (only onto a slot that already holds audio of the record's length: the rest of the records
//   are kept for the next save and the next slice) and STAT's tempo, pattern, octave, chord kind and pads mode. NEW restores the defaults the same way and leaves
//   no current project; the slots' audio stays (the line says so).
// Main loop only; the state and the buffers are in the SDRAM (g_fd, g_proj, g_fbuf in the .cpp).

#include "pattern_screen.h"
#include "perform_screen.h"
#include "sample_screen.h"

#include <o3sdk/app/file_picker.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/name_entry.h>
#include <o3sdk/app/screen.h>

#include <cstdint>

namespace o3_voc03er {

class FileScreen final : public o3sdk::app::Screen {
 public:
  FileScreen() noexcept;

  void init(o3sdk::dsp::UnitId voice, SampleScreen* sample, PerformScreen* perform, PatternScreen* pattern, Bank* bank) noexcept;
  // The screen F8 returns to (the one that opened this); nullptr (and the default) is PERFORM.
  void set_back(o3sdk::app::Screen* back) noexcept { back_ = back; }

  const char* title() const noexcept override { return "FILE"; }
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

  // True while a save, a load or a restore is in hand; the current project's name ("" for none or the app's own folder), the line on the strip (the tests).
  bool busy() const noexcept;
  bool has_project() const noexcept;
  const char* project_name() const noexcept;
  const char* note() const noexcept;

 private:
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  bool refuse(o3sdk::app::Context& ctx) noexcept;
  void open_picker(o3sdk::app::Context& ctx) noexcept;
  void start_save(o3sdk::app::Context& ctx, const char* folder, bool as_new) noexcept;
  void start_load(o3sdk::app::Context& ctx, const char* folder) noexcept;
  void start_new(o3sdk::app::Context& ctx) noexcept;
  void capture(o3sdk::app::Context& ctx) noexcept;
  void tick(o3sdk::app::Context& ctx) noexcept;
  void apply(o3sdk::app::Context& ctx) noexcept;
  void apply_route(o3sdk::app::Context& ctx) noexcept;
  void apply_slots(o3sdk::app::Context& ctx) noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  SampleScreen* sample_ = nullptr;
  PerformScreen* perform_ = nullptr;
  PatternScreen* pattern_ = nullptr;
  Bank* bank_ = nullptr;
  o3sdk::app::Screen* back_ = nullptr;
  o3sdk::app::FilePicker picker_;
  o3sdk::app::NameEntry names_;
};

}  // namespace o3_voc03er
