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
        x.reverse != y.reverse || x.level_half != y.level_half || x.name_len != y.name_len || std::memcmp(x.name, y.name, sizeof x.name) != 0) {
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
  }
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
    put16(b, 0x0101);
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
    b.push_back(0xAA);  // a later minor's tail
    b.push_back(0xBB);
    b.push_back(0xCC);
    return b;
  };
  const Bytes f = write(small());
  const unsigned base = 4 + 1 + 8 + 4 + 1;
  Project got = {};
  REQUIRE(read(rebuild(f, Edit{nullptr, "SLOT", slot_chunk(base + 3, 1), false}), got));
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
