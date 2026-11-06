// The pure logic of the overview waveform (U7b): per slot a column of min and max, built a slice of samples at a time, and the L1 bitmap drawn from it.
// No hardware, no allocation, no float: integer maths only (tests/overview_test.cpp).
#pragma once

#include <cstdint>

namespace o3_voc03er::overview {

inline constexpr unsigned kCols = 744;       // columns a slot's peaks hold, and the width of the big picture: SAMPLE EDIT's rectangle less its 2 px borders
inline constexpr unsigned kSmallCols = 240;  // the narrow picture (SAMPLE's line, SAMPLE REC): the columns merged
inline constexpr unsigned kBigRows = 96;
inline constexpr unsigned kRows = 48;  // the narrow picture's rows: the same peak scale over half the height
inline constexpr unsigned kBigStride = kCols / 8u;        // 93 bytes a row (the bitmap's line stride is any byte count)
inline constexpr unsigned kSmallStride = kSmallCols / 8u;  // 30
inline constexpr unsigned kBigBytes = kBigStride * kBigRows;   // 8928
inline constexpr unsigned kSmallBytes = kSmallStride * kRows;  // 1440
inline constexpr unsigned kSlotBytes = kBigBytes + kSmallBytes;  // one slot's two pictures, side by side in RAM_G and in the staging buffer
inline constexpr std::uint32_t kStepSamples = 4096;  // samples one step() reads
inline constexpr std::int32_t kFloorPeak = 328;      // about -40 dB of full scale: a quieter slot is drawn against this, so silence stays flat
static_assert(kCols % 8 == 0 && kSmallCols % 8 == 0 && kSlotBytes % 4 == 0 && kBigBytes % 4 == 0 && kSmallBytes % 4 == 0, "words for the upload and the bitmaps");

// The one map from a sample position to a column x of a `width`-wide strip: the picture (its columns are col_edge()'s), the marks, the cursor and the
// ruler all use it, so a cut sits at the same x on all of them (tests/sample_edit_test.cpp). A position at or past the end is the last column.
constexpr std::uint32_t x_of(std::uint32_t pos, std::uint32_t length, std::uint32_t width) noexcept {
  if (length == 0 || width == 0) return 0;
  const std::uint32_t x = static_cast<std::uint32_t>(static_cast<std::uint64_t>(pos) * width / length);
  return x < width ? x : width - 1u;
}

struct Peaks {
  std::int16_t lo[kCols];
  std::int16_t hi[kCols];
};

// Where a build stands: the next sample, the column it is in and the sample that column ends before.
struct Cursor {
  std::uint32_t pos = 0, col = 0, col_end = 0;
};

constexpr std::uint32_t col_edge(std::uint32_t length, std::uint32_t col) noexcept {
  return static_cast<std::uint32_t>(static_cast<std::uint64_t>(length) * col / kCols);
}

inline void begin(Peaks& p, Cursor& c, std::uint32_t length) noexcept {
  for (unsigned i = 0; i < kCols; ++i) {
    p.lo[i] = INT16_MAX;
    p.hi[i] = INT16_MIN;
  }
  c = Cursor{};
  c.col_end = col_edge(length, 1);
}

// Reads at most kStepSamples samples of data[0, length); true once every sample is in and the columns are final (columns that no sample fell in, for a
// slot shorter than kCols, take their left neighbour's value). Calling it again after true changes nothing.
inline bool step(const std::int16_t* data, std::uint32_t length, Peaks& p, Cursor& c) noexcept {
  if (c.pos >= length) return true;
  std::uint32_t n = length - c.pos;
  if (n > kStepSamples) n = kStepSamples;
  const std::uint32_t stop = c.pos + n;
  while (c.pos < stop) {
    while (c.pos >= c.col_end && c.col + 1 < kCols) {
      ++c.col;
      c.col_end = col_edge(length, c.col + 1);
    }
    const std::int16_t v = data[c.pos];
    if (v < p.lo[c.col]) p.lo[c.col] = v;
    if (v > p.hi[c.col]) p.hi[c.col] = v;
    ++c.pos;
  }
  if (c.pos < length) return false;
  std::int16_t last = 0;
  for (unsigned i = 0; i < kCols; ++i) {
    if (p.hi[i] < p.lo[i]) {
      p.lo[i] = last;
      p.hi[i] = last;
    }
    last = p.hi[i];
  }
  return true;
}

// The slot's own peak (its largest magnitude), with the floor under it: full height of the picture.
inline std::int32_t scale_peak(const Peaks& p) noexcept {
  std::int32_t m = kFloorPeak;
  for (unsigned i = 0; i < kCols; ++i) {
    const std::int32_t a = -static_cast<std::int32_t>(p.lo[i]);
    const std::int32_t b = p.hi[i];
    if (a > m) m = a;
    if (b > m) m = b;
  }
  return m;
}

// The row of a sample value: 0 at +peak, rows - 1 at -peak.
constexpr unsigned row_of(std::int32_t v, std::int32_t peak, unsigned rows = kRows) noexcept {
  if (v > peak) v = peak;
  if (v < -peak) v = -peak;
  return static_cast<unsigned>(((peak - v) * static_cast<std::int32_t>(rows - 1) + peak) / (2 * peak));
}

// Draws `cols` (kCols or kSmallCols) columns of `rows` rows (kBigRows or kRows) into `out` (cols / 8 bytes a row), MSB first like the kit's wave view:
// pixel x of a row is bit 0x80 >> (x & 7) of byte x / 8. A column lights the rows from its maximum's to its minimum's, one row when they are the same
// one. In the narrow picture a column merges the peaks columns [k * kCols / cols, (k + 1) * kCols / cols), at least one: each takes the min and max over
// the columns it covers.
inline void render(const Peaks& p, unsigned cols, unsigned rows, std::uint8_t* out) noexcept {
  const unsigned stride = cols / 8u;
  for (unsigned i = 0; i < stride * rows; ++i) out[i] = 0;
  const std::int32_t peak = scale_peak(p);
  for (unsigned x = 0; x < cols; ++x) {
    unsigned a = x * kCols / cols;
    unsigned b = (x + 1u) * kCols / cols;
    if (b <= a) b = a + 1u;
    std::int32_t lo = p.lo[a], hi = p.hi[a];
    for (unsigned k = a + 1u; k < b; ++k) {
      if (p.lo[k] < lo) lo = p.lo[k];
      if (p.hi[k] > hi) hi = p.hi[k];
    }
    const unsigned top = row_of(hi, peak, rows), bot = row_of(lo, peak, rows);
    for (unsigned y = top; y <= bot; ++y) out[y * stride + x / 8u] = static_cast<std::uint8_t>(out[y * stride + x / 8u] | (0x80u >> (x & 7u)));
  }
}

}  // namespace o3_voc03er::overview
