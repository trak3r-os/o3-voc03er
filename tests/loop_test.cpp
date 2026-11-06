// SPDX-License-Identifier: GPL-3.0-or-later
// Loop (the bank of recorded loops) and the Vocoder's loop control: recording from the bar lines, the seam, the roles, a full loop,
// the transport stopped. Synthetic signals only.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/loop.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>
#include <o3sdk/dsp/transport.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

namespace {

constexpr float kFs = 44100.0f;
constexpr float kPi2 = 6.2831853f;
using LF = Vocoder::LoopField;

static_assert(Vocoder::Freeze == 9 && Vocoder::Loop1Rec == 10 && Vocoder::Loop1Speed == 22 && Vocoder::CarSrc == 34 && Vocoder::NotesIn == 39 && Vocoder::Bend == 42 && Vocoder::Vib == 43 && Vocoder::Glide == 44 && Vocoder::Gate == 45 && Vocoder::Arp == 46 && Vocoder::kParamCount == 47, "ids are append-only: the first ten never move");
static_assert(Vocoder::loop_param(3, LF::Level) == 33 && Vocoder::loop_param(1, LF::Dir) == 26 && Vocoder::loop_param(3, LF::Role) == 21, "four loops of three parameters from id 10");

// A transport at 120 BPM, playing, `tick` ticks into the run (so the bar lines are at multiples of 3840).
dsp::Transport playing_at(std::uint64_t tick) {
  dsp::Transport t;
  t.playing = true;
  t.tempo_mbpm = 120'000;
  t.inc = dsp::ticks_per_sample_q32(120'000);
  t.inc_lo = dsp::ticks_per_sample_lo(120'000);
  t.pos = tick << 32;
  t.frames = 64;
  return t;
}

// The first sample (counted from `t0`'s block start) whose position reaches `tick`: an oracle that walks Transport::at, not frame_at.
std::uint32_t first_sample_at(const dsp::Transport& t0, std::uint64_t tick) {
  for (std::uint32_t g = 0;; ++g) {
    if (t0.at(g) >= (tick << 32)) {
      return g;
    }
  }
}

struct Rig {
  Vocoder v;
  std::vector<std::int16_t> mem;
  dsp::Transport tr;
  std::uint32_t at = 0;  // samples run so far
  explicit Rig(std::uint32_t per_loop) : mem(static_cast<std::size_t>(Loop::kCount) * per_loop) {
    v.bind_loops(mem.data(), per_loop);
    v.prepare(kFs);
  }
  // One block of `n` frames from `in` (nullptr: silence) with `ev`; returns the output.
  std::vector<float> block(const float* in, std::uint32_t n, std::vector<dsp::Event> ev = {}) {
    std::vector<float> out(n), r(n);
    std::vector<float> zero(n, 0.0f);
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
dsp::Event role(unsigned loop, bool car) { return dsp::param_change(Vocoder::loop_param(loop, LF::Role), car ? 1.0f : 0.0f); }

float rms(const std::vector<float>& v, std::size_t from, std::size_t to) {
  float s = 0.0f;
  for (std::size_t i = from; i < to; ++i) {
    s += v[i] * v[i];
  }
  return std::sqrt(s / static_cast<float>(to - from));
}

float tone_energy(const std::vector<float>& v, std::size_t from, std::size_t to, float hz) {
  const float c = 2.0f * std::cos(kPi2 * hz / kFs);
  float s1 = 0.0f, s2 = 0.0f;
  for (std::size_t i = from; i < to; ++i) {
    const float s = v[i] + c * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  const float n = static_cast<float>(to - from);
  return (s1 * s1 + s2 * s2 - c * s1 * s2) / (n * n);
}

// A tone made of a few partials, 0.3 peak-ish, for a modulator or a carrier.
std::vector<float> rich(std::size_t n, float f0) {
  std::vector<float> x(n);
  for (std::size_t i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / kFs;
    x[i] = 0.12f * (std::sin(kPi2 * f0 * t) + std::sin(kPi2 * 3.0f * f0 * t) + std::sin(kPi2 * 7.0f * f0 * t));
  }
  return x;
}

// Records `x` (as many blocks as it holds, transport stopped) into loop `i`, then stops the recording.
void record_loop(Rig& rig, unsigned i, const std::vector<float>& x) {
  rig.block(x.data(), 64, {rec(i, true)});
  for (std::size_t p = 64; p < x.size(); p += 64) {
    rig.block(x.data() + p, 64);
  }
  rig.block(nullptr, 64, {rec(i, false)});
}

}  // namespace

TEST_CASE("loop: the seam is silent, the length is exact, playback repeats sample for sample") {
  std::vector<std::int16_t> mem(Loop::kCount * 2000u);
  Loop l;
  l.bind(mem.data(), 2000);
  std::vector<float> in(1000, 0.5f);
  l.begin_record(0);
  CHECK(l.record(0, in.data(), 1000) == 1000);
  l.end_record(0);
  CHECK(l.length(0) == 1000);
  CHECK(mem[0] == 0);
  CHECK(mem[999] == 0);
  CHECK(mem[500] == 16384);
  REQUIRE(l.start(0));
  std::vector<float> out(2500);
  l.mix(0, out.data(), 700, false);  // uneven chunks: the wrap lands inside one
  l.mix(0, out.data() + 700, 1800, false);
  float max_step = 0.0f;
  for (std::size_t k = 1; k < out.size(); ++k) {
    max_step = std::fmax(max_step, std::fabs(out[k] - out[k - 1]));
  }
  CHECK(max_step < 0.5f / 60.0f);  // the fade's step is 0.5 / 64; a bare seam would jump by 0.5
  for (std::size_t k = 0; k + 1000 < out.size(); ++k) {
    CHECK(out[k] == out[k + 1000]);
  }
  CHECK(l.position(0) == 500);
  // add mode sums, a stopped loop is silent and untouched
  std::vector<float> acc(8, 1.0f);
  l.mix(0, acc.data(), 8, true);
  CHECK(acc[0] > 0.99f);
  l.stop(0);
  l.mix(0, acc.data(), 8, false);
  CHECK(acc[7] == doctest::Approx(acc[7]));
  CHECK(acc[0] > 0.99f);
}

TEST_CASE("loop: a full loop ends itself, unbound and empty loops do nothing") {
  std::vector<std::int16_t> mem(Loop::kCount * 100u);
  Loop l;
  CHECK_FALSE(l.start(0));
  l.bind(mem.data(), 100);
  CHECK_FALSE(l.start(0));  // empty
  std::vector<float> in(64, 0.25f);
  l.begin_record(1);
  CHECK(l.record(1, in.data(), 64) == 64);
  CHECK(l.recording(1));
  CHECK(l.record(1, in.data(), 64) == 36);  // only the room left
  CHECK_FALSE(l.recording(1));
  CHECK(l.length(1) == 100);
  CHECK(l.record(1, in.data(), 64) == 0);
  CHECK(l.start(1));
  CHECK(Loop::kBankSamples * 2 == 5'644'800);
  l.bind(mem.data(), 1u << 30);  // clamped to 16 s
  CHECK(l.capacity() == Loop::kMaxSamples);
}

TEST_CASE("vocoder loops: recording starts and stops on the bar lines, sample-exact") {
  Rig rig(Loop::kMaxSamples);
  // 960 ticks (a quarter, 22,050 samples) before bar line 3 (tick 11,520): bar lines at samples 22,050 and 110,250.
  rig.tr = playing_at(3840u * 3u - 960u);
  const std::uint32_t g1 = first_sample_at(rig.tr, 3840u * 3u);
  const std::uint32_t g2 = first_sample_at(rig.tr, 3840u * 4u);
  REQUIRE(g1 == 22050);
  REQUIRE(g2 == 110250);
  // The input is 0.5 with a marker of 1.0 a hundred samples after the first bar line and a hundred before the second.
  std::vector<float> x(130000, 0.5f);
  x[g1 + 100] = 1.0f;
  x[g2 - 101] = 1.0f;
  bool saw_armed = false;
  for (std::uint32_t p = 0; p + 64 <= x.size(); p += 64) {
    std::vector<dsp::Event> ev;
    if (p == 0) {
      ev.push_back(rec(0, true));
    }
    if (p == 64u * 700u) {  // between the two bar lines: stop asked, the loop runs on to the line
      ev.push_back(rec(0, false));
    }
    rig.block(x.data() + p, 64, ev);
    if (p + 64 <= g1) {
      CHECK(rig.v.loops().length(0) == 0);  // armed, nothing taken before the line
      saw_armed = saw_armed || rig.v.loop_status(0).rec == Vocoder::LoopStatus::Rec::Armed;
    }
    if (p > 64u * 700u && p + 64 <= g2) {
      CHECK(rig.v.loop_status(0).rec == Vocoder::LoopStatus::Rec::Stopping);
    }
  }
  CHECK(saw_armed);
  const std::uint32_t len = rig.v.loops().length(0);
  CHECK(len == g2 - g1);
  CHECK(len == 88200);
  CHECK(rig.v.loop_status(0).rec == Vocoder::LoopStatus::Rec::Idle);
  CHECK(rig.mem[100] == 32767);
  CHECK(rig.mem[99] == 16384);  // past the 64-sample fade: full 0.5
  CHECK(rig.mem[len - 101] == 32767);
  CHECK(rig.mem[len - 1] == 0);
  CHECK(rig.mem[0] == 0);
}

TEST_CASE("vocoder loops: with the transport stopped arm and stop act at once") {
  Rig rig(Loop::kMaxSamples);
  CHECK_FALSE(rig.tr.playing);
  std::vector<float> x(64 * 8, 0.4f);
  rig.block(x.data(), 64, {rec(2, true)});
  CHECK(rig.v.loops().length(2) == 64);  // started at frame 0 of the block it was asked in
  CHECK(rig.v.loop_status(2).rec == Vocoder::LoopStatus::Rec::Recording);
  for (int b = 1; b < 5; ++b) {
    rig.block(x.data(), 64);
  }
  rig.block(x.data(), 64, {rec(2, false)});  // stops at frame 0: this block's samples are not taken
  CHECK(rig.v.loops().length(2) == 64u * 5u);
  CHECK(rig.v.loop_status(2).rec == Vocoder::LoopStatus::Rec::Idle);
  CHECK(rig.v.loop_status(2).length == 320);
  // an event at frame 40 starts there
  rig.block(x.data(), 64, {dsp::param_change(Vocoder::loop_param(0, LF::Rec), 1.0f, 40)});
  CHECK(rig.v.loops().length(0) == 24);
}

TEST_CASE("vocoder loops: a full loop stops itself and arming again records afresh") {
  Rig rig(1000);
  std::vector<float> x(64, 0.3f);
  rig.block(x.data(), 64, {rec(0, true)});
  for (int b = 0; b < 20; ++b) {
    rig.block(x.data(), 64);
  }
  CHECK(rig.v.loops().length(0) == 1000);
  CHECK(rig.v.loop_status(0).rec == Vocoder::LoopStatus::Rec::Idle);
  rig.block(x.data(), 64, {rec(0, false), rec(0, true)});
  CHECK(rig.v.loops().length(0) == 64);  // emptied and recording again
}

TEST_CASE("vocoder loops: a loop plays from the next bar line") {
  Rig rig(Loop::kMaxSamples);
  std::vector<float> x(640, 0.5f);
  record_loop(rig, 0, x);
  REQUIRE(rig.v.loops().length(0) == 640);
  rig.tr = playing_at(3840u * 3u - 960u);
  const std::uint32_t g1 = first_sample_at(rig.tr, 3840u * 3u);
  rig.block(nullptr, 64, {play(0, true)});
  CHECK(rig.v.loop_status(0).pending);
  CHECK_FALSE(rig.v.loop_status(0).playing);
  const std::uint32_t start = rig.at - 64;  // the play block's first sample: the transport began there
  while (rig.at - start <= g1 + 64) {
    rig.block(nullptr, 64);
  }
  // the block that holds g1 started the loop at its frame: after the blocks to the one after, the position is what was played since g1
  CHECK(rig.v.loop_status(0).playing);
  CHECK_FALSE(rig.v.loop_status(0).pending);
  CHECK(rig.v.loops().position(0) == (rig.at - start - g1) % 640);
}

TEST_CASE("vocoder loops: a Rec off on an idle loop leaves a pending play on its bar line") {
  Rig rig(Loop::kMaxSamples);
  std::vector<float> x(640, 0.5f);
  record_loop(rig, 0, x);
  rig.tr = playing_at(3840u * 3u - 960u);
  const std::uint32_t g1 = first_sample_at(rig.tr, 3840u * 3u);
  rig.block(nullptr, 64, {play(0, true)});
  const std::uint32_t start = rig.at - 64;
  while (rig.at - start + 64 <= g1) {
    rig.block(nullptr, 64);
  }
  REQUIRE(g1 - (rig.at - start) < 63);  // the bar line is in the next block, before the event
  rig.block(nullptr, 64, {dsp::param_change(Vocoder::loop_param(0, LF::Rec), 0.0f, 63)});
  CHECK(rig.v.loop_status(0).playing);
  CHECK(rig.v.loops().position(0) == (rig.at - start - g1) % 640);
}

TEST_CASE("vocoder loops: a loop leaving the carrier stays out of the modulator while it fades") {
  Rig rig(Loop::kMaxSamples);
  std::vector<float> x(6400);
  for (std::size_t i = 0; i < x.size(); ++i) {
    x[i] = 0.5f * std::sin(kPi2 * 440.0f * static_cast<float>(i) / kFs);
  }
  record_loop(rig, 0, x);
  // A MOD-role loop selected as the carrier, the live input silent, the modulator heard dry: nothing of the loop may come out.
  rig.block(nullptr, 64, {dsp::param_change(Vocoder::Dry, 1.0f), dsp::param_change(Vocoder::CarSrc, 3.0f), play(0, true)});
  std::vector<float> settled;
  for (unsigned b = 0; b < 700; ++b) {  // a second: the followers let go of what the recording put in them
    settled = rig.block(nullptr, 64);
  }
  REQUIRE(rms(settled, 0, 64) < 1.0e-3f);
  const auto out = rig.block(nullptr, 64, {dsp::param_change(Vocoder::CarSrc, 1.0f, 32)});
  float before = 0.0f;
  for (std::size_t i = 0; i < 32; ++i) {
    before = std::fmax(before, std::fabs(out[i]));
  }
  CHECK(before < 1.0e-3f);  // the block's first half is before the event: the loop is still only a carrier
}

TEST_CASE("vocoder loops: the role decides where the loop goes") {
  const std::vector<float> tone = rich(64 * 200, 500.0f);
  const auto run_case = [&](bool car, float dry, const std::vector<float>* live, float& e500, float& e3k, float& level) {
    Rig rig(Loop::kMaxSamples);
    record_loop(rig, 0, tone);
    rig.block(nullptr, 64, {dsp::param_change(Vocoder::Dry, dry), role(0, car), play(0, true)});
    std::vector<float> out;
    for (int b = 0; b < 300; ++b) {
      const float* in = live != nullptr ? live->data() + (static_cast<std::size_t>(b) % 100u) * 64u : nullptr;
      const std::vector<float> o = rig.block(in, 64);
      out.insert(out.end(), o.begin(), o.end());
    }
    level = rms(out, 64 * 100, out.size());
    e500 = tone_energy(out, 64 * 100, out.size(), 500.0f);
    e3k = tone_energy(out, 64 * 100, out.size(), 3000.0f);
  };
  float e500, e3k, level;
  const std::vector<float> live3k = rich(64 * 100, 3000.0f / 7.0f * 1.0f);  // partials at 428, 1285, 3000 Hz
  // MOD, DRY 1, the live input is a different tone: the output is the loop (500 Hz), not the input
  run_case(false, 1.0f, &live3k, e500, e3k, level);
  CHECK(level > 0.01f);
  CHECK(e500 > 10.0f * e3k);
  // MOD, DRY 0, no notes: a modulator with no carrier is silent
  run_case(false, 0.0f, nullptr, e500, e3k, level);
  CHECK(level < 1.0e-3f);
  // CAR, DRY 1, no live input: the dry path carries the modulator only, the loop is not it
  run_case(true, 1.0f, nullptr, e500, e3k, level);
  CHECK(level < 1.0e-3f);
  // CAR, DRY 0, a live modulator: the loop is the carrier the bands sing through
  run_case(true, 0.0f, &live3k, e500, e3k, level);
  CHECK(level > 0.01f);
}

TEST_CASE("vocoder loops: without memory the loops do nothing") {
  Vocoder v;
  v.prepare(kFs);
  std::vector<float> in(64, 0.5f), a(64), b(64);
  const dsp::Transport tr;
  const dsp::Event ev[] = {rec(0, true), play(0, true)};
  dsp::ProcessArgs args{};
  args.events = ev;
  args.event_count = 2;
  args.in_l = args.in_r = in.data();
  args.out_l = a.data();
  args.out_r = b.data();
  args.frames = 64;
  args.transport = &tr;
  v.process(args);
  CHECK(v.loop_status(0).rec == Vocoder::LoopStatus::Rec::Idle);
  CHECK(v.loops().length(0) == 0);
}

// ---- speed, direction and level ----------------------------------------------------------------------------------------------------

namespace {

constexpr float kToFloat16 = 1.0f / 32768.0f;

// A loop of `n` samples of a slow wave (period 1000, 0.5 peak), recorded and sealed (the seam fades baked in).
void make_loop(Loop& l, std::vector<std::int16_t>& mem, std::uint32_t n, float amp = 0.5f) {
  mem.assign(Loop::kCount * static_cast<std::size_t>(n), 0);
  l.bind(mem.data(), n);
  std::vector<float> in(n);
  for (std::uint32_t k = 0; k < n; ++k) {
    in[k] = amp * std::sin(kPi2 * static_cast<float>(k) / 1000.0f + 0.3f) + 0.1f * static_cast<float>((k * 7u) % 5u);
  }
  l.begin_record(0);
  REQUIRE(l.record(0, in.data(), n) == n);
  if (l.recording(0)) {
    l.end_record(0);
  }
}

// The oracle: the half-sample position of output sample k, in integers (speed 0 = 0.5x: 1, 1x: 2, 2x: 4), and the interpolated value.
float oracle(const std::vector<std::int16_t>& mem, std::uint32_t n, unsigned speed, bool rev, std::uint64_t k) {
  const std::int64_t step = speed == 0 ? 1 : (speed == 1 ? 2 : 4);
  const std::int64_t n2 = 2 * static_cast<std::int64_t>(n);
  const std::int64_t h0 = rev ? 2 * (static_cast<std::int64_t>(n) - 1) : 0;
  std::int64_t h = (h0 + (rev ? -1 : 1) * step * static_cast<std::int64_t>(k)) % n2;
  if (h < 0) {
    h += n2;
  }
  const std::uint32_t a = static_cast<std::uint32_t>(h >> 1);
  if ((h & 1) == 0) {
    return static_cast<float>(mem[a]) * kToFloat16;
  }
  return 0.5f * (static_cast<float>(mem[a]) + static_cast<float>(mem[(a + 1u) % n])) * kToFloat16;
}

// Plays `total` samples in chunks of the given sizes (cycled), compares each with the oracle, returns the largest absolute difference.
float run_against_oracle(Loop& l, const std::vector<std::int16_t>& mem, std::uint32_t n, unsigned speed, bool rev, std::uint64_t total,
                         const std::vector<std::uint32_t>& chunks) {
  l.set_speed(0, speed);
  l.set_reverse(0, rev);
  REQUIRE(l.start(0));
  std::vector<float> out(300);
  float worst = 0.0f;
  std::uint64_t k = 0;
  for (std::size_t c = 0; k < total; ++c) {
    const std::uint32_t m = chunks[c % chunks.size()];
    l.mix(0, out.data(), m, false);
    for (std::uint32_t j = 0; j < m; ++j) {
      worst = std::fmax(worst, std::fabs(out[j] - oracle(mem, n, speed, rev, k + j)));
    }
    k += m;
  }
  return worst;
}

}  // namespace

TEST_CASE("loop: every speed and direction reads the stored samples as the position says, across both ends") {
  for (const std::uint32_t n : {1000u, 999u}) {
    std::vector<std::int16_t> mem;
    Loop l;
    make_loop(l, mem, n);
    for (unsigned speed = 0; speed < 3; ++speed) {
      for (const bool rev : {false, true}) {
        INFO("n " << n << " speed " << speed << " rev " << rev);
        // five loops' worth: the wrap is crossed at both ends, in chunks that land on it, over it and past it
        CHECK(run_against_oracle(l, mem, n, speed, rev, 5ull * n * (speed == 0 ? 2 : 1) + 77, {64, 64, 7, 1, 33, 64, 200}) < 1e-6f);
      }
    }
  }
}

TEST_CASE("loop: the length in time and no drift over a whole 16 s loop at each speed") {
  const std::uint32_t n = Loop::kMaxSamples;
  std::vector<std::int16_t> mem;
  Loop l;
  make_loop(l, mem, n);
  for (unsigned speed = 0; speed < 3; ++speed) {
    for (const bool rev : {false, true}) {
      INFO("speed " << speed << " rev " << rev);
      // the period: 2n samples at 0.5x, n at 1x, n/2 at 2x (n is even); two periods, every sample against the integer oracle
      const std::uint64_t period = speed == 0 ? 2ull * n : (speed == 1 ? n : n / 2u);
      CHECK(run_against_oracle(l, mem, n, speed, rev, 2 * period, {64}) < 1e-6f);
      CHECK(l.position(0) == (rev ? n - 1u : 0u));  // after whole periods the position is where it started
    }
  }
}

TEST_CASE("loop: a loop shorter than the span wraps correctly at every speed and direction") {
  for (const std::uint32_t n : {1u, 2u, 3u, 5u, 31u, 100u}) {
    std::vector<std::int16_t> mem;
    Loop l;
    make_loop(l, mem, n);
    for (unsigned speed = 0; speed < 3; ++speed) {
      for (const bool rev : {false, true}) {
        INFO("n " << n << " speed " << speed << " rev " << rev);
        CHECK(run_against_oracle(l, mem, n, speed, rev, 900, {64, 5, 64}) < 1e-6f);
      }
    }
  }
}

TEST_CASE("loop: the seam stays quiet at every speed and direction") {
  std::vector<std::int16_t> mem;
  Loop l;
  std::vector<float> in(1000, 0.5f);
  mem.assign(Loop::kCount * 2000u, 0);
  l.bind(mem.data(), 2000);
  l.begin_record(0);
  (void)l.record(0, in.data(), 1000);
  l.end_record(0);
  for (unsigned speed = 0; speed < 3; ++speed) {
    for (const bool rev : {false, true}) {
      l.set_speed(0, speed);
      l.set_reverse(0, rev);
      REQUIRE(l.start(0));
      std::vector<float> out(4200);
      for (std::size_t p = 0; p < out.size(); p += 700) {
        l.mix(0, out.data() + p, 700, false);
      }
      float max_step = 0.0f;
      for (std::size_t k = 1; k < out.size(); ++k) {
        max_step = std::fmax(max_step, std::fabs(out[k] - out[k - 1]));
      }
      INFO("speed " << speed << " rev " << rev << " step " << max_step);
      CHECK(max_step < 0.5f / 60.0f * (speed == 2 ? 2.0f : 1.0f) * 1.05f);  // the fade's slope times the speed; a bare seam would jump 0.5
    }
  }
}

TEST_CASE("loop: speed, direction and level changed while playing do not jump") {
  std::vector<std::int16_t> mem;
  Loop l;
  make_loop(l, mem, 4000, 0.4f);
  REQUIRE(l.start(0));
  std::vector<float> out(64 * 12);
  const auto blk = [&](int b) { l.mix(0, out.data() + 64 * b, 64, false); };
  blk(0);
  blk(1);
  l.set_speed(0, 2);  // 1x -> 2x
  blk(2);
  blk(3);
  l.set_reverse(0, true);  // forward -> reverse
  blk(4);
  blk(5);
  l.set_speed(0, 0);  // 2x -> 0.5x
  blk(6);
  l.set_speed(0, 1);  // back to 1x (a reverse 1x: the plain pass is not used)
  blk(7);
  l.set_reverse(0, false);
  blk(8);
  blk(9);
  l.set_gain(0, 0.25f);  // a level change ramps over the chunk
  blk(10);
  blk(11);
  float max_step = 0.0f;
  for (std::size_t k = 1; k < out.size(); ++k) {
    max_step = std::fmax(max_step, std::fabs(out[k] - out[k - 1]));
  }
  // the wave's slope is 0.4 * 2pi / 1000 = 0.0025 a sample, 0.005 at 2x, plus the noise term (0.4 a step of the 5-cycle) is in the content:
  // content steps up to 0.4 between neighbours would show, so bound it against the content's own largest step times two
  float content = 0.0f;
  for (std::size_t k = 1; k < 4000; ++k) {
    content = std::fmax(content, std::fabs(static_cast<float>(mem[k] - mem[k - 1])) * kToFloat16);
  }
  CHECK(max_step <= 2.0f * content + 1e-6f);
  CHECK(l.playing(0));
}

namespace {

// The per-sample reference for Loop::mix: the same arithmetic with one plain int16 read per sample (what the word-read version replaced).
struct RefPlay {
  const std::int16_t* s = nullptr;
  std::uint32_t len = 0, h = 0;
  unsigned sp = 1;
  bool rev = false;
  float g1 = 1.0f, g = 1.0f;
  void mix(float* dst, std::uint32_t n, bool add) {
    const std::uint32_t len2 = 2u * len;
    if (sp == 1u && !rev) {
      h &= ~1u;
    }
    if (sp == 1u && !rev && g == g1) {
      const float scale = kToFloat16 * g1;
      for (std::uint32_t k = 0; k < n; ++k) {
        const float v = static_cast<float>(s[(h >> 1) % len]) * scale;
        dst[k] = add ? dst[k] + v : v;
        h = (h + 2u) % len2;
      }
      return;
    }
    const std::uint32_t step = sp == 0u ? 1u : (sp == 1u ? 2u : 4u);
    while (n != 0u) {
      const std::uint32_t nn = n < 64u ? n : 64u;
      float tmp[64];
      std::uint32_t hh = h;
      for (std::uint32_t k = 0; k < nn; ++k) {
        const std::uint32_t a = hh >> 1;
        const std::uint32_t a1 = (a + 1u) % len;
        tmp[k] = (hh & 1u) != 0u ? static_cast<float>(s[a] + s[a1]) * 0.5f : static_cast<float>(s[a]);
        hh = rev ? (hh + len2 - step) % len2 : (hh + step) % len2;  // the position h/2 either way, only its direction differs
      }
      h = hh;
      const float dg = (g1 - g) / static_cast<float>(nn);
      for (std::uint32_t k = 0; k < nn; ++k) {
        g += dg;
        tmp[k] *= kToFloat16 * g;
      }
      g = g1;
      for (std::uint32_t k = 0; k < nn; ++k) {
        dst[k] = add ? dst[k] + tmp[k] : tmp[k];
      }
      dst += nn;
      n -= nn;
    }
  }
};

}  // namespace

TEST_CASE("loop: word reads are bit-identical to per-sample reads at any alignment, length, speed, direction, level and add") {
  const std::uint32_t frames[] = {64, 1, 7, 33, 64, 127, 200, 3, 65};
  for (const std::uint32_t per : {1001u, 1000u}) {
    const std::uint32_t lens[Loop::kCount] = {per, per - 1u, 63u, 2u};
    std::vector<std::int16_t> mem(static_cast<std::size_t>(Loop::kCount) * per, 0);
    Loop l;
    l.bind(mem.data(), per);
    std::uint32_t seed = 12345u;
    for (unsigned i = 0; i < Loop::kCount; ++i) {
      std::vector<float> in(lens[i]);
      for (auto& x : in) {
        seed = seed * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(seed >> 16) - 32768) / 32768.0f;
      }
      l.begin_record(i);
      REQUIRE(l.record(i, in.data(), lens[i]) == lens[i]);
      if (l.recording(i)) {
        l.end_record(i);
      }
    }
    for (unsigned i = 0; i < Loop::kCount; ++i) {
      for (unsigned speed = 0; speed < 3; ++speed) {
        for (const bool rev : {false, true}) {
          for (const bool ramp : {false, true}) {
            for (const bool add : {false, true}) {
              INFO("per " << per << " loop " << i << " speed " << speed << " rev " << rev << " ramp " << ramp << " add " << add);
              l.stop(i);
              l.set_speed(i, speed);
              l.set_reverse(i, rev);
              l.set_gain(i, 0.8f);
              REQUIRE(l.start(i));
              RefPlay r;
              r.s = mem.data() + static_cast<std::size_t>(i) * per;
              r.len = lens[i];
              r.sp = speed;
              r.rev = rev;
              r.g1 = r.g = 0.8f;
              r.h = l.position(i) << 1;
              std::uint32_t total = 0;
              const std::uint32_t want = 3u * lens[i] * (speed == 0 ? 2u : 1u) + 50u;
              for (std::size_t b = 0; total < want; ++b) {
                if (ramp && b % 4 == 1) {  // a new level: the next chunk ramps
                  const float ng = 0.3f + 0.15f * static_cast<float>(b % 5);
                  l.set_gain(i, ng);
                  r.g1 = ng;
                }
                const std::uint32_t m = frames[b % (sizeof frames / sizeof frames[0])];
                std::vector<float> a(m, 0.25f), e(m, 0.25f);
                l.mix(i, a.data(), m, add);
                r.mix(e.data(), m, add);
                REQUIRE(std::memcmp(a.data(), e.data(), m * sizeof(float)) == 0);
                total += m;
              }
            }
          }
        }
      }
    }
  }
}

TEST_CASE("loop: the level scales the output, the default is exactly the stored sample") {
  std::vector<std::int16_t> mem;
  Loop l;
  make_loop(l, mem, 500);
  REQUIRE(l.start(0));
  std::vector<float> a(128), b(128);
  l.mix(0, a.data(), 128, false);
  CHECK(a[40] == static_cast<float>(mem[40]) * kToFloat16);  // the fast path, level 1
  l.set_gain(0, 0.5f);
  l.mix(0, b.data(), 64, false);  // the ramp
  l.mix(0, b.data() + 64, 64, false);  // settled, the fast path again
  CHECK(b[127] == doctest::Approx(0.5f * static_cast<float>(mem[255]) * kToFloat16));
  l.stop(0);
  REQUIRE(l.start(0));  // start() takes the level at once
  l.mix(0, a.data(), 128, false);
  CHECK(a[40] == doctest::Approx(0.5f * static_cast<float>(mem[40]) * kToFloat16));
}

TEST_CASE("vocoder loops: a bar line starts a loop sample-exactly at any speed and direction") {
  struct Case { unsigned speed; bool rev; };
  for (const Case c : {Case{0, false}, Case{2, false}, Case{1, true}, Case{2, true}, Case{0, true}}) {
    Rig rig(Loop::kMaxSamples);
    std::vector<float> x(640, 0.5f);
    record_loop(rig, 0, x);
    rig.tr = playing_at(3840u * 3u - 960u);
    const std::uint32_t g1 = first_sample_at(rig.tr, 3840u * 3u);
    rig.block(nullptr, 64, {dsp::param_change(Vocoder::loop_param(0, LF::Speed), static_cast<float>(c.speed)),
                            dsp::param_change(Vocoder::loop_param(0, LF::Dir), c.rev ? 1.0f : 0.0f), play(0, true)});
    CHECK(rig.v.loop_status(0).speed == c.speed);
    CHECK(rig.v.loop_status(0).reverse == c.rev);
    const std::uint32_t start = rig.at - 64;
    while (rig.at - start <= g1 + 64) {
      rig.block(nullptr, 64);
    }
    const std::int64_t e = static_cast<std::int64_t>(rig.at - start - g1);
    const std::int64_t step = c.speed == 0 ? 1 : (c.speed == 1 ? 2 : 4);
    const std::int64_t h0 = c.rev ? 2 * 639 : 0;
    std::int64_t h = (h0 + (c.rev ? -step : step) * e) % 1280;
    if (h < 0) {
      h += 1280;
    }
    INFO("speed " << c.speed << " rev " << c.rev);
    CHECK(rig.v.loops().position(0) == static_cast<std::uint32_t>(h >> 1));
  }
}

TEST_CASE("vocoder loops: the level parameter defaults to 0 dB, ids 0..21 did not move") {
  Rig rig(Loop::kMaxSamples);
  CHECK(rig.v.loop_status(2).level_db == 0.0f);
  CHECK(rig.v.loop_status(2).speed == 1);
  rig.block(nullptr, 64, {dsp::param_change(Vocoder::loop_param(2, LF::Level), -6.0f)});
  CHECK(rig.v.loop_status(2).level_db == -6.0f);
  CHECK(rig.v.info().params[Vocoder::loop_param(2, LF::Level)].def == 0.0f);
  CHECK(rig.v.info().params[Vocoder::Loop4Role].def == 0.0f);
}

TEST_CASE("vocoder loops: GATE cuts a loop playing as the carrier, and GATE 0 leaves the sound bit-identical") {
  const std::vector<float> tone = rich(64 * 200, 500.0f);
  const std::vector<float> live = rich(64 * 100, 3000.0f / 7.0f);
  const auto run_case = [&](float gate, bool write_gate) {
    Rig rig(Loop::kMaxSamples);
    record_loop(rig, 0, tone);
    std::vector<dsp::Event> ev = {dsp::param_change(Vocoder::Dry, 0.0f), role(0, true), play(0, true)};
    if (write_gate) {
      ev.push_back(dsp::param_change(Vocoder::Gate, gate));
    }
    rig.tr = playing_at(3840u * 3u - 960u);  // a quarter before a bar line: the loop starts there, 22,050 frames on
    rig.block(nullptr, 64, ev);
    std::vector<float> out;
    for (int b = 0; b < 1500; ++b) {
      const std::vector<float> o = rig.block(live.data() + (static_cast<std::size_t>(b) % 100u) * 64u, 64);
      out.insert(out.end(), o.begin(), o.end());
    }
    return out;
  };
  // the loop and the live modulator are ordinary signals: with the gate off the sound is steady
  const std::vector<float> open = run_case(0.0f, false);
  const std::vector<float> zero = run_case(0.0f, true);
  CHECK(open == zero);  // GATE written 0 is bit-exact with never written
  // windows of 1024 frames (about 4 half-steps of 1/8 at 120 BPM are 2756 frames each): the level of the quietest and the loudest
  const auto span = [](const std::vector<float>& x) {
    float lo = 1.0e9f, hi = 0.0f;
    for (std::size_t from = 64 * 600; from + 512 <= x.size(); from += 128) {
      const float r = rms(x, from, from + 512);
      lo = std::fmin(lo, r);
      hi = std::fmax(hi, r);
    }
    return std::pair<float, float>(lo, hi);
  };
  const auto so = span(open);
  CHECK(so.second > 0.01f);
  CHECK(so.first > 0.3f * so.second);
  const std::vector<float> gated = run_case(2.0f, true);  // 1/8
  const auto sg = span(gated);
  CHECK(sg.second > 0.01f);
  CHECK(sg.first < 0.05f * sg.second);  // the closed half is silent: the loop does not escape the gate
}
