// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The per-step carrier switch (V5c): a clocked hook that walks the 16 steps of the pattern (one bar, a sixteenth each, on the engine's
// transport) and, at the frame a step with a carrier is reached, emits a Param event for the Vocoder's CARRIER (Vocoder::CarSrc). The
// switch is sample-exact (the unit starts its 5 ms fade at that frame), holds until the next step that sets one, and an empty step emits
// nothing. A stop sends FOLLOW (0) at frame 0 of the stopped block, so a restart begins with the carrier the user set. A live input never
// takes this path: the values are only 0..6 and the unit has no live carrier (its source switch settles for 200 ms).
//
// Threads: the main loop writes the table (set_all) and the render context reads it in block(): per-step relaxed atomics, a single writer.
// A pattern edit during playback may be seen step by step (one step old or new), never torn, which is all a bar-long table needs.

#include <o3_voc03er/pattern.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/engine.h>
#include <o3sdk/dsp/event.h>
#include <o3sdk/dsp/transport.h>

#include <atomic>
#include <cstdint>

namespace o3_voc03er {

class CarrierSeq final : public o3sdk::dsp::Clocked {
 public:
  void init(o3sdk::dsp::UnitId vocoder) noexcept { unit_ = vocoder; }

  // Main loop: the pattern's carriers (kPatternSteps values).
  void set_all(const std::uint8_t* carrier) noexcept {
    for (unsigned i = 0; i < kPatternSteps; ++i) {
      table_[i].store(carrier[i], std::memory_order_relaxed);
    }
  }

  void block(o3sdk::dsp::BlockContext& ctx) noexcept override {
    namespace dsp = o3sdk::dsp;
    const dsp::Transport& t = ctx.transport();
    if (t.stopped) {
      (void)ctx.emit(unit_, dsp::param_change(Vocoder::CarSrc, 0.0f, 0));
    }
    dsp::for_each_step(t, dsp::kTicksPerSixteenth, 0, 0, [&](std::uint32_t frame, std::uint64_t k) {
      const std::uint8_t c = table_[k % kPatternSteps].load(std::memory_order_relaxed);
      if (c != 0u && c <= kCarrierMax) {
        (void)ctx.emit(unit_, dsp::param_change(Vocoder::CarSrc, static_cast<float>(c), static_cast<std::uint16_t>(frame)));
      }
    });
  }

 private:
  o3sdk::dsp::UnitId unit_ = o3sdk::dsp::kNoUnit;
  std::atomic<std::uint8_t> table_[kPatternSteps] = {};
};

}  // namespace o3_voc03er
