// SPDX-License-Identifier: GPL-3.0-or-later
// FILE (U9c2): the pure part (app/src/file_logic.h) and one screen test on a card in a scratch directory: a damaged project is refused and changes nothing.
#include <doctest/doctest.h>

#include "../app/src/file_logic.h"

#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/platform/host/fake_capture.h>
#include <o3sdk/sim/harness.h>

#include <array>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace o3sdk;
namespace fl = o3_voc03er::file;
namespace sample = o3_voc03er::sample;
using o3_voc03er::Project;
using o3_voc03er::ProjectSlot;

TEST_CASE("file: next_id walks the ids the project holds and this build restores") {
  Project p = {};
  CHECK(fl::next_id(p, 0) == o3_voc03er::kEngineIds);
  unsigned held = 0;
  for (unsigned id = 0; id < o3_voc03er::kEngineIds; ++id) {
    if (o3_voc03er::engine_id_known(id) && o3_voc03er::project_set_engine(p, id, 0.5f)) {
      ++held;
    }
  }
  unsigned seen = 0;
  for (unsigned id = fl::next_id(p, 0); id < o3_voc03er::kEngineIds; id = fl::next_id(p, id + 1u)) {
    ++seen;
  }
  CHECK(seen == held);
  CHECK(held > 0u);
  CHECK(fl::next_id(p, o3_voc03er::kEngineIds) == o3_voc03er::kEngineIds);
}

TEST_CASE("file: a slot's record is restored only onto audio of its length") {
  ProjectSlot rec = {};
  CHECK(!fl::slot_fits(rec, 0));
  CHECK(!fl::slot_fits(rec, 1000));
  sample::even_cuts(rec.cuts, 1000);
  CHECK(fl::slot_fits(rec, 1000));
  CHECK(!fl::slot_fits(rec, 999));
  CHECK(!fl::slot_fits(rec, 0));
}

TEST_CASE("file: merge_slot keeps the record of an empty slot, takes the live cuts of a slot with audio") {
  ProjectSlot kept = {};
  sample::even_cuts(kept.cuts, 2000);
  ProjectSlot out = {};
  sample::SliceTable none = {};
  fl::merge_slot(out, kept, none, 0);  // no audio: the record stays for the next slice
  CHECK(out.cuts.length == 2000u);
  CHECK(out.cuts.count == kept.cuts.count);

  fl::merge_slot(out, kept, none, 3000);  // audio of another length: the old record goes, even cuts on the live length
  CHECK(out.cuts.length == 3000u);
  CHECK(out.cuts.count != 0u);

  sample::SliceTable live = {};
  sample::even_cuts(live, 2000, 4);
  fl::merge_slot(out, kept, live, 2000);  // the same length: the live cuts win
  CHECK(out.cuts.count == 4u);
  CHECK(out.cuts.length == 2000u);
}

TEST_CASE("file: the defaults of a new project and the line a load says") {
  Project p = {};
  fl::default_stat(p);
  CHECK(p.stat == 1);
  CHECK(p.tempo_mbpm == 120000u);
  CHECK(p.pattern == 0);
  CHECK(p.voicing == 3);  // P3: the defaults of the 1.1 fields
  CHECK(p.bend_row == 0);
  CHECK(p.bend_range == 2);
  CHECK(p.follow == 0);
  CHECK(p.preset_len == 0);
  CHECK(o3_voc03er::project_preset_index(p) == -1);
  const o3_voc03er::pads::Layout dl = o3_voc03er::project_layout(p);
  CHECK(dl.row[0] == o3_voc03er::pads::Plays::Note);
  CHECK(dl.row[1] == o3_voc03er::pads::Plays::Triad);
  CHECK(dl.row[2] == o3_voc03er::pads::Plays::Formant);
  CHECK(o3_voc03er::stat_valid(p));
  char line[72];
  fl::loaded_note(line, sizeof line, 2, 13);
  CHECK(std::string(line) == "LOADED. SLOTS 2 RESTORED, 13 KEPT FOR LATER");
  char tiny[10];
  fl::loaded_note(tiny, sizeof tiny, 2, 1);
  CHECK(std::string(tiny).size() == 9u);
}

namespace {
void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(5);
}
std::string state(sim::Harness& h) {
  char line[1024];  // the app's pairs run past 512 bytes: loop3 and later are cut there
  const std::size_t n = h.runtime().state_line(line, sizeof line);
  return std::string(line, n);
}
}  // namespace

TEST_CASE("file: the slots a SAVE WAVS and a LOAD visit, in order") {
  std::uint32_t lens[fl::kWavSlots] = {0, 100, 0, 7};
  CHECK(fl::next_save_slot(lens, 0) == 1u);
  CHECK(fl::next_save_slot(lens, 2) == 3u);
  CHECK(fl::next_save_slot(lens, 4) == fl::kWavSlots);
  const std::uint32_t none[fl::kWavSlots] = {};
  CHECK(fl::next_save_slot(none, 0) == fl::kWavSlots);

  Project p = {};
  CHECK(fl::next_load_slot(p, 0) == fl::kWavSlots);
  CHECK(fl::set_wav_name(p.slot[2], "slot3_001.wav"));
  CHECK(fl::set_wav_name(p.slot[0], "slot1_004.wav"));
  CHECK(fl::next_load_slot(p, 0) == 0u);
  CHECK(fl::next_load_slot(p, 1) == 2u);
  CHECK(fl::next_load_slot(p, 3) == fl::kWavSlots);
}

TEST_CASE("file: the number the kit's save_wav picks, from the folder's listing") {
  const char* names[] = {"slot1_001.wav", "SLOT1_002.WAV", "slot1_004.wav", "slot2_003.wav", "slot1_00x.wav", "slot1_5.wav", "project.o3a", "slot1_003.wav.bak"};
  auto at = [&](std::size_t i) { return names[i]; };
  CHECK(fl::wav_number("slot1", "slot1_007.wav") == 7u);
  CHECK(fl::wav_number("slot1", "SLOT1_012.WAV") == 12u);
  CHECK(fl::wav_number("slot1", "slot2_007.wav") == 0u);
  CHECK(fl::wav_number("slot1", "slot1_007.mp3") == 0u);
  CHECK(fl::wav_number("slot1", "slot1-007.wav") == 0u);
  CHECK(fl::wav_number("slot1", "") == 0u);
  CHECK(fl::first_free_wav_number("slot1", 0, at) == 1u);
  CHECK(fl::first_free_wav_number("slot1", 3, at) == 3u);  // 001 and 002 taken (any case): 003 is free, a gap below 004 is filled
  CHECK(fl::first_free_wav_number("slot1", 8, at) == 3u);  // slot1_003.wav.bak is not a WAV of the stem
  CHECK(fl::first_free_wav_number("slot2", 8, at) == 1u);
  CHECK(fl::first_free_wav_number("slot3", 8, at) == 1u);

  // All 999 taken: 0, never a number past 999.
  static char full[999][16];
  for (unsigned i = 0; i < 999u; ++i) {
    char n[o3_voc03er::kWavNameMax + 1];
    fl::wav_file_name(n, "slot1", i + 1u);
    std::strcpy(full[i], n);
  }
  CHECK(fl::first_free_wav_number("slot1", 999, [&](std::size_t i) { return static_cast<const char*>(full[i]); }) == 0u);
  CHECK(fl::first_free_wav_number("slot1", 998, [&](std::size_t i) { return static_cast<const char*>(full[i]); }) == 999u);
}

TEST_CASE("file: a WAV's stem, its name and the record that keeps it") {
  char stem[6];
  fl::wav_stem(stem, 0);
  CHECK(std::string(stem) == "slot1");
  fl::wav_stem(stem, 3);
  CHECK(std::string(stem) == "slot4");
  char name[o3_voc03er::kWavNameMax + 1];
  fl::wav_file_name(name, "slot2", 7);
  CHECK(std::string(name) == "slot2_007.wav");
  fl::wav_file_name(name, "slot2", 123);
  CHECK(std::string(name) == "slot2_123.wav");
  CHECK(fl::wav_number("slot2", name) == 123u);

  ProjectSlot rec = {};
  CHECK_FALSE(fl::wav_missing_keeps_record(rec));
  CHECK_FALSE(fl::set_wav_name(rec, ""));
  CHECK(rec.name_len == 0u);
  const std::string too_long(o3_voc03er::kWavNameMax + 1u, 'a');
  CHECK_FALSE(fl::set_wav_name(rec, too_long.c_str()));
  CHECK(rec.name_len == 0u);
  CHECK(fl::set_wav_name(rec, "slot2_007.wav"));
  CHECK(rec.name_len == 13u);
  CHECK(std::string(rec.name) == "slot2_007.wav");
  CHECK(fl::wav_missing_keeps_record(rec));
  const std::string longest(o3_voc03er::kWavNameMax, 'b');
  CHECK(fl::set_wav_name(rec, longest.c_str()));
  CHECK(rec.name_len == o3_voc03er::kWavNameMax);
}

TEST_CASE("file: the lines the strip says during a WAV run") {
  char line[64];
  fl::wav_job_line(line, sizeof line, "SAVING", 1, 0, 3);
  CHECK(std::string(line) == "SAVING WAV: SLOT 2 (1 OF 3)");
  fl::wav_job_line(line, sizeof line, "LOADING", 3, 2, 3);
  CHECK(std::string(line) == "LOADING WAV: SLOT 4 (3 OF 3)");
  fl::wavs_saved_note(line, sizeof line, 1);
  CHECK(std::string(line) == "SAVED. 1 WAV AND THE PROJECT");
  fl::wavs_saved_note(line, sizeof line, 3);
  CHECK(std::string(line) == "SAVED. 3 WAVS AND THE PROJECT");
  fl::wavs_loaded_note(line, sizeof line, 2, 0, 0, "");
  CHECK(std::string(line) == "LOADED. 2 WAVS");
  fl::wavs_loaded_note(line, sizeof line, 1, 0, 0, "");
  CHECK(std::string(line) == "LOADED. 1 WAV");
  fl::wavs_loaded_note(line, sizeof line, 2, 1, 2, "FILE NOT FOUND");
  CHECK(std::string(line) == "LOADED. 2 WAVS, 1 SKIPPED: SLOT 3 FILE NOT FOUND");
  fl::wav_wait_line(line, sizeof line, 0);
  CHECK(std::string(line) == "SLOT 1 IS GREY UNTIL ITS WAV HAS LANDED");
  // A small buffer never overruns: the line is cut and stays terminated.
  char small[8];
  fl::wav_wait_line(small, sizeof small, 2);
  CHECK(std::strlen(small) < sizeof small);
}

TEST_CASE("file: a damaged project is refused and the unit keeps its values") {
  platform::host::ScratchDir card_dir("o3_voc03er-file");
  REQUIRE(card_dir.ok());
  const std::filesystem::path folder = std::filesystem::path(card_dir.path()) / "Apps" / "o3_voc03er" / "BAD";
  std::filesystem::create_directories(folder);
  {
    std::ofstream out(folder / "project.o3a", std::ios::binary | std::ios::trunc);
    out << "this is not a project file, but it is longer than the header of one";
  }
  platform::host::DirCard card(card_dir.path());
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card);
  REQUIRE(h->start());
  h->run_ms(300);
  tap(*h, app::Button::F1);  // PERFORM's mode key: two taps, BANDS to TALK
  tap(*h, app::Button::F1);
  h->run_ms(700);
  CHECK(state(*h).find("voc_mode=talk") != std::string::npos);
  tap(*h, app::Button::File);
  h->run_ms(100);
  tap(*h, app::Button::F1);  // LOAD asks
  tap(*h, app::Button::F1);  // confirmed: the picker lists the folders
  h->run_ms(300);
  tap(*h, app::Button::F1);  // OPEN the one folder
  h->run_ms(500);
  CHECK(state(*h).find("voc_mode=talk") != std::string::npos);
  CHECK(h->runtime().frame_failures() == 0u);
  // The refusal is real: the card still holds the damaged file as it was (a load never writes).
  CHECK(std::filesystem::file_size(folder / "project.o3a") == 67u);
}

TEST_CASE("file: U9c4 level steps, captured values and their restore") {
  CHECK(fl::level_to_half(0.0f) == 0);
  CHECK(fl::level_to_half(-3.5f) == -7);
  CHECK(fl::level_to_half(2.25f) == 5);  // rounded away from zero
  CHECK(fl::level_to_half(-2.25f) == -5);
  CHECK(fl::level_to_half(100.0f) == 24);  // held to the file's range
  CHECK(fl::level_to_half(-100.0f) == -48);
  CHECK(fl::level_to_half(std::numeric_limits<float>::quiet_NaN()) == 0);
  CHECK(fl::half_to_level(-7) == doctest::Approx(-3.5f));

  ProjectSlot rec = {};
  rec.cuts.length = 1000;
  fl::SlotValues v;
  v.speed = 2;
  v.reverse = true;
  v.level_db = -6.0f;
  v.trim_start = 100;
  v.trim_end = 900;
  fl::capture_values(rec, v);
  CHECK(rec.values == 1u);

  fl::SlotValues back;
  CHECK(fl::restore_values(rec, 1000, back));
  CHECK(back.speed == 2u);
  CHECK(back.reverse);
  CHECK(back.level_db == doctest::Approx(-6.0f));
  CHECK(back.trim_start == 100u);
  CHECK(back.trim_end == 900u);
  CHECK_FALSE(fl::restore_values(rec, 999, back));  // another audio: nothing restored

  v.trim_end = 1001;  // a trim off the slot is not kept
  fl::capture_values(rec, v);
  CHECK(fl::restore_values(rec, 1000, back));
  CHECK(back.trim_start == 0u);
  CHECK(back.trim_end == 0u);

  ProjectSlot old = {};  // a record from before: no values
  old.cuts.length = 1000;
  CHECK_FALSE(fl::restore_values(old, 1000, back));
}

TEST_CASE("file: a LOAD goes on to its end when FILE is left at once (the job is the app's service, not the screen's update)") {
  platform::host::ScratchDir card_dir("o3_voc03er-file-leave");
  REQUIRE(card_dir.ok());
  platform::host::DirCard card(card_dir.path());
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card);
  REQUIRE(h->start());
  h->run_ms(300);
  tap(*h, app::Button::F1);  // two taps of PERFORM's mode key: BANDS to TALK
  tap(*h, app::Button::F1);
  h->run_ms(700);
  REQUIRE(state(*h).find("voc_mode=talk") != std::string::npos);
  tap(*h, app::Button::File);
  h->run_ms(40);
  tap(*h, app::Button::F2);  // SAVE AS: the name starts as PROJECT
  h->run_ms(20);
  tap(*h, app::Button::F7);
  h->run_ms(300);
  REQUIRE(std::filesystem::exists(std::filesystem::path(card_dir.path()) / "Apps" / "o3_voc03er" / "PROJECT" / "project.o3a"));
  tap(*h, app::Button::F8);
  tap(*h, app::Button::F1);  // the live mode moves off TALK; the file still holds TALK
  h->run_ms(600);
  REQUIRE(state(*h).find("voc_mode=talk") == std::string::npos);

  tap(*h, app::Button::File);
  h->run_ms(40);
  tap(*h, app::Button::F1);  // LOAD asks
  tap(*h, app::Button::F1);  // confirmed: the picker lists the folders
  h->run_ms(300);
  // OPEN and leave in the same moment: the screen sees the load start, then it is gone.
  h->press(app::Button::F1);
  h->press(app::Button::Performance);
  h->run_ms(5);
  h->release(app::Button::F1);
  h->release(app::Button::Performance);
  h->run_ms(1500);
  CHECK(state(*h).find("voc_mode=talk") != std::string::npos);
  CHECK(h->runtime().frame_failures() == 0u);

  // FILE is free again after the job: a second visit works as a fresh one.
  tap(*h, app::Button::File);
  h->run_ms(100);
  tap(*h, app::Button::F8);
  h->run_ms(100);
  CHECK(h->runtime().frame_failures() == 0u);
}

TEST_CASE("file: LE1 SAMPLE LOAD pressed while FILE runs a job is refused where the user is, and works again when the job ends") {
  platform::host::ScratchDir card_dir("o3_voc03er-file-busy");
  REQUIRE(card_dir.ok());
  platform::host::DirCard card(card_dir.path());
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card);
  REQUIRE(h->start());
  h->run_ms(300);
  tap(*h, app::Button::File);
  h->run_ms(40);
  tap(*h, app::Button::F2);  // SAVE AS PROJECT
  h->run_ms(20);
  tap(*h, app::Button::F7);
  h->run_ms(300);
  REQUIRE(std::filesystem::exists(std::filesystem::path(card_dir.path()) / "Apps" / "o3_voc03er" / "PROJECT" / "project.o3a"));
  tap(*h, app::Button::F8);
  h->run_ms(40);
  tap(*h, app::Button::File);
  h->run_ms(40);
  tap(*h, app::Button::F1);  // LOAD asks
  tap(*h, app::Button::F1);  // confirmed: the picker lists the folders
  h->run_ms(300);
  // OPEN, and in a later pass SAMPLE LOAD: the job is in hand, so the key opens nothing (the state line names the screen SAMPLE_LOAD).
  h->press(app::Button::F1);  // events are taken in order: OPEN starts the job, then SAMPLE LOAD finds it in hand
  h->press(app::Button::SampleLoad);
  h->run_ms(1);
  h->release(app::Button::F1);
  h->release(app::Button::SampleLoad);
  CHECK(state(*h).find("screen=SAMPLE_LOAD") == std::string::npos);
  CHECK(state(*h).find("file_note=FILE_IS_BUSY") != std::string::npos);
  CHECK(state(*h).find("screen=SAMPLE_LOAD") == std::string::npos);
  CHECK(state(*h).find("screen=FILE") != std::string::npos);
  h->run_ms(1500);
  CHECK(h->runtime().frame_failures() == 0u);
  tap(*h, app::Button::SampleLoad);
  h->run_ms(100);
  CHECK(state(*h).find("screen=SAMPLE_LOAD") != std::string::npos);
}

namespace {

// The number after " key=" in the state line (0 when the key is not there).
unsigned state_num(sim::Harness& h, const char* key) {
  const std::string s = " " + state(h);
  const std::string k = std::string(" ") + key + "=";
  const std::size_t at = s.find(k);
  return at == std::string::npos ? 0u : static_cast<unsigned>(std::strtoul(s.c_str() + at + k.size(), nullptr, 10));
}

// A tap that takes two passes of the main loop: a window of a few milliseconds (a WAV on its way) holds several of them.
void quick(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(1);
  h.release(b);
  h.run_ms(1);
}

// The vocoder's own unit (the TRACT insert is a unit too): a parameter's target as the screens wrote it.
float voc_param(sim::Harness& h, std::uint16_t id) {
  for (unsigned u = 0; u < 8; ++u) {
    const dsp::UnitInfo* info = h.engine().unit_info(static_cast<dsp::UnitId>(u));
    if (info != nullptr && std::strcmp(info->name, "VOCODER") == 0) {
      return h.engine().param(static_cast<dsp::UnitId>(u), id);
    }
  }
  return -1.0f;
}

}  // namespace

TEST_CASE("file: LE2 a slot whose WAV is still on its way is not empty to SAMPLE REC, and SAMPLE EDIT waits") {
  platform::host::ScratchDir card_dir("o3_voc03er-file-waiting");
  REQUIRE(card_dir.ok());
  platform::host::DirCard card(card_dir.path());
  const std::filesystem::path project = std::filesystem::path(card_dir.path()) / "Apps" / "o3_voc03er" / "PROJECT";
  platform::host::FakeCapture cap;
  cap.input_tone(220.0f, -8.0f);
  unsigned len1 = 0;
  unsigned len2 = 0;
  {
    // Two takes of 1 BAR (slots 1 and 2), SAVE AS, SAVE WAVS: the card holds the project and both WAVs.
    auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card, nullptr, &cap);
    REQUIRE(h->start());
    h->run_ms(300);
    tap(*h, app::Button::SampleRec);
    h->run_ms(200);
    tap(*h, app::Button::F3);
    tap(*h, app::Button::F4);
    tap(*h, app::Button::F4);
    tap(*h, app::Button::F4);
    tap(*h, app::Button::Play);
    h->run_ms(500);
    tap(*h, app::Button::Rec);
    h->run_ms(9000);
    tap(*h, app::Button::Down);  // SAMPLE REC's slot 2
    tap(*h, app::Button::Rec);
    h->run_ms(9000);
    tap(*h, app::Button::Up);  // back to SAMPLE REC's slot 1: the screen object outlives this harness (a real reboot starts at 0)
    len1 = state_num(*h, "loop1");
    len2 = state_num(*h, "loop2");
    REQUIRE(len1 != 0u);
    REQUIRE(len2 != 0u);
    tap(*h, app::Button::File);
    h->run_ms(40);
    tap(*h, app::Button::F2);  // SAVE AS PROJECT
    h->run_ms(20);
    tap(*h, app::Button::F7);
    h->run_ms(200);
    tap(*h, app::Button::F5);  // SAVE WAVS
    h->run_ms(3000);
    REQUIRE(std::filesystem::exists(project / "slot1_001.wav"));
    REQUIRE(std::filesystem::exists(project / "slot2_001.wav"));
  }

  // A fresh unit (empty slots) opens the project: the WAVs land slot by slot, each waiting slot is grey until then.
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card, nullptr, &cap);
  REQUIRE(h->start());
  h->run_ms(300);
  tap(*h, app::Button::File);
  h->run_ms(40);
  tap(*h, app::Button::F1);  // LOAD asks
  tap(*h, app::Button::F1);  // the picker lists the folders
  h->run_ms(300);
  h->press(app::Button::F1);  // OPEN
  h->run_ms(1);
  h->release(app::Button::F1);
  // The window: slot 1 has landed, slot 2 is on its way (bit 1 of the mask).
  bool window = false;
  for (unsigned i = 0; i < 4000 && !window; ++i) {
    h->run_ms(1);
    window = state_num(*h, "waiting") == 2u && state_num(*h, "loop1") != 0u;
  }
  REQUIRE(window);

  // SAMPLE REC on the waiting slot 2: a take would be overwritten by the landing WAV (or overwrite it): REC does nothing.
  quick(*h, app::Button::SampleRec);
  quick(*h, app::Button::Down);
  quick(*h, app::Button::Rec);
  CHECK(state_num(*h, "waiting") == 2u);  // still in the window
  CHECK(voc_param(*h, o3_voc03er::Vocoder::loop_param(1, o3_voc03er::Vocoder::LoopField::Rec)) == doctest::Approx(0.0f));

  // SAMPLE EDIT's COPY of slot 1 in the window: refused (the landing owns the slots and the unit's Load parameter, and slot 2 looks empty).
  quick(*h, app::Button::SampleEdit);
  quick(*h, app::Button::Copy);
  quick(*h, app::Button::F1);
  CHECK(state_num(*h, "waiting") == 2u);
  h->run_ms(3000);
  CHECK(state_num(*h, "waiting") == 0u);
  CHECK(state_num(*h, "loop1") == len1);
  CHECK(state_num(*h, "loop2") == len2);  // the WAV landed whole, nothing was copied over it
  CHECK(state_num(*h, "loop3") == 0u);    // and nothing went to the next slot either
  // Once the landing is over, COPY works as ever: the next empty slot after slot 1 is slot 3 now.
  tap(*h, app::Button::Copy);
  tap(*h, app::Button::F1);
  h->run_ms(900);
  CHECK(state_num(*h, "loop3") == len1);
  CHECK(h->runtime().frame_failures() == 0u);
}

namespace {

// A project file of a made-up STAT 1.1 (and TRIG and CHOKE per slot) put on the card under `name`.
Project p3_project() {
  Project p = {};
  fl::default_stat(p);
  p.tempo_mbpm = 133500;
  p.pattern = 3;
  p.octave = 1;
  p.chord = 2;
  p.keys = 1;
  p.voicing = 1;
  p.bend_row = 1;
  p.bend_range = 12;
  p.follow = 1;
  o3_voc03er::set_project_preset(p, 4);
  o3_voc03er::pads::Layout l;
  l.row[0] = o3_voc03er::pads::Plays::PianoTriad;
  l.row[1] = o3_voc03er::pads::Plays::PianoTriad;
  l.row[2] = o3_voc03er::pads::Plays::Bend;
  l.last[0] = o3_voc03er::pads::Plays::Open;
  l.last[1] = o3_voc03er::pads::Plays::Power;
  o3_voc03er::set_project_layout(p, l);
  p.slot[1].gate = 1;
  p.slot[2].choke_off = 1;
  p.slot[3].gate = 1;
  p.slot[3].choke_off = 1;
  return p;
}

bool put_project(const std::filesystem::path& card, const char* folder, const Project& p) {
  const std::filesystem::path dir = card / "Apps" / "o3_voc03er" / folder;
  std::filesystem::create_directories(dir);
  alignas(4) static std::array<std::uint8_t, o3_voc03er::kProjectFileMax> buf;
  const std::size_t n = o3_voc03er::write_project(p, buf.data(), buf.size());
  if (n == 0) {
    return false;
  }
  std::ofstream out(dir / "project.o3a", std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(n));
  return out.good();
}

bool get_project(const std::filesystem::path& file, Project& into) {
  std::ifstream in(file, std::ios::binary);
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return !bytes.empty() && o3_voc03er::read_project(bytes.data(), bytes.size(), into);
}

}  // namespace

TEST_CASE("file: P3 a load puts the layout, FOLLOW, BEND ROW, RANGE, VOICING, the preset and TRIG and CHOKE back, and the next save keeps them; NEW resets all of it") {
  platform::host::ScratchDir card_dir("o3_voc03er-file-p3");
  REQUIRE(card_dir.ok());
  const std::filesystem::path card_root(card_dir.path());
  const Project src = p3_project();
  REQUIRE(put_project(card_root, "SRC", src));
  platform::host::DirCard card(card_dir.path());
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card);
  REQUIRE(h->start());
  h->run_ms(300);
  const auto load_src = [&] {
    tap(*h, app::Button::File);
    h->run_ms(60);
    tap(*h, app::Button::F1);  // LOAD asks
    tap(*h, app::Button::F1);  // confirmed: the picker lists the folders
    h->run_ms(300);
    tap(*h, app::Button::F1);  // OPEN the one folder
    h->run_ms(900);
  };
  const auto save_as = [&] {
    tap(*h, app::Button::File);
    h->run_ms(60);
    tap(*h, app::Button::F3);  // SAVE AS: the name starts as PROJECT
    h->run_ms(20);
    tap(*h, app::Button::F7);
    h->run_ms(400);
  };
  load_src();
  save_as();
  const std::filesystem::path out = card_root / "Apps" / "o3_voc03er" / "PROJECT" / "project.o3a";
  REQUIRE(std::filesystem::exists(out));
  Project got = {};
  REQUIRE(get_project(out, got));
  CHECK(got.stat == 1);
  CHECK(got.tempo_mbpm == src.tempo_mbpm);
  CHECK(got.pattern == src.pattern);
  CHECK(got.octave == src.octave);
  // The 1.0 fields are derived from the layout on save (Q6): a piano of triads with BEND on C is keys 1, chord 0 (TRIAD), bend row 1,
  // whatever the loaded file's own 1.0 fields said (chord 2).
  CHECK(got.chord == 0);
  CHECK(got.keys == 1);
  CHECK(got.voicing == src.voicing);
  CHECK(got.bend_row == src.bend_row);
  CHECK(got.bend_range == src.bend_range);
  CHECK(got.follow == src.follow);
  CHECK(got.preset_len == src.preset_len);
  CHECK(std::memcmp(got.preset, src.preset, sizeof got.preset) == 0);
  CHECK(std::memcmp(got.layout, src.layout, sizeof got.layout) == 0);
  for (unsigned i = 0; i < o3_voc03er::route::kSlots; ++i) {
    CHECK(got.slot[i].gate == src.slot[i].gate);
    CHECK(got.slot[i].choke_off == src.slot[i].choke_off);
  }
  CHECK(h->runtime().frame_failures() == 0u);

}

TEST_CASE("file: P3 NEW puts every field of STAT 1.1 and TRIG and CHOKE back to its default, the layout included") {
  platform::host::ScratchDir card_dir("o3_voc03er-file-p3-new");
  REQUIRE(card_dir.ok());
  const std::filesystem::path card_root(card_dir.path());
  REQUIRE(put_project(card_root, "SRC", p3_project()));
  platform::host::DirCard card(card_dir.path());
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card);
  REQUIRE(h->start());
  h->run_ms(300);
  tap(*h, app::Button::File);
  h->run_ms(60);
  tap(*h, app::Button::F1);  // LOAD asks
  tap(*h, app::Button::F1);  // confirmed: the picker lists the folders
  h->run_ms(300);
  tap(*h, app::Button::F1);  // OPEN the one folder
  h->run_ms(900);
  tap(*h, app::Button::F4);  // NEW asks
  tap(*h, app::Button::F1);  // confirmed
  h->run_ms(400);
  tap(*h, app::Button::F2);  // SAVE: the project has no name, so the name entry opens
  h->run_ms(20);
  tap(*h, app::Button::F7);
  h->run_ms(400);
  const std::filesystem::path out = card_root / "Apps" / "o3_voc03er" / "PROJECT" / "project.o3a";
  REQUIRE(std::filesystem::exists(out));
  Project got = {};
  REQUIRE(get_project(out, got));
  Project want = {};
  fl::default_stat(want);
  CHECK(got.stat == 1);
  CHECK(got.tempo_mbpm == want.tempo_mbpm);
  CHECK(got.pattern == 0);
  CHECK(got.octave == 0);
  CHECK(got.chord == 0);
  CHECK(got.keys == 0);
  CHECK(got.voicing == 3);
  CHECK(got.bend_row == 0);
  CHECK(got.bend_range == 2);
  CHECK(got.follow == 0);
  CHECK(got.preset_len == 0);
  CHECK(std::memcmp(got.layout, want.layout, sizeof got.layout) == 0);
  for (unsigned i = 0; i < o3_voc03er::route::kSlots; ++i) {
    CHECK(got.slot[i].gate == 0u);
    CHECK(got.slot[i].choke_off == 0u);
  }
  CHECK(h->runtime().frame_failures() == 0u);
}

// LE4: a project opened under a walking chain replaces the chain in place. The player used to keep its row (past the new chain's end: the chain then
// ended at the lap's end, the lit row out of range, and the new chain never played); now it starts over at the new chain's first row.
TEST_CASE("file: a LOAD under a walking chain starts the new chain at its first row") {
  platform::host::ScratchDir card_dir("o3_voc03er-file-chain");
  REQUIRE(card_dir.ok());
  platform::host::DirCard card(card_dir.path());
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card);
  REQUIRE(h->start());
  h->run_ms(300);
  tap(*h, app::Button::Song);
  h->run_ms(60);
  tap(*h, app::Button::Insert);  // the project's chain: one row, pattern D
  h->turn(3);
  h->run_ms(20);
  REQUIRE(state_num(*h, "chain") == 1u);
  tap(*h, app::Button::File);
  h->run_ms(40);
  tap(*h, app::Button::F2);  // SAVE AS: the name starts as PROJECT
  h->run_ms(20);
  tap(*h, app::Button::F7);
  h->run_ms(300);
  REQUIRE(std::filesystem::exists(std::filesystem::path(card_dir.path()) / "Apps" / "o3_voc03er" / "PROJECT" / "project.o3a"));
  tap(*h, app::Button::F8);

  tap(*h, app::Button::Song);  // a longer chain of A rows, played from its last row
  h->run_ms(60);
  h->turn(-3);
  tap(*h, app::Button::Insert);
  tap(*h, app::Button::Insert);
  tap(*h, app::Button::Insert);
  h->run_ms(20);
  REQUIRE(state_num(*h, "chain") == 4u);
  tap(*h, app::Button::Play);
  h->run_ms(300);
  REQUIRE(state_num(*h, "playing") == 1u);
  REQUIRE(state_num(*h, "chain_row") == 3u);
  REQUIRE(state_num(*h, "pat_playing") == 0u);

  tap(*h, app::Button::File);
  h->run_ms(40);
  tap(*h, app::Button::F1);  // LOAD asks
  tap(*h, app::Button::F1);  // confirmed: the picker lists the folders
  h->run_ms(300);
  tap(*h, app::Button::F1);  // OPEN, under the walking chain
  h->run_ms(300);
  REQUIRE(state_num(*h, "chain") == 1u);
  CHECK(state_num(*h, "chain_row") == 0u);  // not row 3 of a one-row chain
  bool d_played = false;
  for (unsigned t = 0; t < 200 && !d_played; ++t) {  // the lap in progress plays out, then the new chain's row 0 (pattern D)
    h->run_ms(20);
    d_played = state_num(*h, "playing") == 1u && state_num(*h, "pat_playing") == 3u;
  }
  CHECK(d_played);
  CHECK(h->runtime().frame_failures() == 0u);
}

// LE6: F8 gives a plain save or open up. The kit's save is atomic (the old file stays whole) and a load reads into a buffer the project is not decoded from until it
// ends, so a cancel changes nothing; the restore that follows is not cancellable (a half-applied project would be two projects mixed) and HELP says so.
TEST_CASE("file: LE6 F8 cancels a running save and a running open, and changes nothing") {
  platform::host::ScratchDir card_dir("o3_voc03er-file-cancel");
  REQUIRE(card_dir.ok());
  platform::host::DirCard card(card_dir.path());
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card);
  REQUIRE(h->start());
  h->run_ms(300);
  tap(*h, app::Button::F1);  // two taps of PERFORM's mode key: BANDS to TALK
  tap(*h, app::Button::F1);
  h->run_ms(700);
  REQUIRE(state(*h).find("voc_mode=talk") != std::string::npos);
  tap(*h, app::Button::File);
  h->run_ms(40);
  tap(*h, app::Button::F2);  // SAVE AS: the name starts as PROJECT
  h->run_ms(20);
  tap(*h, app::Button::F7);
  h->run_ms(300);
  const std::filesystem::path file = std::filesystem::path(card_dir.path()) / "Apps" / "o3_voc03er" / "PROJECT" / "project.o3a";
  REQUIRE(std::filesystem::exists(file));
  const auto slurp = [&] {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  const std::string before = slurp();
  REQUIRE_FALSE(before.empty());
  tap(*h, app::Button::F8);
  tap(*h, app::Button::F1);  // the live mode moves off TALK; the file still holds TALK
  h->run_ms(600);
  REQUIRE(state(*h).find("voc_mode=talk") == std::string::npos);

  // A SAVE over the project, given up at once: the file on the card is the one it was.
  tap(*h, app::Button::File);
  h->run_ms(40);
  h->press(app::Button::F2);  // SAVE starts the job, F8 (in the next event) gives it up
  h->press(app::Button::F8);
  h->run_ms(1);
  h->release(app::Button::F2);
  h->release(app::Button::F8);
  h->run_ms(600);
  CHECK(state(*h).find("file_note=CANCELLED") != std::string::npos);
  CHECK(slurp() == before);
  CHECK(state(*h).find("voc_mode=talk") == std::string::npos);

  // An OPEN given up: the project is not applied (the live mode stays off TALK), the screen is free again.
  tap(*h, app::Button::F1);  // LOAD asks
  tap(*h, app::Button::F1);  // confirmed: the picker lists the folders
  h->run_ms(300);
  h->press(app::Button::F1);  // OPEN starts the job, F8 gives it up
  h->press(app::Button::F8);
  h->run_ms(1);
  h->release(app::Button::F1);
  h->release(app::Button::F8);
  h->run_ms(1500);
  CHECK(state(*h).find("file_note=CANCELLED") != std::string::npos);
  CHECK(state(*h).find("voc_mode=talk") == std::string::npos);
  CHECK(h->runtime().frame_failures() == 0u);

  // Free again: the same open, not cancelled, applies the project (TALK is back).
  tap(*h, app::Button::F1);
  tap(*h, app::Button::F1);
  h->run_ms(300);
  tap(*h, app::Button::F1);
  h->run_ms(1500);
  CHECK(state(*h).find("voc_mode=talk") != std::string::npos);
  CHECK(h->runtime().frame_failures() == 0u);
}

TEST_CASE("file: LE6 HELP names F8 as CANCEL while a save or an open runs, and says the restore ends alone") {
  platform::host::ScratchDir card_dir("o3_voc03er-file-cancel-help");
  REQUIRE(card_dir.ok());
  platform::host::DirCard card(card_dir.path());
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card);
  REQUIRE(h->start());
  h->run_ms(300);
  tap(*h, app::Button::File);
  h->run_ms(40);
  tap(*h, app::Button::F2);
  h->run_ms(20);
  tap(*h, app::Button::F7);
  h->run_ms(300);
  const auto help_text = [&](bool* valid) {
    h->press(app::Button::Shift);
    h->run_ms(1);
    h->press(app::Button::Config);
    h->release(app::Button::Config);
    h->release(app::Button::Shift);
    h->run_ms(1);
    std::string out;
    if (h->runtime().help_open()) {
      const app::Help& help = h->runtime().help_overlay().help();
      app::HelpProblem p;
      *valid = app::help_check(help, &p);
      CHECK_MESSAGE(*valid, std::string(app::help_field_name(p.field)) << " " << std::string(p.what));
      out = std::string(help.fkey[7] != nullptr ? help.fkey[7] : "") + "|" + (help.summary != nullptr ? help.summary : "");
      tap(*h, app::Button::Config);
    }
    return out;
  };
  bool ok = false;
  const std::string idle = help_text(&ok);
  CHECK(idle.find("BACK|") == 0u);

  // The save in hand: HELP is asked while the job runs.
  h->press(app::Button::F2);
  h->run_ms(1);
  h->release(app::Button::F2);
  std::string busy = help_text(&ok);
  CHECK_MESSAGE(busy.find("CANCELS THE JOB|") == 0u, busy);
  h->run_ms(600);
}
