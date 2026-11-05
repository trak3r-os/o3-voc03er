// SPDX-License-Identifier: GPL-3.0-or-later
// The pure parts of the input side (app/include/o3_voc03er/input.h): starting gains, the source cycle, the peak meter that forgets
// the previous source after a switch, the console's `voc` words.
#include <doctest/doctest.h>

#include <o3_voc03er/input.h>

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
  CHECK(c.talk);
  CHECK(c.notes == 3u);
  CHECK(parse_voc("bands", nullptr, c));
  CHECK_FALSE(c.talk);
  CHECK(c.notes == 0u);
  CHECK(parse_voc("bands", "8", c));
  CHECK(c.notes == 8u);

  c = VocCommand{true, 5};
  CHECK_FALSE(parse_voc("bands", "9", c));
  CHECK_FALSE(parse_voc("bands", "x", c));
  CHECK_FALSE(parse_voc("bands", "", c));
  CHECK_FALSE(parse_voc("bands", "2x", c));
  CHECK_FALSE(parse_voc("vocode", "2", c));
  CHECK_FALSE(parse_voc(nullptr, nullptr, c));
  CHECK(c.talk);  // a refused line leaves the output alone
  CHECK(c.notes == 5u);
}

TEST_CASE("parse_loop reads a loop number 1..4 and an action") {
  LoopCommand c;
  CHECK(parse_loop("3", "rec", c));
  CHECK(c.loop == 2u);
  CHECK(c.act == LoopAct::Rec);
  CHECK(parse_loop("1", "car", c));
  CHECK(c.act == LoopAct::Car);
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
