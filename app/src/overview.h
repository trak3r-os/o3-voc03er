// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The overview waveform of the four loop slots (slice U7b, docs/UI-DESIGN.md "Built in U7b"): an L1 bitmap per slot in the app's RAM_G, drawn as one
// sprite tinted in the side's ink. Everything here runs in the main loop (service() from SampleScreen::service, pump() from a screen's draw()); the render
// context never sees it. The old picture stays on screen while a new one is built, a slice of samples a pass (overview_logic.h).

#include "overview_logic.h"

#include <o3sdk/hal/display.h>
#include <o3sdk/ui/plot/canvas.h>

#include <cstdint>

namespace o3_voc03er::overview {

inline constexpr unsigned kSlots = 4;
inline constexpr unsigned kRetries = 3;  // upload attempts for one picture before it is given up until the slot's length changes

// RAM_G, from the app's region (ui::plot::kRamGApp): the skin's assets take [0, 10120), the pictures start at 10240 and a slot is kSlotBytes (the big
// picture, then the narrow one). Bitmap handle 0 (ui::plot::kHandleApp), the one the kit leaves to the app: Canvas::bind() binds it for every sprite.
inline constexpr std::uint32_t kRamGOffset = 10240;
inline constexpr std::uint32_t kRamGEnd = kRamGOffset + kSlots * kSlotBytes;

class Overview {
 public:
  // The slots' samples: slot n starts at n * stride in `mem`. Not owned.
  void bind(const std::int16_t* mem, std::uint32_t stride) noexcept {
    mem_ = mem;
    stride_ = stride;
  }

  // Main loop, every pass: `length[n]` is the slot's length in samples, 0 for an empty slot or one being recorded. A change starts a rebuild (one slot
  // at a time, in slot order); 0 clears the picture at once. Reads at most kStepSamples samples and renders the L1 pictures when the columns are done.
  void service(const std::uint32_t (&length)[kSlots]) noexcept;

  // Main loop, from a draw(): sends at most one staged picture to RAM_G. A failure keeps the old picture and tries again on the next call, kRetries in all.
  void pump(o3sdk::hal::Display& d) noexcept;

  bool shown(unsigned slot) const noexcept { return shown_[slot & 3u]; }
  bool busy() const noexcept { return phase_ != Phase::Idle || pending_ != 0u; }
  std::uint32_t slot_addr(unsigned slot) const noexcept;
  unsigned failures() const noexcept { return fails_; }

  // Draws the slot's picture with its top left at (x, y): the big one (384 x 48) or the narrow one (240 x 48), in `ink`. Nothing when the slot has no
  // picture. Inside an open Canvas section: a bind (6 words) and a sprite (1).
  void draw(o3sdk::ui::plot::Canvas& c, unsigned slot, bool big, int x, int y, o3sdk::ui::Rgb ink) const noexcept;

 private:
  enum class Phase : std::uint8_t { Idle, Building, Staged };

  void start_next() noexcept;

  const std::int16_t* mem_ = nullptr;
  std::uint32_t stride_ = 0;
  std::uint32_t want_[kSlots] = {};  // the length last seen
  bool shown_[kSlots] = {};
  std::uint8_t pending_ = 0;  // bit n: slot n waits for its turn
  Phase phase_ = Phase::Idle;
  std::uint8_t active_ = 0;  // the slot being built or staged
  std::uint8_t fails_ = 0;
  Cursor cur_;
  Peaks peaks_;
  alignas(4) std::uint8_t stage_[kSlotBytes];  // big picture, then narrow
};

// The app's one, in the SDRAM (it is about 7 KB): main loop only.
Overview& shared() noexcept;

}  // namespace o3_voc03er::overview
