// SPDX-License-Identifier: GPL-3.0-or-later
// SAMPLE EDIT (U7a): the pure logic of the cuts (sample_edit_logic.h), the screen on the engine (a take, then the keys reach the cut table and the
// pads audition it), the cuts reaching the step sequencer's map and the SLICE lane, and HELP. Synthetic signals only; no card.
#include <doctest/doctest.h>

#include "rec_take.h"
#include "tract_skip.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/dsp/engine.h>
#include <o3sdk/platform/host/fake_capture.h>
#include <o3sdk/sim/harness.h>

#include <o3_voc03er/stepseq.h>
#include <o3_voc03er/units/vocoder/loop.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include "../app/src/overview.h"
#include "../app/src/sample_edit_logic.h"

#include <algorithm>
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

TEST_CASE("sample edit: CUT BEATS steps OFF, 4, 8, 16; a detent stops at the ends, a tap wraps") {
  CHECK(se::beats_per_bar(0) == 0u);
  CHECK(se::beats_per_bar(1) == 4u);
  CHECK(se::beats_per_bar(2) == 8u);
  CHECK(se::beats_per_bar(3) == 16u);
  CHECK(std::string(se::beats_name(0)) == "OFF");
  CHECK(std::string(se::beats_name(3)) == "16");
  CHECK(se::step_beats(0, 1, false) == 1u);
  CHECK(se::step_beats(3, 1, false) == 3u);  // stops
  CHECK(se::step_beats(3, 1, true) == 0u);   // a tap wraps
  CHECK(se::step_beats(0, -1, true) == 3u);
  CHECK(se::step_beats(1, -5, false) == 0u);
}

TEST_CASE("sample edit: a slot is a take when it is a whole number of bars at the tempo") {
  const float bar = 88200.0f;  // 120 BPM
  CHECK(se::is_take(88200, bar));
  CHECK(se::is_take(4u * 88200u, bar));
  CHECK_FALSE(se::is_take(88200u + 20000u, bar));  // 1.2 bars: a WAV
  CHECK_FALSE(se::is_take(44100, bar));             // half a bar
  CHECK_FALSE(se::is_take(0, bar));
  CHECK_FALSE(se::is_take(88200, 100000.0f));       // another tempo
}

TEST_CASE("sample edit: beat_cuts tile the slot on whole beats of the bar") {
  const float bar = 88200.0f;
  sm::SliceTable t;
  se::BeatCut r = se::beat_cuts(t, 88200, bar, 4);  // one bar, 4 a bar
  CHECK(r.count == 4u);
  CHECK_FALSE(r.capped);
  CHECK(tiles(t));
  CHECK(t.cut[1].start == 22050u);
  r = se::beat_cuts(t, 2u * 88200u, bar, 8);  // two bars, 8 a bar: 16
  CHECK(r.count == 16u);
  CHECK(tiles(t));
  CHECK(t.cut[1].start == 11025u);
  r = se::beat_cuts(t, 4u * 88200u, bar, 16);  // four bars, 16 a bar: 64 steps, 24 cuts
  CHECK(r.beats == 64u);
  CHECK(r.count == 24u);
  CHECK(r.capped);
  CHECK(tiles(t));
  CHECK(t.cut[1].start == 5513u);                          // the grid is still the 16th (5512.5 rounded)
  CHECK(t.cut[23].start + t.cut[23].len == 4u * 88200u);  // the last cut runs on to the slot's end
  r = se::beat_cuts(t, 6u * 88200u, bar, 4);  // 24 steps exactly: not capped
  CHECK(r.count == 24u);
  CHECK_FALSE(r.capped);
  CHECK(tiles(t));
  r = se::beat_cuts(t, 7u * 88200u, bar, 4);  // 28 steps
  CHECK(r.capped);
  CHECK(r.count == 24u);
  // A take a few samples short of its bars still cuts whole beats, and the cuts tile it.
  r = se::beat_cuts(t, 2u * 88200u - 3u, bar, 4);
  CHECK(r.count == 8u);
  CHECK(tiles(t));
}

TEST_CASE("sample edit: beat_cuts refuses what cannot be cut and leaves the table alone") {
  sm::SliceTable t = even(88200, 8);
  CHECK(se::beat_cuts(t, 0, 88200.0f, 4).count == 0u);
  CHECK(se::beat_cuts(t, 88200, 88200.0f, 0).count == 0u);
  CHECK(se::beat_cuts(t, 88200, 500.0f, 16).count == 0u);  // a step under 64 samples
  CHECK(t.count == 8u);
  CHECK(tiles(t));
}

TEST_CASE("sample edit: a position is at one x on the picture, its marks and the ruler") {
  CHECK(se::kPicX == 28);
  CHECK(se::kPicW == 744u);
  CHECK(se::kPicX + static_cast<int>(se::kPicW) == 26 + 748 - 2);  // the picture fills the rectangle inside its border
  for (std::uint32_t len : {1000u, 44100u, 88200u, 705600u}) {
    CHECK(se::mark_x(0, len) == se::kPicX);
    CHECK(se::mark_x(len - 1u, len) == se::kPicX + 743);
    CHECK(se::mark_x(len, len) == se::kPicX + 743);
    int last = 0;
    for (std::uint32_t pos = 0; pos < len; pos += len / 97u + 1u) {
      const int x = se::mark_x(pos, len);
      CHECK(x == se::kPicX + static_cast<int>(o3_voc03er::overview::x_of(pos, len, se::kPicW)));  // the picture's own map
      CHECK(x >= last);
      last = x;
    }
    // A cut's x over the picture is its x on the ruler: both draw at mark_x(start).
    const sm::SliceTable t = even(len, 8);
    for (unsigned i = 0; i < t.count; ++i) {
      const int on_picture = se::mark_x(t.cut[i].start, len);
      const int on_ruler = se::mark_x(t.cut[i].start, len);
      CHECK(on_picture == on_ruler);
      CHECK(on_picture >= se::kPicX);
      CHECK(on_picture <= se::kPicX + 743);
    }
  }
}

TEST_CASE("sample edit: NORMALISE scales the peak to -1 dBFS in slices") {
  std::vector<std::int16_t> d(10000);
  for (std::size_t i = 0; i < d.size(); ++i) d[i] = static_cast<std::int16_t>(std::lround(8000.0 * std::sin(0.05 * static_cast<double>(i))));
  d[5000] = -8192;  // the peak, negative
  se::Norm n;
  se::norm_begin(n, 10000);
  CHECK(n.phase == se::NormPhase::Scan);
  unsigned steps = 1;
  while (!se::norm_step(n, d.data())) {
    ++steps;
    REQUIRE(steps < 100u);
  }
  CHECK(n.phase == se::NormPhase::Done);
  CHECK(steps == 6u);  // 3 slices to scan (4,096 each), 3 to apply
  CHECK(n.peak == 8192);
  CHECK(n.gain == doctest::Approx(29204.0f / 8192.0f));
  std::int32_t peak = 0;
  for (auto v : d) peak = std::max<std::int32_t>(peak, v < 0 ? -v : v);
  CHECK(peak == 29204);
  CHECK(d[5000] == -29204);
  CHECK(se::gain_tenths_db(n.gain) == 110);  // +11.0 dB
  CHECK(se::norm_percent(n) == 100u);
  CHECK(se::norm_step(n, d.data()));  // done stays done, and changes nothing
  CHECK(d[5000] == -29204);
}

TEST_CASE("sample edit: NORMALISE reads a slice a call, and the percent climbs") {
  std::vector<std::int16_t> d(3u * se::kNormSlice, 100);
  se::Norm n;
  se::norm_begin(n, static_cast<std::uint32_t>(d.size()));
  CHECK(se::norm_percent(n) == 0u);
  CHECK_FALSE(se::norm_step(n, d.data()));
  CHECK(n.pos == se::kNormSlice);
  CHECK(se::norm_percent(n) == 16u);  // a third of the scan
  CHECK_FALSE(se::norm_step(n, d.data()));
  CHECK_FALSE(se::norm_step(n, d.data()));
  CHECK(n.phase == se::NormPhase::Apply);
  CHECK(se::norm_percent(n) == 50u);
  CHECK(d[0] == 100);  // nothing written during the scan
  CHECK_FALSE(se::norm_step(n, d.data()));
  CHECK(d[0] != 100);
  CHECK(d[se::kNormSlice] == 100);  // the second slice has not been reached
}

TEST_CASE("sample edit: NORMALISE refuses silence, never clips and keeps a quiet slot's sign") {
  std::vector<std::int16_t> z(5000, 0);
  se::Norm n;
  se::norm_begin(n, 5000);
  while (!se::norm_step(n, z.data())) {
  }
  CHECK(n.phase == se::NormPhase::Silent);
  CHECK(z[0] == 0);
  se::norm_begin(n, 0);
  CHECK(se::norm_step(n, z.data()));
  CHECK(n.phase == se::NormPhase::Silent);
  // Full scale comes down to the target.
  std::vector<std::int16_t> f(100, 32767);
  f[1] = -32768;
  se::norm_begin(n, 100);
  while (!se::norm_step(n, f.data())) {
  }
  CHECK(f[0] <= 29204);
  CHECK(f[0] >= 29203);
  CHECK(f[1] >= -29204);
  CHECK(se::gain_tenths_db(n.gain) == -10);
  // A quiet one goes up with its signs.
  std::vector<std::int16_t> q = {1, -1, 2, -2, 0};
  se::norm_begin(n, 5);
  while (!se::norm_step(n, q.data())) {
  }
  CHECK(q[3] == -29204);
  CHECK(q[2] == 29204);
  CHECK(q[0] > 0);
  CHECK(q[1] < 0);
  CHECK(q[4] == 0);
  CHECK(se::gain_tenths_db(0.0f) == 0);
  CHECK(se::gain_tenths_db(1.0f) == 0);
  CHECK(se::gain_tenths_db(2.0f) == 60);
}

// ---- on the engine ----

namespace {

void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(5);
}

// SHIFT + key: SHIFT goes down first and stays through the key (CLR CUTS is SHIFT + F8).
void shift_tap(sim::Harness& h, app::Button b) {
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, b);
  h.release(app::Button::Shift);
  h.run_ms(5);
}

float unit_param(sim::Harness& h, std::uint16_t id) {
  float best = 0.0f;
  for (unsigned u = 0; u < 8; ++u) {
    if (o3_voc03er::test::is_tract_unit(h, u)) continue;
    const float v = h.engine().param(static_cast<dsp::UnitId>(u), id);
    best = std::fabs(v) > std::fabs(best) ? v : best;
  }
  return best;
}

float rp(sim::Harness& h, unsigned reader, Vocoder::RegionField f) { return unit_param(h, Vocoder::region_param(reader, f)); }

// The vocoder's unit: the one whose UnitInfo names it.
dsp::UnitId voc_unit(sim::Harness& h) {
  for (unsigned u = 0; u < 8; ++u) {
    const dsp::UnitInfo* info = h.engine().unit_info(static_cast<dsp::UnitId>(u));
    if (info != nullptr && std::strcmp(info->name, "VOCODER") == 0) {
      return static_cast<dsp::UnitId>(u);
    }
  }
  return dsp::kNoUnit;
}

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
    o3_voc03er::test::rec_length_one_bar(*h);
    o3_voc03er::test::rec_autoplay(*h, false);  // the take waits in the slot, on no side
    o3_voc03er::test::rec_slot(*h, 0);
    tap(*h, app::Button::Rec);
    h->run_ms(9000);
  }
  // A take may loop on after the recording (it depends on SAMPLE's state): the slot stopped, as a trim wants it.
  void stop_slot() {
    for (unsigned u = 0; u < 8; ++u) {
      (void)h->engine().set_param(static_cast<dsp::UnitId>(u), Vocoder::loop_param(0, Vocoder::LoopField::Play), 0.0f);
    }
    h->run_ms(100);
  }
  void open() {
    tap(*h, app::Button::SampleEdit);
    h->run_ms(60);
  }
  // Pad `pad` pressed and let go: the region's Start and Len the unit was asked for, and whether it was asked at all. SAMPLE EDIT also picks the slice
  // (the encoder then moves its start, and a pad never lets a pick go): tap_pad keeps the pick, audition (the probe most tests use) lets it go again
  // with SHIFT + UP, which on slot 1 only sets the focus back to the cursor, so the F key and the cursor are the encoder's, as before SE1.
  bool audition(unsigned pad, float& start, float& len) {
    const bool on = tap_pad(pad, start, len);
    if (on) {
      h->pad_down(pad);
      h->run_ms(40);
      h->pad_up(pad);
      h->run_ms(40);
      unpick();
      if (fkey != app::Button::Shift) {
        tap(*h, fkey);  // the F key the encoder was pointed at: a first tap only points it again
      }
    }
    return on;
  }
  void unpick() { shift_tap(*h, app::Button::Up); }
  app::Button fkey = app::Button::Shift;  // Shift: none; a test that points the encoder at F1..F5 sets it so audition gives the encoder back to it
  bool tap_pad(unsigned pad, float& start, float& len) {
    // The voice of the one before ends first. TRIG ONE SHOT (the default since issue #3) leaves Trig at 1 after its cut ends (only a hit
    // or a GATE release writes it), so Trig reads 1 between hits: the rig lowers it, as a GATE release would, and a hit raises it again.
    const dsp::UnitId voc = voc_unit(*h);
    REQUIRE(voc != dsp::kNoUnit);
    (void)h->engine().set_param(voc, Vocoder::region_param(0, Vocoder::RegionField::Trig), 0.0f);
    h->run_ms(200);
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
  shift_tap(h, app::Button::F8);
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
  REQUIRE(rig.audition(1, s, l));  // a pad puts the cursor on its slice's start: the hand's cut
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
  rig.fkey = app::Button::F1;
  h.turn(1);                // 16: the cuts are still a take's, so no warning
  h.run_ms(20);
  CHECK(h.display().peak_frame_words() < 2048u);
  REQUIRE(rig.audition(0, s, l));
  CHECK(l == doctest::Approx(l0 / 2.0f).epsilon(0.01));
  REQUIRE(rig.audition(15, s, l));  // sixteen cuts
  tap(h, app::Button::Insert);      // a hand edit (the cursor is at 0: refused, so nothing is the hand's yet)
  rig.fkey = app::Button::Shift;
  rig.shift_turn(1);                // the cursor moves, which also lets F1's focus go back to the cursor
  tap(h, app::Button::Insert);
  h.run_ms(60);
  CHECK(rig.audition(16, s, l));  // seventeen
  rig.fkey = app::Button::F1;
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
  shift_tap(h, app::Button::F8);   // warns
  tap(h, app::Button::Right);      // cancels
  shift_tap(h, app::Button::F8);   // warns again
  CHECK(rig.audition(8, s, l));    // nine still; the pad cancels the arm too
  shift_tap(h, app::Button::F8);   // warns
  shift_tap(h, app::Button::F8);   // the second one in a row takes it
  h.run_ms(60);
  CHECK_FALSE(rig.audition(1, s, l));  // one cut over the slot
  CHECK(rig.audition(0, s, l));
  CHECK(l > 0.0f);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: the SHIFT + F8 warning keeps the picked slice, the clear itself lets it go") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s = 0, l = 0;
  rig.shift_turn(3);
  tap(h, app::Button::Insert);
  h.run_ms(60);
  REQUIRE(rig.tap_pad(3, s, l));  // slice 4 picked
  h.run_ms(2500);
  CHECK(h.leds().level(3) == app::kLedOn);
  shift_tap(h, app::Button::F8);  // only warns: the table is the hand's
  h.run_ms(300);
  CHECK(h.leds().level(3) == app::kLedOn);  // cancelling it would leave the pick as it was
  shift_tap(h, app::Button::F8);  // the second one takes it
  h.run_ms(2500);
  CHECK(h.leds().level(3) != app::kLedOn);
  CHECK_FALSE(rig.audition(1, s, l));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: F8 alone never changes the cuts, SHIFT + F8 clears them") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s = 0, l = 0;
  CHECK(rig.audition(7, s, l));  // eight even cuts, nothing the hand did: SHIFT + F8 clears them at once
  tap(h, app::Button::F8);
  h.run_ms(60);
  CHECK(rig.audition(7, s, l));  // F8 alone did nothing
  shift_tap(h, app::Button::F8);
  h.run_ms(60);
  CHECK_FALSE(rig.audition(1, s, l));
  CHECK(rig.audition(0, s, l));
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
  o3_voc03er::test::rec_length_one_bar(h);
  tap(h, app::Button::Right);  // LENGTH: 2 BARS
  o3_voc03er::test::rec_autoplay(h, false);
  o3_voc03er::test::rec_slot(h, 0);
  tap(h, app::Button::Rec);
  h.run_ms(20000);
  tap(h, app::Button::SampleEdit);
  h.run_ms(60);
  CHECK_FALSE(rig.audition(8, s, l));
  CHECK(rig.audition(7, s, l));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: SHIFT + UP and DOWN pick the slot, LEFT and RIGHT walk the cuts") {
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
  shift_tap(h, app::Button::Down);  // slot 2 is empty (UP and DOWN alone zoom)
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
                              app::Button::Shift, app::Button::Params, app::Button::Performance, app::Button::Play,
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
  CHECK(std::string(help.summary).find("EMPTIES THE SLOT") != std::string::npos);
  CHECK(std::string(help.fkey[5]).find("-1 DB") != std::string::npos);
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

namespace {
std::int32_t best_peak() {
  std::int32_t m = 0;
  for (unsigned s = 0; s < 4; ++s) m = std::max(m, o3_voc03er::overview::shared().peak(s));
  return m;
}
unsigned cuts_heard(Rig& rig) {
  unsigned n = 0;
  float s = 0, l = 0;
  for (unsigned p = 0; p < 24; ++p) {
    if (rig.audition(p, s, l)) ++n;
  }
  return n;
}
}  // namespace

TEST_CASE("sample edit: F2 CUT BEATS cuts 4, 8 or 16 a bar on a take, and warns before it replaces a hand's cut") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s = 0, l0 = 0, l = 0;
  REQUIRE(rig.audition(0, s, l0));  // eight even cuts: the whole take is 8 * l0, a whole number of bars
  const float bar = 88200.0f;       // 120 BPM at 44.1 kHz
  // SAMPLE REC keeps its length from one Harness to the next, so the take's bars are read and not assumed.
  const unsigned bars = static_cast<unsigned>(l0 * 8.0f / bar + 0.5f);
  REQUIRE(bars >= 1u);
  // `n` a bar: every cut a beat long, as many as the bars hold, at most 24.
  auto check = [&](unsigned n) {
    REQUIRE(rig.audition(0, s, l));
    CHECK(l == doctest::Approx(bar / static_cast<float>(n)).epsilon(0.01));
    const unsigned cuts = std::min(bars * n, 24u);
    CHECK(rig.audition(cuts - 1u, s, l));
    if (cuts < 24u) {
      CHECK_FALSE(rig.audition(cuts, s, l));
    }
  };
  tap(h, app::Button::F2);  // points the encoder at the key
  rig.fkey = app::Button::F2;
  h.turn(1);
  h.run_ms(40);
  check(4u);
  for (unsigned n : {8u, 16u}) {
    h.turn(1);  // warns: a pad has dropped the arm
    h.run_ms(20);
    h.turn(1);
    h.run_ms(40);
    check(n);
  }
  // A hand's cut: the first try on F2 warns and changes nothing, the second takes it.
  h.turn(-2);  // back to 4 a bar: a re-cut warns as well, once a pad has dropped the arm
  h.run_ms(20);
  h.turn(-2);
  h.run_ms(40);
  check(4u);
  const unsigned cuts = bars * 4u;
  tap(h, app::Button::Right);  // the cursor goes back to the waveform
  rig.fkey = app::Button::Shift;
  rig.shift_turn(3);
  tap(h, app::Button::Insert);
  h.run_ms(60);
  REQUIRE(rig.audition(cuts, s, l));  // one more: the hand's
  tap(h, app::Button::F2);
  rig.fkey = app::Button::F2;
  h.turn(1);  // F2 reads OFF on a hand's table, so this asks for 4 a bar: warns, changes nothing
  h.run_ms(20);
  CHECK(rig.audition(cuts, s, l));  // this pad drops the arm: the next detent warns again
  h.turn(1);
  h.run_ms(20);
  h.turn(1);  // the same detent again takes it
  h.run_ms(20);
  CHECK_FALSE(rig.audition(cuts, s, l));
  CHECK(h.runtime().frame_failures() == 0u);
  CHECK(h.display().peak_frame_words() < 2048u);
}

TEST_CASE("sample edit: F6 asks, F1 confirms, the peak goes to -1 dBFS, any other key cancels") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  const std::int32_t before = best_peak();
  REQUIRE(before > 100);
  REQUIRE(before < 29000);
  tap(h, app::Button::F6);
  tap(h, app::Button::Right);  // cancels
  tap(h, app::Button::F1);     // a plain F1 now is the cut-even key, not a confirm
  h.run_ms(600);
  CHECK(best_peak() == before);
  tap(h, app::Button::F6);
  tap(h, app::Button::F1);
  h.run_ms(600);  // a few slices a frame
  h.run_ms(600);
  CHECK(best_peak() == doctest::Approx(29204.0f).epsilon(0.01));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: F6 on an empty slot or while recording says why and changes nothing") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.open();
  const std::int32_t before = best_peak();  // the overview is shared by every test of the run
  tap(h, app::Button::F6);
  tap(h, app::Button::F1);
  h.run_ms(300);
  CHECK(best_peak() == before);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: SHIFT + DELETE asks, F1 empties the slot, plain DELETE is unchanged") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  REQUIRE(cuts_heard(rig) == 8u);
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Delete);
  h.release(app::Button::Shift);
  h.run_ms(5);
  tap(h, app::Button::Right);  // cancels
  h.run_ms(60);
  CHECK(cuts_heard(rig) == 8u);
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Delete);
  h.release(app::Button::Shift);
  h.run_ms(5);
  tap(h, app::Button::F1);
  h.run_ms(300);
  CHECK(cuts_heard(rig) == 0u);
  CHECK(h.runtime().frame_failures() == 0u);
}

// ---- TRIM (F4 START, F5 END) and COPY (U7c2b) ----

TEST_CASE("sample edit: a bracket moves by detents, stays kMinCutSamples apart and inside the slot") {
  const std::uint32_t len = 44100;
  se::Brackets b{0u, len};
  CHECK_FALSE(se::trim_moved(b, len));
  b = se::bracket_move(b, false, 3, 100u, len);
  CHECK(b.start == 300u);
  CHECK(b.end == len);
  CHECK(se::trim_moved(b, len));
  b = se::bracket_move(b, false, -9, 100u, len);  // not below 0
  CHECK(b.start == 0u);
  b = se::bracket_move(b, true, 5, 100u, len);  // not above the length
  CHECK(b.end == len);
  b = se::bracket_move(b, true, -1000, 1000u, len);  // not closer than kMinCutSamples to START
  CHECK(b.end == se::kMinCutSamples);
  b = se::bracket_move(b, false, 1000, 1000u, len);
  CHECK(b.start == 0u);
  b = se::Brackets{0u, len};
  b = se::bracket_move(b, false, 1000, 1000u, len);
  CHECK(b.start == len - se::kMinCutSamples);
  CHECK(b.end == len);
  // A slot too short for two brackets keeps them.
  const se::Brackets t{0u, se::kMinCutSamples};
  const se::Brackets m = se::bracket_move(t, true, -4, 1u, se::kMinCutSamples);
  CHECK(m.start == 0u);
  CHECK(m.end == se::kMinCutSamples);
}

TEST_CASE("sample edit: trim_cuts shifts, drops and clips so the cuts still tile") {
  const sm::SliceTable in = even(8000, 8);  // 1000 each
  sm::SliceTable out;
  REQUIRE(se::trim_cuts(in, 1500, 6500, out));
  CHECK(out.length == 5000u);
  CHECK(tiles(out));
  REQUIRE(out.count == 6u);  // cuts 2 (clipped) .. 7 (clipped)
  CHECK(out.cut[0].start == 0u);
  CHECK(out.cut[0].len == 500u);
  CHECK(out.cut[5].len == 500u);
  // The whole slot: the same cuts.
  REQUIRE(se::trim_cuts(in, 0, 8000, out));
  CHECK(out.count == 8u);
  CHECK(tiles(out));
  // A range inside one cut: one cut.
  REQUIRE(se::trim_cuts(in, 2100, 2900, out));
  CHECK(out.count == 1u);
  CHECK(out.cut[0].len == 800u);
  CHECK(tiles(out));
  // Refused: empty, backwards, past the length; out untouched.
  const sm::SliceTable keep = out;
  CHECK_FALSE(se::trim_cuts(in, 500, 500, out));
  CHECK_FALSE(se::trim_cuts(in, 600, 500, out));
  CHECK_FALSE(se::trim_cuts(in, 0, 8001, out));
  CHECK(out.length == keep.length);
  // A table that does not tile at all falls back to one cut.
  sm::SliceTable none;
  none.length = 1000;
  REQUIRE(se::trim_cuts(none, 100, 900, out));
  CHECK(out.count == 1u);
  CHECK(out.length == 800u);
}

TEST_CASE("sample edit: next_empty_slot walks on from the slot, wraps, never picks it") {
  bool used[4] = {true, false, true, false};
  CHECK(se::next_empty_slot(used, 0) == 1u);
  CHECK(se::next_empty_slot(used, 1) == 3u);
  CHECK(se::next_empty_slot(used, 3) == 1u);
  used[1] = used[3] = true;
  CHECK(se::next_empty_slot(used, 0) == 4u);
  used[2] = false;
  CHECK(se::next_empty_slot(used, 2) == 4u);  // only itself is free
}

TEST_CASE("sample edit: the trim's move reads below what it writes, bakes a 64-sample fade-in, and goes in slices") {
  const std::uint32_t n = 3u * se::kJobSlice + 500u;
  std::vector<std::int16_t> m(static_cast<std::size_t>(n) + 4000u);
  for (std::size_t i = 0; i < m.size(); ++i) {
    m[i] = static_cast<std::int16_t>(1000 + static_cast<int>(i % 7000));
  }
  const std::vector<std::int16_t> ref = m;
  const std::uint32_t start = 3999;
  se::Job j;
  se::job_begin(j, n, true);
  unsigned steps = 0;
  while (!se::job_step(j, m.data(), m.data() + start)) {
    ++steps;
    REQUIRE(steps < 20u);
  }
  CHECK(steps == 4u);  // four slices of move, then the fade
  CHECK(se::job_percent(j) == 100u);
  CHECK(m[0] == 0);
  CHECK(m[32] == ref[start + 32] / 2);
  CHECK(m[64] == ref[start + 64]);
  CHECK(m[n - 1] == ref[start + n - 1]);
  CHECK(se::job_step(j, m.data(), m.data() + start));  // done stays done, changes nothing
  CHECK(m[n - 1] == ref[start + n - 1]);
  // COPY: no fade, a different slot.
  std::vector<std::int16_t> d(1000, 0);
  se::job_begin(j, 1000, false);
  CHECK(se::job_step(j, d.data(), ref.data()));
  CHECK(d[0] == ref[0]);
  CHECK(d[999] == ref[999]);
  // An empty job is done at once.
  se::job_begin(j, 0, true);
  CHECK(se::job_step(j, d.data(), ref.data()));
}

TEST_CASE("sample edit: F4 and F5 point the encoder at a bracket; F1 trims, the cuts shift, the slot gets shorter") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0;
  REQUIRE(rig.audition(0, s0, l0));
  const float whole = l0 * 8.0f;
  tap(h, app::Button::F1);  // an F1 with no bracket focused is CUT EVEN: nothing to trim
  tap(h, app::Button::F4);
  rig.fkey = app::Button::F4;
  rig.shift_turn(3);  // START at 3/64 of the slot
  h.run_ms(60);
  CHECK(cuts_heard(rig) == 8u);  // moving a bracket changes nothing yet
  rig.stop_slot();               // a trim refuses while the slot plays or a slice sounds
  h.run_ms(500);
  tap(h, app::Button::F1);       // the bracket is off its end: this trims
  h.run_ms(900);
  float s = 0, l = 0;
  REQUIRE(rig.audition(0, s, l));
  CHECK(s < 1.0f);
  CHECK(l == doctest::Approx(whole * 5.0f / 64.0f).epsilon(0.02));  // the first cut lost its head
  CHECK(cuts_heard(rig) == 8u);
  // END: pull it in by an eighth of the new length; the last cut goes.
  tap(h, app::Button::F5);
  rig.fkey = app::Button::F5;
  rig.shift_turn(-9);
  rig.stop_slot();
  h.run_ms(500);
  tap(h, app::Button::F1);
  h.run_ms(900);
  CHECK(cuts_heard(rig) == 7u);
  CHECK(h.runtime().frame_failures() == 0u);
  CHECK(h.display().peak_frame_words() < 2048u);
}

TEST_CASE("sample edit: a trim and a copy on an empty slot, or with nothing moved, change nothing") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.open();
  tap(h, app::Button::F4);
  rig.shift_turn(2);
  tap(h, app::Button::F1);
  tap(h, app::Button::F5);
  tap(h, app::Button::Copy);
  tap(h, app::Button::F1);
  h.run_ms(600);
  CHECK(cuts_heard(rig) == 0u);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: COPY asks, F1 confirms, the next empty slot gets the sample; any other key cancels") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  const std::int32_t src = o3_voc03er::overview::shared().peak(0);
  REQUIRE(src > 100);
  REQUIRE(o3_voc03er::overview::shared().peak(1) == 0);
  tap(h, app::Button::Copy);
  tap(h, app::Button::Right);  // cancels
  tap(h, app::Button::F6);     // F6 asks NORMALISE; this is only to see that F1 no longer copies
  tap(h, app::Button::Right);
  h.run_ms(600);
  CHECK(o3_voc03er::overview::shared().peak(1) == 0);
  tap(h, app::Button::Copy);
  tap(h, app::Button::F1);
  h.run_ms(900);
  CHECK(o3_voc03er::overview::shared().peak(1) == src);
  CHECK(o3_voc03er::overview::shared().peak(0) == src);
  CHECK(h.runtime().frame_failures() == 0u);
}

// ---- F3 CUT HITS (U7d): the detector on synthetic signals, then the key on the engine -------------------------------------------------------

namespace {

// A noise burst of peak amplitude `amp` (of 32767) at sample `at`, decaying with time constant `tau` samples, added into `x`.
// The noise of a burst is its own sequence, seeded by where it starts. A case hears the same bursts whatever cases ran before it; a static
// seed made them depend on the cases before, and the shards run a file's cases in other company.
void burst(std::vector<std::int16_t>& x, std::uint32_t at, float amp, float tau = 900.0f) {
  std::uint32_t seed = 12345u + at * 2654435761u;
  for (std::uint32_t i = 0; at + i < x.size() && i < static_cast<std::uint32_t>(tau * 8.0f); ++i) {
    seed = seed * 1664525u + 1013904223u;
    const float n = static_cast<float>(static_cast<std::int32_t>(seed >> 8) - (1 << 23)) / static_cast<float>(1 << 23);
    const float v = static_cast<float>(x[at + i]) + n * amp * 32767.0f * std::exp(-static_cast<float>(i) / tau);
    x[at + i] = static_cast<std::int16_t>(std::max(-32767.0f, std::min(32767.0f, v)));
  }
}

// Scan the whole of `x` as the screen does, a slice at a time, then cut it at `sens`.
se::HitCut hits_of(const std::vector<std::int16_t>& x, unsigned sens, sm::SliceTable& t) {
  std::vector<std::uint16_t> e(se::kMaxHops, 0u);
  se::HopScan sc;
  se::hop_scan_begin(sc, static_cast<std::uint32_t>(x.size()));
  unsigned slices = 0;
  while (!se::hop_scan_step(sc, x.data(), e.data())) {
    ++slices;
    REQUIRE(slices < 400u);
  }
  return se::hit_cuts(t, e.data(), se::hop_count(static_cast<std::uint32_t>(x.size())), static_cast<std::uint32_t>(x.size()), sens);
}

bool near_cut(const sm::SliceTable& t, std::uint32_t pos) {
  for (unsigned i = 0; i < t.count; ++i) {
    const std::uint32_t d = t.cut[i].start > pos ? t.cut[i].start - pos : pos - t.cut[i].start;
    if (d <= se::kHop) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("sample edit: hit scan holds the mean of |sample| a hop, in slices, and the percent reaches 100") {
  std::vector<std::int16_t> x(10000, 0);
  for (std::uint32_t i = 0; i < 256u; ++i) {
    x[i] = i % 2u ? 1000 : -1000;
  }
  x[9990] = -32768;  // the last hop holds 16 samples: its mean is over what is there
  std::vector<std::uint16_t> e(se::kMaxHops, 7u);
  se::HopScan sc;
  se::hop_scan_begin(sc, 10000u);
  unsigned slices = 0;
  while (!se::hop_scan_step(sc, x.data(), e.data())) {
    ++slices;
    CHECK(se::hop_scan_percent(sc) <= 100u);
  }
  CHECK(slices == 2u);  // 10,000 samples in slices of 4,096: three passes, the third returns true
  CHECK(e[0] == 1000u);
  CHECK(e[1] == 0u);
  CHECK(e[se::hop_count(10000u) - 1u] == 32768u / 16u);
  CHECK(se::hop_count(10000u) == 40u);
  CHECK(se::hop_count(705600u) == se::kMaxHops);
  CHECK(se::hop_scan_step(sc, x.data(), e.data()));  // over: nothing more happens
}

TEST_CASE("sample edit: hit detector finds synthetic hits within a hop and the table tiles") {
  std::vector<std::int16_t> x(44100u * 3u, 0);
  const std::uint32_t at[] = {11025u, 33000u, 61111u, 95000u};
  for (std::uint32_t a : at) {
    burst(x, a, 0.6f);
  }
  sm::SliceTable t;
  const se::HitCut r = hits_of(x, 50u, t);
  CHECK(r.hits == 4u);
  CHECK(r.count == 5u);
  CHECK_FALSE(r.capped);
  CHECK(t.length == x.size());
  CHECK(tiles(t));
  CHECK(t.cut[0].start == 0u);
  for (std::uint32_t a : at) {
    CHECK(near_cut(t, a));
  }
}

TEST_CASE("sample edit: a low sensitivity keeps only the loud hit, a high one the soft too") {
  std::vector<std::int16_t> x(44100u * 3u, 0);
  burst(x, 22050u, 0.8f);  // loud
  burst(x, 77000u, 0.15f);  // soft: a fifth of it
  sm::SliceTable t;
  CHECK(hits_of(x, 100u, t).hits == 2u);
  CHECK(near_cut(t, 22050u));
  CHECK(near_cut(t, 77000u));
  for (unsigned sens : {1u, 20u, 50u}) {
    sm::SliceTable u;
    const se::HitCut r = hits_of(x, sens, u);
    CHECK(r.hits == 1u);
    CHECK(near_cut(u, 22050u));
    CHECK(tiles(u));
  }
}

TEST_CASE("sample edit: a held burst is one hit, at its start, at the highest sensitivity") {
  std::vector<std::int16_t> x(88200, 0);
  for (std::uint32_t at : {10000u, 40000u, 70000u}) {
    for (std::uint32_t i = 0; i < 11025u; ++i) {  // 250 ms: past the running mean's 93 ms, so its sustain is what could cut again
      x[at + i] = i % 2u ? 12000 : -12000;
    }
  }
  sm::SliceTable t;
  const se::HitCut r = hits_of(x, 100u, t);
  CHECK(r.hits == 3u);
  REQUIRE(t.count == 4u);
  CHECK(t.cut[1].start == doctest::Approx(10000.0f).epsilon(0.03));
  CHECK(t.cut[3].start == doctest::Approx(70000.0f).epsilon(0.01));
}

TEST_CASE("sample edit: two hits 20 ms apart give one cut") {
  std::vector<std::int16_t> x(44100u * 2u, 0);
  burst(x, 30000u, 0.7f);
  burst(x, 30000u + 882u, 0.7f);
  sm::SliceTable t;
  const se::HitCut r = hits_of(x, 80u, t);
  CHECK(r.hits == 1u);
  CHECK(r.count == 2u);
  CHECK(near_cut(t, 30000u));
}

TEST_CASE("sample edit: a slow sung-like swell gives no hits") {
  std::vector<std::int16_t> x(44100u * 3u, 0);
  for (std::uint32_t i = 0; i < x.size(); ++i) {
    const float ph = static_cast<float>(i) / static_cast<float>(x.size());
    const float env = 0.3f + 0.5f * (0.5f - 0.5f * std::cos(6.2831853f * ph));  // 0.3 up to 0.8 and back over 3 s
    // a vibrato of 5 Hz on 220 Hz: the phase is 2 pi f t plus its own integral, so the pitch stays inside +-6 Hz
    const float tt = static_cast<float>(i) / 44100.0f;
    const float phase = 6.2831853f * (220.0f * tt + (6.0f / (6.2831853f * 5.0f)) * (1.0f - std::cos(6.2831853f * 5.0f * tt)));
    x[i] = static_cast<std::int16_t>(env * 20000.0f * std::sin(phase));
  }
  sm::SliceTable t = even(static_cast<std::uint32_t>(x.size()));
  const sm::SliceTable before = t;
  const se::HitCut r = hits_of(x, 100u, t);
  CHECK(r.hits == 0u);
  CHECK(t.count == before.count);  // untouched
  CHECK(t.cut[3].start == before.cut[3].start);
}

TEST_CASE("sample edit: over 24 cuts the strongest hits stay, in time order, and capped is set") {
  std::vector<std::int16_t> x(44100u * 8u, 0);
  std::vector<std::uint32_t> at;
  // Every 0.25 s: seven soft hits (0.20 to 0.32), then 23 strong ones (0.5 to 0.9). Steps of 0.024 all the way up were within the noise of
  // a burst's peak hop, and which hit fell out depended on the noise (172 of 200 seeds dropped a wrong one); a gap between the two groups
  // makes the 23 strongest the last 23 whatever the noise.
  for (unsigned k = 0; k < 30u; ++k) {
    at.push_back(4000u + k * 11025u);
    const float amp = k < 7u ? 0.2f + 0.02f * static_cast<float>(k) : 0.5f + 0.4f * static_cast<float>(k - 7u) / 22.0f;
    burst(x, at.back(), amp, 600.0f);
  }
  sm::SliceTable t;
  const se::HitCut r = hits_of(x, 100u, t);
  CHECK(r.capped);
  CHECK(r.hits == 23u);
  CHECK(r.count == 24u);
  CHECK(tiles(t));
  CHECK(t.cut[0].start == 0u);
  for (unsigned k = 7u; k < 30u; ++k) {  // the 23 strongest are the last 23
    CHECK(near_cut(t, at[k]));
  }
  CHECK_FALSE(near_cut(t, at[0]));
  for (unsigned i = 1; i < t.count; ++i) {
    CHECK(t.cut[i].start > t.cut[i - 1u].start);
  }
}

TEST_CASE("sample edit: silence gives no hits, nor a slot of less than a hop, nor sensitivity 0") {
  std::vector<std::int16_t> x(44100u, 0);
  sm::SliceTable t = even(static_cast<std::uint32_t>(x.size()));
  CHECK(hits_of(x, 100u, t).hits == 0u);
  CHECK(t.count == 8u);
  std::vector<std::int16_t> tiny(100, 5000);
  sm::SliceTable u = even(100u, 2u);
  CHECK(hits_of(tiny, 100u, u).hits == 0u);
  CHECK(u.count == 2u);
  std::vector<std::int16_t> loud(44100u, 0);
  burst(loud, 20000u, 0.8f);
  CHECK(hits_of(loud, 0u, u).hits == 0u);
  CHECK(hits_of(loud, 100u, u).hits == 1u);
  sm::SliceTable none;
  std::uint16_t e1[1] = {9000};
  CHECK(se::hit_cuts(none, e1, 1u, 100u, 100u).hits == 0u);
  CHECK(se::hit_cuts(none, e1, 0u, 0u, 100u).hits == 0u);
}

TEST_CASE("sample edit: F3 steps 0 to 100, a detent stops at the ends, a tap goes by 10 and wraps") {
  CHECK(se::step_hits(0u, 1, false) == 1u);
  CHECK(se::step_hits(0u, -3, false) == 0u);
  CHECK(se::step_hits(99u, 5, false) == 100u);
  CHECK(se::step_hits(100u, 1, false) == 100u);
  CHECK(se::step_hits(0u, 1, true) == 50u);  // the first tap from OFF lands at 50
  CHECK(se::step_hits(50u, 1, true) == 60u);
  CHECK(se::step_hits(5u, 1, true) == 10u);
  CHECK(se::step_hits(37u, 1, true) == 40u);
  CHECK(se::step_hits(95u, 1, true) == 100u);
  CHECK(se::step_hits(100u, 1, true) == 0u);
  CHECK(se::hit_ratio(1u) > se::hit_ratio(100u));
  CHECK(se::hit_level(1u) > se::hit_level(100u));
}

TEST_CASE("sample edit: F3 CUT HITS scans the take, cuts at its onsets, warns before it replaces a hand's cut, and F1 reads it as OFF") {
  Rig rig;
  sim::Harness& h = *rig.h;
  // A take of 220 Hz at -60 dBFS with a -12 dBFS burst every 500 ms: the onsets are the hits.
  tap(h, app::Button::SampleRec);
  h.run_ms(60);
  o3_voc03er::test::rec_length_one_bar(h);
  o3_voc03er::test::rec_autoplay(h, false);
  o3_voc03er::test::rec_slot(h, 0);
  tap(h, app::Button::Rec);
  for (unsigned i = 0; i < 36u; ++i) {
    rig.cap.input_tone(220.0f, i % 2u ? -60.0f : -12.0f);
    h.run_ms(250);
  }
  rig.cap.input_tone(220.0f, -60.0f);
  rig.open();
  float s = 0, l = 0;
  tap(h, app::Button::F3);  // points the encoder at the key
  h.turn(100);
  h.run_ms(1500);  // the scan (a slice a frame), then the cuts
  unsigned cuts = 0;
  while (cuts < 24u && rig.audition(cuts, s, l)) {
    ++cuts;
  }
  CHECK(cuts >= 2u);
  REQUIRE(rig.audition(1, s, l));
  CHECK(s > 0.0f);
  // F1 re-cuts to 8 (the first try warns, the second takes it): F3 reads OFF then, so its next step asks for 1 and warns first, changing nothing.
  tap(h, app::Button::F1);
  h.turn(1);
  h.run_ms(20);
  h.turn(1);
  h.run_ms(40);
  tap(h, app::Button::F3);
  h.turn(1);
  h.run_ms(100);
  CHECK(rig.audition(7, s, l));  // still the 8 even cuts: the warning changed nothing
  CHECK_FALSE(rig.audition(8, s, l));
  CHECK(h.runtime().frame_failures() == 0u);
  CHECK(h.display().peak_frame_words() < 2048u);
}

// ---- SE1: a slice picked on its pad, its start moved by the encoder ----

namespace {

// +1000 up to 100, -1000 to 140, +30 to 200, -30 to 300, +1000 to 400: sign changes at 100 and 140 (loud), 200 (quiet), 300 (loud).
std::vector<std::int16_t> crossings() {
  std::vector<std::int16_t> v(400, 1000);
  for (unsigned i = 100; i < 140; ++i) v[i] = -1000;
  for (unsigned i = 140; i < 200; ++i) v[i] = 30;
  for (unsigned i = 200; i < 300; ++i) v[i] = -30;
  return v;
}

}  // namespace

TEST_CASE("sample edit: snap_quiet takes the quietest crossing ahead, in the way the start moves") {
  const std::vector<std::int16_t> v = crossings();
  CHECK(se::snap_quiet(v.data(), 400, 90, true) == 100u);   // 100 and 140 are as loud: the nearer one
  CHECK(se::snap_quiet(v.data(), 400, 150, true) == 200u);  // the quiet one
  CHECK(se::snap_quiet(v.data(), 400, 210, false) == 200u);
  CHECK(se::snap_quiet(v.data(), 400, 232, true) == 232u);  // nothing within 64 ahead: the target itself
  CHECK(se::snap_quiet(v.data(), 400, 250, false) == 200u);
  CHECK(se::snap_quiet(nullptr, 0, 5, true) == 5u);
  CHECK(se::snap_quiet(v.data(), 400, 399, true) == 399u);  // the end of the slot: nothing past it
  CHECK(se::snap_quiet(v.data(), 400, 0, false) == 0u);
}

TEST_CASE("sample edit: a slice start keeps moving the way the encoder turns, a crossing cannot hold it") {
  const std::vector<std::int16_t> v = crossings();
  std::uint32_t pos = 50;
  for (unsigned n = 0; n < 40 && pos < 390; ++n) {
    const std::uint32_t at = se::slice_move(v.data(), 400, pos, 1, false, 0, 390);
    CHECK(at > pos);
    pos = at;
  }
  CHECK(pos == 390u);
  for (unsigned n = 0; n < 40 && pos > 0; ++n) {
    const std::uint32_t at = se::slice_move(v.data(), 400, pos, -1, false, 0, 390);
    CHECK(at < pos);
    pos = at;
  }
  CHECK(pos == 0u);
  // on a crossing already: the next detent leaves it, either way
  CHECK(se::slice_move(v.data(), 400, 200, 1, false, 0, 390) > 200u);
  CHECK(se::slice_move(v.data(), 400, 200, -1, false, 0, 390) < 200u);
}

TEST_CASE("sample edit: slice_limits keep a slice start between its neighbours, slice 1 between 0 and the next cut or the END bracket") {
  const sm::SliceTable t = even(8000, 8);  // a cut every 1000
  std::uint32_t lo = 9, hi = 9;
  REQUIRE(se::slice_limits(t, 3, t.length, lo, hi));
  CHECK(lo == 2000u + se::kMinCutSamples);
  CHECK(hi == 4000u - se::kMinCutSamples);
  REQUIRE(se::slice_limits(t, 7, t.length, lo, hi));
  CHECK(hi == 8000u - se::kMinCutSamples);
  REQUIRE(se::slice_limits(t, 0, t.length, lo, hi));  // the START bracket: from 0, short of cut 2
  CHECK(lo == 0u);
  CHECK(hi == 1000u - se::kMinCutSamples);
  REQUIRE(se::slice_limits(t, 0, 500, lo, hi));  // and of the END bracket
  CHECK(hi == 500u - se::kMinCutSamples);
  CHECK_FALSE(se::slice_limits(t, 0, 10, lo, hi));
  CHECK_FALSE(se::slice_limits(t, 8, t.length, lo, hi));
  CHECK(hi == 0u);
  CHECK(lo == 0u);
}

TEST_CASE("sample edit: slice_limits with one slice, with 24, and with a table too short for two margins") {
  std::uint32_t lo = 9, hi = 9;
  const sm::SliceTable one = even(8000, 1);
  REQUIRE(se::slice_limits(one, 0, one.length, lo, hi));  // the one slice: from 0 to the slot's end, short of the END bracket
  CHECK(lo == 0u);
  CHECK(hi == 8000u - se::kMinCutSamples);
  REQUIRE(se::slice_limits(one, 0, 3000, lo, hi));
  CHECK(hi == 3000u - se::kMinCutSamples);
  CHECK_FALSE(se::slice_limits(one, 1, one.length, lo, hi));
  const sm::SliceTable full = even(24000, 24);  // a cut every 1000
  REQUIRE(se::slice_limits(full, 0, full.length, lo, hi));
  CHECK(lo == 0u);
  CHECK(hi == 1000u - se::kMinCutSamples);
  REQUIRE(se::slice_limits(full, 12, full.length, lo, hi));
  CHECK(lo == 11000u + se::kMinCutSamples);
  CHECK(hi == 13000u - se::kMinCutSamples);
  REQUIRE(se::slice_limits(full, 23, full.length, lo, hi));  // the last: to the slot's end
  CHECK(lo == 22000u + se::kMinCutSamples);
  CHECK(hi == 24000u - se::kMinCutSamples);
  CHECK_FALSE(se::slice_limits(full, 24, full.length, lo, hi));
  CHECK_FALSE(se::slice_limits(even(100, 4), 1, 100, lo, hi));  // 25-sample slices: no room for two margins
}

TEST_CASE("sample edit: slice_move from a start outside its limits walks in, never jumps to the edge") {
  // START left at 6000 by F4 while cut 2 is at 1000: the limits are [0, 936]
  CHECK(se::slice_move(nullptr, 8000, 6000, 1, false, 0, 936) == 6000u);   // away from the range: stays
  CHECK(se::slice_move(nullptr, 8000, 6000, 40, true, 0, 936) == 6000u);
  CHECK(se::slice_move(nullptr, 8000, 6000, -1, false, 0, 936) == 5968u);  // towards it: a step
  CHECK(se::slice_move(nullptr, 8000, 6000, -3, true, 0, 936) == 5997u);
  CHECK(se::slice_move(nullptr, 8000, 6000, -1000, false, 0, 936) == 0u);  // all the way in: the far edge
  CHECK(se::slice_move(nullptr, 8000, 6000, -170, false, 0, 936) == 560u);
  // and below the range
  CHECK(se::slice_move(nullptr, 8000, 100, -1, false, 2064, 3936) == 100u);
  CHECK(se::slice_move(nullptr, 8000, 100, 1, false, 2064, 3936) == 132u);
  CHECK(se::slice_move(nullptr, 8000, 100, 1000, false, 2064, 3936) == 3936u);
}

TEST_CASE("sample edit: slice_move steps 32 a detent, one sample with SHIFT, and clamps to its limits") {
  CHECK(se::slice_move(nullptr, 8000, 3000, 1, false, 2064, 3936) == 3032u);
  CHECK(se::slice_move(nullptr, 8000, 3000, -2, false, 2064, 3936) == 2936u);
  CHECK(se::slice_move(nullptr, 8000, 3000, 2, true, 2064, 3936) == 3002u);
  CHECK(se::slice_move(nullptr, 8000, 3000, -3, true, 2064, 3936) == 2997u);
  CHECK(se::slice_move(nullptr, 8000, 3000, 1000, false, 2064, 3936) == 3936u);
  CHECK(se::slice_move(nullptr, 8000, 3000, -1000, false, 2064, 3936) == 2064u);
  CHECK(se::slice_move(nullptr, 8000, 3000, 1000, true, 2064, 3936) == 3936u);
  CHECK(se::slice_move(nullptr, 8000, 3000, 0, false, 2064, 3936) == 3000u);
  CHECK(se::slice_move(nullptr, 8000, 3900, 1, false, 2064, 3936) == 3932u);
  CHECK(se::slice_move(nullptr, 8000, 3930, 1, false, 2064, 3936) == 3936u);  // the clamp
  CHECK(se::slice_move(nullptr, 8000, 4500, 0, false, 2064, 3936) == 3936u);  // a start outside comes back in
}

TEST_CASE("sample edit: set_slice_start moves the cut and keeps the table tiling; cut 1 and out-of-range targets are refused") {
  sm::SliceTable t = even(8000, 8);
  REQUIRE(se::set_slice_start(t, 3, 3500));
  CHECK(t.cut[2].start == 2000u);
  CHECK(t.cut[2].len == 1500u);
  CHECK(t.cut[3].start == 3500u);
  CHECK(t.cut[3].len == 500u);
  CHECK(t.count == 8u);
  CHECK(tiles(t));
  const sm::SliceTable before = t;
  CHECK_FALSE(se::set_slice_start(t, 0, 100));  // slice 1 is the trim START, not a cut
  CHECK_FALSE(se::set_slice_start(t, 3, 1500u + se::kMinCutSamples - 1u));
  CHECK_FALSE(se::set_slice_start(t, 3, 4000u - se::kMinCutSamples + 1u));
  CHECK_FALSE(se::set_slice_start(t, 8, 100));
  CHECK(std::memcmp(&t, &before, sizeof t) == 0);
  REQUIRE(se::set_slice_start(t, 7, 7936));
  CHECK(t.cut[7].len == 64u);
  CHECK(tiles(t));
}

TEST_CASE("sample edit: the zoom ladder halves from the whole slot down to the deepest rung, every step exactly x2, in and out") {
  const std::uint32_t len = 300000;
  CHECK(se::zoom_span(0, len) == len);
  CHECK(se::zoom_whole(0, len));
  const unsigned deepest = se::zoom_max_rung(len);
  CHECK(deepest == 9u);  // 300000 >> 9 = 585, >> 10 would be 292 < kZoomMinSpan
  unsigned r = 0;
  unsigned steps = 0;
  for (;;) {
    const unsigned n = se::zoom_step(r, len, 1);
    if (n == r) {
      break;
    }
    CHECK(n == r + 1u);  // every rung is a step
    CHECK(se::zoom_span(n, len) == se::zoom_span(r, len) / 2u);  // and each step is x2 (len >> n is the floor of the half of len >> (n - 1))
    r = n;
    ++steps;
  }
  CHECK(steps == deepest);
  CHECK(se::zoom_span(r, len) == 585u);
  CHECK(se::zoom_step(r, len, 1) == r);  // the deepest stays
  for (unsigned i = 0; i < deepest; ++i) {
    r = se::zoom_step(r, len, -1);
  }
  CHECK(r == 0u);
  CHECK(se::zoom_step(0, len, -1) == 0u);
  CHECK(se::zoom_step(3, len, 0) == 3u);
  CHECK(se::zoom_step(99, len, 1) == deepest);   // a stale rung is held inside the ladder
  CHECK(se::zoom_step(99, len, -1) == deepest - 1u);  // and a step out leaves from the deepest one
  CHECK(!se::zoom_whole(99, len));
}

TEST_CASE("sample edit: a 16 s slot goes from the whole slot to 344 samples in x2 steps only, a shorter slot has fewer rungs") {
  const std::uint32_t len = 705600;  // the longest slot: 16 s
  CHECK(se::zoom_max_rung(len) == 11u);
  CHECK(se::zoom_span(1, len) == 352800u);  // the first step is x2, not x4
  CHECK(se::zoom_span(11, len) == 344u);
  for (unsigned r = 1; r <= 11u; ++r) {
    CHECK(se::zoom_span(r, len) == se::zoom_span(r - 1u, len) / 2u);  // x2 (705600 = 2^6 x 11025: exact down to rung 6, then the floor of the half)
  }
  // A short slot has fewer rungs; one shorter than two minimum windows has none.
  CHECK(se::zoom_max_rung(30000) == 6u);
  CHECK(se::zoom_span(6, 30000) == 468u);
  CHECK(se::zoom_max_rung(687) == 0u);
  CHECK(se::zoom_max_rung(688) == 1u);
  CHECK(se::zoom_step(0, 300, 1) == 0u);  // shorter than the deepest rung: nothing to zoom to
  CHECK(se::zoom_whole(5, 300));
  // The view's rung can be deeper than a short slot's ladder: the step out leaves from the slot's own deepest rung.
  CHECK(se::zoom_rung_for(11, 30000) == 6u);
  CHECK(se::zoom_step(11, 30000, -1) == 5u);
  // The most one rebuild reads.
  CHECK(se::kZoomMaxReads == 22134u);
}

TEST_CASE("sample edit: the zoom window is centred on the start and clamped inside the slot") {
  const std::uint32_t len = 100000;
  CHECK(se::zoom_lo_for(50000, 11025, len) == 50000u - 5512u);
  CHECK(se::zoom_lo_for(100, 11025, len) == 0u);                      // the start of the slot
  CHECK(se::zoom_lo_for(99999, 11025, len) == len - 11025u);          // the end of the slot
  CHECK(se::zoom_lo_for(5000, len, len) == 0u);                       // whole
  CHECK(se::zoom_lo_for(5000, 200000, len) == 0u);                    // wider than the slot
  const std::uint32_t lo = se::zoom_lo_for(50000, 11025, len);
  CHECK(std::abs(se::zoom_x(50000, lo, 11025) - static_cast<int>(se::kPicW) / 2) <= 1);  // the start is the middle, to a pixel (an odd span has no centre sample)
  CHECK(se::zoom_x(lo - 1u, lo, 11025) == -1);
  CHECK(se::zoom_x(lo, lo, 11025) == 0);
  CHECK(se::zoom_x(lo + 11025u - 1u, lo, 11025) == static_cast<int>(se::kPicW) - 1);
  CHECK(se::zoom_x(lo + 11025u, lo, 11025) == -1);
  CHECK(se::zoom_x(5, 0, 0) == -1);
  // Whatever the window follows (the cursor, a picked start, the bracket F4 or F5 moves) is inside it, at every rung and every position, ends of the slot included.
  for (unsigned rung = 0; rung <= se::zoom_max_rung(len); ++rung) {
    const std::uint32_t sp = se::zoom_span(rung, len);
    for (std::uint32_t c : {0u, 1u, 343u, 50000u, len - 344u, len - 1u}) {
      const std::uint32_t l = se::zoom_lo_for(c, sp, len);
      CHECK(l + sp <= len);
      CHECK(c >= l);
      CHECK(c < l + sp);
      CHECK(se::zoom_x(c, l, sp) >= 0);
    }
    const std::uint32_t l_end = se::zoom_lo_for(len, sp, len);  // an END bracket at the slot's end: the window's last edge
    CHECK(l_end + sp == len);
    const int last_x = se::zoom_x(len - 1u, l_end, sp);  // the last sample is in the last sample-width of the picture (at 344 samples a sample is 2.2 px)
    CHECK(last_x < static_cast<int>(se::kPicW));
    CHECK(last_x >= static_cast<int>(se::kPicW) - static_cast<int>((se::kPicW + sp - 1u) / sp));
  }
  // The detent is about a pixel, never under a sample.
  CHECK(se::zoom_detent(344) == 1u);
  CHECK(se::zoom_detent(se::kPicW) == 1u);
  CHECK(se::zoom_detent(11025) == 11025u / se::kPicW);
  CHECK(se::zoom_detent(300000) == 300000u / se::kPicW);
}

TEST_CASE("sample edit: the zoom columns hold each column's lowest and highest sample, blank outside the slot, and read a bounded number") {
  std::vector<std::int16_t> v(5000, 1000);
  std::int16_t mn[se::kZoomCols], mx[se::kZoomCols];
  // A window that runs past the slot's end: the columns past it are blank.
  se::zoom_build(v.data(), 5000, 4000, 2000, mn, mx);
  CHECK(mn[0] == 1000);
  CHECK(mx[0] == 1000);
  const unsigned last = se::kZoomCols * 1000u / 2000u;  // the column that starts at sample 5000
  CHECK(mn[last - 1u] == 1000);
  CHECK(mn[last] > mx[last]);
  CHECK(mn[se::kZoomCols - 1u] > mx[se::kZoomCols - 1u]);
  se::zoom_build(nullptr, 5000, 0, 2000, mn, mx);
  CHECK(mn[100] > mx[100]);
  se::zoom_build(v.data(), 5000, 0, 0, mn, mx);  // no span: all blank, no division
  CHECK(mn[0] > mx[0]);
  std::vector<std::int16_t> w(30000, 0);
  w[1000] = -700;
  w[1001] = 900;
  const std::uint32_t span = 11025;
  const std::uint32_t lo = se::zoom_lo_for(1001, span, 30000);
  se::zoom_build(w.data(), 30000, lo, span, mn, mx);
  const unsigned col = static_cast<unsigned>(se::zoom_x(1000, lo, span)) / se::kZoomColW;
  CHECK(mn[col] == -700);  // a column narrower than the read budget reads every sample
  CHECK(mx[col] == 900);
  // The deepest rung: a column is one or two samples, so each sample shows.
  std::vector<std::int16_t> d(1000);
  for (unsigned i = 0; i < d.size(); ++i) {
    d[i] = static_cast<std::int16_t>(i);
  }
  se::zoom_build(d.data(), 1000, 100, 344, mn, mx);
  CHECK(mn[0] == 100);
  CHECK(mx[se::kZoomCols - 1u] == 100 + 343);
  for (unsigned c = 0; c < se::kZoomCols; ++c) {
    CHECK(mn[c] <= mx[c]);
  }
}

TEST_CASE("sample edit: a wide zoom column reads with a stride and stays inside the slot (the sanitizer run watches the reads)") {
  const std::uint32_t len = 3000000;  // 68 s: the widest span a rung can take over a slot this long is 4 s; rung 0 would be the whole slot
  std::vector<std::int16_t> v(len, 0);
  v[len - 1u] = 5;
  std::int16_t mn[se::kZoomCols], mx[se::kZoomCols];
  se::zoom_build(v.data(), len, len - 176400u, 176400u, mn, mx);  // the last 4 s
  for (unsigned c = 0; c < se::kZoomCols; ++c) {
    CHECK(mn[c] <= mx[c]);
    CHECK(mn[c] >= 0);
    CHECK(mx[c] <= 5);
  }
  // The same window with a window that overhangs: a wide column past the end is blank, one that straddles it reads only the slot.
  se::zoom_build(v.data(), len, len - 88200u, 176400u, mn, mx);
  CHECK(mn[0] <= mx[0]);
  CHECK(mn[se::kZoomCols - 1u] > mx[se::kZoomCols - 1u]);
  CHECK(se::kZoomReadCol * se::kZoomCols < 2u * se::kZoomSpan);
}

TEST_CASE("sample edit: the zoom label names the span") {
  char b[16];
  se::zoom_text(0, 300000, b);
  CHECK(std::strcmp(b, "WHOLE SLOT") == 0);
  se::zoom_text(1, 300000, b);
  CHECK(std::strcmp(b, "ZOOM 3.4 S") == 0);  // 150000 samples
  se::zoom_text(2, 300000, b);
  CHECK(std::strcmp(b, "ZOOM 1.7 S") == 0);
  se::zoom_text(3, 300000, b);
  CHECK(std::strcmp(b, "ZOOM 0.9 S") == 0);  // 850 ms, from 500 ms up it reads in seconds
  se::zoom_text(4, 300000, b);
  CHECK(std::strcmp(b, "ZOOM 425 MS") == 0);
  se::zoom_text(9, 300000, b);
  CHECK(std::strcmp(b, "ZOOM 13 MS") == 0);
  se::zoom_text(40, 300000, b);  // a stale rung reads as the deepest
  CHECK(std::strcmp(b, "ZOOM 13 MS") == 0);
  se::zoom_text(1, 705600, b);
  CHECK(std::strcmp(b, "ZOOM 8 S") == 0);
  se::zoom_text(3, 88200, b);
  CHECK(std::strcmp(b, "ZOOM 250 MS") == 0);  // 11025 samples
}

TEST_CASE("sample edit: F8 reads SPEAKS with LIVE or SLOT n, SHIFT shows CLR CUTS, and CONFIRM / REPLACES belongs to SHIFT + F8 only") {
  se::F8Text t = se::f8_text(false, false, 0);
  CHECK(std::strcmp(t.name, "SPEAKS") == 0);
  CHECK(std::strcmp(t.value, "LIVE") == 0);
  t = se::f8_text(false, false, 3);
  CHECK(std::strcmp(t.name, "SPEAKS") == 0);
  CHECK(std::strcmp(t.value, "SLOT 3") == 0);
  t = se::f8_text(false, false, 4);
  CHECK(std::strcmp(t.value, "SLOT 4") == 0);
  // The warning is the cut clear's: plain F8 never says CONFIRM, never "REPLACESON", and keeps what it speaks.
  t = se::f8_text(false, true, 2);
  CHECK(std::strcmp(t.name, "SPEAKS") == 0);
  CHECK(std::strcmp(t.value, "SLOT 2") == 0);
  t = se::f8_text(true, false, 2);
  CHECK(std::strcmp(t.name, "CLR CUTS") == 0);
  CHECK(t.value[0] == '\0');
  t = se::f8_text(true, true, 2);
  CHECK(std::strcmp(t.name, "CONFIRM") == 0);
  CHECK(std::strcmp(t.value, "REPLACES") == 0);
  t = se::f8_text(false, false, 200);  // out of range reads LIVE
  CHECK(std::strcmp(t.value, "LIVE") == 0);
}

TEST_CASE("sample edit: slice_move_step moves a chosen step, snaps only when asked and clamps") {
  CHECK(se::slice_move_step(nullptr, 8000, 3000, 3, 5, false, 2064, 3936) == 3015u);
  CHECK(se::slice_move_step(nullptr, 8000, 3000, -2, 1, false, 2064, 3936) == 2998u);
  CHECK(se::slice_move_step(nullptr, 8000, 3000, 1000, 296, false, 2064, 3936) == 3936u);
  CHECK(se::slice_move_step(nullptr, 8000, 3000, 1, 0, false, 2064, 3936) == 3001u);  // a zero step is one sample
  // Snapped: lands on the crossing ahead.
  std::vector<std::int16_t> s(8000, 100);
  s[3040] = -100;  // crossings at 3040 and 3041
  const std::uint32_t at = se::slice_move_step(s.data(), 8000, 3000, 1, 32, true, 2064, 3936);
  CHECK(at >= 3032u);
  CHECK(at <= 3041u);
  CHECK(se::slice_move_step(s.data(), 8000, 3000, 1, 32, false, 2064, 3936) == 3032u);
  CHECK(se::kSnapMinStep >= 1u);
}

TEST_CASE("sample edit: nearest_cut finds the cut closest to the cursor inside the view, never cut 0") {
  sm::SliceTable t = even(8000, 8);
  CHECK(se::nearest_cut(t, 1100, 0, 8000) == 1);          // 1000 is the nearest of 1000, 2000, ...
  CHECK(se::nearest_cut(t, 1600, 0, 8000) == 2);
  CHECK(se::nearest_cut(t, 10, 0, 8000) == 1);            // cut 0 at 0 is not a candidate
  CHECK(se::nearest_cut(t, 1100, 1500, 3500) == 2);       // the window leaves 1000 out
  CHECK(se::nearest_cut(t, 1100, 1001, 1999) == -1);      // none in view
  CHECK(se::nearest_cut(t, 2000, 2000, 2001) == 2);       // the window's start is in, its end is out
  CHECK(se::nearest_cut(t, 2000, 1000, 2000) == 1);
  sm::SliceTable one = even(8000, 1);
  CHECK(se::nearest_cut(one, 100, 0, 8000) == -1);
}

TEST_CASE("sample edit: slice_middle splits a slice in its middle, clear of both ends") {
  sm::SliceTable t = even(8000, 4);  // 0, 2000, 4000, 6000
  CHECK(se::slice_middle(t, 1, 2000, nullptr) == 3000u);
  CHECK(se::slice_middle(t, 0, 0, nullptr) == 1000u);
  CHECK(se::slice_middle(t, 0, 500, nullptr) == 1250u);  // the START bracket moved in: the slice is what is drawn
  CHECK(se::slice_middle(t, 4, 0, nullptr) == 0u);       // not a slice
  sm::SliceTable tiny = even(2u * se::kMinCutSamples - 2u, 2);
  CHECK(se::slice_middle(tiny, 0, 0, nullptr) == 0u);    // too short to split
  std::vector<std::int16_t> v(8000, 100);
  v[3010] = -100;
  const std::uint32_t at = se::slice_middle(t, 1, 2000, v.data());
  CHECK(at >= 3000u - se::kSnapRadius);
  CHECK(at <= 3000u + se::kSnapRadius);
  CHECK(at >= 2000u + se::kMinCutSamples);
}

TEST_CASE("sample edit: a tap on a pad picks its slice, lit high; the encoder moves its start; the same pad again keeps it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0, s = 0, l = 0;
  REQUIRE(rig.tap_pad(2, s0, l0));  // picked, and it plays
  h.run_ms(2500);
  CHECK(h.leds().level(2) == app::kLedOn);
  CHECK(h.leds().level(3) == app::kLedDim);
  CHECK(h.leds().level(8) == app::kLedOff);
  h.turn(3);  // the picked start, not the lit F key or the cursor
  h.run_ms(60);
  REQUIRE(rig.tap_pad(2, s, l));  // the same pad: plays the slice as it is now, and the pick stays
  CHECK(s >= s0 + 3.0f * static_cast<float>(se::kSliceStep));
  CHECK(s + l == doctest::Approx(s0 + l0).epsilon(0.0001));  // the end stays where it was
  h.run_ms(2500);
  CHECK(h.leds().level(2) == app::kLedOn);
  rig.unpick();
  h.run_ms(60);
  CHECK(h.leds().level(2) == app::kLedDim);
  const float moved = s;
  h.turn(40);  // back to the cursor: the table is not touched
  h.run_ms(60);
  REQUIRE(rig.audition(2, s, l));
  CHECK(s == doctest::Approx(moved));
  float sb = 0, lb = 0;  // the slice before ends where this one now starts
  REQUIRE(rig.audition(1, sb, lb));
  CHECK(sb + lb == doctest::Approx(moved));
  CHECK(h.runtime().frame_failures() == 0u);
  CHECK(h.display().peak_frame_words() < 2048u);
}

TEST_CASE("sample edit: SHIFT + encoder moves the picked start a sample a detent, and it stays 64 samples from its neighbours") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0, s = 0, l = 0;
  REQUIRE(rig.tap_pad(2, s0, l0));
  rig.shift_turn(-5);
  REQUIRE(rig.tap_pad(2, s, l));  // lets it go
  CHECK(s == doctest::Approx(s0 - 5.0f));
  CHECK(s + l == doctest::Approx(s0 + l0));
  float sp = 0, lp = 0;
  REQUIRE(rig.audition(1, sp, lp));
  REQUIRE(rig.tap_pad(2, s, l));  // picked again
  for (int i = 0; i < 60; ++i) {  // far past the slice before (a slice is up to 16 s): held 64 samples after its start
    h.turn(-400);
    h.run_ms(10);
  }
  h.run_ms(60);
  REQUIRE(rig.tap_pad(2, s, l));
  CHECK(s == doctest::Approx(sp + static_cast<float>(se::kMinCutSamples)));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: LEFT and RIGHT pick the previous or next slice, held inside the table; a pad past the last splits nothing") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s = 0, l = 0;
  REQUIRE(rig.tap_pad(2, s, l));
  tap(h, app::Button::Right);
  h.run_ms(2500);
  CHECK(h.leds().level(3) == app::kLedOn);
  CHECK(h.leds().level(2) == app::kLedDim);
  tap(h, app::Button::Left);
  tap(h, app::Button::Left);
  tap(h, app::Button::Left);
  tap(h, app::Button::Left);  // held at slice 1
  h.run_ms(60);
  CHECK(h.leds().level(0) == app::kLedOn);
  CHECK(h.leds().level(1) == app::kLedDim);
  REQUIRE(rig.tap_pad(7, s, l));
  tap(h, app::Button::Right);  // held at the last
  h.run_ms(2500);
  CHECK(h.leds().level(7) == app::kLedOn);
  CHECK(h.leds().level(8) == app::kLedOff);
  CHECK_FALSE(rig.tap_pad(10, s, l));  // past the last: nothing is picked, nothing is cut
  h.run_ms(2500);
  CHECK(h.leds().level(7) == app::kLedDim);
  CHECK(h.leds().level(10) == app::kLedOff);
  CHECK_FALSE(rig.tap_pad(8, s, l));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: slice 1's start is the trim START: the table is untouched, F4 then F1 trims by what the encoder moved") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0, s = 0, l = 0;
  REQUIRE(rig.tap_pad(0, s0, l0));  // slice 1 picked
  h.run_ms(2500);
  CHECK(h.leds().level(0) == app::kLedOn);
  rig.shift_turn(100);
  h.turn(2);
  h.run_ms(60);
  // a detent is a picture pixel of the whole-slot view (SE2); at least: the acceleration may add detents
  const float px = static_cast<float>(se::zoom_detent(static_cast<std::uint32_t>(l0 * 8.0f + 0.5f)));
  const float moved = 100.0f + 2.0f * px;
  tap(h, app::Button::F4);  // lets the slice go; the first tap on START keeps where it is
  h.run_ms(2500);
  CHECK(h.leds().level(0) == app::kLedDim);
  rig.stop_slot();
  h.run_ms(500);
  tap(h, app::Button::F1);  // the bracket is off its end: this trims
  h.run_ms(900);
  REQUIRE(rig.audition(0, s, l));
  CHECK(s < 1.0f);
  CHECK(l0 - l >= moved - 1.0f);
  CHECK(l0 - l < moved + 4.0f * px + 2.0f * static_cast<float>(se::kSnapRadius));
  CHECK(cuts_heard(rig) == 8u);  // no cut was added or lost
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: an F key lets the picked slice go and counts as a first tap; UP and DOWN zoom and keep it, SHIFT + DOWN lets it go") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0, s = 0, l = 0;
  REQUIRE(rig.tap_pad(4, s0, l0));
  tap(h, app::Button::F2);  // CUT BEATS: the first tap points the encoder at it, nothing is replaced
  h.run_ms(2500);
  CHECK(h.leds().level(4) == app::kLedDim);
  REQUIRE(rig.tap_pad(4, s, l));  // the table is as it was
  CHECK(s == doctest::Approx(s0));
  CHECK(l == doctest::Approx(l0));
  h.run_ms(2500);
  CHECK(h.leds().level(4) == app::kLedOn);  // picked again
  tap(h, app::Button::Down);                // zooms out (the whole slot is as far as it goes) and keeps the pick
  h.run_ms(2500);
  CHECK(h.leds().level(4) == app::kLedOn);
  shift_tap(h, app::Button::Down);  // SHIFT + DOWN: the next slot, which is empty
  h.run_ms(2500);
  CHECK(h.leds().level(4) != app::kLedOn);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: pad 1 plays from the START bracket, and the table is whole again after the press") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0, s = 0, l = 0;
  REQUIRE(rig.audition(0, s0, l0));
  CHECK(s0 < 1.0f);
  const float whole = l0 * 8.0f;
  tap(h, app::Button::F4);
  rig.shift_turn(3);  // START at 3/64 of the slot
  h.run_ms(60);
  REQUIRE(rig.tap_pad(0, s, l));  // slice 1 picked: it sounds from where START is
  CHECK(s == doctest::Approx(whole * 3.0f / 64.0f).epsilon(0.01));
  CHECK(s + l == doctest::Approx(l0).epsilon(0.0001));  // up to cut 2, as before
  REQUIRE(rig.audition(1, s, l));  // cut 2 is where it was
  CHECK(s == doctest::Approx(l0));
  CHECK(cuts_heard(rig) == 8u);
  tap(h, app::Button::F4);  // START back to 0 (SHIFT + encoder, the bracket in hand): the swapped cut came back whole
  rig.shift_turn(-3);
  h.run_ms(60);
  REQUIRE(rig.audition(0, s, l));
  CHECK(s < 1.0f);
  CHECK(l == doctest::Approx(l0).epsilon(0.0001));
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: a START past cut 2 is not thrown back to the edge by the encoder on slice 1") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0, s = 0, l = 0;
  REQUIRE(rig.audition(0, s0, l0));
  const float whole = l0 * 8.0f;
  tap(h, app::Button::F4);
  rig.shift_turn(12);  // START at 12/64: cut 2 is at 8/64
  h.run_ms(60);
  REQUIRE(rig.tap_pad(0, s, l));  // slice 1 picked (START is past its end: it plays as the table has it)
  h.turn(3);                      // away from the range: START stays
  h.run_ms(60);
  tap(h, app::Button::F4);  // lets the slice go; the first tap on START keeps where it is
  rig.stop_slot();
  h.run_ms(1500);  // the slice pad's region has ended whatever length the take got (it depends on cases that ran before): a trim refuses while one sounds
  tap(h, app::Button::F1);  // trims at START
  h.run_ms(900);
  REQUIRE(rig.audition(0, s, l));
  CHECK(s < 1.0f);
  CHECK(l == doctest::Approx(whole * 4.0f / 64.0f).epsilon(0.1));  // the head up to 12/64 is gone; thrown back to cut 2 it would be 8/64
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: a pad on an empty slot picks nothing; a take that starts under the screen drops the pick") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.open();  // nothing recorded yet
  h.pad_down(2);
  h.run_ms(40);
  h.pad_up(2);
  h.run_ms(2500);
  CHECK(h.leds().level(2) != app::kLedOn);
  CHECK(h.runtime().frame_failures() == 0u);
  // Two takes of the same length, made from outside this screen (an armed one reaching its bar, PERFORM's capture, MIDI): driven on the unit.
  const dsp::UnitId voc = voc_unit(h);
  REQUIRE(voc != dsp::kNoUnit);
  const auto record = [&](std::uint32_t ms) {
    (void)h.engine().set_param(voc, Vocoder::loop_param(0, Vocoder::LoopField::Rec), 1.0f);
    h.run_ms(ms);
    (void)h.engine().set_param(voc, Vocoder::loop_param(0, Vocoder::LoopField::Rec), 0.0f);
    h.run_ms(1500);
  };
  record(6000u);
  rig.open();
  float s0 = 0, l0 = 0;
  REQUIRE(rig.tap_pad(2, s0, l0));  // picked: lit high, the picture zoomed
  h.run_ms(2500);
  CHECK(h.leds().level(2) == app::kLedOn);
  (void)h.engine().set_param(voc, Vocoder::loop_param(0, Vocoder::LoopField::Rec), 1.0f);  // the slot records under the screen
  h.run_ms(600);
  h.pad_down(3);  // a pad while the slot records picks nothing
  h.run_ms(40);
  h.pad_up(3);
  h.run_ms(1000);
  CHECK(h.leds().level(3) != app::kLedOn);
  h.run_ms(4359);  // 600 + 40 + 1000 + 4359 ms: the take lands on the first one's exact length (the retake the pick must not survive); calibrated on the host sim
  (void)h.engine().set_param(voc, Vocoder::loop_param(0, Vocoder::LoopField::Rec), 0.0f);
  h.run_ms(2500);
  CHECK(h.leds().level(2) != app::kLedOn);  // the old pick did not come back over the new samples, whatever the new length
  CHECK(h.leds().level(3) != app::kLedOn);
  CHECK(h.runtime().frame_failures() == 0u);
}

// ---- SE3: F8 SPEAKS, the latch, DELETE of a picked slice, INSERT in it, the zoom on the screen ---------------------------------------------------

namespace {
float mod_src(sim::Harness& h) { return h.engine().param(voc_unit(h), Vocoder::ModSrc); }
}  // namespace

TEST_CASE("sample edit: F8 makes the slot speak and a second F8 gives LIVE back; an empty slot refuses; SHIFT + F8 never touches the route") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.open();
  const float live = mod_src(h);
  tap(h, app::Button::F8);  // slot 1 is empty
  h.run_ms(60);
  CHECK(mod_src(h) == live);
  rig.take();
  rig.open();
  // What the take left on the route follows SAMPLE REC's AFTER key, whose state outlives a Rig: start from LIVE as the first line did.
  (void)h.engine().set_param(voc_unit(h), Vocoder::ModSrc, live);
  h.run_ms(60);
  tap(h, app::Button::F8);
  h.run_ms(60);
  CHECK(mod_src(h) == static_cast<float>(sm::slot_value(0)));
  shift_tap(h, app::Button::F8);  // the cuts, not the route
  h.run_ms(60);
  CHECK(mod_src(h) == static_cast<float>(sm::slot_value(0)));
  tap(h, app::Button::F8);
  h.run_ms(60);
  CHECK(mod_src(h) == live);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: F8 is SAMPLE's F2 tap: from the same route both keys leave the same SPEAKS and SOUNDS, and a slot that SOUNDS is freed first") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  const dsp::UnitId voc = voc_unit(h);
  const float slot = static_cast<float>(sm::slot_value(0));
  const auto car_src = [&] { return h.engine().param(voc, Vocoder::CarSrc); };
  // The same start for both keys: LIVE speaks and SLOT 1 sounds (the route is what the unit holds).
  const auto start = [&] {
    (void)h.engine().set_param(voc, Vocoder::ModSrc, 0.0f);
    (void)h.engine().set_param(voc, Vocoder::CarSrc, slot);
    h.run_ms(60);
    REQUIRE(mod_src(h) == 0.0f);
    REQUIRE(car_src() == slot);
  };
  float mod[2][2] = {};
  float car[2][2] = {};
  const app::Button screen[2] = {app::Button::SampleEdit, app::Button::SamplePlay};
  const app::Button key[2] = {app::Button::F8, app::Button::F2};
  for (unsigned k = 0; k < 2; ++k) {
    tap(h, screen[k]);
    h.run_ms(60);
    start();
    for (unsigned t = 0; t < 2; ++t) {  // the first tap makes the slot speak, the second gives LIVE back
      tap(h, key[k]);
      h.run_ms(60);
      mod[k][t] = mod_src(h);
      car[k][t] = car_src();
    }
  }
  CHECK(mod[0][0] == slot);  // SAMPLE EDIT's F8: the slot speaks ...
  CHECK(car[0][0] != slot);  // ... and no longer sounds (never on both)
  CHECK(mod[0][1] == 0.0f);  // again: LIVE speaks
  for (unsigned t = 0; t < 2; ++t) {
    CHECK(mod[0][t] == mod[1][t]);
    CHECK(car[0][t] == car[1][t]);
  }
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: SHIFT + a lower pad latches its note: the LED shows it, the same pad lets go, another moves it, leaving releases it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  const auto latch = [&](unsigned pad) {
    h.press(app::Button::Shift);
    h.run_ms(2);
    h.pad_down(pad);
    h.run_ms(40);
    h.pad_up(pad);
    h.run_ms(40);
    h.release(app::Button::Shift);
    h.run_ms(2500);
  };
  CHECK(h.leds().level(30) != app::kLedOn);
  latch(30);
  CHECK(h.leds().level(30) == app::kLedOn);
  h.pad_down(33);  // a plain press plays over it
  h.run_ms(40);
  h.pad_up(33);
  h.run_ms(2500);
  CHECK(h.leds().level(30) == app::kLedOn);
  h.pad_down(30);  // a plain press of the latched pad and its release do not let it go
  h.run_ms(40);
  h.pad_up(30);
  h.run_ms(2500);
  CHECK(h.leds().level(30) == app::kLedOn);
  latch(32);  // another pad moves it
  CHECK(h.leds().level(32) == app::kLedOn);
  CHECK(h.leds().level(30) != app::kLedOn);
  latch(32);  // the same pad lets it go
  CHECK(h.leds().level(32) != app::kLedOn);
  latch(32);
  CHECK(h.leds().level(32) == app::kLedOn);
  tap(h, app::Button::Performance);  // leaving releases it
  h.run_ms(60);
  rig.open();
  h.run_ms(2500);
  CHECK(h.leds().level(32) != app::kLedOn);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: DELETE on a picked slice removes its start cut and picks the slice that took it; slice 1 cannot go") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0, s = 0, l = 0;
  REQUIRE(rig.audition(0, s0, l0));
  REQUIRE(rig.tap_pad(3, s, l));  // slice 4
  tap(h, app::Button::Delete);
  h.run_ms(2500);
  CHECK(h.leds().level(2) == app::kLedOn);  // the slice that swallowed it
  CHECK(h.leds().level(3) == app::kLedDim);
  REQUIRE(rig.tap_pad(2, s, l));
  CHECK(l == doctest::Approx(l0 * 2.0f).epsilon(0.01));
  REQUIRE(rig.tap_pad(0, s, l));  // slice 1: refused
  tap(h, app::Button::Delete);
  h.run_ms(60);
  rig.unpick();
  CHECK(cuts_heard(rig) == 7u);
  tap(h, app::Button::Delete);  // nothing picked: the cut nearest the cursor, which a pad put on slice 1's start, so cut 2
  h.run_ms(60);
  CHECK(cuts_heard(rig) == 6u);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: INSERT on a picked slice splits it in its middle and picks the second half") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0, s = 0, l = 0;
  REQUIRE(rig.tap_pad(1, s0, l0));
  tap(h, app::Button::Insert);
  h.run_ms(2500);
  CHECK(h.leds().level(2) == app::kLedOn);
  CHECK(h.leds().level(1) == app::kLedDim);
  REQUIRE(rig.tap_pad(1, s, l));
  CHECK(s == doctest::Approx(s0));
  CHECK(l == doctest::Approx(l0 * 0.5f).epsilon(0.1));
  rig.unpick();
  CHECK(cuts_heard(rig) == 9u);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: UP zooms in down to a one-sample detent, DOWN zooms out, both stop at their ends, the pick stays") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0, s = 0, l = 0;
  REQUIRE(rig.tap_pad(2, s0, l0));
  for (int i = 0; i < 14; ++i) {  // past the deepest rung
    tap(h, app::Button::Up);
  }
  h.run_ms(100);
  CHECK(h.display().peak_frame_words() < 4096u);
  h.turn(1);  // one detent at the deepest zoom: one sample
  h.run_ms(100);
  REQUIRE(rig.tap_pad(2, s, l));
  CHECK(s == doctest::Approx(s0 + 1.0f));
  CHECK(s + l == doctest::Approx(s0 + l0));
  for (int i = 0; i < 14; ++i) {  // past the whole slot
    tap(h, app::Button::Down);
  }
  h.run_ms(100);
  h.run_ms(2500);
  CHECK(h.leds().level(2) == app::kLedOn);  // the pick survived the zoom
  CHECK(h.runtime().frame_failures() == 0u);
}

// ---- the SE fold: the latch's note-off, the walk's ends, DELETE with nothing to take, the pick after a change of the samples -------------------------------

namespace {
constexpr double kSounding = 1e-4;  // output rms (fraction of full scale) above which a note sounds
// The rms of the next `ms` milliseconds of the recording (Harness::record(true) first).
double listen_rms(sim::Harness& h, unsigned ms) {
  const std::size_t from = h.audio().size();
  h.run_ms(ms);
  const std::vector<std::int16_t>& a = h.audio();
  double sum = 0.0;
  std::size_t n = 0;
  for (std::size_t i = from; i < a.size(); i += 2) {
    sum += static_cast<double>(a[i]) * static_cast<double>(a[i]);
    ++n;
  }
  return n != 0 ? std::sqrt(sum / static_cast<double>(n)) / 32768.0 : 0.0;
}
}  // namespace

TEST_CASE("sample edit: the latched note ends on a SHIFT re-press of its pad, on leaving the screen and on a change of PERFORM's pad mode") {
  Rig rig;
  sim::Harness& h = *rig.h;
  h.record(true);
  rig.open();
  const auto latch = [&](unsigned pad) {
    h.press(app::Button::Shift);
    h.run_ms(2);
    h.pad_down(pad);
    h.run_ms(40);
    h.pad_up(pad);
    h.run_ms(40);
    h.release(app::Button::Shift);
  };
  CHECK(listen_rms(h, 200) < kSounding);  // the live tone alone: no carrier note, no sound
  latch(30);
  CHECK(listen_rms(h, 300) > kSounding);  // the note holds with the pad up
  CHECK(h.leds().level(30) == app::kLedOn);
  latch(30);  // SHIFT again on the same pad: the note-off
  h.run_ms(900);  // the release tail
  CHECK(listen_rms(h, 200) < kSounding);
  CHECK(h.leds().level(30) != app::kLedOn);
  latch(30);
  CHECK(listen_rms(h, 300) > kSounding);
  tap(h, app::Button::Performance);  // leaving
  h.run_ms(900);
  CHECK(listen_rms(h, 200) < kSounding);
  rig.open();
  CHECK(h.leds().level(30) != app::kLedOn);
  latch(30);
  CHECK(listen_rms(h, 300) > kSounding);
  // The mode changes under the screen (the unit's Mode: MIDI, a step, PERFORM): the note and the latch are over, and back in the first mode nothing sounds again.
  const dsp::UnitId voc = voc_unit(h);
  REQUIRE(voc != dsp::kNoUnit);
  const float mode0 = h.engine().param(voc, Vocoder::Mode);
  (void)h.engine().set_param(voc, Vocoder::Mode, mode0 == 1.0f ? 2.0f : 1.0f);
  h.run_ms(100);
  CHECK(h.leds().level(30) != app::kLedOn);
  (void)h.engine().set_param(voc, Vocoder::Mode, mode0);
  h.run_ms(900);
  CHECK(listen_rms(h, 200) < kSounding);  // a note still held would sound here
  CHECK(h.leds().level(30) != app::kLedOn);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: LEFT and RIGHT stop at slice 1 and at slice 24, and the pick does not wrap or move the start") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  tap(h, app::Button::F1);  // CUT EVEN: 8, 16, 24
  h.turn(2);
  h.run_ms(60);
  float s = 0, l = 0, s23 = 0, l23 = 0;
  REQUIRE(rig.tap_pad(23, s23, l23));  // slice 24, the last, picked
  tap(h, app::Button::Right);
  tap(h, app::Button::Right);
  h.run_ms(2500);
  CHECK(h.leds().level(23) == app::kLedOn);
  CHECK(h.leds().level(22) == app::kLedDim);
  REQUIRE(rig.tap_pad(23, s, l));  // the same slice still
  CHECK(s == doctest::Approx(s23));
  for (int i = 0; i < 30; ++i) {  // past slice 1 and back: held at the first
    tap(h, app::Button::Left);
  }
  h.run_ms(2500);
  CHECK(h.leds().level(0) == app::kLedOn);
  CHECK(h.leds().level(23) == app::kLedDim);
  REQUIRE(rig.tap_pad(0, s, l));
  CHECK(s < 1.0f);
  tap(h, app::Button::Left);
  tap(h, app::Button::Right);  // one step on: slice 2
  h.run_ms(2500);
  CHECK(h.leds().level(1) == app::kLedOn);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: DELETE with nothing picked says NO CUT IN VIEW when the zoom shows none, and NO CUT TO REMOVE with one slice") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s = 0, l = 0;
  REQUIRE(rig.audition(0, s, l));
  for (int i = 0; i < 9; ++i) {  // the deepest zoom: a few hundred samples around the cursor, on cut 1's start; the next cut is far outside
    tap(h, app::Button::Up);
  }
  h.run_ms(100);
  tap(h, app::Button::Delete);
  h.run_ms(60);
  rig.unpick();
  CHECK(cuts_heard(rig) == 8u);  // nothing was taken
  shift_tap(h, app::Button::F8);  // the take's own cuts: cleared at once
  h.run_ms(60);
  tap(h, app::Button::Delete);    // one slice: no cut to take
  h.run_ms(60);
  CHECK(cuts_heard(rig) == 1u);
  CHECK(h.runtime().frame_failures() == 0u);
}

TEST_CASE("sample edit: NORMALISE lets the pick go and the slices still play the same cuts; a retake of the slot drops a pick on it") {
  Rig rig;
  sim::Harness& h = *rig.h;
  rig.take();
  rig.open();
  float s0 = 0, l0 = 0, s = 0, l = 0;
  REQUIRE(rig.tap_pad(2, s0, l0));  // picked
  h.run_ms(2500);
  CHECK(h.leds().level(2) == app::kLedOn);
  tap(h, app::Button::F6);  // an F key: the pick goes, as it does for any of them
  tap(h, app::Button::F1);
  h.run_ms(1200);
  CHECK(best_peak() == doctest::Approx(29204.0f).epsilon(0.01));
  CHECK(h.leds().level(2) != app::kLedOn);  // the samples changed under the picture: nothing stays picked over them
  rig.unpick();
  REQUIRE(rig.audition(2, s, l));  // the cuts are as they were: the normalise rewrites samples, not the table
  CHECK(s == doctest::Approx(s0));
  CHECK(l == doctest::Approx(l0));
  CHECK(cuts_heard(rig) == 8u);
  CHECK(h.runtime().frame_failures() == 0u);
}
