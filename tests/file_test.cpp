// SPDX-License-Identifier: GPL-3.0-or-later
// FILE (U9c2): the pure part (app/src/file_logic.h) and one screen test on a card in a scratch directory: a damaged project is refused and changes nothing.
#include <doctest/doctest.h>

#include "../app/src/file_logic.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/sim/harness.h>

#include <filesystem>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string>

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
  char line[512];
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
