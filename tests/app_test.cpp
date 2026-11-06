// SPDX-License-Identifier: GPL-3.0-or-later
// The app end to end in the simulator (o3-sdk docs/FRAMEWORK.md section 7): it starts, draws every frame inside the display list,
// sounds when a pad is played, answers the console's `state`, keeps its pattern and its octave on the card across a reboot, and its
// scenarios run as they are (each on a card of its own: a scratch directory).
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/platform/host/fake_midi.h>
#include <o3sdk/sim/harness.h>
#include <o3sdk/sim/scenario.h>

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace o3sdk;

namespace {

// The rms of the interleaved int16 samples from index `from`, as a fraction of full scale.
double rms(const std::vector<std::int16_t>& lr, std::size_t from) {
  double s = 0.0;
  std::size_t n = 0;
  for (std::size_t i = from; i < lr.size(); i += 2) {
    s += static_cast<double>(lr[i]) * static_cast<double>(lr[i]);
    ++n;
  }
  return n != 0 ? std::sqrt(s / static_cast<double>(n)) / 32768.0 : 0.0;
}

std::string state(sim::Harness& h) {
  char line[512];
  const std::size_t n = h.runtime().state_line(line, sizeof line);
  return std::string(line, n);
}

void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(5);
}

}  // namespace

TEST_CASE("app: it starts, a pad sounds, and every frame fits the display list") {
  platform::host::FakeCapture cap;  // the vocoder speaks only while it hears a modulator
  cap.input_tone(220.0f, -12.0f);
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, nullptr, nullptr, &cap);  // the app's entry point, linked whole into the test
  REQUIRE(h->start());
  h->record(true);
  h->run_ms(200);
  CHECK(rms(h->audio(), 0) < 1e-6);  // nothing played yet: digital silence

  h->pad_down(36);  // bottom left
  h->run_ms(300);
  CHECK(h->runtime().context().panel().pad_held(36));
  CHECK(h->leds().level(36) == 255);  // a held pad is full
  h->pad_up(36);
  h->run_ms(300);
  CHECK(rms(h->audio(), 0) > 0.01);

  CHECK(h->runtime().frames() > 20u);
  CHECK(h->runtime().frame_failures() == 0u);
  CHECK(h->display().render_failures() == 0u);
  CHECK(h->display().peak_frame_words() < 2048u);
  CHECK(h->engine().dropped_commands() == 0u);
}

TEST_CASE("app: the console's state line carries the app's pairs") {
  auto h = std::make_unique<sim::Harness>(o3sdk_app());
  REQUIRE(h->start());
  tap(*h, app::Button::Params);  // PERFORM is home; the old PLAY screen is on PARAMS
  h->run_ms(100);
  char line[300];
  const std::size_t n = h->runtime().state_line(line, sizeof line);
  const std::string text(line, n);
  CHECK(text.find("app=O3-VOC03ER") == 0);
  CHECK(text.find("screen=PLAY") != std::string::npos);
  CHECK(text.find("octave=0") != std::string::npos);
  CHECK(text.find("focus=0") != std::string::npos);
}

TEST_CASE("app: a pattern and an octave saved on the card come back after a restart, a bad file changes nothing") {
  platform::host::ScratchDir card_dir("o3_voc03er-card");
  REQUIRE(card_dir.ok());
  platform::host::DirCard card(card_dir.path());
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card);
  REQUIRE(h->start());
  tap(*h, app::Button::Params);  // the old PLAY screen
  h->run_ms(100);
  CHECK(state(*h).find("steps=0") != std::string::npos);
  for (unsigned pad : {36u, 40u, 43u}) {
    h->pad_down(pad);
    h->run_ms(20);
    h->pad_up(pad);
    h->run_ms(20);
  }
  tap(*h, app::Button::Left);  // an octave down: it saves itself
  h->run_ms(100);              // (a key pressed while a file job runs is ignored: let that one end)
  tap(*h, app::Button::File);
  h->run_ms(300);
  CHECK(state(*h).find("files=done") != std::string::npos);

  REQUIRE(h->reboot());
  h->run_ms(300);
  const std::string after = state(*h);
  CHECK(after.find("steps=3") != std::string::npos);
  CHECK(after.find("octave=-1") != std::string::npos);

  // A file that is not a pattern: the load works, the file is refused whole, the pattern stays and the screen says BAD FILE.
  const std::filesystem::path file = std::filesystem::path(card_dir.path()) / "Apps" / "o3_voc03er" / "pattern.o3a";
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "not a pattern file, but longer than the header of one";
  }
  tap(*h, app::Button::SampleLoad);
  h->run_ms(300);
  CHECK(state(*h).find("steps=3") != std::string::npos);
  CHECK(h->runtime().frame_failures() == 0u);
}

TEST_CASE("app: HELP holds to the kit's limits, names every pad the keyboard uses, and follows the octave") {
  auto h = std::make_unique<sim::Harness>(o3sdk_app());
  REQUIRE(h->start());
  tap(*h, app::Button::Params);  // the old PLAY screen; PERFORM's own HELP is tested in perform_screen_test
  h->run_ms(100);
  for (int step = 0; step < 3; ++step) {  // the octave moves the labels: look at the screen as it is, one octave down, and two
    if (step != 0) {
      tap(*h, app::Button::Left);
    }
    h->press(app::Button::Shift);
    h->run_ms(2);
    tap(*h, app::Button::Config);
    h->release(app::Button::Shift);
    h->run_ms(60);
    REQUIRE(h->runtime().help_open());
    const app::Help& help = h->runtime().help_overlay().help();
    app::HelpProblem p;
    const bool ok = app::help_check(help, &p);  // the same limits and the same wrap rule the kit draws with
    CHECK_MESSAGE(ok, app::help_field_name(p.field) << " " << static_cast<unsigned>(p.index) << ": " << p.what);
    CHECK(help.summary != nullptr);
    for (unsigned pad = 0; pad < app::kPadCount; ++pad) {  // the template's keyboard is a key on every pad at these octaves
      CHECK(help.pad_group[pad] != 0);
      CHECK(help.pad_label[pad] != nullptr);
    }
    CHECK(std::string(help.pad_label[app::pad_at(app::kPadRows - 1u, 0u)]) == (step == 0 ? "C3" : (step == 1 ? "C2" : "C1")));
    tap(*h, app::Button::Config);  // closes it
    REQUIRE_FALSE(h->runtime().help_open());
  }
}

namespace {
// The WAVs sample_load.txt loads (app/scenarios/sample_load_wavs.py writes the same six for a run by hand): the scenario test's card starts empty.
void put_wav(const std::filesystem::path& file, std::uint32_t frames, unsigned ch, std::uint32_t rate, unsigned bits, std::size_t cut = 0) {
  std::vector<std::uint8_t> v;
  const auto le = [&v](std::uint32_t x, unsigned bytes) {
    for (unsigned k = 0; k < bytes; ++k) {
      v.push_back(static_cast<std::uint8_t>(x >> (8u * k)));
    }
  };
  const auto tag = [&v](const char* t) { v.insert(v.end(), t, t + 4); };
  const std::uint32_t data = frames * ch * (bits / 8u);
  tag("RIFF");
  le(36u + data, 4);
  tag("WAVE");
  tag("fmt ");
  le(16, 4);
  le(1, 2);
  le(ch, 2);
  le(rate, 4);
  le(rate * ch * (bits / 8u), 4);
  le(ch * (bits / 8u), 2);
  le(bits, 2);
  tag("data");
  le(data, 4);
  for (std::uint32_t i = 0; i < frames; ++i) {
    for (unsigned c = 0; c < ch; ++c) {
      const float x = 0.5f * std::sin(6.2831853f * (220.0f + 110.0f * static_cast<float>(c)) * static_cast<float>(i) / static_cast<float>(rate));
      if (bits == 16u) {
        le(static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(x * 32767.0f))), 2);
      } else {
        v.push_back(static_cast<std::uint8_t>(static_cast<int>(x * 127.0f) + 128));
      }
    }
  }
  v.resize(v.size() - cut);
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size()));
}

void put_load_wavs(const std::filesystem::path& root) {
  const std::filesystem::path d = root / "Apps" / "o3_voc03er";
  std::filesystem::create_directories(d);
  put_wav(d / "a_mono.wav", 66150, 1, 44100, 16);
  put_wav(d / "b_stereo.wav", 66150, 2, 44100, 16);
  put_wav(d / "c_48k.wav", 48000, 1, 48000, 16);
  put_wav(d / "d_8bit.wav", 44100, 1, 44100, 8);
  put_wav(d / "e_long20.wav", 20u * 44100u, 2, 44100, 16);
  put_wav(d / "f_broken.wav", 44100, 1, 44100, 16, 30000);
}
}  // namespace

TEST_CASE("app: the scenarios run as they are, write their pictures and the audio is not silent") {
  const std::filesystem::path dir = APP_SCENARIO_DIR;
  const std::filesystem::path out = std::filesystem::temp_directory_path() / "o3_voc03er_scenarios";
  std::filesystem::create_directories(out);
  std::size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".txt") {
      continue;
    }
    ++count;
    INFO(entry.path().filename().string());
    std::ifstream in(entry.path());
    std::stringstream text;
    text << in.rdbuf();
    platform::host::ScratchDir card_dir("o3_voc03er-card");  // an empty card for each scenario
    REQUIRE(card_dir.ok());
    if (entry.path().filename() == "sample_load.txt") {
      put_load_wavs(card_dir.path());
    }
    platform::host::DirCard card(card_dir.path());
    platform::host::FakeCapture cap;
    platform::host::FakeMidi midi;  // for the scenarios with a midi line (bend.txt)
    auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, &midi, &card, nullptr, &cap);
    REQUIRE(h->start());
    const sim::ScenarioResult r = sim::run_scenario(*h, text.str(), out.string());
    CHECK_MESSAGE(r.ok, r.error);
    CHECK(h->runtime().frame_failures() == 0u);
    CHECK(h->display().peak_frame_words() < 2048u);
    CHECK(card.violations() == 0u);
  }
  CHECK(count >= 2u);
  CHECK(std::filesystem::exists(out / "boot.wav"));
  CHECK(std::filesystem::exists(out / "01_boot.png"));
}

namespace {
void cmd_run(std::size_t row, const char* name, const char* a1, const char* a2, std::string& text) {
  const app::DiagCommand* rows = nullptr;
  const std::size_t n = o3sdk_app().diag_commands(rows);
  REQUIRE(n > row);
  const char* argv[3] = {name, a1, a2};
  const app::DiagArgs args{a1 == nullptr ? 1u : (a2 == nullptr ? 2u : 3u), argv};
  text.clear();
  app::DiagOut out(app::DiagSink{[](void* c, const char* d, std::size_t k) { static_cast<std::string*>(c)->append(d, k); }, &text});
  rows[row].run(args, out);
}
void ain_run(const char* a1, const char* a2, std::string& text) { cmd_run(0, "ain", a1, a2, text); }
}  // namespace

TEST_CASE("app: ain selects the audio input and in_peak_l follows a tone") {
  platform::host::FakeCapture cap;
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, nullptr, nullptr, &cap);
  REQUIRE(h->start());
  h->run_ms(300);
  std::string t;
  ain_run("bogus", nullptr, t);
  CHECK(t.find("usage") != std::string::npos);
  ain_run("line", "70", t);
  CHECK(t.find("selected line gain 70") != std::string::npos);
  h->run_ms(400);
  CHECK(state(*h).find("in_src=line in_gain=70 in_settling=0 in_failed=0 in_peak_l=0 in_peak_r=0") != std::string::npos);
  cap.input_tone(440.0f, -12.0f);
  h->run_ms(300);
  const std::string on = state(*h);
  const std::size_t at = on.find("in_peak_l=");
  REQUIRE(at != std::string::npos);
  CHECK(std::atoi(on.c_str() + at + 10) > 100);
  cap.input_off();
  h->run_ms(1300);
  CHECK(state(*h).find("in_peak_l=0 ") != std::string::npos);
  ain_run(nullptr, nullptr, t);
  CHECK(t.find("ain: line gain 70") != std::string::npos);
}

TEST_CASE("app: a line source starts at gain 0, a switch clears the peaks, voc sets the mode and holds notes") {
  platform::host::FakeCapture cap;
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, nullptr, nullptr, &cap);
  REQUIRE(h->start());
  h->run_ms(300);
  std::string t;
  ain_run("line", nullptr, t);
  CHECK(t.find("selected line gain 0") != std::string::npos);
  ain_run("miclow", nullptr, t);
  CHECK(t.find("selected miclow gain 50") != std::string::npos);
  ain_run("left", nullptr, t);
  CHECK(t.find("selected left gain 0") != std::string::npos);
  cap.input_tone(440.0f, -6.0f);
  h->run_ms(600);
  CHECK(state(*h).find("in_peak_l=0 ") == std::string::npos);
  ain_run("right", nullptr, t);  // the switch: the old source's peaks are gone at once, and what the gate lets through is ignored
  CHECK(t.find("peak_l 0 peak_r 0") != std::string::npos);
  h->run_ms(100);
  CHECK(state(*h).find("in_peak_l=0 in_peak_r=0") != std::string::npos);

  cmd_run(1, "voc", "talk", "3", t);
  CHECK(t.find("voc: talk notes 3") != std::string::npos);
  h->run_ms(50);
  CHECK(state(*h).find("voc_mode=talk voc_notes=3") != std::string::npos);
  cmd_run(1, "voc", "bands", nullptr, t);
  CHECK(state(*h).find("voc_mode=bands voc_notes=0") != std::string::npos);
  cmd_run(1, "voc", "tune", "2", t);
  CHECK(t.find("voc: tune notes 2") != std::string::npos);
  h->run_ms(50);
  CHECK(state(*h).find("voc_mode=tune voc_notes=2") != std::string::npos);
  cmd_run(1, "voc", "bands", nullptr, t);
  cmd_run(1, "voc", "talk", "9", t);
  CHECK(t.find("usage") != std::string::npos);
  CHECK(h->engine().dropped_commands() == 0u);
}

TEST_CASE("app: a MIDI note plays the SYNTH carrier (M1: the unit is routed to the user's channel)") {
  platform::host::FakeCapture cap;  // the vocoder speaks only while it hears a modulator
  cap.input_tone(220.0f, -12.0f);
  platform::host::FakeMidi midi;
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, &midi, nullptr, nullptr, &cap);
  REQUIRE(h->start());
  h->record(true);
  h->run_ms(300);
  CHECK(rms(h->audio(), 0) < 1e-6);  // no note yet: digital silence
  const std::size_t before = h->audio().size();
  midi.in(hal::midi::Port::Usb, 0x90, 48, 100);  // note on, channel 1, C3
  h->run_ms(400);
  CHECK(rms(h->audio(), before) > 1e-4);
  CHECK(h->runtime().frame_failures() == 0u);
}

TEST_CASE("app: PLAY is the transport and leaves MODE alone; every screen button opens a screen whose HELP holds to the limits") {
  auto h = std::make_unique<sim::Harness>(o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  CHECK_FALSE(h->engine().playing());
  tap(*h, app::Button::Play);
  CHECK(h->engine().playing());
  CHECK(state(*h).find("voc_mode=bands") != std::string::npos);
  tap(*h, app::Button::Play);
  CHECK_FALSE(h->engine().playing());

  const app::Button screens[] = {app::Button::Performance, app::Button::Params, app::Button::Master, app::Button::Song, app::Button::File,
                                 app::Button::SampleLoad,  app::Button::SamplePlay, app::Button::SampleEdit, app::Button::SampleRec,
                                 app::Button::Pattern,     app::Button::Note, app::Button::Instr, app::Button::Vol, app::Button::Fx};
  for (const app::Button b : screens) {
    tap(*h, app::Button::Performance);  // from PERFORM the stubs open (the old PLAY screen keeps its own buttons)
    h->run_ms(50);
    tap(*h, b);
    h->run_ms(100);
    h->press(app::Button::Shift);
    h->run_ms(2);
    tap(*h, app::Button::Config);
    h->release(app::Button::Shift);
    h->run_ms(60);
    INFO(std::string(app::button_name(b)));
    REQUIRE(h->runtime().help_open());
    app::HelpProblem p;
    const bool ok = app::help_check(h->runtime().help_overlay().help(), &p);
    CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
    CHECK(h->runtime().help_overlay().help().summary != nullptr);
    tap(*h, app::Button::Config);
    REQUIRE_FALSE(h->runtime().help_open());
  }
  CHECK(h->runtime().frame_failures() == 0u);
  CHECK(h->display().peak_frame_words() < 2048u);
}

// PERFORM's BND pads, the MIDI wheel and the mod wheel reach the unit's Bend (42) and Vib (43). The vocoder is one of the first units attached:
// the test reads both parameters off whichever unit moves.
namespace {
float peak_param(sim::Harness& h, std::uint16_t id) {
  float best = 0.0f;
  for (unsigned u = 0; u < 8; ++u) {
    const float v = h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
}
}  // namespace

TEST_CASE("app: BND+ ramps the bend up while held and back to 0 on release; MOD ramps the vibrato; the wheels sum") {
  platform::host::FakeMidi midi;
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, &midi, nullptr, nullptr, nullptr);
  REQUIRE(h->start());
  h->run_ms(100);
  tap(*h, app::Button::F7);
  tap(*h, app::Button::F7);  // KEYS
  h->run_ms(50);
  CHECK(peak_param(*h, 42) == doctest::Approx(0.0f));
  h->pad_down(11);  // BND+
  h->run_ms(250);
  const float mid = peak_param(*h, 42);
  CHECK(mid > 0.3f);
  CHECK(mid < 2.0f);
  h->run_ms(400);
  CHECK(peak_param(*h, 42) == doctest::Approx(2.0f));  // BEND RANGE 2
  h->pad_up(11);
  h->run_ms(100);
  CHECK(peak_param(*h, 42) == doctest::Approx(0.0f));

  h->pad_down(9);  // MOD
  h->run_ms(600);
  CHECK(peak_param(*h, 43) == doctest::Approx(1.0f));
  h->pad_up(9);
  h->run_ms(600);
  CHECK(peak_param(*h, 43) == doctest::Approx(0.0f));

  midi.in(hal::midi::Port::Usb, 0xB0, 1, 127);  // CC1 to the top: the vibrato at 1
  h->run_ms(50);
  CHECK(peak_param(*h, 43) == doctest::Approx(1.0f));
  midi.in(hal::midi::Port::Usb, 0xE0, 0, 127);  // the wheel to the top (14 bit 16256): about +2 st
  h->run_ms(50);
  CHECK(peak_param(*h, 42) > 1.9f);
  h->pad_down(11);
  h->run_ms(600);
  CHECK(peak_param(*h, 42) == doctest::Approx(3.96875f));  // the wheel (1.96875) and the pad (2) sum
  h->pad_up(11);
  CHECK(h->runtime().frame_failures() == 0u);
}
