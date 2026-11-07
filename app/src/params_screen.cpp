// SPDX-License-Identifier: GPL-3.0-or-later
#include "params_screen.h"

#include "skin/ornoir.h"

#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/context.h>
#include <o3sdk/app/pad_leds.h>

#include <cstring>

namespace o3_voc03er {

using namespace o3sdk;
namespace pr = params;
using ui::Color;
using ui::Face;
using Button = app::Button;

namespace {

constexpr std::uint64_t kNoteMs = 2000;
constexpr int kTabsY = 224;
constexpr int kVpX = 26, kVpY = 78, kVpW = 748, kVpH = 128;

int centred(ui::Renderer& r, const char* s, Face f, int cx) noexcept { return cx - r.text_width(s, f) / 2; }

int read(const dsp::Engine& e, dsp::UnitId id, const pr::KeyDef& k) noexcept { return pr::to_display(k.fmt, e.param(id, k.param)); }

}  // namespace

void ParamsScreen::init(dsp::UnitId voice, PerformScreen* perform) noexcept {
  voice_ = voice;
  perform_ = perform;
}

const ui::Theme* ParamsScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void ParamsScreen::enter(app::Context& ctx) noexcept {
  note_ = nullptr;
  ctx.redraw();
}

void ParamsScreen::leave(app::Context& ctx) noexcept {
  if (perform_ != nullptr) {
    perform_->leave(ctx);  // releases the held pads
  }
}

void ParamsScreen::say(app::Context& ctx, const char* text) noexcept {
  note_ = text;
  note_until_ = ctx.uptime_ms() + kNoteMs;
  ctx.redraw();
}

void ParamsScreen::next_page(app::Context& ctx, int delta) noexcept {
  page_ = static_cast<std::uint8_t>(pr::next_page(page_, delta));
  focus_ = static_cast<std::uint8_t>(pr::first_focus(page_));
  note_ = nullptr;
  ctx.redraw();
}

void ParamsScreen::update(app::Context& ctx) noexcept {
  if (perform_ != nullptr) {
    perform_->update(ctx);  // the pads' bend and vibrato, as on PERFORM
  }
  if (note_ != nullptr && ctx.uptime_ms() >= note_until_) {
    note_ = nullptr;
  }
  ctx.redraw();  // the voiceprint moves every frame
}

// ---- input --------------------------------------------------------------------------------------------------------------------------

// `delta` detents of key `key` (a tap steps once, coarse, and wraps at the end). The value is read back from the engine, stepped and written.
void ParamsScreen::step(app::Context& ctx, unsigned key, int delta, bool coarse, bool tap) noexcept {
  if (!pr::focusable(page_, key)) {
    return;
  }
  const pr::KeyDef& k = pr::key_def(page_, key);
  dsp::Engine& e = ctx.engine();
  const int next = pr::step_display(k, read(e, voice_, k), delta, coarse, tap);
  (void)e.set_param(voice_, k.param, pr::to_param(k.fmt, next));
  ctx.redraw();
}

void ParamsScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  unsigned k = 0;
  if (in.fkey_press(k)) {
    if (in.shift || k >= pr::kKeys) {
      return;
    }
    const pr::KeyDef& def = pr::key_def(page_, k);
    if (def.kind == pr::Kind::Later) {
      say(ctx, "COMES LATER");
    } else if (def.kind == pr::Kind::Value) {
      if (k == focus_) {  // a second tap steps the value on
        step(ctx, k, 1, true, true);
      } else {
        focus_ = static_cast<std::uint8_t>(k);
        ctx.redraw();
      }
    }
    return;
  }
  if (in.turn()) {
    step(ctx, focus_, in.delta(), in.shift, false);
    return;
  }
  if (in.press(Button::Up)) {
    next_page(ctx, -1);
  } else if (in.press(Button::Down)) {
    next_page(ctx, 1);
  } else if ((in.pad_press() || in.pad_release()) && perform_ != nullptr) {
    perform_->pad_event(ctx, in);
  }
}

// ---- pads, keys -----------------------------------------------------------------------------------------------------------------------

void ParamsScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  if (perform_ != nullptr) {
    perform_->leds(ctx, leds);
  }
}

void ParamsScreen::soft_keys(app::Context&, app::SoftKeys& out) noexcept {
  for (unsigned i = 0; i < pr::kKeys; ++i) {
    out.label[i] = pr::key_def(page_, i).name;
  }
}

// HELP: the pads come from PERFORM (they play its chords); every button input() answers here has a row.
void ParamsScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Up, "PAGE BEFORE", nullptr},
      {Button::Down, "PAGE AFTER", nullptr},
      {Button::Params, "NEXT PAGE", "THE OLD PLAY SCREEN"},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER", nullptr},
      {Button::File, "FILE (SLICE U9)", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD (SLICE U8)", nullptr},
      {Button::SamplePlay, "SAMPLE: THE SLOTS", nullptr},
      {Button::SampleEdit, "SAMPLE EDIT: THE CUTS", nullptr},
      {Button::SampleRec, "SAMPLE REC: TAKE A LOOP", nullptr},
      {Button::Song, "SONG", nullptr},
      {Button::Pattern, "PATTERN: THE 16 STEPS", nullptr},
      {Button::Note, "PATTERN: NOTE LANE", nullptr},
      {Button::Instr, "PATTERN: CARRIER LANE", nullptr},
      {Button::Vol, "PATTERN: LEVEL LANE", nullptr},
      {Button::Fx, "PATTERN: SLICE LANE", nullptr},
      {Button::Shift, "COARSE STEPS ON THE ENCODER", nullptr},
  };
  if (perform_ != nullptr) {
    perform_->help(ctx, h);  // the pad labels and groups; the rest is replaced below
    for (unsigned i = 0; i < app::kFKeyCount; ++i) {
      h.fkey_shift[i] = nullptr;
    }
  }
  h.summary = "THREE PAGES OF EIGHT VALUES: CARRIER, SHAPE, TUNE. PARAMS OR UP AND DOWN\n"
              "TURN THE PAGE. DIM KEYS COME WITH A LATER SLICE. THE PADS PLAY CHORDS.";
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    h.fkey[i] = "TAP: FOCUS, THEN STEP";
  }
  h.fkey[7] = nullptr;
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "THE LIT F KEY'S VALUE";
}

// ---- draw ---------------------------------------------------------------------------------------------------------------------------

void ParamsScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  const dsp::Engine& e = ctx.engine();
  const app::AudioIn& ain = ctx.audio_in();
  const SourceName* src = ain.available() ? find_source(ain.source()) : nullptr;
  const int car_v = pr::to_display(pr::Fmt::Int, e.param(voice_, Vocoder::CarSrc));
  const unsigned car = car_v > 0 ? static_cast<unsigned>(car_v) : 0u;
  const unsigned mode = perform::mode_index(e.param(voice_, Vocoder::Mode));
  const unsigned tempo = e.stats().tempo_mbpm;

  ornoir::Voiceprint vp;
  vp.bars = mode == 0u || mode == 3u;  // BANDS and ROBOT; TALK and TUNE: the envelope and the ladder (follow_print)
  if (perform_ != nullptr) {
    perform_->follow_bands(ctx, vp.level, vp.peak);
    if (!vp.bars) {
      perform_->follow_print(ctx, vp);
    }
  }

  ornoir::HeaderState h;
  h.speaks = src != nullptr ? src->label : "NONE";
  h.sounds = perform::kCarrierNames[car < 9u ? car : 0u];
  h.mode = perform::kModeNames[mode];
  h.playing = e.playing();
  h.tempo_x10 = tempo / 100u;
  h.cpu_percent = ctx.cpu_percent();

  ornoir::FKey keys[ornoir::kFKeys];
  const pr::KeyDef* focused = nullptr;
  for (unsigned i = 0; i < pr::kKeys; ++i) {
    const pr::KeyDef& k = pr::key_def(page_, i);
    keys[i].name = k.name;
    if (k.kind == pr::Kind::Later) {
      keys[i].dim = true;
    } else if (k.kind == pr::Kind::Value) {
      const int v = read(e, voice_, k);
      char txt[12];
      pr::format_value(k, v, txt);
      std::memcpy(keys[i].value, txt, sizeof txt < sizeof keys[i].value ? sizeof txt : sizeof keys[i].value);
      keys[i].value[sizeof keys[i].value - 1u] = '\0';
      keys[i].pct = pr::bar_of(k, v);
      keys[i].hot = i == focus_;
      if (i == focus_) {
        focused = &k;
      }
    }
  }

  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  ornoir::voiceprint_strip(c, kVpX, kVpY, kVpW, kVpH, vp);
  for (unsigned i = 0; i < pr::kPages; ++i) {  // the page tabs
    const int x = kVpX + static_cast<int>(i) * 252;
    ornoir::fill_rect(c, x, kTabsY, 244, 44, i == page_ ? Color::Accent : Color::Empty);
    ornoir::fill_rect(c, x + 2, kTabsY + 2, 240, 40, Color::Pane);
  }
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  for (unsigned i = 0; i < pr::kPages; ++i) {
    const int cx = kVpX + static_cast<int>(i) * 252 + 122;
    r.text(centred(r, pr::kPageNames[i], Face::Label, cx), kTabsY + 14, pr::kPageNames[i], i == page_ ? Color::Text : Color::TextDim, Face::Label);
  }

  // The focused key's name, big, and the line under it: the budget warning, or a moment's note.
  if (focused != nullptr) {
    r.text(kVpX, 292, focused->name, Color::Text, Face::Display);
  }
  if (note_ != nullptr) {
    r.text(kVpX, 350, note_, Color::TextDim, Face::Label);
  }
}

}  // namespace o3_voc03er
