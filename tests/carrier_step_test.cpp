// SPDX-License-Identifier: GPL-3.0-or-later
// V5c, the per-step carrier switch: the pattern file with and without carriers (an older file loads with none), the clocked hook
// (the switch lands on the step's own frame, holds, an empty step emits nothing, a stop and a restart) driven through the engine, and the
// Vocoder's CARRIER parameter (sample-exact, a ramp and not a step, a loop that is selected). Synthetic signals only.
#include <doctest/doctest.h>

#include <o3_voc03er/carrier_seq.h>
#include <o3_voc03er/pattern.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/engine.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;
namespace chunk = o3sdk::formats::chunk;

namespace {

// ---- The file ------------------------------------------------------------------------------------------------------------------

Pattern sample() {
  Pattern p;
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    p.note[i] = static_cast<std::uint8_t>(i % 3 == 0 ? 0 : 40 + i);
  }
  p.carrier[1] = 3;
  p.carrier[5] = 2;
  p.carrier[15] = 6;
  return p;
}

// What V1 wrote: a STEP chunk and nothing else.
std::vector<std::uint8_t> old_file(const Pattern& p) {
  std::uint8_t buf[kPatternFileMax] = {};
  chunk::Writer w(buf, sizeof buf, "PATN");
  REQUIRE(w.begin("STEP"));
  REQUIRE(w.u16(0x0100));
  REQUIRE(w.u8(kPatternSteps));
  REQUIRE(w.bytes(p.note, sizeof p.note));
  REQUIRE(w.end());
  const std::size_t n = w.finish();
  REQUIRE(n != 0);
  return std::vector<std::uint8_t>(buf, buf + n);
}

std::vector<std::uint8_t> file_with_carr(const Pattern& p, unsigned steps, std::uint8_t bad_value, bool bad) {
  std::uint8_t buf[kPatternFileMax] = {};
  chunk::Writer w(buf, sizeof buf, "PATN");
  REQUIRE(w.begin("STEP"));
  REQUIRE(w.u16(0x0100));
  REQUIRE(w.u8(kPatternSteps));
  REQUIRE(w.bytes(p.note, sizeof p.note));
  REQUIRE(w.end());
  std::uint8_t car[kPatternSteps] = {};
  if (bad) {
    car[3] = bad_value;
  }
  REQUIRE(w.begin("CARR"));
  REQUIRE(w.u16(0x0100));
  REQUIRE(w.u8(static_cast<std::uint8_t>(steps)));
  REQUIRE(w.bytes(car, steps));
  REQUIRE(w.end());
  const std::size_t n = w.finish();
  REQUIRE(n != 0);
  return std::vector<std::uint8_t>(buf, buf + n);
}

}  // namespace

TEST_CASE("carrier file: an older pattern file loads with no carrier on any step") {
  const Pattern p = sample();
  const std::vector<std::uint8_t> bytes = old_file(p);
  Pattern got;
  got.carrier[7] = 4;  // whatever was there is replaced, not merged
  REQUIRE(read_pattern(bytes.data(), bytes.size(), got));
  CHECK(got.carriers_used() == 0);
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    CHECK(got.note[i] == p.note[i]);
  }
}

TEST_CASE("carrier file: the carriers round trip, and a pattern without them writes the V1 bytes") {
  const Pattern p = sample();
  std::uint8_t buf[kPatternFileMax] = {};
  const std::size_t n = write_pattern(p, buf, sizeof buf);
  REQUIRE(n != 0);
  CHECK(n <= kPatternFileMax);
  Pattern back;
  REQUIRE(read_pattern(buf, n, back));
  CHECK(back == p);
  CHECK(back.carrier[1] == 3);
  CHECK(back.carrier[15] == 6);

  Pattern plain = p;
  for (std::uint8_t& c : plain.carrier) {
    c = 0;
  }
  std::uint8_t b2[kPatternFileMax] = {};
  const std::size_t n2 = write_pattern(plain, b2, sizeof b2);
  const std::vector<std::uint8_t> v1 = old_file(plain);
  REQUIRE(n2 == v1.size());
  for (std::size_t i = 0; i < n2; ++i) {
    CHECK(b2[i] == v1[i]);
  }
}

TEST_CASE("carrier file: a bad CARR refuses the whole file and leaves the pattern as it was") {
  const Pattern p = sample();
  Pattern keep = p;
  keep.carrier[2] = 1;
  const Pattern before = keep;
  const std::vector<std::uint8_t> value = file_with_carr(p, kPatternSteps, 7, true);
  CHECK_FALSE(read_pattern(value.data(), value.size(), keep));
  const std::vector<std::uint8_t> count = file_with_carr(p, 15, 0, false);
  CHECK_FALSE(read_pattern(count.data(), count.size(), keep));
  CHECK(keep == before);
  const std::vector<std::uint8_t> fine = file_with_carr(p, kPatternSteps, 6, true);
  CHECK(read_pattern(fine.data(), fine.size(), keep));
  CHECK(keep.carrier[3] == 6);
}

TEST_CASE("carrier pattern: cycling steps through none, synth, noise, loop 1..4 and back") {
  Pattern p;
  for (unsigned expect : {1u, 2u, 3u, 4u, 5u, 6u, 0u, 1u}) {
    CHECK(p.cycle_carrier(4));
    CHECK(p.carrier[4] == expect);
  }
  CHECK_FALSE(p.cycle_carrier(kPatternSteps));
  p.clear();
  CHECK(p.carriers_used() == 0);
}

// ---- The hook, through the engine ----------------------------------------------------------------------------------------------

namespace {

constexpr std::array<o3sdk::core::ParamDesc, Vocoder::kParamCount> make_rows() {
  std::array<o3sdk::core::ParamDesc, Vocoder::kParamCount> rows{};
  for (auto& r : rows) {
    r = {"P", "", nullptr, nullptr, 0.0f, 6.0f, 0.0f, 7, 1, 0, o3sdk::core::ParamUnit::Int, o3sdk::core::ParamCurve::Linear, 0, 0};
  }
  return rows;
}
constexpr auto kRows = make_rows();

// An instrument that writes the CARRIER events it gets, with the engine's sample index of each, and checks the event's frame against the
// transport at that frame: the first sample whose position reaches the step's tick.
class Spy final : public dsp::Unit {
 public:
  struct Hit {
    std::uint64_t tick;  // the transport's position of the event's frame, rounded down to a sixteenth
    float value;
    bool exact;  // the frame is the first at or after the tick (at(frame) >= tick > at(frame - 1))
  };
  Spy() : info_{"SPY", dsp::UnitKind::Instrument, kRows.data(), Vocoder::kParamCount, nullptr, 0} {}
  const dsp::UnitInfo& info() const noexcept override { return info_; }
  void prepare(float) noexcept override {}
  void process(const dsp::ProcessArgs& a) noexcept override {
    for (std::uint32_t i = 0; i < a.event_count; ++i) {
      const dsp::Event& e = a.events[i];
      if (e.kind != dsp::EventKind::Param) {
        continue;
      }
      if (e.param != Vocoder::CarSrc) {
        continue;
      }
      const std::uint64_t tick = (a.transport->at(e.frame) >> 32) / 240u * 240u;
      const bool exact = a.transport->at(e.frame) >= (tick << 32) && (e.frame == 0u || a.transport->at(e.frame - 1u) < (tick << 32));
      hits.push_back({tick, e.value, exact});
    }
    ++block_;
    for (std::uint32_t i = 0; i < a.frames; ++i) {
      a.out_l[i] = 0.0f;
      a.out_r[i] = 0.0f;
    }
  }
  std::vector<Hit> hits;
  std::uint64_t block_ = 0;

 private:
  dsp::UnitInfo info_;
};

struct HookRig {
  HookRig() {
    id = e.attach(spy);
    seq.init(id);
    ok = e.set_source(0, id) && e.add_hook(seq);
  }
  void run(unsigned blocks) {
    float l[64], r[64];
    for (unsigned i = 0; i < blocks; ++i) {
      e.render(nullptr, nullptr, l, r, 64);
    }
  }
  dsp::Engine e;
  Spy spy;
  CarrierSeq seq;
  dsp::UnitId id = dsp::kNoUnit;
  bool ok = false;
};

}  // namespace

TEST_CASE("carrier hook: the switch lands on the step's own frame, an empty step emits nothing, the table wraps every bar") {
  HookRig rig;
  REQUIRE(rig.ok);
  std::uint8_t car[kPatternSteps] = {};
  car[1] = 3;  // loop 1
  car[5] = 2;  // noise
  car[9] = 9;  // not a carrier: a value out of range never reaches the unit
  rig.seq.set_all(car);
  rig.e.start();
  rig.spy.hits.clear();
  REQUIRE(rig.e.play());
  rig.run(1400 + 700);  // 2.1 bars
  REQUIRE(rig.spy.hits.size() == 4);  // steps 1 and 5 of both bars: nothing for the 14 empty ones
  const std::uint64_t at[4] = {1, 5, 17, 21};
  const float want[4] = {3.0f, 2.0f, 3.0f, 2.0f};
  for (unsigned i = 0; i < 4; ++i) {
    INFO("hit " << i);
    CHECK(rig.spy.hits[i].exact);
    CHECK(rig.spy.hits[i].tick == at[i] * 240u);
    CHECK(rig.spy.hits[i].value == want[i]);
  }
}

TEST_CASE("carrier hook: a stop sends FOLLOW once, a restart begins from the top and switches again at the same frames") {
  HookRig rig;
  REQUIRE(rig.ok);
  std::uint8_t car[kPatternSteps] = {};
  car[2] = 1;  // synth
  rig.seq.set_all(car);
  rig.e.start();
  rig.spy.hits.clear();
  REQUIRE(rig.e.play());
  rig.run(250);  // past step 2 (at sample 11025), before step 3
  REQUIRE(rig.spy.hits.size() == 1);
  CHECK(rig.spy.hits[0].value == 1.0f);
  CHECK(rig.spy.hits[0].tick == 2u * 240u);
  rig.spy.hits.clear();
  REQUIRE(rig.e.stop());
  rig.run(1);
  REQUIRE(rig.spy.hits.size() == 1);  // FOLLOW, at the first frame of the stopped block
  CHECK(rig.spy.hits[0].value == 0.0f);
  rig.run(20);
  CHECK(rig.spy.hits.size() == 1);  // and nothing while it stays stopped
  REQUIRE(rig.e.play());
  rig.run(250);
  REQUIRE(rig.spy.hits.size() == 2);
  CHECK(rig.spy.hits[1].value == 1.0f);
  CHECK(rig.spy.hits[1].tick == 2u * 240u);  // the run restarts at position 0
}

// ---- The unit ------------------------------------------------------------------------------------------------------------------

namespace {

constexpr float kFs = 44100.0f;
constexpr std::uint32_t kBlock = 64;

// Two vocoders fed the same modulator tone and the same chord; `ev` goes to the second only. block() renders both and returns the outputs.
struct Pair {
  Vocoder a, b;
  std::vector<std::int16_t> mem_a, mem_b;
  dsp::Transport tr;
  std::uint64_t n = 0;
  Pair() : mem_a(Loop::kCount * 4410u), mem_b(Loop::kCount * 4410u) {
    a.bind_loops(mem_a.data(), 4410);
    b.bind_loops(mem_b.data(), 4410);
    a.prepare(kFs);
    b.prepare(kFs);
    tr.playing = false;
    tr.frames = kBlock;
  }
  static void run(Vocoder& v, std::uint64_t at, const std::vector<dsp::Event>& ev, std::vector<float>& out, const dsp::Transport& tr) {
    std::vector<float> in(kBlock), r(kBlock), o(kBlock);
    for (std::uint32_t i = 0; i < kBlock; ++i) {
      in[i] = 0.5f * std::sin(6.2831853f * 220.0f * static_cast<float>(at + i) / kFs);
    }
    dsp::ProcessArgs p{};
    p.events = ev.data();
    p.event_count = static_cast<std::uint32_t>(ev.size());
    p.in_l = in.data();
    p.in_r = in.data();
    p.out_l = o.data();
    p.out_r = r.data();
    p.frames = kBlock;
    p.transport = &tr;
    v.process(p);
    out.insert(out.end(), o.begin(), o.end());
  }
  // One block each; `ev` (frames inside it) only for b.
  void block(const std::vector<dsp::Event>& ev, std::vector<float>& oa, std::vector<float>& ob) {
    run(a, n, {}, oa, tr);
    run(b, n, ev, ob, tr);
    n += kBlock;
  }
};

float peak(const std::vector<float>& v, std::size_t from, std::size_t to) {
  float m = 0.0f;
  for (std::size_t i = from; i < to && i < v.size(); ++i) {
    m = std::fmax(m, std::fabs(v[i]));
  }
  return m;
}

}  // namespace

TEST_CASE("carrier unit: the switch starts on the event's frame (identical before it), then fades in a few milliseconds, not in one sample") {
  Pair p;
  std::vector<float> oa, ob;
  const std::vector<dsp::Event> chord = {dsp::note_on(48, 100, 0, 0), dsp::note_on(55, 100, 0, 0)};
  p.a.process(dsp::ProcessArgs{});  // (a no-op call: the unit is ready)
  std::vector<dsp::Event> first = chord;
  // The chord goes to both.
  Pair::run(p.a, 0, chord, oa, p.tr);
  Pair::run(p.b, 0, chord, ob, p.tr);
  p.n = kBlock;
  for (unsigned i = 0; i < 300; ++i) {
    p.block({}, oa, ob);
  }
  REQUIRE(peak(oa, oa.size() - 640, oa.size()) > 0.01f);  // it sounds
  const std::size_t start = oa.size();
  const std::uint32_t frame = 23;
  // CARRIER = LOOP1 (3): the synth leaves, the (empty) loop is the carrier: silence once the fade is over.
  p.block({dsp::param_change(Vocoder::CarSrc, 3.0f, static_cast<std::uint16_t>(frame))}, oa, ob);
  for (unsigned i = 0; i < 40; ++i) {
    p.block({}, oa, ob);
  }
  for (std::size_t i = start; i < start + frame; ++i) {
    INFO("sample " << i - start);
    CHECK(oa[i] == ob[i]);  // before the event's frame the two runs are the same bits
  }
  CHECK(std::fabs(oa[start + frame + 1] - ob[start + frame + 1]) < 0.2f * peak(oa, start, start + 600));  // one sample later: only just started
  // 5 ms is 220 samples; the bands' envelopes ring down after it: by 20 ms the output is 26 dB under what it was, by 60 ms 40 dB.
  CHECK(peak(ob, start + 900, start + 2000) < 0.05f * peak(oa, start, start + 600));
  CHECK(peak(ob, start + 2600, start + 3000) < 0.01f * peak(oa, start, start + 600));
  // The step is a ramp: the largest sample-to-sample jump of b stays within what the carrier itself does (a hard cut would jump by the signal).
  float jump_b = 0.0f, jump_a = 0.0f;
  for (std::size_t i = start; i < start + 1000; ++i) {
    jump_b = std::fmax(jump_b, std::fabs(ob[i + 1] - ob[i]));
    jump_a = std::fmax(jump_a, std::fabs(oa[i + 1] - oa[i]));
  }
  CHECK(jump_b <= jump_a * 1.05f);
  // FOLLOW again: it comes back (the fade in), at a similar level.
  const std::size_t back = ob.size();
  p.block({dsp::param_change(Vocoder::CarSrc, 0.0f, 5)}, oa, ob);
  for (unsigned i = 0; i < 80; ++i) {
    p.block({}, oa, ob);
  }
  CHECK(peak(ob, back + 1500, back + 5000) > 0.5f * peak(oa, back + 1500, back + 5000));
}

TEST_CASE("carrier unit: SYNTH holds the synth, NOISE changes the sound, the choice holds across blocks until the next event") {
  Pair p;
  std::vector<float> oa, ob;
  const std::vector<dsp::Event> chord = {dsp::note_on(48, 100, 0, 0), dsp::note_on(55, 100, 0, 0)};
  Pair::run(p.a, 0, chord, oa, p.tr);
  Pair::run(p.b, 0, chord, ob, p.tr);
  p.n = kBlock;
  for (unsigned i = 0; i < 200; ++i) {
    p.block({}, oa, ob);
  }
  // SYNTH: the same carrier as FOLLOW with the NOISE knob at 0 (its default).
  p.block({dsp::param_change(Vocoder::CarSrc, 1.0f, 0)}, oa, ob);
  const std::size_t s0 = oa.size();
  for (unsigned i = 0; i < 100; ++i) {
    p.block({}, oa, ob);
  }
  float worst = 0.0f;
  for (std::size_t i = s0; i < oa.size(); ++i) {
    worst = std::fmax(worst, std::fabs(oa[i] - ob[i]));
  }
  CHECK(worst < 1e-4f);
  // NOISE: not the same sound any more, still a sound, and it stays so for many blocks with no further event.
  p.block({dsp::param_change(Vocoder::CarSrc, 2.0f, 10)}, oa, ob);
  const std::size_t n0 = oa.size();
  for (unsigned i = 0; i < 300; ++i) {
    p.block({}, oa, ob);
  }
  float diff = 0.0f;
  for (std::size_t i = n0 + 2000; i < oa.size(); ++i) {
    diff = std::fmax(diff, std::fabs(oa[i] - ob[i]));
  }
  CHECK(diff > 0.001f);
  CHECK(peak(ob, n0 + 2000, ob.size()) > 0.001f);
  CHECK(peak(ob, ob.size() - 640, ob.size()) > 0.001f);  // still sounding at the end: it held
}
