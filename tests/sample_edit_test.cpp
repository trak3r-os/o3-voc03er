// SPDX-License-Identifier: GPL-3.0-or-later
// SAMPLE EDIT (U7a): the pure logic of the cuts (sample_edit_logic.h), the screen on the engine (a take, then the keys reach the cut table and the
// pads audition it), the cuts reaching the step sequencer's map and the SLICE lane, and HELP. Synthetic signals only; no card.
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/dsp/engine.h>
#include <o3sdk/platform/host/fake_capture.h>
#include <o3sdk/sim/harness.h>

#include <o3_voc03er/stepseq.h>
#include <o3_voc03er/units/vocoder/loop.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include "../app/src/sample_edit_logic.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace app = o3sdk::app;
namespace dsp = o3sdk::dsp;
namespace sim = o3sdk::sim;
namespace sm = o3_voc03er::sample;
namespace se = o3_voc03er::sample_edit;
using o3_voc03er::Vocoder;

namespace {

sm::SliceTable even(std::uint32_t length, unsigned n = 8) {
  sm::SliceTable t;
  sm::even_cuts(t, length, n);
  return t;
}

// The cuts tile the slot: cut 1 at 0, each one starting where the one before ends, the lengths adding up to the length.
bool tiles(const sm::SliceTable& t) {
  if (t.length == 0u) {
    return t.count == 0u;
  }
  if (t.count == 0u || t.count > sm::kMaxSlices) {
    return false;
  }
  std::uint32_t end = 0;
  for (unsigned i = 0; i < t.count; ++i) {
    if (t.cut[i].start != end || t.cut[i].len == 0u) {
      return false;
    }
    end += t.cut[i].len;
  }
  return end == t.length;
}

std::string tt(std::uint32_t pos) {
  char b[12];
  se::time_text(pos, b);
  return b;
}

}  // namespace

TEST_CASE("sample edit: cut_at, prev_cut and next_cut") {
  const sm::SliceTable t = even(8000);  // starts 0, 1000, ...
  CHECK(se::cut_at(t, 0) == 0);
  CHECK(se::cut_at(t, 999) == 0);
  CHECK(se::cut_at(t, 1000) == 1);
  CHECK(se::cut_at(t, 7999) == 7);
  CHECK(se::cut_at(t, 8000) == -1);
  CHECK(se::prev_cut(t, 0) == 0u);
  CHECK(se::prev_cut(t, 1000) == 0u);  // strictly before
  CHECK(se::prev_cut(t, 1001) == 1000u);
  CHECK(se::prev_cut(t, 7999) == 7000u);
  CHECK(se::next_cut(t, 0) == 1000u);
  CHECK(se::next_cut(t, 999) == 1000u);
  CHECK(se::next_cut(t, 7000) == 7000u);  // none after the last start: stay
  CHECK(se::next_cut(t, 7500) == 7500u);
  const sm::SliceTable none;
  CHECK(se::cut_at(none, 0) == -1);
  CHECK(se::prev_cut(none, 5) == 0u);
  CHECK(se::next_cut(none, 5) == 5u);
}

TEST_CASE("sample edit: insert_cut splits the cut that holds the cursor") {
  sm::SliceTable t = even(8000, 2);  // [0,4000) [4000,4000)
  se::Edit r = se::insert_cut(t, 1500);
  CHECK(r.ok);
  CHECK(r.why == nullptr);
  CHECK(r.pos == 1500u);
  REQUIRE(t.count == 3u);
  CHECK(t.cut[0].start == 0u);
  CHECK(t.cut[0].len == 1500u);
  CHECK(t.cut[1].start == 1500u);
  CHECK(t.cut[1].len == 2500u);
  CHECK(t.cut[2].start == 4000u);
  CHECK(tiles(t));
  r = se::insert_cut(t, 7000);  // in the last cut
  CHECK(r.ok);
  CHECK(t.count == 4u);
  CHECK(t.cut[3].start == 7000u);
  CHECK(t.cut[3].len == 1000u);
  CHECK(tiles(t));
}

TEST_CASE("sample edit: insert_cut refuses and says why, and changes nothing") {
  sm::SliceTable t = even(8000, 2);
  const sm::SliceTable before = t;
  const auto same = [&] { return std::memcmp(&t, &before, sizeof t) == 0; };
  se::Edit r = se::insert_cut(t, 4000);  // on a cut
  CHECK_FALSE(r.ok);
  CHECK(std::strcmp(r.why, "TOO CLOSE") == 0);
  CHECK(same());
  CHECK(std::strcmp(se::insert_cut(t, 4000 + 63).why, "TOO CLOSE") == 0);  // 63 from a start
  CHECK(std::strcmp(se::insert_cut(t, 4000 - 63).why, "TOO CLOSE") == 0);  // 63 from the end of the cut before
  CHECK(std::strcmp(se::insert_cut(t, 7999).why, "TOO CLOSE") == 0);       // from the slot's end
  CHECK(std::strcmp(se::insert_cut(t, 8000 - 63).why, "TOO CLOSE") == 0);
  CHECK(std::strcmp(se::insert_cut(t, 8000).why, "TOO CLOSE") == 0);  // outside
  CHECK(std::strcmp(se::insert_cut(t, 99999).why, "TOO CLOSE") == 0);
  CHECK(same());
  CHECK(se::insert_cut(t, 4000 + 64).ok);  // 64 is enough
  t = before;
  CHECK(se::insert_cut(t, 4000 - 64).ok);
  t = before;
  CHECK(se::insert_cut(t, 8000 - 64).ok);
  sm::SliceTable none;  // an empty slot
  CHECK_FALSE(se::insert_cut(none, 0).ok);
  CHECK(none.count == 0u);
  sm::SliceTable full = even(24000, 24);
  const sm::SliceTable fb = full;
  r = se::insert_cut(full, 500);
  CHECK_FALSE(r.ok);
  CHECK(std::strcmp(r.why, "NO MORE CUTS") == 0);  // checked first
  CHECK(std::memcmp(&full, &fb, sizeof full) == 0);
  CHECK(std::strcmp(se::insert_cut(full, 1000).why, "NO MORE CUTS") == 0);
}

TEST_CASE("sample edit: delete_cut merges into the cut before, refuses the first cut and an empty spot") {
  sm::SliceTable t = even(8000, 4);  // 0, 2000, 4000, 6000
  se::Edit r = se::delete_cut(t, 2000);
  CHECK(r.ok);
  CHECK(r.pos == 2000u);
  REQUIRE(t.count == 3u);
  CHECK(t.cut[0].len == 4000u);
  CHECK(t.cut[1].start == 4000u);
  CHECK(tiles(t));
  r = se::delete_cut(t, 6000);  // the last
  CHECK(r.ok);
  CHECK(t.count == 2u);
  CHECK(t.cut[1].len == 4000u);
  CHECK(tiles(t));
  const sm::SliceTable before = t;
  r = se::delete_cut(t, 0);
  CHECK_FALSE(r.ok);
  CHECK(std::strcmp(r.why, "THE FIRST CUT STAYS") == 0);
  r = se::delete_cut(t, 4001);  // the cursor is inside a cut, not on its start
  CHECK_FALSE(r.ok);
  CHECK(std::strcmp(r.why, "NO CUT HERE") == 0);
  CHECK(std::strcmp(se::delete_cut(t, 5).why, "NO CUT HERE") == 0);
  CHECK(std::strcmp(se::delete_cut(t, 8000).why, "NO CUT HERE") == 0);
  CHECK(std::memcmp(&t, &before, sizeof t) == 0);
  sm::SliceTable none;
  CHECK(std::strcmp(se::delete_cut(none, 0).why, "THE FIRST CUT STAYS") == 0);
}

TEST_CASE("sample edit: clear_cuts leaves one cut over the slot") {
  sm::SliceTable t = even(8000, 16);
  se::clear_cuts(t);
  CHECK(t.count == 1u);
  CHECK(t.cut[0].start == 0u);
  CHECK(t.cut[0].len == 8000u);
  CHECK(tiles(t));
  sm::SliceTable e;
  e.count = 3;
  se::clear_cuts(e);  // an empty slot has none
  CHECK(e.count == 0u);
}

TEST_CASE("sample edit: the cuts tile the slot through a fixed sequence of inserts and deletes") {
  sm::SliceTable t = even(100000, 3);
  std::uint32_t x = 12345u;
  unsigned oks = 0, refused = 0;
  for (unsigned i = 0; i < 4000; ++i) {
    x = x * 1664525u + 1013904223u;  // a fixed pseudo-random walk
    const std::uint32_t pos = (x >> 8) % 101000u;  // some past the end
    se::Edit r;
    if ((x >> 4) % 3u == 0u && t.count > 1u) {
      r = se::delete_cut(t, t.cut[(x >> 12) % t.count].start);  // mostly the first (refused) or a real start
    } else if ((x >> 4) % 7u == 1u) {
      r = se::delete_cut(t, pos);  // mostly nothing there
    } else {
      r = se::insert_cut(t, pos);
    }
    (r.ok ? oks : refused)++;
    REQUIRE(tiles(t));
    REQUIRE(t.length == 100000u);
  }
  CHECK(oks > 100u);
  CHECK(refused > 100u);
  for (unsigned i = 1; i < t.count; ++i) {  // no cut under the minimum, apart from what even_cuts made
    CHECK(t.cut[i - 1].len >= se::kMinCutSamples);
  }
}

TEST_CASE("sample edit: snap_zero finds the nearest sign change within the radius") {
  std::vector<std::int16_t> s(400, 100);
  for (unsigned i = 0; i < 10; ++i) {
    s[i] = -100;  // negative up to 9, positive from 10: a crossing at 10
  }
  CHECK(se::snap_zero(s.data(), 400, 12) == 10u);  // found, after
  CHECK(se::snap_zero(s.data(), 400, 10) == 10u);  // on it
  CHECK(se::snap_zero(s.data(), 400, 60) == 10u);  // radius edge: 50 away
  CHECK(se::snap_zero(s.data(), 400, 80) == 80u);  // 70 away: none within the radius
  CHECK(se::snap_zero(s.data(), 400, 80, 100) == 10u);
  CHECK(se::snap_zero(s.data(), 400, 300) == 300u);  // none
  const std::vector<std::int16_t> flat(100, 5);
  CHECK(se::snap_zero(flat.data(), 100, 50) == 50u);
  // at both ends
  CHECK(se::snap_zero(s.data(), 400, 0) == 10u);  // index 0 is never a crossing
  std::vector<std::int16_t> e(100, 100);
  e[99] = -100;  // the last index crosses
  CHECK(se::snap_zero(e.data(), 100, 99) == 99u);
  CHECK(se::snap_zero(e.data(), 100, 500) == 99u);  // a cursor past the end is held inside
  CHECK(se::snap_zero(e.data(), 100, 70) == 99u);
  // a tie: crossings at 8 and 12, the cursor on 10: the earlier index
  std::vector<std::int16_t> tie(40, 100);
  for (unsigned i = 0; i < 8; ++i) {
    tie[i] = -100;
  }
  for (unsigned i = 12; i < 40; ++i) {
    tie[i] = -100;  // positive 8..11, negative from 12
  }
  CHECK(se::snap_zero(tie.data(), 40, 10) == 8u);
  // a run of zeros counts as non-negative: -1, 0 0 0 0, then -1 again
  const std::int16_t z[] = {-1, 0, 0, 0, 0, -1, -1};
  CHECK(se::snap_zero(z, 7, 3) == 1u);   // (-1, 0): a crossing at 1; d = 2 beats the one at 5 (d = 2) as the earlier index
  CHECK(se::snap_zero(z, 7, 4) == 5u);   // (0, -1) at 5, d = 1
  CHECK(se::snap_zero(nullptr, 100, 5) == 0u);
  CHECK(se::snap_zero(s.data(), 0, 5) == 0u);
}

TEST_CASE("sample edit: time_text is seconds with three decimals at 44,100 Hz") {
  CHECK(tt(0) == "0.000");
  CHECK(tt(1) == "0.000");  // 23 microseconds
  CHECK(tt(22050) == "0.500");
  CHECK(tt(44100) == "1.000");
  CHECK(tt(44099) == "1.000");  // rounds up and carries into the second
  CHECK(tt(44100u * 123u + 4410u) == "123.100");
  CHECK(tt(44100u * 16u) == "16.000");
  CHECK(tt(44100u * 2u + 44u) == "2.001");  // 0.9977 ms
  CHECK(tt(44100u * 2u + 22u) == "2.000");
  CHECK(tt(44100u * 5u + 4u * 441u) == "5.040");
  char b[12];
  se::time_text(0xFFFFFFFFu, b);  // fits the buffer whatever the position
  CHECK(std::strlen(b) < sizeof b);
  se::time_text(44100u, b, 22050u);  // another rate
  CHECK(std::string(b) == "2.000");
}

TEST_CASE("sample edit: the cursor's steps") {
  CHECK(se::move_cursor(10, 1, 1, 100) == 11u);
  CHECK(se::move_cursor(10, -20, 1, 100) == 0u);
  CHECK(se::move_cursor(10, 500, 1, 100) == 99u);
  CHECK(se::move_cursor(10, 3, 5, 100) == 25u);
  CHECK(se::move_cursor(5, 1, 1, 0) == 0u);
  CHECK(se::move_cursor(0xFFFFFFF0u, 1000, 0x10000u, 0xFFFFFFFFu) == 0xFFFFFFFEu);  // no overflow
  CHECK(se::coarse_step(6400) == 100u);
  CHECK(se::coarse_step(10) == 1u);
  CHECK(se::clamp_cursor(50, 40) == 39u);
  CHECK(se::clamp_cursor(5, 40) == 5u);
  CHECK(se::clamp_cursor(5, 0) == 0u);
}

TEST_CASE("sample edit: CUT EVEN steps OFF, 8, 16, 24; a detent stops at the ends, a tap wraps") {
  CHECK(se::even_cuts_of(0) == 8u);  // the default code is 8: a zeroed table is right
  CHECK(se::even_cuts_of(1) == 16u);
  CHECK(se::even_cuts_of(2) == 24u);
  CHECK(se::even_cuts_of(3) == 0u);
  CHECK(std::string(se::even_name(0)) == "8");
  CHECK(std::string(se::even_name(3)) == "OFF");
  unsigned c = 3;  // OFF
  c = se::step_even(c, 1, false);
  CHECK(c == 0u);  // 8
  c = se::step_even(c, 1, false);
  CHECK(c == 1u);  // 16
  c = se::step_even(c, 1, false);
  CHECK(c == 2u);  // 24
  CHECK(se::step_even(c, 1, false) == 2u);  // stops
  CHECK(se::step_even(c, 1, true) == 3u);   // a tap wraps to OFF
  CHECK(se::step_even(3, -1, false) == 3u);
  CHECK(se::step_even(3, -1, true) == 2u);
  CHECK(se::step_even(0, -5, false) == 3u);
}

TEST_CASE("sample edit: the REPLACES arm warns once per key while the table is the hand's") {
  se::Arm a;  // closed
  CHECK(se::arm_step(a, 0, true) == se::Gate::Warn);
  CHECK(se::arm_step(a, 0, true) == se::Gate::Go);
  CHECK(se::arm_step(a, 0, true) == se::Gate::Go);  // stays open for the next detents
  CHECK(se::arm_step(a, 7, true) == se::Gate::Warn);  // another key warns for itself
  CHECK(se::arm_step(a, 7, true) == se::Gate::Go);
  se::arm_cancel(a);
  CHECK(se::arm_step(a, 7, true) == se::Gate::Warn);
  se::arm_cancel(a);
  CHECK(se::arm_step(a, 0, false) == se::Gate::Go);  // no edit, no warning
  CHECK(se::arm_step(a, 7, false) == se::Gate::Go);
  const se::Arm zero{};
  CHECK(zero.key1 == 0u);  // a zeroed one (SDRAM) is closed
}

// ---- on the engine ----

namespace {

void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(5);
}

float unit_param(sim::Harness& h, std::uint16_t id) {
  float best = 0.0f;
  for (unsigned u = 0; u < 8; ++u) {
    const float v = h.engine().param(static_cast<dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
}

float rp(sim::Harness& h, unsigned reader, Vocoder::RegionField f) { return unit_param(h, Vocoder::region_param(reader, f)); }

// SAMPLE REC takes 1 BAR into slot 1, then SAMPLE EDIT opens.
struct Rig {
  o3sdk::platform::host::FakeCapture cap;
  std::unique_ptr<sim::Harness> h;
  Rig() {
    cap.input_tone(220.0f, -12.0f);
    h = std::make_unique<sim::Harness>(::o3sdk_app(), nullptr, nullptr, nullptr, nullptr, &cap);
    REQUIRE(h->start());
    h->run_ms(100);
  }
  void take() {
    tap(*h, app::Button::SampleRec);
    h->run_ms(60);
    tap(*h, app::Button::F3);
    tap(*h, app::Button::F4);
    tap(*h, app::Button::F4);
    tap(*h, app::Button::F4);
    tap(*h, app::Button::Rec);
    h->run_ms(9000);
  }
  void open() {
    tap(*h, app::Button::SampleEdit);
    h->run_ms(60);
  }
  // Pad `pad` pressed and let go: the region's Start and Len the unit was asked for, and whether it was asked at all.
  bool audition(unsigned pad, float& start, float& len) {
    h->run_ms(200);  // the voice of the one before ends
    h->pad_down(pad);
    h->run_ms(40);
    const bool on = rp(*h, 0, Vocoder::RegionField::Trig) == doctest::Approx(1.0f);
    start = rp(*h, 0, Vocoder::RegionField::Start);
    len = rp(*h, 0, Vocoder::RegionField::Len);
    h->pad_up(pad);
    h->run_ms(40);
    return on;
  }
  void shift_turn(int detents) {
    h->press(app::Button::Shift);
    h->run_ms(2);
    h->turn(detents);
    h->run_ms(5);
    h->release(app::Button::Shift);
    h->run_ms(5);
  }
};

bool has_row(const app::Help& help, app::Button b) {
  for (std::size_t i = 0; i < help.button_count; ++i) {
    if (help.buttons[i].button == b) {
      return help.buttons[i].text != nullptr;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("sample edit: the screen opens on an empty slot and every cut key says why it did nothing") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.open();
  tap(h, app::Button::Insert);
  tap(h, app::Button::Delete);
  tap(h, app::Button::F1);
  tap(h, app::Button::F1);
  tap(h, app::Button::F8);
  tap(h, app::Button::F2);
  tap(h, app::Button::F7);
  h.turn(5);
  h.run_ms(60);
  float start = 0, len = 0;
  CHECK_FALSE(rig.audition(0, start, len));  // no cut to play
  CHECK(h.runtime().frame_failures() == 0u);
  CHECK(h.display().peak_frame_words() < 2048u);
}

TEST_CASE("sample edit: INSERT cuts at the cursor, the pads play the hand's cut, DELETE takes it back") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0;
  REQUIRE(rig.audition(0, s0, l0));
  const float whole = l0 * 8.0f;  // eight even cuts
  REQUIRE(whole > 6400.0f);
  float s = 0, l = 0;
  CHECK_FALSE(rig.audition(8, s, l));  // no ninth cut yet
  rig.shift_turn(3);                   // 3/64 of the slot, inside the first cut
  tap(h, app::Button::Insert);
  h.run_ms(60);
  REQUIRE(rig.audition(1, s, l));  // the second cut is the hand's: starts near 3/64, ends where the old first one did
  CHECK(s == doctest::Approx(whole * 3.0f / 64.0f).epsilon(0.02));
  CHECK(s + l == doctest::Approx(l0).epsilon(0.001));
  CHECK(rig.audition(8, s, l));  // nine cuts
  tap(h, app::Button::Insert);   // the same spot again: too close, nothing changes
  h.run_ms(60);
  CHECK_FALSE(rig.audition(9, s, l));
  tap(h, app::Button::Delete);  // the cursor is on the hand's cut
  h.run_ms(60);
  CHECK_FALSE(rig.audition(8, s, l));  // back to eight
  REQUIRE(rig.audition(1, s, l));
  CHECK(s == doctest::Approx(l0).epsilon(0.001));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: CUT EVEN replaces the hand's cuts on the second try, a take's own cuts at once") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s = 0, l = 0, s0 = 0, l0 = 0;
  REQUIRE(rig.audition(0, s0, l0));
  tap(h, app::Button::F1);  // F1 points the encoder at CUT EVEN
  h.turn(1);                // 16: the cuts are still a take's, so no warning
  h.run_ms(20);
  CHECK(h.display().peak_frame_words() < 2048u);
  REQUIRE(rig.audition(0, s, l));
  CHECK(l == doctest::Approx(l0 / 2.0f).epsilon(0.01));
  REQUIRE(rig.audition(15, s, l));  // sixteen cuts
  tap(h, app::Button::Insert);      // a hand edit (the cursor is at 0: refused, so nothing is the hand's yet)
  rig.shift_turn(1);                // the cursor moves, which also lets F1's focus go back to the cursor
  tap(h, app::Button::Insert);
  h.run_ms(60);
  CHECK(rig.audition(16, s, l));  // seventeen
  tap(h, app::Button::F1);
  h.turn(1);  // OFF to 8: the first try on the key warns and changes nothing
  h.run_ms(20);
  h.turn(1);  // the same detent again takes it: eight even cuts
  h.run_ms(20);
  CHECK_FALSE(rig.audition(8, s, l));
  CHECK(rig.audition(7, s, l));
  tap(h, app::Button::F1);
  h.turn(1);  // 16: the table is a take's again, no warning
  h.run_ms(20);
  CHECK(rig.audition(15, s, l));
  CHECK_FALSE(rig.audition(16, s, l));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: any other key cancels the REPLACES warning") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s = 0, l = 0;
  rig.shift_turn(3);
  tap(h, app::Button::Insert);
  h.run_ms(60);
  REQUIRE(rig.audition(8, s, l));  // nine cuts, the hand's
  tap(h, app::Button::F8);         // warns
  tap(h, app::Button::Right);      // cancels
  tap(h, app::Button::F8);         // warns again
  CHECK(rig.audition(8, s, l));    // nine still; the pad cancels the arm too
  tap(h, app::Button::F8);         // warns
  tap(h, app::Button::F8);         // the second one in a row takes it
  h.run_ms(60);
  CHECK_FALSE(rig.audition(1, s, l));  // one cut over the slot
  CHECK(rig.audition(0, s, l));
  CHECK(l > 0.0f);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: a hand cut survives at the same length, a new take starts again from eight") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s = 0, l = 0;
  rig.shift_turn(3);
  tap(h, app::Button::Insert);
  h.run_ms(3000);  // service() runs every pass
  CHECK(rig.audition(8, s, l));  // still nine
  // a new take (SAMPLE REC) of another length: eight even cuts, edited cleared
  tap(h, app::Button::SampleRec);
  h.run_ms(60);
  tap(h, app::Button::F3);  // LENGTH: next value
  tap(h, app::Button::F3);
  tap(h, app::Button::Rec);
  h.run_ms(20000);
  tap(h, app::Button::SampleEdit);
  h.run_ms(60);
  CHECK_FALSE(rig.audition(8, s, l));
  CHECK(rig.audition(7, s, l));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: UP and DOWN pick the slot, LEFT and RIGHT walk the cuts") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s = 0, l = 0, l0 = 0;
  REQUIRE(rig.audition(0, s, l0));
  tap(h, app::Button::Right);  // the cursor on cut 2's start
  tap(h, app::Button::Right);  // and 3's
  tap(h, app::Button::Delete);  // the cut 3 goes: cut 2 is now two long
  h.run_ms(60);
  REQUIRE(rig.audition(1, s, l));
  CHECK(l == doctest::Approx(l0 * 2.0f).epsilon(0.01));
  tap(h, app::Button::Left);  // back to cut 2's start
  tap(h, app::Button::Left);  // 0
  tap(h, app::Button::Delete);  // refused: the first cut stays
  h.run_ms(60);
  CHECK(rig.audition(0, s, l));
  tap(h, app::Button::Down);  // slot 2 is empty
  h.run_ms(60);
  CHECK_FALSE(rig.audition(0, s, l));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: HELP gives a row to every button the screen answers") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.open();
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
  for (const app::Button b : {app::Button::Up, app::Button::Down, app::Button::Left, app::Button::Right, app::Button::Insert, app::Button::Delete,
                              app::Button::Shift, app::Button::Rec, app::Button::Params, app::Button::Performance, app::Button::Play,
                              app::Button::Master, app::Button::File, app::Button::SampleLoad, app::Button::SamplePlay, app::Button::SampleEdit,
                              app::Button::SampleRec, app::Button::Song, app::Button::Pattern, app::Button::Note, app::Button::Instr,
                              app::Button::Vol, app::Button::Fx}) {
    INFO(std::string(app::button_name(b)));
    CHECK(has_row(help, b));
  }
  for (unsigned k = 0; k < 8; ++k) {
    CHECK(help.fkey[k] != nullptr);
  }
  CHECK(help.encoder != nullptr);
  CHECK(help.pad_group[0] != 0u);
  CHECK(help.summary != nullptr);
  CHECK(std::string(help.summary).find("INSERT") != std::string::npos);
  CHECK(std::string(help.summary).find("DELETE") != std::string::npos);
  CHECK(std::string(help.summary).find("COME LATER") != std::string::npos);
}

// ---- the cuts reach the step sequencer ----

namespace {

constexpr unsigned kBlock = 64;
constexpr std::uint32_t kPer = 4000;
constexpr double kSixteenth = 5512.5;

struct Real {
  dsp::Engine e;
  Vocoder v;
  std::vector<std::int16_t> mem;
  o3_voc03er::Bank bank = {};
  o3_voc03er::SliceMap map = {};
  o3_voc03er::StepSeq seq;
  dsp::UnitId id = dsp::kNoUnit;
  bool ok = false;
  Real() : mem(static_cast<std::size_t>(o3_voc03er::Loop::kCount) * kPer, std::int16_t{0}) {
    for (std::uint32_t k = 0; k < kPer; ++k) {
      mem[kPer + k] = static_cast<std::int16_t>(12000.0f * std::sin(6.2831853f * 220.0f * static_cast<float>(k) / 44100.0f));
    }
    v.bind_loops(mem.data(), kPer);
    id = e.attach(v);
    seq.init(id, &bank, &map);
    ok = e.set_source(0, id) && e.add_hook(seq);
    e.start();
    (void)e.set_param(id, Vocoder::CarSrc, 1.0f);
    (void)e.set_param(id, Vocoder::ModSrc, 4.0f);  // slot 2 speaks
    (void)e.note_on(id, 48, 100);
  }
  void run(unsigned blocks) {
    float l[kBlock], r[kBlock];
    for (unsigned b = 0; b < blocks; ++b) {
      e.render(nullptr, nullptr, l, r, kBlock);
    }
  }
};

}  // namespace

TEST_CASE("sample edit: an inserted cut reaches the map after the forced publish, and the SLICE lane plays it") {
  Real r;
  REQUIRE(r.ok);
  sm::SliceTable t = even(kPer, 1);
  sm::SlicePublished st;
  REQUIRE(sm::publish_slices(r.seq, st, 1, &t, kPer));
  CHECK(r.seq.slice_count() == 1u);
  REQUIRE(se::insert_cut(t, 1500).ok);
  CHECK_FALSE(sm::publish_slices(r.seq, st, 1, &t, kPer));  // same slot, same length: nothing, until the screen forces it
  CHECK(r.seq.slice_count() == 1u);
  st = sm::SlicePublished{};  // SampleScreen::cuts_changed()
  CHECK(sm::publish_slices(r.seq, st, 1, &t, kPer));
  REQUIRE(r.seq.slice_count() == 2u);
  CHECK(r.map.start[1] == kPer + 1500u);
  CHECK(r.map.len[1] == 2500u);
  REQUIRE(o3_voc03er::set_slice(r.bank.pattern[0], 2, 2));  // step 3 plays cut 2, the hand's
  REQUIRE(r.e.play());
  r.run(static_cast<unsigned>(2.0 * kSixteenth / kBlock) - 2u);
  CHECK(r.v.loop_status(1).region == false);
  r.run(8);
  REQUIRE(r.v.loop_status(1).region == true);
  const std::uint32_t pos = r.v.loop_status(1).position;
  CHECK(pos >= kPer + 1500u);
  CHECK(pos < kPer + 1500u + 2500u);
}
