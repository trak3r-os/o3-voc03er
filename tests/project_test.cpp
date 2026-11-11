// SPDX-License-Identifier: GPL-3.0-or-later
// The project file (app/include/o3_voc03er/project.h): the round trip, the refusals, the forward compatibility. Pure: no engine, no screen.

#include <doctest/doctest.h>

#include <o3_voc03er/project.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace o3_voc03er;
namespace chunk = o3sdk::formats::chunk;
using Bytes = std::vector<std::uint8_t>;

Project full() {
  Project p = {};
  for (unsigned b = 0; b < kBankPatterns; ++b) {
    PatternData& pd = p.bank.pattern[b];
    CHECK(set_length(pd, 4 + b));
    for (unsigned i = 0; i < kPatternSteps; ++i) {
      Step s;
      s.note = static_cast<std::uint8_t>(36 + i + b);
      s.kind = static_cast<std::uint8_t>(i % 5);
      s.voicing = static_cast<std::uint8_t>(i % 4);
      s.carrier = static_cast<std::uint8_t>(i % (kCarrierMax + 1u));
      s.level = static_cast<std::uint8_t>(1 + i);
      s.slice = static_cast<std::uint8_t>(i % (kSliceMax + 1u));
      CHECK(set_step(pd, i, s));
    }
  }
  p.chain.count = 5;
  p.chain.loop = 1;
  for (unsigned i = 0; i < 5; ++i) {
    p.chain.entry[i].pattern = static_cast<std::uint8_t>((i * 3) % 8);
    p.chain.entry[i].repeats = static_cast<std::uint8_t>(1 + i * 7);
  }
  set_project_route(p, route::Route{route::kFirstSlot, route::kFirstSlot + 1});
  p.live_src = static_cast<std::uint8_t>(route::LiveSrc::MicLow);
  for (unsigned i = 0; i < route::kSlots; ++i) {
    ProjectSlot& s = p.slot[i];
    if (i == 2) {
      continue;  // an empty slot
    }
    sample::even_cuts(s.cuts, 44100u * (i + 1u), 8 + 4 * i);
    s.speed = static_cast<std::int8_t>(static_cast<int>(i % 3) - 1);
    s.reverse = static_cast<std::uint8_t>(i & 1u);
    s.level_half = static_cast<std::int8_t>(static_cast<int>(i) * 10 - 20);
    s.values = static_cast<std::uint8_t>(i != 1u ? 1u : 0u);  // slot 1 is a record from before: no captured values
    const char* nm = i == 0 ? "KICK LOOP 1.wav" : (i == 1 ? "a-31-byte-name-0123456789abcd.wav" : "take3");
    s.name_len = static_cast<std::uint8_t>(std::strlen(nm) > kWavNameMax ? kWavNameMax : std::strlen(nm));
    std::memcpy(s.name, nm, s.name_len);
  }
  p.slot[1].trim_start = 100;
  p.slot[1].trim_end = p.slot[1].cuts.length - 50;
  for (unsigned id = 0; id < Vocoder::kParamCount; ++id) {
    if (engine_id_known(id)) {
      CHECK(project_set_engine(p, id, 0.25f * static_cast<float>(id) - 3.0f));
    }
  }
  return p;
}

// A STAT that is valid and not the default in any 1.1 field.
void with_stat(Project& p) {
  p.stat = 1;
  p.tempo_mbpm = 133500;
  p.swing = 0;
  p.pattern = 5;
  p.octave = -2;
  p.chord = 2;
  p.keys = 1;
  p.voicing = 1;
  p.bend_row = 1;
  p.bend_range = 12;
  p.follow = 1;
  set_project_preset(p, 1);  // CHOIR
  pads::Layout l;
  l.row[0] = pads::Plays::PianoOpen;
  l.row[1] = pads::Plays::PianoOpen;
  l.row[2] = pads::Plays::Bend;
  l.last[0] = pads::Plays::Seventh;
  l.last[1] = pads::Plays::Power;
  set_project_layout(p, l);
}

Project small() {
  Project p = {};
  Step s;
  s.note = 40;
  CHECK(set_step(p.bank.pattern[1], 3, s));
  p.chain.count = 1;
  p.chain.entry[0] = {1, 2};
  set_project_route(p, route::Route{route::kSpeaksLive, route::kSoundsNoise});
  sample::even_cuts(p.slot[0].cuts, 1000, 2);
  p.slot[0].name_len = 5;
  std::memcpy(p.slot[0].name, "a.wav", 5);
  CHECK(project_set_engine(p, Vocoder::Wave, 1.0f));
  CHECK(project_set_engine(p, Vocoder::Key, 7.0f));
  CHECK(project_set_engine(p, Vocoder::Level, -6.5f));
  return p;
}

bool same(const Project& a, const Project& b) {
  for (unsigned i = 0; i < kBankPatterns; ++i) {
    if (a.bank.pattern[i].length != b.bank.pattern[i].length || std::memcmp(a.bank.pattern[i].step, b.bank.pattern[i].step, sizeof a.bank.pattern[i].step) != 0) {
      return false;
    }
  }
  if (a.chain.count != b.chain.count || a.chain.loop != b.chain.loop) {
    return false;
  }
  for (unsigned i = 0; i < kChainMax; ++i) {
    if (a.chain.entry[i].pattern != b.chain.entry[i].pattern || a.chain.entry[i].repeats != b.chain.entry[i].repeats) {
      return false;
    }
  }
  if (a.stat != b.stat) {
    return false;
  }
  if (a.stat != 0u && (a.tempo_mbpm != b.tempo_mbpm || a.swing != b.swing || a.pattern != b.pattern || a.octave != b.octave || a.chord != b.chord || a.keys != b.keys ||
                       a.voicing != b.voicing || a.bend_row != b.bend_row || a.bend_range != b.bend_range || a.follow != b.follow || a.preset_len != b.preset_len ||
                       std::memcmp(a.preset, b.preset, sizeof a.preset) != 0 || std::memcmp(a.layout, b.layout, sizeof a.layout) != 0)) {
    return false;
  }
  if (a.speaks != b.speaks || a.sounds != b.sounds || a.live_src != b.live_src || a.engine_set != b.engine_set) {
    return false;
  }
  for (unsigned id = 0; id < kEngineIds; ++id) {
    if (project_has_engine(a, id) && std::memcmp(&a.engine[id], &b.engine[id], sizeof(float)) != 0) {
      return false;
    }
  }
  for (unsigned i = 0; i < route::kSlots; ++i) {
    const ProjectSlot& x = a.slot[i];
    const ProjectSlot& y = b.slot[i];
    if (x.cuts.length != y.cuts.length || x.cuts.count != y.cuts.count || x.trim_start != y.trim_start || x.trim_end != y.trim_end || x.speed != y.speed ||
        x.reverse != y.reverse || x.level_half != y.level_half || x.values != y.values || x.gate != y.gate || x.choke_off != y.choke_off || x.name_len != y.name_len || std::memcmp(x.name, y.name, sizeof x.name) != 0) {
      return false;
    }
    for (unsigned k = 0; k < x.cuts.count; ++k) {
      if (x.cuts.cut[k].start != y.cuts.cut[k].start || x.cuts.cut[k].len != y.cuts.cut[k].len) {
        return false;
      }
    }
  }
  return true;
}

Bytes write(const Project& p) {
  alignas(4) std::array<std::uint8_t, kProjectFileMax> buf = {};
  const std::size_t n = write_project(p, buf.data(), buf.size());
  return Bytes(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
}

bool read(const Bytes& b, Project& into) { return read_project(b.data(), b.size(), into); }

// The file again with one change: a chunk dropped, one replaced, an unknown one in front.
struct Edit {
  const char* drop = nullptr;
  const char* replace = nullptr;
  Bytes with;
  bool extra = false;
};

Bytes rebuild(const Bytes& src, const Edit& e) {
  chunk::Reader r;
  REQUIRE(r.open(src.data(), src.size(), "VOCP") == chunk::Error::None);
  alignas(4) std::array<std::uint8_t, kProjectFileMax> buf = {};
  chunk::Writer w(buf.data(), buf.size(), "VOCP");
  if (e.extra) {
    static const std::uint8_t kX[5] = {1, 2, 3, 4, 5};
    REQUIRE(w.chunk("XTRA", kX, sizeof kX));
  }
  chunk::Chunk c{};
  while (r.next(c)) {
    const char id[5] = {c.id[0], c.id[1], c.id[2], c.id[3], 0};
    if (e.drop != nullptr && std::memcmp(id, e.drop, 4) == 0) {
      continue;
    }
    if (e.replace != nullptr && std::memcmp(id, e.replace, 4) == 0) {
      REQUIRE(w.chunk(id, e.with.data(), e.with.size()));
    } else {
      REQUIRE(w.chunk(id, c.data, c.size));
    }
  }
  const std::size_t n = w.finish();
  REQUIRE(n != 0);
  return Bytes(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
}

void put16(Bytes& b, unsigned v) {
  b.push_back(static_cast<std::uint8_t>(v >> 8));
  b.push_back(static_cast<std::uint8_t>(v));
}
void put32(Bytes& b, std::uint32_t v) {
  put16(b, v >> 16);
  put16(b, v & 0xFFFFu);
}

// A VALU payload: version, then (id, value) pairs.
Bytes valu(std::initializer_list<std::pair<unsigned, float>> pairs, unsigned version = 0x0100) {
  Bytes b;
  put16(b, version);
  put16(b, static_cast<unsigned>(pairs.size()));
  for (const auto& pr : pairs) {
    put16(b, pr.first);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &pr.second, sizeof bits);
    put32(b, bits);
  }
  return b;
}

}  // namespace

TEST_CASE("project: a full project round-trips exactly") {
  const Project p = full();
  const Bytes f = write(p);
  REQUIRE(!f.empty());
  CHECK(f.size() <= kProjectFileMax);
  Project got = {};
  REQUIRE(read(f, got));
  CHECK(same(p, got));
  CHECK(project_route(got).speaks == route::kFirstSlot);
  CHECK(project_route(got).sounds == route::kFirstSlot + 1);
  CHECK(write(got) == f);  // and the bytes are a function of the data
}

TEST_CASE("project: an empty project round-trips, replacing what was in the destination") {
  const Project z = {};
  const Bytes f = write(z);
  REQUIRE(!f.empty());
  Project got = full();
  REQUIRE(read(f, got));
  CHECK(same(z, got));
  CHECK(got.engine_set == 0u);
  const route::Route r = project_route(got);
  CHECK(r.speaks == route::kSpeaksLive);
  CHECK(r.sounds == route::kSoundsSynth);
}

TEST_CASE("project: the worst case fits the buffer") {
  Project p = full();
  for (ProjectSlot& s : p.slot) {
    sample::even_cuts(s.cuts, Loop::kMaxSamples, sample::kMaxSlices);
    s.name_len = static_cast<std::uint8_t>(kWavNameMax);
    std::memset(s.name, 'x', kWavNameMax);
    s.values = 1;
    s.gate = 1;
    s.choke_off = 1;
  }
  with_stat(p);
  p.preset_len = static_cast<std::uint8_t>(kPresetNameMax);  // the longest name a file may hold
  std::memset(p.preset, 'W', kPresetNameMax);
  p.chain.count = kChainMax;
  for (unsigned i = 0; i < kChainMax; ++i) {
    p.chain.entry[i] = {static_cast<std::uint8_t>(i % kBankPatterns), kChainRepeatMax};
  }
  const Bytes f = write(p);
  REQUIRE(!f.empty());
  CHECK(f.size() <= project_detail::kWorst);
  CHECK(f.size() <= kProjectFileMax);
  Project got = {};
  REQUIRE(read(f, got));
  CHECK(same(p, got));
}

TEST_CASE("project: every single-byte corruption of a small file is refused and changes nothing") {
  const Project orig = small();
  const Bytes f = write(orig);
  REQUIRE(f.size() > 100);
  const Project sentinel = full();
  unsigned accepted = 0, touched = 0;
  for (std::size_t i = 0; i < f.size(); ++i) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      Bytes g = f;
      g[i] = static_cast<std::uint8_t>(g[i] ^ (1u << bit));
      Project into = sentinel;
      if (read(g, into)) {
        // The one byte the CRC does not cover is the minor of the CRC chunk's own version: accepted, and the data are the same.
        accepted += same(into, orig) ? 0u : 1u;
      } else if (!same(into, sentinel)) {
        ++touched;
      }
    }
    Bytes g = f;
    g[i] = static_cast<std::uint8_t>(~g[i]);
    Project into = sentinel;
    accepted += (read(g, into) && !same(into, orig)) ? 1u : 0u;
  }
  CHECK(accepted == 0u);
  CHECK(touched == 0u);
}

TEST_CASE("project: a cut file is refused and changes nothing") {
  const Bytes f = write(full());
  const Project sentinel = small();
  unsigned accepted = 0, touched = 0;
  for (std::size_t n = 0; n < f.size(); ++n) {
    Project into = sentinel;
    if (read_project(f.data(), n, into)) {
      ++accepted;
    } else if (!same(into, sentinel)) {
      ++touched;
    }
  }
  CHECK(accepted == 0u);
  CHECK(touched == 0u);
  Project into = sentinel;
  CHECK(!read_project(nullptr, 0, into));
}

TEST_CASE("project: an unknown chunk is skipped") {
  const Project p = full();
  Edit e;
  e.extra = true;
  const Bytes g = rebuild(write(p), e);
  REQUIRE(g.size() > write(p).size());
  Project got = {};
  REQUIRE(read(g, got));
  CHECK(same(p, got));
}

TEST_CASE("project: an engine id the table does not know, or that is an action, is ignored") {
  // Wave and Key are state; 200 and 64 are outside the table; Load and CarSrc and a loop's Play are actions or another chunk's; Freeze is left out.
  const Edit e{nullptr, "VALU", valu({{Vocoder::Wave, 1.0f}, {200, 5.0f}, {64, 5.0f}, {Vocoder::Load, 4.0f}, {Vocoder::CarSrc, 3.0f}, {Vocoder::Loop2Play, 1.0f},
                                      {Vocoder::Freeze, 1.0f}, {Vocoder::Key, 7.0f}, {Vocoder::kParamCount, 1.0f}}), false};
  const Bytes g = rebuild(write(small()), e);
  Project got = {};
  REQUIRE(read(g, got));
  CHECK(got.engine_set == ((std::uint64_t{1} << Vocoder::Wave) | (std::uint64_t{1} << Vocoder::Key)));
  CHECK(got.engine[Vocoder::Wave] == 1.0f);
  CHECK(got.engine[Vocoder::Key] == 7.0f);
}

TEST_CASE("project: a value that is not a number is a damaged file") {
  const Edit e{nullptr, "VALU", valu({{Vocoder::Wave, 1.0f}, {Vocoder::Key, std::numeric_limits<float>::quiet_NaN()}}), false};
  const Bytes g = rebuild(write(small()), e);
  Project into = full();
  CHECK(!read(g, into));
  CHECK(same(into, full()));
}

TEST_CASE("project: a file missing a chunk leaves that part at its defaults") {
  const Project p = full();
  const Bytes f = write(p);
  for (const char* drop : {"PTNS", "CHAI", "ROUT", "SLOT", "VALU"}) {
    Edit e;
    e.drop = drop;
    Project got = {};
    REQUIRE(read(rebuild(f, e), got));
    Project want = p;
    if (std::strcmp(drop, "PTNS") == 0) {
      want.bank = Bank{};
    } else if (std::strcmp(drop, "CHAI") == 0) {
      want.chain = Chain{};
    } else if (std::strcmp(drop, "ROUT") == 0) {
      want.speaks = want.sounds = want.live_src = 0;
    } else if (std::strcmp(drop, "SLOT") == 0) {
      for (ProjectSlot& s : want.slot) {
        s = ProjectSlot{};
      }
    } else {
      want.engine_set = 0;
    }
    CHECK_MESSAGE(same(got, want), drop);
  }
  Edit all;
  all.drop = "PTNS";
  Project got = full();
  REQUIRE(read(rebuild(rebuild(f, all), Edit{"SLOT", nullptr, {}, false}), got));
  CHECK(got.slot[0].cuts.length == 0u);
  CHECK(got.bank.pattern[0].length == 0u);
  CHECK(got.chain.count == 5);
}

TEST_CASE("project: a newer major of a known chunk is refused, a newer minor is read as far as it is known") {
  const Bytes f = write(small());
  Project into = full();
  CHECK(!read(rebuild(f, Edit{nullptr, "VALU", valu({{Vocoder::Wave, 1.0f}}, 0x0200), false}), into));
  CHECK(same(into, full()));
  Bytes chai;
  put16(chai, 0x0105);
  chai.push_back(1);
  chai.push_back(0);
  chai.push_back(2);
  chai.push_back(9);
  chai.push_back(0xEE);  // something a later minor appended
  chai.push_back(0xEF);
  Project got = {};
  REQUIRE(read(rebuild(f, Edit{nullptr, "CHAI", chai, false}), got));
  CHECK(got.chain.count == 1);
  CHECK(got.chain.entry[0].pattern == 2);
  CHECK(got.chain.entry[0].repeats == 9);
}

TEST_CASE("project: a slot record a later minor made longer is skipped, and a record that cannot hold its fields is refused") {
  // One slot of 1000 samples, no cuts, name "b" and three bytes of a later minor after it.
  auto slot_chunk = [](unsigned rec_len, unsigned count_of_slots) {
    Bytes b;
    put16(b, 0x0102);
    b.push_back(static_cast<std::uint8_t>(count_of_slots));
    put16(b, rec_len);
    put32(b, 1000);
    b.push_back(0);   // cuts
    put32(b, 0);      // trim
    put32(b, 0);
    b.push_back(1);   // speed +1 (two's complement)
    b.push_back(1);   // reverse
    b.push_back(static_cast<std::uint8_t>(-4));  // level -4 half-dB
    b.push_back(1);   // name length
    b.push_back('b');
    b.push_back(1);     // values (U9c4)
    b.push_back(1);     // TRIG GATE (1.1)
    b.push_back(1);     // CHOKE OFF (1.1)
    b.push_back(0xAA);  // a later minor's tail
    b.push_back(0xBB);
    b.push_back(0xCC);
    return b;
  };
  const Bytes f = write(small());
  const unsigned base = 4 + 1 + 8 + 4 + 1 + 3;  // the fields up to the name, then values, TRIG and CHOKE
  Project got = {};
  REQUIRE(read(rebuild(f, Edit{nullptr, "SLOT", slot_chunk(base + 3, 1), false}), got));
  CHECK(got.slot[0].values == 1u);
  CHECK(got.slot[0].gate == 1u);
  CHECK(got.slot[0].choke_off == 1u);
  CHECK(got.slot[0].cuts.length == 1000u);
  CHECK(got.slot[0].speed == 1);
  CHECK(got.slot[0].reverse == 1);
  CHECK(got.slot[0].level_half == -4);
  CHECK(got.slot[0].name_len == 1);
  CHECK(got.slot[0].name[0] == 'b');
  CHECK(got.slot[1].cuts.length == 0u);  // the slots the file lacks are defaults
  Project into = full();
  CHECK(!read(rebuild(f, Edit{nullptr, "SLOT", slot_chunk(base - 1, 1), false}), into));  // the record says it is shorter than its own fields
  CHECK(!read(rebuild(f, Edit{nullptr, "SLOT", slot_chunk(base + 40, 1), false}), into));  // longer than what is left of the chunk
  CHECK(same(into, full()));
}

TEST_CASE("project: a known chunk twice is refused") {
  const Bytes f = write(small());
  chunk::Reader r;
  REQUIRE(r.open(f.data(), f.size(), "VOCP") == chunk::Error::None);
  alignas(4) std::array<std::uint8_t, kProjectFileMax> buf = {};
  chunk::Writer w(buf.data(), buf.size(), "VOCP");
  chunk::Chunk c{};
  while (r.next(c)) {
    const char id[5] = {c.id[0], c.id[1], c.id[2], c.id[3], 0};
    REQUIRE(w.chunk(id, c.data, c.size));
    if (chunk::id_is(c, "CHAI")) {
      REQUIRE(w.chunk(id, c.data, c.size));
    }
  }
  const std::size_t n = w.finish();
  REQUIRE(n != 0);
  Project into = {};
  CHECK(!read_project(buf.data(), n, into));
}

TEST_CASE("project: values out of range are refused on both sides") {
  const auto refused = [](auto&& damage) {
    Project p = full();
    damage(p);
    alignas(4) std::array<std::uint8_t, kProjectFileMax> buf = {};
    CHECK(write_project(p, buf.data(), buf.size()) == 0u);
  };
  refused([](Project& p) { p.slot[0].cuts.count = sample::kMaxSlices + 1; });
  refused([](Project& p) { p.slot[0].cuts.cut[0].len = p.slot[0].cuts.length + 1; });
  refused([](Project& p) { p.slot[0].speed = 2; });
  refused([](Project& p) { p.slot[0].level_half = 25; });
  refused([](Project& p) { p.slot[0].name[0] = '/'; });
  refused([](Project& p) { p.slot[0].name_len = static_cast<std::uint8_t>(kWavNameMax + 1); });
  refused([](Project& p) { p.slot[1].trim_end = p.slot[1].trim_start; });
  refused([](Project& p) { p.chain.count = kChainMax + 1; });
  refused([](Project& p) { p.chain.entry[0].repeats = 0; });
  refused([](Project& p) { p.chain.entry[0].repeats = kChainRepeatMax + 1; });
  refused([](Project& p) { p.chain.entry[0].pattern = kBankPatterns; });
  refused([](Project& p) { p.chain.loop = 2; });
  refused([](Project& p) { p.bank.pattern[0].length = kPatternSteps + 1; });
  refused([](Project& p) { p.bank.pattern[0].step[0] = 0xFFFFFFFFu; });
  refused([](Project& p) { set_project_route(p, route::Route{route::kFirstSlot, route::kFirstSlot}); });  // a slot on both sides
  refused([](Project& p) { p.live_src = route::kLiveSrcCount; });
  refused([](Project& p) { p.slot[0].gate = 2; });
  refused([](Project& p) { p.slot[0].choke_off = 2; });
  refused([](Project& p) { p.engine_set |= std::uint64_t{1} << Vocoder::Load; });
  Project scratch = {};
  CHECK(!project_set_engine(scratch, Vocoder::Load, 1.0f));
  CHECK(!project_set_engine(scratch, Vocoder::Wave, std::numeric_limits<float>::infinity()));
}

TEST_CASE("project: which ids are state") {
  unsigned known = 0;
  for (unsigned id = 0; id < kEngineIds; ++id) {
    const bool action_or_owned = id == Vocoder::Freeze || (id >= Vocoder::Loop1Rec && id <= Vocoder::Loop4Level) || id == Vocoder::CarSrc || id == Vocoder::ModSrc ||
                                 (id >= Vocoder::R1Start && id <= Vocoder::Load);
    CHECK(engine_id_is_state(id) == !action_or_owned);
    known += engine_id_known(id) ? 1u : 0u;
  }
  CHECK(!engine_id_is_state(kEngineIds));
  CHECK(engine_id_is_state(62));  // SUB and TILT, when the unit has them
  CHECK(engine_id_is_state(63));
  CHECK(engine_id_is_state(Vocoder::Wave));
  CHECK(engine_id_is_state(Vocoder::LiveCh));
  CHECK(known >= 22u);
}

TEST_CASE("project: STAT (U9c2) round trip, a file without it, a tempo out of range") {
  Project p = full();
  with_stat(p);
  p.tempo_mbpm = 133500;
  p.swing = 3;
  p.pattern = 5;
  p.octave = -2;
  p.chord = 2;
  p.keys = 1;
  const Bytes b = write(p);
  REQUIRE(!b.empty());
  Project q = {};
  REQUIRE(read(b, q));
  CHECK(q.stat == 1);
  CHECK(q.tempo_mbpm == 133500u);
  CHECK(q.swing == 3);
  CHECK(q.pattern == 5);
  CHECK(q.octave == -2);
  CHECK(q.chord == 2);
  CHECK(q.keys == 1);

  // Without STAT (an older file, or a project that never had one): the fields stay at their zero defaults and `stat` says there was none.
  Project old = full();
  old.stat = 0;
  const Bytes ob = write(old);
  Project r = {};
  REQUIRE(read(ob, r));
  CHECK(r.stat == 0);
  CHECK(r.tempo_mbpm == 0u);
  CHECK(r.octave == 0);

  // A tempo out of range or a pattern past the bank is refused at write, so a file never holds one.
  Project bad = p;
  bad.tempo_mbpm = 1000;
  alignas(4) std::array<std::uint8_t, kProjectFileMax> buf = {};
  CHECK(write_project(bad, buf.data(), buf.size()) == 0u);
  bad = p;
  bad.pattern = static_cast<std::uint8_t>(kBankPatterns);
  CHECK(write_project(bad, buf.data(), buf.size()) == 0u);
}

TEST_CASE("project: U9c4 the captured trim, speed, reverse and level round-trip one by one, and a file from before loads as it did") {
  const auto one = [](auto&& set) {
    Project p = small();
    p.slot[0].values = 1;
    set(p.slot[0]);
    const Bytes f = write(p);
    REQUIRE(!f.empty());
    Project got = {};
    REQUIRE(read(f, got));
    CHECK(same(p, got));
    CHECK(got.slot[0].values == 1u);
    return got;
  };
  CHECK(one([](ProjectSlot& s) { s.trim_start = 10; s.trim_end = 900; }).slot[0].trim_end == 900u);
  CHECK(one([](ProjectSlot& s) { s.speed = -1; }).slot[0].speed == -1);
  CHECK(one([](ProjectSlot& s) { s.speed = 1; }).slot[0].speed == 1);
  CHECK(one([](ProjectSlot& s) { s.reverse = 1; }).slot[0].reverse == 1u);
  CHECK(one([](ProjectSlot& s) { s.level_half = -48; }).slot[0].level_half == -48);
  CHECK(one([](ProjectSlot& s) { s.level_half = 24; }).slot[0].level_half == 24);

  // The record of an app from before this minor ends at the name: values is 0 and every other field is read as it was.
  Bytes rec;
  put16(rec, 0x0100);
  rec.push_back(1);
  put16(rec, 4 + 1 + 8 + 4 + 1);
  put32(rec, 1000);
  rec.push_back(0);
  put32(rec, 20);
  put32(rec, 800);
  rec.push_back(1);
  rec.push_back(1);
  rec.push_back(static_cast<std::uint8_t>(-6));
  rec.push_back(1);
  rec.push_back('b');
  Project got = {};
  REQUIRE(read(rebuild(write(small()), Edit{nullptr, "SLOT", rec, false}), got));
  CHECK(got.slot[0].values == 0u);
  CHECK(got.slot[0].trim_start == 20u);
  CHECK(got.slot[0].trim_end == 800u);
  CHECK(got.slot[0].speed == 1);
  CHECK(got.slot[0].reverse == 1u);
  CHECK(got.slot[0].level_half == -6);
  CHECK(got.slot[0].name_len == 1);
}

// ---- P3 + Q8: STAT 1.1 and SLOT 1.1 ----

namespace {

// A STAT payload as a 1.0 build wrote it: version, tempo, swing, pattern, octave, chord kind, pads mode.
Bytes stat10(unsigned chord, unsigned keys) {
  Bytes b;
  put16(b, 0x0100);
  put32(b, 98000);
  b.push_back(0);
  b.push_back(2);
  b.push_back(static_cast<std::uint8_t>(-1));
  b.push_back(static_cast<std::uint8_t>(chord));
  b.push_back(static_cast<std::uint8_t>(keys));
  return b;
}

// The 1.1 STAT as bytes (so a test can damage one byte): the 1.0 part, then voicing, bend row, range, FOLLOW, the name, the layout.
Bytes stat11(unsigned version = 0x0101) {
  Bytes b = stat10(1, 0);
  b[0] = static_cast<std::uint8_t>(version >> 8);
  b[1] = static_cast<std::uint8_t>(version);
  b.push_back(2);   // voicing
  b.push_back(1);   // bend row
  b.push_back(5);   // range
  b.push_back(1);   // FOLLOW
  b.push_back(5);   // name length
  for (const char ch : {'C', 'H', 'O', 'I', 'R'}) {
    b.push_back(static_cast<std::uint8_t>(ch));
  }
  for (const unsigned v : {1u, 2u, 12u, 0u, 1u}) {  // A TRIAD, B 7TH, C BEND, last NOTE, TRIAD
    b.push_back(static_cast<std::uint8_t>(v));
  }
  return b;
}

Project stat_base() {
  Project p = small();
  p.stat = 1;
  p.tempo_mbpm = 120000;
  p.voicing = 3;
  p.bend_range = 2;
  return p;
}

}  // namespace

TEST_CASE("project: P3 the 1.1 STAT and SLOT fields round-trip one by one") {
  Project p = full();
  with_stat(p);
  for (unsigned i = 0; i < route::kSlots; ++i) {
    p.slot[i].gate = static_cast<std::uint8_t>(i & 1u);
    p.slot[i].choke_off = static_cast<std::uint8_t>((i >> 1) & 1u);
  }
  const Bytes b = write(p);
  REQUIRE(!b.empty());
  Project q = {};
  REQUIRE(read(b, q));
  CHECK(same(p, q));
  CHECK(q.voicing == 1);
  CHECK(q.bend_row == 1);
  CHECK(q.bend_range == 12);
  CHECK(q.follow == 1);
  REQUIRE(q.preset_len == 5);
  CHECK(std::memcmp(q.preset, "CHOIR", 5) == 0);
  CHECK(project_preset_index(q) == 1);
  const pads::Layout l = project_layout(q);
  CHECK(l.row[0] == pads::Plays::PianoOpen);
  CHECK(l.row[1] == pads::Plays::PianoOpen);
  CHECK(l.row[2] == pads::Plays::Bend);
  CHECK(l.last[0] == pads::Plays::Seventh);
  CHECK(l.last[1] == pads::Plays::Power);
  CHECK(q.slot[1].gate == 1u);
  CHECK(q.slot[2].choke_off == 1u);  // the empty slot's TRIG and CHOKE ride too
  CHECK(q.slot[0].gate == 0u);
  CHECK(write(q) == b);

  // Each value of the small sets round-trips: every voicing, flag, range and preset of the table.
  for (unsigned v = 0; v <= 3; ++v) {
    Project r = p;
    r.voicing = static_cast<std::uint8_t>(v);
    Project g = {};
    REQUIRE(read(write(r), g));
    CHECK(g.voicing == v);
  }
  for (const unsigned range : {2u, 5u, 12u}) {
    Project r = p;
    r.bend_range = static_cast<std::uint8_t>(range);
    Project g = {};
    REQUIRE(read(write(r), g));
    CHECK(g.bend_range == range);
  }
  for (unsigned i = 0; i < kPresetCount; ++i) {
    Project r = p;
    set_project_preset(r, i);
    Project g = {};
    REQUIRE(read(write(r), g));
    CHECK(project_preset_index(g) == static_cast<int>(i));
  }
  Project none = p;
  set_project_preset(none, kPresetCount);
  Project g = {};
  REQUIRE(read(write(none), g));
  CHECK(g.preset_len == 0);
  CHECK(project_preset_index(g) == -1);
}

TEST_CASE("project: P3 a preset is matched by name, an unknown name is none") {
  Project p = stat_base();
  p.preset_len = 5;
  std::memcpy(p.preset, "NOPE!", 5);
  Project g = {};
  REQUIRE(read(write(p), g));  // a name a later build added is kept in the file and means no preset here
  CHECK(g.preset_len == 5);
  CHECK(project_preset_index(g) == -1);
  Project h = stat_base();
  h.preset_len = 4;
  std::memcpy(h.preset, "ROBO", 4);  // a prefix of a name is not that name
  CHECK(project_preset_index(h) == -1);
  Project k = stat_base();
  set_project_preset(k, 7);  // the last of the table: the name, not the index, is what the file holds
  Bytes b = write(k);
  Project r = {};
  REQUIRE(read(b, r));
  CHECK(project_preset_index(r) == 7);
  CHECK(std::strlen(kPresets[7].name) == r.preset_len);
}

TEST_CASE("project: P3 a 1.0 STAT loads with the defaults of today and the layout its pads mode and chord kind meant") {
  const Bytes base = write(stat_base());
  // KEYS: a piano on A and B, FORMANT on C, whatever the chord kind.
  {
    Project got = {};
    REQUIRE(read(rebuild(base, Edit{nullptr, "STAT", stat10(2, 1), false}), got));
    CHECK(got.stat == 1);
    CHECK(got.tempo_mbpm == 98000u);
    CHECK(got.pattern == 2);
    CHECK(got.octave == -1);
    CHECK(got.keys == 1);
    CHECK(got.voicing == 3);
    CHECK(got.bend_row == 0);
    CHECK(got.bend_range == 2);
    CHECK(got.follow == 0);
    CHECK(got.preset_len == 0);
    CHECK(project_preset_index(got) == -1);
    const pads::Layout l = project_layout(got);
    CHECK(l.row[0] == pads::Plays::PianoNote);
    CHECK(l.row[1] == pads::Plays::PianoNote);
    CHECK(l.row[2] == pads::Plays::Formant);
  }
  // CHORDS: the layout of pads::layout_from_legacy for each chord kind.
  for (unsigned kind = 0; kind < 4; ++kind) {
    Project got = {};
    REQUIRE(read(rebuild(base, Edit{nullptr, "STAT", stat10(kind, 0), false}), got));
    const pads::Layout want = pads::layout_from_legacy(false, static_cast<ChordKind>(kind), false);
    const pads::Layout l = project_layout(got);
    for (unsigned r = 0; r < pads::kRows; ++r) {
      CHECK(l.row[r] == want.row[r]);
    }
    CHECK(l.last[0] == want.last[0]);
    CHECK(l.last[1] == want.last[1]);
    CHECK(got.voicing == 3);
    CHECK(got.bend_range == 2);
  }
  // The bottom row of kind 0 (TRIAD) is C TRIAD, B above, A above that: not what a previous project left.
  Project got = {};
  REQUIRE(read(rebuild(base, Edit{nullptr, "STAT", stat10(0, 0), false}), got));
  CHECK(project_layout(got).row[2] == pads::Plays::Triad);
}

TEST_CASE("project: P3 a 1.0 SLOT record has TRIG ONE SHOT and CHOKE ON, a 1.0 chunk's extra bytes are not read as 1.1 fields") {
  Bytes rec;
  put16(rec, 0x0100);
  rec.push_back(1);
  put16(rec, 4 + 1 + 8 + 4 + 1 + 1 + 2);  // up to the name, values, and two bytes a 1.0 reader skips
  put32(rec, 1000);
  rec.push_back(0);
  put32(rec, 0);
  put32(rec, 0);
  rec.push_back(1);
  rec.push_back(0);
  rec.push_back(0);
  rec.push_back(1);
  rec.push_back('b');
  rec.push_back(1);  // values
  rec.push_back(1);
  rec.push_back(1);
  Project got = {};
  Project base = small();
  base.slot[0].gate = 1;
  base.slot[0].choke_off = 1;
  REQUIRE(read(rebuild(write(base), Edit{nullptr, "SLOT", rec, false}), got));
  CHECK(got.slot[0].values == 1u);
  CHECK(got.slot[0].gate == 0u);
  CHECK(got.slot[0].choke_off == 0u);
}

TEST_CASE("project: P3 a 1.2 file with bytes after the 1.1 fields still loads") {
  const Project base = stat_base();
  Bytes st = stat11(0x0102);
  st.push_back(0xAB);  // what a later minor appended to STAT
  st.push_back(0xCD);
  Project got = {};
  REQUIRE(read(rebuild(write(base), Edit{nullptr, "STAT", st, false}), got));
  CHECK(got.voicing == 2);
  CHECK(got.bend_row == 1);
  CHECK(got.bend_range == 5);
  CHECK(got.follow == 1);
  CHECK(project_preset_index(got) == 1);
  const pads::Layout l = project_layout(got);
  CHECK(l.row[0] == pads::Plays::Triad);
  CHECK(l.row[1] == pads::Plays::Seventh);
  CHECK(l.row[2] == pads::Plays::Bend);

  // The SLOT record the same way (the test above holds the 1.1 bytes and a 3-byte tail already; here the tail is longer and the slot has a name and cuts).
  Project s = small();
  s.slot[0].gate = 1;
  s.slot[0].choke_off = 1;
  Bytes sl;
  put16(sl, 0x0103);
  sl.push_back(1);
  put16(sl, 4 + 1 + 8 + 4 + 5 + 3 + 6);
  put32(sl, 1000);
  sl.push_back(0);
  put32(sl, 0);
  put32(sl, 0);
  sl.push_back(0);
  sl.push_back(0);
  sl.push_back(0);
  sl.push_back(5);
  for (const char ch : {'a', '.', 'w', 'a', 'v'}) {
    sl.push_back(static_cast<std::uint8_t>(ch));
  }
  sl.push_back(1);  // values
  sl.push_back(1);  // GATE
  sl.push_back(1);  // CHOKE OFF
  for (unsigned i = 0; i < 6; ++i) {
    sl.push_back(0xEE);
  }
  Project g2 = {};
  REQUIRE(read(rebuild(write(s), Edit{nullptr, "SLOT", sl, false}), g2));
  CHECK(g2.slot[0].gate == 1u);
  CHECK(g2.slot[0].choke_off == 1u);
  CHECK(g2.slot[0].name_len == 5);
}

TEST_CASE("project: P3 a STAT field out of range refuses the file and leaves the destination untouched") {
  const Project base = stat_base();
  const Bytes f = write(base);
  const Project sentinel = full();
  // Offsets in the 1.1 STAT payload: 0 version(2), 2 tempo(4), 6 swing, 7 pattern, 8 octave, 9 chord, 10 keys, 11 voicing, 12 bend row, 13 range, 14 FOLLOW,
  // 15 name length, 16..20 the name, 21..25 the layout.
  const auto refused = [&](const char* what, unsigned at, unsigned value) {
    Bytes st = stat11();
    st[at] = static_cast<std::uint8_t>(value);
    Project into = sentinel;
    INFO(what);
    CHECK(!read(rebuild(f, Edit{nullptr, "STAT", st, false}), into));
    CHECK(same(into, sentinel));
  };
  refused("voicing 4", 11, 4);
  refused("bend row 2", 12, 2);
  refused("range 3", 13, 3);
  refused("range 0", 13, 0);
  refused("range 13", 13, 13);
  refused("FOLLOW 2", 14, 2);
  refused("name length 17", 15, 17);
  refused("name length past the chunk", 15, 200);
  refused("a name byte below the space", 16, 0x07);
  refused("a name byte past the tilde", 16, 0x80);
  refused("layout A 13", 21, static_cast<unsigned>(pads::kPlaysCount));
  refused("layout last 255", 25, 255);
  refused("layout A FORMANT (C only)", 21, static_cast<unsigned>(pads::Plays::Formant));
  refused("layout A BEND (C only)", 21, static_cast<unsigned>(pads::Plays::Bend));
  refused("layout B FORMANT", 22, static_cast<unsigned>(pads::Plays::Formant));
  refused("layout C a piano value", 23, static_cast<unsigned>(pads::Plays::PianoNote));
  refused("layout last A BEND", 24, static_cast<unsigned>(pads::Plays::Bend));
  refused("layout A MOUTH (C only)", 21, static_cast<unsigned>(pads::Plays::Mouth));
  refused("layout B MOUTH (C only)", 22, static_cast<unsigned>(pads::Plays::Mouth));
  refused("layout last B MOUTH", 25, static_cast<unsigned>(pads::Plays::Mouth));
  // A STAT cut short of its 1.1 fields (a 1.1 version over 1.0's length) is damaged, not a 1.0.
  Bytes cut = stat11();
  cut.resize(cut.size() - 3);
  Project into = sentinel;
  CHECK(!read(rebuild(f, Edit{nullptr, "STAT", cut, false}), into));
  CHECK(same(into, sentinel));
  // And the good one is accepted, so the offsets above are the right ones.
  Project good = {};
  REQUIRE(read(rebuild(f, Edit{nullptr, "STAT", stat11(), false}), good));
  CHECK(good.voicing == 2);

  // At write: the same values are refused, so a file never holds one.
  alignas(4) std::array<std::uint8_t, kProjectFileMax> buf = {};
  const auto wrefused = [&](auto&& damage) {
    Project p = stat_base();
    damage(p);
    CHECK(write_project(p, buf.data(), buf.size()) == 0u);
  };
  wrefused([](Project& p) { p.voicing = 4; });
  wrefused([](Project& p) { p.bend_row = 2; });
  wrefused([](Project& p) { p.bend_range = 3; });
  wrefused([](Project& p) { p.follow = 2; });
  wrefused([](Project& p) { p.preset_len = static_cast<std::uint8_t>(kPresetNameMax + 1); });
  wrefused([](Project& p) { p.preset_len = 1; p.preset[0] = '\n'; });
  wrefused([](Project& p) { p.layout[0] = pads::kPlaysCount; });
  wrefused([](Project& p) { p.layout[2] = static_cast<std::uint8_t>(pads::Plays::PianoNote); });
  wrefused([](Project& p) { p.layout[0] = static_cast<std::uint8_t>(pads::Plays::Mouth); });
  wrefused([](Project& p) { p.layout[1] = static_cast<std::uint8_t>(pads::Plays::Mouth); });
}

TEST_CASE("project: V2 a layout with row C on MOUTH round-trips whatever the mode, and MOUTH on A or B never loads") {
  Project p = stat_base();
  pads::Layout l = project_layout(p);
  l.row[2] = pads::Plays::Mouth;
  set_project_layout(p, l);
  Bytes b = write(p);
  REQUIRE(!b.empty());
  Project q = {};
  REQUIRE(read(b, q));
  CHECK(project_layout(q).row[2] == pads::Plays::Mouth);
  CHECK(project_layout(q).row[0] == l.row[0]);
  CHECK(project_layout(q).row[1] == l.row[1]);
  CHECK(write(q) == b);
  // A and B take their own values only: the legacy row of a MOUTH layout is a plain NOTE / TRIAD one
  const pads::LegacyFields lf = pads::legacy_fields(l);
  CHECK_FALSE(lf.bend_row);
}

TEST_CASE("project: P3 a file without STAT keeps the header's rule, the 1.1 fields are not read") {
  Project old = full();
  old.stat = 0;
  Project got = {};
  REQUIRE(read(write(old), got));
  CHECK(got.stat == 0);
  CHECK(got.voicing == 0);
  CHECK(got.bend_range == 0);
  CHECK(got.preset_len == 0);
}
