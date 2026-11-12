// SPDX-License-Identifier: GPL-3.0-or-later
#include "pattern_screen.h"

#include "midi_pattern_logic.h"
#include "mouth_capture.h"
#include "perform_logic.h"
#include "sample_rec_logic.h"
#include "sample_screen.h"
#include "song_screen.h"
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
constexpr const char* kKeyNames[ornoir::kFKeys] = {"PATTERN", "LANE", "LENGTH", "TEMPO", "VALUE", "ROW", "PLAYS", "SAVE"};
// The SHIFT row: F6, F7 and F8 (the others are blank and dim), labelled the way PERFORM's is. F8 CAPTURE is the MOUTH lane's (dim elsewhere).
constexpr const char* kShiftNames[ornoir::kFKeys] = {"", "", "", "", "", "CLR LANE", "FOLLOW", "CAPTURE"};
constexpr unsigned kKeyRow = 5;     // F6
constexpr unsigned kKeyPlays = 6;   // F7
constexpr unsigned kKeyCapture = 7; // F8 (SHIFT)
inline constexpr pads::Layout kDefaultLayout{};  // when no PERFORM is wired (a standalone screen)
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

unsigned round_param(const dsp::Engine& e, dsp::UnitId id, std::uint16_t p) noexcept {
  const float v = e.param(id, p);
  return v < 0.0f ? 0u : static_cast<unsigned>(v + 0.5f);
}

int centred(ui::Renderer& r, const char* s, Face f, int cx) noexcept { return cx - r.text_width(s, f) / 2; }

// What a step stores in a lane, as text for the cell (`line2` is a second, smaller line: a chord's kind). Empty for nothing.
void cell_text(pt::Lane lane, const Step& s, std::uint8_t mouth, char (&line1)[6], const char*& line2) noexcept {
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
    case pt::Lane::Mouth:
      pt::mouth_text(line1, mouth);
      break;
  }
}

// Does the lane hold something in this step? (the step pad's dim LED, the cell's ink)
bool has_content(pt::Lane lane, const Step& s, std::uint8_t mouth) noexcept {
  switch (lane) {
    case pt::Lane::Note:
      return s.note != 0u;
    case pt::Lane::Carrier:
      return s.carrier != 0u;
    case pt::Lane::Level:
      return s.level != 0u;
    case pt::Lane::Slice:
      return s.slice != 0u;
    case pt::Lane::Mouth:
      return mouth != 0u;
  }
  return false;
}

// HELP's tables, in the SDRAM (valid while HELP is open): the 48 pad labels.
struct PatternHelp {
  char name[app::kPadCount][app::kHelpPadLabel + 1];
};
O3SDK_BIG PatternHelp g_pd = {};

// COPY's clipboard (a step or a pattern), kept while the app runs; INSERT pastes it on any pattern.
O3SDK_BIG pt::Clip g_clip = {};

// CAPTURE's analysis (mouth_capture.h, about 5.2 KB), one at a time.
O3SDK_BIG mouthcap::Capture g_cap = {};
constexpr std::uint8_t kArmLane = 1;     // CLEAR LANE (SHIFT + F6)
constexpr std::uint8_t kArmPattern = 2;  // SHIFT + DELETE
constexpr std::uint8_t kArmFill = 3;     // F7 FILL on the MOUTH lane over a lane that holds something of the hands'
constexpr std::uint8_t kNoWord = 0xFF;
constexpr const char* kPageNames[2] = {"VOWELS", "WORDS"};

constexpr int kGridX = 124;
constexpr int kCellW = 40;
constexpr int kCellY = 120;
constexpr int kCellH = 48;
constexpr int kLaneStep = 52;  // five lanes (V3): 120 + 5 * 52 leaves the strip's line above the F row (y 419)

}  // namespace

void PatternScreen::init(dsp::UnitId voice, Bank* bank, StepSeq* seq, PerformScreen* perform, SaveFn save, void* user, SampleScreen* sample,
                         const std::int16_t* loop_mem) noexcept {
  voice_ = voice;
  sample_ = sample;
  loop_mem_ = loop_mem;
  g_cap.cancel();
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
  row_ = 0;
  flash_until_ = 0;
  page_ = 0;
  fill_count_ = 0;  // a fresh run: the first fill is fill #0 again
  fill_has_ = 0;
}

const ui::Theme* PatternScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void PatternScreen::enter(app::Context& ctx) noexcept {
  note_ = nullptr;
  armed_ = 0;
  wob_said_ = false;
  steps_held_ = 0;
  ctx.redraw();
}

void PatternScreen::leave(app::Context& ctx) noexcept {
  g_cap.cancel();  // a capture runs from this screen's update() only: leaving ends it, nothing written
  if (perform_ != nullptr) {
    perform_->audition_release_all(ctx);
  }
  for (unsigned pad = 0; pad < pt::kPalettePads && sample_ != nullptr; ++pad) {
    if (((aud_ >> pad) & 1u) != 0u) {
      sample_->release_slice(ctx, pad);  // a GATE slice ends with the pad
    }
  }
  aud_ = 0;
  if (mouth_aud_ != 0u && perform_ != nullptr) {
    perform_->mouth_sound_release(ctx);  // a closure still held ends with the screen
  }
  mouth_aud_ = 0;
  word_end(ctx);  // a word still sounding ends with the screen
  steps_held_ = 0;
}

void PatternScreen::audition(app::Context& ctx, unsigned pad, unsigned note, unsigned kind, std::uint8_t velocity, unsigned voicing) noexcept {
  if (perform_ == nullptr || pad >= pt::kPalettePads) {
    return;
  }
  perform_->audition_press(ctx, pad, note, kind, velocity, voicing);
  aud_ |= std::uint32_t{1} << pad;
}

// The LEVEL lane (Q5): the step's own note or chord, at the level just written (the sequencer's velocity rule). A rest sounds nothing.
void PatternScreen::audition_step(app::Context& ctx, unsigned pad, unsigned step) noexcept {
  const Step s = get_step(pattern_now(), step);
  if (s.note >= 1u) {
    audition(ctx, pad, s.note, s.kind, step_velocity(s), s.voicing);
  }
}

// The SLICE lane (Q5): the slice just written, fired on the SPEAKS slot as SAMPLE's pads fire it. The pad's release lets a GATE slice go.
void PatternScreen::audition_slice(app::Context& ctx, unsigned pad) noexcept {
  if (sample_ != nullptr && pad < pt::kPalettePads) {
    sample_->fire_slice(ctx, pad);
    aud_ |= std::uint32_t{1} << pad;
  }
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
  lane_chosen(ctx);
}

// The MOUTH lane speaks in TRACT only: outside it the lane is shown dim, still edits, and the strip says so once, when the lane is chosen.
void PatternScreen::lane_chosen(app::Context& ctx) noexcept {
  if (lane() == pt::Lane::Mouth && !tract_on(ctx)) {
    say(ctx, perform::kMouthElsewhere);
    return;
  }
  ctx.redraw();
}

bool PatternScreen::tract_on(const app::Context& ctx) const noexcept {
  return pl::mode_index(ctx.engine().param(voice_, Vocoder::Mode)) == kModeTract;
}

void PatternScreen::show_pattern(app::Context& ctx, unsigned p, bool by_hand) noexcept {
  if (g_cap.running() && (p & 7u) != cap_pattern_) {  // a capture writes the pattern it started on: a pattern change ends it, nothing written
    g_cap.cancel();
    say(ctx, "CAPTURE CANCELLED: THE PATTERN CHANGED");
  }
  shown_ = static_cast<std::uint8_t>(p & 7u);
  // A SONG chain that plays owns the request: it re-asks for each lap's pattern only when a lap ends, so a hand request here would play for one
  // lap against the chain (or keep the chain's last lap from ending it). The screen still shows and edits the pattern picked.
  const bool owned = by_hand && song_ != nullptr && song_->playing_chain();
  if (seq_ != nullptr && !owned) {
    seq_->request_pattern(shown_);  // it starts when the playing one ends (or at the next Play)
  }
  if (cursor_ >= length_of(pattern_now())) {
    cursor_ = 0;
  }
  ctx.redraw();
}

void PatternScreen::record_midi(app::Context& ctx, const Vocoder::NoteIn& n) noexcept {
  if (bank_ == nullptr || n.note < 1u || n.note > 127u) {
    return;
  }
  const StepSeq::Position now = seq_ != nullptr ? seq_->position() : StepSeq::Position{};
  const midipat::Target t = midipat::target_of(n.playing, n.sixteenth, now, length_of(bank_->pattern[now.pattern & 7u]));
  PatternData& p = t.at_cursor ? pattern_now() : bank_->pattern[t.pattern & 7u];
  const unsigned at = t.at_cursor ? (cursor_ < length_of(p) ? cursor_ : 0u) : t.step;
  if (!set_note(p, at, n.note) || !set_level(p, at, midipat::level_of_velocity(n.velocity))) {
    return;
  }
  if (t.at_cursor) {
    cursor_ = static_cast<std::uint8_t>(pt::next_cursor(at, length_of(p)));
  } else if ((t.pattern & 7u) == shown_) {
    cursor_ = static_cast<std::uint8_t>(at);  // the cursor follows what the take writes
  }
  ctx.redraw();
}

// A palette pad writes into the cursor's step, and the cursor moves on. With step pads held (Q4) it writes every held step instead and the cursor stays.
// On the NOTE lane the pad writes what PERFORM's row A or B plays there (a note, or a chord of PATTERN's voicing); a pad that plays nothing (OFF, a piano
// gap, out of range) writes nothing and sounds nothing. The pad sounds while it is held (the audition, Q3); SHIFT + a pad sounds and writes nothing.
void PatternScreen::write_palette(app::Context& ctx, unsigned pad, bool shift) noexcept {
  PatternData& p = pattern_now();
  const unsigned len = length_of(p);
  std::uint32_t targets = steps_held_ & ((std::uint32_t{1} << len) - 1u);  // a held step past LENGTH (it was cut short meanwhile) is not written
  const bool held = targets != 0u;
  unsigned heard = cursor_ < len ? cursor_ : 0u;  // the step the LEVEL audition plays: the cursor's, or the first held one when the cursor's was let go
  if (!held) {
    targets = std::uint32_t{1} << heard;
  } else if (((targets >> heard) & 1u) == 0u) {
    for (heard = 0; ((targets >> heard) & 1u) == 0u; ++heard) {
    }
  }
  pads::PadPlay pp;
  if (lane() == pt::Lane::Note) {
    pp = pt::palette_play(pad, perform_ != nullptr ? perform_->octave() : 0, layout_now());
    if (pp.note < 1) {
      return;
    }
    audition(ctx, pad, static_cast<unsigned>(pp.note), pp.kind);
    if (shift) {
      ctx.redraw();
      return;
    }
  }
  if (lane() == pt::Lane::Mouth) {  // a dark pad (row 1 past the nine vowels) writes nothing and sounds nothing
    const std::uint8_t m = pt::mouth_of_pad(pad, mouth_page());
    if (m == 0u) {
      return;
    }
    if (mouth_is_wob(m)) {  // a WOB wobbles the line between its neighbours: nothing to hear from a pad
      if (!wob_said_) {
        wob_said_ = true;
        say(ctx, pt::kWobPlays);
      }
    } else if (mouth_is_word(m)) {  // a word plays once, from update()
      word_start(ctx, mouth_arg(m));
    } else if (perform_ != nullptr && perform_->mouth_active(ctx.engine())) {  // it sounds while held: a vowel sets VOWEL, a closure holds CLOSE
      perform_->mouth_sound(ctx, m);
      aud_ |= std::uint32_t{1} << pad;
      if (mouth_is_closure(m)) {
        mouth_aud_ |= std::uint32_t{1} << pad;
      }
    }
    if (shift) {
      ctx.redraw();
      return;
    }
  }
  bool ok = false;
  for (unsigned i = 0; i < len; ++i) {
    if (((targets >> i) & 1u) == 0u) {
      continue;
    }
    switch (lane()) {
      case pt::Lane::Note: {
        const unsigned note = static_cast<unsigned>(pp.note);
        ok |= pp.kind == 0u ? set_note(p, i, note)
                            : set_chord(p, i, note, static_cast<ChordKind>(pp.kind - 1u), static_cast<Voicing>(perform_ != nullptr ? perform_->voicing() : 3u));
        break;
      }
      case pt::Lane::Carrier:
        ok |= pad < pt::kCarrierPads && set_carrier(p, i, pad);
        break;
      case pt::Lane::Level:
        ok |= set_level(p, i, pt::level_of_pad(pad));
        break;
      case pt::Lane::Slice: {
        const pt::SliceWrite w = pt::slice_write(p, i, pad, seq_ != nullptr ? seq_->slice_count() : 0u);
        if (w == pt::SliceWrite::NoSlices) {
          say(ctx, "SPEAKS HAS NO SLICES");
          return;
        }
        ok |= w == pt::SliceWrite::Written;
        break;
      }
      case pt::Lane::Mouth:
        ok |= pt::mouth_write(p, i, pad, mouth_page());
        break;
    }
  }
  if (ok && lane() == pt::Lane::Level) {
    audition_step(ctx, pad, heard);
  } else if (ok && lane() == pt::Lane::Slice) {
    audition_slice(ctx, pad);
  }
  if (ok && !held) {
    cursor_ = static_cast<std::uint8_t>(pt::next_cursor(cursor_, len));
  }
  ctx.redraw();
}

const pads::Layout& PatternScreen::layout_now() const noexcept { return perform_ != nullptr ? perform_->layout() : kDefaultLayout; }

void PatternScreen::pad(app::Context& ctx, unsigned pad, bool shift) noexcept {
  if (pad < pt::kPalettePads) {
    write_palette(ctx, pad, shift);
    return;
  }
  const int step = pt::step_of_pad(pad);
  if (step >= static_cast<int>(length_of(pattern_now()))) {
    return;  // a step past LENGTH is dark: the cursor stays on the pattern's steps
  }
  if (step >= 0) {
    cursor_ = static_cast<std::uint8_t>(step);
    steps_held_ |= static_cast<std::uint16_t>(1u << step);
    if (shift) {
      (void)pt::rest_lane(pattern_now(), cursor_, lane());
    }
    ctx.redraw();
    return;
  }
  const int pat = pt::pattern_of_pad(pad);
  if (pat >= 0) {
    show_pattern(ctx, static_cast<unsigned>(pat), true);
  }
}

// `delta` is the encoder's detents, or +1 for a second tap on the key; a tap wraps where a detent stops at the end.
void PatternScreen::turn(app::Context& ctx, unsigned key, int delta) noexcept {
  const bool tap = delta == 1 && key_tap_;
  PatternData& p = pattern_now();
  switch (key) {
    case 0: {  // PATTERN
      const int n = static_cast<int>(shown_) + delta;
      show_pattern(ctx, tap ? (shown_ + 1u) & 7u : static_cast<unsigned>(n < 0 ? 0 : (n > 7 ? 7 : n)), true);
      break;
    }
    case 1: {  // LANE
      const int n = static_cast<int>(lane_) + delta;
      const std::uint8_t before = lane_;
      lane_ = static_cast<std::uint8_t>(tap ? (lane_ + 1u) % pt::kLanes : (n < 0 ? 0 : (n >= static_cast<int>(pt::kLanes) ? pt::kLanes - 1u : static_cast<unsigned>(n))));
      if (lane_ != before) {
        lane_chosen(ctx);
        return;
      }
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
    case kKeyRow:  // ROW: A or B, the row F7 sets (a tap flips, a detent stops at the ends)
    case kKeyPlays: {  // PLAYS: the chosen row's value, written through PERFORM's layout
      if (lane() != pt::Lane::Note || perform_ == nullptr) {
        return;
      }
      if (key == kKeyRow) {
        const int n = static_cast<int>(row_) + delta;
        const unsigned next = tap ? row_ ^ 1u : (n < 0 ? 0u : (n > 1 ? 1u : static_cast<unsigned>(n)));
        if (next != row_) {
          row_ = static_cast<std::uint8_t>(next);
          flash_until_ = ctx.uptime_ms() + pads::kRowFlashMs;
        }
      } else {
        const pads::Row r = static_cast<pads::Row>(row_);
        const pads::Plays cur = perform_->layout().row[row_];
        (void)perform_->set_plays(r, tap ? pads::plays_tap(r, cur) : pads::plays_step(r, cur, delta));
      }
      break;
    }
    default:
      return;
  }
  ctx.redraw();
}

void PatternScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  unsigned k = 0;
  if (armed_ != 0u && in.type != app::Input::Type::Release) {  // CLEAR LANE or SHIFT + DELETE asked: F1 clears, anything else cancels and is spent
    const std::uint8_t what = armed_;
    armed_ = 0;
    if (in.fkey_press(k) && k == 0u && !in.shift) {
      PatternData& p = pattern_now();
      if (what == kArmPattern) {
        pt::clear_steps(p);
        say(ctx, "PATTERN EMPTIED");
        return;
      }
      if (what == kArmFill) {
        fill_now(ctx);
        return;
      }
      for (unsigned i = 0; i < kPatternSteps; ++i) {
        (void)pt::rest_lane(p, i, lane());
      }
      say(ctx, "LANE CLEARED");
    } else {
      say(ctx, what == kArmFill ? "NOT FILLED" : "NOT CLEARED");
    }
    return;
  }
  if (in.fkey_press(k)) {
    if (in.shift) {
      if (k == kKeyRow) {  // SHIFT + F6, CLEAR LANE: asks first (F1 confirms, any other key cancels), on the pattern shown
        armed_ = kArmLane;
        ctx.redraw();
      } else if (k == kKeyPlays) {  // SHIFT + F7, FOLLOW
        follow_ = !follow_;
        ctx.redraw();
      } else if (k == kKeyCapture) {  // SHIFT + F8, CAPTURE (the MOUTH lane's; a second press while it runs cancels)
        capture_press(ctx);
      }
      return;
    }
    if (k == 7u) {  // SAVE
      say(ctx, save_ != nullptr && save_(save_user_, ctx) ? "SAVING" : "CARD BUSY");
      return;
    }
    if ((k == kKeyRow || k == kKeyPlays) && lane() == pt::Lane::Mouth) {  // SET and FILL: actions of the key, no focus for the encoder
      if (k == kKeyRow) {
        page_ ^= 1u;
        ctx.redraw();
      } else {
        fill_press(ctx);
      }
      return;
    }
    if ((k == kKeyRow || k == kKeyPlays) && lane() != pt::Lane::Note) {
      return;  // ROW and PLAYS are the NOTE lane's palette (dim elsewhere)
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
  if (in.press(Button::Up) || in.press(Button::Down)) {  // the octave PERFORM and SAMPLE share (the palette's notes move by 12)
    if (perform_ != nullptr) {
      perform_->step_octave(ctx, in.press(Button::Up) ? 1 : -1);
      const int o = perform_->octave();
      std::size_t n = 0;
      static constexpr char kHead[] = "OCTAVE ";
      for (; kHead[n] != '\0'; ++n) {
        oct_say_[n] = kHead[n];
      }
      oct_say_[n++] = o < 0 ? '-' : '+';
      oct_say_[n++] = static_cast<char>('0' + (o < 0 ? -o : o));
      oct_say_[n] = '\0';
      say(ctx, oct_say_);
    }
  } else if (in.press(Button::Note)) {
    set_lane(ctx, pt::Lane::Note);
  } else if (in.press(Button::Instr)) {
    set_lane(ctx, pt::Lane::Carrier);
  } else if (in.press(Button::Vol)) {
    set_lane(ctx, pt::Lane::Level);
  } else if (in.press(Button::Fx)) {  // SLICE, then MOUTH, then back to SLICE
    set_lane(ctx, lane() == pt::Lane::Slice ? pt::Lane::Mouth : pt::Lane::Slice);
  } else if (in.press(Button::Left) || in.press(Button::Right)) {  // repeats arrive as presses (repeat_buttons)
    const unsigned len = length_of(pattern_now());
    const unsigned c = cursor_;
    cursor_ = static_cast<std::uint8_t>(in.press(Button::Right) ? (c + 1u) % len : (c == 0u || c > len ? len - 1u : c - 1u));
    ctx.redraw();
  } else if (in.press(Button::Copy)) {  // the cursor's step, every lane (SHIFT: the whole pattern)
    if (in.shift) {
      pt::copy_whole(g_clip, pattern_now());
    } else {
      pt::copy_step(g_clip, pattern_now(), cursor_);
    }
    say(ctx, in.shift ? "PATTERN COPIED" : "STEP COPIED");
  } else if (in.press(Button::Insert)) {  // the copied step at the cursor, the rest one step right (SHIFT: the copied pattern over this one)
    PatternData& p = pattern_now();
    const bool ok = in.shift ? pt::paste_whole(p, g_clip) : pt::insert_step(p, cursor_, g_clip);
    const unsigned len = length_of(p);
    cursor_ = static_cast<std::uint8_t>(cursor_ < len ? cursor_ : len - 1u);  // a pasted pattern can be shorter
    say(ctx, ok ? (in.shift ? "PATTERN PASTED" : "STEP INSERTED") : (in.shift ? "COPY A PATTERN FIRST (SHIFT + COPY)" : "COPY A STEP FIRST"));
  } else if (in.press(Button::Delete)) {  // the cursor's step empty in every lane (SHIFT: the whole pattern, once F1 confirms)
    if (in.shift) {
      armed_ = kArmPattern;
    } else {
      (void)pt::clear_step(pattern_now(), cursor_);
    }
    ctx.redraw();
  } else if (in.pad_press()) {
    pad(ctx, in.pad(), in.shift);
  } else if (in.pad_release() && pt::step_of_pad(in.pad()) >= 0) {
    steps_held_ &= static_cast<std::uint16_t>(~(1u << pt::step_of_pad(in.pad())));
  } else if (in.pad_release() && in.pad() < pt::kPalettePads && ((aud_ >> in.pad()) & 1u) != 0u) {
    aud_ &= ~(std::uint32_t{1} << in.pad());
    if (perform_ != nullptr) {
      perform_->audition_release(ctx, in.pad());
      if (((mouth_aud_ >> in.pad()) & 1u) != 0u) {
        mouth_aud_ &= ~(std::uint32_t{1} << in.pad());
        perform_->mouth_sound_release(ctx);
      }
    }
    if (sample_ != nullptr) {
      sample_->release_slice(ctx, in.pad());
    }
    ctx.redraw();
  }
}

// ---- the MOUTH lane's F6 / F7 (V3b) ----------------------------------------------------------------------------------------------------

// F7 FILL. A lane that is empty, or that is the last fill's own output on this pattern, is filled at once (F7 again re-rolls); anything else asks first.
void PatternScreen::fill_press(app::Context& ctx) noexcept {
  const PatternData& p = pattern_now();
  const unsigned pi = shown_ & 7u;
  const bool own = ((fill_has_ >> pi) & 1u) != 0u && pt::mouth_lane_is(p, fill_out_[pi]);
  if (!own && !pt::mouth_lane_empty(p)) {
    armed_ = kArmFill;
    ctx.redraw();
    return;
  }
  fill_now(ctx);
}

// The seed is the screen's count of fills plus the pattern's index (pt::fill_seed): the same pattern's next fill differs, and a fresh run of the app
// starts the count again, so the first fill of a pattern is the same on every boot. Steps 0..LENGTH-1 only; the 16 bytes left are kept as "own output".
void PatternScreen::fill_now(app::Context& ctx) noexcept {
  PatternData& p = pattern_now();
  const unsigned pi = shown_ & 7u;
  pt::fill_mouth(p, length_of(p), pt::fill_seed(fill_count_++, pi));
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    fill_out_[pi][i] = get_mouth(p, i);
  }
  fill_has_ = static_cast<std::uint8_t>(fill_has_ | (1u << pi));
  say(ctx, pt::kFilled);
}

// The WORD audition: the word's points are played from the main loop against uptime_ms, a point when its tick (of the sixteenth's 240) at the tempo has
// passed. update() runs once a frame, so a point lands within a frame (about 16 ms) of its time: enough to hear a word, which the sequencer plays to the
// frame on the audio side. Nothing outside TRACT.
void PatternScreen::word_start(app::Context& ctx, unsigned word) noexcept {
  if (perform_ == nullptr || !perform_->mouth_active(ctx.engine()) || word >= kMouthWords) {
    return;
  }
  if (word_ != kNoWord) {
    word_end(ctx);
  }
  word_ = static_cast<std::uint8_t>(word);
  word_next_ = 0;
  word_t0_ = ctx.uptime_ms();
  word_pump(ctx);
}

void PatternScreen::word_pump(app::Context& ctx) noexcept {
  if (word_ == kNoWord || perform_ == nullptr) {
    return;
  }
  const std::uint32_t mbpm = ctx.engine().own_tempo();
  const float step_ms = mbpm != 0u ? 15000000.0f / static_cast<float>(mbpm) : 2000.0f;  // a sixteenth
  const float elapsed = static_cast<float>(ctx.uptime_ms() - word_t0_);
  const Word& w = kWords[word_];
  while (word_next_ < w.count && elapsed * static_cast<float>(dsp::kTicksPerSixteenth) >= static_cast<float>(w.pt[word_next_].tick) * step_ms) {
    perform_->mouth_word_point(ctx, step_ms, w.pt[word_next_]);
    ++word_next_;
  }
  if (word_next_ >= w.count && elapsed >= step_ms) {  // every word ends inside its sixteenth
    word_end(ctx);
  }
}

void PatternScreen::word_end(app::Context& ctx) noexcept {
  if (word_ != kNoWord && perform_ != nullptr) {
    perform_->mouth_word_end(ctx);
  }
  word_ = kNoWord;
}

// Starts the analysis of `slot` for `pattern`: nullptr when it runs, else why not. The slot must be recorded or loaded (not empty, not recording) and at
// least mouthcap::kMinLength long; a slot of bars gives 16 steps a bar, any other the pattern's length over the whole slot (mouthcap::plan).
const char* PatternScreen::capture_begin(app::Context& ctx, unsigned pattern, unsigned slot) noexcept {
  if (sample_ == nullptr || loop_mem_ == nullptr) {
    return "NO SLOT TO READ";
  }
  slot &= 3u;
  pattern &= 7u;
  const Vocoder::LoopStatus st = sample_->status(slot);
  if (st.rec != Vocoder::LoopStatus::Rec::Idle) {
    return "THE SLOT IS RECORDING: WAIT FOR IT TO END";
  }
  if (st.length == 0u) {
    return "SLOT IS EMPTY: RECORD OR LOAD ONE IN SAMPLE";
  }
  const mouthcap::Plan pn = mouthcap::plan(st.length, pl::bar_samples(ctx.engine().stats().tempo_mbpm), length_of(bank_->pattern[pattern]));
  const std::int16_t* mem = loop_mem_ + static_cast<std::size_t>(slot) * Loop::kMaxSamples;
  if (!g_cap.begin(mem, st.length, pn.total, pn.used, static_cast<float>(sample_rec::kSampleRate))) {
    return "SLOT IS TOO SHORT TO READ";
  }
  cap_pattern_ = static_cast<std::uint8_t>(pattern);
  cap_slot_ = static_cast<std::uint8_t>(slot);
  cap_length_ = st.length;
  return nullptr;
}

// The slot a running capture reads is still what it was at the start: not recording, the same length (a new take or a load changes it).
bool PatternScreen::capture_slot_same() const noexcept {
  if (sample_ == nullptr) {
    return false;
  }
  const Vocoder::LoopStatus st = sample_->status(cap_slot_);
  return mouthcap::slot_same(st.rec == Vocoder::LoopStatus::Rec::Idle, st.length, cap_length_);
}

// SHIFT + F8 on the MOUTH lane: a first press starts the analysis of the slot SAMPLE has selected, a second press while it runs cancels. Nothing is
// written until the analysis ends (capture_finish).
void PatternScreen::capture_press(app::Context& ctx) noexcept {
  if (g_cap.running()) {
    g_cap.cancel();
    say(ctx, "CAPTURE CANCELLED");
    return;
  }
  if (lane() != pt::Lane::Mouth) {
    say(ctx, "CAPTURE IS THE MOUTH LANE'S: PRESS F2 TO SELECT IT");
    return;
  }
  const char* why = capture_begin(ctx, shown_, sample_ != nullptr ? sample_->slot() : 0u);
  if (why != nullptr) {
    say(ctx, why);
  }
  ctx.redraw();
}

// The console's `mcap`: the same analysis run to the end in this call (about 50 ms [est] on the unit, the audio runs on its own interrupt), written to
// `pattern`'s MOUTH lane. nullptr when written, else why not. Refused while PATTERN's own capture runs (they share the one Capture).
const char* PatternScreen::capture_now(app::Context& ctx, unsigned pattern, unsigned slot) noexcept {
  if (g_cap.running()) {
    return "A CAPTURE IS RUNNING ON THE PATTERN SCREEN";
  }
  const char* why = capture_begin(ctx, pattern, slot);
  if (why != nullptr) {
    return why;
  }
  while (g_cap.step()) {
  }
  const char* res = nullptr;
  if (!capture_slot_same()) {  // an armed take can start from the audio side while this runs
    res = "SLOT CHANGED: NOTHING WRITTEN";
  } else if (g_cap.silent()) {
    res = "SLOT IS SILENT: NOTHING WRITTEN";
  } else if (!mouthcap::apply(g_cap, bank_->pattern[cap_pattern_ & 7u])) {
    res = "NOTHING WRITTEN";
  }
  g_cap.cancel();
  ctx.redraw();
  return res;
}

// One piece of the analysis a pass (a reference, a level chunk or a step: well under a millisecond each but the references, 4 ms [est]).
void PatternScreen::capture_pump(app::Context& ctx) noexcept {
  if (!g_cap.running()) {
    return;
  }
  if (!capture_slot_same()) {  // recorded over or reloaded since the start: the lane stays as it was
    g_cap.cancel();
    say(ctx, "SLOT CHANGED");
    ctx.redraw();
    return;
  }
  (void)g_cap.step();
  if (g_cap.done()) {
    capture_finish(ctx);
  }
}

void PatternScreen::capture_finish(app::Context& ctx) noexcept {
  if (!capture_slot_same()) {  // the slot moved during the last pass
    g_cap.cancel();
    say(ctx, "SLOT CHANGED");
    ctx.redraw();
    return;
  }
  if (g_cap.silent()) {
    say(ctx, "SLOT IS SILENT: NOTHING WRITTEN");
  } else if (mouthcap::apply(g_cap, bank_->pattern[cap_pattern_ & 7u])) {
    say(ctx, "MOUTH LANE CAPTURED FROM THE SLOT");
  } else {
    say(ctx, "NOTHING WRITTEN");
  }
  g_cap.cancel();
  ctx.redraw();
}

void PatternScreen::update(app::Context& ctx) noexcept {
  word_pump(ctx);
  capture_pump(ctx);
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

// Rows 0 and 1: the lane's palette (mid where it holds what the cursor's step stores); rows 2 and 3: the steps and the patterns. On the NOTE lane the
// palette is PERFORM's rows A and B as they play now: the key's root low, the scale dim, the rest and the pads that play nothing dark; F6's row goes off for
// a moment when it changes.
void PatternScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  const PatternData& p = pattern_now();
  const unsigned len = length_of(p);
  const Step cur = get_step(p, cursor_);
  const std::uint8_t curm = get_mouth(p, cursor_);
  const int oct = perform_ != nullptr ? perform_->octave() : 0;
  const pads::Layout& layout = layout_now();
  const unsigned slices = seq_ != nullptr ? seq_->slice_count() : 0u;
  const dsp::Engine& e = ctx.engine();
  const unsigned key = round_param(e, voice_, Vocoder::Key) % 12u;
  const std::uint16_t cols = pl::scale_cols(key, round_param(e, voice_, Vocoder::Scale));
  const bool flash = lane() == pt::Lane::Note && ctx.uptime_ms() < flash_until_;
  for (unsigned pad = 0; pad < pt::kPalettePads; ++pad) {
    std::uint8_t level = app::kLedDim;
    if (lane() == pt::Lane::Note) {
      const pads::PadPlay pp = pt::palette_play(pad, oct, layout);
      level = app::kLedOff;
      if (pp.note > 0 && !(flash && app::pad_row(pad) % 2u == row_)) {
        const unsigned pc = static_cast<unsigned>(pp.note) % 12u;
        level = pc == key ? app::kLedLow : (((cols >> pc) & 1u) != 0u ? app::kLedDim : app::kLedOff);
        if (pt::palette_matches(lane(), pad, cur, oct, layout)) {
          level = app::kLedMid;
        }
      }
    } else if (lane() == pt::Lane::Slice) {  // the SPEAKS slot's slices: dim up to its count, mid on the cursor step's
      level = pad < slices ? (pt::palette_matches(lane(), pad, cur, oct, layout) ? app::kLedMid : app::kLedDim) : app::kLedOff;
    } else if (lane() == pt::Lane::Carrier && pad >= pt::kCarrierPads) {
      level = app::kLedOff;
    } else if (lane() == pt::Lane::Mouth) {  // the strip and its GLD row; the dark pads off
      level = pt::mouth_of_pad(pad, mouth_page()) == 0u ? app::kLedOff
                                                           : (pt::palette_matches(lane(), pad, cur, oct, layout, curm, mouth_page()) ? app::kLedMid : app::kLedDim);
    } else if (pt::palette_matches(lane(), pad, cur, oct, layout)) {
      level = app::kLedMid;
    }
    if (((aud_ >> pad) & 1u) != 0u) {
      level = app::kLedOn;  // held and sounding, as PERFORM's held pads
    }
    leds.set(pad, level);
  }
  const bool playing = seq_ != nullptr && ctx.engine().playing() && seq_->playing_pattern() == shown_;
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    std::uint8_t level = app::kLedOff;
    if (i < len) {
      level = playing && seq_->playing_step() == i ? app::kLedOn : (i == cursor_ ? app::kLedMid : (has_content(lane(), get_step(p, i), get_mouth(p, i)) ? app::kLedDim : app::kLedOff));
    }
    leds.set(pt::pad_of_step(i), level);
  }
  const unsigned running = seq_ != nullptr && ctx.engine().playing() ? seq_->playing_pattern() : kBankPatterns;
  for (unsigned q = 0; q < kBankPatterns; ++q) {
    leds.set(pt::pad_of_pattern(q), q == running ? app::kLedOn : (q == shown_ ? app::kLedMid : app::kLedDim));
  }
}

void PatternScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    out.label[i] = kKeyNames[i];
  }
  if (lane() == pt::Lane::Mouth) {  // F6 SET shows its page, F7 is FILL; both dim outside TRACT, where the lane does not speak
    out.label[kKeyRow] = kPageNames[page_ & 1u];
    out.label[kKeyPlays] = "FILL";
    out.dim = tract_on(ctx) ? std::uint8_t{0} : static_cast<std::uint8_t>((1u << kKeyRow) | (1u << kKeyPlays));
  } else if (lane() != pt::Lane::Note) {  // ROW and PLAYS are the NOTE lane's
    out.dim = static_cast<std::uint8_t>((1u << kKeyRow) | (1u << kKeyPlays));
  }
}

// HELP: every button input() answers has a row (tests/pattern_screen_test.cpp holds it), the pads in three groups.
void PatternScreen::help(const app::Context&, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Up, "OCTAVE UP", nullptr},
      {Button::Down, "OCTAVE DOWN", nullptr},
      {Button::Left, "CURSOR BACK, HOLD REPEATS", nullptr},
      {Button::Right, "CURSOR ON, HOLD REPEATS", nullptr},
      {Button::Note, "NOTE LANE", nullptr},
      {Button::Instr, "CARRIER LANE", nullptr},
      {Button::Vol, "LEVEL LANE", nullptr},
      {Button::Fx, "SLICE LANE, THEN MOUTH LANE", nullptr},
      {Button::Shift, "SHIFT + STEP: A REST", nullptr},
      {Button::Copy, "COPY THE STEP", "COPY THE PATTERN"},
      {Button::Insert, "INSERT THE COPIED STEP", "PASTE THE COPIED PATTERN"},
      {Button::Delete, "EMPTY THE STEP", "EMPTY THE PATTERN (F1)"},
      {Button::Params, "PARAMS: FOUR PAGES", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER", nullptr},
      {Button::File, "FILE", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD", nullptr},
      {Button::SamplePlay, "SAMPLE: THE SLOTS", nullptr},
      {Button::SampleEdit, "SAMPLE EDIT: THE CUTS", nullptr},
      {Button::SampleRec, "SAMPLE REC: A TAKE", nullptr},
      {Button::Song, "SONG", nullptr},
      {Button::Pattern, "THIS SCREEN", nullptr},
  };
  static constexpr app::HelpPadGroup kGroups[] = {
      {"PALETTE", "WRITES THE CURSOR'S STEP, OR EVERY HELD STEP", Color::Accent},
      {"STEPS", "MOVES THE CURSOR; SHIFT: A REST IN THE LANE", Color::Play},
      {"PATTERNS", "A TO H: SHOW IT, PLAY IT NEXT", Color::Sel},
  };
  // The NOTE lane's palette says what its pads write: PERFORM's rows A and B as the layout has them now.
  static constexpr app::HelpPadGroup kNoteGroups[] = {
      {"NOTES", "WHAT PERFORM'S ROWS A AND B PLAY (F6, F7)", Color::Accent},
      kGroups[1],
      kGroups[2],
  };
  const int oct = perform_ != nullptr ? perform_->octave() : 0;
  const pads::Layout& layout = layout_now();
  h.summary = "THE 16 STEPS OF A PATTERN AND THEIR LANES. THE UPPER PADS WRITE THE LIT LANE.\n"
              "SHIFT + F6: CLEAR LANE (F1 CONFIRMS). SHIFT + F7: FOLLOW. SHIFT + F8: CAPTURE (MOUTH).";
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    h.fkey[i] = nullptr;
    h.fkey_shift[i] = nullptr;
  }
  h.fkey[0] = "A TO H";
  h.fkey[1] = "NOTE TO MOUTH, FIVE LANES";
  h.fkey[2] = "1 TO 16 STEPS";
  h.fkey[3] = "THE BAR'S BPM";
  h.fkey[4] = "THE CURSOR STEP'S VALUE";
  const bool note_lane = lane() == pt::Lane::Note;
  const bool mouth_lane = lane() == pt::Lane::Mouth;
  h.fkey[5] = note_lane ? "PAD ROW A OR B" : (mouth_lane ? "SET: VOWELS OR WORDS PALETTE" : "NOTE LANE ONLY");
  h.fkey[6] = note_lane ? "WHAT THE ROW PLAYS" : (mouth_lane ? "FILL: A RANDOM VOWEL LINE (F1 IF IT HOLDS STEPS)" : "NOTE LANE ONLY");
  h.fkey[7] = "SAVES THE PATTERNS";
  h.fkey_shift[5] = kShiftNames[5];
  h.fkey_shift[6] = kShiftNames[6];
  h.fkey_shift[7] = kShiftNames[7];
  for (unsigned i = 0; i < pt::kPalettePads; ++i) {
    char* l = g_pd.name[i];
    l[0] = '\0';
    switch (lane()) {
      case pt::Lane::Note: {
        const pads::PadPlay pp = pt::palette_play(i, oct, layout);
        if (pp.note > 0) {  // the root and the kind after it: "C", "C7TH", "C#PWR"
          cat(l, sizeof g_pd.name[i], pt::kPitchClass[static_cast<unsigned>(pp.note) % 12u]);
          if (pp.kind != 0u) {
            cat(l, sizeof g_pd.name[i], pt::kCellKind[(pp.kind - 1u) & 3u]);
          }
        }
        break;
      }
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
      case pt::Lane::Mouth: {  // the strip: "A", "AE" .. "U", "LIP", "TIP", "BCK"; row 1 the same with ">" (GLD); two pads dark
        char t[6];
        pt::mouth_text(t, pt::mouth_of_pad(i, mouth_page()));
        cat(l, sizeof g_pd.name[i], t);
        break;
      }
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
  h.groups = lane() != pt::Lane::Note ? kGroups : kNoteGroups;
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
  const unsigned mode = pl::mode_index(e.param(voice_, Vocoder::Mode));
  const unsigned tempo = e.stats().tempo_mbpm;
  const PatternData& p = pattern_now();
  const unsigned len = length_of(p);
  const bool playing = seq_ != nullptr && e.playing() && seq_->playing_pattern() == shown_;
  const unsigned head = playing ? seq_->playing_step() : kPatternSteps;
  const Step cur = get_step(p, cursor_);
  const std::uint8_t curm = get_mouth(p, cursor_);
  const bool mouth_dim = !tract_on(ctx);  // the MOUTH lane speaks in TRACT only: shown dim elsewhere

  ornoir::HeaderState h;
  header_route(e, voice_, src != nullptr ? src->label : "NONE", src != nullptr && src->source == hal::capture::Source::Usb, h);
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
  cat(keys[1].value, sizeof keys[1].value, pt::kLaneNames[lane_ % pt::kLanes]);
  cat_uint(keys[2].value, sizeof keys[2].value, len);
  cat_uint(keys[3].value, sizeof keys[3].value, e.own_tempo() / 1000u);
  {
    char one[6];
    const char* two = "";
    cell_text(lane(), cur, curm, one, two);
    cat(keys[4].value, sizeof keys[4].value, one[0] != '\0' ? one : "-");
  }
  {
    const bool note_lane = lane() == pt::Lane::Note;
    const unsigned row = row_ & 1u;
    cat(keys[kKeyRow].value, sizeof keys[kKeyRow].value, pads::kRowNames[row]);
    cat(keys[kKeyPlays].value, sizeof keys[kKeyPlays].value, pads::kPlaysShort[pads::idx(layout_now().row[row])]);
    keys[kKeyRow].dim = keys[kKeyPlays].dim = !note_lane;  // the NOTE lane's palette; dim elsewhere
    keys[kKeyRow].hot = keys[kKeyRow].hot && note_lane;
    keys[kKeyPlays].hot = keys[kKeyPlays].hot && note_lane;
    if (lane() == pt::Lane::Mouth) {  // SET (the palette's page) and FILL: actions, no encoder focus
      keys[kKeyRow].name = "SET";
      keys[kKeyRow].value[0] = '\0';
      cat(keys[kKeyRow].value, sizeof keys[kKeyRow].value, kPageNames[page_ & 1u]);
      keys[kKeyPlays].name = "FILL";
      keys[kKeyPlays].value[0] = '\0';
      keys[kKeyRow].dim = keys[kKeyPlays].dim = mouth_dim;
    }
  }
  if (ctx.panel().shift()) {  // the SHIFT row replaces the plain one (names and values)
    for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
      keys[i] = ornoir::FKey{};
      keys[i].name = kShiftNames[i];
      keys[i].dim = i != kKeyRow && i != kKeyPlays && (i != kKeyCapture || lane() != pt::Lane::Mouth);
    }
    if (g_cap.running()) {
      keys[kKeyCapture].name = "CANCEL";
    }
    cat(keys[kKeyPlays].value, sizeof keys[kKeyPlays].value, follow_ ? "ON" : "OFF");
  }

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
    ornoir::fill_rect(c, x, kCellY, kCellW - 2, static_cast<int>(pt::kLanes) * kLaneStep - 4, i == cursor_ ? Color::Accent : (i == head ? Color::Play : Color::Empty));
    for (unsigned l = 0; l < pt::kLanes; ++l) {
      ornoir::fill_rect(c, x + 2, kCellY + static_cast<int>(l) * kLaneStep + 2, kCellW - 6, kCellH - 4, Color::Pane);
    }
  }
  for (unsigned l = 0; l < pt::kLanes; ++l) {  // the lane in force: a bar at the left of its row
    if (l == lane_) {
      ornoir::fill_rect(c, 12, kCellY + static_cast<int>(l) * kLaneStep + 2, 6, kCellH - 4,
                        l == static_cast<unsigned>(pt::Lane::Mouth) && mouth_dim ? Color::TextDim : Color::Accent);
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
    const bool dim = l == static_cast<unsigned>(pt::Lane::Mouth) && mouth_dim;
    r.text(24, y + 16, pt::kLaneNames[l], l == lane_ && !dim ? Color::Text : Color::TextDim, Face::Label);
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
      const std::uint8_t sm = get_mouth(p, i);
      cell_text(static_cast<pt::Lane>(l), s, sm, t1, t2);
      const int y = kCellY + static_cast<int>(l) * kLaneStep;
      const int cx = x + kCellW / 2 - 1;
      const bool content = has_content(static_cast<pt::Lane>(l), s, sm);
      const bool dim = static_cast<pt::Lane>(l) == pt::Lane::Mouth && mouth_dim;
      const Color ink = content ? (l == lane_ && !dim ? Color::Text : Color::TextDim) : Color::Empty;
      const bool shown_text = t1[0] != '\0' && (content || static_cast<pt::Lane>(l) == pt::Lane::Level);
      if (shown_text) {
        r.text(centred(r, t1, Face::Label, cx), y + (t2[0] != '\0' ? 6 : 16), t1, ink == Color::Empty ? Color::TextDim : ink, Face::Label);
      }
      if (t2[0] != '\0') {
        r.text(centred(r, t2, Face::Micro, cx), y + 30, t2, Color::TextDim, Face::Micro);
      }
    }
  }
  if (g_cap.running()) {
    char cap[48];
    cap[0] = '\0';
    cat(cap, sizeof cap, g_cap.phase() == mouthcap::Capture::Phase::Refs ? "PREPARING THE VOWELS " : "READING THE SLOT ");
    cat_uint(cap, sizeof cap, g_cap.percent());
    cat(cap, sizeof cap, "%   SHIFT + F8 CANCELS");
    r.text(kGridX, kCellY + static_cast<int>(pt::kLanes) * kLaneStep + 10, cap, Color::Accent, Face::Label);
  } else if (armed_ != 0u) {
    r.text(kGridX, kCellY + static_cast<int>(pt::kLanes) * kLaneStep + 10, armed_ == kArmPattern ? pt::kClearPatternPrompt : (armed_ == kArmFill ? pt::kFillPrompt : pt::kClearPrompt[lane_ % pt::kLanes]), Color::Accent,
           Face::Label);
  } else if (note_ != nullptr) {
    r.text(kGridX, kCellY + static_cast<int>(pt::kLanes) * kLaneStep + 10, note_, Color::Accent, Face::Label);
  } else {
    r.text(kGridX, kCellY + static_cast<int>(pt::kLanes) * kLaneStep + 14,
           lane() == pt::Lane::Note ? "PADS: ROWS A AND B ABOVE (F6, F7), STEPS AND PATTERNS BELOW. SHIFT + STEP: REST"
                                    : "PADS: PALETTE ABOVE, STEPS AND PATTERNS BELOW. SHIFT + STEP: REST",
           Color::TextDim, Face::Micro);
  }
}

}  // namespace o3_voc03er
