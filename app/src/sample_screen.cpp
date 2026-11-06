// SPDX-License-Identifier: GPL-3.0-or-later
#include "sample_screen.h"

#include "perform_logic.h"
#include "sample_logic.h"
#include "sample_rec_logic.h"
#include "skin/ornoir.h"

#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/context.h>
#include <o3sdk/app/pad_leds.h>

#include <cstring>

namespace o3_voc03er {

using namespace o3sdk;
namespace pl = perform;
namespace sm = sample;
namespace sr = sample_rec;
using ui::Color;
using ui::Face;
using Button = app::Button;
using Field = Vocoder::LoopField;

namespace {

constexpr std::uint64_t kNoteMs = 2500;
constexpr int kLevelTap = 6;  // a second tap on F8 moves the level this many half-dB (a detent moves 1)

constexpr const char* kSlotNames[4] = {"SLOT 1", "SLOT 2", "SLOT 3", "SLOT 4"};
constexpr const char* kKeyNames[ornoir::kFKeys] = {"CHORD", "SIDE", "SPEED", "DIR", "LOOP", "", "", "LEVEL"};

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

void cat_tenths(char* dst, std::size_t cap, unsigned tenths) noexcept {
  cat_uint(dst, cap, tenths / 10u);
  cat(dst, cap, ".");
  cat_uint(dst, cap, tenths % 10u);
}

int centred(ui::Renderer& r, const char* s, Face f, int cx) noexcept { return cx - r.text_width(s, f) / 2; }

unsigned round_param(const dsp::Engine& e, dsp::UnitId id, std::uint16_t p) noexcept {
  const float v = e.param(id, p) + 0.5f;
  return v > 0.0f ? static_cast<unsigned>(v) : 0u;
}

Color side_ink(sm::Side s) noexcept { return s == sm::Side::Speaks ? Color::Play : (s == sm::Side::Sounds ? Color::Sel : Color::TextDim); }

}  // namespace

void SampleScreen::init(dsp::UnitId voice, const Vocoder* voc, PerformScreen* perform) noexcept {
  voice_ = voice;
  voc_ = voc;
  perform_ = perform;
}

const ui::Theme* SampleScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void SampleScreen::enter(app::Context& ctx) noexcept {
  note_ = nullptr;
  ctx.redraw();
}

void SampleScreen::leave(app::Context& ctx) noexcept {
  if (perform_ != nullptr) {
    perform_->leave(ctx);  // releases the held pads
  }
}

std::uint64_t SampleScreen::repeat_buttons(const app::Context&) const noexcept {
  return (std::uint64_t{1} << static_cast<unsigned>(Button::Left)) | (std::uint64_t{1} << static_cast<unsigned>(Button::Right));
}

void SampleScreen::say(app::Context& ctx, const char* text) noexcept {
  note_ = text;
  note_until_ = ctx.uptime_ms() + kNoteMs;
  ctx.redraw();
}

void SampleScreen::write(app::Context& ctx, Field f, float v) noexcept {
  (void)ctx.engine().set_param(voice_, Vocoder::loop_param(slot_, f), v);
}

// F2: the slot moves to the next side. The side it leaves is freed first, so each write keeps the route allowed and the unit stops or plays
// the slot by the roles (PerformScreen::apply_route).
void SampleScreen::set_side(app::Context& ctx, int delta) noexcept {
  if (voc_ == nullptr || perform_ == nullptr) {
    return;
  }
  const route::Route cur = perform_->route_now(ctx);
  const sm::Side target = sm::step_side(sm::side_of(cur, slot_), delta);
  const sm::SidePlan p = sm::plan_side(cur, target, slot_, voc_->loop_status(slot_).length == 0u);
  if (p.refused) {
    say(ctx, p.why);
    return;
  }
  if (!p.change) {
    return;
  }
  const std::uint8_t v = sm::slot_value(slot_);
  route::Route mid = cur;
  if (cur.speaks == v && p.to.speaks != v) {  // leave first, then enter: the slot is never on both sides
    mid.speaks = p.to.speaks;
    perform_->apply_route(ctx, route::Side::Speaks, cur, mid);
  }
  if (cur.sounds == v && p.to.sounds != v) {
    const route::Route from = mid;
    mid.sounds = p.to.sounds;
    perform_->apply_route(ctx, route::Side::Sounds, from, mid);
  }
  if (p.to.speaks == v && mid.speaks != v) {
    const route::Route from = mid;
    mid.speaks = v;
    perform_->apply_route(ctx, route::Side::Speaks, from, mid);
  }
  if (p.to.sounds == v && mid.sounds != v) {
    const route::Route from = mid;
    mid.sounds = v;
    perform_->apply_route(ctx, route::Side::Sounds, from, mid);
  }
}

// `delta` is the encoder's detents, or +-1 for a tap; a tap wraps where a detent stops at the end.
void SampleScreen::turn(app::Context& ctx, unsigned key, int delta) noexcept {
  const bool tap = delta == 1 && key_tap_;
  const Vocoder::LoopStatus ls = voc_ != nullptr ? voc_->loop_status(slot_) : Vocoder::LoopStatus{};
  switch (key) {
    case 0:  // CHORD
      if (perform_ != nullptr) {
        perform_->step_chord(ctx, delta);
      }
      break;
    case 1:  // SIDE
      set_side(ctx, delta);
      break;
    case 2:  // SPEED
      write(ctx, Field::Speed, static_cast<float>(sm::step_speed(ls.speed, delta, tap)));
      break;
    case 3:  // DIR
      write(ctx, Field::Dir, ls.reverse ? 0.0f : 1.0f);
      break;
    case 4:  // LOOP
      if (ls.length == 0u) {
        say(ctx, "EMPTY: SAMPLE REC OR SAMPLE LOAD");
        return;
      }
      write(ctx, Field::Play, ls.playing || ls.pending ? 0.0f : 1.0f);
      break;
    case 7:  // LEVEL
      write(ctx, Field::Level, sm::step_level(ls.level_db, tap ? kLevelTap : delta));
      break;
    default:
      return;
  }
  ctx.redraw();
}

void SampleScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  unsigned k = 0;
  if (in.fkey_press(k)) {
    if (in.shift || k == 5u || k == 6u) {
      return;
    }
    if (k == focus_ || k == 3u || k == 4u) {  // a second tap steps the value on (DIR and LOOP have nothing else to do)
      key_tap_ = true;
      turn(ctx, k, 1);
      key_tap_ = false;
    }
    focus_ = static_cast<std::uint8_t>(k);
    ctx.redraw();
    return;
  }
  if (in.turn()) {
    key_tap_ = false;
    turn(ctx, focus_, in.delta());
    return;
  }
  if (in.press(Button::Up)) {
    slot_ = static_cast<std::uint8_t>(slot_ > 0u ? slot_ - 1u : 0u);
    ctx.redraw();
  } else if (in.press(Button::Down)) {
    slot_ = static_cast<std::uint8_t>(slot_ < 3u ? slot_ + 1u : 3u);
    ctx.redraw();
  } else if ((in.press(Button::Left) || in.press(Button::Right)) && perform_ != nullptr) {  // repeats arrive as presses (repeat_buttons)
    perform_->step_octave(ctx, in.press(Button::Left) ? -1 : 1);
  } else if ((in.press(Button::Shift) || in.press(Button::Delete)) && perform_ != nullptr) {  // PERFORM's latch and release
    perform_->input(ctx, in);
  } else if ((in.pad_press() || in.pad_release()) && perform_ != nullptr && sm::chord_pad(in.pad())) {
    perform_->pad_event(ctx, in);
  }
}

void SampleScreen::update(app::Context& ctx) noexcept {
  if (perform_ != nullptr) {
    perform_->update(ctx);  // the pads' bend and vibrato, as on PERFORM
  }
  if (note_ != nullptr && ctx.uptime_ms() >= note_until_) {
    note_ = nullptr;
  }
  ctx.redraw();  // the play state moves with the bar
}

// The chord grid of PERFORM's two lower rows; the top two rows stay dark until the slices.
void SampleScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  if (perform_ == nullptr) {
    return;
  }
  perform_->leds(ctx, leds);
  for (unsigned pad = 0; pad < sm::kTopPads; ++pad) {
    leds.set(pad, app::kLedOff);
  }
}

void SampleScreen::soft_keys(app::Context&, app::SoftKeys& out) noexcept {
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    out.label[i] = kKeyNames[i];
  }
}

// HELP: the pads come from PERFORM (they play its chords); every button input() answers here has a row (tests/sample_test.cpp holds it).
void SampleScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Up, "SLOT BEFORE", nullptr},
      {Button::Down, "SLOT AFTER", nullptr},
      {Button::Left, "OCTAVE DOWN, HOLD REPEATS", nullptr},
      {Button::Right, "OCTAVE UP, HOLD REPEATS", nullptr},
      {Button::Shift, "LATCH THE HELD CHORD PAD", nullptr},
      {Button::Delete, "RELEASE THE LATCHED CHORD", nullptr},
      {Button::Rec, "SAMPLE REC: A TAKE", nullptr},
      {Button::Params, "THE OLD PLAY SCREEN: LOOPS", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER (SLICE U9)", nullptr},
      {Button::File, "FILE (SLICE U9)", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD (SLICE U8)", nullptr},
      {Button::SamplePlay, "THIS SCREEN", nullptr},
      {Button::SampleEdit, "SAMPLE EDIT (SLICE U7)", nullptr},
      {Button::SampleRec, "SAMPLE REC: A TAKE", nullptr},
      {Button::Song, "SONG (SLICE U9)", nullptr},
      {Button::Pattern, "PATTERN (SLICE U6)", nullptr},
      {Button::Note, "PATTERN: NOTE LANE (U6)", nullptr},
      {Button::Instr, "PATTERN: CARRIER LANE (U6)", nullptr},
      {Button::Vol, "PATTERN: LEVEL LANE (U6)", nullptr},
      {Button::Fx, "PATTERN: SLICE LANE (U6)", nullptr},
  };
  if (perform_ != nullptr) {
    perform_->help(ctx, h);  // the pad labels and groups; the rest is replaced below
    for (unsigned i = 0; i < app::kFKeyCount; ++i) {
      h.fkey[i] = nullptr;
      h.fkey_shift[i] = nullptr;
    }
  }
  h.summary = "THE FOUR SLOTS: UP AND DOWN PICK ONE; ITS SIDE, SPEED, DIRECTION AND LEVEL ARE THE F KEYS.\n"
              "THE LOWER PAD ROWS PLAY CHORDS; THE UPPER ROWS ARE DARK UNTIL THE SLICES.";
  h.fkey[0] = "THE CHORD ROW'S KIND";
  h.fkey[1] = "SPEAKS, SOUNDS, NONE";
  h.fkey[2] = "0.5X, 1X, 2X";
  h.fkey[3] = "FORWARD, REVERSE";
  h.fkey[4] = "THE WHOLE SLOT PLAYS ROUND";
  h.fkey[7] = "-24 TO +12 DB";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "THE LIT F KEY'S VALUE";
}

// ---- draw ---------------------------------------------------------------------------------------------------------------------------

namespace {
constexpr int kColX = 26;
constexpr int kColW = 252;
constexpr int kRowH = 70;
constexpr int kRowY = 88;
constexpr int kMidX = 300;
constexpr int kMidW = 270;
constexpr int kRightX = 592;
}  // namespace

void SampleScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  const dsp::Engine& e = ctx.engine();
  const app::AudioIn& ain = ctx.audio_in();
  const SourceName* src = ain.available() ? find_source(ain.source()) : nullptr;
  const unsigned car = round_param(e, voice_, Vocoder::CarSrc);
  const unsigned mode = pl::mode_index(e.param(voice_, Vocoder::Mode));
  const unsigned tempo = e.stats().tempo_mbpm;
  const float bar = pl::bar_samples(tempo);
  const route::Route rt = perform_ != nullptr ? perform_->route_now(ctx) : route::Route{};
  const unsigned kind = perform_ != nullptr ? perform_->chord_kind() : 0u;

  Vocoder::LoopStatus st[4];
  for (unsigned i = 0; i < 4 && voc_ != nullptr; ++i) {
    st[i] = voc_->loop_status(i);
  }
  const Vocoder::LoopStatus& ls = st[slot_ & 3u];
  const bool empty = ls.length == 0u;
  const sm::Side side = sm::side_of(rt, slot_);

  ornoir::HeaderState h;
  h.speaks = rt.speaks >= route::kFirstSlot ? kSlotNames[(rt.speaks - route::kFirstSlot) & 3u] : (src != nullptr ? src->label : "NONE");
  h.sounds = rt.sounds >= route::kFirstSlot && rt.sounds < route::kSoundsLive ? kSlotNames[(rt.sounds - route::kFirstSlot) & 3u]
                                                                              : pl::kCarrierNames[car < 8u ? car : 0u];
  h.mode = pl::kModeNames[mode];
  h.playing = e.playing();
  h.tempo_x10 = tempo / 100u;
  h.cpu_percent = ctx.cpu_percent();

  ornoir::FKey keys[ornoir::kFKeys];
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    keys[i].name = kKeyNames[i];
    keys[i].hot = i == focus_ && i != 5u && i != 6u;
  }
  cat(keys[0].value, sizeof keys[0].value, pl::kKindNames[kind & 3u]);
  cat(keys[1].value, sizeof keys[1].value, sm::kSideNames[static_cast<unsigned>(side)]);
  keys[1].role = side == sm::Side::Speaks ? ornoir::KeyRole::Voice : (side == sm::Side::Sounds ? ornoir::KeyRole::Carrier : ornoir::KeyRole::Plain);
  cat(keys[2].value, sizeof keys[2].value, sm::kSpeedNames[ls.speed < sm::kSpeedCount ? ls.speed : 1u]);
  cat(keys[3].value, sizeof keys[3].value, sm::kDirNames[ls.reverse ? 1 : 0]);
  cat(keys[4].value, sizeof keys[4].value, ls.playing || ls.pending ? "ON" : "OFF");
  {
    char num[8];
    sm::level_text(ls.level_db, num);
    cat(keys[7].value, sizeof keys[7].value, num);
    cat(keys[7].value, sizeof keys[7].value, " DB");
    keys[7].pct = sm::level_percent(ls.level_db);
  }

  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  for (unsigned i = 0; i < 4; ++i) {
    const int y = kRowY + static_cast<int>(i) * kRowH;
    ornoir::fill_rect(c, kColX, y, kColW, kRowH - 6, i == slot_ ? Color::Accent : Color::Empty);
    ornoir::fill_rect(c, kColX + 2, y + 2, kColW - 4, kRowH - 10, Color::Pane);
  }
  ornoir::fill_rect(c, kMidX, kRowY, kMidW, 4 * kRowH - 6, Color::Empty);
  ornoir::fill_rect(c, kMidX + 2, kRowY + 2, kMidW - 4, 4 * kRowH - 10, Color::Pane);
  ornoir::fill_rect(c, kRightX, kRowY, 182, 4 * kRowH - 6, Color::Empty);
  ornoir::fill_rect(c, kRightX + 2, kRowY + 2, 178, 4 * kRowH - 10, Color::Pane);
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  char buf[40];
  for (unsigned i = 0; i < 4; ++i) {
    const int y = kRowY + static_cast<int>(i) * kRowH;
    const Vocoder::LoopStatus& s = st[i];
    const bool full = s.length != 0u;
    const sm::Side sd = sm::side_of(rt, i);
    r.text(kColX + 10, y + 8, kSlotNames[i], i == slot_ ? Color::Text : Color::TextDim, Face::Label);
    buf[0] = '\0';
    if (full) {
      cat_tenths(buf, sizeof buf, sr::tenths_bars(s.length, bar));
      cat(buf, sizeof buf, " BARS");
    } else {
      cat(buf, sizeof buf, "EMPTY");
    }
    r.text(kColX + kColW - 10 - r.text_width(buf, Face::Label), y + 8, buf, full ? Color::Text : Color::TextDim, Face::Label);
    if (full) {
      buf[0] = '\0';
      cat(buf, sizeof buf, sm::kSpeedNames[s.speed < sm::kSpeedCount ? s.speed : 1u]);
      cat(buf, sizeof buf, "  ");
      cat(buf, sizeof buf, sm::kDirNames[s.reverse ? 1 : 0]);
      r.text(kColX + 10, y + 38, buf, Color::Text, Face::Label);
      r.text(kColX + kColW - 10 - r.text_width(sm::kSideNames[static_cast<unsigned>(sd)], Face::Label), y + 38, sm::kSideNames[static_cast<unsigned>(sd)],
             side_ink(sd), Face::Label);
    }
  }

  // The middle: the slot's name, its play state and where it plays (no overview yet: LoopStatus has no play position, slice U7 draws the wave).
  const int mid = kMidX + kMidW / 2;
  r.text(centred(r, kSlotNames[slot_ & 3u], Face::Display, mid), kRowY + 14, kSlotNames[slot_ & 3u], Color::Text, Face::Display);
  if (empty) {
    r.text(centred(r, "EMPTY: SAMPLE REC", Face::Label, mid), kRowY + 100, "EMPTY: SAMPLE REC", Color::Accent, Face::Label);
    r.text(centred(r, "OR SAMPLE LOAD", Face::Label, mid), kRowY + 126, "OR SAMPLE LOAD", Color::Accent, Face::Label);
  } else {
    buf[0] = '\0';
    cat_tenths(buf, sizeof buf, sr::tenths_bars(ls.length, bar));
    cat(buf, sizeof buf, " BARS   ");
    cat_tenths(buf, sizeof buf, sr::tenths_seconds(ls.length));
    cat(buf, sizeof buf, " S");
    r.text(centred(r, buf, Face::Label, mid), kRowY + 100, buf, Color::Text, Face::Label);
    const char* word = ls.playing ? "PLAYING" : (ls.pending ? "WAITING FOR THE BAR" : "STOPPED");
    r.text(centred(r, word, Face::Label, mid), kRowY + 134, word, ls.playing ? Color::Play : Color::TextDim, Face::Label);
    const char* on = side == sm::Side::Speaks ? "ON SPEAKS" : (side == sm::Side::Sounds ? "ON SOUNDS" : "ON NO SIDE");
    r.text(centred(r, on, Face::Label, mid), kRowY + 168, on, side_ink(side), Face::Label);
  }

  // The right: SOUNDS as PERFORM shows it, and the chord.
  r.text(kRightX + 12, kRowY + 12, "SOUNDS", Color::TextDim, Face::Micro);
  r.text(kRightX + 12, kRowY + 34, h.sounds, Color::Sel, Face::Label);
  r.text(kRightX + 12, kRowY + 100, "CHORD", Color::TextDim, Face::Micro);
  r.text(kRightX + 12, kRowY + 122, pl::kKindNames[kind & 3u], Color::Text, Face::Label);

  if (note_ != nullptr) {
    r.text(kColX, kRowY + 4 * kRowH + 2, note_, Color::Accent, Face::Label);
  } else {
    r.text(kColX, kRowY + 4 * kRowH + 6, "PADS: THE CHORDS OF PERFORM, LOWER TWO ROWS", Color::TextDim, Face::Micro);
  }
}

}  // namespace o3_voc03er
