// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MouthLink: the live voice's tube shape, handed from the Vocoder (which analyses the voice, units/vocoder mouth_live.h) to the TractUnit (which drives
// o3-sdk's tract Processor with it, TractUnit param SOURCE = LIVE). Two units, one struct, owned by the app and bound into both before the engine starts.
//
// Threading: none needed. Both units run in the render context, in one block, the Vocoder before the TractUnit (the instrument first, insert 0 after
// it), so the writer and the reader never overlap and nothing here is atomic. The main loop never touches it (the choice reaches the TractUnit as an
// ordinary parameter event). If the engine ever ran the two in different contexts this struct would need a sequence counter.
//
// The handshake is self-healing: the TractUnit sets `want` in every block in which it needs a live shape (SOURCE LIVE and MIX above 0); the Vocoder reads
// it and clears it once a block, and analyses the voice only in a block that had it set. If the TractUnit stops asking (SOURCE back to PADS, MIX 0, the
// unit removed) the analysis stops by itself one block later, so the app has nothing to keep consistent.

#include <cstdint>

namespace o3_voc03er {

struct MouthLink {
  static constexpr std::uint32_t kMaxAreas = 24;  // the most areas the tract Processor's set_areas takes
  alignas(4) float area[kMaxAreas] = {};   // relative areas, glottis first; positive, finite, widest about 1; read only while `valid`
  std::uint32_t n = 0;                     // how many are written (2..kMaxAreas)
  bool valid = false;                      // a shape has been written since the app started; it is then held (never cleared)
  bool want = false;                       // the reader's request for this block, cleared by the writer
};

}  // namespace o3_voc03er
