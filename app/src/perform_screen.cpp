// SPDX-License-Identifier: GPL-3.0-or-later
#include "perform_screen.h"

#include "perform_logic.h"
#include "skin/ornoir.h"
#include "voiceprint_logic.h"

#include <o3_voc03er/chords.h>
#include <o3_voc03er/presets.h>

#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/context.h>
#include <o3sdk/core/placement.h>

#include <cmath>
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
  ornoir::PickerView pick;  // the open picker's rows (build_picker)
  voiceprint::Axis axis;                     // TALK's 32 angles, computed at the first use (axis.ready)
  std::uint8_t env[voiceprint::kPoints];     // TALK's envelope as last computed
  std::uint64_t env_ms;                      // when (Context::uptime_ms)
  std::uint8_t centre;                       // TUNE's ladder centre (0: not set), see voiceprint::ladder_follow
};
O3SDK_BIG PerformData g_pd = {};
O3SDK_BIG app::HelpPadNames g_help_names = {};  // help(): valid while HELP is open

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

// ---- the pickers' and the capture's helpers --------------------------------------------------------------------------------------------

constexpr unsigned kCapBars = 2;            // a capture is two bars long
constexpr std::uint64_t kCapStartMs = 250;  // the unit shows the Rec within this, or the capture failed
constexpr const char* kSlotNames[route::kSlots] = {"SLOT 1", "SLOT 2", "SLOT 3", "SLOT 4"};
static_assert(kSourceCount == route::kLiveSrcCount, "route.h's LiveSrc order is input.h's kSources");

template <std::size_t N>
void put(char (&dst)[N], const char* s) noexcept {
  std::size_t n = 0;
  while (*s != '\0' && n + 1u < N) {
    dst[n++] = *s++;
  }
  dst[n] = '\0';
}

void cat(char* dst, std::size_t cap, const char* s) noexcept {
  std::size_t n = std::strlen(dst);
  while (*s != '\0' && n + 1u < cap) {
    dst[n++] = *s++;
  }
  dst[n] = '\0';
}

// The route the engine holds, as route.h knows it: a ModSrc that is not LIVE or a slot (NONE) counts as LIVE, a CarSrc that is not a route value
// (FOLLOW) as SYNTH. Only the side a pick changes is written back, so those values survive a pick on the other side.
route::Route route_of(const dsp::Engine& e, dsp::UnitId id) noexcept {
  const unsigned m = round_param(e, id, Vocoder::ModSrc);
  const unsigned c = round_param(e, id, Vocoder::CarSrc);
  route::Route r;
  r.speaks = m < 256u && route::speaks_valid(static_cast<std::uint8_t>(m)) ? static_cast<std::uint8_t>(m) : route::kSpeaksLive;
  r.sounds = c < 256u && route::sounds_valid(static_cast<std::uint8_t>(c)) ? static_cast<std::uint8_t>(c) : route::kSoundsSynth;
  if (!route::allowed(r)) {
    r.sounds = route::kSoundsSynth;
  }
  return r;
}

unsigned live_index(const app::AudioIn& a) noexcept {
  if (a.available()) {
    for (unsigned i = 0; i < kSourceCount; ++i) {
      if (kSources[i].source == a.source()) {
        return i;
      }
    }
  }
  return 0;
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
  sfocus_ = pl::kNoShiftFocus;
  octave_ = 0;
  bottom_ = 0;
  keys_ = false;
  strip_ = false;
  strip_last_ = 0;
  range_ = 2;
  local_ = 0.0f;
  wheel_ = 0.0f;
  spring_at_ = 0;
  ctl_held_ = 0;
  bnd_dir_ = 0;
  bnd_at_[0] = bnd_at_[1] = 0;
  mod_held_ = false;
  mod_t_ = 0;
  mod_from_ = 0.0f;
  vib_pad_ = 0.0f;
  cc1_ = 0;
  vib_sent_ = 0.0f;
  flash_[0] = '\0';
  flash_until_ = 0;
  latched_ = -1;
  held_ = 0;
  label_sig_ = 0xFFFFFFFFu;
  frame_ = 0;
  pick_ = 0;
  pick_cursor_ = 0;
  cap_ = Cap::Idle;
  cap_slot_ = 0;
  cap_stop_ = false;
  cap_pre_ = false;
  cap_pre_src_ = 0;
  cap_pre_side_ = 0;
  cap_t0_ = 0;
  std::memset(&g_pd, 0, sizeof g_pd);
}

const ui::Theme* PerformScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void PerformScreen::enter(app::Context& ctx) noexcept { ctx.redraw(); }

void PerformScreen::leave(app::Context& ctx) noexcept {
  pick_ = 0;  // a picker does not outlive the screen; a capture does (update() resumes it)
  release_all(ctx);
}

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
  if (pl::on_strip(pad, strip_)) {  // the bend strip: no notes, the pad's semitones bend the carrier (the last pad pressed wins)
    held_ |= std::uint64_t{1} << pad;
    g_pd.count[pad] = 0;
    strip_last_ = pad;
    bnd_dir_ = 0;
    local_ = static_cast<float>(pl::strip_semitone(app::pad_col(pad)));
    spring_at_ = 0;
    apply_bend(ctx);
    ctx.redraw();
    return;
  }
  if (keys_) {
    const pl::PianoPad pp = pl::piano_pad(app::pad_row(pad), app::pad_col(pad), strip_);
    if (pp.fn == pl::PianoFn::Formant) {  // the formant strip: a tap sets Formant and it stays (no note, nothing held)
      (void)e.set_param(voice_, Vocoder::Formant, static_cast<float>(pp.offset));
      char val[pl::kLabelChars + 1] = {};
      pl::strip_label(pp.offset, val);
      show_flash(ctx, "FORMANT", val);
      return;
    }
    if (pl::is_control(pp.fn)) {  // a control: no note; the notes held keep the note they began with
      control_press(ctx, pad, pp.fn);
      return;
    }
    const int note = pl::piano_note(pp, octave_);
    if (note < 0) {
      return;
    }
    g_pd.notes[pad][0] = static_cast<std::uint8_t>(note);
    g_pd.count[pad] = 1;
  } else {
    const Chord prev = chord_from(g_pd.last_notes, g_pd.last_count);
    const Chord c = make_chord(pl::row_kind(app::pad_row(pad), static_cast<ChordKind>(bottom_), strip_), app::pad_col(pad), octave_,
                               round_param(e, voice_, Vocoder::Key), round_param(e, voice_, Vocoder::Scale), Voicing::Smooth,
                               g_pd.last_count != 0u ? &prev : nullptr);
    g_pd.count[pad] = c.count;
    for (unsigned i = 0; i < kChordMaxNotes; ++i) {
      g_pd.notes[pad][i] = i < c.count ? c.notes[i] : 0u;
    }
    g_pd.last_count = c.count;
    std::memcpy(g_pd.last_notes, c.notes, sizeof g_pd.last_notes);
  }
  held_ |= std::uint64_t{1} << pad;
  for (unsigned i = 0; i < g_pd.count[pad]; ++i) {
    (void)e.note_on(voice_, g_pd.notes[pad][i], kVelocity, o3_voc03er::Vocoder::kNoteLocal);
  }
  ctx.redraw();
}

void PerformScreen::release_pad(app::Context& ctx, unsigned pad) noexcept {
  if (pad < app::kPadCount && ((ctl_held_ >> pad) & 1u) != 0u) {
    control_release(ctx, pad, pl::piano_pad(app::pad_row(pad), app::pad_col(pad), strip_).fn);
    return;
  }
  if (pad >= app::kPadCount || ((held_ >> pad) & 1u) == 0u) {
    return;
  }
  held_ &= ~(std::uint64_t{1} << pad);
  if (pl::on_strip(pad, strip_)) {  // back to 0, to the BND pad still held, or to the strip pad still held when this was the last
    const std::uint64_t rest = held_ >> (pl::kStripRow * app::kPadCols);
    if (rest == 0u && (ctl_held_ & 0xC00u) != 0u) {  // BND- and BND+ are pads 10 and 11 of row 0
      bnd_dir_ = (ctl_held_ & 0x800u) != 0u ? 1 : -1;
    } else if (rest == 0u) {
      local_ = 0.0f;
    } else if (pad == strip_last_) {
      unsigned c = 0;
      while (((rest >> c) & 1u) == 0u) {
        ++c;
      }
      strip_last_ = pl::kStripRow * app::kPadCols + c;
      local_ = static_cast<float>(pl::strip_semitone(c));
    }
    apply_bend(ctx);
    ctx.redraw();
    return;
  }
  for (unsigned i = 0; i < g_pd.count[pad]; ++i) {
    if (!note_in_use(g_pd.notes[pad][i], app::kPadCount)) {  // a note another held pad or the latch still holds stays on
      (void)ctx.engine().send(voice_, o3_voc03er::Vocoder::note_off_local(g_pd.notes[pad][i]));
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
      (void)e.send(voice_, o3_voc03er::Vocoder::note_off_local(old[i]));
    }
  }
  ctx.redraw();
}

void PerformScreen::release_all(app::Context& ctx) noexcept {
  dsp::Engine& e = ctx.engine();
  for (unsigned p = 0; p < app::kPadCount; ++p) {
    if (((held_ >> p) & 1u) != 0u) {
      for (unsigned i = 0; i < g_pd.count[p]; ++i) {
        (void)e.send(voice_, o3_voc03er::Vocoder::note_off_local(g_pd.notes[p][i]));
      }
    }
  }
  if (latched_ >= 0) {
    for (unsigned i = 0; i < g_pd.lat_count && i < kChordMaxNotes; ++i) {
      (void)e.send(voice_, o3_voc03er::Vocoder::note_off_local(g_pd.lat_notes[i]));
    }
  }
  held_ = 0;
  ctl_held_ = 0;
  bnd_dir_ = 0;
  mod_held_ = false;
  vib_pad_ = 0.0f;
  latched_ = -1;
  g_pd.lat_count = 0;
  local_ = 0.0f;  // the strip's and the encoder's bend end with the pads (the wheel stays)
  spring_at_ = 0;
  apply_bend(ctx);
  apply_vib(ctx);
}

// The note of a keyboard pad: with BEND ROW the three rows above the strip are the keyboard (the lowest octave moves up a row).
int PerformScreen::key_pad_note(unsigned pad) const noexcept {
  return pl::piano_note(pl::piano_pad(app::pad_row(pad), app::pad_col(pad), strip_), octave_);
}

// BEND ROW on or off (SHIFT + F7, or the BEND pad of KEYS): the pads' notes end and the strip's and the encoder's bend return to 0.
void PerformScreen::toggle_strip(app::Context& ctx) noexcept {
  release_all(ctx);
  strip_ = !strip_;
  ctx.redraw();
}

// The unit's Bend: the local and the wheel's semitones added, held to +-12 (the unit ramps it; main loop only).
void PerformScreen::apply_bend(app::Context& ctx) noexcept { (void)ctx.engine().set_param(voice_, Vocoder::Bend, pl::bend_sum(local_, wheel_)); }

void PerformScreen::free_encoder(app::Context& ctx) noexcept {
  pick_ = 0;
  focus_ = pl::kBendFocus;
  sfocus_ = pl::kNoShiftFocus;
  ctx.redraw();
}

void PerformScreen::set_wheel(app::Context& ctx, unsigned lsb, unsigned msb) noexcept {
  wheel_ = pl::wheel_semitones(lsb, msb, range_);
  apply_bend(ctx);
}

bool PerformScreen::set_bend_range(unsigned semitones) noexcept {
  for (const unsigned r : pl::kBendRanges) {
    if (r == semitones) {
      range_ = r;
      local_ = pl::bend_step(local_, 0, false, r);  // held inside the new range
      return true;
    }
  }
  return false;
}

// The vibrato depth the unit gets: the MOD pad's and the mod wheel's, the larger; only a change is written.
void PerformScreen::apply_vib(app::Context& ctx) noexcept {
  const float v = pl::vib_depth(vib_pad_, cc1_);
  if (v != vib_sent_) {
    vib_sent_ = v;
    (void)ctx.engine().set_param(voice_, Vocoder::Vib, v);
  }
}

void PerformScreen::set_mod_wheel(app::Context& ctx, unsigned cc) noexcept {
  cc1_ = cc;
  apply_vib(ctx);
}

// A moment's text on the hint line (VOL, ROOT, OCT).
void PerformScreen::show_flash(app::Context& ctx, const char* name, const char* value) noexcept {
  std::size_t n = 0;
  for (const char* p = name; *p != '\0' && n + 1 < sizeof flash_; ++p) {
    flash_[n++] = *p;
  }
  if (n + 1 < sizeof flash_) {
    flash_[n++] = ' ';
  }
  for (const char* p = value; *p != '\0' && n + 1 < sizeof flash_; ++p) {
    flash_[n++] = *p;
  }
  flash_[n] = '\0';
  flash_until_ = ctx.uptime_ms() + 1200u;
  ctx.redraw();
}

namespace {
inline constexpr float kLevelMin = -48.0f;  // Vocoder::Level's range (the one PLAY's OUTPUT LEVEL steps)
inline constexpr float kLevelMax = 6.0f;
}  // namespace

// A KEYS control pad goes down: no note. OCT, VOL and ROOT act at once; BND and MOD act while held (update() runs their ramps).
void PerformScreen::control_press(app::Context& ctx, unsigned pad, pl::PianoFn fn) noexcept {
  dsp::Engine& e = ctx.engine();
  const std::uint64_t now = ctx.uptime_ms();
  ctl_held_ |= std::uint64_t{1} << pad;
  char val[12] = {};
  switch (fn) {
    case pl::PianoFn::OctDown:
    case pl::PianoFn::OctUp: {
      octave_ = clamp_octave(octave_ + (fn == pl::PianoFn::OctUp ? 1 : -1));
      unsigned n = 0;
      if (octave_ > 0) {
        val[n++] = '+';
      } else if (octave_ < 0) {
        val[n++] = '-';
      }
      val[n++] = static_cast<char>('0' + (octave_ < 0 ? -octave_ : octave_));
      val[n] = '\0';
      show_flash(ctx, "OCT", val);
      break;
    }
    case pl::PianoFn::VolDown:
    case pl::PianoFn::VolUp: {
      (void)e.step_param(voice_, Vocoder::Level, fn == pl::PianoFn::VolUp ? 1 : -1, true);
      pl::format_db(e.param(voice_, Vocoder::Level), val);
      show_flash(ctx, "VOL", val);
      break;
    }
    case pl::PianoFn::RootDown:
    case pl::PianoFn::RootUp: {
      static constexpr const char* kRoot[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
      const unsigned k = static_cast<unsigned>(pl::step_wrapped(round_param(e, voice_, Vocoder::Key), fn == pl::PianoFn::RootUp ? 1 : -1, 12));
      (void)e.set_param(voice_, Vocoder::Key, static_cast<float>(k));
      show_flash(ctx, "ROOT", kRoot[k % 12u]);
      break;
    }
    case pl::PianoFn::BndDown:
    case pl::PianoFn::BndUp: {
      const unsigned i = fn == pl::PianoFn::BndUp ? 1u : 0u;
      bnd_at_[i] = now;
      bnd_dir_ = i != 0u ? 1 : -1;
      spring_at_ = 0;
      ctx.redraw();
      break;
    }
    case pl::PianoFn::Mod:
      mod_held_ = true;
      mod_t_ = now - static_cast<std::uint32_t>(vib_pad_ * static_cast<float>(pl::kRampMs));  // the rise goes on from the depth in force
      ctx.redraw();
      break;
    default:
      break;
  }
}

void PerformScreen::control_release(app::Context& ctx, unsigned pad, pl::PianoFn fn) noexcept {
  ctl_held_ &= ~(std::uint64_t{1} << pad);
  if (fn == pl::PianoFn::Mod) {
    mod_held_ = false;
    mod_from_ = vib_pad_;
    mod_t_ = ctx.uptime_ms();
  } else if (fn == pl::PianoFn::BndDown || fn == pl::PianoFn::BndUp) {
    const int mine = fn == pl::PianoFn::BndUp ? 1 : -1;
    if (bnd_dir_ == mine) {
      const bool other_up = (ctl_held_ & 0x800u) != 0u;
      const bool other_down = (ctl_held_ & 0x400u) != 0u;
      if (other_up || other_down) {  // the other BND pad is still down: it takes over from now
        bnd_dir_ = other_up ? 1 : -1;
        bnd_at_[other_up ? 1 : 0] = ctx.uptime_ms();
      } else {
        bnd_dir_ = 0;
        const std::uint64_t strip_held = held_ >> (pl::kStripRow * app::kPadCols);
        if (strip_ && strip_held != 0u) {
          local_ = static_cast<float>(pl::strip_semitone(app::pad_col(strip_last_)));
        } else {
          local_ = 0.0f;
        }
        apply_bend(ctx);
      }
    }
  }
  ctx.redraw();
}

// The encoder's bend springs back about kBendSpringMs after the last detent (the unit's ramp makes the glide); BND and MOD ramp with the time
// held; the hint's flash ends. Each unit parameter is written only when it changes.
void PerformScreen::update(app::Context& ctx) noexcept {
  const std::uint64_t now = ctx.uptime_ms();
  if (spring_at_ != 0u && now >= spring_at_) {
    spring_at_ = 0;
    local_ = 0.0f;
    apply_bend(ctx);
    ctx.redraw();
  }
  if (bnd_dir_ != 0) {
    const std::uint64_t t0 = bnd_at_[bnd_dir_ > 0 ? 1 : 0];
    const float st = pl::bend_time_semitones(now - t0, bnd_dir_, range_);
    if (st != local_) {
      local_ = st;
      apply_bend(ctx);
      ctx.redraw();
    }
  }
  if (mod_held_) {
    vib_pad_ = pl::vib_rise(now - mod_t_);
  } else if (vib_pad_ > 0.0f) {
    vib_pad_ = pl::vib_fall(mod_from_, now - mod_t_);
  }
  apply_vib(ctx);
  if (flash_until_ != 0u && now >= flash_until_) {
    flash_until_ = 0;
    flash_[0] = '\0';
    ctx.redraw();
  }
  if (cap_ != Cap::Idle) {
    capture_tick(ctx);
  }
}

// ---- the pickers and the capture ------------------------------------------------------------------------------------------------------

void PerformScreen::open_picker(app::Context& ctx, route::Side side) noexcept {
  pick_ = side == route::Side::Speaks ? 1u : 2u;
  focus_ = side == route::Side::Speaks ? 1u : 2u;
  build_picker(ctx);
  pick_cursor_ = 0;
  for (unsigned i = 0; i < g_pd.pick.count; ++i) {  // the cursor starts on the value in force
    if (g_pd.pick.row[i].on) {
      pick_cursor_ = static_cast<std::uint8_t>(i);
      break;
    }
  }
  g_pd.pick.cursor = pick_cursor_;
  ctx.redraw();
}

void PerformScreen::close_picker(app::Context& ctx) noexcept {
  if (pick_ != 0u) {
    pick_ = 0;
    ctx.redraw();
  }
}

// The rows of the open picker, from route.h, with the state the engine and the loops are in now.
void PerformScreen::build_picker(app::Context& ctx) noexcept {
  const route::Side side = pick_ == 1u ? route::Side::Speaks : route::Side::Sounds;
  const route::Route r = route_of(ctx.engine(), voice_);
  const route::LiveSrc cur = static_cast<route::LiveSrc>(live_index(ctx.audio_in()));
  const std::uint8_t now = side == route::Side::Speaks ? r.speaks : r.sounds;
  route::Row rows[route::kMaxRows];
  const unsigned n = route::rows(side, cur, rows, route::kMaxRows);
  ornoir::PickerView& v = g_pd.pick;
  v.count = n < ornoir::kPickerRows ? n : ornoir::kPickerRows;
  for (unsigned i = 0; i < v.count; ++i) {
    ornoir::PickerRow& pr = v.row[i];
    pr.name[0] = pr.note[0] = '\0';
    pr.on = false;
    const route::RowState st = route::row_state(side, r, rows[i]);
    pr.dimmed = st.dimmed;
    switch (rows[i].kind) {
      case route::Kind::Synth:
        put(pr.name, "SYNTH");
        pr.on = now == route::kSoundsSynth;
        break;
      case route::Kind::Noise:
        put(pr.name, "NOISE");
        pr.on = now == route::kSoundsNoise;
        break;
      case route::Kind::Voice:
        put(pr.name, "VOICE");
        put(pr.note, "SUNG PITCH");
        pr.on = now == route::kSoundsVoice;
        break;
      case route::Kind::Live:
        put(pr.name, "LIVE");
        put(pr.note, route::live_name(kSources[static_cast<unsigned>(cur)].label, kSources[static_cast<unsigned>(cur)].source == hal::capture::Source::Usb, side));
        pr.on = now == (side == route::Side::Speaks ? route::kSpeaksLive : route::kSoundsLive);
        break;
      case route::Kind::Source:
        put(pr.name, kSources[rows[i].arg < kSourceCount ? rows[i].arg : 0u].label);
        put(pr.note, route::reason_text(st.reason));
        break;
      case route::Kind::Slot: {
        put(pr.name, kSlotNames[rows[i].arg & 3u]);
        const std::uint32_t len = voc_ != nullptr ? voc_->loop_status(rows[i].arg & 3u).length : 0u;
        put(pr.note, st.dimmed ? route::reason_text(st.reason) : (len != 0u ? "LOOP" : "EMPTY"));
        pr.on = now == route::kFirstSlot + (rows[i].arg & 3u);
        break;
      }
    }
  }
  if (pick_cursor_ >= v.count) {
    pick_cursor_ = static_cast<std::uint8_t>(v.count != 0u ? v.count - 1u : 0u);
  }
  v.cursor = pick_cursor_;
}

void PerformScreen::loop_rec(app::Context& ctx, unsigned slot, bool on) noexcept {
  (void)ctx.engine().set_param(voice_, Vocoder::loop_param(slot, Vocoder::LoopField::Rec), on ? 1.0f : 0.0f);
}

void PerformScreen::switch_source(unsigned live_index_) noexcept {
  if (step_source_ != nullptr && live_index_ < kSourceCount) {
    step_source_(source_user_, kSources[live_index_].source, default_gain(kSources[live_index_].source));
  }
}

// The engine side of a route change: the one side `side` is written; a slot that left a side stops (its role, MOD by default, would replace the
// modulator for as long as it played), a slot that took one starts when it holds audio (a slot speaks or sounds only while it plays).
void PerformScreen::set_route(app::Context& ctx, route::Side side, route::Route from, route::Route to) noexcept {
  dsp::Engine& e = ctx.engine();
  const route::Roles a = route::roles(from);
  const route::Roles b = route::roles(to);
  for (unsigned i = 0; i < route::kSlots; ++i) {
    const std::uint16_t play = Vocoder::loop_param(i, Vocoder::LoopField::Play);
    if (a.slot[i] != route::SlotRole::Free && b.slot[i] == route::SlotRole::Free) {
      (void)e.set_param(voice_, play, 0.0f);
    } else if (a.slot[i] == route::SlotRole::Free && b.slot[i] != route::SlotRole::Free && voc_ != nullptr && voc_->loop_status(i).length != 0u) {
      (void)e.set_param(voice_, play, 1.0f);
    }
  }
  if (side == route::Side::Speaks) {
    (void)e.set_param(voice_, Vocoder::ModSrc, static_cast<float>(to.speaks));
  } else {
    (void)e.set_param(voice_, Vocoder::CarSrc, static_cast<float>(to.sounds));
  }
}

void PerformScreen::pad_event(app::Context& ctx, const app::Input& in) noexcept {
  if (in.pad_press()) {
    press_pad(ctx, in.pad());
  } else if (in.pad_release()) {
    release_pad(ctx, in.pad());
  }
}

void PerformScreen::step_octave(app::Context& ctx, int delta) noexcept {
  octave_ = clamp_octave(octave_ + delta);
  ctx.redraw();
}

route::Route PerformScreen::route_now(const app::Context& ctx) const noexcept { return route_of(ctx.engine(), voice_); }

// The cursor's row is taken. A dimmed LIVE source arms a capture (REC does it, the source follows); another dimmed row says why it cannot.
void PerformScreen::take_choice(app::Context& ctx) noexcept {
  const route::Side side = pick_ == 1u ? route::Side::Speaks : route::Side::Sounds;
  const route::Route r = route_of(ctx.engine(), voice_);
  route::Row rows[route::kMaxRows];
  const unsigned n = route::rows(side, static_cast<route::LiveSrc>(live_index(ctx.audio_in())), rows, route::kMaxRows);
  const unsigned at = pick_cursor_;
  close_picker(ctx);
  if (at >= n) {
    return;
  }
  const route::Row row = rows[at];
  const route::RowState st = route::row_state(side, r, row);
  if (st.dimmed) {
    if (st.pre_arm()) {
      cap_pre_ = true;
      cap_pre_src_ = row.arg;
      cap_pre_side_ = side == route::Side::Speaks ? 1u : 2u;
      show_flash(ctx, "REC", "CAPTURES FIRST");
    } else {
      show_flash(ctx, route::reason_text(st.reason), "");
    }
    return;
  }
  if (cap_ != Cap::Idle && (row.kind == route::Kind::Source || (row.kind == route::Kind::Slot && row.arg == cap_slot_))) {
    show_flash(ctx, "CAPTURING", "");  // the input and the slot are busy until it ends
    return;
  }
  route::Route to = r;
  if (!route::pick(side, to, row)) {
    return;
  }
  cap_pre_ = false;  // a plain choice drops the pre-arm
  if (row.kind == route::Kind::Source) {
    switch_source(row.arg);
  }
  set_route(ctx, side, r, to);
  ctx.redraw();
}

void PerformScreen::abort_capture(app::Context& ctx, const char* why) noexcept {
  loop_rec(ctx, cap_slot_, false);
  cap_ = Cap::Idle;
  cap_stop_ = false;
  show_flash(ctx, "REC", why);
}

// REC: capture the live source into the first empty slot. Again while it waits for the bar line: cancel. Again while it records: stop there.
void PerformScreen::rec_press(app::Context& ctx) noexcept {
  if (voc_ == nullptr) {
    return;
  }
  if (cap_ != Cap::Idle) {
    const bool armed = cap_ == Cap::Starting || voc_->loop_status(cap_slot_).rec == Vocoder::LoopStatus::Rec::Armed;
    if (armed) {
      abort_capture(ctx, "CANCELLED");  // no switch, no slot taken
    } else if (!cap_stop_) {
      loop_rec(ctx, cap_slot_, false);  // stops at the bar line; update() sees the loop end and finishes
      cap_stop_ = true;
    }
    return;
  }
  if (!ctx.audio_in().available()) {
    show_flash(ctx, "REC", "NO INPUT");
    return;
  }
  const route::Route r = route_of(ctx.engine(), voice_);
  int slot = -1;
  for (unsigned i = 0; i < route::kSlots && slot < 0; ++i) {
    const Vocoder::LoopStatus ls = voc_->loop_status(i);
    const auto v = static_cast<std::uint8_t>(route::kFirstSlot + i);
    if (ls.length == 0u && ls.rec == Vocoder::LoopStatus::Rec::Idle && !ls.playing && r.speaks != v && r.sounds != v) {
      slot = static_cast<int>(i);
    }
  }
  if (slot < 0) {
    show_flash(ctx, "REC", "NO EMPTY SLOT");
    return;
  }
  cap_slot_ = static_cast<std::uint8_t>(slot);
  cap_stop_ = false;
  cap_t0_ = ctx.uptime_ms();
  loop_rec(ctx, cap_slot_, false);  // the Rec parameter's stored value stays 1 after a stop: 0 then 1 records again
  loop_rec(ctx, cap_slot_, true);
  cap_ = Cap::Starting;
  ctx.redraw();
}

// Two bars from the bar line the unit starts at. The transport running: Rec 0 is written in the middle of the second bar and the unit stops at its
// end line; stopped: both happen at once, so 0 goes out when two bars have been recorded. The end of the loop (Idle with audio) finishes it.
void PerformScreen::capture_tick(app::Context& ctx) noexcept {
  if (voc_ == nullptr) {
    return;
  }
  using R = Vocoder::LoopStatus::Rec;
  const Vocoder::LoopStatus ls = voc_->loop_status(cap_slot_);
  if (cap_ == Cap::Starting) {
    if (ls.rec != R::Idle) {
      cap_ = Cap::Active;
    } else if (ctx.uptime_ms() - cap_t0_ > kCapStartMs) {
      abort_capture(ctx, "FAILED");
      return;
    }
  }
  if (cap_ == Cap::Active) {
    if (ls.rec == R::Idle) {
      if (ls.length != 0u) {
        finish_capture(ctx);
      } else {
        abort_capture(ctx, "FAILED");
      }
      return;
    }
    if (ls.rec == R::Recording && !cap_stop_) {
      const dsp::Engine& e = ctx.engine();
      const float bar = pl::bar_samples(e.stats().tempo_mbpm);
      const float need = pl::capture_stop_at(kCapBars, bar, e.playing());
      if (static_cast<float>(ls.length) >= need) {
        loop_rec(ctx, cap_slot_, false);
        cap_stop_ = true;
      }
    }
  }
  ctx.redraw();
}

// The loop holds audio: the side that held LIVE takes the slot (SPEAKS first), or, for a capture a dimmed row armed, the other side than the one that
// armed it. A pre-armed source then goes to the ADC and to the picker's side, in this same step: the source never changes without the slot taking over.
void PerformScreen::finish_capture(app::Context& ctx) noexcept {
  const route::Route r = route_of(ctx.engine(), voice_);
  route::Route cur = r;
  const route::Row slot_row{route::Kind::Slot, cap_slot_};
  cap_ = Cap::Idle;
  cap_stop_ = false;
  bool has = false;
  route::Side take = route::Side::Speaks;
  if (cap_pre_) {
    take = cap_pre_side_ == 1u ? route::Side::Sounds : route::Side::Speaks;
    has = take == route::Side::Speaks ? r.speaks == route::kSpeaksLive : r.sounds == route::kSoundsLive;
  } else if (r.speaks == route::kSpeaksLive) {
    has = true;
  } else if (r.sounds == route::kSoundsLive) {
    take = route::Side::Sounds;
    has = true;
  }
  if (has && route::pick(take, cur, slot_row)) {
    set_route(ctx, take, r, cur);
  } else {
    cur = r;
    has = false;
  }
  if (cap_pre_) {
    const route::Side side = cap_pre_side_ == 1u ? route::Side::Speaks : route::Side::Sounds;
    route::Route to = cur;
    if (route::pick(side, to, route::Row{route::Kind::Source, cap_pre_src_})) {
      switch_source(cap_pre_src_);
      set_route(ctx, side, cur, to);
    }
    cap_pre_ = false;
  }
  show_flash(ctx, kSlotNames[cap_slot_ & 3u], has ? "TAKEN" : "CAPTURED");
}

// ---- input --------------------------------------------------------------------------------------------------------------------------

void PerformScreen::bottom_kind(app::Context& ctx, int delta) noexcept {
  bottom_ = pl::step_wrapped(bottom_, delta, 4);
  ctx.redraw();
}

void PerformScreen::turn(app::Context& ctx, int delta, bool coarse) noexcept {
  dsp::Engine& e = ctx.engine();
  const unsigned mode = pl::mode_index(e.param(voice_, Vocoder::Mode));
  if (sfocus_ == 1u || sfocus_ == 2u) {  // SHIFT + F2 GATE, F3 ARP: the unit's enums, stepped round
    const std::uint16_t id = sfocus_ == 1u ? Vocoder::Gate : Vocoder::Arp;
    const unsigned count = sfocus_ == 1u ? pl::kGateCount : pl::kArpCount;
    (void)e.set_param(voice_, id, static_cast<float>(pl::step_wrapped(round_param(e, voice_, id), delta, count)));
    ctx.redraw();
    return;
  }
  if (sfocus_ == 3u) {  // SHIFT + F4 GLIDE: 10 ms a detent, SHIFT coarse 50
    (void)e.set_param(voice_, Vocoder::Glide, static_cast<float>(pl::glide_step(static_cast<int>(round_param(e, voice_, Vocoder::Glide)), delta, coarse)));
    ctx.redraw();
    return;
  }
  if (sfocus_ == 7u) {  // SHIFT + F8 PRESET: the sound changes at once, as the encoder turns
    preset_ = pl::preset_step(preset_, delta, kPresetCount);
    for (const PresetValue& pv : kPresets[preset_].v) {
      (void)e.set_param(voice_, pv.id, pv.value);
    }
    ctx.redraw();
    return;
  }
  if (sfocus_ == 4u) {  // SHIFT + F5 FORMANT: one semitone a detent, SHIFT coarse 4
    (void)e.set_param(voice_, Vocoder::Formant,
                      static_cast<float>(pl::formant_step(static_cast<int>(std::floor(e.param(voice_, Vocoder::Formant) + 0.5f)), delta, coarse)));
    ctx.redraw();
    return;
  }
  if (sfocus_ == 5u) {  // SHIFT + F6 THRU: 5 % a detent, written in the unit's 0..1
    const int pct = pl::thru_step(static_cast<int>(e.param(voice_, Vocoder::Thru) * 100.0f + 0.5f), delta, coarse);
    (void)e.set_param(voice_, Vocoder::Thru, static_cast<float>(pct) * 0.01f);
    ctx.redraw();
    return;
  }
  if (pick_ != 0u) {  // an open picker: the encoder moves its cursor
    build_picker(ctx);
    pick_cursor_ = static_cast<std::uint8_t>(pl::step_wrapped(pick_cursor_, delta, g_pd.pick.count != 0u ? g_pd.pick.count : 1u));
    g_pd.pick.cursor = pick_cursor_;
    ctx.redraw();
    return;
  }
  if (focus_ >= pl::kBendFocus) {  // the encoder is free: it bends
    if (delta != 0) {
      local_ = pl::bend_step(local_, delta, coarse, range_);
      spring_at_ = ctx.uptime_ms() + pl::kBendSpringMs;
      apply_bend(ctx);
    }
    ctx.redraw();
    return;
  }
  switch (focus_) {
    case 0:
      (void)e.set_param(voice_, Vocoder::Mode, static_cast<float>(pl::step_clamped(static_cast<int>(mode), delta, 0, 3)));
      break;
    case 1:
      if (delta != 0) {
        open_picker(ctx, route::Side::Speaks);
      }
      return;
    case 2:
      if (delta != 0) {
        open_picker(ctx, route::Side::Sounds);
      }
      return;
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
      if (pl::mode_page(mode) == 0u) {  // SPREAD: the detune, cents (BANDS and ROBOT)
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

// SHIFT + an F key: the SHIFT row (pl::kShiftKeys). FREEZE and BEND ROW are actions; the others take the encoder
// (a second tap steps the value on, as the plain row does).
void PerformScreen::shift_key(app::Context& ctx, unsigned key) noexcept {
  if (key >= 8u) {
    return;
  }
  switch (pl::kShiftKeys[key].fn) {
    case pl::ShiftFn::Freeze:
      (void)ctx.engine().set_param(voice_, Vocoder::Freeze, ctx.engine().param(voice_, Vocoder::Freeze) >= 0.5f ? 0.0f : 1.0f);
      ctx.redraw();
      break;
    case pl::ShiftFn::BendRow:  // the pads' notes end, the strip's and the encoder's bend return to 0
      toggle_strip(ctx);
      break;
    case pl::ShiftFn::Value:
      if (key == sfocus_) {
        turn(ctx, 1, false);
      } else {
        sfocus_ = key;
        ctx.redraw();
      }
      break;
  }
}

void PerformScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  unsigned k = 0;
  if (in.fkey_press(k)) {
    if (in.shift) {
      close_picker(ctx);
      shift_key(ctx, k);
    } else if (pick_ != 0u && (k == 0u || k == pick_)) {  // F1 or the picker's own key takes the cursor's row
      take_choice(ctx);
    } else if (pick_ != 0u && k == 7u) {  // F8 backs out
      close_picker(ctx);
    } else if (pick_ != 0u && (k == 1u || k == 2u)) {  // the other list
      open_picker(ctx, k == 1u ? route::Side::Speaks : route::Side::Sounds);
    } else if (pick_ == 0u && sfocus_ == pl::kNoShiftFocus && (k == 1u || k == 2u)) {  // F2 SPEAKS, F3 SOUNDS: a list, not a step
      open_picker(ctx, k == 1u ? route::Side::Speaks : route::Side::Sounds);
    } else if (sfocus_ != pl::kNoShiftFocus) {  // a plain tap goes back to the plain row, its key focused
      sfocus_ = pl::kNoShiftFocus;
      focus_ = k;
      ctx.redraw();
    } else if (k == focus_) {
      turn(ctx, 1, false);  // a second tap steps the value on
    } else {
      close_picker(ctx);
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
  } else if (in.press(Button::Rec)) {
    rec_press(ctx);
  } else if (in.press(Button::Up)) {
    if (pick_ != 0u) {
      turn(ctx, -1, false);
    } else {
      bottom_kind(ctx, 1);
    }
  } else if (in.press(Button::Down)) {
    if (pick_ != 0u) {
      turn(ctx, 1, false);
    } else {
      bottom_kind(ctx, -1);
    }
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

namespace {

// An F key's name: the skin's F row and HELP's caps. F8 is the mode's own value.
const char* fkey_name(unsigned key, unsigned mode) noexcept {
  static constexpr const char* kNames[] = {"MODE", "SPEAKS", "SOUNDS", "KEY", "SCALE", "CHORD", "PADS"};
  const unsigned page = pl::mode_page(mode);
  return key < 7u ? kNames[key] : (page == 0u ? "SPREAD" : (page == 1u ? "QUALITY" : "SPEED"));
}

}  // namespace

void PerformScreen::refresh_labels(const dsp::Engine& e) noexcept {
  const unsigned key = round_param(e, voice_, Vocoder::Key);
  const unsigned scale = round_param(e, voice_, Vocoder::Scale);
  const std::uint32_t sig = key | (scale << 4) | (static_cast<std::uint32_t>(octave_ + 8) << 8) | (bottom_ << 12) | (keys_ ? 1u << 16 : 0u) | (strip_ ? 1u << 17 : 0u);
  if (sig == label_sig_) {
    return;
  }
  label_sig_ = sig;
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    char (&out)[12] = g_pd.label[pad];
    out[0] = '\0';
    if (pl::on_strip(pad, strip_)) {
      char lbl[pl::kLabelChars + 1] = {};
      pl::strip_label(pl::strip_semitone(app::pad_col(pad)), lbl);
      append(out, lbl);
    } else if (keys_) {
      const pl::PianoPad pp = pl::piano_pad(app::pad_row(pad), app::pad_col(pad), strip_);
      const int note = pl::piano_note(pp, octave_);
      if (note >= 0) {
        char nm[8] = {};
        (void)ornoir::note_name(note, nm);
        append(out, nm);
      } else if (pp.fn == pl::PianoFn::Formant) {
        char lbl[pl::kLabelChars + 1] = {};
        pl::strip_label(pp.offset, lbl);
        append(out, lbl);
      } else if (pl::is_control(pp.fn)) {
        append(out, pl::kControlName[app::pad_col(pad)]);
      }
    } else {
      const Chord c = make_chord(pl::row_kind(app::pad_row(pad), static_cast<ChordKind>(bottom_), strip_), app::pad_col(pad), octave_, key, scale, Voicing::Close,
                                 nullptr);
      char lbl[pl::kLabelChars + 1] = {};
      pl::chord_label(c, lbl);
      append(out, lbl);
    }
  }
}

void PerformScreen::follow_bands(app::Context& ctx, std::uint8_t (&level)[16], std::uint8_t (&peak)[16]) noexcept {
  static_assert(ornoir::kBandCount == 16, "PARAMS' arrays are one per band");
  ++frame_;
  const bool busy = ctx.cpu_percent() > 60u && (frame_ & 1u) != 0u;
  if (!busy && voc_ != nullptr) {
    const Vocoder::View v = voc_->view();
    for (unsigned i = 0; i < ornoir::kBandCount; ++i) {
      follow(v.band[i], g_pd.level[i], g_pd.peak[i], g_pd.age[i]);
    }
  }
  std::memcpy(level, g_pd.level, sizeof level);
  std::memcpy(peak, g_pd.peak, sizeof peak);
}

void PerformScreen::follow_print(app::Context& ctx, ornoir::Voiceprint& vp) noexcept {
  const dsp::Engine& e = ctx.engine();
  const unsigned mode = pl::mode_index(e.param(voice_, Vocoder::Mode));
  if (voc_ == nullptr || (mode != 1u && mode != 2u)) {
    return;
  }
  const Vocoder::View v = voc_->view();
  if (mode == 1u) {
    vp.kind = ornoir::Print::Talk;
    vp.frozen = v.freeze;
    const std::uint64_t now = ctx.uptime_ms();
    if (now - g_pd.env_ms >= (ctx.cpu_percent() > 60u ? 80u : 40u)) {
      g_pd.env_ms = now;
      if (!g_pd.axis.ready) {
        voiceprint::axis_init(g_pd.axis);
      }
      float k[voiceprint::kMaxOrder];
      for (unsigned i = 0; i < voiceprint::kMaxOrder; ++i) {
        k[i] = static_cast<float>(v.refl[i]) * (1.0f / 32768.0f);
      }
      voiceprint::envelope(g_pd.axis, k, v.order, g_pd.env);
    }
    std::memcpy(vp.env, g_pd.env, sizeof vp.env);
  } else {
    const unsigned key = round_param(e, voice_, Vocoder::Key);
    vp.kind = ornoir::Print::Tune;
    g_pd.centre = static_cast<std::uint8_t>(voiceprint::ladder_follow(g_pd.centre, v.target_note));
    vp.centre = g_pd.centre;
    vp.target = v.target_note;
    vp.shift = v.shift_cents;
    vp.voiced = v.voiced;
    vp.key = static_cast<std::uint8_t>(key % 12u);
    vp.scale_mask = pl::scale_cols(key, round_param(e, voice_, Vocoder::Scale));
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

  const route::Route rt = route_of(e, voice_);
  const char* live = src != nullptr ? src->label : "NONE";
  const bool usb = src != nullptr && src->source == hal::capture::Source::Usb;
  ornoir::HeaderState h;
  h.speaks = rt.speaks >= route::kFirstSlot ? kSlotNames[(rt.speaks - route::kFirstSlot) & 3u] : (rt.speaks == route::kSpeaksLive ? route::live_name(live, usb, route::Side::Speaks) : "NONE");
  h.sounds = rt.sounds >= route::kFirstSlot && rt.sounds < route::kSoundsLive ? kSlotNames[(rt.sounds - route::kFirstSlot) & 3u]
                                                                              : (rt.sounds == route::kSoundsLive ? route::live_name(live, usb, route::Side::Sounds) : pl::kCarrierNames[car < 9u ? car : 0u]);
  h.mode = pl::kModeNames[mode];
  h.playing = e.playing();
  h.tempo_x10 = st.tempo_mbpm / 100u;
  h.cpu_percent = ctx.cpu_percent();

  ornoir::FKey keys[ornoir::kFKeys];
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    keys[i].name = fkey_name(i, mode);
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
  set(6, keys_ ? (strip_ ? "KEYS +B" : "KEYS") : (strip_ ? "CHORDS +B" : "CHORDS"));
  {
    char num[12] = {};
    if (pl::mode_page(mode) == 0u) {
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

  if (ctx.panel().shift() || sfocus_ != pl::kNoShiftFocus) {  // the SHIFT row replaces the plain one (names and values)
    for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
      const pl::ShiftKey& sk = pl::kShiftKeys[i];
      keys[i] = ornoir::FKey{};
      keys[i].name = sk.name;
      keys[i].hot = i == sfocus_;
      char num[12] = {};
      switch (sk.fn) {
        case pl::ShiftFn::Freeze:
          append(keys[i].value, e.param(voice_, Vocoder::Freeze) >= 0.5f ? "ON" : "OFF");
          break;
        case pl::ShiftFn::BendRow:
          append(keys[i].value, strip_ ? "ON" : "OFF");
          break;
        case pl::ShiftFn::Value:
          if (i == 1u || i == 2u) {
            const unsigned v = round_param(e, voice_, i == 1u ? Vocoder::Gate : Vocoder::Arp);
            const unsigned count = i == 1u ? pl::kGateCount : pl::kArpCount;
            append(keys[i].value, (i == 1u ? pl::kGateNames : pl::kArpNames)[v < count ? v : 0u]);
          } else if (i == 3u) {
            const unsigned ms = round_param(e, voice_, Vocoder::Glide);
            (void)pl::format_uint(ms, num);
            append(keys[i].value, num);
            append(keys[i].value, "MS");
            keys[i].pct = pl::bar_percent(static_cast<float>(ms), 0.0f, 500.0f);
          } else if (i == 7u) {
            append(keys[i].value, preset_ < kPresetCount ? kPresets[preset_].name : "---");
          } else if (i == 4u) {
            const int f = static_cast<int>(std::floor(e.param(voice_, Vocoder::Formant) + 0.5f));
            char lbl[pl::kLabelChars + 1] = {};
            pl::strip_label(f, lbl);
            append(keys[i].value, lbl);
            keys[i].pct = pl::bar_percent(static_cast<float>(f), -12.0f, 12.0f);
          } else {
            const unsigned pct = static_cast<unsigned>(e.param(voice_, Vocoder::Thru) * 100.0f + 0.5f);
            (void)pl::format_uint(pct, num);
            append(keys[i].value, num);
            keys[i].pct = static_cast<int>(pct > 100u ? 100u : pct);
          }
          break;
      }
    }
  }

  ornoir::Voiceprint vp;
  vp.bars = pl::mode_page(mode) == 0u;  // BANDS and ROBOT draw the bars
  if (!vp.bars) {
    follow_print(ctx, vp);  // TALK's envelope, TUNE's ladder
  }
  std::memcpy(vp.level, g_pd.level, sizeof vp.level);
  std::memcpy(vp.peak, g_pd.peak, sizeof vp.peak);

  ornoir::SpeaksState sp;
  sp.source = h.speaks;
  sp.gain = ain.available() ? ain.gain() : 0u;
  sp.in = {g_pd.in_level, g_pd.in_peak};
  sp.clipping = meter_ != nullptr && ain.available() && meter_->clipping();
  sp.live = live;
  char capture[40] = {};
  if (voc_ != nullptr) {
    for (unsigned i = 0; i < route::kSlots; ++i) {
      const Vocoder::LoopStatus ls = voc_->loop_status(i);
      const auto v = static_cast<std::uint8_t>(route::kFirstSlot + i);
      sp.slot[i] = ls.rec != Vocoder::LoopStatus::Rec::Idle ? 3u : (rt.speaks == v || rt.sounds == v ? 2u : (ls.length != 0u ? 1u : 0u));
    }
    if (cap_ != Cap::Idle) {
      cat(capture, sizeof capture, "REC: CAPTURE ");
      cat(capture, sizeof capture, live);
      cat(capture, sizeof capture, " > ");
      cat(capture, sizeof capture, kSlotNames[cap_slot_ & 3u]);
    } else if (cap_pre_) {
      cat(capture, sizeof capture, "REC > SLOT, THEN ");
      cat(capture, sizeof capture, kSources[cap_pre_src_ < kSourceCount ? cap_pre_src_ : 0u].label);
    }
  }
  sp.capture = capture;
  if (pick_ != 0u) {
    build_picker(ctx);
    sp.picker = pick_ == 1u ? &g_pd.pick : nullptr;
  }

  // The readout is the latched pad's chord, else the lowest held one.
  const std::uint64_t sounding = strip_ ? held_ & ((std::uint64_t{1} << (pl::kStripRow * app::kPadCols)) - 1u) : held_;  // the strip's pads sound nothing
  int shown = latched_;
  if (shown < 0 && sounding != 0u) {
    shown = 0;
    while (((sounding >> static_cast<unsigned>(shown)) & 1u) == 0u) {
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

  if (pick_ == 2u) {
    so.picker = &g_pd.pick;
  }
  ornoir::PadsView pads;
  pads.label = g_pd.label;
  pads.scale_cols = keys_ ? 0xFFFu : pl::scale_cols(key, scale);
  pads.column_in_scale_known = !keys_;
  pads.held = held_ | ctl_held_;
  pads.latched = latched_;
  if (!keys_) {
    for (unsigned row = 0; row < 4; ++row) {
      pads.row_name[row] = pl::kKindNames[static_cast<unsigned>(pl::row_kind(row, static_cast<ChordKind>(bottom_ & 3u), strip_))];
    }
  }
  if (strip_) {
    pads.row_name[pl::kStripRow] = "BEND";
  } else if (keys_) {
    pads.row_name[pl::kStripRow] = "FORMANT";
  }
  pads.key_hint = flash_[0] != '\0' ? flash_ : (pick_ != 0u ? "" : pl::key_hint(focus_, mode, sfocus_));
  if (pick_ != 0u) {
    pads.hint_left = "UP DOWN OR TURN: CURSOR   F1 OR SAME KEY: TAKE   F8: BACK";
  } else if (keys_) {
    pads.hint_left = strip_ ? "BOTTOM ROW BENDS  LEFT RIGHT OCTAVE" : "KEYS  LEFT RIGHT OCTAVE";
  } else {
    pads.hint_left = strip_ ? "BOTTOM ROW BENDS  SHIFT LATCH  UP DOWN KIND  LEFT RIGHT OCTAVE" : "SHIFT LATCH  DELETE RELEASE  UP DOWN ROW  LEFT RIGHT OCTAVE";
  }

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
  const int formant = static_cast<int>(std::floor(e.param(voice_, Vocoder::Formant) + 0.5f));
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    std::uint8_t level = app::kLedOff;
    if (pl::on_strip(pad, strip_)) {  // the middle pair (0) lit dim
      const unsigned col = app::pad_col(pad);
      level = pl::strip_semitone(col) == 0 ? app::kLedDim : app::kLedOff;
    } else if (keys_) {  // the piano: the key's root low, the scale dim, outside it dark; the controls dim
      const pl::PianoPad pp = pl::piano_pad(app::pad_row(pad), app::pad_col(pad), strip_);
      const int note = pl::piano_note(pp, octave_);
      if (note >= 0) {
        const unsigned pc = static_cast<unsigned>(note) % 12u;
        level = pc == key % 12u ? app::kLedLow : (((cols >> pc) & 1u) != 0u ? app::kLedDim : app::kLedOff);
      } else if (pp.fn == pl::PianoFn::OctDown) {
        level = clamp_octave(octave_ - 1) != octave_ ? app::kLedDim : app::kLedOff;
      } else if (pp.fn == pl::PianoFn::OctUp) {
        level = clamp_octave(octave_ + 1) != octave_ ? app::kLedDim : app::kLedOff;
      } else if (pp.fn == pl::PianoFn::VolDown) {
        level = e.param(voice_, Vocoder::Level) > kLevelMin ? app::kLedDim : app::kLedOff;
      } else if (pp.fn == pl::PianoFn::VolUp) {
        level = e.param(voice_, Vocoder::Level) < kLevelMax ? app::kLedDim : app::kLedOff;
      } else if (pp.fn == pl::PianoFn::Formant) {  // the pad of the value in force on, the others dim
        level = pl::formant_lit(formant, app::pad_col(pad)) ? app::kLedOn : app::kLedDim;
      } else if (pl::is_control(pp.fn)) {  // ROOT, BND, MOD: always dim
        level = app::kLedDim;
      }
    } else {
      const unsigned col = app::pad_col(pad);
      level = col == key % 12u ? app::kLedLow : (((cols >> col) & 1u) != 0u ? app::kLedDim : app::kLedOff);
      if (static_cast<int>(pad) == latched_) {
        level = app::kLedMid;
      }
    }
    if ((((held_ | ctl_held_) >> pad) & 1u) != 0u) {
      level = app::kLedOn;
    }
    leds.set(pad, level);
  }
}

// Chrome::None: the runtime draws no soft keys here. It still asks when HELP opens, and these are the names on HELP's F caps.
void PerformScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  const unsigned mode = pl::mode_index(ctx.engine().param(voice_, Vocoder::Mode));
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    out.label[i] = fkey_name(i, mode);
  }
}

// HELP (Shift + Config). A button with no entry draws as a hollow cap, which says it does nothing here: every button app.cpp's input() answers
// on this screen has a row, and tests/perform_screen_test.cpp holds that. The legend carries the groups of the page in force only.
void PerformScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Params, "PARAMS: THREE PAGES", "THE OLD PLAY SCREEN"},
      {Button::Performance, "AGAIN: FREES THE ENCODER", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER (SLICE U9)", nullptr},
      {Button::File, "FILE (SLICE U9)", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD (SLICE U8)", nullptr},
      {Button::SamplePlay, "SAMPLE: THE SLOTS", nullptr},
      {Button::SampleEdit, "SAMPLE EDIT: THE CUTS", nullptr},
      {Button::SampleRec, "SAMPLE REC (SLICE U4)", nullptr},
      {Button::Song, "SONG (SLICE U9)", nullptr},
      {Button::Pattern, "PATTERN: THE 16 STEPS", nullptr},
      {Button::Note, "PATTERN: NOTE LANE", nullptr},
      {Button::Instr, "PATTERN: CARRIER LANE", nullptr},
      {Button::Vol, "PATTERN: LEVEL LANE", nullptr},
      {Button::Fx, "PATTERN: SLICE LANE", nullptr},
      {Button::Rec, "CAPTURE LIVE INTO A SLOT", nullptr},
      {Button::Shift, "HOLD A PAD, TAP: LATCH CHORD", nullptr},
      {Button::Delete, "RELEASE THE LATCH", nullptr},
      {Button::Up, "BOTTOM ROW: NEXT KIND", nullptr},
      {Button::Down, "BOTTOM ROW: PREVIOUS KIND", nullptr},
      {Button::Left, "OCTAVE DOWN (HOLD: REPEAT)", nullptr},
      {Button::Right, "OCTAVE UP (HOLD: REPEAT)", nullptr},
  };
  static constexpr app::HelpPadGroup kChordGroups[] = {
      {"POWER", "ROOT, FIFTH, OCTAVE", ui::Color::Vol},
      {"OPEN", "ROOT, FIFTH, NINTH: SUS2", ui::Color::Instr},
      {"7TH", "THE SEVENTH CHORDS OF THE SCALE", ui::Color::Fx},
      {"BOTTOM ROW", "F6, UP OR DOWN CHOOSE ITS KIND", ui::Color::Play},
      {"PAD LEDS", "KEY COLUMN BRIGHTEST, SCALE DIM, OUTSIDE IT DARK", ui::Color::TextDim},
  };
  static constexpr app::HelpPadGroup kKeyGroups[] = {
      {"WHITE KEYS", "C TO G, THE CURRENT OCTAVE", ui::Color::Vol},
      {"BLACK KEYS", "THE SEMITONES; NO KEY ON A GAP", ui::Color::Fx},
      {"CONTROLS", "OCT VOL ROOT STEP; HOLD MOD VIBRATO, BND BEND", ui::Color::Instr},
      {"BEND ROW", "0 IN THE MIDDLE, +-1 A PAD; LIT DIM AT 0", ui::Color::Play},
      {"FORMANT", "A TAP SETS IT, -12 TO +12, AND IT STAYS; LIT ON", ui::Color::Play},
  };
  static constexpr const char* kKindText[4] = {"ROOT, THIRD, FIFTH OF THE SCALE", "THE SEVENTH CHORDS OF THE SCALE", "ROOT, FIFTH, NINTH: SUS2", "ROOT, FIFTH, OCTAVE"};
  static constexpr ui::Color kRowColor[3] = {ui::Color::Vol, ui::Color::Instr, ui::Color::Fx};
  const ChordKind bottom = static_cast<ChordKind>(bottom_ & 3u);
  if (strip_ && !keys_) {  // the rows moved up one: the legend names their kinds now, then the strip
    for (unsigned r = 0; r < 3; ++r) {
      const unsigned kind = static_cast<unsigned>(pl::row_kind(r, bottom, true));
      help_groups_[r] = {pl::kKindNames[kind], r == 2u ? "F6, UP OR DOWN CHOOSE ITS KIND" : kKindText[kind], kRowColor[r]};
    }
    help_groups_[3] = {"BEND ROW", "0 IN THE MIDDLE, +-1 A PAD; LIT DIM AT 0", ui::Color::Play};
    help_groups_[4] = {"PAD LEDS", "KEY COLUMN BRIGHTEST, SCALE DIM, OUTSIDE IT DARK", ui::Color::TextDim};
  }
  const unsigned mode = pl::mode_index(ctx.engine().param(voice_, Vocoder::Mode));
  h.summary = "SPEAKS > SOUNDS: THE AUDIO INPUT SPEAKS THROUGH THE CARRIER, PLAYED FROM THE PADS OR BY MIDI.\n"
              "F2 F3: A LIST (UP DOWN, F1 TAKES, F8 BACKS). OTHER F KEYS: A 2ND TAP STEPS. SHIFT: MORE.";
  h.fkey[0] = "BANDS, TALK, TUNE, ROBOT";
  h.fkey[1] = "A LIST OF WHAT TALKS";
  h.fkey[2] = "SYNTH, LOOPS, VOICE, LIVE";
  h.fkey[3] = "THE SCALE'S ROOT";
  h.fkey[4] = "SCALE OF THE CHORDS";
  h.fkey[5] = "BOTTOM ROW KIND";
  h.fkey[6] = "CHORDS OR KEYS";
  for (unsigned i = 0; i < 8; ++i) {  // the SHIFT row
    h.fkey_shift[i] = pl::kShiftKeys[i].name;
  }
  h.fkey[7] = pl::mode_page(mode) == 0u ? "DETUNE, CENTS" : (mode == 1u ? "LPC ORDER" : "MS: 0 IS STEPPED");
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "VALUE OR BEND (SHIFT COARSE)";
  if (keys_) {
    h.groups = kKeyGroups;  // the fourth is the strip's (BEND ROW on) or the formant's (off): the row 3 pads are group 4 either way
    h.group_count = 4u;
    if (!strip_) {
      help_groups_[0] = kKeyGroups[0];
      help_groups_[1] = kKeyGroups[1];
      help_groups_[2] = kKeyGroups[2];
      help_groups_[3] = kKeyGroups[4];
      h.groups = help_groups_;
    }
  } else if (strip_) {
    h.groups = help_groups_;
    h.group_count = 5;
  } else {
    h.groups = kChordGroups;
    h.group_count = sizeof kChordGroups / sizeof kChordGroups[0];
  }
  if (keys_) {
    h.pads = strip_ ? "A PIANO: WHITE KEYS, BLACK ABOVE, CONTROLS ON TOP. THE BOTTOM ROW BENDS: PRESS, RELEASE TO 0."
                    : "A PIANO: WHITE KEYS, BLACK ABOVE, THE CONTROLS ON TOP. NO LATCH HERE.";
  } else {
    h.pads = strip_ ? "COLUMN: ROOT. BOTTOM ROW BENDS: PRESS, RELEASE TO 0. THE CHORD ROWS ARE ABOVE IT."
                    : "COLUMN: ROOT. ROW: KIND. THE QUALITY COMES FROM KEY AND SCALE; - IS MINOR.";
  }
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    if (pl::on_strip(pad, strip_)) {  // the strip, whichever page: -5 .. +5, the pair at 0
      char lbl[pl::kLabelChars + 1] = {};
      pl::strip_label(pl::strip_semitone(app::pad_col(pad)), lbl);
      std::size_t n = 0;
      while (lbl[n] != '\0' && n < app::kHelpPadLabel) {
        g_help_names[pad][n] = lbl[n];
        ++n;
      }
      g_help_names[pad][n] = '\0';
      h.pad_label[pad] = g_help_names[pad];
      h.pad_group[pad] = 4;
    } else if (keys_) {
      const pl::PianoPad pp = pl::piano_pad(app::pad_row(pad), app::pad_col(pad), strip_);
      const int note = pl::piano_note(pp, octave_);
      if (note >= 0) {
        h.pad_group[pad] = pp.fn == pl::PianoFn::White ? 1 : 2;
        (void)app::help_pad_note(h, g_help_names, pad, note);
      } else if (pp.fn == pl::PianoFn::Formant) {
        char lbl[pl::kLabelChars + 1] = {};
        pl::strip_label(pp.offset, lbl);
        std::size_t n = 0;
        while (lbl[n] != '\0' && n < app::kHelpPadLabel) {
          g_help_names[pad][n] = lbl[n];
          ++n;
        }
        g_help_names[pad][n] = '\0';
        h.pad_label[pad] = g_help_names[pad];
        h.pad_group[pad] = 4;
      } else if (pp.fn != pl::PianoFn::None) {
        const char* nm = pl::kControlName[app::pad_col(pad)];
        std::size_t n = 0;
        while (nm[n] != '\0') {
          g_help_names[pad][n] = nm[n];
          ++n;
        }
        g_help_names[pad][n] = '\0';
        h.pad_label[pad] = g_help_names[pad];
        h.pad_group[pad] = 3;
      }
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
        g_help_names[pad][n] = ch;
        ++n;
      }
      g_help_names[pad][n] = '\0';
      h.pad_label[pad] = g_help_names[pad];
    }
  }
}

}  // namespace o3_voc03er
