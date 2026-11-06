// SPDX-License-Identifier: GPL-3.0-or-later
// Region play (U5a): a loop reader plays a region of the whole bank once, through 64-sample ramps; the whole loop pauses under it. Loop
// level for the samples and the ramps, Vocoder level for the parameters, the frame, the recording priority and the status.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/loop.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>
#include <o3sdk/dsp/transport.h>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

namespace {

constexpr float kFs = 44100.0f;
constexpr float kInv = 1.0f / 32768.0f;
constexpr std::uint32_t kPer = 4000;  // samples a slot
using RF = Vocoder::RegionField;
using LF = Vocoder::LoopField;

static_assert(Vocoder::LiveCh == 48 && Vocoder::R1Start == 49 && Vocoder::R4Trig == 60 && Vocoder::kParamCount == 62, "append only");
static_assert(Vocoder::kParamCount <= 64, "the kit's limit per unit");
static_assert(Vocoder::region_param(0, RF::Start) == 49 && Vocoder::region_param(2, RF::Len) == 56 && Vocoder::region_param(3, RF::Trig) == 60, "three a reader");

// A bank whose sample k is a distinct, slowly rising value: any read position can be told from the output.
struct Bank {
  std::vector<std::int16_t> mem;
  Loop l;
  Bank() : mem(Loop::kCount * kPer) {
    for (std::size_t k = 0; k < mem.size(); ++k) {
      mem[k] = static_cast<std::int16_t>(1000 + static_cast<int>(k % 20000));
    }
    l.bind(mem.data(), kPer);
  }
  float at(std::size_t k) const { return static_cast<float>(mem[k]) * kInv; }
  // A region of reader 0 from frame 0, rendered in blocks of `blk` for `total` samples.
  std::vector<float> run(std::uint32_t start, std::uint32_t len, std::uint32_t total, std::uint32_t blk = 64) {
    l.set_region_start(0, start);
    l.set_region_len(0, len);
    REQUIRE(l.trig_region(0, 0));
    return render(total, blk);
  }
  std::vector<float> render(std::uint32_t total, std::uint32_t blk = 64) {
    std::vector<float> out(total, 0.0f);
    for (std::uint32_t p = 0; p < total; p += blk) {
      l.play(0, out.data() + p, blk < total - p ? blk : total - p, false);
    }
    return out;
  }
};

float max_step(const std::vector<float>& v) {
  float m = 0.0f;
  for (std::size_t k = 1; k < v.size(); ++k) {
    m = std::fmax(m, std::fabs(v[k] - v[k - 1]));
  }
  return m;
}

struct Rig {
  Vocoder v;
  std::vector<std::int16_t> mem;
  dsp::Transport tr;  // stopped
  Rig() : mem(Loop::kCount * kPer, std::int16_t{0}) {
    v.bind_loops(mem.data(), kPer);
    v.prepare(kFs);
    for (std::size_t k = 0; k < mem.size(); ++k) {
      mem[k] = static_cast<std::int16_t>(2000 + static_cast<int>(k % 3000));
    }
  }
  void block(std::vector<dsp::Event> ev, std::uint32_t n = 64) {
    std::vector<float> out(n), r(n), in(n, 0.0f);
    dsp::ProcessArgs a{};
    a.events = ev.data();
    a.event_count = static_cast<std::uint32_t>(ev.size());
    a.in_l = in.data();
    a.in_r = in.data();
    a.out_l = out.data();
    a.out_r = r.data();
    a.frames = n;
    tr.frames = n;
    a.transport = &tr;
    v.process(a);
  }
};
dsp::Event rp(unsigned n, RF f, float v, std::uint16_t frame = 0) { return dsp::param_change(Vocoder::region_param(n, f), v, frame); }

}  // namespace

TEST_CASE("region: 1x plays the samples exactly, with a 64-sample ramp in and out, once") {
  Bank b;
  const std::uint32_t start = 100, len = 500;
  const auto out = b.run(start, len, 700, 37);
  for (std::uint32_t j = 0; j < len; ++j) {
    float f = 1.0f;
    if (j < 64) f = static_cast<float>(j + 1) / 64.0f;
    if (len - j <= 64) f = std::fmin(f, static_cast<float>(len - j - 1) / 64.0f);
    CHECK(out[j] == doctest::Approx(b.at(start + j) * f).epsilon(1e-5).scale(1.0));
  }
  CHECK(out[len - 1] == 0.0f);  // ends on zero
  for (std::uint32_t j = len; j < 700; ++j) {
    CHECK(out[j] == 0.0f);  // silent after: not even written
  }
  CHECK_FALSE(b.l.region(0));
}

TEST_CASE("region: 0.5x and 2x take twice and half the time, reverse reads back from the end") {
  Bank b;
  b.l.set_speed(0, 0);
  auto out = b.run(200, 300, 900);
  for (std::uint32_t j = 80; j < 500; j += 2) {
    CHECK(out[j] == doctest::Approx(b.at(200 + j / 2)).epsilon(1e-5));
  }
  CHECK(out[2 * 300 - 2] == 0.0f);  // the last of 2 * 300 - 1 outputs
  CHECK(out[2 * 300 - 1] == 0.0f);
  CHECK_FALSE(b.l.region(0));
  b.l.set_speed(0, 2);
  out = b.run(200, 300, 400);
  for (std::uint32_t j = 66; j < 84; ++j) {
    CHECK(out[j] == doctest::Approx(b.at(200 + 2 * j)).epsilon(1e-5));
  }
  CHECK(b.l.region(0) == false);
  std::uint32_t last = 0;
  for (std::uint32_t j = 0; j < 400; ++j) {
    if (out[j] != 0.0f) last = j;
  }
  CHECK(last >= 140);
  CHECK(last <= 150);  // 150 outputs: (2 * 299) / 4 + 1
  b.l.set_speed(0, 1);
  b.l.set_reverse(0, true);
  out = b.run(1000, 400, 500);
  for (std::uint32_t j = 70; j < 330; ++j) {
    CHECK(out[j] == doctest::Approx(b.at(1000 + 399 - j)).epsilon(1e-5));
  }
  CHECK(out[399] == 0.0f);
  CHECK_FALSE(b.l.region(0));
}

TEST_CASE("region: a release and a retrigger never step more than the ramp allows") {
  Bank b;
  std::fill(b.mem.begin(), b.mem.end(), std::int16_t{16384});  // DC 0.5: any jump is a click
  const float ramp = 0.5f / 64.0f * 1.001f;
  // release in the middle of a block
  b.l.set_region_start(0, 10);
  b.l.set_region_len(0, 3000);
  REQUIRE(b.l.trig_region(0, 0));
  std::vector<float> out(800, 0.0f);
  b.l.play(0, out.data(), 256, false);
  b.l.release_region(0, 30);
  b.l.play(0, out.data() + 256, 256, false);
  b.l.play(0, out.data() + 512, 288, false);
  CHECK(max_step(out) <= ramp);
  CHECK(out[255] == doctest::Approx(0.5f));
  CHECK(out[256 + 30 + 63] == 0.0f);  // 64 samples later it is silent
  CHECK_FALSE(b.l.region(0));
  // retrigger on a sounding reader: fades out, then the new one fades in
  b.l.release_region(0, 0);
  REQUIRE(b.l.trig_region(0, 0));
  std::vector<float> o2(1000, 0.0f);
  b.l.play(0, o2.data(), 300, false);
  REQUIRE(b.l.trig_region(0, 20));
  b.l.play(0, o2.data() + 300, 300, false);
  b.l.play(0, o2.data() + 600, 400, false);
  CHECK(max_step(o2) <= ramp);
  CHECK(o2[299] == doctest::Approx(0.5f));
  float lo = 1.0f;
  for (std::uint32_t k = 300; k < 420; ++k) lo = std::fmin(lo, o2[k]);
  CHECK(lo == 0.0f);  // it went all the way down
  CHECK(o2[999] == doctest::Approx(0.5f));  // and the new region is sounding
  CHECK(b.l.region_sounding(0));
}

TEST_CASE("region: any reader plays any slot's memory; bank edges clamp; length 0 does nothing") {
  Bank b;
  const std::uint32_t start = kPer + 50;  // slot 2 (index 1)
  const auto out = b.run(start, 300, 300);
  for (std::uint32_t j = 64; j < 230; ++j) {
    CHECK(out[j] == doctest::Approx(b.at(start + j)).epsilon(1e-5));
  }
  b.l.set_region_start(0, Loop::kCount * kPer - 300);  // 300 samples to the end: len clamps
  b.l.set_region_len(0, 5000);
  REQUIRE(b.l.trig_region(0, 0));
  const auto o2 = b.render(400);
  CHECK(o2[150] == doctest::Approx(b.at(Loop::kCount * kPer - 300 + 150)).epsilon(1e-5));
  CHECK(o2[299] == 0.0f);
  CHECK_FALSE(b.l.region(0));
  b.l.set_region_len(0, 0);
  CHECK_FALSE(b.l.trig_region(0, 0));
  b.l.set_region_len(0, 100);
  b.l.set_region_start(0, Loop::kCount * kPer);  // past the end
  CHECK_FALSE(b.l.trig_region(0, 0));
  CHECK_FALSE(b.l.region(0));
}

TEST_CASE("region: the whole loop pauses under it and resumes at its position") {
  Bank b;
  std::vector<float> in(1000);
  for (std::size_t k = 0; k < in.size(); ++k) in[k] = 0.2f + 0.0003f * static_cast<float>(k);
  b.l.begin_record(0);
  REQUIRE(b.l.record(0, in.data(), 1000) == 1000);
  b.l.end_record(0);
  REQUIRE(b.l.start(0));
  std::vector<float> out(300);
  b.l.play(0, out.data(), 300, false);
  CHECK(b.l.position(0) == 300);
  std::vector<std::int16_t> keep(b.mem.begin(), b.mem.begin() + 1000);
  b.l.set_region_start(0, kPer + 10);
  b.l.set_region_len(0, 400);
  REQUIRE(b.l.trig_region(0, 0));
  std::vector<float> o(700, 0.0f);
  b.l.play(0, o.data(), 300, false);
  b.l.play(0, o.data() + 300, 400, false);
  CHECK(b.l.playing(0));
  CHECK_FALSE(b.l.region(0));
  CHECK(max_step(o) < 0.01f);  // the duck, the region and the resume are all ramps
  // the loop is back at sample 300 + 64 (the duck chunk) + the samples after the region, and sounds from there at its own level
  for (std::uint32_t k = 400 + 64; k < 600; ++k) {
    CHECK(o[k] == doctest::Approx(static_cast<float>(keep[(300 + 64 + (k - 400)) % 1000]) * kInv).epsilon(1e-5));
  }
}

TEST_CASE("region: Vocoder parameters: the frame, the status, recording priority, CLEAR") {
  Rig r;
  CHECK(r.v.info().param_count == Vocoder::kParamCount);
  CHECK_FALSE(r.v.loop_status(0).region);
  // Start and Len written first, the Trig at frame 10: the reader begins there
  r.block({rp(0, RF::Start, 1500.0f), rp(0, RF::Len, 800.0f), rp(0, RF::Trig, 1.0f, 10)});
  auto st = r.v.loop_status(0);
  CHECK(st.region);
  CHECK(st.position == 1500u + 54u);  // 64 - 10 samples at 1x
  r.block({}, 64);
  CHECK(r.v.loop_status(0).position == 1500u + 118u);
  // a retrigger on another region of the same reader, then a release
  r.block({rp(0, RF::Start, 100.0f), rp(0, RF::Len, 300.0f), rp(0, RF::Trig, 1.0f)});
  r.block({});
  r.block({});
  st = r.v.loop_status(0);
  CHECK(st.region);
  CHECK(st.position < 100u + 300u);
  CHECK(st.position >= 100u);
  r.block({rp(0, RF::Trig, 0.0f)});
  r.block({});
  CHECK_FALSE(r.v.loop_status(0).region);
  // a recording has priority: Trig on a reader that records does nothing
  r.block({dsp::param_change(Vocoder::loop_param(1, LF::Rec), 1.0f)});
  CHECK(r.v.loop_status(1).rec == Vocoder::LoopStatus::Rec::Recording);
  r.block({rp(1, RF::Start, 0.0f), rp(1, RF::Len, 500.0f), rp(1, RF::Trig, 1.0f)});
  r.block({});
  CHECK_FALSE(r.v.loop_status(1).region);
  // Rec CLEAR on a sounding reader stops its region
  r.block({dsp::param_change(Vocoder::loop_param(1, LF::Rec), 0.0f)});
  r.block({rp(1, RF::Trig, 1.0f)});
  CHECK(r.v.loop_status(1).region);
  r.block({dsp::param_change(Vocoder::loop_param(1, LF::Rec), 2.0f)});
  CHECK_FALSE(r.v.loop_status(1).region);
  // length 0 and a start past the bank do nothing
  r.block({rp(2, RF::Start, 100.0f), rp(2, RF::Len, 0.0f), rp(2, RF::Trig, 1.0f)});
  CHECK_FALSE(r.v.loop_status(2).region);
  r.block({rp(2, RF::Start, 2822399.0f), rp(2, RF::Len, 1000.0f), rp(2, RF::Trig, 0.0f)});
  r.block({rp(2, RF::Trig, 1.0f)});
  CHECK_FALSE(r.v.loop_status(2).region);  // the bank here is 4 * 4000 samples: past it
}
