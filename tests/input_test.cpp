// SPDX-License-Identifier: GPL-3.0-or-later
// The pure parts of the input side (app/include/o3_voc03er/input.h): starting gains, the source cycle, the peak meter that forgets
// the previous source after a switch, the console's `voc` words.
#include <doctest/doctest.h>

#include <o3_voc03er/input.h>

#include <initializer_list>

using namespace o3_voc03er;
using Source = o3sdk::hal::capture::Source;

TEST_CASE("input: line sources start at gain 0, the others at 50") {
  CHECK(default_gain(Source::LineMix) == 0u);
  CHECK(default_gain(Source::LineLeft) == 0u);
  CHECK(default_gain(Source::LineRight) == 0u);
  CHECK(default_gain(Source::MicLow) == 50u);
  CHECK(default_gain(Source::MicHigh) == 50u);
  CHECK(default_gain(Source::Radio) == 50u);
  CHECK(default_gain(Source::Usb) == 50u);
}

TEST_CASE("input: the source key visits every source once and comes back") {
  Source s = kSources[0].source;
  for (std::size_t i = 0; i < kSourceCount; ++i) {
    CHECK(s == kSources[i].source);
    s = next_source(s);
  }
  CHECK(s == kSources[0].source);
}

TEST_CASE("input: the meter holds a peak for 0.5 to 1 s and a reset forgets it") {
  InputMeter m;
  m.feed(0.9f, 0.2f, 100);
  CHECK(m.left() == doctest::Approx(0.9f));
  CHECK(m.peak() == doctest::Approx(0.9f));
  m.feed(0.1f, 0.1f, 700);  // the bucket turned: the old one is still the previous
  CHECK(m.left() == doctest::Approx(0.9f));
  m.feed(0.1f, 0.1f, 1300);  // two turns later it is gone
  CHECK(m.left() == doctest::Approx(0.1f));

  m.feed(1.0f, 1.0f, 1400);
  CHECK(m.clipping());
  m.reset(1500, InputMeter::kSelectHoldoffMs);
  CHECK(m.peak() == 0.0f);
  CHECK_FALSE(m.clipping());
  m.feed(1.0f, 1.0f, 1600);  // the previous source's tail inside the holdoff: dropped
  CHECK(m.peak() == 0.0f);
  m.feed(0.3f, 0.4f, 1500 + InputMeter::kSelectHoldoffMs);
  CHECK(m.left() == doctest::Approx(0.3f));
  CHECK(m.right() == doctest::Approx(0.4f));
}

TEST_CASE("input: voc takes a mode and 0..8 notes, nothing else") {
  VocCommand c;
  CHECK(parse_voc("talk", "3", c));
  CHECK(c.mode == 1u);
  CHECK(c.notes == 3u);
  CHECK(parse_voc("bands", nullptr, c));
  CHECK(c.mode == 0u);
  CHECK(c.notes == 0u);
  CHECK(parse_voc("tune", "2", c));
  CHECK(c.mode == 2u);
  CHECK(c.notes == 2u);
  CHECK(parse_voc("robot", "1", c));
  CHECK(c.mode == 3u);
  CHECK(c.notes == 1u);
  CHECK(parse_voc("bands", "8", c));
  CHECK(c.notes == 8u);

  c = VocCommand{1, 5};
  CHECK_FALSE(parse_voc("bands", "9", c));
  CHECK_FALSE(parse_voc("bands", "x", c));
  CHECK_FALSE(parse_voc("bands", "", c));
  CHECK_FALSE(parse_voc("bands", "2x", c));
  CHECK_FALSE(parse_voc("vocode", "2", c));
  CHECK_FALSE(parse_voc(nullptr, nullptr, c));
  CHECK(c.mode == 1u);  // a refused line leaves the output alone
  CHECK(c.notes == 5u);
}

TEST_CASE("parse_loop reads a loop number 1..4 and an action") {
  LoopCommand c;
  CHECK(parse_loop("3", "rec", c));
  CHECK(c.loop == 2u);
  CHECK(c.act == LoopAct::Rec);
  CHECK(parse_loop("1", "car", c));
  CHECK(c.act == LoopAct::Car);
  LoopCommand clear;  // its own, because the refusals below check that `c` keeps its value
  CHECK(parse_loop("4", "clear", clear));
  CHECK(clear.act == LoopAct::Clear);
  CHECK_FALSE(parse_loop("5", "play", c));
  CHECK_FALSE(parse_loop("0", "play", c));
  CHECK_FALSE(parse_loop("12", "play", c));
  CHECK_FALSE(parse_loop("2", "jump", c));
  CHECK_FALSE(parse_loop("2", nullptr, c));
  CHECK_FALSE(parse_loop(nullptr, "rec", c));
  CHECK(c.loop == 0u);  // a refused line leaves the output alone
  CHECK(c.act == LoopAct::Car);
}

TEST_CASE("parse_loop reads speed, direction and level") {
  LoopCommand c;
  CHECK(parse_loop("2", "half", c));
  CHECK(c.act == LoopAct::Half);
  CHECK(parse_loop("2", "x2", c));
  CHECK(c.act == LoopAct::X2);
  CHECK(parse_loop("4", "rev", c));
  CHECK(c.act == LoopAct::Rev);
  CHECK(c.loop == 3u);
  CHECK(parse_loop("1", "lvl", c, "-6"));
  CHECK(c.act == LoopAct::Level);
  CHECK(c.db == -6);
  CHECK(parse_loop("1", "lvl", c, "12"));
  CHECK(c.db == 12);
  CHECK_FALSE(parse_loop("1", "lvl", c, "13"));
  CHECK_FALSE(parse_loop("1", "lvl", c, "-25"));
  CHECK_FALSE(parse_loop("1", "lvl", c, "x"));
  CHECK_FALSE(parse_loop("1", "lvl", c));
  CHECK_FALSE(parse_loop("1", "rev", c, "3"));
}

TEST_CASE("voc formant and voc thru: the console's two BANDS values") {
  VocShape s;
  CHECK(parse_voc_shape("formant", "-12", s));
  CHECK_FALSE(s.thru);
  CHECK(s.value == -12);
  CHECK(parse_voc_shape("formant", "7", s));
  CHECK(s.value == 7);
  CHECK(parse_voc_shape("thru", "100", s));
  CHECK(s.thru);
  CHECK(s.value == 100);
  CHECK_FALSE(parse_voc_shape("formant", "13", s));
  CHECK_FALSE(parse_voc_shape("formant", "-13", s));
  CHECK_FALSE(parse_voc_shape("formant", "-", s));
  CHECK(parse_voc_shape("speaks", "6", s));
  CHECK(s.speaks);
  CHECK(s.value == 6);
  CHECK(parse_voc_shape("speaks", "0", s));
  CHECK_FALSE(parse_voc_shape("speaks", "7", s));
  CHECK_FALSE(parse_voc_shape("speaks", "-1", s));
  CHECK(parse_voc_shape("sounds", "7", s));
  CHECK(s.sounds);
  CHECK_FALSE(s.speaks);
  CHECK_FALSE(parse_voc_shape("sounds", "8", s));
  CHECK_FALSE(parse_voc_shape("thru", "-1", s));
  CHECK_FALSE(parse_voc_shape("thru", "101", s));
  CHECK_FALSE(parse_voc_shape("thru", "1x", s));
  CHECK(parse_voc_shape("glide", "500", s));
  CHECK((s.glide && !s.thru && s.value == 500));
  CHECK(parse_voc_shape("glide", "0", s));
  CHECK_FALSE(parse_voc_shape("glide", "501", s));
  CHECK_FALSE(parse_voc_shape("glide", "-1", s));
  CHECK(parse_voc_shape("gate", "4", s));
  CHECK((s.gate && s.value == 4));
  CHECK_FALSE(parse_voc_shape("gate", "5", s));
  CHECK(parse_voc_shape("arp", "3", s));
  CHECK((s.arp && s.value == 3));
  CHECK_FALSE(parse_voc_shape("arp", "5", s));
  CHECK_FALSE(parse_voc_shape("arp", "", s));
  CHECK_FALSE(parse_voc_shape("bands", "3", s));
  CHECK_FALSE(parse_voc_shape("thru", nullptr, s));
}

TEST_CASE("parse_pat reads a pattern letter, a step and one lane") {
  PatCommand c;
  auto parse = [&](std::initializer_list<const char*> words) {
    const char* w[8] = {};
    unsigned n = 0;
    for (const char* t : words) {
      w[n++] = t;
    }
    return parse_pat(w, n, c);
  };
  REQUIRE(parse({"C"}));
  CHECK(c.pattern == 2u);
  CHECK(c.act == PatAct::Show);
  REQUIRE(parse({"h"}));
  CHECK(c.pattern == 7u);
  REQUIRE(parse({"1"}));
  CHECK(c.pattern == 0u);
  REQUIRE(parse({"B", "len", "12"}));
  CHECK(c.act == PatAct::Length);
  CHECK(c.value == 12u);
  REQUIRE(parse({"A", "3", "note", "60"}));
  CHECK(c.act == PatAct::Note);
  CHECK(c.step == 2u);
  CHECK(c.value == 60u);
  REQUIRE(parse({"A", "16", "note", "rest"}));
  CHECK(c.step == 15u);
  CHECK(c.value == 0u);
  REQUIRE(parse({"D", "1", "chord", "48", "sev"}));
  CHECK(c.act == PatAct::Chord);
  CHECK(c.value == 48u);
  CHECK(c.kind == 1u);
  CHECK(c.voicing == 0u);
  REQUIRE(parse({"D", "2", "chord", "50", "pow", "smooth"}));
  CHECK(c.kind == 3u);
  CHECK(c.voicing == 3u);
  REQUIRE(parse({"A", "4", "car", "s3"}));
  CHECK(c.act == PatAct::Carrier);
  CHECK(c.value == 5u);
  REQUIRE(parse({"A", "4", "car", "follow"}));
  CHECK(c.value == 0u);
  REQUIRE(parse({"A", "5", "lvl", "0"}));
  CHECK(c.act == PatAct::Level);
  REQUIRE(parse({"A", "5", "lvl", "127"}));
  CHECK(c.value == 127u);
  REQUIRE(parse({"A", "3", "slc", "5"}));
  CHECK(c.act == PatAct::Slice);
  CHECK(c.step == 2u);
  CHECK(c.value == 5u);
  REQUIRE(parse({"A", "3", "slc", "0"}));
  CHECK(c.value == 0u);
  REQUIRE(parse({"A", "3", "slc", "24"}));
  CHECK(c.value == 24u);
  REQUIRE(parse({"A", "4", "mou", "0"}));
  CHECK(c.act == PatAct::Mouth);
  CHECK(c.step == 3u);
  CHECK(c.value == 0u);
  REQUIRE(parse({"B", "16", "mou", "12"}));
  CHECK(c.value == 12u);
  REQUIRE(parse({"A", "1", "mou", "129"}));  // A with GLD
  CHECK(c.value == 129u);
  REQUIRE(parse({"A", "1", "mou", "137"}));  // U with GLD
  CHECK(c.value == 137u);
  REQUIRE(parse({"A", "1", "mou", "13"}));  // WORD 0 (WOW)
  CHECK(c.value == 13u);
  REQUIRE(parse({"A", "1", "mou", "125"}));  // WORD 7 (GA): 13 + 7 * 16
  CHECK(c.value == 125u);
  REQUIRE(parse({"A", "1", "mou", "14"}));  // WOB rate 0 (1/4)
  REQUIRE(parse({"A", "1", "mou", "94"}));  // WOB rate 5 (1/16T): 14 + 5 * 16
  CHECK(c.value == 94u);
}

TEST_CASE("parse_pat refuses what is out of range, unknown or incomplete and leaves the command alone") {
  PatCommand c;
  c.step = 9;
  auto bad = [&](std::initializer_list<const char*> words) {
    const char* w[8] = {};
    unsigned n = 0;
    for (const char* t : words) {
      w[n++] = t;
    }
    return !parse_pat(w, n, c) && c.step == 9u;
  };
  CHECK(bad({}));
  CHECK(bad({"I"}));
  CHECK(bad({"9"}));
  CHECK(bad({"AB"}));
  CHECK(bad({"A", "0", "note", "60"}));
  CHECK(bad({"A", "17", "note", "60"}));
  CHECK(bad({"A", "1", "note", "128"}));
  CHECK(bad({"A", "1", "note", "0"}));  // a rest is spelled out
  CHECK(bad({"A", "1", "note"}));
  CHECK(bad({"A", "1", "note", "60", "x"}));
  CHECK(bad({"A", "1", "mou", "15"}));   // 15 is refused
  CHECK(bad({"A", "1", "mou", "127"}));  // WORD with bits 4..6 = 7 and the value 15
  CHECK(bad({"A", "1", "mou", "110"}));  // WOB rate 6 (14 + 6 * 16)
  CHECK(bad({"A", "1", "mou", "142"}));  // GLD on a WOB
  CHECK(bad({"A", "1", "mou", "141"}));  // GLD on a WORD
  CHECK(bad({"A", "1", "mou", "17"}));   // bits 4..6 set on a vowel
  CHECK(bad({"A", "1", "mou", "128"}));  // GLD on FOLLOW
  CHECK(bad({"A", "1", "mou", "138"}));  // GLD on a closure
  CHECK(bad({"A", "1", "mou", "256"}));
  CHECK(bad({"A", "1", "mou", "-1"}));
  CHECK(bad({"A", "1", "mou"}));
  CHECK(bad({"A", "17", "mou", "1"}));
  CHECK(bad({"A", "1", "chord", "60"}));
  CHECK(bad({"A", "1", "chord", "60", "maj"}));
  CHECK(bad({"A", "1", "chord", "60", "tri", "huge"}));
  CHECK(bad({"A", "1", "car", "live"}));  // LIVE is not a lane value
  CHECK(bad({"A", "1", "lvl", "128"}));
  CHECK(bad({"A", "1", "lvl", "-1"}));
  CHECK(bad({"A", "1", "slc", "25"}));
  CHECK(bad({"A", "1", "slc", "-1"}));
  CHECK(bad({"A", "1", "slc"}));
  CHECK(bad({"A", "len", "0"}));
  CHECK(bad({"A", "len", "17"}));
  CHECK(bad({"A", "len"}));
  CHECK(bad({"A", "1", "jump", "3"}));
}

TEST_CASE("parse_tract names every tract parameter, SOURCE last, and reads a number or a choice") {
  using o3_voc03er::parse_tract;
  using o3_voc03er::TractCommand;
  TractCommand c;
  REQUIRE(parse_tract("vowel", "0.5", c));
  CHECK(c.id == 0u);
  CHECK(c.value == doctest::Approx(0.5f));
  REQUIRE(parse_tract("close", "back", c));
  CHECK(c.id == 4u);
  CHECK(c.value == doctest::Approx(3.0f));
  REQUIRE(parse_tract("size", "tiny", c));
  CHECK(c.id == 6u);
  CHECK(c.value == doctest::Approx(4.0f));
  REQUIRE(parse_tract("slide", "120", c));
  CHECK(c.id == 10u);
  CHECK(c.value == doctest::Approx(120.0f));
  REQUIRE(parse_tract("accent", "0.25", c));  // id 11, the MOUTH line's (V3b); the app checks the unit's 0..1 range
  CHECK(c.id == 11u);
  CHECK(c.value == doctest::Approx(0.25f));
  REQUIRE(parse_tract("source", "live", c));  // id 12, the live voice (LT1); the app checks the unit's 0..1 range
  CHECK(c.id == 12u);
  CHECK(c.value == doctest::Approx(1.0f));
  REQUIRE(parse_tract("source", "pads", c));
  CHECK(c.value == doctest::Approx(0.0f));
  REQUIRE(parse_tract("source", "1", c));
  CHECK(c.value == doctest::Approx(1.0f));
  const TractCommand before = c;
  CHECK_FALSE(parse_tract("mouth", "1", c));  // the MOUTH line is the lane, not a parameter
  CHECK_FALSE(parse_tract("accent", "x", c));
  CHECK_FALSE(parse_tract("accent", "1e3", c));
  CHECK_FALSE(parse_tract("close", "nose", c));
  CHECK_FALSE(parse_tract(nullptr, "1", c));
  CHECK(c.id == before.id);
  CHECK(c.value == before.value);
}
