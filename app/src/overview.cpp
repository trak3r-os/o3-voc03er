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
static_assert(kSlotBytes <= o3sdk::hal::kMaxUploadBytes, "a slot's two pictures go up in one upload, so a failure never leaves half of one");
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
  render(peaks_, kCols, stage_);
  render(peaks_, kSmallCols, stage_ + kBigBytes);
  phase_ = Phase::Staged;
}

void Overview::pump(o3sdk::hal::Display& d) noexcept {
  if (phase_ != Phase::Staged) return;
  if (d.upload(slot_addr(active_), stage_, kSlotBytes) == o3sdk::hal::UploadResult::Ok) {
    shown_[active_] = true;
    phase_ = Phase::Idle;
    return;
  }
  if (++fails_ >= kRetries) phase_ = Phase::Idle;  // given up: the old picture stays, until the length changes
}

void Overview::draw(plot::Canvas& c, unsigned slot, bool big, int x, int y, o3sdk::ui::Rgb ink) const noexcept {
  if (!shown_[slot & 3u]) return;
  const unsigned w = big ? kCols : kSmallCols;
  const plot::BitmapDesc d{slot_addr(slot) + (big ? 0u : kBigBytes), o3sdk::eve::BitmapFormat::L1, static_cast<std::uint16_t>(w / 8u),
                           static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(kRows), static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(kRows)};
  c.bind(d);
  c.sprite(0, x, y, ink);
}

}  // namespace o3_voc03er::overview
