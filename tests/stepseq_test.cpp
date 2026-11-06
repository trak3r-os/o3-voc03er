// SPDX-License-Identifier: GPL-3.0-or-later
// U6a, the pattern bank and the step sequencer (app/include/o3_voc03er/stepseq.h): the packed step, the editing functions, the file (v2 with
// all eight patterns, the old file into pattern A, a damaged one refused whole), and the clocked hook driven through the engine (lanes,
// LENGTH, wrap, a pattern change at the pattern's end, no note left hanging on Stop). Synthetic signals only; no card.
#include <doctest/doctest.h>

#include <o3_voc03er/stepseq.h>

#include <o3sdk/dsp/engine.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;
namespace chunk = o3sdk::formats::chunk;

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
      seen.push_back({block_ * kBlock + e.frame, e.kind, e.note, e.velocity, e.value, e.param});
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
  CHECK_FALSE(step_valid(Step{1, 0, 0, 7, 0, 0}));  // LIVE (7) is never a lane value
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
  CHECK_FALSE(set_carrier(p, 3, 7));
  CHECK_FALSE(set_level(p, 3, 128));
  CHECK_FALSE(set_length(p, 0));
  CHECK_FALSE(set_length(p, 17));
  CHECK(load_word(p.step[3]) == before);
  CHECK(length_of(p) == 16);
  REQUIRE(set_length(p, 5));
  CHECK(length_of(p) == 5);
  for (unsigned i = 0; i < kCarrierMax + 1u; ++i) {  // FOLLOW, SYNTH, NOISE, SLOT 1..4, FOLLOW
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
  CHECK(refused(with_bank(8, 16, pack_step(Step{1, 0, 0, 0, 0, 0}) | (7u << 13))));  // carrier 7 (LIVE)
  CHECK(refused(with_bank(8, 16, 0x00000080u)));                                     // note 128
  CHECK(refused(with_bank(8, 16, 5u << 8)));                                         // kind 5
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
