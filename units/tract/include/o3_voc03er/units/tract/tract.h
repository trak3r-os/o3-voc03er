// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// TRACT: an Effect around o3-sdk's tract Processor (extras/tract, Quality::Full: the tube at fs/2 with 22 sections, formants up to 11 kHz). The
// vocoder's TRACT mode puts the carrier through it: the unit sits on slot 0 as insert 0, after the Vocoder instrument, which in that mode writes
// the carrier alone. A mono tube: the planes are averaged on the way in (the vocoder writes the same signal on both, so the mean is exact) and the
// result goes to both planes. At MIX 0 the planes pass untouched, bit for bit.
//
// Parameters (the id is the index in the table: append, never insert). Values are in the descriptor's own units:
//   0 Vowel   0..1 through A E I O U (default 0, A)
//   1 Tongue  -1..+1: the tongue's place, added to the vowel's shape (default 0)
//   2 Jaw     -1..+1: the jaw's opening, added to the vowel's shape (default 0)
//   3 Lips    -1..+1: the lips' rounding and opening, added to the vowel's shape (default 0)
//   4 Close   0 OPEN, 1 LIPS, 2 TIP, 3 BACK (default OPEN): a closure of the tube (the consonant's stop); moves in 12 ms whatever GLIDE says
//   5 Glide   ms, 5..500 (default 30): the time the vowel, the tongue, the jaw and the lips take to move
//   6 Size    0 BIG, 1 LARGE, 2 NORMAL, 3 SMALL, 4 TINY (default NORMAL): the length of the tube (formants x0.85 .. x1.22); a change runs a 5 ms dip
//   7 Res     0..1 (default 0.5): the ringing of the formants
//   8 Hiss    0..1 (default 0.3): noise at the narrowest point, only while it is narrow (a consonant's turbulence)
//   9 Mix     0..1 (default 0): 0 is a bit-exact bypass (no state runs), 1 the tube alone
//  10 Slide   ms, 0..2000 (default 0): the sequencer's own glide (the MOUTH line writes it, a slid step's length). 0 means GLIDE rules; above 0 the
//             unit glides with SLIDE instead, so a step's glide never fights the GLIDE knob (a GLIDE turn never changes SLIDE, and the hook never
//             writes GLIDE). The Processor clamps the glide it is given to 5..500 ms: a slide longer than 500 ms is 500. The glide is one pole (its
//             time is the time constant), so a move that must arrive within T ms is written as T / 3 (95 %)
//  11 Accent  0..1 (default 0): the jaw opens by this much more, the bend takes JAW + ACCENT clamped to -1..+1 (the MOUTH line writes it on an accented
//             step; JAW keeps the knob's value and the hook never writes it). 0 is bit-exact with a build without it
//  12 Source  0 PADS, 1 LIVE (default PADS): where the tube's shape comes from. PADS is the vowel, tongue, jaw and lips above (the pads, the lane, the
//             MOUTH line) and is bit-exact with a build without this parameter. LIVE drives the tube from the voice at the input: the Vocoder
//             analyses the modulator (mouth_live.h) and hands the areas over a MouthLink (bind_mouth); VOWEL, TONGUE, JAW, LIPS and ACCENT keep
//             moving underneath but do not shape the tube while the areas rule (the way back to PADS lands where they are); CLOSE still silences
//             (a stop), and SIZE, RES, HISS, GLIDE and MIX act as always. It needs a bound link, MIX above 0 and
//             a voice above the analysis' gate: until the first shape the pads' vowel sounds, and in silence the last shape holds. Not saved: the
//             project and the config store TRCT's first eight values, so the app starts on PADS (docs/MANUAL.md)
//
// Two things the wrapper adds to the Processor:
//  * MIX rises only after a hold of kEnterHoldMs (12 ms, a little more than the vocoder's 10 ms mode crossfade): the app writes MIX 1 together with
//    Mode TRACT, and the tube (about 6 % of the CPU) should not run while the old engine's crossfade (which with eight notes already costs
//    76..88 % of a block) is still going. During the hold the unit is a bypass. A drop to 0 acts at once (the Processor ramps it in 5 ms).
//  * Sleep on silence: when MIX is above 0 and both the input and the tube's own output have stayed under kSilent (-80 dB) for kSleepFrames (256),
//    the Processor is skipped and the output is zeros, until the input is above kSilent again; then the Processor is reset (a clean tube, which
//    cannot step: its state was already below -80 dB) and runs on. The Processor only sleeps at MIX 0, which is no help to a TRACT mode with a
//    held-down GATE or no notes: an idle tube costs the few instructions that read the input's peak.
//
// Cost [est]: the Processor at Quality::Full is about 256 cycles a frame measured by an instruction trace (o3-sdk extras/tract/README.md), 16,400 a
// block of 64, about 6.3 % of the CPU; the wrapper adds about 8 cycles a frame (mean and two peaks). Asleep or at MIX 0 it is about 3 cycles a
// frame. A unit measurement is owed.

#include <o3_voc03er/units/mouth/link.h>
#include <o3sdk/dsp/unit.h>
#include <o3sdk/extras/tract/processor.h>

#include <cstdint>

namespace o3_voc03er {

class TractUnit final : public o3sdk::dsp::Unit {
 public:
  enum Param : std::uint16_t { Vowel, Tongue, Jaw, Lips, Close, Glide, Size, Res, Hiss, Mix, Slide, Accent, Source, kParamCount };

  static constexpr float kEnterHoldMs = 12.0f;        // MIX 0 -> above 0 waits this long (the vocoder's crossfade is 10 ms)
  static constexpr float kSilent = 1.0e-4f;           // -80 dB: under it the input, and the tube's output, count as silent
  static constexpr std::uint32_t kSleepFrames = 256;  // silent for this many frames in a row: sleep
  static constexpr std::uint32_t kMaxFrames = 64;     // the engine's block (kMaxBlockFrames, engine.h: a unit does not include the engine)

  TractUnit() noexcept;

  const o3sdk::dsp::UnitInfo& info() const noexcept override;
  void prepare(float sample_rate) noexcept override;
  void process(const o3sdk::dsp::ProcessArgs& args) noexcept override;

  // The live voice's shape (SOURCE LIVE), main loop before the engine starts; the link outlives the unit, and the Vocoder is bound to the same one.
  // Null (the default): SOURCE LIVE does nothing and the pads' vowel rules.
  void bind_mouth(MouthLink* link) noexcept { link_ = link; }
  bool mouth_live() const noexcept { return live_; }   // SOURCE LIVE, a link and MIX above 0: the tube is (or is about to be) driven by the voice

  // For a test or a screen (the render context's state, read after a block).
  bool asleep() const noexcept { return asleep_; }
  bool holding() const noexcept { return hold_left_ != 0u; }
  // The frames the Processor ran in, since prepare(): what the sleep saves is the difference with the frames processed.
  std::uint32_t processed_frames() const noexcept { return processed_; }

 private:
  void apply(const o3sdk::dsp::Event& e) noexcept;
  void set(std::uint16_t id, float v) noexcept;
  void run(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  void pass(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  void drive_mouth() noexcept;
  void update_live() noexcept;

  o3sdk::extras::tract::Processor proc_;
  alignas(8) float mono_[kMaxFrames] = {};
  float bent_jaw() const noexcept;  // JAW + ACCENT, clamped
  float values_[kParamCount] = {};
  float fs_ = 44100.0f;
  float mix_ = 0.0f;                 // the MIX the app asked for
  std::uint32_t hold_frames_ = 529;  // kEnterHoldMs at fs
  std::uint32_t hold_left_ = 0;      // frames of the hold still to run; the Processor's MIX is 0 until it ends
  std::uint32_t quiet_ = 0;          // frames in a row with a silent input and a silent output
  std::uint32_t processed_ = 0;
  MouthLink* link_ = nullptr;        // the Vocoder's shape, render context only (link.h)
  bool live_ = false;                // SOURCE LIVE with a link and MIX above 0
  bool asleep_ = false;
  bool engaged_ = false;             // the Processor's MIX has been raised (and not dropped to 0 since)
};

static_assert(TractUnit::kMaxFrames % 2 == 0, "the mono block is a whole number of words");

}  // namespace o3_voc03er
