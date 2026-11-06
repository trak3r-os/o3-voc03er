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
constexpr const char* kKeyNames[ornoir::kFKeys] = {"CUT EVEN", "BEATS", "HITS", "START", "END", "NORMALISE", "SAVE WAV", "CLR CUTS"};

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
  char note_buf[32];
};
static_assert(offsetof(EditData, hop) % 4u == 0u, "the hop energies are word aligned");
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
constexpr int kBarH = 14;

}  // namespace

void SampleEditScreen::init(SampleScreen* sample, std::int16_t* mem) noexcept {
  sample_ = sample;
  mem_ = mem;
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
  ctx.redraw();
}

void SampleEditScreen::leave(app::Context& ctx) noexcept {
  g_se.pending = se::Pending::None;
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
  g_se.beats[slot & 3u] = 0;  // F2's own call sets it again after this
  g_se.scan_sens = 0;         // a scan still running keeps its energies, but its cuts must not replace this table
  g_se.hits[slot & 3u] = 0;   // so does F3's
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
    g_se.note_buf[0] = '\0';
    cat(g_se.note_buf, sizeof g_se.note_buf, "REPLACES ");
    cat_uint(g_se.note_buf, sizeof g_se.note_buf, t.count);
    cat(g_se.note_buf, sizeof g_se.note_buf, " CUTS");
    say(ctx, g_se.note_buf);
    return;
  }
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
    g_se.note_buf[0] = '\0';
    cat(g_se.note_buf, sizeof g_se.note_buf, "REPLACES ");
    cat_uint(g_se.note_buf, sizeof g_se.note_buf, t.count);
    cat(g_se.note_buf, sizeof g_se.note_buf, " CUTS");
    say(ctx, g_se.note_buf);
    return;
  }
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
  say(ctx, "NORMALISE TO -1 DB? F1 CONFIRMS");
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
  say(ctx, g_se.note_buf);
}

// ---- TRIM (F4, F5) and COPY: a bounded slice a pass, then Load until the unit adopts the length ------------------------------------------------

// True (and says why) while NORMALISE or a job owns the samples.
bool SampleEditScreen::busy(app::Context& ctx) noexcept {
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
void SampleEditScreen::tap_bracket(app::Context& ctx, bool end_side) noexcept {
  if (refuse(ctx, true)) {
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  fit_brackets(slot);
  const std::uint8_t want = end_side ? kFocusEnd : kFocusStart;
  if (g_se.focus == want) {
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
  if (g_se.pending != se::Pending::None && in.type != app::Input::Type::Release) {  // F1 takes what F6 or SHIFT + DELETE asked; anything else drops it
    if (fk && !in.shift && k == 0u) {
      confirm(ctx);
      return;
    }
    g_se.pending = se::Pending::None;
    g_se.note = nullptr;
    ctx.redraw();
  }
  const bool keeps_arm = in.type == app::Input::Type::Release || (fk && !in.shift && (k == 0u || k == 1u || k == 2u || k == 7u)) ||
                         (in.turn() && (g_se.focus == kFocusCut || g_se.focus == kFocusBeats || g_se.focus == kFocusHits));
  if (!keeps_arm) {
    se::arm_cancel(g_se.arm);
  }
  if (fk) {
    if (in.shift) {
      return;
    }
    if (k == 0u) {
      const unsigned bs = sample_->slot() & 3u;
      if ((g_se.focus == kFocusStart || g_se.focus == kFocusEnd) && (fit_brackets(bs), se::trim_moved(g_se.br[bs], g_se.br_len[bs]))) {
        run_trim(ctx);  // a bracket in hand and off its end: F1 confirms the trim
        return;
      }
      if (g_se.focus == kFocusCut) {  // a second tap steps it
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
      if (g_se.focus == kFocusBeats) {  // a second tap steps it
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
      if (g_se.focus == kFocusHits) {  // a second tap steps it
        g_se.key_tap = true;
        turn_hits(ctx, 1);
        g_se.key_tap = false;
      }
      g_se.focus = kFocusHits;
      ctx.redraw();
    } else if (k == 3u || k == 4u) {
      tap_bracket(ctx, k == 4u);
    } else if (k == 5u) {
      ask_normalise(ctx);
    } else if (k == 7u) {
      clear_cuts(ctx);
    } else {
      save_wav(ctx);  // the encoder stays where it was
    }
    return;
  }
  if (in.turn()) {
    g_se.key_tap = false;
    if (g_se.focus == kFocusCut) {
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
    if (in.shift) {
      ask_empty(ctx);
    } else {
      remove(ctx);
    }
  } else if (in.press(Button::Copy)) {
    if (!in.shift) {  // SHIFT + COPY stays free
      ask_copy(ctx);
    }
  } else if (g_se.job_kind == kJobNone) {
    (void)sample_->pad_input(ctx, in);  // the pads and SHIFT's latch, as on SAMPLE; none while a job rewrites a slot
  }
}

void SampleEditScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  if (sample_ != nullptr) {
    sample_->pad_leds(ctx, leds);
  }
}

void SampleEditScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    out.label[i] = kKeyNames[i];
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
  if (g_se.scan.on) {
    scan_tick(ctx);
  }
  if (g_se.saving && ctx.files().status().phase != app::Files::Status::Running) {
    g_se.saving = false;  // the kit's strip says how it ended
  }
  for (unsigned i = 0; i < sm::kReaders; ++i) {  // a fresh take (possibly of the same length) makes the slot's energies stale
    if (sample_->status(i).rec != Vocoder::LoopStatus::Rec::Idle) {
      g_se.hop_len[i] = 0;
    }
  }
  if (g_se.note != nullptr && ctx.uptime_ms() >= g_se.note_until) {
    g_se.note = nullptr;
    g_se.pending = se::Pending::None;
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
      {Button::Copy, "COPY TO THE NEXT EMPTY SLOT", nullptr},
      {Button::Shift, "COARSE CURSOR, LATCH CHORD", nullptr},
      {Button::Rec, "SAMPLE REC: A TAKE", nullptr},
      {Button::Params, "THE OLD PLAY SCREEN: LOOPS", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::Master, "MASTER (SLICE U9)", nullptr},
      {Button::File, "FILE (SLICE U9)", nullptr},
      {Button::SampleLoad, "SAMPLE LOAD: A WAV", nullptr},
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
  h.summary = "INSERT CUTS AT THE CURSOR, DELETE REMOVES THE CUT THERE.\n"
              "SHIFT + DELETE EMPTIES THE SLOT, COPY COPIES IT, F4 / F5 TRIM: F1 CONFIRMS.";
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
  h.fkey[7] = "ONE CUT OVER THE SLOT";
  h.pads = "UPPER ROWS: THE SLOT'S CUTS. LOWER ROWS: CHORDS (LATCH WITH SHIFT).";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "CURSOR, LIT F KEY, BRACKET";
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

  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  ornoir::fill_rect(c, kX, kWaveY, kW, kWaveH, Color::Empty);  // the overview's rectangle
  ornoir::fill_rect(c, kX + 2, kWaveY + 2, kW - 4, kWaveH - 4, Color::Pane);
  if (len != 0u && overview::shared().shown(slot)) {  // the picture at 1x (the kit has no bitmap scaling), centred; the cuts and the cursor are lines over it
    overview::shared().draw(c, slot, true, kPicX, kPicY, sample_->slot_ink(ctx, slot));
    for (unsigned i = 0; i < t.count; ++i) {
      ornoir::fill_rect(c, se::mark_x(t.cut[i].start, len), kPicY, 1, kPicH, Color::Accent);
    }
    ornoir::fill_rect(c, se::mark_x(cur, len), kPicY, 1, kPicH, Color::Text);
  }
  if (bracket_on) {  // [ START and END ] on the picture and the ruler: one map, the cursor's
    const int xs = se::mark_x(br.start, len);
    const int xe = se::mark_x(br.end, len);
    for (int side = 0; side < 2; ++side) {
      const int x = side == 0 ? xs : xe - 1;
      const int serif = side == 0 ? 0 : -6;
      ornoir::fill_rect(c, x, kPicY, 3, kPicH, Color::Accent);
      ornoir::fill_rect(c, x + serif, kPicY, 9, 3, Color::Accent);
      ornoir::fill_rect(c, x + serif, kPicY + kPicH - 3, 9, 3, Color::Accent);
      ornoir::fill_rect(c, x, kBarY - 14, 3, kBarH + 28, Color::Accent);
    }
  }
  ornoir::fill_rect(c, kX, kBarY, kW, kBarH, Color::Empty);
  if (len != 0u) {
    for (std::uint32_t s = se::kRateHz; s < len; s += se::kRateHz) {  // a tick a second
      ornoir::fill_rect(c, se::mark_x(s, len), kBarY + kBarH - 4, 1, 4, Color::TextDim);
    }
    for (unsigned i = 0; i < t.count; ++i) {
      ornoir::fill_rect(c, se::mark_x(t.cut[i].start, len), kBarY - 14, 2, kBarH + 28, Color::Accent);
    }
    ornoir::fill_rect(c, se::mark_x(cur, len), kBarY - 24, 2, kBarH + 48, Color::Text);
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
  } else if (has && se::trim_moved(br, len)) {  // a bracket off its end: say what F1 does now
    const bool armed = g_se.focus == kFocusStart || g_se.focus == kFocusEnd;
    r.text(kX, 398, armed ? "TRIM TO THE BRACKETS: F1 CONFIRMS (NO UNDO)" : "BRACKETS MOVED: F4 OR F5, THEN F1 TRIMS", Color::Accent, Face::Label);
  } else {
    r.text(kX, 402, "INSERT CUTS AT THE CURSOR, DELETE REMOVES THE CUT UNDER IT", Color::TextDim, Face::Micro);
  }
}

}  // namespace o3_voc03er
