// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PERFORM's eight factory sounds (SHIFT + F8, docs/UI-DESIGN.md "The SHIFT row of PERFORM"): a constexpr table in flash. Every preset lists
// the same fifteen sound parameters, in the same order, so choosing one leaves nothing behind from the one before; KEY, SCALE, the audio
// source, the loops and the transport are never in it. Pure data: tests/presets_test.cpp proves each value is inside its range.
//
// These are STARTING POINTS for the maintainer's ear, set by reasoning and by the host renders, not by listening on the unit: change a value
// here and nothing else has to move.

#include <o3_voc03er/units/tract/tract.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <cstddef>
#include <cstdint>

namespace o3_voc03er {

struct PresetValue {
  std::uint16_t id;
  float value;
};

inline constexpr std::size_t kPresetParams = 15;
inline constexpr std::size_t kPresetCount = 8;

struct Preset {
  const char* name;  // at most 8 characters: it is the F8 window's value on the SHIFT row
  PresetValue v[kPresetParams];
};

namespace preset_detail {
using V = Vocoder;
// The fifteen, in the order every preset lists them.
constexpr Preset make(const char* name, float mode, float carsrc, float wave, float noise, float attack_ms, float release_ms, float detune_ct,
                      float quality, float formant_st, float thru, float glide_ms, float gate, float arp, float vib, float freeze) {
  return {name,
          {{V::Mode, mode},       {V::CarSrc, carsrc},   {V::Wave, wave},       {V::Noise, noise},   {V::Attack, attack_ms},
           {V::Release, release_ms}, {V::Detune, detune_ct}, {V::Quality, quality}, {V::Formant, formant_st}, {V::Thru, thru},
           {V::Glide, glide_ms},  {V::Gate, gate},       {V::Arp, arp},         {V::Vib, vib},       {V::Freeze, freeze}}};
}
}  // namespace preset_detail

// UX2: no preset sets Freeze 1: FREEZE is TALK's latching gesture and a mode change clears it (apply_mode), so a preset that changed mode could not hold it,
// and in BANDS it is not heard. SWELL (named FROZEN until UX2) is the slow-attack, slow-release pad.
// Mode 0 BANDS, 3 ROBOT (these eight never select TRACT: in TRACT SHIFT + F8 picks from kTractPresets below). CarSrc 1 SYNTH, 2 NOISE. Gate 3 is 1/16; Arp 1 is UP.
//                                                      mode car wave noise  atk  rel  det  q   fmt  thru glide gate arp vib frz
inline constexpr Preset kPresets[kPresetCount] = {
    preset_detail::make("ROBOT",   3, 1, 0, 0.0f,   5.0f,   80.0f,  0.0f, 16, 0,  0.0f,   0.0f, 0, 0, 0.0f, 0),
    preset_detail::make("CHOIR",   0, 1, 0, 0.0f, 140.0f,  700.0f, 16.0f, 16, 0,  0.15f,  0.0f, 0, 0, 0.0f, 0),
    preset_detail::make("WHISPER", 0, 2, 0, 0.0f,  10.0f,  120.0f,  0.0f, 16, 0,  0.60f,  0.0f, 0, 0, 0.0f, 0),
    preset_detail::make("GIANT",   0, 1, 0, 0.0f,  15.0f,  200.0f,  6.0f, 16, -9, 0.0f, 250.0f, 0, 0, 0.0f, 0),
    preset_detail::make("ELF",     0, 1, 0, 0.0f,   5.0f,   80.0f,  6.0f, 16, 9,  0.0f,   0.0f, 0, 0, 0.0f, 0),
    preset_detail::make("PULSE",   0, 1, 0, 0.0f,   5.0f,   60.0f,  6.0f, 16, 0,  0.0f,   0.0f, 3, 0, 0.0f, 0),
    preset_detail::make("ARP",     0, 1, 0, 0.0f,   5.0f,   30.0f,  4.0f, 16, 0,  0.0f,   0.0f, 0, 1, 0.0f, 0),
    preset_detail::make("SWELL",   0, 1, 0, 0.0f, 300.0f,  600.0f, 10.0f, 16, 0,  0.0f,   0.0f, 0, 0, 0.0f, 0),
};

// ---- TRACT's eight (SHIFT + F8 while the mode is TRACT) ----------------------------------------------------------------------------------
// They write the tract unit (TractUnit, not the Vocoder): every one lists all twelve of its mouth and tube values, in the unit's own order, so choosing
// one leaves nothing behind from the one before. Source (PADS or LIVE) is not in it, nor are KEY, SCALE, the loops, the transport or any vocoder value.
// The names differ from the vocoder table's (a project saves one name: project_preset_index and project_tract_preset_index tell the tables apart by
// it). Mix is 1 (the tube alone, as apply_mode() leaves it in TRACT) except where a preset says otherwise. Vowel runs A 0, E .25, I .5, O .75, U 1.
// STARTING POINTS for the maintainer's ear, set by reasoning and never heard on the unit. Signs, read from the kit: Tongue + is front, Jaw + is more
// open (- narrows the tube), Lips + is spread and - is rounded. BREATH sits on I with the jaw down and the lips rounded: HISS only sounds while the narrowest section
// is under 0.3 wide (presets_test measures it). AH and BIG are the least sure of the eight.
inline constexpr std::size_t kTractPresetParams = 12;
inline constexpr std::size_t kTractPresetCount = 8;

struct TractPreset {
  const char* name;  // at most 8 characters
  PresetValue v[kTractPresetParams];
};

namespace preset_detail {
using T = TractUnit;
constexpr TractPreset tmake(const char* name, float vowel, float tongue, float jaw, float lips, float close, float glide_ms, float size, float res,
                            float hiss, float mix, float slide_ms, float accent) {
  return {name,
          {{T::Vowel, vowel}, {T::Tongue, tongue}, {T::Jaw, jaw},     {T::Lips, lips},   {T::Close, close},   {T::Glide, glide_ms},
           {T::Size, size},   {T::Res, res},       {T::Hiss, hiss},   {T::Mix, mix},     {T::Slide, slide_ms}, {T::Accent, accent}}};
}
}  // namespace preset_detail

// Close 0 OPEN. Size 0 BIG, 1 LARGE, 2 NORMAL, 3 SMALL, 4 TINY.
//                                                           vowel tongue jaw  lips close glide size res  hiss mix slide acc
inline constexpr TractPreset kTractPresets[kTractPresetCount] = {
    preset_detail::tmake("AH",      0.00f,  0.0f,  0.3f,  0.0f, 0,  30.0f, 2, 0.5f, 0.10f, 1.0f, 0.0f, 0.0f),
    preset_detail::tmake("OO",      1.00f,  0.0f, -0.2f, -0.1f, 0,  40.0f, 2, 0.5f, 0.10f, 1.0f, 0.0f, 0.0f),
    preset_detail::tmake("EE",      0.50f,  0.0f, -0.1f,  0.3f, 0,  30.0f, 2, 0.5f, 0.10f, 1.0f, 0.0f, 0.0f),
    preset_detail::tmake("WOW",     0.75f,  0.0f,  0.1f,  0.0f, 0, 200.0f, 1, 0.6f, 0.05f, 1.0f, 0.0f, 0.3f),
    preset_detail::tmake("BREATH",  0.50f,  0.0f, -0.3f, -0.3f, 0,  60.0f, 2, 0.3f, 0.90f, 0.7f, 0.0f, 0.0f),
    preset_detail::tmake("TIN MAN", 0.25f,  0.0f,  0.0f,  0.0f, 0,   5.0f, 4, 0.9f, 0.10f, 1.0f, 0.0f, 0.0f),
    preset_detail::tmake("TINY",    0.50f,  0.0f,  0.0f,  0.0f, 0,  25.0f, 4, 0.5f, 0.15f, 1.0f, 0.0f, 0.0f),
    preset_detail::tmake("BIG",     0.00f, -0.2f,  0.2f,  0.0f, 0,  80.0f, 0, 0.5f, 0.15f, 1.0f, 0.0f, 0.0f),
};

}  // namespace o3_voc03er
