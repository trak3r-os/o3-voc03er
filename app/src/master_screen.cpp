// SPDX-License-Identifier: GPL-3.0-or-later
#include "master_screen.h"

#include "skin/ornoir.h"

#include <o3sdk/app/context.h>

#include <cstring>

namespace o3_voc03er {

using namespace o3sdk;
namespace ms = master;
using ui::Color;
using ui::Face;
using Button = app::Button;

namespace {

constexpr std::uint64_t kNoteMs = 2000;

// The three panels of the signal path, left to right (LIVE, the mode, OUTPUT), and the ladders inside the outer two.
constexpr int kPanelY = 56, kPanelH = 274;
constexpr int kLiveX = 16, kModeX = 300, kOutX = 534, kPanelW = 250, kModeW = 200;
constexpr int kMeterY = 96, kMeterH = 216, kMeterW = 28, kRowPx = 6;
constexpr int kInMeterX = 212, kOutMeterX = 742;
constexpr int kRowY0 = 96, kRowStep = 66, kBarW = 150;

int len(const char* s) noexcept { return static_cast<int>(std::strlen(s)); }

void copy_text(char (&dst)[12], const char* s) noexcept {
  std::size_t n = 0;
  while (s[n] != '\0' && n + 1u < sizeof dst) {
    dst[n] = s[n];
    ++n;
  }
  dst[n] = '\0';
}

// A tall ladder in whole 6 px rows: a track, the lit rows from the bottom, the held peak as one row of Text.
void meter_shapes(ui::plot::Canvas& c, int x, const master::Follower& f, Color lit) noexcept {
  ornoir::fill_rect(c, x, kMeterY, kMeterW, kMeterH, Color::Empty);
  const int rows = static_cast<int>(ornoir::ladder_height(f.level, kMeterH)) / kRowPx;
  for (int i = 0; i < rows; ++i) {
    ornoir::fill_rect(c, x + 2, kMeterY + kMeterH - (i + 1) * kRowPx + 1, kMeterW - 4, kRowPx - 2, lit);
  }
  const int pk = static_cast<int>(ornoir::ladder_height(f.peak, kMeterH)) / kRowPx;
  if (pk > rows) {
    ornoir::fill_rect(c, x + 2, kMeterY + kMeterH - pk * kRowPx + 1, kMeterW - 4, kRowPx - 2, Color::Text);
  }
}

void meter_scale(ui::Renderer& r, int x) noexcept {
  r.text(x - 6 - 6 * 1, kMeterY, "0", Color::TextDim, Face::Micro);
  r.text(x - 6 - 6 * 3, kMeterY + kMeterH * 12 / 60, "-12", Color::TextDim, Face::Micro);
  r.text(x - 6 - 6 * 3, kMeterY + kMeterH * 48 / 60, "-48", Color::TextDim, Face::Micro);
}

void panel_shapes(ui::plot::Canvas& c, int x, int w) noexcept {
  ornoir::fill_rect(c, x, kPanelY, w, kPanelH, Color::Empty);
  ornoir::fill_rect(c, x + 2, kPanelY + 2, w - 4, kPanelH - 4, Color::Pane);
}

}  // namespace

void MasterScreen::init(dsp::UnitId voice, const InputMeter* meter, PerformScreen::SourceStep pick, void* user) noexcept {
  voice_ = voice;
  meter_ = meter;
  step_source_ = pick;
  user_ = user;
}

const ui::Theme* MasterScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

// Nothing is focused on entry: the encoder moves nothing until a key is tapped, so a stray detent never switches the live SOURCE.
void MasterScreen::enter(app::Context& ctx) noexcept {
  note_ = nullptr;
  focus_ = kNoFocus;
  ctx.redraw();
}

void MasterScreen::say(app::Context& ctx, const char* text) noexcept {
  note_ = text;
  note_until_ = ctx.uptime_ms() + kNoteMs;
  ctx.redraw();
}

void MasterScreen::update(app::Context& ctx) noexcept {
  const app::AudioIn& ain = ctx.audio_in();
  const bool have = meter_ != nullptr && ain.available();
  in_.feed(have ? perform::level_byte(meter_->peak()) : static_cast<std::uint8_t>(0));
  const dsp::EngineStats st = ctx.engine().stats();
  out_.feed(perform::level_byte(st.peak_l > st.peak_r ? st.peak_l : st.peak_r));
  if (note_ != nullptr && ctx.uptime_ms() >= note_until_) {
    note_ = nullptr;
  }
  ctx.redraw();  // the meters move every frame
}

// ---- the board ------------------------------------------------------------------------------------------------------------------------

ms::Avail MasterScreen::avail(const app::Context& ctx) const noexcept {
  const app::AudioIn& ain = ctx.audio_in();
  ms::Avail a;
  a.audio_in = ain.available();
  a.usb_source = a.audio_in && ain.source() == hal::capture::Source::Usb;
  a.radio_source = a.audio_in && ain.source() == hal::capture::Source::Radio;
  a.radio_usable = ctx.radio().usable();
  a.usb_device = ctx.usb_audio().available();
  return a;
}

int MasterScreen::read(const app::Context& ctx, unsigned k) const noexcept {
  switch (k) {
    case ms::Source:
      return ms::source_index(ctx.audio_in().source());
    case ms::Gain:
      return static_cast<int>(ctx.audio_in().gain());
    case ms::Monitor:
      return static_cast<int>(ctx.audio_in().monitor());
    case ms::Return:
      return static_cast<int>(ctx.usb_audio().return_level());
    case ms::Dry:
    case ms::Level:
      return params::to_display(ms::kDefs[k].fmt, ctx.engine().param(voice_, ms::kDefs[k].param));
    default:
      return 0;
  }
}

void MasterScreen::write(app::Context& ctx, unsigned k, int v) noexcept {
  const unsigned pct = v > 0 ? static_cast<unsigned>(v) : 0u;
  switch (k) {
    case ms::Source: {
      const hal::capture::Source src = kSources[static_cast<std::size_t>(v > 0 ? v : 0) % kSourceCount].source;
      if (step_source_ != nullptr) {
        step_source_(user_, src, default_gain(src));  // the app's one source path: select, the meter's reset, the USB channel
      } else {
        (void)ctx.audio_in().select(src, default_gain(src));
      }
      break;
    }
    case ms::Gain:
      (void)ctx.audio_in().set_gain(pct);
      break;
    case ms::Monitor:
      ctx.audio_in().monitor(pct);
      break;
    case ms::Return:
      ctx.usb_audio().set_return_level(pct);
      break;
    case ms::Dry:
    case ms::Level:
      (void)ctx.engine().set_param(voice_, ms::kDefs[k].param, params::to_param(ms::kDefs[k].fmt, v));
      break;
    default:
      break;
  }
}

// `delta` detents of key `k`; a tap is the second tap (coarse, wraps, MONITOR toggles). SOURCE is a round key.
void MasterScreen::step(app::Context& ctx, unsigned k, int delta, bool coarse, bool tap) noexcept {
  if (!ms::steppable(k) || !ms::live(k, avail(ctx))) {
    return;
  }
  const int cur = read(ctx, k);
  const int next = tap ? ms::tap_value(k, cur) : params::step_display(ms::kDefs[k], cur, delta, coarse, k == ms::Source);
  if (next != cur) {
    write(ctx, k, next);
  }
  ctx.redraw();
}

// ---- input --------------------------------------------------------------------------------------------------------------------------

void MasterScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  unsigned k = 0;
  if (in.fkey_press(k)) {
    if (in.shift || k >= ms::kKeys) {
      return;
    }
    const ms::Avail a = avail(ctx);
    if (!ms::live(k, a)) {
      say(ctx, ms::dim_note(k, a));
    } else if (!ms::steppable(k)) {
      ctx.radio().seek(k == ms::SeekUp);
      ctx.redraw();
    } else if (k == focus_) {  // a second tap steps the value on
      step(ctx, k, 1, true, true);
    } else {
      focus_ = static_cast<std::uint8_t>(k);
      ctx.redraw();
    }
    return;
  }
  if (in.turn()) {
    if (focus_ >= ms::kKeys) {
      say(ctx, "TAP AN F KEY FIRST");  // the encoder points at nothing yet
      return;
    }
    step(ctx, focus_, in.delta(), in.shift, false);
  }
}

void MasterScreen::soft_keys(app::Context&, app::SoftKeys& out) noexcept {
  for (unsigned i = 0; i < ms::kKeys; ++i) {
    out.label[i] = ms::key_name(i);
  }
}

// HELP: the buttons the app opens screens with (MASTER answers none itself); each soft key line is short.
void MasterScreen::help(const app::Context&, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Params, "PARAMS: THE VALUES", nullptr},
      {Button::Master, "MASTER: THIS SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::File, "FILE", nullptr},
      {Button::Song, "SONG", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD", nullptr},
      {Button::SamplePlay, "SAMPLE: THE SLOTS", nullptr},
      {Button::SampleEdit, "SAMPLE EDIT: THE CUTS", nullptr},
      {Button::SampleRec, "SAMPLE REC: TAKE A LOOP", nullptr},
      {Button::Pattern, "PATTERN: THE 16 STEPS", nullptr},
      {Button::Shift, "COARSE STEPS ON THE ENCODER", nullptr},
  };
  h.summary = "THE SIGNAL PATH: LIVE IN, THE MODE, OUTPUT. TAP A KEY TO POINT THE ENCODER AT IT (NONE\n"
              "AT FIRST), TAP AGAIN TO STEP. A DIM KEY SAYS WHY. THE MODE IS SET ON PERFORM F1.";
  h.fkey[ms::Source] = "LINE, MIC, RADIO OR USB";
  h.fkey[ms::Gain] = "INPUT GAIN, NOT ON USB";
  h.fkey[ms::Monitor] = "INPUT TO THE MIX";
  h.fkey[ms::Dry] = "DRY SHARE OF THE OUTPUT";
  h.fkey[ms::Level] = "MASTER LEVEL IN DB";
  h.fkey[ms::Return] = "COMPUTER AUDIO";
  h.fkey[ms::SeekDown] = "NEXT STATION DOWN: RADIO";
  h.fkey[ms::SeekUp] = "NEXT STATION UP: RADIO";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "THE TAPPED F KEY'S VALUE";
}

// ---- draw ---------------------------------------------------------------------------------------------------------------------------

void MasterScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  const dsp::Engine& e = ctx.engine();
  const app::AudioIn& ain = ctx.audio_in();
  const ms::Avail a = avail(ctx);
  const SourceName* src = a.audio_in ? find_source(ain.source()) : nullptr;
  const unsigned mode = perform::mode_index(e.param(voice_, Vocoder::Mode));

  ornoir::HeaderState h;
  header_route(e, voice_, src != nullptr ? src->label : "NONE", src != nullptr && src->source == hal::capture::Source::Usb, h);
  h.mode = perform::kModeNames[mode];
  h.playing = e.playing();
  h.tempo_x10 = e.stats().tempo_mbpm / 100u;
  h.cpu_percent = ctx.cpu_percent();

  // The eight F keys, and the six rows of the panels, from the same values.
  ornoir::FKey keys[ornoir::kFKeys];
  bool dim[ms::kKeys] = {};
  char value[6][12] = {};
  int pct[6] = {-1, -1, -1, -1, -1, -1};
  for (unsigned i = 0; i < ms::kKeys; ++i) {
    keys[i].name = ms::key_name(i);
    dim[i] = !ms::live(i, a);
    keys[i].dim = dim[i];
    keys[i].hot = i == focus_ && !dim[i];
    if (i >= 6u) {
      copy_text(keys[i].value, i == ms::SeekDown ? "<<" : ">>");
      continue;
    }
    const int v = read(ctx, i);
    if (i == ms::Source) {
      copy_text(value[i], kSources[static_cast<std::size_t>(v) % kSourceCount].label);
    } else if (i == ms::Monitor && v == 0) {
      copy_text(value[i], "OFF");
    } else {
      params::format_value(ms::kDefs[i], v, value[i]);
      value[i][sizeof value[i] - 1u] = '\0';
    }
    if (i != ms::Source) {
      pct[i] = params::bar_of(ms::kDefs[i], v);
    }
    if (dim[i]) {
      copy_text(value[i], i == ms::Gain && a.usb_source ? "USB" : "NONE");
      pct[i] = -1;
    }
    copy_text(keys[i].value, value[i]);
    keys[i].pct = pct[i];
    keys[i].role = i == ms::Dry || i == ms::Level || i == ms::Return ? ornoir::KeyRole::Carrier : ornoir::KeyRole::Voice;
  }
  const bool clipping = meter_ != nullptr && a.audio_in && meter_->clipping();

  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  panel_shapes(c, kLiveX, kPanelW);
  panel_shapes(c, kModeX, kModeW);
  panel_shapes(c, kOutX, kPanelW);
  meter_shapes(c, kInMeterX, in_, Color::Play);
  meter_shapes(c, kOutMeterX, out_, Color::Sel);
  ornoir::fill_rect(c, kInMeterX - 8, 66, 44, 16, clipping ? Color::Rec : Color::Empty);
  // The rows' bars, and the lit row's tag.
  for (unsigned i = 0; i < 6u; ++i) {
    const int px = i < 3u ? kLiveX + 12 : kOutX + 12;
    const int y0 = kRowY0 + static_cast<int>(i % 3u) * kRowStep;
    if (i == focus_ && !dim[i]) {
      ornoir::fill_rect(c, px - 8, y0 - 2, 4, 50, Color::Accent);
    }
    if (pct[i] >= 0) {
      ornoir::fill_rect(c, px, y0 + 44, kBarW, 6, Color::Empty);
      ornoir::fill_rect(c, px, y0 + 44, pct[i] * kBarW / 100, 6, i < 3u ? Color::Play : Color::Sel);
    }
  }
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  r.text(kLiveX + 12, 64, "LIVE", Color::Play, Face::Label);
  r.text(kModeX + 12, 64, "MODE", Color::TextDim, Face::Label);
  r.text(kOutX + 12, 64, "OUTPUT", Color::Sel, Face::Label);
  r.text(kInMeterX - 8 + 6, 70, "CLIP", clipping ? Color::Text : Color::TextDim, Face::Micro);
  r.text(kInMeterX, kMeterY + kMeterH + 6, "IN", Color::TextDim, Face::Micro);
  r.text(kOutMeterX, kMeterY + kMeterH + 6, "OUT", Color::TextDim, Face::Micro);
  meter_scale(r, kInMeterX);
  meter_scale(r, kOutMeterX);
  r.text(kLiveX + kPanelW + 11, 170, ">", Color::TextDim, Face::Display);
  r.text(kModeX + kModeW + 11, 170, ">", Color::TextDim, Face::Display);

  for (unsigned i = 0; i < 6u; ++i) {
    const int px = i < 3u ? kLiveX + 12 : kOutX + 12;
    const int y0 = kRowY0 + static_cast<int>(i % 3u) * kRowStep;
    r.text(px, y0, ms::key_name(i), i == focus_ && !dim[i] ? Color::Text : Color::TextDim, Face::Label);
    r.text(px, y0 + 18, value[i], dim[i] ? Color::TextDim : Color::Text, Face::Data);
  }

  const int mode_w = r.text_width(h.mode, Face::Display);
  r.text(kModeX + (kModeW - mode_w) / 2, 150, h.mode, Color::Text, Face::Display);
  const int car_w = r.text_width(h.sounds, Face::Label);
  r.text(kModeX + (kModeW - car_w) / 2, 196, h.sounds, Color::SelText, Face::Label);

  const char* set_on = "SET ON PERFORM F1";
  r.text(kModeX + (kModeW - r.text_width(set_on, Face::Micro)) / 2, 222, set_on, Color::TextDim, Face::Micro);
  r.text(kLiveX, 340, "ONE LIVE SOURCE: MIC, LINE AND RADIO SHARE THE ADC; USB IS STEREO", Color::TextDim, Face::Label);
  if (a.radio_source) {
    char f[12];
    ms::format_freq(ctx.radio().freq(), f);
    r.text(kLiveX, 372, "FREQ", Color::TextDim, Face::Label);
    r.text(kLiveX + 56, 364, f, a.radio_usable ? Color::Text : Color::TextDim, Face::Display);
    r.text(kLiveX + 56 + 16 * len(f) + 8, 372, "MHZ", Color::TextDim, Face::Label);
  }
  if (note_ != nullptr) {
    r.text(kLiveX, 400, note_, Color::TextDim, Face::Label);
  }
}

}  // namespace o3_voc03er
