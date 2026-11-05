// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Loop: a bank of kCount mono int16 loops of up to 16 s at 44.1 kHz in one buffer the owner hands over (bind). It knows nothing of the
// engine, the transport or the Vocoder: it records the samples it is given, plays a recorded one back at 1x with sample-exact loop points
// (the loop is exactly the samples recorded), and says nothing about when to start: the caller decides the frame (Vocoder, from the
// bar lines).
//
// The seam. A loop's two ends are made to meet at silence: end_record() bakes a short fade into the stored samples, the first and the last
// kFadeSamples (64, 1.5 ms; half the loop at most) ramping linearly from and to zero. Playback then does nothing at the seam (no crossfade
// path, no per-sample cost), and the wrap cannot click whatever the source was doing at the bar line. The price is a dip of 1.5 ms once a loop.
//
// Speed and direction. Each loop plays at 0.5x, 1x or 2x, forward or reverse (set_speed, set_reverse), and has a level (set_gain). The play
// position is an integer in HALF samples (h, 0..2*length-1): 0.5x advances 1 a sample, 1x 2, 2x 4, so there is no drift over any number of
// loops, and the output is a linear interpolation between the samples h/2 and h/2+1 (the second wraps to the first). 1x forward on a whole
// sample with a settled level keeps the plain pass (the fast path); every other case copies, once a chunk of 64 output samples, the span of
// samples the chunk needs into a stack buffer (one sequential SDRAM pass, forward or backward, wrapping at the loop's end or start, even for
// a loop shorter than the span) and interpolates there. The fades are in the stored samples, so a wrap is as quiet at any speed and way.
// A change of speed or direction while playing acts from the next mix() (the position carries over: the signal is continuous, only its
// slope changes, and going to 1x forward drops an odd half sample). A level change ramps linearly over the next chunk. start() begins at
// the first sample (the last one in reverse) at the loop's own speed.
//
// Memory. The buffer is `kCount * per_loop` int16, 5.6 MB for the full size (kBankSamples): the app declares it with O3SDK_BIG (SDRAM) and
// gives it here; a unit never places memory itself. Real-time rules: nothing here allocates or blocks; record() and mix() are bounded by
// the block. mix() reads a loop's samples in one sequential pass (an SDRAM read is about 29 cycles sequential, 35 random, measured) and
// converts as it goes: no division, no libm. A loop that is not playing costs nothing (the caller does not call mix()).
// Threads: all of it runs in the render context, except bind() (main loop, before the engine starts).

#include <cstdint>

namespace o3_voc03er {

class Loop {
 public:
  static constexpr unsigned kCount = 4;
  static constexpr std::uint32_t kMaxSamples = 16u * 44100u;  // one loop: 16 s at 44.1 kHz
  static constexpr std::uint32_t kBankSamples = kCount * kMaxSamples;
  static constexpr std::uint32_t kFadeSamples = 64;

  // `mem` holds kCount loops of `per_loop` samples (clamped to kMaxSamples), 2-byte aligned. nullptr or 0: unbound (every call is a no-op).
  // Empties all loops. The memory itself is not touched.
  void bind(std::int16_t* mem, std::uint32_t per_loop) noexcept;
  bool bound() const noexcept { return mem_ != nullptr; }
  std::uint32_t capacity() const noexcept { return cap_; }
  void clear() noexcept;  // every loop empty, nothing recording or playing

  // Recording. begin_record() empties loop i (and stops it playing) and starts taking samples; record() appends up to `n` samples of `in`
  // (clipped to +-1) and returns how many it took; when the loop is full it ends the recording itself (recording() is then false).
  // end_record() makes what was taken the loop (the seam fade is baked in).
  void begin_record(unsigned i) noexcept;
  std::uint32_t record(unsigned i, const float* in, std::uint32_t n) noexcept;
  void end_record(unsigned i) noexcept;
  bool recording(unsigned i) const noexcept { return i < kCount && rec_[i]; }

  // Playing. start() plays loop i from its first sample (false when it is empty, bound or not); stop() halts it; mix() writes (add false) or
  // adds (add true) `n` samples of loop i, scaled to +-1, to dst and advances it, wrapping at the length. A loop that is not playing: no-op.
  bool start(unsigned i) noexcept;
  void stop(unsigned i) noexcept;
  void mix(unsigned i, float* dst, std::uint32_t n, bool add) noexcept;
  bool playing(unsigned i) const noexcept { return i < kCount && playing_[i]; }

  // Per loop, any time (render context): speed 0 = 0.5x, 1 = 1x, 2 = 2x (others clamp to 1x); reverse; gain, linear (default 1).
  void set_speed(unsigned i, unsigned speed) noexcept;
  void set_reverse(unsigned i, bool reverse) noexcept;
  void set_gain(unsigned i, float gain) noexcept;

  std::uint32_t length(unsigned i) const noexcept { return i < kCount ? len_[i] : 0u; }  // samples recorded so far / in the loop
  std::uint32_t position(unsigned i) const noexcept { return i < kCount ? (h_[i] >> 1) : 0u; }  // the sample the play position is in

 private:
  std::int16_t* mem_ = nullptr;
  std::uint32_t cap_ = 0;
  std::uint32_t len_[kCount] = {};
  std::uint32_t h_[kCount] = {};  // the play position in half samples
  std::uint8_t speed_[kCount] = {1, 1, 1, 1};
  bool rev_[kCount] = {};
  float gain_[kCount] = {1.0f, 1.0f, 1.0f, 1.0f};   // target
  float gcur_[kCount] = {1.0f, 1.0f, 1.0f, 1.0f};   // what the last chunk ended at
  bool rec_[kCount] = {};
  bool playing_[kCount] = {};
};

}  // namespace o3_voc03er
