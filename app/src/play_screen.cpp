// SPDX-License-Identifier: GPL-3.0-or-later
#include "play_screen.h"

#include <o3_voc03er/pad_map.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/core/placement.h>

#include <cstddef>

namespace o3_voc03er {

using namespace o3sdk;
using app::Button;
using ui::Color;
using ui::Face;
using ui::Rect;

namespace {

O3SDK_BIG app::HelpPadNames g_help_names = {};  // help(): valid while HELP is open

// "C#4" for a MIDI note (60 is C4), into `out`; a rest is "-".
void note_name(int note, char (&out)[5]) noexcept {
  static constexpr char kLetters[12] = {'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B'};
  static constexpr bool kSharp[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
  std::size_t n = 0;
  if (note <= 0) {
    out[n++] = '-';
  } else {
    out[n++] = kLetters[note % 12];
    if (kSharp[note % 12]) {
      out[n++] = '#';
    }
    out[n++] = static_cast<char>('0' + (note / 12 > 0 ? note / 12 - 1 : 0) % 10);  // the notes below 12 (octave -1) show as 0
  }
  out[n] = '\0';
}

// The card as the files service sees it (Files::card()), in the words the screen uses.
const char* card_text(const app::Files& files) noexcept {
  if (!files.available()) {
    return "NO CARD";
  }
  switch (files.card()) {
    case app::Files::Card::None: return "NO CARD";
    case app::Files::Card::Ready: return "READY";
    case app::Files::Card::ReadOnly: return "WRITE PROTECTED";
    case app::Files::Card::Unsupported: return "NOT SUPPORTED";
    case app::Files::Card::OnComputer: return "ON COMPUTER";
    case app::Files::Card::InUse: return "UPDATE RUNNING";
  }
  return "";
}

}  // namespace

void PlayScreen::init(dsp::UnitId voice, const InputMeter* meter, const Vocoder* loops, PatternData* pattern) noexcept {
  // setup() can run again on the same app object (every test starts a fresh simulator on the one o3sdk_app() instance): start from scratch.
  voice_ = voice;
  meter_ = meter;
  loops_ = loops;
  loop_sel_ = 0;
  octave_ = 0;
  pd_ = pattern;
  cursor_ = 0;
  note_ = nullptr;
  page_ = app::ParamPage{"VOCODER", voice,
                         {Vocoder::Mode, Vocoder::Quality, Vocoder::Freeze, Vocoder::Dry, Vocoder::Level, Vocoder::Wave, Vocoder::Detune,
                          Vocoder::Release},
                         8};
  tune_page_ = false;
  keys_.set_pages(&page_, 1);  // the table must outlive the helper: it is a member
  for (auto& n : held_) {
    n = -1;
  }
}

// F1..F8 follow the mode: TUNE has its own values (key, scale, speed, amount, notes in); the other modes keep the vocoder's page.
void PlayScreen::sync_page(const dsp::Engine& e) const noexcept {
  const bool tune = e.param(voice_, Vocoder::Mode) >= 1.5f;
  if (tune == tune_page_) {
    return;
  }
  tune_page_ = tune;
  if (tune) {
    page_.name = "TUNE";
    const std::uint16_t t[8] = {Vocoder::Mode, Vocoder::Key, Vocoder::Scale, Vocoder::Speed, Vocoder::Amount, Vocoder::NotesIn, Vocoder::Dry, Vocoder::Level};
    for (unsigned i = 0; i < 8; ++i) {
      page_.ids[i] = t[i];
    }
  } else {
    page_.name = "VOCODER";
    const std::uint16_t v[8] = {Vocoder::Mode, Vocoder::Quality, Vocoder::Freeze, Vocoder::Dry, Vocoder::Level, Vocoder::Wave, Vocoder::Detune, Vocoder::Release};
    for (unsigned i = 0; i < 8; ++i) {
      page_.ids[i] = v[i];
    }
  }
}

void PlayScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  sync_page(ctx.engine());
  if (keys_.input(ctx, in)) {  // F1..F8 and the encoder
    return;
  }
  if (loops_ != nullptr) {
    static constexpr Button kPick[Loop::kCount] = {Button::Note, Button::Instr, Button::Vol, Button::Fx};
    for (unsigned i = 0; i < Loop::kCount; ++i) {
      if (in.press(kPick[i])) {
        loop_sel_ = i;
        ctx.redraw();
        return;
      }
    }
    const Vocoder::LoopStatus st = loops_->loop_status(loop_sel_);
    dsp::Engine& e = ctx.engine();
    if (in.press(Button::Rec)) {  // arm, or stop at the next bar line
      (void)e.set_param(voice_, Vocoder::loop_param(loop_sel_, Vocoder::LoopField::Rec), st.rec == Vocoder::LoopStatus::Rec::Idle ? 1.0f : 0.0f);
      ctx.redraw();
      return;
    }
    if (in.press(Button::Up)) {
      (void)e.set_param(voice_, Vocoder::loop_param(loop_sel_, Vocoder::LoopField::Play), st.playing || st.pending ? 0.0f : 1.0f);
      ctx.redraw();
      return;
    }
    if (in.press(Button::Down)) {
      (void)e.set_param(voice_, Vocoder::loop_param(loop_sel_, Vocoder::LoopField::Role), st.carrier ? 0.0f : 1.0f);
      ctx.redraw();
      return;
    }
    if (in.press(Button::Copy)) {  // speed: 0.5X, 1X, 2X, round
      (void)e.set_param(voice_, Vocoder::loop_param(loop_sel_, Vocoder::LoopField::Speed), static_cast<float>((st.speed + 1u) % 3u));
      ctx.redraw();
      return;
    }
    if (in.press(Button::Insert)) {  // direction
      (void)e.set_param(voice_, Vocoder::loop_param(loop_sel_, Vocoder::LoopField::Dir), st.reverse ? 0.0f : 1.0f);
      ctx.redraw();
      return;
    }
    if (in.press(Button::SampleEdit) || in.press(Button::SampleRec)) {  // level, 3 dB a press
      const float db = st.level_db + (in.press(Button::SampleEdit) ? -3.0f : 3.0f);
      (void)e.set_param(voice_, Vocoder::loop_param(loop_sel_, Vocoder::LoopField::Level), db < -24.0f ? -24.0f : (db > 12.0f ? 12.0f : db));
      ctx.redraw();
      return;
    }
  }
  if (in.press(Button::Delete)) {
    clear_pattern(*pd_);
    cursor_ = 0;
    note_ = nullptr;
    ctx.redraw();
  } else if (in.press(Button::Pattern)) {  // the carrier of the step under the cursor: none, synth, noise, loop 1..4 (stepseq.h)
    (void)cycle_carrier(*pd_, cursor_);
    ctx.redraw();
  } else if (in.shift && (in.press(Button::Left) || in.press(Button::Right))) {  // the cursor, a step
    cursor_ = (cursor_ + (in.press(Button::Left) ? kPatternSteps - 1u : 1u)) % kPatternSteps;
    ctx.redraw();
  } else if (in.press(Button::Left) || in.press(Button::Right)) {
    octave_ = clamp_octave(octave_ + (in.press(Button::Left) ? -1 : 1));
    ctx.redraw();
  } else if (in.pad_press()) {
    const int note = key_note(in.pad(), octave_);
    if (note >= 0) {
      held_[in.pad()] = note;
      record_note(*pd_, cursor_, note);  // the pattern is what the pads played; FILE saves it
      ctx.redraw();
      (void)ctx.engine().note_on(voice_, static_cast<std::uint8_t>(note), 110, o3_voc03er::Vocoder::kNoteLocal);
    }
  } else if (in.pad_release()) {
    if (held_[in.pad()] >= 0) {
      (void)ctx.engine().send(voice_, o3_voc03er::Vocoder::note_off_local(static_cast<std::uint8_t>(held_[in.pad()])));
      held_[in.pad()] = -1;
    }
  }
}

void PlayScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  // The pads as they are laid out; a held pad is lit in the accent colour, a white key is filled and a black key framed.
  const int cell_w = 30;
  const int cell_h = 26;
  const int map_x = 16;
  const int map_y = area.y + 34;
  r.text(map_x, area.y + 12, "PADS", Color::TextDim, Face::Label);
  char oct[8] = "OCT +0";
  oct[4] = octave_ < 0 ? '-' : '+';
  oct[5] = static_cast<char>('0' + (octave_ < 0 ? -octave_ : octave_));
  r.text(map_x + 12 * cell_w - r.text_width(oct, Face::Label), area.y + 12, oct, Color::Text, Face::Label);
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    const Rect c{map_x + static_cast<int>(app::pad_col(pad)) * cell_w, map_y + static_cast<int>(app::pad_row(pad)) * cell_h,
                 cell_w - 3, cell_h - 3};
    const int note = key_note(pad, octave_);
    if (ctx.panel().pad_held(pad)) {
      r.panel(c, Color::Accent);
    } else if (note >= 0 && is_natural(note)) {
      r.panel(c, Color::Empty);
    } else {
      r.frame(c, Color::Empty);
    }
    if (note >= 0 && note % 12 == 0) {
      char name[4] = {'C', static_cast<char>('0' + (note / 12 - 1) % 10), '\0', '\0'};
      r.text(c.x + 4, c.y + 6, name, ctx.panel().pad_held(pad) ? Color::AccentText : Color::Text, Face::Micro);
    }
  }

  // The output level, and what the keys do.
  const int rx = 420;
  const int rw = ui::kScreenW - rx - 16;
  r.text(rx, area.y + 12, "OUT", Color::TextDim, Face::Label);
  const dsp::EngineStats s = ctx.engine().stats();
  app::level_meter(r, Rect{rx + 40, area.y + 14, rw - 40, 10}, s.peak_l > s.peak_r ? s.peak_l : s.peak_r);
  // The input: its peak (the louder channel; CLIP when it reaches full scale, since gain 0 is the ADC's 0 dB and a hot source can only be
  // turned down at the source), the mode and the source.
  const app::AudioIn& ain = ctx.audio_in();
  const float in_peak = meter_ != nullptr && ain.available() ? meter_->peak() : 0.0f;
  const bool clip = meter_ != nullptr && ain.available() && meter_->clipping();
  r.text(rx, area.y + 32, "IN", Color::TextDim, Face::Label);
  app::level_meter(r, Rect{rx + 40, area.y + 34, rw - 100, 10}, in_peak);
  const unsigned pm = permille(in_peak);
  char pct[8] = {};
  std::size_t n = 0;
  if (pm >= 1000u) {
    pct[n++] = '1';
    pct[n++] = '0';
    pct[n++] = '0';
  } else {
    if (pm >= 100u) {
      pct[n++] = static_cast<char>('0' + pm / 100u);
    }
    pct[n++] = static_cast<char>('0' + (pm / 10u) % 10u);
  }
  pct[n++] = '%';
  r.text(rx + rw - 54, area.y + 32, clip ? "CLIP" : pct, clip ? Color::Accent : Color::Text, Face::Label);
  const float mode_v = ctx.engine().param(voice_, Vocoder::Mode);
  const char* mode_name = mode_v >= 1.5f ? "TUNE" : (mode_v >= 0.5f ? "TALK" : "BANDS");
  const SourceName* src = ain.available() ? find_source(ain.source()) : nullptr;
  r.text(rx, area.y + 54, "MODE", Color::TextDim, Face::Label);
  r.text(rx + 48, area.y + 54, mode_name, Color::Text, Face::Label);
  r.text(rx + 130, area.y + 54, "PLAY", Color::TextDim, Face::Micro);
  r.text(rx, area.y + 74, "SOURCE", Color::TextDim, Face::Label);
  r.text(rx + 70, area.y + 74, src != nullptr ? src->label : "NONE", Color::Text, Face::Label);
  r.text(rx + 190, area.y + 74, "SAMPLE PLAY", Color::TextDim, Face::Micro);
  // The card and the last file job (Context::files(), polled: the screen never waits for the card). The kit's strip shows the same result
  // for a few seconds; this line stays.
  const app::Files& files = ctx.files();
  const app::Files::Status fs = files.status();
  r.text(rx, area.y + 96, "CARD", Color::TextDim, Face::Label);
  r.text(rx + 48, area.y + 96, card_text(files), files.available() && files.card() == app::Files::Card::Ready ? Color::Play : Color::Text, Face::Label);
  r.text(rx, area.y + 116, "FILE", Color::TextDim, Face::Label);
  r.text(rx + 48, area.y + 116, note_ != nullptr ? note_ : (fs.phase == app::Files::Status::Idle ? "-" : fs.text),
         note_ != nullptr || fs.phase == app::Files::Status::Failed ? Color::Accent : Color::Text, Face::Label);
  const int hy = map_y + static_cast<int>(app::kPadRows) * cell_h + 18;
  r.text(16, hy, "PADS play chords   F1-F8 pick a value   ENCODER turn it (SHIFT coarse)   LEFT/RIGHT octave", Color::TextDim, Face::Label);

  // The pattern: 16 steps, the cursor framed.
  r.text(16, hy + 20, "PATTERN: pads record  DELETE clear  FILE save  LOAD load  PATTERN key: carrier  SHIFT+L/R: step", Color::TextDim, Face::Label);
  const int step_w = 40;
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    const Rect c{16 + static_cast<int>(i) * step_w, hy + 38, step_w - 4, 26};
    const Step st = get_step(*pd_, i);
    if (st.note != 0) {
      r.panel(c, Color::Empty);
    } else {
      r.frame(c, Color::Empty);
    }
    if (i == cursor_) {
      r.frame(c, Color::Accent);
    }
    char name[5];
    note_name(st.note, name);
    r.text(c.x + 4, c.y + 3, name, Color::Text, Face::Micro);
    if (st.carrier != 0) {  // the step's carrier, under the note
      static constexpr const char* kTag[] = {"", "SY", "NZ", "L1", "L2", "L3", "L4"};
      r.text(c.x + 4, c.y + 14, kTag[st.carrier], Color::Accent, Face::Micro);
    }
  }

  if (loops_ != nullptr) {  // the four loops under the pattern: number and state, length, role; the selected one framed
    const int ly = hy + 76;
    for (unsigned i = 0; i < Loop::kCount; ++i) {
      const Vocoder::LoopStatus st = loops_->loop_status(i);
      const int x = 16 + static_cast<int>(i) * 196;
      const Rect c{x, ly, 188, 52};
      r.frame(c, i == loop_sel_ ? Color::Accent : Color::Empty);
      char num[2] = {static_cast<char>('1' + i), '\0'};
      r.text(x + 8, ly + 8, num, Color::Text, Face::Label);
      const char* state = st.rec == Vocoder::LoopStatus::Rec::Armed       ? "ARMED"
                          : st.rec == Vocoder::LoopStatus::Rec::Recording ? "REC"
                          : st.rec == Vocoder::LoopStatus::Rec::Stopping  ? "STOPPING"
                          : st.playing                                    ? "PLAY"
                          : st.pending                                    ? "WAIT"
                          : st.length > 0                                 ? "STOP"
                                                                          : "EMPTY";
      r.text(x + 28, ly + 8, state, st.rec != Vocoder::LoopStatus::Rec::Idle ? Color::Accent : (st.playing ? Color::Play : Color::Text), Face::Label);
      const unsigned tenths = st.length * 10u / 44100u;  // integer: the screen is not a sample loop, and a length is only shown
      char len[8] = {static_cast<char>('0' + (tenths / 100u) % 10u), static_cast<char>('0' + (tenths / 10u) % 10u), '.',
                     static_cast<char>('0' + tenths % 10u), 'S', '\0', '\0', '\0'};
      r.text(x + 8, ly + 28, st.carrier ? "CARRIER" : "MODULATOR", Color::Text, Face::Label);
      {  // speed, direction, level: "0.5X REV -3"
        char opt[14] = {};
        unsigned q = 0;
        const char* sp = st.speed == 0u ? "0.5X" : (st.speed == 2u ? "2X" : "1X");
        while (*sp != '\0') {
          opt[q++] = *sp++;
        }
        opt[q++] = ' ';
        const char* dir = st.reverse ? "REV" : "FWD";
        while (*dir != '\0') {
          opt[q++] = *dir++;
        }
        opt[q++] = ' ';
        const int db = static_cast<int>(st.level_db);
        if (db < 0) {
          opt[q++] = '-';
        } else if (db > 0) {
          opt[q++] = '+';
        }
        const unsigned a = static_cast<unsigned>(db < 0 ? -db : db);
        if (a >= 10u) {
          opt[q++] = static_cast<char>('0' + a / 10u);
        }
        opt[q++] = static_cast<char>('0' + a % 10u);
        r.text(x + 8, ly + 40, opt, Color::TextDim, Face::Micro);
      }
      r.text(x + 130, ly + 28, st.length > 0 ? (len[0] == '0' ? len + 1 : len) : "-", Color::TextDim, Face::Label);
    }
    r.text(16, ly + 60, "LOOPS: NOTE-FX PICK  REC arm/stop at the bar  UP play  DOWN role", Color::TextDim, Face::Label);
    r.text(16, ly + 80, "COPY speed  INSERT dir  EDIT/REC level", Color::TextDim, Face::Label);
  }

  // Eight cells above the soft keys: name, value and a bar (the kit's widget).
  sync_page(ctx.engine());
  app::knob_strip(r, Rect{0, area.y + area.h - app::kKnobStripH, ui::kScreenW, app::kKnobStripH}, ctx.engine(), keys_);
}

void PlayScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  const app::ParamPage* p = keys_.page();
  if (p == nullptr) {
    return;
  }
  for (unsigned k = 0; k < p->count; ++k) {
    const core::ParamDesc* d = ctx.engine().param_desc(p->unit, p->ids[k]);
    out.label[k] = d != nullptr ? d->name : "";
  }
  out.hot = static_cast<std::uint8_t>(1u << keys_.focus());  // the key the encoder drives now
}

// HELP (Shift + Config, o3-sdk app/help.h): the kit draws it from this data, in the screen's theme. Every control this screen uses is here, in upper case, from static tables (the limits are in
// docs/FRAMEWORK.md, "Help", and tests/app_test.cpp checks them with help_check()). A button with no entry is shown dimmed. The pads' labels are built per call: the note a pad plays moves with
// the octave. FILE and SAMPLE LOAD are handled in app.cpp. Look at it: app/scenarios/help.txt (Shift + Config, BUTTONS, PADS).
void PlayScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Params, "THIS SCREEN", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Master, "MASTER (NOT DRAWN YET)", nullptr},
      {Button::Song, "SONG (SLICE U9)", nullptr},
      {Button::SamplePlay, "THE NEXT AUDIO SOURCE", nullptr},
      {Button::File, "SAVE THE PATTERN", nullptr},
      {Button::SampleLoad, "LOAD THE PATTERN", nullptr},
      {Button::Delete, "CLEAR THE PATTERN", nullptr},
      {Button::Pattern, "STEP CARRIER: SY NZ L1-L4", nullptr},
      {Button::Note, "PICK LOOP 1", nullptr},
      {Button::Instr, "PICK LOOP 2", nullptr},
      {Button::Vol, "PICK LOOP 3", nullptr},
      {Button::Fx, "PICK LOOP 4", nullptr},
      {Button::Rec, "ARM OR STOP THE LOOP RECORD", nullptr},
      {Button::Up, "PLAY OR STOP THE LOOP", nullptr},
      {Button::Down, "LOOP ROLE: MOD OR CAR", nullptr},
      {Button::Copy, "LOOP SPEED: 0.5X, 1X, 2X", nullptr},
      {Button::Insert, "LOOP DIRECTION: FWD OR REV", nullptr},
      {Button::SampleEdit, "LOOP LEVEL DOWN 3 DB", nullptr},
      {Button::SampleRec, "LOOP LEVEL UP 3 DB", nullptr},
      {Button::Left, "KEYBOARD AN OCTAVE DOWN", "CURSOR ONE STEP BACK"},
      {Button::Right, "KEYBOARD AN OCTAVE UP", "CURSOR ONE STEP ON"},
  };
  static constexpr app::HelpPadGroup kGroups[] = {
      {"WHITE KEYS", "PLAY THE CARRIER, CHORDS, RECORD", Color::Sel},
      {"BLACK KEYS", "THE SEMITONES, THE SAME", Color::Instr},
  };
  h.summary = "THE AUDIO INPUT SPEAKS THROUGH THE PADS' CHORD. F1-F8 POINT THE ENCODER AT A VALUE (A TAP LATCHES IT) AND THE ENCODER CHANGES IT.";
  h.fkey[0] = "BANDS, TALK, TUNE";
  if (ctx.engine().param(voice_, Vocoder::Mode) >= 1.5f) {  // TUNE: the F keys follow sync_page()
    h.summary = "TUNE: THE VOICE IS PITCH-CORRECTED TO THE KEY AND SCALE, OR TO THE HELD PADS. F1-F8 POINT THE ENCODER AT A VALUE.";
    h.fkey[1] = "KEY";
    h.fkey[2] = "SCALE";
    h.fkey[3] = "SPEED, MS: 0 IS STEPPED";
    h.fkey[4] = "HOW MUCH IS CORRECTED";
    h.fkey[5] = "HELD PADS ARE THE TARGET";
    h.fkey[6] = "DRY INPUT";
    h.fkey[7] = "OUTPUT LEVEL";
  } else {
    h.fkey[1] = "LPC ORDER";
    h.fkey[2] = "HOLD THE VOWEL";
    h.fkey[3] = "DRY INPUT";
    h.fkey[4] = "OUTPUT LEVEL";
    h.fkey[5] = "SAW OR PULSE";
    h.fkey[6] = "DETUNE, CENTS";
    h.fkey[7] = "NOTE RELEASE";
  }
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "CHANGE VALUE (SHIFT COARSE)";
  h.groups = kGroups;
  h.group_count = sizeof kGroups / sizeof kGroups[0];
  h.pads = "ONE OCTAVE A ROW, THE BOTTOM ROW LOWEST. LEFT / RIGHT MOVE THE WHOLE KEYBOARD.";
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    const int note = key_note(pad, octave_);
    if (note < 0) {  // past 0..127: not a key, drawn dark
      continue;
    }
    h.pad_group[pad] = is_natural(note) ? 1 : 2;
    (void)app::help_pad_note(h, g_help_names, pad, note);  // "C3", "F#4": 60 is C4, as the screen's own names
  }
}

void PlayScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  // Monochrome LEDs: a few levels carry the meaning (app/pad_leds.h). A white key is dim, a held pad is full.
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    const int note = key_note(pad, octave_);
    if (note < 0) {
      continue;
    }
    leds.set(pad, ctx.panel().pad_held(pad) ? app::kLedOn : (is_natural(note) ? app::kLedDim : app::kLedOff));
  }
}

}  // namespace o3_voc03er
