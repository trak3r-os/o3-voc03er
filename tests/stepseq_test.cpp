// SPDX-License-Identifier: GPL-3.0-or-later
// U6a, the pattern bank and the step sequencer (app/include/o3_voc03er/stepseq.h): the packed step, the editing functions, the file (v2 with
// all eight patterns, the old file into pattern A, a damaged one refused whole), and the clocked hook driven through the engine (lanes,
// LENGTH, wrap, a pattern change at the pattern's end, no note left hanging on Stop). Synthetic signals only; no card.
#include <doctest/doctest.h>

#include <o3_voc03er/stepseq.h>

#include <o3sdk/dsp/engine.h>

#include "../app/src/pattern_logic.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;
namespace chunk = o3sdk::formats::chunk;
namespace pt = o3_voc03er::pattern;

namespace {

constexpr unsigned kBlock = 64;
constexpr double kSixteenth = 5512.5;  // samples at 120 BPM, 44.1 kHz (the engine's default tempo)

constexpr std::array<o3sdk::core::ParamDesc, Vocoder::kParamCount> make_rows() {
  std::array<o3sdk::core::ParamDesc, Vocoder::kParamCount> rows{};
  for (auto& r : rows) {
    r = {"P", "", nullptr, nullptr, 0.0f, 7.0f, 0.0f, 7, 1, 0, o3sdk::core::ParamUnit::Int, o3sdk::core::ParamCurve::Linear, 0, 0};
  }
  return rows;
}
constexpr auto kRows = make_rows();

struct Seen {
  std::uint64_t at;  // engine sample
  dsp::EventKind kind;
  std::uint8_t note;
  std::uint8_t vel;
  float value;  // Param events only
  std::uint16_t param;
  std::uint8_t flags;  // notes: Vocoder::kNoteLocal when the app sent them
};

// An instrument that writes down every note and CarSrc event it is given, with the sample it lands on.
class Spy final : public dsp::Unit {
 public:
  Spy() : info_{"SPY", dsp::UnitKind::Instrument, kRows.data(), Vocoder::kParamCount, nullptr, 0} {}
  const dsp::UnitInfo& info() const noexcept override { return info_; }
  void prepare(float) noexcept override {}
  void process(const dsp::ProcessArgs& a) noexcept override {
    for (std::uint32_t i = 0; i < a.event_count; ++i) {
      const dsp::Event& e = a.events[i];
      const bool mine = e.param == Vocoder::CarSrc || (e.param >= Vocoder::R1Start && e.param <= Vocoder::R4Trig);  // the hook's: CARRIER and the regions
      if (e.kind == dsp::EventKind::Param && (!mine || block_ == 0)) {
        continue;  // block 0 carries the engine's stored values, not the hook's events
      }
      seen.push_back({block_ * kBlock + e.frame, e.kind, e.note, e.velocity, e.value, e.param, e.flags});
    }
    ++block_;
    for (std::uint32_t i = 0; i < a.frames; ++i) {
      a.out_l[i] = 0.0f;
      a.out_r[i] = 0.0f;
    }
  }
  std::vector<Seen> seen;
  std::uint64_t block_ = 0;

 private:
  dsp::UnitInfo info_;
};

struct Rig {
  Rig() {
    id = e.attach(spy);
    seq.init(id, &bank, &map);
    ok = e.set_source(0, id) && e.add_hook(seq);
    e.start();
  }
  void run(unsigned blocks) {
    float l[kBlock], r[kBlock];
    for (unsigned i = 0; i < blocks; ++i) {
      e.render(nullptr, nullptr, l, r, kBlock);
    }
  }
  void run_samples(double n) { run(static_cast<unsigned>(n / kBlock) + 1u); }
  std::vector<Seen> of(dsp::EventKind k) const {
    std::vector<Seen> v;
    for (const Seen& s : spy.seen) {
      if (s.kind == k) {
        v.push_back(s);
      }
    }
    return v;
  }
  // The notes still sounding at the end of what the spy saw: an on without its off.
  unsigned hanging() const {
    int held[128] = {};
    for (const Seen& s : spy.seen) {
      if (s.kind == dsp::EventKind::NoteOn) {
        ++held[s.note];
      } else if (s.kind == dsp::EventKind::NoteOff) {
        --held[s.note];
      }
    }
    unsigned n = 0;
    for (int h : held) {
      n += h != 0 ? 1u : 0u;
    }
    return n;
  }
  // The SPEAKS slot's slices as the sample screen would publish them: reader `rd`, `n` cuts, cut i starting at 1000 + 100 i, 500 + i long.
  void publish(unsigned rd, unsigned n) {
    SliceMap* m = seq.slices_open();
    REQUIRE(m == &map);
    for (unsigned i = 0; i < kSliceMax; ++i) {
      m->start[i] = 1000u + 100u * i;
      m->len[i] = 500u + i;
    }
    seq.slices_close(rd, n);
  }
  // The region events of one reader, in order, as (field, value).
  std::vector<Seen> region(unsigned rd) const {
    std::vector<Seen> v;
    for (const Seen& x : spy.seen) {
      if (x.kind == dsp::EventKind::Param && x.param >= Vocoder::region_param(rd, Vocoder::RegionField::Start) &&
          x.param <= Vocoder::region_param(rd, Vocoder::RegionField::Trig)) {
        v.push_back(x);
      }
    }
    return v;
  }
  dsp::Engine e;
  Spy spy;
  Bank bank = {};
  SliceMap map = {};
  StepSeq seq;
  dsp::UnitId id = dsp::kNoUnit;
  bool ok = false;
};

// The engine starts the transport one block after play(): every step lands kBlock samples after its grid point.
bool near(std::uint64_t at, double want) { const double a = static_cast<double>(at) - static_cast<double>(kBlock); return a + 1.0 >= want && a <= want + 1.0; }
// The tract spy counts the samples rendered (the start's zero-frame call adds none), so its `at` is the grid's own sample, with no lag.
bool at_near(std::uint64_t at, double want, double tol = 1.0) {
  const double a = static_cast<double>(at);
  return a + tol >= want && a <= want + tol;
}

Step chord_step(unsigned root, ChordKind k, Voicing v) {
  Step s;
  s.note = static_cast<std::uint8_t>(root);
  s.kind = static_cast<std::uint8_t>(static_cast<unsigned>(k) + 1u);
  s.voicing = static_cast<std::uint8_t>(v);
  return s;
}

// A bank with something different in every pattern.
Bank full_bank() {
  Bank b = {};
  for (unsigned p = 0; p < kBankPatterns; ++p) {
    PatternData& d = b.pattern[p];
    for (unsigned i = 0; i < kPatternSteps; ++i) {
      Step s;
      s.note = static_cast<std::uint8_t>((p * 13u + i * 7u) % 128u);
      s.kind = static_cast<std::uint8_t>((p + i) % 5u);
      s.voicing = static_cast<std::uint8_t>((p * 3u + i) % 4u);
      s.carrier = static_cast<std::uint8_t>((p + i * 2u) % (kCarrierMax + 1u));
      s.level = static_cast<std::uint8_t>((p * 17u + i * 5u) % 128u);
      s.slice = static_cast<std::uint8_t>((p * 16u + i) % (kSliceMax + 1u));
      REQUIRE(set_step(d, i, s));
    }
    REQUIRE(set_length(d, 1u + (p * 5u) % kPatternSteps));
  }
  return b;
}

bool same_bank(const Bank& a, const Bank& b) {
  for (unsigned p = 0; p < kBankPatterns; ++p) {
    if (!same_pattern(a.pattern[p], b.pattern[p])) {
      return false;
    }
  }
  return true;
}

std::vector<std::uint8_t> bytes_of(const Bank& b) {
  std::vector<std::uint8_t> buf(kBankFileMax);
  const std::size_t n = write_bank(b, buf.data(), buf.size());
  REQUIRE(n != 0u);
  buf.resize(n);
  return buf;
}

}  // namespace

// ---- The step and the editing --------------------------------------------------------------------------------------------------

TEST_CASE("step: every field round trips through the word, and the zero word is a valid empty step") {
  const Step z = unpack_step(0u);
  CHECK(step_valid(z));
  CHECK(z.note == 0);
  CHECK(step_velocity(z) == kLevelDefault);  // LEVEL 0 is the default
  Step s;
  s.note = 127;
  s.kind = 4;
  s.voicing = 3;
  s.carrier = 6;
  s.level = 127;
  s.slice = 24;
  const Step back = unpack_step(pack_step(s));
  CHECK(back.note == 127);
  CHECK(back.kind == 4);
  CHECK(back.voicing == 3);
  CHECK(back.carrier == 6);
  CHECK(back.level == 127);
  CHECK(back.slice == 24);
  CHECK(step_velocity(back) == 127);
  CHECK_FALSE(step_valid(Step{128, 0, 0, 0, 0, 0}));
  CHECK_FALSE(step_valid(Step{1, 5, 0, 0, 0, 0}));
  CHECK_FALSE(step_valid(Step{1, 0, 4, 0, 0, 0}));
  CHECK(step_valid(Step{1, 0, 0, 7, 0, 0}));        // LIVE (7, LC1) is the last carrier
  CHECK_FALSE(step_valid(Step{1, 0, 0, 8, 0, 0}));  // 8 does not exist
  CHECK_FALSE(step_valid(Step{1, 0, 0, 0, 128, 0}));
  CHECK_FALSE(step_valid(Step{1, 0, 0, 0, 0, 25}));  // SLICE is 0..24
  CHECK(step_valid(Step{1, 0, 0, 0, 0, 24}));
}

TEST_CASE("editing: the lanes change one field each, a bad value or step changes nothing") {
  Bank b = {};
  PatternData& p = b.pattern[0];
  CHECK(length_of(p) == 16);  // the zero state: 16
  REQUIRE(set_chord(p, 3, 60, ChordKind::Seventh, Voicing::Wide));
  REQUIRE(set_carrier(p, 3, 5));
  REQUIRE(set_level(p, 3, 64));
  Step s = get_step(p, 3);
  CHECK(s.note == 60);
  CHECK(s.kind == 2);
  CHECK(s.voicing == static_cast<unsigned>(Voicing::Wide));
  CHECK(s.carrier == 5);
  CHECK(s.level == 64);
  REQUIRE(set_note(p, 3, 40));  // a single note replaces the chord and keeps the other lanes
  s = get_step(p, 3);
  CHECK(s.note == 40);
  CHECK(s.kind == 0);
  CHECK(s.carrier == 5);
  CHECK(s.level == 64);
  REQUIRE(set_rest(p, 3));
  CHECK(get_step(p, 3).note == 0);
  const std::uint32_t before = load_word(p.step[3]);
  CHECK_FALSE(set_note(p, 3, 128));
  CHECK_FALSE(set_note(p, 16, 40));
  CHECK_FALSE(set_chord(p, 3, 0, ChordKind::Triad, Voicing::Close));  // a chord needs a root
  CHECK_FALSE(set_carrier(p, 3, kCarrierMax + 1u));
  CHECK_FALSE(set_level(p, 3, 128));
  CHECK_FALSE(set_length(p, 0));
  CHECK_FALSE(set_length(p, 17));
  CHECK(load_word(p.step[3]) == before);
  CHECK(length_of(p) == 16);
  REQUIRE(set_length(p, 5));
  CHECK(length_of(p) == 5);
  for (unsigned i = 0; i < kCarrierMax + 1u; ++i) {  // FOLLOW, SYNTH, NOISE, SLOT 1..4, LIVE, FOLLOW
    CHECK(get_step(p, 7).carrier == i % (kCarrierMax + 1u));
    REQUIRE(cycle_carrier(p, 7));
  }
  CHECK(get_step(p, 7).carrier == 0);
  clear_pattern(p);
  CHECK(notes_used(p) == 0);
  CHECK(carriers_used(p) == 0);
}

TEST_CASE("editing: recording moves the cursor on and wraps at the pattern's LENGTH") {
  Bank b = {};
  PatternData& p = b.pattern[2];
  REQUIRE(set_length(p, 3));
  unsigned cur = 0;
  record_note(p, cur, 50);
  record_note(p, cur, 0);
  record_note(p, cur, 52);
  CHECK(cur == 0);
  record_note(p, cur, 200);  // out of range: a rest
  CHECK(get_step(p, 0).note == 0);
  CHECK(get_step(p, 2).note == 52);
  CHECK(get_step(p, 3).note == 0);  // beyond LENGTH nothing was written
  CHECK(notes_used(p) == 1);
}

TEST_CASE("step notes: a chord step is make_chord's chord, a single note is itself, a rest is nothing") {
  std::uint8_t out[kChordMaxNotes] = {};
  CHECK(step_notes(Step{}, 0, 0, out, nullptr) == 0);
  CHECK(step_notes(Step{57, 0, 0, 0, 0, 0}, 0, 0, out, nullptr) == 1);
  CHECK(out[0] == 57);
  for (unsigned kind = 0; kind < 4; ++kind) {
    for (unsigned root : {36u, 48u, 61u, 71u, 83u}) {
      const Chord want = make_chord(static_cast<ChordKind>(kind), root % 12u, static_cast<int>(root / 12u) - 4, 2, 1, Voicing::Open);
      const unsigned n = step_notes(chord_step(root, static_cast<ChordKind>(kind), Voicing::Open), 2, 1, out, nullptr);
      REQUIRE(n == want.count);
      REQUIRE(n >= 1);
      for (unsigned i = 0; i < n; ++i) {
        CHECK(out[i] == want.notes[i]);
      }
    }
  }
  // The root of the chord is the step's MIDI note in the C major triad: octave 0 starts at 48 (kChordBaseNote).
  CHECK(step_notes(chord_step(60, ChordKind::Triad, Voicing::Close), 0, 0, out, nullptr) == 3);
  CHECK(out[0] == 60);
}

// ---- The file ------------------------------------------------------------------------------------------------------------------

TEST_CASE("bank file: all eight patterns round trip, lengths and every lane, and it fits its buffer") {
  const Bank b = full_bank();
  const std::vector<std::uint8_t> f = bytes_of(b);
  CHECK(f.size() <= kBankFileMax);
  Bank got = {};
  REQUIRE(read_bank(f.data(), f.size(), got));
  CHECK(same_bank(b, got));
  CHECK(get_step(got.pattern[5], 4).slice == get_step(b.pattern[5], 4).slice);  // the reserved lane is kept
}

TEST_CASE("bank file: an older reader gets pattern A's notes and carriers from the v2 file") {
  Bank b = full_bank();
  Pattern old;
  REQUIRE(read_pattern(bytes_of(b).data(), bytes_of(b).size(), old));
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    CHECK(old.note[i] == get_step(b.pattern[0], i).note);
    CHECK(old.carrier[i] == get_step(b.pattern[0], i).carrier);
  }
}

TEST_CASE("bank file: the old file loads into pattern A and leaves B to H alone") {
  Pattern old;
  for (int n : {36, 0, 48, 60}) {
    old.record(n);
  }
  old.carrier[2] = 3;
  std::uint8_t buf[kPatternFileMax] = {};
  const std::size_t n = write_pattern(old, buf, sizeof buf);
  REQUIRE(n != 0);
  Bank b = full_bank();
  const PatternData keep = b.pattern[3];
  REQUIRE(read_bank(buf, n, b));
  CHECK(get_step(b.pattern[0], 0).note == 36);
  CHECK(get_step(b.pattern[0], 2).note == 48);
  CHECK(get_step(b.pattern[0], 2).carrier == 3);
  CHECK(get_step(b.pattern[0], 2).kind == 0);
  CHECK(length_of(b.pattern[0]) == 16);
  CHECK(same_pattern(b.pattern[3], keep));
}

TEST_CASE("bank file: a file that fails a check loads nothing") {
  const Bank b = full_bank();
  const std::vector<std::uint8_t> good = bytes_of(b);
  Bank target = {};
  (void)set_note(target.pattern[1], 0, 77);
  const Bank before = target;
  auto refused = [&](const std::vector<std::uint8_t>& f) {
    Bank t = before;
    const bool ok = read_bank(f.data(), f.size(), t);
    return !ok && same_bank(t, before);
  };
  CHECK(refused({}));
  std::vector<std::uint8_t> bad = good;
  bad[bad.size() / 2] ^= 0x40u;  // a flipped bit: the CRC
  CHECK(refused(bad));
  bad = good;
  bad.resize(bad.size() - 7);  // a cut file
  CHECK(refused(bad));
  // A well-formed container with a BANK that is wrong: built by hand so the CRC is right.
  auto with_bank = [&](unsigned count, unsigned length, std::uint32_t word) {
    std::uint8_t out[kBankFileMax] = {};
    chunk::Writer w(out, sizeof out, "PATN");
    const std::uint8_t notes[kPatternSteps] = {};
    REQUIRE(w.begin("STEP"));
    REQUIRE(w.u16(0x0100));
    REQUIRE(w.u8(kPatternSteps));
    REQUIRE(w.bytes(notes, sizeof notes));
    REQUIRE(w.end());
    REQUIRE(w.begin("BANK"));
    REQUIRE(w.u16(0x0100));
    REQUIRE(w.u8(static_cast<std::uint8_t>(count)));
    for (unsigned p = 0; p < count; ++p) {
      REQUIRE(w.u8(static_cast<std::uint8_t>(p == 4 ? length : 16u)));
      for (unsigned i = 0; i < kPatternSteps; ++i) {
        REQUIRE(w.u32(p == 6 && i == 9 ? word : 0u));
      }
    }
    REQUIRE(w.end());
    const std::size_t n = w.finish();
    REQUIRE(n != 0);
    return std::vector<std::uint8_t>(out, out + n);
  };
  Bank t = {};
  const std::vector<std::uint8_t> fine = with_bank(8, 16, 0u);
  CHECK(read_bank(fine.data(), fine.size(), t));  // the builder itself is right
  CHECK(refused(with_bank(7, 16, 0u)));            // a wrong pattern count
  CHECK(refused(with_bank(8, 0, 0u)));             // LENGTH 0
  CHECK(refused(with_bank(8, 17, 0u)));            // LENGTH 17
  // the carrier field is three bits and 7 (LIVE, LC1) is valid: no pattern file can hold a bad carrier
  CHECK(refused(with_bank(8, 16, 0x00000080u)));                                     // note 128
  CHECK(refused(with_bank(8, 16, 5u << 8)));                                         // kind 5
  CHECK(refused(with_bank(8, 16, 6u << 8)));                                         // kind 6
  CHECK(refused(with_bank(8, 16, (7u << 8) | 60u)));                                 // kind 7 (a TIE) with a note
  CHECK(refused(with_bank(8, 16, (7u << 8) | (1u << 11))));                          // a TIE with a voicing
  const std::vector<std::uint8_t> tie = with_bank(8, 16, 7u << 8);
  CHECK(read_bank(tie.data(), tie.size(), t));                                       // a TIE: note 0, kind 7, loads
  CHECK(is_tie(get_step(t.pattern[6], 9)));
  CHECK(refused(with_bank(8, 16, 25u << 24)));                                       // slice 25
  const std::vector<std::uint8_t> s24 = with_bank(8, 16, 24u << 24);
  CHECK(read_bank(s24.data(), s24.size(), t));                                       // slice 24 is the last one
  CHECK(get_step(t.pattern[6], 9).slice == 24u);
}

// ---- The hook ------------------------------------------------------------------------------------------------------------------

TEST_CASE("step hook: NOTE, CARRIER and LEVEL lanes land on the step's frame, the note ends at the gate") {
  Rig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_level(a, 0, 90));
  REQUIRE(set_chord(a, 2, 60, ChordKind::Triad, Voicing::Close));
  REQUIRE(set_carrier(a, 2, 3));  // SLOT 1
  rig.seq.set_key_scale(0, 0);
  REQUIRE(rig.e.play());
  rig.run_samples(4 * kSixteenth);
  const std::vector<Seen> on = rig.of(dsp::EventKind::NoteOn);
  const std::vector<Seen> off = rig.of(dsp::EventKind::NoteOff);
  REQUIRE(on.size() == 4);  // step 0's note, then step 2's three
  CHECK(on[0].note == 60);
  CHECK(on[0].vel == 90);  // LEVEL is the velocity
  for (const Seen& s : on) {
    CHECK(s.flags == Vocoder::owner_flags(Vocoder::Owner::Seq));  // the unit tells the hook's notes from a MIDI key by this mark, and holds them as the sequencer's (NO1)
  }
  for (const Seen& s : off) {
    CHECK(s.flags == Vocoder::owner_flags(Vocoder::Owner::Seq));  // the NoteOff names the same owner
  }
  CHECK(on[0].at == kBlock);
  std::uint8_t want[kChordMaxNotes] = {};
  REQUIRE(step_notes(chord_step(60, ChordKind::Triad, Voicing::Close), 0, 0, want, nullptr) == 3);
  for (unsigned i = 0; i < 3; ++i) {
    CHECK(on[1 + i].note == want[i]);
    CHECK(on[1 + i].vel == kLevelDefault);  // LEVEL 0: the default
    CHECK(near(on[1 + i].at, 2 * kSixteenth));
  }
  REQUIRE(off.size() == 4);
  CHECK(off[0].note == 60);
  CHECK(near(off[0].at, 0.75 * kSixteenth));  // the gate: 75 % of the step, before the next
  for (unsigned i = 0; i < 3; ++i) {
    CHECK(near(off[1 + i].at, 2.75 * kSixteenth));
  }
  const std::vector<Seen> car = rig.of(dsp::EventKind::Param);
  REQUIRE(car.size() == 1);
  CHECK(car[0].value == 3.0f);
  CHECK(near(car[0].at, 2 * kSixteenth));  // the carrier changes on the same frame as the chord
  CHECK(rig.hanging() == 0);
}

TEST_CASE("step hook: LENGTH wraps the pattern, a longer pattern plays its steps") {
  Rig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 4));
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_note(a, 3, 62));
  REQUIRE(set_note(a, 4, 70));  // beyond LENGTH: never played
  REQUIRE(rig.e.play());
  rig.run_samples(9.5 * kSixteenth);  // two and a bit patterns of four
  const std::vector<Seen> on = rig.of(dsp::EventKind::NoteOn);
  REQUIRE(on.size() == 5);
  const std::uint8_t want[5] = {60, 62, 60, 62, 60};
  const double step[5] = {0, 3, 4, 7, 8};
  for (unsigned i = 0; i < 5; ++i) {
    CHECK(on[i].note == want[i]);
    CHECK(near(on[i].at, step[i] * kSixteenth));
  }
}

TEST_CASE("step hook: a requested pattern starts when the playing one ends, never in the middle") {
  Rig rig;
  REQUIRE(rig.ok);
  REQUIRE(set_length(rig.bank.pattern[0], 4));
  REQUIRE(set_length(rig.bank.pattern[1], 4));
  REQUIRE(set_note(rig.bank.pattern[0], 0, 60));
  REQUIRE(set_note(rig.bank.pattern[0], 2, 61));
  REQUIRE(set_note(rig.bank.pattern[1], 0, 72));
  REQUIRE(set_note(rig.bank.pattern[1], 2, 73));
  REQUIRE(rig.e.play());
  rig.run_samples(1.5 * kSixteenth);  // inside step 1 of A
  rig.seq.request_pattern(1);
  rig.run_samples(8.0 * kSixteenth);
  const std::vector<Seen> on = rig.of(dsp::EventKind::NoteOn);
  REQUIRE(on.size() >= 4);
  const std::uint8_t want[4] = {60, 61, 72, 73};  // A finishes its four steps, then B
  for (unsigned i = 0; i < 4; ++i) {
    CHECK(on[i].note == want[i]);
  }
  CHECK(near(on[2].at, 4 * kSixteenth));
  CHECK(rig.seq.playing_pattern() == 1);
}

TEST_CASE("step hook: a pattern requested while stopped plays from the next Play, from its first step") {
  Rig rig;
  REQUIRE(rig.ok);
  REQUIRE(set_note(rig.bank.pattern[2], 0, 66));
  REQUIRE(set_note(rig.bank.pattern[0], 0, 60));
  rig.seq.request_pattern(2);
  REQUIRE(rig.e.play());
  rig.run_samples(kSixteenth);
  const std::vector<Seen> on = rig.of(dsp::EventKind::NoteOn);
  REQUIRE(on.size() == 1);
  CHECK(on[0].note == 66);
}

TEST_CASE("step hook: an edit during playback is heard from the step's next visit, whole") {
  Rig rig;
  REQUIRE(rig.ok);
  REQUIRE(set_note(rig.bank.pattern[0], 5, 50));
  REQUIRE(rig.e.play());
  rig.run_samples(3.5 * kSixteenth);
  REQUIRE(set_chord(rig.bank.pattern[0], 5, 60, ChordKind::Power, Voicing::Close));  // one word: the old step or this one
  rig.run_samples(3.0 * kSixteenth);
  const std::vector<Seen> on = rig.of(dsp::EventKind::NoteOn);
  REQUIRE(on.size() >= 2);
  CHECK(on[0].note == 60);  // the chord's lowest note: step 5 had not been reached when the edit was made
  CHECK(near(on[0].at, 5 * kSixteenth));
}

TEST_CASE("step hook: no note is left hanging on Stop, mid-note, mid-chord or after the gate") {
  for (double stop_at : {0.2, 0.9, 1.5}) {  // before the gate, after the gate, on a rest
    Rig rig;
    REQUIRE(rig.ok);
    REQUIRE(set_chord(rig.bank.pattern[0], 0, 60, ChordKind::Seventh, Voicing::Close));
    REQUIRE(rig.e.play());
    rig.run_samples(stop_at * kSixteenth);
    REQUIRE(rig.e.stop());
    rig.run(4);
    INFO("stopped at " << stop_at << " of a step");
    CHECK(rig.of(dsp::EventKind::NoteOn).size() >= 4);
    CHECK(rig.hanging() == 0);
    // A restart plays from the top again and ends clean too.
    rig.spy.seen.clear();
    const std::uint64_t base = rig.spy.block_ * kBlock;
    REQUIRE(rig.e.play());
    rig.run_samples(0.4 * kSixteenth);
    CHECK(rig.of(dsp::EventKind::NoteOn).size() >= 4);
    CHECK(rig.of(dsp::EventKind::NoteOn)[0].at == base);  // from the top
    REQUIRE(rig.e.stop());
    rig.run(4);
    CHECK(rig.hanging() == 0);
  }
}

TEST_CASE("step hook: a big chord stays within the unit's event budget for a block") {
  Rig rig;
  REQUIRE(rig.ok);
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    REQUIRE(set_chord(rig.bank.pattern[0], i, 48u + i, ChordKind::Seventh, Voicing::Wide));
    REQUIRE(set_carrier(rig.bank.pattern[0], i, 1u + i % 6u));
  }
  REQUIRE(rig.e.play());
  rig.run_samples(32 * kSixteenth);
  std::uint64_t per_block[4096] = {};
  for (const Seen& s : rig.spy.seen) {
    ++per_block[s.at / kBlock];
  }
  std::uint64_t worst = 0;
  for (std::uint64_t n : per_block) {
    worst = n > worst ? n : worst;
  }
  CHECK(worst <= kChordMaxNotes * 2 + 1);  // the ons of a step, the offs before them and one CarSrc, at most
  CHECK(worst < dsp::kMaxEventsPerUnit - dsp::kMaxInjectPerUnit);
  REQUIRE(rig.e.stop());
  rig.run(4);
  CHECK(rig.hanging() == 0);
}

// ---- The SLICE lane (U6c) ------------------------------------------------------------------------------------------------------

TEST_CASE("slice: set_slice edits one field, 0..24, a bad value or step changes nothing") {
  PatternData p = {};
  REQUIRE(set_note(p, 2, 60));
  REQUIRE(set_slice(p, 2, 24));
  CHECK(get_step(p, 2).slice == 24u);
  CHECK(get_step(p, 2).note == 60u);  // the other lanes stay
  CHECK_FALSE(set_slice(p, 2, 25));
  CHECK_FALSE(set_slice(p, 16, 1));
  CHECK(get_step(p, 2).slice == 24u);
  REQUIRE(set_slice(p, 2, 0));
  CHECK(get_step(p, 2).slice == 0u);
  REQUIRE(set_slice(p, 5, 7));
  REQUIRE(set_level(p, 5, 90));  // LEVEL does not touch the slice
  CHECK(get_step(p, 5).slice == 7u);
}

TEST_CASE("slice hook: Start, Len, Trig 1 of the published reader, in that order, on the step's frame") {
  Rig rig;
  REQUIRE(rig.ok);
  rig.publish(2, 8);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_slice(a, 0, 3));
  REQUIRE(set_slice(a, 1, 0));   // none
  REQUIRE(set_slice(a, 2, 9));   // above the count of 8: silent
  REQUIRE(set_slice(a, 3, 5));   // a rest in NOTE still fires its slice
  REQUIRE(rig.e.play());
  rig.run_samples(4 * kSixteenth);
  const std::vector<Seen> r = rig.region(2);
  REQUIRE(r.size() == 6);
  using RF = Vocoder::RegionField;
  CHECK(r[0].param == Vocoder::region_param(2, RF::Start));
  CHECK(r[0].value == 1000.0f + 200.0f);
  CHECK(r[1].param == Vocoder::region_param(2, RF::Len));
  CHECK(r[1].value == 502.0f);
  CHECK(r[2].param == Vocoder::region_param(2, RF::Trig));
  CHECK(r[2].value == 1.0f);
  CHECK(r[0].at == kBlock);
  CHECK(r[2].at == kBlock);
  CHECK(r[3].value == 1000.0f + 400.0f);
  CHECK(r[4].value == 504.0f);
  CHECK(r[5].value == 1.0f);
  CHECK(near(r[3].at, 3 * kSixteenth));
  CHECK(near(r[5].at, 3 * kSixteenth));
  for (unsigned k = 0; k < 4; ++k) {
    if (k != 2) {
      CHECK(rig.region(k).empty());  // no other reader hears of it
    }
  }
  CHECK(rig.seq.playing_slice() == 5u);
}

TEST_CASE("slice hook: no source, a hidden map or a SPEAKS with no slices emits nothing, not even a Stop") {
  Rig rig;
  REQUIRE(rig.ok);
  REQUIRE(set_slice(rig.bank.pattern[0], 0, 1));
  REQUIRE(set_slice(rig.bank.pattern[0], 1, 2));
  REQUIRE(rig.e.play());  // nothing published
  rig.run_samples(2 * kSixteenth);
  CHECK(rig.region(0).empty());
  rig.publish(1, 4);
  (void)rig.seq.slices_open();  // a rewrite in progress: count 0 until slices_close
  CHECK(rig.seq.slice_count() == 0u);
  rig.run_samples(32 * kSixteenth);
  for (unsigned k = 0; k < 4; ++k) {
    CHECK(rig.region(k).empty());
  }
  CHECK(rig.seq.playing_slice() == 0u);
  REQUIRE(rig.e.stop());
  rig.run(4);
  for (unsigned k = 0; k < 4; ++k) {
    CHECK(rig.region(k).empty());  // nothing fired: no Trig 0
  }
  rig.seq.slices_close(1, 0);  // a count of 0 is none
  CHECK(rig.seq.slice_reader() == StepSeq::kNoReader);
}

TEST_CASE("slice hook: Stop releases the reader that fired, once, even after the source changed") {
  Rig rig;
  REQUIRE(rig.ok);
  rig.publish(2, 8);
  REQUIRE(set_slice(rig.bank.pattern[0], 0, 3));
  REQUIRE(rig.e.play());
  rig.run_samples(0.5 * kSixteenth);
  CHECK(rig.seq.playing_slice() == 3u);
  rig.publish(1, 6);  // SPEAKS moved to slot 2 while it played
  REQUIRE(rig.e.stop());
  rig.run(4);
  const std::vector<Seen> r = rig.region(2);
  REQUIRE(r.size() == 4);  // Start, Len, Trig 1, then the Trig 0
  CHECK(r[3].param == Vocoder::region_param(2, Vocoder::RegionField::Trig));
  CHECK(r[3].value == 0.0f);
  CHECK(rig.region(1).empty());
  CHECK(rig.seq.playing_slice() == 0u);
  rig.run(12);  // a longer stopped stretch: no second release
  CHECK(rig.region(2).size() == 4);
}

// ---- The MOUTH lane (V3) -------------------------------------------------------------------------------------------------------

namespace {

// A second instrument standing in for the tract unit: it writes down the Param events the hook sends it (VOWEL 0, CLOSE 4, SLIDE 10, ACCENT 11).
constexpr unsigned kTractRows = 12;
constexpr std::array<o3sdk::core::ParamDesc, kTractRows> make_tract_rows() {
  std::array<o3sdk::core::ParamDesc, kTractRows> rows{};
  for (auto& r : rows) {
    r = {"T", "", nullptr, nullptr, 0.0f, 2000.0f, 0.0f, 7, 1, 0, o3sdk::core::ParamUnit::Int, o3sdk::core::ParamCurve::Linear, 0, 0};
  }
  return rows;
}
constexpr auto kTractTable = make_tract_rows();

class TractSpy final : public dsp::Unit {
 public:
  TractSpy() : info_{"TSPY", dsp::UnitKind::Effect, kTractTable.data(), kTractRows, nullptr, 0} {}
  const dsp::UnitInfo& info() const noexcept override { return info_; }
  void prepare(float) noexcept override {}
  void process(const dsp::ProcessArgs& a) noexcept override {
    for (std::uint32_t i = 0; i < a.event_count; ++i) {
      const dsp::Event& e = a.events[i];
      if (e.kind == dsp::EventKind::Param && block_ != 0) {  // block 0 carries the engine's stored values, not the hook's events
        seen.push_back({pos_ + e.frame, e.kind, e.note, e.velocity, e.value, e.param, e.flags});  // pos_: the render's samples so far, whatever the block size
      }
    }
    ++block_;
    pos_ += a.frames;
    for (std::uint32_t i = 0; i < a.frames; ++i) {
      a.out_l[i] = 0.0f;
      a.out_r[i] = 0.0f;
    }
  }
  std::vector<Seen> seen;
  std::uint64_t block_ = 0;
  std::uint64_t pos_ = 0;

 private:
  dsp::UnitInfo info_;
};

// The ids of TractUnit's VOWEL, CLOSE, SLIDE and ACCENT as the hook writes them (tract_mode_test checks the unit's own enum against these).
constexpr std::uint16_t kVowelId = 0;
constexpr std::uint16_t kCloseId = 4;
constexpr std::uint16_t kSlideId = 10;
constexpr std::uint16_t kAccentId = 11;

struct MouthRig {
  explicit MouthRig(bool named = true) {
    id = e.attach(spy);
    tid = e.attach(tract);
    seq.init(id, &bank, &map);
    if (named) {
      seq.set_tract(tid);
    }
    ok = e.set_source(0, id) && e.set_insert(0, 0, tid) && e.add_hook(seq);  // the tract unit is an insert of the vocoder's slot
    e.start();
  }
  void run(unsigned blocks) {
    float l[kBlock], r[kBlock];
    for (unsigned i = 0; i < blocks; ++i) {
      e.render(nullptr, nullptr, l, r, kBlock);
    }
  }
  void run_samples(double n) { run(static_cast<unsigned>(n / kBlock) + 1u); }
  const std::vector<Seen>& ev() const { return tract.seen; }
  // CLOSE left on at the end of what the tract saw (the last CLOSE write is not 0).
  unsigned hanging_mouth() const {
    float last = 0.0f;
    for (const Seen& x : tract.seen) {
      if (x.param == kCloseId) {
        last = x.value;
      }
    }
    return last != 0.0f ? 1u : 0u;
  }
  std::vector<Seen> of(dsp::EventKind k) const {
    std::vector<Seen> v;
    for (const Seen& x : spy.seen) {
      if (x.kind == k) {
        v.push_back(x);
      }
    }
    return v;
  }
  dsp::Engine e;
  Spy spy;
  TractSpy tract;
  Bank bank = {};
  SliceMap map = {};
  StepSeq seq;
  dsp::UnitId id = dsp::kNoUnit;
  dsp::UnitId tid = dsp::kNoUnit;
  bool ok = false;
};

constexpr unsigned kGld = 128;  // the GLD bit on a vowel's value

}  // namespace

TEST_CASE("mouth hook: a vowel writes VOWEL on the step's frame, (value-1)/8, and nothing else") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_mouth(a, 0, 1));  // A
  REQUIRE(set_mouth(a, 2, 9));  // U
  REQUIRE(set_mouth(a, 4, 5));  // I
  REQUIRE(rig.e.play());
  rig.run_samples(6 * kSixteenth);
  const std::vector<Seen>& v = rig.ev();
  REQUIRE(v.size() == 3);
  const float want[3] = {0.0f, 1.0f, 0.5f};
  const double at[3] = {0, 2, 4};
  for (unsigned i = 0; i < 3; ++i) {
    CHECK(v[i].param == kVowelId);
    CHECK(v[i].value == want[i]);
    CHECK(at_near(v[i].at, at[i] * kSixteenth));
  }
  CHECK(rig.of(dsp::EventKind::Param).empty());  // the vocoder's own spy saw no mouth event
}

TEST_CASE("mouth hook: GLD writes SLIDE (a sixteenth in ms / 3) before VOWEL, the next plain vowel writes SLIDE 0 first") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_mouth(a, 0, kGld | 3));  // E, slid
  REQUIRE(set_mouth(a, 1, kGld | 7));  // O, slid: SLIDE again, the same value
  REQUIRE(set_mouth(a, 2, 1));         // A, plain: SLIDE 0 first
  REQUIRE(set_mouth(a, 3, 9));         // U, plain: no second SLIDE 0
  REQUIRE(rig.e.play());
  rig.run_samples(5 * kSixteenth);
  const std::vector<Seen>& v = rig.ev();
  REQUIRE(v.size() == 7);
  const std::uint16_t id[7] = {kSlideId, kVowelId, kSlideId, kVowelId, kSlideId, kVowelId, kVowelId};
  const float val[7] = {125.0f / 3.0f, 0.25f, 125.0f / 3.0f, 0.75f, 0.0f, 0.0f, 1.0f};  // 120 BPM: a sixteenth is 125 ms, the tube's one pole needs a third
  const double at[7] = {0, 0, 1, 1, 2, 2, 3};
  for (unsigned i = 0; i < 7; ++i) {
    INFO("event " << i);
    CHECK(v[i].param == id[i]);
    CHECK(v[i].value == doctest::Approx(val[i]).epsilon(0.001));  // the tempo reaches the hook as mbpm, a sixteenth is 125 ms to a rounding
    CHECK(at_near(v[i].at, at[i] * kSixteenth));
  }
  for (const Seen& s : v) {
    CHECK(s.param != 5);  // the hook never writes GLIDE
  }
}

TEST_CASE("mouth hook: a closure with a note writes CLOSE n on the step and CLOSE 0 at the gate's end") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_mouth(a, 0, 10));  // LIPS
  REQUIRE(set_note(a, 2, 62));
  REQUIRE(set_mouth(a, 2, 12));  // BACK
  REQUIRE(rig.e.play());
  rig.run_samples(4 * kSixteenth);
  const std::vector<Seen>& v = rig.ev();
  REQUIRE(v.size() == 4);
  const float val[4] = {1.0f, 0.0f, 3.0f, 0.0f};
  const double at[4] = {0, 0.75, 2, 2.75};
  for (unsigned i = 0; i < 4; ++i) {
    INFO("event " << i);
    CHECK(v[i].param == kCloseId);
    CHECK(v[i].value == val[i]);
    CHECK(at_near(v[i].at, at[i] * kSixteenth));
  }
}

TEST_CASE("mouth hook: a closure with no note ends at the step's end, the next edge, unless the next step is a closure too") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_mouth(a, 0, 11));  // TIP, a rest: ends at step 1's edge
  REQUIRE(set_mouth(a, 4, 10));  // LIPS, a rest, then
  REQUIRE(set_mouth(a, 5, 12));  // BACK straight after it, then a FOLLOW step
  REQUIRE(rig.e.play());
  rig.run_samples(8 * kSixteenth);
  const std::vector<Seen>& v = rig.ev();
  REQUIRE(v.size() == 5);
  const float val[5] = {2.0f, 0.0f, 1.0f, 3.0f, 0.0f};
  const double at[5] = {0, 1, 4, 5, 6};
  for (unsigned i = 0; i < 5; ++i) {
    INFO("event " << i);
    CHECK(v[i].param == kCloseId);
    CHECK(v[i].value == val[i]);
    CHECK(at_near(v[i].at, at[i] * kSixteenth));
  }
}

TEST_CASE("mouth hook: a step writes at most three events and a FOLLOW step none") {
  MouthRig rig;
  REQUIRE(rig.ok);
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    REQUIRE(set_note(rig.bank.pattern[0], i, 48u + i));
    const unsigned m[4] = {kGld | (1u + i % 9u), 1u + i % 9u, 10u + i % 3u, 0u};
    REQUIRE(set_mouth(rig.bank.pattern[0], i, m[i % 4]));
  }
  REQUIRE(rig.e.play());
  rig.run_samples(32 * kSixteenth);
  std::uint64_t per_block[2048] = {};
  for (const Seen& s : rig.ev()) {
    ++per_block[s.at / kBlock];
  }
  for (std::uint64_t n : per_block) {
    CHECK(n <= 3);
  }
  REQUIRE(rig.e.stop());
  rig.run(4);
  CHECK(rig.hanging_mouth() == 0);
}

TEST_CASE("mouth hook: Stop writes CLOSE 0 and SLIDE 0 once, and nothing when the line wrote nothing") {
  {
    MouthRig rig;
    REQUIRE(rig.ok);
    PatternData& a = rig.bank.pattern[0];
    REQUIRE(set_mouth(a, 0, kGld | 5));  // a slid vowel
    REQUIRE(set_mouth(a, 1, 11));        // a closure with no note, still open at the stop
    REQUIRE(rig.e.play());
    rig.run_samples(1.5 * kSixteenth);
    const std::size_t before = rig.ev().size();
    REQUIRE(before == 3);  // SLIDE, VOWEL, CLOSE 2 at step 1
    REQUIRE(rig.e.stop());
    rig.run(4);
    const std::vector<Seen>& v = rig.ev();
    REQUIRE(v.size() == before + 2);
    CHECK(v[before].param == kCloseId);
    CHECK(v[before].value == 0.0f);
    CHECK(v[before + 1].param == kSlideId);
    CHECK(v[before + 1].value == 0.0f);
    rig.run(12);  // a longer stopped stretch: no second pair
    CHECK(rig.ev().size() == before + 2);
    // A restart replays the line and the next Stop writes the pair again.
    REQUIRE(rig.e.play());
    rig.run_samples(0.5 * kSixteenth);
    REQUIRE(rig.e.stop());
    rig.run(4);
    CHECK(rig.ev().size() == before + 2 + 2 + 2);  // SLIDE, VOWEL of the new lap, then the pair
  }
  {
    MouthRig rig;  // a pattern with no mouth bytes: the Stop is silent
    REQUIRE(rig.ok);
    REQUIRE(set_note(rig.bank.pattern[0], 0, 60));
    REQUIRE(rig.e.play());
    rig.run_samples(2 * kSixteenth);
    REQUIRE(rig.e.stop());
    rig.run(8);
    CHECK(rig.ev().empty());
  }
}

TEST_CASE("mouth hook: a pattern change that lands on a Stop leaves the tract closed and straight") {
  MouthRig rig;
  REQUIRE(rig.ok);
  REQUIRE(set_length(rig.bank.pattern[0], 4));
  REQUIRE(set_length(rig.bank.pattern[1], 4));
  REQUIRE(set_mouth(rig.bank.pattern[0], 3, 10));         // A ends on a closure (a rest: it would end at B's first edge)
  REQUIRE(set_mouth(rig.bank.pattern[1], 0, kGld | 1));   // B starts on a slid vowel
  rig.seq.request_pattern(1);
  REQUIRE(rig.e.play());
  rig.run_samples(3.5 * kSixteenth);  // A's closure has begun
  REQUIRE(rig.e.stop());
  rig.run(4);
  const std::vector<Seen>& v = rig.ev();
  REQUIRE(v.size() >= 3);
  CHECK(v[v.size() - 2].param == kCloseId);
  CHECK(v[v.size() - 2].value == 0.0f);
  CHECK(v.back().param == kSlideId);
  CHECK(v.back().value == 0.0f);
}

TEST_CASE("mouth hook: while a strip pad is held the hook emits no mouth event, and speaks again on release") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 4));
  for (unsigned i = 0; i < 4; ++i) {
    REQUIRE(set_mouth(a, i, kGld | 1u));
  }
  rig.seq.set_mouth_hold(true);
  CHECK(rig.seq.mouth_hold());
  REQUIRE(rig.e.play());
  rig.run_samples(5 * kSixteenth);
  CHECK(rig.ev().empty());
  REQUIRE(rig.e.stop());
  rig.run(4);
  CHECK(rig.ev().empty());  // the line wrote nothing, so the Stop has nothing to clear
  rig.seq.set_mouth_hold(false);
  CHECK(!rig.seq.mouth_hold());
  REQUIRE(rig.e.play());
  rig.run_samples(1.5 * kSixteenth);
  CHECK(!rig.ev().empty());
}

TEST_CASE("mouth hook: a Stop under a held strip pad clears SLIDE (the GLIDE knob must not stay dead) but leaves CLOSE to the pad") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_mouth(a, 0, kGld | 5));  // a slid vowel: SLIDE is on
  REQUIRE(set_mouth(a, 1, 11));        // a closure still open
  REQUIRE(rig.e.play());
  rig.run_samples(1.5 * kSixteenth);
  const std::size_t before = rig.ev().size();
  REQUIRE(before == 3);
  rig.seq.set_mouth_hold(true);  // a pad goes down (a vowel pad holds as well as a closure pad)
  REQUIRE(rig.e.stop());
  rig.run(4);
  const std::vector<Seen>& v = rig.ev();
  REQUIRE(v.size() == before + 1);
  CHECK(v[before].param == kSlideId);
  CHECK(v[before].value == 0.0f);
  rig.run(12);
  CHECK(rig.ev().size() == before + 1);  // once
}

TEST_CASE("mouth hook: with no tract unit named the line writes nothing, and a missing mouth is FOLLOW") {
  MouthRig rig(false);
  REQUIRE(rig.ok);
  REQUIRE(set_mouth(rig.bank.pattern[0], 0, kGld | 3));
  REQUIRE(set_mouth(rig.bank.pattern[0], 1, 11));
  REQUIRE(rig.e.play());
  rig.run_samples(4 * kSixteenth);
  REQUIRE(rig.e.stop());
  rig.run(4);
  CHECK(rig.ev().empty());
  MouthRig plain;  // named, but every byte is FOLLOW
  REQUIRE(plain.ok);
  REQUIRE(plain.e.play());
  plain.run_samples(4 * kSixteenth);
  CHECK(plain.ev().empty());
}

TEST_CASE("mouth hook: the mouth line runs in the step's own pattern, each pattern its own line") {
  MouthRig rig;
  REQUIRE(rig.ok);
  REQUIRE(set_length(rig.bank.pattern[0], 2));
  REQUIRE(set_length(rig.bank.pattern[1], 2));
  REQUIRE(set_mouth(rig.bank.pattern[0], 0, 1));  // A
  REQUIRE(set_mouth(rig.bank.pattern[1], 0, 9));  // U
  rig.seq.request_pattern(1);
  REQUIRE(rig.e.play());  // a request made while stopped plays from the next Play
  rig.run_samples(2.5 * kSixteenth);
  REQUIRE(!rig.ev().empty());
  CHECK(rig.ev()[0].value == 1.0f);  // B's U
}

// ---- Words, WOB and ACCENT on the MOUTH lane (V3b) -----------------------------------------------------------------------------

namespace {

// One expected tract event: the parameter, the value, and where it lands in sixteenths after the grid's start (120 BPM).
struct Want {
  std::uint16_t id;
  float v;
  double at;
};

// The events a word writes in step `step`, from its table.
std::vector<Want> word_events(unsigned word, double step) {
  std::vector<Want> w;
  for (unsigned j = 0; j < kWords[word].count; ++j) {
    const WordPoint& p = kWords[word].pt[j];
    const double at = step + p.tick / 240.0;
    if (p.vowel != 0) {
      w.push_back({kSlideId, p.over == 0 ? kSlideMinMs : slide_for(125.0f * static_cast<float>(p.over) * 0.01f), at});
      w.push_back({kVowelId, static_cast<float>(p.vowel - 1) / 8.0f, at});
    }
    if (p.close >= 0) {
      w.push_back({kCloseId, static_cast<float>(p.close), at});
    }
  }
  return w;
}

// The events a WOB run over steps first..last (inclusive) of `rate` writes between the vowels with VOWEL values before and after, worked out from the
// ticks alone: an edge at every multiple of the half period, even halves going to `after`; a run that starts off an edge first writes the half it is in,
// arriving at the next edge.
std::vector<Want> wob_events(unsigned rate, unsigned first, unsigned last, float before, float after) {
  const unsigned half = kWobHalfTicks[rate];
  const float tick_ms = 125.0f / 240.0f;
  std::vector<Want> w;
  const unsigned t0 = first * 240u;
  const unsigned t1 = (last + 1u) * 240u;
  if (t0 % half != 0u) {
    w.push_back({kSlideId, slide_for(tick_ms * static_cast<float>(half - t0 % half)), t0 / 240.0});
    w.push_back({kVowelId, (t0 / half) % 2u != 0u ? before : after, t0 / 240.0});
  }
  for (unsigned t = (t0 + half - 1u) / half * half; t < t1; t += half) {
    w.push_back({kSlideId, slide_for(tick_ms * static_cast<float>(half)), t / 240.0});
    w.push_back({kVowelId, (t / half) % 2u != 0u ? before : after, t / 240.0});
  }
  return w;
}

// The tract's events for a line `setup` wrote into a rig, rendered in blocks of `frames`; `at` is counted from the grid's start.
template <typename F>
std::vector<Seen> run_line(F setup, double sixteenths, unsigned frames = kBlock, std::uint32_t mbpm = 0) {
  MouthRig rig;
  REQUIRE(rig.ok);
  if (mbpm != 0u) {
    REQUIRE(rig.e.set_tempo(mbpm));
  }
  setup(rig);
  REQUIRE(rig.e.play());
  float l[kBlock], r[kBlock];
  const unsigned total = static_cast<unsigned>(sixteenths * kSixteenth) + frames;
  for (unsigned done = 0; done < total; done += frames) {
    rig.e.render(nullptr, nullptr, l, r, frames);
  }
  std::vector<Seen> v = rig.ev();
  return v;
}

void check_line(const std::vector<Seen>& v, const std::vector<Want>& want, double tol = 1.0) {
  REQUIRE(v.size() == want.size());
  for (std::size_t i = 0; i < want.size(); ++i) {
    INFO("event " << i);
    CHECK(v[i].param == want[i].id);
    CHECK(v[i].value == doctest::Approx(want[i].v).epsilon(0.001));  // the tempo reaches the hook as mbpm, to a rounding
    CHECK(at_near(v[i].at, want[i].at * kSixteenth, tol));
  }
}

// I at step 0, steps first..last WOB of `rate`, O at step 8: the neighbours are I (VOWEL 0.5) and O (0.75).
void wob_line(MouthRig& r, unsigned rate, unsigned first = 4, unsigned last = 7) {
  PatternData& a = r.bank.pattern[0];
  REQUIRE(set_mouth(a, 0, 5));
  for (unsigned i = first; i <= last; ++i) {
    REQUIRE(set_mouth(a, i, mouth_wob_byte(rate)));
  }
  REQUIRE(set_mouth(a, 8, 7));
}

std::vector<Want> wob_line_events(unsigned rate, unsigned first = 4, unsigned last = 7) {
  std::vector<Want> w = {{kVowelId, 0.5f, 0.0}};
  const std::vector<Want> e = wob_events(rate, first, last, 0.5f, 0.75f);
  w.insert(w.end(), e.begin(), e.end());
  w.push_back({kSlideId, 0.0f, 8.0});  // the plain O ends the slide
  w.push_back({kVowelId, 0.75f, 8.0});
  return w;
}

}  // namespace

TEST_CASE("mouth hook: every word writes its table, point by point, on the frames of its ticks") {
  for (unsigned w = 0; w < kMouthWords; ++w) {
    INFO("word " << w);
    const std::vector<Seen> v = run_line([w](MouthRig& r) { REQUIRE(set_mouth(r.bank.pattern[0], 0, mouth_word_byte(w))); }, 2.0);
    check_line(v, word_events(w, 0.0));
  }
  // WOW by hand: U at the step, A at 24 ticks over 40 % of 125 ms, U at 144 ticks.
  const std::vector<Seen> v = run_line([](MouthRig& r) { REQUIRE(set_mouth(r.bank.pattern[0], 0, mouth_word_byte(0))); }, 2.0);
  REQUIRE(v.size() == 6);
  CHECK(v[0].param == kSlideId);
  CHECK(v[0].value == kSlideMinMs);
  CHECK(v[1].value == 1.0f);
  CHECK(v[2].value == doctest::Approx(50.0f / 3.0f).epsilon(0.001));
  CHECK(v[3].value == 0.0f);
  CHECK(at_near(v[3].at, 0.1 * kSixteenth));
  CHECK(v[5].value == 1.0f);
  CHECK(at_near(v[5].at, 0.6 * kSixteenth));
}

TEST_CASE("mouth hook: a word does not depend on the block size") {
  for (unsigned w : {0u, 4u, 6u}) {
    INFO("word " << w);
    for (unsigned frames : {64u, 48u, 33u, 17u, 7u}) {
      INFO("block " << frames);
      check_line(run_line([w](MouthRig& r) { REQUIRE(set_mouth(r.bank.pattern[0], 0, mouth_word_byte(w))); }, 2.0, frames), word_events(w, 0.0), 2.0);
    }
  }
}

TEST_CASE("mouth hook: a word after a closure keeps its own CLOSE and skips CLOSE 0") {
  const std::vector<Seen> v = run_line(
      [](MouthRig& r) {
        REQUIRE(set_mouth(r.bank.pattern[0], 0, 10));                  // LIPS, a rest: it would end at step 1's edge
        REQUIRE(set_mouth(r.bank.pattern[0], 1, mouth_word_byte(5)));  // BA
      },
      3.0);
  check_line(v, {{kCloseId, 1.0f, 0.0}, {kSlideId, kSlideMinMs, 1.0}, {kVowelId, 0.0f, 1.0}, {kCloseId, 1.0f, 1.0}, {kCloseId, 0.0f, 1.0 + 36.0 / 240.0}});
}

TEST_CASE("mouth hook: a WOB wobbles between its neighbours at every rate, one half period a move") {
  for (unsigned rate = 0; rate < kMouthWobRates; ++rate) {
    INFO("rate " << rate);
    check_line(run_line([rate](MouthRig& r) { wob_line(r, rate); }, 9.5), wob_line_events(rate));
  }
  // 1/16 by hand: the half period is a step, 125 ms, SLIDE 125/3, O on the even steps and I on the odd ones.
  const std::vector<Seen> v = run_line([](MouthRig& r) { wob_line(r, 2); }, 9.5);
  REQUIRE(v.size() == 11);
  CHECK(v[1].param == kSlideId);
  CHECK(v[1].value == doctest::Approx(125.0f / 3.0f).epsilon(0.001));
  CHECK(v[2].value == 0.75f);
  CHECK(v[4].value == 0.5f);
}

TEST_CASE("mouth hook: a WOB does not depend on the block size, and a run that starts off an edge arrives at the next one") {
  for (unsigned frames : {64u, 33u, 7u}) {
    INFO("block " << frames);
    check_line(run_line([](MouthRig& r) { wob_line(r, 4); }, 9.5, frames), wob_line_events(4), 2.0);
    check_line(run_line([](MouthRig& r) { wob_line(r, 4, 5, 7); }, 9.5, frames), wob_line_events(4, 5, 7), 2.0);  // step 5 is 240 ticks into a 320 half
  }
}

TEST_CASE("mouth hook: a WOB keeps its phase against the bar, lap after lap") {
  const std::vector<Seen> v = run_line([](MouthRig& r) { wob_line(r, 4); }, 16.0 + 9.5);
  const std::vector<Want> lap = wob_line_events(4);
  std::vector<Want> two = lap;
  for (Want w : lap) {
    w.at += 16.0;  // 3840 ticks: twelve halves, the same parity
    two.push_back(w);
  }
  check_line(v, two);
}

TEST_CASE("mouth hook: a WOB run takes two steps' rates one each, and a line with no vowel wobbles between A and U") {
  const std::vector<Seen> v = run_line(
      [](MouthRig& r) {
        PatternData& a = r.bank.pattern[0];
        REQUIRE(set_mouth(a, 2, mouth_wob_byte(2)));  // 1/16: one move
        REQUIRE(set_mouth(a, 3, mouth_wob_byte(0)));  // 1/4: tick 720 is 3/4 of the way into a half: no edge inside the step
      },
      4.5);
  REQUIRE(v.size() >= 2);
  CHECK(v[0].param == kSlideId);
  CHECK(v[1].param == kVowelId);
  CHECK(v[1].value == 1.0f);  // tick 480 / 240 = 2: even, the after place, U
  CHECK(at_near(v[1].at, 2.0 * kSixteenth));
}

TEST_CASE("wob_places: the vowels around a run, wrapping, one vowel, none, and only inside LENGTH") {
  std::uint8_t m[kPatternSteps] = {};
  const std::uint8_t wob = mouth_wob_byte(2);
  m[14] = 5;
  m[15] = wob;
  m[0] = wob;
  m[1] = 0x87;  // O, slid
  WobPlaces p = wob_places(m, 16, 15);
  CHECK(p.before == 5u);
  CHECK(p.after == 7u);
  p = wob_places(m, 16, 0);  // the same run from its other step
  CHECK(p.before == 5u);
  CHECK(p.after == 7u);
  m[12] = 10;  // a closure is not a vowel
  m[13] = mouth_word_byte(3);  // nor is a word
  p = wob_places(m, 16, 15);
  CHECK(p.before == 5u);
  // One vowel step: both places are it, the other one is U (A if it is U).
  std::uint8_t one[kPatternSteps] = {};
  one[3] = 3;
  one[9] = wob;
  p = wob_places(one, 16, 9);
  CHECK(p.before == 3u);
  CHECK(p.after == 9u);
  one[3] = 9;
  p = wob_places(one, 16, 9);
  CHECK(p.before == 9u);
  CHECK(p.after == 1u);
  // None: A and U.
  std::uint8_t none[kPatternSteps] = {};
  none[4] = wob;
  p = wob_places(none, 16, 4);
  CHECK(p.before == 1u);
  CHECK(p.after == 9u);
  // A line shorter than the pattern: steps beyond LENGTH are not neighbours.
  std::uint8_t cut[kPatternSteps] = {};
  cut[2] = 5;
  cut[5] = wob;
  cut[12] = 7;
  p = wob_places(cut, 16, 5);
  CHECK(p.before == 5u);
  CHECK(p.after == 7u);
  p = wob_places(cut, 8, 5);
  CHECK(p.before == 5u);
  CHECK(p.after == 9u);
  // A whole line of WOB has no vowel at all.
  std::uint8_t all[kPatternSteps];
  for (std::uint8_t& b : all) {
    b = wob;
  }
  p = wob_places(all, 16, 7);
  CHECK(p.before == 1u);
  CHECK(p.after == 9u);
}

TEST_CASE("mouth hook: words and a WOB are the strip's while a pad is held, and a hold that starts mid-word cuts it short") {
  {
    MouthRig rig;
    REQUIRE(rig.ok);
    PatternData& a = rig.bank.pattern[0];
    REQUIRE(set_mouth(a, 0, mouth_word_byte(1)));
    wob_line(rig, 3, 4, 7);
    REQUIRE(set_mouth(a, 0, mouth_word_byte(1)));
    rig.seq.set_mouth_hold(true);
    REQUIRE(rig.e.play());
    rig.run_samples(10 * kSixteenth);
    CHECK(rig.ev().empty());
    REQUIRE(rig.e.stop());
    rig.run(4);
    CHECK(rig.ev().empty());
  }
  {
    MouthRig rig;
    REQUIRE(rig.ok);
    REQUIRE(set_mouth(rig.bank.pattern[0], 0, mouth_word_byte(0)));
    REQUIRE(rig.e.play());
    rig.run_samples(0.05 * kSixteenth);  // the first point is out, the next is at tick 24
    REQUIRE(rig.ev().size() == 2);
    rig.seq.set_mouth_hold(true);
    rig.run_samples(2 * kSixteenth);
    CHECK(rig.ev().size() == 2);
  }
}

TEST_CASE("mouth hook: ACCENT follows a note's LEVEL above 100, only when it changes") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  const unsigned note[8] = {1, 1, 1, 1, 0, 1, 1, 1};
  const unsigned level[8] = {127, 127, 114, 100, 127, 0, 101, 100};
  for (unsigned i = 0; i < 8; ++i) {
    if (note[i] != 0u) {
      REQUIRE(set_note(a, i, 60));
    }
    REQUIRE(set_level(a, i, level[i]));
  }
  REQUIRE(rig.e.play());
  rig.run_samples(8.5 * kSixteenth);
  const std::vector<Seen>& v = rig.ev();
  const double at[5] = {0, 2, 3, 6, 7};  // 1 repeats 0; 4 is a rest; 5 is the default, already 0
  const float val[5] = {0.5f, 0.5f * 14.0f / 27.0f, 0.0f, 0.5f * 1.0f / 27.0f, 0.0f};
  REQUIRE(v.size() == 5);
  for (unsigned i = 0; i < 5; ++i) {
    INFO("event " << i);
    CHECK(v[i].param == kAccentId);
    CHECK(v[i].value == doctest::Approx(val[i]).epsilon(0.0001));
    CHECK(at_near(v[i].at, at[i] * kSixteenth));
  }
}

TEST_CASE("mouth hook: a Stop writes ACCENT 0 once, and only if an ACCENT is on") {
  MouthRig rig;
  REQUIRE(rig.ok);
  REQUIRE(set_note(rig.bank.pattern[0], 0, 60));
  REQUIRE(set_level(rig.bank.pattern[0], 0, 127));
  REQUIRE(set_note(rig.bank.pattern[0], 1, 60));  // the default level: 0
  REQUIRE(rig.e.play());
  rig.run_samples(1.5 * kSixteenth);
  REQUIRE(rig.ev().size() == 2);  // 0.5, then 0 at step 1
  REQUIRE(rig.e.stop());
  rig.run(4);
  CHECK(rig.ev().size() == 2);  // already 0: nothing at the Stop
  REQUIRE(rig.e.play());
  rig.run_samples(0.5 * kSixteenth);
  REQUIRE(rig.ev().size() == 3);
  REQUIRE(rig.e.stop());
  rig.run(4);
  REQUIRE(rig.ev().size() == 4);
  CHECK(rig.ev()[3].param == kAccentId);
  CHECK(rig.ev()[3].value == 0.0f);
  rig.run(12);
  CHECK(rig.ev().size() == 4);
}

TEST_CASE("mouth hook: the hold does not suppress ACCENT, and a Stop under a hold still writes ACCENT 0") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_level(a, 0, 127));
  REQUIRE(set_note(a, 1, 62));
  REQUIRE(set_level(a, 1, 114));
  REQUIRE(set_mouth(a, 0, 5));  // the lane itself is the pad's
  rig.seq.set_mouth_hold(true);
  REQUIRE(rig.e.play());
  rig.run_samples(2.5 * kSixteenth);
  REQUIRE(rig.ev().size() == 2);
  CHECK(rig.ev()[0].param == kAccentId);
  CHECK(rig.ev()[0].value == 0.5f);
  CHECK(rig.ev()[1].value == doctest::Approx(0.5f * 14.0f / 27.0f).epsilon(0.0001));
  REQUIRE(rig.e.stop());
  rig.run(4);
  REQUIRE(rig.ev().size() == 3);
  CHECK(rig.ev()[2].param == kAccentId);
  CHECK(rig.ev()[2].value == 0.0f);
}

TEST_CASE("mouth hook: ACCENT comes first on its frame, with no tract unit it writes nothing") {
  {
    MouthRig rig;
    REQUIRE(rig.ok);
    REQUIRE(set_note(rig.bank.pattern[0], 0, 60));
    REQUIRE(set_level(rig.bank.pattern[0], 0, 127));
    REQUIRE(set_mouth(rig.bank.pattern[0], 0, 5));
    REQUIRE(rig.e.play());
    rig.run_samples(1.5 * kSixteenth);
    REQUIRE(rig.ev().size() == 2);
    CHECK(rig.ev()[0].param == kAccentId);
    CHECK(rig.ev()[1].param == kVowelId);
    CHECK(rig.ev()[0].at == rig.ev()[1].at);
  }
  {
    MouthRig rig(false);
    REQUIRE(rig.ok);
    REQUIRE(set_note(rig.bank.pattern[0], 0, 60));
    REQUIRE(set_level(rig.bank.pattern[0], 0, 127));
    REQUIRE(rig.e.play());
    rig.run_samples(2 * kSixteenth);
    REQUIRE(rig.e.stop());
    rig.run(4);
    CHECK(rig.ev().empty());
  }
}

TEST_CASE("mouth hook: every SLIDE is the move's length / 3, at any tempo") {
  // GLD: a sixteenth / 3 (60 BPM: 250 ms, 240 BPM: 62.5 ms).
  for (std::uint32_t mbpm : {60000u, 240000u}) {
    INFO("mbpm " << mbpm);
    const float step = 15000.0f / (static_cast<float>(mbpm) * 0.001f);
    const std::vector<Seen> v = run_line([](MouthRig& r) { REQUIRE(set_mouth(r.bank.pattern[0], 0, kGld | 3)); }, 0.5, kBlock, mbpm);
    REQUIRE(v.size() >= 1);
    CHECK(v[0].param == kSlideId);
    CHECK(v[0].value == doctest::Approx(step / 3.0f).epsilon(0.001));
    // A word: its second point (WOW's A: 40 % of the step).
    const std::vector<Seen> w = run_line([](MouthRig& r) { REQUIRE(set_mouth(r.bank.pattern[0], 0, mouth_word_byte(0))); }, 1.0, kBlock, mbpm);
    REQUIRE(w.size() >= 4);
    CHECK(w[2].param == kSlideId);
    CHECK(w[2].value == doctest::Approx(step * 0.4f / 3.0f).epsilon(0.001));
  }
}

TEST_CASE("mouth hook: the event budget: 4 on a frame, 10 in a step, 8 in a block, and the tract ends clear") {
  const unsigned m[16] = {kGld | 3, mouth_word_byte(0), 10, mouth_word_byte(5), mouth_word_byte(1), mouth_wob_byte(5), mouth_wob_byte(5), 5,
                          mouth_wob_byte(3), 11, mouth_word_byte(6), mouth_wob_byte(4), mouth_wob_byte(4), 9, 12, mouth_word_byte(2)};
  for (std::uint32_t mbpm : {60000u, 120000u, 300000u}) {
    INFO("mbpm " << mbpm);
    MouthRig rig;
    REQUIRE(rig.ok);
    REQUIRE(rig.e.set_tempo(mbpm));
    PatternData& a = rig.bank.pattern[0];
    for (unsigned i = 0; i < kPatternSteps; ++i) {
      REQUIRE(set_mouth(a, i, m[i]));
      if (i != 9u) {
        REQUIRE(set_note(a, i, 48u + i));
      }
      REQUIRE(set_level(a, i, i % 2u == 0u ? 127u : 101u));  // ACCENT moves on every step with a note
    }
    REQUIRE(rig.e.play());
    const double spp = kSixteenth * 120000.0 / mbpm;  // samples a step
    rig.run_samples(40.0 * spp);
    const std::vector<Seen> v = rig.ev();
    REQUIRE(v.size() > 60);
    std::vector<unsigned> per_block(v.back().at / kBlock + 1u, 0u);
    std::vector<unsigned> per_step(static_cast<std::size_t>(static_cast<double>(v.back().at) / spp) + 2u, 0u);
    unsigned run = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
      run = i != 0u && v[i].at == v[i - 1].at ? run + 1u : 1u;
      CHECK(run <= 4u);
      ++per_block[v[i].at / kBlock];
      ++per_step[static_cast<std::size_t>(static_cast<double>(v[i].at) / spp + 0.003)];
    }
    for (unsigned n : per_block) {
      CHECK(n <= 8u);
    }
    for (unsigned n : per_step) {
      CHECK(n <= 10u);
    }
    REQUIRE(rig.e.stop());
    rig.run(4);
    float close = -1.0f, slide = -1.0f, accent = -1.0f;
    for (const Seen& s : rig.ev()) {
      if (s.param == kCloseId) {
        close = s.value;
      } else if (s.param == kSlideId) {
        slide = s.value;
      } else if (s.param == kAccentId) {
        accent = s.value;
      }
    }
    CHECK(close == 0.0f);
    CHECK(slide == 0.0f);
    CHECK(accent == 0.0f);
  }
}

// ---- The MOUT chunk of the bank file (V3) --------------------------------------------------------------------------------------

namespace {

// A bank file by hand: STEP, BANK (all lengths 16, empty steps) and then `mouts` MOUT chunks, each `count` patterns of `fill(p, i)` bytes.
template <typename F>
std::vector<std::uint8_t> with_mout(unsigned mouts, unsigned count, F fill, std::uint16_t version = 0x0100) {
  std::uint8_t out[kBankFileMax] = {};
  chunk::Writer w(out, sizeof out, "PATN");
  const std::uint8_t notes[kPatternSteps] = {};
  REQUIRE(w.begin("STEP"));
  REQUIRE(w.u16(0x0100));
  REQUIRE(w.u8(kPatternSteps));
  REQUIRE(w.bytes(notes, sizeof notes));
  REQUIRE(w.end());
  REQUIRE(w.begin("BANK"));
  REQUIRE(w.u16(0x0100));
  REQUIRE(w.u8(kBankPatterns));
  for (unsigned p = 0; p < kBankPatterns; ++p) {
    REQUIRE(w.u8(16));
    for (unsigned i = 0; i < kPatternSteps; ++i) {
      REQUIRE(w.u32(0u));
    }
  }
  REQUIRE(w.end());
  for (unsigned k = 0; k < mouts; ++k) {
    REQUIRE(w.begin("MOUT"));
    REQUIRE(w.u16(version));
    REQUIRE(w.u8(static_cast<std::uint8_t>(count)));
    for (unsigned p = 0; p < count; ++p) {
      for (unsigned i = 0; i < kPatternSteps; ++i) {
        REQUIRE(w.u8(static_cast<std::uint8_t>(fill(p, i))));
      }
    }
    REQUIRE(w.end());
  }
  const std::size_t n = w.finish();
  REQUIRE(n != 0);
  return std::vector<std::uint8_t>(out, out + n);
}

// Every valid mouth byte appears: 0, the nine vowels plain and slid, the three closures, the eight words, the six WOB rates (36 in all).
unsigned any_valid_mouth(unsigned p, unsigned i) {
  static const unsigned v[36] = {0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  129, 130, 131, 132, 133,
                                 134, 135, 136, 137, 0x0D, 0x1D, 0x2D, 0x3D, 0x4D, 0x5D, 0x6D, 0x7D, 0x0E, 0x1E, 0x2E, 0x3E, 0x4E, 0x5E};
  return v[(p * 5u + i) % 36u];
}

// The version of the MOUT chunk in a bank file (0 when there is none): the id, the u32 length, then the u16.
std::uint16_t mout_version(const std::vector<std::uint8_t>& f) {
  for (std::size_t i = 12; i + 10 <= f.size(); ++i) {
    if (f[i] == 'M' && f[i + 1] == 'O' && f[i + 2] == 'U' && f[i + 3] == 'T') {
      return static_cast<std::uint16_t>((f[i + 8] << 8) | f[i + 9]);
    }
  }
  return 0;
}

}  // namespace

TEST_CASE("mouth: the pattern carries its mouth bytes through copy, clear and compare") {
  PatternData a = {};
  REQUIRE(set_mouth(a, 3, 130));
  REQUIRE(set_mouth(a, 15, 12));
  CHECK(get_mouth(a, 3) == 130u);
  CHECK(mouths_used(a) == 2u);
  PatternData b = {};
  CHECK(!same_pattern(a, b));
  copy_pattern(b, a);
  CHECK(same_pattern(a, b));
  CHECK(get_mouth(b, 15) == 12u);
  clear_pattern(b);
  CHECK(mouths_used(b) == 0u);
  CHECK(!same_pattern(a, b));
  // The step word is untouched by the mouth: a note edit does not move it, and the reverse.
  REQUIRE(set_note(a, 3, 60));
  CHECK(get_mouth(a, 3) == 130u);
  CHECK(!set_mouth(a, 16, 1));  // a step out of range
  CHECK(!set_mouth(a, 0, 15));  // 15 is nothing
  CHECK(!set_mouth(a, 0, 0x6E));  // WOB rate 6
  CHECK(!set_mouth(a, 0, 0x7E));  // WOB rate 7
  CHECK(!set_mouth(a, 0, 0x8D));  // GLD on a word
  CHECK(!set_mouth(a, 0, 0x9E));  // GLD on a WOB
  CHECK(!set_mouth(a, 0, 0x80 | 0x0F));
  CHECK(set_mouth(a, 4, 0x7D));   // GA
  CHECK(set_mouth(a, 5, 0x5E));   // WOB 1/16T
  CHECK(get_mouth(a, 4) == 0x7Du);
  CHECK(mouths_used(a) == 4u);
  CHECK(set_mouth(a, 4, 0));
  CHECK(set_mouth(a, 5, 0));
  CHECK(!set_mouth(a, 0, 0x8A));  // GLD on a closure
  CHECK(!set_mouth(a, 0, 0x80));  // GLD on FOLLOW
  CHECK(!set_mouth(a, 0, 0x11));  // bits 4..6 are zero
  CHECK(get_mouth(a, 0) == 0u);
}

TEST_CASE("bank file: the MOUT chunk round trips every valid byte, and the file fits its buffer") {
  Bank b = full_bank();
  for (unsigned p = 0; p < kBankPatterns; ++p) {
    for (unsigned i = 0; i < kPatternSteps; ++i) {
      REQUIRE(set_mouth(b.pattern[p], i, any_valid_mouth(p, i)));
    }
  }
  const std::vector<std::uint8_t> f = bytes_of(b);
  CHECK(f.size() <= kBankFileMax);
  CHECK(mout_version(f) == 0x0101u);  // V3b writes the minor that says words and WOB may be in it
  Bank got = {};
  REQUIRE(read_bank(f.data(), f.size(), got));
  CHECK(same_bank(b, got));
  for (unsigned p = 0; p < kBankPatterns; ++p) {
    for (unsigned i = 0; i < kPatternSteps; ++i) {
      CHECK(get_mouth(got.pattern[p], i) == any_valid_mouth(p, i));
    }
  }
  // The same file again through the writer: byte for byte (a deterministic file).
  CHECK(bytes_of(got) == f);
}

TEST_CASE("bank file: no mouth byte means no MOUT chunk, and a file without one loads FOLLOW") {
  const Bank b = full_bank();
  const std::vector<std::uint8_t> plain = bytes_of(b);
  Bank with = b;
  REQUIRE(set_mouth(with.pattern[7], 15, 1));
  const std::vector<std::uint8_t> bigger = bytes_of(with);
  CHECK(bigger.size() >= plain.size() + 3u + kBankPatterns * kPatternSteps);  // the version, the count and the bytes, plus the chunk's header
  CHECK(bigger.size() <= plain.size() + 16u + kBankPatterns * kPatternSteps);
  Bank got = with;  // a bank that had mouths loads a file without any: they are FOLLOW again
  REQUIRE(read_bank(plain.data(), plain.size(), got));
  for (const PatternData& p : got.pattern) {
    CHECK(mouths_used(p) == 0u);
  }
  CHECK(same_bank(b, got));
}

TEST_CASE("bank file: a MOUT chunk that is wrong refuses the file whole") {
  Bank t = {};
  const auto ok = with_mout(1, kBankPatterns, [](unsigned p, unsigned i) { return any_valid_mouth(p, i); });
  CHECK(read_bank(ok.data(), ok.size(), t));  // the builder itself is right
  CHECK(get_mouth(t.pattern[1], 3) == any_valid_mouth(1, 3));
  auto refused = [&](const std::vector<std::uint8_t>& v) {
    Bank keep = full_bank();
    const Bank before = keep;
    const bool r = read_bank(v.data(), v.size(), keep);
    return !r && same_bank(keep, before);  // and nothing of it was loaded
  };
  CHECK(refused(with_mout(1, 7, [](unsigned, unsigned) { return 0u; })));            // a wrong pattern count
  CHECK(refused(with_mout(2, kBankPatterns, [](unsigned, unsigned) { return 0u; })));  // two MOUT chunks
  CHECK(refused(with_mout(1, kBankPatterns, [](unsigned, unsigned) { return 0u; }, 0x0200)));  // a major version this reader does not know
  CHECK(refused(with_mout(1, kBankPatterns, [](unsigned p, unsigned i) { return p == 2 && i == 0 ? 15u : 0u; }, 0x0101)));
  CHECK(refused(with_mout(1, kBankPatterns, [](unsigned p, unsigned i) { return p == 6 && i == 9 ? 0x6Eu : 0u; }, 0x0101)));  // WOB rate 6
  CHECK(refused(with_mout(1, kBankPatterns, [](unsigned p, unsigned i) { return p == 6 && i == 9 ? 0x7Eu : 0u; }, 0x0101)));  // WOB rate 7
  CHECK(refused(with_mout(1, kBankPatterns, [](unsigned p, unsigned i) { return p == 1 && i == 2 ? 0x8Du : 0u; }, 0x0101)));  // GLD on a word
  CHECK(refused(with_mout(1, kBankPatterns, [](unsigned p, unsigned i) { return p == 1 && i == 2 ? 0x9Eu : 0u; }, 0x0101)));  // GLD on a WOB
  CHECK(refused(with_mout(1, kBankPatterns, [](unsigned p, unsigned i) { return p == 0 && i == 15 ? 0x8Au : 0u; })));  // GLD on a closure
  CHECK(refused(with_mout(1, kBankPatterns, [](unsigned p, unsigned i) { return p == 7 && i == 7 ? 0x80u : 0u; })));   // GLD on FOLLOW
  CHECK(refused(with_mout(1, kBankPatterns, [](unsigned p, unsigned i) { return p == 3 && i == 1 ? 0x21u : 0u; })));   // a bit in 4..6
  // A 0x0100 MOUT loads (V3's own files); a word in it is as good as in a 0x0101 one: the reader takes any minor of its major.
  const auto v0 = with_mout(1, kBankPatterns, [](unsigned p, unsigned i) { return p == 4 && i == 4 ? 0x0Du : 0u; }, 0x0100);
  Bank t0 = {};
  CHECK(read_bank(v0.data(), v0.size(), t0));
  CHECK(get_mouth(t0.pattern[4], 4) == 0x0Du);
  const auto cut = [&] {
    std::vector<std::uint8_t> v = ok;
    v.resize(v.size() - 9);
    return v;
  }();
  CHECK(refused(cut));
}

// ---- TIE: a step that holds the notes before it ---------------------------------------------------------------------------------

TEST_CASE("tie: note 0 with kind 7 is the only valid kind 7, kinds 5 and 6 never are, and the word round trips") {
  Step t;
  t.kind = kTieKind;
  CHECK(is_tie(t));
  CHECK(step_valid(t));
  CHECK_FALSE(is_tie(Step{}));
  Step n = t;
  n.note = 60;  // kind 7 with a note is not a TIE and not valid
  CHECK_FALSE(is_tie(n));
  CHECK_FALSE(step_valid(n));
  Step v = t;
  v.voicing = 1;  // a TIE has no voicing
  CHECK_FALSE(step_valid(v));
  for (unsigned kind : {5u, 6u}) {
    Step k;
    k.kind = static_cast<std::uint8_t>(kind);
    CHECK_FALSE(step_valid(k));
    k.note = 60;
    CHECK_FALSE(step_valid(k));
  }
  Step full = t;
  full.carrier = 5;
  full.level = 90;
  full.slice = 12;
  REQUIRE(step_valid(full));
  const Step back = unpack_step(pack_step(full));
  CHECK(is_tie(back));
  CHECK(back.kind == kTieKind);
  CHECK(back.carrier == 5);
  CHECK(back.level == 90);
  CHECK(back.slice == 12);
  CHECK(back.voicing == 0);
}

TEST_CASE("tie: set_tie keeps carrier, level and slice; set_note, set_rest and set_chord clear it") {
  Bank b = {};
  PatternData& p = b.pattern[0];
  REQUIRE(set_chord(p, 3, 60, ChordKind::Seventh, Voicing::Wide));
  REQUIRE(set_carrier(p, 3, 5));
  REQUIRE(set_level(p, 3, 64));
  REQUIRE(set_slice(p, 3, 9));
  REQUIRE(set_tie(p, 3));
  Step s = get_step(p, 3);
  CHECK(is_tie(s));
  CHECK(s.note == 0);
  CHECK(s.kind == kTieKind);
  CHECK(s.voicing == 0);
  CHECK(s.carrier == 5);
  CHECK(s.level == 64);
  CHECK(s.slice == 9);
  CHECK(step_valid(s));
  const std::uint32_t before = load_word(p.step[3]);
  CHECK_FALSE(set_tie(p, 16));
  CHECK(load_word(p.step[3]) == before);
  REQUIRE(set_note(p, 3, 40));
  s = get_step(p, 3);
  CHECK_FALSE(is_tie(s));
  CHECK(s.kind == 0);
  CHECK(s.note == 40);
  CHECK(s.carrier == 5);
  REQUIRE(set_tie(p, 3));
  REQUIRE(set_rest(p, 3));
  CHECK_FALSE(is_tie(get_step(p, 3)));
  CHECK(get_step(p, 3).kind == 0);
  REQUIRE(set_tie(p, 3));
  REQUIRE(set_chord(p, 3, 50, ChordKind::Power, Voicing::Close));
  CHECK_FALSE(is_tie(get_step(p, 3)));
  CHECK(get_step(p, 3).kind == 4);
  // an empty step becomes a TIE and a TIE has no note for the legacy reader
  REQUIRE(set_tie(p, 0));
  CHECK(to_legacy(p).note[0] == 0);
  std::uint8_t none[kChordMaxNotes] = {};
  CHECK(step_notes(get_step(p, 0), 0, 0, none, nullptr) == 0u);
}

TEST_CASE("tie: pattern, bank file and clipboard carry a TIE as it is") {
  Bank b = full_bank();
  for (unsigned p = 0; p < kBankPatterns; ++p) {
    REQUIRE(set_tie(b.pattern[p], (p * 3u + 1u) % kPatternSteps));
    REQUIRE(set_tie(b.pattern[p], 15));
  }
  const std::vector<std::uint8_t> f = bytes_of(b);
  Bank back = {};
  REQUIRE(read_bank(f.data(), f.size(), back));  // a file with ties loads
  CHECK(same_bank(b, back));
  for (unsigned p = 0; p < kBankPatterns; ++p) {
    CHECK(is_tie(get_step(back.pattern[p], 15)));
    CHECK(is_tie(get_step(back.pattern[p], (p * 3u + 1u) % kPatternSteps)));
  }
  PatternData c = {};
  copy_pattern(c, b.pattern[2]);
  CHECK(same_pattern(c, b.pattern[2]));
  CHECK(is_tie(get_step(c, 15)));
  // the clipboard: a copied TIE pastes as a TIE, a pasted note over a TIE is not one, DELETE leaves a rest
  PatternData q = {};
  REQUIRE(set_length(q, 4));
  REQUIRE(set_note(q, 0, 60));
  REQUIRE(set_tie(q, 1));
  REQUIRE(set_level(q, 1, 77));
  pt::Clip clip;
  pt::copy_step(clip, q, 1);
  REQUIRE(pt::insert_step(q, 0, clip));
  CHECK(is_tie(get_step(q, 0)));
  CHECK(get_step(q, 0).level == 77);
  CHECK(get_step(q, 1).note == 60);  // the note moved one step right
  CHECK(is_tie(get_step(q, 2)));
  pt::Clip whole;
  pt::copy_whole(whole, q);
  PatternData r = {};
  REQUIRE(pt::paste_whole(r, whole));
  CHECK(same_pattern(r, q));
  REQUIRE(pt::clear_step(r, 0));
  CHECK_FALSE(is_tie(get_step(r, 0)));
  CHECK(get_step(r, 0).kind == 0);
  // the VALUE key on a TIE: the NOTE lane starts a note (48) and the tie is gone; the other lanes keep it
  REQUIRE(pt::step_value(q, 0, pt::Lane::Level, -1));
  CHECK(is_tie(get_step(q, 0)));
  REQUIRE(pt::step_value(q, 0, pt::Lane::Carrier, 1));
  CHECK(is_tie(get_step(q, 0)));
  REQUIRE(pt::step_value(q, 0, pt::Lane::Note, 1));
  CHECK_FALSE(is_tie(get_step(q, 0)));
  CHECK(get_step(q, 0).note == 48);
}

TEST_CASE("tie hook: [C, TIE, TIE, rest] is one note-on and one note-off, at the gate of the last tie") {
  Rig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 4));
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_tie(a, 1));
  REQUIRE(set_tie(a, 2));
  REQUIRE(rig.e.play());
  rig.run_samples(3.5 * kSixteenth);
  const std::vector<Seen> on = rig.of(dsp::EventKind::NoteOn);
  const std::vector<Seen> off = rig.of(dsp::EventKind::NoteOff);
  REQUIRE(on.size() == 1);
  CHECK(on[0].note == 60);
  CHECK(near(on[0].at, 0));
  REQUIRE(off.size() == 1);
  CHECK(off[0].note == 60);
  CHECK(near(off[0].at, 2.75 * kSixteenth));  // 75 % of step 2, where the next step is no TIE
  CHECK(rig.hanging() == 0);
}

TEST_CASE("tie hook: a tied chord holds every note, each goes off once, together") {
  Rig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 4));
  REQUIRE(set_chord(a, 0, 60, ChordKind::Triad, Voicing::Close));
  REQUIRE(set_tie(a, 1));
  rig.seq.set_key_scale(0, 0);
  REQUIRE(rig.e.play());
  rig.run_samples(3.5 * kSixteenth);
  const std::vector<Seen> on = rig.of(dsp::EventKind::NoteOn);
  const std::vector<Seen> off = rig.of(dsp::EventKind::NoteOff);
  REQUIRE(on.size() == 3);
  REQUIRE(off.size() == 3);
  for (unsigned i = 0; i < 3; ++i) {
    CHECK(on[i].note == off[i].note);
    CHECK(near(on[i].at, 0));
    CHECK(near(off[i].at, 1.75 * kSixteenth));
    CHECK(off[i].flags == Vocoder::owner_flags(Vocoder::Owner::Seq));
  }
  CHECK(rig.hanging() == 0);
}

TEST_CASE("tie hook: with nothing sounding a TIE is a rest") {
  Rig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 2));
  REQUIRE(set_tie(a, 1));  // [rest, TIE], looping
  REQUIRE(rig.e.play());
  rig.run_samples(6.5 * kSixteenth);
  CHECK(rig.of(dsp::EventKind::NoteOn).empty());
  CHECK(rig.of(dsp::EventKind::NoteOff).empty());
}

TEST_CASE("tie hook: [C, TIE, D] sends C off at the gate of the TIE, then D on at its edge") {
  Rig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 4));
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_tie(a, 1));
  REQUIRE(set_note(a, 2, 62));
  REQUIRE(rig.e.play());
  rig.run_samples(3.5 * kSixteenth);
  const std::vector<Seen> on = rig.of(dsp::EventKind::NoteOn);
  const std::vector<Seen> off = rig.of(dsp::EventKind::NoteOff);
  REQUIRE(on.size() == 2);
  REQUIRE(off.size() == 2);
  CHECK(on[0].note == 60);
  CHECK(near(on[0].at, 0));
  CHECK(on[1].note == 62);
  CHECK(near(on[1].at, 2 * kSixteenth));
  CHECK(off[0].note == 60);
  CHECK(near(off[0].at, 1.75 * kSixteenth));  // the TIE's own gate: the step after it is no TIE
  CHECK(off[1].note == 62);
  CHECK(near(off[1].at, 2.75 * kSixteenth));
  CHECK(rig.hanging() == 0);
}

TEST_CASE("tie hook: an edit after a skipped gate: the next edge releases, and a new note follows it") {
  for (int replace : {0, 1}) {  // the TIE becomes a rest, or a note
    Rig rig;
    REQUIRE(rig.ok);
    PatternData& a = rig.bank.pattern[0];
    REQUIRE(set_length(a, 4));
    REQUIRE(set_note(a, 0, 60));
    REQUIRE(set_tie(a, 1));
    REQUIRE(rig.e.play());
    rig.run_samples(0.9 * kSixteenth);  // past step 0's gate, which saw a TIE next and did not release
    REQUIRE(rig.of(dsp::EventKind::NoteOff).empty());
    REQUIRE((replace == 0 ? set_rest(a, 1) : set_note(a, 1, 64)));
    rig.run_samples(1.5 * kSixteenth);
    const std::vector<Seen> off = rig.of(dsp::EventKind::NoteOff);
    REQUIRE(off.size() >= 1);
    CHECK(off[0].note == 60);
    CHECK(near(off[0].at, 1.0 * kSixteenth));  // the edge of the step that stopped being a TIE
    CHECK(rig.of(dsp::EventKind::NoteOn).size() == (replace == 0 ? 1u : 2u));
  }
}

TEST_CASE("tie hook: a TIE at step 0 holds the last step's notes across the loop, and on the first pass it is a rest") {
  Rig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 2));
  REQUIRE(set_tie(a, 0));
  REQUIRE(set_note(a, 1, 60));  // [TIE, C], looping
  REQUIRE(rig.e.play());
  rig.run_samples(5.5 * kSixteenth);
  const std::vector<Seen> on = rig.of(dsp::EventKind::NoteOn);
  const std::vector<Seen> off = rig.of(dsp::EventKind::NoteOff);
  REQUIRE(on.size() == 3);  // nothing on the first pass of step 0; C on steps 1, 3 and 5
  CHECK(near(on[0].at, 1 * kSixteenth));
  CHECK(near(on[1].at, 3 * kSixteenth));
  CHECK(near(on[2].at, 5 * kSixteenth));
  REQUIRE(off.size() == 2);  // the gate of step 1 sees the TIE of the loop and holds; the TIE's own gate releases
  CHECK(near(off[0].at, 2.75 * kSixteenth));
  CHECK(near(off[1].at, 4.75 * kSixteenth));
  CHECK(rig.hanging() == 1);  // step 5's note is still sounding at 5.5
}

TEST_CASE("tie hook: a Stop during a tie sends the offs, once, and the next Play starts clean") {
  for (double stop_at : {0.5, 1.5, 2.9}) {
    Rig rig;
    REQUIRE(rig.ok);
    PatternData& a = rig.bank.pattern[0];
    REQUIRE(set_length(a, 4));
    REQUIRE(set_chord(a, 0, 60, ChordKind::Seventh, Voicing::Close));
    REQUIRE(set_tie(a, 1));
    REQUIRE(set_tie(a, 2));
    REQUIRE(set_tie(a, 3));
    REQUIRE(rig.e.play());
    rig.run_samples(stop_at * kSixteenth);
    REQUIRE(rig.e.stop());
    rig.run(4);
    INFO("stopped at " << stop_at << " of a step");
    CHECK(rig.hanging() == 0);
    CHECK(rig.of(dsp::EventKind::NoteOn).size() == rig.of(dsp::EventKind::NoteOff).size());
    rig.spy.seen.clear();
    REQUIRE(rig.e.play());  // the tie of step 3 holds nothing at the top: step 0 plays its chord again
    rig.run_samples(0.4 * kSixteenth);
    CHECK(rig.of(dsp::EventKind::NoteOn).size() >= 4);
    REQUIRE(rig.e.stop());
    rig.run(4);
    CHECK(rig.hanging() == 0);
  }
}

TEST_CASE("tie hook: a TIE holds across a pattern change when the next pattern starts with one, and is released at the gate when it does not") {
  for (int next_tie : {0, 1}) {
    Rig rig;
    REQUIRE(rig.ok);
    PatternData& a = rig.bank.pattern[0];
    PatternData& b = rig.bank.pattern[1];
    REQUIRE(set_length(a, 2));
    REQUIRE(set_length(b, 2));
    REQUIRE(set_note(a, 0, 60));
    REQUIRE(set_tie(a, 1));
    if (next_tie != 0) {
      REQUIRE(set_tie(b, 0));  // [TIE, rest]
    } else {
      REQUIRE(set_note(b, 0, 72));
    }
    REQUIRE(rig.e.play());
    rig.run_samples(0.5 * kSixteenth);
    rig.seq.request_pattern(1);
    rig.run_samples(3.0 * kSixteenth);  // up to 3.5 steps: B's step 0 (time 2) and step 1, not its second loop
    const std::vector<Seen> off = rig.of(dsp::EventKind::NoteOff);
    REQUIRE(off.size() >= 1);
    CHECK(off[0].note == 60);
    // A: C, TIE; the pattern ends at step 1's edge (time 1) and B follows. B starts with a TIE: C goes on to B's step 0 (time 2) and off at its gate.
    CHECK(near(off[0].at, (next_tie != 0 ? 2.75 : 1.75) * kSixteenth));
    CHECK(rig.seq.playing_pattern() == 1);
    CHECK(rig.of(dsp::EventKind::NoteOn).size() == (next_tie != 0 ? 1u : 2u));
  }
}

TEST_CASE("tie hook: every note-on has its note-off over many loops, with ties, chords and a Stop") {
  Rig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 8));
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_tie(a, 1));
  REQUIRE(set_chord(a, 2, 55, ChordKind::Triad, Voicing::Close));
  REQUIRE(set_tie(a, 3));
  REQUIRE(set_tie(a, 4));
  REQUIRE(set_note(a, 6, 65));
  REQUIRE(set_tie(a, 7));  // the last step ties into the first: C comes in again at the loop
  rig.seq.set_key_scale(0, 0);
  REQUIRE(rig.e.play());
  rig.run_samples(8 * 4 * kSixteenth - 0.5 * kSixteenth);  // four loops, ending inside the last TIE with a note sounding
  REQUIRE(rig.e.stop());
  rig.run(4);
  CHECK(rig.of(dsp::EventKind::NoteOn).size() == rig.of(dsp::EventKind::NoteOff).size());
  CHECK(rig.of(dsp::EventKind::NoteOn).size() == 4u * (1u + 3u + 1u));
  CHECK(rig.hanging() == 0);
}

TEST_CASE("tie hook: CARRIER and SLICE act on a TIE, LEVEL does nothing, no new note is sent") {
  Rig rig;
  REQUIRE(rig.ok);
  rig.publish(2, 8);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 4));
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_level(a, 0, 80));
  REQUIRE(set_tie(a, 1));
  REQUIRE(set_carrier(a, 1, 3));
  REQUIRE(set_level(a, 1, 127));
  REQUIRE(set_slice(a, 1, 5));
  REQUIRE(rig.e.play());
  rig.run_samples(2.5 * kSixteenth);
  std::vector<Seen> car;
  for (const Seen& x : rig.of(dsp::EventKind::Param)) {
    if (x.param == Vocoder::CarSrc) {
      car.push_back(x);
    }
  }
  REQUIRE(car.size() == 1);
  CHECK(car[0].value == 3.0f);
  CHECK(near(car[0].at, 1 * kSixteenth));
  const std::vector<Seen> r = rig.region(2);
  REQUIRE(r.size() == 3);
  CHECK(near(r[2].at, 1 * kSixteenth));
  const std::vector<Seen> on = rig.of(dsp::EventKind::NoteOn);
  REQUIRE(on.size() == 1);  // the TIE's LEVEL 127 plays nothing
  CHECK(on[0].vel == 80);
}

TEST_CASE("tie mouth: a closure with a held note ends at the TIE's edge, not at the gate before it") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 4));
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_mouth(a, 0, 10));  // LIPS
  REQUIRE(set_tie(a, 1));
  REQUIRE(rig.e.play());
  rig.run_samples(3.5 * kSixteenth);
  const std::vector<Seen>& v = rig.ev();
  REQUIRE(v.size() == 2);  // no CLOSE 0 at 0.75: the note goes on
  CHECK(v[0].param == kCloseId);
  CHECK(v[0].value == 1.0f);
  CHECK(at_near(v[0].at, 0));
  CHECK(v[1].param == kCloseId);
  CHECK(v[1].value == 0.0f);
  CHECK(at_near(v[1].at, 1 * kSixteenth));
  const std::vector<Seen> off = rig.of(dsp::EventKind::NoteOff);
  REQUIRE(off.size() == 1);
  CHECK(near(off[0].at, 1.75 * kSixteenth));  // the note is released at the TIE's gate
}

TEST_CASE("tie mouth: a TIE with its own closure moves the mouth and ends it at the gate that releases the note") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 4));
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_tie(a, 1));
  REQUIRE(set_mouth(a, 1, 12));  // BACK
  REQUIRE(rig.e.play());
  rig.run_samples(3.5 * kSixteenth);
  const std::vector<Seen>& v = rig.ev();
  REQUIRE(v.size() == 2);
  CHECK(v[0].value == 3.0f);
  CHECK(at_near(v[0].at, 1 * kSixteenth));
  CHECK(v[1].param == kCloseId);
  CHECK(v[1].value == 0.0f);
  CHECK(at_near(v[1].at, 1.75 * kSixteenth));
}

TEST_CASE("tie mouth: a TIE over silence is a rest for the mouth too, a closure ends at the next edge") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 4));
  REQUIRE(set_tie(a, 1));
  REQUIRE(set_mouth(a, 1, 11));  // TIP
  REQUIRE(rig.e.play());
  rig.run_samples(3.5 * kSixteenth);
  const std::vector<Seen>& v = rig.ev();
  REQUIRE(v.size() == 2);
  CHECK(v[0].value == 2.0f);
  CHECK(at_near(v[0].at, 1 * kSixteenth));
  CHECK(v[1].value == 0.0f);
  CHECK(at_near(v[1].at, 2 * kSixteenth));
}

TEST_CASE("tie mouth: a closure runs on through a chain of ties, one that closes again replaces it, and ACCENT is not touched by a TIE") {
  MouthRig rig;
  REQUIRE(rig.ok);
  PatternData& a = rig.bank.pattern[0];
  REQUIRE(set_length(a, 4));
  REQUIRE(set_note(a, 0, 60));
  REQUIRE(set_level(a, 0, 127));
  REQUIRE(set_mouth(a, 0, 10));  // LIPS
  REQUIRE(set_tie(a, 1));
  REQUIRE(set_mouth(a, 1, 12));  // BACK again
  REQUIRE(set_tie(a, 2));
  REQUIRE(set_level(a, 2, 0));
  REQUIRE(rig.e.play());
  rig.run_samples(3.5 * kSixteenth);
  const std::vector<Seen>& v = rig.ev();
  REQUIRE(v.size() == 4);
  CHECK(v[0].param == kAccentId);  // the note's ACCENT, once, on step 0
  CHECK(at_near(v[0].at, 0));
  CHECK(v[1].param == kCloseId);
  CHECK(v[1].value == 1.0f);
  CHECK(v[2].param == kCloseId);
  CHECK(v[2].value == 3.0f);
  CHECK(at_near(v[2].at, 1 * kSixteenth));
  CHECK(v[3].param == kCloseId);
  CHECK(v[3].value == 0.0f);
  CHECK(at_near(v[3].at, 2 * kSixteenth));  // step 2 is a TIE with FOLLOW: the closure ends with its edge
  CHECK(rig.of(dsp::EventKind::NoteOn).size() == 1);
  CHECK(rig.hanging_mouth() == 0);
}
