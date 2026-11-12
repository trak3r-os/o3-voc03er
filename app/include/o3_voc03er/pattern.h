// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the template keeps on the SD card, and the bytes it is kept in: a 16-step pattern (the notes the pads recorded) and one setting
// (the keyboard octave). Pure (no card, no state of the app), so a host test covers the format (tests/pattern_test.cpp). The card is
// reached through Context::files() (o3-sdk kit/app/include/o3sdk/app/files.h: read its header comment first; docs/NEW-APP.md section 13 and
// docs/FRAMEWORK.md "Files on the card" say more): these functions only turn the data into a file's bytes and back, with the kit's chunk container
// (docs/formats/o3sdk-chunk-file.md: big-endian, a CRC-32 trailer), so a cut or a damaged file is refused whole and never half applied.
//
// Why a file of the app's own for the setting: the kit's settings file (system.o3s) holds only what the system Config screen shows and the
// app cannot add a key to it. An app's own values go in a file of its own, as here.
//
//   pattern.o3a   type "PATN", one chunk STEP: u16 version (0x0100), u8 step count (16), 16 notes (0: a rest, else 1..127)
//                 plus, since V5c, an optional chunk CARR: u16 version (0x0100), u8 step count (16), 16 carriers (0: none, follow what is set;
//                 1 synth, 2 noise, 3..6 loop 1..4: the Vocoder's CARRIER values minus 0 FOLLOW). An absent CARR (every older file) is no carrier
//                 on any step; a CARR with a wrong count or a value above 6 refuses the whole file. An older reader skips the chunk.
//   config.o3a    type "CONF", one chunk OCTV: u16 version (0x0100), u8 octave + 3 (so 0..5 for the octaves -3..+2)
//                 plus VOIC (u16 version, u8 voicing 0..3) and, since MS1, SLCH: u16 version (0x0100), u8 the MIDI slice channel (0 SAME, 1..16 a
//                 channel, 17 OFF; midi_slice_logic.h). An absent SLCH (every older file) is SAME, today's behaviour; a value above 17 refuses the file.
// A newer minor version (the low byte) is read as far as this one knows it; a newer major, a wrong count or a value out of range is refused.

#include <o3_voc03er/pad_map.h>

#include <o3sdk/formats/chunk_file.h>

#include <cstddef>
#include <cstdint>

namespace o3_voc03er {

inline constexpr unsigned kPatternSteps = 16;
inline constexpr std::uint8_t kCarrierMax = 6;  // a step's carrier: 0 none, 1 synth, 2 noise, 3..6 loop 1..4
inline constexpr const char* kPatternFile = "pattern.o3a";  // in the app's folder on the card, Apps/<id>/ (the CMake id)
inline constexpr const char* kConfigFile = "config.o3a";
// The largest file each makes (about 80 and 50 bytes) with room to spare; the buffers that hold them are word aligned (Files::save reads them).
inline constexpr std::size_t kPatternFileMax = 1024;  // the V2 file with eight patterns is about 600 bytes, 750 with a mouth line (stepseq.h)
inline constexpr std::size_t kConfigFileMax = 64;

struct Pattern {
  std::uint8_t note[kPatternSteps] = {};  // 0: a rest
  std::uint8_t carrier[kPatternSteps] = {};  // 0: this step does not change the carrier (V5c), else 1..kCarrierMax
  unsigned cursor = 0;                    // the step the next recorded note goes to (not kept in the file)

  // Writes `n` into the step under the cursor and moves the cursor on (it wraps). A note out of 1..127 is a rest.
  void record(int n) noexcept {
    note[cursor] = n >= 1 && n <= 127 ? static_cast<std::uint8_t>(n) : 0;
    cursor = (cursor + 1u) % kPatternSteps;
  }
  // The next carrier for step `i` (none, synth, noise, loop 1..4, none ...); false for a step out of range.
  bool cycle_carrier(unsigned i) noexcept {
    if (i >= kPatternSteps) {
      return false;
    }
    carrier[i] = carrier[i] >= kCarrierMax ? 0 : static_cast<std::uint8_t>(carrier[i] + 1u);
    return true;
  }
  unsigned carriers_used() const noexcept {
    unsigned count = 0;
    for (std::uint8_t c : carrier) {
      count += c != 0 ? 1u : 0u;
    }
    return count;
  }
  void clear() noexcept {
    for (std::uint8_t& n : note) {
      n = 0;
    }
    for (std::uint8_t& c : carrier) {
      c = 0;
    }
    cursor = 0;
  }
  unsigned used() const noexcept {
    unsigned count = 0;
    for (std::uint8_t n : note) {
      count += n != 0 ? 1u : 0u;
    }
    return count;
  }
  friend bool operator==(const Pattern& a, const Pattern& b) noexcept {
    for (unsigned i = 0; i < kPatternSteps; ++i) {
      if (a.note[i] != b.note[i] || a.carrier[i] != b.carrier[i]) {
        return false;
      }
    }
    return true;  // (the cursor is not part of the file)
  }
};

// The pattern as a file: the size written, or 0 when `cap` is too small.
inline std::size_t write_pattern(const Pattern& p, std::uint8_t* out, std::size_t cap) noexcept {
  namespace chunk = o3sdk::formats::chunk;
  chunk::Writer w(out, cap, "PATN");
  if (!w.begin("STEP") || !w.u16(0x0100) || !w.u8(kPatternSteps) || !w.bytes(p.note, sizeof p.note) || !w.end()) {
    return 0;
  }
  if (p.carriers_used() != 0u) {  // no CARR for a pattern without carriers: the file stays what V1 wrote
    if (!w.begin("CARR") || !w.u16(0x0100) || !w.u8(kPatternSteps) || !w.bytes(p.carrier, sizeof p.carrier) || !w.end()) {
      return 0;
    }
  }
  return w.finish();
}

// A pattern from a file's bytes: false, and `p` untouched, when the file is damaged, short, of a newer major version, not 16 steps or has a
// note above 127. Everything is checked before anything is applied; the cursor goes back to the first step.
inline bool read_pattern(const std::uint8_t* data, std::size_t n, Pattern& p) noexcept {
  namespace chunk = o3sdk::formats::chunk;
  chunk::Reader r;
  chunk::Chunk c{};
  if (r.open(data, n, "PATN") != chunk::Error::None) {  // the container's checks: length, ids, pads and the CRC
    return false;
  }
  Pattern got;
  bool have_step = false;
  while (r.next(c)) {
    chunk::Fields f(c);
    if (chunk::id_is(c, "STEP")) {
      const std::uint16_t version = f.u16();
      const unsigned steps = f.u8();
      if ((version >> 8) != 1u || steps != kPatternSteps) {
        return false;
      }
      for (std::uint8_t& note : got.note) {
        note = f.u8();
        if (note > 127u) {
          return false;
        }
      }
      if (!f.ok()) {
        return false;
      }
      have_step = true;
    } else if (chunk::id_is(c, "CARR")) {
      const std::uint16_t version = f.u16();
      const unsigned steps = f.u8();
      if ((version >> 8) != 1u || steps != kPatternSteps) {
        return false;
      }
      for (std::uint8_t& car : got.carrier) {
        car = f.u8();
        if (car > kCarrierMax) {
          return false;
        }
      }
      if (!f.ok()) {
        return false;
      }
    }  // any other chunk is a newer version's: skipped
  }
  if (!have_step) {
    return false;
  }
  p = got;
  return true;
}

// The settings as a file: the octave (OCTV) and the chords' voicing (VOIC, 0..3, the Voicing enum). A file from before VOIC existed has no such chunk
// and reads as SMOOTH, the default.
inline constexpr unsigned kDefaultVoicing = 3;
// The MIDI slice channel (MS1, SLCH): 0 SAME (the default), 1..16, 17 OFF. The meaning is midi_slice_logic.h's; here it is a byte in range.
inline constexpr unsigned kSliceChMax = 17;

inline std::size_t write_config(int octave, std::uint8_t* out, std::size_t cap, unsigned voicing = kDefaultVoicing, unsigned slice_ch = 0) noexcept {
  namespace chunk = o3sdk::formats::chunk;
  chunk::Writer w(out, cap, "CONF");
  if (!w.begin("OCTV") || !w.u16(0x0100) || !w.u8(static_cast<std::uint8_t>(clamp_octave(octave) - kMinOctave)) || !w.end()) {
    return 0;
  }
  if (!w.begin("VOIC") || !w.u16(0x0100) || !w.u8(static_cast<std::uint8_t>(voicing > 3u ? 3u : voicing)) || !w.end()) {
    return 0;
  }
  if (!w.begin("SLCH") || !w.u16(0x0100) || !w.u8(static_cast<std::uint8_t>(slice_ch > kSliceChMax ? 0u : slice_ch)) || !w.end()) {
    return 0;
  }
  return w.finish();
}

// The octave, the voicing and the slice channel from a file's bytes: false, all untouched, when it is damaged or any is out of range. No VOIC chunk:
// SMOOTH; no SLCH chunk: SAME.
inline bool read_config(const std::uint8_t* data, std::size_t n, int& octave, unsigned& voicing, unsigned& slice_ch) noexcept {
  namespace chunk = o3sdk::formats::chunk;
  chunk::Reader r;
  chunk::Chunk c{};
  if (r.open(data, n, "CONF") != chunk::Error::None) {
    return false;
  }
  bool have_octave = false;
  int got_octave = 0;
  unsigned got_voicing = kDefaultVoicing;
  unsigned got_slice = 0;
  while (r.next(c)) {
    const bool is_octave = chunk::id_is(c, "OCTV");
    const bool is_slice = chunk::id_is(c, "SLCH");
    if (!is_octave && !is_slice && !chunk::id_is(c, "VOIC")) {
      continue;
    }
    chunk::Fields f(c);
    const std::uint16_t version = f.u16();
    const unsigned v = f.u8();
    if (!f.ok() || (version >> 8) != 1u) {
      return false;
    }
    if (is_octave) {
      if (v > static_cast<unsigned>(kMaxOctave - kMinOctave)) {
        return false;
      }
      got_octave = static_cast<int>(v) + kMinOctave;
      have_octave = true;
    } else if (is_slice) {
      if (v > kSliceChMax) {
        return false;
      }
      got_slice = v;
    } else {
      if (v > 3u) {
        return false;
      }
      got_voicing = v;
    }
  }
  if (!have_octave) {
    return false;
  }
  octave = got_octave;
  voicing = got_voicing;
  slice_ch = got_slice;
  return true;
}

inline bool read_config(const std::uint8_t* data, std::size_t n, int& octave, unsigned& voicing) noexcept {
  unsigned slice_ch = 0;
  return read_config(data, n, octave, voicing, slice_ch);
}

inline bool read_config(const std::uint8_t* data, std::size_t n, int& octave) noexcept {
  unsigned voicing = kDefaultVoicing;
  return read_config(data, n, octave, voicing);
}

}  // namespace o3_voc03er
