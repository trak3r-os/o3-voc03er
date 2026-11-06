// SPDX-License-Identifier: GPL-3.0-or-later
#include "overview.h"

#include "skin/ornoir_assets.h"

#include <o3sdk/core/placement.h>
#include <o3sdk/ui/plot/ram_g_map.h>

namespace o3_voc03er::overview {

namespace {
namespace plot = o3sdk::ui::plot;
static_assert(kRamGOffset >= ornoir::kAtlasOffset + ornoir::kAtlasBytes && kRamGOffset % 4 == 0, "the pictures sit after the skin's assets");
static_assert(kRamGEnd <= plot::kRamGAppBytes, "the pictures fit the app's RAM_G region");
static_assert(kChunkBytes % o3sdk::hal::kUploadAlign == 0 && kChunkBytes % 4 == 0, "every chunk's address and size are words");
static_assert(kChunks * kChunkBytes >= kSlotBytes && kSlots * kSlotBytes <= plot::kRamGAppBytes - kRamGOffset, "the chunks cover the slot, four slots fit");
O3SDK_BIG Overview g_overview;
}  // namespace

Overview& shared() noexcept { return g_overview; }

std::uint32_t Overview::slot_addr(unsigned slot) const noexcept { return plot::kRamGApp + kRamGOffset + (slot & 3u) * kSlotBytes; }

void Overview::start_next() noexcept {
  for (unsigned s = 0; s < kSlots; ++s) {
    if ((pending_ >> s) & 1u) {
      pending_ = static_cast<std::uint8_t>(pending_ & ~(1u << s));
      active_ = static_cast<std::uint8_t>(s);
      fails_ = 0;
      phase_ = Phase::Building;
      begin(peaks_, cur_, want_[s]);
      return;
    }
  }
}

void Overview::service(const std::uint32_t (&length)[kSlots]) noexcept {
  for (unsigned s = 0; s < kSlots; ++s) {
    if (length[s] == want_[s]) continue;
    want_[s] = length[s];
    const bool mine = phase_ != Phase::Idle && active_ == s;
    if (length[s] == 0) {
      shown_[s] = false;
      pending_ = static_cast<std::uint8_t>(pending_ & ~(1u << s));
      if (mine) phase_ = Phase::Idle;
    } else if (mine) {
      phase_ = Phase::Idle;  // restart from the first sample, in the same turn
      pending_ = static_cast<std::uint8_t>(pending_ | (1u << s));
    } else {
      pending_ = static_cast<std::uint8_t>(pending_ | (1u << s));
    }
  }
  if (phase_ == Phase::Idle) start_next();
  if (phase_ != Phase::Building) return;
  const std::uint32_t len = want_[active_];
  if (mem_ == nullptr || !step(mem_ + static_cast<std::uint64_t>(active_) * stride_, len, peaks_, cur_)) return;
  std::int32_t pk = 0;
  for (unsigned i = 0; i < kCols; ++i) {
    const std::int32_t a = peaks_.hi[i], b = -static_cast<std::int32_t>(peaks_.lo[i]);
    pk = a > pk ? a : pk;
    pk = b > pk ? b : pk;
  }
  peak_[active_] = pk;
  render(peaks_, kCols, kBigRows, stage_);
  render(peaks_, kSmallCols, kRows, stage_ + kBigBytes);
  chunk_ = 0;
  fails_ = 0;
  phase_ = Phase::Staged;
}

void Overview::pump(o3sdk::hal::Display& d) noexcept {
  if (phase_ != Phase::Staged) return;
  const unsigned off = chunk_ * kChunkBytes;
  const unsigned n = (kSlotBytes - off < kChunkBytes) ? kSlotBytes - off : kChunkBytes;
  if (d.upload(slot_addr(active_) + off, stage_ + off, n) == o3sdk::hal::UploadResult::Ok) {
    fails_ = 0;
    if (chunk_ == 0) shown_[active_] = false;  // from the first chunk on the picture in RAM_G is half old, half new
    if (++chunk_ >= kChunks) {
      shown_[active_] = true;
      phase_ = Phase::Idle;
    }
    return;
  }
  if (++fails_ >= kRetries) {  // given up: not shown until the length changes or the slot is marked dirty
    shown_[active_] = false;
    phase_ = Phase::Idle;
  }
}

void Overview::draw(plot::Canvas& c, unsigned slot, bool big, int x, int y, o3sdk::ui::Rgb ink) const noexcept {
  if (!shown_[slot & 3u]) return;
  const unsigned w = big ? kCols : kSmallCols;
  const unsigned h = big ? kBigRows : kRows;
  const plot::BitmapDesc d{slot_addr(slot) + (big ? 0u : kBigBytes), o3sdk::eve::BitmapFormat::L1, static_cast<std::uint16_t>(w / 8u),
                           static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h), static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h)};
  c.bind(d);
  c.sprite(0, x, y, ink);
}

}  // namespace o3_voc03er::overview
