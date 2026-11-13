// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_screen.h"

#include "params_logic.h"
#include "perform_screen.h"
#include "skin/ornoir.h"

#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/context.h>

namespace o3_voc03er {

using namespace o3sdk;
using ui::Color;
using ui::Face;
using Button = app::Button;

namespace {

constexpr std::uint64_t kNoteMs = 2000;
constexpr std::uint64_t kConfirmMs = 2000;  // the second DELETE tap must come within this
// Two columns of eight rows.
constexpr unsigned kRowsPerColumn = kChainMax / 2u;
constexpr int kX0 = 16, kColStride = 392, kRowW = 376, kY0 = 78, kRowStep = 36, kRowH = 32;

void copy_text(char (&dst)[12], const char* s) noexcept {
  std::size_t n = 0;
  while (s[n] != '\0' && n + 1u < sizeof dst) {
    dst[n] = s[n];
    ++n;
  }
  dst[n] = '\0';
}

// "7", "12": a number of at most three digits at buf, NUL-terminated; returns the length.
unsigned put_dec(char* buf, unsigned v) noexcept {
  char tmp[3];
  unsigned n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + v % 10u);
    v /= 10u;
  } while (v != 0u && n < sizeof tmp);
  for (unsigned i = 0; i < n; ++i) {
    buf[i] = tmp[n - 1u - i];
  }
  buf[n] = '\0';
  return n;
}

}  // namespace

void SongScreen::init(dsp::UnitId voice, Chain* chain, StepSeq* seq, app::Screen* back) noexcept {
  voice_ = voice;
  chain_ = chain;
  seq_ = seq;
  back_ = back;
  player_ = song::Player{};
  cursor_ = 0;
  first_ = false;
  repeats_focus_ = false;
  stop_pending_ = false;
  note_ = nullptr;
}

const ui::Theme* SongScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void SongScreen::enter(app::Context& ctx) noexcept {
  note_ = nullptr;
  if (chain_ != nullptr) {
    cursor_ = static_cast<std::uint8_t>(song::clamp_cursor(*chain_, cursor_));
  }
  ctx.redraw();
}

void SongScreen::halt() noexcept {
  const bool was = player_.active || stop_pending_;
  player_ = song::Player{};
  stop_pending_ = false;
  if (was && seq_ != nullptr) {
    seq_->request_pattern(kBankPatterns);  // no pattern pending: the transport plays on with the one it plays
  }
}

void SongScreen::say(app::Context& ctx, const char* text) noexcept {
  note_ = text;
  note_until_ = ctx.uptime_ms() + kNoteMs;
  ctx.redraw();
}

// ---- playback -----------------------------------------------------------------------------------------------------------------------

void SongScreen::sync_ahead() noexcept {
  if (!player_.active || chain_ == nullptr || seq_ == nullptr) {
    return;
  }
  if (first_) {  // the Play has not taken its pattern yet: the request is still the lap in progress's own
    seq_->request_pattern(song::pattern_of(*chain_, player_.entry));
    return;
  }
  unsigned e = 0;
  unsigned l = 0;
  if (song::next_lap(*chain_, player_, e, l)) {
    seq_->request_pattern(song::pattern_of(*chain_, e));
  } else {
    seq_->request_pattern(kBankPatterns);  // the chain ends with this lap
  }
}

void SongScreen::chain_replaced() noexcept {
  if (chain_ == nullptr) {
    return;
  }
  cursor_ = static_cast<std::uint8_t>(song::clamp_cursor(*chain_, cursor_));
  if (!player_.active) {
    return;
  }
  player_.entry = 0;
  player_.lap = 0;
  player_.gone = !first_;  // gone: the lap in progress finishes and the next lap is the first of the entry at index 0 (the Play's own lap is that entry's already)
  sync_ahead();
}

bool SongScreen::start(app::Context& ctx) noexcept {
  dsp::Engine& e = ctx.engine();
  if (chain_ == nullptr || seq_ == nullptr || chain_->count == 0u) {
    return false;
  }
  if (e.playing()) {  // PLAY stops the transport (the app's, below this call); never in silence
    say(ctx, song::play_note(player_.active, e.following()));
    return false;
  }
  if (!song::start(*chain_, cursor_, player_)) {
    return false;
  }
  stop_pending_ = false;
  first_ = true;
  last_laps_ = static_cast<std::uint8_t>(seq_->laps());
  seq_->request_pattern(song::pattern_of(*chain_, player_.entry));  // taken at the Play, as a PATTERN request is
  (void)e.play();
  ctx.redraw();
  return true;
}

// The player's per-pass part, from App::update every pass of the main loop, on whichever screen is up: a screen button leaves SONG at any moment
// (the kit gives App::input the key first), and a chain that only SONG's update walked would stop mid-song. PLAY stops it (!playing() below).
void SongScreen::service(app::Context& ctx) noexcept {
  if (chain_ != nullptr && seq_ != nullptr) {
    dsp::Engine& e = ctx.engine();
    if (player_.active && !e.playing()) {
      halt();  // stopped by PLAY
    }
    if (player_.active) {
      const unsigned now = seq_->laps();
      unsigned n = song::laps_between(last_laps_, now);
      last_laps_ = static_cast<std::uint8_t>(now);
      const bool lapped = n != 0u;
      bool ended = false;
      for (; n != 0u && !ended; --n) {
        if (first_) {
          first_ = false;  // the lap begin of the Play
        } else if (!song::advance(*chain_, player_)) {
          ended = true;
        }
      }
      if (ended) {
        const std::uint32_t ms = song::step_ms(e.stats().tempo_mbpm);
        stop_pending_ = true;
        stop_at_ = ctx.uptime_ms() + ms * 7u / 8u;  // past the last step's gate (75 %), before the next lap's first step
        seq_->request_pattern(kBankPatterns);
      } else if (lapped) {
        sync_ahead();
      }
    }
    if (stop_pending_) {
      if (!e.playing()) {
        stop_pending_ = false;
      } else if (ctx.uptime_ms() >= stop_at_) {
        stop_pending_ = false;
        (void)e.stop();
      }
    }
  }
}

// What drawing needs: the note's expiry and the redraw (the lit row and the lap move with the transport).
void SongScreen::update(app::Context& ctx) noexcept {
  if (note_ != nullptr && ctx.uptime_ms() >= note_until_) {
    note_ = nullptr;
  }
  ctx.redraw();  // the lit row and the lap move with the transport
}

// ---- input --------------------------------------------------------------------------------------------------------------------------

void SongScreen::add(app::Context& ctx) noexcept {
  unsigned at = 0;
  if (!song::add_after(*chain_, cursor_, seq_ != nullptr ? seq_->playing_pattern() : 0u, at)) {
    say(ctx, "THE CHAIN IS FULL");
    return;
  }
  song::inserted(player_, at);
  cursor_ = static_cast<std::uint8_t>(at);
  sync_ahead();
  ctx.redraw();
}

void SongScreen::disarm() noexcept {
  if (confirm_.armed) {
    confirm_.clear();
    note_ = nullptr;
  }
}

void SongScreen::remove(app::Context& ctx) noexcept {
  if (chain_->count == 0u) {
    return;
  }
  if (!confirm_.tap(cursor_, ctx.uptime_ms(), kConfirmMs)) {
    say(ctx, "DELETE AGAIN TO REMOVE THE ROW");
    return;
  }
  note_ = nullptr;
  unsigned c = cursor_;
  const unsigned at = cursor_;
  if (!song::remove_at(*chain_, at, c)) {
    return;
  }
  song::removed(player_, at);
  cursor_ = static_cast<std::uint8_t>(c);
  sync_ahead();
  ctx.redraw();
}

void SongScreen::edit(app::Context& ctx, int delta) noexcept {
  if (chain_->count == 0u) {
    return;
  }
  if (repeats_focus_) {
    song::step_repeats(*chain_, cursor_, delta);
  } else {
    song::step_pattern(*chain_, cursor_, delta);
  }
  sync_ahead();
  ctx.redraw();
}

void SongScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  if (chain_ == nullptr) {
    return;
  }
  unsigned k = 0;
  if (in.type != app::Input::Type::Release && !in.press(Button::Delete)) {  // a release of DELETE itself must not undo its ask
    disarm();
  }
  if (in.fkey_press(k)) {
    if (in.shift) {
      return;
    }
    if (k == 0u) {
      add(ctx);
    } else if (k == 1u) {
      if (chain_->count == 0u) {
        say(ctx, "ADD AN ENTRY FIRST");
      } else {
        repeats_focus_ = !repeats_focus_;
        ctx.redraw();
      }
    } else if (k == 2u) {
      chain_->loop = chain_->loop != 0u ? 0u : 1u;
      sync_ahead();  // the last lap may now have a successor, or none
      ctx.redraw();
    } else if (k == 7u && back_ != nullptr) {
      ctx.show(*back_);
    }
    return;
  }
  if (in.turn()) {
    edit(ctx, in.delta());
  } else if (in.press(Button::Left) || in.press(Button::Right)) {  // one encoder step: the row's pattern, or its repeats while F2 is lit; repeats arrive as presses
    edit(ctx, in.press(Button::Left) ? -1 : 1);
  } else if (in.press(Button::Up)) {
    cursor_ = static_cast<std::uint8_t>(song::move_cursor(*chain_, cursor_, -1));
    ctx.redraw();
  } else if (in.press(Button::Down)) {
    cursor_ = static_cast<std::uint8_t>(song::move_cursor(*chain_, cursor_, 1));
    ctx.redraw();
  } else if (in.press(Button::Insert)) {
    add(ctx);
  } else if (in.press(Button::Delete)) {
    remove(ctx);
  }
}

std::uint64_t SongScreen::repeat_buttons(const app::Context&) const noexcept {
  return (std::uint64_t{1} << static_cast<unsigned>(Button::Left)) | (std::uint64_t{1} << static_cast<unsigned>(Button::Right));
}

// ---- keys, HELP -----------------------------------------------------------------------------------------------------------------------

void SongScreen::soft_keys(app::Context&, app::SoftKeys& out) noexcept {
  static constexpr const char* kNames[8] = {"ADD", "REPEATS", "LOOP", "", "", "", "", "PERFORM"};
  for (unsigned i = 0; i < 8u; ++i) {
    out.label[i] = kNames[i];
  }
}

void SongScreen::help(const app::Context&, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Up, "ROW BEFORE", nullptr},
      {Button::Down, "ROW AFTER", nullptr},
      {Button::Left, "PATTERN OR REPEATS DOWN", nullptr},
      {Button::Right, "PATTERN OR REPEATS UP", nullptr},
      {Button::Insert, "ADD AFTER THE CURSOR", nullptr},
      {Button::Delete, "REMOVE THE ROW: TAP TWICE", nullptr},
      {Button::Play, "PLAY THE CHAIN FROM HERE", nullptr},
      {Button::Song, "SONG: THIS SCREEN", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Params, "PARAMS: THE VALUES", nullptr},
      {Button::Master, "MASTER", nullptr},
      {Button::File, "FILE", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD", nullptr},
      {Button::SamplePlay, "SAMPLE: THE SLOTS", nullptr},
      {Button::SampleEdit, "SAMPLE EDIT: THE CUTS", nullptr},
      {Button::SampleRec, "SAMPLE REC: TAKE A LOOP", nullptr},
      {Button::Pattern, "PATTERN: THE 16 STEPS", nullptr},
  };
  h.summary = "THE CHAIN: UP TO 16 ROWS OF A PATTERN AND ITS REPEATS. PLAY RUNS IT FROM THE\n"
              "CURSOR AND IT PLAYS ON WHEN YOU LEAVE; PLAY AGAIN STOPS IT. A CHANGE LANDS AT THE PATTERN'S END.";
  h.fkey[0] = "ADD AFTER THE CURSOR";
  h.fkey[1] = "ENCODER: THE REPEATS";
  h.fkey[2] = "LOOP THE CHAIN AT ITS END";
  h.fkey[7] = "GO TO PERFORM";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "THE ROW'S PATTERN";
}

// ---- draw ---------------------------------------------------------------------------------------------------------------------------

void SongScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  if (chain_ == nullptr) {
    return;
  }
  const Chain& ch = *chain_;
  const dsp::Engine& e = ctx.engine();
  const app::AudioIn& ain = ctx.audio_in();
  const SourceName* src = ain.available() ? find_source(ain.source()) : nullptr;
  const unsigned mode = perform::mode_index(e.param(voice_, Vocoder::Mode));

  ornoir::HeaderState h;
  header_route(e, voice_, src != nullptr ? src->label : "NONE", src != nullptr && src->source == hal::capture::Source::Usb, h);
  h.mode = perform::kModeNames[mode];
  h.playing = e.playing();
  h.tempo_x10 = e.stats().tempo_mbpm / 100u;
  h.cpu_percent = ctx.cpu_percent();

  const unsigned cur = song::clamp_cursor(ch, cursor_);
  ornoir::FKey keys[ornoir::kFKeys];
  keys[0].name = "ADD";
  keys[1].name = "REPEATS";
  keys[1].hot = repeats_focus_ && ch.count != 0u;
  keys[1].dim = ch.count == 0u;
  if (ch.count != 0u) {
    char* v = keys[1].value;
    const unsigned n = put_dec(v, ch.entry[cur].repeats);
    v[n] = '\0';
  }
  keys[2].name = "LOOP";
  copy_text(keys[2].value, ch.loop != 0u ? "ON" : "OFF");
  keys[2].hot = ch.loop != 0u;
  keys[7].name = "PERFORM";

  const unsigned lit = playing_row();
  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  for (unsigned i = 0; i < ch.count; ++i) {
    const int x = kX0 + static_cast<int>(i / kRowsPerColumn) * kColStride;
    const int y = kY0 + static_cast<int>(i % kRowsPerColumn) * kRowStep;
    if (i == cur) {
      ornoir::fill_rect(c, x - 3, y - 3, kRowW + 6, kRowH + 6, Color::Accent);
    }
    ornoir::fill_rect(c, x, y, kRowW, kRowH, Color::Pane);
    if (i == lit) {
      ornoir::fill_rect(c, x, y, 10, kRowH, Color::Play);
    }
  }
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  if (ch.count == 0u) {
    r.text(kX0, kY0 + 20, "NO ENTRIES. F1 OR INSERT ADDS ONE, WITH THE PATTERN PLAYING NOW.", Color::TextDim, Face::Label);
  }
  for (unsigned i = 0; i < ch.count; ++i) {
    const int x = kX0 + static_cast<int>(i / kRowsPerColumn) * kColStride;
    const int y = kY0 + static_cast<int>(i % kRowsPerColumn) * kRowStep;
    char idx[4] = {};
    char* p = idx;
    if (i + 1u < 10u) {
      *p++ = '0';
    }
    (void)put_dec(p, i + 1u);
    r.text(x + 22, y + 10, idx, Color::TextDim, Face::Label);

    const char letter[2] = {static_cast<char>('A' + (ch.entry[i].pattern < kBankPatterns ? ch.entry[i].pattern : 0u)), '\0'};
    r.text(x + 70, y + 4, letter, i == lit ? Color::Play : Color::Text, Face::Display);

    char rep[8] = {'X', ' '};
    (void)put_dec(rep + 2, ch.entry[i].repeats);
    r.text(x + 130, y + 10, rep, i == cur && repeats_focus_ ? Color::Sel : Color::Text, Face::Label);

    if (i == lit) {  // "LAP 2 OF 4"
      char lap[16] = {'L', 'A', 'P', ' '};
      unsigned n = 4;
      n += put_dec(lap + n, static_cast<unsigned>(player_.lap) + 1u);
      lap[n++] = '/';
      (void)put_dec(lap + n, ch.entry[i].repeats);
      r.text(x + 230, y + 10, lap, Color::Play, Face::Label);
    }
  }
  if (note_ != nullptr) {
    r.text(kX0, 380, note_, Color::TextDim, Face::Label);
  } else {  // two micro lines, as PATTERN's, clear of the F row at y 419: what the keys do (a note, when there is one, takes their place)
    r.text(kX0, 391, "UP, DOWN: A ROW. ENCODER, LEFT, RIGHT: ITS PATTERN, A TO H. F2: THEY SET ITS REPEATS (1 TO 64) UNTIL F2 AGAIN.", Color::TextDim, Face::Micro);
    r.text(kX0, 403, "F1, INSERT: ADD A ROW AFTER THE CURSOR (SAME PATTERN, 1 REPEAT). DELETE TWICE: REMOVE IT. F3 LOOP: RESTART AT ROW 1.", Color::TextDim,
           Face::Micro);
  }
}

}  // namespace o3_voc03er
