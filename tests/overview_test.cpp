// SPDX-License-Identifier: GPL-3.0-or-later
// The overview waveform (U7b): the columns of min and max built a slice at a time, the scale and its floor, the L1 packing, and the class that keeps the
// four pictures (a length change restarts, 0 clears, a failed upload retries three times and keeps the old picture). Synthetic signals; no display.
#include <doctest/doctest.h>

#include "../app/src/overview.h"
#include "../app/src/overview_logic.h"

#include <o3sdk/hal/display.h>
#include <o3sdk/ui/plot/wave_bitmap.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ov = o3_voc03er::overview;
namespace hal = o3sdk::hal;
namespace plot = o3sdk::ui::plot;

namespace {

// Runs step() until done; returns the number of calls.
unsigned build(const std::int16_t* d, std::uint32_t len, ov::Peaks& p) {
  ov::Cursor c;
  ov::begin(p, c, len);
  unsigned calls = 1;
  while (!ov::step(d, len, p, c)) {
    ++calls;
    REQUIRE(calls < 1000u);
  }
  return calls;
}

bool lit(const std::uint8_t* img, unsigned stride, unsigned x, unsigned y) { return (img[y * stride + x / 8u] & (0x80u >> (x & 7u))) != 0u; }

// A display that stores nothing and answers as told.
class FakeDisplay final : public hal::Display {
 public:
  hal::UploadResult answer = hal::UploadResult::Ok;
  unsigned calls = 0;
  std::uint32_t last_addr = 0;
  bool begin_frame() noexcept override { return true; }
  bool write(const std::uint32_t*, std::size_t) noexcept override { return true; }
  void end_frame() noexcept override {}
  void abort_frame() noexcept override {}
  hal::UploadResult upload(std::uint32_t addr, const std::uint8_t*, std::size_t) noexcept override {
    ++calls;
    last_addr = addr;
    return answer;
  }
};

}  // namespace

TEST_CASE("overview: the columns of a ramp") {
  const std::uint32_t len = ov::kCols * 100u;
  std::vector<std::int16_t> d(len);
  for (std::uint32_t i = 0; i < len; ++i) d[i] = static_cast<std::int16_t>(-30000 + static_cast<std::int64_t>(i) * 60000 / static_cast<std::int64_t>(len - 1));
  static ov::Peaks p;
  build(d.data(), len, p);
  for (unsigned c = 0; c < ov::kCols; ++c) {
    CHECK(p.lo[c] == d[c * 100u]);
    CHECK(p.hi[c] == d[c * 100u + 99u]);
    CHECK(p.lo[c] <= p.hi[c]);
  }
}

TEST_CASE("overview: the columns of a sine hold its peak and its trough") {
  const std::uint32_t len = ov::kCols * 400u;  // 4 cycles of 38,400 samples
  std::vector<std::int16_t> d(len);
  for (std::uint32_t i = 0; i < len; ++i) d[i] = static_cast<std::int16_t>(std::lround(20000.0 * std::sin(6.283185307179586 * 4.0 * i / len)));
  static ov::Peaks p;
  build(d.data(), len, p);
  std::int16_t mx = INT16_MIN, mn = INT16_MAX;
  for (unsigned c = 0; c < ov::kCols; ++c) {
    mx = p.hi[c] > mx ? p.hi[c] : mx;
    mn = p.lo[c] < mn ? p.lo[c] : mn;
  }
  CHECK(mx >= 19990);
  CHECK(mn <= -19990);
  CHECK(ov::scale_peak(p) == std::max(-static_cast<int>(mn), static_cast<int>(mx)));
}

TEST_CASE("overview: step() eats at most 4,096 samples a call") {
  std::vector<std::int16_t> d(10000, 1000);
  static ov::Peaks p;
  ov::Cursor c;
  ov::begin(p, c, 10000);
  CHECK_FALSE(ov::step(d.data(), 10000, p, c));
  CHECK(c.pos == ov::kStepSamples);
  CHECK_FALSE(ov::step(d.data(), 10000, p, c));
  CHECK(c.pos == 2u * ov::kStepSamples);
  CHECK(ov::step(d.data(), 10000, p, c));
  CHECK(c.pos == 10000u);
  CHECK(ov::step(d.data(), 10000, p, c));  // done stays done
  CHECK(build(d.data(), 10000, p) == 3u);
  std::vector<std::int16_t> big(705600, 5);  // a full slot: about 170 passes
  CHECK(build(big.data(), 705600, p) == (705600u + ov::kStepSamples - 1u) / ov::kStepSamples);
}

TEST_CASE("overview: a slot shorter than the columns fills them from the left") {
  std::int16_t d[10];
  for (int i = 0; i < 10; ++i) d[i] = static_cast<std::int16_t>(i * 100);
  static ov::Peaks p;
  build(d, 10, p);
  for (unsigned c = 0; c < ov::kCols; ++c) CHECK(p.lo[c] <= p.hi[c]);
  CHECK(p.hi[ov::kCols - 1] == 900);
}

TEST_CASE("overview: the -40 dB floor keeps silence a line") {
  std::vector<std::int16_t> quiet(4000, 0);
  static ov::Peaks p;
  build(quiet.data(), 4000, p);
  CHECK(ov::scale_peak(p) == ov::kFloorPeak);
  static std::uint8_t img[ov::kBigBytes];
  ov::render(p, ov::kCols, ov::kBigRows, img);
  unsigned rows_lit = 0;
  for (unsigned y = 0; y < ov::kBigRows; ++y) {
    unsigned n = 0;
    for (unsigned x = 0; x < ov::kCols; ++x) n += lit(img, ov::kBigStride, x, y) ? 1u : 0u;
    if (n != 0) {
      ++rows_lit;
      CHECK(n == ov::kCols);  // one pixel a column, all on the same row
    }
  }
  CHECK(rows_lit == 1u);
  for (auto& v : quiet) v = 100;  // below the floor: still against the floor, so it stays near the middle
  build(quiet.data(), 4000, p);
  CHECK(ov::scale_peak(p) == ov::kFloorPeak);
  std::vector<std::int16_t> loud(4000, 0);
  loud[7] = 20000;
  build(loud.data(), 4000, p);
  CHECK(ov::scale_peak(p) == 20000);
  CHECK(ov::row_of(20000, 20000) == 0u);
  CHECK(ov::row_of(-20000, 20000) == ov::kRows - 1u);
  CHECK(ov::row_of(-20000, 20000, ov::kBigRows) == ov::kBigRows - 1u);
}

TEST_CASE("overview: one pixel when min equals max, the whole span otherwise") {
  static ov::Peaks p;
  for (unsigned c = 0; c < ov::kCols; ++c) p.lo[c] = p.hi[c] = 0;
  p.lo[10] = -32000;
  p.hi[10] = 32000;
  p.lo[20] = p.hi[20] = 32000;
  static std::uint8_t img[ov::kBigBytes];
  ov::render(p, ov::kCols, ov::kBigRows, img);
  unsigned n10 = 0, n20 = 0, n30 = 0;
  for (unsigned y = 0; y < ov::kBigRows; ++y) {
    n10 += lit(img, ov::kBigStride, 10, y) ? 1u : 0u;
    n20 += lit(img, ov::kBigStride, 20, y) ? 1u : 0u;
    n30 += lit(img, ov::kBigStride, 30, y) ? 1u : 0u;
  }
  CHECK(n10 == ov::kBigRows);
  CHECK(n20 == 1u);
  CHECK(lit(img, ov::kBigStride, 20, 0));
  CHECK(n30 == 1u);
}

TEST_CASE("overview: the L1 packing is MSB first, as the kit's wave view") {
  static ov::Peaks p;
  static plot::PeakPair ref[ov::kCols];
  for (unsigned c = 0; c < ov::kCols; ++c) {
    p.lo[c] = p.hi[c] = 0;
    ref[c] = plot::PeakPair{0, 0};
  }
  const unsigned xs[] = {0, 1, 7, 8, 100, 383, ov::kCols - 1u};
  for (unsigned x : xs) {
    p.lo[x] = -32767;
    p.hi[x] = 32767;
    ref[x] = plot::PeakPair{-32767, 32767};
  }
  static std::uint8_t mine[ov::kBigBytes], kit[ov::kBigBytes];
  ov::render(p, ov::kCols, ov::kBigRows, mine);
  plot::WaveBitmap::render_reference(ref, ov::kCols, ov::kBigRows, plot::WaveDepth::L1, kit);
  for (unsigned y = 0; y < ov::kBigRows; ++y) {
    for (unsigned x : xs) CHECK(lit(mine, ov::kBigStride, x, y) == lit(kit, ov::kBigStride, x, y));
  }
  CHECK(mine[0] == 0xC1);  // columns 0, 1 and 7 of the first row: 0x80 | 0x40 | 0x01
  CHECK(plot::WaveBitmap::stride_of(ov::kCols, plot::WaveDepth::L1) == ov::kBigStride);
}

TEST_CASE("overview: the narrow picture merges the columns in proportion") {
  static ov::Peaks p;
  for (unsigned c = 0; c < ov::kCols; ++c) p.lo[c] = p.hi[c] = 0;
  p.hi[1] = 30000;  // 744 / 240 = 3.1 columns each: narrow column 0 covers peaks columns 0..2, column 1 covers 3..5
  p.lo[3] = -30000;
  static std::uint8_t img[ov::kSmallBytes];
  ov::render(p, ov::kSmallCols, ov::kRows, img);
  CHECK(lit(img, ov::kSmallStride, 0, 0));                       // the +30000 at peaks column 1
  CHECK(lit(img, ov::kSmallStride, 1, ov::kRows - 1u));          // the -30000 at peaks column 3
  CHECK_FALSE(lit(img, ov::kSmallStride, 1, 0));
  CHECK_FALSE(lit(img, ov::kSmallStride, 5, 0));
  CHECK_FALSE(lit(img, ov::kSmallStride, 0, ov::kRows - 1u));
  // Every peaks column lands in exactly one narrow column: the merge loses nothing.
  unsigned covered = 0;
  for (unsigned x = 0; x < ov::kSmallCols; ++x) {
    unsigned a = x * ov::kCols / ov::kSmallCols, b = (x + 1u) * ov::kCols / ov::kSmallCols;
    covered += b - a;
  }
  CHECK(covered == ov::kCols);
}

TEST_CASE("overview: the big picture is 744 x 96 and a slot fits four times in RAM_G") {
  CHECK(ov::kCols == 744u);
  CHECK(ov::kBigRows == 96u);
  CHECK(ov::kBigStride == 93u);
  CHECK(ov::kBigBytes == 93u * 96u);
  CHECK(ov::kSmallCols == 240u);
  CHECK(ov::kRows == 48u);
  CHECK(ov::kSlotBytes == ov::kBigBytes + ov::kSmallBytes);
  CHECK(ov::kChunks == 3u);
  CHECK(ov::kSlotBytes % 4u == 0u);
  CHECK(ov::kBigBytes % 4u == 0u);
}

TEST_CASE("overview: x_of maps a position to a column, the last sample on the last column") {
  CHECK(ov::x_of(0, 1000, 744) == 0u);
  CHECK(ov::x_of(999, 1000, 744) == 743u);
  CHECK(ov::x_of(1000, 1000, 744) == 743u);   // past the end clamps
  CHECK(ov::x_of(500, 1000, 744) == 372u);
  CHECK(ov::x_of(5, 0, 744) == 0u);
  CHECK(ov::x_of(5, 100, 0) == 0u);
}

TEST_CASE("overview: a rebuild, a length change mid-rebuild, a 0 length") {
  static ov::Overview o;
  static std::int16_t mem[3u * 20000u];
  for (std::size_t i = 0; i < sizeof mem / sizeof mem[0]; ++i) mem[i] = static_cast<std::int16_t>((i % 100) * 100);
  o.bind(mem, 20000);
  FakeDisplay d;
  std::uint32_t len[4] = {10000, 0, 0, 0};
  CHECK_FALSE(o.shown(0));
  o.service(len);
  CHECK(o.busy());
  o.pump(d);
  CHECK(d.calls == 0u);  // 4,096 of 10,000 read: nothing staged yet
  len[0] = 5000;         // a new length: starts again
  o.service(len);        // 4,096 of 5,000
  o.pump(d);
  CHECK(d.calls == 0u);
  o.service(len);  // done and staged
  o.pump(d);
  CHECK(d.calls == 1u);  // the first of three chunks
  CHECK(d.last_addr == o.slot_addr(0));
  CHECK_FALSE(o.shown(0));  // shown only after the last chunk
  o.pump(d);
  CHECK_FALSE(o.shown(0));
  o.pump(d);
  CHECK(d.calls == 3u);
  CHECK(d.last_addr == o.slot_addr(0) + 2u * ov::kChunkBytes);
  CHECK(o.shown(0));
  CHECK_FALSE(o.busy());
  CHECK(o.peak(0) > 0);
  len[0] = 0;  // cleared at once, no display needed
  o.service(len);
  CHECK_FALSE(o.shown(0));
  CHECK_FALSE(o.busy());
  len[0] = 5000;  // slots go in order, one at a time
  len[2] = 5000;
  for (int i = 0; i < 20; ++i) {
    o.service(len);
    o.pump(d);
  }
  CHECK(o.shown(0));
  CHECK(o.shown(2));
  CHECK(d.last_addr == o.slot_addr(2) + 2u * ov::kChunkBytes);
}

TEST_CASE("overview: a slot goes up in chunks of at most 4,096 bytes that cover it") {
  static ov::Overview o;
  static std::int16_t mem[20000];
  for (auto& v : mem) v = 1000;
  o.bind(mem, 20000);
  struct Rec final : hal::Display {
    std::uint32_t addr[8] = {};
    std::size_t size[8] = {};
    unsigned n = 0;
    bool begin_frame() noexcept override { return true; }
    bool write(const std::uint32_t*, std::size_t) noexcept override { return true; }
    void end_frame() noexcept override {}
    void abort_frame() noexcept override {}
    hal::UploadResult upload(std::uint32_t a, const std::uint8_t*, std::size_t c) noexcept override {
      if (n < 8) {
        addr[n] = a;
        size[n] = c;
      }
      ++n;
      return hal::UploadResult::Ok;
    }
  } d;
  std::uint32_t len[4] = {3000, 0, 0, 0};
  for (int i = 0; i < 8; ++i) {
    o.service(len);
    o.pump(d);
  }
  REQUIRE(d.n == ov::kChunks);
  std::size_t total = 0;
  for (unsigned i = 0; i < d.n; ++i) {
    CHECK(d.size[i] <= hal::kMaxUploadBytes);
    CHECK(d.addr[i] % hal::kUploadAlign == 0u);
    CHECK(d.size[i] % 4u == 0u);
    CHECK(d.addr[i] == o.slot_addr(0) + total);
    total += d.size[i];
  }
  CHECK(total == ov::kSlotBytes);
  CHECK(o.shown(0));
}

TEST_CASE("overview: a failed chunk retries that chunk, and giving up leaves the slot not shown") {
  static ov::Overview o;
  static std::int16_t mem[20000];
  for (auto& v : mem) v = 1000;
  o.bind(mem, 20000);
  FakeDisplay d;
  std::uint32_t len[4] = {3000, 0, 0, 0};
  for (int i = 0; i < 6; ++i) {
    o.service(len);
    o.pump(d);
  }
  REQUIRE(o.shown(0));
  d.calls = 0;
  d.answer = hal::UploadResult::Busy;
  len[0] = 3500;  // a rebuild whose pictures never get up
  for (int i = 0; i < 12; ++i) {
    o.service(len);
    o.pump(d);
  }
  CHECK(d.calls == ov::kRetries);
  CHECK_FALSE(o.shown(0));  // given up: the half-written picture is not shown
  CHECK_FALSE(o.busy());
  d.answer = hal::UploadResult::Ok;  // nothing more is tried until the length changes or the slot is marked dirty
  for (int i = 0; i < 6; ++i) {
    o.service(len);
    o.pump(d);
  }
  CHECK(d.calls == ov::kRetries);
  CHECK_FALSE(o.shown(0));
  len[0] = 3600;
  for (int i = 0; i < 8; ++i) {
    o.service(len);
    o.pump(d);
  }
  CHECK(d.calls == ov::kRetries + ov::kChunks);
  CHECK(o.shown(0));
}

TEST_CASE("overview: a failing second chunk is retried alone, then the slot shows") {
  static ov::Overview o;
  static std::int16_t mem[20000];
  for (auto& v : mem) v = 1000;
  o.bind(mem, 20000);
  FakeDisplay d;
  std::uint32_t len[4] = {3000, 0, 0, 0};
  o.service(len);
  o.service(len);  // built and staged
  o.pump(d);       // chunk 0 up
  d.answer = hal::UploadResult::Busy;
  o.pump(d);       // chunk 1 fails once
  CHECK(d.last_addr == o.slot_addr(0) + ov::kChunkBytes);
  CHECK_FALSE(o.shown(0));
  d.answer = hal::UploadResult::Ok;
  o.pump(d);  // the same chunk again
  CHECK(d.last_addr == o.slot_addr(0) + ov::kChunkBytes);
  o.pump(d);  // the last
  CHECK(o.shown(0));
  CHECK(d.calls == 4u);
}

TEST_CASE("overview: mark_dirty rebuilds a slot whose length did not change") {
  static ov::Overview o;
  static std::int16_t mem[20000];
  for (auto& v : mem) v = 1000;
  o.bind(mem, 20000);
  FakeDisplay d;
  std::uint32_t len[4] = {3000, 0, 0, 0};
  for (int i = 0; i < 6; ++i) {
    o.service(len);
    o.pump(d);
  }
  REQUIRE(o.shown(0));
  CHECK(o.peak(0) == 1000);
  for (auto& v : mem) v = 2000;
  o.mark_dirty(0);
  for (int i = 0; i < 8; ++i) {
    o.service(len);
    o.pump(d);
  }
  CHECK(o.shown(0));
  CHECK(o.peak(0) == 2000);
}

TEST_CASE("overview: the pictures sit after the skin's assets inside the app's region") {
  CHECK(ov::kRamGOffset >= 10120u);
  CHECK(ov::kRamGEnd <= plot::kRamGAppBytes);
  CHECK(ov::kRamGEnd == 10240u + 4u * 10368u);
  CHECK(ov::kChunkBytes <= hal::kMaxUploadBytes);
}
