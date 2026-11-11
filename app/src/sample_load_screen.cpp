// SPDX-License-Identifier: GPL-3.0-or-later
#include "sample_load_screen.h"

#include "overview.h"
#include "sample_edit_logic.h"
#include "sample_edit_screen.h"
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
namespace sl = sample_load;
namespace se = sample_edit;

namespace {

constexpr std::uint64_t kNoteMs = 3000;
constexpr std::uint32_t kClearMs = 1000;  // how long the slot may take to show length 0 after the clear
constexpr std::uint32_t kLoadMs = 1000;   // how long Load is sent while the unit has not adopted the length
constexpr std::uint32_t kResendMs = 100;
constexpr std::size_t kEntries = 64;      // the picker's listing (3 KB in the SDRAM); a longer folder is cut

enum Stage : std::uint8_t {
  kBrowse,    // the picker is up (or an ask is)
  kReading,   // the card job fills the stage
  kClearing,  // the slot's old sample is being emptied
  kCopying,   // the stage goes into the slot memory, a slice a pass
  kWaiting,   // Load has gone: the unit shows the length
};

// The screen's state, in the SDRAM (O3SDK_BIG: zero at start, main loop only).
struct LoadData {
  std::uint8_t stage;
  bool asking;                  // REPLACES SLOT n is on the strip: F1 confirms
  std::uint8_t slot;            // the slot of the job in hand
  sl::Write w;
  sl::Info info;
  app::Files::Entry entry;      // the file asked for
  char folder[40];              // its folder ("" for the app's own, the picker's case; a project folder for FILE's entry)
  bool ext;                     // FILE drives this job (begin_external): no picker, no strip line of this screen, the end is told by ext_state
  bool ext_ok;                  // the job in hand has reached its good end
  std::uint8_t ext_state;       // External
  const char* ext_text;
  std::uint64_t deadline, resend;
  const char* note;             // a moment's line (a static string or note_buf), until note_until
  std::uint64_t note_until;
  char note_buf[64];
};
O3SDK_BIG LoadData g_sl = {};
O3SDK_BIG app::Files::Entry g_entries[kEntries] = {};
// The file as the card job delivers it: the header, then 16 s of stereo at most (a longer file is cut at the end of the stage).
alignas(4) O3SDK_BIG std::uint8_t g_stage[sl::kStageBytes];
static_assert(sizeof g_stage == sl::kStageBytes, "the stage is the logic's size");

constexpr int kX = 26;
constexpr int kW = 748;
constexpr int kListY = 128;
constexpr int kListH = 250;
constexpr int kNoteY = 398;
constexpr const char* kSlotNames[4] = {"SLOT 1", "SLOT 2", "SLOT 3", "SLOT 4"};

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

// "1.234 S" style: seconds with three decimals.
void cat_seconds(char* dst, std::size_t cap, std::uint32_t frames) noexcept {
  char tm[12];
  se::time_text(frames, tm);
  cat(dst, cap, tm);
  cat(dst, cap, " S");
}

// A file size as "123 KB" or "1.2 MB" (whole numbers below 10 MB would hide a 16-bit file's length, so one decimal there).
void cat_size(char* dst, std::size_t cap, std::uint32_t bytes) noexcept {
  if (bytes >= 1024u * 1024u) {
    const unsigned tenths = static_cast<unsigned>((static_cast<std::uint64_t>(bytes) * 10u) / (1024u * 1024u));
    cat_uint(dst, cap, tenths / 10u);
    cat(dst, cap, ".");
    cat_uint(dst, cap, tenths % 10u);
    cat(dst, cap, " MB");
  } else {
    cat_uint(dst, cap, (bytes + 1023u) / 1024u);
    cat(dst, cap, " KB");
  }
}

int centred(ui::Renderer& r, const char* s, Face f, int cx) noexcept { return cx - r.text_width(s, f) / 2; }

}  // namespace

SampleLoadScreen::SampleLoadScreen() noexcept : picker_(g_entries, kEntries) {}

void SampleLoadScreen::init(SampleScreen* sample, std::int16_t* mem) noexcept {
  sample_ = sample;
  mem_ = mem;
}

const ui::Theme* SampleLoadScreen::theme(const app::Context&) const noexcept { return &ornoir::theme(); }

bool SampleLoadScreen::loading() const noexcept { return g_sl.stage != kBrowse; }

void SampleLoadScreen::say(app::Context& ctx, const char* text) noexcept {
  if (g_sl.ext) {  // FILE's job: the reason goes to the driver (a success line has no reader)
    g_sl.ext_text = g_sl.ext_ok ? "" : text;
    return;
  }
  g_sl.note = text;
  g_sl.note_until = ctx.uptime_ms() + kNoteMs;
  ctx.redraw();
}

void SampleLoadScreen::open_picker(app::Context& ctx) noexcept {
  g_sl.asking = false;
  (void)picker_.open(ctx, "WAV FILES", "", "wav");
  ctx.redraw();
}

void SampleLoadScreen::go_back(app::Context& ctx) noexcept {
  ctx.show(back_ != nullptr ? *back_ : static_cast<app::Screen&>(*sample_));
  ctx.redraw();
}

void SampleLoadScreen::enter(app::Context& ctx) noexcept {
  g_sl.ext = false;  // an ended job of FILE's is forgotten; one in hand is never here (FILE gives it up when it is left)
  g_sl.ext_state = 0;
  g_sl.folder[0] = '\0';
  g_sl.stage = kBrowse;
  g_sl.asking = false;
  g_sl.note = nullptr;
  if (back_ == nullptr || back_ == this) {
    back_ = sample_;
  }
  open_picker(ctx);
}

// The screen is left at any stage: the read is dropped, a write in hand is finished and sent (it is bounded: at most 16 s of samples), the rest is reset.
void SampleLoadScreen::leave(app::Context& ctx) noexcept {
  switch (g_sl.stage) {
    case kReading: ctx.files().cancel(); break;
    case kCopying: finish_copy(ctx); break;
    default: break;
  }
  g_sl.stage = kBrowse;
  g_sl.asking = false;
  ctx.files().set_quiet(false);
  picker_.close();
}

bool SampleLoadScreen::slot_stopped(unsigned slot) const noexcept {
  const Vocoder::LoopStatus st = sample_->status(slot);
  if (st.rec != Vocoder::LoopStatus::Rec::Idle || st.playing || st.pending) {
    return false;
  }
  for (unsigned k = 0; k < 4u; ++k) {
    if (sample_->status(k).region) {
      return false;  // a slice may borrow the slot
    }
  }
  return true;
}

void SampleLoadScreen::input(app::Context& ctx, const app::Input& in) noexcept {
  if (sample_ == nullptr) {
    return;
  }
  if (g_sl.stage != kBrowse) {  // a job in hand: only F8 while the card is read gives it up
    if (in.press(Button::F8) && g_sl.stage == kReading) {
      ctx.files().cancel();
      end_job(ctx);
      say(ctx, "CANCELLED");
    }
    return;
  }
  if (g_sl.asking) {  // REPLACES SLOT n: F1 confirms, any other key cancels (and is used up by that)
    if (in.type == app::Input::Type::Press || in.pad_press()) {
      if (in.press(Button::F1) && !in.shift) {
        g_sl.asking = false;
        begin(ctx);
      } else {
        open_picker(ctx);
        say(ctx, "NOT REPLACED");
      }
    }
    return;
  }
  const unsigned slot = sample_->slot() & 3u;
  if (in.press(Button::Up) || in.press(Button::Down)) {
    sample_->set_slot(in.press(Button::Up) ? (slot > 0u ? slot - 1u : 0u) : slot + 1u);
    ctx.redraw();
    return;
  }
  switch (picker_.input(ctx, in)) {
    case app::FilePicker::Result::Chosen: chosen(ctx); break;
    case app::FilePicker::Result::Cancelled: go_back(ctx); break;
    case app::FilePicker::Result::Browsing: break;
  }
  ctx.redraw();
}

// A file was picked: the slot must be quiet; a slot that holds a sample asks first.
void SampleLoadScreen::chosen(app::Context& ctx) noexcept {
  g_sl.entry = picker_.chosen();
  const unsigned slot = sample_->slot() & 3u;
  g_sl.slot = static_cast<std::uint8_t>(slot);
  // The picker closed itself on F1 (and with it its listing): a refusal brings the list back, a load or an ask keeps the card free.
  if (mem_ == nullptr) {
    open_picker(ctx);
    say(ctx, "NO LOOP MEMORY");
    return;
  }
  if (!slot_stopped(slot)) {
    open_picker(ctx);
    say(ctx, "STOP THE SLOT FIRST");
    return;
  }
  if (sample_->status(slot).length != 0u || sample_->table(slot).length != 0u) {
    g_sl.asking = true;
    ctx.redraw();
    return;
  }
  begin(ctx);
}

void SampleLoadScreen::begin(app::Context& ctx) noexcept {
  app::Files& files = ctx.files();
  files.set_quiet(true);  // the screen has its own strip
  sink_.bind(g_stage, sl::kStageBytes);
  g_sl.note = nullptr;
  if (!files.load(g_sl.folder, g_sl.entry.name, sink_)) {
    const char* why = files.status().text;
    end_job(ctx);
    say(ctx, why != nullptr && why[0] != '\0' ? why : "CARD BUSY");
    return;
  }
  g_sl.stage = kReading;
  ctx.redraw();
}

// A job is over (done, refused or given up): the picker is back and the card's own strip too.
void SampleLoadScreen::end_job(app::Context& ctx) noexcept {
  g_sl.stage = kBrowse;
  g_sl.asking = false;
  ctx.files().set_quiet(false);
  if (g_sl.ext) {  // FILE's job: no picker; the driver reads the end
    g_sl.ext_state = static_cast<std::uint8_t>(g_sl.ext_ok ? External::Done : External::Failed);
    return;
  }
  open_picker(ctx);
}

bool SampleLoadScreen::begin_external(app::Context& ctx, unsigned slot, const char* folder, const char* name) noexcept {
  g_sl.ext = true;
  g_sl.ext_ok = false;
  g_sl.ext_text = "";
  g_sl.ext_state = static_cast<std::uint8_t>(External::Failed);
  if (sample_ == nullptr || mem_ == nullptr || slot >= 4u || folder == nullptr || name == nullptr || std::strlen(folder) >= sizeof g_sl.folder ||
      std::strlen(name) >= sizeof g_sl.entry.name) {
    g_sl.ext_text = "NO LOOP MEMORY";
    return false;
  }
  if (!slot_stopped(slot)) {
    g_sl.ext_text = "STOP THE SLOT FIRST";
    return false;
  }
  std::strcpy(g_sl.folder, folder);
  std::strcpy(g_sl.entry.name, name);
  g_sl.entry.size = 0;
  g_sl.entry.folder = false;
  g_sl.slot = static_cast<std::uint8_t>(slot);
  g_sl.asking = false;
  g_sl.ext_state = static_cast<std::uint8_t>(External::Idle);
  begin(ctx);  // a refusal ends the job through end_job() and say(): Failed with the kit's reason
  return g_sl.stage != kBrowse;
}

void SampleLoadScreen::tick_external(app::Context& ctx) noexcept {
  if (g_sl.ext && g_sl.stage != kBrowse) {
    tick(ctx);
  }
}

SampleLoadScreen::External SampleLoadScreen::external() const noexcept {
  if (!g_sl.ext) {
    return External::Idle;
  }
  return g_sl.stage != kBrowse ? External::Running : static_cast<External>(g_sl.ext_state);
}

const char* SampleLoadScreen::external_text() const noexcept { return g_sl.ext && g_sl.ext_text != nullptr ? g_sl.ext_text : ""; }

void SampleLoadScreen::abort_external(app::Context& ctx) noexcept {
  if (!g_sl.ext || g_sl.stage == kBrowse) {
    return;
  }
  switch (g_sl.stage) {
    case kReading: ctx.files().cancel(); break;
    case kCopying: finish_copy(ctx); break;
    default: break;
  }
  g_sl.ext_ok = false;
  g_sl.ext_text = "CANCELLED";
  g_sl.ext_state = static_cast<std::uint8_t>(External::Failed);
  g_sl.stage = kBrowse;
  ctx.files().set_quiet(false);
}

void SampleLoadScreen::update(app::Context& ctx) noexcept {
  if (sample_ == nullptr) {
    return;
  }
  sample_->pad_update(ctx);
  if (g_sl.stage == kBrowse) {
    if (picker_.active()) {
      picker_.update(ctx);
    }
  } else {
    tick(ctx);
    ctx.redraw();
  }
  if (g_sl.note != nullptr && ctx.uptime_ms() >= g_sl.note_until) {
    g_sl.note = nullptr;
    ctx.redraw();
  }
}

void SampleLoadScreen::tick(app::Context& ctx) noexcept {
  switch (g_sl.stage) {
    case kReading: tick_read(ctx); break;
    case kClearing: tick_clear(ctx); break;
    case kCopying: tick_copy(ctx); break;
    case kWaiting: tick_wait(ctx); break;
    default: break;
  }
}

void SampleLoadScreen::tick_read(app::Context& ctx) noexcept {
  app::Files& files = ctx.files();
  const app::Files::Status st = files.status();
  bool done = st.phase == app::Files::Status::Done;
  if (sink_.overflowed() && st.phase == app::Files::Status::Running) {
    files.cancel();  // 16 s of stereo are staged: the rest of a longer file is not needed
    done = true;
  } else if (st.phase == app::Files::Status::Failed) {
    if (sink_.overflowed()) {
      done = true;  // the cancel above came back as a Failed "CANCELLED"
    } else {
      const char* why = st.text;
      end_job(ctx);
      say(ctx, why != nullptr && why[0] != '\0' ? why : "READ FAILED");
      return;
    }
  }
  if (!done) {
    return;
  }
  const sl::Info info = sl::parse(sink_.data(), sink_.held(), sink_.overflowed() ? g_sl.entry.size : sink_.seen());
  if (info.verdict != sl::Verdict::Ok) {
    end_job(ctx);
    say(ctx, sl::refusal_text(info.verdict));
    return;
  }
  g_sl.info = info;
  const unsigned slot = g_sl.slot & 3u;
  if (!slot_stopped(slot)) {  // the slot started under the read
    end_job(ctx);
    say(ctx, "STOP THE SLOT FIRST");
    return;
  }
  if (sample_->status(slot).length != 0u || sample_->table(slot).length != 0u) {
    const dsp::UnitId v = sample_->voice();  // the unit's own Rec CLEAR (as SAMPLE EDIT's empty): the new samples are a new length to every screen
    (void)ctx.engine().set_param(v, Vocoder::loop_param(slot, Vocoder::LoopField::Play), 0.0f);
    (void)ctx.engine().set_param(v, Vocoder::loop_param(slot, Vocoder::LoopField::Rec), 2.0f);
    (void)ctx.engine().set_param(v, Vocoder::loop_param(slot, Vocoder::LoopField::Rec), 0.0f);
    g_sl.deadline = ctx.uptime_ms() + kClearMs;
    g_sl.stage = kClearing;
  } else {
    sl::write_begin(g_sl.w, info.frames, info.channels);
    g_sl.stage = kCopying;
  }
}

// The old sample is gone once the unit and SAMPLE's table both show length 0.
void SampleLoadScreen::tick_clear(app::Context& ctx) noexcept {
  const unsigned slot = g_sl.slot & 3u;
  if (sample_->status(slot).length == 0u && sample_->table(slot).length == 0u) {
    sl::write_begin(g_sl.w, g_sl.info.frames, g_sl.info.channels);
    g_sl.stage = kCopying;
  } else if (ctx.uptime_ms() >= g_sl.deadline) {
    end_job(ctx);
    say(ctx, "SLOT NOT CLEARED");
  }
}

void SampleLoadScreen::tick_copy(app::Context& ctx) noexcept {
  const unsigned slot = g_sl.slot & 3u;
  if (sample_->status(slot).rec != Vocoder::LoopStatus::Rec::Idle || sample_->status(slot).playing) {  // a take started on the slot under the load
    end_job(ctx);
    say(ctx, "STOP THE SLOT FIRST");
    return;
  }
  std::int16_t* to = mem_ + static_cast<std::size_t>(slot) * Loop::kMaxSamples;
  if (sl::write_step(g_sl.w, g_stage + g_sl.info.data_offset, to)) {
    send_load(ctx);
  }
}

// The slot memory is complete: Load goes to the unit (the slot's length is then the file's), the screens that cache the slot are told, and the wait begins.
void SampleLoadScreen::send_load(app::Context& ctx) noexcept {
  const unsigned slot = g_sl.slot & 3u;
  const float v = static_cast<float>(static_cast<std::uint32_t>(slot) * Loop::kMaxSamples + g_sl.info.frames);
  (void)ctx.engine().set_param(sample_->voice(), Vocoder::Load, v);
  (void)ctx.engine().set_param(sample_->voice(), Vocoder::Load, 0.0f);  // a saved project must not hold it
  SampleEditScreen::slot_replaced(slot);  // the energies, beats, hits, brackets and cursor were the old samples'
  overview::shared().mark_dirty(slot);
  g_sl.stage = kWaiting;
  g_sl.deadline = ctx.uptime_ms() + kLoadMs;
  g_sl.resend = ctx.uptime_ms() + kResendMs;
}

// leave() in the middle of the copy: the rest of it, then Load, so that the slot is never left half written with the old length.
void SampleLoadScreen::finish_copy(app::Context& ctx) noexcept {
  const unsigned slot = g_sl.slot & 3u;
  std::int16_t* to = mem_ + static_cast<std::size_t>(slot) * Loop::kMaxSamples;
  for (unsigned guard = 0; guard < 4u + sl::kMaxFrames / sl::kSlice; ++guard) {  // bounded: a slice a turn
    if (sl::write_step(g_sl.w, g_stage + g_sl.info.data_offset, to)) {
      break;
    }
  }
  send_load(ctx);
}

void SampleLoadScreen::tick_wait(app::Context& ctx) noexcept {
  const unsigned slot = g_sl.slot & 3u;
  const Vocoder::LoopStatus st = sample_->status(slot);
  const std::uint64_t now = ctx.uptime_ms();
  if (st.rec == Vocoder::LoopStatus::Rec::Idle && st.length == g_sl.info.frames) {
    g_sl.note_buf[0] = '\0';
    if (g_sl.info.clipped) {
      cat(g_sl.note_buf, sizeof g_sl.note_buf, "FIRST ");
      cat_seconds(g_sl.note_buf, sizeof g_sl.note_buf, g_sl.info.frames);
      cat(g_sl.note_buf, sizeof g_sl.note_buf, " LOADED, FILE HAS ");
      cat_seconds(g_sl.note_buf, sizeof g_sl.note_buf, g_sl.info.file_frames);
    } else {
      cat(g_sl.note_buf, sizeof g_sl.note_buf, "LOADED ");
      cat_seconds(g_sl.note_buf, sizeof g_sl.note_buf, g_sl.info.frames);
    }
    g_sl.ext_ok = true;
    end_job(ctx);
    say(ctx, g_sl.note_buf);
  } else if (now >= g_sl.deadline) {
    overview::shared().mark_dirty(slot);
    end_job(ctx);
    say(ctx, "LOAD FAILED");
  } else if (now >= g_sl.resend) {
    const float v = static_cast<float>(static_cast<std::uint32_t>(slot) * Loop::kMaxSamples + g_sl.info.frames);
    (void)ctx.engine().set_param(sample_->voice(), Vocoder::Load, v);
    (void)ctx.engine().set_param(sample_->voice(), Vocoder::Load, 0.0f);
    g_sl.resend = now + kResendMs;
  }
}

void SampleLoadScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  if (sample_ != nullptr) {
    sample_->pad_leds(ctx, leds);
  }
}

void SampleLoadScreen::soft_keys(app::Context&, app::SoftKeys& out) noexcept {
  out.label[0] = "OPEN";
  out.label[7] = g_sl.stage == kReading ? "CANCEL" : "BACK";
  out.hot = 0;
  out.dim = static_cast<std::uint8_t>(0x7Eu | (g_sl.stage != kBrowse || g_sl.asking ? 0x01u : 0u) | (g_sl.stage != kBrowse && g_sl.stage != kReading ? 0x80u : 0u));
}

void SampleLoadScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  static constexpr app::HelpButton kButtons[] = {
      {Button::Up, "SLOT BEFORE", nullptr},
      {Button::Down, "SLOT AFTER", nullptr},
      {Button::Left, "PAGE UP THE LIST", nullptr},
      {Button::Right, "PAGE DOWN THE LIST", nullptr},
      {Button::Params, "PARAMS: THREE PAGES", nullptr},
      {Button::Performance, "PERFORM: THE HOME SCREEN", nullptr},
      {Button::Play, "START OR STOP THE BAR", nullptr},
      {Button::SampleLoad, "THIS SCREEN", nullptr},
      {Button::SamplePlay, "SAMPLE: THE FOUR SLOTS", nullptr},
      {Button::SampleEdit, "SAMPLE EDIT: THE CUTS", nullptr},
      {Button::SampleRec, "SAMPLE REC: A TAKE", nullptr},
  };
  if (sample_ != nullptr) {
    sample_->help(ctx, h);  // the pad labels and groups; the rest is replaced below
  }
  h.summary = "A 16-BIT 44.1 KHZ WAV, MONO OR STEREO, FROM APPS/O3_VOC03ER ON THE CARD, INTO THE SLOT AT THE TOP. OVER 16 S GIVES THE FIRST 16 S.";
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    h.fkey[i] = nullptr;
    h.fkey_shift[i] = nullptr;
  }
  h.fkey[0] = "LOADS THE FILE";
  h.fkey[7] = "BACK, OR CANCEL";
  h.pads = "A PAD PICKS A ROW OF THE LIST.";
  h.buttons = kButtons;
  h.button_count = sizeof kButtons / sizeof kButtons[0];
  h.encoder = "MOVES THE CURSOR IN THE LIST";
}

void SampleLoadScreen::draw(app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept {
  if (sample_ == nullptr) {
    return;
  }
  const unsigned slot = (g_sl.stage != kBrowse ? g_sl.slot : sample_->slot()) & 3u;
  const bool job = g_sl.stage != kBrowse;
  const bool panel = job || g_sl.asking;

  ornoir::HeaderState h;
  sample_->fill_header(ctx, h);
  ornoir::FKey keys[ornoir::kFKeys];
  app::SoftKeys sk;
  soft_keys(ctx, sk);
  for (unsigned i = 0; i < ornoir::kFKeys; ++i) {
    keys[i].name = sk.label[i] != nullptr ? sk.label[i] : "";
    keys[i].dim = ((sk.dim >> i) & 1u) != 0u;
  }

  unsigned pct = 0;
  const char* verb = "";
  if (g_sl.stage == kReading) {
    verb = "READING ";
    pct = ctx.files().status().permille / 10u;
    if (sink_.overflowed()) {
      pct = 100;
    }
  } else if (g_sl.stage == kClearing) {
    verb = "CLEARING ";
  } else if (g_sl.stage == kCopying) {
    verb = "WRITING ";
    pct = sl::write_percent(g_sl.w);
  } else if (g_sl.stage == kWaiting) {
    verb = "WRITING ";
    pct = 100;
  }
  const int barx = kX + 20;
  const int barw = kW - 40;
  const int bary = kListY + 120;

  ui::plot::Canvas c(r);
  c.open();
  ornoir::header_shapes(c, h);
  ornoir::frow_shapes(c, keys);
  if (panel) {
    ornoir::fill_rect(c, kX, kListY, kW, kListH, Color::Empty);
    ornoir::fill_rect(c, kX + 2, kListY + 2, kW - 4, kListH - 4, Color::Pane);
    if (job) {
      ornoir::fill_rect(c, barx, bary, barw, 14, Color::Empty);
      ornoir::fill_rect(c, barx + 2, bary + 2, (barw - 4) * static_cast<int>(pct) / 100, 10, Color::Accent);
    }
  }
  c.close();
  ornoir::header_text(r, h);
  ornoir::frow_text(r, keys);

  r.text(kX, 88, kSlotNames[slot], Color::Text, Face::Display);
  const std::uint32_t len = sample_->table(slot).length != 0u ? sample_->table(slot).length : sample_->status(slot).length;
  char line[48];
  line[0] = '\0';
  if (len == 0u) {
    cat(line, sizeof line, "EMPTY");
  } else {
    cat(line, sizeof line, "HOLDS ");
    cat_seconds(line, sizeof line, len);
  }
  r.text(kX + kW - r.text_width(line, Face::Label), 100, line, Color::Text, Face::Label);

  if (panel) {
    char nm[64];
    nm[0] = '\0';
    cat(nm, sizeof nm, g_sl.entry.name);
    r.text(centred(r, nm, Face::Display, kX + kW / 2), kListY + 40, nm, Color::Text, Face::Display);
    char sz[24];
    sz[0] = '\0';
    cat_size(sz, sizeof sz, g_sl.entry.size);
    r.text(centred(r, sz, Face::Label, kX + kW / 2), kListY + 80, sz, Color::TextDim, Face::Label);
  } else if (picker_.active()) {
    picker_.draw(r, ui::Rect{kX, kListY, kW, kListH});
  }

  char strip[64];
  strip[0] = '\0';
  if (job) {
    cat(strip, sizeof strip, verb);
    if (g_sl.stage != kClearing) {
      cat_uint(strip, sizeof strip, pct);
      cat(strip, sizeof strip, "%");
    }
    r.text(kX, kNoteY, strip, Color::Accent, Face::Label);
  } else if (g_sl.asking) {
    cat(strip, sizeof strip, "REPLACES SLOT ");
    cat_uint(strip, sizeof strip, slot + 1u);
    cat(strip, sizeof strip, ": F1 CONFIRMS");
    r.text(kX, kNoteY, strip, Color::Accent, Face::Label);
  } else if (g_sl.note != nullptr) {
    r.text(kX, kNoteY, g_sl.note, Color::Accent, Face::Label);
  } else {
    r.text(kX, kNoteY + 4, "UP AND DOWN PICK THE SLOT, F1 LOADS THE FILE UNDER THE CURSOR", Color::TextDim, Face::Micro);
  }
}

}  // namespace o3_voc03er
