// SPDX-License-Identifier: GPL-3.0-or-later
#include "ornoir.h"

#include "ornoir_assets.h"

#include <o3sdk/eve/dl.h>

#include <cstddef>
#include <cstring>

namespace o3_voc03er::ornoir {

namespace ui = o3sdk::ui;
namespace plot = o3sdk::ui::plot;
using ui::Color;
using ui::Face;
using ui::Rect;
using ui::Rgb;

namespace {

// The page's tokens (docs/UI-DESIGN.md, the OR NOIR preview).
constexpr ui::Theme kTheme = {
    "or_noir",
    {
        {40, 34, 24},     // Chrome: the warm panel
        {246, 240, 226},  // ChromeText
        {192, 176, 136},  // ChromeDim
        {0, 0, 0},        // Pane: black (the runtime clears to it)
        {248, 244, 232},  // Text
        {176, 164, 134},  // TextDim
        {74, 58, 28},     // Empty: the unlit pinhole
        {234, 142, 86},   // Instr
        {255, 232, 164},  // Vol
        {160, 118, 44},  // Fx: brass, the F row's lower band
        {196, 218, 238},  // Sel: what sounds
        {0, 0, 0},        // SelText
        {255, 255, 255},  // Accent: the latched slot, a toast
        {0, 0, 0},        // AccentText
        {255, 190, 36},   // Play: what speaks
        {0, 0, 0},        // PlayText
        {255, 56, 40},    // Rec
    },
    {ui::Font::F16x32, ui::Font::F10x20, ui::Font::F8x16, ui::Font::F6x12},
    {1, 4, 4, 24, 32, 26, 16, 22, 30, 4},
};
static_assert(kTheme.colour[static_cast<unsigned>(Color::Pane)].r == 0 && kTheme.colour[static_cast<unsigned>(Color::Pane)].g == 0 &&
                  kTheme.colour[static_cast<unsigned>(Color::Pane)].b == 0,
              "the frame's CLEAR is the pane, which is black");

Rgb col(Color c) noexcept { return kTheme.rgb(c); }
int len(const char* s) noexcept { return static_cast<int>(std::strlen(s)); }

// Layout (px).
constexpr int kVx = 256, kVy = 72, kHorizon = 204, kPitch = 6, kColStride = 18, kColW = 12, kRowsUp = 22;
constexpr int kPadX = 64, kPadY = 292, kPadW = 60, kPadH = 27;
constexpr std::uint8_t kDim = 120;  // the unlit pinhole's alpha
constexpr unsigned kAtlasAdvance = 32;

void fill(plot::Canvas& c, int x, int y, int w, int h, Color k, std::uint8_t a = 255) noexcept { c.rect(Rect{x, y, w, h}, col(k), a); }

}  // namespace

void fill_rect(plot::Canvas& c, int x, int y, int w, int h, Color k) noexcept { fill(c, x, y, w, h, k); }

namespace {

// The list over a column: a frame, the cursor's row lit (Accent), the row in force marked with a dot in the column's colour.
constexpr int kPickY = 96, kPickRowH = 14;

void picker_shapes(plot::Canvas& c, const PickerView& p, int x, int w, Color side) noexcept {
  const unsigned n = p.count < kPickerRows ? p.count : kPickerRows;
  const int h = static_cast<int>(n) * kPickRowH + 4;
  fill(c, x, kPickY - 2, w, h, Color::Empty, kDim);
  fill(c, x, kPickY - 2, w, 1, side);
  fill(c, x, kPickY - 3 + h, w, 1, side);
  for (unsigned i = 0; i < n; ++i) {
    const int y = kPickY + static_cast<int>(i) * kPickRowH;
    if (i == p.cursor) fill(c, x + 1, y, w - 2, kPickRowH, Color::Accent);
    if (p.row[i].on) c.round_rect(Rect{x + 5, y + 4, 6, 6}, 128, col(i == p.cursor ? Color::AccentText : side));
  }
}

void picker_text(ui::Renderer& r, const PickerView& p, int x, int w) noexcept {
  const unsigned n = p.count < kPickerRows ? p.count : kPickerRows;
  for (unsigned i = 0; i < n; ++i) {
    const PickerRow& row = p.row[i];
    const int y = kPickY + static_cast<int>(i) * kPickRowH + 1;
    const bool hot = i == p.cursor;
    r.text(x + 16, y, row.name, hot ? Color::AccentText : row.dimmed ? Color::TextDim : Color::Text, Face::Micro);
    if (row.note[0] != '\0') {
      r.text(x + w - 6 - len(row.note) * 6, y, row.note, hot ? Color::AccentText : row.dimmed ? Color::Rec : Color::TextDim, Face::Micro);
    }
  }
}

}  // namespace

const ui::Theme& theme() noexcept { return kTheme; }

// ---- small formats --------------------------------------------------------------------------------------------------------------------

int chip_width(const char* text) noexcept { return len(text) * 8 + 20; }
int sounds_chip_x(const char* speaks) noexcept { return 136 + chip_width(speaks) + 18; }

unsigned format_tempo(unsigned tempo_x10, char* out) noexcept {
  unsigned n = 0;
  const unsigned whole = tempo_x10 / 10u;
  char tmp[6];
  unsigned t = 0;
  unsigned w = whole;
  do {
    tmp[t++] = static_cast<char>('0' + w % 10u);
    w /= 10u;
  } while (w != 0 && t < sizeof tmp);
  while (t != 0) {
    out[n++] = tmp[--t];
  }
  out[n++] = '.';
  out[n++] = static_cast<char>('0' + tempo_x10 % 10u);
  out[n] = '\0';
  return n;
}

unsigned format_percent(unsigned percent, char* out) noexcept {
  unsigned n = 0;
  char tmp[4];
  unsigned t = 0;
  unsigned p = percent > 999u ? 999u : percent;
  do {
    tmp[t++] = static_cast<char>('0' + p % 10u);
    p /= 10u;
  } while (p != 0 && t < sizeof tmp);
  while (t != 0) {
    out[n++] = tmp[--t];
  }
  out[n++] = '%';
  out[n] = '\0';
  return n;
}

unsigned note_name(int note, char* out) noexcept {
  static const char kPc[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  unsigned n = 0;
  if (note < 0 || note > 127) {
    out[0] = '-';
    out[1] = '\0';
    return 1;
  }
  for (const char* s = kPc[note % 12]; *s != '\0'; ++s) {
    out[n++] = *s;
  }
  const int oct = note / 12 - 1;
  if (oct < 0) {
    out[n++] = '-';
    out[n++] = '1';
  } else {
    out[n++] = static_cast<char>('0' + oct % 10);
  }
  out[n] = '\0';
  return n;
}

bool atlas_can_spell(const char* name) noexcept {
  if (len(name) > 6 || name[0] == '\0') {  // six cells of 32 px fill the readout
    return false;
  }
  for (const char* s = name; *s != '\0'; ++s) {
    if (glyph_cell(*s) == 0) {
      return false;
    }
  }
  return true;
}

unsigned bar_rows(std::uint8_t level) noexcept {
  const unsigned n = (static_cast<unsigned>(level) * kRowsUp + 127u) / 255u;
  return n < 1u ? 1u : n;
}
unsigned peak_rows(std::uint8_t peak) noexcept {
  const unsigned n = (static_cast<unsigned>(peak) * kRowsUp + 127u) / 255u;
  return n > kRowsUp - 1u ? kRowsUp - 1u : n;
}
unsigned ladder_height(std::uint8_t level, unsigned full_px) noexcept { return (static_cast<unsigned>(level) * full_px / 255u) / 6u * 6u; }

// ---- header ---------------------------------------------------------------------------------------------------------------------------

void header_shapes(plot::Canvas& c, const HeaderState& h) noexcept {
  fill(c, 0, 0, 800, 38, Color::Chrome);
  fill(c, 0, 38, 800, 1, Color::Empty);
  fill(c, 0, 39, 800, 1, Color::Play);
  const int w1 = chip_width(h.speaks);
  const int x2 = sounds_chip_x(h.speaks);
  c.round_rect(Rect{136, 9, w1, 20}, 160, col(Color::Play));
  c.round_rect(Rect{x2, 9, chip_width(h.sounds), 20}, 160, col(Color::Sel));
  const int cx = 136 + w1 + 9;
  c.line((cx - 2) * 16, 14 * 16, (cx + 2) * 16, 19 * 16, 16, col(Color::TextDim));
  c.line((cx + 2) * 16, 19 * 16, (cx - 2) * 16, 24 * 16, 16, col(Color::TextDim));
  c.round_rect(Rect{528, 9, 52, 20}, 160, h.playing ? col(Color::Play) : col(Color::Empty));
}

void header_text(ui::Renderer& r, const HeaderState& h) noexcept {
  // The wordmark: one line of text under four scissor bands (a scissor is two words, end_raw restores it).
  struct Band {
    int y0, y1;
    Color k;
  };
  static const Band kBands[] = {{0, 9, Color::Chrome}, {9, 15, Color::Accent}, {15, 20, Color::Sel}, {20, 22, Color::Empty}, {22, 38, Color::Play}};
  for (const Band& b : kBands) {
    if (b.k == Color::Chrome) {
      continue;  // the panel itself above the cut
    }
    r.begin_raw();
    r.raw(o3sdk::eve::dl::scissor_xy(0, static_cast<std::uint32_t>(b.y0)));
    r.raw(o3sdk::eve::dl::scissor_size(136, static_cast<std::uint32_t>(b.y1 - b.y0)));
    r.text(16, 3, "VOC03ER", b.k, Face::Display);
    r.end_raw();
  }
  r.text(146, 11, h.speaks, Color::PlayText, Face::Label);
  r.text(sounds_chip_x(h.speaks) + 10, 11, h.sounds, Color::SelText, Face::Label);
  r.text(334, 9, h.mode, Color::Text, Face::Data);
  char buf[8];
  r.text(416, 5, "TEMPO", Color::TextDim, Face::Micro);
  format_tempo(h.tempo_x10, buf);
  r.text(416, 16, buf, Color::Text, Face::Data);
  r.text(540, 11, h.playing ? "PLAY" : "STOP", h.playing ? Color::PlayText : Color::Text, Face::Label);
  r.text(740, 5, "CPU", Color::TextDim, Face::Micro);
  format_percent(h.cpu_percent, buf);
  r.text(740, 16, buf, Color::Text, Face::Data);
}

// ---- F row ----------------------------------------------------------------------------------------------------------------------------

constexpr Rgb kGoldBand{228, 186, 92};  // the F row's metal, between Vol's pale gold and Fx's brass

void frow_shapes(plot::Canvas& c, const FKey (&keys)[kFKeys]) noexcept {
  // The value windows sit in the brass; each key's name is on the gold bar under them, right above the key itself.
  fill(c, 0, 419, 800, 1, Color::Accent);
  fill(c, 0, 420, 800, 2, Color::Vol);
  c.rect(Rect{0, 422, 800, 2}, kGoldBand);
  fill(c, 0, 424, 800, 26, Color::Fx);
  fill(c, 0, 450, 800, 1, Color::PlayText);
  fill(c, 0, 451, 800, 2, Color::Vol);
  c.rect(Rect{0, 453, 800, 25}, kGoldBand);
  fill(c, 0, 478, 800, 2, Color::Instr);
  for (unsigned i = 0; i < kFKeys; ++i) {
    const int x = static_cast<int>(i) * 100;
    const FKey& k = keys[i];
    if (i > 0) {
      fill(c, x, 420, 1, 30, Color::PlayText);
      fill(c, x, 451, 1, 27, Color::PlayText);
    }
    fill(c, x + 5, 460, 18, 12, Color::PlayText);
    fill(c, x + 5, 424, 90, 24, k.hot ? Color::Accent : Color::Pane);
    if (k.pct >= 0) {
      fill(c, x + 10, 445, 80, 1, k.hot ? Color::Fx : Color::Empty);
      const int w = 80 * (k.pct > 100 ? 100 : k.pct) / 100;
      if (w > 0) {
        fill(c, x + 10, 444, w, 2, k.hot ? Color::PlayText : Color::TextDim);
      }
    }
  }
}

void frow_text(ui::Renderer& r, const FKey (&keys)[kFKeys]) noexcept {
  for (unsigned i = 0; i < kFKeys; ++i) {
    const int x = static_cast<int>(i) * 100;
    const FKey& k = keys[i];
    const char fn[3] = {'F', static_cast<char>('1' + i), '\0'};
    r.text(x + 8, 460, fn, Color::Vol, Face::Micro);
    r.text(x + 27, 458, k.name, k.dim ? Color::Vol : Color::PlayText, Face::Label);
    const Color ink = k.dim ? Color::TextDim : k.hot ? Color::AccentText : k.role == KeyRole::Voice ? Color::Play : k.role == KeyRole::Carrier ? Color::Sel : Color::Text;
    r.text(x + 10, 425, k.value, ink, Face::Data);
  }
}

// ---- the frame ------------------------------------------------------------------------------------------------------------------------

void perform_shapes(plot::Canvas& c, const HeaderState& h, const FKey (&keys)[kFKeys], const Voiceprint& vp, const SpeaksState& sp,
                    const SoundsState& so, const PadsView& pads) noexcept {
  header_shapes(c, h);

  // The voiceprint's frame and its unlit grid.
  fill(c, 240, 48, 320, 1, Color::Empty);
  fill(c, 240, 271, 320, 1, Color::Empty);
  fill(c, 240, 48, 1, 224, Color::Empty);
  fill(c, 559, 48, 1, 224, Color::Empty);
  for (int i = 0; i < 2; ++i) {
    const int x = i == 0 ? 240 : 548;
    for (int j = 0; j < 2; ++j) {
      const int y = j == 0 ? 48 : 270;
      fill(c, x, j == 0 ? y : y - 1, 12, 2, Color::Play);
      fill(c, i == 0 ? x : x + 10, j == 0 ? y : y - 11, 2, 12, Color::Play);
    }
  }
  fill(c, kVx, kVy, 288, 174, Color::Empty, kDim);
  c.round_rect(Rect{516, 52, 32, 14}, 112, col(Color::Play));

  // SPEAKS.
  c.round_rect(Rect{16, 68, 208, 20}, 160, col(Color::Play));
  if (sp.picker != nullptr) {
    picker_shapes(c, *sp.picker, 16, 208, Color::Play);
  } else {
    fill(c, 16, 120, 12, 150, Color::Empty, kDim);
    c.round_rect(Rect{60, 120, 44, 18}, 144, col(Color::Empty));
    c.round_rect(Rect{61, 121, 42, 16}, 128, sp.clipping ? col(Color::Rec) : col(Color::Pane));
    for (int i = 0; i < 4; ++i) {
      const std::uint8_t st = sp.slot[i];
      c.round_rect(Rect{60 + i * 40, 244, 34, 20}, 128, col(st == 3 ? Color::Rec : st == 2 ? Color::Play : Color::Empty));
      c.round_rect(Rect{61 + i * 40, 245, 32, 18}, 112, col(st == 1 ? Color::Empty : Color::Pane));
    }
  }
  if (sp.capture[0] != '\0') {
    c.round_rect(Rect{16, 274, 208, 14}, 128, col(Color::Rec));
    c.round_rect(Rect{17, 275, 206, 12}, 112, col(Color::Pane));
  }

  // SOUNDS.
  c.round_rect(Rect{576, 68, 72, 20}, 160, col(Color::Sel));
  if (so.picker != nullptr) {
    picker_shapes(c, *so.picker, 576, 216, Color::Sel);
  } else {
    fill(c, 576, 94, 216, 48, Color::Empty, kDim);
    fill(c, 576, 216, 216, 12, Color::Empty, kDim);
    {
      const bool five = so.note_count >= 5;
      const int w = five ? 40 : 44;
      const int pitch = five ? 42 : 50;
      for (unsigned i = 0; i < so.note_count && i < 5; ++i) {
        c.round_rect(Rect{576 + static_cast<int>(i) * pitch, 148, w, 22}, 96, col(Color::Sel));
      }
    }
  }

  // The pads.
  for (unsigned cl = 0; cl < 12; ++cl) {
    const int x = kPadX + static_cast<int>(cl) * kPadW;
    if ((pads.scale_cols >> cl) & 1u) {
      fill(c, x, kPadY, kPadW, 4 * kPadH, Color::Empty, kDim);
    }
  }
  std::uint16_t hot_cols = 0;
  for (unsigned p = 0; pads.column_in_scale_known && p < 48; ++p) {  // a column is a root only on a chord page: on KEYS it means nothing
    if (((pads.held >> p) & 1u) != 0 || static_cast<int>(p) == pads.latched) {
      hot_cols = static_cast<std::uint16_t>(hot_cols | (1u << (p % 12u)));
    }
  }
  for (unsigned cl = 0; cl < 12; ++cl) {
    if ((hot_cols >> cl) & 1u) {
      fill(c, kPadX + static_cast<int>(cl) * kPadW, kPadY, kPadW, 4 * kPadH, Color::Sel, 44);
    }
  }
  for (int i = 0; i <= 12; ++i) {
    fill(c, kPadX + i * kPadW, kPadY, 1, 4 * kPadH + 1, Color::Empty);
  }
  for (int i = 0; i <= 4; ++i) {
    fill(c, kPadX, kPadY + i * kPadH, 12 * kPadW + 1, 1, Color::Empty);
  }
  for (unsigned p = 0; p < 48; ++p) {
    const bool held = ((pads.held >> p) & 1u) != 0;
    const bool latched = static_cast<int>(p) == pads.latched;
    if (!held && !latched) {
      continue;
    }
    const int hx = kPadX + static_cast<int>(p % 12u) * kPadW;
    const int hy = kPadY + static_cast<int>(p / 12u) * kPadH;
    fill(c, kPadX, hy, 12 * kPadW + 1, 1, Color::Sel);
    fill(c, kPadX, hy + kPadH, 12 * kPadW + 1, 1, Color::Sel);
    fill(c, hx, kPadY, 1, 4 * kPadH + 1, Color::Sel);
    fill(c, hx + kPadW, kPadY, 1, 4 * kPadH + 1, Color::Sel);
    fill(c, hx + 1, hy + 1, kPadW - 1, kPadH - 1, Color::Sel, held ? 255 : 170);
  }

  frow_shapes(c, keys);

  // The lit dots of the voiceprint, the IN ladder and the OUT ladder.
  if (vp.bars) {
    for (unsigned i = 0; i < kBandCount; ++i) {
      const int x = kVx + static_cast<int>(i) * kColStride;
      const int n = static_cast<int>(bar_rows(vp.level[i]));
      fill(c, x, kHorizon - n * kPitch, kColW, n * kPitch, Color::Play);
      int refl = n / 3;
      refl = refl < 1 ? 1 : refl > 6 ? 6 : refl;
      fill(c, x, kHorizon + kPitch, kColW, refl * kPitch, Color::Play, 96);
    }
    for (unsigned i = 0; i < kBandCount; ++i) {
      const int x = kVx + static_cast<int>(i) * kColStride;
      const int np = static_cast<int>(peak_rows(vp.peak[i]));
      fill(c, x, kHorizon - np * kPitch - kPitch, kColW, kPitch, Color::Text);
    }
  }
  {
    const int h_in = static_cast<int>(ladder_height(sp.in.level, 150));
    if (h_in > 0 && sp.picker == nullptr) {
      fill(c, 16, 270 - h_in, 12, h_in, Color::Play);
    }
    const int pk = static_cast<int>(ladder_height(sp.in.peak, 150));
    if (sp.picker == nullptr) {
      fill(c, 16, 270 - pk - kPitch, 12, kPitch, Color::Text);
    }
    const int w_out = static_cast<int>(ladder_height(so.out.level, 216));
    if (w_out > 0 && so.picker == nullptr) {
      fill(c, 576, 216, w_out, 12, Color::Sel);
    }
  }

  // The masks (Pane ink over the lit dots: the pinholes), then what sits on top of them.
  c.bind(coarse_mask(288, 174));
  c.sprite(0, kVx, kVy, col(Color::Pane));
  if (sp.picker == nullptr) {  // an open list covers the meter, the notes' strip and the OUT bar: their masks go with them
    c.bind(coarse_mask(12, 150));
    c.sprite(0, 16, 120, col(Color::Pane));
  }
  if (so.picker == nullptr) {
    c.bind(coarse_mask(216, 12));
    c.sprite(0, 576, 216, col(Color::Pane));
    c.bind(fine_mask(216, 48));
    c.sprite(0, 576, 94, col(Color::Pane));
  }
  if (so.picker == nullptr && so.chord[0] != '\0' && atlas_can_spell(so.chord)) {
    c.bind(kAtlasDesc);
    int x = 580;
    for (const char* s = so.chord; *s != '\0'; ++s) {
      c.sprite(glyph_cell(*s), x, 98, col(Color::Sel));
      x += static_cast<int>(kAtlasAdvance);
    }
  }
  if (vp.bars) {
    fill(c, kVx, kHorizon + 2, 288, 2, Color::Sel);
  }
}

void perform_text(ui::Renderer& r, const HeaderState& h, const FKey (&keys)[kFKeys], const Voiceprint& vp, const SpeaksState& sp, const SoundsState& so,
                  const PadsView& pads, const char* placeholder) noexcept {
  header_text(r, h);

  // Voiceprint.
  r.text(250, 52, "VOICEPRINT", Color::TextDim, Face::Label);
  r.text(523, 53, "SSS", Color::PlayText, Face::Micro);
  if (vp.bars) {
    static const struct {
      int half_col;
      const char* s;
    } kAxis[] = {{0, "125"}, {5, "250"}, {10, "500"}, {15, "1K"}, {20, "2K"}, {25, "4K"}, {30, "8K"}};
    for (const auto& a : kAxis) {
      r.text(kVx + a.half_col * kColStride / 2 + 6 - len(a.s) * 3, 254, a.s, Color::TextDim, Face::Micro);
    }
  } else {
    r.text(240 + (320 - len(placeholder) * 8) / 2, 150, placeholder, Color::TextDim, Face::Label);
  }

  // SPEAKS.
  r.text(16, 48, "SPEAKS", Color::Play, Face::Label);
  r.text(30, 70, sp.source, Color::PlayText, Face::Label);
  char buf[16];
  {
    unsigned n = 0;
    for (const char* s = "GAIN "; *s != '\0'; ++s) buf[n++] = *s;
    char tmp[4];
    unsigned t = 0;
    unsigned g = sp.gain > 999u ? 999u : sp.gain;
    do {
      tmp[t++] = static_cast<char>('0' + g % 10u);
      g /= 10u;
    } while (g != 0);
    while (t != 0) buf[n++] = tmp[--t];
    buf[n] = '\0';
  }
  if (sp.picker != nullptr) {
    picker_text(r, *sp.picker, 16, 208);
  } else {
    r.text(16, 96, buf, Color::TextDim, Face::Label);
    r.text(36, 120, "0", Color::TextDim, Face::Micro);
    r.text(36, 180, "-12", Color::TextDim, Face::Micro);
    r.text(36, 258, "-48", Color::TextDim, Face::Micro);
    r.text(70, 121, "CLIP", sp.clipping ? Color::Text : Color::TextDim, Face::Micro);
    r.text(60, 150, "LIVE", Color::Text, Face::Label);
    r.text(60, 168, sp.live != nullptr ? sp.live : sp.source, Color::Play, Face::Label);
    bool any = false;
    for (int i = 0; i < 4; ++i) any = any || sp.slot[i] != 0;
    if (!any) r.text(60, 188, "NO SLOTS YET", Color::TextDim, Face::Micro);
    r.text(60, 226, "SLOTS", Color::TextDim, Face::Label);
    for (int i = 0; i < 4; ++i) {
      const char d[2] = {static_cast<char>('1' + i), '\0'};
      const std::uint8_t st = sp.slot[i];
      r.text(74 + i * 40, 246, d, st == 3 ? Color::Rec : st == 2 ? Color::Play : st == 1 ? Color::Text : Color::TextDim, Face::Micro);
    }
  }
  if (sp.capture[0] != '\0') r.text(22, 277, sp.capture, Color::Rec, Face::Micro);

  // SOUNDS.
  r.text(576, 48, "SOUNDS", Color::Sel, Face::Label);
  if (so.held[0] != '\0') {
    r.text(792 - len(so.held) * 8, 48, so.held, Color::Sel, Face::Label);
  }
  r.text(590, 70, so.carrier, Color::SelText, Face::Label);
  {
    unsigned n = 0;
    for (const char* s = "SPREAD "; *s != '\0'; ++s) buf[n++] = *s;
    char tmp[4];
    unsigned t = 0;
    unsigned g = so.spread > 999u ? 999u : so.spread;
    do {
      tmp[t++] = static_cast<char>('0' + g % 10u);
      g /= 10u;
    } while (g != 0);
    while (t != 0) buf[n++] = tmp[--t];
    buf[n] = '\0';
  }
  r.text(660, 70, buf, Color::TextDim, Face::Label);
  if (so.picker != nullptr) {
    picker_text(r, *so.picker, 576, 216);
  } else {
    if (so.chord[0] != '\0' && !atlas_can_spell(so.chord)) {
      r.text(580, 102, so.chord, Color::Sel, Face::Display);
    }
    {
      const bool five = so.note_count >= 5;
      const int w = five ? 40 : 44;
      const int pitch = five ? 42 : 50;
      for (unsigned i = 0; i < so.note_count && i < 5; ++i) {
        char nm[8];
        const unsigned n = note_name(so.notes[i], nm);
        r.text(576 + static_cast<int>(i) * pitch + (w - static_cast<int>(n) * 8) / 2, 150, nm, Color::SelText, Face::Label);
      }
    }
    r.text(576, 178, so.voicing, Color::TextDim, Face::Micro);
    r.text(576, 198, "OUT", Color::TextDim, Face::Label);
    {
      unsigned n = 0;
      int db = so.out_db < -99 ? -99 : so.out_db > 0 ? 0 : so.out_db;
      if (db < 0) {
        buf[n++] = '-';
        db = -db;
      }
      if (db >= 10) {
        buf[n++] = static_cast<char>('0' + db / 10);
      }
      buf[n++] = static_cast<char>('0' + db % 10);
      for (const char* s = " dB"; *s != '\0'; ++s) buf[n++] = *s;
      buf[n] = '\0';
      r.text(792 - static_cast<int>(n) * 6, 200, buf, Color::TextDim, Face::Micro);
    }
  }

  // Pads.
  for (unsigned row = 0; row < 4; ++row) {
    const char* nm = pads.row_name[row];
    r.text(60 - len(nm) * 6, kPadY + static_cast<int>(row) * kPadH + 8, nm, Color::TextDim, Face::Micro);
  }
  if (pads.label != nullptr) {
    for (unsigned p = 0; p < 48; ++p) {
      const unsigned row = p / 12u, cl = p % 12u;
      const bool hot = ((pads.held >> p) & 1u) != 0 || static_cast<int>(p) == pads.latched;
      const Color ink = hot ? Color::SelText : ((pads.scale_cols >> cl) & 1u) != 0 ? Color::Text : Color::TextDim;
      const char* s = pads.label[p];
      r.text(kPadX + static_cast<int>(cl) * kPadW + (kPadW - len(s) * 8) / 2 + 1, kPadY + static_cast<int>(row) * kPadH + 6, s, ink, Face::Label);
    }
  }
  // One line under the grid: the pads' gestures at the left, what the F key under the encoder does at the right.
  static constexpr const char kTurn[] = "  TURN TO CHANGE";
  const int turn_x = 784 - len(kTurn) * 6;
  r.text(16, 403, pads.hint_left, Color::TextDim, Face::Micro);
  if (pads.key_hint[0] != '\0') {
    r.text(turn_x - len(pads.key_hint) * 6, 403, pads.key_hint, Color::Text, Face::Micro);
    r.text(turn_x, 403, kTurn, Color::TextDim, Face::Micro);
  }

  frow_text(r, keys);
}

}  // namespace o3_voc03er::ornoir
