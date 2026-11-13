// SPDX-License-Identifier: GPL-3.0-or-later
// SAMPLE LOAD's pure logic (app/src/sample_load_logic.h): the header walk and its refusals, the conversion of 8, 16, 24 and 32-bit PCM and 32-bit float at 22.05 to 96 kHz
// to a slot's mono 16-bit 44.1 kHz, the 16 s cut, the bounded work per pass, the fades, the slicer and the same-length rule. The WAVs are built in memory (nothing binary in the
// repository). The converter's own quality (ripple, rejection, cost) is measured in tests/wav_convert_test.cpp.

#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/loop.h>

#include "../app/src/sample_load_logic.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

using namespace o3_voc03er;
namespace sl = o3_voc03er::sample_load;
using Bytes = std::vector<std::uint8_t>;

namespace {

void put16(Bytes& v, std::uint32_t x) {
  v.push_back(static_cast<std::uint8_t>(x & 0xFFu));
  v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFFu));
}
void put32(Bytes& v, std::uint32_t x) {
  put16(v, x & 0xFFFFu);
  put16(v, x >> 16);
}
void tag(Bytes& v, const char* t) { v.insert(v.end(), t, t + 4); }

// A chunk with its id and size, padded to an even length.
Bytes chunk(const char* id, const Bytes& body) {
  Bytes v;
  tag(v, id);
  put32(v, static_cast<std::uint32_t>(body.size()));
  v.insert(v.end(), body.begin(), body.end());
  if (body.size() & 1u) {
    v.push_back(0);
  }
  return v;
}

struct Spec {
  unsigned ch = 1;
  std::uint32_t rate = 44100;
  unsigned bits = 16;
  bool is_float = false;
  bool ext = false;          // WAVE_FORMAT_EXTENSIBLE (a 40-byte fmt chunk) instead of the plain 16 bytes
  unsigned tag_override = 0;  // a format tag to write instead (the sub-format's, for ext)
  unsigned align_override = 0;
};

// A WAV: `frames` frames; `gen(i, c)` is sample i of channel c in the file's own units (an int for PCM, 8-bit as 0..255, a float for float data). `before` goes between the
// fmt chunk and the data chunk, `after` behind the data.
template <class G>
Bytes make_wav(std::uint32_t frames, const Spec& s, G gen, const Bytes& before = {}, const Bytes& after = {}) {
  const unsigned bps = s.bits / 8u;
  Bytes fmt;
  const unsigned real_tag = s.tag_override != 0u ? s.tag_override : (s.is_float ? 3u : 1u);
  put16(fmt, s.ext ? 0xFFFEu : real_tag);
  put16(fmt, s.ch);
  put32(fmt, s.rate);
  put32(fmt, s.rate * s.ch * bps);
  put16(fmt, s.align_override != 0u ? s.align_override : s.ch * bps);
  put16(fmt, s.bits);
  if (s.ext) {
    put16(fmt, 22);
    put16(fmt, s.bits);
    put32(fmt, s.ch == 1u ? 4u : 3u);
    put16(fmt, real_tag);  // the GUID: the tag, then the fixed tail
    const std::uint8_t tail[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
    fmt.insert(fmt.end(), tail, tail + 14);
  }
  Bytes data;
  for (std::uint32_t i = 0; i < frames; ++i) {
    for (unsigned c = 0; c < s.ch; ++c) {
      const auto x = gen(i, c);
      if constexpr (std::is_floating_point_v<decltype(x)>) {
        const auto f = static_cast<float>(x);
        std::uint32_t u;
        std::memcpy(&u, &f, sizeof u);
        put32(data, u);
      } else {
        const auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(x));
        for (unsigned b = 0; b < bps; ++b) {
          data.push_back(b < 4u ? static_cast<std::uint8_t>((u >> (8u * b)) & 0xFFu) : std::uint8_t{0});  // 64-bit files (refused) pad with zeros
        }
      }
    }
  }
  Bytes body;
  tag(body, "WAVE");
  const Bytes f = chunk("fmt ", fmt);
  body.insert(body.end(), f.begin(), f.end());
  body.insert(body.end(), before.begin(), before.end());
  const Bytes d = chunk("data", data);
  body.insert(body.end(), d.begin(), d.end());
  body.insert(body.end(), after.begin(), after.end());
  Bytes v;
  tag(v, "RIFF");
  put32(v, static_cast<std::uint32_t>(body.size()));
  v.insert(v.end(), body.begin(), body.end());
  return v;
}

const auto ramp = [](std::uint32_t i, unsigned c) { return static_cast<std::int16_t>(1000 + static_cast<int>(i % 5000u) + static_cast<int>(c) * 10); };
const auto dc = [](std::int32_t v) { return [v](std::uint32_t, unsigned) { return v; }; };

// A fixed pseudo-random 16-bit sequence, the extremes included.
std::int16_t lcg16(std::uint32_t i, unsigned c) {
  std::uint32_t x = (i * 2u + c) * 2654435761u + 12345u;
  x ^= x >> 15;
  x *= 2246822519u;
  x ^= x >> 13;
  const auto r = static_cast<std::int16_t>(x & 0xFFFFu);
  return (i % 97u) == 3u ? static_cast<std::int16_t>(32767) : ((i % 89u) == 5u ? static_cast<std::int16_t>(-32768) : r);
}

struct Loaded {
  sl::Info info;
  std::vector<std::int16_t> out;  // the stage after the load (sentinel-filled where nothing was written)
  unsigned passes = 0;
  std::uint32_t work = 0;
  std::uint32_t worst_pass_work = 0;
  bool refused_early = false;  // the sink was settled before the whole file was offered
};

// What the card job does: the file offered in `slice` bytes, up to 16 KiB a pass, a pass over at a WouldBlock or when the sink is settled; then finish().
// `known_size`: the job knows the file's size (a file of the card), else 0 (an external load).
Loaded load(const Bytes& file, std::size_t slice = 2048, bool known_size = true) {
  Loaded r;
  r.out.assign(sl::kMaxFrames, 0x5A5A);
  sl::LoadSink sink;
  sink.bind(r.out.data(), known_size ? file.size() : 0u);
  std::size_t at = 0;
  while (at < file.size() && !sink.settled()) {
    REQUIRE(r.passes < 20000u);  // bounded: 16 KiB of file a pass, or less when the converter's budget ends the pass
    ++r.passes;
    sink.begin_pass();
    const std::uint32_t w0 = sink.work();
    std::size_t moved = 0;
    while (at < file.size() && moved < 16384u && !sink.settled()) {
      const std::size_t n = file.size() - at < slice ? file.size() - at : slice;
      const auto res = sink.write(file.data() + at, n);
      if (res.status == o3sdk::core::IoStatus::WouldBlock) {
        CHECK(res.count == 0u);
        break;
      }
      REQUIRE(res.status == o3sdk::core::IoStatus::Ok);
      REQUIRE(res.count > 0u);  // a short count is normal: the pass's budget ended inside the chunk, the rest is offered again
      REQUIRE(res.count <= n);
      at += res.count;
      moved += res.count;
    }
    const std::uint32_t spent = sink.work() - w0;
    r.worst_pass_work = spent > r.worst_pass_work ? spent : r.worst_pass_work;
  }
  r.refused_early = at < file.size();
  sink.finish();
  r.info = sink.info();
  r.work = sink.work();
  return r;
}

std::uint32_t ceil_div(std::uint64_t a, std::uint64_t b) { return static_cast<std::uint32_t>((a + b - 1u) / b); }

}  // namespace

// ---- 16-bit at 44.1 kHz: what it always was ---------------------------------------------------------------------------------------------

TEST_CASE("sample_load: a mono 44.1 kHz 16-bit file is taken as its own samples, in any slicing") {
  const Spec s;
  const auto wav = make_wav(1000, s, ramp);
  for (std::size_t slice : {std::size_t{1}, std::size_t{3}, std::size_t{7}, std::size_t{2048}, wav.size()}) {
    const auto r = load(wav, slice);
    REQUIRE(r.info.verdict == sl::Verdict::Ok);
    CHECK(r.info.frames == 1000u);
    CHECK(r.info.file_frames == 1000u);
    CHECK(r.info.channels == 1);
    CHECK(r.info.rate == 44100u);
    CHECK_FALSE(r.info.clipped);
    CHECK_FALSE(r.info.converted());
    CHECK(r.work == 0u);  // the integer path: no converter
    for (std::uint32_t i = 0; i < 1000u; ++i) {
      REQUIRE(r.out[i] == ramp(i, 0));
    }
    CHECK(r.out[1000] == 0x5A5A);  // nothing past the length
  }
}

// FNV-1a over the little-endian bytes of the samples.
std::uint32_t fnv16(const std::int16_t* d, std::uint32_t n) {
  std::uint32_t h = 2166136261u;
  for (std::uint32_t i = 0; i < n; ++i) {
    const auto u = static_cast<std::uint16_t>(d[i]);
    h = (h ^ (u & 255u)) * 16777619u;
    h = (h ^ (u >> 8)) * 16777619u;
  }
  return h;
}

TEST_CASE("sample_load: 16-bit 44.1 kHz is byte-identical to what the loader of f8dd91a made of the same file, mono and stereo, in any slicing") {
  // The constants were computed by building f8dd91a's app/src/sample_load_logic.h as committed (parse, mix_frames, then write_begin / write_step into a slot, the fades
  // included) on the same 5000-frame file of lcg16 noise with the extremes in it, in a scratch directory; the values below are its FNV-1a digests.
  struct Pin { unsigned ch; std::uint32_t mix, slot; };
  for (const Pin pin : {Pin{1, 0xAB92560Bu, 0xA0A431E3u}, Pin{2, 0x1E7F3E4Fu, 0xABE2D724u}}) {
    Spec sp;
    sp.ch = pin.ch;
    const auto wav = make_wav(5000, sp, lcg16);
    for (std::size_t slice : {std::size_t{1}, std::size_t{5}, std::size_t{2048}, wav.size()}) {
      const auto r = load(wav, slice);
      INFO("channels " << pin.ch << " slice " << slice);
      REQUIRE(r.info.verdict == sl::Verdict::Ok);
      CHECK(r.info.frames == 5000u);
      CHECK_FALSE(r.info.converted());
      CHECK(fnv16(r.out.data(), 5000u) == pin.mix);
      std::vector<std::int16_t> slot(5000, 0);
      sl::Write w;
      sl::write_begin(w, r.info.frames);
      unsigned guard = 0;
      while (!sl::write_step(w, r.out.data(), slot.data())) {
        REQUIRE(++guard < 100u);
      }
      CHECK(fnv16(slot.data(), 5000u) == pin.slot);
    }
  }
  const auto loud = make_wav(2, [] { Spec x; x.ch = 2; return x; }(), dc(-32768));
  CHECK(load(loud).out[0] == -32768);  // the extremes of two channels never overflow the mean
}

// ---- the matrix: 16 and 24 bit, 44.1 and 48 kHz, mono and stereo ------------------------------------------------------------------------

TEST_CASE("sample_load: 16 and 24 bit at 44.1 and 48 kHz, mono and stereo, all load; a constant stays that constant") {
  for (unsigned bits : {16u, 24u}) {
    for (std::uint32_t rate : {44100u, 48000u}) {
      for (unsigned ch : {1u, 2u}) {
        Spec s;
        s.bits = bits;
        s.rate = rate;
        s.ch = ch;
        // 5000 at 3 and 11 give a mean of 7000 / 2... keep the two channels apart: L = 4000, R = 8000 -> 6000 in 16-bit units
        const auto wav = make_wav(2400, s, [bits, ch](std::uint32_t, unsigned c) { return (ch == 2u ? (c == 0u ? 4000 : 8000) : 6000) * (bits == 24u ? 256 : 1); });
        const auto r = load(wav);
        INFO("bits " << bits << " rate " << rate << " ch " << ch);
        REQUIRE(r.info.verdict == sl::Verdict::Ok);
        CHECK(r.info.rate == rate);
        CHECK(r.info.bits == bits);
        CHECK(r.info.channels == ch);
        CHECK(r.info.file_frames == 2400u);
        CHECK(r.info.frames == ceil_div(2400ull * 44100u, rate));
        CHECK(r.info.converted() == (rate != 44100u || bits != 16u));
        CHECK_FALSE(r.info.clipped);
        for (std::uint32_t i = 0; i < r.info.frames; ++i) {
          REQUIRE(std::abs(r.out[i] - 6000) <= 1);  // the whole length, the two ends included: the converter does not droop at the edges
        }
        CHECK(r.out[r.info.frames] == 0x5A5A);
        CHECK((r.work > 0u) == (rate != 44100u));
      }
    }
  }
}

TEST_CASE("sample_load: the converted length is the file's length at 44.1 kHz, rounded up, for the rates of the range") {
  struct Case { std::uint32_t rate, frames; };
  for (const Case c : {Case{48000, 48000}, Case{48000, 1000}, Case{96000, 96000}, Case{88200, 4410}, Case{32000, 3200}, Case{22050, 2205}, Case{22050, 1}, Case{48000, 1}}) {
    Spec s;
    s.rate = c.rate;
    const auto r = load(make_wav(c.frames, s, dc(1234)));
    INFO("rate " << c.rate << " frames " << c.frames);
    REQUIRE(r.info.verdict == sl::Verdict::Ok);
    CHECK(r.info.frames == ceil_div(static_cast<std::uint64_t>(c.frames) * 44100u, c.rate));
    CHECK(r.info.total_frames() == r.info.frames);
  }
}

TEST_CASE("sample_load: 24-bit rounds to the nearest 16-bit value, a tie up, and clips nothing it need not") {
  Spec s;
  s.bits = 24;
  const std::int32_t in[] = {0, 127, 128, 129, 255, 256, 384, -1, -127, -128, -129, -255, -256, -257, 0x7FFFFF, 0x7FFF7F, 0x7FFF80, -0x800000, -0x7FFF80, -0x7FFF81};
  const std::int32_t want[] = {0, 0, 1, 1, 1, 1, 2, 0, 0, 0, -1, -1, -1, -1, 32767, 32767, 32767, -32768, -32767, -32768};
  const auto r = load(make_wav(sizeof in / sizeof in[0], s, [&](std::uint32_t i, unsigned) { return in[i]; }));
  REQUIRE(r.info.verdict == sl::Verdict::Ok);
  for (std::uint32_t i = 0; i < sizeof in / sizeof in[0]; ++i) {
    INFO("24-bit value " << in[i]);
    CHECK(r.out[i] == want[i]);
  }
  // every 16-bit value written as 24 bits (shifted left 8, with a low byte of 0) comes back exactly
  const auto all = make_wav(65536, s, [](std::uint32_t i, unsigned) { return (static_cast<std::int32_t>(i) - 32768) * 256; });
  const auto ra = load(all);
  REQUIRE(ra.info.verdict == sl::Verdict::Ok);
  for (std::uint32_t i = 0; i < 65536u; ++i) {
    REQUIRE(ra.out[i] == static_cast<int>(i) - 32768);
  }
  // a stereo pair is rounded once, from the sum
  Spec st = s;
  st.ch = 2;
  const auto rs = load(make_wav(3, st, [](std::uint32_t i, unsigned c) { return i == 0u ? (c == 0u ? 128 : 0) : (i == 1u ? (c == 0u ? 0x7FFFFF : 0x7FFFFF) : (c == 0u ? -128 : -129)); }));
  CHECK(rs.out[0] == 0);   // 128 / 2 = 64 of 256: below half
  CHECK(rs.out[1] == 32767);
  CHECK(rs.out[2] == -1);  // -257 / 2 = -128.5 of 256: below half a step
}

TEST_CASE("sample_load: 8 and 32-bit PCM and 32-bit float load by their own rules") {
  Spec s8;
  s8.bits = 8;
  const std::int32_t u8[] = {128, 0, 255, 129, 127};  // unsigned: 128 is silence
  const auto r8 = load(make_wav(5, s8, [&](std::uint32_t i, unsigned) { return u8[i]; }));
  REQUIRE(r8.info.verdict == sl::Verdict::Ok);
  CHECK(r8.out[0] == 0);
  CHECK(r8.out[1] == -32768);
  CHECK(r8.out[2] == 127 * 256);
  CHECK(r8.out[3] == 256);
  CHECK(r8.out[4] == -256);

  Spec s32;
  s32.bits = 32;
  const std::int32_t i32[] = {0, 0x7FFFFFFF, -0x7FFFFFFF - 1, 0x00008000, 0x00007FFF, -0x00008000, -0x00008001};
  const std::int32_t w32[] = {0, 32767, -32768, 1, 0, 0, -1};
  const auto r32 = load(make_wav(7, s32, [&](std::uint32_t i, unsigned) { return i32[i]; }));
  REQUIRE(r32.info.verdict == sl::Verdict::Ok);
  for (std::uint32_t i = 0; i < 7u; ++i) {
    INFO("32-bit value " << i32[i]);
    CHECK(r32.out[i] == w32[i]);
  }

  Spec sf;
  sf.bits = 32;
  sf.is_float = true;
  const float fl[] = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -3.0f, std::nanf(""), HUGE_VALF, -HUGE_VALF, 1.0f / 65536.0f, 0.75f / 65536.0f};
  const std::int32_t wf[] = {0, 16384, -16384, 32767, -32768, 32767, -32768, 0, 32767, -32768, 1, 0};
  const auto rf = load(make_wav(12, sf, [&](std::uint32_t i, unsigned) { return fl[i]; }));
  REQUIRE(rf.info.verdict == sl::Verdict::Ok);
  CHECK(rf.info.is_float);
  CHECK(rf.info.converted());
  for (std::uint32_t i = 0; i < 12u; ++i) {
    INFO("float " << fl[i]);
    CHECK(rf.out[i] == wf[i]);
  }
  // an infinity and its opposite in a pair is no signal; a float file at 48 kHz converts without a NaN reaching the output
  Spec sfs = sf;
  sfs.ch = 2;
  sfs.rate = 48000;
  const auto rs = load(make_wav(300, sfs, [](std::uint32_t i, unsigned c) { return i == 100u ? (c == 0u ? HUGE_VALF : -HUGE_VALF) : (c == 0u ? 0.25f : 0.5f); }));
  REQUIRE(rs.info.verdict == sl::Verdict::Ok);
  CHECK(rs.out[0] == 12288);
  CHECK(rs.out[rs.info.frames - 1u] == 12288);
}

// ---- headers ----------------------------------------------------------------------------------------------------------------------------

TEST_CASE("sample_load: an extensible header, an 18-byte fmt chunk and the plain one load the same samples") {
  for (unsigned bits : {16u, 24u}) {
    Spec plain;
    plain.bits = bits;
    plain.rate = 48000;
    plain.ch = 2;
    Spec ext = plain;
    ext.ext = true;
    const auto g = [bits](std::uint32_t i, unsigned c) { return (static_cast<int>(i % 700u) * 37 - 9000 + static_cast<int>(c) * 333) * (bits == 24u ? 256 : 1); };
    const auto a = load(make_wav(1500, plain, g));
    const auto b = load(make_wav(1500, ext, g));
    REQUIRE(a.info.verdict == sl::Verdict::Ok);
    REQUIRE(b.info.verdict == sl::Verdict::Ok);
    CHECK(b.info.frames == a.info.frames);
    CHECK(b.out == a.out);
  }
  Spec ef;
  ef.bits = 32;
  ef.is_float = true;
  ef.ext = true;
  const auto rf = load(make_wav(10, ef, [](std::uint32_t, unsigned) { return 0.5f; }));
  REQUIRE(rf.info.verdict == sl::Verdict::Ok);
  CHECK(rf.info.is_float);
  CHECK(rf.out[0] == 16384);
  // an extensible header whose sub-format is not PCM or float is refused, whatever its first field says
  Spec eadpcm;
  eadpcm.ext = true;
  eadpcm.tag_override = 2;
  CHECK(load(make_wav(10, eadpcm, dc(0))).info.verdict == sl::Verdict::NotPcm);
  // a plain fmt chunk with the 2-byte cbSize (18 bytes)
  Bytes f18;
  put16(f18, 1); put16(f18, 1); put32(f18, 44100); put32(f18, 88200); put16(f18, 2); put16(f18, 16); put16(f18, 0);
  Bytes data;
  for (unsigned i = 0; i < 20u; ++i) {
    put16(data, i * 100u);
  }
  Bytes body;
  tag(body, "WAVE");
  for (const auto& c : {chunk("fmt ", f18), chunk("data", data)}) {
    body.insert(body.end(), c.begin(), c.end());
  }
  Bytes v;
  tag(v, "RIFF");
  put32(v, static_cast<std::uint32_t>(body.size()));
  v.insert(v.end(), body.begin(), body.end());
  const auto r18 = load(v);
  REQUIRE(r18.info.verdict == sl::Verdict::Ok);
  CHECK(r18.info.frames == 20u);
  CHECK(r18.out[7] == 700);
}

TEST_CASE("sample_load: other chunks before and after the data are skipped, odd sizes padded") {
  const Spec s;
  Bytes before = chunk("LIST", Bytes{1, 2, 3, 4, 5});       // odd size, one pad byte
  const Bytes more = chunk("junk", Bytes{});                    // empty
  const Bytes more2 = chunk("bext", Bytes(257, 0xAB));          // odd, long enough to span a slice
  before.insert(before.end(), more.begin(), more.end());
  before.insert(before.end(), more2.begin(), more2.end());
  const Bytes after = chunk("LIST", Bytes{9, 9, 9});
  const auto wav = make_wav(50, s, ramp, before, after);
  for (std::size_t slice : {std::size_t{1}, std::size_t{13}, std::size_t{2048}}) {
    const auto r = load(wav, slice);
    REQUIRE(r.info.verdict == sl::Verdict::Ok);
    CHECK(r.info.frames == 50u);
    CHECK(r.out[49] == ramp(49, 0));
    CHECK(r.out[0] == ramp(0, 0));
  }
  // the same in a 48 kHz 24-bit file
  Spec s24;
  s24.bits = 24;
  s24.rate = 48000;
  const auto r24 = load(make_wav(480, s24, dc(256 * 777), before, after), 5);
  REQUIRE(r24.info.verdict == sl::Verdict::Ok);
  CHECK(r24.info.frames == 441u);
  CHECK(std::abs(r24.out[200] - 777) <= 1);
  // a file with a trailing chunk and an unknown size (an external load) is the same
  const auto rx = load(wav, 2048, false);
  REQUIRE(rx.info.verdict == sl::Verdict::Ok);
  CHECK(rx.info.frames == 50u);
}

TEST_CASE("sample_load: a truncated or damaged file is refused and never read past what is there") {
  const Spec s;
  const auto wav = make_wav(1000, s, ramp);
  // the header cut at every point up to and inside the fmt chunk and the data header: known size and unknown size
  for (std::size_t cut : {std::size_t{0}, std::size_t{5}, std::size_t{11}, std::size_t{12}, std::size_t{20}, std::size_t{35}, std::size_t{43}}) {
    const Bytes head(wav.begin(), wav.begin() + static_cast<std::ptrdiff_t>(cut));
    INFO("cut at " << cut);
    CHECK(load(head).info.verdict == sl::Verdict::Broken);
    CHECK(load(head, 2048, false).info.verdict == sl::Verdict::Broken);
  }
  // the data cut short: the header says 2000 bytes, the file has 500
  const Bytes shortdata(wav.begin(), wav.begin() + 44 + 500);
  const auto early = load(shortdata);
  CHECK(early.info.verdict == sl::Verdict::Broken);
  CHECK(early.refused_early == false);  // the whole 544 bytes were offered in one slice, but the verdict came from the header, not from the end
  CHECK(load(shortdata, 2048, false).info.verdict == sl::Verdict::Broken);  // size unknown: found when the stream ends inside the data
  // a data chunk cut inside its last frame of a 24-bit file, too
  Spec s24;
  s24.bits = 24;
  const auto w24 = make_wav(100, s24, ramp);
  const Bytes cut24(w24.begin(), w24.end() - 2);
  CHECK(load(cut24).info.verdict == sl::Verdict::Broken);
  CHECK(load(cut24, 2048, false).info.verdict == sl::Verdict::Broken);
  // the cut is found before the whole file is read when the size is known: the sink is settled at the data header
  const Bytes big = make_wav(40000, s, ramp);
  Bytes bigcut(big.begin(), big.begin() + 44 + 30000);
  const auto ebig = load(bigcut, 2048);
  CHECK(ebig.info.verdict == sl::Verdict::Broken);
  CHECK(ebig.refused_early);
  CHECK(ebig.passes == 1u);
  // a file that is not a WAV
  const Bytes junk(200, 0x41);
  CHECK(load(junk).info.verdict == sl::Verdict::NotWav);
  Bytes riff_only = wav;
  riff_only[8] = 'X';  // RIFF....XAVE
  CHECK(load(riff_only).info.verdict == sl::Verdict::NotWav);
  Bytes rifx = wav;
  rifx[3] = 'X';
  CHECK(load(rifx).info.verdict == sl::Verdict::NotWav);
  CHECK(load(Bytes{}).info.verdict == sl::Verdict::Broken);
  // no data chunk at all, a data chunk before the fmt chunk, a fmt chunk too short
  CHECK(load(Bytes(wav.begin(), wav.begin() + 36)).info.verdict == sl::Verdict::Broken);
  Bytes early_data;
  tag(early_data, "RIFF");
  put32(early_data, 4 + 8 + 4);
  tag(early_data, "WAVE");
  tag(early_data, "data");
  put32(early_data, 4);
  put32(early_data, 0);
  CHECK(load(early_data).info.verdict == sl::Verdict::Broken);
  auto shortfmt = wav;
  shortfmt[16] = 14;
  CHECK(load(shortfmt).info.verdict == sl::Verdict::Broken);
  // an empty data chunk
  CHECK(load(make_wav(0, s, ramp)).info.verdict == sl::Verdict::Empty);
  // one byte of a 4-byte frame is no frame either
  CHECK(load(make_wav(0, s24, ramp, {}, {})).info.verdict == sl::Verdict::Empty);
  // a chunk size that points far past the end ends as Broken, not as a wild read
  auto wild = wav;
  wild[40] = 0xFE; wild[41] = 0xFF; wild[42] = 0xFF; wild[43] = 0x7F;
  CHECK(load(wild).info.verdict == sl::Verdict::Broken);
  Bytes wild_skip = chunk("LIST", Bytes(10, 1));
  wild_skip[4] = 0xFF; wild_skip[5] = 0xFF; wild_skip[6] = 0xFF; wild_skip[7] = 0x7F;  // a skipped chunk of 2 GiB, then the file ends
  CHECK(load(make_wav(10, s, ramp, wild_skip)).info.verdict == sl::Verdict::Broken);
  // a file of nothing but tiny chunks
  Bytes many;
  for (unsigned i = 0; i < 70u; ++i) {
    const Bytes c = chunk("pad ", Bytes{});
    many.insert(many.end(), c.begin(), c.end());
  }
  CHECK(load(make_wav(10, s, ramp, many)).info.verdict == sl::Verdict::Broken);
}

TEST_CASE("sample_load: a data size of 0xFFFFFFFF means the rest of the file") {
  const Spec s;
  auto wav = make_wav(300, s, ramp);
  wav[40] = 0xFF; wav[41] = 0xFF; wav[42] = 0xFF; wav[43] = 0xFF;
  const auto known = load(wav);
  REQUIRE(known.info.verdict == sl::Verdict::Ok);
  CHECK(known.info.frames == 300u);
  CHECK(known.info.file_frames == 300u);
  const auto unknown = load(wav, 2048, false);
  REQUIRE(unknown.info.verdict == sl::Verdict::Ok);
  CHECK(unknown.info.frames == 300u);
  CHECK(unknown.info.file_frames == 0u);  // not known
  CHECK_FALSE(unknown.info.clipped);
}

// ---- refusals ---------------------------------------------------------------------------------------------------------------------------

TEST_CASE("sample_load: what cannot be loaded is refused with its own text, and refused at the header") {
  auto verdict = [](const Spec& s) { return load(make_wav(100, s, dc(0))).info.verdict; };
  Spec adpcm;
  adpcm.tag_override = 2;
  CHECK(verdict(adpcm) == sl::Verdict::NotPcm);
  Spec mp3;
  mp3.tag_override = 0x55;
  CHECK(verdict(mp3) == sl::Verdict::NotPcm);
  Spec alaw;
  alaw.tag_override = 6;
  alaw.bits = 8;
  CHECK(verdict(alaw) == sl::Verdict::NotPcm);
  Spec b20;
  b20.bits = 24;
  b20.align_override = 3;
  Bytes odd_bits;  // 20-bit PCM in 3-byte containers
  {
    Spec s = b20;
    auto w = make_wav(100, s, dc(0));
    w[34] = 20;  // wBitsPerSample
    CHECK(load(w).info.verdict == sl::Verdict::Bits);
  }
  Spec f64;
  f64.bits = 64;
  f64.is_float = true;
  CHECK(verdict(f64) == sl::Verdict::Bits);
  Spec f16;  // float of 16 bits is no format
  f16.is_float = true;
  CHECK(verdict(f16) == sl::Verdict::Bits);
  for (std::uint32_t rate : {8000u, 11025u, 22049u, 96001u, 176400u, 192000u}) {
    Spec s;
    s.rate = rate;
    INFO("rate " << rate);
    CHECK(verdict(s) == sl::Verdict::Rate);
  }
  for (std::uint32_t rate : {22050u, 32000u, 44100u, 48000u, 88200u, 96000u}) {
    Spec s;
    s.rate = rate;
    INFO("rate " << rate);
    CHECK(verdict(s) == sl::Verdict::Ok);
  }
  for (unsigned ch : {3u, 4u, 6u, 8u}) {
    Spec s;
    s.ch = ch;
    INFO("channels " << ch);
    CHECK(verdict(s) == sl::Verdict::Channels);
  }
  {
    auto w = make_wav(100, Spec{}, dc(0));
    w[22] = 0;  // no channels
    CHECK(load(w).info.verdict == sl::Verdict::Channels);
  }
  Spec badalign;
  badalign.align_override = 3;
  CHECK(verdict(badalign) == sl::Verdict::Broken);
  // the order of the verdicts when a file is wrong in two ways: format, bits, rate, channels, align
  Spec both;
  both.tag_override = 2;
  both.rate = 8000;
  CHECK(verdict(both) == sl::Verdict::NotPcm);
  Spec rate_ch;
  rate_ch.rate = 8000;
  rate_ch.ch = 3;
  CHECK(verdict(rate_ch) == sl::Verdict::Rate);

  // refused at the header: a long file of a refused kind is not read to its end, and the Info says what the header was
  Spec far;
  far.rate = 192000;
  far.ch = 2;
  far.bits = 24;
  const auto r = load(make_wav(20000, far, dc(0)));
  CHECK(r.info.verdict == sl::Verdict::Rate);
  CHECK(r.refused_early);
  CHECK(r.info.rate == 192000u);
  CHECK(r.info.channels == 2);
  CHECK(r.info.bits == 24);
  CHECK(r.info.frames == 0u);

  // the texts: one per refusal, upper case, short enough for the strip
  CHECK(std::strcmp(sl::refusal_text(sl::Verdict::Ok), "") == 0);
  CHECK(std::strcmp(sl::refusal_text(sl::Verdict::NotWav), "NOT A WAV FILE") == 0);
  CHECK(std::strcmp(sl::refusal_text(sl::Verdict::NotPcm), "NEEDS PCM OR FLOAT") == 0);
  CHECK(std::strcmp(sl::refusal_text(sl::Verdict::Bits), "NEEDS 8 TO 32 BIT") == 0);
  CHECK(std::strcmp(sl::refusal_text(sl::Verdict::Rate), "NEEDS 22 TO 96 KHZ") == 0);
  CHECK(std::strcmp(sl::refusal_text(sl::Verdict::Channels), "NEEDS MONO OR STEREO") == 0);
  CHECK(std::strcmp(sl::refusal_text(sl::Verdict::Broken), "BROKEN WAV FILE") == 0);
  CHECK(std::strcmp(sl::refusal_text(sl::Verdict::Empty), "EMPTY WAV FILE") == 0);
  for (auto v : {sl::Verdict::NotWav, sl::Verdict::Broken, sl::Verdict::NotPcm, sl::Verdict::Bits, sl::Verdict::Rate, sl::Verdict::Channels, sl::Verdict::Empty}) {
    CHECK(std::strlen(sl::refusal_text(v)) <= 24u);
  }
  CHECK(sl::Info{}.verdict == sl::Verdict::Broken);  // a zeroed Info (the SDRAM's start state) is a refusal
}

TEST_CASE("sample_load: the text that says what was converted") {
  auto text = [](std::uint32_t rate, unsigned bits, bool is_float, unsigned ch) {
    sl::Info i;
    i.verdict = sl::Verdict::Ok;
    i.rate = rate;
    i.bits = static_cast<std::uint8_t>(bits);
    i.is_float = is_float;
    i.channels = static_cast<std::uint8_t>(ch);
    char buf[48];
    const std::size_t n = sl::conversion_text(i, buf, sizeof buf);
    CHECK(n == std::strlen(buf));
    return std::string(buf);
  };
  CHECK(text(48000, 24, false, 2) == "48K 24B TO 44.1K 16B");
  CHECK(text(48000, 16, false, 1) == "48K TO 44.1K");
  CHECK(text(44100, 24, false, 1) == "24B TO 16B");
  CHECK(text(44100, 32, true, 1) == "FLOAT TO 16B");
  CHECK(text(96000, 32, true, 2) == "96K FLOAT TO 44.1K 16B");
  CHECK(text(22050, 8, false, 1) == "22.05K 8B TO 44.1K 16B");
  CHECK(text(88200, 32, false, 2) == "88.2K 32B TO 44.1K 16B");
  CHECK(text(44100, 16, false, 2).empty());  // a stereo mix alone says nothing
  CHECK(text(44100, 16, false, 1).empty());
  // a small buffer is cut, never overrun, and stays terminated
  sl::Info i;
  i.rate = 48000;
  i.bits = 24;
  char tiny[8];
  std::memset(tiny, 'x', sizeof tiny);
  const std::size_t n = sl::conversion_text(i, tiny, sizeof tiny);
  CHECK(n == 7u);
  CHECK(tiny[7] == '\0');
  CHECK(sl::conversion_text(i, tiny, 0) == 0u);
}

// ---- the 16 s cut -----------------------------------------------------------------------------------------------------------------------

TEST_CASE("sample_load: a 20 s file at 44.1 kHz gives the first 16 s and says it was cut; the slicer fills the slot in bounded passes") {
  const std::uint32_t frames = 20u * 44100u;
  const Spec s;
  const auto r = load(make_wav(frames, s, ramp));
  REQUIRE(r.info.verdict == sl::Verdict::Ok);
  CHECK(r.info.frames == sl::kMaxFrames);
  CHECK(r.info.file_frames == frames);
  CHECK(r.info.clipped);
  CHECK(r.refused_early);  // the sink was settled with the stage full: the rest of the file is never read
  CHECK(r.out[sl::kMaxFrames - 1u] == ramp(sl::kMaxFrames - 1u, 0));
  std::vector<std::int16_t> slot(sl::kMaxFrames, 0);
  sl::Write w;
  sl::write_begin(w, r.info.frames);
  unsigned passes = 0;
  while (!sl::write_step(w, r.out.data(), slot.data())) {
    ++passes;
    REQUIRE(passes < 100u);  // bounded: 16 s in 8192-frame slices
  }
  CHECK(passes + 1u == (sl::kMaxFrames + sl::kSlice - 1u) / sl::kSlice);
  CHECK(slot[sl::kMaxFrames / 2u] == ramp(sl::kMaxFrames / 2u, 0));
  CHECK(slot[sl::kMaxFrames - 1u] == 0);  // the last sample is the end of the fade
}

TEST_CASE("sample_load: the 16 s are counted after the conversion, and a file of exactly 16 s is not cut") {
  Spec s;
  s.rate = 48000;
  // 16.0 s at 48 kHz converts to exactly 705 600 frames: the slot's length, not a cut
  const std::uint32_t exact = 16u * 48000u;
  const auto a = load(make_wav(exact, s, dc(2000)));
  REQUIRE(a.info.verdict == sl::Verdict::Ok);
  CHECK(a.info.frames == sl::kMaxFrames);
  CHECK_FALSE(a.info.clipped);
  CHECK(a.info.total_frames() == sl::kMaxFrames);
  CHECK(std::abs(a.out[sl::kMaxFrames - 1u] - 2000) <= 1);
  // 16.5 s: cut, and the cut is the first 16 s of the converted signal; the file's length at 44.1 kHz is known
  const std::uint32_t over = 16u * 48000u + 24000u;
  const auto b = load(make_wav(over, s, dc(2000)));
  REQUIRE(b.info.verdict == sl::Verdict::Ok);
  CHECK(b.info.frames == sl::kMaxFrames);
  CHECK(b.info.clipped);
  CHECK(b.info.file_frames == over);
  CHECK(b.info.total_frames() == ceil_div(static_cast<std::uint64_t>(over) * 44100u, 48000u));
  CHECK(b.refused_early);
  CHECK(std::abs(b.out[sl::kMaxFrames - 1u] - 2000) <= 1);
  // a 17 s stereo 24-bit file at 44.1 kHz (the integer path) is cut too
  Spec st;
  st.ch = 2;
  st.bits = 24;
  const auto c = load(make_wav(17u * 44100u, st, ramp));
  REQUIRE(c.info.verdict == sl::Verdict::Ok);
  CHECK(c.info.frames == sl::kMaxFrames);
  CHECK(c.info.clipped);
}

TEST_CASE("sample_load: the converter's work in one pass is bounded, and a load in many passes equals a load in one") {
  // the worst chunk: 8-bit mono at 22.05 kHz doubles up to 44.1, 2048 samples a chunk
  Spec s;
  s.bits = 8;
  s.rate = 22050;
  const auto wav = make_wav(30000, s, [](std::uint32_t i, unsigned) { return 128 + static_cast<int>((i * 37u) % 200u) - 100; });
  const auto many = load(wav, 2048);
  REQUIRE(many.info.verdict == sl::Verdict::Ok);
  CHECK(many.info.frames == 60000u);
  CHECK(many.worst_pass_work <= sl::kPassTaps + 140u);  // the budget and one frame's overshoot, not a chunk's
  CHECK(many.passes > 5u);                                   // the budget did end passes
  // the whole file offered in one piece: the sink takes it in budget-sized parts (short counts), and the result is the same
  sl::LoadSink one;
  std::vector<std::int16_t> out(sl::kMaxFrames, 0);
  one.bind(out.data(), wav.size());
  std::size_t oat = 0;
  unsigned shorts = 0;
  for (unsigned guard = 0; oat < wav.size(); ++guard) {
    REQUIRE(guard < 1000u);
    one.begin_pass();
    const std::uint32_t before = one.work();
    const auto r1 = one.write(wav.data() + oat, wav.size() - oat);
    REQUIRE(r1.status == o3sdk::core::IoStatus::Ok);
    REQUIRE(r1.count > 0u);
    CHECK(one.work() - before <= sl::kPassTaps + 140u);  // a pass stays near the budget even when a call is offered the whole file
    shorts += r1.count < wav.size() - oat ? 1u : 0u;
    oat += r1.count;
  }
  CHECK(shorts > 5u);
  one.finish();
  REQUIRE(one.info().verdict == sl::Verdict::Ok);
  CHECK(one.info().frames == many.info.frames);
  for (std::uint32_t i = 0; i < many.info.frames; ++i) {
    REQUIRE(out[i] == many.out[i]);
  }
  // a refused write consumes nothing: the same bytes offered again give the same result
  sl::LoadSink w;
  std::vector<std::int16_t> out2(sl::kMaxFrames, 0);
  w.bind(out2.data(), wav.size());
  w.begin_pass();
  std::size_t at = 0;
  unsigned blocked = 0;
  while (at < wav.size()) {
    const std::size_t n = wav.size() - at < 700u ? wav.size() - at : 700u;
    const auto res = w.write(wav.data() + at, n);
    if (res.status == o3sdk::core::IoStatus::WouldBlock) {
      CHECK(res.count == 0u);
      ++blocked;
      w.begin_pass();
      continue;
    }
    at += res.count;
  }
  w.finish();
  CHECK(blocked > 0u);
  CHECK(w.info().frames == many.info.frames);
  for (std::uint32_t i = 0; i < many.info.frames; ++i) {
    REQUIRE(out2[i] == many.out[i]);
  }
}

TEST_CASE("sample_load: a conversion is the same in whatever slices the file arrives, split frames included") {
  Spec s;
  s.bits = 24;
  s.rate = 48000;
  s.ch = 2;
  const auto wav = make_wav(6000, s, [](std::uint32_t i, unsigned c) { return (static_cast<int>(lcg16(i, c)) * 200) + static_cast<int>(i % 7u); });
  const auto ref = load(wav, wav.size());
  REQUIRE(ref.info.verdict == sl::Verdict::Ok);
  CHECK(ref.info.frames == ceil_div(6000ull * 44100u, 48000u));
  for (std::size_t slice : {std::size_t{1}, std::size_t{5}, std::size_t{6}, std::size_t{7}, std::size_t{13}, std::size_t{1000}, std::size_t{2048}}) {
    const auto r = load(wav, slice);
    INFO("slice " << slice);
    REQUIRE(r.info.verdict == sl::Verdict::Ok);
    REQUIRE(r.info.frames == ref.info.frames);
    CHECK(r.out == ref.out);
    CHECK(r.work == ref.work);
  }
  // and with an unknown size
  const auto rx = load(wav, 2048, false);
  REQUIRE(rx.info.verdict == sl::Verdict::Ok);
  CHECK(rx.out == ref.out);
}

// ---- the slot's stage -------------------------------------------------------------------------------------------------------------------

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
  const Spec s;
  const auto r = load(make_wav(frames, s, ramp));
  REQUIRE(r.info.verdict == sl::Verdict::Ok);
  std::vector<std::int16_t> slot(frames + 8u, 0x5555);
  sl::Write w;
  sl::write_begin(w, r.info.frames);
  CHECK(sl::write_percent(w) == 0u);
  unsigned passes = 0;
  unsigned last = 0;
  while (!sl::write_step(w, r.out.data(), slot.data())) {
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
  CHECK(sl::write_step(w, r.out.data(), slot.data()));  // done stays done
  // a zero-length write is done at once
  sl::write_begin(w, 0);
  CHECK(sl::write_step(w, r.out.data(), slot.data()));
}

TEST_CASE("sample_load: a same-length replacement is told apart from a first load") {
  CHECK(sl::same_length(44100, 44100));
  CHECK_FALSE(sl::same_length(44100, 44101));
  CHECK_FALSE(sl::same_length(0, 0));  // an empty slot: nothing to mark stale
  CHECK_FALSE(sl::same_length(0, 44100));
}

TEST_CASE("sample load: UX3 the refusal names what to stop, and the cap says so") {
  CHECK(sl::busy_of(false, false, false) == sl::Busy::None);
  CHECK(sl::busy_of(true, true, true) == sl::Busy::Recording);  // the first thing to stop wins
  CHECK(sl::busy_of(false, true, true) == sl::Busy::Loop);
  CHECK(sl::busy_of(false, false, true) == sl::Busy::Slice);
  for (const auto b : {sl::Busy::Recording, sl::Busy::Loop, sl::Busy::Slice}) {
    CHECK(std::strlen(sl::busy_text(b)) > 0u);
    CHECK(std::strlen(sl::busy_text(b)) <= 44u);
  }
  CHECK(std::string(sl::busy_text(sl::Busy::None)).empty());
  CHECK(std::string(sl::capped_text(false)).empty());
  CHECK(std::string(sl::capped_text(true)).find("64") != std::string::npos);
}

TEST_CASE("sample load: UX3 the note after a load fits the line in the worst case, the NO SIDE hint on a line of its own") {
  sl::Info i;
  i.verdict = sl::Verdict::Ok;
  i.frames = sl::kMaxFrames;
  i.clipped = true;
  i.rate = 22050;  // the worst conversion text: 22.05K 32B TO 44.1K 16B
  i.bits = 32;
  i.is_float = true;
  i.file_frames = 0xFFFFFFFFu / 2u;  // a very long file at a low rate: the biggest "FILE HAS" figure
  char b[112];
  const std::size_t n = sl::loaded_note(i, b, sizeof b);
  CHECK(n == std::strlen(b));
  CHECK(n <= sl::kNoteCols);
  CHECK(std::strlen(sl::kLoadedNoSideTail) <= sl::kNoteCols);
  // and with the tail glued on, as one line, it would not have fitted: that is why it has its own
  CHECK(n + 2u + std::strlen(sl::kLoadedNoSideTail) > sl::kNoteCols);
  // the other shapes are shorter
  i.file_frames = 0;  // size not known
  CHECK(sl::loaded_note(i, b, sizeof b) <= sl::kNoteCols);
  i.clipped = false;
  CHECK(sl::loaded_note(i, b, sizeof b) <= sl::kNoteCols);
  CHECK(std::strncmp(b, "LOADED ", 7) == 0);
  // a small buffer is cut, never overrun
  char tiny[8];
  CHECK(sl::loaded_note(i, tiny, sizeof tiny) < sizeof tiny);
  CHECK(sl::loaded_note(i, tiny, 0) == 0u);
}
