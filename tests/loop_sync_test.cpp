// SPDX-License-Identifier: GPL-3.0-or-later
// LS1: a loop of whole bars stays on the bar (bar_line re-syncs it, without a click), a stopped loop ramps down over kFadeSamples instead of
// cutting, and the Vocoder counts the bars of a take. The tests drive Loop the way Vocoder::run_loops does: per block, the bar line (if one
// falls in it) first, then mix(). Synthetic signals only.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/loop.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>
#include <o3sdk/dsp/transport.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

namespace {

constexpr float kFs = 44100.0f;
constexpr float kPi2 = 6.2831853f;
constexpr std::uint32_t kBar120 = 88200;  // one 4/4 bar at 120 BPM, 44.1 kHz
constexpr std::uint32_t kBlock = 64;
constexpr float kDeclick = 0.08f;  // a 441 Hz sine of 0.5 peak steps 0.031 a sample; a cut between two phases steps up to 1.0
using LF = Vocoder::LoopField;

std::uint32_t bar_samples(unsigned mbpm) { return static_cast<std::uint32_t>(std::lround(240000.0 * 44100.0 / static_cast<double>(mbpm))); }

// A loop of `len` samples of a 441 Hz sine (an integer number of cycles when len is a multiple of 100 samples), with the seam fades baked.
struct Rec {
  std::vector<std::int16_t> mem;
  Loop l;
  explicit Rec(std::uint32_t len, unsigned bars, bool sine = true) : mem(static_cast<std::size_t>(Loop::kCount) * len) {
    l.bind(mem.data(), len);
    l.begin_record(0);
    std::vector<float> x(len);
    for (std::uint32_t i = 0; i < len; ++i) {
      x[i] = sine ? 0.5f * std::sin(kPi2 * 441.0f * static_cast<float>(i) / kFs) : 0.5f;
    }
    for (std::uint32_t p = 0; p < len; p += kBlock) {
      (void)l.record(0, x.data() + p, len - p < kBlock ? len - p : kBlock);
    }
    l.end_record(0);
    if (bars != 0u) {
      l.set_bars(0, bars);
    }
  }
};

// Drives one loop through bar lines `bar` samples apart (a double, test only), rendering kBlock at a time.
struct Drive {
  Loop& l;
  std::vector<float> out;
  std::uint64_t g = 0;
  double line_at = 0.0;
  std::uint32_t k = 0;
  double bar = static_cast<double>(kBar120);
  bool lines = true;
  struct Seen {
    std::uint32_t frame, k, pos, after;
    std::uint64_t at;  // the output index of the block's first sample
  };
  std::vector<Seen> seen;
  explicit Drive(Loop& loop) : l(loop) {}
  void block() {
    std::uint32_t f = kBlock;
    if (lines && static_cast<std::uint64_t>(std::ceil(line_at)) < g + kBlock) {
      f = static_cast<std::uint32_t>(static_cast<std::uint64_t>(std::ceil(line_at)) - g);
      seen.push_back({f, k, l.position(0), 0, g});
      l.bar_line(0, f, k);
      ++k;
      line_at += bar;
    }
    float b[kBlock];
    l.mix(0, b, kBlock, false);
    if (f < kBlock) {
      seen.back().after = l.position(0);
    }
    out.insert(out.end(), b, b + kBlock);
    g += kBlock;
  }
  void bars(unsigned n, double bar_samples_now) {
    bar = bar_samples_now;
    const std::uint32_t target = k + n;
    while (k < target) {
      block();
    }
  }
};

float max_step(const std::vector<float>& v, std::size_t from) {
  float m = 0.0f;
  for (std::size_t i = from + 1; i < v.size(); ++i) {
    m = std::fmax(m, std::fabs(v[i] - v[i - 1]));
  }
  return m;
}

// Half-sample position the cycle puts at bar `j` after the cycle's start (the contract in loop.h), and where it stands `ahead` samples later.
std::uint32_t expect_h(std::uint32_t len, unsigned n, unsigned step, bool rev, std::uint64_t j, std::uint32_t ahead) {
  const std::uint64_t len2 = 2ull * len;
  const std::uint64_t x = (j * step * len + n / 2u) / n % len2;
  const std::uint64_t e = rev ? (2ull * (len - 1u) + len2 - x) % len2 : x;
  const std::uint64_t a = static_cast<std::uint64_t>(ahead) * step % len2;
  return static_cast<std::uint32_t>(rev ? (e + len2 - a) % len2 : (e + a) % len2);
}

}  // namespace

TEST_CASE("loop sync: tempo 120 to 100 to 140, ten bars each: position 0 at every bar line and no click") {
  Rec r(kBar120, 1);
  REQUIRE(r.l.start(0, 0));
  Drive d(r.l);
  d.bars(10, kBar120);
  const std::size_t after_120 = d.seen.size();
  d.bars(10, bar_samples(100'000));
  d.bars(10, bar_samples(140'000));
  d.block();
  REQUIRE(d.seen.size() >= 30);
  for (std::size_t i = 0; i < d.seen.size(); ++i) {
    INFO("bar line " << i << " at frame " << d.seen[i].frame);
    CHECK(d.seen[i].after == kBlock - d.seen[i].frame);  // the cycle restarts at the line's own frame
  }
  CHECK(after_120 >= 10);
  CHECK(max_step(d.out, kBlock * 2) < kDeclick);
}

TEST_CASE("loop sync: at 120 BPM (a bar of a whole number of samples) the output is bit-identical to a loop that is never re-synced") {
  Rec a(kBar120, 1);
  Rec b(kBar120, 0);
  REQUIRE(a.l.start(0, 0));
  REQUIRE(b.l.start(0, 0));
  Drive da(a.l);
  Drive db(b.l);
  da.bars(10, kBar120);
  db.lines = false;
  while (db.g < da.g) {
    db.block();
  }
  REQUIRE(da.out.size() == db.out.size());
  CHECK(std::memcmp(da.out.data(), db.out.data(), da.out.size() * sizeof(float)) == 0);
}

namespace {

// Circular distance in samples between two positions of a loop of `len` samples.
std::uint32_t circ(std::uint32_t a, std::uint32_t b, std::uint32_t len) {
  const std::uint32_t d = a > b ? a - b : b - a;
  return d < len - d ? d : len - d;
}

}  // namespace

TEST_CASE("loop sync: at 123 BPM (a bar of 86048.78 samples) forty bars stay within the tolerance at every line, with no step over the declick threshold") {
  struct Case {
    std::uint32_t len;
    unsigned bars;
  };
  // A take is the bars rounded to whole samples: one bar 86049 (slips 0.22 a cycle), two bars 172098 (rounded up from 172097.56: 0.44 a cycle).
  const Case cases[] = {{86049u, 1u}, {172098u, 2u}, {86048u, 1u}};
  for (const Case& c : cases) {
    INFO("len " << c.len << " bars " << c.bars);
    Rec r(c.len, c.bars);
    REQUIRE(r.l.start(0, 0));
    Drive d(r.l);
    d.bars(40, 240.0 * 44100.0 / 123.0);
    d.block();
    REQUIRE(d.seen.size() >= 40);
    for (std::size_t i = 0; i < d.seen.size(); ++i) {
      // the grid puts the cycle at (k * len / bars) rounded, at the line's own frame
      const std::uint64_t at = (i * static_cast<std::uint64_t>(c.len) + c.bars / 2u) / c.bars % c.len;
      const std::uint32_t want = static_cast<std::uint32_t>((at + kBlock - d.seen[i].frame) % c.len);
      INFO("bar line " << i);
      CHECK(circ(d.seen[i].after, want, c.len) <= 2u);
    }
    CHECK(max_step(d.out, kBlock * 2) < kDeclick);
  }
}

TEST_CASE("loop sync: a slip past the tolerance on the seam is put right with a snap (no ghost under the new position), a few times in sixty bars") {
  Rec r(86049u, 1);
  REQUIRE(r.l.start(0, 0));
  Drive d(r.l);
  d.bars(60, 240.0 * 44100.0 / 123.0);
  d.block();
  // sixty bars at 123 BPM slip 13 samples: the loop is put back at the seam whenever the slip passes 2 samples
  unsigned snaps = 0;
  for (std::size_t i = 1; i < d.seen.size(); ++i) {
    const Drive::Seen& s = d.seen[i];
    const std::uint32_t free_pos = (s.pos + kBlock) % 86049u;  // where a free loop would be a block later
    if (s.after != free_pos) {
      ++snaps;
      // from the line's frame the output is the loop's own samples from the first one, nothing of the old position faded in
      for (std::uint32_t k = 0; k < 32u && s.frame + k < kBlock; ++k) {
        const float want = static_cast<float>(r.mem[k]) * (1.0f / 32768.0f);
        INFO("line " << i << " k " << k);
        CHECK(std::fabs(d.out[s.at + s.frame + k] - want) < 1e-5f);
      }
    }
  }
  CHECK(snaps >= 2u);
  CHECK(snaps <= 8u);
  CHECK(max_step(d.out, kBlock * 2) < kDeclick);
}

TEST_CASE("loop sync: a free loop (bars 0) ignores the bar lines, bit for bit, whatever the tempo") {
  Rec a(kBar120, 0);
  Rec b(kBar120, 0);
  REQUIRE(a.l.start(0, 0));
  REQUIRE(b.l.start(0, 0));
  CHECK(a.l.bars(0) == 0);
  Drive da(a.l);
  Drive db(b.l);
  da.bars(4, bar_samples(100'000));
  da.bars(4, bar_samples(140'000));
  db.lines = false;
  while (db.g < da.g) {
    db.block();
  }
  CHECK(std::memcmp(da.out.data(), db.out.data(), da.out.size() * sizeof(float)) == 0);
}

TEST_CASE("loop sync: a loop playing through a transport stop lands on bar 0 when the transport starts again") {
  Rec r(kBar120, 1);
  REQUIRE(r.l.start(0, 0));
  Drive d(r.l);
  d.bars(3, kBar120);
  d.lines = false;  // the transport is stopped: the loop free-runs for 0.37 bars
  for (unsigned i = 0; i < 520; ++i) {
    d.block();
  }
  CHECK(r.l.playing(0));
  CHECK(r.l.position(0) != 0);
  d.lines = true;
  d.k = 0;  // Start: the transport is back at bar 0, its line in the next block
  d.line_at = static_cast<double>(d.g) + 17.0;
  d.block();
  REQUIRE(d.seen.back().k == 0);
  CHECK(d.seen.back().after == kBlock - 17u);
  d.bars(3, kBar120);
  CHECK(max_step(d.out, kBlock * 2) < kDeclick);
}

TEST_CASE("loop sync: a loop started on bar 3 is on the grid, and a later bar 0 re-phases it") {
  Rec r(kBar120, 1);
  REQUIRE(r.l.start(0, 3));
  Drive d(r.l);
  d.k = 3;
  d.line_at = 0.0;
  d.bars(4, kBar120);
  for (const auto& s : d.seen) {
    CHECK(s.after == kBlock - s.frame);  // no jump needed, the position is the one the grid asks for
  }
  d.k = 0;
  d.line_at = static_cast<double>(d.g) + 5.0;
  d.block();
  CHECK(d.seen.back().after == kBlock - 5u);
}

TEST_CASE("loop sync: half speed, double speed and reverse keep their cycle on the bars") {
  struct Case {
    unsigned speed, bars;
    bool rev;
  };
  for (const Case c : {Case{0, 1, false}, Case{0, 2, false}, Case{2, 1, false}, Case{2, 3, false}, Case{1, 1, true}, Case{0, 1, true}, Case{2, 3, true}}) {
    INFO("speed " << c.speed << " bars " << c.bars << " rev " << c.rev);
    const std::uint32_t len = c.bars * kBar120;
    Rec r(len, c.bars);
    r.l.set_speed(0, c.speed);
    r.l.set_reverse(0, c.rev);
    REQUIRE(r.l.start(0, 0));
    Drive d(r.l);
    d.bars(3, kBar120);
    d.bars(8, bar_samples(100'000));
    d.bars(8, bar_samples(140'000));
    d.block();
    const unsigned step = c.speed == 0 ? 1u : (c.speed == 1 ? 2u : 4u);
    for (const auto& s : d.seen) {
      const std::uint32_t want = expect_h(len, c.bars, step, c.rev, s.k, kBlock - s.frame) >> 1;
      const std::uint32_t got = s.after;  // position() is in samples
      CHECK(got == want);
    }
    CHECK(max_step(d.out, kBlock * 2) < kDeclick * 2.0f);  // 2x doubles the slope of the tone itself
  }
}

TEST_CASE("loop stop: a ramp over kFadeSamples, silent after it, playing() false at once") {
  Rec r(kBar120, 0);
  REQUIRE(r.l.start(0, 0));
  std::vector<float> out;
  float b[kBlock];
  for (unsigned i = 0; i < 40; ++i) {
    r.l.mix(0, b, kBlock, false);
    out.insert(out.end(), b, b + kBlock);
  }
  // stop at a sample where the sine is far from zero: the old cut jumped by its full value
  r.l.stop(0);
  CHECK_FALSE(r.l.playing(0));
  CHECK(r.l.sounding(0));
  const std::size_t at = out.size();
  const float last = out.back();
  r.l.mix(0, b, kBlock, false);
  out.insert(out.end(), b, b + kBlock);
  CHECK(std::fabs(b[0] - last) < kDeclick);
  CHECK(max_step(out, at - 8) < kDeclick);
  CHECK(std::fabs(b[kBlock - 1]) < 0.01f);  // weight 1/64 of a 0.5 peak
  CHECK(r.l.sounding(0) == false);
  for (float& v : b) v = 0.0f;  // a silent loop does not write dst
  r.l.mix(0, b, kBlock, false);
  for (std::uint32_t k = 0; k < kBlock; ++k) {
    CHECK(b[k] == 0.0f);
  }
  // an add mix of a stopped loop leaves dst alone
  float c[kBlock];
  for (float& v : c) v = 0.25f;
  r.l.mix(0, c, kBlock, true);
  CHECK(c[10] == 0.25f);
}

TEST_CASE("loop stop: a start during the ramp plays, and the tail fades out independently") {
  Rec r(kBar120, 0);
  REQUIRE(r.l.start(0, 0));
  std::vector<float> out;
  float b[kBlock];
  for (unsigned i = 0; i < 40; ++i) {
    r.l.mix(0, b, kBlock, false);
    out.insert(out.end(), b, b + kBlock);
  }
  r.l.stop(0);
  float half[kBlock / 2];
  r.l.mix(0, half, kBlock / 2, false);  // half of the ramp
  out.insert(out.end(), half, half + kBlock / 2);
  REQUIRE(r.l.start(0, 0));
  CHECK(r.l.playing(0));
  for (unsigned i = 0; i < 6; ++i) {
    r.l.mix(0, b, kBlock, false);
    out.insert(out.end(), b, b + kBlock);
  }
  CHECK(r.l.playing(0));
  CHECK(r.l.position(0) == 6u * kBlock);
  CHECK(max_step(out, 40u * kBlock - 8u) < kDeclick);
  CHECK(std::fabs(out[out.size() - 1]) > 0.0f);
}

TEST_CASE("loop stop: stopping a loop that was never playing does nothing") {
  Rec r(kBar120, 0);
  r.l.stop(0);
  CHECK_FALSE(r.l.sounding(0));
  float b[kBlock] = {};
  r.l.mix(0, b, kBlock, false);
  CHECK(b[5] == 0.0f);
}

namespace {

dsp::Transport playing_at(std::uint64_t tick, unsigned mbpm = 120'000) {
  dsp::Transport t;
  t.playing = true;
  t.tempo_mbpm = mbpm;
  t.inc = dsp::ticks_per_sample_q32(mbpm);
  t.inc_lo = dsp::ticks_per_sample_lo(mbpm);
  t.pos = tick << 32;
  t.frames = kBlock;
  return t;
}

void set_tempo(dsp::Transport& t, unsigned mbpm) {
  t.tempo_mbpm = mbpm;
  t.inc = dsp::ticks_per_sample_q32(mbpm);
  t.inc_lo = dsp::ticks_per_sample_lo(mbpm);
}

struct Rig {
  Vocoder v;
  std::vector<std::int16_t> mem;
  dsp::Transport tr;
  std::uint32_t at = 0;
  explicit Rig(std::uint32_t per_loop) : mem(static_cast<std::size_t>(Loop::kCount) * per_loop) {
    v.bind_loops(mem.data(), per_loop);
    v.prepare(kFs);
  }
  std::vector<float> block(const float* in, std::uint32_t n, std::vector<dsp::Event> ev = {}) {
    std::vector<float> out(n), r(n), zero(n, 0.0f);
    dsp::ProcessArgs a{};
    a.events = ev.data();
    a.event_count = static_cast<std::uint32_t>(ev.size());
    a.in_l = in != nullptr ? in : zero.data();
    a.in_r = a.in_l;
    a.out_l = out.data();
    a.out_r = r.data();
    a.frames = n;
    tr.frames = n;
    a.transport = &tr;
    v.process(a);
    if (tr.playing) {
      tr.advance(n);
    }
    at += n;
    return out;
  }
};

dsp::Event rec(unsigned loop, bool on) { return dsp::param_change(Vocoder::loop_param(loop, LF::Rec), on ? 1.0f : 0.0f); }
dsp::Event play(unsigned loop, bool on) { return dsp::param_change(Vocoder::loop_param(loop, LF::Play), on ? 1.0f : 0.0f); }
dsp::Event load(unsigned slot, std::uint32_t len) { return dsp::param_change(Vocoder::Load, static_cast<float>(slot * Loop::kMaxSamples + len)); }

// The input of a take: a sine, 441 Hz.
std::vector<float> tone(std::size_t n) {
  std::vector<float> x(n);
  for (std::size_t i = 0; i < n; ++i) {
    x[i] = 0.4f * std::sin(kPi2 * 441.0f * static_cast<float>(i) / kFs);
  }
  return x;
}

// Records `bars` bars into loop 0 from bar line 1 of a transport that plays from tick 3840 - 960; stops on the line `bars` later. `pause_at`: the
// block index (from the start of the run) in which the transport is held stopped for one block (it still moves on), or -1.
void take(Rig& rig, unsigned bars, int pause_at = -1) {
  const std::vector<float> x = tone(static_cast<std::size_t>(bars + 2) * kBar120);
  rig.tr = playing_at(3840u - 960u);
  constexpr std::uint32_t kFirstLine = kBar120 / 4u;  // 960 ticks (a quarter note) at 120 BPM: 22050 samples
  const std::uint32_t total = kFirstLine + bars * kBar120 + 4u * kBlock;
  bool stop_sent = false;
  for (std::uint32_t p = 0, bi = 0; p + kBlock <= total; p += kBlock, ++bi) {
    std::vector<dsp::Event> ev;
    if (p == 0) {
      ev.push_back(rec(0, true));
    }
    if (!stop_sent && p >= kFirstLine + (bars - 1u) * kBar120 + kBar120 / 2u) {  // half way into the last bar
      ev.push_back(rec(0, false));
      stop_sent = true;
    }
    const bool hold = pause_at >= 0 && static_cast<int>(bi) == pause_at;
    if (hold) {
      rig.tr.playing = false;
    }
    rig.block(x.data() + p, kBlock, ev);
    if (hold) {
      rig.tr.playing = true;
      rig.tr.advance(kBlock);
    }
  }
}

}  // namespace

TEST_CASE("vocoder loop sync: a take counts its bars, a take with the transport stopped or interrupted is free") {
  {
    Rig rig(Loop::kMaxSamples);
    take(rig, 1);
    CHECK(rig.v.loops().length(0) == kBar120);
    CHECK(rig.v.loops().bars(0) == 1);
  }
  {
    Rig rig(Loop::kMaxSamples);
    take(rig, 2);
    CHECK(rig.v.loops().length(0) == 2 * kBar120);
    CHECK(rig.v.loops().bars(0) == 2);
  }
  {
    Rig rig(Loop::kMaxSamples);
    take(rig, 1, 700);  // the transport stops for a block mid-take: its length is no longer a count of lines
    CHECK(rig.v.loops().length(0) == kBar120);
    CHECK(rig.v.loops().bars(0) == 0);
  }
  {
    Rig rig(Loop::kMaxSamples);  // armed and stopped with the transport stopped: free
    const std::vector<float> x = tone(kBar120);
    rig.block(x.data(), kBlock, {rec(0, true)});
    for (std::uint32_t p = kBlock; p < 20000; p += kBlock) {
      rig.block(x.data() + p, kBlock);
    }
    rig.block(nullptr, kBlock, {rec(0, false)});
    CHECK(rig.v.loops().length(0) != 0);
    CHECK(rig.v.loops().bars(0) == 0);
  }
}

TEST_CASE("vocoder loop sync: a take that fills the buffer is free, unless the stop line is the very sample that fills it") {
  {
    Rig rig(Loop::kMaxSamples);  // 4 bars at 60 BPM are exactly 16 s: the stop line fills the buffer
    rig.tr = playing_at(3840u - 960u, 60'000);
    const std::vector<float> x = tone(bar_samples(60'000) / 4u + Loop::kMaxSamples + 8u * kBlock);  // the take starts at the first line, a quarter bar in
    const std::uint32_t bar = bar_samples(60'000);
    REQUIRE(bar * 4u == Loop::kMaxSamples);
    bool stop_sent = false;
    for (std::uint32_t p = 0; p + kBlock <= x.size(); p += kBlock) {
      std::vector<dsp::Event> ev;
      if (p == 0) ev.push_back(rec(0, true));
      if (!stop_sent && p > bar / 4u + 3u * bar + bar / 2u) {
        ev.push_back(rec(0, false));
        stop_sent = true;
      }
      rig.block(x.data() + p, kBlock, ev);
    }
    CHECK(rig.v.loops().length(0) == Loop::kMaxSamples);
    CHECK(rig.v.loops().bars(0) == 4);
  }
  {
    Rig rig(Loop::kMaxSamples);  // never stopped: the buffer fills mid-bar at 70 BPM
    rig.tr = playing_at(3840u - 960u, 70'000);
    const std::vector<float> x = tone(bar_samples(70'000) / 4u + Loop::kMaxSamples + 8u * kBlock);  // the take starts at the first line, a quarter bar in
    for (std::uint32_t p = 0; p + kBlock <= x.size(); p += kBlock) {
      rig.block(x.data() + p, kBlock, p == 0 ? std::vector<dsp::Event>{rec(0, true)} : std::vector<dsp::Event>{});
    }
    CHECK(rig.v.loops().length(0) == Loop::kMaxSamples);
    CHECK(rig.v.loops().bars(0) == 0);
  }
}

TEST_CASE("vocoder loop sync: Load of exactly whole bars at the tempo of the transport is on the grid, anything else is free") {
  Rig rig(Loop::kMaxSamples);
  rig.mem[0] = 3000;
  rig.block(nullptr, kBlock, {load(0, kBar120)});
  CHECK(rig.v.loops().bars(0) == 1);
  rig.block(nullptr, kBlock, {load(0, 2 * kBar120 + 2)});
  CHECK(rig.v.loops().bars(0) == 2);
  rig.block(nullptr, kBlock, {load(0, kBar120 + 10)});
  CHECK(rig.v.loops().bars(0) == 0);
  rig.block(nullptr, kBlock, {load(0, kBar120 / 2)});
  CHECK(rig.v.loops().bars(0) == 0);
  rig.block(nullptr, kBlock, {load(0, 1000)});
  CHECK(rig.v.loops().bars(0) == 0);
  rig.tr = playing_at(0, 100'000);  // the transport tempo is what the next Load is judged against
  rig.block(nullptr, kBlock);
  rig.block(nullptr, kBlock, {load(0, bar_samples(100'000))});
  CHECK(rig.v.loops().bars(0) == 1);
  rig.block(nullptr, kBlock, {load(0, kBar120)});
  CHECK(rig.v.loops().bars(0) == 0);
}

TEST_CASE("vocoder loop sync: an adopt that keeps the length keeps the bar count, at any tempo (a resent Load, an edit)") {
  Rig rig(Loop::kMaxSamples);
  rig.mem[0] = 3000;
  rig.block(nullptr, kBlock, {load(0, 2 * kBar120)});
  REQUIRE(rig.v.loops().bars(0) == 2);
  rig.tr = playing_at(0, 100'000);  // 176400 samples are 1.67 bars of 100 BPM: judged there it would be free
  rig.block(nullptr, kBlock);
  rig.block(nullptr, kBlock, {load(0, 2 * kBar120)});
  CHECK(rig.v.loops().bars(0) == 2);
  rig.block(nullptr, kBlock, {load(0, 2 * kBar120)});
  CHECK(rig.v.loops().bars(0) == 2);
}

TEST_CASE("vocoder loop sync: a copy to another slot carries the bar count of the source, at the tempo of the moment too") {
  Rig rig(Loop::kMaxSamples);
  rig.mem[0] = 3000;
  rig.block(nullptr, kBlock, {load(0, 2 * kBar120)});
  REQUIRE(rig.v.loops().bars(0) == 2);
  rig.tr = playing_at(0, 100'000);
  rig.block(nullptr, kBlock);
  rig.block(nullptr, kBlock, {load(1, 2 * kBar120)});  // what the SAMPLE EDIT copy sends once the samples are in the target
  CHECK(rig.v.loops().bars(1) == 2);
  // a length no slot has and that is not whole at this tempo stays free
  rig.block(nullptr, kBlock, {load(2, 2 * kBar120 + 5000)});
  CHECK(rig.v.loops().bars(2) == 0);
}

TEST_CASE("vocoder loop sync: a trim to a whole number of the loop's own bars keeps that many, any other length is judged at the tempo") {
  Rig rig(Loop::kMaxSamples);
  rig.mem[0] = 3000;
  rig.block(nullptr, kBlock, {load(0, 4 * kBar120)});
  REQUIRE(rig.v.loops().bars(0) == 4);
  rig.tr = playing_at(0, 100'000);
  rig.block(nullptr, kBlock);
  rig.block(nullptr, kBlock, {load(0, 2 * kBar120)});  // half of it: 2 bars of the take, 1.67 of the tempo of the moment
  CHECK(rig.v.loops().bars(0) == 2);
  rig.block(nullptr, kBlock, {load(0, bar_samples(100'000))});  // a bar of today's tempo
  CHECK(rig.v.loops().bars(0) == 1);
  rig.block(nullptr, kBlock, {load(0, kBar120 + 1000)});  // neither
  CHECK(rig.v.loops().bars(0) == 0);
}

TEST_CASE("vocoder loop sync: a WAV load is judged at the tempo of the moment whatever the slot held before (a project reload sets its tempo first)") {
  const std::uint32_t wav = 2 * bar_samples(90'000);
  std::uint16_t got[2];
  for (unsigned history = 0; history < 2; ++history) {
    Rig rig(Loop::kMaxSamples);
    rig.mem[0] = 3000;
    if (history == 1) {  // an older take in the slot and in another one, judged at another tempo
      rig.block(nullptr, kBlock, {load(2, kBar120)});
      rig.block(nullptr, kBlock, {load(3, 3 * kBar120)});
    }
    rig.tr = playing_at(0, 90'000);  // the project's tempo, applied before the WAVs
    rig.block(nullptr, kBlock);
    rig.block(nullptr, kBlock, {load(2, wav)});  // the loader empties the slot first (length 0), then loads
    got[history] = static_cast<std::uint16_t>(rig.v.loops().bars(2));
  }
  CHECK(got[0] == 2);
  CHECK(got[1] == 2);
}

TEST_CASE("vocoder loop sync: through the unit, the loop restarts at each bar line across tempo changes and a transport stop") {
  Rig rig(Loop::kMaxSamples);
  take(rig, 1);
  REQUIRE(rig.v.loops().bars(0) == 1);
  rig.tr = playing_at(3840u * 4u - 960u);
  rig.block(nullptr, kBlock, {play(0, true)});
  const auto run_bars = [&](unsigned bars, unsigned mbpm, bool expect_sync) {
    set_tempo(rig.tr, mbpm);
    unsigned seen = 0;
    while (seen < bars) {
      const std::uint64_t before = rig.tr.pos >> 32;
      const dsp::Transport t0 = rig.tr;
      rig.block(nullptr, kBlock);
      const std::uint64_t after = rig.tr.pos >> 32;
      if (before / 3840u != after / 3840u || (after % 3840u) < (before % 3840u)) {
        // a bar line fell in this block: find its frame with the transport's own oracle
        const std::uint64_t line = (after / 3840u) * 3840u;
        std::uint32_t f = 0;
        while (f < kBlock && t0.at(f) < (line << 32)) {
          ++f;
        }
        if (f < kBlock && expect_sync) {
          INFO("bar " << line / 3840u << " at " << mbpm << " mbpm, frame " << f);
          CHECK(rig.v.loops().position(0) == kBlock - f);
        }
        ++seen;
      }
    }
  };
  run_bars(3, 120'000, true);
  run_bars(10, 100'000, true);
  run_bars(10, 140'000, true);
  // transport Stop: the loop sounds on; Start from 0 puts it back at its first sample on the line
  rig.tr.playing = false;
  for (unsigned i = 0; i < 300; ++i) {
    rig.block(nullptr, kBlock);
  }
  CHECK(rig.v.loop_status(0).playing);
  CHECK(rig.v.loops().position(0) != 0);
  set_tempo(rig.tr, 120'000);
  rig.tr.playing = true;
  rig.tr.pos = 0;
  rig.block(nullptr, kBlock);
  CHECK(rig.v.loops().position(0) == kBlock);
}

TEST_CASE("vocoder loop sync: a loop under a region through a transport restart is re-anchored at bar 0, not by the bar it started on") {
  Rig rig(Loop::kMaxSamples);
  take(rig, 2);
  REQUIRE(rig.v.loops().bars(0) == 2);
  rig.tr = playing_at(3840u * 3u - 960u);  // the loop starts on bar 3 (odd): its anchor is 3
  rig.block(nullptr, kBlock, {play(0, true)});
  for (unsigned i = 0; i < 40; ++i) {
    rig.block(nullptr, kBlock);
  }
  // a region over the loop (1.45 s), and the transport stops and starts again from 0 while it sounds: the bar-0 line passes with the loop paused
  rig.block(nullptr, kBlock,
            {dsp::param_change(Vocoder::R1Start, 0.0f), dsp::param_change(Vocoder::R1Len, 64000.0f), dsp::param_change(Vocoder::R1Trig, 1.0f)});
  REQUIRE(rig.v.loops().region(0));
  rig.tr.playing = false;
  for (unsigned i = 0; i < 100; ++i) {
    rig.block(nullptr, kBlock);
  }
  REQUIRE(rig.v.loops().region(0));
  rig.tr.playing = true;
  rig.tr.pos = 0;
  bool line1 = false;
  for (unsigned guard = 0; guard < 2000 && !line1; ++guard) {
    const dsp::Transport t0 = rig.tr;
    rig.block(nullptr, kBlock);
    if ((rig.tr.pos >> 32) >= 3840u && (t0.pos >> 32) < 3840u) {  // bar 1 falls in this block
      std::uint32_t f = 0;
      while (f < kBlock && t0.at(f) < (static_cast<std::uint64_t>(3840u) << 32)) {
        ++f;
      }
      REQUIRE(f < kBlock);
      REQUIRE_FALSE(rig.v.loops().region(0));
      // anchored at 0 the cycle is half way round at bar 1 (two bars: j = 1); anchored at the stale 3 it would be at its first sample
      CHECK(rig.v.loops().position(0) == kBar120 + kBlock - f);
      line1 = true;
    }
  }
  CHECK(line1);
}

TEST_CASE("vocoder loop sync: at a constant tempo the unit's output is bit-identical to the same take left free") {
  Rig a(Loop::kMaxSamples);
  Rig b(Loop::kMaxSamples);
  take(a, 1);
  take(b, 1, 700);  // the same samples, but a transport that stopped for a block: free
  REQUIRE(a.v.loops().bars(0) == 1);
  REQUIRE(b.v.loops().bars(0) == 0);
  REQUIRE(std::memcmp(a.mem.data(), b.mem.data(), kBar120 * sizeof(std::int16_t)) == 0);
  for (Rig* r : {&a, &b}) {
    r->tr = playing_at(3840u * 4u - 960u);
    r->block(nullptr, kBlock, {play(0, true)});
  }
  for (std::uint32_t i = 0; i < 10u * kBar120 / kBlock; ++i) {
    const std::vector<float> oa = a.block(nullptr, kBlock);
    const std::vector<float> ob = b.block(nullptr, kBlock);
    REQUIRE(std::memcmp(oa.data(), ob.data(), oa.size() * sizeof(float)) == 0);
  }
}

TEST_CASE("vocoder loop stop: Play off ramps the loop out instead of cutting it, and sounding() carries it") {
  Rig rig(Loop::kMaxSamples);
  take(rig, 1);
  rig.tr = playing_at(3840u * 4u - 960u);
  rig.block(nullptr, kBlock, {play(0, true)});
  for (std::uint32_t i = 0; i < 2u * kBar120 / kBlock; ++i) {
    rig.block(nullptr, kBlock);
  }
  REQUIRE(rig.v.loop_status(0).playing);
  rig.block(nullptr, kBlock, {play(0, false)});
  CHECK_FALSE(rig.v.loop_status(0).playing);
  CHECK(rig.v.loops().sounding(0) == false);  // the tail is one block of 64 and it has just been rendered
}

TEST_CASE("loop stop tail: non-zero right after stop, exactly zero after kFadeSamples, no step over the declick threshold, at every speed, both ways, replace and add") {
  // Both roles reach Loop::play: a MOD loop replaces the modulator, a CAR loop adds into the carrier (or into a scratch buffer under the carrier's
  // fade), so the replace and add paths here are the two roles' tails; the carrier's own gain ramp multiplies a continuous signal.
  for (const unsigned speed : {0u, 1u, 2u}) {
    for (const bool rev : {false, true}) {
      for (const bool add : {false, true}) {
        INFO("speed " << speed << " reverse " << rev << " add " << add);
        Rec r(44100u, 0);
        r.l.set_speed(0, speed);
        r.l.set_reverse(0, rev);
        REQUIRE(r.l.start(0));
        std::vector<float> out;
        for (unsigned b = 0; b < 40; ++b) {  // settle, and stop at an odd place
          float x[kBlock] = {};
          r.l.play(0, x, kBlock, add);
          out.insert(out.end(), x, x + kBlock);
        }
        r.l.stop(0);
        CHECK_FALSE(r.l.playing(0));
        CHECK(r.l.sounding(0));
        float tail[2 * Loop::kFadeSamples] = {};
        r.l.play(0, tail, 2 * Loop::kFadeSamples, add);
        CHECK(tail[0] != 0.0f);  // right after stop the loop is still sounding
        bool any = false;
        for (std::uint32_t k = 0; k < Loop::kFadeSamples; ++k) {
          any = any || tail[k] != 0.0f;
        }
        CHECK(any);
        for (std::uint32_t k = Loop::kFadeSamples; k < 2 * Loop::kFadeSamples; ++k) {
          CHECK(tail[k] == 0.0f);  // ramped out: silent and untouched
        }
        CHECK_FALSE(r.l.sounding(0));
        out.insert(out.end(), tail, tail + 2 * Loop::kFadeSamples);
        CHECK(max_step(out, 0) < kDeclick);
      }
    }
  }
}

TEST_CASE("take outcome: a tempo that falls late fills the buffer before the next bar line: the take is free, not on the bar, and the status says so") {
  Rig rig(Loop::kMaxSamples);
  rig.tr = playing_at(3840u - 960u);  // 120 BPM: a bar is 2 s
  const std::vector<float> x = tone(static_cast<std::size_t>(Loop::kMaxSamples) + 2u * kBar120);
  const std::uint32_t first_line = kBar120 / 4u;
  bool slowed = false;
  for (std::uint32_t p = 0; p + kBlock <= x.size(); p += kBlock) {
    std::vector<dsp::Event> ev;
    if (p == 0) {
      ev.push_back(rec(0, true));
    }
    if (!slowed && p >= first_line + 3u * kBar120 + kBar120 / 20u) {  // 5 % into the fourth bar: 6.1 s recorded
      set_tempo(rig.tr, 20'000);  // the rest of the bar is 11.4 s at 20 BPM: the 16 s buffer fills first
      ev.push_back(rec(0, false));  // the screen stops the take as if REC were pressed
      slowed = true;
    }
    rig.block(x.data() + p, kBlock, ev);
  }
  REQUIRE(slowed);
  CHECK(rig.v.loops().length(0) == Loop::kMaxSamples);
  CHECK(rig.v.loops().bars(0) == 0);
  CHECK_FALSE(rig.v.loop_status(0).on_bar);
  CHECK(rig.v.loop_status(0).rec == Vocoder::LoopStatus::Rec::Idle);
}

TEST_CASE("take outcome: a tempo that falls early enough ends the take on the next bar line, whole bars, and the status says on the bar") {
  Rig rig(Loop::kMaxSamples);
  rig.tr = playing_at(3840u - 960u);
  const std::vector<float> x = tone(static_cast<std::size_t>(Loop::kMaxSamples) + 2u * kBar120);
  const std::uint32_t first_line = kBar120 / 4u;
  bool slowed = false;
  for (std::uint32_t p = 0; p + kBlock <= x.size(); p += kBlock) {
    std::vector<dsp::Event> ev;
    if (p == 0) {
      ev.push_back(rec(0, true));
    }
    if (!slowed && p >= first_line + 1u * kBar120 + kBar120 / 2u) {  // half way into the second bar: 3 s recorded
      set_tempo(rig.tr, 40'000);  // the rest of the bar is 3 s at 40 BPM: the line comes at 6 s
      ev.push_back(rec(0, false));
      slowed = true;
    }
    rig.block(x.data() + p, kBlock, ev);
  }
  REQUIRE(slowed);
  CHECK(rig.v.loops().bars(0) == 2);
  CHECK(rig.v.loop_status(0).on_bar);
  CHECK(rig.v.loops().length(0) < Loop::kMaxSamples);
}
