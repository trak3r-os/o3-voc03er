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
  const std::uint32_t len = 384u * 100u;
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
  const std::uint32_t len = 384u * 400u;  // 4 cycles of 38,400 samples
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
  ov::render(p, ov::kCols, img);
  unsigned rows_lit = 0;
  for (unsigned y = 0; y < ov::kRows; ++y) {
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
}

TEST_CASE("overview: one pixel when min equals max, the whole span otherwise") {
  static ov::Peaks p;
  for (unsigned c = 0; c < ov::kCols; ++c) p.lo[c] = p.hi[c] = 0;
  p.lo[10] = -32000;
  p.hi[10] = 32000;
  p.lo[20] = p.hi[20] = 32000;
  static std::uint8_t img[ov::kBigBytes];
  ov::render(p, ov::kCols, img);
  unsigned n10 = 0, n20 = 0, n30 = 0;
  for (unsigned y = 0; y < ov::kRows; ++y) {
    n10 += lit(img, ov::kBigStride, 10, y) ? 1u : 0u;
    n20 += lit(img, ov::kBigStride, 20, y) ? 1u : 0u;
    n30 += lit(img, ov::kBigStride, 30, y) ? 1u : 0u;
  }
  CHECK(n10 == ov::kRows);
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
  const unsigned xs[] = {0, 1, 7, 8, 100, 383};
  for (unsigned x : xs) {
    p.lo[x] = -32767;
    p.hi[x] = 32767;
    ref[x] = plot::PeakPair{-32767, 32767};
  }
  static std::uint8_t mine[ov::kBigBytes], kit[ov::kBigBytes];
  ov::render(p, ov::kCols, mine);
  plot::WaveBitmap::render_reference(ref, ov::kCols, ov::kRows, plot::WaveDepth::L1, kit);
  for (unsigned y = 0; y < ov::kRows; ++y) {
    for (unsigned x : xs) CHECK(lit(mine, ov::kBigStride, x, y) == lit(kit, ov::kBigStride, x, y));
  }
  CHECK(mine[0] == 0xC1);  // columns 0, 1 and 7 of the first row: 0x80 | 0x40 | 0x01
  CHECK(plot::WaveBitmap::stride_of(ov::kCols, plot::WaveDepth::L1) == ov::kBigStride);
}

TEST_CASE("overview: the narrow picture merges the columns") {
  static ov::Peaks p;
  for (unsigned c = 0; c < ov::kCols; ++c) p.lo[c] = p.hi[c] = 0;
  p.hi[1] = 30000;  // inside narrow column 0 (peaks columns 0 and 1)
  static std::uint8_t img[ov::kSmallBytes];
  ov::render(p, ov::kSmallCols, img);
  CHECK(lit(img, ov::kSmallStride, 1, 0));  // narrow column 1 merges peaks columns 1 and 2
  CHECK_FALSE(lit(img, ov::kSmallStride, 5, 0));
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
  CHECK(d.calls == 1u);
  CHECK(d.last_addr == o.slot_addr(0));
  CHECK(o.shown(0));
  CHECK_FALSE(o.busy());
  len[0] = 0;  // cleared at once, no display needed
  o.service(len);
  CHECK_FALSE(o.shown(0));
  CHECK_FALSE(o.busy());
  len[0] = 5000;  // slots go in order, one at a time
  len[2] = 5000;
  for (int i = 0; i < 6; ++i) {
    o.service(len);
    o.pump(d);
  }
  CHECK(o.shown(0));
  CHECK(o.shown(2));
  CHECK(d.last_addr == o.slot_addr(2));
}

TEST_CASE("overview: a failed upload retries three times, keeps the old picture and gives up") {
  static ov::Overview o;
  static std::int16_t mem[20000];
  for (auto& v : mem) v = 1000;
  o.bind(mem, 20000);
  FakeDisplay d;
  std::uint32_t len[4] = {3000, 0, 0, 0};
  o.service(len);
  o.pump(d);
  REQUIRE(o.shown(0));
  d.calls = 0;
  d.answer = hal::UploadResult::Busy;
  len[0] = 3500;  // a rebuild whose pictures never get up
  for (int i = 0; i < 12; ++i) {
    o.service(len);
    o.pump(d);
  }
  CHECK(d.calls == ov::kRetries);
  CHECK(o.shown(0));  // the old picture stays marked shown
  CHECK_FALSE(o.busy());
  d.answer = hal::UploadResult::Ok;  // nothing more is tried until the length changes
  for (int i = 0; i < 4; ++i) {
    o.service(len);
    o.pump(d);
  }
  CHECK(d.calls == ov::kRetries);
  len[0] = 3600;
  for (int i = 0; i < 4; ++i) {
    o.service(len);
    o.pump(d);
  }
  CHECK(d.calls == ov::kRetries + 1u);
}

TEST_CASE("overview: the pictures sit after the skin's assets inside the app's region") {
  CHECK(ov::kRamGOffset >= 10120u);
  CHECK(ov::kRamGEnd <= plot::kRamGAppBytes);
  CHECK(ov::kSlotBytes <= hal::kMaxUploadBytes);
}
