// SPDX-License-Identifier: GPL-3.0-or-later
#include "sample_edit_screen.h"

#include "sample_edit_logic.h"
#include "skin/ornoir.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/placement.h>

#include <cstring>

namespace o3_voc03er {

using namespace o3sdk;
namespace se = sample_edit;
namespace sm = sample;
using ui::Color;
using ui::Face;
using Button = app::Button;

namespace {

constexpr std::uint64_t kNoteMs = 2500;
constexpr std::uint8_t kFocusCursor = 0;  // the encoder moves the cursor
constexpr std::uint8_t kFocusCut = 1;     // the encoder steps F1 (CUT EVEN)

constexpr const char* kSlotNames[4] = {"SLOT 1", "SLOT 2", "SLOT 3", "SLOT 4"};
constexpr const char* kKeyNames[ornoir::kFKeys] = {"CUT EVEN", "BEATS", "HITS", "START", "END", "NORMALISE", "SAVE WAV", "CLR CUTS"};
constexpr std::uint8_t kLaterMask = 0x7Eu;  // F2..F7 draw dim: they come later

// The screen's state, in the SDRAM (O3SDK_BIG: zero at start, main loop only), so the screen object is the vtable and two pointers.
struct EditData {
  std::uint32_t cursor[sm::kReaders];  // per slot, in samples
  se::Arm arm;                         // the REPLACES warning
  std::uint8_t focus;                  // kFocusCursor or kFocusCut
  bool key_tap;                        // turn() came from a second tap on F1
  const char* note;                    // a moment's line (a static string or note_buf), until note_until
  std::uint64_t note_until;
  char note_buf[24];
};
O3SDK_BIG EditData g_se = {};

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

int centred(ui::Renderer& r, const char* s, Face f, int cx) noexcept { return cx - r.text_width(s, f) / 2; }

constexpr int kX = 26;
constexpr int kW = 748;
constexpr int kWaveY = 140;  // the rectangle U7b draws the waveform in
constexpr int kWaveH = 176;
constexpr int kBarY = 330;
constexpr int kBarH = 14;

}  // namespace

void SampleEditScreen::init(SampleScreen* sample, const std::int16_t* mem) noexcept {
  sample_ = sample;
  mem_ = mem;
  g_se = EditData{};
}

const ui::Theme* SampleEditScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void SampleEditScreen::enter(app::Context& ctx) noexcept {
  se::arm_cancel(g_se.arm);
  g_se.focus = kFocusCursor;
  g_se.note = nullptr;
  ctx.redraw();
}

void SampleEditScreen::leave(app::Context& ctx) noexcept {
  if (sample_ != nullptr) {
    sample_->pad_leave(ctx);
  }
}

std::uint64_t SampleEditScreen::repeat_buttons(const app::Context&) const noexcept {
  return (std::uint64_t{1} << static_cast<unsigned>(Button::Left)) | (std::uint64_t{1} << static_cast<unsigned>(Button::Right));
}

std::uint32_t SampleEditScreen::cursor() const noexcept {
  if (sample_ == nullptr) {
    return 0;
  }
  const unsigned slot = sample_->slot() & 3u;
  return se::clamp_cursor(g_se.cursor[slot], sample_->table(slot).length);
}

const std::int16_t* SampleEditScreen::samples() const noexcept {
  return mem_ != nullptr && sample_ != nullptr ? mem_ + static_cast<std::size_t>(sample_->slot() & 3u) * Loop::kMaxSamples : nullptr;
}

void SampleEditScreen::say(app::Context& ctx, const char* text) noexcept {
  g_se.note = text;
  g_se.note_until = ctx.uptime_ms() + kNoteMs;
  ctx.redraw();
}

void SampleEditScreen::set_cursor(app::Context& ctx, std::uint32_t pos) noexcept {
  const unsigned slot = sample_->slot() & 3u;
  g_se.cursor[slot] = se::clamp_cursor(pos, sample_->table(slot).length);
  ctx.redraw();
}

// True when the key must not change the table now: the slot records (the table is empty then) or holds nothing.
bool SampleEditScreen::refuse(app::Context& ctx, bool needs_cuts) noexcept {
  if (sample_->status(sample_->slot()).rec != Vocoder::LoopStatus::Rec::Idle) {
    say(ctx, "SLOT IS RECORDING");
    return true;
  }
  if (needs_cuts && sample_->table(sample_->slot()).length == 0u) {
    say(ctx, "EMPTY SLOT");
    return true;
  }
  return false;
}

// The table changed: the SPEAKS slot's cuts reach the step sequencer at the next service() pass.
static void table_changed(SampleScreen& s, unsigned slot, bool edited, unsigned code) noexcept {
  s.set_edited(slot, edited);
  s.set_even_code(slot, code);
  s.cuts_changed();
}

void SampleEditScreen::insert(app::Context& ctx) noexcept {
  if (refuse(ctx, true)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  sm::SliceTable& t = sample_->table(slot);
  const std::uint32_t pos = cursor();
  const std::int16_t* s = samples();
  const std::uint32_t snapped = s != nullptr ? se::snap_zero(s, t.length, pos) : pos;
  se::Edit r = se::insert_cut(t, snapped);
  if (!r.ok && snapped != pos) {
    r = se::insert_cut(t, pos);  // the zero crossing crowds a cut: the raw spot may not
  }
  if (!r.ok) {
    say(ctx, r.why);
    return;
  }
  table_changed(*sample_, slot, true, 3u);
  set_cursor(ctx, r.pos);
}

void SampleEditScreen::remove(app::Context& ctx) noexcept {
  if (refuse(ctx, true)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  const se::Edit r = se::delete_cut(sample_->table(slot), cursor());
  if (!r.ok) {
    say(ctx, r.why);
    return;
  }
  table_changed(*sample_, slot, true, 3u);
  ctx.redraw();
}

// F1's value `code` (se::even_cuts_of): OFF only leaves the cuts as they are.
void SampleEditScreen::recut(app::Context& ctx, unsigned code) noexcept {
  if (refuse(ctx, true)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  if (code == sample_->even_code(slot)) {
    return;
  }
  const unsigned n = se::even_cuts_of(code);
  if (n == 0u) {
    sample_->set_even_code(slot, code);
    ctx.redraw();
    return;
  }
  sm::SliceTable& t = sample_->table(slot);
  if (se::arm_step(g_se.arm, 0u, sample_->edited(slot)) == se::Gate::Warn) {
    g_se.note_buf[0] = '\0';
    cat(g_se.note_buf, sizeof g_se.note_buf, "REPLACES ");
    cat_uint(g_se.note_buf, sizeof g_se.note_buf, t.count);
    cat(g_se.note_buf, sizeof g_se.note_buf, " CUTS");
    say(ctx, g_se.note_buf);
    return;
  }
  sm::even_cuts(t, t.length, n);
  table_changed(*sample_, slot, true, code);
  g_se.note = nullptr;  // the warning has been answered
  ctx.redraw();
}

void SampleEditScreen::clear_cuts(app::Context& ctx) noexcept {
  if (refuse(ctx, true)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  sm::SliceTable& t = sample_->table(slot);
  if (se::arm_step(g_se.arm, 7u, sample_->edited(slot)) == se::Gate::Warn) {
    g_se.note_buf[0] = '\0';
    cat(g_se.note_buf, sizeof g_se.note_buf, "REPLACES ");
    cat_uint(g_se.note_buf, sizeof g_se.note_buf, t.count);
    cat(g_se.note_buf, sizeof g_se.note_buf, " CUTS");
    say(ctx, g_se.note_buf);
    return;
  }
  se::clear_cuts(t);
  table_changed(*sample_, slot, false, 3u);  // one cut over the slot: nothing of the hand's left to guard
  g_se.note = nullptr;
  ctx.redraw();
}

// F1's step: `delta` is the encoder's detents (or +1 for a second tap, which wraps).
void SampleEditScreen::turn(app::Context& ctx, int delta) noexcept {
  recut(ctx, se::step_even(sample_->even_code(sample_->slot()), delta, g_se.key_tap));
}

void SampleEditScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  if (sample_ == nullptr) {
    return;
  }
  unsigned k = 0;
  const bool fk = in.fkey_press(k);
  const bool keeps_arm = in.type == app::Input::Type::Release || (fk && !in.shift && (k == 0u || k == 7u)) ||
                         (in.turn() && g_se.focus == kFocusCut);
  if (!keeps_arm) {
    se::arm_cancel(g_se.arm);
  }
  if (fk) {
    if (in.shift) {
      return;
    }
    if (k == 0u) {
      if (g_se.focus == kFocusCut) {  // a second tap steps it
        g_se.key_tap = true;
        turn(ctx, 1);
        g_se.key_tap = false;
      }
      g_se.focus = kFocusCut;
      ctx.redraw();
    } else if (k == 7u) {
      clear_cuts(ctx);
    } else {
      say(ctx, "COMES LATER");  // BEATS, HITS, START, END, NORMALISE, SAVE WAV; the encoder stays where it was
    }
    return;
  }
  if (in.turn()) {
    g_se.key_tap = false;
    if (g_se.focus == kFocusCut) {
      turn(ctx, in.delta());
    } else {
      const unsigned slot = sample_->slot() & 3u;
      const std::uint32_t len = sample_->table(slot).length;
      set_cursor(ctx, se::move_cursor(cursor(), in.delta(), in.shift ? se::coarse_step(len) : 1u, len));
    }
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  if (in.press(Button::Up) || in.press(Button::Down)) {
    g_se.focus = kFocusCursor;
    sample_->set_slot(in.press(Button::Up) ? (slot > 0u ? slot - 1u : 0u) : slot + 1u);
    ctx.redraw();
  } else if (in.press(Button::Left) || in.press(Button::Right)) {  // repeats arrive as presses (repeat_buttons)
    g_se.focus = kFocusCursor;
    const sm::SliceTable& t = sample_->table(slot);
    set_cursor(ctx, in.press(Button::Left) ? se::prev_cut(t, cursor()) : se::next_cut(t, cursor()));
  } else if (in.press(Button::Insert)) {
    g_se.focus = kFocusCursor;
    insert(ctx);
  } else if (in.press(Button::Delete)) {
    g_se.focus = kFocusCursor;
    remove(ctx);
  } else {
    (void)sample_->pad_input(ctx, in);  // the pads and SHIFT's latch, as on SAMPLE
  }
}

void SampleEditScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  if (sample_ != nullptr) {
    sample_->pad_leds(ctx, leds);
  }
}

void SampleEditScreen::soft_keys(app::Context&, app::SoftKeys& out) noexcept {
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    out.label[i] = kKeyNames[i];
  }
  out.hot = g_se.focus == kFocusCut ? 1u : 0u;
  out.dim = kLaterMask;
}

void SampleEditScreen::update(app::Context& ctx) noexcept {
  if (sample_ == nullptr) {
    return;
  }
  sample_->pad_update(ctx);
  if (g_se.note != nullptr && ctx.uptime_ms() >= g_se.note_until) {
    g_se.note = nullptr;
  }
  ctx.redraw();  // the slot's length can change under a take
}

// HELP: the pads and their groups are SAMPLE's; the rest is this screen's.
void SampleEditScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Up, "SLOT BEFORE", nullptr},
      {Button::Down, "SLOT AFTER", nullptr},
      {Button::Left, "PREVIOUS CUT, HOLD REPEATS", nullptr},
      {Button::Right, "NEXT CUT, HOLD REPEATS", nullptr},
      {Button::Insert, "A CUT AT THE CURSOR", nullptr},
      {Button::Delete, "THE CUT AT THE CURSOR", nullptr},
      {Button::Shift, "COARSE CURSOR, LATCH CHORD", nullptr},
      {Button::Rec, "SAMPLE REC: A TAKE", nullptr},
      {Button::Params, "THE OLD PLAY SCREEN: LOOPS", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER (SLICE U9)", nullptr},
      {Button::File, "FILE (SLICE U9)", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD (SLICE U8)", nullptr},
      {Button::SamplePlay, "SAMPLE: THE FOUR SLOTS", nullptr},
      {Button::SampleEdit, "THIS SCREEN", nullptr},
      {Button::SampleRec, "SAMPLE REC: A TAKE", nullptr},
      {Button::Song, "SONG (SLICE U9)", nullptr},
      {Button::Pattern, "PATTERN: THE 16 STEPS", nullptr},
      {Button::Note, "PATTERN: NOTE LANE", nullptr},
      {Button::Instr, "PATTERN: CARRIER LANE", nullptr},
      {Button::Vol, "PATTERN: LEVEL LANE", nullptr},
      {Button::Fx, "PATTERN: SLICE LANE", nullptr},
  };
  if (sample_ != nullptr) {
    sample_->help(ctx, h);  // the pad labels and groups; the rest is replaced below
  }
  h.summary = "INSERT CUTS THE SLOT AT THE CURSOR; DELETE REMOVES THE CUT THAT STARTS THERE.\n"
              "F2 TO F7 (BEATS TO SAVE WAV) COME LATER.";
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    h.fkey[i] = nullptr;
    h.fkey_shift[i] = nullptr;
  }
  h.fkey[0] = "OFF, 8, 16 OR 24 EVEN CUTS";
  for (unsigned i = 1; i < 7; ++i) {
    h.fkey[i] = "COMES LATER";
  }
  h.fkey[7] = "ONE CUT OVER THE SLOT";
  h.pads = "UPPER ROWS: THE SLOT'S CUTS. LOWER ROWS: CHORDS (LATCH WITH SHIFT).";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "THE CURSOR, OR THE LIT F KEY";
}

void SampleEditScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  if (sample_ == nullptr) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  const sm::SliceTable& t = sample_->table(slot);
  const std::uint32_t len = t.length;
  const bool recording = sample_->status(slot).rec != Vocoder::LoopStatus::Rec::Idle;
  const std::uint32_t cur = cursor();

  ornoir::HeaderState h;
  sample_->fill_header(ctx, h);

  ornoir::FKey keys[ornoir::kFKeys];
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    keys[i].name = kKeyNames[i];
    keys[i].dim = ((kLaterMask >> i) & 1u) != 0u;
  }
  keys[0].hot = g_se.focus == kFocusCut;
  cat(keys[0].value, sizeof keys[0].value, se::even_name(sample_->even_code(slot)));

  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  ornoir::fill_rect(c, kX, kWaveY, kW, kWaveH, Color::Empty);  // U7b's rectangle
  ornoir::fill_rect(c, kX + 2, kWaveY + 2, kW - 4, kWaveH - 4, Color::Pane);
  ornoir::fill_rect(c, kX, kBarY, kW, kBarH, Color::Empty);
  if (len != 0u) {
    for (std::uint32_t s = se::kRateHz; s < len; s += se::kRateHz) {  // a tick a second
      ornoir::fill_rect(c, kX + static_cast<int>(sm::bar_x(s, len, kW - 2)), kBarY + kBarH - 4, 1, 4, Color::TextDim);
    }
    for (unsigned i = 0; i < t.count; ++i) {
      ornoir::fill_rect(c, kX + static_cast<int>(sm::bar_x(t.cut[i].start, len, kW - 2)), kBarY - 14, 2, kBarH + 28, Color::Accent);
    }
    ornoir::fill_rect(c, kX + static_cast<int>(sm::bar_x(cur, len, kW - 2)), kBarY - 24, 2, kBarH + 48, Color::Text);
  }
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  char buf[40];
  r.text(kX, 88, kSlotNames[slot], Color::Text, Face::Display);
  buf[0] = '\0';
  cat_uint(buf, sizeof buf, t.count);
  cat(buf, sizeof buf, t.count == 1u ? " CUT" : " CUTS");
  if (sample_->edited(slot)) {
    cat(buf, sizeof buf, "  EDITED");
  }
  r.text(kX + kW - r.text_width(buf, Face::Label), 100, buf, Color::Text, Face::Label);

  if (len == 0u) {
    const char* line = recording ? "RECORDING" : "EMPTY: SAMPLE REC OR SAMPLE LOAD";
    r.text(centred(r, line, Face::Label, kX + kW / 2), kWaveY + kWaveH / 2 - 8, line, Color::Accent, Face::Label);
  } else {
    int last = kX - 100;
    for (unsigned i = 0; i < t.count; ++i) {  // the numbers, skipping one that would sit on the one before
      char num[4];
      num[0] = '\0';
      cat_uint(num, sizeof num, i + 1u);
      const int x = kX + static_cast<int>(sm::bar_x(t.cut[i].start, len, kW - 2)) + 4;
      if (x >= last + 20) {
        r.text(x, kBarY - 30, num, Color::Accent, Face::Micro);
        last = x;
      }
    }
    char tm[12];
    se::time_text(cur, tm);
    int tx = kX + static_cast<int>(sm::bar_x(cur, len, kW - 2)) - r.text_width(tm, Face::Label) / 2;
    const int right = kX + kW - r.text_width(tm, Face::Label);
    tx = tx < kX ? kX : (tx > right ? right : tx);
    r.text(tx, kBarY + kBarH + 32, tm, Color::Text, Face::Label);
  }

  if (g_se.note != nullptr) {
    r.text(kX, 398, g_se.note, Color::Accent, Face::Label);
  } else {
    r.text(kX, 402, "INSERT CUTS AT THE CURSOR, DELETE REMOVES THE CUT UNDER IT", Color::TextDim, Face::Micro);
  }
}

}  // namespace o3_voc03er
