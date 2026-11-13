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
namespace live = o3_voc03er::live;
using ui::Color;
using ui::Face;
using Button = app::Button;

namespace {

constexpr std::uint64_t kNoteMs = 2000;
constexpr int kTitleY = 44;  // under the header, above the voiceprint
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
  mode_ = 0;  // setup() can run again on this object (tests, `reboot`): PARAMS opens on the first page of the mode (sync reads it)
  page_ = 0;
  learn_armed_ = false;
  learn_page_ = 0;
  learn_key_ = 0;
  focus_ = static_cast<std::uint8_t>(pr::first_focus_at(pr::layout_of(0, 0), 0));
  note_ = nullptr;
}

const ui::Theme* ParamsScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void ParamsScreen::enter(app::Context& ctx) noexcept {
  note_ = nullptr;
  (void)sync(ctx);
  ctx.redraw();
}

void ParamsScreen::leave(app::Context& ctx) noexcept {
  learn_drop(ctx);
  if (perform_ != nullptr) {
    perform_->leave(ctx);  // releases the held pads
  }
}

void ParamsScreen::say(app::Context& ctx, const char* text) noexcept {
  note_ = text;
  note_until_ = ctx.uptime_ms() + kNoteMs;
  ctx.redraw();
}

// The layout of the engine's mode and carrier now. A change of the mode: the first page, its first key. A change of the carrier alone changes no cell
// (SPEED's reserved cell fills in or goes blank): the focus stays where it is, and falls to the page's first key only when its cell went blank.
// Either way a learn that is no longer on the page is cancelled.
const pr::Layout& ParamsScreen::sync(app::Context& ctx) noexcept {
  const dsp::Engine& e = ctx.engine();
  const unsigned mode = perform::mode_index(e.param(voice_, Vocoder::Mode));
  const unsigned car_src = static_cast<unsigned>(e.param(voice_, Vocoder::CarSrc) + 0.5f);  // SPEED is heard in any mode with the VOICE source (liveness.h)
  const pr::Layout& lay = pr::layout_of(mode, car_src);
  if (mode != mode_) {
    page_ = 0;
    focus_ = static_cast<std::uint8_t>(pr::first_focus_at(lay, 0));
    note_ = nullptr;
    mode_ = static_cast<std::uint8_t>(mode);
  }
  if (!pr::focusable_at(lay, page_, focus_)) {  // a valid focus, whatever happened
    page_ = static_cast<std::uint8_t>(page_ < lay.pages ? page_ : 0u);
    focus_ = static_cast<std::uint8_t>(pr::first_focus_at(lay, page_));
  }
  if (learn_armed_) {
    unsigned p = 0, k = 0;
    const pr::Cell armed{learn_page_, learn_key_};
    if (!pr::find_cell(lay, armed, p, k) || p != page_) {
      learn_drop(ctx);
    }
  }
  return lay;
}

// ---- MIDI learn (ML1) ---------------------------------------------------------------------------------------------------------------

void ParamsScreen::learn_drop(app::Context& ctx) noexcept {
  if (learn_armed_ && ctx.midi().learning()) {
    ctx.midi().learn_cancel();
  }
  learn_armed_ = false;
}

bool ParamsScreen::learn_on(const app::Context& ctx, const pr::Layout& lay, unsigned key) const noexcept {
  const pr::Cell c = pr::cell_at(lay, page_, key);
  return learn_armed_ && !pr::blank(c) && c.page == learn_page_ && c.key == learn_key_ && ctx.midi().learning();
}

// SHIFT + F `key`: arm learn for its parameter, or cancel it when it is armed on that key; a value that is not a unit parameter says so.
void ParamsScreen::learn_key(app::Context& ctx, const pr::Layout& lay, unsigned key) noexcept {
  const pr::Cell cell = pr::cell_at(lay, page_, key);
  if (pr::blank(cell)) {
    return;
  }
  const bool can = pr::learnable(cell.page, cell.key);
  switch (pr::learn_tap(can, can && learn_on(ctx, lay, key))) {
    case pr::LearnTap::Cannot:
      learn_drop(ctx);  // an arm on another key goes too, so the message is not hidden under "MOVE A KNOB"
      say(ctx, "CANNOT LEARN THIS VALUE");
      return;
    case pr::LearnTap::Cancel:
      learn_drop(ctx);
      say(ctx, "LEARN OFF");
      return;
    case pr::LearnTap::Arm:
      break;
  }
  const pr::KeyDef& k = pr::def_of(cell);
  learn_drop(ctx);  // an earlier arm (another key) goes whether or not this one takes
  if (!ctx.midi().learn(unit_of(k, voice_, tract_), k.param)) {
    say(ctx, "CANNOT LEARN THIS VALUE");
    return;
  }
  learn_armed_ = true;
  learn_page_ = cell.page;
  learn_key_ = cell.key;
  note_ = nullptr;
  ctx.redraw();
}

void ParamsScreen::next_page(app::Context& ctx, int delta) noexcept {
  const pr::Layout& lay = sync(ctx);
  learn_drop(ctx);  // the arm is a page's: the next page's keys are other values
  page_ = static_cast<std::uint8_t>(pr::next_page_in(lay, page_, delta));
  focus_ = static_cast<std::uint8_t>(pr::first_focus_at(lay, page_));
  note_ = nullptr;
  ctx.redraw();
}

void ParamsScreen::update(app::Context& ctx) noexcept {
  (void)sync(ctx);
  if (perform_ != nullptr) {
    perform_->update(ctx);  // the pads' bend and vibrato, as on PERFORM
  }
  if (learn_armed_ && !ctx.midi().learning()) {
    learn_drop(ctx);  // a controller bound it (the strip says LEARNED CC n), or something else disarmed it
  }
  if (note_ != nullptr && ctx.uptime_ms() >= note_until_) {
    note_ = nullptr;
  }
  ctx.redraw();  // the voiceprint moves every frame
}

// ---- input --------------------------------------------------------------------------------------------------------------------------

// `delta` detents of key `key` (a tap steps once, coarse, and wraps at the end). The value is read back from the engine, stepped and written.
void ParamsScreen::step(app::Context& ctx, const pr::Layout& lay, unsigned key, int delta, bool coarse, bool tap) noexcept {
  const pr::Cell cell = pr::cell_at(lay, page_, key);
  if (pr::blank(cell) || !pr::focusable(cell.page, cell.key)) {
    return;
  }
  const pr::KeyDef& k = pr::def_of(cell);
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
  const pr::Layout& lay = sync(ctx);
  unsigned k = 0;
  if (in.fkey_press(k)) {
    if (k >= pr::kKeys) {
      return;
    }
    if (in.shift) {  // ML1: SHIFT + F is MIDI learn (the focus and the value stay as they are)
      learn_key(ctx, lay, k);
      return;
    }
    if (pr::focusable_at(lay, page_, k)) {
      if (k == focus_) {  // a second tap steps the value on
        step(ctx, lay, k, 1, true, true);
      } else {
        focus_ = static_cast<std::uint8_t>(k);
        ctx.redraw();
      }
    }
    return;
  }
  if (in.turn()) {
    step(ctx, lay, focus_, in.delta(), in.shift, false);
    return;
  }
  if (in.press(Button::Up)) {
    next_page(ctx, -1);
  } else if (in.press(Button::Down)) {
    next_page(ctx, 1);
  } else if (in.press(Button::Left) || in.press(Button::Right)) {  // one encoder step of the lit key, SHIFT as the encoder takes it; repeats arrive as presses
    step(ctx, lay, focus_, in.press(Button::Left) ? -1 : 1, in.shift, false);
  } else if ((in.pad_press() || in.pad_release()) && perform_ != nullptr) {
    perform_->pad_event(ctx, in);
  }
}

std::uint64_t ParamsScreen::repeat_buttons(const app::Context&) const noexcept {
  return (std::uint64_t{1} << static_cast<unsigned>(Button::Left)) | (std::uint64_t{1} << static_cast<unsigned>(Button::Right));
}

// ---- pads, keys -----------------------------------------------------------------------------------------------------------------------

void ParamsScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  if (perform_ != nullptr) {
    perform_->leds(ctx, leds);
  }
}

void ParamsScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  const pr::Layout& lay = sync(ctx);
  const bool shift = ctx.panel().shift();
  for (unsigned i = 0; i < pr::kKeys; ++i) {
    const pr::Cell c = pr::cell_at(lay, page_, i);
    out.label[i] = pr::blank(c) ? "" : (shift ? (pr::learnable_at(lay, page_, i) ? "LEARN" : "-") : pr::view_name(c));
  }
}

// HELP: the pads come from PERFORM (they play its chords); every button input() answers here has a row.
void ParamsScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Up, "PAGE BEFORE", nullptr},
      {Button::Down, "PAGE AFTER", nullptr},
      {Button::Left, "LIT KEY DOWN, HOLD REPEATS", nullptr},
      {Button::Right, "LIT KEY UP, HOLD REPEATS", nullptr},
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
      {Button::Fx, "PATTERN: SLICE, MOUTH LANE", nullptr},
      {Button::Shift, "COARSE STEPS; F KEYS: LEARN", nullptr},
  };
  // The layout this screen shows (sync sees the same engine on the next frame; a help opened in the very frame of a mode change reads the new one).
  const dsp::Engine& e = ctx.engine();
  const pr::Layout& lay = pr::layout_of(perform::mode_index(e.param(voice_, Vocoder::Mode)), static_cast<unsigned>(e.param(voice_, Vocoder::CarSrc) + 0.5f));
  const unsigned page = page_ < lay.pages ? page_ : 0u;
  if (perform_ != nullptr) {
    perform_->help(ctx, h);  // the pad labels and groups; the rest is replaced below
  }
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    h.fkey_shift[i] = pr::learnable_at(lay, page, i) ? "LEARN" : nullptr;
  }
  h.summary =
      "THE VALUES OF THE MODE PERFORM IS IN: ONLY WHAT IT HEARS, GROUPED BY MEANING.\n"
      "PARAMS OR UP AND DOWN TURN THE PAGE. MODE IS PERFORM'S F1. SHIFT + F: LEARN.";
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    // a blank key has no row; MOUTH (LT1) says what it does: the pads and the lane, or the voice at the input, shape the tube
    const pr::Cell c = pr::cell_at(lay, page, i);
    h.fkey[i] = pr::blank(c) ? nullptr : (pr::def_of(c).fmt == pr::Fmt::Source ? "MOUTH: PADS OR LIVE VOICE" : "TAP: FOCUS, THEN STEP");
  }
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "THE LIT F KEY'S VALUE";
}

// ---- draw ---------------------------------------------------------------------------------------------------------------------------

void ParamsScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  const pr::Layout& lay = sync(ctx);
  const dsp::Engine& e = ctx.engine();
  const app::AudioIn& ain = ctx.audio_in();
  const SourceName* src = ain.available() ? find_source(ain.source()) : nullptr;
  const unsigned mode = mode_;
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
  const char* focused_name = "";
  const bool shift_row = ctx.panel().shift();  // ML1: held SHIFT turns the row into LEARN cells
  for (unsigned i = 0; i < pr::kKeys; ++i) {
    const pr::Cell c = pr::cell_at(lay, page_, i);
    if (pr::blank(c)) {
      continue;  // the end of the mode's last page: nothing there
    }
    const pr::KeyDef& k = pr::def_of(c);
    keys[i].name = pr::view_name(c);
    const int v = k.kind == pr::Kind::App ? read_app(perform_, k) : read(e, unit_of(k, voice_, tract_), k);
    char txt[12];
    pr::format_value(k, v, txt);
    std::memcpy(keys[i].value, txt, sizeof txt < sizeof keys[i].value ? sizeof txt : sizeof keys[i].value);
    keys[i].value[sizeof keys[i].value - 1u] = '\0';
    keys[i].pct = pr::bar_of(k, v);
    keys[i].hot = i == focus_;
    if (i == focus_) {
      focused = &k;
      focused_name = pr::view_name(c);
    }
  }

  if (shift_row) {
    for (unsigned i = 0; i < pr::kKeys; ++i) {
      const pr::Cell c = pr::cell_at(lay, page_, i);
      keys[i] = ornoir::FKey{};
      if (pr::blank(c)) {
        continue;
      }
      if (pr::learnable(c.page, c.key)) {
        keys[i].name = "LEARN";
        std::strncpy(keys[i].value, pr::view_name(c), sizeof keys[i].value - 1u);  // the value it would bind (the array starts zeroed: FKey{})
        keys[i].hot = learn_on(ctx, lay, i);
      } else {
        keys[i].name = "-";
        std::memcpy(keys[i].value, "-", 2);  // the window shows the value's slot: a dash
        keys[i].dim = true;
      }
    }
  }

  const unsigned tabs = lay.pages;
  const int tab_w = pr::tab_w(tabs);
  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  ornoir::voiceprint_strip(c, kVpX, kVpY, kVpW, kVpH, vp);
  for (unsigned i = 0; i < tabs; ++i) {  // the page tabs: as many as the mode has pages
    const int x = pr::tab_x(tabs, i);
    ornoir::fill_rect(c, x, kTabsY, tab_w, kTabH, i == page_ ? Color::Accent : Color::Empty);
    ornoir::fill_rect(c, x + 2, kTabsY + 2, tab_w - 4, kTabH - 4, Color::Pane);
  }
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  char title[24] = "PARAMS  ";  // the screen's name and the mode it edits
  std::strncat(title, perform::kModeNames[mode], sizeof title - std::strlen(title) - 1u);
  r.text(kVpX, kTitleY, title, Color::Text, Face::Display);

  for (unsigned i = 0; i < tabs; ++i) {
    const int cx = pr::tab_x(tabs, i) + tab_w / 2;
    r.text(centred(r, lay.name[i], Face::Label, cx), kTabsY + 14, lay.name[i], i == page_ ? Color::Text : Color::TextDim, Face::Label);
  }

  // The focused key's name, big, and the line under it: the learn prompt, or a moment's note.
  if (focused != nullptr) {
    r.text(kVpX, 292, focused_name, Color::Text, Face::Display);
  }
  bool armed_here = false;
  for (unsigned i = 0; i < pr::kKeys && !armed_here; ++i) {
    armed_here = learn_on(ctx, lay, i);
  }
  if (armed_here) {
    char line[48] = "LEARN ";
    std::strncat(line, pr::view_name(pr::Cell{learn_page_, learn_key_}), 16);
    std::strncat(line, ": MOVE A KNOB", sizeof line - std::strlen(line) - 1u);
    r.text(kVpX, 350, line, Color::Accent, Face::Label);
  } else if (note_ != nullptr) {
    r.text(kVpX, 350, note_, Color::TextDim, Face::Label);
  }
}

}  // namespace o3_voc03er
