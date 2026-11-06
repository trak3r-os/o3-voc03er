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
inline constexpr unsigned kRetries = 3;  // upload attempts for one chunk before the slot is given up (not shown) until its length changes or it is marked dirty
inline constexpr unsigned kChunkBytes = static_cast<unsigned>(o3sdk::hal::kMaxUploadBytes);
inline constexpr unsigned kChunks = (kSlotBytes + kChunkBytes - 1u) / kChunkBytes;  // a slot goes up in kChunks uploads, one a pump()

// RAM_G, from the app's region (ui::plot::kRamGApp): the skin's assets take [0, 10120), the pictures start at 10240 and a slot is kSlotBytes (the big
// picture 744 x 96, then the narrow one 240 x 48). Bitmap handle 0 (ui::plot::kHandleApp), the one the kit leaves to the app: Canvas::bind() binds it for every sprite.
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

  // Main loop, from a draw(): sends one chunk (at most kMaxUploadBytes) of the staged slot to RAM_G. The slot stops being shown when its first chunk goes
  // up (the old picture is torn from then on) and is shown again after the last one. A failed chunk is sent again on the next call, kRetries in all;
  // then the slot is given up, not shown, until its length changes or mark_dirty().
  void pump(o3sdk::hal::Display& d) noexcept;

  // The slot's samples changed in place (NORMALISE): rebuild its picture although the length is the same. Main loop.
  void mark_dirty(unsigned slot) noexcept { want_[slot & 3u] = kDirty; }

  bool shown(unsigned slot) const noexcept { return shown_[slot & 3u]; }
  bool busy() const noexcept { return phase_ != Phase::Idle || pending_ != 0u; }
  std::uint32_t slot_addr(unsigned slot) const noexcept;
  unsigned failures() const noexcept { return fails_; }
  // The largest magnitude the slot's last finished build saw (0 before one): what NORMALISE changes (the tests read it, the screen does not).
  std::int32_t peak(unsigned slot) const noexcept { return peak_[slot & 3u]; }

  // Draws the slot's picture with its top left at (x, y): the big one (744 x 96) or the narrow one (240 x 48), in `ink`. Nothing when the slot has no
  // picture. Inside an open Canvas section: a bind (6 words) and a sprite (1).
  void draw(o3sdk::ui::plot::Canvas& c, unsigned slot, bool big, int x, int y, o3sdk::ui::Rgb ink) const noexcept;

 private:
  enum class Phase : std::uint8_t { Idle, Building, Staged };

  static constexpr std::uint32_t kDirty = 0xFFFFFFFFu;  // a want_ no length has: the next service() sees a change

  void start_next() noexcept;

  const std::int16_t* mem_ = nullptr;
  std::uint32_t stride_ = 0;
  std::uint32_t want_[kSlots] = {};  // the length last seen
  bool shown_[kSlots] = {};
  std::int32_t peak_[kSlots] = {};
  std::uint8_t pending_ = 0;  // bit n: slot n waits for its turn
  Phase phase_ = Phase::Idle;
  std::uint8_t active_ = 0;  // the slot being built or staged
  std::uint8_t fails_ = 0;
  std::uint8_t chunk_ = 0;  // the next chunk of the staged slot
  Cursor cur_;
  Peaks peaks_;
  alignas(4) std::uint8_t stage_[kSlotBytes];  // big picture, then narrow
};

// The app's one, in the SDRAM (it is about 15 KB): main loop only.
Overview& shared() noexcept;

}  // namespace o3_voc03er::overview
