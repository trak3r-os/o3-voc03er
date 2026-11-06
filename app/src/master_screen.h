// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MASTER (docs/UI-DESIGN.md, "MASTER"), in the OR NOIR skin: the signal path drawn large with its one rule written on it. LIVE (SOURCE, GAIN,
// MONITOR, the tall IN meter with CLIP) > the mode > OUTPUT (DRY, LEVEL, RETURN, the tall OUT meter) and, with the radio as the source, FREQ.
//
//   F1 SOURCE  F2 GAIN  F3 MONITOR  F4 DRY  F5 LEVEL  F6 RETURN  F7 SEEK DOWN  F8 SEEK UP (the seek keys are dim unless the source is RADIO).
//   A tap on F1..F6 points the encoder at it (it stays lit); a second tap steps it on (MONITOR: off / on). SHIFT + the encoder: coarse steps.
//   A dim key says why when it is tapped. UP and DOWN are not used: MASTER has no pages.
//
// The source goes through the app's one source path (the callback app.cpp gives PERFORM: AudioIn::select, the meter's reset, the USB channel), so
// the kit's settling gate holds the input silent while it switches. GAIN, MONITOR and the radio are the kit's (AudioIn, Radio), RETURN is
// UsbAudio's, DRY and LEVEL are the unit's parameters as PARAMS writes them. Main loop only; the state is a few dozen bytes, nothing is big.

#include "master_logic.h"
#include "perform_screen.h"

#include <o3_voc03er/input.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_voc03er {

class MasterScreen final : public o3sdk::app::Screen {
 public:
  // `meter` is the app's input meter, `pick` its source path (PERFORM's callback).
  void init(o3sdk::dsp::UnitId voice, const InputMeter* meter, PerformScreen::SourceStep pick, void* user) noexcept;

  const char* title() const noexcept override { return "MASTER"; }
  o3sdk::app::Chrome chrome(const o3sdk::app::Context&) const noexcept override { return o3sdk::app::Chrome::None; }
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context&) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& out) noexcept override;
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& help) const noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;

  unsigned focus() const noexcept { return focus_; }

 private:
  master::Avail avail(const o3sdk::app::Context& ctx) const noexcept;
  // The value of key `k` in display units (a percent, tenths of a dB, a source's index).
  int read(const o3sdk::app::Context& ctx, unsigned k) const noexcept;
  void write(o3sdk::app::Context& ctx, unsigned k, int display) noexcept;
  void step(o3sdk::app::Context& ctx, unsigned k, int delta, bool coarse, bool tap) noexcept;
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  const InputMeter* meter_ = nullptr;
  PerformScreen::SourceStep step_source_ = nullptr;
  void* user_ = nullptr;
  master::Follower in_;   // the IN meter's fall and held peak (update(), main loop)
  master::Follower out_;  // the OUT meter's
  std::uint8_t focus_ = master::Source;  // the F key the encoder drives (F1..F6)
  const char* note_ = nullptr;           // a moment's line (a static string), until note_until_
  std::uint64_t note_until_ = 0;
};

}  // namespace o3_voc03er
