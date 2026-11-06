// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The screens of docs/UI-DESIGN.md that a later slice draws (SAMPLE, PATTERN, FILE, MASTER, SONG ...), as ONE object: the screen buttons open
// it (app.cpp) with the row of the screen they name, so the SRAM does not grow per screen. It draws OR NOIR's header and an empty F row, the
// screen's name, and the slice that will build it; its HELP says the same. A stub has no state and no sound; every other button is the app's.

#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_voc03er {

// One row of the table the app keeps in flash: what the screen is called, which lane it opens on (null: none), the slice that builds it, and
// the line HELP gives. `exact`: the one line drawn and nothing else (SONG's, section 3 of the design).
struct StubInfo {
  const char* name;
  const char* detail;
  const char* slice;
  const char* exact;
  const char* summary;
};

class StubScreen final : public o3sdk::app::Screen {
 public:
  void init(o3sdk::dsp::UnitId voice) noexcept { voice_ = voice; }
  void set(const StubInfo* info) noexcept { info_ = info; }
  const StubInfo* info() const noexcept { return info_; }

  const char* title() const noexcept override;
  o3sdk::app::Chrome chrome(const o3sdk::app::Context&) const noexcept override { return o3sdk::app::Chrome::None; }
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context&) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& help) const noexcept override;

 private:
  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  const StubInfo* info_ = nullptr;
};

}  // namespace o3_voc03er
