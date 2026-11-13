// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// UX3-3: which slots hold audio that no WAV of the project carries yet, whatever its length. A take that finishes (SAMPLE REC) or a WAV the hand loads (SAMPLE LOAD)
// sets the slot's bit; a WAV saved by FILE's F5, or one FILE's own project load read in, clears it. FILE's SAVE note combines it with the length check
// (file_logic.h unsaved_audio_mask), so a re-record of the same length is flagged too. One word, main loop only (every writer and the reader are screens).

#include <cstdint>

namespace o3_voc03er {

struct AudioDirty {
  std::uint8_t bits = 0;  // bit s: slot s (0..3)
  void mark(unsigned slot) noexcept { bits = static_cast<std::uint8_t>(bits | (1u << (slot & 3u))); }
  void clear(unsigned slot) noexcept { bits = static_cast<std::uint8_t>(bits & ~(1u << (slot & 3u))); }
  bool test(unsigned slot) const noexcept { return ((bits >> (slot & 3u)) & 1u) != 0u; }
};

inline AudioDirty& audio_dirty() noexcept {
  static AudioDirty d;
  return d;
}

}  // namespace o3_voc03er
