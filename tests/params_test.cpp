// SPDX-License-Identifier: GPL-3.0-or-later
// PARAMS: the pure logic (pages, focus, stepping, text, bars, the over-budget test) and the screen on the engine (a page opens, the encoder moves a
// value through the engine's parameter, VOICING is the app's own key, PARAMS / DOWN page it, HELP).
#include <doctest/doctest.h>

#include "tract_skip.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/midi.h>
#include <o3sdk/platform/host/fake_midi.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/sim/harness.h>

#include <o3_voc03er/units/tract/tract.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include "../app/src/params_logic.h"
#include "../app/src/perform_screen.h"

#include <cmath>
#include <cstring>
#include <string>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace pm = o3_voc03er::params;
using o3_voc03er::Vocoder;

TEST_CASE("params: four pages of eight, the live keys name a parameter the unit has") {
  CHECK(pm::kPages == 4u);
  CHECK(pm::kKeys == 8u);
  CHECK(std::strcmp(pm::key_def(0, 0).name, "SHAPE") == 0);
  CHECK(pm::key_def(0, 0).kind == pm::Kind::Value);
  CHECK(std::strcmp(pm::key_def(0, 2).name, "SUB") == 0);
  CHECK(pm::key_def(0, 2).kind == pm::Kind::Value);
  CHECK(pm::key_def(0, 2).param == Vocoder::Sub);
  CHECK(pm::key_def(0, 3).param == Vocoder::Tilt);
  CHECK(std::strcmp(pm::key_def(1, 4).name, "VOICING") == 0);
  CHECK(pm::key_def(1, 4).kind == pm::Kind::App);
  CHECK(pm::key_def(1, 4).fmt == pm::Fmt::Voicing);
  CHECK(std::strcmp(pm::key_def(1, 1).name, "QUALITY") == 0);
  CHECK(pm::key_def(1, 1).param == Vocoder::Quality);
  CHECK(pm::key_def(2, 4).param == Vocoder::NotesIn);
  CHECK(pm::key_def(1, 7).param == Vocoder::Thru);
  for (unsigned p = 0; p < pm::kPages; ++p) {
    for (unsigned k = 0; k < pm::kKeys; ++k) {
      const pm::KeyDef& d = pm::key_def(p, k);
      CHECK(pm::focusable(p, k) == (d.kind == pm::Kind::Value || d.kind == pm::Kind::App || d.kind == pm::Kind::Tract));
      if (d.kind == pm::Kind::Value) {
        CHECK(d.lo < d.hi);
        CHECK(d.step > 0);
        CHECK(d.param < Vocoder::kParamCount);
      }
    }
  }
  CHECK_FALSE(pm::focusable(0, 8));
}

namespace {
using o3_voc03er::kModeCount;
constexpr unsigned kVoiceCar = o3_voc03er::live::kCarrierVoice;

// The cells of a layout page as text: "SHAPE SPREAD ..." with "_" for a blank.
std::string page_text(const pm::Layout& l, unsigned page) {
  std::string out;
  for (unsigned k = 0; k < pm::kKeys; ++k) {
    const pm::Cell c = l.cell[page][k];
    out += k > 0u ? " " : "";
    out += pm::blank(c) ? "_" : pm::view_name(c);
  }
  return out;
}
}  // namespace

TEST_CASE("params: every mode shows only what it hears, in pages by meaning with fixed cells") {
  struct Want {
    unsigned mode;
    unsigned car;
    unsigned pages;
    const char* name[4];
    const char* text[4];
  };
  const char* const carrier = "SHAPE SPREAD SUB TILT NOISE ATTACK RELEASE GLIDE";
  const char* const mouth = "VOWEL TONGUE JAW LIPS T GLIDE SIZE RES HISS";
  const Want want[] = {
      {0, 0, 3, {"CARRIER", "VOICE", "NOTES"}, {carrier, "FORMANT THRU DRY LEVEL _ _ _ _", "KEY SCALE _ GATE ARP VOICING _ _"}},
      {0, kVoiceCar, 3, {"CARRIER", "VOICE", "NOTES"}, {carrier, "FORMANT THRU DRY LEVEL _ _ _ _", "KEY SCALE SPEED GATE ARP VOICING _ _"}},
      {1, 0, 3, {"CARRIER", "VOICE", "NOTES"}, {carrier, "FORMANT THRU DRY LEVEL QUALITY FREEZE _ _", "KEY SCALE _ GATE ARP VOICING _ _"}},
      {1, kVoiceCar, 3, {"CARRIER", "VOICE", "NOTES"}, {carrier, "FORMANT THRU DRY LEVEL QUALITY FREEZE _ _", "KEY SCALE SPEED GATE ARP VOICING _ _"}},
      {2, 0, 1, {"TUNE"}, {"KEY SCALE SPEED AMOUNT NOTES IN DRY LEVEL _"}},
      {2, kVoiceCar, 1, {"TUNE"}, {"KEY SCALE SPEED AMOUNT NOTES IN DRY LEVEL _"}},
      {3, 0, 3, {"CARRIER", "VOICE", "NOTES"}, {carrier, "FORMANT THRU DRY LEVEL _ _ _ _", "KEY SCALE _ GATE ARP VOICING _ _"}},
      {3, kVoiceCar, 3, {"CARRIER", "VOICE", "NOTES"}, {carrier, "FORMANT THRU DRY LEVEL _ _ _ _", "KEY SCALE SPEED GATE ARP VOICING _ _"}},
      {4, 0, 4, {"CARRIER", "MOUTH", "NOTES", "MIX"}, {carrier, mouth, "KEY SCALE _ GATE ARP VOICING _ _", "MOUTH DRY LEVEL _ _ _ _ _"}},
      {4, kVoiceCar, 4, {"CARRIER", "MOUTH", "NOTES", "MIX"}, {carrier, mouth, "KEY SCALE SPEED GATE ARP VOICING _ _", "MOUTH DRY LEVEL _ _ _ _ _"}},
  };
  for (const Want& w : want) {
    const pm::Layout& l = pm::layout_of(w.mode, w.car);
    INFO("mode " << w.mode << " carrier " << w.car);
    REQUIRE(l.pages == w.pages);
    for (unsigned p = 0; p < w.pages; ++p) {
      CHECK(page_text(l, p) == w.text[p]);
      CHECK(std::string(l.name[p]) == w.name[p]);
    }
    // an unheard value is never there: SPEED outside TUNE only with the VOICE carrier, MODE in none
    unsigned fp = 0, fk = 0;
    CHECK(pm::find_cell(l, pm::Cell{2, 2}, fp, fk) == (w.mode == 2u || w.car == kVoiceCar));
    CHECK_FALSE(pm::find_cell(l, pm::Cell{1, 0}, fp, fk));
  }
  // the layout of a mode out of range is held in range, a carrier other than VOICE is any carrier
  CHECK(&pm::layout_of(99, 0) == &pm::layout_of(0, 0));
  CHECK(&pm::layout_of(1, 3) == &pm::layout_of(1, 0));
}

TEST_CASE("params: a page never holds a single key, a tab is one word, KEY is F1 of NOTES, DRY comes before LEVEL, MODE is nowhere") {
  for (unsigned m = 0; m < kModeCount; ++m) {
    for (const unsigned car : {0u, kVoiceCar}) {
      const pm::Layout& l = pm::layout_of(m, car);
      INFO("mode " << m << " carrier " << car);
      CHECK(l.pages >= 1u);
      CHECK(l.pages <= pm::kMaxTabs);
      unsigned shown = 0;
      for (unsigned p = 0; p < l.pages; ++p) {
        unsigned in_page = 0;
        for (unsigned k = 0; k < pm::kKeys; ++k) {
          const pm::Cell c = l.cell[p][k];
          if (pm::blank(c)) {
            CHECK_FALSE(pm::focusable_at(l, p, k));
            continue;
          }
          ++in_page;
          const pm::KeyDef& d = pm::def_of(c);
          CHECK(pm::key_live(d, m, car));
          CHECK_FALSE((d.kind == pm::Kind::Value && d.param == Vocoder::Mode));
          CHECK(pm::focusable_at(l, p, k));
        }
        CHECK(in_page >= 2u);
        CHECK(std::strlen(l.name[p]) > 0u);
        CHECK(std::string(l.name[p]).find(' ') == std::string::npos);
        const std::string name = l.name[p];
        if (name == "NOTES" || name == "TUNE") {
          CHECK(pm::same(l.cell[p][0], pm::Cell{2, 0}));  // KEY
        }
        shown += in_page;
      }
      CHECK(shown == l.count);
      // the cells past the pages are blank for the stepping and the learn
      CHECK(pm::blank(pm::cell_at(l, l.pages, 0)));
      CHECK_FALSE(pm::focusable_at(l, 0, pm::kKeys));
    }
  }
  // the names of one mode's tabs are all different
  for (unsigned m = 0; m < kModeCount; ++m) {
    const pm::Layout& l = pm::layout_of(m, kVoiceCar);
    for (unsigned a = 0; a < l.pages; ++a) {
      for (unsigned b = a + 1u; b < l.pages; ++b) {
        CHECK(std::string(l.name[a]) != std::string(l.name[b]));
      }
    }
  }
}

TEST_CASE("params: the carrier moves no key: only SPEED's reserved cell fills in, and the same key sits in the same cell in every mode") {
  for (unsigned m = 0; m < kModeCount; ++m) {
    const pm::Layout& off = pm::layout_of(m, 0);
    const pm::Layout& on = pm::layout_of(m, kVoiceCar);
    INFO("mode " << m);
    REQUIRE(off.pages == on.pages);
    unsigned filled = 0;
    for (unsigned p = 0; p < off.pages; ++p) {
      for (unsigned k = 0; k < pm::kKeys; ++k) {
        if (pm::blank(off.cell[p][k])) {
          if (!pm::blank(on.cell[p][k])) {
            CHECK(pm::same(on.cell[p][k], pm::Cell{2, 2}));  // SPEED
            ++filled;
          }
        } else {
          CHECK(pm::same(off.cell[p][k], on.cell[p][k]));
        }
      }
    }
    CHECK(filled == (m == 2u ? 0u : 1u));
  }
  // a key found on a page of the same name in two modes is in the same cell
  for (unsigned a = 0; a < kModeCount; ++a) {
    for (unsigned b = 0; b < kModeCount; ++b) {
      const pm::Layout& la = pm::layout_of(a, kVoiceCar);
      const pm::Layout& lb = pm::layout_of(b, kVoiceCar);
      for (unsigned p = 0; p < la.pages; ++p) {
        for (unsigned q = 0; q < lb.pages; ++q) {
          if (std::string(la.name[p]) != lb.name[q]) {
            continue;
          }
          for (unsigned k = 0; k < pm::kKeys; ++k) {
            unsigned at = 0, cell = 0;
            if (!pm::blank(la.cell[p][k]) && pm::find_cell(lb, la.cell[p][k], at, cell) && at == q) {
              CHECK(cell == k);
            }
          }
        }
      }
    }
  }
}

TEST_CASE("params: a page change is round inside the mode's own pages, and a page opens on its first key") {
  const pm::Layout& bands = pm::layout_of(0, 0);
  CHECK(pm::next_page_in(bands, 0) == 1u);
  CHECK(pm::next_page_in(bands, 2) == 0u);
  CHECK(pm::next_page_in(bands, 0, -1) == 2u);
  CHECK(pm::next_page_in(bands, 1, -1) == 0u);
  const pm::Layout& tune = pm::layout_of(2, 0);
  CHECK(pm::next_page_in(tune, 0) == 0u);
  CHECK(pm::next_page_in(tune, 0, -1) == 0u);
  const pm::Layout& tract = pm::layout_of(4, kVoiceCar);
  CHECK(pm::next_page_in(tract, 3) == 0u);
  CHECK(pm::next_page_in(tract, 0, -1) == 3u);
  CHECK(pm::next_page_in(bands, 7) == 1u);  // a page past the layout (a stale one) starts from the first
  for (unsigned m = 0; m < kModeCount; ++m) {
    const pm::Layout& l = pm::layout_of(m, 0);
    for (unsigned p = 0; p < l.pages; ++p) {
      CHECK(pm::first_focus_at(l, p) == 0u);
    }
  }
  // a cell is found where the layout puts it; a table cell the mode does not show is not found
  unsigned page = 9, key = 9;
  CHECK(pm::find_cell(bands, pm::Cell{1, 6}, page, key));  // FORMANT
  CHECK(page == 1u);
  CHECK(key == 0u);
  CHECK_FALSE(pm::find_cell(bands, pm::Cell{1, 2}, page, key));  // FREEZE: TALK's
  CHECK_FALSE(pm::find_cell(bands, pm::kNoCell, page, key));
  CHECK(pm::find_cell(pm::layout_of(1, 0), pm::Cell{1, 2}, page, key));
  CHECK(page == 1u);
  CHECK(key == 5u);
  // NOTES opens on KEY, not on a blank: SPEED's cell is blank without the VOICE carrier, the cells after it are not
  CHECK(pm::first_focus_at(bands, 2) == 0u);
  CHECK_FALSE(pm::focusable_at(bands, 2, 2));
  CHECK(pm::focusable_at(bands, 2, 3));
}

TEST_CASE("params: the learn of a cell is the table's rule on its coordinate, wherever the layout puts it") {
  const pm::Layout& talk = pm::layout_of(1, 0);
  // TALK page 1: FORMANT THRU DRY LEVEL QUALITY FREEZE; page 2: KEY SCALE _ GATE ARP VOICING
  CHECK(pm::learnable_at(talk, 1, 0));
  CHECK_FALSE(pm::learnable_at(talk, 1, 4));  // QUALITY
  CHECK(pm::learnable_at(talk, 1, 5));        // FREEZE
  CHECK_FALSE(pm::learnable_at(talk, 2, 5));  // VOICING
  CHECK_FALSE(pm::learnable_at(talk, 2, 2));  // SPEED's reserved cell, blank
  CHECK_FALSE(pm::learnable_at(talk, 2, 7));  // a blank
  CHECK_FALSE(pm::learnable_at(talk, 3, 0));  // no such page
  const pm::Layout& tract = pm::layout_of(4, 0);
  CHECK(pm::learnable_at(tract, 1, 0));  // VOWEL
  CHECK(pm::learnable_at(tract, 3, 0));  // MOUTH
  for (unsigned m = 0; m < kModeCount; ++m) {
    const pm::Layout& l = pm::layout_of(m, kVoiceCar);
    for (unsigned p = 0; p < l.pages; ++p) {
      for (unsigned k = 0; k < pm::kKeys; ++k) {
        const pm::Cell c = l.cell[p][k];
        CHECK(pm::learnable_at(l, p, k) == (!pm::blank(c) && pm::learnable(c.page, c.key)));
      }
    }
  }
}

TEST_CASE("params: the TRACT page is the tract unit's eight keys, signed percents and a size by name, and the tabs fill the voiceprint's width") {
  using o3_voc03er::TractUnit;
  CHECK(pm::kTractPage == 3u);
  const char* const names[8] = {"VOWEL", "TONGUE", "JAW", "LIPS", "GLIDE", "SIZE", "RES", "HISS"};
  const std::uint16_t ids[8] = {TractUnit::Vowel, TractUnit::Tongue, TractUnit::Jaw, TractUnit::Lips,
                                TractUnit::Glide, TractUnit::Size,   TractUnit::Res, TractUnit::Hiss};
  for (unsigned k = 0; k < pm::kKeys; ++k) {
    const pm::KeyDef& d = pm::key_def(3, k);
    INFO(names[k]);
    CHECK(std::strcmp(d.name, names[k]) == 0);
    CHECK(d.kind == pm::Kind::Tract);
    CHECK(d.param == ids[k]);
    CHECK(d.param < TractUnit::kParamCount);
    CHECK(d.lo < d.hi);
    CHECK(d.step > 0);
    CHECK(pm::focusable(3, k));
    CHECK_FALSE(pm::key_live(d, 0u));  // the mouth speaks in TRACT only: no other mode shows it
    CHECK(pm::key_live(d, o3_voc03er::kModeTract));
  }
  char t[12];
  pm::format_value(pm::key_def(3, 1), -35, t);
  CHECK(std::string(t) == "-35%");
  pm::format_value(pm::key_def(3, 1), 20, t);
  CHECK(std::string(t) == "+20%");
  pm::format_value(pm::key_def(3, 1), 0, t);
  CHECK(std::string(t) == "0%");
  for (int d = 0; d < 5; ++d) {
    pm::format_value(pm::key_def(3, 5), d, t);
    CHECK(std::string(t) == pm::kSizeNames[d]);
    CHECK(pm::to_display(pm::Fmt::Size, static_cast<float>(d)) == d);
  }
  CHECK(pm::to_display(pm::Fmt::SPct, -0.35f) == -35);
  CHECK(pm::to_param(pm::Fmt::SPct, -35) == doctest::Approx(-0.35f));
  // a signed key steps through zero and stops at both ends; VOWEL goes in 1 % detents and 10 % coarse
  const pm::KeyDef& tongue = pm::key_def(3, 1);
  CHECK(pm::step_display(tongue, 0, -1, false) == -2);
  CHECK(pm::step_display(tongue, -98, -3, false) == -100);
  CHECK(pm::step_display(tongue, 95, 1, true) == 100);
  const pm::KeyDef& vowel = pm::key_def(3, 0);
  CHECK(pm::step_display(vowel, 50, 1, false) == 51);
  CHECK(pm::step_display(vowel, 50, -1, true) == 40);
  CHECK(pm::step_display(vowel, 100, 1, false) == 100);
  // the tabs: one to four, filling the voiceprint's width (x 26..774) with an 8 px gap, in order, none past it
  CHECK(pm::tab_w(4) == 181);
  for (unsigned n = 1; n <= pm::kMaxTabs; ++n) {
    for (unsigned i = 0; i < n; ++i) {
      CHECK(pm::tab_w(n) >= 181);
      CHECK(pm::tab_x(n, i) >= pm::kTabX0);
      CHECK(pm::tab_x(n, i) + pm::tab_w(n) <= pm::kTabX0 + pm::kTabsWidth);
      if (i > 0u) {
        CHECK(pm::tab_x(n, i) == pm::tab_x(n, i - 1u) + pm::tab_w(n) + pm::kTabGap);
      }
    }
    CHECK(pm::tab_x(n, n - 1u) + pm::tab_w(n) <= pm::kTabX0 + pm::kTabsWidth);
    CHECK(pm::kTabX0 + pm::kTabsWidth - (pm::tab_x(n, n - 1u) + pm::tab_w(n)) < static_cast<int>(n));  // at most a pixel a tab lost to the rounding
  }
}

TEST_CASE("params: VOICING is an app key, 0..3 by name, clamped at its ends") {
  const pm::KeyDef& v = pm::key_def(1, 4);
  CHECK(v.kind == pm::Kind::App);
  CHECK(pm::focusable(1, 4));
  CHECK(v.lo == 0);
  CHECK(v.hi == 3);
  CHECK(static_cast<int>(o3_voc03er::Voicing::Smooth) == v.hi);  // the default is the last: SMOOTH
  const char* const names[4] = {"CLOSE", "OPEN", "WIDE", "SMOOTH"};
  for (int d = v.lo; d <= v.hi; ++d) {
    char txt[12];
    pm::format_value(v, d, txt);
    CHECK(std::strcmp(txt, names[d]) == 0);
    CHECK(std::strcmp(o3_voc03er::perform::kVoicingNames[d], names[d]) == 0);
    CHECK(pm::bar_of(v, d) == -1);  // a word, no bar
  }
  CHECK(pm::step_display(v, 0, 1, false) == 1);
  CHECK(pm::step_display(v, 1, 1, false) == 2);
  CHECK(pm::step_display(v, 2, 1, false) == 3);
  CHECK(pm::step_display(v, 3, 1, false) == 3);    // no wrap at the top
  CHECK(pm::step_display(v, 0, -1, false) == 0);   // nor at the bottom
  CHECK(pm::step_display(v, 3, -2, false) == 1);
  CHECK(pm::step_display(v, 1, 9, true) == 3);     // SHIFT (coarse) is the same step
  CHECK(pm::step_display(v, 2, 0, false) == 2);
  CHECK(pm::step_display(v, 3, 1, false, true) == 0);  // a tap at the end goes round, as on the other keys
  // PerformScreen holds it: SMOOTH until set, and a value past the range stops at SMOOTH
  o3_voc03er::PerformScreen ps;
  CHECK(ps.voicing() == 3u);
  ps.set_voicing(1);
  CHECK(ps.voicing() == 1u);
  ps.set_voicing(9);
  CHECK(ps.voicing() == 3u);
}

TEST_CASE("params: display values and parameter values convert both ways") {
  CHECK(pm::to_display(pm::Fmt::Pct, 0.5f) == 50);
  CHECK(pm::to_display(pm::Fmt::Db, -6.0f) == -60);
  CHECK(pm::to_display(pm::Fmt::Db, -0.04f) == 0);
  CHECK(pm::to_display(pm::Fmt::Int, 24.0f) == 24);
  CHECK(pm::to_param(pm::Fmt::Pct, 25) == doctest::Approx(0.25f));
  CHECK(pm::to_param(pm::Fmt::Db, -60) == doctest::Approx(-6.0f));
  CHECK(pm::to_param(pm::Fmt::Int, 12) == doctest::Approx(12.0f));
  for (int d = 0; d <= 100; d += 7) {
    CHECK(pm::to_display(pm::Fmt::Pct, pm::to_param(pm::Fmt::Pct, d)) == d);
  }
}

TEST_CASE("params: stepping clamps the linear keys and wraps the round ones") {
  const pm::KeyDef& noise = pm::key_def(0, 4);
  CHECK(pm::step_display(noise, 50, 1, false) == 52);
  CHECK(pm::step_display(noise, 50, -2, false) == 46);
  CHECK(pm::step_display(noise, 50, 1, true) == 60);
  CHECK(pm::step_display(noise, 99, 1, false) == 100);
  CHECK(pm::step_display(noise, 100, 1, false) == 100);
  CHECK(pm::step_display(noise, 0, -1, false) == 0);
  CHECK(pm::step_display(noise, 100, 1, false, true) == 0);  // a tap at the end goes to the other end
  CHECK(pm::step_display(noise, 0, -1, false, true) == 100);
  CHECK(pm::step_display(noise, 40, 0, false) == 40);
  // LEVEL's tap stops at +6 dB: from the default -6 dB, taps go 0, +6, +6, never round to -48 dB (Fable's finding 11)
  const pm::KeyDef& tap_level = pm::key_def(0, 7);
  REQUIRE(tap_level.param == Vocoder::Level);
  CHECK(pm::step_display(tap_level, -60, 1, true, true) == 0);
  CHECK(pm::step_display(tap_level, 0, 1, true, true) == 60);
  CHECK(pm::step_display(tap_level, 60, 1, true, true) == 60);
  const pm::KeyDef& key = pm::key_def(2, 0);
  CHECK(pm::step_display(key, 11, 1, false) == 0);
  CHECK(pm::step_display(key, 0, -1, false) == 11);
  CHECK(pm::step_display(key, 5, 14, false) == 7);
  const pm::KeyDef& scale = pm::key_def(2, 1);
  CHECK(pm::step_display(scale, static_cast<int>(o3_voc03er::kScaleTableCount) - 1, 1, false) == 0);
  const pm::KeyDef& notes = pm::key_def(2, 4);
  CHECK(pm::step_display(notes, 0, 1, false) == 1);
  CHECK(pm::step_display(notes, 1, 1, false) == 0);

  const pm::KeyDef& quality = pm::key_def(1, 1);
  CHECK(pm::step_display(quality, 8, -1, false) == 8);
  CHECK(pm::step_display(quality, 20, 1, true) == 24);
  CHECK(pm::step_display(quality, 24, 1, false) == 24);
  const pm::KeyDef& mode = pm::key_def(1, 0);
  CHECK(pm::step_display(mode, 2, 1, false) == 3);  // ROBOT
  CHECK(pm::step_display(mode, 3, 1, false) == 4);  // TRACT, the last mode
  CHECK(pm::step_display(mode, 4, 1, false) == 4);

  const pm::KeyDef& level = pm::key_def(0, 7);
  CHECK(pm::step_display(level, 0, 1, false) == 5);
  CHECK(pm::step_display(level, 58, 1, true) == 60);
  CHECK(pm::step_display(level, -478, -1, true) == -480);

  const pm::KeyDef& speed = pm::key_def(2, 2);
  CHECK(pm::step_display(speed, 0, 1, false) == 5);
  CHECK(pm::step_display(speed, 490, 1, true) == 500);

  // the log keys step a tenth of the value (a third coarse), at least 1, and stay inside their ends
  const pm::KeyDef& attack = pm::key_def(0, 5);
  CHECK(pm::step_display(attack, 1, 1, false) == 2);
  CHECK(pm::step_display(attack, 100, 1, false) == 110);
  CHECK(pm::step_display(attack, 100, 1, true) == 133);
  CHECK(pm::step_display(attack, 500, 1, false) == 500);
  CHECK(pm::step_display(attack, 1, -1, false) == 1);
  int v = 1;
  for (int i = 0; i < 200; ++i) {
    v = pm::step_display(attack, v, 1, false);
  }
  CHECK(v == 500);
}

TEST_CASE("params: a key is live in the modes liveness.h says, every key of the pages") {
  REQUIRE(std::string(o3_voc03er::kModeNames[1]) == "TALK");
  for (unsigned m = 0; m < o3_voc03er::kModeCount; ++m) {
    for (unsigned page = 0; page < pm::kPages; ++page) {
      for (unsigned k = 0; k < pm::kKeys; ++k) {
        const pm::KeyDef& d = pm::key_def(page, k);
        if (d.kind == pm::Kind::None) {
          continue;
        }
        INFO(d.name);
        if (d.kind == pm::Kind::Tract) {
          CHECK(pm::key_live(d, m) == (m == o3_voc03er::kModeTract));  // the tract unit's values, MOUTH among them
        } else if (d.kind == pm::Kind::App) {
          CHECK(pm::key_live(d, m) == (m != 2u));  // VOICING: the chords go to the carrier, TUNE has none
        } else {
          CHECK(pm::key_live(d, m) == o3_voc03er::live::voice_heard(d.param, m));
        }
      }
    }
  }
  CHECK(pm::key_live(pm::key_def(1, 1), 1u));   // QUALITY: TALK
  CHECK_FALSE(pm::key_live(pm::key_def(1, 1), 0u));
  CHECK(pm::key_live(pm::key_def(1, 3), 4u));   // DRY: every mode, TRACT's mix too
  CHECK(pm::key_live(pm::key_def(0, 7), 2u));   // LEVEL: every mode
}

TEST_CASE("params: UX2 keys, FORMANT and THRU after MOUTH, GLIDE, GATE and ARP after NOTES IN, read and step as PERFORM's SHIFT row does") {
  const pm::KeyDef& formant = pm::key_def(1, 6);
  const pm::KeyDef& thru = pm::key_def(1, 7);
  const pm::KeyDef& glide = pm::key_def(2, 5);
  const pm::KeyDef& gate = pm::key_def(2, 6);
  const pm::KeyDef& arp = pm::key_def(2, 7);
  CHECK(std::string(formant.name) == "FORMANT");
  CHECK(std::string(thru.name) == "THRU");
  CHECK(std::string(glide.name) == "GLIDE");
  CHECK(std::string(gate.name) == "GATE");
  CHECK(std::string(arp.name) == "ARP");
  for (const pm::KeyDef* d : {&formant, &thru, &glide, &gate, &arp}) {
    CHECK(d->kind == pm::Kind::Value);
    CHECK(d->param < Vocoder::kParamCount);
  }
  CHECK(pm::learnable(1, 6));
  CHECK(pm::learnable(1, 7));
  CHECK(pm::learnable(2, 5));
  CHECK(pm::learnable(2, 6));
  CHECK(pm::learnable(2, 7));
  char t[12];
  pm::format_value(formant, -5, t);
  CHECK(std::string(t) == "-5 ST");
  pm::format_value(formant, 0, t);
  CHECK(std::string(t) == "0 ST");
  pm::format_value(formant, 12, t);
  CHECK(std::string(t) == "+12 ST");
  CHECK(pm::to_display(formant.fmt, -5.0f) == -5);
  CHECK(pm::to_param(formant.fmt, -5) == -5.0f);
  CHECK(pm::step_display(formant, 12, 1, false) == 12);  // clamps
  CHECK(pm::step_display(formant, 0, -1, true) == -4);   // coarse, as pl::formant_step
  CHECK(pm::step_display(thru, 0, 1, false) == 5);        // pl::thru_step
  CHECK(pm::step_display(thru, 0, 1, true) == 20);
  CHECK(pm::step_display(glide, 0, 1, false) == 10);      // pl::glide_step
  CHECK(pm::step_display(glide, 0, 1, true) == 50);
  CHECK(pm::step_display(glide, 500, 1, false) == 500);
  for (int d = 0; d < 5; ++d) {
    pm::format_value(gate, d, t);
    CHECK(std::string(t) == o3_voc03er::perform::kGateNames[d]);
    pm::format_value(arp, d, t);
    CHECK(std::string(t) == o3_voc03er::perform::kArpNames[d]);
  }
  CHECK(pm::step_display(gate, 4, 1, false) == 0);   // an enum wraps
  CHECK(pm::step_display(arp, 0, -1, false) == 4);
  CHECK(pm::bar_of(gate, 2) == -1);
  CHECK(pm::bar_of(formant, 0) == 50);
}

TEST_CASE("params: MOUTH (LT1) is TRACT's SOURCE on the second page: PADS or LIVE by name, a round key") {
  using o3_voc03er::TractUnit;
  const pm::KeyDef& k = pm::key_def(1, 5);
  CHECK(std::string(k.name) == "MOUTH");
  CHECK(k.kind == pm::Kind::Tract);
  CHECK(k.param == TractUnit::Source);
  CHECK(k.param < TractUnit::kParamCount);
  CHECK(k.fmt == pm::Fmt::Source);
  CHECK(pm::focusable(1, 5));
  char t[12];
  pm::format_value(k, 0, t);
  CHECK(std::string(t) == "PADS");
  pm::format_value(k, 1, t);
  CHECK(std::string(t) == "LIVE");
  CHECK(pm::to_display(k.fmt, 1.0f) == 1);
  CHECK(pm::to_param(k.fmt, 1) == 1.0f);
  CHECK(pm::step_display(k, 0, 1, false) == 1);
  CHECK(pm::step_display(k, 1, 1, false) == 0);   // round: a detent past LIVE is PADS
  CHECK(pm::step_display(k, 0, -1, true) == 1);
  CHECK(pm::bar_of(k, 1) == -1);                  // a word: no bar
  CHECK(pm::key_def(1, 6).param == Vocoder::Formant);  // UX2: the keys after MOUTH are the SHIFT row's FORMANT and THRU
}

TEST_CASE("params: values read as short text") {
  char t[12];
  pm::format_value(pm::key_def(0, 4), 50, t);
  CHECK(std::string(t) == "50%");
  pm::format_value(pm::key_def(0, 5), 120, t);
  CHECK(std::string(t) == "120MS");
  pm::format_value(pm::key_def(0, 7), -60, t);
  CHECK(std::string(t) == "-6.0");
  pm::format_value(pm::key_def(1, 0), 1, t);
  CHECK(std::string(t) == "TALK");
  pm::format_value(pm::key_def(1, 1), 16, t);
  CHECK(std::string(t) == "16");
  pm::format_value(pm::key_def(1, 2), 1, t);
  CHECK(std::string(t) == "ON");
  pm::format_value(pm::key_def(1, 2), 0, t);
  CHECK(std::string(t) == "OFF");
  pm::format_value(pm::key_def(2, 0), 3, t);
  CHECK(std::string(t) == "D#");
  pm::format_value(pm::key_def(2, 1), 2, t);
  CHECK(std::string(t) == "MINOR");
  for (unsigned p = 0; p < pm::kPages; ++p) {
    for (unsigned k = 0; k < pm::kKeys; ++k) {
      const pm::KeyDef& d = pm::key_def(p, k);
      if (d.kind == pm::Kind::Value) {
        pm::format_value(d, d.hi, t);
        CHECK(std::strlen(t) <= 11u);
        pm::format_value(d, d.lo, t);
        CHECK(std::strlen(t) <= 11u);
      }
    }
  }
}

TEST_CASE("params: a bar goes 0..100 for a number and is absent for a word") {
  CHECK(pm::bar_of(pm::key_def(0, 4), 0) == 0);
  CHECK(pm::bar_of(pm::key_def(0, 4), 50) == 50);
  CHECK(pm::bar_of(pm::key_def(0, 4), 100) == 100);
  CHECK(pm::bar_of(pm::key_def(1, 1), 16) == 50);
  CHECK(pm::bar_of(pm::key_def(1, 0), 1) == -1);
  CHECK(pm::bar_of(pm::key_def(2, 0), 4) == -1);
  CHECK(pm::bar_of(pm::key_def(1, 2), 1) == -1);
  const pm::KeyDef& attack = pm::key_def(0, 5);
  const int lo = pm::bar_of(attack, 1), mid = pm::bar_of(attack, 20), hi = pm::bar_of(attack, 500);
  CHECK(lo <= mid);
  CHECK(mid < hi);
  CHECK(hi == 100);
}

namespace {
void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(20);
}

// The vocoder's unit (the tract unit shares low parameter ids with it, so a reader that scans units can be fooled).
o3sdk::dsp::UnitId voice_unit(sim::Harness& h) {
  for (unsigned u = 0; u < 8; ++u) {
    const auto id = static_cast<o3sdk::dsp::UnitId>(u);
    const o3sdk::dsp::UnitInfo* info = h.engine().unit_info(id);
    if (info != nullptr && !o3_voc03er::test::is_tract_unit(h, u) && info->param_count == Vocoder::kParamCount) {
      return id;
    }
  }
  return o3sdk::dsp::kNoUnit;
}

float param(sim::Harness& h, std::uint16_t id) { return h.engine().param(voice_unit(h), id); }

// The mode (or any vocoder value) set straight on the unit, as a preset, a project load or a MIDI program change would: the screen has to follow.
void set_voice(sim::Harness& h, std::uint16_t id, float v) {
  (void)h.engine().set_param(voice_unit(h), id, v);
  h.run_ms(300);
}

// The tract unit's own parameter (the unit named TRACT; `param` skips it).
float tract_param(sim::Harness& h, std::uint16_t id) {
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(h, u)) {
      return h.engine().param(static_cast<o3sdk::dsp::UnitId>(u), id);
    }
  }
  return -1.0e9f;
}

bool has_row(const app::Help& help, app::Button b) {
  for (std::size_t i = 0; i < help.button_count; ++i) {
    if (help.buttons[i].button == b) {
      return help.buttons[i].text != nullptr;
    }
  }
  return false;
}
}  // namespace

TEST_CASE("params: the screen moves a value through the engine, VOICING takes the encoder, PARAMS and UP and DOWN page it, HELP is valid") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  tap(h, app::Button::Params);
  h.run_ms(60);
  REQUIRE(param(h, Vocoder::Mode) == doctest::Approx(0.0f));  // BANDS: CARRIER, VOICE, NOTES

  // CARRIER opens on SHAPE; NOISE is F5, and a detent is a step of 2 %
  const float noise0 = param(h, Vocoder::Noise);
  tap(h, app::Button::F5);
  h.turn(5);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(noise0 + 0.10f).epsilon(0.02));

  // SUB is live, a detent a step of 2 %; the encoder has left NOISE
  const float noise1 = param(h, Vocoder::Noise);
  tap(h, app::Button::F3);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(noise1).epsilon(0.02));
  CHECK(param(h, Vocoder::Sub) == doctest::Approx(0.02f).epsilon(0.02));

  // DOWN pages to VOICE, the encoder on FORMANT (a semitone a detent); there is no MODE key to turn
  tap(h, app::Button::Down);
  h.run_ms(60);
  const float mode0 = param(h, Vocoder::Mode);
  h.turn(2);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Formant) == doctest::Approx(2.0f));
  CHECK(param(h, Vocoder::Mode) == doctest::Approx(mode0));
  // PARAMS pages on to NOTES, KEY under the encoder as the page opens
  tap(h, app::Button::Params);
  h.run_ms(60);
  const float key0 = param(h, Vocoder::Key);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Key) == doctest::Approx(std::fmod(key0 + 1.0f, 12.0f)));
  // VOICING is the app's key (F6 here): a tap points the encoder at it, so a turn moves nothing in the unit (the value is PerformScreen's)
  tap(h, app::Button::F6);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Formant) == doctest::Approx(2.0f));
  CHECK(param(h, Vocoder::Key) == doctest::Approx(std::fmod(key0 + 1.0f, 12.0f)));
  CHECK(param(h, Vocoder::Mode) == doctest::Approx(mode0));
  // ARP is F5; SPEED's cell (F3) is blank without the VOICE carrier and takes no focus: the encoder stays on ARP
  tap(h, app::Button::F5);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Arp) == doctest::Approx(1.0f));
  tap(h, app::Button::F3);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Arp) == doctest::Approx(2.0f));
  // PARAMS goes round to CARRIER (SHAPE under the encoder), UP back to NOTES (KEY under the encoder)
  tap(h, app::Button::Params);
  h.run_ms(60);
  const float wave0 = param(h, Vocoder::Wave);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Wave) != doctest::Approx(wave0));
  tap(h, app::Button::Up);
  h.run_ms(60);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Key) == doctest::Approx(std::fmod(key0 + 2.0f, 12.0f)));
  CHECK(param(h, Vocoder::Arp) == doctest::Approx(2.0f));

  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Config);
  h.release(app::Button::Shift);
  h.run_ms(60);
  REQUIRE(h.runtime().help_open());
  const app::Help& help = h.runtime().help_overlay().help();
  app::HelpProblem p;
  const bool ok = app::help_check(help, &p);
  CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
  for (const app::Button b : {app::Button::Up, app::Button::Down, app::Button::Params, app::Button::Performance}) {
    INFO(std::string(app::button_name(b)));
    CHECK(has_row(help, b));
  }
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("params: TUNE has one page of its seven keys, and a tap on a blank key or a page key moves nothing") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  set_voice(h, Vocoder::Mode, 2.0f);
  tap(h, app::Button::Params);
  h.run_ms(60);
  const float key0 = param(h, Vocoder::Key);
  h.turn(1);  // KEY is F1 and under the encoder as the page opens
  h.run_ms(60);
  CHECK(param(h, Vocoder::Key) == doctest::Approx(std::fmod(key0 + 1.0f, 12.0f)));
  tap(h, app::Button::F8);  // blank
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Key) == doctest::Approx(std::fmod(key0 + 2.0f, 12.0f)));
  tap(h, app::Button::F5);  // NOTES IN: an on/off value, one detent flips it
  const float notes0 = param(h, Vocoder::NotesIn);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::NotesIn) == doctest::Approx(1.0f - notes0));
  // the only page: DOWN, UP and PARAMS leave it where it is
  for (const app::Button b : {app::Button::Down, app::Button::Up, app::Button::Params}) {
    tap(h, b);
    h.run_ms(60);
  }
  tap(h, app::Button::F6);  // DRY
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Dry) == doctest::Approx(0.02f).epsilon(0.05));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("params: the screen follows the mode and the carrier, whoever changes them: first page, first key, a valid focus") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  tap(h, app::Button::Params);
  h.run_ms(60);
  tap(h, app::Button::Down);  // BANDS, VOICE
  h.run_ms(60);
  tap(h, app::Button::Down);  // NOTES
  h.run_ms(60);
  tap(h, app::Button::F6);    // VOICING
  h.run_ms(60);
  // a mode set on the unit (a preset, a project, a program change): TALK opens on CARRIER's first key
  set_voice(h, Vocoder::Mode, 1.0f);
  const float wave0 = param(h, Vocoder::Wave);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Wave) != doctest::Approx(wave0));
  // TALK's VOICE page holds QUALITY (F5) and FREEZE (F6): a tap points the encoder at QUALITY
  tap(h, app::Button::Down);
  h.run_ms(60);
  tap(h, app::Button::F5);
  const float quality0 = param(h, Vocoder::Quality);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Quality) == doctest::Approx(std::fmin(quality0 + 1.0f, 24.0f)));
  // TRACT: first page again
  set_voice(h, Vocoder::Mode, 4.0f);
  const float shape1 = param(h, Vocoder::Wave);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Wave) != doctest::Approx(shape1));
  CHECK(param(h, Vocoder::Quality) == doctest::Approx(std::fmin(quality0 + 1.0f, 24.0f)));
  // the carrier VOICE fills SPEED's reserved cell, NOTES F3, and moves no other key
  set_voice(h, Vocoder::Mode, 0.0f);
  tap(h, app::Button::Down);
  h.run_ms(60);
  tap(h, app::Button::Down);  // NOTES
  h.run_ms(60);
  tap(h, app::Button::F6);    // VOICING
  h.run_ms(60);
  (void)h.engine().set_param(voice_unit(h), Vocoder::CarSrc, 8.0f);
  h.run_ms(300);
  tap(h, app::Button::F3);  // SPEED, on the page and in the cell reserved for it
  const float speed0 = param(h, Vocoder::Speed);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Speed) == doctest::Approx(speed0 + 5.0f));
  // and the carrier leaving blanks that cell: the page stays, the focus moves to the page's first key (KEY)
  (void)h.engine().set_param(voice_unit(h), Vocoder::CarSrc, 0.0f);
  h.run_ms(300);
  const float key1 = param(h, Vocoder::Key);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Key) == doctest::Approx(std::fmod(key1 + 1.0f, 12.0f)));
  CHECK(param(h, Vocoder::Speed) == doctest::Approx(speed0 + 5.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("params: PERFORM's MODE key changes the mode and PARAMS opens on the new mode's first page") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  tap(h, app::Button::Params);
  h.run_ms(60);
  tap(h, app::Button::Down);
  h.run_ms(60);
  tap(h, app::Button::Performance);
  h.run_ms(60);
  tap(h, app::Button::F1);  // MODE takes the focus; the encoder then goes to TRACT
  h.run_ms(20);
  h.turn(4);
  h.run_ms(300);
  REQUIRE(param(h, Vocoder::Mode) == doctest::Approx(4.0f));
  tap(h, app::Button::Params);
  h.run_ms(60);
  const float wave0 = param(h, Vocoder::Wave);
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Wave) != doctest::Approx(wave0));
  CHECK(param(h, Vocoder::Mode) == doctest::Approx(4.0f));
}

TEST_CASE("params: TRACT's MOUTH page is the tract unit's keys, written in TRACT only, and its HELP is valid") {
  using o3_voc03er::TractUnit;
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  REQUIRE(tract_param(h, TractUnit::Hiss) > -1.0e8f);  // the unit is in the engine
  set_voice(h, Vocoder::Mode, 4.0f);
  tap(h, app::Button::Params);
  h.run_ms(60);
  tap(h, app::Button::Down);  // CARRIER, one page on: MOUTH
  h.run_ms(60);
  const float mode0 = param(h, Vocoder::Mode);
  // VOWEL is F1, the page's first key and so under the encoder as the page opens: a detent 1 %
  h.turn(7);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.07f).epsilon(0.02));
  // JAW is signed: a detent 2 %, down through zero
  tap(h, app::Button::F3);
  h.turn(-3);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Jaw) == doctest::Approx(-0.06f).epsilon(0.02));
  // SIZE by name: BIG..TINY, written as 0..4
  tap(h, app::Button::F6);
  h.turn(1);
  h.run_ms(60);
  CHECK(tract_param(h, TractUnit::Size) == doctest::Approx(3.0f));
  // the vocoder is untouched, and so is the vowel while the encoder points elsewhere
  CHECK(param(h, Vocoder::Mode) == doctest::Approx(mode0));
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(0.07f).epsilon(0.02));

  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Config);
  h.release(app::Button::Shift);
  h.run_ms(60);
  REQUIRE(h.runtime().help_open());
  app::HelpProblem p;
  const bool ok = app::help_check(h.runtime().help_overlay().help(), &p);
  CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("header: a slot that speaks is named the same on every screen, not the live source") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  o3sdk::dsp::UnitId voice = o3sdk::dsp::kNoUnit;
  for (unsigned u = 0; u < 8 && voice == o3sdk::dsp::kNoUnit; ++u) {
    const auto id = static_cast<o3sdk::dsp::UnitId>(u);
    (void)h.engine().set_param(id, Vocoder::ModSrc, static_cast<float>(o3_voc03er::route::kFirstSlot));
    h.run_ms(20);
    voice = h.engine().param(id, Vocoder::ModSrc) == static_cast<float>(o3_voc03er::route::kFirstSlot) ? id : voice;
  }
  REQUIRE(voice != o3sdk::dsp::kNoUnit);
  o3_voc03er::ornoir::HeaderState hs;
  o3_voc03er::header_route(h.engine(), voice, "USB", true, hs);
  CHECK(std::strcmp(hs.speaks, "SLOT 1") == 0);
  (void)h.engine().set_param(voice, Vocoder::ModSrc, static_cast<float>(o3_voc03er::route::kSpeaksLive));
  h.run_ms(20);
  o3_voc03er::header_route(h.engine(), voice, "LINE", false, hs);
  CHECK(std::strcmp(hs.speaks, "LINE") == 0);
}

// ---- ML1: MIDI learn (SHIFT + F) ---------------------------------------------------------------------------------------------------

TEST_CASE("params: learnable is a live-safe unit parameter (Value, Tract); MODE, QUALITY, VOICING and a blank are not") {
  CHECK_FALSE(pm::learnable(1, 0));  // MODE: swaps the layout and resets (and no layout shows it)
  CHECK_FALSE(pm::learnable(1, 1));  // QUALITY: reallocates TALK's LPC order
  CHECK(pm::learnable(0, 7));        // LEVEL stays: pickup stops a jump
  CHECK(pm::learnable(0, 0));
  CHECK(pm::learnable(2, 4));  // NOTES IN
  CHECK(pm::learnable(2, 6));  // GATE (UX2)
  CHECK(pm::learnable(pm::kTractPage, 0));
  CHECK(pm::learnable(1, 5));  // MOUTH: the tract unit's SOURCE
  CHECK_FALSE(pm::learnable(1, 4));  // VOICING: the app's own setting
  CHECK(pm::learnable(1, 7));  // THRU (UX2)
  CHECK_FALSE(pm::learnable(1, 8));  // past the page
  CHECK_FALSE(pm::learnable(0, 8));
  for (unsigned p = 0; p < pm::kPages; ++p) {
    for (unsigned k = 0; k < pm::kKeys; ++k) {
      const pm::Kind kind = pm::key_def(p, k).kind;
      const pm::KeyDef& d = pm::key_def(p, k);
      CHECK(pm::learnable(p, k) == (kind == pm::Kind::Tract || (kind == pm::Kind::Value && d.param != Vocoder::Mode && d.param != Vocoder::Quality)));
      if (pm::learnable(p, k)) {
        CHECK(pm::focusable(p, k));
      }
    }
  }
  CHECK(pm::learn_tap(false, false) == pm::LearnTap::Cannot);
  CHECK(pm::learn_tap(false, true) == pm::LearnTap::Cannot);
  CHECK(pm::learn_tap(true, false) == pm::LearnTap::Arm);
  CHECK(pm::learn_tap(true, true) == pm::LearnTap::Cancel);
}

namespace {
void shift_tap(sim::Harness& h, app::Button b) {
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, b);
  h.release(app::Button::Shift);
  h.run_ms(20);
}
}  // namespace

TEST_CASE("params: SHIFT + F arms the kit's learn, the same keys cancel, a page or screen change cancels, VOICING does not take it") {
  o3sdk::platform::host::FakeMidi midi;
  sim::Harness h(::o3sdk_app(), nullptr, &midi);
  REQUIRE(h.start());
  h.run_ms(100);
  app::Midi& km = h.runtime().context().midi();
  tap(h, app::Button::Params);
  h.run_ms(60);
  const float noise0 = param(h, Vocoder::Noise);

  // SHIFT + NOISE (F5) arms; the focus and the value are untouched; the same keys again cancel
  shift_tap(h, app::Button::F5);
  CHECK(km.learning());
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(noise0));
  shift_tap(h, app::Button::F5);
  CHECK_FALSE(km.learning());

  // another key re-aims (still one arm); DOWN leaves the page and cancels
  shift_tap(h, app::Button::F1);
  shift_tap(h, app::Button::F2);
  CHECK(km.learning());
  tap(h, app::Button::Down);
  h.run_ms(40);
  CHECK_FALSE(km.learning());

  // BANDS' VOICE page: FORMANT THRU DRY LEVEL and four blanks: a blank arms nothing
  shift_tap(h, app::Button::F5);
  CHECK_FALSE(km.learning());
  // THRU (F2) arms like any value, and the same keys cancel
  shift_tap(h, app::Button::F2);
  CHECK(km.learning());
  shift_tap(h, app::Button::F2);
  CHECK_FALSE(km.learning());
  // NOTES: KEY SCALE _ GATE ARP VOICING. SPEED's reserved cell (F3) is blank without the VOICE carrier, and VOICING (F6) is the app's
  tap(h, app::Button::Params);
  h.run_ms(40);
  shift_tap(h, app::Button::F3);
  CHECK_FALSE(km.learning());
  shift_tap(h, app::Button::F6);
  CHECK_FALSE(km.learning());

  // leaving the screen cancels
  shift_tap(h, app::Button::F5);  // ARP
  CHECK(km.learning());
  tap(h, app::Button::Performance);
  h.run_ms(60);
  CHECK_FALSE(km.learning());
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("params: a learn armed on a value the new mode does not show is cancelled, and MODE and QUALITY take none") {
  o3sdk::platform::host::FakeMidi midi;
  sim::Harness h(::o3sdk_app(), nullptr, &midi);
  REQUIRE(h.start());
  h.run_ms(100);
  app::Midi& km = h.runtime().context().midi();
  tap(h, app::Button::Params);
  h.run_ms(60);
  shift_tap(h, app::Button::F5);  // NOISE
  REQUIRE(km.learning());
  set_voice(h, Vocoder::Mode, 2.0f);  // TUNE has no NOISE
  CHECK_FALSE(km.learning());

  // TALK, VOICE page: FORMANT THRU DRY LEVEL QUALITY FREEZE
  set_voice(h, Vocoder::Mode, 1.0f);
  tap(h, app::Button::Down);
  h.run_ms(40);
  shift_tap(h, app::Button::F5);  // QUALITY: not a live gesture
  CHECK_FALSE(km.learning());
  shift_tap(h, app::Button::F6);  // FREEZE
  REQUIRE(km.learning());
  shift_tap(h, app::Button::F5);  // QUALITY: refused, and the arm on FREEZE is cancelled
  CHECK_FALSE(km.learning());
  shift_tap(h, app::Button::F1);  // FORMANT, shown in BANDS too, but the mode change opens the first page: cancelled
  REQUIRE(km.learning());
  set_voice(h, Vocoder::Mode, 0.0f);
  CHECK_FALSE(km.learning());
  // the carrier VOICE fills SPEED's cell and moves no key: an arm on a key that stays on the page stays
  tap(h, app::Button::Down);
  h.run_ms(40);
  tap(h, app::Button::Down);  // NOTES
  h.run_ms(40);
  shift_tap(h, app::Button::F1);  // KEY
  REQUIRE(km.learning());
  (void)h.engine().set_param(voice_unit(h), Vocoder::CarSrc, 8.0f);
  h.run_ms(300);
  CHECK(km.learning());
  // SPEED arms in its cell, and the carrier leaving takes the cell away and the arm with it
  shift_tap(h, app::Button::F3);
  REQUIRE(km.learning());
  (void)h.engine().set_param(voice_unit(h), Vocoder::CarSrc, 0.0f);
  h.run_ms(300);
  CHECK_FALSE(km.learning());
  shift_tap(h, app::Button::F1);  // KEY, where it was
  CHECK(km.learning());
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("params: a controller binds the parameter, not the place: the strip says LEARNED, the value moves, TRACT's keys are the tract unit's") {
  using o3_voc03er::TractUnit;
  o3sdk::platform::host::FakeMidi midi;
  sim::Harness h(::o3sdk_app(), nullptr, &midi);
  REQUIRE(h.start());
  h.run_ms(100);
  app::Midi& km = h.runtime().context().midi();
  tap(h, app::Button::Params);
  h.run_ms(60);
  REQUIRE(km.bindings() == 0u);
  shift_tap(h, app::Button::F5);  // NOISE
  REQUIRE(km.learning());
  midi.in(o3sdk::hal::midi::Port::Usb, 0xB0, 21, 0);
  h.run_ms(60);
  CHECK_FALSE(km.learning());
  CHECK(km.bindings() == 1u);
  const int sweep[] = {0, 32, 64, 96, 127};
  for (const int v : sweep) {
    midi.in(o3sdk::hal::midi::Port::Usb, 0xB0, 21, static_cast<std::uint8_t>(v));
    h.run_ms(30);
  }
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(1.0f).epsilon(0.02));

  // TRACT: VOWEL is on the MOUTH page, and the binding is the tract unit's VOWEL
  set_voice(h, Vocoder::Mode, 4.0f);
  tap(h, app::Button::Down);  // CARRIER, one page on: MOUTH
  h.run_ms(40);
  shift_tap(h, app::Button::F1);  // VOWEL
  REQUIRE(km.learning());
  midi.in(o3sdk::hal::midi::Port::Usb, 0xB0, 22, 0);
  h.run_ms(60);
  CHECK(km.bindings() == 2u);
  for (const int v : sweep) {
    midi.in(o3sdk::hal::midi::Port::Usb, 0xB0, 22, static_cast<std::uint8_t>(v));
    h.run_ms(30);
  }
  CHECK(tract_param(h, TractUnit::Vowel) == doctest::Approx(1.0f).epsilon(0.02));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("params: FREEZE is on PARAMS in TALK only; a learned CC freezes in TALK and nothing outside it (UX2)") {
  o3sdk::platform::host::FakeMidi midi;
  sim::Harness h(::o3sdk_app(), nullptr, &midi);
  REQUIRE(h.start());
  h.run_ms(100);
  for (unsigned m = 0; m < kModeCount; ++m) {
    unsigned p = 0, k = 0;
    CHECK(pm::find_cell(pm::layout_of(m, 0), pm::Cell{1, 2}, p, k) == (m == 1u));
  }
  set_voice(h, Vocoder::Mode, 1.0f);
  tap(h, app::Button::Params);
  h.run_ms(60);
  tap(h, app::Button::Down);  // VOICE: FORMANT THRU DRY LEVEL QUALITY FREEZE
  h.run_ms(60);
  shift_tap(h, app::Button::F6);
  midi.in(o3sdk::hal::midi::Port::Usb, 0xB0, 23, 0);
  h.run_ms(60);
  for (const int v : {0, 127}) {
    midi.in(o3sdk::hal::midi::Port::Usb, 0xB0, 23, static_cast<std::uint8_t>(v));
    h.run_ms(60);
  }
  CHECK(param(h, Vocoder::Freeze) == doctest::Approx(1.0f));
  midi.in(o3sdk::hal::midi::Port::Usb, 0xB0, 23, 0);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Freeze) == doctest::Approx(0.0f));
  tap(h, app::Button::F6);  // the PARAMS key works in TALK
  h.turn(1);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Freeze) == doctest::Approx(1.0f));
  // leaving TALK ends the freeze, and the bound controller cannot start it in BANDS
  set_voice(h, Vocoder::Mode, 0.0f);
  CHECK(param(h, Vocoder::Freeze) == doctest::Approx(0.0f));
  for (const int v : {0, 127}) {
    midi.in(o3sdk::hal::midi::Port::Usb, 0xB0, 23, static_cast<std::uint8_t>(v));
    h.run_ms(60);
  }
  CHECK(param(h, Vocoder::Freeze) == doctest::Approx(0.0f));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("params: HELP names LEARN on the F keys that can take it, of the page the mode shows") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  set_voice(h, Vocoder::Mode, 1.0f);
  tap(h, app::Button::Params);
  h.run_ms(60);
  tap(h, app::Button::Down);  // TALK, VOICE: FORMANT THRU DRY LEVEL QUALITY FREEZE
  h.run_ms(40);
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Config);
  h.release(app::Button::Shift);
  h.run_ms(60);
  REQUIRE(h.runtime().help_open());
  const app::Help& help = h.runtime().help_overlay().help();
  CHECK(help.fkey_shift[0] != nullptr);  // FORMANT
  CHECK(help.fkey_shift[1] != nullptr);  // THRU
  CHECK(help.fkey_shift[2] != nullptr);  // DRY
  CHECK(help.fkey_shift[3] != nullptr);  // LEVEL
  CHECK(help.fkey_shift[4] == nullptr);  // QUALITY
  CHECK(help.fkey_shift[5] != nullptr);  // FREEZE
  CHECK(help.fkey_shift[6] == nullptr);  // blank
  CHECK(help.fkey_shift[7] == nullptr);  // blank
  app::HelpProblem p;
  const bool ok = app::help_check(help, &p);
  CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
}

// RD2: LEFT / RIGHT are one encoder step of the lit F key, on every page; UP / DOWN stay the page.
TEST_CASE("params: LEFT / RIGHT step the lit key as the encoder does, SHIFT passes as coarse, a held arrow repeats, a tap alone moves nothing") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  tap(h, app::Button::Params);
  h.run_ms(60);
  REQUIRE(param(h, Vocoder::Mode) == doctest::Approx(0.0f));
  const float noise0 = param(h, Vocoder::Noise);
  tap(h, app::Button::F5);  // lights NOISE and changes nothing
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(noise0));
  tap(h, app::Button::Right);
  h.run_ms(60);
  const float step = param(h, Vocoder::Noise) - noise0;
  CHECK(step > 0.0f);
  h.turn(1);  // the same step by the encoder
  h.run_ms(60);
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(noise0 + 2.0f * step));
  tap(h, app::Button::Left);
  tap(h, app::Button::Left);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(noise0));
  // SHIFT + arrow is SHIFT + encoder: the coarse step
  h.press(app::Button::Shift);
  h.run_ms(5);
  tap(h, app::Button::Right);
  h.release(app::Button::Shift);
  h.run_ms(60);
  const float coarse = param(h, Vocoder::Noise) - noise0;
  CHECK(coarse > step * 1.5f);
  h.press(app::Button::Shift);
  h.run_ms(5);
  tap(h, app::Button::Left);
  h.release(app::Button::Shift);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(noise0));
  // a held arrow repeats (the kit's auto-repeat arrives as presses) and the release stops it
  h.press(app::Button::Right);
  h.run_ms(1500);
  h.release(app::Button::Right);
  h.run_ms(20);
  const float held = param(h, Vocoder::Noise);
  CHECK(held - noise0 >= 4.0f * step);
  h.run_ms(1000);
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(held));
  // the arrows follow the focus: F3 SUB, and NOISE stays
  tap(h, app::Button::F3);
  tap(h, app::Button::Right);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Sub) == doctest::Approx(0.02f).epsilon(0.02));
  CHECK(param(h, Vocoder::Noise) == doctest::Approx(held));
  // UP / DOWN are still the page: the encoder points at the next page's first key
  tap(h, app::Button::Down);
  h.run_ms(60);
  const float formant0 = param(h, Vocoder::Formant);
  tap(h, app::Button::Right);
  h.run_ms(60);
  CHECK(param(h, Vocoder::Formant) == doctest::Approx(formant0 + 1.0f));
  CHECK(param(h, Vocoder::Sub) == doctest::Approx(0.02f).epsilon(0.02));
  // HELP names the arrows
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Config);
  h.release(app::Button::Shift);
  h.run_ms(60);
  REQUIRE(h.runtime().help_open());
  const app::Help& help = h.runtime().help_overlay().help();
  CHECK(has_row(help, app::Button::Left));
  CHECK(has_row(help, app::Button::Right));
  app::HelpProblem p;
  const bool ok = app::help_check(help, &p);
  CHECK_MESSAGE(ok, std::string(app::help_field_name(p.field)) << " " << static_cast<unsigned>(p.index) << ": " << std::string(p.what));
  CHECK(h.runtime().frame_failures() == 0u);
}
