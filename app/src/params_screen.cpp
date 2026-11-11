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
constexpr int kTabH = 44;
constexpr int kVpX = 26, kVpY = 78, kVpW = 748, kVpH = 128;

int centred(ui::Renderer& r, const char* s, Face f, int cx) noexcept { return cx - r.text_width(s, f) / 2; }

int read(const dsp::Engine& e, dsp::UnitId id, const pr::KeyDef& k) noexcept { return pr::to_display(k.fmt, e.param(id, k.param)); }

// The unit a key's parameter belongs to: the tract unit's for Kind::Tract, the vocoder's otherwise.
dsp::UnitId unit_of(const pr::KeyDef& k, dsp::UnitId voice, dsp::UnitId tract) noexcept { return k.kind == pr::Kind::Tract ? tract : voice; }

// An App key's value is PerformScreen's (SMOOTH, the default, when the screen has none).
int read_app(const PerformScreen* perform, const pr::KeyDef& k) noexcept {
  return k.fmt == pr::Fmt::Voicing && perform != nullptr ? static_cast<int>(perform->voicing()) : 3;
}

}  // namespace

void ParamsScreen::init(dsp::UnitId voice, dsp::UnitId tract, PerformScreen* perform) noexcept {
  voice_ = voice;
  tract_ = tract;
  perform_ = perform;
  page_ = 0;  // setup() can run again on this object (tests, `reboot`): PARAMS opens on its first page
  focus_ = static_cast<std::uint8_t>(pr::first_focus(0));
  note_ = nullptr;
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
  if (k.kind == pr::Kind::App) {
    if (perform_ != nullptr && k.fmt == pr::Fmt::Voicing) {
      perform_->set_voicing(static_cast<unsigned>(pr::step_display(k, read_app(perform_, k), delta, coarse, tap)));
    }
    ctx.redraw();
    return;
  }
  const dsp::UnitId id = unit_of(k, voice_, tract_);
  const int next = pr::step_display(k, read(e, id, k), delta, coarse, tap);
  (void)e.set_param(id, k.param, pr::to_param(k.fmt, next));
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
    } else if (pr::live_kind(def.kind)) {
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
      {Button::Params, "NEXT PAGE", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER", nullptr},
      {Button::File, "FILE", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD", nullptr},
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
  h.summary = page_ == pr::kTractPage
                  ? "THE MOUTH OF TRACT: VOWEL, TONGUE, JAW, LIPS, GLIDE, SIZE, RES, HISS.\n"
                    "THEY SPEAK IN TRACT MODE ONLY. THE VALUES ARE KEPT IN THE OTHER MODES."
                  : "FOUR PAGES OF EIGHT VALUES: CARRIER, SHAPE, TUNE, TRACT. PARAMS OR UP\n"
                    "AND DOWN TURN THE PAGE. A DIM KEY DOES NOTHING IN THIS MODE. PADS PLAY CHORDS.";
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    h.fkey[i] = pr::focusable(page_, i) ? "TAP: FOCUS, THEN STEP" : nullptr;  // a blank key has no row
  }
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "THE LIT F KEY'S VALUE";
}

// ---- draw ---------------------------------------------------------------------------------------------------------------------------

void ParamsScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  const dsp::Engine& e = ctx.engine();
  const app::AudioIn& ain = ctx.audio_in();
  const SourceName* src = ain.available() ? find_source(ain.source()) : nullptr;
  const unsigned mode = perform::mode_index(e.param(voice_, Vocoder::Mode));
  const unsigned tempo = e.stats().tempo_mbpm;

  ornoir::Voiceprint vp;
  vp.bars = mode == 0u || mode == 3u;  // BANDS and ROBOT; TALK and TUNE: the envelope and the ladder (follow_print); TRACT: the mouth (follow_print)
  if (perform_ != nullptr) {
    perform_->follow_bands(ctx, vp.level, vp.peak);
    if (!vp.bars) {
      perform_->follow_print(ctx, vp);
    }
  }

  ornoir::HeaderState h;
  header_route(e, voice_, src != nullptr ? src->label : "NONE", src != nullptr && src->source == hal::capture::Source::Usb, h);
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
    } else if (pr::live_kind(k.kind)) {
      keys[i].dim = !pr::key_live(k, mode);  // QUALITY outside TALK
      const int v = k.kind == pr::Kind::App ? read_app(perform_, k) : read(e, unit_of(k, voice_, tract_), k);
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
    const int x = pr::tab_x(i);
    ornoir::fill_rect(c, x, kTabsY, pr::kTabW, kTabH, i == page_ ? Color::Accent : Color::Empty);
    ornoir::fill_rect(c, x + 2, kTabsY + 2, pr::kTabW - 4, kTabH - 4, Color::Pane);
  }
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  for (unsigned i = 0; i < pr::kPages; ++i) {
    const int cx = pr::tab_x(i) + pr::kTabW / 2;
    r.text(centred(r, pr::kPageNames[i], Face::Label, cx), kTabsY + 14, pr::kPageNames[i], i == page_ ? Color::Text : Color::TextDim, Face::Label);
  }

  // The focused key's name, big, and the line under it: the budget warning, or a moment's note.
  if (focused != nullptr) {
    r.text(kVpX, 292, focused->name, Color::Text, Face::Display);
  }
  if (note_ != nullptr) {
    r.text(kVpX, 350, note_, Color::TextDim, Face::Label);
  } else if (page_ == pr::kTractPage && mode != kModeTract) {
    r.text(kVpX, 350, "TRACT MODE ONLY", Color::TextDim, Face::Label);  // the values are kept; the mouth speaks in TRACT
  }
}

}  // namespace o3_voc03er
