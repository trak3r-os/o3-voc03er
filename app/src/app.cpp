// SPDX-License-Identifier: GPL-3.0-or-later
//
// O3-VOC03ER: the app. An app is a whole firmware image (o3-sdk docs/FRAMEWORK.md, "Apps"): it owns its units and screens as plain
// objects (no heap), wires them into the engine in setup() and shows the first screen. The one entry point at the end is all the
// platform's main and the simulator know of it.
//
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

#include "play_screen.h"

#include <o3_voc03er/pattern.h>
#include <o3_voc03er/units/voice/voice.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/context.h>
#include <o3sdk/app/audio_in.h>
#include <o3sdk/app/diag.h>
#include <o3sdk/dsp/engine.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

using namespace o3sdk;

class MyApp;
MyApp& instance() noexcept;
void ain_command(const app::DiagArgs& args, app::DiagOut& out) noexcept;

constexpr app::DiagCommand kCommands[] = {
    {"ain", "ain <source> [gain 0..100]: select the audio input, show its level", &ain_command},
};

struct SourceName {
  const char* name;
  hal::capture::Source source;
};
constexpr SourceName kSources[] = {
    {"line", hal::capture::Source::LineMix},   {"left", hal::capture::Source::LineLeft},   {"right", hal::capture::Source::LineRight},
    {"miclow", hal::capture::Source::MicLow},  {"michigh", hal::capture::Source::MicHigh}, {"radio", hal::capture::Source::Radio},
    {"usb", hal::capture::Source::Usb},
};

const char* source_name(hal::capture::Source s) noexcept {
  for (const SourceName& n : kSources) {
    if (n.source == s) {
      return n.name;
    }
  }
  return "?";
}

// Per-mille of full scale, 0..1000+ (saturating far above).
unsigned permille(float peak) noexcept {
  const float v = peak * 1000.0f + 0.5f;
  return v <= 0.0f ? 0u : (v > 100000.0f ? 100000u : static_cast<unsigned>(v));
}

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
    const dsp::UnitId voice = e.attach(voice_);
    // Slot 0 plays the voice; the mixer's level, pan and sends are the slot's. A refused call is counted (Engine::setup_errors) and the
    // simulator and app_test fail on it, so the result need not be checked here.
    (void)e.set_source(0, voice);
    play_.init(voice);
    // setup() can run again on this object (a test starts a fresh simulator on it, the simulator's `reboot`): the file state starts over too.
    op_ = Op::None;
    boot_ = Boot::Pattern;
    saved_octave_ = play_.octave();
    ctx.show(play_);
  }

  // The file jobs end and start here, once per pass of the main loop (a Screen has an update() too, for what only it needs).
  void update(app::Context& ctx) noexcept override {
    poll_peaks(ctx);
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
      op_ = files.load("", o3_voc03er::kPatternFile, load_buf_, sizeof load_buf_) ? Op::LoadPattern : Op::None;
      boot_ = Boot::Config;
      return;
    }
    if (boot_ == Boot::Config) {
      files.set_quiet(true);
      op_ = files.load("", o3_voc03er::kConfigFile, load_buf_, sizeof load_buf_) ? Op::LoadConfig : Op::None;
      boot_ = Boot::Done;
      return;
    }
    // The setting saves itself when it changes (and the card is there): the file's bytes are made now and stay in cfg_buf_ until the job ends.
    if (play_.octave() != saved_octave_) {
      saved_octave_ = play_.octave();  // (even when refused: it is tried again at the next change, not every pass)
      const std::size_t n = o3_voc03er::write_config(saved_octave_, cfg_buf_, sizeof cfg_buf_);
      files.set_quiet(true);
      op_ = n != 0u && files.save("", o3_voc03er::kConfigFile, cfg_buf_, n) ? Op::SaveConfig : Op::None;
    }
  }

  // The keys the screen does not use: FILE saves the pattern, SAMPLE LOAD loads it. A key pressed while a job runs is ignored (busy()).
  bool input(app::Context& ctx, const app::Input& in) noexcept override {
    const bool save = in.press(app::Button::File);
    if (!save && !in.press(app::Button::SampleLoad)) {
      return false;
    }
    app::Files& files = ctx.files();
    if (op_ == Op::None && !files.busy()) {
      files.set_quiet(false);  // the user asked: the strip says SAVED, NO CARD, ...
      play_.set_note(nullptr);
      if (save) {
        const std::size_t n = o3_voc03er::write_pattern(play_.pattern(), save_buf_, sizeof save_buf_);
        op_ = n != 0u && files.save("", o3_voc03er::kPatternFile, save_buf_, n) ? Op::SavePattern : Op::None;
      } else {
        op_ = files.load("", o3_voc03er::kPatternFile, load_buf_, sizeof load_buf_) ? Op::LoadPattern : Op::None;
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
      for (const SourceName& n : kSources) {
        if (std::strcmp(args.arg(1), n.name) == 0) {
          src = &n.source;
        }
      }
      unsigned gain = 50;
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
      const bool acked = in.select(*src, gain);
      out.text(acked ? "ain: selected " : "ain: select refused ");
    } else {
      out.text("ain: ");
    }
    out.text(source_name(in.source())).text(" gain ").dec(in.gain()).text(" settling ").dec(in.settling() ? 1u : 0u);
    out.text(" failed ").dec(in.failed() ? 1u : 0u).text(" peak_l ").dec(peak_l_).text(" peak_r ").dec(peak_r_).nl();
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
    w.num("focus", focus);
    w.str("value", value);
    const app::AudioIn& in = ctx.audio_in();
    const bool have = in.available();
    w.str("in_src", have ? source_name(in.source()) : "none");
    w.num("in_gain", have ? in.gain() : 0u);
    w.num("in_settling", have && in.settling() ? 1u : 0u);
    w.num("in_failed", have && in.failed() ? 1u : 0u);
    w.num("in_peak_l", peak_l_);
    w.num("in_peak_r", peak_r_);
    return w.size();
  }

 private:
  // Peaks (main loop only, update() writes, describe_state reads): peaks() is polled every pass (it measures only while called at least
  // once a second) and its per-pass maxima fill two 500 ms buckets; the shown value is the larger of the two, so it covers 0.5..1 s.
  void poll_peaks(app::Context& ctx) noexcept {
    app::AudioIn& in = ctx.audio_in();
    if (!in.available()) {
      peak_l_ = peak_r_ = 0;
      return;
    }
    const dsp::InputPeaks p = in.peaks();
    const std::uint64_t now = ctx.uptime_ms();
    if (now - bucket_start_ >= kBucketMs) {
      prev_l_ = now - bucket_start_ >= 2u * kBucketMs ? 0.0f : cur_l_;
      prev_r_ = now - bucket_start_ >= 2u * kBucketMs ? 0.0f : cur_r_;
      cur_l_ = cur_r_ = 0.0f;
      bucket_start_ = now;
    }
    cur_l_ = p.l > cur_l_ ? p.l : cur_l_;
    cur_r_ = p.r > cur_r_ ? p.r : cur_r_;
    peak_l_ = permille(cur_l_ > prev_l_ ? cur_l_ : prev_l_);
    peak_r_ = permille(cur_r_ > prev_r_ ? cur_r_ : prev_r_);
  }

  app::Context* ctx_ = nullptr;
  static constexpr std::uint64_t kBucketMs = 500;
  float cur_l_ = 0.0f, cur_r_ = 0.0f, prev_l_ = 0.0f, prev_r_ = 0.0f;
  std::uint64_t bucket_start_ = 0;
  unsigned peak_l_ = 0, peak_r_ = 0;

  enum class Op : std::uint8_t { None, LoadPattern, LoadConfig, SavePattern, SaveConfig };  // the job in flight
  enum class Boot : std::uint8_t { Pattern, Config, Done };                                 // the loads at start, in order

  // A job ended: a load that is Done is checked and applied. A load that is not a pattern changes nothing and says so (BAD FILE); one that
  // failed (no file, no card) has already said why in status().
  void settle(app::Context& ctx, bool done) noexcept {
    const std::size_t got = ctx.files().loaded();
    if (done && op_ == Op::LoadPattern) {
      o3_voc03er::Pattern p;
      if (o3_voc03er::read_pattern(load_buf_, got, p)) {
        play_.set_pattern(p);
      } else {
        play_.set_note("BAD FILE");
      }
    } else if (done && op_ == Op::LoadConfig) {
      int octave = 0;
      if (o3_voc03er::read_config(load_buf_, got, octave)) {
        play_.set_octave(octave);
        saved_octave_ = play_.octave();  // what the card holds: nothing to save
      }
    }
    op_ = Op::None;
    ctx.redraw();
  }

  o3_voc03er::Voice voice_;
  o3_voc03er::PlayScreen play_;

  Op op_ = Op::None;
  Boot boot_ = Boot::Pattern;
  int saved_octave_ = 0;  // the octave the card holds (or was last asked to)
  // The buffers of the file jobs: word aligned, and a save's stays unchanged until its job ends, so each file has its own; one load at a
  // time shares one (the files service runs one job at a time).
  alignas(4) std::uint8_t save_buf_[o3_voc03er::kPatternFileMax] = {};
  alignas(4) std::uint8_t cfg_buf_[o3_voc03er::kConfigFileMax] = {};
  alignas(4) std::uint8_t load_buf_[o3_voc03er::kPatternFileMax] = {};
  static_assert(sizeof save_buf_ % 4 == 0 && sizeof cfg_buf_ % 4 == 0 && sizeof load_buf_ % 4 == 0, "word-aligned buffers are whole words");
};

MyApp& instance() noexcept {
  static MyApp app;
  return app;
}

// `ain <line|left|right|miclow|michigh|radio|usb> [gain 0..100]` selects the input (default gain 50); `ain` alone prints it.
void ain_command(const app::DiagArgs& args, app::DiagOut& out) noexcept {
  instance().ain(args, out);
}

}  // namespace

o3sdk::app::App& o3sdk_app() noexcept { return instance(); }
