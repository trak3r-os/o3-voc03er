// SPDX-License-Identifier: GPL-3.0-or-later
// SAMPLE LOAD's pure logic (app/src/sample_load_logic.h): the header checks and their refusals, the stereo mix, the 16 s clip, the fades, the slicer
// and the same-length rule. The WAVs are built in memory (nothing binary in the repository).

#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/loop.h>

#include "../app/src/sample_load_logic.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace o3_voc03er;
namespace sl = o3_voc03er::sample_load;

namespace {

void put16(std::vector<std::uint8_t>& v, std::uint32_t x) {
  v.push_back(static_cast<std::uint8_t>(x & 0xFFu));
  v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFFu));
}
void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
  put16(v, x & 0xFFFFu);
  put16(v, x >> 16);
}
void tag(std::vector<std::uint8_t>& v, const char* t) { v.insert(v.end(), t, t + 4); }

// A WAV: `frames` frames of `ch` channels; sample i of channel c is `gen(i, c)`.
template <class G>
std::vector<std::uint8_t> make_wav(std::uint32_t frames, unsigned ch, std::uint32_t rate, unsigned bits, G gen, unsigned fmt_tag = 1) {
  std::vector<std::uint8_t> v;
  const std::uint32_t bytes = frames * ch * (bits / 8u);
  tag(v, "RIFF");
  put32(v, 36u + bytes);
  tag(v, "WAVE");
  tag(v, "fmt ");
  put32(v, 16);
  put16(v, fmt_tag);
  put16(v, ch);
  put32(v, rate);
  put32(v, rate * ch * (bits / 8u));
  put16(v, ch * (bits / 8u));
  put16(v, bits);
  tag(v, "data");
  put32(v, bytes);
  for (std::uint32_t i = 0; i < frames; ++i) {
    for (unsigned c = 0; c < ch; ++c) {
      if (bits == 16u) {
        put16(v, static_cast<std::uint32_t>(static_cast<std::uint16_t>(gen(i, c))));
      } else {
        v.push_back(static_cast<std::uint8_t>(gen(i, c)));
      }
    }
  }
  return v;
}

const auto ramp = [](std::uint32_t i, unsigned c) { return static_cast<std::int16_t>(1000 + static_cast<int>(i % 5000u) + static_cast<int>(c) * 10); };

// What the screen does with a file: the stage keeps at most kStageBytes, parse sees that, the slicer fills the slot.
sl::Info stage_and_parse(const std::vector<std::uint8_t>& file, std::vector<std::uint8_t>& stage) {
  sl::StageSink sink;
  stage.assign(sl::kStageBytes, 0);
  sink.bind(stage.data(), stage.size());
  std::size_t at = 0;
  while (at < file.size()) {  // the card job's slices
    const std::size_t n = file.size() - at < 2048u ? file.size() - at : 2048u;
    const auto r = sink.write(file.data() + at, n);
    REQUIRE(r.status == o3sdk::core::IoStatus::Ok);
    at += n;
  }
  return sl::parse(sink.data(), sink.held(), file.size());
}

}  // namespace

TEST_CASE("sample_load: a mono 44.1 kHz 16-bit file is accepted") {
  const auto wav = make_wav(1000, 1, 44100, 16, ramp);
  const auto info = sl::parse(wav.data(), wav.size(), wav.size());
  CHECK(info.verdict == sl::Verdict::Ok);
  CHECK(info.frames == 1000u);
  CHECK(info.file_frames == 1000u);
  CHECK(info.channels == 1);
  CHECK(info.data_offset == 44u);
  CHECK_FALSE(info.clipped);
  std::vector<std::int16_t> dst(1000, 0x7777);
  sl::mix_frames(wav.data() + info.data_offset, info.frames, info.channels, dst.data());
  for (std::uint32_t i = 0; i < 1000u; ++i) {
    CHECK(dst[i] == ramp(i, 0));
  }
}

TEST_CASE("sample_load: a stereo file is the mean of its two channels") {
  const auto wav = make_wav(300, 2, 44100, 16, [](std::uint32_t i, unsigned c) { return static_cast<std::int16_t>(c == 0 ? -20000 + static_cast<int>(i) : 30001 - static_cast<int>(i)); });
  const auto info = sl::parse(wav.data(), wav.size(), wav.size());
  REQUIRE(info.verdict == sl::Verdict::Ok);
  CHECK(info.channels == 2);
  CHECK(info.frames == 300u);
  std::vector<std::int16_t> dst(300, 0);
  sl::mix_frames(wav.data() + info.data_offset, info.frames, info.channels, dst.data());
  for (std::uint32_t i = 0; i < 300u; ++i) {
    const int a = -20000 + static_cast<int>(i), b = 30001 - static_cast<int>(i);
    CHECK(dst[i] == (a + b) / 2);
  }
  // the extremes of two channels never overflow the mean
  const auto loud = make_wav(2, 2, 44100, 16, [](std::uint32_t, unsigned) { return static_cast<std::int16_t>(-32768); });
  std::int16_t two[2] = {0, 0};
  sl::mix_frames(loud.data() + 44, 2, 2, two);
  CHECK(two[0] == -32768);
}

TEST_CASE("sample_load: another rate, bit depth, format or channel count is refused with its own text") {
  const auto r48 = make_wav(100, 1, 48000, 16, ramp);
  CHECK(sl::parse(r48.data(), r48.size(), r48.size()).verdict == sl::Verdict::Rate);
  const auto b8 = make_wav(100, 1, 44100, 8, ramp);
  CHECK(sl::parse(b8.data(), b8.size(), b8.size()).verdict == sl::Verdict::Bits);
  const auto fl = make_wav(100, 1, 44100, 16, ramp, 3);
  CHECK(sl::parse(fl.data(), fl.size(), fl.size()).verdict == sl::Verdict::NotPcm);
  const auto c3 = make_wav(100, 3, 44100, 16, ramp);
  CHECK(sl::parse(c3.data(), c3.size(), c3.size()).verdict == sl::Verdict::Channels);
  CHECK(std::strcmp(sl::refusal_text(sl::Verdict::Rate), "NEEDS 44.1 KHZ") == 0);
  CHECK(std::strcmp(sl::refusal_text(sl::Verdict::Bits), "NEEDS 16 BIT") == 0);
  CHECK(std::strcmp(sl::refusal_text(sl::Verdict::Ok), "") == 0);
  for (auto v : {sl::Verdict::NotWav, sl::Verdict::Broken, sl::Verdict::NotPcm, sl::Verdict::Channels, sl::Verdict::Empty}) {
    CHECK(sl::refusal_text(v)[0] != '\0');
  }
}

TEST_CASE("sample_load: a truncated or damaged file is refused and never read past what is there") {
  const auto wav = make_wav(1000, 1, 44100, 16, ramp);
  // the header cut inside the fmt chunk, inside the data header, and the data cut short
  for (std::size_t cut : {std::size_t{0}, std::size_t{5}, std::size_t{11}, std::size_t{20}, std::size_t{35}, std::size_t{43}}) {
    std::vector<std::uint8_t> head(wav.begin(), wav.begin() + static_cast<std::ptrdiff_t>(cut));
    CHECK(sl::parse(head.data(), head.size(), head.size()).verdict == sl::Verdict::Broken);
  }
  std::vector<std::uint8_t> shortdata(wav.begin(), wav.begin() + 44 + 500);  // the header says 2000 bytes, the file has 500
  CHECK(sl::parse(shortdata.data(), shortdata.size(), shortdata.size()).verdict == sl::Verdict::Broken);
  // a file that is not a WAV
  std::vector<std::uint8_t> junk(200, 0x41);
  CHECK(sl::parse(junk.data(), junk.size(), junk.size()).verdict == sl::Verdict::NotWav);
  CHECK(sl::parse(nullptr, 0, 0).verdict == sl::Verdict::Broken);
  // no data chunk at all, and a data chunk before the fmt chunk
  std::vector<std::uint8_t> nodata(wav.begin(), wav.begin() + 36);
  CHECK(sl::parse(nodata.data(), nodata.size(), nodata.size()).verdict == sl::Verdict::Broken);
  std::vector<std::uint8_t> early;
  tag(early, "RIFF");
  put32(early, 4 + 8 + 4);
  tag(early, "WAVE");
  tag(early, "data");
  put32(early, 4);
  put32(early, 0);
  CHECK(sl::parse(early.data(), early.size(), early.size()).verdict == sl::Verdict::Broken);
  // an empty data chunk
  const auto empty = make_wav(0, 1, 44100, 16, ramp);
  CHECK(sl::parse(empty.data(), empty.size(), empty.size()).verdict == sl::Verdict::Empty);
  // a chunk size that points far past the end ends as Broken, not as a wild read
  auto wild = wav;
  wild[40] = 0xFE; wild[41] = 0xFF; wild[42] = 0xFF; wild[43] = 0x7F;
  CHECK(sl::parse(wild.data(), wild.size(), wild.size()).verdict == sl::Verdict::Broken);
}

TEST_CASE("sample_load: other chunks before the data are skipped, odd sizes padded") {
  auto wav = make_wav(50, 1, 44100, 16, ramp);
  std::vector<std::uint8_t> v(wav.begin(), wav.begin() + 36);
  tag(v, "LIST");
  put32(v, 5);
  v.insert(v.end(), {1, 2, 3, 4, 5, 0});  // odd size, one pad byte
  v.insert(v.end(), wav.begin() + 36, wav.end());
  const auto info = sl::parse(v.data(), v.size(), v.size());
  REQUIRE(info.verdict == sl::Verdict::Ok);
  CHECK(info.data_offset == 44u + 8u + 6u);
  CHECK(info.frames == 50u);
}

TEST_CASE("sample_load: a 20 s file gives the first 16 s and says it was clipped") {
  const std::uint32_t frames = 20u * 44100u;
  const auto wav = make_wav(frames, 1, 44100, 16, ramp);
  std::vector<std::uint8_t> stage;
  const auto info = stage_and_parse(wav, stage);
  REQUIRE(info.verdict == sl::Verdict::Ok);
  CHECK(info.frames == sl::kMaxFrames);
  CHECK(info.file_frames == frames);
  CHECK(info.clipped);
  std::vector<std::int16_t> slot(sl::kMaxFrames, 0);
  sl::Write w;
  sl::write_begin(w, info.frames, info.channels);
  unsigned passes = 0;
  while (!sl::write_step(w, stage.data() + info.data_offset, slot.data())) {
    ++passes;
    REQUIRE(passes < 100u);  // bounded: 16 s in 8192-frame slices
  }
  CHECK(passes + 1u == (sl::kMaxFrames + sl::kSlice - 1u) / sl::kSlice);
  CHECK(slot[sl::kMaxFrames / 2u] == ramp(sl::kMaxFrames / 2u, 0));
  CHECK(slot[sl::kMaxFrames - 1u] == 0);  // the last sample is the end of the fade
}

TEST_CASE("sample_load: a 20 s stereo file is clipped to 16 s of frames too") {
  const std::uint32_t frames = 20u * 44100u;
  const auto wav = make_wav(frames, 2, 44100, 16, ramp);
  std::vector<std::uint8_t> stage;
  const auto info = stage_and_parse(wav, stage);
  REQUIRE(info.verdict == sl::Verdict::Ok);
  CHECK(info.frames == sl::kMaxFrames);
  CHECK(info.clipped);
  CHECK(info.channels == 2);
}

TEST_CASE("sample_load: the sink drops what does not fit and counts it") {
  std::vector<std::uint8_t> buf(10, 0);
  sl::StageSink sink;
  sink.bind(buf.data(), buf.size());
  const std::uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  CHECK(sink.write(a, 8).count == 8u);
  CHECK_FALSE(sink.overflowed());
  const auto r = sink.write(a, 8);
  CHECK(r.count == 8u);
  CHECK(r.status == o3sdk::core::IoStatus::Ok);
  CHECK(sink.overflowed());
  CHECK(sink.held() == 10u);
  CHECK(sink.seen() == 16u);
  CHECK(buf[9] == 2);
  sink.reset();
  CHECK(sink.held() == 0u);
  CHECK_FALSE(sink.overflowed());
}

TEST_CASE("sample_load: fades start and end at zero and leave the middle alone") {
  std::vector<std::int16_t> s(1000, 10000);
  sl::fade_ends(s.data(), 1000);
  CHECK(s[0] == 0);
  CHECK(s[999] == 0);
  CHECK(s[1] > 0);
  CHECK(s[sl::kFadeFrames - 1] < 10000);
  CHECK(s[sl::kFadeFrames] == 10000);
  CHECK(s[1000 - 1 - sl::kFadeFrames] == 10000);
  for (std::uint32_t k = 1; k < sl::kFadeFrames; ++k) {
    CHECK(s[k] >= s[k - 1]);
  }
  // a file shorter than two fades: each half fades, nothing runs past the ends
  std::vector<std::int16_t> t(40, -8000);
  sl::fade_ends(t.data(), 40);
  CHECK(t[0] == 0);
  CHECK(t[39] == 0);
  std::vector<std::int16_t> one(1, 5);
  sl::fade_ends(one.data(), 1);  // n/2 = 0: untouched
  CHECK(one[0] == 5);
}

TEST_CASE("sample_load: the slicer writes the whole file, in bounded passes, and reports its progress") {
  const std::uint32_t frames = 3u * sl::kSlice + 100u;
  const auto wav = make_wav(frames, 1, 44100, 16, ramp);
  const auto info = sl::parse(wav.data(), wav.size(), wav.size());
  REQUIRE(info.verdict == sl::Verdict::Ok);
  std::vector<std::int16_t> slot(frames + 8u, 0x5555);
  sl::Write w;
  sl::write_begin(w, info.frames, info.channels);
  CHECK(sl::write_percent(w) == 0u);
  unsigned passes = 0;
  unsigned last = 0;
  while (!sl::write_step(w, wav.data() + info.data_offset, slot.data())) {
    ++passes;
    CHECK(w.pos == passes * sl::kSlice);
    CHECK(sl::write_percent(w) >= last);
    last = sl::write_percent(w);
  }
  CHECK(passes == 3u);
  CHECK(sl::write_percent(w) == 100u);
  CHECK(slot[frames] == 0x5555);  // nothing past the length
  CHECK(slot[1000] == ramp(1000, 0));
  CHECK(slot[0] == 0);
  CHECK(slot[frames - 1] == 0);
  CHECK(sl::write_step(w, wav.data() + info.data_offset, slot.data()));  // done stays done
  // a zero-length write is done at once
  sl::write_begin(w, 0, 1);
  CHECK(sl::write_step(w, wav.data(), slot.data()));
}

TEST_CASE("sample_load: a same-length replacement is told apart from a first load") {
  CHECK(sl::same_length(44100, 44100));
  CHECK_FALSE(sl::same_length(44100, 44101));
  CHECK_FALSE(sl::same_length(0, 0));  // an empty slot: nothing to mark stale
  CHECK_FALSE(sl::same_length(0, 44100));
}
