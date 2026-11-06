// SPDX-License-Identifier: GPL-3.0-or-later
// U6c, the SLICE lane: the SPEAKS slot's cuts published to the step sequencer (sample_logic.h publish_slices) and a step's slice played on the
// slot's own reader through a real Vocoder in the engine (stepseq.h hook). The hook's events, Stop and playing_slice() are in stepseq_test.cpp,
// the palette and the lane's editing in pattern_screen_test.cpp. Synthetic signals only; no card.
#include <doctest/doctest.h>

#include "../app/src/sample_logic.h"

#include <o3_voc03er/stepseq.h>
#include <o3_voc03er/units/vocoder/loop.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/engine.h>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;
namespace sm = o3_voc03er::sample;

namespace {

constexpr unsigned kBlock = 64;
constexpr std::uint32_t kPer = 4000;  // samples a slot
constexpr double kSixteenth = 5512.5;

sm::SliceTable cuts(std::uint32_t length, unsigned want) {
  sm::SliceTable t;
  sm::even_cuts(t, length, want);
  return t;
}

// ---- publishing ----

struct Pub {
  Bank bank = {};
  SliceMap map = {};
  StepSeq seq;
  sm::SlicePublished st;
  Pub() { seq.init(dsp::kNoUnit, &bank, &map); }
};

}  // namespace

TEST_CASE("publish: a slot with cuts shows its reader, count and bank-relative starts, once") {
  Pub p;
  CHECK(p.seq.slice_count() == 0u);
  CHECK(p.seq.slice_reader() == StepSeq::kNoReader);
  const sm::SliceTable t = cuts(4000, 4);
  CHECK(sm::publish_slices(p.seq, p.st, 1, &t, kPer));
  CHECK(p.seq.slice_count() == 4u);
  CHECK(p.seq.slice_reader() == 1u);
  for (unsigned i = 0; i < 4; ++i) {
    CHECK(p.map.start[i] == kPer + 1000u * i);  // slot 1 begins at kPer in the bank
    CHECK(p.map.len[i] == 1000u);
  }
  CHECK_FALSE(sm::publish_slices(p.seq, p.st, 1, &t, kPer));  // an idle pass publishes nothing
  CHECK_FALSE(sm::publish_slices(p.seq, p.st, 1, &t, kPer));
  CHECK(p.seq.slice_count() == 4u);
}

TEST_CASE("publish: a change of the SPEAKS slot or of the length republishes, nothing else does") {
  Pub p;
  const sm::SliceTable a = cuts(4000, 4);
  REQUIRE(sm::publish_slices(p.seq, p.st, 0, &a, kPer));
  CHECK(p.seq.slice_reader() == 0u);
  // SPEAKS moves to slot 3 (same table length): republished on the new reader
  CHECK(sm::publish_slices(p.seq, p.st, 2, &a, kPer));
  CHECK(p.seq.slice_reader() == 2u);
  CHECK(p.map.start[1] == 2u * kPer + 1000u);
  // the slot is recorded again, longer: republished with the new cuts
  const sm::SliceTable b = cuts(3000, 3);
  CHECK(sm::publish_slices(p.seq, p.st, 2, &b, kPer));
  CHECK(p.seq.slice_count() == 3u);
  CHECK(p.map.len[0] == 1000u);
  CHECK_FALSE(sm::publish_slices(p.seq, p.st, 2, &b, kPer));
  // an empty slot, SPEAKS LIVE (slot 99) or NONE (no table) publish "none"
  const sm::SliceTable empty;
  CHECK(sm::publish_slices(p.seq, p.st, 2, &empty, kPer));
  CHECK(p.seq.slice_count() == 0u);
  CHECK(p.seq.slice_reader() == StepSeq::kNoReader);
  CHECK_FALSE(sm::publish_slices(p.seq, p.st, 2, &empty, kPer));
  REQUIRE(sm::publish_slices(p.seq, p.st, 2, &b, kPer));
  CHECK(sm::publish_slices(p.seq, p.st, 99, nullptr, kPer));
  CHECK(p.seq.slice_count() == 0u);
  REQUIRE(sm::publish_slices(p.seq, p.st, 2, &b, kPer));
  CHECK(sm::publish_slices(p.seq, p.st, 99, &b, kPer));
  CHECK(p.seq.slice_count() == 0u);
}

TEST_CASE("publish: the map is hidden while it is rewritten, and a new one is whole when it shows") {
  Pub p;
  const sm::SliceTable a = cuts(4000, 4);
  REQUIRE(sm::publish_slices(p.seq, p.st, 0, &a, kPer));
  // what publish_slices does in its rewrite: count 0 first, so the hook sees no source until the new map is in
  SliceMap* m = p.seq.slices_open();
  REQUIRE(m == &p.map);
  CHECK(p.seq.slice_count() == 0u);
  m->start[0] = 12345u;  // half written
  CHECK(p.seq.slice_count() == 0u);
  p.seq.slices_close(0, 4);
  CHECK(p.seq.slice_count() == 4u);
  // a rewrite by publish_slices ends whole
  const sm::SliceTable b = cuts(3000, 3);
  REQUIRE(sm::publish_slices(p.seq, p.st, 1, &b, kPer));
  CHECK(p.map.start[0] == kPer);
  CHECK(p.map.start[2] == kPer + 2000u);
}

// ---- a step's slice through a real Vocoder ----

namespace {

struct Real {
  dsp::Engine e;
  Vocoder v;
  std::vector<std::int16_t> mem;
  Bank bank = {};
  SliceMap map = {};
  StepSeq seq;
  dsp::UnitId id = dsp::kNoUnit;
  bool ok = false;
  std::uint64_t t = 0;  // samples rendered
  // The tone sits in `tone_slot` only; the table is the even cuts of `cuts` for it. `speaks`: the slot that ModSrc names; `reader`: the slot
  // the cuts are published for.
  Real(unsigned tone_slot, unsigned speaks, unsigned reader, unsigned n_cuts) : mem(static_cast<std::size_t>(Loop::kCount) * kPer, std::int16_t{0}) {
    for (std::uint32_t k = 0; k < kPer; ++k) {
      mem[tone_slot * kPer + k] = static_cast<std::int16_t>(12000.0f * std::sin(6.2831853f * 220.0f * static_cast<float>(k) / 44100.0f));
    }
    v.bind_loops(mem.data(), kPer);
    id = e.attach(v);
    seq.init(id, &bank, &map);
    ok = e.set_source(0, id) && e.add_hook(seq);
    e.start();
    sm::SlicePublished st;
    const sm::SliceTable tab = cuts(kPer, n_cuts);
    (void)sm::publish_slices(seq, st, reader, &tab, kPer);
    (void)e.set_param(id, Vocoder::CarSrc, 1.0f);  // SYNTH
    (void)e.set_param(id, Vocoder::ModSrc, static_cast<float>(3u + speaks));
    (void)e.note_on(id, 48, 100);  // a held note: the carrier sounds
  }
  float rms_of(unsigned blocks) {
    float s = 0.0f;
    std::uint32_t n = 0;
    float l[kBlock], r[kBlock];
    for (unsigned b = 0; b < blocks; ++b) {
      e.render(nullptr, nullptr, l, r, kBlock);
      for (unsigned i = 0; i < kBlock; ++i) {
        s += l[i] * l[i];
      }
      n += kBlock;
      t += kBlock;
    }
    return std::sqrt(s / static_cast<float>(n));
  }
};

}  // namespace

TEST_CASE("slice lane: a step's slice speaks through the SPEAKS slot's reader, from that step on") {
  Real r(1, 1, 1, 4);  // the tone is in slot 2, which speaks, and the cuts are published for it
  REQUIRE(r.ok);
  REQUIRE(set_slice(r.bank.pattern[0], 2, 1));  // step 3: the first cut
  REQUIRE(r.e.play());
  const unsigned before = static_cast<unsigned>(2.0 * kSixteenth / kBlock) - 2u;  // blocks up to just before step 3
  const float quiet = r.rms_of(before);
  CHECK(quiet < 1.0e-4f);  // a held carrier and nothing speaking
  CHECK(r.v.loop_status(1).region == false);
  const float loud = r.rms_of(8);  // the step lands in the first of these blocks
  CHECK(loud > 20.0f * quiet + 1.0e-3f);
  CHECK(r.v.loop_status(1).region == true);
  const std::uint32_t pos = r.v.loop_status(1).position;
  CHECK(pos >= kPer);  // slot 2 is bank samples kPer .. 2 kPer - 1, the first cut its first quarter
  CHECK(pos < kPer + 1000u);
}

TEST_CASE("slice lane: the event goes to the slot's own reader, not to another one") {
  // The tone is in slot 2 and speaks; the cuts are published for slot 1: reader 0 plays silence and nothing is heard from slot 2.
  Real wrong(1, 1, 0, 4);
  REQUIRE(wrong.ok);
  REQUIRE(set_slice(wrong.bank.pattern[0], 0, 2));
  REQUIRE(wrong.e.play());
  CHECK(wrong.rms_of(40) < 1.0e-4f);
  CHECK(wrong.v.loop_status(1).region == false);
  // the same with the right reader is heard
  Real right(1, 1, 1, 4);
  REQUIRE(right.ok);
  REQUIRE(set_slice(right.bank.pattern[0], 0, 2));
  REQUIRE(right.e.play());
  CHECK(right.rms_of(40) > 1.0e-3f);
}

TEST_CASE("slice lane: another cut starts the reader at another place of the slot") {
  for (unsigned slice = 1; slice <= 4; ++slice) {
    Real r(2, 2, 2, 4);
    REQUIRE(r.ok);
    REQUIRE(set_slice(r.bank.pattern[0], 0, slice));
    REQUIRE(r.e.play());
    (void)r.rms_of(4);  // the step lands in the second block; a little of the region has played by now
    REQUIRE(r.v.loop_status(2).region);
    const std::uint32_t pos = r.v.loop_status(2).position;
    const std::uint32_t from = 2u * kPer + 1000u * (slice - 1u);
    CHECK(pos >= from);
    CHECK(pos < from + 400u);  // 3 blocks at most
  }
}

TEST_CASE("slice lane: Stop releases the region") {
  Real r(1, 1, 1, 4);
  REQUIRE(r.ok);
  REQUIRE(set_slice(r.bank.pattern[0], 0, 1));
  REQUIRE(r.e.play());
  (void)r.rms_of(4);
  REQUIRE(r.v.loop_status(1).region);
  CHECK(r.seq.playing_slice() == 1u);
  REQUIRE(r.e.stop());
  (void)r.rms_of(4);
  CHECK_FALSE(r.v.loop_status(1).region);
  CHECK(r.seq.playing_slice() == 0u);
}
