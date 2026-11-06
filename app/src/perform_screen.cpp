// SPDX-License-Identifier: GPL-3.0-or-later
#include "perform_screen.h"

#include "perform_logic.h"
#include "skin/ornoir.h"

#include <o3_voc03er/chords.h>

#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/context.h>
#include <o3sdk/core/placement.h>

#include <cstddef>
#include <cstring>

namespace o3_voc03er {

using namespace o3sdk;
using app::Button;
namespace pl = perform;

namespace {

constexpr std::uint8_t kVelocity = 110;
constexpr std::uint32_t kPeakHoldFrames = 20;

// PERFORM's tables, in the SDRAM (O3SDK_BIG: zero at start, main loop only; the screen keeps just a cursor in the SRAM). Plain bytes: only zero
// initialisers are legal there.
struct PerformData {
  char label[app::kPadCount][12];                    // each pad's chord name (CHORDS) or note name (KEYS), truncated
  std::uint8_t notes[app::kPadCount][kChordMaxNotes];  // the notes each pad last started
  std::uint8_t count[app::kPadCount];
  std::uint8_t lat_notes[kChordMaxNotes];  // the latched chord
  std::uint8_t lat_count;
  std::uint8_t last_notes[kChordMaxNotes];  // the chord before, for SMOOTH
  std::uint8_t last_count;
  std::uint8_t level[ornoir::kBandCount];  // smoothed bands, held peaks and their ages
  std::uint8_t peak[ornoir::kBandCount];
  std::uint8_t age[ornoir::kBandCount];
  std::uint8_t in_level, in_peak, in_age;
  std::uint8_t out_level, out_peak, out_age;
};
O3SDK_BIG PerformData g_pd = {};

// Attack at once, release by a quarter of the distance; a peak holds for kPeakHoldFrames, then falls two a frame.
void follow(std::uint8_t raw, std::uint8_t& level, std::uint8_t& peak, std::uint8_t& age) noexcept {
  if (raw >= level) {
    level = raw;
  } else {
    const unsigned d = (static_cast<unsigned>(level) - raw + 3u) / 4u;
    level = static_cast<std::uint8_t>(level - d);
  }
  if (level >= peak) {
    peak = level;
    age = 0;
  } else if (age < kPeakHoldFrames) {
    ++age;
  } else {
    peak = static_cast<std::uint8_t>(peak > 2u ? peak - 2u : 0u);
  }
}

void append(char (&dst)[12], const char* s) noexcept {
  std::size_t n = std::strlen(dst);
  while (*s != '\0' && n + 1u < sizeof dst) {
    dst[n++] = *s++;
  }
  dst[n] = '\0';
}

unsigned round_param(const dsp::Engine& e, dsp::UnitId id, std::uint16_t p) noexcept {
  const float v = e.param(id, p);
  return v < 0.0f ? 0u : static_cast<unsigned>(v + 0.5f);
}

Chord chord_from(const std::uint8_t* notes, unsigned count) noexcept {
  Chord c;
  c.count = static_cast<std::uint8_t>(count > kChordMaxNotes ? kChordMaxNotes : count);
  for (unsigned i = 0; i < c.count; ++i) {
    c.notes[i] = notes[i];
  }
  return c;
}

}  // namespace

void PerformScreen::init(dsp::UnitId voice, const InputMeter* meter, const Vocoder* voc, SourceStep step, void* user) noexcept {
  voice_ = voice;
  meter_ = meter;
  voc_ = voc;
  step_source_ = step;
  source_user_ = user;
  focus_ = 5;
  octave_ = 0;
  bottom_ = 0;
  keys_ = false;
  latched_ = -1;
  held_ = 0;
  label_sig_ = 0xFFFFFFFFu;
  frame_ = 0;
  std::memset(&g_pd, 0, sizeof g_pd);
}

const ui::Theme* PerformScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void PerformScreen::enter(app::Context& ctx) noexcept { ctx.redraw(); }

void PerformScreen::leave(app::Context& ctx) noexcept { release_all(ctx); }

std::uint64_t PerformScreen::repeat_buttons(const app::Context&) const noexcept {
  return (std::uint64_t{1} << static_cast<unsigned>(Button::Left)) | (std::uint64_t{1} << static_cast<unsigned>(Button::Right));
}

// ---- notes --------------------------------------------------------------------------------------------------------------------------

bool PerformScreen::note_in_use(std::uint8_t note, unsigned except_pad) const noexcept {
  for (unsigned p = 0; p < app::kPadCount; ++p) {
    if (p == except_pad || ((held_ >> p) & 1u) == 0u) {
      continue;
    }
    for (unsigned i = 0; i < g_pd.count[p] && i < kChordMaxNotes; ++i) {
      if (g_pd.notes[p][i] == note) {
        return true;
      }
    }
  }
  if (latched_ >= 0) {
    for (unsigned i = 0; i < g_pd.lat_count && i < kChordMaxNotes; ++i) {
      if (g_pd.lat_notes[i] == note) {
        return true;
      }
    }
  }
  return false;
}

void PerformScreen::press_pad(app::Context& ctx, unsigned pad) noexcept {
  if (pad >= app::kPadCount) {
    return;
  }
  dsp::Engine& e = ctx.engine();
  if (keys_) {
    const int note = key_note(pad, octave_);
    if (note < 0) {
      return;
    }
    g_pd.notes[pad][0] = static_cast<std::uint8_t>(note);
    g_pd.count[pad] = 1;
  } else {
    const Chord prev = chord_from(g_pd.last_notes, g_pd.last_count);
    const Chord c = chord_for_pad(app::pad_row(pad), app::pad_col(pad), octave_, round_param(e, voice_, Vocoder::Key), round_param(e, voice_, Vocoder::Scale),
                                  Voicing::Smooth, g_pd.last_count != 0u ? &prev : nullptr, static_cast<ChordKind>(bottom_));
    g_pd.count[pad] = c.count;
    for (unsigned i = 0; i < kChordMaxNotes; ++i) {
      g_pd.notes[pad][i] = i < c.count ? c.notes[i] : 0u;
    }
    g_pd.last_count = c.count;
    std::memcpy(g_pd.last_notes, c.notes, sizeof g_pd.last_notes);
  }
  held_ |= std::uint64_t{1} << pad;
  for (unsigned i = 0; i < g_pd.count[pad]; ++i) {
    (void)e.note_on(voice_, g_pd.notes[pad][i], kVelocity);
  }
  ctx.redraw();
}

void PerformScreen::release_pad(app::Context& ctx, unsigned pad) noexcept {
  if (pad >= app::kPadCount || ((held_ >> pad) & 1u) == 0u) {
    return;
  }
  held_ &= ~(std::uint64_t{1} << pad);
  for (unsigned i = 0; i < g_pd.count[pad]; ++i) {
    if (!note_in_use(g_pd.notes[pad][i], app::kPadCount)) {  // a note another held pad or the latch still holds stays on
      (void)ctx.engine().note_off(voice_, g_pd.notes[pad][i]);
    }
  }
  ctx.redraw();
}

// The held pad's chord keeps sounding after the pad is let go (on), or the latch is let go (off).
void PerformScreen::latch(app::Context& ctx, bool on) noexcept {
  dsp::Engine& e = ctx.engine();
  std::uint8_t old[kChordMaxNotes] = {};
  const unsigned old_count = latched_ >= 0 ? g_pd.lat_count : 0u;
  std::memcpy(old, g_pd.lat_notes, sizeof old);
  if (on) {
    if (held_ == 0u || keys_) {
      return;  // nothing held, or the keys have no latch
    }
    unsigned p = 0;
    while (((held_ >> p) & 1u) == 0u) {
      ++p;
    }
    latched_ = static_cast<int>(p);
    g_pd.lat_count = g_pd.count[p];
    std::memcpy(g_pd.lat_notes, g_pd.notes[p], sizeof g_pd.lat_notes);
  } else {
    latched_ = -1;
    g_pd.lat_count = 0;
  }
  for (unsigned i = 0; i < old_count && i < kChordMaxNotes; ++i) {  // the old latch's notes the new state does not hold
    if (!note_in_use(old[i], app::kPadCount)) {
      (void)e.note_off(voice_, old[i]);
    }
  }
  ctx.redraw();
}

void PerformScreen::release_all(app::Context& ctx) noexcept {
  dsp::Engine& e = ctx.engine();
  for (unsigned p = 0; p < app::kPadCount; ++p) {
    if (((held_ >> p) & 1u) != 0u) {
      for (unsigned i = 0; i < g_pd.count[p]; ++i) {
        (void)e.note_off(voice_, g_pd.notes[p][i]);
      }
    }
  }
  if (latched_ >= 0) {
    for (unsigned i = 0; i < g_pd.lat_count && i < kChordMaxNotes; ++i) {
      (void)e.note_off(voice_, g_pd.lat_notes[i]);
    }
  }
  held_ = 0;
  latched_ = -1;
  g_pd.lat_count = 0;
}

// ---- input --------------------------------------------------------------------------------------------------------------------------

void PerformScreen::bottom_kind(app::Context& ctx, int delta) noexcept {
  bottom_ = pl::step_wrapped(bottom_, delta, 4);
  ctx.redraw();
}

void PerformScreen::turn(app::Context& ctx, int delta, bool coarse) noexcept {
  dsp::Engine& e = ctx.engine();
  const unsigned mode = pl::mode_index(e.param(voice_, Vocoder::Mode));
  switch (focus_) {
    case 0:
      (void)e.set_param(voice_, Vocoder::Mode, static_cast<float>(pl::step_clamped(static_cast<int>(mode), delta, 0, 2)));
      break;
    case 1:
      if (step_source_ != nullptr && delta != 0) {
        step_source_(source_user_);
      }
      break;
    case 2:
      (void)e.set_param(voice_, Vocoder::CarSrc,
                        static_cast<float>(pl::step_clamped(static_cast<int>(round_param(e, voice_, Vocoder::CarSrc)), delta, 0, 6)));
      break;
    case 3:
      (void)e.set_param(voice_, Vocoder::Key, static_cast<float>(pl::step_wrapped(round_param(e, voice_, Vocoder::Key), delta, 12)));
      break;
    case 4:
      (void)e.set_param(voice_, Vocoder::Scale, static_cast<float>(pl::step_wrapped(round_param(e, voice_, Vocoder::Scale), delta, kScaleTableCount)));
      break;
    case 5:
      bottom_kind(ctx, delta);
      break;
    case 6:
      if (delta != 0) {
        release_all(ctx);
        keys_ = !keys_;
      }
      break;
    default:
      if (mode == 0u) {  // SPREAD: the detune, cents
        (void)e.set_param(voice_, Vocoder::Detune,
                          static_cast<float>(pl::step_clamped(static_cast<int>(round_param(e, voice_, Vocoder::Detune)), delta * (coarse ? 5 : 1), 0, 20)));
      } else if (mode == 1u) {  // QUALITY: the LPC order
        (void)e.set_param(voice_, Vocoder::Quality,
                          static_cast<float>(pl::step_clamped(static_cast<int>(round_param(e, voice_, Vocoder::Quality)), delta * (coarse ? 4 : 1), 8, 24)));
      } else {  // SPEED: ms
        (void)e.set_param(voice_, Vocoder::Speed,
                          static_cast<float>(pl::step_clamped(static_cast<int>(round_param(e, voice_, Vocoder::Speed)), delta * (coarse ? 25 : 5), 0, 500)));
      }
      break;
  }
  ctx.redraw();
}

void PerformScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  unsigned k = 0;
  if (in.fkey_press(k)) {
    if (k == focus_) {
      turn(ctx, 1, false);  // a second tap steps the value on
    } else {
      focus_ = k;
      ctx.redraw();
    }
    return;
  }
  if (in.turn()) {
    turn(ctx, in.delta(), in.shift);
    return;
  }
  if (in.press(Button::Shift)) {
    latch(ctx, true);
  } else if (in.press(Button::Delete)) {
    latch(ctx, false);
  } else if (in.press(Button::Up)) {
    bottom_kind(ctx, 1);
  } else if (in.press(Button::Down)) {
    bottom_kind(ctx, -1);
  } else if (in.press(Button::Left) || in.press(Button::Right)) {  // repeats arrive as presses (repeat_buttons)
    octave_ = clamp_octave(octave_ + (in.press(Button::Left) ? -1 : 1));
    ctx.redraw();
  } else if (in.pad_press()) {
    press_pad(ctx, in.pad());
  } else if (in.pad_release()) {
    release_pad(ctx, in.pad());
  }
}

// ---- draw ---------------------------------------------------------------------------------------------------------------------------

void PerformScreen::refresh_labels(const dsp::Engine& e) noexcept {
  const unsigned key = round_param(e, voice_, Vocoder::Key);
  const unsigned scale = round_param(e, voice_, Vocoder::Scale);
  const std::uint32_t sig = key | (scale << 4) | (static_cast<std::uint32_t>(octave_ + 8) << 8) | (bottom_ << 12) | (keys_ ? 1u << 16 : 0u);
  if (sig == label_sig_) {
    return;
  }
  label_sig_ = sig;
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    char (&out)[12] = g_pd.label[pad];
    out[0] = '\0';
    if (keys_) {
      const int note = key_note(pad, octave_);
      if (note >= 0) {
        char nm[8] = {};
        (void)ornoir::note_name(note, nm);
        append(out, nm);
      }
    } else {
      const Chord c = chord_for_pad(app::pad_row(pad), app::pad_col(pad), octave_, key, scale, Voicing::Close, nullptr, static_cast<ChordKind>(bottom_));
      char lbl[pl::kLabelChars + 1] = {};
      pl::chord_label(c, lbl);
      append(out, lbl);
    }
  }
}

void PerformScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  const dsp::Engine& e = ctx.engine();
  ++frame_;
  refresh_labels(e);

  const unsigned mode = pl::mode_index(e.param(voice_, Vocoder::Mode));
  const unsigned key = round_param(e, voice_, Vocoder::Key);
  const unsigned scale = round_param(e, voice_, Vocoder::Scale);
  const unsigned car = round_param(e, voice_, Vocoder::CarSrc);
  const app::AudioIn& ain = ctx.audio_in();
  const SourceName* src = ain.available() ? find_source(ain.source()) : nullptr;
  const dsp::EngineStats st = e.stats();

  // The levels follow the unit's View; above 60 % CPU every other frame skips the work (the previous values stay).
  const bool busy = ctx.cpu_percent() > 60u && (frame_ & 1u) != 0u;
  if (!busy) {
    if (voc_ != nullptr) {
      const Vocoder::View v = voc_->view();
      for (unsigned i = 0; i < ornoir::kBandCount; ++i) {
        follow(v.band[i], g_pd.level[i], g_pd.peak[i], g_pd.age[i]);
      }
    }
    const bool have = meter_ != nullptr && ain.available();
    follow(have ? pl::level_byte(meter_->peak()) : 0u, g_pd.in_level, g_pd.in_peak, g_pd.in_age);
    follow(pl::level_byte(st.peak_l > st.peak_r ? st.peak_l : st.peak_r), g_pd.out_level, g_pd.out_peak, g_pd.out_age);
  }

  ornoir::HeaderState h;
  h.speaks = src != nullptr ? src->label : "NONE";
  h.sounds = pl::kCarrierNames[car < 7u ? car : 0u];
  h.mode = pl::kModeNames[mode];
  h.playing = e.playing();
  h.tempo_x10 = st.tempo_mbpm / 100u;
  h.cpu_percent = ctx.cpu_percent();

  ornoir::FKey keys[ornoir::kFKeys];
  const char* names[ornoir::kFKeys] = {"MODE", "SPEAKS", "SOUNDS", "KEY", "SCALE", "CHORD", "PADS", mode == 0u ? "SPREAD" : (mode == 1u ? "QUALITY" : "SPEED")};
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    keys[i].name = names[i];
    keys[i].hot = i == focus_;
  }
  auto set = [&keys](unsigned i, const char* v) { append(keys[i].value, v); };
  set(0, pl::kModeNames[mode]);
  set(1, h.speaks);
  keys[1].role = ornoir::KeyRole::Voice;
  set(2, h.sounds);
  keys[2].role = ornoir::KeyRole::Carrier;
  set(3, pl::kKeyNames[key % 12u]);
  set(4, pl::kScaleNames[scale < kScaleTableCount ? scale : 0u]);
  set(5, pl::kKindNames[bottom_ & 3u]);
  set(6, keys_ ? "KEYS" : "CHORDS");
  {
    char num[12] = {};
    if (mode == 0u) {
      const unsigned ct = round_param(e, voice_, Vocoder::Detune);
      (void)pl::format_uint(ct, num);
      append(keys[7].value, num);
      keys[7].pct = pl::bar_percent(static_cast<float>(ct), 0.0f, 20.0f);
    } else if (mode == 1u) {
      const unsigned q = round_param(e, voice_, Vocoder::Quality);
      (void)pl::format_uint(q, num);
      append(keys[7].value, num);
      keys[7].pct = pl::bar_percent(static_cast<float>(q), 8.0f, 24.0f);
    } else {
      const unsigned ms = round_param(e, voice_, Vocoder::Speed);
      (void)pl::format_uint(ms, num);
      append(keys[7].value, num);
      append(keys[7].value, "MS");
      keys[7].pct = pl::bar_percent(static_cast<float>(ms), 0.0f, 500.0f);
    }
  }

  ornoir::Voiceprint vp;
  vp.bars = mode == 0u;
  std::memcpy(vp.level, g_pd.level, sizeof vp.level);
  std::memcpy(vp.peak, g_pd.peak, sizeof vp.peak);

  ornoir::SpeaksState sp;
  sp.source = h.speaks;
  sp.gain = ain.available() ? ain.gain() : 0u;
  sp.in = {g_pd.in_level, g_pd.in_peak};
  sp.clipping = meter_ != nullptr && ain.available() && meter_->clipping();

  // The readout is the latched pad's chord, else the lowest held one.
  int shown = latched_;
  if (shown < 0 && held_ != 0u) {
    shown = 0;
    while (((held_ >> static_cast<unsigned>(shown)) & 1u) == 0u) {
      ++shown;
    }
  }
  ornoir::SoundsState so;
  so.carrier = h.sounds;
  so.spread = round_param(e, voice_, Vocoder::Detune);
  so.voicing = "SMOOTH";
  so.out = {g_pd.out_level, g_pd.out_peak};
  so.out_db = static_cast<int>(g_pd.out_level) * 60 / 255 - 60;
  if (shown >= 0) {
    const unsigned p = static_cast<unsigned>(shown);
    so.chord = g_pd.label[p];
    so.note_count = g_pd.count[p] < 5u ? g_pd.count[p] : 5u;
    for (unsigned i = 0; i < so.note_count; ++i) {
      so.notes[i] = g_pd.notes[p][i];
    }
    so.held = latched_ >= 0 ? "LATCHED" : "HELD";
  }

  ornoir::PadsView pads;
  pads.label = g_pd.label;
  pads.scale_cols = keys_ ? 0xFFFu : pl::scale_cols(key, scale);
  pads.column_in_scale_known = !keys_;
  pads.held = held_;
  pads.latched = latched_;
  if (!keys_) {
    pads.row_name[0] = pl::kRowNames[0];
    pads.row_name[1] = pl::kRowNames[1];
    pads.row_name[2] = pl::kRowNames[2];
    pads.row_name[3] = pl::kKindNames[bottom_ & 3u];
  }
  pads.key_hint = pl::key_hint(focus_, mode);
  pads.hint_left = keys_ ? "KEYS  LEFT RIGHT OCTAVE" : "SHIFT LATCH  DELETE RELEASE  UP DOWN ROW  LEFT RIGHT OCTAVE";

  ui::plot::Canvas c(r);
  c.open();
  ornoir::perform_shapes(c, h, keys, vp, sp, so, pads);
  c.close();
  ornoir::perform_text(r, h, keys, vp, sp, so, pads, mode == 1u ? "TALK: LPC, NO BANDS" : "TUNE: THE PADS ARE THE TARGET");
}

// ---- LEDs and help ------------------------------------------------------------------------------------------------------------------

void PerformScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  const dsp::Engine& e = ctx.engine();
  const unsigned key = round_param(e, voice_, Vocoder::Key);
  const std::uint16_t cols = pl::scale_cols(key, round_param(e, voice_, Vocoder::Scale));
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    std::uint8_t level = app::kLedOff;
    if (keys_) {
      const int note = key_note(pad, octave_);
      if (note < 0) {
        continue;
      }
      level = is_natural(note) ? app::kLedDim : app::kLedOff;
    } else {
      const unsigned col = app::pad_col(pad);
      level = col == key % 12u ? app::kLedLow : (((cols >> col) & 1u) != 0u ? app::kLedDim : app::kLedOff);
      if (static_cast<int>(pad) == latched_) {
        level = app::kLedMid;
      }
    }
    if (((held_ >> pad) & 1u) != 0u) {
      level = app::kLedOn;
    }
    leds.set(pad, level);
  }
}

void PerformScreen::help(const app::Context&, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Params, "THE OLD PLAY SCREEN", nullptr},
      {Button::Performance, "PERFORM", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER (NOT DRAWN YET)", nullptr},
      {Button::File, "FILE (NOT DRAWN YET)", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD (NOT DRAWN YET)", nullptr},
      {Button::SamplePlay, "SAMPLE (NOT DRAWN YET)", nullptr},
      {Button::SampleEdit, "SAMPLE EDIT (NOT DRAWN YET)", nullptr},
      {Button::SampleRec, "SAMPLE REC (NOT DRAWN YET)", nullptr},
      {Button::Song, "SONG (NOT IN THIS VERSION)", nullptr},
      {Button::Pattern, "PATTERN (NOT DRAWN YET)", nullptr},
      {Button::Shift, "WITH A PAD: LATCH ITS CHORD", nullptr},
      {Button::Delete, "RELEASE THE LATCH", nullptr},
      {Button::Up, "BOTTOM ROW: NEXT KIND", nullptr},
      {Button::Down, "BOTTOM ROW: PREVIOUS KIND", nullptr},
      {Button::Left, "OCTAVE DOWN (HOLD: REPEAT)", nullptr},
      {Button::Right, "OCTAVE UP (HOLD: REPEAT)", nullptr},
  };
  static constexpr app::HelpPadGroup kGroups[] = {
      {"POWER", "ROOT AND FIFTH", ui::Color::Sel},
      {"OPEN", "OPEN TRIADS", ui::Color::Sel},
      {"7TH", "SEVENTH CHORDS", ui::Color::Sel},
      {"BOTTOM ROW", "THE KIND UP AND DOWN CHOOSE", ui::Color::Play},
      {"WHITE KEYS", "THE CARRIER'S NOTES", ui::Color::Sel},
      {"BLACK KEYS", "THE SEMITONES", ui::Color::Instr},
  };
  h.summary = "SPEAKS > SOUNDS: THE AUDIO INPUT SPEAKS THROUGH THE CARRIER THE PADS PLAY. F1-F8 POINT THE ENCODER AT A VALUE.";
  h.fkey[0] = "BANDS, TALK, TUNE";
  h.fkey[1] = "THE NEXT AUDIO SOURCE";
  h.fkey[2] = "THE CARRIER";
  h.fkey[3] = "KEY";
  h.fkey[4] = "SCALE";
  h.fkey[5] = "BOTTOM ROW KIND";
  h.fkey[6] = "CHORDS OR KEYS";
  h.fkey[7] = "SPREAD, QUALITY, SPEED";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "CHANGE VALUE (SHIFT COARSE)";
  h.groups = kGroups;
  h.group_count = sizeof kGroups / sizeof kGroups[0];
  h.pads = keys_ ? "A CHROMATIC KEYBOARD, THE BOTTOM ROW LOWEST." : "A CHORD A PAD: COLUMN ROOT, ROW KIND; - IS MINOR.";
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    if (keys_) {
      const int note = key_note(pad, octave_);
      if (note < 0) {
        continue;
      }
      h.pad_group[pad] = is_natural(note) ? 5 : 6;
      (void)app::help_pad_note(h, help_names_, pad, note);
    } else {
      const unsigned row = app::pad_row(pad);
      h.pad_group[pad] = static_cast<std::uint8_t>(row + 1u);
      // HELP draws capitals only: the minor's "m" right after the root becomes "-" (so "Cm7" is "C-7" and "CM7" stays the major seventh).
      std::size_t n = 0;
      bool minor_done = false;
      while (n < app::kHelpPadLabel && g_pd.label[pad][n] != '\0') {
        char ch = g_pd.label[pad][n];
        const char prev = n > 0 ? g_pd.label[pad][n - 1] : '\0';
        if (ch == 'm' && !minor_done && n > 0 && ((prev >= 'A' && prev <= 'Z') || prev == '#')) {
          ch = '-';
          minor_done = true;
        } else if (ch >= 'a' && ch <= 'z') {
          ch = static_cast<char>(ch - 'a' + 'A');
        }
        help_names_[pad][n] = ch;
        ++n;
      }
      help_names_[pad][n] = '\0';
      h.pad_label[pad] = help_names_[pad];
    }
  }
}

}  // namespace o3_voc03er
