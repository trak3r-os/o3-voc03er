// SPDX-License-Identifier: GPL-3.0-or-later
#include "sample_rec_screen.h"

#include "overview.h"
#include "perform_logic.h"
#include "skin/ornoir.h"

#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/context.h>
#include <o3sdk/app/pad_leds.h>

#include <cstring>

namespace o3_voc03er {

using namespace o3sdk;
namespace pl = perform;
namespace sr = sample_rec;
using ui::Color;
using ui::Face;
using Button = app::Button;
using Rec = Vocoder::LoopStatus::Rec;

namespace {

constexpr std::uint64_t kStartMs = 250;  // the unit shows the Rec within this, or the take failed (as PERFORM's capture)
constexpr std::uint64_t kNoteMs = 2500;
constexpr std::uint32_t kTrimMaxOver = 4410;  // a stopped take is trimmed to its mark only when it ran at most this far (100 ms) past it
constexpr unsigned kGainTap = 5;  // a second tap on F2 moves the gain this much (a detent moves 1)
constexpr int kPicX = 548, kPicY = 210;  // the overview picture (240 x 48), right of the slot cards, under the BAR chip

constexpr const char* kSlotNames[4] = {"SLOT 1", "SLOT 2", "SLOT 3", "SLOT 4"};
constexpr const char* kKeyNames[ornoir::kFKeys] = {"SOURCE", "GAIN", "LENGTH", "AFTER", "AUTOPLAY", "MONITOR", "SLOT", ""};

void cat(char* dst, std::size_t cap, const char* s) noexcept {
  std::size_t n = std::strlen(dst);
  while (*s != '\0' && n + 1u < cap) {
    dst[n++] = *s++;
  }
  dst[n] = '\0';
}

void cat_uint(char* dst, std::size_t cap, unsigned v) noexcept {
  char tmp[12];
  unsigned t = 0;
  do {
    tmp[t++] = static_cast<char>('0' + v % 10u);
    v /= 10u;
  } while (v != 0u && t < sizeof tmp - 1u);
  char out[12];
  unsigned n = 0;
  while (t != 0u) {
    out[n++] = tmp[--t];
  }
  out[n] = '\0';
  cat(dst, cap, out);
}

// "3.4" from tenths.
void cat_tenths(char* dst, std::size_t cap, unsigned tenths) noexcept {
  cat_uint(dst, cap, tenths / 10u);
  cat(dst, cap, ".");
  cat_uint(dst, cap, tenths % 10u);
}

int centred(ui::Renderer& r, const char* s, Face f, int cx) noexcept { return cx - r.text_width(s, f) / 2; }

// What the screen shows of the slot.
enum class View : std::uint8_t { Empty, Armed, Recording, Stopping, Take };

}  // namespace

void SampleRecScreen::init(dsp::UnitId voice, const InputMeter* meter, const Vocoder* voc, PerformScreen* perform, PerformScreen::SourceStep step,
                           void* user) noexcept {
  voice_ = voice;
  meter_ = meter;
  voc_ = voc;
  perform_ = perform;
  step_source_ = step;
  source_user_ = user;
}

const ui::Theme* SampleRecScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void SampleRecScreen::enter(app::Context& ctx) noexcept {
  note_ = nullptr;
  ctx.redraw();
}

// A take in flight ends with the screen: an armed one is cancelled, a running one stops at its bar line and stays in the slot, not placed.
void SampleRecScreen::leave(app::Context& ctx) noexcept {
  if (phase_ != Phase::Idle) {
    write_rec(ctx, false);
    phase_ = Phase::Idle;
    stop_ = false;
    drop_ = false;
  }
  if (perform_ != nullptr) {
    perform_->leave(ctx);  // releases the held pads
  }
}

std::uint64_t SampleRecScreen::repeat_buttons(const app::Context&) const noexcept {
  return (std::uint64_t{1} << static_cast<unsigned>(Button::Left)) | (std::uint64_t{1} << static_cast<unsigned>(Button::Right));
}

void SampleRecScreen::say(app::Context& ctx, const char* text) noexcept {
  note_ = text;
  note_until_ = ctx.uptime_ms() + kNoteMs;
  ctx.redraw();
}

bool SampleRecScreen::holds_audio(unsigned slot) const noexcept {
  return voc_ != nullptr && slot < 4u && voc_->loop_status(slot).length != 0u;
}

void SampleRecScreen::write_rec(app::Context& ctx, bool on) noexcept {
  (void)ctx.engine().set_param(voice_, Vocoder::loop_param(phase_ != Phase::Idle ? rec_slot_ : slot_, Vocoder::LoopField::Rec), on ? 1.0f : 0.0f);
}

// ---- the take -----------------------------------------------------------------------------------------------------------------------

// REC: arm. Again while it waits for the bar line: cancel. Again while it records: stop at the bar line (update() sees the loop end).
void SampleRecScreen::rec_press(app::Context& ctx) noexcept {
  if (voc_ == nullptr) {
    return;
  }
  if (phase_ != Phase::Idle) {
    const bool armed = phase_ == Phase::Starting || voc_->loop_status(rec_slot_).rec == Rec::Armed;
    if (armed) {
      write_rec(ctx, false);
      phase_ = Phase::Idle;
      drop_ = false;
      say(ctx, "CANCELLED");
    } else if (!stop_) {
      write_rec(ctx, false);
      stop_ = true;
    }
    ctx.redraw();
    return;
  }
  if (waiting(slot_)) {  // its WAV is still on its way: a take now would be overwritten by the landing, or overwrite it
    say(ctx, "SLOT IS LOADING");
    return;
  }
  if (!ctx.audio_in().available()) {
    say(ctx, "NO INPUT");
    return;
  }
  rec_slot_ = slot_;
  stop_ = false;
  drop_ = false;
  t0_ = ctx.uptime_ms();
  phase_ = Phase::Starting;
  write_rec(ctx, false);  // the Rec parameter's stored value stays 1 after a stop: 0 then 1 records again
  write_rec(ctx, true);
  ctx.redraw();
}

// The slot leaves its side (if one holds it), stops and is emptied in the unit, so every screen sees it empty.
void SampleRecScreen::delete_press(app::Context& ctx) noexcept {
  if (voc_ == nullptr) {
    return;
  }
  if (phase_ != Phase::Idle) {
    const bool armed = phase_ == Phase::Starting || voc_->loop_status(rec_slot_).rec == Rec::Armed;
    if (armed) {  // nothing is recorded yet: cancel exactly as REC does, the slot keeps its audio (a second DELETE empties it)
      write_rec(ctx, false);
      phase_ = Phase::Idle;
      drop_ = false;
      say(ctx, "CANCELLED");
    } else {  // recording, or stopping until the bar line: the take ends there and is thrown away
      drop_ = true;
      if (!stop_) {
        write_rec(ctx, false);
        stop_ = true;
      }
    }
    ctx.redraw();
    return;
  }
  if (waiting(slot_)) {
    say(ctx, "SLOT IS LOADING");
    return;
  }
  if (!holds_audio(slot_)) {
    say(ctx, "NOTHING TO DELETE");
    return;
  }
  drop_ = true;
  rec_slot_ = slot_;
  finish(ctx);
}

// The take is over (or thrown away). Plays whole and looped on the side AFTER says, when AUTOPLAY is on.
void SampleRecScreen::finish(app::Context& ctx) noexcept {
  phase_ = Phase::Idle;
  stop_ = false;
  const unsigned slot = rec_slot_ & 3u;
  const std::uint32_t trim = trim_;
  trim_ = 0;
  const auto v = static_cast<std::uint8_t>(route::kFirstSlot + slot);
  route::Route cur = perform_->route_now(ctx);
  if (drop_) {
    drop_ = false;
    (void)ctx.engine().set_param(voice_, Vocoder::loop_param(slot, Vocoder::LoopField::Play), 0.0f);
    if (cur.speaks == v) {
      route::Route to = cur;
      to.speaks = route::kSpeaksLive;
      perform_->apply_route(ctx, route::Side::Speaks, cur, to);
      cur = to;
    }
    if (cur.sounds == v) {
      route::Route to = cur;
      to.sounds = route::kSoundsSynth;
      perform_->apply_route(ctx, route::Side::Sounds, cur, to);
    }
    const std::uint16_t rec = Vocoder::loop_param(slot, Vocoder::LoopField::Rec);
    (void)ctx.engine().set_param(voice_, rec, 2.0f);  // CLEAR, then back to OFF: a saved project must not hold it
    (void)ctx.engine().set_param(voice_, rec, 0.0f);
    say(ctx, "DELETED");
    return;
  }
  // The exact length of a take that stopped with the transport stopped: only down, only by what a frame can overshoot, only while the transport
  // is still stopped. Load, then back to 0: a saved project must not hold it (as for CLEAR above); the unit adopts it while the slot is idle.
  if (trim != 0u && !ctx.engine().playing()) {
    const std::uint32_t got = voc_->loop_status(slot).length;
    if (got > trim && got - trim <= kTrimMaxOver) {
      (void)ctx.engine().set_param(voice_, Vocoder::Load, static_cast<float>(slot * Loop::kMaxSamples + trim));
      (void)ctx.engine().set_param(voice_, Vocoder::Load, 0.0f);
    }
  }
  if (!autoplay_) {
    say(ctx, "TAKEN, NOT PLAYING");
    return;
  }
  if (cur.speaks == v || cur.sounds == v) {  // the slot is on a side already: it plays there again
    (void)ctx.engine().set_param(voice_, Vocoder::loop_param(slot, Vocoder::LoopField::Play), 1.0f);
    say(ctx, "PLAYING");
    return;
  }
  const sr::AfterPlan plan = sr::plan_after(cur, after_, slot);
  if (plan.place) {
    perform_->apply_route(ctx, plan.side, cur, plan.to);
    say(ctx, plan.side == route::Side::Speaks ? "PLAYING ON SPEAKS" : "PLAYING ON SOUNDS");
  } else if (plan.refused) {  // a slot is one reader: the other side holds it
    say(ctx, plan.side == route::Side::Speaks ? "SOUNDS HOLDS THE SLOT" : "SPEAKS HOLDS THE SLOT");
  } else {
    say(ctx, "TAKEN");
  }
}

void SampleRecScreen::tick(app::Context& ctx) noexcept {
  const Vocoder::LoopStatus ls = voc_->loop_status(rec_slot_);
  if (phase_ == Phase::Starting) {
    if (ls.rec != Rec::Idle) {
      phase_ = Phase::Active;
    } else if (ctx.uptime_ms() - t0_ > kStartMs) {
      phase_ = Phase::Idle;
      say(ctx, "FAILED");
      return;
    }
  }
  if (phase_ == Phase::Active) {
    if (ls.rec == Rec::Idle) {
      if (ls.length != 0u || drop_) {
        finish(ctx);
      } else {
        phase_ = Phase::Idle;
        say(ctx, "FAILED");
      }
      return;
    }
    const unsigned bars = sr::kLengthBars[length_ < sr::kLengthCount ? length_ : 0u];
    if (ls.rec == Rec::Recording && !stop_ && sr::auto_stops(length_)) {
      const dsp::Engine& e = ctx.engine();
      const float bar = pl::bar_samples(e.stats().tempo_mbpm);
      if (static_cast<float>(ls.length) >= pl::capture_stop_at(bars, bar, e.playing())) {
        write_rec(ctx, false);
        stop_ = true;
        // Stopped, the unit ends the take at the next block: up to a frame's worth past the mark, and loops do not resync at the bar.
        trim_ = e.playing() ? 0u : static_cast<std::uint32_t>(pl::capture_stop_at(bars, bar, false) + 0.5f);
      }
    }
  }
}

void SampleRecScreen::update(app::Context& ctx) noexcept {
  if (perform_ != nullptr) {
    perform_->update(ctx);  // the pads' bend and vibrato, as on PERFORM
  }
  if (voc_ != nullptr && phase_ != Phase::Idle) {
    tick(ctx);
  }
  if (note_ != nullptr && ctx.uptime_ms() >= note_until_) {
    note_ = nullptr;
  }
  ctx.redraw();  // the meter and the bar counter move every frame
}

// ---- input --------------------------------------------------------------------------------------------------------------------------

// `delta` is the encoder's detents, or +-1 for a tap or a button; a tap (`tap`) wraps where a detent stops at the end.
void SampleRecScreen::turn(app::Context& ctx, unsigned key, int delta) noexcept {
  const bool tap = delta == 1 && key_tap_;
  switch (key) {
    case 0: {  // SOURCE: through the app's switch, a line source at gain 0
      if (!ctx.audio_in().available() || step_source_ == nullptr) {
        say(ctx, "NO INPUT");
        return;
      }
      unsigned idx = 0;
      const SourceName* cur = find_source(ctx.audio_in().source());
      if (cur != nullptr) {
        idx = static_cast<unsigned>(cur - kSources);
      }
      const int n = static_cast<int>(kSourceCount);
      const int next = ((static_cast<int>(idx) + delta) % n + n) % n;
      const hal::capture::Source src = kSources[next].source;
      step_source_(source_user_, src, default_gain(src));
      break;
    }
    case 1: {  // GAIN
      if (!ctx.audio_in().available()) {
        say(ctx, "NO INPUT");
        return;
      }
      const int g = static_cast<int>(ctx.audio_in().gain()) + (tap ? static_cast<int>(kGainTap) : delta);
      (void)ctx.audio_in().set_gain(static_cast<unsigned>(g < 0 ? 0 : (g > 100 ? 100 : g)));
      break;
    }
    case 2:  // LENGTH
      length_ = static_cast<std::uint8_t>(tap ? (length_ + 1u) % sr::kLengthCount : sr::step_length(length_, delta));
      break;
    case 3: {  // AFTER
      const int n = static_cast<int>(sr::kAfterCount);
      after_ = static_cast<sr::After>(((static_cast<int>(after_) + delta) % n + n) % n);
      break;
    }
    case 4:  // AUTOPLAY
      autoplay_ = !autoplay_;
      break;
    case 5: {  // MONITOR: the kit's, on or off
      app::AudioIn& a = ctx.audio_in();
      if (!a.available()) {
        say(ctx, "NO INPUT");
        return;
      }
      a.monitor(tap || delta == 0 ? (a.monitor() == 0u ? app::AudioIn::kMonitorOn : 0u) : (delta > 0 ? app::AudioIn::kMonitorOn : 0u));
      break;
    }
    case 6: {  // SLOT
      if (phase_ != Phase::Idle) {
        say(ctx, "RECORDING");
        return;
      }
      slot_ = static_cast<std::uint8_t>(tap ? (slot_ + 1u) % 4u : (delta < 0 ? (slot_ > 0u ? slot_ - 1u : 0u) : (slot_ < 3u ? slot_ + 1u : 3u)));
      break;
    }
    default:
      return;
  }
  ctx.redraw();
}

void SampleRecScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  unsigned k = 0;
  if (in.fkey_press(k)) {
    if (in.shift || k > 6u) {
      return;
    }
    if (k == focus_ || k == 4u) {  // a second tap steps the value on (AUTOPLAY has nothing else to do)
      key_tap_ = true;
      turn(ctx, k, 1);
      key_tap_ = false;
    } else {
      focus_ = static_cast<std::uint8_t>(k);
      ctx.redraw();
    }
    return;
  }
  if (in.turn()) {
    key_tap_ = false;
    int d = in.delta();
    if (focus_ == 1u && in.shift) {
      d *= static_cast<int>(kGainTap);
    }
    turn(ctx, focus_, d);
    return;
  }
  if (in.press(Button::Rec)) {
    rec_press(ctx);
  } else if (in.press(Button::Delete)) {
    delete_press(ctx);
  } else if (in.press(Button::Up)) {
    turn(ctx, 6, -1);
  } else if (in.press(Button::Down)) {
    turn(ctx, 6, 1);
  } else if (in.press(Button::Left)) {  // repeats arrive as presses (repeat_buttons)
    turn(ctx, 2, -1);
  } else if (in.press(Button::Right)) {
    turn(ctx, 2, 1);
  } else if ((in.pad_press() || in.pad_release()) && perform_ != nullptr) {
    perform_->pad_event(ctx, in);
  }
}

// ---- pads, keys -----------------------------------------------------------------------------------------------------------------------

// The chord grid of PERFORM. While a take is on, the grid's root pads breathe with the beat (a pulse at the transport's tempo).
void SampleRecScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  if (perform_ == nullptr) {
    return;
  }
  perform_->leds(ctx, leds);
  if (phase_ == Phase::Idle || !sr::pulse_on(ctx.uptime_ms(), ctx.engine().stats().tempo_mbpm)) {
    return;
  }
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    if (leds.get(pad) == app::kLedLow) {
      leds.set(pad, app::kLedMid);
    }
  }
}

void SampleRecScreen::soft_keys(app::Context&, app::SoftKeys& out) noexcept {
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    out.label[i] = kKeyNames[i];
  }
}

// HELP: the pads come from PERFORM (they play its chords); every button input() answers here has a row (tests/sample_rec_test.cpp holds it).
void SampleRecScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Rec, "ARM, OR STOP AT THE BAR", nullptr},
      {Button::Delete, "CANCEL/DROP TAKE, EMPTY SLOT", nullptr},
      {Button::Up, "SLOT BEFORE", nullptr},
      {Button::Down, "SLOT AFTER", nullptr},
      {Button::Left, "SHORTER LENGTH, HOLD REPEATS", nullptr},
      {Button::Right, "LONGER LENGTH, HOLD REPEATS", nullptr},
      {Button::Params, "PARAMS: THREE PAGES", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER", nullptr},
      {Button::File, "FILE", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD", nullptr},
      {Button::SamplePlay, "SAMPLE: THE SLOTS", nullptr},
      {Button::SampleEdit, "SAMPLE EDIT: THE CUTS", nullptr},
      {Button::SampleRec, "THIS SCREEN", nullptr},
      {Button::Song, "SONG", nullptr},
      {Button::Pattern, "PATTERN: THE 16 STEPS", nullptr},
      {Button::Note, "PATTERN: NOTE LANE", nullptr},
      {Button::Instr, "PATTERN: CARRIER LANE", nullptr},
      {Button::Vol, "PATTERN: LEVEL LANE", nullptr},
      {Button::Fx, "PATTERN: SLICE LANE", nullptr},
      {Button::Shift, "FINER GAIN STEPS ON F2", nullptr},
  };
  if (perform_ != nullptr) {
    perform_->help(ctx, h);  // the pad labels and groups; the rest is replaced below
    h.fkey_shift[0] = nullptr;
    for (unsigned i = 0; i < app::kFKeyCount; ++i) {
      h.fkey_shift[i] = nullptr;
    }
  }
  h.summary = "REC ARMS AT THE NEXT BAR LINE; REC AGAIN OR THE LENGTH ENDS IT AT A BAR.\n"
              "THE TAKE LOOPS WHERE AFTER SAYS. THE PADS PLAY PERFORM'S LAYOUT.";
  h.fkey[0] = "THE INPUT SOURCE";
  h.fkey[1] = "INPUT GAIN";
  h.fkey[2] = "FREE, 1 TO 8 BARS";
  h.fkey[3] = "SPEAKS, SOUNDS, NONE";
  h.fkey[4] = "PLAY THE TAKE AT ONCE";
  h.fkey[5] = "INPUT TO THE MASTER";
  h.fkey[6] = "SLOT 1 TO 4";
  h.fkey[7] = nullptr;
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "THE LIT F KEY'S VALUE";
}

// ---- draw ---------------------------------------------------------------------------------------------------------------------------

void SampleRecScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  overview::shared().pump(r.display());  // a staged overview picture goes up to RAM_G (main loop, before the display list uses it)
  const dsp::Engine& e = ctx.engine();
  const app::AudioIn& ain = ctx.audio_in();
  const bool have = ain.available();
  const SourceName* src = have ? find_source(ain.source()) : nullptr;
  const unsigned mode = pl::mode_index(e.param(voice_, Vocoder::Mode));
  const unsigned tempo = e.stats().tempo_mbpm;
  const float bar = pl::bar_samples(tempo);
  const unsigned bars_set = sr::kLengthBars[length_ < sr::kLengthCount ? length_ : 0u];

  const unsigned shown = phase_ != Phase::Idle ? rec_slot_ : slot_;
  Vocoder::LoopStatus ls;
  if (voc_ != nullptr) {
    ls = voc_->loop_status(shown);
  }
  View view = View::Empty;
  if (phase_ == Phase::Starting || ls.rec == Rec::Armed) {
    view = View::Armed;
  } else if (ls.rec == Rec::Recording) {
    view = View::Recording;
  } else if (ls.rec == Rec::Stopping) {
    view = View::Stopping;
  } else if (holds_audio(shown)) {
    view = View::Take;
  }

  // The meter: a fall of a quarter of the distance a frame, as PERFORM's.
  const std::uint8_t raw = have && meter_ != nullptr ? pl::level_byte(meter_->peak()) : 0u;
  if (raw >= in_level_) {
    in_level_ = raw;
  } else {
    in_level_ = static_cast<std::uint8_t>(in_level_ - (static_cast<unsigned>(in_level_) - raw + 3u) / 4u);
  }
  const bool clip = have && meter_ != nullptr && meter_->clipping();

  ornoir::HeaderState h;
  header_route(e, voice_, src != nullptr ? src->label : "NONE", src != nullptr && src->source == hal::capture::Source::Usb, h);
  h.mode = pl::kModeNames[mode];
  h.playing = e.playing();
  h.tempo_x10 = tempo / 100u;
  h.cpu_percent = ctx.cpu_percent();

  ornoir::FKey keys[ornoir::kFKeys];
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    keys[i].name = kKeyNames[i];
    keys[i].hot = i == focus_ && i != 7u;
  }
  std::strncpy(keys[0].value, src != nullptr ? src->label : "NONE", sizeof keys[0].value - 1u);
  keys[0].role = ornoir::KeyRole::Voice;
  if (have) {
    keys[1].value[0] = '\0';
    cat_uint(keys[1].value, sizeof keys[1].value, ain.gain());
    cat(keys[1].value, sizeof keys[1].value, "%");
    keys[1].pct = static_cast<int>(ain.gain());
    cat(keys[5].value, sizeof keys[5].value, ain.monitor() != 0u ? "ON" : "OFF");
  } else {
    cat(keys[1].value, sizeof keys[1].value, "-");
    cat(keys[5].value, sizeof keys[5].value, "-");
  }
  cat(keys[2].value, sizeof keys[2].value, sr::kLengthNames[length_ < sr::kLengthCount ? length_ : 0u]);
  cat(keys[3].value, sizeof keys[3].value, sr::kAfterNames[static_cast<unsigned>(after_) < sr::kAfterCount ? static_cast<unsigned>(after_) : 0u]);
  cat(keys[4].value, sizeof keys[4].value, autoplay_ ? "ON" : "OFF");
  cat_uint(keys[6].value, sizeof keys[6].value, slot_ + 1u);
  keys[6].role = ornoir::KeyRole::Carrier;

  const route::Route rt = perform_ != nullptr ? perform_->route_now(ctx) : route::Route{};
  const auto sv = static_cast<std::uint8_t>(route::kFirstSlot + shown);

  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  // The IN meter, tall: the frame, the slot behind it, the level from the bottom.
  ornoir::fill_rect(c, 26, 98, 36, 276, Color::Empty);
  ornoir::fill_rect(c, 28, 100, 32, 272, Color::Pane);
  const int mh = static_cast<int>(in_level_) * 272 / 255;
  if (mh > 0) {
    ornoir::fill_rect(c, 28, 372 - mh, 32, mh, clip ? Color::Accent : Color::Play);
  }
  // The state's window.
  ornoir::fill_rect(c, 96, 124, 440, 52, Color::Empty);
  ornoir::fill_rect(c, 98, 126, 436, 48, view == View::Recording || view == View::Stopping ? Color::Accent : Color::Pane);
  // The four slots.
  for (unsigned i = 0; i < 4; ++i) {
    const int x = 96 + static_cast<int>(i) * 112;
    const bool cur = i == shown;
    ornoir::fill_rect(c, x, 272, 104, 52, cur ? Color::Accent : Color::Empty);
    ornoir::fill_rect(c, x + 2, 274, 100, 48, Color::Pane);
  }
  if (view != View::Recording && view != View::Stopping && overview::shared().shown(shown)) {  // the armed slot's picture, at 1x, in its side's ink
    const Color ink = rt.speaks == sv ? Color::Play : (rt.sounds == sv ? Color::Sel : Color::TextDim);
    overview::shared().draw(c, shown, false, kPicX, kPicY, ornoir::theme().rgb(ink));
  }
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  r.text(26, 80, "IN", Color::TextDim, Face::Label);
  char buf[40];
  buf[0] = '\0';
  cat(buf, sizeof buf, clip ? "CLIP" : "GAIN ");
  if (!clip) {
    cat_uint(buf, sizeof buf, have ? ain.gain() : 0u);
  }
  r.text(26, 380, buf, clip ? Color::Accent : Color::TextDim, Face::Label);

  r.text(96, 84, kSlotNames[shown & 3u], Color::Text, Face::Display);
  const sr::BarBeat bb = sr::bar_beat(e.stats().pos);
  buf[0] = '\0';
  cat(buf, sizeof buf, "BAR ");
  cat_uint(buf, sizeof buf, bb.bar);
  cat(buf, sizeof buf, ".");
  cat_uint(buf, sizeof buf, bb.beat);
  r.text(560, 84, buf, e.playing() ? Color::Play : Color::TextDim, Face::Display);
  r.text(560, 124, e.playing() ? "TRANSPORT RUNS" : "TRANSPORT STOPPED: A TAKE STARTS AT ONCE", Color::TextDim, Face::Micro);

  const char* word = "EMPTY";
  Color ink = Color::TextDim;
  switch (view) {
    case View::Armed: word = "ARMED"; ink = Color::Accent; break;
    case View::Recording: word = "REC"; ink = Color::AccentText; break;
    case View::Stopping: word = "STOPPING"; ink = Color::AccentText; break;
    case View::Take: word = "TAKE"; ink = Color::Play; break;
    case View::Empty: word = waiting(shown) ? "LOADING" : "EMPTY"; break;
  }
  r.text(centred(r, word, Face::Display, 316), 136, word, ink, Face::Display);

  buf[0] = '\0';
  switch (view) {
    case View::Armed:
      cat(buf, sizeof buf, e.playing() ? "WAITING FOR THE BAR LINE" : "STARTING");
      break;
    case View::Recording:
    case View::Stopping:
      cat(buf, sizeof buf, "BAR ");
      cat_uint(buf, sizeof buf, sr::bars_done(ls.length, bar) + 1u);
      if (bars_set != 0u) {
        cat(buf, sizeof buf, " OF ");
        cat_uint(buf, sizeof buf, bars_set);
      }
      cat(buf, sizeof buf, "   ");
      cat_tenths(buf, sizeof buf, sr::tenths_seconds(ls.length));
      cat(buf, sizeof buf, " S");
      break;
    case View::Take:
      cat_tenths(buf, sizeof buf, sr::tenths_bars(ls.length, bar));
      cat(buf, sizeof buf, " BARS   ");
      cat_tenths(buf, sizeof buf, sr::tenths_seconds(ls.length));
      cat(buf, sizeof buf, " S");
      break;
    case View::Empty:
      cat(buf, sizeof buf, "REC ARMS A TAKE");
      break;
  }
  r.text(centred(r, buf, Face::Label, 316), 190, buf, Color::Text, Face::Label);

  // Where the slot plays.
  const char* side = "ON NO SIDE";
  if (rt.speaks == sv && rt.sounds == sv) {
    side = "ON SPEAKS AND SOUNDS";
  } else if (rt.speaks == sv) {
    side = "ON SPEAKS";
  } else if (rt.sounds == sv) {
    side = "ON SOUNDS";
  }
  buf[0] = '\0';
  cat(buf, sizeof buf, side);
  if (view == View::Take) {
    cat(buf, sizeof buf, ls.playing ? "  PLAYING" : (ls.pending ? "  WAITING FOR THE BAR" : "  STOPPED"));
  }
  r.text(centred(r, buf, Face::Label, 316), 226, buf, view == View::Take && ls.playing ? Color::Play : Color::TextDim, Face::Label);

  for (unsigned i = 0; i < 4; ++i) {
    const int x = 96 + static_cast<int>(i) * 112;
    const Vocoder::LoopStatus s = voc_ != nullptr ? voc_->loop_status(i) : Vocoder::LoopStatus{};
    const auto v = static_cast<std::uint8_t>(route::kFirstSlot + i);
    const char* st = s.rec != Rec::Idle ? "REC" : (holds_audio(i) ? (rt.speaks == v ? "SPEAKS" : (rt.sounds == v ? "SOUNDS" : "HELD")) : (waiting(i) ? "LOADING" : "EMPTY"));
    r.text(x + 8, 280, kSlotNames[i], i == shown ? Color::Text : Color::TextDim, Face::Label);
    r.text(x + 8, 302, st, s.rec != Rec::Idle ? Color::Accent : (holds_audio(i) ? Color::Play : Color::TextDim), Face::Label);
  }
  if (note_ != nullptr) {
    r.text(96, 346, note_, Color::Accent, Face::Label);
  }
  r.text(96, 384, "PADS: PERFORM'S LAYOUT", Color::TextDim, Face::Micro);
}

}  // namespace o3_voc03er
