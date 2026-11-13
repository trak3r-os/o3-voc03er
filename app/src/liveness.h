// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The liveness table (UX2): which modes hear which value. One table, (parameter, mode) -> heard, that PERFORM's SHIFT row, the parameter strips and PARAMS
// all read, so the three agree. Pure: no engine, no screen; tests/liveness_test.cpp holds it.
//
// The rule (docs/MANUAL.md): a DIM key is not heard in this mode. A tap on it still sets the value, for the modes where it is heard, and the screen says
// where that is ("HEARD IN TALK ONLY"). The exceptions are the gestures that latch (FREEZE) and the strips' pads, which refuse outside their mode.
//
// Where the facts come from (units/vocoder, vocoder.cpp):
//   TUNE runs neither the carrier nor BANDS and TALK: the CARRIER page (SHAPE, SPREAD, SUB, TILT, NOISE, ATTACK, RELEASE), GLIDE, GATE, ARP and
//     VOICING are not heard there. Notes still reach TUNE's target raw (the ARP does not change that).
//   TRACT is the carrier alone (after GATE, ARP, GLIDE, BEND, VIB, SUB, TILT and NOISE) through the tract unit: the carrier page, GATE, ARP and GLIDE are
//     heard; FORMANT, THRU, QUALITY and FREEZE are not. The tract unit's own values speak in TRACT only.
//   FORMANT and THRU act on BANDS, TALK and ROBOT (ROBOT is BANDS with a leveller).
//   FREEZE and QUALITY are TALK's. DRY and LEVEL are in every output mix, TRACT's too. KEY and SCALE reach the pads' notes in every mode.
//   SPEED, AMOUNT and NOTES IN are TUNE's, except that SPEED is also heard in any mode while the carrier source is VOICE (CarSrc 8: Autotune::voice_step
//   smooths the sung note with SPEED's alpha; it ignores AMOUNT and NOTES IN). So the table takes the carrier source, `car_src`: a step's carrier lane
//   cannot select VOICE (pattern.h kCarrierMax is 7), so the engine's own CarSrc is the one to read. KEY and SCALE are heard in every mode.
//   The rule (UX2, settled): DIM means certainly not heard in this mode with the current settings. A lit key may be heard only with some carriers
//   (the CARRIER page, GLIDE and ARP are lit outside TUNE whichever carrier plays).

#include <o3_voc03er/modes.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <cstddef>
#include <cstdint>

namespace o3_voc03er::live {

constexpr std::uint8_t mode_bit(unsigned m) noexcept { return static_cast<std::uint8_t>(1u << m); }

inline constexpr std::uint8_t kAllModes = static_cast<std::uint8_t>((1u << kModeCount) - 1u);
inline constexpr std::uint8_t kNotTune = static_cast<std::uint8_t>(kAllModes & ~mode_bit(2));
inline constexpr std::uint8_t kBandsTalkRobot = static_cast<std::uint8_t>(mode_bit(0) | mode_bit(1) | mode_bit(3));
inline constexpr std::uint8_t kTractOnly = mode_bit(kModeTract);
inline constexpr std::uint8_t kTalkOnly = mode_bit(1);
inline constexpr std::uint8_t kTuneOnly = mode_bit(2);

// The app's own VOICING (the chords' voicing): the chords go to the carrier, so TUNE (which has none) does not hear it.
inline constexpr std::uint8_t kVoicing = kNotTune;

struct Row {
  std::uint16_t param;  // Vocoder::Param
  std::uint8_t modes;   // mode_bit set
};

// The vocoder's parameters that some mode does not hear. A parameter not listed is heard in every mode.
inline constexpr Row kVoice[] = {
    {Vocoder::Wave, kNotTune},       {Vocoder::Detune, kNotTune},  {Vocoder::Sub, kNotTune},    {Vocoder::Tilt, kNotTune},
    {Vocoder::Noise, kNotTune},      {Vocoder::Attack, kNotTune},  {Vocoder::Release, kNotTune}, {Vocoder::Glide, kNotTune},
    {Vocoder::Gate, kNotTune},       {Vocoder::Arp, kNotTune},     {Vocoder::Quality, kTalkOnly}, {Vocoder::Freeze, kTalkOnly},
    {Vocoder::Formant, kBandsTalkRobot}, {Vocoder::Thru, kBandsTalkRobot},
    {Vocoder::Speed, kTuneOnly},     {Vocoder::Amount, kTuneOnly}, {Vocoder::NotesIn, kTuneOnly},
};
inline constexpr unsigned kVoiceCount = sizeof kVoice / sizeof kVoice[0];

// The carrier source VOICE (Vocoder::CarSrc 8): the sung note drives the synth in any mode, so SPEED (the note's smoothing) is heard there too.
inline constexpr unsigned kCarrierVoice = 8;
static_assert(kCarrierVoice == Vocoder::kCarVoice, "the table reads the unit's VOICE source");

// The modes that hear vocoder parameter `param` with carrier source `car_src` (CarSrc; the default 0 is any source but VOICE).
constexpr std::uint8_t voice_modes(std::uint16_t param, unsigned car_src = 0) noexcept {
  if (param == Vocoder::Speed && car_src == kCarrierVoice) {
    return kAllModes;
  }
  for (unsigned i = 0; i < kVoiceCount; ++i) {
    if (kVoice[i].param == param) {
      return kVoice[i].modes;
    }
  }
  return kAllModes;
}

constexpr bool heard(std::uint8_t modes, unsigned mode) noexcept { return mode < kModeCount && (modes & mode_bit(mode)) != 0u; }
constexpr bool voice_heard(std::uint16_t param, unsigned mode, unsigned car_src = 0) noexcept { return heard(voice_modes(param, car_src), mode); }

// The line that says where a value is heard: "HEARD IN TALK ONLY", "NOT HEARD IN TUNE" (every mode but one), "HEARD IN BANDS, TALK, ROBOT". At most 39
// characters, always terminated. `modes` outside kAllModes is read as none ("NOT HEARD ANYWHERE").
constexpr void heard_text(std::uint8_t modes, char (&out)[40]) noexcept {
  std::size_t n = 0;
  const auto put = [&out, &n](const char* s) noexcept {
    while (*s != '\0' && n + 1u < sizeof out) {
      out[n++] = *s++;
    }
    out[n] = '\0';
  };
  unsigned count = 0;
  unsigned last = 0;
  unsigned missing = 0;
  for (unsigned m = 0; m < kModeCount; ++m) {
    if (heard(modes, m)) {
      ++count;
      last = m;
    } else {
      missing = m;
    }
  }
  out[0] = '\0';
  if (count == 0u) {
    put("NOT HEARD ANYWHERE");
  } else if (count == 1u) {
    put("HEARD IN ");
    put(kModeNames[last]);
    put(" ONLY");
  } else if (count == kModeCount) {
    put("HEARD IN EVERY MODE");
  } else if (count == kModeCount - 1u) {
    put("NOT HEARD IN ");
    put(kModeNames[missing]);
  } else {
    put("HEARD IN ");
    bool first = true;
    for (unsigned m = 0; m < kModeCount; ++m) {
      if (heard(modes, m)) {
        put(first ? "" : ", ");
        put(kModeNames[m]);
        first = false;
      }
    }
  }
}

// The line for a dim vocoder parameter: heard_text, except SPEED, which also runs with the VOICE carrier ("HEARD IN TUNE, OR WITH CARRIER VOICE").
constexpr void voice_heard_text(std::uint16_t param, unsigned car_src, char (&out)[40]) noexcept {
  if (param == Vocoder::Speed && car_src != kCarrierVoice) {
    constexpr char kSpeed[] = "HEARD IN TUNE, OR WITH CARRIER VOICE";
    static_assert(sizeof kSpeed <= 40, "fits the line");
    for (std::size_t i = 0; i < sizeof kSpeed; ++i) {
      out[i] = kSpeed[i];
    }
    return;
  }
  heard_text(voice_modes(param, car_src), out);
}

}  // namespace o3_voc03er::live
