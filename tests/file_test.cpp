// SPDX-License-Identifier: GPL-3.0-or-later
// FILE (U9c2): the pure part (app/src/file_logic.h) and one screen test on a card in a scratch directory: a damaged project is refused and changes nothing.
#include <doctest/doctest.h>

#include "../app/src/file_logic.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/sim/harness.h>

#include <filesystem>
#include <fstream>
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
