// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PARAMS (docs/UI-DESIGN.md, "PARAMS"), in the OR NOIR skin: the in-depth editor of the mode PERFORM is in. It shows only the values that mode hears
// (params_logic.h's layout of the mode: one to four pages by meaning on the F row, a one-word tab each under PERFORM's small voiceprint, the title naming the mode;
// a key keeps its page and its cell whatever the carrier is).
//
//   A tap on an F key points the encoder at it (it stays lit); a second tap steps it on. SHIFT + the encoder: coarse steps.
//   PARAMS pressed again (app.cpp calls next_page()), DOWN: the next page; UP: the one before.
//   MODE is not here: PERFORM's F1 changes it. When the mode changes by any road (PERFORM, a preset, a project load, a MIDI program change), the
//   screen shows the new layout on its first page; the VOICE carrier only fills in SPEED's reserved cell on NOTES (nothing else moves). A learn armed
//   on a value no longer on the page is cancelled.
//   MIDI learn (ML1): SHIFT + an F key arms the kit's learn for that value (the next controller binds, the strip says LEARNED CC n); the same keys
//   again cancel. Held SHIFT turns the F row into LEARN cells ("-" where a value is not a unit parameter: VOICING). Leaving the page or the screen cancels.
//   The binding stays on the parameter (the table's coordinates), never on the key's place in a page.
//
//   TRACT's mouth values are the tract unit's own parameters (Kind::Tract); they appear on TRACT's pages only, where the mouth speaks.
//
// Every value goes through the engine's parameters, the ids and units PERFORM writes, so the two screens never disagree. The pads play PERFORM's
// chords (its code), as on SAMPLE REC. Main loop only; the state is a few bytes, nothing is big.

#include "params_logic.h"
#include "perform_screen.h"

#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_voc03er {

class ParamsScreen final : public o3sdk::app::Screen {
 public:
  // `tract` is the tract unit's id (the TRACT page); `perform` is the app's PERFORM (the pads and the voiceprint's bands).
  void init(o3sdk::dsp::UnitId voice, o3sdk::dsp::UnitId tract, PerformScreen* perform) noexcept;

  const char* title() const noexcept override { return "PARAMS"; }  // the screen draws the mode after it (draw)
  o3sdk::app::Chrome chrome(const o3sdk::app::Context&) const noexcept override { return o3sdk::app::Chrome::None; }
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context&) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& out) noexcept override;
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& help) const noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;

  // Page `delta` pages on (round, inside the mode's own pages): PARAMS pressed on this screen, UP / DOWN. The encoder points at the page's first key.
  void next_page(o3sdk::app::Context& ctx, int delta = 1) noexcept;

  // The page (a place in the mode's layout) and the F key the encoder drives. They follow the mode: read them after the screen has seen a frame
  // (update, input, draw), or call sync().
  unsigned page() const noexcept { return page_; }
  unsigned focus() const noexcept { return focus_; }

  // The layout of the engine's mode now (and the carrier source). Follows a change of either: first page, a valid focus, a learn on a value no longer
  // there dropped. Cheap (two parameter reads); every entry point calls it.
  const params::Layout& sync(o3sdk::app::Context& ctx) noexcept;

  // A refusal App::input makes for a key it handled itself (SAMPLE LOAD while FILE runs a job): said on this screen's own line.
  void notice(o3sdk::app::Context& ctx, const char* text) noexcept { say(ctx, text); }

 private:
  void step(o3sdk::app::Context& ctx, const params::Layout& lay, unsigned key, int delta, bool coarse, bool tap) noexcept;
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  void learn_key(o3sdk::app::Context& ctx, const params::Layout& lay, unsigned key) noexcept;
  void learn_drop(o3sdk::app::Context& ctx) noexcept;  // cancels this screen's arm (nothing else's)
  // armed here, on the value of this page's key `key`, and the kit still waiting
  bool learn_on(const o3sdk::app::Context& ctx, const params::Layout& lay, unsigned key) const noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId tract_ = o3sdk::dsp::kNoUnit;
  PerformScreen* perform_ = nullptr;
  std::uint8_t mode_ = 0;           // the mode the layout was taken for (sync)
  std::uint8_t page_ = 0;           // a page of that layout
  std::uint8_t focus_ = 0;          // the F key the encoder drives (always one the page shows)
  const char* note_ = nullptr;                   // a moment's line (a static string), until note_until_
  std::uint64_t note_until_ = 0;
  bool learn_armed_ = false;  // this screen armed the kit's learn (main loop only); the kit says whether it still waits (a bound controller ends it)
  std::uint8_t learn_page_ = 0;  // the armed value's place in params::kTable (the parameter's, not a place on the screen)
  std::uint8_t learn_key_ = 0;
};

}  // namespace o3_voc03er
