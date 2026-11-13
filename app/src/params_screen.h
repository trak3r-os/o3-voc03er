// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PARAMS (docs/UI-DESIGN.md, "PARAMS"), in the OR NOIR skin: the unit's values in four pages of eight on the F row (CARRIER, SHAPE, TUNE, TRACT), with
// PERFORM's voiceprint small above them so a change is seen. The pages and the stepping are params_logic.h's.
//
//   A tap on an F key points the encoder at it (it stays lit); a second tap steps it on. SHIFT + the encoder: coarse steps.
//   PARAMS pressed again (app.cpp calls next_page()), DOWN: the next page; UP: the one before.
//   A DIM key is not heard in this mode (liveness.h, the table the strips and the SHIFT row read): a tap on it still sets the value, for the modes that
//   hear it, and the line under the key's name says where ("HEARD IN TALK ONLY").
//   MIDI learn (ML1): SHIFT + an F key arms the kit's learn for that value (the next controller binds, the strip says LEARNED CC n); the same keys
//   again cancel. Held SHIFT turns the F row into LEARN cells ("-" where a value is not a unit parameter: VOICING). Leaving the page or the screen cancels.
//
//   The TRACT page writes the tract unit's own parameters (Kind::Tract); settable in every mode, dim outside TRACT, where the mouth speaks.
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
  void learn_key(o3sdk::app::Context& ctx, unsigned key) noexcept;
  void learn_drop(o3sdk::app::Context& ctx) noexcept;  // cancels this screen's arm (nothing else's)
  bool learn_on(const o3sdk::app::Context& ctx, unsigned key) const noexcept;  // armed here, on this page's key, and the kit still waiting

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId tract_ = o3sdk::dsp::kNoUnit;
  PerformScreen* perform_ = nullptr;
  std::uint8_t page_ = 0;
  std::uint8_t focus_ = params::first_focus(0);  // the F key the encoder drives (always a live key)
  const char* note_ = nullptr;                   // a moment's line (a static string), until note_until_
  std::uint64_t note_until_ = 0;
  bool learn_armed_ = false;  // this screen armed the kit's learn (main loop only); the kit says whether it still waits (a bound controller ends it)
  std::uint8_t learn_page_ = 0;
  std::uint8_t learn_key_ = 0;
};

}  // namespace o3_voc03er
