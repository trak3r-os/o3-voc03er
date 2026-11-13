// SPDX-License-Identifier: GPL-3.0-or-later
// The parameter strips of PLAYS (app/src/strip_logic.h, TP1): the pad-to-value maps, the closures, the stable ids, F7's list, the layout per mode, the
// parameters a strip must never take.
#include <doctest/doctest.h>

#include "../app/src/strip_logic.h"

#include <cstring>
#include <string>
#include <set>

using namespace o3_voc03er;
namespace st = o3_voc03er::strip;
namespace pd = o3_voc03er::pads;
using pd::Plays;
using pd::Row;

namespace {

const st::StripDef& by_name(const char* n) {
  for (const st::StripDef& d : st::kStrips) {
    if (std::strcmp(d.name, n) == 0) {
      return d;
    }
  }
  FAIL("no strip " << n);
  return st::kStrips[0];
}

}  // namespace

TEST_CASE("strip: a continuous strip runs from the minimum on pad 1 to the maximum on pad 12, rising") {
  for (const char* n : {"SHAPE", "SPREAD", "SUB", "NOISE", "DRY", "VOWEL", "HISS", "RES", "ACCENT", "ATTACK", "RELEASE", "T GLIDE", "THRU", "GLIDE", "SPEED", "AMOUNT"}) {
    const st::StripDef& d = by_name(n);
    INFO(n);
    CHECK(st::pad_count(d) == 12u);
    CHECK(st::pad_value(d, 0) == d.key.lo);
    CHECK(st::pad_value(d, 11) == d.key.hi);
    for (unsigned c = 1; c < 12; ++c) {
      CHECK(st::pad_value(d, c) > st::pad_value(d, c - 1));
    }
  }
  // a column past the strip is the last pad
  CHECK(st::pad_value(by_name("SHAPE"), 40) == 200);
}

TEST_CASE("strip: a log range is spaced by ratio, not by distance") {
  const st::StripDef& d = by_name("ATTACK");  // 1..500 ms
  CHECK(st::pad_value(d, 0) == 1);
  CHECK(st::pad_value(d, 11) == 500);
  CHECK(st::pad_value(d, 5) < 30);  // a linear map would give 228
}

TEST_CASE("strip: a bipolar range has the middle pair both 0, as BEND does") {
  for (const char* n : {"TILT", "TONGUE", "JAW", "LIPS"}) {
    const st::StripDef& d = by_name(n);
    INFO(n);
    CHECK(d.shape == st::Shape::Bipolar);
    CHECK(st::pad_value(d, 0) == d.key.lo);
    CHECK(st::pad_value(d, 11) == d.key.hi);
    CHECK(st::pad_value(d, 5) == 0);
    CHECK(st::pad_value(d, 6) == 0);
    for (unsigned c = 1; c < 12; ++c) {
      CHECK(st::pad_value(d, c) >= st::pad_value(d, c - 1));
    }
    CHECK(st::pad_value(d, 4) < 0);
    CHECK(st::pad_value(d, 7) > 0);
    // the lit pads of a value at 0 are both middle pads, of a value at the minimum pad 1 only
    CHECK(st::lit_mask(d, 0) == ((1u << 5) | (1u << 6)));
    CHECK(st::lit_mask(d, d.key.lo) == 1u);
    CHECK(st::lit_mask(d, d.key.hi) == (1u << 11));
  }
}

TEST_CASE("strip: at most twelve choices take one pad each and leave the rest dark") {
  const st::StripDef& gate = by_name("GATE");  // 0..4
  CHECK(st::pad_count(gate) == 5u);
  for (unsigned c = 0; c < 5; ++c) {
    CHECK(st::pad_value(gate, c) == static_cast<int>(c));
    CHECK(st::pad_used(gate, c));
  }
  for (unsigned c = 5; c < 12; ++c) {
    CHECK_FALSE(st::pad_used(gate, c));
    CHECK(st::pad_led(gate, c, 0) == o3sdk::app::kLedOff);
  }
  const st::StripDef& key = by_name("KEY");  // twelve
  CHECK(st::pad_count(key) == 12u);
  for (unsigned c = 0; c < 12; ++c) {
    CHECK(st::pad_value(key, c) == static_cast<int>(c));
  }
  const st::StripDef& onoff = by_name("FREEZE");
  CHECK(st::pad_count(onoff) == 2u);
  CHECK(st::pad_value(onoff, 1) == 1);
  // the lit pad of the current choice
  CHECK(st::lit_mask(gate, 3) == (1u << 3));
  CHECK(st::pad_led(gate, 3, st::lit_mask(gate, 3)) == o3sdk::app::kLedOn);
  CHECK(st::pad_led(gate, 2, st::lit_mask(gate, 3)) == o3sdk::app::kLedDim);
}

TEST_CASE("strip: more than twelve choices spread twelve pads over them, ends included") {
  const st::StripDef many = {"MANY", st::Shape::Enum, 0, st::kAllModes, params::KeyDef{"MANY", params::Kind::Value, 0, params::Fmt::Int, 0, 99, 1, 1}, 0};
  CHECK(st::pad_count(many) == 12u);
  CHECK(st::pad_value(many, 0) == 0);
  CHECK(st::pad_value(many, 11) == 99);
  for (unsigned c = 1; c < 12; ++c) {
    CHECK(st::pad_value(many, c) > st::pad_value(many, c - 1));
  }
}

TEST_CASE("strip: the three closures are held strips, named and numbered stably") {
  const st::StripDef& lips = by_name("CL LIPS");
  const st::StripDef& tip = by_name("CL TIP");
  const st::StripDef& back = by_name("CL BACK");
  CHECK(lips.shape == st::Shape::Hold);
  CHECK(lips.hold == 1);
  CHECK(tip.hold == 2);
  CHECK(back.hold == 3);
  CHECK(st::pad_count(tip) == 12u);
  CHECK(st::lit_mask(tip, 0) == 0u);  // a closure has no value to show: the screen lights the held pad
  char t[12];
  st::pad_text(tip, 3, t);
  CHECK(std::strcmp(t, "TIP") == 0);
  st::pad_text(back, 0, t);
  CHECK(std::strcmp(t, "BACK") == 0);
  CHECK(st::id_of(lips) == 0x0401);
  CHECK(st::id_of(tip) == 0x0402);
  CHECK(st::id_of(back) == 0x0403);
}

TEST_CASE("strip: an id is tag and parameter, unique, and never a list position") {
  std::set<unsigned> seen;
  for (const st::StripDef& d : st::kStrips) {
    const std::uint16_t id = st::id_of(d);
    CHECK(id != pd::kNoStrip);
    CHECK(seen.insert(id).second);
    CHECK(st::find(id) == &d);
  }
  CHECK(st::find(pd::kNoStrip) == nullptr);
  CHECK(st::find(0x7F7F) == nullptr);
  // the saved numbers of today (a file from this build must keep reading): tag 1 vocoder, 2 tract, 3 app, 4 closure
  CHECK(st::id_of(by_name("SHAPE")) == ((1u << 8) | static_cast<unsigned>(Vocoder::Wave)));
  CHECK(st::id_of(by_name("DRY")) == ((1u << 8) | static_cast<unsigned>(Vocoder::Dry)));
  CHECK(st::id_of(by_name("VOWEL")) == 0x0200u);
  CHECK(st::id_of(by_name("TONGUE")) == 0x0201u);
  CHECK(st::id_of(by_name("JAW")) == 0x0202u);
  CHECK(st::id_of(by_name("LIPS")) == 0x0203u);
  CHECK(st::id_of(by_name("T GLIDE")) == 0x0205u);
  CHECK(st::id_of(by_name("SIZE")) == 0x0206u);
  CHECK(st::id_of(by_name("RES")) == 0x0207u);
  CHECK(st::id_of(by_name("HISS")) == 0x0208u);
  CHECK(st::id_of(by_name("ACCENT")) == 0x020Bu);
  CHECK(st::id_of(by_name("SOURCE")) == 0x020Cu);
  CHECK(st::kIdVowel == 0x0200u);
  CHECK(st::kIdTongue == 0x0201u);
  CHECK(st::kIdTip == 0x0402u);
}

TEST_CASE("strip: the names fit the margin of the row") {
  for (const st::StripDef& d : st::kStrips) {
    CHECK(std::strlen(d.name) <= 8u);
  }
}

TEST_CASE("strip: every TRACT parameter but the ones left out has a strip, and the left-out ones are the three that the app writes") {
  const unsigned tract[] = {TractUnit::Vowel, TractUnit::Tongue, TractUnit::Jaw,    TractUnit::Lips,  TractUnit::Glide,  TractUnit::Size,
                            TractUnit::Res,   TractUnit::Hiss,   TractUnit::Accent, TractUnit::Source};
  for (const unsigned p : tract) {
    bool found = false;
    for (const st::StripDef& d : st::kStrips) {
      found = found || (d.key.kind == params::Kind::Tract && d.shape != st::Shape::Hold && d.key.param == p);
    }
    CHECK(found);
  }
  // CLOSE: only as the three held closures
  unsigned holds = 0;
  for (const st::StripDef& d : st::kStrips) {
    if (d.key.kind == params::Kind::Tract && d.key.param == TractUnit::Close) {
      CHECK(d.shape == st::Shape::Hold);
      ++holds;
    }
  }
  CHECK(holds == 3u);
}

TEST_CASE("strip: the excluded parameters have no strip, and no strip is a parameter that needs a reallocation or a rewrite of another control") {
  for (const st::StripDef& d : st::kStrips) {
    INFO(d.name);
    if (d.key.kind == params::Kind::Value) {
      for (const std::uint16_t x : st::kExcludedVoice) {
        CHECK(d.key.param != x);
      }
    }
    if (d.key.kind == params::Kind::Tract && d.shape != st::Shape::Hold) {
      for (const std::uint16_t x : st::kExcludedTract) {
        CHECK(d.key.param != x);
      }
    }
    // only a parameter the app can set live, never a placeholder
    CHECK(d.key.kind != params::Kind::None);
  }
  // the reasons, one by one: MODE swaps the layout, QUALITY reallocates the LPC, LEVEL is the VOL pads', SLIDE and MIX are written by the word line and the mode
  CHECK(st::find(st::kIdVowel) != nullptr);
  CHECK(st::find(pd::strip_id(st::kTagVoice, Vocoder::Mode)) == nullptr);
  CHECK(st::find(pd::strip_id(st::kTagVoice, Vocoder::Quality)) == nullptr);
  CHECK(st::find(pd::strip_id(st::kTagVoice, Vocoder::Level)) == nullptr);
  CHECK(st::find(pd::strip_id(st::kTagVoice, Vocoder::Formant)) == nullptr);
  CHECK(st::find(pd::strip_id(st::kTagTract, TractUnit::Mix)) == nullptr);
  CHECK(st::find(pd::strip_id(st::kTagTract, TractUnit::Slide)) == nullptr);
  CHECK(st::find(pd::strip_id(st::kTagTract, TractUnit::Close)) == nullptr);
}

TEST_CASE("strip: a strip's range is the range PARAMS shows for the same parameter") {
  unsigned matched = 0;
  for (const st::StripDef& d : st::kStrips) {
    if (d.shape == st::Shape::Hold) {
      continue;
    }
    for (unsigned p = 0; p < params::kPages; ++p) {
      for (unsigned k = 0; k < params::kKeys; ++k) {
        const params::KeyDef& e = params::key_def(p, k);
        if (e.kind == d.key.kind && e.param == d.key.param && e.kind != params::Kind::App) {
          INFO(d.name);
          CHECK(e.lo == d.key.lo);
          CHECK(e.hi == d.key.hi);
          CHECK(e.fmt == d.key.fmt);
          ++matched;
        }
      }
    }
  }
  CHECK(matched >= 15u);
}

TEST_CASE("strip: a strip is offered in the modes that use its parameter, so a dark one can say where it lives") {
  CHECK(st::in_mode(by_name("VOWEL"), kModeTract));
  CHECK_FALSE(st::in_mode(by_name("VOWEL"), 0));
  CHECK(st::in_mode(by_name("FREEZE"), 1));
  CHECK_FALSE(st::in_mode(by_name("FREEZE"), 0));
  CHECK(st::in_mode(by_name("SPEED"), 2));
  CHECK_FALSE(st::in_mode(by_name("SHAPE"), 2));  // TUNE has no carrier
  CHECK_FALSE(st::in_mode(by_name("THRU"), 2));
  CHECK_FALSE(st::in_mode(by_name("DRY"), kModeCount));
  char t[40];
  st::elsewhere_text(by_name("VOWEL"), 0, t);
  CHECK(std::strcmp(t, "VOWEL IN TRACT") == 0);
  st::elsewhere_text(by_name("SPEED"), 0, t);
  CHECK(std::strcmp(t, "SPEED IN TUNE") == 0);
  st::elsewhere_text(by_name("THRU"), 2, t);
  CHECK(std::strstr(t, "THRU NOT IN ") == t);
}

TEST_CASE("strip: F7's list is the row's notes and then the mode's parameters, by group") {
  for (unsigned m = 0; m < kModeCount; ++m) {
    unsigned used = 0;
    for (const st::StripDef& d : st::kStrips) {
      used += st::in_mode(d, m) ? 1u : 0u;
    }
    CHECK(st::entry_count(Row::A, m) == 6u + used);
    // row C: FORMANT only where the formant is heard, MOUTH only in TRACT
    const unsigned c_notes = 7u + (o3_voc03er::live::voice_heard(o3_voc03er::Vocoder::Formant, m) ? 1u : 0u) + (m == kModeTract ? 1u : 0u);
    CHECK(st::notes_count(Row::C, m) == c_notes);
    CHECK(st::entry_count(Row::C, m) == c_notes + used);
    // the notes first: the piano is gone, OFF is the last note entry of A and B
    CHECK(st::entry_at(Row::A, m, 0).plays == Plays::Note);
    CHECK(st::entry_at(Row::A, m, 5).plays == Plays::Off);
    CHECK(st::entry_at(Row::C, m, c_notes - 1u).plays == Plays::Off);
    CHECK(st::entry_at(Row::A, m, 6).plays == Plays::Strip);
    // every entry finds its own index back, and an applied entry reads back
    for (const Row r : {Row::A, Row::B, Row::C}) {
      for (unsigned i = 0; i < st::entry_count(r, m); ++i) {
        const st::Entry e = st::entry_at(r, m, i);
        CHECK(st::entry_index(r, m, e.plays, e.strip) == i);
        if (e.plays == Plays::Strip) {
          CHECK(st::find(e.strip) != nullptr);
          CHECK(st::in_mode(*st::find(e.strip), m));
        }
        pd::Layout l;
        st::apply_entry(l, r, m, i);
        CHECK(l.row[pd::idx(r)] == e.plays);
        CHECK(l.strip[pd::idx(r)] == e.strip);
      }
    }
  }
  // TRACT's list has the closures, BANDS' does not
  bool tip = false;
  for (unsigned i = 0; i < st::entry_count(Row::A, kModeTract); ++i) {
    tip = tip || st::entry_at(Row::A, kModeTract, i).strip == st::kIdTip;
  }
  CHECK(tip);
  for (unsigned i = 0; i < st::entry_count(Row::A, 0); ++i) {
    CHECK(st::entry_at(Row::A, 0, i).strip != st::kIdTip);
  }
}

TEST_CASE("strip: SHIFT + the encoder in F7's list jumps by group") {
  const unsigned n = st::entry_count(Row::A, 0);
  // the groups: the notes, CARRIER, SHAPE, TUNE, PLAY in BANDS
  const unsigned carrier = st::entry_jump(Row::A, 0, 0, 1);
  CHECK(carrier == 6u);
  CHECK(st::entry_at(Row::A, 0, carrier).group == 1u);
  const unsigned shape = st::entry_jump(Row::A, 0, carrier, 1);
  CHECK(st::entry_at(Row::A, 0, shape).group == 2u);
  CHECK(st::entry_jump(Row::A, 0, shape, -1) == carrier);       // back to the start of the group before
  CHECK(st::entry_jump(Row::A, 0, shape + 1u, -1) == shape);    // inside a group: its start
  CHECK(st::entry_jump(Row::A, 0, carrier, -1) == 0u);
  CHECK(st::entry_jump(Row::A, 0, 0, -1) == 0u);
  // the last group stays at the last entry
  CHECK(st::entry_jump(Row::A, 0, n - 1u, 1) == n - 1u);
  // every jump forward moves on or stays; every jump back moves back or stays
  for (unsigned i = 0; i < n; ++i) {
    CHECK(st::entry_jump(Row::A, 0, i, 1) >= i);
    CHECK(st::entry_jump(Row::A, 0, i, -1) <= i);
  }
  // TRACT's groups reach the closures
  const unsigned tn = st::entry_count(Row::C, kModeTract);
  unsigned i = 0;
  unsigned guard = 0;
  while (st::entry_at(Row::C, kModeTract, i).group != 4u && guard++ < 20u) {
    i = st::entry_jump(Row::C, kModeTract, i, 1);
  }
  CHECK(st::entry_at(Row::C, kModeTract, i).group == 4u);
  CHECK(i < tn);
}

TEST_CASE("strip: the default layouts, one per mode, every row doing something with the mode's own defaults") {
  for (unsigned m = 0; m < kModeCount; ++m) {
    const pd::Layout l = st::default_layout(m);
    CHECK(st::layout_valid(l));
    CHECK(l.row[0] == Plays::Note);  // every mode that sounds from notes has a note row (TUNE with NOTES IN on, TRACT with SOURCE PADS)
    for (unsigned r = 0; r < pd::kRows; ++r) {
      if (l.row[r] == Plays::Strip) {
        const st::StripDef* d = st::find(l.strip[r]);
        REQUIRE(d != nullptr);
        CHECK(st::in_mode(*d, m));  // a strip the mode uses, never a dark one
      }
    }
  }
  // BANDS: melody, chords, formant.
  pd::Layout l = st::default_layout(0);
  CHECK(l.row[1] == Plays::Triad);
  CHECK(l.row[2] == Plays::Formant);
  CHECK(l.strip[0] == pd::kNoStrip);
  // TALK: seventh chords.
  l = st::default_layout(st::kModeTalk);
  CHECK(l.row[1] == Plays::Seventh);
  CHECK(l.row[2] == Plays::Formant);
  // TUNE: the scale and the retune speed as strips (KEY is silent at the default CHROM scale).
  l = st::default_layout(st::kModeTune);
  CHECK(l.row[1] == Plays::Strip);
  CHECK(l.strip[1] == st::kIdScale);
  CHECK(l.row[2] == Plays::Strip);
  CHECK(l.strip[2] == st::kIdSpeed);
  CHECK(std::strcmp(st::row_name(l, Row::B), "SCALE") == 0);
  CHECK(std::strcmp(st::row_name(l, Row::C), "SPEED") == 0);
  // ROBOT: the carrier's shape between the notes and the formant.
  l = st::default_layout(st::kModeRobot);
  CHECK(l.row[1] == Plays::Strip);
  CHECK(l.strip[1] == st::kIdShape);
  CHECK(l.row[2] == Plays::Formant);
  CHECK(std::strcmp(st::row_name(l, Row::B), "SHAPE") == 0);
  // TRACT: the notes pitch the voice, then the tongue and the MOUTH strip.
  l = st::default_layout(kModeTract);
  CHECK(l.row[1] == Plays::Strip);
  CHECK(l.strip[1] == st::kIdTongue);
  CHECK(l.row[2] == Plays::Mouth);
  CHECK(std::strcmp(st::row_name(l, Row::B), "TONGUE") == 0);
  CHECK(std::strcmp(st::row_name(l, Row::C), "MOUTH") == 0);
}

TEST_CASE("strip: F7's value is what the row plays, and the hint gives the place in the list") {
  char out[16] = {};
  pd::Layout l = st::default_layout(kModeTract);
  CHECK(std::strcmp(st::row_name(l, Row::A), "NOTE") == 0);
  CHECK(std::strcmp(st::row_name(l, Row::B), "TONGUE") == 0);
  CHECK(std::strcmp(st::row_name(l, Row::C), "MOUTH") == 0);
  l = st::default_layout(st::kModeTalk);
  CHECK(std::strcmp(st::row_name(l, Row::B), "7TH") == 0);
  // Every entry of every list: its name fits F7's value cell (ten characters) and the hint is "NAME i/n".
  for (unsigned m = 0; m < kModeCount; ++m) {
    for (unsigned r = 0; r < pd::kRows; ++r) {
      const unsigned n = st::entry_count(static_cast<Row>(r), m);
      for (unsigned i = 0; i < n; ++i) {
        pd::Layout t = st::default_layout(m);
        st::apply_entry(t, static_cast<Row>(r), m, i);
        CHECK(std::strlen(st::row_name(t, static_cast<Row>(r))) <= 10u);
        st::plays_position(t, static_cast<Row>(r), i, n, out);
        const std::string want = std::string(st::row_name(t, static_cast<Row>(r))) + " " + std::to_string(i + 1u) + "/" + std::to_string(n);
        CHECK(want == out);
      }
    }
  }
  // The first entry of row A is NOTE: "NOTE 1/n".
  l = st::default_layout(0);
  st::plays_position(l, Row::A, 0, st::entry_count(Row::A, 0), out);
  CHECK(std::string(out).rfind("NOTE 1/", 0) == 0);
}

TEST_CASE("strip: a layout is valid when its rows are offered and its ids known, and sanitize drops an unknown id to the mode's default row") {
  pd::Layout l;
  CHECK(st::layout_valid(l));
  l.row[1] = Plays::Strip;
  l.strip[1] = 0x7F7F;
  CHECK_FALSE(st::layout_valid(l));
  const pd::Layout s = st::sanitize(l, kModeTract);
  CHECK(s.row[1] == Plays::Strip);
  CHECK(s.strip[1] == st::kIdTongue);
  CHECK(st::layout_valid(s));
  const pd::Layout b = st::sanitize(l, 0);
  CHECK(b.row[1] == Plays::Triad);
  CHECK(b.strip[1] == pd::kNoStrip);
  // a good strip row stays
  pd::Layout g;
  CHECK(pd::set_strip(g, Row::B, st::id_of(by_name("DRY"))));
  CHECK(st::sanitize(g, 0).row[1] == Plays::Strip);
  CHECK(st::sanitize(g, 0).strip[1] == st::id_of(by_name("DRY")));
  // a strip id on a note row, or FORMANT on row A, is damaged
  pd::Layout x;
  x.strip[2] = st::kIdVowel;
  CHECK_FALSE(st::layout_valid(x));
  pd::Layout y;
  y.row[0] = Plays::Formant;
  CHECK_FALSE(st::layout_valid(y));
  CHECK(st::sanitize(y, 0).row[0] == Plays::Note);
  // setting a strip drops no other row's value, and a plain value drops the strip
  pd::Layout z;
  CHECK(pd::set_strip(z, Row::A, st::kIdVowel));
  CHECK(z.row[1] == Plays::Triad);
  CHECK(pd::set_plays(z, Row::A, Plays::Open));
  CHECK(z.strip[0] == pd::kNoStrip);
}

TEST_CASE("strip: a row on a strip plays no notes, so the rows above it keep the octave they would have had") {
  pd::Layout l;
  l.row[0] = Plays::Note;
  l.row[1] = Plays::Triad;
  l.row[2] = Plays::Strip;
  l.strip[2] = st::kIdTip;
  CHECK_FALSE(pd::plays_notes(Plays::Strip));
  CHECK(pd::row_octave(l, Row::C) == 0);
  CHECK(pd::row_octave(l, Row::B) == 0);
  CHECK(pd::row_octave(l, Row::A) == 1);
}

TEST_CASE("strip: a pad's text is the value as PARAMS writes it, and the current value's text is bounded") {
  char t[12];
  st::pad_text(by_name("SHAPE"), 11, t);
  CHECK(std::strlen(t) > 0u);
  st::pad_text(by_name("GATE"), 20, t);  // a dark pad has no text
  CHECK(t[0] == '\0');
  st::pad_text(by_name("GATE"), 2, t);
  CHECK(std::strlen(t) > 0u);
  st::value_text(by_name("ARP"), 99, t);
  CHECK(std::strlen(t) < sizeof t);
  st::value_text(by_name("TILT"), -120, t);
  CHECK(std::strlen(t) > 0u);
}

// The PERFORM report of 2026-11-13 ("ROW PLAYS is broken"): F7's list offered values that do nothing in the mode (FORMANT in TUNE and TRACT, MOUTH outside
// TRACT: dark pads), and a layout an older image saved could hold a strip the mode no longer uses (GLIDE in TUNE), which the list cannot show (it read as
// entry 0, so a step went to TRIAD and a step down did nothing).
TEST_CASE("strip: every entry of F7's list acts in its mode, and a loaded value the list does not offer drops to the mode's default") {
  using o3_voc03er::Vocoder;
  namespace lv = o3_voc03er::live;
  for (unsigned m = 0; m < kModeCount; ++m) {
    for (const Row r : {Row::A, Row::B, Row::C}) {
      for (unsigned i = 0; i < st::entry_count(r, m); ++i) {
        const st::Entry e = st::entry_at(r, m, i);
        if (e.plays == Plays::Formant) {
          CHECK(lv::voice_heard(Vocoder::Formant, m));
        }
        if (e.plays == Plays::Mouth) {
          CHECK(m == kModeTract);
        }
        if (e.plays == Plays::Strip) {
          REQUIRE(st::find(e.strip) != nullptr);
          CHECK(st::in_mode(*st::find(e.strip), m));
        }
        CHECK(st::entry_offered(r, m, e.plays, e.strip));
      }
    }
    // the mode's default layout is all offered, so sanitize never drops a default to itself
    const pd::Layout d = st::default_layout(m);
    for (unsigned r = 0; r < pd::kRows; ++r) {
      CHECK(st::entry_offered(static_cast<Row>(r), m, d.row[r], d.strip[r]));
    }
  }
  CHECK_FALSE(st::plays_live(Row::C, Plays::Formant, st::kModeTune));
  CHECK_FALSE(st::plays_live(Row::C, Plays::Formant, kModeTract));
  CHECK(st::plays_live(Row::C, Plays::Formant, st::kModeRobot));
  CHECK_FALSE(st::plays_live(Row::C, Plays::Mouth, 0));
  CHECK(st::plays_live(Row::C, Plays::Mouth, kModeTract));
  CHECK(st::plays_live(Row::C, Plays::Bend, st::kModeTune));
  CHECK(st::plays_live(Row::A, Plays::Off, st::kModeTune));
  CHECK_FALSE(st::plays_live(Row::A, Plays::Formant, 0));

  // GLIDE on TUNE's row B (an older image's project): not offered, so the load drops it to TUNE's default B SCALE
  pd::Layout t = st::default_layout(st::kModeTune);
  REQUIRE(pd::set_strip(t, Row::B, st::id_of(by_name("GLIDE"))));
  CHECK(st::layout_valid(t));  // a known id: the file is not damaged
  CHECK(st::entry_find(Row::B, st::kModeTune, t.row[1], t.strip[1]) == st::entry_count(Row::B, st::kModeTune));
  const pd::Layout ts = st::sanitize(t, st::kModeTune);
  CHECK(ts.row[1] == Plays::Strip);
  CHECK(ts.strip[1] == st::kIdScale);
  // FORMANT on TUNE's row C, MOUTH on BANDS' row C: dropped to the default; FORMANT on BANDS' row C stays
  pd::Layout f = st::default_layout(st::kModeTune);
  REQUIRE(pd::set_plays(f, Row::C, Plays::Formant));
  CHECK(st::sanitize(f, st::kModeTune).strip[2] == st::kIdSpeed);
  pd::Layout b;
  REQUIRE(pd::set_plays(b, Row::C, Plays::Mouth));
  CHECK(st::sanitize(b, 0).row[2] == Plays::Formant);
  CHECK(st::sanitize(pd::Layout{}, 0).row[2] == Plays::Formant);
  // a row the list offers stays as it is
  pd::Layout k;
  REQUIRE(pd::set_plays(k, Row::C, Plays::Bend));
  CHECK(st::sanitize(k, st::kModeTune).row[2] == Plays::Bend);
}

TEST_CASE("strip: F7's list as shown: a heading before each group, every entry once and in order, and a window that keeps the cursor in view") {
  unsigned longest = 0;
  for (unsigned m = 0; m < kModeCount; ++m) {
    for (const Row r : {Row::A, Row::B, Row::C}) {
      st::ListRow rows[st::kListMax];
      const unsigned n = st::list_rows(r, m, rows);
      longest = n > longest ? n : longest;
      unsigned next = 0;
      unsigned groups = 0;
      for (unsigned i = 0; i < n; ++i) {
        if (rows[i].heading) {
          ++groups;
          REQUIRE(i + 1u < n);
          CHECK_FALSE(rows[i + 1u].heading);  // a heading is followed by its group's first entry
          CHECK(rows[i + 1u].group == rows[i].group);
          continue;
        }
        CHECK(rows[i].entry == next);
        CHECK(rows[i].group == st::entry_at(r, m, next).group);
        ++next;
      }
      CHECK(next == st::entry_count(r, m));
      CHECK(rows[0].heading);
      CHECK(rows[0].group == st::kGroupNotes);
      CHECK(groups <= st::kGroupCount);
    }
  }
  CHECK(longest < st::kListMax);  // room to spare: TRACT's row C is the longest
  // the window: fourteen rows of a long list, the cursor near the middle, never past either end
  CHECK(st::list_top(0, 10, 14) == 0u);
  CHECK(st::list_top(9, 10, 14) == 0u);
  CHECK(st::list_top(0, 40, 14) == 0u);
  CHECK(st::list_top(20, 40, 14) == 13u);
  CHECK(st::list_top(39, 40, 14) == 26u);
  for (unsigned pos = 0; pos < 40; ++pos) {
    const unsigned top = st::list_top(pos, 40, 14);
    CHECK(pos >= top);
    CHECK(pos < top + 14u);
  }
}
