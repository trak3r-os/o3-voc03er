// SPDX-License-Identifier: GPL-3.0-or-later
// M2, MIDI slices: an unmarked NoteOn from Vocoder::kSliceNoteLow fires a slice of the published SPEAKS source at its frame, its velocity the
// slice's level; marked notes (the app's own senders) and notes past the count are the carrier's. A real Vocoder in the engine, the way
// slice_lane_test.cpp builds one. Synthetic signals only; no card.
#include <doctest/doctest.h>

#include <o3_voc03er/stepseq.h>
#include <o3_voc03er/units/vocoder/loop.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/engine.h>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

namespace {

constexpr unsigned kBlock = 64;
constexpr std::uint32_t kPer = 4000;  // samples a slot
constexpr unsigned kReader = 1;       // the tone is in slot 2, which speaks and has the cuts
constexpr std::uint8_t kLow = static_cast<std::uint8_t>(Vocoder::kSliceNoteLow);

dsp::Event midi_on(unsigned note, unsigned vel, std::uint16_t frame = 0) {
  return dsp::note_on(static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(vel), 0, frame);
}
dsp::Event midi_off(unsigned note, std::uint16_t frame = 0) { return dsp::note_off(static_cast<std::uint8_t>(note), frame); }
// A note of the slice channel as the app sends it (MS1).
dsp::Event slice_on(unsigned note, unsigned vel = 100, std::uint16_t frame = 0) {
  return dsp::note_on(static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(vel), Vocoder::kNoteSlice, frame);
}
dsp::Event slice_off(unsigned note) {
  return dsp::Event{0, dsp::EventKind::NoteOff, static_cast<std::uint8_t>(note), 0u, Vocoder::kNoteSlice, 0u, 0.0f};
}
dsp::Event pad_on(unsigned note, unsigned vel = 100) {
  return dsp::note_on(static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(vel), Vocoder::kNoteLocal, 0);
}

struct Rig {
  dsp::Engine e;
  Vocoder v;
  std::vector<std::int16_t> mem;
  SliceMap map = {};
  dsp::UnitId id = dsp::kNoUnit;
  bool ok = false;
  // `cuts` slices of 1000 samples published for kReader (0: none published); `bind`: the unit gets the map.
  explicit Rig(unsigned cuts, bool bind = true) : mem(static_cast<std::size_t>(Loop::kCount) * kPer, std::int16_t{0}) {
    for (std::uint32_t k = 0; k < kPer; ++k) {
      mem[kReader * kPer + k] = static_cast<std::int16_t>(2500.0f * std::sin(6.2831853f * 220.0f * static_cast<float>(k) / 44100.0f));
    }
    v.bind_loops(mem.data(), kPer);
    if (bind) {
      v.bind_slices(&map);
    }
    id = e.attach(v);
    ok = e.set_source(0, id);
    e.start();
    publish(cuts);
    (void)e.set_param(id, Vocoder::CarSrc, 1.0f);  // SYNTH
    (void)e.set_param(id, Vocoder::ModSrc, static_cast<float>(3u + kReader));
    (void)e.inject(id, pad_on(48));  // a held carrier note
  }
  void publish(unsigned cuts) {
    __atomic_store_n(&map.source, 0u, __ATOMIC_RELEASE);
    for (unsigned i = 0; i < kSliceMax; ++i) {
      map.start[i] = kReader * kPer + 1000u * (i % 4u);
      map.len[i] = 1000u;
    }
    __atomic_store_n(&map.source, cuts == 0u ? 0u : (kReader | cuts << 8), __ATOMIC_RELEASE);
  }
  // Renders one block into `l`; returns its energy over [from, to).
  float block(std::vector<float>& l, unsigned from = 0, unsigned to = kBlock) {
    l.assign(kBlock, 0.0f);
    std::vector<float> r(kBlock, 0.0f);
    e.render(nullptr, nullptr, l.data(), r.data(), kBlock);
    float s = 0.0f;
    for (unsigned i = from; i < to; ++i) {
      s += l[i] * l[i];
    }
    return s;
  }
  float rms(unsigned blocks) {
    std::vector<float> l;
    float s = 0.0f;
    for (unsigned b = 0; b < blocks; ++b) {
      s += block(l);
    }
    return std::sqrt(s / static_cast<float>(blocks * kBlock));
  }
};

}  // namespace

TEST_CASE("midi slices: an unmarked note in range fires the published reader at its frame") {
  Rig r(4);
  REQUIRE(r.ok);
  CHECK(r.rms(3) < 1.0e-4f);  // a held carrier and nothing speaking
  REQUIRE_FALSE(r.v.loop_status(kReader).region);
  REQUIRE(r.e.inject(r.id, midi_on(kLow + 1, 100, 40)));  // slice 2, frame 40 of the next block
  std::vector<float> l;
  (void)r.block(l);
  CHECK(r.v.loop_status(kReader).region);
  float before = 0.0f;
  for (unsigned i = 0; i < 40; ++i) {
    before += l[i] * l[i];
  }
  CHECK(before < 1.0e-8f);  // nothing before the note's frame
  CHECK(r.rms(4) > 1.0e-3f);  // and the slice speaks after it
  const std::uint32_t pos = r.v.loop_status(kReader).position;
  CHECK(pos >= kPer + 1000u);  // the second cut (starts repeat every four in this map)
  CHECK(pos < kPer + 1000u + 1000u);
  // the same note at frame 0 is heard earlier in its block
  Rig r0(4);
  (void)r0.rms(3);
  REQUIRE(r0.e.inject(r0.id, midi_on(kLow + 1, 100, 0)));
  std::vector<float> l0;
  (void)r0.block(l0);
  float tail = 0.0f, tail0 = 0.0f;
  for (unsigned i = 40; i < kBlock; ++i) {
    tail += l[i] * l[i];
    tail0 += l0[i] * l0[i];
  }
  CHECK(tail0 > tail);
}

TEST_CASE("midi slices: a marked note of the same number plays the carrier and fires nothing") {
  Rig r(4);
  REQUIRE(r.ok);
  REQUIRE(r.e.inject(r.id, pad_on(kLow)));
  (void)r.rms(4);
  CHECK_FALSE(r.v.loop_status(kReader).region);
  CHECK(r.v.carrier().holds(kLow));
  CHECK(r.rms(4) < 1.0e-4f);
  // an unmarked one fires the slice and the carrier never hears it
  Rig m(4);
  REQUIRE(m.e.inject(m.id, midi_on(kLow, 100)));
  (void)m.rms(2);
  CHECK(m.v.loop_status(kReader).region);
  CHECK_FALSE(m.v.carrier().holds(kLow));
  CHECK(m.v.carrier().holds(48));
}

TEST_CASE("midi slices: a note past the count, and a note with no source, are the carrier's") {
  Rig r(4);
  REQUIRE(r.e.inject(r.id, midi_on(kLow + 4, 100)));  // 36..39 are slices; 40 is not
  REQUIRE(r.e.inject(r.id, midi_on(kLow - 1, 100)));
  (void)r.rms(3);
  CHECK_FALSE(r.v.loop_status(kReader).region);
  CHECK(r.v.carrier().holds(kLow + 4));
  CHECK(r.v.carrier().holds(kLow - 1));
  Rig n(0);  // a map is bound but nothing is published
  REQUIRE(n.e.inject(n.id, midi_on(kLow, 100)));
  (void)n.rms(3);
  CHECK_FALSE(n.v.loop_status(kReader).region);
  CHECK(n.v.carrier().holds(kLow));
}

TEST_CASE("midi slices: velocity is the level, and a Param trigger afterwards is at full level") {
  auto level = [](unsigned vel) {
    Rig r(4);
    (void)r.rms(2);
    (void)r.e.inject(r.id, midi_on(kLow, vel));
    (void)r.rms(1);
    return r.rms(8);
  };
  const float full = level(100);
  const float half = level(50);
  const float over = level(127);
  REQUIRE(full > 1.0e-3f);
  CHECK(half / full > 0.35f);
  CHECK(half / full < 0.65f);
  CHECK(over == full);  // 100 and up are the same level
  // a soft slice, then the lane's way (Start, Len, Trig by Param): full level, the sound of a full slice before it
  Rig soft(4), ref(4);
  for (unsigned i = 0; i < 2; ++i) {
    Rig& p = i == 0 ? soft : ref;
    (void)p.rms(2);
    (void)p.e.inject(p.id, midi_on(kLow, i == 0 ? 40u : 100u));
    (void)p.rms(2500);  // past its end and the followers' and the AGC's tails (500 ms release)
    REQUIRE_FALSE(p.v.loop_status(kReader).region);
    (void)p.e.set_param(p.id, Vocoder::region_param(kReader, Vocoder::RegionField::Start), static_cast<float>(kPer));
    (void)p.e.set_param(p.id, Vocoder::region_param(kReader, Vocoder::RegionField::Len), 1000.0f);
    (void)p.e.set_param(p.id, Vocoder::region_param(kReader, Vocoder::RegionField::Trig), 1.0f);
  }
  const float a = soft.rms(8);
  const float b = ref.rms(8);
  CHECK(a > 1.0e-3f);
  CHECK(a == doctest::Approx(b).epsilon(1.0e-4));
}

TEST_CASE("midi slices: at full level the slice is the Param path's, sample for sample") {
  Rig a(4), b(4);
  (void)a.rms(2);
  (void)b.rms(2);
  REQUIRE(a.e.inject(a.id, midi_on(kLow + 1, 100, 17)));
  (void)b.e.set_param(b.id, Vocoder::region_param(kReader, Vocoder::RegionField::Start), static_cast<float>(kPer + 1000u));
  (void)b.e.set_param(b.id, Vocoder::region_param(kReader, Vocoder::RegionField::Len), 1000.0f);
  REQUIRE(b.e.inject(b.id, dsp::param_change(Vocoder::region_param(kReader, Vocoder::RegionField::Trig), 1.0f, 17)));
  std::vector<float> la, lb;
  for (unsigned i = 0; i < 20; ++i) {
    (void)a.block(la);
    (void)b.block(lb);
    CHECK(la == lb);
  }
}

TEST_CASE("midi slices: the slice sounding keeps its level until the new one's frame") {
  Rig a(4), b(4);  // a: a soft slice, then a full one at frame 50 of a later block; b: the soft slice alone
  for (Rig* p : {&a, &b}) {
    (void)p->rms(2);
    (void)p->e.inject(p->id, midi_on(kLow, 50));
    (void)p->rms(2);
  }
  REQUIRE(a.e.inject(a.id, midi_on(kLow + 1, 127, 50)));
  std::vector<float> la, lb;
  (void)a.block(la);
  (void)b.block(lb);
  for (unsigned i = 0; i < 50; ++i) {
    CHECK(la[i] == doctest::Approx(lb[i]).epsilon(1.0e-4).scale(1.0));  // (the run splits at 50: rounding only) before the new note's frame the soft slice sounds as it did
  }
}

TEST_CASE("midi slices: a NoteOff of a fired note does not end the slice or release a marked note of its number") {
  Rig r(4);
  REQUIRE(r.e.inject(r.id, pad_on(kLow)));  // a pad holds the same number
  (void)r.rms(2);
  REQUIRE(r.e.inject(r.id, midi_on(kLow, 100)));
  (void)r.rms(1);
  REQUIRE(r.v.loop_status(kReader).region);
  REQUIRE(r.e.inject(r.id, midi_off(kLow)));  // the key is let go
  (void)r.rms(1);
  CHECK(r.v.loop_status(kReader).region);  // one shot
  CHECK(r.v.carrier().holds(kLow));        // the pad's note is still on
  // the pad's own (marked) NoteOff releases it
  REQUIRE(r.e.inject(r.id, Vocoder::note_off_local(kLow)));
  (void)r.rms(1);
  CHECK_FALSE(r.v.carrier().holds(kLow));
}

TEST_CASE("midi slices: a source that changes under a held key leaves no stuck carrier note") {
  Rig r(0);  // no source: the note is the carrier's
  REQUIRE(r.e.inject(r.id, midi_on(kLow, 100)));
  (void)r.rms(2);
  REQUIRE(r.v.carrier().holds(kLow));
  r.publish(4);  // a source appears while the key is down
  REQUIRE(r.e.inject(r.id, midi_off(kLow)));
  (void)r.rms(1);
  CHECK_FALSE(r.v.carrier().holds(kLow));  // its NoteOff was not fired, so it is the carrier's
  // and the other way: a fired note whose source then goes away does not touch the carrier at its off
  REQUIRE(r.e.inject(r.id, midi_on(kLow, 100)));
  (void)r.rms(1);
  CHECK(r.v.loop_status(kReader).region);
  r.publish(0);
  REQUIRE(r.e.inject(r.id, midi_off(kLow)));
  (void)r.rms(1);
  CHECK_FALSE(r.v.carrier().holds(kLow));
}

TEST_CASE("midi slices: AllNotesOff clears the fired notes and does not stop a slice") {
  Rig r(4);
  REQUIRE(r.e.inject(r.id, midi_on(kLow, 100)));
  (void)r.rms(1);
  REQUIRE(r.v.loop_status(kReader).region);
  REQUIRE(r.e.inject(r.id, dsp::all_notes_off()));
  (void)r.rms(1);
  CHECK(r.v.loop_status(kReader).region);
  // the fired bit is gone: a later unmarked NoteOff of that number is the carrier's (a held pad note of it is released)
  REQUIRE(r.e.inject(r.id, pad_on(kLow)));
  REQUIRE(r.e.inject(r.id, midi_off(kLow)));
  (void)r.rms(1);
  CHECK_FALSE(r.v.carrier().holds(kLow));
}

TEST_CASE("midi slices: a reader that is recording refuses the note") {
  Rig r(4);
  REQUIRE(r.e.set_param(r.id, Vocoder::loop_param(kReader, Vocoder::LoopField::Rec), 1.0f));  // ARM (the transport is stopped: at once)
  (void)r.rms(2);
  REQUIRE(r.v.loop_status(kReader).rec != Vocoder::LoopStatus::Rec::Idle);
  REQUIRE(r.e.inject(r.id, midi_on(kLow, 100)));
  (void)r.rms(2);
  CHECK_FALSE(r.v.loop_status(kReader).region);
  CHECK_FALSE(r.v.carrier().holds(kLow));  // it does nothing and does not fall to the carrier
}

TEST_CASE("midi slices: with no map bound the notes are the carrier's, as before") {
  Rig a(4, false), b(4, true);
  b.v.bind_slices(nullptr);
  std::vector<float> la, lb;
  for (unsigned i = 0; i < 6; ++i) {
    if (i == 2) {
      REQUIRE(a.e.inject(a.id, midi_on(kLow, 90, 5)));
      REQUIRE(b.e.inject(b.id, midi_on(kLow, 90, 5)));
    }
    (void)a.block(la);
    (void)b.block(lb);
    CHECK(la == lb);
  }
  CHECK(a.v.carrier().holds(kLow));
  CHECK_FALSE(a.v.loop_status(kReader).region);
  CHECK(Vocoder::note_off_local(60, 3).flags == Vocoder::kNoteLocal);
  CHECK(Vocoder::note_off_local(60, 3).kind == dsp::EventKind::NoteOff);
}

TEST_CASE("midi slices: the step sequencer's notes carry the mark and its SLICE lane fires through the shared source") {
  // The unit's side of the hook is in stepseq_test.cpp (events carry kNoteLocal) and slice_lane_test.cpp (the lane plays); this checks the
  // word both read.
  SliceMap m = {};
  Bank bank = {};
  StepSeq seq;
  seq.init(dsp::kNoUnit, &bank, &m);
  CHECK(seq.slice_reader() == StepSeq::kNoReader);
  SliceMap* w = seq.slices_open();
  REQUIRE(w == &m);
  seq.slices_close(2, 30);  // clamped to kSliceMax
  CHECK(m.source == (2u | kSliceMax << 8));
  CHECK(seq.slice_count() == kSliceMax);
  CHECK(seq.slice_reader() == 2u);
  (void)seq.slices_open();
  CHECK(m.source == 0u);
}

// ---- MS1: slices on their own channel --------------------------------------------------------------------------------------------------

TEST_CASE("slice channel: SAME (the default) fires slices from the unmarked notes from 36") {
  Rig r(4);
  CHECK(r.v.midi_slices());
  REQUIRE(r.e.inject(r.id, midi_on(kLow, 100)));
  (void)r.rms(2);
  CHECK(r.v.loop_status(kReader).region);
  CHECK_FALSE(r.v.carrier().holds(kLow));
}

TEST_CASE("slice channel: with the route's slices off (OFF, or a slice channel) unmarked 36 and up play the carrier") {
  Rig r(4);
  r.v.set_midi_slices(false);
  CHECK_FALSE(r.v.midi_slices());
  REQUIRE(r.e.inject(r.id, midi_on(kLow, 100)));
  REQUIRE(r.e.inject(r.id, midi_on(kLow + 3, 100)));
  (void)r.rms(3);
  CHECK_FALSE(r.v.loop_status(kReader).region);
  CHECK(r.v.carrier().holds(kLow));
  CHECK(r.v.carrier().holds(kLow + 3));
  // and the key's release is the carrier's, not a fired slice's
  REQUIRE(r.e.inject(r.id, midi_off(kLow)));
  (void)r.rms(1);
  CHECK_FALSE(r.v.carrier().holds(kLow));
  // they record into PATTERN's notes-in ring like any carrier note
  Vocoder::NoteIn n;
  unsigned taken = 0;
  while (r.v.take_note_in(n)) {
    ++taken;
  }
  CHECK(taken == 2u);
}

TEST_CASE("slice channel: a flagged note fires the slice in every mode and is never the carrier's") {
  for (const bool route : {true, false}) {
    INFO("route fires slices: " << route);
    Rig r(4);
    r.v.set_midi_slices(route);
    REQUIRE(r.e.inject(r.id, slice_on(kLow + 1, 100, 40)));
    std::vector<float> l;
    (void)r.block(l);
    CHECK(r.v.loop_status(kReader).region);
    CHECK(r.rms(4) > 1.0e-3f);
    CHECK_FALSE(r.v.carrier().holds(kLow + 1));
    Vocoder::NoteIn n;
    CHECK_FALSE(r.v.take_note_in(n));  // the notes-in ring is for the carrier's notes: the NOTE lane never records a slice note
  }
}

TEST_CASE("slice channel: a flagged note out of the range or with no source does nothing, and is not the carrier's") {
  Rig r(4);
  r.v.set_midi_slices(false);
  REQUIRE(r.e.inject(r.id, slice_on(kLow + 4)));  // past the count
  REQUIRE(r.e.inject(r.id, slice_on(kLow - 1)));  // below the range
  (void)r.rms(3);
  CHECK_FALSE(r.v.loop_status(kReader).region);
  CHECK_FALSE(r.v.carrier().holds(kLow + 4));
  CHECK_FALSE(r.v.carrier().holds(kLow - 1));
  Rig n(0);
  REQUIRE(n.e.inject(n.id, slice_on(kLow)));
  (void)n.rms(3);
  CHECK_FALSE(n.v.loop_status(kReader).region);
  CHECK_FALSE(n.v.carrier().holds(kLow));
  Rig u(4, false);  // no map bound at all
  REQUIRE(u.e.inject(u.id, slice_on(kLow)));
  (void)u.rms(3);
  CHECK_FALSE(u.v.carrier().holds(kLow));
}

TEST_CASE("slice channel: a flagged NoteOff does nothing: a carrier note of the same number stays and the slice is one shot") {
  Rig r(4);
  r.v.set_midi_slices(false);
  REQUIRE(r.e.inject(r.id, midi_on(kLow, 100)));  // the user's channel plays the carrier note 36
  (void)r.rms(2);
  REQUIRE(r.v.carrier().holds(kLow));
  REQUIRE(r.e.inject(r.id, slice_on(kLow)));      // the slice channel's note 36 fires slice 1
  (void)r.rms(1);
  REQUIRE(r.v.loop_status(kReader).region);
  REQUIRE(r.e.inject(r.id, slice_off(kLow)));
  (void)r.rms(1);
  CHECK(r.v.loop_status(kReader).region);  // one shot
  CHECK(r.v.carrier().holds(kLow));        // the carrier note is still on
  // and the route's own NoteOff releases it
  REQUIRE(r.e.inject(r.id, midi_off(kLow)));
  (void)r.rms(1);
  CHECK_FALSE(r.v.carrier().holds(kLow));
}

TEST_CASE("slice channel: switching the route's slices off under a fired key leaves no stuck carrier note") {
  Rig r(4);
  REQUIRE(r.e.inject(r.id, midi_on(kLow, 100)));  // fires (SAME)
  (void)r.rms(1);
  r.v.set_midi_slices(false);
  REQUIRE(r.e.inject(r.id, midi_off(kLow)));      // its release is still the slice's
  (void)r.rms(1);
  CHECK_FALSE(r.v.carrier().holds(kLow));
}

TEST_CASE("slice channel: a reader that is recording refuses a flagged note too") {
  Rig r(4);
  REQUIRE(r.e.set_param(r.id, Vocoder::loop_param(kReader, Vocoder::LoopField::Rec), 1.0f));
  (void)r.rms(2);
  REQUIRE(r.v.loop_status(kReader).rec != Vocoder::LoopStatus::Rec::Idle);
  REQUIRE(r.e.inject(r.id, slice_on(kLow)));
  (void)r.rms(2);
  CHECK_FALSE(r.v.loop_status(kReader).region);
  CHECK_FALSE(r.v.carrier().holds(kLow));
}
