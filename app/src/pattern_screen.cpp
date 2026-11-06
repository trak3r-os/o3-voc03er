// SPDX-License-Identifier: GPL-3.0-or-later
#include "pattern_screen.h"

#include "perform_logic.h"
#include "skin/ornoir.h"

#include <o3_voc03er/input.h>

#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/context.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/placement.h>

#include <cstring>

namespace o3_voc03er {

using namespace o3sdk;
namespace pl = perform;
namespace pt = pattern;
using ui::Color;
using ui::Face;
using Button = app::Button;

namespace {

constexpr std::uint64_t kNoteMs = 2500;
constexpr const char* kKeyNames[ornoir::kFKeys] = {"PATTERN", "LANE", "LENGTH", "TEMPO", "VALUE", "CLR LANE", "FOLLOW", "SAVE"};
constexpr const char* kSlotNames[4] = {"SLOT 1", "SLOT 2", "SLOT 3", "SLOT 4"};
constexpr char kLetters[kBankPatterns + 1] = "ABCDEFGH";
constexpr unsigned kTempoStep = 1000;  // one BPM, in milli-BPM

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

// What a step stores in a lane, as text for the cell (`line2` is a second, smaller line: a chord's kind). Empty for nothing.
void cell_text(pt::Lane lane, const Step& s, char (&line1)[6], const char*& line2) noexcept {
  line1[0] = '\0';
  line2 = "";
  switch (lane) {
    case pt::Lane::Note:
      if (s.note != 0u) {
        pt::note_name(line1, s.note);
        if (s.kind != 0u) {
          line2 = pt::kCellKind[(s.kind - 1u) & 3u];
        }
      }
      break;
    case pt::Lane::Carrier:
      cat(line1, sizeof line1, pt::kCellCarrier[s.carrier < pt::kCarrierPads ? s.carrier : 0u]);
      break;
    case pt::Lane::Level:
      cat_uint(line1, sizeof line1, step_velocity(s));
      break;
    case pt::Lane::Slice:
      if (s.slice != 0u) {
        cat_uint(line1, sizeof line1, s.slice);
      }
      break;
  }
}

// Does the lane hold something in this step? (the step pad's dim LED, the cell's ink)
bool has_content(pt::Lane lane, const Step& s) noexcept {
  switch (lane) {
    case pt::Lane::Note:
      return s.note != 0u;
    case pt::Lane::Carrier:
      return s.carrier != 0u;
    case pt::Lane::Level:
      return s.level != 0u;
    case pt::Lane::Slice:
      return s.slice != 0u;
  }
  return false;
}

// HELP's tables, in the SDRAM (valid while HELP is open): the 48 pad labels.
struct PatternHelp {
  char name[app::kPadCount][app::kHelpPadLabel + 1];
};
O3SDK_BIG PatternHelp g_pd = {};

constexpr int kGridX = 124;
constexpr int kCellW = 40;
constexpr int kCellY = 128;
constexpr int kCellH = 56;
constexpr int kLaneStep = 60;

}  // namespace

void PatternScreen::init(dsp::UnitId voice, Bank* bank, StepSeq* seq, PerformScreen* perform, SaveFn save, void* user) noexcept {
  voice_ = voice;
  bank_ = bank;
  seq_ = seq;
  perform_ = perform;
  save_ = save;
  save_user_ = user;
  cursor_ = 0;
  lane_ = 0;
  focus_ = 4;
  shown_ = 0;
  follow_ = false;
}

const ui::Theme* PatternScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void PatternScreen::enter(app::Context& ctx) noexcept {
  note_ = nullptr;
  ctx.redraw();
}

std::uint64_t PatternScreen::repeat_buttons(const app::Context&) const noexcept {
  return (std::uint64_t{1} << static_cast<unsigned>(Button::Left)) | (std::uint64_t{1} << static_cast<unsigned>(Button::Right));
}

void PatternScreen::say(app::Context& ctx, const char* text) noexcept {
  note_ = text;
  note_until_ = ctx.uptime_ms() + kNoteMs;
  ctx.redraw();
}

void PatternScreen::set_lane(app::Context& ctx, pt::Lane lane) noexcept {
  lane_ = static_cast<std::uint8_t>(lane);
  ctx.redraw();
}

void PatternScreen::show_pattern(app::Context& ctx, unsigned p) noexcept {
  shown_ = static_cast<std::uint8_t>(p & 7u);
  if (seq_ != nullptr) {
    seq_->request_pattern(shown_);  // it starts when the playing one ends (or at the next Play)
  }
  if (cursor_ >= length_of(pattern_now())) {
    cursor_ = 0;
  }
  ctx.redraw();
}

// A palette pad writes into the cursor's step, and the cursor moves on.
void PatternScreen::write_palette(app::Context& ctx, unsigned pad) noexcept {
  PatternData& p = pattern_now();
  bool ok = false;
  switch (lane()) {
    case pt::Lane::Note: {
      const bool keys = perform_ != nullptr && perform_->keys_mode();
      const int oct = perform_ != nullptr ? perform_->octave() : 0;
      const ChordKind bottom = static_cast<ChordKind>(perform_ != nullptr ? perform_->chord_kind() : 0u);
      const unsigned note = pt::note_of_pad(pad, oct, keys);
      ok = keys ? set_note(p, cursor_, note) : set_chord(p, cursor_, note, pt::kind_of_pad(pad, bottom), Voicing::Close);
      break;
    }
    case pt::Lane::Carrier:
      ok = pad < pt::kCarrierPads && set_carrier(p, cursor_, pad);
      break;
    case pt::Lane::Level:
      ok = set_level(p, cursor_, pt::level_of_pad(pad));
      break;
    case pt::Lane::Slice: {
      const pt::SliceWrite w = pt::slice_write(p, cursor_, pad, seq_ != nullptr ? seq_->slice_count() : 0u);
      if (w == pt::SliceWrite::NoSlices) {
        say(ctx, "SPEAKS HAS NO SLICES");
        return;
      }
      ok = w == pt::SliceWrite::Written;
      break;
    }
  }
  if (ok) {
    cursor_ = static_cast<std::uint8_t>(pt::next_cursor(cursor_, length_of(p)));
  }
  ctx.redraw();
}

void PatternScreen::pad(app::Context& ctx, unsigned pad, bool shift) noexcept {
  if (pad < pt::kPalettePads) {
    write_palette(ctx, pad);
    return;
  }
  const int step = pt::step_of_pad(pad);
  if (step >= 0) {
    cursor_ = static_cast<std::uint8_t>(step);
    if (shift) {
      (void)pt::rest_lane(pattern_now(), cursor_, lane());
    }
    ctx.redraw();
    return;
  }
  const int pat = pt::pattern_of_pad(pad);
  if (pat >= 0) {
    show_pattern(ctx, static_cast<unsigned>(pat));
  }
}

// `delta` is the encoder's detents, or +1 for a second tap on the key; a tap wraps where a detent stops at the end.
void PatternScreen::turn(app::Context& ctx, unsigned key, int delta) noexcept {
  const bool tap = delta == 1 && key_tap_;
  PatternData& p = pattern_now();
  switch (key) {
    case 0: {  // PATTERN
      const int n = static_cast<int>(shown_) + delta;
      show_pattern(ctx, tap ? (shown_ + 1u) & 7u : static_cast<unsigned>(n < 0 ? 0 : (n > 7 ? 7 : n)));
      break;
    }
    case 1: {  // LANE
      const int n = static_cast<int>(lane_) + delta;
      lane_ = static_cast<std::uint8_t>(tap ? (lane_ + 1u) % pt::kLanes : (n < 0 ? 0 : (n >= static_cast<int>(pt::kLanes) ? pt::kLanes - 1u : static_cast<unsigned>(n))));
      break;
    }
    case 2: {  // LENGTH
      const int n = static_cast<int>(length_of(p)) + delta;
      (void)set_length(p, tap && n > static_cast<int>(kPatternSteps) ? 1u : static_cast<unsigned>(n < 1 ? 1 : (n > 16 ? 16 : n)));
      if (cursor_ >= length_of(p)) {
        cursor_ = static_cast<std::uint8_t>(length_of(p) - 1u);
      }
      break;
    }
    case 3: {  // TEMPO
      const int t = static_cast<int>(ctx.engine().own_tempo() / kTempoStep) + delta;
      (void)ctx.engine().set_tempo(static_cast<std::uint32_t>(t < 20 ? 20 : t) * kTempoStep);
      break;
    }
    case 4:  // VALUE
      if (lane() == pt::Lane::Slice && (seq_ == nullptr || seq_->slice_count() == 0u)) {
        say(ctx, "SPEAKS HAS NO SLICES");
        return;
      }
      (void)pt::step_value(p, cursor_, lane(), delta, seq_ != nullptr ? seq_->slice_count() : 0u);
      break;
    default:
      return;
  }
  ctx.redraw();
}

void PatternScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  unsigned k = 0;
  if (in.fkey_press(k)) {
    if (in.shift) {
      return;
    }
    if (k == 5u) {  // CLEAR LANE: at once, on the pattern shown
      PatternData& p = pattern_now();
      for (unsigned i = 0; i < kPatternSteps; ++i) {
        (void)pt::rest_lane(p, i, lane());
      }
      say(ctx, "LANE CLEARED");
      return;
    }
    if (k == 6u) {  // FOLLOW
      follow_ = !follow_;
      ctx.redraw();
      return;
    }
    if (k == 7u) {  // SAVE
      say(ctx, save_ != nullptr && save_(save_user_, ctx) ? "SAVING" : "CARD BUSY");
      return;
    }
    if (k == focus_) {  // a second tap steps the value on
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
  if (in.press(Button::Up) || in.press(Button::Down)) {
    const unsigned l = lane_;
    lane_ = static_cast<std::uint8_t>(in.press(Button::Up) ? (l > 0u ? l - 1u : 0u) : (l + 1u < pt::kLanes ? l + 1u : pt::kLanes - 1u));
    ctx.redraw();
  } else if (in.press(Button::Note)) {
    set_lane(ctx, pt::Lane::Note);
  } else if (in.press(Button::Instr)) {
    set_lane(ctx, pt::Lane::Carrier);
  } else if (in.press(Button::Vol)) {
    set_lane(ctx, pt::Lane::Level);
  } else if (in.press(Button::Fx)) {
    set_lane(ctx, pt::Lane::Slice);
  } else if (in.press(Button::Left) || in.press(Button::Right)) {  // repeats arrive as presses (repeat_buttons)
    const unsigned len = length_of(pattern_now());
    const unsigned c = cursor_;
    cursor_ = static_cast<std::uint8_t>(in.press(Button::Right) ? (c + 1u) % len : (c == 0u || c > len ? len - 1u : c - 1u));
    ctx.redraw();
  } else if (in.pad_press()) {
    pad(ctx, in.pad(), in.shift);
  }
}

void PatternScreen::update(app::Context& ctx) noexcept {
  if (follow_ && seq_ != nullptr && ctx.engine().playing()) {
    const unsigned s = seq_->playing_step();
    if (s < kPatternSteps && s < length_of(pattern_now())) {
      cursor_ = static_cast<std::uint8_t>(s);
    }
  }
  if (note_ != nullptr && ctx.uptime_ms() >= note_until_) {
    note_ = nullptr;
  }
  ctx.redraw();  // the playhead moves with the bar
}

// Rows 0 and 1: the lane's palette (mid where it holds what the cursor's step stores); rows 2 and 3: the steps and the patterns.
void PatternScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  const PatternData& p = pattern_now();
  const unsigned len = length_of(p);
  const Step cur = get_step(p, cursor_);
  const bool keys = perform_ != nullptr && perform_->keys_mode();
  const int oct = perform_ != nullptr ? perform_->octave() : 0;
  const ChordKind bottom = static_cast<ChordKind>(perform_ != nullptr ? perform_->chord_kind() : 0u);
  const unsigned slices = seq_ != nullptr ? seq_->slice_count() : 0u;
  for (unsigned pad = 0; pad < pt::kPalettePads; ++pad) {
    std::uint8_t level = app::kLedDim;
    if (lane() == pt::Lane::Slice) {  // the SPEAKS slot's slices: dim up to its count, mid on the cursor step's
      level = pad < slices ? (pt::palette_matches(lane(), pad, cur, oct, keys, bottom) ? app::kLedMid : app::kLedDim) : app::kLedOff;
    } else if (lane() == pt::Lane::Carrier && pad >= pt::kCarrierPads) {
      level = app::kLedOff;
    } else if (pt::palette_matches(lane(), pad, cur, oct, keys, bottom)) {
      level = app::kLedMid;
    }
    leds.set(pad, level);
  }
  const bool playing = seq_ != nullptr && ctx.engine().playing() && seq_->playing_pattern() == shown_;
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    std::uint8_t level = app::kLedOff;
    if (i < len) {
      level = playing && seq_->playing_step() == i ? app::kLedOn : (i == cursor_ ? app::kLedMid : (has_content(lane(), get_step(p, i)) ? app::kLedDim : app::kLedOff));
    }
    leds.set(pt::pad_of_step(i), level);
  }
  const unsigned running = seq_ != nullptr && ctx.engine().playing() ? seq_->playing_pattern() : kBankPatterns;
  for (unsigned q = 0; q < kBankPatterns; ++q) {
    leds.set(pt::pad_of_pattern(q), q == running ? app::kLedOn : (q == shown_ ? app::kLedMid : app::kLedDim));
  }
}

void PatternScreen::soft_keys(app::Context&, app::SoftKeys& out) noexcept {
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    out.label[i] = kKeyNames[i];
  }
}

// HELP: every button input() answers has a row (tests/pattern_screen_test.cpp holds it), the pads in three groups.
void PatternScreen::help(const app::Context&, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Up, "LANE BEFORE", nullptr},
      {Button::Down, "LANE AFTER", nullptr},
      {Button::Left, "CURSOR BACK, HOLD REPEATS", nullptr},
      {Button::Right, "CURSOR ON, HOLD REPEATS", nullptr},
      {Button::Note, "NOTE LANE", nullptr},
      {Button::Instr, "CARRIER LANE", nullptr},
      {Button::Vol, "LEVEL LANE", nullptr},
      {Button::Fx, "SLICE LANE: A SPEAKS CUT", nullptr},
      {Button::Shift, "WITH A STEP PAD: A REST", nullptr},
      {Button::Rec, "SAMPLE REC: A TAKE", nullptr},
      {Button::Params, "THE OLD PLAY SCREEN: LOOPS", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER (SLICE U9)", nullptr},
      {Button::File, "FILE (SLICE U9)", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD (SLICE U8)", nullptr},
      {Button::SamplePlay, "SAMPLE: THE SLOTS", nullptr},
      {Button::SampleEdit, "SAMPLE EDIT: THE CUTS", nullptr},
      {Button::SampleRec, "SAMPLE REC: A TAKE", nullptr},
      {Button::Song, "SONG (SLICE U9)", nullptr},
      {Button::Pattern, "THIS SCREEN", nullptr},
  };
  static constexpr app::HelpPadGroup kGroups[] = {
      {"PALETTE", "WRITES THE CURSOR'S STEP, CURSOR MOVES ON", Color::Accent},
      {"STEPS", "MOVES THE CURSOR; SHIFT: A REST IN THE LANE", Color::Play},
      {"PATTERNS", "A TO H: SHOW IT, PLAY IT NEXT", Color::Sel},
  };
  h.summary = "THE 16 STEPS OF A PATTERN AND THEIR LANES. THE UPPER PADS WRITE THE LIT LANE.\n"
              "A SLICE FIRES A SPEAKS CUT AT ITS STEP, ONE SHOT; STOP RELEASES IT.";
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    h.fkey[i] = nullptr;
    h.fkey_shift[i] = nullptr;
  }
  h.fkey[0] = "A TO H";
  h.fkey[1] = "NOTE, CARRIER, LEVEL, SLICE";
  h.fkey[2] = "1 TO 16 STEPS";
  h.fkey[3] = "THE BAR'S BPM";
  h.fkey[4] = "THE CURSOR STEP'S VALUE";
  h.fkey[5] = "EMPTIES THE LANE";
  h.fkey[6] = "CURSOR RIDES THE PLAYHEAD";
  h.fkey[7] = "SAVES THE PATTERNS";
  for (unsigned i = 0; i < pt::kPalettePads; ++i) {
    char* l = g_pd.name[i];
    l[0] = '\0';
    switch (lane()) {
      case pt::Lane::Note:
        cat(l, sizeof g_pd.name[i], pt::kPitchClass[app::pad_col(i) % 12u]);
        break;
      case pt::Lane::Carrier:
        if (i < pt::kCarrierPads) {
          cat(l, sizeof g_pd.name[i], pt::kPadCarrier[i]);
        }
        break;
      case pt::Lane::Level:
        cat_uint(l, sizeof g_pd.name[i], pt::level_of_pad(i));
        break;
      case pt::Lane::Slice:
        cat_uint(l, sizeof g_pd.name[i], i + 1u);
        break;
    }
    h.pad_label[i] = l[0] != '\0' ? l : nullptr;
  }
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    char* l = g_pd.name[pt::pad_of_step(i)];
    l[0] = '\0';
    cat_uint(l, sizeof g_pd.name[0], i + 1u);
    h.pad_label[pt::pad_of_step(i)] = l;
  }
  for (unsigned q = 0; q < kBankPatterns; ++q) {
    char* l = g_pd.name[pt::pad_of_pattern(q)];
    l[0] = kLetters[q];
    l[1] = '\0';
    h.pad_label[pt::pad_of_pattern(q)] = l;
  }
  h.groups = kGroups;
  h.group_count = sizeof kGroups / sizeof kGroups[0];
  // help_pads() would null the labels set above, so the groups are written here.
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    h.pad_group[pad] = pad < pt::kPalettePads ? 1u : (pt::step_of_pad(pad) >= 0 ? 2u : 3u);
  }
  h.pads = "UPPER ROWS: THE LANE'S VALUES. LOWER LEFT: STEPS 1 TO 16. LOWER RIGHT: PATTERNS A TO H.";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "THE LIT F KEY'S VALUE";
}

// ---- draw ---------------------------------------------------------------------------------------------------------------------------

void PatternScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  const dsp::Engine& e = ctx.engine();
  const app::AudioIn& ain = ctx.audio_in();
  const SourceName* src = ain.available() ? find_source(ain.source()) : nullptr;
  const float cv = e.param(voice_, Vocoder::CarSrc) + 0.5f;
  const unsigned car = cv > 0.0f ? static_cast<unsigned>(cv) : 0u;
  const unsigned mode = pl::mode_index(e.param(voice_, Vocoder::Mode));
  const unsigned tempo = e.stats().tempo_mbpm;
  const route::Route rt = perform_ != nullptr ? perform_->route_now(ctx) : route::Route{};
  const PatternData& p = pattern_now();
  const unsigned len = length_of(p);
  const bool playing = seq_ != nullptr && e.playing() && seq_->playing_pattern() == shown_;
  const unsigned head = playing ? seq_->playing_step() : kPatternSteps;
  const Step cur = get_step(p, cursor_);

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
    keys[i].hot = i == focus_;
  }
  keys[0].value[0] = kLetters[shown_ & 7u];
  cat(keys[1].value, sizeof keys[1].value, pt::kLaneNames[lane_ & 3u]);
  cat_uint(keys[2].value, sizeof keys[2].value, len);
  cat_uint(keys[3].value, sizeof keys[3].value, e.own_tempo() / 1000u);
  {
    char one[6];
    const char* two = "";
    cell_text(lane(), cur, one, two);
    cat(keys[4].value, sizeof keys[4].value, one[0] != '\0' ? one : "-");
  }
  cat(keys[6].value, sizeof keys[6].value, follow_ ? "ON" : "OFF");

  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  // A column per step: the frame is the playhead's ink, or the cursor's (the cursor wins), the cells inside in the pane's.
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    const int x = kGridX + static_cast<int>(i) * kCellW;
    if (i == head) {
      ornoir::fill_rect(c, x, kCellY - 8, kCellW - 2, 5, Color::Play);
    }
    if (i >= len) {
      continue;
    }
    ornoir::fill_rect(c, x, kCellY, kCellW - 2, 4 * kLaneStep - 4, i == cursor_ ? Color::Accent : (i == head ? Color::Play : Color::Empty));
    for (unsigned l = 0; l < pt::kLanes; ++l) {
      ornoir::fill_rect(c, x + 2, kCellY + static_cast<int>(l) * kLaneStep + 2, kCellW - 6, kCellH - 4, Color::Pane);
    }
  }
  for (unsigned l = 0; l < pt::kLanes; ++l) {  // the lane in force: a bar at the left of its row
    if (l == lane_) {
      ornoir::fill_rect(c, 12, kCellY + static_cast<int>(l) * kLaneStep + 2, 6, kCellH - 4, Color::Accent);
    }
  }
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  // The letter and the pattern's facts at the top left.
  char one[2] = {kLetters[shown_ & 7u], '\0'};
  r.text(24, 52, one, shown_ == (seq_ != nullptr ? seq_->playing_pattern() : 0u) && playing ? Color::Play : Color::Text, Face::Display);
  char buf[32];
  buf[0] = '\0';
  cat(buf, sizeof buf, "LENGTH ");
  cat_uint(buf, sizeof buf, len);
  cat(buf, sizeof buf, "   TEMPO ");
  cat_uint(buf, sizeof buf, tempo / 1000u);
  cat(buf, sizeof buf, follow_ ? "   FOLLOW" : "");
  r.text(kGridX, 56, buf, Color::Text, Face::Label);
  r.text(kGridX, 82, playing ? "PLAYING" : "PATTERN A TO H ON THE LOWER RIGHT PADS", playing ? Color::Play : Color::TextDim, Face::Micro);

  for (unsigned l = 0; l < pt::kLanes; ++l) {
    const int y = kCellY + static_cast<int>(l) * kLaneStep;
    r.text(24, y + 20, pt::kLaneNames[l], l == lane_ ? Color::Text : Color::TextDim, Face::Label);
  }
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    const int x = kGridX + static_cast<int>(i) * kCellW;
    buf[0] = '\0';
    cat_uint(buf, sizeof buf, i + 1u);
    r.text(centred(r, buf, Face::Micro, x + kCellW / 2 - 1), kCellY - 22, buf, i == cursor_ ? Color::Accent : Color::TextDim, Face::Micro);
    if (i >= len) {
      continue;
    }
    const Step s = get_step(p, i);
    for (unsigned l = 0; l < pt::kLanes; ++l) {
      char t1[6];
      const char* t2 = "";
      cell_text(static_cast<pt::Lane>(l), s, t1, t2);
      const int y = kCellY + static_cast<int>(l) * kLaneStep;
      const int cx = x + kCellW / 2 - 1;
      const Color ink = has_content(static_cast<pt::Lane>(l), s) ? (l == lane_ ? Color::Text : Color::TextDim) : Color::Empty;
      const bool shown_text = t1[0] != '\0' && (has_content(static_cast<pt::Lane>(l), s) || static_cast<pt::Lane>(l) == pt::Lane::Level);
      if (shown_text) {
        r.text(centred(r, t1, Face::Label, cx), y + (t2[0] != '\0' ? 10 : 20), t1, ink == Color::Empty ? Color::TextDim : ink, Face::Label);
      }
      if (t2[0] != '\0') {
        r.text(centred(r, t2, Face::Micro, cx), y + 34, t2, Color::TextDim, Face::Micro);
      }
    }
  }
  if (note_ != nullptr) {
    r.text(kGridX, kCellY + 4 * kLaneStep + 10, note_, Color::Accent, Face::Label);
  } else {
    r.text(kGridX, kCellY + 4 * kLaneStep + 14, "PADS: PALETTE ABOVE, STEPS AND PATTERNS BELOW. SHIFT + STEP: REST", Color::TextDim, Face::Micro);
  }
}

}  // namespace o3_voc03er
