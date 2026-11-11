// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PARAMS (docs/UI-DESIGN.md, "PARAMS"), in the OR NOIR skin: the unit's values in four pages of eight on the F row (CARRIER, SHAPE, TUNE, TRACT), with
// PERFORM's voiceprint small above them so a change is seen. The pages and the stepping are params_logic.h's.
//
//   A tap on an F key points the encoder at it (it stays lit); a second tap steps it on. SHIFT + the encoder: coarse steps.
//   PARAMS pressed again (app.cpp calls next_page()), DOWN: the next page; UP: the one before.
//   A key whose parameter the unit does not have yet is drawn dim with no value, and a tap says COMES LATER.
//
//   The TRACT page writes the tract unit's own parameters (Kind::Tract); it is live in every mode and says TRACT MODE ONLY outside TRACT.
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

  const char* title() const noexcept override { return "PARAMS"; }
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

  // Page `delta` pages on (round): PARAMS pressed on this screen, UP / DOWN. The encoder points at the page's first live key.
  void next_page(o3sdk::app::Context& ctx, int delta = 1) noexcept;

  unsigned page() const noexcept { return page_; }
  unsigned focus() const noexcept { return focus_; }

  // A refusal App::input makes for a key it handled itself (SAMPLE LOAD while FILE runs a job): said on this screen's own line.
  void notice(o3sdk::app::Context& ctx, const char* text) noexcept { say(ctx, text); }

 private:
  void step(o3sdk::app::Context& ctx, unsigned key, int delta, bool coarse, bool tap) noexcept;
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId tract_ = o3sdk::dsp::kNoUnit;
  PerformScreen* perform_ = nullptr;
  std::uint8_t page_ = 0;
  std::uint8_t focus_ = params::first_focus(0);  // the F key the encoder drives (always a live key)
  const char* note_ = nullptr;                   // a moment's line (a static string), until note_until_
  std::uint64_t note_until_ = 0;
};

}  // namespace o3_voc03er
