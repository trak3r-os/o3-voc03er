// SPDX-License-Identifier: GPL-3.0-or-later
//
// O3-VOC03ER: the app. An app is a whole firmware image (o3-sdk docs/FRAMEWORK.md, "Apps"): it owns its units and screens as plain
// objects (no heap), wires them into the engine in setup() and shows the first screen. The one entry point at the end is all the
// platform's main and the simulator know of it.
//
//   PLAY          MODE: BANDS, TALK, TUNE, round (SHIFT + PLAY: the transport); PATTERN: the carrier of the cursor step; SHIFT + LEFT / RIGHT: the cursor;  SAMPLE PLAY  the next audio source (line sources start at gain 0)
//   pads          a chromatic keyboard (app/include/o3_voc03er/pad_map.h); LEFT / RIGHT move it an octave
//   F1..F3        point the encoder at a value (tap: latched; hold + turn: for a moment); the encoder changes it (SHIFT: coarse)
//   pads, DELETE  the pads also record into a 16-step pattern; DELETE clears it
//   FILE          saves the pattern to the SD card, Apps/o3_voc03er/pattern.o3a;  SAMPLE LOAD  loads it back
//
// The card (Context::files(), o3-sdk kit/app/include/o3sdk/app/files.h: read its header comment, it is short). Every call is a job the
// runtime advances in the main loop, so the sound and the screen never wait for it: a call only records the request and returns, and update()
// polls status() for Done or Failed. Rules this file follows: one job at a time (a call made while busy() is refused, so check it first),
// the buffer a save reads stays untouched until the job ends (a buffer of its own for each file), a load goes into a buffer you give and is
// checked before it is applied (pattern.h), a failed call says why in status().text and on the kit's strip (SAVED, NO CARD, FILE TOO BIG,
// ...), and a power cut never leaves half a file (the kit writes a temporary one and renames it; the old one stays as pattern.o3a.bak).
// What the app keeps: the pattern (on the keys above) and the keyboard octave, saved by itself a moment after it changes and loaded at
// start as soon as the card is ready. Two files, pattern.o3a and config.o3a, in the app's folder on the card (the CMake id names it).
//
// To grow it: more screens (a class like PlayScreen, ctx.show() to switch), a sequencer (a dsp::Clocked hook added to the engine;
// apps/hello/src/hello_app.cpp in o3-sdk has a riff), more units (attach, route to a slot, a send or an insert), console commands
// (App::diag_commands). The Config button is the kit's system Config screen (the theme, MIDI, audio in): this app does not use it.
//
// What else an app can reach through its Context (docs/FRAMEWORK.md has the pages): midi() (notes in, clock, the user's MIDI settings),
// audio_in() (line and microphone input, the monitor), radio() (the FM radio) and recorder() (a take of the input or the master, saved to
// the card as a WAV). The template uses none of them; each is a short call like files() and a line in the app's state.

#include "perform_screen.h"
#include "play_screen.h"
#include "skin/ornoir_assets.h"
#include <o3_voc03er/carrier_seq.h>

#include <o3_voc03er/input.h>
#include <o3_voc03er/pattern.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/context.h>
#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/diag.h>
#include <o3sdk/core/placement.h>
#include <o3sdk/dsp/engine.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

using namespace o3sdk;

// The four loops' samples (5.6 MB, mono int16): in the SDRAM on the unit (O3SDK_BIG), handed to the unit with bind_loops(); the unit never
// places memory. The Vocoder is a member of MyApp, which lives in the SRAM, so it cannot hold them itself. (A section attribute needs a
// namespace-scope object, not a member.)
alignas(8) O3SDK_BIG std::int16_t g_loop_mem[o3_voc03er::Loop::kBankSamples] = {};
static_assert(sizeof g_loop_mem == o3_voc03er::Loop::kBankSamples * sizeof(std::int16_t) && sizeof g_loop_mem % 8 == 0, "the bank is whole words");
// The buffers of the file jobs (main loop only: the card is polled by the core, so SDRAM is legal, mem_map.h): word aligned, and a save's stays
// unchanged until its job ends, so each file has its own; one load at a time shares one (the files service runs one job at a time).
alignas(4) O3SDK_BIG std::uint8_t g_save_buf[o3_voc03er::kPatternFileMax] = {};
alignas(4) O3SDK_BIG std::uint8_t g_cfg_buf[o3_voc03er::kConfigFileMax] = {};
alignas(4) O3SDK_BIG std::uint8_t g_load_buf[o3_voc03er::kPatternFileMax] = {};
static_assert(sizeof g_save_buf % 4 == 0 && sizeof g_cfg_buf % 4 == 0 && sizeof g_load_buf % 4 == 0, "word-aligned buffers are whole words");

class MyApp;
MyApp& instance() noexcept;
void ain_command(const app::DiagArgs& args, app::DiagOut& out) noexcept;
void voc_command(const app::DiagArgs& args, app::DiagOut& out) noexcept;
void loop_command(const app::DiagArgs& args, app::DiagOut& out) noexcept;

constexpr app::DiagCommand kCommands[] = {
    {"ain", "ain <source> [gain 0..100]: select the audio input, show its level", &ain_command},
    {"voc", "voc <bands|talk|tune> [notes 0..8]: set the vocoder mode, hold N notes", &voc_command},
    {"loop", "loop <n> rec|stop|play|halt|mod|car|half|x1|x2|fwd|rev|lvl <dB>", &loop_command},
};

constexpr const char* kModeWord[3] = {"bands", "talk", "tune"};
constexpr unsigned mode_index(float v) noexcept { return v >= 1.5f ? 2u : (v >= 0.5f ? 1u : 0u); }

constexpr std::size_t text_length(const char* s) noexcept {
  std::size_t n = 0;
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

// A row past the console's limits is dropped on the unit with only a log line, and the host never runs that check, so a long help text costs
// a whole unit round: the first G0 round measured nothing for this.
constexpr bool commands_fit() noexcept {
  for (const app::DiagCommand& c : kCommands) {
    const std::size_t name = text_length(c.name);
    if (name < app::kDiagNameMin || name > app::kDiagNameMax || text_length(c.help) > app::kDiagHelpMax) {
      return false;
    }
  }
  return sizeof kCommands / sizeof kCommands[0] <= app::kDiagMaxCommands;
}
static_assert(commands_fit(), "a console command's name or help is too long: the unit would drop it");

using o3_voc03er::permille;
using o3_voc03er::source_name;

class MyApp final : public app::App {
 public:
  const app::AppInfo& info() const noexcept override {
    static constexpr app::AppInfo kInfo{"O3-VOC03ER", "0.1.0"};  // upper case: it is shown in the header strip and on the boot screen
    return kInfo;
  }

  // Main loop, once, with the engine in its setup phase: attach and route the units, add hooks, open the first screen.
  void setup(app::Context& ctx) noexcept override {
    ctx_ = &ctx;  // the console's handlers have no Context argument; they run in the main loop
    dsp::Engine& e = ctx.engine();
    voc_.bind_loops(g_loop_mem, o3_voc03er::Loop::kMaxSamples);  // before attach: the unit takes its memory from the app
    const dsp::UnitId voice = e.attach(voc_);  // an Instrument: the engine hands it the codec input as in_l / in_r (the modulator)
    // Slot 0 plays the vocoder; the mixer's level, pan and sends are the slot's. A refused call is counted (Engine::setup_errors) and the
    // simulator and app_test fail on it, so the result need not be checked here.
    (void)e.set_source(0, voice);
    voc_id_ = voice;
    meter_.reset(0, 0);
    console_notes_ = 0;
    play_.init(voice, &meter_, &voc_);
    perform_.init(voice, &meter_, &voc_, &step_source_cb, this);
    carriers_.init(voice);
    (void)e.add_hook(carriers_);  // the per-step carrier switch (V5c); a refusal is counted like the others
    // setup() can run again on this object (a test starts a fresh simulator on it, the simulator's `reboot`): the file state starts over too.
    op_ = Op::None;
    boot_ = Boot::Pattern;
    saved_octave_ = play_.octave();
    ctx.show(perform_);
  }

  // OR NOIR's bitmaps, put in RAM_G by the runtime before setup().
  std::size_t assets(const app::AppAsset*& rows) const noexcept override {
    rows = o3_voc03er::ornoir::kAssets;
    return o3_voc03er::ornoir::kAssetCount;
  }

  // The file jobs end and start here, once per pass of the main loop (a Screen has an update() too, for what only it needs).
  void update(app::Context& ctx) noexcept override {
    poll_peaks(ctx);
    carriers_.set_all(play_.pattern().carrier);  // the render side's copy of the steps' carriers (relaxed atomics, carrier_seq.h)
    app::Files& files = ctx.files();
    if (op_ != Op::None) {
      const app::Files::Status st = files.status();
      if (st.phase != app::Files::Status::Done && st.phase != app::Files::Status::Failed) {
        return;  // still running
      }
      settle(ctx, st.phase == app::Files::Status::Done);
      return;
    }
    if (files.busy() || !files.available() || files.card() != app::Files::Card::Ready) {
      return;
    }
    // At start, as soon as the card is ready (the simulator and the unit mount it after setup()): the pattern, then the octave. A first run
    // has neither file: that is a failed load, which the screen shows as NOT FOUND and nothing more. The jobs are quiet (no strip line):
    // the user did not ask for them.
    if (boot_ == Boot::Pattern) {
      files.set_quiet(true);
      op_ = files.load("", o3_voc03er::kPatternFile, g_load_buf, sizeof g_load_buf) ? Op::LoadPattern : Op::None;
      boot_ = Boot::Config;
      return;
    }
    if (boot_ == Boot::Config) {
      files.set_quiet(true);
      op_ = files.load("", o3_voc03er::kConfigFile, g_load_buf, sizeof g_load_buf) ? Op::LoadConfig : Op::None;
      boot_ = Boot::Done;
      return;
    }
    // The setting saves itself when it changes (and the card is there): the file's bytes are made now and stay in g_cfg_buf until the job ends.
    if (play_.octave() != saved_octave_) {
      saved_octave_ = play_.octave();  // (even when refused: it is tried again at the next change, not every pass)
      const std::size_t n = o3_voc03er::write_config(saved_octave_, g_cfg_buf, sizeof g_cfg_buf);
      files.set_quiet(true);
      op_ = n != 0u && files.save("", o3_voc03er::kConfigFile, g_cfg_buf, n) ? Op::SaveConfig : Op::None;
    }
  }

  // The keys the screen does not use: FILE saves the pattern, SAMPLE LOAD loads it. A key pressed while a job runs is ignored (busy()).
  bool input(app::Context& ctx, const app::Input& in) noexcept override {
    if (in.press(app::Button::Params)) {  // the old PLAY screen (pattern, carrier steps), kept reachable here
      ctx.show(play_);
      return true;
    }
    if (in.press(app::Button::Performance)) {  // PERFORM, the home screen
      ctx.show(perform_);
      return true;
    }
    if (in.press(app::Button::Play)) {  // MODE: BANDS, TALK, TUNE, round; SHIFT + PLAY starts or stops the transport (the per-step carriers and the loops' bar lines run on it)
      dsp::Engine& e = ctx.engine();
      if (in.shift) {
        (void)(e.playing() ? e.stop() : e.play());
        ctx.redraw();
        return true;
      }
      (void)e.set_param(voc_id_, o3_voc03er::Vocoder::Mode, static_cast<float>((mode_index(e.param(voc_id_, o3_voc03er::Vocoder::Mode)) + 1u) % 3u));
      ctx.redraw();
      return true;
    }
    if (in.press(app::Button::SamplePlay)) {  // the next source, at its own starting gain
      if (ctx.audio_in().available()) {
        const hal::capture::Source next = o3_voc03er::next_source(ctx.audio_in().source());
        (void)select_source(next, o3_voc03er::default_gain(next));
        ctx.redraw();
      }
      return true;
    }
    const bool save = in.press(app::Button::File);
    if (!save && !in.press(app::Button::SampleLoad)) {
      return false;
    }
    app::Files& files = ctx.files();
    if (op_ == Op::None && !files.busy()) {
      files.set_quiet(false);  // the user asked: the strip says SAVED, NO CARD, ...
      play_.set_note(nullptr);
      if (save) {
        const std::size_t n = o3_voc03er::write_pattern(play_.pattern(), g_save_buf, sizeof g_save_buf);
        op_ = n != 0u && files.save("", o3_voc03er::kPatternFile, g_save_buf, n) ? Op::SavePattern : Op::None;
      } else {
        op_ = files.load("", o3_voc03er::kPatternFile, g_load_buf, sizeof g_load_buf) ? Op::LoadPattern : Op::None;
      }
      ctx.redraw();
    }
    return true;
  }

  std::size_t diag_commands(const app::DiagCommand*& rows) const noexcept override {
    rows = kCommands;
    return sizeof kCommands / sizeof kCommands[0];
  }

  // `ain` (main loop): see ain_command.
  void ain(const app::DiagArgs& args, app::DiagOut& out) noexcept {
    if (ctx_ == nullptr || !ctx_->audio_in().available()) {
      out.text("ain: no audio input on this platform").nl();
      return;
    }
    app::AudioIn& in = ctx_->audio_in();
    if (args.argc >= 2u) {
      const hal::capture::Source* src = nullptr;
      for (const o3_voc03er::SourceName& n : o3_voc03er::kSources) {
        if (std::strcmp(args.arg(1), n.name) == 0) {
          src = &n.source;
        }
      }
      unsigned gain = src != nullptr ? o3_voc03er::default_gain(*src) : 0u;  // a line source starts at the ADC's 0 dB
      bool ok = src != nullptr && args.argc <= 3u;
      if (ok && args.argc == 3u) {
        const char* g = args.arg(2);
        unsigned v = 0;
        std::size_t i = 0;
        for (; g[i] >= '0' && g[i] <= '9' && i < 4u; ++i) {
          v = v * 10u + static_cast<unsigned>(g[i] - '0');
        }
        ok = i != 0u && g[i] == '\0' && v <= 100u;
        gain = v;
      }
      if (!ok) {
        out.text("usage: ain [line|left|right|miclow|michigh|radio|usb] [gain 0..100]").nl();
        return;
      }
      const bool acked = select_source(*src, gain);
      out.text(acked ? "ain: selected " : "ain: select refused ");
    } else {
      out.text("ain: ");
    }
    out.text(source_name(in.source())).text(" gain ").dec(in.gain()).text(" settling ").dec(in.settling() ? 1u : 0u);
    out.text(" failed ").dec(in.failed() ? 1u : 0u).text(" peak_l ").dec(permille(meter_.left())).text(" peak_r ").dec(permille(meter_.right())).nl();
  }

  // `loop <1-4> <rec|stop|play|halt|mod|car>` (main loop): the same events as the keys; `loop` alone prints the four loops.
  void loop(const app::DiagArgs& args, app::DiagOut& out) noexcept {
    o3_voc03er::LoopCommand c;
    if (args.argc == 1u && ctx_ != nullptr) {
      for (unsigned i = 0; i < o3_voc03er::Loop::kCount; ++i) {
        const o3_voc03er::Vocoder::LoopStatus st = voc_.loop_status(i);
        out.text("loop ").dec(i + 1u).text(": ").text(st.carrier ? "car" : "mod").text(" len ").dec(st.length);
        out.text(" rec ").dec(static_cast<unsigned>(st.rec)).text(" play ").dec(st.playing ? 1u : 0u);
        out.text(st.speed == 0u ? " 0.5x" : (st.speed == 2u ? " 2x" : " 1x")).text(st.reverse ? " rev" : " fwd").text(" lvl ").dec(static_cast<std::int32_t>(st.level_db)).nl();
      }
      return;
    }
    if (ctx_ == nullptr || (args.argc != 3u && args.argc != 4u) || !o3_voc03er::parse_loop(args.arg(1), args.arg(2), c, args.argc == 4u ? args.arg(3) : nullptr)) {
      out.text("usage: loop [1-4 rec|stop|play|halt|mod|car|half|x1|x2|fwd|rev|lvl dB]").nl();
      return;
    }
    using LF = o3_voc03er::Vocoder::LoopField;
    const unsigned i = c.loop;
    dsp::Engine& e = ctx_->engine();
    switch (c.act) {
      case o3_voc03er::LoopAct::Rec: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Rec), 0.0f);  // 0 then 1: a full loop stays at 1
                                     (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Rec), 1.0f); break;
      case o3_voc03er::LoopAct::StopRec: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Rec), 0.0f); break;
      case o3_voc03er::LoopAct::Play: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Play), 1.0f); break;
      case o3_voc03er::LoopAct::Halt: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Play), 0.0f); break;
      case o3_voc03er::LoopAct::Mod: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Role), 0.0f); break;
      case o3_voc03er::LoopAct::Car: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Role), 1.0f); break;
      case o3_voc03er::LoopAct::Half: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Speed), 0.0f); break;
      case o3_voc03er::LoopAct::X1: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Speed), 1.0f); break;
      case o3_voc03er::LoopAct::X2: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Speed), 2.0f); break;
      case o3_voc03er::LoopAct::Fwd: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Dir), 0.0f); break;
      case o3_voc03er::LoopAct::Rev: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Dir), 1.0f); break;
      case o3_voc03er::LoopAct::Level: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Level), static_cast<float>(c.db)); break;
    }
    out.text("loop ").dec(i + 1u).text(" asked").nl();
    ctx_->redraw();
  }

  // `voc <bands|talk|tune> [notes 0..8]` (main loop): the mode, and N notes held from the console (the previous ones are released first).
  void voc(const app::DiagArgs& args, app::DiagOut& out) noexcept {
    o3_voc03er::VocCommand c;
    if (ctx_ == nullptr || args.argc < 2u || args.argc > 3u ||
        !o3_voc03er::parse_voc(args.arg(1), args.argc == 3u ? args.arg(2) : nullptr, c)) {
      out.text("usage: voc <bands|talk|tune> [notes 0..8]").nl();
      return;
    }
    dsp::Engine& e = ctx_->engine();
    (void)e.set_param(voc_id_, o3_voc03er::Vocoder::Mode, static_cast<float>(c.mode));
    for (unsigned i = 0; i < console_notes_; ++i) {
      (void)e.note_off(voc_id_, o3_voc03er::kVocChord[i]);
    }
    console_notes_ = c.notes;
    for (unsigned i = 0; i < console_notes_; ++i) {
      (void)e.note_on(voc_id_, o3_voc03er::kVocChord[i], 100);
    }
    out.text("voc: ").text(kModeWord[c.mode]).text(" notes ").dec(console_notes_).nl();
    ctx_->redraw();
  }

  // The console's `state` after the kit's pairs (`o3console.py state` over USB; the same line is in the simulator's tests): the keyboard
  // octave, the key the encoder drives and its value as the knob strip shows it.
  std::size_t describe_state(const app::Context& ctx, char* out, std::size_t cap) const noexcept override {
    app::StateWriter w(out, cap);
    const app::ParamKeys& keys = play_.keys();
    const app::ParamPage* page = keys.page();
    const unsigned focus = keys.focus();
    char value[16] = {};
    if (page != nullptr && focus < page->count) {
      (void)ctx.engine().format_param(page->unit, page->ids[focus], value, sizeof value);
    }
    w.sint("octave", play_.octave());
    w.num("steps", play_.pattern().used());
    w.num("cursor", play_.pattern().cursor);
    w.num("carriers", play_.pattern().carriers_used());
    w.num("playing", ctx.engine().playing() ? 1u : 0u);
    w.num("focus", focus);
    w.str("value", value);
    const app::AudioIn& in = ctx.audio_in();
    const bool have = in.available();
    w.str("in_src", have ? source_name(in.source()) : "none");
    w.num("in_gain", have ? in.gain() : 0u);
    w.num("in_settling", have && in.settling() ? 1u : 0u);
    w.num("in_failed", have && in.failed() ? 1u : 0u);
    w.num("in_peak_l", permille(meter_.left()));
    w.num("in_peak_r", permille(meter_.right()));
    w.str("voc_mode", kModeWord[mode_index(ctx.engine().param(voc_id_, o3_voc03er::Vocoder::Mode))]);
    w.num("voc_notes", console_notes_);
    for (unsigned i = 0; i < o3_voc03er::Loop::kCount; ++i) {
      const o3_voc03er::Vocoder::LoopStatus st = voc_.loop_status(i);
      char key[8] = {'l', 'o', 'o', 'p', static_cast<char>('1' + i), '\0'};
      w.num(key, st.length);
    }
    return w.size();
  }

 private:
  // Peaks (main loop only: update() writes, the screen and describe_state read): peaks() is polled every pass (it measures only while
  // called at least once a second); the meter (input.h) keeps them and forgets the previous source after a switch.
  void poll_peaks(app::Context& ctx) noexcept {
    app::AudioIn& in = ctx.audio_in();
    if (!in.available()) {
      meter_.reset(ctx.uptime_ms(), 0);
      return;
    }
    const dsp::InputPeaks p = in.peaks();  // always read: it drains what the engine measured
    const std::uint64_t now = ctx.uptime_ms();
    if (in.settling()) {
      meter_.reset(now, o3_voc03er::InputMeter::kSettleHoldoffMs);
    }
    meter_.feed(p.l, p.r, now);
  }

  // The one place a source is chosen (the console's `ain` and the SAMPLE PLAY key): the meter forgets the old source's peaks.
  bool select_source(hal::capture::Source src, unsigned gain) noexcept {
    const bool acked = ctx_->audio_in().select(src, gain);
    meter_.reset(ctx_->uptime_ms(), o3_voc03er::InputMeter::kSelectHoldoffMs);
    return acked;
  }

  // PERFORM's F2: the next audio source, at its own starting gain.
  static void step_source_cb(void* user) noexcept {
    MyApp& a = *static_cast<MyApp*>(user);
    if (a.ctx_ != nullptr && a.ctx_->audio_in().available()) {
      const hal::capture::Source next = o3_voc03er::next_source(a.ctx_->audio_in().source());
      (void)a.select_source(next, o3_voc03er::default_gain(next));
    }
  }

  app::Context* ctx_ = nullptr;
  o3_voc03er::InputMeter meter_;
  dsp::UnitId voc_id_ = dsp::kNoUnit;
  unsigned console_notes_ = 0;  // the notes `voc` holds (main loop only)

  enum class Op : std::uint8_t { None, LoadPattern, LoadConfig, SavePattern, SaveConfig };  // the job in flight
  enum class Boot : std::uint8_t { Pattern, Config, Done };                                 // the loads at start, in order

  // A job ended: a load that is Done is checked and applied. A load that is not a pattern changes nothing and says so (BAD FILE); one that
  // failed (no file, no card) has already said why in status().
  void settle(app::Context& ctx, bool done) noexcept {
    const std::size_t got = ctx.files().loaded();
    if (done && op_ == Op::LoadPattern) {
      o3_voc03er::Pattern p;
      if (o3_voc03er::read_pattern(g_load_buf, got, p)) {
        play_.set_pattern(p);
      } else {
        play_.set_note("BAD FILE");
      }
    } else if (done && op_ == Op::LoadConfig) {
      int octave = 0;
      if (o3_voc03er::read_config(g_load_buf, got, octave)) {
        play_.set_octave(octave);
        saved_octave_ = play_.octave();  // what the card holds: nothing to save
      }
    }
    op_ = Op::None;
    ctx.redraw();
  }

  o3_voc03er::Vocoder voc_;
  o3_voc03er::PlayScreen play_;
  o3_voc03er::PerformScreen perform_;
  o3_voc03er::CarrierSeq carriers_;

  Op op_ = Op::None;
  Boot boot_ = Boot::Pattern;
  int saved_octave_ = 0;  // the octave the card holds (or was last asked to)
};

MyApp& instance() noexcept {
  static MyApp app;
  return app;
}

// `ain <line|left|right|miclow|michigh|radio|usb> [gain 0..100]` selects the input (default gain: 0 for a line source, else 50); `ain` alone prints it.
void ain_command(const app::DiagArgs& args, app::DiagOut& out) noexcept {
  instance().ain(args, out);
}

void voc_command(const app::DiagArgs& args, app::DiagOut& out) noexcept {
  instance().voc(args, out);
}

void loop_command(const app::DiagArgs& args, app::DiagOut& out) noexcept {
  instance().loop(args, out);
}

}  // namespace

o3sdk::app::App& o3sdk_app() noexcept { return instance(); }
