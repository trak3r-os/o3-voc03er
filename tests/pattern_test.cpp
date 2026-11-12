// SPDX-License-Identifier: GPL-3.0-or-later
// The bytes the app keeps on the card (app/include/o3_voc03er/pattern.h): a round trip, and a short, damaged or foreign file refused with
// nothing applied. Pure: no card and no simulator. (The card itself is tested in app_test.cpp and the scenario files.txt.)
#include <doctest/doctest.h>

#include <o3_voc03er/pattern.h>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace o3_voc03er;

namespace {

Pattern sample() {
  Pattern p;
  const int notes[] = {36, 0, 48, 127, 1, 60};
  for (int n : notes) {
    p.record(n);
  }
  return p;
}

std::vector<std::uint8_t> file_of(const Pattern& p) {
  std::uint8_t buf[kPatternFileMax] = {};
  const std::size_t n = write_pattern(p, buf, sizeof buf);
  REQUIRE(n != 0u);
  return std::vector<std::uint8_t>(buf, buf + n);
}

}  // namespace

TEST_CASE("pattern: the cursor records, wraps and a rest is 0") {
  Pattern p;
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    p.record(40 + static_cast<int>(i));
  }
  CHECK(p.cursor == 0u);  // 16 notes: back at the first step
  CHECK(p.note[0] == 40);
  CHECK(p.note[15] == 55);
  p.record(200);  // out of range: a rest, over the first step
  CHECK(p.note[0] == 0);
  CHECK(p.cursor == 1u);
  CHECK(p.used() == 15u);
  p.clear();
  CHECK(p.used() == 0u);
  CHECK(p.cursor == 0u);
}

TEST_CASE("pattern: a round trip gives the same steps, and the cursor starts again") {
  const Pattern p = sample();
  const std::vector<std::uint8_t> bytes = file_of(p);
  CHECK(bytes.size() == 54u);  // the size the buffers' constant is chosen for
  CHECK(bytes.size() <= kPatternFileMax);
  Pattern back;
  back.note[5] = 99;  // whatever was there is replaced
  back.cursor = 7;
  REQUIRE(read_pattern(bytes.data(), bytes.size(), back));
  CHECK(back == p);
  CHECK(back.used() == 5u);
}

TEST_CASE("pattern: a write into a buffer that is too small writes nothing") {
  std::uint8_t tiny[40] = {};
  CHECK(write_pattern(sample(), tiny, sizeof tiny) == 0u);
}

TEST_CASE("pattern: a short file is refused and the pattern is left as it was") {
  const std::vector<std::uint8_t> bytes = file_of(sample());
  Pattern keep;
  keep.record(70);
  const Pattern before = keep;
  for (std::size_t n : {std::size_t{0}, std::size_t{11}, std::size_t{12}, bytes.size() - 1u}) {
    INFO("length " << n);
    CHECK_FALSE(read_pattern(bytes.data(), n, keep));
    CHECK(keep == before);
  }
}

TEST_CASE("pattern: a damaged file is refused (the CRC), and so is one of another type") {
  std::vector<std::uint8_t> bytes = file_of(sample());
  Pattern keep;
  const Pattern before = keep;
  for (std::size_t at = 0; at < bytes.size(); ++at) {  // one flipped bit in each byte in turn
    if (at == bytes.size() - 6u || at == bytes.size() - 5u) {
      continue;  // the CRC chunk's own version field: the one thing the CRC does not cover (a newer minor is read)
    }
    std::vector<std::uint8_t> bad = bytes;
    bad[at] = static_cast<std::uint8_t>(bad[at] ^ 0x10u);
    INFO("byte " << at);
    CHECK_FALSE(read_pattern(bad.data(), bad.size(), keep));
  }
  CHECK(keep == before);
  // A good container of the config's type is not a pattern.
  std::uint8_t cfg[kConfigFileMax] = {};
  const std::size_t n = write_config(1, cfg, sizeof cfg);
  REQUIRE(n != 0u);
  CHECK_FALSE(read_pattern(cfg, n, keep));
}

TEST_CASE("pattern: a well-formed file of another shape is refused whole") {
  namespace chunk = o3sdk::formats::chunk;
  std::uint8_t buf[kPatternFileMax] = {};
  Pattern keep;
  // 15 steps, a note above 127, a newer major version: each is a good container with a bad STEP.
  struct Case {
    std::uint16_t version;
    std::uint8_t steps;
    std::uint8_t first;
  };
  const Case cases[] = {{0x0100, 15, 36}, {0x0100, 16, 128}, {0x0200, 16, 36}};
  for (const Case& c : cases) {
    chunk::Writer w(buf, sizeof buf, "PATN");
    std::uint8_t notes[kPatternSteps] = {};
    notes[0] = c.first;
    REQUIRE(w.begin("STEP"));
    REQUIRE(w.u16(c.version));
    REQUIRE(w.u8(c.steps));
    REQUIRE(w.bytes(notes, c.steps));  // (the 15-step case is consistent in itself)
    REQUIRE(w.end());
    const std::size_t n = w.finish();
    REQUIRE(n != 0u);
    INFO("version " << c.version << " steps " << int(c.steps) << " first " << int(c.first));
    CHECK_FALSE(read_pattern(buf, n, keep));
  }
  // A newer minor with a chunk this version does not know is read as far as it is known.
  chunk::Writer w(buf, sizeof buf, "PATN");
  std::uint8_t notes[kPatternSteps] = {40};
  REQUIRE(w.chunk("NEWR", notes, 3));
  REQUIRE(w.begin("STEP"));
  REQUIRE(w.u16(0x0103));
  REQUIRE(w.u8(kPatternSteps));
  REQUIRE(w.bytes(notes, sizeof notes));
  REQUIRE(w.u8(9));  // a field of the newer minor
  REQUIRE(w.end());
  const std::size_t n = w.finish();
  REQUIRE(n != 0u);
  CHECK(read_pattern(buf, n, keep));
  CHECK(keep.note[0] == 40);
}

TEST_CASE("config: the octave round trips for every octave, and a bad file is refused") {
  for (int octave = kMinOctave; octave <= kMaxOctave; ++octave) {
    std::uint8_t buf[kConfigFileMax] = {};
    const std::size_t n = write_config(octave, buf, sizeof buf);
    REQUIRE(n != 0u);
    CHECK(n == 62u);  // CONF + OCTV + VOIC + SLCH
    int back = 99;
    REQUIRE(read_config(buf, n, back));
    CHECK(back == octave);
  }
  std::uint8_t buf[kConfigFileMax] = {};
  const std::size_t n = write_config(-1, buf, sizeof buf);
  int keep = 5;
  CHECK_FALSE(read_config(buf, n - 1u, keep));  // short
  buf[22] = static_cast<std::uint8_t>(buf[22] ^ 0x01u);  // the octave byte
  CHECK_FALSE(read_config(buf, n, keep));       // damaged
  CHECK(keep == 5);
  // The octave is clamped when written, and a stored value past the range is refused when read.
  CHECK(write_config(50, buf, sizeof buf) == n);
  CHECK(read_config(buf, n, keep));
  CHECK(keep == kMaxOctave);
  namespace chunk = o3sdk::formats::chunk;
  chunk::Writer w(buf, sizeof buf, "CONF");
  REQUIRE(w.begin("OCTV"));
  REQUIRE(w.u16(0x0100));
  REQUIRE(w.u8(6));
  REQUIRE(w.end());
  const std::size_t m = w.finish();
  CHECK_FALSE(read_config(buf, m, keep));
}

TEST_CASE("config: the voicing round trips, a file from before it reads as SMOOTH, a bad one is refused") {
  for (unsigned voicing = 0; voicing <= 3; ++voicing) {
    std::uint8_t buf[kConfigFileMax] = {};
    const std::size_t n = write_config(1, buf, sizeof buf, voicing);
    REQUIRE(n != 0u);
    int octave = 99;
    unsigned back = 99;
    REQUIRE(read_config(buf, n, octave, back));
    CHECK(octave == 1);
    CHECK(back == voicing);
  }
  namespace chunk = o3sdk::formats::chunk;
  std::uint8_t buf[kConfigFileMax] = {};
  int octave = 99;
  unsigned voicing = 0;
  // The file the first version wrote: OCTV only. It still loads, and the voicing is SMOOTH.
  {
    chunk::Writer w(buf, sizeof buf, "CONF");
    REQUIRE(w.begin("OCTV"));
    REQUIRE(w.u16(0x0100));
    REQUIRE(w.u8(static_cast<std::uint8_t>(2 - kMinOctave)));
    REQUIRE(w.end());
    const std::size_t n = w.finish();
    REQUIRE(n == 38u);
    REQUIRE(read_config(buf, n, octave, voicing));
    CHECK(octave == 2);
    CHECK(voicing == 3u);
  }
  // A voicing past SMOOTH refuses the file whole, and both stay as they were.
  {
    chunk::Writer w(buf, sizeof buf, "CONF");
    REQUIRE(w.begin("OCTV"));
    REQUIRE(w.u16(0x0100));
    REQUIRE(w.u8(static_cast<std::uint8_t>(0 - kMinOctave)));
    REQUIRE(w.end());
    REQUIRE(w.begin("VOIC"));
    REQUIRE(w.u16(0x0100));
    REQUIRE(w.u8(4));
    REQUIRE(w.end());
    const std::size_t n = w.finish();
    octave = 99;
    voicing = 1;
    CHECK_FALSE(read_config(buf, n, octave, voicing));
    CHECK(octave == 99);
    CHECK(voicing == 1u);
  }
  // A voicing asked past the range is written as SMOOTH.
  const std::size_t n = write_config(0, buf, sizeof buf, 9);
  REQUIRE(read_config(buf, n, octave, voicing));
  CHECK(voicing == 3u);
}

TEST_CASE("config: the MIDI slice channel round trips, a file from before it reads as SAME, a bad one is refused") {
  for (unsigned ch : {0u, 1u, 9u, 16u, 17u}) {
    std::uint8_t buf[kConfigFileMax] = {};
    const std::size_t n = write_config(-1, buf, sizeof buf, 2, ch);
    REQUIRE(n != 0u);
    int octave = 99;
    unsigned voicing = 99;
    unsigned back = 99;
    REQUIRE(read_config(buf, n, octave, voicing, back));
    CHECK(octave == -1);
    CHECK(voicing == 2u);
    CHECK(back == ch);
  }
  namespace chunk = o3sdk::formats::chunk;
  std::uint8_t buf[kConfigFileMax] = {};
  int octave = 99;
  unsigned voicing = 99;
  unsigned slice = 5;
  // The file the voicing's version wrote (OCTV and VOIC): it loads, and the slice channel is SAME.
  {
    chunk::Writer w(buf, sizeof buf, "CONF");
    REQUIRE(w.begin("OCTV"));
    REQUIRE(w.u16(0x0100));
    REQUIRE(w.u8(static_cast<std::uint8_t>(1 - kMinOctave)));
    REQUIRE(w.end());
    REQUIRE(w.begin("VOIC"));
    REQUIRE(w.u16(0x0100));
    REQUIRE(w.u8(1));
    REQUIRE(w.end());
    const std::size_t n = w.finish();
    REQUIRE(read_config(buf, n, octave, voicing, slice));
    CHECK(octave == 1);
    CHECK(voicing == 1u);
    CHECK(slice == 0u);
  }
  // A slice channel past OFF refuses the file whole, and all three stay as they were.
  {
    chunk::Writer w(buf, sizeof buf, "CONF");
    REQUIRE(w.begin("OCTV"));
    REQUIRE(w.u16(0x0100));
    REQUIRE(w.u8(static_cast<std::uint8_t>(0 - kMinOctave)));
    REQUIRE(w.end());
    REQUIRE(w.begin("SLCH"));
    REQUIRE(w.u16(0x0100));
    REQUIRE(w.u8(18));
    REQUIRE(w.end());
    const std::size_t n = w.finish();
    octave = 99;
    voicing = 1;
    slice = 5;
    CHECK_FALSE(read_config(buf, n, octave, voicing, slice));
    CHECK(octave == 99);
    CHECK(voicing == 1u);
    CHECK(slice == 5u);
  }
  // Asked past the range, it is written as SAME; the three-argument reader still reads a file that has the chunk.
  const std::size_t n = write_config(0, buf, sizeof buf, 3, 40);
  REQUIRE(n != 0u);
  REQUIRE(read_config(buf, n, octave, voicing, slice));
  CHECK(slice == 0u);
  CHECK(read_config(buf, n, octave));
}
