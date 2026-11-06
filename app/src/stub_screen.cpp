// SPDX-License-Identifier: GPL-3.0-or-later
#include "stub_screen.h"

#include "perform_logic.h"
#include "skin/ornoir.h"

#include <o3_voc03er/input.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/context.h>

#include <cstring>

namespace o3_voc03er {

using namespace o3sdk;
namespace pl = perform;
using ui::Color;
using ui::Face;

namespace {

int text_width(const char* s, int per_char) noexcept { return static_cast<int>(std::strlen(s)) * per_char; }

}  // namespace

const char* StubScreen::title() const noexcept { return info_ != nullptr ? info_->name : "SCREEN"; }

const ui::Theme* StubScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void StubScreen::enter(app::Context& ctx) noexcept { ctx.redraw(); }

void StubScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  const dsp::Engine& e = ctx.engine();
  const unsigned mode = pl::mode_index(e.param(voice_, Vocoder::Mode));
  const unsigned car = static_cast<unsigned>(e.param(voice_, Vocoder::CarSrc) + 0.5f);
  const app::AudioIn& ain = ctx.audio_in();
  const SourceName* src = ain.available() ? find_source(ain.source()) : nullptr;

  ornoir::HeaderState h;
  h.speaks = src != nullptr ? src->label : "NONE";
  h.sounds = pl::kCarrierNames[car < 9u ? car : 0u];
  h.mode = pl::kModeNames[mode];
  h.playing = e.playing();
  h.tempo_x10 = e.stats().tempo_mbpm / 100u;
  h.cpu_percent = ctx.cpu_percent();
  ornoir::FKey keys[ornoir::kFKeys];  // eight empty keys: the screen has no values yet

  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  if (info_ == nullptr) {
    return;
  }
  if (info_->exact != nullptr) {
    r.text(400 - text_width(info_->exact, 8) / 2, 220, info_->exact, Color::TextDim, Face::Label);
    return;
  }
  r.text(400 - text_width(info_->name, 8) / 2, 150, info_->name, Color::PlayText, Face::Label);
  if (info_->detail != nullptr) {
    r.text(400 - text_width(info_->detail, 8) / 2, 180, info_->detail, Color::Text, Face::Label);
  }
  r.text(400 - text_width(info_->slice, 8) / 2, 230, info_->slice, Color::TextDim, Face::Label);
}

void StubScreen::help(const app::Context&, app::Help& h) const noexcept {
  // Every button app.cpp's input() answers on a stub: a button with no row draws as a hollow cap, which would say it does nothing.
  static constexpr app::HelpButton kButtons[] = {
      {app::Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {app::Button::Params, "PARAMS: THREE PAGES", "THE OLD PLAY SCREEN"},
      {app::Button::Play, "START OR STOP THE BAR", nullptr},
      {app::Button::Master, "MASTER (SLICE U9)", nullptr},
      {app::Button::File, "FILE (SLICE U9)", nullptr},
      {app::Button::SampleLoad, "SAMPLE LOAD (SLICE U8)", nullptr},
      {app::Button::SamplePlay, "SAMPLE: THE SLOTS", nullptr},
      {app::Button::SampleEdit, "SAMPLE EDIT: THE CUTS", nullptr},
      {app::Button::SampleRec, "SAMPLE REC (SLICE U4)", nullptr},
      {app::Button::Song, "SONG (SLICE U9)", nullptr},
      {app::Button::Pattern, "PATTERN: THE 16 STEPS", nullptr},
      {app::Button::Note, "PATTERN: NOTE LANE", nullptr},
      {app::Button::Instr, "PATTERN: CARRIER LANE", nullptr},
      {app::Button::Vol, "PATTERN: LEVEL LANE", nullptr},
      {app::Button::Fx, "PATTERN: SLICE LANE", nullptr},
      {app::Button::Shift, "NO SHIFT KEYS ON THIS SCREEN", nullptr},
  };
  h.summary = info_ != nullptr ? info_->summary : "THIS SCREEN IS NOT DRAWN YET.";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
}

}  // namespace o3_voc03er
