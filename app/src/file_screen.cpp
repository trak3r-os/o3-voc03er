// SPDX-License-Identifier: GPL-3.0-or-later
#include "file_screen.h"

#include "audio_dirty.h"
#include "file_logic.h"
#include "sample_edit_screen.h"
#include "song_screen.h"
#include "skin/ornoir.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/files.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/placement.h>

#include <cstddef>
#include <cstring>

namespace o3_voc03er {

using namespace o3sdk;
using ui::Color;
using ui::Face;
using Button = app::Button;
namespace fl = file;

namespace {

constexpr std::uint64_t kNoteMs = 3500;

enum Stage : std::uint8_t {
  kMenu,       // the menu (or an ask)
  kPicking,    // the picker lists the project folders
  kNaming,     // the NameEntry is up
  kSaving,     // the card job writes the file
  kLoading,    // the card job reads the file
  kRestoring,  // the values go to the engine a few a frame
  kWavList,    // F5: the card job lists the project folder (the names the WAVs will get)
  kWavSave,    // F5: the card job writes one slot's WAV
  kWavLoad,    // LOAD: a slot's WAV is read into it (SampleLoadScreen's external job), then its cuts go back
};
enum Ask : std::uint8_t { kNone, kLoad, kNew };

// A save or a load in hand (not the restore, not a WAV run): F8 gives it up. The kit's save is atomic (its temporary file goes and the old file stays whole, a cancel
// past the write is a rollback), and a load reads into g_fbuf only: g_proj and the engine are not touched until the read ends, so a cancel leaves the project as it was.
// The restore that follows is another matter (values reach the engine a few a frame): a half-applied project would be two projects mixed, so it finishes on its own.
bool kit_job_stage(std::uint8_t stage) noexcept { return stage == kSaving || stage == kLoading; }

// The screen's state, in the SDRAM (O3SDK_BIG: zero at start, main loop only).
struct FileData {
  std::uint8_t stage;
  std::uint8_t ask;
  bool has_name;      // a current project (name "" is the app's own folder)
  bool pending_has;   // the folder of the job in hand becomes the current project when it ends
  bool reset_slots;   // NEW: the slots' cuts go back to even
  unsigned next;      // the next engine id the restore looks at
  unsigned tnext;     // then the next TRCT value (V3) it writes to the tract unit
  unsigned restored, kept;
  char name[40];
  char pending[40];
  const char* note;
  std::uint64_t note_until;
  bool note_unseen;  // the note was set while another screen was up (a job ended after FILE was left): enter() keeps it
  char note_buf[72];
  // SAVE WAVS and the WAVs of a LOAD (U9c3).
  std::uint32_t lens[fl::kWavSlots];  // the length of each slot at F5 (0: no audio, no WAV)
  unsigned wav_cur;                   // the slot of the job in hand (a save) or the next one to look at (a load)
  unsigned wav_done, wav_total;       // jobs finished before this one, jobs of the run
  unsigned wav_saved_mask;            // slots whose WAV is on the card in this run
  unsigned wl_loaded, wl_skipped, wl_first;  // a load: WAVs landed, slots skipped, the first skipped slot
  bool wav_end;                       // the project save in hand ends a SAVE WAVS run
  unsigned unsaved;                   // slots with audio the save in hand does not carry (file_logic.h unsaved_audio_mask)
  bool wl_settle;                     // the WAV has landed: waiting for the slot's table to show its length
  std::uint64_t wl_deadline;
  char wavname[fl::kWavSlots][kWavNameMax + 1];
  char why[40];
  char line[56];                      // the strip while a WAV job runs
  char wait[48];                      // a slot is grey until its WAV has landed
  std::uint8_t waiting;               // bit n: slot n's WAV is on its way (grey in FILE's slot row and in PERFORM's); main loop only
};
O3SDK_BIG FileData g_fd = {};
O3SDK_BIG Project g_proj = {};  // what the last load read or save wrote: the records a slot without audio keeps wait here
alignas(4) O3SDK_BIG std::uint8_t g_fbuf[kProjectFileMax] = {};
static_assert(sizeof g_fbuf % 4 == 0, "the file buffer is whole words");

constexpr int kX = 26;
constexpr int kW = 748;
constexpr int kListY = 128;
constexpr int kListH = 250;
constexpr int kNoteY = 398;

void copy_name(char* dst, std::size_t cap, const char* s) noexcept {
  std::size_t n = 0;
  while (s != nullptr && s[n] != '\0' && n + 1u < cap) {
    dst[n] = s[n];
    ++n;
  }
  dst[n] = '\0';
}

}  // namespace

static constexpr std::size_t kPickEntries = 24;
static O3SDK_BIG app::Files::Entry g_pick[kPickEntries] = {};  // the picker's rows, in the SDRAM
static constexpr std::size_t kWlEntries = 64;                  // the project folder's listing for the names of the WAVs: a longer one is refused
static O3SDK_BIG app::Files::Entry g_wl[kWlEntries] = {};

FileScreen::FileScreen() noexcept : picker_(g_pick, kPickEntries) {}

void FileScreen::init(dsp::UnitId voice, SampleScreen* sample, PerformScreen* perform, PatternScreen* pattern, Bank* bank, Chain* chain, SampleLoadScreen* load,
                      std::int16_t* mem) noexcept {
  load_ = load;
  mem_ = mem;
  chain_ = chain;
  voice_ = voice;
  sample_ = sample;
  perform_ = perform;
  pattern_ = pattern;
  bank_ = bank;
}

const ui::Theme* FileScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

bool FileScreen::busy() const noexcept { return g_fd.stage >= kSaving; }
bool FileScreen::has_project() const noexcept { return g_fd.has_name; }
const char* FileScreen::project_name() const noexcept { return g_fd.name; }
const char* FileScreen::note() const noexcept { return g_fd.note != nullptr ? g_fd.note : ""; }

void FileScreen::say(app::Context& ctx, const char* text) noexcept {
  g_fd.note = text;
  g_fd.note_until = ctx.uptime_ms() + kNoteMs;
  g_fd.note_unseen = ctx.screen() != this;
  ctx.redraw();
}

// A job in hand (a save, a load, a restore, a WAV run) goes on while FILE is not the screen: service() advances it from App::update every pass, and enter() leaves
// its state as it is. What a job said when it ended while FILE was away is still on the strip now.
void FileScreen::enter(app::Context& ctx) noexcept {
  if (g_fd.stage < kSaving) {
    g_fd.stage = kMenu;
    g_fd.ask = kNone;
  }
  if (g_fd.note_unseen && g_fd.note != nullptr) {
    g_fd.note_until = ctx.uptime_ms() + kNoteMs;
  } else {
    g_fd.note = nullptr;
  }
  g_fd.note_unseen = false;
  ctx.redraw();
}

// Leaving gives nothing up: a card job, a restore and a WAV run are not FILE's screen but the app's work (a screen button can leave at any moment), and they end in
// service(). Only the menu's own state goes.
void FileScreen::leave(app::Context&) noexcept {
  if (g_fd.stage < kSaving) {
    g_fd.stage = kMenu;
  }
  g_fd.ask = kNone;
  picker_.close();
  names_.close();
}

bool FileScreen::refuse(app::Context& ctx) noexcept {
  if (ctx.files().busy()) {
    say(ctx, "A CARD JOB IS RUNNING");
    return true;
  }
  if (sample_ != nullptr) {
    for (unsigned s = 0; s < route::kSlots; ++s) {
      if (sample_->status(s).rec != Vocoder::LoopStatus::Rec::Idle) {
        say(ctx, "A TAKE IS RUNNING");
        return true;
      }
    }
  }
  return false;
}

void FileScreen::open_picker(app::Context& ctx) noexcept {
  g_fd.stage = kPicking;
  (void)picker_.open(ctx, "PROJECTS", "", "o3a", true);
  ctx.redraw();
}

void FileScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  if (sample_ == nullptr || perform_ == nullptr || pattern_ == nullptr || bank_ == nullptr) {
    return;
  }
  if (wav_stage()) {  // F8 gives the WAV run up; the rest waits
    if (in.press(Button::F8)) {
      abort_wavs(ctx);
      ctx.redraw();
    }
    return;
  }
  if (kit_job_stage(g_fd.stage)) {  // LE6: F8 gives a save or a load up; service() ends the stage with the kit's "CANCELLED" (wav_end and the pending name go with it)
    if (in.press(Button::F8)) {
      ctx.files().cancel();
      ctx.redraw();
    }
    return;
  }
  if (g_fd.stage >= kSaving) {  // the restore: it finishes on its own (a half-applied project is a mixed one)
    return;
  }
  if (g_fd.stage == kNaming) {
    switch (names_.input(in)) {
      case app::NameEntry::Result::Done: start_save(ctx, names_.text(), true); break;
      case app::NameEntry::Result::Cancelled: g_fd.stage = kMenu; names_.close(); break;
      case app::NameEntry::Result::Editing: break;
    }
    ctx.redraw();
    return;
  }
  if (g_fd.stage == kPicking) {
    switch (picker_.input(ctx, in)) {
      case app::FilePicker::Result::Chosen: {
        const app::Files::Entry& e = picker_.chosen();
        if (std::strcmp(e.name, kProjectFile) == 0) {
          start_load(ctx, picker_.folder());
        } else if (e.size == 0u) {
          start_load(ctx, e.name);
        } else {
          open_picker(ctx);
          say(ctx, "NOT A PROJECT");
        }
        break;
      }
      case app::FilePicker::Result::Cancelled: g_fd.stage = kMenu; break;
      case app::FilePicker::Result::Browsing: break;
    }
    ctx.redraw();
    return;
  }
  if (g_fd.ask != kNone) {  // F1 confirms, any other key cancels (and is used up by that)
    if (in.type == app::Input::Type::Press || in.pad_press()) {
      const std::uint8_t ask = g_fd.ask;
      g_fd.ask = kNone;
      if (in.press(Button::F1) && !in.shift) {
        if (!refuse(ctx)) {
          if (ask == kLoad) {
            open_picker(ctx);
          } else {
            start_new(ctx);
          }
        }
      } else {
        say(ctx, "CANCELLED");
      }
      ctx.redraw();
    }
    return;
  }
  if (in.press(Button::F1) && !refuse(ctx)) {
    g_fd.ask = kLoad;
  } else if (in.press(Button::F2) && !refuse(ctx)) {
    if (g_fd.has_name) {
      start_save(ctx, g_fd.name, false);
    } else {
      names_.open("SAVE AS", "PROJECT", app::NameEntry::Kind::FileName, 24);
      g_fd.stage = kNaming;
    }
  } else if (in.press(Button::F3) && !refuse(ctx)) {
    names_.open("SAVE AS", "PROJECT", app::NameEntry::Kind::FileName, 24);
    g_fd.stage = kNaming;
  } else if (in.press(Button::F4) && !refuse(ctx)) {
    g_fd.ask = kNew;
  } else if (in.press(Button::F5) && !refuse(ctx)) {
    start_wavs(ctx);
  } else if (in.press(Button::F8)) {
    ctx.show(back_ != nullptr && back_ != this ? *back_ : static_cast<app::Screen&>(*perform_));
  }
  ctx.redraw();
}

// What the project holds now, over the records the last load or save kept (the chain, the live source, the slots without audio).
void FileScreen::capture(app::Context& ctx) noexcept {
  Project& p = g_proj;
  p.bank = *bank_;
  p.chain = *chain_;
  set_project_route(p, perform_->route_now(ctx));
  for (unsigned id = 0; id < Vocoder::kParamCount && id < kEngineIds; ++id) {
    if (engine_id_known(id)) {
      (void)project_set_engine(p, id, ctx.engine().param(voice_, static_cast<std::uint16_t>(id)));
    }
  }
  p.trct = 0;
  if (tract_ != dsp::kNoUnit) {
    for (unsigned i = 0; i < kTractSaved; ++i) {  // inside the unit's range, SIZE a whole choice: a value a knob left between two steps still saves
      float v = ctx.engine().param(tract_, kTractSavedId[i]);
      v = v < kTractSavedMin[i] ? kTractSavedMin[i] : (v > kTractSavedMax[i] ? kTractSavedMax[i] : v);
      p.tract[i] = kTractSavedId[i] == TractUnit::Size ? static_cast<float>(static_cast<int>(v + 0.5f)) : v;
    }
    p.trct = 1;
  }
  const std::uint32_t t = ctx.engine().own_tempo();
  p.stat = 1;
  p.tempo_mbpm = t < kStatTempoMin ? kStatTempoMin : (t > kStatTempoMax ? kStatTempoMax : t);
  p.pattern = static_cast<std::uint8_t>(pattern_->shown() & 7u);
  p.octave = static_cast<std::int8_t>(perform_->octave());
  // The three 1.0 fields (chord kind, pads mode, bend row) are derived from the layout, which is what a 1.1 file loads through.
  const pads::LegacyFields lf = pads::legacy_fields(perform_->layouts()[0]);
  p.chord = lf.chord;
  p.keys = lf.keys;
  p.voicing = static_cast<std::uint8_t>(perform_->voicing());
  p.bend_row = lf.bend_row;
  p.bend_range = static_cast<std::uint8_t>(perform_->bend_range());
  p.follow = pattern_->follow() ? 1u : 0u;
  if (perform_->preset_is_tract()) {  // one saved name: the table the last SHIFT + F8 choice came from
    set_project_tract_preset(p, perform_->tract_preset());
  } else {
    set_project_preset(p, perform_->preset());
  }
  set_project_pads(p, perform_->layouts());
  for (unsigned s = 0; s < route::kSlots; ++s) {
    const sample::SliceTable& t2 = sample_->table(s);
    const std::uint32_t len = t2.length != 0u ? t2.length : sample_->status(s).length;
    fl::merge_slot(p.slot[s], p.slot[s], t2, len);
    p.slot[s].gate = sample_->gate(s) ? 1u : 0u;  // after merge_slot, which rebuilds the record
    p.slot[s].choke_off = 0u;  // the screen is always MONO; the field stays in the file (a loaded 1 is ignored)
    if (len != 0u) {
      // What is live goes in the record: SAMPLE's speed, direction and level, SAMPLE EDIT's trim brackets.
      const Vocoder::LoopStatus st = sample_->status(s);
      fl::SlotValues v;
      v.speed = st.speed;
      v.reverse = st.reverse;
      v.level_db = st.level_db;
      (void)SampleEditScreen::trim_brackets(s, len, v.trim_start, v.trim_end);
      fl::capture_values(p.slot[s], v);
    }
  }
}

// A record's captured values put on its slot (the slot holds audio of the record's length): by the SAMPLE screen's own setters. A record without them (a file from
// before) leaves the slot as it is.
void FileScreen::restore_values(app::Context& ctx, unsigned s, std::uint32_t len) noexcept {
  fl::SlotValues v;
  if (!fl::restore_values(g_proj.slot[s], len, v)) {
    return;
  }
  sample_->set_values(ctx, s, v.speed, v.reverse, v.level_db);
  (void)SampleEditScreen::set_trim_brackets(s, len, v.trim_start, v.trim_end);
}

void FileScreen::set_waiting(unsigned mask) noexcept {
  g_fd.waiting = static_cast<std::uint8_t>(mask & 0xFu);
  if (perform_ != nullptr) {
    perform_->set_slots_waiting(g_fd.waiting);
  }
}

// A save: SAVE writes over the current project's file (the old one kept as .bak); SAVE AS writes a folder that must not hold a project yet.
void FileScreen::start_save(app::Context& ctx, const char* folder, bool as_new) noexcept {
  g_fd.stage = kMenu;
  names_.close();
  capture(ctx);
  const std::size_t n = write_project(g_proj, g_fbuf, sizeof g_fbuf);
  if (n == 0u) {
    say(ctx, "NOT SAVED: A VALUE IS OUT OF RANGE");
    return;
  }
  std::uint32_t live[fl::kWavSlots] = {};
  for (unsigned s = 0; s < fl::kWavSlots && sample_ != nullptr; ++s) {
    live[s] = sample_->table(s).length != 0u ? sample_->table(s).length : sample_->status(s).length;
    live[s] = live[s] > Loop::kMaxSamples ? static_cast<std::uint32_t>(Loop::kMaxSamples) : live[s];
  }
  g_fd.unsaved = fl::unsaved_audio_mask(g_proj, live, audio_dirty().bits);
  const app::Files::Mode mode = as_new ? app::Files::Mode::NewOnly : app::Files::Mode::Replace;
  if (!ctx.files().save(folder, kProjectFile, g_fbuf, n, mode)) {
    say(ctx, ctx.files().status().text);
    return;
  }
  copy_name(g_fd.pending, sizeof g_fd.pending, folder);
  g_fd.pending_has = true;
  g_fd.stage = kSaving;
}

void FileScreen::start_load(app::Context& ctx, const char* folder) noexcept {
  g_fd.stage = kMenu;
  if (!ctx.files().load(folder, kProjectFile, g_fbuf, sizeof g_fbuf)) {
    say(ctx, ctx.files().status().text);
    return;
  }
  copy_name(g_fd.pending, sizeof g_fd.pending, folder);
  g_fd.pending_has = true;
  g_fd.stage = kLoading;
}

// NEW: the defaults go through the same restore as a load; no current project; the slots' audio stays, their cuts go back to even.
void FileScreen::start_new(app::Context& ctx) noexcept {
  g_proj = Project{};
  for (unsigned id = 0; id < Vocoder::kParamCount && id < kEngineIds; ++id) {
    const core::ParamDesc* d = ctx.engine().param_desc(voice_, static_cast<std::uint16_t>(id));
    if (d != nullptr && engine_id_known(id)) {
      (void)project_set_engine(g_proj, id, d->def);
    }
  }
  fl::default_stat(g_proj);
  g_fd.has_name = false;
  g_fd.name[0] = '\0';
  g_fd.pending_has = false;
  g_fd.reset_slots = true;
  g_fd.next = 0;
  g_fd.tnext = 0;
  g_fd.stage = kRestoring;
}

// What drawing needs: the note's expiry and the picker's listing. The jobs are not advanced here but in service().
void FileScreen::update(app::Context& ctx) noexcept {
  if (g_fd.note != nullptr && ctx.uptime_ms() >= g_fd.note_until) {
    g_fd.note = nullptr;
    ctx.redraw();
  }
  if (picker_.active()) {
    picker_.update(ctx);
  }
}

// The jobs' advance, from App::update every pass of the main loop whichever screen is up.
void FileScreen::service(app::Context& ctx) noexcept {
  app::Files& files = ctx.files();
  if (g_fd.stage == kWavList) {
    tick_wav_list(ctx);
    return;
  }
  if (g_fd.stage == kWavSave) {
    tick_wav_save(ctx);
    return;
  }
  if (g_fd.stage == kWavLoad) {
    tick_wav_load(ctx);
    return;
  }
  if (g_fd.stage == kSaving || g_fd.stage == kLoading) {
    const app::Files::Status st = files.status();
    if (st.phase != app::Files::Status::Done && st.phase != app::Files::Status::Failed) {
      return;
    }
    const bool saving = g_fd.stage == kSaving;
    g_fd.stage = kMenu;
    if (st.phase == app::Files::Status::Failed) {
      g_fd.wav_end = false;
      say(ctx, st.text);
      return;
    }
    if (saving) {
      copy_name(g_fd.name, sizeof g_fd.name, g_fd.pending);
      g_fd.has_name = g_fd.pending_has;
      if (g_fd.wav_end) {
        g_fd.wav_end = false;
        unsigned n = 0;
        for (unsigned s = 0; s < fl::kWavSlots; ++s) {
          n += (g_fd.wav_saved_mask >> s) & 1u;
        }
        fl::wavs_saved_note(g_fd.note_buf, sizeof g_fd.note_buf, n);
        say(ctx, g_fd.note_buf);
      } else {
        say(ctx, g_fd.unsaved != 0u ? fl::kSavedUnsavedAudio : "SAVED");  // a note that still says SAVED while a slot's take is only in RAM would mislead
      }
      return;
    }
    if (!read_project(g_fbuf, files.loaded(), g_proj)) {  // the file is decoded into a copy: g_proj is as it was
      say(ctx, "DAMAGED FILE: NOTHING CHANGED");
      return;
    }
    copy_name(g_fd.name, sizeof g_fd.name, g_fd.pending);
    g_fd.has_name = g_fd.pending_has;
    g_fd.reset_slots = false;
    g_fd.next = 0;
    g_fd.tnext = 0;
    g_fd.stage = kRestoring;
  }
  if (g_fd.stage != kRestoring) {
    return;
  }
  unsigned sent = 0;
  while (g_fd.next < kEngineIds && sent < fl::kValuesPerFrame) {
    const unsigned id = fl::next_id(g_proj, g_fd.next);
    if (id >= kEngineIds) {
      g_fd.next = kEngineIds;
      break;
    }
    if (!ctx.engine().set_param(voice_, static_cast<std::uint16_t>(id), g_proj.engine[id])) {
      return;  // the ring is full: the same value again next frame
    }
    g_fd.next = id + 1u;
    ++sent;
  }
  if (g_fd.next < kEngineIds) {
    return;
  }
  // The tract's values (TRCT, V3): the file's, or the unit's defaults when the file has none (NEW, or a file from before). Through the same ring, a few a
  // frame, and not cancellable like the rest of the restore (LE6). After them the four values no file holds (CLOSE, SLIDE, ACCENT: the unit's defaults, and
  // MIX as apply_mode() writes it for the loaded mode: 1 in TRACT, 0 elsewhere), so a load or NEW never leaves a TRACT preset's value behind.
  constexpr unsigned kUnsaved = 4;
  constexpr std::uint16_t kUnsavedId[kUnsaved] = {TractUnit::Close, TractUnit::Slide, TractUnit::Accent, TractUnit::Mix};
  while (tract_ != dsp::kNoUnit && g_fd.tnext < kTractSaved + kUnsaved && sent < fl::kValuesPerFrame) {
    float v = 0.0f;
    std::uint16_t id = 0;
    if (g_fd.tnext < kTractSaved) {
      id = kTractSavedId[g_fd.tnext];
      const core::ParamDesc* d = ctx.engine().param_desc(tract_, id);
      v = g_proj.trct != 0u ? g_proj.tract[g_fd.tnext] : (d != nullptr ? d->def : 0.0f);
    } else {
      id = kUnsavedId[g_fd.tnext - kTractSaved];
      if (id == TractUnit::Mix) {
        const float m = project_has_engine(g_proj, Vocoder::Mode) ? g_proj.engine[Vocoder::Mode] : ctx.engine().param(voice_, Vocoder::Mode);
        v = mode_index(m) == kModeTract ? 1.0f : 0.0f;
      } else {
        const core::ParamDesc* d = ctx.engine().param_desc(tract_, id);
        v = d != nullptr ? d->def : 0.0f;
      }
    }
    if (!ctx.engine().set_param(tract_, id, v)) {
      return;
    }
    ++g_fd.tnext;
    ++sent;
  }
  if (tract_ == dsp::kNoUnit || g_fd.tnext >= kTractSaved + kUnsaved) {
    apply(ctx, true);
  }
}

void FileScreen::apply_route(app::Context& ctx) noexcept {
  const route::Route from = perform_->route_now(ctx);
  const route::Route to = project_route(g_proj);
  if (from.speaks == to.speaks && from.sounds == to.sounds) {
    return;
  }
  const route::Route mid_a = {to.speaks, from.sounds};  // the speaks side first, unless a slot would sit on both sides
  if (route::allowed(mid_a)) {
    perform_->apply_route(ctx, route::Side::Speaks, from, mid_a);
    perform_->apply_route(ctx, route::Side::Sounds, mid_a, to);
  } else {
    const route::Route mid_b = {from.speaks, to.sounds};
    perform_->apply_route(ctx, route::Side::Sounds, from, mid_b);
    perform_->apply_route(ctx, route::Side::Speaks, mid_b, to);
  }
}

void FileScreen::apply_slots(app::Context& ctx) noexcept {
  g_fd.restored = 0;
  g_fd.kept = 0;
  for (unsigned s = 0; s < route::kSlots; ++s) {
    sample::SliceTable& t = sample_->table(s);
    const std::uint32_t len = t.length != 0u ? t.length : sample_->status(s).length;
    if (g_fd.reset_slots) {
      t = sample::SliceTable{};
      sample_->set_edited(s, false);
      continue;
    }
    const ProjectSlot& rec = g_proj.slot[s];
    if (rec.cuts.length == 0u) {
      continue;
    }
    if (fl::slot_fits(rec, len)) {
      t = rec.cuts;
      sample_->set_edited(s, true);
      restore_values(ctx, s, len);
      ++g_fd.restored;
    } else {
      ++g_fd.kept;
    }
  }
  sample_->cuts_changed();
}

// The values are in: the patterns, then the route, the slots, and STAT's fields.
void FileScreen::apply(app::Context& ctx, bool wavs) noexcept {
  *bank_ = g_proj.bank;
  *chain_ = g_proj.chain;
  pattern_->show(ctx, g_proj.stat != 0u ? g_proj.pattern : pattern_->shown());
  if (song_ != nullptr) {
    song_->chain_replaced();  // after the pattern's own request: a chain that walks keeps the last word on what plays next
  }
  apply_route(ctx);
  apply_slots(ctx);
  perform_->notes_reset();  // the notes palette is in no file: a load and NEW both put A NOTE, B TRIAD back (SP1)
  if (g_proj.stat != 0u) {
    (void)ctx.engine().set_tempo(g_proj.tempo_mbpm);
    perform_->set_octave(g_proj.octave);
    // STAT 1.1 (a 1.0 file arrives here with the defaults of today and the legacy layout, read_stat's doing).
    perform_->set_voicing(g_proj.voicing);
    (void)perform_->set_bend_range(g_proj.bend_range);
    pattern_->set_follow(g_proj.follow != 0u);
    const int preset = project_preset_index(g_proj);
    perform_->set_preset(preset < 0 ? perform::kNoPreset : static_cast<unsigned>(preset));
    const int tpreset = project_tract_preset_index(g_proj);  // the name is in at most one table (presets_test)
    perform_->set_tract_preset(tpreset < 0 ? perform::kNoPreset : static_cast<unsigned>(tpreset));
    perform_->set_preset_is_tract(tpreset >= 0);
    pads::Layout lay[kModeCount];
    fl::project_layouts(g_proj, lay);  // an older file's one layout serves every mode but TRACT
    perform_->set_layouts(lay);
  }
  for (unsigned s = 0; s < route::kSlots; ++s) {  // TRIG rides with every record, audio or not (a record without it is ONE SHOT)
    sample_->set_gate(s, g_proj.slot[s].gate != 0u);
  }
  g_fd.stage = kMenu;
  if (!g_fd.reset_slots && wavs && fl::next_load_slot(g_proj, 0) < fl::kWavSlots) {
    begin_wav_loads(ctx);  // the strip tells the run; its end tells the result
    return;
  }
  if (g_fd.reset_slots) {
    say(ctx, fl::kNewDone);
  } else {
    fl::loaded_note(g_fd.note_buf, sizeof g_fd.note_buf, g_fd.restored, g_fd.kept);
    say(ctx, g_fd.note_buf);
  }
  g_fd.reset_slots = false;
}

bool FileScreen::wav_stage() const noexcept { return g_fd.stage >= kWavList; }

// F5: the WAV of every slot that holds audio goes into the project's folder, one card job after another (slotN_NNN.wav, the kit's first free number: the folder is listed
// first because the kit does not tell the name it chose), then the project file is saved again so that its records name them. A project that was never saved has no folder.
void FileScreen::start_wavs(app::Context& ctx) noexcept {
  if (!g_fd.has_name) {
    say(ctx, fl::kNameFirstLine);
    return;
  }
  if (mem_ == nullptr || sample_ == nullptr) {
    say(ctx, "NO LOOP MEMORY");
    return;
  }
  unsigned total = 0;
  for (unsigned s = 0; s < fl::kWavSlots; ++s) {
    const std::uint32_t len = sample_->table(s).length != 0u ? sample_->table(s).length : sample_->status(s).length;
    g_fd.lens[s] = len > Loop::kMaxSamples ? static_cast<std::uint32_t>(Loop::kMaxSamples) : len;
    total += len != 0u ? 1u : 0u;
  }
  if (total == 0u) {
    say(ctx, "NO SLOT HOLDS AUDIO");
    return;
  }
  if (!ctx.files().list(g_fd.name, g_wl, kWlEntries)) {
    say(ctx, ctx.files().status().text);
    return;
  }
  g_fd.wav_total = total;
  g_fd.wav_done = 0;
  g_fd.wav_saved_mask = 0;
  g_fd.wav_end = false;
  g_fd.wav_cur = fl::next_save_slot(g_fd.lens, 0);
  copy_name(g_fd.line, sizeof g_fd.line, "SAVING WAVS: READING THE FOLDER");
  g_fd.stage = kWavList;
}

void FileScreen::tick_wav_list(app::Context& ctx) noexcept {
  app::Files& files = ctx.files();
  const app::Files::Status st = files.status();
  if (st.phase != app::Files::Status::Done && st.phase != app::Files::Status::Failed) {
    return;
  }
  g_fd.stage = kMenu;
  if (st.phase == app::Files::Status::Failed) {
    say(ctx, st.text);
    return;
  }
  if (files.list_truncated()) {
    say(ctx, "TOO MANY FILES IN THE FOLDER");
    return;
  }
  const std::size_t n = files.listed() < kWlEntries ? files.listed() : kWlEntries;
  for (unsigned s = 0; s < fl::kWavSlots; ++s) {
    if (g_fd.lens[s] == 0u) {
      continue;
    }
    char stem[6];
    fl::wav_stem(stem, s);
    const unsigned num = fl::first_free_wav_number(stem, n, [](std::size_t i) { return static_cast<const char*>(g_wl[i].name); });
    if (num == 0u) {
      say(ctx, "NO FREE NUMBER FOR A WAV");
      return;
    }
    fl::wav_file_name(g_fd.wavname[s], stem, num);
  }
  begin_wav_save(ctx);
}

void FileScreen::begin_wav_save(app::Context& ctx) noexcept {
  const unsigned s = g_fd.wav_cur;
  char stem[6];
  fl::wav_stem(stem, s);
  fl::wav_job_line(g_fd.line, sizeof g_fd.line, "SAVING", s, g_fd.wav_done, g_fd.wav_total);
  if (!ctx.files().save_wav(g_fd.name, mem_ + static_cast<std::size_t>(s) * Loop::kMaxSamples, g_fd.lens[s], 1u, 44100u, stem)) {
    g_fd.stage = kMenu;
    say(ctx, ctx.files().status().text);
    return;
  }
  g_fd.stage = kWavSave;
  ctx.redraw();
}

void FileScreen::tick_wav_save(app::Context& ctx) noexcept {
  app::Files& files = ctx.files();
  const app::Files::Status st = files.status();
  if (st.phase != app::Files::Status::Done && st.phase != app::Files::Status::Failed) {
    return;
  }
  if (st.phase == app::Files::Status::Failed) {  // the WAVs already written stay on the card; the project file is not touched
    g_fd.stage = kMenu;
    say(ctx, st.text);
    return;
  }
  g_fd.wav_saved_mask |= 1u << g_fd.wav_cur;
  audio_dirty().clear(g_fd.wav_cur);  // the card now holds this take
  ++g_fd.wav_done;
  const unsigned next = fl::next_save_slot(g_fd.lens, g_fd.wav_cur + 1u);
  if (next < fl::kWavSlots) {
    g_fd.wav_cur = next;
    begin_wav_save(ctx);
    return;
  }
  // The last WAV is down: the project with the records naming them (capture first: it keeps a name only for audio the record already fits).
  g_fd.stage = kMenu;
  capture(ctx);
  for (unsigned s = 0; s < fl::kWavSlots; ++s) {
    if (((g_fd.wav_saved_mask >> s) & 1u) != 0u) {
      (void)fl::set_wav_name(g_proj.slot[s], g_fd.wavname[s]);
    }
  }
  const std::size_t n = write_project(g_proj, g_fbuf, sizeof g_fbuf);
  if (n == 0u) {
    say(ctx, "NOT SAVED: A VALUE IS OUT OF RANGE");
    return;
  }
  if (!files.save(g_fd.name, kProjectFile, g_fbuf, n, app::Files::Mode::Replace)) {
    say(ctx, files.status().text);
    return;
  }
  copy_name(g_fd.pending, sizeof g_fd.pending, g_fd.name);
  g_fd.pending_has = true;
  g_fd.wav_end = true;
  g_fd.stage = kSaving;
}

// After a project load: each record that names a WAV, in slot order, is read from the project folder into its slot (SampleLoadScreen's job), and its cuts go back when
// it has landed. A file that is missing or refused skips that slot (the record stays for the next save); the others go on.
void FileScreen::begin_wav_loads(app::Context& ctx) noexcept {
  g_fd.wav_cur = 0;
  g_fd.wav_done = 0;
  g_fd.wav_total = 0;
  for (unsigned s = 0; s < fl::kWavSlots; ++s) {
    g_fd.wav_total += g_proj.slot[s].name_len != 0u ? 1u : 0u;
  }
  unsigned grey = 0;
  for (unsigned s = 0; s < fl::kWavSlots; ++s) {
    grey |= g_proj.slot[s].name_len != 0u ? 1u << s : 0u;
  }
  set_waiting(grey);  // every slot a WAV is coming for is grey until it has landed or been skipped
  g_fd.wl_loaded = 0;
  g_fd.wl_skipped = 0;
  g_fd.wl_first = 0;
  g_fd.why[0] = '\0';
  g_fd.wl_settle = false;
  next_wav_load(ctx);
}

void FileScreen::skip_wav(const char* why) noexcept {
  set_waiting(g_fd.waiting & ~(1u << (g_fd.wav_cur & 3u)));
  if (g_fd.wl_skipped == 0u) {
    g_fd.wl_first = g_fd.wav_cur;
    copy_name(g_fd.why, sizeof g_fd.why, why != nullptr && why[0] != '\0' ? why : "NOT LOADED");
  }
  ++g_fd.wl_skipped;
  ++g_fd.wav_done;
  g_fd.wav_cur += 1u;
  g_fd.wl_settle = false;
}

void FileScreen::next_wav_load(app::Context& ctx) noexcept {
  for (unsigned guard = 0; guard <= fl::kWavSlots; ++guard) {
    const unsigned s = fl::next_load_slot(g_proj, g_fd.wav_cur);
    if (s >= fl::kWavSlots) {
      break;
    }
    g_fd.wav_cur = s;
    g_fd.wl_settle = false;
    fl::wav_job_line(g_fd.line, sizeof g_fd.line, "LOADING", s, g_fd.wav_done, g_fd.wav_total);
    fl::wav_wait_line(g_fd.wait, sizeof g_fd.wait, s);
    if (load_ != nullptr && load_->begin_external(ctx, s, g_fd.name, g_proj.slot[s].name)) {
      g_fd.stage = kWavLoad;
      ctx.redraw();
      return;
    }
    skip_wav(load_ != nullptr ? load_->external_text() : "NO LOADER");
  }
  end_wav_loads(ctx);
}

// Threading (LE5): both steps of an open run on the main loop, in App::update's order (file_.service, then sample_.service), so `Done` and `wl_settle` cannot
// change between them: external() reads the job's own state (written by tick_external a line above) and wl_settle is only written here. The one value
// the audio side writes is the slot's length, which Vocoder publishes as one relaxed atomic word (status_[slot]); it is read once into `len` and every
// later use below (seen, slot_fits, restore_values) takes that copy, so the pair "length and cuts" cannot tear. A length that lands after `Done` (the
// unit drains the writes at its next block) is simply not seen yet, and wl_deadline ends the wait; the record's cuts then stay the even ones.
void FileScreen::tick_wav_load(app::Context& ctx) noexcept {
  const unsigned s = g_fd.wav_cur;
  if (!g_fd.wl_settle) {
    load_->tick_external(ctx);
    const SampleLoadScreen::External e = load_->external();
    if (e == SampleLoadScreen::External::Running) {
      return;
    }
    if (e != SampleLoadScreen::External::Done) {
      skip_wav(load_->external_text());
      next_wav_load(ctx);
      return;
    }
    g_fd.wl_settle = true;
    g_fd.wl_deadline = ctx.uptime_ms() + 500u;
  }
  // The slot shows its new length, and SAMPLE's service() has cut it evenly (it runs every pass): only then do the record's cuts go on it, and only if they fit.
  const std::uint32_t len = sample_->status(s).length;
  const bool seen = len != 0u && sample_->table(s).length == len;
  if (!seen && ctx.uptime_ms() < g_fd.wl_deadline) {
    return;
  }
  if (seen && fl::slot_fits(g_proj.slot[s], len)) {
    sample_->table(s) = g_proj.slot[s].cuts;
    sample_->set_edited(s, true);
    sample_->cuts_changed();
  }
  if (seen) {
    restore_values(ctx, s, len);  // after the landing: the load puts the slot's own speed, direction, level and trim, and these go over them
  }
  audio_dirty().clear(s);  // the slot holds the project's own WAV again
  set_waiting(g_fd.waiting & ~(1u << (s & 3u)));
  ++g_fd.wl_loaded;
  ++g_fd.wav_done;
  g_fd.wav_cur = s + 1u;
  g_fd.wl_settle = false;
  next_wav_load(ctx);
}

void FileScreen::end_wav_loads(app::Context& ctx) noexcept {
  set_waiting(0);
  g_fd.stage = kMenu;
  fl::wavs_loaded_note(g_fd.note_buf, sizeof g_fd.note_buf, g_fd.wl_loaded, g_fd.wl_skipped, g_fd.wl_first, g_fd.why);
  say(ctx, g_fd.note_buf);
}

// F8: the job in hand is given up (the kit removes a half WAV; a copy into a slot is finished and sent), the rest of the run is not started.
void FileScreen::abort_wavs(app::Context& ctx) noexcept {
  if (g_fd.stage == kWavList || g_fd.stage == kWavSave) {
    ctx.files().cancel();
  } else if (g_fd.stage == kWavLoad && !g_fd.wl_settle && load_ != nullptr) {
    load_->abort_external(ctx);
  }
  g_fd.wav_end = false;
  g_fd.wl_settle = false;
  set_waiting(0);
  g_fd.stage = kMenu;
  say(ctx, "CANCELLED");
}

// UX3-3: the pads do nothing on FILE (the help says so), so none is lit (the runtime clears the LEDs before every screen's leds()).
void FileScreen::leds(app::Context&, app::PadLeds&) noexcept {}

void FileScreen::soft_keys(app::Context&, app::SoftKeys& out) noexcept {
  if (g_fd.stage == kNaming) {
    names_.soft_keys(out);
    return;
  }
  out.label[0] = g_fd.stage == kPicking ? "OPEN" : "LOAD";
  out.label[1] = "SAVE";
  out.label[2] = "SAVE AS";
  out.label[3] = "NEW";
  out.label[4] = "SAVE WAVS";
  out.label[7] = wav_stage() || kit_job_stage(g_fd.stage) ? "CANCEL" : "BACK";
  out.hot = 0;
  const bool locked = g_fd.stage >= kSaving || g_fd.ask != kNone;
  out.dim = static_cast<std::uint8_t>((locked ? 0x9Fu : 0u) | (g_fd.stage == kPicking ? 0x1Eu : 0u) | (!g_fd.has_name ? 0x10u : 0u));
  if (g_fd.stage == kPicking) {
    out.dim = static_cast<std::uint8_t>(out.dim & ~0x01u);
  }
  if (g_fd.stage >= kSaving && !wav_stage()) {
    out.dim = static_cast<std::uint8_t>(out.dim | 0x80u);
  }
  if (wav_stage() || kit_job_stage(g_fd.stage)) {
    out.dim = static_cast<std::uint8_t>(out.dim & ~0x80u);  // F8 gives the run, the save or the load up
  }
}

void FileScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Up, "LIST: ROW BEFORE", nullptr},
      {Button::Down, "LIST: ROW AFTER", nullptr},
      {Button::Left, "LIST: PAGE UP", nullptr},
      {Button::Right, "LIST: PAGE DOWN", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::File, "THIS SCREEN", nullptr},
  };
  if (sample_ != nullptr) {
    sample_->help(ctx, h);
  }
  h.summary = "THE PROJECT FILE: PATTERNS, CHAIN, ROUTE, CUTS, THE UNIT'S VALUES AND THE STATE, IN A FOLDER OF APPS/O3_VOC03ER. F5 ADDS THE SLOTS' WAVS, AND A LOAD READS THEM BACK.";
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    h.fkey[i] = nullptr;
    h.fkey_shift[i] = nullptr;
  }
  h.fkey[0] = "LOADS A PROJECT";
  h.fkey[1] = "SAVES THE PROJECT";
  h.fkey[2] = "SAVES UNDER A NEW NAME";
  h.fkey[3] = "STARTS A NEW PROJECT";
  h.fkey[4] = "SAVES THE SLOTS' WAVS";
  h.fkey[7] = "BACK";
  if (wav_stage() || kit_job_stage(g_fd.stage)) {
    h.fkey[7] = "CANCELS THE JOB";
    h.summary = kit_job_stage(g_fd.stage) ? "A CANCELLED SAVE KEEPS THE OLD FILE WHOLE, AND A CANCELLED LOAD CHANGES NOTHING."
                                          : "A CANCELLED WAV RUN STOPS AT THE JOB IN HAND: THE WAVS ALREADY WRITTEN OR LOADED STAY.";
  } else if (g_fd.stage == kRestoring) {
    h.fkey[7] = "WAIT: IT ENDS ALONE";
    h.summary = "A LOAD IS APPLYING THE PROJECT: IT FINISHES ON ITS OWN, BECAUSE STOPPING HALF WAY WOULD LEAVE TWO PROJECTS MIXED.";
  }
  h.pads = "A PAD PICKS THE LIST ROW UNDER IT.";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "MOVES THE CURSOR IN THE LIST";
}

// The four slots as PERFORM shows them: a box each, grey while its WAV is on its way (the picker's list takes this place when it is up).
void FileScreen::slots_draw(ui::Renderer& r) const noexcept {
  std::uint8_t st[4];
  for (unsigned s = 0; s < 4u; ++s) {
    st[s] = ((g_fd.waiting >> s) & 1u) != 0u ? ornoir::kSlotWaiting : (sample_->status(s).length != 0u ? ornoir::kSlotAudio : ornoir::kSlotEmpty);
  }
  ui::plot::Canvas c(r);
  c.open();
  ornoir::slot_shapes(c, kX + 90, 270, st);
  c.close();
  ornoir::slot_text(r, kX + 90, 270, st);
}

void FileScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect& area) noexcept {
  if (sample_ == nullptr) {
    return;
  }
  ornoir::HeaderState h;
  sample_->fill_header(ctx, h);
  ornoir::FKey keys[ornoir::kFKeys];
  app::SoftKeys sk;
  soft_keys(ctx, sk);
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    keys[i].name = sk.label[i] != nullptr ? sk.label[i] : "";
    keys[i].dim = ((sk.dim >> i) & 1u) != 0u;
  }
  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  if (g_fd.stage == kNaming) {
    names_.draw(r, area);
    return;
  }
  if (g_fd.stage == kPicking && picker_.active()) {
    picker_.draw(r, ui::Rect{kX, kListY, kW, kListH});
  } else {
    r.text(kX, 88, "PROJECT", Color::TextDim, Face::Label);
    const char* name = !g_fd.has_name ? "NONE YET" : (g_fd.name[0] == '\0' ? "THE APP'S FOLDER" : g_fd.name);
    r.text(kX, 112, name, Color::Text, Face::Display);
    r.text(kX, 190, "F1 LOADS A PROJECT, F2 SAVES IT, F3 SAVES IT UNDER A NEW NAME, F4 STARTS A NEW ONE (THE SLOTS KEEP THEIR AUDIO)", Color::TextDim, Face::Micro);
    r.text(kX, 210, "THE FILE HOLDS THE PATTERNS, THE CHAIN, THE ROUTE, THE CUTS, THE UNIT'S VALUES, TEMPO AND OCTAVE", Color::TextDim, Face::Micro);
    r.text(kX, 230, "F5 SAVES THE SLOTS' WAVS INTO THE FOLDER: A LOAD READS THEM BACK, THEN THE CUTS, TRIM, SPEED, DIRECTION AND LEVEL GO ON THEM", Color::TextDim, Face::Micro);
    r.text(kX, 276, "SLOTS", Color::TextDim, Face::Label);
    slots_draw(r);
  }
  const char* strip = nullptr;
  if (g_fd.stage == kSaving) {
    strip = "SAVING";
  } else if (g_fd.stage == kLoading) {
    strip = "LOADING";
  } else if (g_fd.stage == kRestoring) {
    strip = "RESTORING";
  } else if (wav_stage()) {
    strip = g_fd.line;
    if (g_fd.stage == kWavLoad) {
      r.text(kX, kNoteY - 26, g_fd.wait, Color::TextDim, Face::Label);
    }
  } else if (g_fd.ask == kLoad) {
    strip = "LOAD REPLACES THE PROJECT: F1 CONFIRMS";
  } else if (g_fd.ask == kNew) {
    strip = fl::kNewAsk;
  } else if (g_fd.note != nullptr) {
    strip = g_fd.note;
  }
  if (strip != nullptr) {
    r.text(kX, kNoteY, strip, Color::Accent, Face::Label);
  }
}

}  // namespace o3_voc03er
