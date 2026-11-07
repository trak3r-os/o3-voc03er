// SPDX-License-Identifier: GPL-3.0-or-later
// SONG: the chain's editing and the playback rule (song_logic.h: repeats counted, the last entry, LOOP CHAIN on and off, the start at the cursor,
// an entry deleted or inserted while it plays, an empty chain, the lap counter's wrap), and one screen test on the engine (INSERT builds a chain,
// PLAY on SONG runs it and the transport stops at its end, an empty chain leaves PLAY as it is, HELP is valid).
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/sim/harness.h>

#include "../app/src/song_logic.h"

#include <string>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;
namespace sg = o3_voc03er::song;
using o3_voc03er::Chain;

namespace {

Chain make(std::initializer_list<unsigned> pats, std::initializer_list<unsigned> reps, unsigned loop) {
  Chain c;
  unsigned i = 0;
  auto r = reps.begin();
  for (const unsigned p : pats) {
    c.entry[i].pattern = static_cast<std::uint8_t>(p);
    c.entry[i].repeats = static_cast<std::uint8_t>(*r++);
    ++i;
  }
  c.count = static_cast<std::uint8_t>(i);
  c.loop = static_cast<std::uint8_t>(loop);
  return c;
}

// The laps the chain plays from `cursor`, as (entry, lap) pairs in order, until it ends or `cap` laps passed.
std::string walk(const Chain& c, unsigned cursor, unsigned cap) {
  sg::Player p;
  std::string out;
  if (!sg::start(c, cursor, p)) {
    return "none";
  }
  for (unsigned n = 0; n < cap; ++n) {
    out += static_cast<char>('0' + p.entry);
    out += static_cast<char>('0' + p.lap);
    out += ' ';
    if (!sg::advance(c, p)) {
      out += "end";
      return out;
    }
  }
  return out + "...";
}

}  // namespace

TEST_CASE("song: the repeats are counted and the chain ends after its last entry") {
  const Chain c = make({0, 1, 2}, {2, 1, 3}, 0);
  CHECK(walk(c, 0, 20) == "00 01 10 20 21 22 end");
}

TEST_CASE("song: LOOP CHAIN goes back to the first entry, off it ends") {
  Chain c = make({0, 1}, {1, 2}, 1);
  CHECK(walk(c, 0, 7) == "00 10 11 00 10 11 00 ...");
  c.loop = 0;
  CHECK(walk(c, 0, 7) == "00 10 11 end");
}

TEST_CASE("song: PLAY starts at the cursor's entry and a loop comes back to the first") {
  Chain c = make({0, 1, 2}, {1, 1, 1}, 1);
  CHECK(walk(c, 1, 5) == "10 20 00 10 20 ...");
  c.loop = 0;
  CHECK(walk(c, 2, 5) == "20 end");
  CHECK(walk(c, 9, 5) == "20 end");  // a cursor past the chain is the last row
}

TEST_CASE("song: an empty chain does not start, a one-entry one with one repeat plays one lap") {
  Chain c;
  sg::Player p;
  CHECK_FALSE(sg::start(c, 0, p));
  CHECK_FALSE(p.active);
  CHECK(walk(c, 0, 4) == "none");
  c = make({3}, {1}, 0);
  CHECK(walk(c, 0, 4) == "00 end");
  c.loop = 1;
  CHECK(walk(c, 0, 3) == "00 00 00 ...");
}

TEST_CASE("song: the repeats are read live, so lowering them takes effect at the next lap") {
  Chain c = make({0, 1}, {4, 1}, 0);
  sg::Player p;
  REQUIRE(sg::start(c, 0, p));
  REQUIRE(sg::advance(c, p));  // lap 1 of entry 0
  c.entry[0].repeats = 2;
  unsigned e = 9;
  unsigned l = 9;
  REQUIRE(sg::next_lap(c, p, e, l));
  CHECK(e == 1);  // the last lap of the entry was this one
  CHECK(l == 0);
}

TEST_CASE("song: deleting the playing entry lets its lap finish, then the entry that moved up plays") {
  Chain c = make({0, 1, 2}, {3, 1, 1}, 0);
  sg::Player p;
  REQUIRE(sg::start(c, 1, p));
  unsigned cur = 1;
  REQUIRE(sg::remove_at(c, 1, cur));
  sg::removed(p, 1);
  CHECK(c.count == 2);
  CHECK(c.entry[1].pattern == 2);
  CHECK(p.gone);
  unsigned e = 9;
  unsigned l = 9;
  REQUIRE(sg::next_lap(c, p, e, l));
  CHECK(e == 1);  // what moved up into its place
  CHECK(l == 0);
  REQUIRE(sg::advance(c, p));
  CHECK(p.entry == 1);
  CHECK(p.lap == 0);
  CHECK_FALSE(p.gone);
  CHECK_FALSE(sg::advance(c, p));  // the last entry ended the chain
  CHECK_FALSE(p.active);
}

TEST_CASE("song: deleting the last playing entry ends the chain after its lap, or loops") {
  Chain c = make({0, 1}, {1, 1}, 0);
  sg::Player p;
  REQUIRE(sg::start(c, 1, p));
  unsigned cur = 1;
  REQUIRE(sg::remove_at(c, 1, cur));
  sg::removed(p, 1);
  unsigned e = 9;
  unsigned l = 9;
  CHECK_FALSE(sg::next_lap(c, p, e, l));
  c.loop = 1;
  REQUIRE(sg::next_lap(c, p, e, l));
  CHECK(e == 0);
}

TEST_CASE("song: an entry inserted or removed before the playing one moves the player with it") {
  Chain c = make({0, 1, 2}, {1, 1, 1}, 0);
  sg::Player p;
  REQUIRE(sg::start(c, 1, p));
  unsigned at = 0;
  REQUIRE(sg::add_after(c, 0, 0, at));  // after row 0: row 1 is new, the playing one is now row 2
  CHECK(at == 1);
  sg::inserted(p, at);
  CHECK(p.entry == 2);
  CHECK(c.entry[p.entry].pattern == 1);
  unsigned cur = 0;
  REQUIRE(sg::remove_at(c, 0, cur));
  sg::removed(p, 0);
  CHECK(p.entry == 1);
  CHECK(c.entry[p.entry].pattern == 1);
  CHECK_FALSE(p.gone);
}

TEST_CASE("song: ADD takes the cursor's pattern after it, the chain stops at 16, DELETE shifts up and clears") {
  Chain c;
  unsigned at = 9;
  REQUIRE(sg::add_after(c, 0, 5, at));  // empty: the fallback (the pattern playing)
  CHECK(at == 0);
  CHECK(c.entry[0].pattern == 5);
  CHECK(c.entry[0].repeats == 1);
  sg::step_pattern(c, 0, -1);
  REQUIRE(sg::add_after(c, 0, 0, at));
  CHECK(at == 1);
  CHECK(c.entry[1].pattern == 4);
  while (c.count < o3_voc03er::kChainMax) {
    REQUIRE(sg::add_after(c, c.count - 1u, 0, at));
  }
  CHECK_FALSE(sg::add_after(c, 0, 0, at));
  CHECK(c.count == o3_voc03er::kChainMax);
  unsigned cur = o3_voc03er::kChainMax - 1u;
  REQUIRE(sg::remove_at(c, cur, cur));
  CHECK(c.count == o3_voc03er::kChainMax - 1u);
  CHECK(cur == c.count - 1u);
  CHECK(c.entry[o3_voc03er::kChainMax - 1u].repeats == 0);  // an unused entry reads all-zero
  CHECK_FALSE(sg::remove_at(c, 20, cur));
}

TEST_CASE("song: the encoder stops at the ends, the cursor too") {
  Chain c = make({0, 7}, {1, 64}, 0);
  sg::step_pattern(c, 0, -3);
  CHECK(c.entry[0].pattern == 0);
  sg::step_pattern(c, 1, 5);
  CHECK(c.entry[1].pattern == 7);
  sg::step_repeats(c, 0, -5);
  CHECK(c.entry[0].repeats == 1);
  sg::step_repeats(c, 1, 9);
  CHECK(c.entry[1].repeats == o3_voc03er::kChainRepeatMax);
  sg::step_repeats(c, 5, 1);  // a row that does not exist: nothing
  CHECK(sg::move_cursor(c, 0, -1) == 0);
  CHECK(sg::move_cursor(c, 0, 1) == 1);
  CHECK(sg::move_cursor(c, 1, 1) == 1);
  CHECK(sg::move_cursor(Chain{}, 3, 1) == 0);
}

TEST_CASE("song: the lap counter wraps and the sixteenth follows the tempo") {
  CHECK(sg::laps_between(250, 3) == 9);
  CHECK(sg::laps_between(7, 7) == 0);
  CHECK(sg::step_ms(120000) == 125);
  CHECK(sg::step_ms(0) == 125);
  CHECK(sg::step_ms(60000) == 250);
}

namespace {
void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(20);
}
}  // namespace

TEST_CASE("song: INSERT builds a chain, PLAY runs it and stops at its end, an empty chain leaves PLAY alone, HELP is valid") {
  sim::Harness h(::o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(100);
  tap(h, app::Button::Song);
  h.run_ms(60);

  tap(h, app::Button::Play);  // an empty chain: the transport's PLAY, as on every screen
  h.run_ms(100);
  CHECK(h.engine().playing());
  tap(h, app::Button::Play);
  h.run_ms(100);
  CHECK_FALSE(h.engine().playing());

  tap(h, app::Button::Insert);
  tap(h, app::Button::Insert);
  h.run_ms(60);
  tap(h, app::Button::Up);
  tap(h, app::Button::Play);  // from row 1 of 2 (the cursor's): one lap, then the end
  h.run_ms(300);
  CHECK(h.engine().playing());
  h.run_ms(5000);
  CHECK_FALSE(h.engine().playing());

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
