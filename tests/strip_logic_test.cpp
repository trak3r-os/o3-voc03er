// SPDX-License-Identifier: GPL-3.0-or-later
// The parameter strips of PLAYS (app/src/strip_logic.h, TP1): the pad-to-value maps, the closures, the stable ids, F7's list, the layout per mode, the
// parameters a strip must never take.
#include <doctest/doctest.h>

#include "../app/src/strip_logic.h"

#include <cstring>
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
    CHECK(d.key.kind != params::Kind::Later);
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
    CHECK(st::entry_count(Row::C, m) == 9u + used);
    // the notes first: the piano is gone, OFF is the last note entry of A and B
    CHECK(st::entry_at(Row::A, m, 0).plays == Plays::Note);
    CHECK(st::entry_at(Row::A, m, 5).plays == Plays::Off);
    CHECK(st::entry_at(Row::C, m, 8).plays == Plays::Off);
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

TEST_CASE("strip: a tap steps on and wraps, the encoder stops at the ends, SHIFT jumps by group") {
  const unsigned n = st::entry_count(Row::A, 0);
  CHECK(st::entry_tap(Row::A, 0, 0) == 1u);
  CHECK(st::entry_tap(Row::A, 0, n - 1u) == 0u);
  CHECK(st::entry_step(Row::A, 0, 0, -1) == 0u);
  CHECK(st::entry_step(Row::A, 0, n - 1u, 1) == n - 1u);
  CHECK(st::entry_step(Row::A, 0, 3, 2) == 5u);
  CHECK(st::entry_step(Row::A, 0, 3, -100) == 0u);
  CHECK(st::entry_step(Row::A, 0, 3, 100) == n - 1u);
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

TEST_CASE("strip: the default layout is A NOTE, B TRIAD, C FORMANT, and TRACT's is A VOWEL, B TONGUE, C TIP") {
  for (unsigned m = 0; m < kModeCount; ++m) {
    const pd::Layout l = st::default_layout(m);
    CHECK(st::layout_valid(l));
    if (m == kModeTract) {
      CHECK(l.row[0] == Plays::Strip);
      CHECK(l.strip[0] == st::kIdVowel);
      CHECK(l.row[1] == Plays::Strip);
      CHECK(l.strip[1] == st::kIdTongue);
      CHECK(l.row[2] == Plays::Strip);
      CHECK(l.strip[2] == st::kIdTip);
      CHECK(std::strcmp(st::row_name(l, Row::A), "VOWEL") == 0);
      CHECK(std::strcmp(st::row_name(l, Row::B), "TONGUE") == 0);
      CHECK(std::strcmp(st::row_name(l, Row::C), "CL TIP") == 0);
    } else {
      CHECK(l.row[0] == Plays::Note);
      CHECK(l.row[1] == Plays::Triad);
      CHECK(l.row[2] == Plays::Formant);
      CHECK(l.strip[0] == pd::kNoStrip);
    }
  }
}

TEST_CASE("strip: a layout is valid when its rows are offered and its ids known, and sanitize drops an unknown id to the mode's default row") {
  pd::Layout l;
  CHECK(st::layout_valid(l));
  l.row[0] = Plays::Strip;
  l.strip[0] = 0x7F7F;
  CHECK_FALSE(st::layout_valid(l));
  const pd::Layout s = st::sanitize(l, kModeTract);
  CHECK(s.row[0] == Plays::Strip);
  CHECK(s.strip[0] == st::kIdVowel);
  CHECK(st::layout_valid(s));
  const pd::Layout b = st::sanitize(l, 0);
  CHECK(b.row[0] == Plays::Note);
  CHECK(b.strip[0] == pd::kNoStrip);
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
