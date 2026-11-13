// SPDX-License-Identifier: GPL-3.0-or-later
#include "sample_edit_screen.h"

#include "overview.h"
#include "perform_logic.h"
#include "sample_edit_logic.h"
#include "skin/ornoir.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/placement.h>

#include <algorithm>
#include <cstddef>
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
constexpr std::uint8_t kFocusBeats = 2;   // the encoder steps F2 (CUT BEATS)
constexpr std::uint8_t kFocusHits = 5;    // ... F3 (CUT HITS)
constexpr std::uint8_t kFocusStart = 3;   // the encoder moves the trim's START bracket (F4)
constexpr std::uint8_t kFocusEnd = 4;     // ... its END bracket (F5)
constexpr std::uint8_t kJobNone = 0;      // g_se.job_kind
constexpr std::uint8_t kJobTrim = 1;
constexpr std::uint8_t kJobCopy = 2;
constexpr std::uint32_t kJobMs = 1000;    // how long a job keeps sending Load while the unit has not adopted the length
constexpr std::uint32_t kResendMs = 100;
static_assert(se::kTrimFade == Loop::kFadeSamples, "the trim's seam is the unit's own fade");

constexpr const char* kSlotNames[4] = {"SLOT 1", "SLOT 2", "SLOT 3", "SLOT 4"};
constexpr const char* kKeyNames[ornoir::kFKeys] = {"CUT EVEN", "BEATS", "HITS", "START", "END", "NORMALISE", "SAVE WAV", "SPEAKS"};
constexpr unsigned kKeySpeaks = 7;  // F8: SPEAKS, and SHIFT + F8 clears the cuts

// The screen's state, in the SDRAM (O3SDK_BIG: zero at start, main loop only), so the screen object is the vtable and two pointers.
struct EditData {
  std::uint32_t cursor[sm::kReaders];  // per slot, in samples
  se::Arm arm;                         // the REPLACES warning
  std::uint8_t focus;                  // kFocusCursor, kFocusCut or kFocusBeats
  std::uint8_t beats[sm::kReaders];    // per slot, F2's value (se::beats_per_bar's code); valid while beats_len[slot] is the slot's length
  std::uint32_t beats_len[sm::kReaders];
  std::uint8_t hits[sm::kReaders];     // per slot, F3's value (0 off, 1..100); valid while hits_len[slot] is the slot's length (the last one tried, also after NO HITS)
  std::uint32_t hits_len[sm::kReaders];
  se::HopScan scan;                    // the hop-energy scan for F3 (main loop: a slice a pass from update())
  bool saving;                         // F7's job runs: the slot's memory must stay as it is
  std::uint8_t scan_slot, scan_sens;   // the slot being scanned and the sensitivity to cut with when it is done
  std::uint32_t hop_len[sm::kReaders]; // per slot, the length the energies below were scanned for; 0 stale (a take, NORMALISE, a trim or a copy make them so)
  alignas(4) std::uint16_t hop[sm::kReaders][se::kMaxHops];  // the hop energies, 4 x 2,756 x 2 B = 22 KB
  se::Pending pending;                 // F6 or SHIFT + DELETE asked, F1 confirms (any other input cancels)
  se::Norm norm;                       // the NORMALISE run (main loop: a slice a pass from update())
  std::uint8_t norm_slot;
  se::Brackets br[sm::kReaders];       // the trim's brackets per slot; valid while br_len[slot] is the slot's length (else {0, length}, see fit_brackets)
  std::uint32_t br_len[sm::kReaders];
  se::Job job;                         // the TRIM or COPY move (main loop: a slice a pass from update(), then Load until the unit adopts the length)
  std::uint8_t job_kind;               // JobKind: 0 none, 1 trim, 2 copy
  std::uint8_t job_src, job_dst;       // slots (a trim has both the same)
  std::uint8_t job_edited, job_even;   // a copy's flags for the target
  bool job_sent;                       // the move is done and Load has gone
  std::uint32_t job_start;             // a trim's START
  std::uint32_t job_len;               // the length the target slot must show for the job to be over
  std::uint64_t job_deadline, job_resend;
  sm::SliceTable job_table;            // the cuts the target gets once the length is adopted (the unit's own length change would reset them)
  bool key_tap;                        // turn() came from a second tap on F1
  const char* note;                    // a moment's line (a static string or note_buf), until note_until
  std::uint64_t note_until;
  char note_buf[48];
  std::uint8_t sel1;                   // SE1: the slice picked on its pad, plus one (0 none); valid while sel_slot and sel_len are the table's (see selected())
  std::uint8_t sel_slot;
  std::uint32_t sel_len;
  std::uint8_t warn_key1;              // a REPLACES line is up for this F key, plus one (0 none): that key says CONFIRM until the arm closes or the line is answered
  bool zoom_ok;                        // the zoom columns below are for zoom_slot, zoom_lo, zoom_span and zoom_len (any change of the samples clears it)
  std::uint8_t zoom_slot;
  std::uint32_t zoom_lo, zoom_span;    // the window they were built for
  std::uint32_t zoom_len;
  std::uint8_t zoom_rung;              // SE2: the zoom ladder's rung (se::kZoomRung), 0 the whole slot; the view's rung for every slot
  std::uint8_t latch_pad;              // the latched lower pad (24..47), 0 none: SHIFT + a lower pad holds its note until released (main loop only, the audition is PERFORM's)
  std::uint32_t swallow;               // bit (pad - 24): the physical release of a pad the latch took over is held back
  std::uint32_t lower;                 // bit (pad - 24): a lower pad pressed plain whose note sounds (the hint stays quiet while one is down)
  std::uint8_t last_mode;              // PERFORM's pad mode when the latch was set: a change of mode releases it
  alignas(4) std::int16_t zoom_mn[se::kZoomCols];  // the zoomed picture's columns (lowest and highest sample of each): 2 x 186 x 2 B
  alignas(4) std::int16_t zoom_mx[se::kZoomCols];
};
static_assert(offsetof(EditData, hop) % 4u == 0u, "the hop energies are word aligned");
static_assert(offsetof(EditData, zoom_mn) % 4u == 0u && offsetof(EditData, zoom_mx) % 4u == 0u && sizeof(EditData::zoom_mn) % 4u == 0u, "the zoom columns are word aligned");
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
constexpr int kWaveY = 140;  // the rectangle the overview picture sits in
constexpr int kWaveH = 176;
constexpr int kPicX = se::kPicX;  // the overview picture spans the rectangle inside its 2 px border: 744 x 96, centred vertically
constexpr int kPicY = kWaveY + (kWaveH - static_cast<int>(overview::kBigRows)) / 2;
constexpr int kPicH = static_cast<int>(overview::kBigRows);
static_assert(kPicX == kX + 2 && kPicX + static_cast<int>(se::kPicW) == kX + kW - 2, "the picture fills the rectangle less its border");
constexpr int kBarY = 330;
constexpr int kInfoY = 124;  // the picked slice line and the mode cue, between the header and the picture
constexpr int kBarH = 14;

}  // namespace

void SampleEditScreen::init(SampleScreen* sample, std::int16_t* mem, PerformScreen* perform) noexcept {
  sample_ = sample;
  mem_ = mem;
  perform_ = perform;
  overview::shared().bind(mem, Loop::kMaxSamples);
  g_se = EditData{};
}

const ui::Theme* SampleEditScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

void SampleEditScreen::enter(app::Context& ctx) noexcept {
  se::arm_cancel(g_se.arm);
  g_se.scan = se::HopScan{};
  for (unsigned i = 0; i < sm::kReaders; ++i) {
    g_se.hop_len[i] = 0;  // a take may have changed any slot while this screen was away
  }
  g_se.focus = kFocusCursor;
  g_se.pending = se::Pending::None;
  g_se.note = nullptr;
  g_se.sel1 = 0;
  g_se.warn_key1 = 0;
  g_se.zoom_ok = false;
  g_se.zoom_rung = 0;  // WHOLE SLOT on every visit
  g_se.latch_pad = 0;  // the notes were released when the screen left (or never sounded)
  g_se.swallow = 0;
  g_se.lower = 0;
  ctx.redraw();
}

void SampleEditScreen::leave(app::Context& ctx) noexcept {
  g_se.pending = se::Pending::None;
  g_se.sel1 = 0;
  g_se.latch_pad = 0;  // pad_leave below releases every audition note, the latched one with them
  g_se.swallow = 0;
  g_se.lower = 0;
  g_se.scan = se::HopScan{};  // reads only: stopping it costs nothing but the pass
  if (g_se.saving) {  // the job reads the slot's memory and the other screens write it: the card keeps no half file
    ctx.files().cancel();
    g_se.saving = false;
  }
  if (g_se.norm.phase == se::NormPhase::Scan || g_se.norm.phase == se::NormPhase::Apply) {  // never leave a slot half scaled: finish now (at most 173 slices)
    while (g_se.norm.phase == se::NormPhase::Scan || g_se.norm.phase == se::NormPhase::Apply) {
      norm_tick(ctx);
    }
  }
  if (g_se.job_kind != kJobNone) {  // the move ends now (at most 87 slices) and Load goes; the unit adopts it by itself and SAMPLE's service() makes even cuts
    while (!g_se.job_sent && g_se.job_kind != kJobNone) {
      job_tick(ctx);
    }
    g_se.job_kind = kJobNone;
  }
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

void SampleEditScreen::slot_replaced(unsigned slot) noexcept {
  slot &= 3u;
  g_se.beats_len[slot] = 0;
  g_se.hits_len[slot] = 0;
  g_se.br_len[slot] = 0;
  g_se.hop_len[slot] = 0;
  g_se.cursor[slot] = 0;
  g_se.beats[slot] = 0;
  g_se.sel1 = 0;
  g_se.zoom_ok = false;
}

bool SampleEditScreen::trim_brackets(unsigned slot, std::uint32_t length, std::uint32_t& start, std::uint32_t& end) noexcept {
  slot &= 3u;
  start = 0;
  end = 0;
  if (length == 0u || g_se.br_len[slot] != length || !se::trim_moved(g_se.br[slot], length)) {
    return false;
  }
  start = g_se.br[slot].start;
  end = g_se.br[slot].end;
  return true;
}

bool SampleEditScreen::set_trim_brackets(unsigned slot, std::uint32_t length, std::uint32_t start, std::uint32_t end) noexcept {
  slot &= 3u;
  if (start == 0u && end == 0u) {
    g_se.br_len[slot] = 0;  // not moved: the brackets start from the slot's own ends again when the screen next needs them
    return true;
  }
  if (length < 2u * se::kMinCutSamples || start >= end || end > length || end - start < se::kMinCutSamples) {
    return false;
  }
  g_se.br[slot] = se::Brackets{start, end};
  g_se.br_len[slot] = length;
  return true;
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
static void table_changed(SampleScreen& s, unsigned slot, bool edited, unsigned code, bool keep_sel = false) noexcept {
  if (!keep_sel) {
    g_se.sel1 = 0;  // the slices were renumbered or replaced: back to the overview
  }
  s.set_edited(slot, edited);
  s.set_even_code(slot, code);
  s.cuts_changed();
  g_se.beats[slot & 3u] = 0;  // F2's own call sets it again after this
  g_se.scan_sens = 0;         // a scan still running keeps its energies, but its cuts must not replace this table
  g_se.hits[slot & 3u] = 0;   // so does F3's
}

// A recut key (F1..F3, SHIFT + F8) met a table the hand edited: the line says what the key will replace, and the key reads CONFIRM until it is pressed again.
void SampleEditScreen::warn_replaces(app::Context& ctx, unsigned key, unsigned count) noexcept {
  g_se.note_buf[0] = '\0';
  cat(g_se.note_buf, sizeof g_se.note_buf, "REPLACES ");
  cat_uint(g_se.note_buf, sizeof g_se.note_buf, count);
  cat(g_se.note_buf, sizeof g_se.note_buf, key == 7u ? " CUTS: SHIFT + F8 AGAIN" : " CUTS: PRESS AGAIN");
  g_se.warn_key1 = static_cast<std::uint8_t>(key + 1u);
  say(ctx, g_se.note_buf);
}

// "7 SLICES" / "1 SLICE" onto note_buf (after what is there).
static void cat_slices(unsigned n) noexcept {
  cat_uint(g_se.note_buf, sizeof g_se.note_buf, n);
  cat(g_se.note_buf, sizeof g_se.note_buf, n == 1u ? " SLICE" : " SLICES");
}

static std::uint32_t slice_start(const sm::SliceTable& t, unsigned slot, unsigned i) noexcept;  // below, with the pick

// "CUT 5 ADDED: 8 SLICES" / "CUT 5 REMOVED: 7 SLICES" onto the line.
static void say_cut(const char* what, unsigned cut, unsigned slices) noexcept {
  g_se.note_buf[0] = '\0';
  cat(g_se.note_buf, sizeof g_se.note_buf, "CUT ");
  cat_uint(g_se.note_buf, sizeof g_se.note_buf, cut);
  cat(g_se.note_buf, sizeof g_se.note_buf, what);
  cat_slices(slices);
}

// A cut at the cursor; with a slice picked, a cut in the middle of that slice (the encoder then moves the new start at once: the second half is the pick).
void SampleEditScreen::insert(app::Context& ctx) noexcept {
  if (refuse(ctx, true)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  sm::SliceTable& t = sample_->table(slot);
  const std::int16_t* s = samples();
  const unsigned sel = selected();
  if (sel != 0u) {
    fit_brackets(slot);
    const std::uint32_t at = se::slice_middle(t, sel - 1u, slice_start(t, slot, sel - 1u), s);
    const se::Edit r = at != 0u ? se::insert_cut(t, at) : se::Edit{false, "SLICE TOO SHORT TO SPLIT", 0u};
    if (!r.ok) {
      say(ctx, r.why);
      return;
    }
    table_changed(*sample_, slot, true, 3u, true);
    select_slice(ctx, sel);  // the new second half (its cut index is `sel`)
    say_cut(" ADDED: ", sel + 1u, t.count);
    say(ctx, g_se.note_buf);
    return;
  }
  const std::uint32_t pos = cursor();
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

// DELETE: a picked slice's start cut goes (slice 1's cannot: it is the slot's start) and the pick moves to the slice that swallowed it; with nothing picked, the
// cut nearest the cursor inside the view.
void SampleEditScreen::remove(app::Context& ctx) noexcept {
  if (refuse(ctx, true)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  sm::SliceTable& t = sample_->table(slot);
  const unsigned sel = selected();
  unsigned idx = 0;
  if (sel != 0u) {
    if (sel == 1u) {
      say(ctx, "SLICE 1 CANNOT GO");
      return;
    }
    idx = sel - 1u;
  } else {
    const std::uint32_t lo = view_lo();
    const int n = t.count > 1u ? se::nearest_cut(t, cursor(), lo, lo + view_span()) : -1;
    if (n < 0) {
      say(ctx, t.count > 1u ? "NO CUT IN VIEW" : "NO CUT TO REMOVE");
      return;
    }
    idx = static_cast<unsigned>(n);
  }
  const se::Edit r = se::delete_cut(t, t.cut[idx].start);
  if (!r.ok) {
    say(ctx, r.why);
    return;
  }
  table_changed(*sample_, slot, true, 3u, sel != 0u);
  if (sel != 0u) {
    select_slice(ctx, idx - 1u);
  }
  say_cut(" REMOVED: ", idx + 1u, t.count);
  say(ctx, g_se.note_buf);
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
    warn_replaces(ctx, 0u, t.count);
    return;
  }
  g_se.warn_key1 = 0;
  sm::even_cuts(t, t.length, n);
  table_changed(*sample_, slot, true, code);
  g_se.note = nullptr;  // the warning has been answered
  ctx.redraw();
}

// The F2 value the slot shows: OFF unless its cuts are the ones F2 made at this length.
static unsigned shown_beats(SampleScreen& s, unsigned slot) noexcept {
  return g_se.beats_len[slot] == s.table(slot).length && s.table(slot).length != 0u ? g_se.beats[slot] : 0u;
}

bool SampleEditScreen::beats_dim(const app::Context& ctx) const noexcept {
  if (sample_ == nullptr) {
    return true;
  }
  const std::uint32_t len = sample_->table(sample_->slot()).length;
  return len == 0u || !se::is_take(len, perform::bar_samples(ctx.engine().stats().tempo_mbpm));
}

// F2's value `code` (se::beats_per_bar): OFF only leaves the cuts as they are. Same rules as F1: a table the hand edited is replaced on the second try.
void SampleEditScreen::recut_beats(app::Context& ctx, unsigned code) noexcept {
  if (refuse(ctx, true)) {
    return;
  }
  if (beats_dim(ctx)) {
    say(ctx, "NOT A TAKE: WHOLE BARS ONLY");
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  if (code == shown_beats(*sample_, slot)) {
    return;
  }
  if (code == 0u) {
    g_se.beats[slot] = 0;
    ctx.redraw();
    return;
  }
  sm::SliceTable& t = sample_->table(slot);
  if (se::arm_step(g_se.arm, 1u, sample_->edited(slot)) == se::Gate::Warn) {
    warn_replaces(ctx, 1u, t.count);
    return;
  }
  g_se.warn_key1 = 0;
  const se::BeatCut r = se::beat_cuts(t, t.length, perform::bar_samples(ctx.engine().stats().tempo_mbpm), se::beats_per_bar(code));
  if (r.count == 0u) {
    say(ctx, "BEAT TOO SHORT");
    return;
  }
  table_changed(*sample_, slot, true, 3u);
  g_se.beats[slot] = static_cast<std::uint8_t>(code);
  g_se.beats_len[slot] = t.length;
  g_se.note = nullptr;  // the warning has been answered
  if (r.capped) {
    say(ctx, "CAPPED AT 24 CUTS");
  }
  ctx.redraw();
}

// F2's step: `delta` is the encoder's detents (or +1 for a second tap, which wraps).
void SampleEditScreen::turn_beats(app::Context& ctx, int delta) noexcept {
  recut_beats(ctx, se::step_beats(shown_beats(*sample_, sample_->slot() & 3u), delta, g_se.key_tap));
}

// ---- CUT HITS (F3) ---------------------------------------------------------------------------------------------------------------------

// The F3 value the slot shows: 0 unless it was set for this length (any re-cut by another key or by hand reads 0, see table_changed).
static unsigned shown_hits(SampleScreen& s, unsigned slot) noexcept {
  return g_se.hits_len[slot] == s.table(slot).length && s.table(slot).length != 0u ? g_se.hits[slot] : 0u;
}

// The table from the stored energies at `sens`. NO HITS leaves the table as it was; the value stays the one tried, so the encoder goes on from it.
void SampleEditScreen::cut_hits(app::Context& ctx, unsigned slot, unsigned sens) noexcept {
  sm::SliceTable& t = sample_->table(slot);
  const se::HitCut r = se::hit_cuts(t, g_se.hop[slot], se::hop_count(t.length), t.length, sens);
  if (r.hits == 0u) {
    g_se.hits[slot] = static_cast<std::uint8_t>(sens);
    g_se.hits_len[slot] = t.length;
    say(ctx, "NO HITS");
    return;
  }
  table_changed(*sample_, slot, true, 3u);
  g_se.hits[slot] = static_cast<std::uint8_t>(sens);
  g_se.hits_len[slot] = t.length;
  g_se.note = nullptr;  // the warning has been answered
  if (r.capped) {
    say(ctx, "CAPPED AT 24 CUTS");
  }
  ctx.redraw();
}

// F3's value `code` (0..100): 0 only leaves the cuts as they are. Same rules as F1 and F2: a table the hand edited is replaced on the second try. The
// energies are scanned once per slot length (a slice a pass, then the cuts follow); a turn that finds them stored re-cuts at once.
void SampleEditScreen::recut_hits(app::Context& ctx, unsigned code) noexcept {
  if (refuse(ctx, true) || busy(ctx)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  if (code == shown_hits(*sample_, slot)) {
    return;
  }
  if (code == 0u) {
    g_se.hits[slot] = 0;
    ctx.redraw();
    return;
  }
  if (mem_ == nullptr) {
    return;
  }
  const sm::SliceTable& t = sample_->table(slot);
  if (se::arm_step(g_se.arm, 2u, sample_->edited(slot)) == se::Gate::Warn) {
    warn_replaces(ctx, 2u, t.count);
    return;
  }
  g_se.warn_key1 = 0;
  if (g_se.hop_len[slot] != t.length) {
    se::hop_scan_begin(g_se.scan, t.length);
    g_se.scan_slot = static_cast<std::uint8_t>(slot);
    g_se.scan_sens = static_cast<std::uint8_t>(code);
    g_se.note = nullptr;
    ctx.redraw();
    return;
  }
  cut_hits(ctx, slot, code);
}

// F3's step: `delta` is the encoder's detents (or +1 for a second tap, which steps by 10 and wraps).
void SampleEditScreen::turn_hits(app::Context& ctx, int delta) noexcept {
  recut_hits(ctx, se::step_hits(shown_hits(*sample_, sample_->slot() & 3u), delta, g_se.key_tap));
}

// One slice of the scan; the cuts follow when it ends. It ends early when the slot changed under it (a take, a trim).
void SampleEditScreen::scan_tick(app::Context& ctx) noexcept {
  const unsigned slot = g_se.scan_slot & 3u;
  const std::uint32_t len = sample_->table(slot).length;
  if (mem_ == nullptr || sample_->status(slot).rec != Vocoder::LoopStatus::Rec::Idle || std::min(len, se::kMaxHops * se::kHop) != g_se.scan.end) {
    g_se.scan = se::HopScan{};
    say(ctx, "SCAN CANCELLED");
    return;
  }
  if (se::hop_scan_step(g_se.scan, mem_ + static_cast<std::size_t>(slot) * Loop::kMaxSamples, g_se.hop[slot])) {
    g_se.hop_len[slot] = len;
    if (slot == (sample_->slot() & 3u) && g_se.scan_sens != 0u) {  // 0: the table changed under the scan, the energies stay and no cut does
      cut_hits(ctx, slot, g_se.scan_sens);
    }
  }
  ctx.redraw();
}

// F7: the slot as it stands, all of it, as Samples/slotN_NNN.wav (the first free NNN). The memory is already 16-bit mono at the unit's rate, so the kit's job reads it
// where it is; busy() keeps every edit off it until the job ends. Progress and the result are the kit's strip.
void SampleEditScreen::save_wav(app::Context& ctx) noexcept {
  if (busy(ctx) || refuse(ctx, true) || mem_ == nullptr) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  char stem[6] = {'s', 'l', 'o', 't', static_cast<char>('1' + slot), '\0'};
  g_se.saving = ctx.files().save_wav("Samples", mem_ + static_cast<std::size_t>(slot) * Loop::kMaxSamples, sample_->table(slot).length, 1u, 44100u, stem);
  ctx.redraw();
}

// F6: asks; F1 confirms. SHIFT + DELETE below.
void SampleEditScreen::ask_normalise(app::Context& ctx) noexcept {
  if (busy(ctx)) {
    return;
  }
  if (refuse(ctx, true)) {
    return;
  }
  g_se.pending = se::Pending::Normalise;
  say(ctx, "NORMALISE TO -1 DB: F1 CONFIRMS (NO UNDO)");
}

void SampleEditScreen::ask_empty(app::Context& ctx) noexcept {
  if (busy(ctx) || refuse(ctx, true)) {
    return;
  }
  g_se.pending = se::Pending::Empty;
  say(ctx, "EMPTY THE SLOT? F1 CONFIRMS");
}

// F1 while something waits: takes it.
void SampleEditScreen::confirm(app::Context& ctx) noexcept {
  const se::Pending what = g_se.pending;
  g_se.pending = se::Pending::None;
  if (busy(ctx) || refuse(ctx, true)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  if (what == se::Pending::Copy) {
    run_copy(ctx);
  } else if (what == se::Pending::Normalise) {
    if (mem_ == nullptr) {
      return;
    }
    se::norm_begin(g_se.norm, sample_->table(slot).length);
    g_se.hop_len[slot] = 0;  // the samples are about to change
    g_se.norm_slot = static_cast<std::uint8_t>(slot);
    g_se.note = nullptr;
    ctx.redraw();
  } else if (what == se::Pending::Empty) {
    const dsp::UnitId v = sample_->voice();  // the existing Rec CLEAR (value 2), then back to OFF; the table follows the length (SampleScreen::service)
    (void)ctx.engine().set_param(v, Vocoder::loop_param(slot, Vocoder::LoopField::Play), 0.0f);
    (void)ctx.engine().set_param(v, Vocoder::loop_param(slot, Vocoder::LoopField::Rec), 2.0f);
    (void)ctx.engine().set_param(v, Vocoder::loop_param(slot, Vocoder::LoopField::Rec), 0.0f);
    g_se.cursor[slot] = 0;
    g_se.beats[slot] = 0;
    g_se.hop_len[slot] = 0;
    say(ctx, "SLOT EMPTIED");
  }
}

// One slice of the NORMALISE run; ends it when the slot changed under it (a take) or when the run is over.
void SampleEditScreen::norm_tick(app::Context& ctx) noexcept {
  const unsigned slot = g_se.norm_slot & 3u;
  if (mem_ == nullptr || sample_->table(slot).length != g_se.norm.len ||
      sample_->status(slot).rec != Vocoder::LoopStatus::Rec::Idle) {
    g_se.norm = se::Norm{};
    say(ctx, "NORMALISE CANCELLED");
    return;
  }
  if (se::norm_step(g_se.norm, mem_ + static_cast<std::size_t>(slot) * Loop::kMaxSamples)) {
    norm_finish(ctx);
  }
  ctx.redraw();
}

void SampleEditScreen::norm_finish(app::Context& ctx) noexcept {
  if (g_se.norm.phase == se::NormPhase::Silent) {
    g_se.norm = se::Norm{};
    say(ctx, "SILENT");
    return;
  }
  const int db = se::gain_tenths_db(g_se.norm.gain);
  const unsigned a = static_cast<unsigned>(db < 0 ? -db : db);
  g_se.note_buf[0] = '\0';
  cat(g_se.note_buf, sizeof g_se.note_buf, "NORMALISED ");
  cat(g_se.note_buf, sizeof g_se.note_buf, db < 0 ? "-" : "+");
  cat_uint(g_se.note_buf, sizeof g_se.note_buf, a / 10u);
  cat(g_se.note_buf, sizeof g_se.note_buf, ".");
  cat_uint(g_se.note_buf, sizeof g_se.note_buf, a % 10u);
  cat(g_se.note_buf, sizeof g_se.note_buf, " DB");
  g_se.norm = se::Norm{};
  g_se.hop_len[g_se.norm_slot & 3u] = 0;
  overview::shared().mark_dirty(g_se.norm_slot);  // the samples changed in place: the picture is built again
  g_se.zoom_ok = false;
  say(ctx, g_se.note_buf);
}

// ---- TRIM (F4, F5) and COPY: a bounded slice a pass, then Load until the unit adopts the length ------------------------------------------------

// True (and says why) while NORMALISE or a job owns the samples.
bool SampleEditScreen::busy(app::Context& ctx) noexcept {
  // FILE's open is still landing WAVs: it writes the slots and the unit's one Load parameter, as a trim or a copy would, and a slot it waits for
  // looks empty (a copy would pick it as its target); every edit waits until the last WAV has landed.
  for (unsigned k = 0; k < sm::kReaders; ++k) {
    if (sample_->waiting(k)) {
      say(ctx, "LOADING");
      return true;
    }
  }
  if (g_se.norm.phase == se::NormPhase::Scan || g_se.norm.phase == se::NormPhase::Apply) {
    say(ctx, "NORMALISING");
    return true;
  }
  if (g_se.job_kind != kJobNone) {
    say(ctx, g_se.job_kind == kJobTrim ? "TRIMMING" : "COPYING");
    return true;
  }
  if (g_se.saving) {
    say(ctx, "SAVING");
    return true;
  }
  if (g_se.scan.on) {
    say(ctx, "SCANNING");
    return true;
  }
  return false;
}

// The slot's brackets, {0, length} when the length is not the one they were set for.
void SampleEditScreen::fit_brackets(unsigned slot) noexcept {
  const std::uint32_t len = sample_->table(slot).length;
  if (g_se.br_len[slot] != len) {
    g_se.br[slot] = se::Brackets{0u, len};
    g_se.br_len[slot] = len;
  }
}

// A slot the trim may rewrite: nothing plays it, waits to, or sounds a region on any reader (a slice may borrow one).
bool SampleEditScreen::slot_stopped(unsigned slot) const noexcept {
  const Vocoder::LoopStatus st = sample_->status(slot);
  if (st.playing || st.pending) {
    return false;
  }
  for (unsigned k = 0; k < sm::kReaders; ++k) {
    if (sample_->status(k).region) {
      return false;
    }
  }
  return true;
}

void SampleEditScreen::send_load(app::Context& ctx) noexcept {
  const float v = static_cast<float>(static_cast<std::uint32_t>(g_se.job_dst) * Loop::kMaxSamples + g_se.job_len);
  (void)ctx.engine().set_param(sample_->voice(), Vocoder::Load, v);
  (void)ctx.engine().set_param(sample_->voice(), Vocoder::Load, 0.0f);  // a saved project must not hold it
  g_se.job_resend = ctx.uptime_ms() + kResendMs;
}

// F4 or F5 tapped: points the encoder at the bracket; a second tap puts it back at its end.
void SampleEditScreen::tap_bracket(app::Context& ctx, bool end_side, bool first) noexcept {
  if (refuse(ctx, true)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  fit_brackets(slot);
  const std::uint8_t want = end_side ? kFocusEnd : kFocusStart;
  if (g_se.focus == want && !first) {
    (end_side ? g_se.br[slot].end : g_se.br[slot].start) = end_side ? g_se.br_len[slot] : 0u;
  }
  g_se.focus = want;
  g_se.note = nullptr;  // the strip now says what F1 does
  ctx.redraw();
}

void SampleEditScreen::turn_bracket(app::Context& ctx, int delta, bool shift) noexcept {
  const unsigned slot = sample_->slot() & 3u;
  fit_brackets(slot);
  const std::uint32_t len = g_se.br_len[slot];
  g_se.br[slot] = se::bracket_move(g_se.br[slot], g_se.focus == kFocusEnd, delta, shift ? se::coarse_step(len) : 1u, len);
  g_se.note = nullptr;
  ctx.redraw();
}

// F1 on a moved bracket: move [start, end) down to 0 in slices, bake the seam, then Load.
void SampleEditScreen::run_trim(app::Context& ctx) noexcept {
  if (busy(ctx) || refuse(ctx, true)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  fit_brackets(slot);
  const se::Brackets b = g_se.br[slot];
  if (mem_ == nullptr || !se::trim_moved(b, g_se.br_len[slot])) {
    return;
  }
  if (!slot_stopped(slot)) {
    say(ctx, "STOP THE SLOT FIRST");
    return;
  }
  if (!se::trim_cuts(sample_->table(slot), b.start, b.end, g_se.job_table)) {
    say(ctx, "NOTHING TO TRIM");
    return;
  }
  se::job_begin(g_se.job, b.end - b.start, true);
  g_se.job_kind = kJobTrim;
  g_se.job_src = g_se.job_dst = static_cast<std::uint8_t>(slot);
  g_se.job_start = b.start;
  g_se.job_len = b.end - b.start;
  g_se.job_sent = false;
  g_se.note = nullptr;
  se::arm_cancel(g_se.arm);
  ctx.redraw();
}

void SampleEditScreen::ask_copy(app::Context& ctx) noexcept {
  if (busy(ctx) || refuse(ctx, true)) {
    return;
  }
  const unsigned to = copy_target();
  if (to >= sm::kReaders) {
    say(ctx, "NO EMPTY SLOT");
    return;
  }
  g_se.pending = se::Pending::Copy;
  g_se.note_buf[0] = '\0';
  cat(g_se.note_buf, sizeof g_se.note_buf, "COPY TO SLOT ");
  cat_uint(g_se.note_buf, sizeof g_se.note_buf, to + 1u);
  cat(g_se.note_buf, sizeof g_se.note_buf, "? F1 CONFIRMS");
  say(ctx, g_se.note_buf);
}

// The next slot after the selected one that holds nothing, records nothing and has no length: kReaders when there is none.
unsigned SampleEditScreen::copy_target() const noexcept {
  bool used[sm::kReaders];
  for (unsigned k = 0; k < sm::kReaders; ++k) {
    const Vocoder::LoopStatus st = sample_->status(k);
    used[k] = st.rec != Vocoder::LoopStatus::Rec::Idle || st.length != 0u || sample_->table(k).length != 0u ||
              (g_se.job_kind != kJobNone && g_se.job_dst == k);
  }
  return se::next_empty_slot(used, sample_->slot() & 3u);
}

void SampleEditScreen::run_copy(app::Context& ctx) noexcept {
  const unsigned slot = sample_->slot() & 3u;
  const unsigned to = copy_target();
  if (mem_ == nullptr) {
    return;
  }
  if (to >= sm::kReaders) {
    say(ctx, "NO EMPTY SLOT");
    return;
  }
  g_se.job_table = sample_->table(slot);
  se::job_begin(g_se.job, g_se.job_table.length, false);
  g_se.job_kind = kJobCopy;
  g_se.job_src = static_cast<std::uint8_t>(slot);
  g_se.job_dst = static_cast<std::uint8_t>(to);
  g_se.job_edited = sample_->edited(slot) ? 1u : 0u;
  g_se.job_even = static_cast<std::uint8_t>(sample_->even_code(slot));
  g_se.job_len = g_se.job_table.length;
  g_se.job_sent = false;
  g_se.note = nullptr;
  ctx.redraw();
}

// One step of the job: a slice of the move, then Load until the target slot shows the length (or the second is up).
void SampleEditScreen::job_tick(app::Context& ctx) noexcept {
  const unsigned src = g_se.job_src & 3u;
  const unsigned dst = g_se.job_dst & 3u;
  const bool trim = g_se.job_kind == kJobTrim;
  const std::uint64_t now = ctx.uptime_ms();
  if (!g_se.job_sent) {
    if (mem_ == nullptr || sample_->status(src).rec != Vocoder::LoopStatus::Rec::Idle || sample_->table(src).length == 0u) {
      g_se.job_kind = kJobNone;
      say(ctx, trim ? "TRIM CANCELLED" : "COPY CANCELLED");
      return;
    }
    std::int16_t* to = mem_ + static_cast<std::size_t>(dst) * Loop::kMaxSamples;
    const std::int16_t* from = mem_ + static_cast<std::size_t>(src) * Loop::kMaxSamples + (trim ? g_se.job_start : 0u);
    if (se::job_step(g_se.job, to, from)) {
      g_se.job_sent = true;
      g_se.job_deadline = now + kJobMs;
      send_load(ctx);
    }
    ctx.redraw();
    return;
  }
  const Vocoder::LoopStatus st = sample_->status(dst);
  if (st.rec == Vocoder::LoopStatus::Rec::Idle && st.length == g_se.job_len) {
    job_finish(ctx);
  } else if (now >= g_se.job_deadline) {
    g_se.job_kind = kJobNone;
    overview::shared().mark_dirty(dst);  // a failed trim has moved the samples under the old length
    g_se.hop_len[dst] = 0;
    say(ctx, trim ? "TRIM FAILED" : "COPY FAILED");
  } else if (now >= g_se.job_resend) {
    send_load(ctx);
  }
  ctx.redraw();
}

// The unit adopted the length: the target's cuts are the job's (SAMPLE's service() had made even ones for the new length), flagged as the hand's.
void SampleEditScreen::job_finish(app::Context& ctx) noexcept {
  const unsigned dst = g_se.job_dst & 3u;
  const bool trim = g_se.job_kind == kJobTrim;
  g_se.job_kind = kJobNone;
  sample_->table(dst) = g_se.job_table;
  table_changed(*sample_, dst, trim ? true : g_se.job_edited != 0u, trim ? 3u : g_se.job_even);
  g_se.cursor[dst] = 0;
  g_se.hop_len[dst] = 0;
  g_se.zoom_ok = false;  // the samples under the old window moved
  char tm[12];
  g_se.note_buf[0] = '\0';
  if (trim) {
    se::time_text(g_se.job_len, tm);
    cat(g_se.note_buf, sizeof g_se.note_buf, "TRIMMED TO ");
    cat(g_se.note_buf, sizeof g_se.note_buf, tm);
    cat(g_se.note_buf, sizeof g_se.note_buf, " S");
  } else {
    cat(g_se.note_buf, sizeof g_se.note_buf, "COPIED TO SLOT ");
    cat_uint(g_se.note_buf, sizeof g_se.note_buf, dst + 1u);
  }
  say(ctx, g_se.note_buf);
}

void SampleEditScreen::clear_cuts(app::Context& ctx) noexcept {
  if (refuse(ctx, true)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  sm::SliceTable& t = sample_->table(slot);
  if (se::arm_step(g_se.arm, 7u, sample_->edited(slot)) == se::Gate::Warn) {
    warn_replaces(ctx, 7u, t.count);
    return;
  }
  g_se.warn_key1 = 0;
  se::clear_cuts(t);
  table_changed(*sample_, slot, false, 3u);  // one cut over the slot: nothing of the hand's left to guard
  g_se.note = nullptr;
  ctx.redraw();
}

// F1's step: `delta` is the encoder's detents (or +1 for a second tap, which wraps).
void SampleEditScreen::turn(app::Context& ctx, int delta) noexcept {
  recut(ctx, se::step_even(sample_->even_code(sample_->slot()), delta, g_se.key_tap));
}

// ---- SE1: one slice picked on its pad ---------------------------------------------------------------------------------------------------

// The picked slice's index plus one, 0 when none, or when the slot, its length or its cut count are no longer the ones it was picked in.
unsigned SampleEditScreen::selected() const noexcept {
  if (sample_ == nullptr || g_se.sel1 == 0u) {
    return 0;
  }
  const unsigned slot = sample_->slot() & 3u;
  const sm::SliceTable& t = sample_->table(slot);
  return g_se.sel_slot == slot && g_se.sel_len == t.length && t.length != 0u && g_se.sel1 <= t.count ? g_se.sel1 : 0u;
}

// Where slice `i` starts as the picture shows it: slice 1 starts at the trim's START bracket (the table's cut 1 stays at 0), the others at their cut.
static std::uint32_t slice_start(const sm::SliceTable& t, unsigned slot, unsigned i) noexcept {
  if (i != 0u) {
    return t.cut[i].start;
  }
  return g_se.br_len[slot] == t.length ? g_se.br[slot].start : 0u;  // brackets of another length are not this slot's (fit_brackets resets them)
}

// The slice's end: the next cut's start, the slot's end for the last one.
static std::uint32_t slice_end(const sm::SliceTable& t, unsigned i) noexcept { return i + 1u < t.count ? t.cut[i + 1u].start : t.length; }

void SampleEditScreen::select_slice(app::Context& ctx, unsigned index) noexcept {
  const unsigned slot = sample_->slot() & 3u;
  const sm::SliceTable& t = sample_->table(slot);
  if (index >= t.count) {
    return;
  }
  fit_brackets(slot);
  g_se.sel1 = static_cast<std::uint8_t>(index + 1u);
  g_se.sel_slot = static_cast<std::uint8_t>(slot);
  g_se.sel_len = t.length;
  g_se.note = nullptr;
  set_cursor(ctx, slice_start(t, slot, index));
}

void SampleEditScreen::clear_sel(app::Context& ctx) noexcept {
  if (g_se.sel1 != 0u) {
    g_se.sel1 = 0;
    ctx.redraw();
  }
}

// LEFT or RIGHT with a slice picked: the one before or after it; at either end the line says so.
void SampleEditScreen::step_slice(app::Context& ctx, int dir) noexcept {
  const unsigned sel = selected();
  const unsigned count = sample_->table(sample_->slot() & 3u).count;
  if (sel == 0u || count == 0u) {
    return;
  }
  if (dir < 0 ? sel == 1u : sel >= count) {
    say(ctx, dir < 0 ? "FIRST SLICE" : "LAST SLICE");
    return;
  }
  select_slice(ctx, dir < 0 ? sel - 2u : sel);
}

// A top pad was tapped: its slice is picked (and the pad plays it, SAMPLE's pad_input), whatever was picked or not before. A pad past the last slice splits
// nothing: the line says INSERT does.
void SampleEditScreen::pad_tap(app::Context& ctx, unsigned pad) noexcept {
  const unsigned slot = sample_->slot() & 3u;
  const sm::SliceTable& t = sample_->table(slot);
  if (t.length == 0u || sample_->status(slot).rec != Vocoder::LoopStatus::Rec::Idle) {
    return;
  }
  if (pad >= t.count) {
    clear_sel(ctx);
    g_se.note_buf[0] = '\0';
    cat(g_se.note_buf, sizeof g_se.note_buf, "NO SLICE ");
    cat_uint(g_se.note_buf, sizeof g_se.note_buf, pad + 1u);
    cat(g_se.note_buf, sizeof g_se.note_buf, ": INSERT ADDS A CUT");
    say(ctx, g_se.note_buf);
    return;
  }
  select_slice(ctx, pad);
  hint_latch(ctx);
}

// The encoder with a slice picked: its start between its neighbours (slice 1: the trim START, between 0 and the next cut or the END bracket). A detent is a
// picture pixel of the zoom (SE2); SHIFT is one sample and no snap.
void SampleEditScreen::turn_slice(app::Context& ctx, int delta, bool fine) noexcept {
  const unsigned sel = selected();
  if (sel == 0u) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  sm::SliceTable& t = sample_->table(slot);
  const unsigned i = sel - 1u;
  fit_brackets(slot);
  std::uint32_t lo = 0, hi = 0;
  if (!se::slice_limits(t, i, g_se.br[slot].end, lo, hi)) {
    say(ctx, "NO ROOM TO MOVE");
    return;
  }
  const std::uint32_t pos = slice_start(t, slot, i);
  const std::uint32_t step = fine ? 1u : se::zoom_detent(view_span());
  const std::uint32_t at = se::slice_move_step(samples(), t.length, pos, delta, step, !fine && step >= se::kSnapMinStep, lo, hi);
  if (at == pos) {
    return;
  }
  if (i == 0u) {
    g_se.br[slot].start = at;  // the one notion of slice 1's start: the trim's START (a trim is still F4 or F5, then F1)
  } else if (se::set_slice_start(t, i, at)) {
    table_changed(*sample_, slot, true, 3u, true);
  } else {
    return;
  }
  g_se.note = nullptr;
  set_cursor(ctx, at);
}

// ---- SE2: the zoom ---------------------------------------------------------------------------------------------------------------------

// The window of the selected slot: se::zoom_span samples wide (the whole slot at rung 0), centred on the picked start or else the cursor, held inside the slot.
std::uint32_t SampleEditScreen::view_span() const noexcept {
  return sample_ == nullptr ? 0u : se::zoom_span(g_se.zoom_rung, sample_->table(sample_->slot() & 3u).length);
}

std::uint32_t SampleEditScreen::view_lo() const noexcept {
  if (sample_ == nullptr) {
    return 0;
  }
  const unsigned slot = sample_->slot() & 3u;
  const sm::SliceTable& t = sample_->table(slot);
  const std::uint32_t span = se::zoom_span(g_se.zoom_rung, t.length);
  if (span >= t.length) {
    return 0;
  }
  const unsigned sel = selected();
  return se::zoom_lo_for(sel != 0u ? slice_start(t, slot, sel - 1u) : cursor(), span, t.length);
}

// UP (dir > 0) zooms in, DOWN out, one rung of the ladder.
void SampleEditScreen::zoom_by(app::Context& ctx, int dir) noexcept {
  const std::uint32_t len = sample_->table(sample_->slot() & 3u).length;
  if (len == 0u) {
    say(ctx, "EMPTY: NOTHING TO ZOOM");
    return;
  }
  const unsigned next = se::zoom_step(g_se.zoom_rung, len, dir);
  if (next == g_se.zoom_rung) {
    say(ctx, dir > 0 ? "DEEPEST ZOOM" : "WHOLE SLOT");
    return;
  }
  g_se.zoom_rung = static_cast<std::uint8_t>(next);
  ctx.redraw();
}

// The columns of the window [lo, lo + span) of `slot`, rebuilt only when the window, the slot or its length changed (any change of the samples clears zoom_ok).
void SampleEditScreen::zoom_refresh(unsigned slot, std::uint32_t lo, std::uint32_t span) noexcept {
  const std::uint32_t len = sample_->table(slot).length;
  if (g_se.zoom_ok && g_se.zoom_slot == slot && g_se.zoom_lo == lo && g_se.zoom_span == span && g_se.zoom_len == len) {
    return;
  }
  se::zoom_build(samples(), len, lo, span, g_se.zoom_mn, g_se.zoom_mx);
  g_se.zoom_slot = static_cast<std::uint8_t>(slot);
  g_se.zoom_lo = lo;
  g_se.zoom_span = span;
  g_se.zoom_len = len;
  g_se.zoom_ok = true;
}

// ---- F8: the slot speaks ---------------------------------------------------------------------------------------------------------------

// The slot becomes the route's SPEAKS source, or (when it is already) the route goes back to LIVE. The same route change as SAMPLE's SIDE key (set_side): the
// side the slot leaves is freed first, so it is never on both.
void SampleEditScreen::toggle_speaks(app::Context& ctx) noexcept {
  if (perform_ == nullptr) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  const route::Route cur = perform_->route_now(ctx);
  const bool off = sm::side_of(cur, slot) == sm::Side::Speaks;
  const sm::SidePlan p = sm::plan_side(cur, off ? sm::Side::None : sm::Side::Speaks, slot, sample_->status(slot).length == 0u);
  if (p.refused) {
    say(ctx, p.why);
    return;
  }
  if (!p.change) {
    return;
  }
  const std::uint8_t v = sm::slot_value(slot);
  route::Route mid = cur;
  if (cur.speaks == v && p.to.speaks != v) {
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
  g_se.note_buf[0] = '\0';
  if (off) {
    cat(g_se.note_buf, sizeof g_se.note_buf, "LIVE SPEAKS AGAIN");
  } else {
    cat(g_se.note_buf, sizeof g_se.note_buf, "SLOT ");
    cat_uint(g_se.note_buf, sizeof g_se.note_buf, slot + 1u);
    cat(g_se.note_buf, sizeof g_se.note_buf, " SPEAKS");
  }
  say(ctx, g_se.note_buf);
}

// ---- the latch: SHIFT + a lower pad holds its note --------------------------------------------------------------------------------------

// Lower pads (24..47) only. Returns true when the input is taken here (not passed to SAMPLE's pads): the SHIFT press that latches, moves or lets go, the plain
// press of the latched pad (the note keeps sounding), and the release of any of those. Every other pad input passes through (a plain press of another lower pad
// plays over the latch, SAMPLE's own audition). The sound is PERFORM's audition path through SAMPLE's pad_input: the latch is that pad's held note whose physical
// release is swallowed.
bool SampleEditScreen::latch_input(app::Context& ctx, const app::Input& in) noexcept {
  if (perform_ == nullptr || !(in.pad_press() || in.pad_release())) {
    return false;
  }
  const unsigned pad = in.pad();
  if (pad >= app::kPadCount || !sm::layout_pad(pad)) {
    return false;
  }
  const std::uint32_t bit = std::uint32_t{1} << (pad - sm::kTopPads);
  if (in.pad_release()) {
    if ((g_se.swallow & bit) != 0u) {
      g_se.swallow &= ~bit;
      return true;
    }
    g_se.lower &= ~bit;
    return false;
  }
  const bool latched = g_se.latch_pad == pad;
  if (!in.shift) {
    if (latched) {
      g_se.swallow |= bit;  // a plain press of the held pad: nothing new to play, and its release must not let go
      return true;
    }
    g_se.lower |= bit;
    return false;
  }
  if (latched) {
    latch_release(ctx);
    g_se.swallow |= bit;
    return true;
  }
  const unsigned p = pad - sm::kTopPads;
  const pads::PadPlay pp = pads::pad_play(perform_->notes(), p < app::kPadCols ? pads::Row::A : pads::Row::B, p % app::kPadCols, perform_->octave());
  g_se.swallow |= bit;
  if (pp.note < 1) {
    say(ctx, "NO NOTE ON THAT PAD");
    return true;
  }
  latch_release(ctx);
  (void)sample_->pad_input(ctx, in);
  g_se.latch_pad = static_cast<std::uint8_t>(pad);
  g_se.last_mode = static_cast<std::uint8_t>(perform_->layout_mode());
  g_se.note = nullptr;  // the "how to latch" hint has done its job
  ctx.redraw();
  return true;
}

// Lets the latched note go: the physical release of its pad, made here.
void SampleEditScreen::latch_release(app::Context& ctx) noexcept {
  if (g_se.latch_pad == 0u || sample_ == nullptr) {
    return;
  }
  app::Input rel{};
  rel.type = app::Input::Type::Release;
  rel.kind = core::InputKind::Pad;
  rel.id = g_se.latch_pad;
  g_se.latch_pad = 0;
  (void)sample_->pad_input(ctx, rel);
  ctx.redraw();
}

// A slice pad is heard in BANDS, TALK and ROBOT only while a synth note sounds, and the live input is mixed unless the slot speaks: with the SYNTH carrier and no
// latch or held lower pad, one line says how to get the note. It cannot see a note the step sequencer plays.
void SampleEditScreen::hint_latch(app::Context& ctx) noexcept {
  if (perform_ == nullptr || g_se.latch_pad != 0u || g_se.lower != 0u) {
    return;
  }
  const unsigned mode = perform_->layout_mode();
  if (mode == sm::kModeTune || mode == kModeTract) {
    return;
  }
  const route::Route rt = perform_->route_now(ctx);
  if (rt.sounds != route::kSoundsSynth || sm::side_of(rt, sample_->slot() & 3u) == sm::Side::Sounds) {
    return;
  }
  say(ctx, "SHIFT + LOW PAD LATCHES A NOTE");
}

void SampleEditScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  if (sample_ == nullptr) {
    return;
  }
  unsigned k = 0;
  const bool fk = in.fkey_press(k);
  if ((in.pad_release() || g_se.job_kind == kJobNone) && latch_input(ctx, in)) {  // SHIFT + a lower pad latches its note; a release must always arrive
    return;
  }
  if (g_se.pending != se::Pending::None && in.type != app::Input::Type::Release) {  // F1 takes what F6 or SHIFT + DELETE asked; anything else drops it
    if (fk && !in.shift && k == 0u) {
      confirm(ctx);
      return;
    }
    g_se.pending = se::Pending::None;
    g_se.note = nullptr;
    ctx.redraw();
  }
  // SE1: a picked slice owns the encoder and LEFT / RIGHT; an F key (not SHIFT's, not F8) or SHIFT + UP / DOWN (another slot) lets it go; UP / DOWN zoom and keep
  // it. The key that lets it go is a first tap: a bracket or a value it points at is not stepped, reset or trimmed by the press that only leaves the slice.
  const bool was_sel = selected() != 0u;
  const bool keeps_arm = in.type == app::Input::Type::Release || (fk && !in.shift && (k == 0u || k == 1u || k == 2u)) ||
                         (fk && in.shift && k == 7u) || (in.press(Button::Shift) && g_se.warn_key1 == 8u) ||
                         (in.turn() && !was_sel && (g_se.focus == kFocusCut || g_se.focus == kFocusBeats || g_se.focus == kFocusHits));
  if (!keeps_arm) {
    se::arm_cancel(g_se.arm);
    g_se.warn_key1 = 0;
  }
  if (fk) {
    if (in.shift) {  // SHIFT + F8 clears the cuts (a hand's table asks twice); the other SHIFT + F keys are free
      if (k == 7u) {
        clear_cuts(ctx);  // the pick goes with the table when the clear really happens (table_changed); the warning keeps it
      }
      return;
    }
    if (k != 7u) {
      g_se.sel1 = 0;  // F8 (SPEAKS) keeps the pick: the slice that was picked is the one that now speaks
    }
    if (k == 0u) {
      const unsigned bs = sample_->slot() & 3u;
      if (!was_sel && (g_se.focus == kFocusStart || g_se.focus == kFocusEnd) && (fit_brackets(bs), se::trim_moved(g_se.br[bs], g_se.br_len[bs]))) {
        run_trim(ctx);  // a bracket in hand and off its end: F1 confirms the trim
        return;
      }
      if (g_se.focus == kFocusCut && !was_sel) {  // a second tap steps it
        g_se.key_tap = true;
        turn(ctx, 1);
        g_se.key_tap = false;
      }
      g_se.focus = kFocusCut;
      ctx.redraw();
    } else if (k == 1u) {
      if (refuse(ctx, true)) {
        return;
      }
      if (beats_dim(ctx)) {
        say(ctx, "NOT A TAKE: WHOLE BARS ONLY");
        return;
      }
      if (g_se.focus == kFocusBeats && !was_sel) {  // a second tap steps it
        g_se.key_tap = true;
        turn_beats(ctx, 1);
        g_se.key_tap = false;
      }
      g_se.focus = kFocusBeats;
      ctx.redraw();
    } else if (k == 2u) {
      if (refuse(ctx, true)) {
        return;
      }
      if (g_se.focus == kFocusHits && !was_sel) {  // a second tap steps it
        g_se.key_tap = true;
        turn_hits(ctx, 1);
        g_se.key_tap = false;
      }
      g_se.focus = kFocusHits;
      ctx.redraw();
    } else if (k == 3u || k == 4u) {
      tap_bracket(ctx, k == 4u, was_sel);
    } else if (k == 5u) {
      ask_normalise(ctx);
    } else if (k == 7u) {
      toggle_speaks(ctx);  // F8 alone never changes the table
    } else {
      save_wav(ctx);  // the encoder stays where it was
    }
    return;
  }
  if (in.turn()) {
    g_se.key_tap = false;
    if (was_sel) {
      turn_slice(ctx, in.delta(), in.shift);
    } else if (g_se.focus == kFocusCut) {
      turn(ctx, in.delta());
    } else if (g_se.focus == kFocusBeats) {
      turn_beats(ctx, in.delta());
    } else if (g_se.focus == kFocusHits) {
      turn_hits(ctx, in.delta());
    } else if (g_se.focus == kFocusStart || g_se.focus == kFocusEnd) {
      turn_bracket(ctx, in.delta(), in.shift);
    } else {
      const unsigned slot = sample_->slot() & 3u;
      const std::uint32_t len = sample_->table(slot).length;
      const std::uint32_t detent = se::zoom_detent(view_span());  // a picture pixel of the zoom: 1 px a detent, sample-exact at the deepest zoom
      set_cursor(ctx, se::move_cursor(cursor(), in.delta(), in.shift ? std::max(view_span() / 64u, detent) : detent, len));
    }
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  if (in.press(Button::Up) || in.press(Button::Down)) {
    if (in.shift) {  // SHIFT + UP / DOWN: the slot before / after (UP / DOWN alone zoom)
      g_se.focus = kFocusCursor;
      g_se.sel1 = 0;
      sample_->set_slot(in.press(Button::Up) ? (slot > 0u ? slot - 1u : 0u) : slot + 1u);
      ctx.redraw();
    } else {
      zoom_by(ctx, in.press(Button::Up) ? 1 : -1);
    }
  } else if (in.press(Button::Left) || in.press(Button::Right)) {  // repeats arrive as presses (repeat_buttons)
    if (was_sel) {
      step_slice(ctx, in.press(Button::Left) ? -1 : 1);  // a slice is picked: the one before or after it
      return;
    }
    g_se.focus = kFocusCursor;
    const sm::SliceTable& t = sample_->table(slot);
    set_cursor(ctx, in.press(Button::Left) ? se::prev_cut(t, cursor()) : se::next_cut(t, cursor()));
  } else if (in.press(Button::Insert)) {
    g_se.focus = kFocusCursor;
    insert(ctx);
  } else if (in.press(Button::Delete)) {
    g_se.focus = kFocusCursor;
    if (in.shift) {
      ask_empty(ctx);
    } else {
      remove(ctx);
    }
  } else if (in.press(Button::Copy)) {
    if (!in.shift) {  // SHIFT + COPY stays free
      ask_copy(ctx);
    }
  } else if (in.pad_release() || g_se.job_kind == kJobNone) {
    if (in.pad_press() && !sm::layout_pad(in.pad())) {
      pad_tap(ctx, in.pad());  // SE1: the pad also picks its slice
    }
    audition_pad(ctx, in);  // the pads, as on SAMPLE; no press while a job rewrites a slot (a release always arrives)
  }
}

// The pads, as SAMPLE plays them, except pad 1 while the trim's START is off 0: slice 1 starts at the START bracket (the table's cut 1 stays at 0 until
// F4 + F1 trims), so the pad plays from there and what the encoder moves is what is heard. The cut is swapped for the one press only: the voice copies it
// when it fires and the table is whole again before anything else reads it.
void SampleEditScreen::audition_pad(app::Context& ctx, const app::Input& in) noexcept {
  const unsigned slot = sample_->slot() & 3u;
  sm::SliceTable& t = sample_->table(slot);
  if (in.pad_press() && in.pad() == 0u && t.count != 0u && t.length != 0u && sample_->status(slot).rec == Vocoder::LoopStatus::Rec::Idle) {
    fit_brackets(slot);
    const std::uint32_t from = g_se.br[slot].start;
    const std::uint32_t to = t.count > 1u ? t.cut[1].start : t.length;
    if (from != 0u && from + se::kMinCutSamples <= to) {
      const sm::Cut whole = t.cut[0];
      t.cut[0].start = from;
      t.cut[0].len = to - from;
      (void)sample_->pad_input(ctx, in, false);
      t.cut[0] = whole;
      return;
    }
  }
  (void)sample_->pad_input(ctx, in, false);
}

void SampleEditScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  if (sample_ != nullptr) {
    sample_->pad_leds(ctx, leds);
    const unsigned sel = selected();
    if (sel != 0u) {
      leds.set(sel - 1u, app::kLedOn);  // the picked slice's pad, high
    }
    if (g_se.latch_pad != 0u) {
      leds.set(g_se.latch_pad, app::kLedOn);  // the latched note's pad
    }
  }
}

// A bracket is in hand and off its end, and no slice is picked: F1 would trim.
bool SampleEditScreen::trim_armed() noexcept {
  if (sample_ == nullptr || (g_se.focus != kFocusStart && g_se.focus != kFocusEnd) || selected() != 0u) {
    return false;
  }
  const unsigned slot = sample_->slot() & 3u;
  if (sample_->table(slot).length == 0u) {
    return false;
  }
  fit_brackets(slot);
  return se::trim_moved(g_se.br[slot], g_se.br_len[slot]);
}

// The key that answers what waits is labelled CONFIRM: F1 for NORMALISE, EMPTY, COPY and a trim; the F key that met a table the hand edited (REPLACES).
static bool confirming(unsigned k, bool trim) noexcept {
  return (k == 0u && (g_se.pending != se::Pending::None || trim)) || g_se.warn_key1 == k + 1u;
}

void SampleEditScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  if (ctx.panel().shift()) {  // the SHIFT row: only F8 (CLR CUTS) is used
    for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
      out.label[i] = "";
    }
    out.label[kKeySpeaks] = g_se.warn_key1 == 8u ? "CONFIRM" : "CLR CUTS";
    out.hot = 0;
    out.dim = 0x7Fu;
    return;
  }
  const bool trim = trim_armed();
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    out.label[i] = confirming(i, trim) ? "CONFIRM" : kKeyNames[i];
  }
  out.hot = static_cast<std::uint8_t>(g_se.focus == kFocusCut ? 1u : (g_se.focus == kFocusBeats ? 2u : (g_se.focus == kFocusHits ? 4u : (g_se.focus == kFocusStart ? 8u : (g_se.focus == kFocusEnd ? 16u : 0u)))));
  out.dim = beats_dim(ctx) ? 2u : 0u;  // F2 is dim for a WAV
}

void SampleEditScreen::update(app::Context& ctx) noexcept {
  if (sample_ == nullptr) {
    return;
  }
  sample_->pad_update(ctx);
  if (g_se.norm.phase == se::NormPhase::Scan || g_se.norm.phase == se::NormPhase::Apply) {
    norm_tick(ctx);  // a slice a frame
  }
  if (g_se.job_kind != kJobNone) {
    job_tick(ctx);
  }
  if (g_se.norm.phase == se::NormPhase::Scan || g_se.norm.phase == se::NormPhase::Apply || g_se.job_kind != kJobNone) {
    g_se.zoom_ok = false;  // the samples are being rewritten: the cached columns are of the old ones
  }
  if (perform_ != nullptr && g_se.latch_pad != 0u && perform_->layout_mode() != g_se.last_mode) {
    latch_release(ctx);  // another mode has another palette: the latched pad would hold a different note
  }
  if (g_se.scan.on) {
    scan_tick(ctx);
  }
  if (g_se.saving && ctx.files().status().phase != app::Files::Status::Running) {
    g_se.saving = false;  // the kit's strip says how it ended
  }
  for (unsigned i = 0; i < sm::kReaders; ++i) {  // a fresh take (possibly of the same length) makes the slot's energies stale
    if (sample_->status(i).rec != Vocoder::LoopStatus::Rec::Idle) {
      g_se.hop_len[i] = 0;
      if (g_se.sel_slot == i) {
        g_se.sel1 = 0;  // a retake of the same length would bring the old pick back over new samples
      }
      if (g_se.zoom_slot == i) {
        g_se.zoom_ok = false;  // and the old take's cached columns
      }
    }
  }
  if (g_se.note != nullptr && ctx.uptime_ms() >= g_se.note_until) {
    g_se.note = nullptr;
    g_se.pending = se::Pending::None;
    if (g_se.warn_key1 != 0u) {  // the REPLACES line ran out: so does the second tap that took it
      se::arm_cancel(g_se.arm);
      g_se.warn_key1 = 0;
    }
  }
  ctx.redraw();  // the slot's length can change under a take
}

// HELP: the pads and their groups are SAMPLE's; the rest is this screen's.
void SampleEditScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Up, "ZOOM IN, SHIFT: SLOT BEFORE", nullptr},
      {Button::Down, "ZOOM OUT, SHIFT: SLOT AFTER", nullptr},
      {Button::Left, "PREVIOUS CUT OR SLICE", nullptr},
      {Button::Right, "NEXT CUT OR SLICE", nullptr},
      {Button::Insert, "A CUT AT CURSOR OR IN SLICE", nullptr},
      {Button::Delete, "REMOVES THE PICKED CUT", nullptr},
      {Button::Copy, "COPY TO THE NEXT EMPTY SLOT", nullptr},
      {Button::Shift, "COARSE; + UP/DOWN: SLOT", nullptr},
      {Button::Params, "PARAMS: FOUR PAGES", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER", nullptr},
      {Button::File, "FILE", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD: A WAV", nullptr},
      {Button::SamplePlay, "SAMPLE: THE FOUR SLOTS", nullptr},
      {Button::SampleEdit, "THIS SCREEN", nullptr},
      {Button::SampleRec, "SAMPLE REC: A TAKE", nullptr},
      {Button::Song, "SONG", nullptr},
      {Button::Pattern, "PATTERN: THE 16 STEPS", nullptr},
      {Button::Note, "PATTERN: NOTE LANE", nullptr},
      {Button::Instr, "PATTERN: CARRIER LANE", nullptr},
      {Button::Vol, "PATTERN: LEVEL LANE", nullptr},
      {Button::Fx, "PATTERN: SLICE LANE", nullptr},
  };
  if (sample_ != nullptr) {
    sample_->help(ctx, h);  // the pad labels and groups; the rest is replaced below
  }
  h.summary = "TAP A PAD TO PICK AND HEAR A SLICE. UP, DOWN: ZOOM. INSERT CUTS, DELETE REMOVES A CUT.\n"
              "F8: THE SLOT SPEAKS. SHIFT + A LOW PAD LATCHES A NOTE. SHIFT + DELETE EMPTIES THE SLOT.";
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    h.fkey[i] = nullptr;
    h.fkey_shift[i] = nullptr;
  }
  h.fkey[0] = "OFF, 8, 16 OR 24 EVEN CUTS";
  h.fkey[2] = "CUTS AT HITS, 0 TO 100";
  h.fkey[6] = "THE SLOT TO THE CARD";
  h.fkey[1] = "4, 8 OR 16 CUTS A BAR";
  h.fkey[3] = "TRIM START, F1 TRIMS";
  h.fkey[4] = "TRIM END, F1 TRIMS";
  h.fkey[5] = "PEAK TO -1 DB, F1 CONFIRMS";
  h.fkey[7] = "SLOT SPEAKS, OR LIVE AGAIN";
  h.fkey_shift[7] = "CLR CUTS";
  h.pads = "UPPER: A TAP PICKS AND PLAYS A SLICE. LOWER: THE NOTES PALETTE; SHIFT + A PAD LATCHES ITS NOTE.";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "PICKED START, ELSE CURSOR";
}

void SampleEditScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  if (sample_ == nullptr) {
    return;
  }
  overview::shared().pump(r.display());  // a staged overview picture goes up to RAM_G (main loop, before the display list uses it)
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
    keys[i].dim = false;
  }
  const unsigned sel = selected();
  const bool trim = trim_armed();
  keys[0].hot = g_se.focus == kFocusCut;
  cat(keys[0].value, sizeof keys[0].value, se::even_name(sample_->even_code(slot)));
  const bool bdim = beats_dim(ctx);
  keys[1].dim = bdim;
  keys[1].hot = g_se.focus == kFocusBeats;
  if (!bdim) {
    cat(keys[1].value, sizeof keys[1].value, se::beats_name(shown_beats(*sample_, slot)));
  }

  keys[2].hot = g_se.focus == kFocusHits;
  if (len != 0u) {
    const unsigned hv = shown_hits(*sample_, slot);
    if (hv == 0u) {
      cat(keys[2].value, sizeof keys[2].value, "OFF");
    } else {
      cat_uint(keys[2].value, sizeof keys[2].value, hv);
    }
  }

  const bool has = len != 0u;
  se::Brackets br;
  bool bracket_on = false;
  if (has) {
    fit_brackets(slot);
    br = g_se.br[slot];
    keys[3].hot = g_se.focus == kFocusStart;
    keys[4].hot = g_se.focus == kFocusEnd;
    se::time_text(br.start, keys[3].value);
    se::time_text(br.end, keys[4].value);
    bracket_on = se::trim_moved(br, len) || g_se.focus == kFocusStart || g_se.focus == kFocusEnd;
  }

  // SE1 / the confirm cue: the waiting key reads CONFIRM and says what it will do; a trim and a NORMALISE cannot be undone.
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    if (confirming(i, trim)) {
      keys[i].name = "CONFIRM";
      keys[i].hot = true;
      keys[i].value[0] = '\0';
      const char* what = g_se.warn_key1 == i + 1u ? "REPLACES" : (g_se.pending == se::Pending::Normalise || g_se.pending == se::Pending::None ? "NO UNDO" : (g_se.pending == se::Pending::Empty ? "EMPTY" : "COPY"));
      cat(keys[i].value, sizeof keys[i].value, what);
    }
  }
  if (g_se.pending == se::Pending::Normalise) {
    keys[5].hot = true;
    keys[5].value[0] = '\0';
    cat(keys[5].value, sizeof keys[5].value, "NO UNDO");
  }

  // F8 SPEAKS: ON when the slot is the route's SPEAKS source. While SHIFT is held the F row is the SHIFT row, as on SAMPLE, PERFORM and PATTERN.
  const route::Route rt = perform_ != nullptr ? perform_->route_now(ctx) : route::Route{};
  const sm::Side side = perform_ != nullptr ? sm::side_of(rt, slot) : sm::Side::None;
  cat(keys[7].value, sizeof keys[7].value, side == sm::Side::Speaks ? "ON" : "OFF");
  keys[7].role = side == sm::Side::Speaks ? ornoir::KeyRole::Voice : ornoir::KeyRole::Plain;
  keys[7].dim = perform_ == nullptr;
  const bool shift = ctx.panel().shift();
  if (shift) {
    const bool warn = g_se.warn_key1 == 8u;
    for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
      keys[i] = ornoir::FKey{};
      keys[i].name = "";
      keys[i].dim = true;
    }
    keys[7].name = warn ? "CONFIRM" : "CLR CUTS";
    keys[7].dim = false;
    keys[7].hot = warn;
    if (warn) {
      cat(keys[7].value, sizeof keys[7].value, "REPLACES");
    }
  }

  const std::uint32_t span = view_span();
  const bool zoomed = len != 0u && !se::zoom_whole(g_se.zoom_rung, len);
  const std::uint32_t zlo = zoomed ? view_lo() : 0u;
  const std::uint32_t sel_pos = sel != 0u ? slice_start(t, slot, sel - 1u) : 0u;
  const std::uint32_t sel_end = sel != 0u ? slice_end(t, sel - 1u) : 0u;
  // One map for the picture: the window's when zoomed, the whole slot's otherwise; -1 outside the window.
  const auto map = [&](std::uint32_t pos) noexcept -> int {
    if (!zoomed) {
      return se::mark_x(pos, len);
    }
    const int x = se::zoom_x(pos, zlo, span);
    return x < 0 ? -1 : kPicX + x;
  };
  // The picked slice's span on the picture, clipped to the window.
  int pick_x0 = -1, pick_x1 = -1;
  if (sel != 0u) {
    pick_x0 = zoomed && sel_pos < zlo ? kPicX : map(sel_pos);
    pick_x1 = zoomed && sel_end >= zlo + span ? kPicX + static_cast<int>(se::kPicW) : (zoomed && sel_end < zlo ? -1 : map(sel_end));
    if (pick_x0 < 0 || pick_x1 < 0) {
      pick_x0 = -1;  // wholly outside the window (a zoom centred elsewhere cannot do this, the pick is the centre)
    }
  }

  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  ornoir::fill_rect(c, kX, kWaveY, kW, kWaveH, Color::Empty);  // the overview's rectangle
  ornoir::fill_rect(c, kX + 2, kWaveY + 2, kW - 4, kWaveH - 4, Color::Pane);
  if (pick_x0 >= 0 && pick_x1 > pick_x0) {  // SE3: the picked slice's span, filled behind the picture
    ornoir::fill_rect(c, pick_x0, kPicY - 8, pick_x1 - pick_x0, kPicH + 16, Color::Empty);
  }
  if (zoomed && samples() != nullptr) {  // SE2: min / max columns of the window; the cuts and the brackets are mapped into it
    zoom_refresh(slot, zlo, span);
    int peak = 512;  // a floor, so a quiet window is not blown up into noise
    for (unsigned i = 0; i < se::kZoomCols; ++i) {
      if (g_se.zoom_mn[i] <= g_se.zoom_mx[i]) {
        const int hi_v = g_se.zoom_mx[i];
        const int lo_v = -static_cast<int>(g_se.zoom_mn[i]);
        peak = hi_v > peak ? hi_v : peak;
        peak = lo_v > peak ? lo_v : peak;
      }
    }
    const int mid = kPicY + kPicH / 2;
    const int half = kPicH / 2 - 2;
    for (unsigned i = 0; i < se::kZoomCols; ++i) {
      if (g_se.zoom_mn[i] > g_se.zoom_mx[i]) {
        continue;  // before the slot's start or past its end
      }
      const int top = mid - g_se.zoom_mx[i] * half / peak;
      const int bot = mid - g_se.zoom_mn[i] * half / peak;
      ornoir::fill_rect(c, kPicX + static_cast<int>(i * se::kZoomColW), top, static_cast<int>(se::kZoomColW) - 1, bot - top + 1, Color::TextDim);
    }
    for (unsigned i = 1; i < t.count; ++i) {
      const int x = map(t.cut[i].start);
      if (x >= 0) {
        ornoir::fill_rect(c, x, kPicY, 1, kPicH, Color::Accent);
      }
    }
    const int cx = map(cur);
    if (cx >= 0) {
      ornoir::fill_rect(c, cx, kPicY, 1, kPicH, Color::Text);
    }
    const int w0 = se::mark_x(zlo, len);
    int w1 = se::mark_x(zlo + span, len);
    w1 = w1 > w0 + 2 ? w1 : w0 + 2;
    ornoir::fill_rect(c, w0, kBarY - 6, w1 - w0, 3, Color::Text);  // where the window sits on the ruler
  } else if (len != 0u && overview::shared().shown(slot)) {  // the picture at 1x (the kit has no bitmap scaling), centred; the cuts and the cursor are lines over it
    overview::shared().draw(c, slot, true, kPicX, kPicY, sample_->slot_ink(ctx, slot));
    for (unsigned i = 0; i < t.count; ++i) {
      ornoir::fill_rect(c, se::mark_x(t.cut[i].start, len), kPicY, 1, kPicH, Color::Accent);
    }
    ornoir::fill_rect(c, se::mark_x(cur, len), kPicY, 1, kPicH, Color::Text);
  }
  if (bracket_on) {  // [ START and END ] on the picture and the ruler: one map, the picture's
    const int xs = map(br.start);
    const int xe = map(br.end);
    for (int side_i = 0; side_i < 2; ++side_i) {
      const int x = side_i == 0 ? xs : xe - 1;
      ornoir::fill_rect(c, se::mark_x(side_i == 0 ? br.start : br.end, len) - (side_i == 0 ? 0 : 1), kBarY - 14, 3, kBarH + 28, Color::Accent);  // the ruler always shows it
      if ((side_i == 0 ? xs : xe) < 0) {
        continue;  // outside the window: no bracket on the picture
      }
      const int serif = side_i == 0 ? 0 : -6;
      ornoir::fill_rect(c, x, kPicY, 3, kPicH, Color::Accent);
      ornoir::fill_rect(c, x + serif, kPicY, 9, 3, Color::Accent);
      ornoir::fill_rect(c, x + serif, kPicY + kPicH - 3, 9, 3, Color::Accent);
    }
  }
  ornoir::fill_rect(c, kX, kBarY, kW, kBarH, Color::Empty);
  if (len != 0u) {
    if (sel != 0u) {  // SE3: the picked slice's span on the cut strip
      const int x0 = se::mark_x(sel_pos, len);
      const int x1 = se::mark_x(sel_end, len);
      ornoir::fill_rect(c, x0, kBarY, x1 > x0 + 2 ? x1 - x0 : 2, kBarH, Color::Fx);
    }
    for (std::uint32_t sec = se::kRateHz; sec < len; sec += se::kRateHz) {  // a tick a second
      ornoir::fill_rect(c, se::mark_x(sec, len), kBarY + kBarH - 4, 1, 4, Color::TextDim);
    }
    for (unsigned i = 0; i < t.count; ++i) {
      ornoir::fill_rect(c, se::mark_x(t.cut[i].start, len), kBarY - 14, 2, kBarH + 28, Color::Accent);
    }
    ornoir::fill_rect(c, se::mark_x(cur, len), kBarY - 24, 2, kBarH + 48, Color::Text);
  }
  if (sel != 0u) {  // SE3: the picked start, the brightest line, on the picture and over the strip (slice 1's start is the START bracket, the same line)
    const int x = map(sel_pos);
    if (x >= 0) {
      ornoir::fill_rect(c, x - 1, kPicY - 8, 3, kPicH + 16, Color::Play);
    }
    ornoir::fill_rect(c, se::mark_x(sel_pos, len) - 1, kBarY - 14, 4, kBarH + 28, Color::Play);
  }
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);
  if (len != 0u) {
    char zt[16];
    se::zoom_text(g_se.zoom_rung, len, zt);
    r.text(kX + 8, kWaveY + 6, zt, Color::TextDim, Face::Micro);
  }
  if (zoomed) {
    for (unsigned i = 0; i < t.count; ++i) {  // the cuts in the window by number (1 is the START bracket)
      const int x = map(i == 0u ? br.start : t.cut[i].start);
      if (x >= 0) {
        char num[4];
        num[0] = '\0';
        cat_uint(num, sizeof num, i + 1u);
        r.text(x + 5, kPicY - 14, num, i + 1u == sel ? Color::Play : Color::Accent, Face::Micro);
      }
    }
  }

  char buf[40];
  r.text(kX, 88, kSlotNames[slot], Color::Text, Face::Display);
  if (side != sm::Side::None) {  // the slot's place in the route, always next to its name
    r.text(kX + r.text_width(kSlotNames[slot], Face::Display) + 16, 100, side == sm::Side::Speaks ? "SPEAKS" : "SOUNDS", side == sm::Side::Speaks ? Color::Play : Color::Sel, Face::Label);
  }
  buf[0] = '\0';
  if (g_se.latch_pad != 0u) {
    cat(buf, sizeof buf, "NOTE LATCHED  ");
  }
  cat_uint(buf, sizeof buf, t.count);
  cat(buf, sizeof buf, t.count == 1u ? " CUT" : " CUTS");
  if (sample_->edited(slot)) {
    cat(buf, sizeof buf, "  EDITED");
  }
  r.text(kX + kW - r.text_width(buf, Face::Label), 100, buf, Color::Text, Face::Label);

  if (len != 0u && sel != 0u) {  // SE3: which slice is picked, in numbers
    char it[56];
    it[0] = '\0';
    char tb[12];
    cat(it, sizeof it, "SLICE ");
    cat_uint(it, sizeof it, sel);
    cat(it, sizeof it, " OF ");
    cat_uint(it, sizeof it, t.count);
    cat(it, sizeof it, "   START ");
    se::time_text(sel_pos, tb);
    cat(it, sizeof it, tb);
    cat(it, sizeof it, "   LEN ");
    se::time_text(sel_end - sel_pos, tb);
    cat(it, sizeof it, tb);
    r.text(kX, kInfoY, it, Color::Play, Face::Label);
  }
  if (len != 0u && perform_ != nullptr) {  // what the mode does with this slot's place (TUNE plays no synth, TRACT hears the mouth through dry)
    const char* why = sm::cue_reason(sm::mode_cue(perform_->layout_mode(), side));
    if (why != nullptr) {
      r.text(kX + kW - r.text_width(why, Face::Micro), kInfoY + 4, why, Color::TextDim, Face::Micro);
    }
  }
  if (len == 0u) {
    const char* line = recording ? "RECORDING" : (sample_->waiting(slot) ? "LOADING" : "EMPTY: SAMPLE REC OR SAMPLE LOAD");
    r.text(centred(r, line, Face::Label, kX + kW / 2), kWaveY + kWaveH / 2 - 8, line, Color::Accent, Face::Label);
  } else {
    int last = kX - 100;
    for (unsigned i = 0; i < t.count; ++i) {  // the numbers, skipping one that would sit on the one before
      char num[4];
      num[0] = '\0';
      cat_uint(num, sizeof num, i + 1u);
      const int x = se::mark_x(t.cut[i].start, len) + 4;
      if (x >= last + 20) {
        r.text(x, kBarY - 30, num, Color::Accent, Face::Micro);
        last = x;
      }
    }
    char tm[12];
    se::time_text(cur, tm);
    int tx = se::mark_x(cur, len) - r.text_width(tm, Face::Label) / 2;
    const int right = kX + kW - r.text_width(tm, Face::Label);
    tx = tx < kX ? kX : (tx > right ? right : tx);
    r.text(tx, kBarY + kBarH + 32, tm, Color::Text, Face::Label);
  }

  if (g_se.job_kind != kJobNone) {
    char pb[24];
    pb[0] = '\0';
    cat(pb, sizeof pb, g_se.job_kind == kJobTrim ? "TRIMMING " : "COPYING ");
    cat_uint(pb, sizeof pb, g_se.job_sent ? 100u : se::job_percent(g_se.job));
    cat(pb, sizeof pb, "%");
    r.text(kX, 398, pb, Color::Accent, Face::Label);
  } else if (g_se.scan.on) {
    char pb[24];
    pb[0] = '\0';
    cat(pb, sizeof pb, "SCANNING ");
    cat_uint(pb, sizeof pb, se::hop_scan_percent(g_se.scan));
    cat(pb, sizeof pb, "%");
    r.text(kX, 398, pb, Color::Accent, Face::Label);
  } else if (g_se.norm.phase == se::NormPhase::Scan || g_se.norm.phase == se::NormPhase::Apply) {
    char pb[24];
    pb[0] = '\0';
    cat(pb, sizeof pb, g_se.norm.phase == se::NormPhase::Scan ? "SCANNING " : "NORMALISING ");
    cat_uint(pb, sizeof pb, se::norm_percent(g_se.norm));
    cat(pb, sizeof pb, "%");
    r.text(kX, 398, pb, Color::Accent, Face::Label);
  } else if (g_se.note != nullptr) {
    r.text(kX, 398, g_se.note, Color::Accent, Face::Label);
  } else if (shift) {
    r.text(kX, 402, "SHIFT: UP DOWN SLOT   LOW PAD LATCHES A NOTE   F8 CLEARS THE CUTS", Color::TextDim, Face::Micro);
  } else if (sel != 0u) {
    r.text(kX, 402, sel == 1u ? "SLICE 1 STARTS AT THE TRIM START. ENCODER MOVES IT, SHIFT FINE. DELETE CANNOT REMOVE IT"
                              : "ENCODER MOVES THE START, SHIFT FINE. LEFT RIGHT PICK, INSERT SPLITS, DELETE REMOVES THE CUT", Color::TextDim, Face::Micro);
  } else if (has && se::trim_moved(br, len)) {  // a bracket off its end: say what F1 does now
    const bool armed = g_se.focus == kFocusStart || g_se.focus == kFocusEnd;
    r.text(kX, 398, armed ? "TRIM TO THE BRACKETS: F1 CONFIRMS (NO UNDO)" : "BRACKETS MOVED: F4 OR F5, THEN F1 TRIMS", Color::Accent, Face::Label);
  } else {
    r.text(kX, 402, "UP DOWN ZOOM   INSERT CUTS AT THE CURSOR   DELETE REMOVES THE NEAREST CUT", Color::TextDim, Face::Micro);
  }
}

}  // namespace o3_voc03er
