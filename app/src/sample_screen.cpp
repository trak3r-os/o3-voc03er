// SPDX-License-Identifier: GPL-3.0-or-later
#include "sample_screen.h"

#include "midi_slice_logic.h"
#include "overview.h"
#include "pads_logic.h"
#include "pattern_logic.h"
#include "perform_logic.h"
#include "sample_logic.h"
#include "sample_rec_logic.h"
#include "skin/ornoir.h"

#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/context.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/placement.h>

#include <cstring>

namespace o3_voc03er {

using namespace o3sdk;
namespace pl = perform;
namespace sm = sample;
namespace sr = sample_rec;
namespace ov = overview;
namespace pt = pattern;
using ui::Color;
using ui::Face;
using Button = app::Button;
using Field = Vocoder::LoopField;

namespace {

constexpr std::uint64_t kNoteMs = 2500;
constexpr int kLevelTap = 6;  // a second tap on F8 moves the level this many half-dB (a detent moves 1)

constexpr pads::Layout kDefaultNotes{};  // A NOTE, B TRIAD: when no PERFORM is wired (a standalone screen)
constexpr const char* kSlotNames[4] = {"SLOT 1", "SLOT 2", "SLOT 3", "SLOT 4"};
constexpr const char* kKeyNames[ornoir::kFKeys] = {"LOW PADS", "SPEAKS", "SPEED", "DIR", "LOOP", "TRIG", "SLICES", "LEVEL"};
// The SHIFT row: F1 to F3 only (the others are blank and dim), labelled the way PATTERN's and PERFORM's are.
constexpr const char* kShiftNames[ornoir::kFKeys] = {"", "SLICE CH", "OCTAVE", "", "", "", "", ""};
constexpr unsigned kKeyPlays = 0;  // F1
constexpr unsigned kKeySliceCh = 1;  // F2, with SHIFT
constexpr unsigned kKeyOctave = 2;   // F3, with SHIFT (AK1: the octave was LEFT and RIGHT)

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

// A slice voice is a loop reader playing a region (the unit's Regions). Reader k's own Speed, Dir, Level and Role are overwritten for the slice
// of another slot, so the first slice on a reader keeps what they were (`saved`) and the end of its region writes them back: until then
// SampleScreen::view() shows slot k from `saved`. Main loop only.
struct SliceVoice {
  bool active;    // asked, not yet seen to end
  bool seen;      // LoopStatus::region was true once since the ask
  bool modified;  // the four values of the reader are the slice's: restore at the end
  bool gate;      // a pad release ends it
  bool rep;       // a repeating slice (Trig 2: LOOP was ON when the pad went down): it owns the slot's reader until another pad, F5 OFF or (GATE) the release
  std::uint8_t slot, pad;
  std::uint8_t speed, dir, role;  // saved
  float level_db;                 // saved
  std::uint32_t seq;
  std::uint64_t started_ms;
};

// SAMPLE's tables, in the SDRAM (O3SDK_BIG: zero at start, main loop only; the screen keeps just the cursor in the SRAM). Zero means TRIG ONE SHOT and
// SLICES MONO (CHOKE ON in the code), the defaults. The help tables are valid while HELP is open.
struct SampleData {
  sm::SliceTable table[sm::kReaders];
  SliceVoice voice[sm::kReaders];
  std::uint8_t edited[sm::kReaders];     // the cuts are the hand's or a re-cut's (SAMPLE EDIT): service() leaves them alone until the length changes
  std::uint8_t even_code[sm::kReaders];  // SAMPLE EDIT's CUT EVEN value (sample_edit_logic.h even_cuts_of); 0 = 8, the cuts a take starts from
  std::uint8_t gate[sm::kReaders];       // TRIG per slot: 1 GATE (the release also stops the slice), 0 ONE SHOT
  std::uint8_t choke_off[sm::kReaders];  // CHOKE per slot: 1 OFF
  std::uint8_t was_region[sm::kReaders]; // service(): the reader's region bit of the last pass (a slice that begins turns its slot's loop off)
  std::uint32_t seq;
  SliceMap map;                 // the SPEAKS slot's slices for the step sequencer's SLICE lane (published through StepSeq::slices_open)
  sm::SlicePublished published;  // what the map holds
  app::HelpPadGroup groups[app::kHelpMaxGroups];
  char pad_name[app::kPadCount][app::kHelpPadLabel + 1];
};
O3SDK_BIG SampleData g_sd = {};
static_assert(kSliceMax == sm::kMaxSlices, "the step sequencer's map holds one entry per slice");

// The slices of `slot` that sound, as a bit mask (bit i: slice i + 1).
std::uint32_t sounding_mask(unsigned slot) noexcept {
  std::uint32_t m = 0;
  for (const SliceVoice& v : g_sd.voice) {
    if (v.active && v.slot == slot && v.pad < sm::kMaxSlices) {
      m |= std::uint32_t{1} << v.pad;
    }
  }
  return m;
}

// The slice that repeats on `slot` (pad + 1), 0 when none does.
unsigned repeating_slice(unsigned slot) noexcept {
  for (const SliceVoice& v : g_sd.voice) {
    if (v.active && v.rep && v.slot == slot) {
      return static_cast<unsigned>(v.pad) + 1u;
    }
  }
  return 0;
}

}  // namespace

void SampleScreen::init(dsp::UnitId voice, const Vocoder* voc, PerformScreen* perform, StepSeq* seq) noexcept {
  seq_ = seq;
  voice_ = voice;
  voc_ = voc;
  perform_ = perform;
  slot_ = 0;
  focus_ = 0;
  slice_ch_ = 0;
  shift_slice_ = false;
  shift_oct_ = false;
  flash_until_ = 0;
  aud_ = 0;
  g_sd = SampleData{};  // a restarted app starts from the defaults
}

SliceMap* SampleScreen::slice_map() noexcept { return &g_sd.map; }

void SampleScreen::set_slice_ch(unsigned v) noexcept { slice_ch_ = midislice::clamp(v); }

void SampleScreen::set_slot(unsigned slot) noexcept { slot_ = static_cast<std::uint8_t>(slot < sm::kReaders ? slot : sm::kReaders - 1u); }
sm::SliceTable& SampleScreen::table(unsigned slot) noexcept { return g_sd.table[slot & 3u]; }
bool SampleScreen::edited(unsigned slot) const noexcept { return g_sd.edited[slot & 3u] != 0u; }
void SampleScreen::set_edited(unsigned slot, bool v) noexcept { g_sd.edited[slot & 3u] = v ? 1u : 0u; }
unsigned SampleScreen::even_code(unsigned slot) const noexcept { return g_sd.even_code[slot & 3u]; }
void SampleScreen::set_even_code(unsigned slot, unsigned code) noexcept { g_sd.even_code[slot & 3u] = static_cast<std::uint8_t>(code); }
// publish_slices compares only the slot and the length: a table edited in place needs the next service() pass to publish it again.
void SampleScreen::cuts_changed() noexcept { g_sd.published = sm::SlicePublished{}; }

const ui::Theme* SampleScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void SampleScreen::enter(app::Context& ctx) noexcept {
  note_ = nullptr;
  shift_slice_ = false;  // SHIFT may have been let go on another screen, where update() did not see it
  shift_oct_ = false;
  ctx.redraw();
}

void SampleScreen::leave(app::Context& ctx) noexcept { pad_leave(ctx); }

void SampleScreen::pad_leave(app::Context& ctx) noexcept {
  release_all(ctx);
  if (perform_ != nullptr) {
    perform_->audition_release_all(ctx);  // the lower pads' notes let go
  }
  aud_ = 0;
}

std::uint64_t SampleScreen::repeat_buttons(const app::Context&) const noexcept {
  return (std::uint64_t{1} << static_cast<unsigned>(Button::Left)) | (std::uint64_t{1} << static_cast<unsigned>(Button::Right));
}

void SampleScreen::say(app::Context& ctx, const char* text) noexcept {
  note_ = text;
  note_until_ = ctx.uptime_ms() + kNoteMs;
  ctx.redraw();
}

// SPEED, DIR, LEVEL and LOOP of the selected slot. While a slice of another slot borrows its reader the new value waits in `saved` (the reader
// plays that slice); while a slice of its own slot plays on it both are written, or the end of the region would undo the change.
void SampleScreen::write(app::Context& ctx, Field f, float v) noexcept { write_slot(ctx, slot_, f, v); }

void SampleScreen::write_slot(app::Context& ctx, unsigned loop, Field f, float v) noexcept {
  loop &= 3u;
  SliceVoice& sv = g_sd.voice[loop];
  if (sv.modified) {
    if (f == Field::Speed) {
      sv.speed = static_cast<std::uint8_t>(v);
    } else if (f == Field::Dir) {
      sv.dir = v >= 0.5f ? 1u : 0u;
    } else if (f == Field::Level) {
      sv.level_db = v;
    }
    if (sv.slot != loop && f != Field::Play) {
      return;
    }
  }
  (void)ctx.engine().set_param(voice_, Vocoder::loop_param(loop, f), v);
}

bool SampleScreen::gate(unsigned slot) const noexcept { return g_sd.gate[slot & 3u] != 0u; }
bool SampleScreen::choke_off(unsigned slot) const noexcept { return g_sd.choke_off[slot & 3u] != 0u; }
void SampleScreen::set_gate(unsigned slot, bool on) noexcept { g_sd.gate[slot & 3u] = on ? 1u : 0u; }
void SampleScreen::set_choke_off(unsigned slot, bool off) noexcept { g_sd.choke_off[slot & 3u] = off ? 1u : 0u; }

void SampleScreen::set_values(app::Context& ctx, unsigned loop, unsigned speed, bool reverse, float level_db) noexcept {
  write_slot(ctx, loop, Field::Speed, static_cast<float>(speed < sm::kSpeedCount ? speed : 1u));
  write_slot(ctx, loop, Field::Dir, reverse ? 1.0f : 0.0f);
  write_slot(ctx, loop, Field::Level, level_db < sm::kLevelMinDb ? sm::kLevelMinDb : (level_db > sm::kLevelMaxDb ? sm::kLevelMaxDb : level_db));
}

// The slot's status as the screen shows it: a borrowed reader's own values come from `saved`, not from what the slice has put there.
Vocoder::LoopStatus SampleScreen::view(unsigned loop) const noexcept {
  Vocoder::LoopStatus st = voc_ != nullptr ? voc_->loop_status(loop) : Vocoder::LoopStatus{};
  const SliceVoice& sv = g_sd.voice[loop & 3u];
  if (sv.modified && sv.slot != loop) {
    st.speed = sv.speed;
    st.reverse = sv.dir != 0u;
    st.level_db = sv.level_db;
  }
  return st;
}

// A slice of `slot` (SAMPLE: the selected one; PATTERN's audition: the one that speaks) on a reader (sm::pick_reader). The reader plays the slot's cut at the slot's Speed, Dir and Level, on the slot's
// side (a slot on no side speaks): those four are written first, then Start, Len and Trig 1, in that order, so the unit takes the span with the
// trigger. A press is at most 7 writes (3 when the reader is the slot's own and its values already are the slot's): the unit drains 64 a block
// (kMaxParamsPerBlock) and the ring holds 512, and the 24 slice pads give at most 24 * 7 in a frame, which the frame cadence does not allow.
//
// A pad of this screen (`pressed`) while the slot's whole loop plays (or waits) does not turn the loop off: it repeats that slice (Trig 2) on the slot's own reader, where the unit
// pauses the loop under it. The slot has one repeating slice (the CHOKE-ON choice whatever SLICES says): another pad takes it over, F5 OFF or (TRIG GATE) the release lets it go.
void SampleScreen::trigger(app::Context& ctx, unsigned slot, unsigned pad, bool pressed) noexcept {
  slot &= 3u;
  if (voc_ == nullptr || perform_ == nullptr) {
    return;
  }
  if (pad >= g_sd.table[slot].count) {  // UX3-8: a pad past the count says how many slices there are
    static char line[32];                // main loop only; note_ points at it for kNoteMs
    sm::slices_line(g_sd.table[slot].count, line);
    say(ctx, line);
    return;
  }
  dsp::Engine& e = ctx.engine();
  const sm::Cut cut = g_sd.table[slot].cut[pad];
  sm::ReaderState rs[sm::kReaders];
  const route::Route rt = perform_->route_now(ctx);
  for (unsigned k = 0; k < sm::kReaders; ++k) {
    const Vocoder::LoopStatus st = voc_->loop_status(k);
    rs[k].sided = sm::side_of(rt, k) != sm::Side::None;
    rs[k].recording = st.rec != Vocoder::LoopStatus::Rec::Idle;
    rs[k].loop_on = st.playing || st.pending;
    rs[k].region = g_sd.voice[k].active || st.region;
    rs[k].seq = g_sd.voice[k].seq;
  }
  const bool repeat = pressed && rs[slot].loop_on && !rs[slot].recording;
  const int pick = sm::pick_reader(rs, slot, repeat || g_sd.choke_off[slot] == 0u);
  if (pick < 0) {
    say(ctx, sm::no_reader_line(voc_->loop_status(slot).rec == Vocoder::LoopStatus::Rec::Armed));
    return;
  }
  const unsigned k = static_cast<unsigned>(pick);
  const Vocoder::LoopStatus src = view(slot);  // before reader k's values change (k may be borrowed)
  const bool car = sm::slice_is_carrier(sm::side_of(rt, slot));
  SliceVoice& v = g_sd.voice[k];
  if (!v.modified) {
    v.speed = static_cast<std::uint8_t>(round_param(e, voice_, Vocoder::loop_param(k, Field::Speed)));
    v.dir = static_cast<std::uint8_t>(round_param(e, voice_, Vocoder::loop_param(k, Field::Dir)));
    v.role = static_cast<std::uint8_t>(round_param(e, voice_, Vocoder::loop_param(k, Field::Role)));
    v.level_db = e.param(voice_, Vocoder::loop_param(k, Field::Level));
    v.modified = true;
  }
  const auto put = [&](Field f, float val) {
    const std::uint16_t id = Vocoder::loop_param(k, f);
    if (e.param(voice_, id) != val) {
      (void)e.set_param(voice_, id, val);
    }
  };
  put(Field::Speed, static_cast<float>(src.speed));
  put(Field::Dir, src.reverse ? 1.0f : 0.0f);
  put(Field::Level, src.level_db);
  put(Field::Role, car ? 1.0f : 0.0f);
  // The slot's whole loop does not wait under the slice: a loop that plays (or waits for the bar) is turned off first, so LOOP reads OFF and the
  // pads own the slot until F5 starts it again. The unit would otherwise resume the loop where the slice paused it.
  if (!repeat) {
    stop_loop(ctx, slot);
  }
  if (g_sd.choke_off[slot] == 0u || repeat) {  // SLICES MONO: a slice of this slot on a borrowed reader (SLICES was POLY) fades out
    for (unsigned j = 0; j < sm::kReaders; ++j) {
      if (j != k && g_sd.voice[j].active && g_sd.voice[j].slot == slot) {
        g_sd.voice[j].gate = false;
        (void)e.set_param(voice_, Vocoder::region_param(j, Vocoder::RegionField::Trig), 0.0f);
      }
    }
  }
  (void)e.set_param(voice_, Vocoder::region_param(k, Vocoder::RegionField::Start), static_cast<float>(sm::region_start(slot, Loop::kMaxSamples, cut)));
  (void)e.set_param(voice_, Vocoder::region_param(k, Vocoder::RegionField::Len), static_cast<float>(cut.len));
  (void)e.set_param(voice_, Vocoder::region_param(k, Vocoder::RegionField::Trig), repeat ? 2.0f : 1.0f);
  v.active = true;
  v.seen = false;
  v.rep = repeat;
  v.gate = g_sd.gate[slot] != 0u;
  v.slot = static_cast<std::uint8_t>(slot);
  v.pad = static_cast<std::uint8_t>(pad);
  v.seq = ++g_sd.seq;
  v.started_ms = ctx.uptime_ms();
  ctx.redraw();
}

void SampleScreen::fire_slice(app::Context& ctx, unsigned pad) noexcept {
  if (perform_ == nullptr) {
    return;
  }
  const std::uint8_t sp = perform_->route_now(ctx).speaks;
  if (route::is_slot(sp)) {
    trigger(ctx, static_cast<unsigned>(sp - route::kFirstSlot), pad);
  }
}

// Turns the slot's own loop off when it plays or waits for the bar (the unit's Play 0 leaves a sounding region alone). Reads the unit's status,
// not view(): a borrowed reader's saved values are not its play state.
void SampleScreen::stop_loop(app::Context& ctx, unsigned slot) noexcept {
  if (voc_ == nullptr) {
    return;
  }
  const Vocoder::LoopStatus st = voc_->loop_status(slot & 3u);
  if (st.playing || st.pending) {
    (void)ctx.engine().set_param(voice_, Vocoder::loop_param(slot & 3u, Field::Play), 0.0f);
  }
}

// TRIG GATE: the pad's release fades the voice out. A ONE SHOT voice (the default) ignores it and plays its cut to the end cut.
void SampleScreen::release_pad(app::Context& ctx, unsigned pad) noexcept {
  for (unsigned k = 0; k < sm::kReaders; ++k) {
    SliceVoice& v = g_sd.voice[k];
    if (v.active && v.gate && v.pad == pad) {
      v.gate = false;
      v.rep = false;  // the unit's release also ends a repeat: the slot's loop goes on
      (void)ctx.engine().set_param(voice_, Vocoder::region_param(k, Vocoder::RegionField::Trig), 0.0f);
    }
  }
}

// F5 OFF: the slot's repeating slice lets go (Trig 0 fades it out; the loop's own Play 0 is the caller's).
void SampleScreen::stop_repeat(app::Context& ctx, unsigned slot) noexcept {
  for (unsigned k = 0; k < sm::kReaders; ++k) {
    SliceVoice& v = g_sd.voice[k];
    if (v.active && v.rep && v.slot == (slot & 3u)) {
      v.rep = false;
      v.gate = false;
      (void)ctx.engine().set_param(voice_, Vocoder::region_param(k, Vocoder::RegionField::Trig), 0.0f);
    }
  }
}

void SampleScreen::release_all(app::Context& ctx) noexcept {
  for (unsigned pad = 0; pad < sm::kTopPads; ++pad) {
    release_pad(ctx, pad);
  }
}

void SampleScreen::service(app::Context& ctx) noexcept {
  if (voc_ == nullptr) {
    return;
  }
  dsp::Engine& e = ctx.engine();
  const std::uint64_t now = ctx.uptime_ms();
  for (unsigned k = 0; k < sm::kReaders; ++k) {  // a slice that begins without a pad (the SLICE lane, a MIDI note) owns its slot as a pad's does
    const Vocoder::LoopStatus st = voc_->loop_status(k);
    if (st.region && g_sd.was_region[k] == 0u && (st.playing || st.pending) && !(g_sd.voice[k].active && g_sd.voice[k].rep)) {
      (void)e.set_param(voice_, Vocoder::loop_param(k, Field::Play), 0.0f);
    }
    g_sd.was_region[k] = st.region ? 1u : 0u;
  }
  for (unsigned k = 0; k < sm::kReaders; ++k) {
    SliceVoice& v = g_sd.voice[k];
    if (!v.active) {
      continue;
    }
    const Vocoder::LoopStatus vs = voc_->loop_status(k);
    if (v.rep && !(vs.playing || vs.pending)) {  // the slot's loop was turned off elsewhere (PERFORM, a MIDI start): the repeat lets go with it
      v.rep = false;
      v.gate = false;
      (void)e.set_param(voice_, Vocoder::region_param(k, Vocoder::RegionField::Trig), 0.0f);
    }
    const bool region = vs.region;
    v.seen = v.seen || region;
    if (!sm::region_ended(v.seen, region, now - v.started_ms)) {
      continue;
    }
    v.active = false;
    v.gate = false;
    v.rep = false;
    if (v.modified) {  // the reader's own slot's values and role come back, as the screens and PERFORM's roles had them
      const auto back = [&](Field f, float val) {
        const std::uint16_t id = Vocoder::loop_param(k, f);
        if (e.param(voice_, id) != val) {
          (void)e.set_param(voice_, id, val);
        }
      };
      back(Field::Speed, static_cast<float>(v.speed));
      back(Field::Dir, static_cast<float>(v.dir));
      back(Field::Level, v.level_db);
      back(Field::Role, static_cast<float>(v.role));
      v.modified = false;
    }
  }
  for (unsigned j = 0; j < sm::kReaders; ++j) {  // the cuts follow the slot's length (SAMPLE EDIT, U7, will edit them)
    const Vocoder::LoopStatus st = voc_->loop_status(j);
    const std::uint32_t len = st.rec == Vocoder::LoopStatus::Rec::Idle ? st.length : 0u;
    if (g_sd.table[j].length != len) {  // a new take (its length is 0 while it records) starts from 8 even cuts; hand cuts at the same length stay
      if (g_sd.edited[j] != 0u) {
        say(ctx, sm::kCutsResetLine);  // UX3-8: hand-edited cuts do not go silently
      }
      sm::even_cuts(g_sd.table[j], len);
      g_sd.edited[j] = 0;
      g_sd.even_code[j] = 0;
    }
  }
  std::uint32_t lens[ov::kSlots];  // the overview waveform follows the lengths too (a take starts it, an empty slot clears it)
  for (unsigned j = 0; j < ov::kSlots; ++j) lens[j] = g_sd.table[j].length;
  ov::shared().service(lens);
  if (seq_ != nullptr && perform_ != nullptr) {  // the SPEAKS slot's slices go to the step sequencer when the slot or its length changed
    const route::Route rt = perform_->route_now(ctx);
    const unsigned slot = route::is_slot(rt.speaks) ? static_cast<unsigned>(rt.speaks - route::kFirstSlot) : sm::kReaders;
    (void)sm::publish_slices(*seq_, g_sd.published, slot, slot < sm::kReaders ? &g_sd.table[slot] : nullptr, Loop::kMaxSamples);
  }
}

// Writes a plan for `slot`: the side it leaves is freed first, so each write keeps the route allowed and the unit stops or plays the slot by the roles
// (PerformScreen::apply_route).
void SampleScreen::move_slot(app::Context& ctx, unsigned slot, const route::Route& cur, const sm::SidePlan& p) noexcept {
  const std::uint8_t v = sm::slot_value(slot);
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

// F2 SPEAKS, as PERFORM's SPEAKS chip names it: LIVE or SLOT 1..4. A tap makes the selected slot speak (again: SPEAKS goes back to LIVE); the encoder steps LIVE,
// SLOT 1..4 over the slots that hold audio and the selection follows the slot that speaks. A slot that is the SOUNDS source is freed to the synth (plan_side)
// and the note says so. Where SPEAKS cannot be heard in this mode (TRACT without MOUTH on LIVE) the note is the reason.
void SampleScreen::set_speaks(app::Context& ctx, int delta, bool tap) noexcept {
  if (voc_ == nullptr || perform_ == nullptr) {
    return;
  }
  const route::Route cur = perform_->route_now(ctx);
  sm::Side target = sm::Side::Speaks;
  unsigned slot = slot_;
  if (tap) {
    target = sm::tap_speaks_target(cur, slot_);
  } else {
    unsigned audio = 0;
    for (unsigned i = 0; i < sm::kReaders; ++i) {
      audio |= voc_->loop_status(i).length != 0u ? 1u << i : 0u;
    }
    const unsigned pos = sm::speaks_pos(cur);
    const unsigned np = sm::step_speaks(pos, delta, audio);
    if (np == pos) {
      return;
    }
    if (np == 0u) {
      target = sm::Side::None;
      slot = static_cast<unsigned>(cur.speaks - route::kFirstSlot);
    } else {
      slot = np - 1u;
    }
  }
  const sm::SidePlan p = sm::plan_side(cur, target, slot, voc_->loop_status(slot).length == 0u);
  if (p.refused) {
    say(ctx, p.why);
    return;
  }
  if (!p.change) {
    return;
  }
  const bool freed = cur.sounds == sm::slot_value(slot) && p.to.sounds != cur.sounds;
  move_slot(ctx, slot, cur, p);
  if (target == sm::Side::Speaks && !tap) {
    slot_ = static_cast<std::uint8_t>(slot);
  }
  const unsigned mode = pl::mode_index(ctx.engine().param(voice_, Vocoder::Mode));
  const sm::Cue cue = sm::mode_cue(mode, sm::Side::Speaks, sm::tract_live(ctx.engine()));
  if (target != sm::Side::Speaks) {
    say(ctx, sm::kSpeaksLiveNote);
  } else if (sm::cue_dim(cue)) {
    static char line[64];  // main loop only; note_ points at it for kNoteMs
    sm::cue_line(line, slot, sm::Side::Speaks, cue, false, p.to.speaks);
    say(ctx, line);
  } else {
    say(ctx, freed ? sm::kSpeaksFreedNote[slot] : sm::kSpeaksNote[slot]);
  }
}

// `delta` is the encoder's detents, or +-1 for a tap; a tap wraps where a detent stops at the end.
void SampleScreen::turn(app::Context& ctx, unsigned key, int delta) noexcept {
  const bool tap = delta == 1 && key_tap_;
  const Vocoder::LoopStatus ls = view(slot_);
  switch (key) {
    case kKeyPlays:  // LOW PADS: the row's note kind, in the notes palette
      step_plays(ctx, delta, tap);
      break;
    case 1:  // SPEAKS
      set_speaks(ctx, delta, tap);
      break;
    case 2:  // SPEED
      write(ctx, Field::Speed, static_cast<float>(sm::step_speed(ls.speed, delta, tap)));
      break;
    case 3:  // DIR
      write(ctx, Field::Dir, ls.reverse ? 0.0f : 1.0f);
      break;
    case 4:  // LOOP
      if (ls.length == 0u) {
        say(ctx, waiting(slot_) ? "SLOT IS LOADING" : "EMPTY: SAMPLE REC OR SAMPLE LOAD");
        return;
      }
      if (ls.playing || ls.pending) {
        stop_repeat(ctx, slot_);  // F5 OFF stops everything of the slot: a repeating slice fades out with the loop
        write(ctx, Field::Play, 0.0f);
      } else {
        write(ctx, Field::Play, 1.0f);
      }
      break;
    case 5:  // TRIG: a flip
      g_sd.gate[slot_ & 3u] ^= 1u;
      break;
    case 6:  // SLICES: MONO (a new slice cuts the one before) or POLY (they overlap)
      g_sd.choke_off[slot_ & 3u] ^= 1u;
      say(ctx, g_sd.choke_off[slot_ & 3u] != 0u ? "SLICES: POLY, THEY OVERLAP" : "SLICES: MONO, A NEW SLICE CUTS THE ONE BEFORE");
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
    if (in.shift) {
      const bool again_oct = shift_oct_ && k == kKeyOctave;
      shift_slice_ = k == kKeySliceCh;
      shift_oct_ = k == kKeyOctave;
      if (k == kKeySliceCh) {  // SHIFT + F2: SLICE CH, a tap steps on and wraps (SAME, 1 .. 16, OFF), the encoder steps without wrapping
        slice_ch_ = slice_ch_ >= midislice::kOff ? midislice::kSame : static_cast<std::uint8_t>(slice_ch_ + 1u);
        ctx.redraw();
      } else if (k == kKeyOctave) {  // SHIFT + F3: OCTAVE takes the encoder and the arrows; a second tap steps it up
        if (again_oct && perform_ != nullptr) {
          perform_->step_octave(ctx, 1);
        }
        ctx.redraw();
      }
      return;
    }
    shift_slice_ = false;
    shift_oct_ = false;
    if (k == focus_ || k <= 1u || (k >= 3u && k <= 6u)) {  // a second tap steps the value on (LOW PADS, SPEAKS, DIR, LOOP, TRIG and SLICES act on the first)
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
    step_value(ctx, in.shift, in.delta());
    return;
  }
  if (in.press(Button::Up)) {
    slot_ = static_cast<std::uint8_t>(slot_ > 0u ? slot_ - 1u : 0u);
    ctx.redraw();
  } else if (in.press(Button::Down)) {
    slot_ = static_cast<std::uint8_t>(slot_ < 3u ? slot_ + 1u : 3u);
    ctx.redraw();
  } else if (in.press(Button::Left) || in.press(Button::Right)) {  // the focused value one step down or up, as a detent (AK1); repeats arrive as presses (repeat_buttons)
    key_tap_ = false;
    step_value(ctx, in.shift, in.press(Button::Left) ? -1 : 1);
  } else {
    (void)pad_input(ctx, in);
  }
}

// The encoder's detents or an arrow: the SHIFT-row value in focus while SHIFT is held (SLICE CH, OCTAVE), else the F key's value. With SHIFT held and no
// SHIFT value focused the plain value steps, as it always did.
void SampleScreen::step_value(app::Context& ctx, bool shift, int delta) noexcept {
  if (shift && shift_slice_) {
    slice_ch_ = midislice::step(slice_ch_, delta);
    ctx.redraw();
    return;
  }
  if (shift && shift_oct_) {
    if (perform_ != nullptr) {
      perform_->step_octave(ctx, delta);
    }
    return;
  }
  turn(ctx, focus_, delta);
}

// The pads, here and on SAMPLE EDIT: the top rows play the selected slot's slices, the bottom rows this screen's own rows A and B (notes only).
bool SampleScreen::pad_input(app::Context& ctx, const app::Input& in, bool repeat_ok) noexcept {
  if (in.pad_press()) {
    if (sm::layout_pad(in.pad())) {
      play_pad(ctx, in.pad());
    } else {
      trigger(ctx, slot_, in.pad(), repeat_ok);
    }
  } else if (in.pad_release()) {
    if (sm::layout_pad(in.pad())) {
      release_play(ctx, in.pad());
    } else {
      release_pad(ctx, in.pad());
    }
  } else {
    return false;
  }
  return true;
}

// Pad 24..47 is column pad % 12 of row A (24..35) or B (36..47), as PATTERN's palette rows 0 and 1 are: the notes palette says what it plays (a note or a chord, nothing on
// OFF) at PERFORM's octave, and PERFORM's audition entry sounds it (ids 0..23: pad - 24, no widening needed). Never a parameter strip: the palette is notes only,
// whatever mode PERFORM is in (SP1).
const pads::Layout& SampleScreen::lay() const noexcept { return perform_ != nullptr ? perform_->notes() : kDefaultNotes; }

void SampleScreen::play_pad(app::Context& ctx, unsigned pad) noexcept {
  if (perform_ == nullptr || !sm::layout_pad(pad) || pad >= app::kPadCount) {
    return;
  }
  const unsigned p = pad - sm::kTopPads;  // 0..23
  const pads::Row prow = p < app::kPadCols ? pads::Row::A : pads::Row::B;
  const pads::PadPlay pp = pads::pad_play(lay(), prow, p % app::kPadCols, perform_->octave());
  if (pp.note < 1) {
    return;
  }
  perform_->audition_press(ctx, p, static_cast<unsigned>(pp.note), pp.kind);
  aud_ |= std::uint32_t{1} << p;
}

void SampleScreen::release_play(app::Context& ctx, unsigned pad) noexcept {
  if (perform_ == nullptr || !sm::layout_pad(pad) || pad >= app::kPadCount) {
    return;
  }
  const unsigned p = pad - sm::kTopPads;
  if (((aud_ >> p) & 1u) != 0u) {
    aud_ &= ~(std::uint32_t{1} << p);
    perform_->audition_release(ctx, p);
  }
}

// F1 LOW PADS: both rows at once, one of sm::kLowPadsNames (NOTE+TRIAD is the palette's default: A NOTE, B TRIAD; the others give both rows that kind; never OFF). The first
// tap steps, a tap wraps, a detent stops at the ends. A pair the list has no place for steps to the first (or, from the end, repairs to the pair the step names). A new
// value ends the notes: the old meaning started them (the held pad stays down and sounds again at its next press).
void SampleScreen::step_plays(app::Context& ctx, int delta, bool tap) noexcept {
  if (perform_ == nullptr) {
    return;
  }
  const pads::Layout& l = lay();
  const unsigned next = sm::low_pads_step(sm::low_pads_index(l.row[0], l.row[1]), delta, tap);
  if (l.row[0] != sm::kLowA[next] || l.row[1] != sm::kLowB[next]) {
    (void)perform_->notes_set(pads::Row::A, sm::kLowA[next]);
    (void)perform_->notes_set(pads::Row::B, sm::kLowB[next]);
    perform_->audition_release_all(ctx);
    aud_ = 0;
    flash_until_ = ctx.uptime_ms() + pads::kRowFlashMs;
  }
  ctx.redraw();
}

void SampleScreen::pad_update(app::Context& ctx) noexcept {
  if (perform_ != nullptr) {
    perform_->update(ctx);  // the pads' bend and vibrato, as on PERFORM
  }
}

void SampleScreen::update(app::Context& ctx) noexcept {
  if (!ctx.panel().shift()) {
    shift_slice_ = false;
    shift_oct_ = false;
  }
  pad_update(ctx);
  if (note_ != nullptr && ctx.uptime_ms() >= note_until_) {
    note_ = nullptr;
  }
  ctx.redraw();  // the play state moves with the bar
}

// The top two rows are the slot's slices: a cut dim, a sounding one on, an empty slot dark. The bottom two are this screen's rows A and B as they play now (the
// key's root low, the scale dim, the rest and the pads that play nothing dark, a held pad on), both going dark for a moment when LOW PADS changes.
void SampleScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept { pad_leds(ctx, leds); }

void SampleScreen::pad_leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  if (perform_ == nullptr) {
    return;
  }
  {
    const dsp::Engine& e = ctx.engine();
    const unsigned key = round_param(e, voice_, Vocoder::Key) % 12u;
    const std::uint16_t cols = pl::scale_cols(key, round_param(e, voice_, Vocoder::Scale));
    const bool flash = ctx.uptime_ms() < flash_until_;
    for (unsigned pad = sm::kTopPads; pad < app::kPadCount; ++pad) {
      const unsigned p = pad - sm::kTopPads;
      const unsigned row = p / app::kPadCols;
      const pads::PadPlay pp = pads::pad_play(lay(), static_cast<pads::Row>(row), p % app::kPadCols, perform_->octave());
      std::uint8_t level = app::kLedOff;
      if (pp.note > 0 && !flash) {
        const unsigned pc = static_cast<unsigned>(pp.note) % 12u;
        level = pc == key ? app::kLedLow : (((cols >> pc) & 1u) != 0u ? app::kLedDim : app::kLedOff);
      }
      if (((aud_ >> p) & 1u) != 0u) {
        level = app::kLedOn;
      }
      leds.set(pad, level);
    }
  }
  const unsigned count = g_sd.table[slot_ & 3u].count;
  const std::uint32_t on = sounding_mask(slot_);
  // The slice the SLICE lane played last, on the SPEAKS slot: mid, unless a pad press has it on already.
  unsigned lane_pad = sm::kTopPads;
  const unsigned lane_slice = seq_ != nullptr ? seq_->playing_slice() : 0u;
  if (lane_slice != 0u && perform_->route_now(ctx).speaks == sm::slot_value(slot_)) {
    lane_pad = lane_slice - 1u;
  }
  for (unsigned pad = 0; pad < sm::kTopPads; ++pad) {
    std::uint8_t level = pad < count ? (((on >> pad) & 1u) != 0u ? app::kLedOn : app::kLedDim) : app::kLedOff;
    if (pad == lane_pad && level != app::kLedOn) {
      level = app::kLedMid;
    }
    leds.set(pad, level);
  }
}

void SampleScreen::soft_keys(app::Context&, app::SoftKeys& out) noexcept {
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    out.label[i] = kKeyNames[i];
  }
}

// HELP: the lower pads are this screen's rows A and B (notes only, SP1); every button input() answers here has a row (tests/sample_test.cpp holds it).
void SampleScreen::help(const app::Context&, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Up, "SLOT BEFORE", nullptr},
      {Button::Down, "SLOT AFTER", nullptr},
      {Button::Left, "F VALUE DOWN, HOLD REPEATS", nullptr},
      {Button::Right, "F VALUE UP, HOLD REPEATS", nullptr},
      {Button::Shift, "F2 SLICE CH, F3 OCTAVE", nullptr},
      {Button::Params, "PARAMS: FOUR PAGES", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER", nullptr},
      {Button::File, "FILE", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD", nullptr},
      {Button::SamplePlay, "THIS SCREEN", nullptr},
      {Button::SampleEdit, "SAMPLE EDIT: THE CUTS", nullptr},
      {Button::SampleRec, "SAMPLE REC: A TAKE", nullptr},
      {Button::Song, "SONG", nullptr},
      {Button::Pattern, "PATTERN: THE 16 STEPS", nullptr},
      {Button::Note, "PATTERN: NOTE LANE", nullptr},
      {Button::Instr, "PATTERN: CARRIER LANE", nullptr},
      {Button::Vol, "PATTERN: LEVEL LANE", nullptr},
      {Button::Fx, "PATTERN: SLICE LANE", nullptr},
  };
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    h.fkey[i] = nullptr;
    h.fkey_shift[i] = nullptr;
  }
  h.summary = "THE FOUR SLOTS: UP AND DOWN PICK ONE; F2 CHOOSES WHO SPEAKS; THE F KEYS DO THE REST.\n"
              "UPPER PADS: SLICES; LOWER: NOTES (F1). MIDI FROM 36: SLICES (CHANNEL: SHIFT + F2).";
  // The legend: the slices, then this screen's rows A and B as they play now (a pad's group is its row + 1: the slices 1, A 2, B 3).
  const pads::Layout& layout = lay();
  const int oct = perform_ != nullptr ? perform_->octave() : 0;
  g_sd.groups[0] = {"SLICES", "A SLICE PLAYS ITS CUT; WITH LOOP ON IT REPEATS", ui::Color::Accent};
  g_sd.groups[1] = {pads::kPlaysNames[pads::idx(layout.row[0])], "ROW A: F1 LOW PADS SETS WHAT IT PLAYS", ui::Color::Vol};
  g_sd.groups[2] = {pads::kPlaysNames[pads::idx(layout.row[1])], "ROW B: F1 LOW PADS SETS IT TOO", ui::Color::Fx};
  h.groups = g_sd.groups;
  h.group_count = 3;
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    char* l = g_sd.pad_name[pad];
    l[0] = '\0';
    h.pad_group[pad] = static_cast<std::uint8_t>(pad < sm::kTopPads ? 1u : (pad - sm::kTopPads < app::kPadCols ? 2u : 3u));
    if (pad < sm::kTopPads) {
      cat_uint(l, sizeof g_sd.pad_name[pad], pad + 1u);
    } else {
      const unsigned p = pad - sm::kTopPads;
      const pads::Row prow = p < app::kPadCols ? pads::Row::A : pads::Row::B;
      const pads::PadPlay pp = pads::pad_play(layout, prow, p % app::kPadCols, oct);
      if (pp.note > 0) {  // the root and the kind after it: "C", "C7TH", "C#PWR"
        cat(l, sizeof g_sd.pad_name[pad], pt::kPitchClass[static_cast<unsigned>(pp.note) % 12u]);
        if (pp.kind != 0u) {
          cat(l, sizeof g_sd.pad_name[pad], pt::kCellKind[(pp.kind - 1u) & 3u]);
        }
      }
    }
    h.pad_label[pad] = l[0] != '\0' ? l : nullptr;
  }
  h.pads = "UPPER ROWS: THE SLOT'S SLICES. LOWER ROWS: NOTES; F1 LOW PADS SETS THE KIND.";
  h.fkey[0] = "BOTH LOWER ROWS' KIND";
  h.fkey_shift[1] = kShiftNames[1];
  h.fkey_shift[2] = kShiftNames[2];
  h.fkey[1] = "WHO SPEAKS: LIVE OR A SLOT";
  h.fkey[2] = "0.5X, 1X, 2X";
  h.fkey[3] = "FORWARD, REVERSE";
  h.fkey[4] = "LOOP. PADS REPEAT";
  h.fkey[5] = "GATE STOPS AT THE RELEASE";
  h.fkey[6] = "MONO CUTS THE ONE BEFORE";
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

// The play line of the selected slot: a bar, a tick at each cut (lit while its slice sounds), a head for every reader whose position is in the slot
// (the slot's own loop or a slice on another reader), and under the bar what is left of each slice that sounds. Rects only, so the display list grows by 2 + the cuts + the heads + a remaining bar for each slice that sounds.
bool SampleScreen::on_no_side(app::Context& ctx, unsigned slot) const noexcept {
  const route::Route rt = perform_ != nullptr ? perform_->route_now(ctx) : route::Route{};
  return sm::side_of(rt, slot & 3u) == sm::Side::None;
}

o3sdk::ui::Rgb SampleScreen::slot_ink(app::Context& ctx, unsigned slot) const noexcept {
  const route::Route rt = perform_ != nullptr ? perform_->route_now(ctx) : route::Route{};
  return ornoir::theme().rgb(side_ink(sm::side_of(rt, slot & 3u)));
}

void SampleScreen::draw_line(app::Context& ctx, ui::Renderer& r, unsigned slot, std::uint32_t length, int x, int y, int w) const noexcept {
  const sm::SliceTable& t = g_sd.table[slot & 3u];
  const std::uint32_t on = sounding_mask(slot);
  const std::uint32_t base = slot * Loop::kMaxSamples;
  ui::plot::Canvas c(r);
  c.open();
  if (ov::shared().shown(slot)) {  // the overview picture (240 wide) above the line, the cuts over it
    ov::shared().draw(c, slot, false, x, kRowY + 50, slot_ink(ctx, slot));
    for (unsigned i = 0; i < t.count; ++i) {
      ornoir::fill_rect(c, x + static_cast<int>(sm::bar_x(t.cut[i].start, length, static_cast<std::uint32_t>(w - 2))), kRowY + 50, 1, static_cast<int>(ov::kRows),
                        ((on >> i) & 1u) != 0u ? Color::Play : Color::TextDim);
    }
  }
  ornoir::fill_rect(c, x, y, w, 10, Color::Empty);
  for (unsigned i = 0; i < t.count; ++i) {
    const int tx = x + static_cast<int>(sm::bar_x(t.cut[i].start, length, static_cast<std::uint32_t>(w - 2)));
    ornoir::fill_rect(c, tx, y - 6, 2, 22, ((on >> i) & 1u) != 0u ? Color::Play : Color::TextDim);
  }
  for (unsigned k = 0; k < sm::kReaders && voc_ != nullptr; ++k) {
    const Vocoder::LoopStatus s = voc_->loop_status(k);
    if ((s.playing || s.region) && s.position >= base && s.position - base < length) {
      const int hx = x + static_cast<int>(sm::bar_x(s.position - base, length, static_cast<std::uint32_t>(w - 4)));
      ornoir::fill_rect(c, hx, y - 3, 4, 16, Color::Accent);
      if (s.region) {  // what is left of the slice that sounds: from the head to the end cut (to the start cut when it plays reversed), under the bar
        const std::uint32_t off = s.position - base;
        for (unsigned i = 0; i < t.count; ++i) {
          if (off >= t.cut[i].start && off - t.cut[i].start < t.cut[i].len) {
            const std::uint32_t wd = static_cast<std::uint32_t>(w - 4);
            const int a = x + static_cast<int>(sm::bar_x(t.cut[i].start, length, wd));
            const int b = x + static_cast<int>(sm::bar_x(t.cut[i].start + t.cut[i].len, length, wd)) + 4;
            const int x0 = s.reverse ? a : hx;
            const int x1 = s.reverse ? hx + 4 : b;
            if (x1 > x0) {
              ornoir::fill_rect(c, x0, y + 16, x1 - x0, 4, Color::Play);
            }
            break;
          }
        }
      }
    }
  }
  c.close();
}

// The header's chips as SAMPLE shows them (SAMPLE EDIT draws the same).
void SampleScreen::fill_header(app::Context& ctx, ornoir::HeaderState& h) const noexcept {
  const dsp::Engine& e = ctx.engine();
  const app::AudioIn& ain = ctx.audio_in();
  const SourceName* src = ain.available() ? find_source(ain.source()) : nullptr;
  const unsigned mode = pl::mode_index(e.param(voice_, Vocoder::Mode));
  header_route(e, voice_, src != nullptr ? src->label : "NONE", src != nullptr && src->source == hal::capture::Source::Usb, h);
  h.mode = pl::kModeNames[mode];
  h.playing = e.playing();
  h.tempo_x10 = e.stats().tempo_mbpm / 100u;
  h.cpu_percent = ctx.cpu_percent();
}

void SampleScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  ov::shared().pump(r.display());  // a staged overview picture goes up to RAM_G (main loop, before the display list uses it)
  const dsp::Engine& e = ctx.engine();
  const unsigned tempo = e.stats().tempo_mbpm;
  const float bar = pl::bar_samples(tempo);
  const route::Route rt = perform_ != nullptr ? perform_->route_now(ctx) : route::Route{};
  const pads::Layout& layout = lay();  // SP1: the notes palette, whatever mode PERFORM is in

  Vocoder::LoopStatus st[4];
  for (unsigned i = 0; i < 4 && voc_ != nullptr; ++i) {
    st[i] = view(i);
  }
  const Vocoder::LoopStatus& ls = st[slot_ & 3u];
  const bool empty = ls.length == 0u;
  const sm::Side side = sm::side_of(rt, slot_);
  // UX3-1: in TUNE a SOUNDS slot is not heard, in TRACT a SPEAKS or NONE slot only through DRY: that side is drawn dim, with the reason below.
  const unsigned mode = pl::mode_index(ctx.engine().param(voice_, Vocoder::Mode));
  const bool mouth_live = sm::tract_live(ctx.engine());
  const auto ink = [mode, mouth_live](sm::Side sd) { return sm::cue_dim(sm::mode_cue(mode, sd, mouth_live)) ? Color::TextDim : side_ink(sd); };
  const sm::Cue cue = sm::mode_cue(mode, side, mouth_live);

  ornoir::HeaderState h;
  fill_header(ctx, h);

  ornoir::FKey keys[ornoir::kFKeys];
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    keys[i].name = kKeyNames[i];
    keys[i].hot = i == focus_;
  }
  cat(keys[0].value, sizeof keys[0].value, sm::low_pads_key(layout.row[0], layout.row[1]));  // "NOTE+TRI": what both rows play (the full name is in the notes)
  const unsigned speaks_at = sm::speaks_pos(rt);  // F2 as PERFORM's SPEAKS chip: LIVE or SLOT n
  if (speaks_at == 0u) {
    cat(keys[1].value, sizeof keys[1].value, "LIVE");
  } else {
    cat(keys[1].value, sizeof keys[1].value, "SLOT ");
    cat_uint(keys[1].value, sizeof keys[1].value, speaks_at);
  }
  const sm::Cue speaks_cue = sm::mode_cue(mode, sm::Side::Speaks, mouth_live);  // is what SPEAKS says heard in this mode?
  keys[1].role = speaks_at != 0u && !sm::cue_dim(speaks_cue) ? ornoir::KeyRole::Voice : ornoir::KeyRole::Plain;
  keys[1].dim = speaks_at != 0u && sm::cue_dim(speaks_cue);
  cat(keys[2].value, sizeof keys[2].value, sm::kSpeedNames[ls.speed < sm::kSpeedCount ? ls.speed : 1u]);
  cat(keys[3].value, sizeof keys[3].value, sm::kDirNames[ls.reverse ? 1 : 0]);
  sm::loop_text(ls.playing || ls.pending, repeating_slice(slot_ & 3u), keys[4].value);
  cat(keys[5].value, sizeof keys[5].value, g_sd.gate[slot_ & 3u] != 0u ? "GATE" : "ONE SHOT");
  cat(keys[6].value, sizeof keys[6].value, g_sd.choke_off[slot_ & 3u] != 0u ? "POLY" : "MONO");
  {
    char num[8];
    sm::level_text(ls.level_db, num);
    cat(keys[7].value, sizeof keys[7].value, num);
    cat(keys[7].value, sizeof keys[7].value, " DB");
    keys[7].pct = sm::level_percent(ls.level_db);
  }

  if (ctx.panel().shift()) {  // the SHIFT row replaces the plain one (names and values)
    for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
      keys[i] = ornoir::FKey{};
      keys[i].name = kShiftNames[i];
      keys[i].dim = i != kKeySliceCh && i != kKeyOctave;
    }
    midislice::format(slice_ch_, keys[kKeySliceCh].value);
    keys[kKeySliceCh].hot = shift_slice_;
    {
      const int oct = perform_ != nullptr ? perform_->octave() : 0;
      char num[3];
      std::size_t n = 0;
      if (oct > 0) {
        num[n++] = '+';
      } else if (oct < 0) {
        num[n++] = '-';
      }
      num[n++] = static_cast<char>('0' + (oct < 0 ? -oct : oct));
      num[n] = '\0';
      cat(keys[kKeyOctave].value, sizeof keys[kKeyOctave].value, num);
      keys[kKeyOctave].pct = pl::bar_percent(static_cast<float>(oct), static_cast<float>(kMinOctave), static_cast<float>(kMaxOctave));
      keys[kKeyOctave].hot = shift_oct_;
    }
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
      cat(buf, sizeof buf, waiting(i) ? "LOADING" : "EMPTY");
    }
    r.text(kColX + kColW - 10 - r.text_width(buf, Face::Label), y + 8, buf, full ? Color::Text : Color::TextDim, Face::Label);
    if (full) {
      buf[0] = '\0';
      cat(buf, sizeof buf, sm::kSpeedNames[s.speed < sm::kSpeedCount ? s.speed : 1u]);
      cat(buf, sizeof buf, "  ");
      cat(buf, sizeof buf, sm::kDirNames[s.reverse ? 1 : 0]);
      r.text(kColX + 10, y + 38, buf, Color::Text, Face::Label);
      r.text(kColX + kColW - 10 - r.text_width(sm::kSideNames[static_cast<unsigned>(sd)], Face::Label), y + 38, sm::kSideNames[static_cast<unsigned>(sd)],
             ink(sd), Face::Label);
    }
  }

  // The middle: the slot's name, its play state and where it plays (no wave: SAMPLE EDIT, U7, draws it); below, the play line.
  const int mid = kMidX + kMidW / 2;
  r.text(centred(r, kSlotNames[slot_ & 3u], Face::Display, mid), kRowY + 14, kSlotNames[slot_ & 3u], Color::Text, Face::Display);
  if (empty && waiting(slot_)) {
    r.text(centred(r, "LOADING", Face::Label, mid), kRowY + 100, "LOADING", Color::Accent, Face::Label);
  } else if (empty) {
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
    r.text(centred(r, on, Face::Label, mid), kRowY + 168, on, ink(side), Face::Label);
    draw_line(ctx, r, slot_, ls.length, kMidX + 14, kRowY + 214, kMidW - 28);
  }

  // The right: SOUNDS as PERFORM shows it, and what the two lower pad rows play (F1 LOW PADS sets both).
  r.text(kRightX + 12, kRowY + 12, "SOUNDS", Color::TextDim, Face::Micro);
  r.text(kRightX + 12, kRowY + 34, h.sounds, Color::Sel, Face::Label);
  r.text(kRightX + 12, kRowY + 84, "LOWER PADS", Color::TextDim, Face::Micro);
  for (unsigned i = 0; i < 2; ++i) {
    buf[0] = '\0';
    cat(buf, sizeof buf, pads::kRowNames[i]);
    cat(buf, sizeof buf, " ");
    cat(buf, sizeof buf, pads::kPlaysNames[pads::idx(layout.row[i])]);
    r.text(kRightX + 12, kRowY + 104 + static_cast<int>(i) * 22, buf, Color::Text, Face::Label);
  }
  {  // MS1: the channel MIDI fires slices on; "SET MIDI CH" when it cannot work (the user's channel is omni: the route takes every channel)
    char val[12];
    midislice::format(slice_ch_, val);
    buf[0] = '\0';
    cat(buf, sizeof buf, "SLICE CH ");
    cat(buf, sizeof buf, val);
    r.text(kRightX + 12, kRowY + 168, buf, Color::TextDim, Face::Micro);
    if (midislice::needs_hint(slice_ch_, ctx.midi().config().channel)) {
      r.text(kRightX + 12, kRowY + 190, "SET MIDI CH", Color::Accent, Face::Label);
    }
  }

  if (note_ != nullptr) {
    r.text(kColX, kRowY + 4 * kRowH + 2, note_, Color::Accent, Face::Label);
  } else {
    char hint[64];
    hint[0] = '\0';
    cat(hint, sizeof hint, "UPPER PADS: SLICES. LOWER PADS: ");
    for (unsigned i = 0; i < 2; ++i) {
      cat(hint, sizeof hint, i == 0 ? "A " : "  B ");
      cat(hint, sizeof hint, pads::kPlaysNames[pads::idx(layout.row[i])]);
    }
    r.text(kColX, kRowY + 4 * kRowH + 36, hint, Color::TextDim, Face::Micro);
    if (!empty) {  // the cue line: what the selected slot does now, or why it is quiet in this mode
      static char line[64];  // main loop only
      sm::cue_line(line, slot_, side, cue, ls.playing || ls.pending, rt.speaks);
      r.text(kColX, kRowY + 4 * kRowH + 2, line, sm::cue_dim(cue) ? Color::Accent : Color::Text, Face::Label);
    }
  }
}

}  // namespace o3_voc03er
