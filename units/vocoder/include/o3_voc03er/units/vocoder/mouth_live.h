// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MouthLive: the live voice as a tube shape for the TRACT mode (TractUnit param SOURCE = LIVE). It runs Talk's analysis (lpc.h) on the modulator and
// turns each hop's reflection coefficients into the areas of a vocal tract, which the TractUnit hands to the tract Processor's set_areas
// (o3-sdk extras/tract). The carrier then goes through a tube shaped like the speaker's: the "mouth" of the voice, without a lattice filter.
//
// Per hop (kHop = 32 samples at fs/4, so every 128 frames, every second block) at order kOrder = 11:
//  1. Levinson gives k[1..11]; Talk::areas_from_reflection gives the 12 areas of the lossless tube it describes, glottis first (the analysis
//     is the same one that drives TALK's lattice, so TALK's own numbers are untouched).
//  2. The last area (index 11) is the termination of the model (the radiation load), not a section of the tube: it is dropped. The other 11 are
//     normalised to the widest and turned into diameters d = sqrt(a / widest), floored at kFloor.
//  3. The diameters are smoothed across hops (one pole, kSmooth) and each is written twice, 22 areas in all: the Processor's Full tube has 22
//     sections, so set_areas is the identity (no resampling) and a section is a section of the analysed tube. (11 analysis sections at 11,025 Hz,
//     where the sound travels two sections of the kit's tube at 22,050 Hz per sample, so each analysis section is two of the kit's.)
// This mapping was found by a grid search over the orientation, the dropped area and the duplication, against the kit's own vowels excited by a
// glottal-like source: about 1 to 2 dB rms of log-spectral distance, and the formants within the errors tests/mouth_test.cpp prints. It is not
// a model of the speaker's real tract: LPC estimates an all-pole spectrum, and the tube it describes has the same spectrum, not the same shape.
//
// Honest limits (also in docs/MANUAL.md): the analysis pre-emphasises by 0.9 (it is built into Lpc), which a real voice's -6 dB/octave source
// tilt is meant to match; a source with a flat spectrum (a buzz, a click train) reads as a brighter vowel, its formants tens of percent high.
// A noisy room reads as a wide, open tube. Fricatives and whispers give shapes that have the spectrum of the noise, not of a mouth.
//
// Silence: below kGate on Lpc::power() the hop is skipped and the last shape holds. Why hold: a pause between words keeps its vowel (the tube
// does not snap to the pad vowel and back, which would click and be heard as a swallowed note), the held shape is a finite, valid tube (never a
// closed one), and the next word glides from it through the Processor's own glide. Before the first shape the link stays invalid and the
// TractUnit uses the pad/lane vowel.
//
// Cost [est]: the analysis is Talk's, about 7,000 cycles a block at order 12 (a little less at 11), the two decimators 1,100, the ring 500: about
// 8,500 a block, every block. The hop block adds Levinson at order 11 (about 250 multiply-adds, 11 divisions), the 11 areas (recip_groups, 3
// divisions), 11 square roots and the 22 writes: about 1,500 more, every second block. The TractUnit's set_areas (about 200) and the Processor's
// area steps (about 3,200 a block while the shape moves, a few hundred settled; kit README) come on top. About 12,000 cycles a block, 15,000 on
// a hop block, against the brief's 10,000 [est: a unit measurement is owed]. The hop work is not scheduled away from heavy blocks: it is one
// small step and the analysis itself is already sliced evenly across the hop (lpc.h).
// Memory: two halfbands, the smoothed diameters and a few words (about 100 bytes), here; the Lpc is Talk's (shared, see Vocoder). Real-time: no
// allocation, no double, no blocking; one context.

#include <o3_voc03er/units/mouth/link.h>
#include <o3_voc03er/units/vocoder/halfband.h>
#include <o3_voc03er/units/vocoder/lpc.h>

#include <cstdint>

namespace o3_voc03er {

class MouthLive {
 public:
  static constexpr std::uint32_t kOrder = 11;            // LPC order = sections of the analysed tube
  static constexpr std::uint32_t kAreas = 2u * kOrder;   // what the link carries: each section twice (the Full tube's 22)
  static constexpr float kGate = 1.0e-6f;                // Lpc::power() under this is silence: the shape holds [est: about -60 dB; the unit decides]
  static constexpr float kFloor = 0.05f;                 // the narrowest diameter, relative to the widest (about 1/400 in area)
  static constexpr float kSmooth = 0.5f;                 // one-pole coefficient on the diameters, per hop (about 6 ms)

  // Start (or restart) listening: clears the decimators and the smoothing, and takes `lpc` for the analysis (order kOrder). Does not touch the link's
  // shape, so what was last seen holds until the first hop of the new listening.
  void start(Lpc& lpc) noexcept;

  // n modulator samples (at most 64). `lpc` is the one start() took, `link` the one the TractUnit reads. Writes link.area, n and valid at a hop end.
  void process(Lpc& lpc, const float* mod, std::uint32_t n, MouthLink& link) noexcept;

  // The silent hops since the last shape (a test seam), and the hops written.
  std::uint32_t held_hops() const noexcept { return held_; }
  std::uint32_t shapes() const noexcept { return shapes_; }

 private:
  void shape(Lpc& lpc, MouthLink& link) noexcept;

  Halfband h1_, h2_;
  bool odd1_ = false, odd2_ = false;
  bool have_ = false;                  // the smoothing holds a shape of this listening
  float d_[kOrder] = {};               // smoothed diameters, glottis first
  std::uint32_t held_ = 0;
  std::uint32_t shapes_ = 0;
};

static_assert(2u * MouthLive::kOrder <= MouthLink::kMaxAreas, "the doubled sections fit the link");

}  // namespace o3_voc03er
