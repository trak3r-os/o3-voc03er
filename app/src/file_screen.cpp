// SPDX-License-Identifier: GPL-3.0-or-later
#include "file_screen.h"

#include "file_logic.h"
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
};
enum Ask : std::uint8_t { kNone, kLoad, kNew };

// The screen's state, in the SDRAM (O3SDK_BIG: zero at start, main loop only).
struct FileData {
  std::uint8_t stage;
  std::uint8_t ask;
  bool has_name;      // a current project (name "" is the app's own folder)
  bool pending_has;   // the folder of the job in hand becomes the current project when it ends
  bool reset_slots;   // NEW: the slots' cuts go back to even
  unsigned next;      // the next engine id the restore looks at
  unsigned restored, kept;
  char name[40];
  char pending[40];
  const char* note;
  std::uint64_t note_until;
  char note_buf[72];
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

FileScreen::FileScreen() noexcept : picker_(g_pick, kPickEntries) {}

void FileScreen::init(dsp::UnitId voice, SampleScreen* sample, PerformScreen* perform, PatternScreen* pattern, Bank* bank, Chain* chain) noexcept {
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
  ctx.redraw();
}

void FileScreen::enter(app::Context& ctx) noexcept {
  g_fd.stage = kMenu;
  g_fd.ask = kNone;
  g_fd.note = nullptr;
  ctx.redraw();
}

void FileScreen::leave(app::Context& ctx) noexcept {
  if (g_fd.stage == kLoading || g_fd.stage == kSaving) {
    ctx.files().cancel();  // the buffers are ours again when the job has ended; a half-read file is never decoded
  }
  if (g_fd.stage == kRestoring) {
    g_fd.next = kEngineIds;  // the rest of a restore is done on the spot (a few dozen values; a full ring leaves the rest as it is)
    apply(ctx);
  }
  g_fd.stage = kMenu;
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
  if (g_fd.stage >= kSaving) {  // a job in hand: its buffers stay as they are
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
  } else if (in.press(Button::F5)) {
    say(ctx, "SAVE WAVS COMES LATER");
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
  const std::uint32_t t = ctx.engine().own_tempo();
  p.stat = 1;
  p.tempo_mbpm = t < kStatTempoMin ? kStatTempoMin : (t > kStatTempoMax ? kStatTempoMax : t);
  p.swing = 0;
  p.pattern = static_cast<std::uint8_t>(pattern_->shown() & 7u);
  p.octave = static_cast<std::int8_t>(perform_->octave());
  p.chord = static_cast<std::uint8_t>(perform_->chord_kind());
  p.keys = perform_->keys_mode() ? 1u : 0u;
  for (unsigned s = 0; s < route::kSlots; ++s) {
    const sample::SliceTable& t2 = sample_->table(s);
    const std::uint32_t len = t2.length != 0u ? t2.length : sample_->status(s).length;
    fl::merge_slot(p.slot[s], p.slot[s], t2, len);
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
  g_fd.stage = kRestoring;
}

void FileScreen::update(app::Context& ctx) noexcept {
  if (g_fd.note != nullptr && ctx.uptime_ms() >= g_fd.note_until) {
    g_fd.note = nullptr;
    ctx.redraw();
  }
  if (picker_.active()) {
    picker_.update(ctx);
  }
  tick(ctx);
}

void FileScreen::tick(app::Context& ctx) noexcept {
  app::Files& files = ctx.files();
  if (g_fd.stage == kSaving || g_fd.stage == kLoading) {
    const app::Files::Status st = files.status();
    if (st.phase != app::Files::Status::Done && st.phase != app::Files::Status::Failed) {
      return;
    }
    const bool saving = g_fd.stage == kSaving;
    g_fd.stage = kMenu;
    if (st.phase == app::Files::Status::Failed) {
      say(ctx, st.text);
      return;
    }
    if (saving) {
      copy_name(g_fd.name, sizeof g_fd.name, g_fd.pending);
      g_fd.has_name = g_fd.pending_has;
      say(ctx, "SAVED");
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
  if (g_fd.next >= kEngineIds) {
    apply(ctx);
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
      ++g_fd.restored;
    } else {
      ++g_fd.kept;
    }
  }
  sample_->cuts_changed();
  (void)ctx;
}

// The values are in: the patterns, then the route, the slots, and STAT's fields.
void FileScreen::apply(app::Context& ctx) noexcept {
  *bank_ = g_proj.bank;
  *chain_ = g_proj.chain;
  pattern_->show(ctx, g_proj.stat != 0u ? g_proj.pattern : pattern_->shown());
  apply_route(ctx);
  apply_slots(ctx);
  if (g_proj.stat != 0u) {
    (void)ctx.engine().set_tempo(g_proj.tempo_mbpm);
    perform_->set_octave(g_proj.octave);
    perform_->set_chord_kind(g_proj.chord);
    perform_->set_keys(g_proj.keys != 0u);
  }
  g_fd.stage = kMenu;
  if (g_fd.reset_slots) {
    say(ctx, "NEW PROJECT. THE SLOTS' AUDIO STAYS");
  } else {
    fl::loaded_note(g_fd.note_buf, sizeof g_fd.note_buf, g_fd.restored, g_fd.kept);
    say(ctx, g_fd.note_buf);
  }
  g_fd.reset_slots = false;
}

void FileScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  if (sample_ != nullptr) {
    sample_->pad_leds(ctx, leds);
  }
}

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
  out.label[7] = "BACK";
  out.hot = 0;
  const bool locked = g_fd.stage >= kSaving || g_fd.ask != kNone;
  out.dim = static_cast<std::uint8_t>(0x60u | 0x10u | (locked ? 0x8Fu : 0u) | (g_fd.stage == kPicking ? 0x0Eu : 0u));
  if (g_fd.stage == kPicking) {
    out.dim = static_cast<std::uint8_t>(out.dim & ~0x01u);
  }
  if (g_fd.stage >= kSaving) {
    out.dim = static_cast<std::uint8_t>(out.dim | 0x80u);
  }
}

void FileScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::File, "THIS SCREEN", nullptr},
  };
  if (sample_ != nullptr) {
    sample_->help(ctx, h);
  }
  h.summary = "THE PROJECT FILE: PATTERNS, CHAIN, ROUTE, CUTS, THE UNIT'S VALUES AND THE STATE, IN A FOLDER OF APPS/O3_VOC03ER. THE WAVS COME LATER.";
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    h.fkey[i] = nullptr;
    h.fkey_shift[i] = nullptr;
  }
  h.fkey[0] = "LOADS A PROJECT";
  h.fkey[1] = "SAVES THE PROJECT";
  h.fkey[2] = "SAVES UNDER A NEW NAME";
  h.fkey[3] = "STARTS A NEW PROJECT";
  h.fkey[4] = "COMES LATER";
  h.fkey[7] = "BACK";
  h.pads = "NOT USED HERE.";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "MOVES THE CURSOR IN THE LIST";
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
    r.text(kX, 190, "F1 LOADS A PROJECT, F2 SAVES IT, F3 SAVES IT UNDER A NEW NAME, F4 STARTS A NEW ONE", Color::TextDim, Face::Micro);
    r.text(kX, 210, "THE FILE HOLDS THE PATTERNS, THE CHAIN, THE ROUTE, THE CUTS, THE UNIT'S VALUES, TEMPO AND OCTAVE", Color::TextDim, Face::Micro);
    r.text(kX, 230, "THE WAVS ARE NOT IN IT YET: A SLOT'S CUTS COME BACK ONLY ONTO A SAMPLE OF THE SAME LENGTH", Color::TextDim, Face::Micro);
  }
  const char* strip = nullptr;
  if (g_fd.stage == kSaving) {
    strip = "SAVING";
  } else if (g_fd.stage == kLoading) {
    strip = "LOADING";
  } else if (g_fd.stage == kRestoring) {
    strip = "RESTORING";
  } else if (g_fd.ask == kLoad) {
    strip = "LOAD REPLACES THE PROJECT: F1 CONFIRMS";
  } else if (g_fd.ask == kNew) {
    strip = "NEW CLEARS THE PROJECT: F1 CONFIRMS";
  } else if (g_fd.note != nullptr) {
    strip = g_fd.note;
  }
  if (strip != nullptr) {
    r.text(kX, kNoteY, strip, Color::Accent, Face::Label);
  }
}

}  // namespace o3_voc03er
