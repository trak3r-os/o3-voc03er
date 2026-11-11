// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PERFORM's eight factory sounds (SHIFT + F8, docs/UI-DESIGN.md "The SHIFT row of PERFORM"): a constexpr table in flash. Every preset lists
// the same fifteen sound parameters, in the same order, so choosing one leaves nothing behind from the one before; KEY, SCALE, the audio
// source, the loops and the transport are never in it. Pure data: tests/presets_test.cpp proves each value is inside its range.
//
// These are STARTING POINTS for the maintainer's ear, set by reasoning and by the host renders, not by listening on the unit: change a value
// here and nothing else has to move.

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

// Mode 0 BANDS, 3 ROBOT (4 TRACT is not a preset: the tube is played, not preset). CarSrc 1 SYNTH, 2 NOISE. Gate 3 is 1/16; Arp 1 is UP.
//                                                      mode car wave noise  atk  rel  det  q   fmt  thru glide gate arp vib frz
inline constexpr Preset kPresets[kPresetCount] = {
    preset_detail::make("ROBOT",   3, 1, 0, 0.0f,   5.0f,   80.0f,  0.0f, 16, 0,  0.0f,   0.0f, 0, 0, 0.0f, 0),
    preset_detail::make("CHOIR",   0, 1, 0, 0.0f, 140.0f,  700.0f, 16.0f, 16, 0,  0.15f,  0.0f, 0, 0, 0.0f, 0),
    preset_detail::make("WHISPER", 0, 2, 0, 0.0f,  10.0f,  120.0f,  0.0f, 16, 0,  0.60f,  0.0f, 0, 0, 0.0f, 0),
    preset_detail::make("GIANT",   0, 1, 0, 0.0f,  15.0f,  200.0f,  6.0f, 16, -9, 0.0f, 250.0f, 0, 0, 0.0f, 0),
    preset_detail::make("ELF",     0, 1, 0, 0.0f,   5.0f,   80.0f,  6.0f, 16, 9,  0.0f,   0.0f, 0, 0, 0.0f, 0),
    preset_detail::make("PULSE",   0, 1, 0, 0.0f,   5.0f,   60.0f,  6.0f, 16, 0,  0.0f,   0.0f, 3, 0, 0.0f, 0),
    preset_detail::make("ARP",     0, 1, 0, 0.0f,   5.0f,   30.0f,  4.0f, 16, 0,  0.0f,   0.0f, 0, 1, 0.0f, 0),
    preset_detail::make("FROZEN",  0, 1, 0, 0.0f, 300.0f,  600.0f, 10.0f, 16, 0,  0.0f,   0.0f, 0, 0, 0.0f, 1),
};

}  // namespace o3_voc03er
