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
// Bend. set_bend(ratio) multiplies the speed of every sounding reader, loop or region, by a ratio in 0.5 .. 2 (one octave down to one up): the
// Vocoder passes 2^(Bend/12), so a recorded voice or sample bends like the synth. Exactly 1 (bend 0) leaves everything above untouched,
// bit for bit: the integer paths are taken by a test once a block. Away from 1 a reader plays from a fixed-point phase (Q33: whole samples
// in the high bits, 2^-33 of a sample below; one uint64, no float to integer conversion, no double): the speed (0.5x, 1x or 2x times the
// ratio) is added to it a sample, the output is the linear interpolation between the two neighbouring samples (the second wraps at a loop's
// seam, and stops at a region's end), and the ratio glides linearly from the one the last chunk ended at to the target over the next chunk
// (the Vocoder already glides the semitones over 20 ms, so no step is heard). The fades live in the stored samples, so a loop's seam is as quiet
// bent as at 1x; a region keeps its fade in and its release as counts, and its natural end fades by position (the last 64 samples' worth of
// distance at its nominal speed), since how many samples are left now depends on the ratio. Back at exactly 1 the reader leaves the fraction
// in one chunk of 32 samples or more that lands on a whole sample (a whole half sample at 0.5x and in reverse), by a speed within 2 % of the
// nominal one, and the integer paths go on from there. While a loop is bent, bar lines are ignored (it drifts from the bars by design); the first one after the bend is
// back at 0 puts it right with the usual crossfade. A jump or stop tail running (ghost) plays at the nominal speed. Cost of a bent chunk of 64:
// a span copy plus about 20 cycles a sample (interpolate, add the phase), a few hundred cycles of set-up; an unbent reader pays three compares.
//
// Bars. A loop knows how many whole bars it is (set_bars, 0 = free: a take that is not a count of bar lines, or a WAV that is not whole bars).
// Only the owner knows that (Vocoder counts the bar lines a take spans, and judges a loaded WAV against the transport's tempo). For a loop of N
// bars the owner calls bar_line(i, frame, k) at every bar line of the transport (k the bar index, `frame` in the next mix() block): the loop
// puts its position where its cycle stands at that line, so it restarts with the bar whatever the tempo did meanwhile. The cycle is anchored
// at the bar start() was given (k0, the transport's bar when it started, 0 when the transport was stopped); bar k is j = k - k0 bars into it,
// and a loop at speed s (half samples a sample: 1, 2 or 4) is at half position (j * s * length / N) mod 2*length there (reverse: counted down
// from 2*(length-1)), rounded to the nearest. A bar line number 0 (the transport started again) re-anchors k0 to 0 (reanchor(), for every slot). The position it already has
// within 4 half samples of that is left alone, so at a constant tempo that is a whole number of samples a bar (120 BPM) bar_line() changes
// nothing and the output is bit-for-bit the free running loop's; at another tempo (123 BPM: 86048.78 samples a bar) the loop slips a
// fraction of a sample a cycle, and once the slip is past the tolerance a line on the loop's seam (the first sample, where the baked fades
// are at about zero) snaps it back by up to 8 half samples with no fade: nothing to hear. Any other line stages a jump at `frame`: the
// old position goes on for kFadeSamples (a ghost) fading out while the new one fades in, linearly, so the jump does not click;
// there is no time-stretch (a slower tempo replays the start of the loop until the line, a faster one cuts it at the line). A free loop, a loop that is not playing, one with a region or a ghost still running ignores the line. The
// transport stopping is not a bar line: the loop goes on, free, until the next start.
//
// Stop. stop() does not cut: the loop keeps playing from where it was for kFadeSamples more, fading to zero (the same ghost), so playing() is
// false at once but sounding() stays true until the tail ends. The caller keeps mixing while sounding(), not only while playing(). A start()
// inside the tail plays from the first sample at once and the tail fades out under it.
//
// Memory. The buffer is `kCount * per_loop` int16, 5.6 MB for the full size (kBankSamples): the app declares it with O3SDK_BIG (SDRAM) and
// gives it here; a unit never places memory itself. Real-time rules: nothing here allocates or blocks; record() and mix() are bounded by
// the block. mix() reads a loop's samples in one sequential pass (an SDRAM read is about 29 cycles sequential, 35 random, measured) and
// converts as it goes: no division, no libm. A loop that is not playing costs nothing (the caller does not call mix()).
//
// Regions. A reader can also play a region of the whole bank (set_region_start / set_region_len, then trig_region at a frame): `start` counts
// from the first sample of the bank, so any reader plays any slot's memory, `len` is in samples (start + len is clamped to the memory, len 0
// does nothing). It plays once, no wrap, at the reader's own speed, direction and level, and ends by itself at its end (reverse: from the
// end back to the start). Every start, release, natural end and retrigger goes through a kFadeSamples linear level ramp (an integer count,
// so exact); a retrigger fades the old region out first and then starts the new one. A whole-loop play on that reader is paused under the
// region (its position is kept, h_) and ducked over the first ramp with the loop's own level ramp, then resumes where it paused. play() is
// what the caller mixes with: the region when there is one, else mix() (which is unchanged). set_region_start / len are staged and read when
// the region begins, so a write between a retrigger and the end of the old region's fade changes the new one. One command a block and reader
// (the last wins; a release of a start in the same block cancels it).
// Threads: all of it runs in the render context, except bind() (main loop, before the engine starts).

#include <cstdint>

namespace o3_voc03er {

class Loop {
 public:
  static constexpr unsigned kCount = 4;
  static constexpr std::uint32_t kMaxSamples = 16u * 44100u;  // one loop: 16 s at 44.1 kHz
  static constexpr std::uint32_t kBankSamples = kCount * kMaxSamples;
  static constexpr std::uint32_t kFadeSamples = 64;
  static constexpr std::uint32_t kRatioOne = 1u << 24;  // the bend ratio 1.0 in Q24
  static constexpr std::uint64_t kSnap = 8;  // half samples: the slip a seam line puts right with a snap instead of a crossfade (bar_line)

  // `mem` holds kCount loops of `per_loop` samples (clamped to kMaxSamples), 2-byte aligned. nullptr or 0: unbound (every call is a no-op).
  // Empties all loops. The memory itself is not touched.
  void bind(std::int16_t* mem, std::uint32_t per_loop) noexcept;
  bool bound() const noexcept { return mem_ != nullptr; }
  std::uint32_t capacity() const noexcept { return cap_; }
  void clear() noexcept;  // every loop empty, nothing recording or playing
  void forget(unsigned i) noexcept;  // loop i alone: empty, not recording, not playing (its speed, direction and gain stay)

  // adopt(): loop i takes `length` samples the owner wrote into its memory (main loop, while the loop was stopped) as a finished take: what
  // end_record() leaves (length, position 0, not playing). The writer bakes its own fades, but a length shorter than the loop had gets a fade at
  // its new end, since the old one is cut off. false and no change when i is out of range, the loop is unbound, recording, playing or has a
  // region sounding or pending, or `length` is over capacity(). 0 is forget().
  bool adopt(unsigned i, std::uint32_t length) noexcept;

  // Recording. begin_record() empties loop i (and stops it playing) and starts taking samples; record() appends up to `n` samples of `in`
  // (clipped to +-1) and returns how many it took; when the loop is full it ends the recording itself (recording() is then false).
  // end_record() makes what was taken the loop (the seam fade is baked in).
  void begin_record(unsigned i) noexcept;
  std::uint32_t record(unsigned i, const float* in, std::uint32_t n) noexcept;
  void end_record(unsigned i) noexcept;
  bool recording(unsigned i) const noexcept { return i < kCount && rec_[i]; }

  // Playing. start() plays loop i from its first sample (false when it is empty, bound or not); stop() halts it; mix() writes (add false) or
  // adds (add true) `n` samples of loop i, scaled to +-1, to dst and advances it, wrapping at the length. A loop that is not playing: no-op.
  // `bar` is the transport's bar index when it starts (the anchor of the loop's bar grid, see Bars above); 0 for a free loop or a stopped transport.
  bool start(unsigned i, std::uint32_t bar = 0) noexcept;
  void stop(unsigned i) noexcept;
  void mix(unsigned i, float* dst, std::uint32_t n, bool add) noexcept;
  bool playing(unsigned i) const noexcept { return i < kCount && playing_[i]; }
  // playing() or a stop tail / jump fade still running: the caller mixes while this is true.
  bool sounding(unsigned i) const noexcept { return i < kCount && (playing_[i] || gn_[i] != 0u); }

  // Bars (render context, see above). set_bars: 0 = free, else the loop is that many whole bars (reset by every take, clear, forget and adopt).
  void set_bars(unsigned i, std::uint32_t bars) noexcept;
  std::uint32_t bars(unsigned i) const noexcept { return i < kCount ? bars_[i] : 0u; }
  // True while the loop holds the bars' cycle: it has bars and no bend has moved it off since the last bar line (the first line after the bend
  // is back at 0 puts it right, and sets this again).
  bool on_bar(unsigned i) const noexcept { return i < kCount && bars_[i] != 0u && !drift_[i]; }
  void bar_line(unsigned i, std::uint32_t frame, std::uint32_t bar) noexcept;
  // The transport's bar-0 line passed (it started again): the cycle is anchored at bar 0 again, for a loop under a region or one that is free too.
  void reanchor(unsigned i) noexcept;

  // Per loop, any time (render context): speed 0 = 0.5x, 1 = 1x, 2 = 2x (others clamp to 1x); reverse; gain, linear (default 1).
  void set_speed(unsigned i, unsigned speed) noexcept;
  void set_reverse(unsigned i, bool reverse) noexcept;
  void set_gain(unsigned i, float gain) noexcept;
  // The bend, for every reader (render context, any time; once a block is the intended rate): the speed factor, clamped to 0.5 .. 2 (NaN is 1).
  void set_bend(float ratio) noexcept;

  // Regions (render context). trig_region: start (or retrigger) at `frame` of the next play() block; false when ignored (unbound, recording,
  // nothing to play). release_region: fade out from `frame`; nothing when no region sounds.
  void set_region_start(unsigned i, std::uint32_t start) noexcept;
  void set_region_len(unsigned i, std::uint32_t len) noexcept;
  // `level` 0..100 (100 full) is the region's level, staged here and taken when the region begins (a region still sounding keeps its own);
  // the default is full, so a caller that never passes it plays at full level.
  // `repeat`: at its natural end the region begins again at once through the same ramps (out at its end, in at its start) until release_region
  // or a new trig_region; a retrigger takes the repeat flag of the new trig_region.
  bool trig_region(unsigned i, std::uint32_t frame, std::uint8_t level = 100, bool repeat = false) noexcept;
  void release_region(unsigned i, std::uint32_t frame) noexcept;
  bool region(unsigned i) const noexcept { return i < kCount && (act_[i] || cmd_[i] != Cmd::None); }  // sounding or about to
  bool region_sounding(unsigned i) const noexcept { return i < kCount && act_[i]; }
  bool region_repeating(unsigned i) const noexcept { return i < kCount && act_[i] && rep_[i]; }  // sounding a region that wraps
  void play(unsigned i, float* dst, std::uint32_t n, bool add) noexcept;  // the region if any, else mix()
  // Where reader i is, in samples from the start of the bank: its region's position, else its loop's.
  std::uint32_t bank_position(unsigned i) const noexcept;

  std::uint32_t length(unsigned i) const noexcept { return i < kCount ? len_[i] : 0u; }  // samples recorded so far / in the loop
  std::uint32_t position(unsigned i) const noexcept { return i < kCount ? (h_[i] >> 1) : 0u; }  // the sample the play position is in

 private:
  enum class Cmd : std::uint8_t { None, Start, Release };
  std::uint32_t bank_samples() const noexcept { return kCount * cap_; }
  bool begin_region(unsigned i, bool again = false) noexcept;
  std::uint32_t region_chunk(unsigned i, float* dst, std::uint32_t n, bool add) noexcept;
  std::uint32_t advance(unsigned i, float* dst, std::uint32_t from, std::uint32_t to, bool add) noexcept;
  void mix_region(unsigned i, float* dst, std::uint32_t n, bool add) noexcept;
  void region_off(unsigned i) noexcept;
  void mix_plain(unsigned i, float* dst, std::uint32_t n, bool add) noexcept;
  std::uint32_t mix_bent(unsigned i, float* dst, std::uint32_t n, bool add) noexcept;
  std::uint32_t region_chunk_bent(unsigned i, float* dst, std::uint32_t n, bool add) noexcept;
  // The bent path is in charge of a loop / a region: a ratio other than 1 now or asked for, or a phase off the grid the integer paths play on
  // (a fraction of a half sample, or an odd half sample at 1x and 2x, where they never leave the whole ones).
  bool loop_bent(unsigned i) const noexcept {
    return bq_ != kRatioOne || rq_[i] != kRatioOne || hf_[i] != 0u || (speed_[i] != 0u && (h_[i] & 1u) != 0u);
  }
  bool region_bent(unsigned i) const noexcept {
    return bq_ != kRatioOne || rrq_[i] != kRatioOne || rhf_[i] != 0u || (speed_[i] != 0u && (rh_[i] & 1u) != 0u);
  }
  void mix_slow(unsigned i, float* dst, std::uint32_t n, bool add) noexcept;
  void ghost_off(unsigned i) noexcept {
    gn_[i] = 0;
    sf_[i] = -1;
  }
  std::int16_t* mem_ = nullptr;
  std::uint32_t cap_ = 0;
  std::uint32_t len_[kCount] = {};
  std::uint32_t h_[kCount] = {};  // the play position in half samples
  // The bend (render context only). bq_: the target ratio, Q24, 0.5 .. 2 (set_bend); rq_: the ratio each reader's last chunk ended at; hf_ /
  // rhf_: the fraction of a half sample the loop's / region's position is past h_ / rh_, Q32 (0 whenever the integer paths are in charge);
  // rrq_: rq_ of the region.
  std::uint32_t bq_ = kRatioOne;
  std::uint32_t rq_[kCount] = {kRatioOne, kRatioOne, kRatioOne, kRatioOne};
  std::uint32_t rrq_[kCount] = {kRatioOne, kRatioOne, kRatioOne, kRatioOne};
  std::uint32_t hf_[kCount] = {};
  std::uint32_t rhf_[kCount] = {};
  std::uint8_t speed_[kCount] = {1, 1, 1, 1};
  bool rev_[kCount] = {};
  float gain_[kCount] = {1.0f, 1.0f, 1.0f, 1.0f};   // target
  float gcur_[kCount] = {1.0f, 1.0f, 1.0f, 1.0f};   // what the last chunk ended at
  bool rec_[kCount] = {};
  bool playing_[kCount] = {};
  // Bars and the ghost (render context only). bars_ 0 = free; k0_ the transport bar the cycle is anchored at. The ghost is a second read
  // position (gh_, gs_ half samples a sample, gv_ reverse) fading out over gn_ more samples (kFadeSamples at the start; 0 = none); gm_ true:
  // the main position fades in under it (a jump), false: the main is silent or full (a stop tail). sf_ >= 0: a jump staged at that frame
  // of the next mix() to half position sj_ (a plain snap, no ghost, when sn_).
  std::uint16_t bars_[kCount] = {};
  std::uint32_t k0_[kCount] = {};
  bool drift_[kCount] = {};  // a bend moved the loop off its bar cycle (render context; the bar line clears it)
  std::uint32_t gh_[kCount] = {};
  float gc_[kCount] = {};  // a stop tail's start offset (raw sample units): where a bent loop really was minus the whole half sample the ghost reads
  std::uint8_t gn_[kCount] = {};
  std::uint8_t gs_[kCount] = {};
  bool gv_[kCount] = {};
  bool gm_[kCount] = {};
  std::int32_t sf_[kCount] = {-1, -1, -1, -1};
  std::uint32_t sj_[kCount] = {};
  bool sn_[kCount] = {};  // the staged jump is a snap (no ghost, no fade): see bar_line
  // Regions: staged span, the sounding one (rs_, rl_, rh_ in half samples from its start), the level it ended the last chunk at, the fade
  // (fn_ of kFadeSamples, dir_ +1 in, 0 steady, -1 out), a retrigger waiting for the fade-out (rst_), the command and its frame.
  std::uint32_t st_s_[kCount] = {};
  std::uint32_t st_l_[kCount] = {};
  std::uint8_t st_v_[kCount] = {100, 100, 100, 100};  // the staged level (trig_region), taken by begin_region
  std::uint8_t rv_[kCount] = {100, 100, 100, 100};    // the sounding region's level, 0..100; folded into the chunk's gain
  std::uint32_t rs_[kCount] = {};
  std::uint32_t rl_[kCount] = {};
  std::uint32_t rh_[kCount] = {};
  float rg_[kCount] = {};
  std::uint16_t cf_[kCount] = {};
  std::uint8_t fn_[kCount] = {};
  std::int8_t dir_[kCount] = {};
  Cmd cmd_[kCount] = {};
  bool act_[kCount] = {};
  bool rst_[kCount] = {};
  bool st_r_[kCount] = {};  // the staged repeat flag (trig_region), taken by begin_region
  bool rep_[kCount] = {};   // the sounding region wraps at its end (until a release or a retrigger)
};

}  // namespace o3_voc03er
