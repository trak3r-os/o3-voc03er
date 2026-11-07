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
//   FILE          saves the pattern to the SD card, Apps/o3_voc03er/pattern.o3a;  SAMPLE LOAD  loads it back (on this screen; elsewhere it opens SAMPLE LOAD, the WAV loader)
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
// To grow it: more screens (a class like PerformScreen, ctx.show() to switch), a sequencer (a dsp::Clocked hook added to the engine;
// apps/hello/src/hello_app.cpp in o3-sdk has a riff), more units (attach, route to a slot, a send or an insert), console commands
// (App::diag_commands). The Config button is the kit's system Config screen (the theme, MIDI, audio in): this app does not use it.
//
// What else an app can reach through its Context (docs/FRAMEWORK.md has the pages): midi() (notes in, clock, the user's MIDI settings),
// audio_in() (line and microphone input, the monitor), radio() (the FM radio) and recorder() (a take of the input or the master, saved to
// the card as a WAV). The template uses none of them; each is a short call like files() and a line in the app's state.

#include "pattern_screen.h"
#include "perform_screen.h"
#include "skin/ornoir_assets.h"
#include "master_screen.h"
#include "params_screen.h"
#include "sample_rec_screen.h"
#include "sample_edit_screen.h"
#include "file_screen.h"
#include "sample_load_screen.h"
#include "sample_screen.h"
#include "song_screen.h"
#include <cmath>

#include <o3_voc03er/stepseq.h>

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
// The eight patterns A-H (stepseq.h): SDRAM, zero at boot (every step a rest, every length 16). The step sequencer hook reads it.
alignas(4) O3SDK_BIG o3_voc03er::Bank g_bank;
// SONG's chain (project.h): SDRAM, all-zero at boot (no entries, no loop). SongScreen edits it; FILE will save it.
alignas(4) O3SDK_BIG o3_voc03er::Chain g_chain;
alignas(4) O3SDK_BIG std::uint8_t g_save_buf[o3_voc03er::kPatternFileMax] = {};
alignas(4) O3SDK_BIG std::uint8_t g_cfg_buf[o3_voc03er::kConfigFileMax] = {};
alignas(4) O3SDK_BIG std::uint8_t g_load_buf[o3_voc03er::kPatternFileMax] = {};
static_assert(sizeof g_save_buf % 4 == 0 && sizeof g_cfg_buf % 4 == 0 && sizeof g_load_buf % 4 == 0, "word-aligned buffers are whole words");

class MyApp;
MyApp& instance() noexcept;
void ain_command(const app::DiagArgs& args, app::DiagOut& out) noexcept;
void voc_command(const app::DiagArgs& args, app::DiagOut& out) noexcept;
void loop_command(const app::DiagArgs& args, app::DiagOut& out) noexcept;
void pat_command(const app::DiagArgs& args, app::DiagOut& out) noexcept;

constexpr app::DiagCommand kCommands[] = {
    {"ain", "ain <source> [gain 0..100]: select the audio input, show its level", &ain_command},
    {"voc", "voc <mode> [n]|formant|thru|glide|gate|arp|speaks|sounds <v>|bendrange", &voc_command},
    {"loop", "loop <n> rec|stop|play|halt|clear|mod|car|half|x1|x2|fwd|rev|lvl <dB>", &loop_command},
    {"pat", "pat [A-H [len n|<step> note n|chord r k v|car c|lvl v|slc n]]", &pat_command},
};

constexpr const char* kModeWord[4] = {"bands", "talk", "tune", "robot"};
constexpr unsigned mode_index(float v) noexcept { return v >= 2.5f ? 3u : (v >= 1.5f ? 2u : (v >= 0.5f ? 1u : 0u)); }

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
    // the first boot of section 10 of docs/UI-DESIGN.md: the synth under the voice, in C major
    (void)e.set_param(voice, o3_voc03er::Vocoder::CarSrc, 1.0f);
    (void)e.set_param(voice, o3_voc03er::Vocoder::Scale, 1.0f);
    meter_.reset(0, 0);
    console_notes_ = 0;
    g_bank = o3_voc03er::Bank{};  // setup() can run again on this object (tests, `reboot`): the patterns start empty; no render runs yet
    g_chain = o3_voc03er::Chain{};
    perform_.init(voice, &meter_, &voc_, &pick_source_cb, this);
    sample_rec_.init(voice, &meter_, &voc_, &perform_, &pick_source_cb, this);
    params_.init(voice, &perform_);
    master_.init(voice, &meter_, &pick_source_cb, this);
    sample_.init(voice, &voc_, &perform_, &carriers_);
    sample_edit_.init(&sample_, g_loop_mem);
    sample_load_.init(&sample_, g_loop_mem);
    file_.init(voice, &sample_, &perform_, &pattern_, &g_bank, &g_chain, &sample_load_, g_loop_mem);
    pattern_.init(voice, &g_bank, &carriers_, &perform_, &save_bank_cb, this);
    song_.init(voice, &g_chain, &carriers_, &perform_);
    // M1 (docs/UI-DESIGN.md 7c): the notes of the user's MIDI channel play the SYNTH carrier, as the pads do (the unit takes them in apply()).
    // M2: from note 36 the unit fires the SPEAKS slot's slices instead (Vocoder::kSliceNoteLow), told from the pads' notes by kNoteLocal.
    (void)ctx.midi().route(voice, 0);
    carriers_.init(voice, &g_bank, sample_.slice_map());
    voc_.bind_slices(sample_.slice_map());  // the same map the hook reads; its address never moves
    carriers_.set_picked(1);  // the boot CarSrc above
    (void)e.add_hook(carriers_);  // the per-step carrier switch (V5c); a refusal is counted like the others
    // setup() can run again on this object (a test starts a fresh simulator on it, the simulator's `reboot`): the file state starts over too.
    op_ = Op::None;
    boot_ = Boot::Pattern;
    saved_octave_ = perform_.octave();
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
    sample_.service(ctx);  // slice voices end and tables follow lengths whichever screen is up
    {  // what the hook needs of the main side: the carrier the user picked (restored on Stop), and the key and scale the chords are spelled in
      const dsp::Engine& e = ctx.engine();
      carriers_.set_picked(static_cast<std::uint8_t>(std::lround(e.param(voc_id_, o3_voc03er::Vocoder::CarSrc))));
      carriers_.set_key_scale(static_cast<std::uint8_t>(std::lround(e.param(voc_id_, o3_voc03er::Vocoder::Key))),
                              static_cast<std::uint8_t>(std::lround(e.param(voc_id_, o3_voc03er::Vocoder::Scale))));
    }
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
    if (perform_.octave() != saved_octave_) {
      saved_octave_ = perform_.octave();  // (even when refused: it is tried again at the next change, not every pass)
      const std::size_t n = o3_voc03er::write_config(saved_octave_, g_cfg_buf, sizeof g_cfg_buf);
      files.set_quiet(true);
      op_ = n != 0u && files.save("", o3_voc03er::kConfigFile, g_cfg_buf, n) ? Op::SaveConfig : Op::None;
    }
  }

  // The keys the screens do not use: the screen buttons and the transport. FILE saves and loads the project and the pattern.
  bool input(app::Context& ctx, const app::Input& in) noexcept override {
    if (in.press(app::Button::Params)) {  // PARAMS, and on it the next page (SHIFT + PARAMS does the same)
      if (ctx.screen() == &params_) {
        params_.next_page(ctx);
      } else {
        ctx.show(params_);
      }
      return true;
    }
    if (in.press(app::Button::Performance)) {  // PERFORM, the home screen
      if (ctx.screen() == &perform_) {  // already up: the encoder is freed to bend
        perform_.free_encoder(ctx);
      } else {
        ctx.show(perform_);
      }
      return true;
    }
    if (in.press(app::Button::Play)) {  // the transport, on every screen (the per-step carriers and the loops' bar lines run on it); MODE is F1 on PERFORM
      dsp::Engine& e = ctx.engine();
      if (ctx.screen() == &song_ && song_.start(ctx)) {  // SONG: the chain plays from the cursor (an empty chain: PLAY as everywhere)
        return true;
      }
      (void)(e.playing() ? e.stop() : e.play());
      ctx.redraw();
      return true;
    }
    // The other screen buttons open their screen (docs/UI-DESIGN.md section 3).
    if (in.press(app::Button::SampleRec)) {
      if (ctx.screen() != &sample_rec_) {
        ctx.show(sample_rec_);
      }
      ctx.redraw();
      return true;
    }
    if (in.press(app::Button::SamplePlay)) {
      if (ctx.screen() != &sample_) {
        ctx.show(sample_);
      }
      ctx.redraw();
      return true;
    }
    if (in.press(app::Button::SampleEdit)) {
      if (ctx.screen() != &sample_edit_) {
        ctx.show(sample_edit_);
      }
      ctx.redraw();
      return true;
    }
    if (in.press(app::Button::SampleLoad)) {  // the WAV picker; F8 returns to the screen it was opened from
      if (ctx.screen() != &sample_load_) {
        sample_load_.set_back(ctx.screen());
        ctx.show(sample_load_);
      }
      ctx.redraw();
      return true;
    }
    if (in.press(app::Button::File)) {  // the project file; F8 returns to the screen it was opened from
      if (ctx.screen() != &file_) {
        file_.set_back(ctx.screen());
        ctx.show(file_);
      }
      ctx.redraw();
      return true;
    }
    if (in.press(app::Button::Song)) {  // the chain
      if (ctx.screen() != &song_) {
        ctx.show(song_);
      }
      ctx.redraw();
      return true;
    }
    if (in.press(app::Button::Master)) {  // the signal path: source, gain, monitor, dry, level, return
      if (ctx.screen() != &master_) {
        ctx.show(master_);
      }
      ctx.redraw();
      return true;
    }
    struct Lane {
      app::Button button;
      o3_voc03er::pattern::Lane lane;
    };
    static constexpr Lane kLanes[] = {
        {app::Button::Note, o3_voc03er::pattern::Lane::Note},
        {app::Button::Instr, o3_voc03er::pattern::Lane::Carrier},
        {app::Button::Vol, o3_voc03er::pattern::Lane::Level},
        {app::Button::Fx, o3_voc03er::pattern::Lane::Slice},
    };
    if (in.press(app::Button::Pattern)) {  // the 16 steps, on the lane it was left on
      if (ctx.screen() != &pattern_) {
        ctx.show(pattern_);
      }
      ctx.redraw();
      return true;
    }
    for (const Lane& l : kLanes) {  // NOTE, INSTR, VOL and FX open the same screen on their lane
      if (in.press(l.button)) {
        pattern_.set_lane(ctx, l.lane);
        if (ctx.screen() != &pattern_) {
          ctx.show(pattern_);
        }
        return true;
      }
    }
    return false;
  }

  // FILE's save, and PATTERN's F8 (through save_bank_cb): the bank to the card; false when a file job is still running.
  bool save_bank(app::Context& ctx) noexcept {
    app::Files& files = ctx.files();
    if (op_ != Op::None || files.busy()) {
      return false;
    }
    files.set_quiet(false);  // the user asked: the strip says SAVED, NO CARD, ...
    const std::size_t n = o3_voc03er::write_bank(g_bank, g_save_buf, sizeof g_save_buf);
    op_ = n != 0u && files.save("", o3_voc03er::kPatternFile, g_save_buf, n) ? Op::SavePattern : Op::None;
    ctx.redraw();
    return true;
  }
  static bool save_bank_cb(void* user, app::Context& ctx) noexcept { return static_cast<MyApp*>(user)->save_bank(ctx); }

  // MIDI pitch bend and the mod wheel (docs/UI-DESIGN.md 7c), on the user's channel (0 is omni). The wheel is scaled into BEND RANGE and adds to the
  // screen's own bend (the encoder and the strip), so the encoder's spring leaves a held wheel where it is; CC1 is the vibrato depth, held apart
  // from the MOD pad (the larger wins). A controller the user bound by MIDI learn never reaches here (midi_learned_moved takes it). Main loop.
  bool midi(app::Context& ctx, const core::MidiMessage& m) noexcept override {
    const unsigned kind = core::midi_kind(m.status);
    if (kind != 0xE0u && !(kind == 0xB0u && m.data1 == 1u)) {
      return false;
    }
    const std::uint8_t ch = ctx.midi().config().channel;
    if (ch != 0u && core::midi_channel(m.status) != ch) {
      return false;
    }
    if (kind == 0xE0u) {
      perform_.set_wheel(ctx, m.data1, m.data2);
    } else {
      perform_.set_mod_wheel(ctx, m.data2);
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
      out.text("usage: loop [1-4 rec|stop|play|halt|clear|mod|car|half|x1|x2|fwd|rev|lvl dB]").nl();
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
      case o3_voc03er::LoopAct::Clear: (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Rec), 2.0f);
                                       (void)e.set_param(voc_id_, o3_voc03er::Vocoder::loop_param(i, LF::Rec), 0.0f); break;
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

  // `pat [A-H [len n | note s n | chord s root kind voicing | car s c | lvl s v]]` (main loop): prints a pattern (or all eight), or edits one step
  // with the pure functions of stepseq.h (an edit while playing is read whole at the next step). Steps are 1..16.
  void pat(const app::DiagArgs& args, app::DiagOut& out) noexcept {
    namespace ov = o3_voc03er;
    auto show = [&](unsigned p) noexcept {
      const ov::PatternData& pd = g_bank.pattern[p];
      out.chr(static_cast<char>('A' + p)).text(p == carriers_.playing_pattern() ? "*" : " ").text(" len ").dec(ov::length_of(pd)).text(":");
      for (unsigned i = 0; i < ov::length_of(pd); ++i) {
        const ov::Step s = ov::get_step(pd, i);
        out.chr(' ');
        if (s.note == 0u) {
          out.chr('-');
        } else {
          out.dec(s.note);
          if (s.kind != 0u || s.voicing != 0u) {
            out.chr('c');
          }
        }
        if (s.carrier != 0u) {
          out.chr('/').dec(s.carrier);
        }
        if (s.level != 0u) {
          out.chr('v').dec(s.level);
        }
        if (s.slice != 0u) {
          out.chr('s').dec(s.slice);
        }
      }
      out.nl();
    };
    if (args.argc == 1u) {
      for (unsigned p = 0; p < ov::kBankPatterns; ++p) {
        show(p);
      }
      return;
    }
    ov::PatCommand c;
    if (!ov::parse_pat(args.argv + 1, args.argc - 1u, c)) {
      out.text("usage: pat [A-H [len n|<step> note n|chord root kind voic|car follow|synth|noise|s1..s4|lvl v|slc n]]").nl();
      return;
    }
    ov::PatternData& pd = g_bank.pattern[c.pattern];
    bool ok = true;
    switch (c.act) {
      case ov::PatAct::Show: break;
      case ov::PatAct::Length: ok = ov::set_length(pd, c.value); break;
      case ov::PatAct::Note: ok = ov::set_note(pd, c.step, c.value); break;
      case ov::PatAct::Chord: ok = ov::set_chord(pd, c.step, c.value, static_cast<ov::ChordKind>(c.kind), static_cast<ov::Voicing>(c.voicing)); break;
      case ov::PatAct::Carrier: ok = ov::set_carrier(pd, c.step, c.value); break;
      case ov::PatAct::Level: ok = ov::set_level(pd, c.step, c.value); break;
      case ov::PatAct::Slice: ok = ov::set_slice(pd, c.step, c.value); break;
    }
    if (!ok) {
      out.text("refused").nl();
      return;
    }
    show(c.pattern);
    if (ctx_ != nullptr) {
      ctx_->redraw();
    }
  }

  // `voc <bands|talk|tune|robot> [notes 0..8]` (main loop): the mode, and N notes held from the console (the previous ones are released first).
  void voc(const app::DiagArgs& args, app::DiagOut& out) noexcept {
    o3_voc03er::VocShape shape;
    if (ctx_ != nullptr && args.argc == 3u && o3_voc03er::parse_voc_shape(args.arg(1), args.arg(2), shape)) {
      dsp::Engine& e = ctx_->engine();
      if (shape.range) {
        (void)perform_.set_bend_range(static_cast<unsigned>(shape.value));
        out.text("voc: bendrange ").dec(static_cast<unsigned>(shape.value)).nl();
        return;
      }
      if (shape.thru) {
        (void)e.set_param(voc_id_, o3_voc03er::Vocoder::Thru, static_cast<float>(shape.value) * 0.01f);
      } else if (shape.speaks || shape.sounds) {
        (void)e.set_param(voc_id_, shape.speaks ? o3_voc03er::Vocoder::ModSrc : o3_voc03er::Vocoder::CarSrc, static_cast<float>(shape.value));
        out.text(shape.speaks ? "voc: speaks " : "voc: sounds ").dec(static_cast<unsigned>(shape.value)).nl();
        return;
      } else if (shape.glide || shape.gate || shape.arp) {
        const std::uint16_t id = shape.glide ? o3_voc03er::Vocoder::Glide : (shape.gate ? o3_voc03er::Vocoder::Gate : o3_voc03er::Vocoder::Arp);
        (void)e.set_param(voc_id_, id, static_cast<float>(shape.value));
      } else {
        (void)e.set_param(voc_id_, o3_voc03er::Vocoder::Formant, static_cast<float>(shape.value));
      }
      const unsigned size = static_cast<unsigned>(shape.value < 0 ? -shape.value : shape.value);
      out.text("voc: ").text(shape.thru ? "thru " : (shape.glide ? "glide " : (shape.gate ? "gate " : (shape.arp ? "arp " : "formant ")))).text(shape.value < 0 ? "-" : "").dec(size).nl();
      return;
    }
    o3_voc03er::VocCommand c;
    if (ctx_ == nullptr || args.argc < 2u || args.argc > 3u ||
        !o3_voc03er::parse_voc(args.arg(1), args.argc == 3u ? args.arg(2) : nullptr, c)) {
      out.text("usage: voc <bands|talk|tune|robot> [notes 0..8] | formant <st> | thru <0..100> | glide <0..500> | gate <0..4> | arp <0..4> | speaks <0..6> | sounds <0..7> | bendrange <2|5|12>").nl();
      return;
    }
    dsp::Engine& e = ctx_->engine();
    (void)e.set_param(voc_id_, o3_voc03er::Vocoder::Mode, static_cast<float>(c.mode));
    for (unsigned i = 0; i < console_notes_; ++i) {
      (void)e.send(voc_id_, o3_voc03er::Vocoder::note_off_local(o3_voc03er::kVocChord[i]));
    }
    console_notes_ = c.notes;
    for (unsigned i = 0; i < console_notes_; ++i) {
      (void)e.note_on(voc_id_, o3_voc03er::kVocChord[i], 100, o3_voc03er::Vocoder::kNoteLocal);
    }
    out.text("voc: ").text(kModeWord[c.mode]).text(" notes ").dec(console_notes_).nl();
    ctx_->redraw();
  }

  // The console's `state` after the kit's pairs (`o3console.py state` over USB; the same line is in the simulator's tests): the keyboard
  // octave (PERFORM's), pattern A's steps and carrier steps, PATTERN's cursor, the transport and the chain.
  std::size_t describe_state(const app::Context& ctx, char* out, std::size_t cap) const noexcept override {
    app::StateWriter w(out, cap);
    w.sint("octave", perform_.octave());
    w.num("steps", o3_voc03er::notes_used(g_bank.pattern[0]));
    w.num("cursor", pattern_.cursor());
    w.num("carriers", o3_voc03er::carriers_used(g_bank.pattern[0]));
    w.num("playing", ctx.engine().playing() ? 1u : 0u);
    w.num("chain", g_chain.count);  // SONG: the entries, the row lit (kChainMax: none), the cursor, LOOP CHAIN
    w.num("chain_row", song_.playing_row());
    w.num("chain_cursor", song_.cursor());
    w.num("chain_loop", g_chain.loop);
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
      char ek[8] = {'e', 'd', 'i', 't', 'e', 'd', static_cast<char>('1' + i), '\0'};
      w.num(ek, sample_.edited(i) ? 1u : 0u);  // the slot's cuts are the hand's (or a project load's)
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
    // USB is the one stereo source: a live carrier takes its right channel, what speaks stays on the left.
    (void)ctx_->engine().set_param(voc_id_, o3_voc03er::Vocoder::LiveCh, src == hal::capture::Source::Usb ? 1.0f : 0.0f);
    meter_.reset(ctx_->uptime_ms(), o3_voc03er::InputMeter::kSelectHoldoffMs);
    return acked;
  }

  // PERFORM's pickers: the audio source a pick or a finished capture asks for.
  static void pick_source_cb(void* user, hal::capture::Source src, unsigned gain) noexcept {
    MyApp& a = *static_cast<MyApp*>(user);
    if (a.ctx_ != nullptr && a.ctx_->audio_in().available()) {
      (void)a.select_source(src, gain);
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
      (void)o3_voc03er::read_bank(g_load_buf, got, g_bank);  // a file that is not a pattern is refused whole: the bank stays
    } else if (done && op_ == Op::LoadConfig) {
      int octave = 0;
      if (o3_voc03er::read_config(g_load_buf, got, octave)) {
        perform_.set_octave(octave);
        saved_octave_ = perform_.octave();  // what the card holds: nothing to save
      }
    }
    op_ = Op::None;
    ctx.redraw();
  }

  o3_voc03er::Vocoder voc_;
  o3_voc03er::PerformScreen perform_;
  o3_voc03er::SampleRecScreen sample_rec_;
  o3_voc03er::ParamsScreen params_;
  o3_voc03er::SongScreen song_;
  o3_voc03er::MasterScreen master_;
  o3_voc03er::SampleScreen sample_;
  o3_voc03er::SampleEditScreen sample_edit_;
  o3_voc03er::SampleLoadScreen sample_load_;
  o3_voc03er::FileScreen file_;
  o3_voc03er::PatternScreen pattern_;
  o3_voc03er::StepSeq carriers_;  // the step sequencer hook (stepseq.h)

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

void pat_command(const app::DiagArgs& args, app::DiagOut& out) noexcept {
  instance().pat(args, out);
}

}  // namespace

o3sdk::app::App& o3sdk_app() noexcept { return instance(); }
