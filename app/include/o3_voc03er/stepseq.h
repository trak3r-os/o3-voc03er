// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pattern bank and the step sequencer (U6a): eight patterns A..H of 16 steps, LENGTH 1..16 each, and the clocked hook that plays them
// (it replaces the V5c CarrierSeq). docs/UI-DESIGN.md "PATTERN" is the spec; the PATTERN screen is U6b, the SLICE lane U6c.
//
// A step is ONE 32-bit word, so the render context reads a whole step with a single aligned load (atomic on the M4) and an edit made
// during playback is seen as the old step or the new one, never a mix of the two:
//   byte 0  note     0 a rest, else 1..127: the single note, or the chord's ROOT as a MIDI note (pitch class = note % 12, octave = note / 12 - 4,
//                    the way make_chord counts: MIDI = 48 + 12 * octave + pitch class)
//   byte 1  bits 0..2 kind     0 a single note, 1..4 a chord of ChordKind (Triad, Seventh, Open, Power) + 1
//           bits 3..4 voicing  Voicing (chords only)
//           bits 5..7 carrier  CARRIER lane: 0 FOLLOW (the step changes nothing: what SOUNDS says stays), 1 SYNTH, 2 NOISE, 3..6 SLOT 1..4.
//                              LIVE is never a lane value (it is the user's pick and the lane cannot reach it, V5c)
//   byte 2  level    LEVEL lane = the note's velocity, 0 means the default (kLevelDefault), else 1..127
//   byte 3  slice    SLICE lane (U6c): 0 none, 1..24 fires that slice of the SPEAKS slot at the step's frame (kSliceMax); a value above the
//                    slot's slice count is stored and stays silent
// Everything zero is a valid step (a rest, FOLLOW, default level), so the bank can live zero-initialised in the SDRAM (O3SDK_BIG).
//
// The MOUTH lane (TRACT V3) is a byte a step beside the step word, in PatternData::mouth (16 bytes a pattern, so a copy, a clear and a file carry it
// with the pattern; the step word is untouched):
//   bits 0..3 value  0 FOLLOW (the step writes nothing), 1..9 the vowels A, A-E, E, E-I, I, I-O, O, O-U, U (the tract's VOWEL is (value - 1) / 8),
//                    10 LIPS, 11 TIP, 12 BACK (closures); 13..15 are refused on load (kept free for V3b)
//   bits 4..6        zero
//   bit 7 GLD        a vowel slides to the step's vowel over the step's length instead of GLIDE's time; refused on a closure and on FOLLOW
// Read with a relaxed atomic byte load, written by the main loop (single writer), as the step words are.
//
// Threads: the main loop edits through the functions below (relaxed atomic word stores, single writer); the render context reads in
// StepSeq::block(). `length` is a byte of its own, also read and written atomically; 0 reads as 16. The hook's other inputs (the pattern to
// play next, the key and scale, what the user picked as CARRIER) are relaxed atomics of the hook.

#include <o3_voc03er/chords.h>
#include <o3_voc03er/pattern.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/engine.h>
#include <o3sdk/dsp/event.h>
#include <o3sdk/dsp/transport.h>
#include <o3sdk/formats/chunk_file.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace o3_voc03er {

inline constexpr unsigned kBankPatterns = 8;
inline constexpr std::uint8_t kLevelDefault = 100;
// The v2 pattern file: the app's PATN with the old STEP and CARR chunks (pattern A, so an older reader still gets it), BANK (all eight) and, when a
// step has a mouth byte, MOUT (V3).
inline constexpr std::size_t kBankFileMax = 1024;  // about 750 bytes with a mouth line, with room to spare

// SliceMap (the SPEAKS slot's slices: the hook's SLICE lane and the unit's MIDI slices read the same map) and kSliceMax live in vocoder.h; the
// main loop writes it through StepSeq::slices_open / slices_close.

struct Step {
  std::uint8_t note = 0;
  std::uint8_t kind = 0;
  std::uint8_t voicing = 0;
  std::uint8_t carrier = 0;
  std::uint8_t level = 0;
  std::uint8_t slice = 0;
};

constexpr bool step_valid(const Step& s) noexcept {
  return s.note <= 127u && s.kind <= 4u && s.voicing <= 3u && s.carrier <= kCarrierMax && s.level <= 127u && s.slice <= kSliceMax;
}
constexpr std::uint32_t pack_step(const Step& s) noexcept {
  return static_cast<std::uint32_t>(s.note) |
         (static_cast<std::uint32_t>((s.kind & 7u) | ((s.voicing & 3u) << 3) | ((s.carrier & 7u) << 5)) << 8) |
         (static_cast<std::uint32_t>(s.level) << 16) | (static_cast<std::uint32_t>(s.slice) << 24);
}
constexpr Step unpack_step(std::uint32_t w) noexcept {
  Step s;
  s.note = static_cast<std::uint8_t>(w & 0xFFu);
  const std::uint32_t b1 = (w >> 8) & 0xFFu;
  s.kind = static_cast<std::uint8_t>(b1 & 7u);
  s.voicing = static_cast<std::uint8_t>((b1 >> 3) & 3u);
  s.carrier = static_cast<std::uint8_t>(b1 >> 5);
  s.level = static_cast<std::uint8_t>((w >> 16) & 0xFFu);
  s.slice = static_cast<std::uint8_t>(w >> 24);
  return s;
}
constexpr std::uint8_t step_velocity(const Step& s) noexcept { return s.level == 0u ? kLevelDefault : s.level; }

struct alignas(4) PatternData {
  std::uint32_t step[kPatternSteps];  // packed steps (pack_step); read and written with the atomics below
  std::uint8_t length;                // 1..16, 0 reads as 16
  std::uint8_t reserved[3];
  std::uint8_t mouth[kPatternSteps];  // the MOUTH lane (see the header comment); 0 FOLLOW; relaxed atomic bytes
};
static_assert(sizeof(PatternData) == 4 * kPatternSteps + 4 + kPatternSteps && alignof(PatternData) == 4, "the hook reads the words aligned, no padding");

// The bank is zero-initialised POD, so it can sit in the SDRAM: `O3SDK_BIG Bank g_bank;`.
struct alignas(4) Bank {
  PatternData pattern[kBankPatterns];
};
static_assert(sizeof(Bank) == kBankPatterns * sizeof(PatternData), "no padding in the bank");

// --- access: single aligned words, relaxed (see the header comment) ---
inline std::uint32_t load_word(const std::uint32_t& w) noexcept { return __atomic_load_n(&w, __ATOMIC_RELAXED); }
inline void store_word(std::uint32_t& w, std::uint32_t v) noexcept { __atomic_store_n(&w, v, __ATOMIC_RELAXED); }

inline unsigned length_of(const PatternData& p) noexcept {
  const unsigned n = __atomic_load_n(&p.length, __ATOMIC_RELAXED);
  return n >= 1u && n <= kPatternSteps ? n : kPatternSteps;
}
inline Step get_step(const PatternData& p, unsigned i) noexcept {
  return i < kPatternSteps ? unpack_step(load_word(p.step[i])) : Step{};
}

// --- the MOUTH lane's byte ---
inline constexpr unsigned kMouthVowels = 9;       // values 1..9: A, A-E, E, E-I, I, I-O, O, O-U, U
inline constexpr unsigned kMouthLips = 10;        // values 10..12: the closures
inline constexpr unsigned kMouthBack = 12;
inline constexpr std::uint8_t kMouthGld = 0x80;
inline constexpr std::uint8_t kMouthValueMask = 0x0F;
constexpr unsigned mouth_value(std::uint8_t m) noexcept { return m & kMouthValueMask; }
constexpr bool mouth_gld(std::uint8_t m) noexcept { return (m & kMouthGld) != 0u; }
constexpr bool mouth_is_vowel(std::uint8_t m) noexcept { return mouth_value(m) >= 1u && mouth_value(m) <= kMouthVowels; }
constexpr bool mouth_is_closure(std::uint8_t m) noexcept { return mouth_value(m) >= kMouthLips && mouth_value(m) <= kMouthBack; }
// The closure's CLOSE value (1 LIPS, 2 TIP, 3 BACK) of a closure byte.
constexpr unsigned mouth_close(std::uint8_t m) noexcept { return mouth_value(m) - kMouthLips + 1u; }
// The tract's VOWEL (0..1) of a vowel byte: value 1 is A at 0, value 9 is U at 1.
constexpr float mouth_vowel(std::uint8_t m) noexcept { return static_cast<float>(mouth_value(m) - 1u) * (1.0f / 8.0f); }
// A byte the lane holds: bits 4..6 zero, a value 0..12, GLD on a vowel only.
constexpr bool mouth_valid(unsigned m) noexcept {
  if (m > 0xFFu || (m & 0x70u) != 0u) {
    return false;
  }
  const std::uint8_t b = static_cast<std::uint8_t>(m);
  return mouth_value(b) <= kMouthBack && (!mouth_gld(b) || mouth_is_vowel(b));
}
// SLIDE for a move that must arrive within `ms`: the tube's glide is one pole (time constant = the glide), so a third of the time reaches 95 %. The
// tract's Processor clamps its glide to 5..500 ms, and so does this.
inline constexpr float kSlideMinMs = 5.0f;
inline constexpr float kSlideMaxMs = 500.0f;
constexpr float slide_clamp(float ms) noexcept { return ms < kSlideMinMs ? kSlideMinMs : (ms > kSlideMaxMs ? kSlideMaxMs : ms); }
constexpr float slide_for(float ms) noexcept { return slide_clamp(ms * (1.0f / 3.0f)); }
inline std::uint8_t get_mouth(const PatternData& p, unsigned i) noexcept {
  return i < kPatternSteps ? __atomic_load_n(&p.mouth[i], __ATOMIC_RELAXED) : std::uint8_t{0};
}

// --- editing (main loop). Every one returns false, changing nothing, for an index or a value out of range. ---
inline bool set_step(PatternData& p, unsigned i, const Step& s) noexcept {
  if (i >= kPatternSteps || !step_valid(s)) {
    return false;
  }
  store_word(p.step[i], pack_step(s));
  return true;
}
inline bool edit_step(PatternData& p, unsigned i, std::uint8_t Step::*field, unsigned v, unsigned max) noexcept {
  if (i >= kPatternSteps || v > max) {
    return false;
  }
  Step s = get_step(p, i);
  s.*field = static_cast<std::uint8_t>(v);
  return set_step(p, i, s);
}
// A single note (1..127), or a rest for 0. Keeps the step's carrier and level; a note makes the step a single note.
inline bool set_note(PatternData& p, unsigned i, unsigned note) noexcept {
  if (i >= kPatternSteps || note > 127u) {
    return false;
  }
  Step s = get_step(p, i);
  s.note = static_cast<std::uint8_t>(note);
  s.kind = 0;
  s.voicing = 0;
  return set_step(p, i, s);
}
inline bool set_rest(PatternData& p, unsigned i) noexcept { return set_note(p, i, 0); }
// A chord: its root as a MIDI note (1..127), the kind and the voicing. Keeps the carrier and the level.
inline bool set_chord(PatternData& p, unsigned i, unsigned root, ChordKind kind, Voicing voicing) noexcept {
  if (i >= kPatternSteps || root < 1u || root > 127u) {
    return false;
  }
  Step s = get_step(p, i);
  s.note = static_cast<std::uint8_t>(root);
  s.kind = static_cast<std::uint8_t>(static_cast<unsigned>(kind) + 1u);
  s.voicing = static_cast<std::uint8_t>(voicing);
  return set_step(p, i, s);
}
inline bool set_carrier(PatternData& p, unsigned i, unsigned c) noexcept { return edit_step(p, i, &Step::carrier, c, kCarrierMax); }
inline bool set_level(PatternData& p, unsigned i, unsigned v) noexcept { return edit_step(p, i, &Step::level, v, 127u); }
inline bool set_slice(PatternData& p, unsigned i, unsigned v) noexcept { return edit_step(p, i, &Step::slice, v, kSliceMax); }
// The mouth byte of a step (mouth_valid: 0 clears it).
inline bool set_mouth(PatternData& p, unsigned i, unsigned m) noexcept {
  if (i >= kPatternSteps || !mouth_valid(m)) {
    return false;
  }
  __atomic_store_n(&p.mouth[i], static_cast<std::uint8_t>(m), __ATOMIC_RELAXED);
  return true;
}
// The next carrier of step `i` (FOLLOW, SYNTH, NOISE, SLOT 1..4, FOLLOW ...).
inline bool cycle_carrier(PatternData& p, unsigned i) noexcept {
  if (i >= kPatternSteps) {
    return false;
  }
  const unsigned c = get_step(p, i).carrier;
  return set_carrier(p, i, c >= kCarrierMax ? 0u : c + 1u);
}
inline bool set_length(PatternData& p, unsigned n) noexcept {
  if (n < 1u || n > kPatternSteps) {
    return false;
  }
  __atomic_store_n(&p.length, static_cast<std::uint8_t>(n), __ATOMIC_RELAXED);
  return true;
}
inline void clear_pattern(PatternData& p) noexcept {
  for (std::uint32_t& w : p.step) {
    store_word(w, 0u);
  }
  for (std::uint8_t& m : p.mouth) {
    __atomic_store_n(&m, std::uint8_t{0}, __ATOMIC_RELAXED);
  }
  __atomic_store_n(&p.length, std::uint8_t{0}, __ATOMIC_RELAXED);
}
// The pads' recording: the note under `cursor` (a single note, or a rest when out of 1..127), the cursor moves on and wraps at LENGTH.
inline void record_note(PatternData& p, unsigned& cursor, int note) noexcept {
  const unsigned n = length_of(p);
  cursor = cursor < n ? cursor : 0u;
  (void)set_note(p, cursor, note >= 1 && note <= 127 ? static_cast<unsigned>(note) : 0u);
  cursor = (cursor + 1u) % n;
}
inline unsigned notes_used(const PatternData& p) noexcept {
  unsigned c = 0;
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    c += get_step(p, i).note != 0u ? 1u : 0u;
  }
  return c;
}
inline unsigned carriers_used(const PatternData& p) noexcept {
  unsigned c = 0;
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    c += get_step(p, i).carrier != 0u ? 1u : 0u;
  }
  return c;
}
inline unsigned mouths_used(const PatternData& p) noexcept {
  unsigned c = 0;
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    c += get_mouth(p, i) != 0u ? 1u : 0u;
  }
  return c;
}
inline bool same_pattern(const PatternData& a, const PatternData& b) noexcept {
  if (length_of(a) != length_of(b)) {
    return false;
  }
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    if (load_word(a.step[i]) != load_word(b.step[i]) || get_mouth(a, i) != get_mouth(b, i)) {
      return false;
    }
  }
  return true;
}
inline void copy_pattern(PatternData& dst, const PatternData& src) noexcept {
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    store_word(dst.step[i], load_word(src.step[i]));
    __atomic_store_n(&dst.mouth[i], get_mouth(src, i), __ATOMIC_RELAXED);
  }
  __atomic_store_n(&dst.length, static_cast<std::uint8_t>(length_of(src)), __ATOMIC_RELAXED);
}
// The V1/V5c pattern (notes and carriers, 16 steps) into a pattern: single notes, FOLLOW levels, length 16.
inline void from_legacy(PatternData& dst, const Pattern& src) noexcept {
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    Step s;
    s.note = src.note[i];
    s.carrier = src.carrier[i];
    (void)set_step(dst, i, s);
    __atomic_store_n(&dst.mouth[i], std::uint8_t{0}, __ATOMIC_RELAXED);  // a v1 file has no mouth line
  }
  (void)set_length(dst, kPatternSteps);
}
inline Pattern to_legacy(const PatternData& src) noexcept {
  Pattern p;
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    const Step s = get_step(src, i);
    p.note[i] = s.note;
    p.carrier[i] = s.carrier;
  }
  return p;
}

// What a step plays: the notes (a chord of at most kChordMaxNotes, or the one note). Pure, so a test can compare it with make_chord.
// `prev` is the chord before, for SMOOTH only (null: none). Returns the count (0 for a rest).
inline unsigned step_notes(const Step& s, unsigned key, unsigned scale, std::uint8_t (&out)[kChordMaxNotes], const Chord* prev) noexcept {
  if (s.note == 0u) {
    return 0;
  }
  if (s.kind == 0u) {
    out[0] = s.note;
    return 1;
  }
  const Chord c = make_chord(static_cast<ChordKind>(s.kind - 1u), s.note % 12u, static_cast<int>(s.note / 12u) - 4, key, scale,
                             static_cast<Voicing>(s.voicing), prev);
  unsigned n = 0;
  for (; n < c.count && n < kChordMaxNotes; ++n) {
    out[n] = c.notes[n];
  }
  return n;
}

// --- the file ---
// BANK: u16 version (0x0100), u8 pattern count (8), then per pattern u8 length (1..16) and 16 u32 steps. A BANK with a wrong count, a length or a
// step field out of range refuses the whole file; no BANK is a v1 file, which loads into pattern A only.
// MOUT (V3, written only when a step has a mouth byte): u16 version (0x0100), u8 pattern count (8), then per pattern its 16 mouth bytes. A MOUT with a
// wrong count or a byte mouth_valid refuses the file; no MOUT means every step FOLLOWs.
inline std::size_t write_bank(const Bank& b, std::uint8_t* out, std::size_t cap) noexcept {
  namespace chunk = o3sdk::formats::chunk;
  const Pattern a = to_legacy(b.pattern[0]);  // the old reader's view of pattern A
  chunk::Writer w(out, cap, "PATN");
  if (!w.begin("STEP") || !w.u16(0x0100) || !w.u8(kPatternSteps) || !w.bytes(a.note, sizeof a.note) || !w.end()) {
    return 0;
  }
  if (a.carriers_used() != 0u && (!w.begin("CARR") || !w.u16(0x0100) || !w.u8(kPatternSteps) || !w.bytes(a.carrier, sizeof a.carrier) || !w.end())) {
    return 0;
  }
  if (!w.begin("BANK") || !w.u16(0x0100) || !w.u8(kBankPatterns)) {
    return 0;
  }
  for (const PatternData& p : b.pattern) {
    if (!w.u8(static_cast<std::uint8_t>(length_of(p)))) {
      return 0;
    }
    for (unsigned i = 0; i < kPatternSteps; ++i) {
      if (!w.u32(load_word(p.step[i]))) {
        return 0;
      }
    }
  }
  if (!w.end()) {
    return 0;
  }
  bool any_mouth = false;
  for (const PatternData& p : b.pattern) {
    any_mouth = any_mouth || mouths_used(p) != 0u;
  }
  if (any_mouth) {
    if (!w.begin("MOUT") || !w.u16(0x0100) || !w.u8(kBankPatterns)) {
      return 0;
    }
    for (const PatternData& p : b.pattern) {
      std::uint8_t m[kPatternSteps];
      for (unsigned i = 0; i < kPatternSteps; ++i) {
        m[i] = get_mouth(p, i);
      }
      if (!w.bytes(m, sizeof m)) {
        return 0;
      }
    }
    if (!w.end()) {
      return 0;
    }
  }
  return w.finish();
}

// `b` is changed only when the whole file is good (the checks run on a copy). A v1 file (read_pattern's) loads pattern A and leaves B..H alone.
inline bool read_bank(const std::uint8_t* data, std::size_t n, Bank& b) noexcept {
  namespace chunk = o3sdk::formats::chunk;
  chunk::Reader r;
  chunk::Chunk c{};
  if (r.open(data, n, "PATN") != chunk::Error::None) {
    return false;
  }
  Bank got = {};
  bool have_bank = false;
  bool have_mout = false;
  std::uint8_t mouth[kBankPatterns][kPatternSteps] = {};
  while (r.next(c)) {
    if (chunk::id_is(c, "MOUT")) {
      chunk::Fields f(c);
      if (have_mout || (f.u16() >> 8) != 1u || f.u8() != kBankPatterns) {
        return false;
      }
      for (auto& row : mouth) {
        for (std::uint8_t& m : row) {
          m = f.u8();
          if (!mouth_valid(m)) {
            return false;
          }
        }
      }
      if (!f.ok()) {
        return false;
      }
      have_mout = true;
      continue;
    }
    if (!chunk::id_is(c, "BANK")) {
      continue;
    }
    chunk::Fields f(c);
    const std::uint16_t version = f.u16();
    const unsigned count = f.u8();
    if ((version >> 8) != 1u || count != kBankPatterns) {
      return false;
    }
    for (PatternData& p : got.pattern) {
      const unsigned len = f.u8();
      if (len < 1u || len > kPatternSteps) {
        return false;
      }
      p.length = static_cast<std::uint8_t>(len);
      for (std::uint32_t& w : p.step) {
        w = f.u32();
        if (!step_valid(unpack_step(w))) {
          return false;
        }
      }
    }
    if (!f.ok()) {
      return false;
    }
    have_bank = true;
  }
  if (have_bank) {
    // The v1 chunks are checked too (a damaged one is a damaged file), by the old reader.
    Pattern ignore;
    if (!read_pattern(data, n, ignore)) {
      return false;
    }
    for (unsigned i = 0; i < kBankPatterns; ++i) {
      std::memcpy(got.pattern[i].mouth, mouth[i], kPatternSteps);  // absent MOUT: every step FOLLOWs
      copy_pattern(b.pattern[i], got.pattern[i]);
    }
    return true;
  }
  Pattern legacy;
  if (!read_pattern(data, n, legacy)) {
    return false;
  }
  from_legacy(b.pattern[0], legacy);
  return true;
}

// --- the hook ---
// A clocked hook that walks the playing pattern on the engine's transport (a sixteenth a step, no swing here) and, for each step:
//   CARRIER  a Param event for Vocoder::CarSrc at the step's frame when the lane is not FOLLOW (the unit starts its 5 ms fade there);
//   NOTE     the previous step's notes off, then the notes on at the step's frame; they go off at 75 % of the step (a gate of 180 ticks, a second
//            for_each_step grid), so a step never runs into the next, and at a Stop;
//   SLICE    when the step's slice is 1..count of the SPEAKS slot's map: Start, Len and Trig 1 of that slot's own reader at the step's frame (a
//            rest in NOTE still fires it). Always one shot: it plays to its end or until the next slice on that reader; the slot's TRIG and
//            CHOKE are the pads' and do not apply. A slice above the count, or no source, emits nothing. At a Stop one Trig 0 releases the
//            reader that fired, only if a slice fired since the last Stop.
//   LEVEL    the notes' velocity: the last note sets the level of the SYNTH and NOISE carriers (one gain, 100 and up are full). A slice has no level.
//   MOUTH    (V3, once set_tract() names the tract unit; events go out in every mode, the unit is a bypass outside TRACT) at most 3 Param events
//            a step, all on the step's frame or on its gate's end: a vowel writes VOWEL at the step's frame; with GLD it writes SLIDE (the step's
//            length / 3: the tube's glide is one pole, so a third of the time reaches 95 %) first, and the next plain vowel writes SLIDE 0 first; a closure writes CLOSE 1..3 at the step's frame and CLOSE 0 at the
//            gate's end (a step with no note: at the step's end, the next edge, unless the next step is a closure too); FOLLOW writes nothing. The
//            hook never writes GLIDE. A Stop (and so a pattern change that lands on it) writes CLOSE 0 and SLIDE 0 once, if the line wrote since the
//            last one. While set_mouth_hold(true) (a PERFORM strip pad, vowel or closure, is held: the pad owns the tract) it emits no mouth event, and
//            at a Stop CLOSE stays the pad's but SLIDE 0 still goes out, or the GLIDE knob would stay dead.
// An event budget for a block (the vocoder's; the mouth's events go to the tract unit, whose own 32 are far from used): a step's chord is at most kChordMaxNotes ons, plus the same offs at the gate, one CarSrc and the 3 region events:
// 14 of kMaxEventsPerUnit (32, of which the notes injected by the main loop take at most kMaxInjectPerUnit = 16, so 16 are left); a block holds
// at most one step edge.
// PATTERN CHANGE: request_pattern() only stores the wish; the hook takes it when the playing pattern reaches its end (and at a Play). A LENGTH
// change takes effect at the same boundary or earlier: the end is read from the pattern each step.
// STOP: the notes still sounding get their offs at frame 0 of the stopped block; the carrier the lane set is restored to what the user picked
// (SOUNDS: LIVE or a slot) and only then: a Stop of a pattern that never changed the carrier emits no CarSrc at all.
// Objects: about 56 bytes of SRAM_U, no heap, no division beyond `% 12` in make_chord, no libm, no double.
class StepSeq final : public o3sdk::dsp::Clocked {
 public:
  static constexpr std::uint8_t kNoPattern = 0xFF;

  static constexpr std::uint8_t kNoReader = 0xFF;

  void init(o3sdk::dsp::UnitId vocoder, const Bank* bank, SliceMap* map = nullptr) noexcept {
    unit_ = vocoder;
    bank_ = bank;
    map_ = map;
    if (map_ != nullptr) {
      __atomic_store_n(&map_->source, 0u, __ATOMIC_RELAXED);
    }
  }
  // Main loop (single writer): the SPEAKS slot's slices. slices_open() hides the map (source 0, release) and returns it to rewrite; slices_close()
  // shows it again (reader | count << 8 in one word, release). The hook and the unit load the word with acquire, so they never read a half-written
  // map and never see a reader with another's count. A null map (none given at init) returns null and publishes nothing. `count` is clamped to
  // kSliceMax; a reader above 3 or a count of 0 means none.
  SliceMap* slices_open() noexcept {
    if (map_ != nullptr) {
      __atomic_store_n(&map_->source, 0u, __ATOMIC_RELEASE);
    }
    return map_;
  }
  void slices_close(unsigned reader, unsigned count) noexcept {
    if (map_ == nullptr) {
      return;
    }
    const bool ok = reader < 4u && count != 0u;
    const std::uint32_t n = count < kSliceMax ? count : kSliceMax;
    __atomic_store_n(&map_->source, ok ? (reader | n << 8) : 0u, __ATOMIC_RELEASE);
  }
  // The published source (UI: the PATTERN palette). 0 slices: none.
  unsigned slice_count() const noexcept { return source() >> 8; }
  unsigned slice_reader() const noexcept {
    const std::uint32_t src = source();
    return src == 0u ? static_cast<unsigned>(kNoReader) : (src & 0xFFu);
  }
  // The slice the last step that fired one played; 0 after a Stop (relaxed, may be a block old).
  unsigned playing_slice() const noexcept { return playing_slice_.load(std::memory_order_relaxed); }
  // Main loop: the pattern that plays after the current one's last step (or from the next Play). 0..7.
  void request_pattern(unsigned p) noexcept {
    next_.store(p < kBankPatterns ? static_cast<std::uint8_t>(p) : kNoPattern, std::memory_order_relaxed);
  }
  // Main loop: how many times the hook has taken its next pattern (at each Play and at each pattern end, a wrapping byte; relaxed). The SONG
  // screen counts the differences to walk its chain one pattern ahead; nothing else reads it.
  unsigned laps() const noexcept { return laps_.load(std::memory_order_relaxed); }
  // The pattern playing now and the step that played last (UI and tests; relaxed, may be a block old).
  unsigned playing_pattern() const noexcept { return cur_.load(std::memory_order_relaxed); }
  unsigned playing_step() const noexcept { return step_.load(std::memory_order_relaxed); }
  // Main loop (M3, MIDI notes recorded into PATTERN): where the hook is, in ONE word so that the three fields belong together: the edge (the
  // step's index k on the transport's sixteenth grid from Play, a wrapping 16 bits), the step that played last and the pattern it is in.
  // `playing` is false before the first step of a run and after a Stop. Relaxed; may be a block old.
  struct Position {
    bool playing = false;
    std::uint16_t edge = 0;
    std::uint8_t step = 0;
    std::uint8_t pattern = 0;
  };
  Position position() const noexcept {
    const std::uint32_t w = pos_word_.load(std::memory_order_relaxed);
    Position p;
    p.playing = (w & kPosValid) != 0u;
    p.edge = static_cast<std::uint16_t>(w & 0xFFFFu);
    p.step = static_cast<std::uint8_t>((w >> 16) & 0xFFu);
    p.pattern = static_cast<std::uint8_t>((w >> 24) & 7u);
    return p;
  }
  // Main loop, once at setup: the tract unit the MOUTH lane writes to (kNoUnit, the default: no mouth events).
  void set_tract(o3sdk::dsp::UnitId tract) noexcept { tract_ = tract; }
  // Main loop: true while a PERFORM strip pad is held. The hook emits no mouth events while it is set (relaxed, may be a block old).
  void set_mouth_hold(bool held) noexcept { mouth_hold_.store(held, std::memory_order_relaxed); }
  bool mouth_hold() const noexcept { return mouth_hold_.load(std::memory_order_relaxed); }
  // Main loop: the CARRIER the user picked (every write of CarSrc from the main side, mirrored by the app each pass), the key and scale the
  // chords are spelled in.
  void set_picked(std::uint8_t v) noexcept { picked_.store(v, std::memory_order_relaxed); }
  void set_key_scale(std::uint8_t key, std::uint8_t scale) noexcept {
    key_.store(key, std::memory_order_relaxed);
    scale_.store(scale, std::memory_order_relaxed);
  }

  void block(o3sdk::dsp::BlockContext& ctx) noexcept override {
    namespace dsp = o3sdk::dsp;
    if (bank_ == nullptr) {
      return;
    }
    const dsp::Transport& t = ctx.transport();
    if (t.stopped) {
      release(ctx, 0);
      if (fired_ != kNoReader) {  // one Trig 0 on the reader a lane step fired (the render-context byte: a source change loses nothing)
        (void)ctx.emit(unit_, dsp::param_change(Vocoder::region_param(fired_, Vocoder::RegionField::Trig), 0.0f, 0));
        fired_ = kNoReader;
      }
      playing_slice_.store(0, std::memory_order_relaxed);
      mouth_stop(ctx);
      pos_word_.store(0u, std::memory_order_relaxed);
      if (changed_) {  // only what a lane step did is undone, to the user's pick (never FOLLOW: LIVE and slots survive a Stop)
        (void)ctx.emit(unit_, dsp::param_change(Vocoder::CarSrc, static_cast<float>(picked_.load(std::memory_order_relaxed)), 0));
        changed_ = false;
      }
    }
    if (t.started) {
      pos_ = 0;
      take_next();
      note_lap();
    }
    int on_frame = -1;
    int close_frame = -1;  // the frame a closure began at in this block (its gate's end is after it)
    dsp::for_each_step(t, dsp::kTicksPerSixteenth, 0, 0, [&](std::uint32_t frame, std::uint64_t k) {
      const PatternData& p = bank_->pattern[cur_.load(std::memory_order_relaxed) % kBankPatterns];
      const unsigned len = length_of(p);
      const unsigned i = pos_ < len ? pos_ : 0u;
      const Step s = get_step(p, i);
      step_.store(static_cast<std::uint8_t>(i), std::memory_order_relaxed);
      pos_word_.store(kPosValid | static_cast<std::uint32_t>(k & 0xFFFFu) | (static_cast<std::uint32_t>(i) << 16) |
                          (static_cast<std::uint32_t>(cur_.load(std::memory_order_relaxed) & 7u) << 24),
                      std::memory_order_relaxed);
      pos_ = static_cast<std::uint8_t>(i + 1u);
      if (pos_ >= len) {  // the pattern's end: the next step is the top of this or of the requested pattern
        pos_ = 0;
        take_next();
        note_lap();
      }
      release(ctx, static_cast<std::uint16_t>(frame));  // a note never runs into the next step
      if (s.carrier != 0u && s.carrier <= kCarrierMax) {
        changed_ = true;
        (void)ctx.emit(unit_, dsp::param_change(Vocoder::CarSrc, static_cast<float>(s.carrier), static_cast<std::uint16_t>(frame)));
      }
      if (s.slice != 0u) {  // the SLICE lane, independent of the NOTE lane
        const std::uint32_t src = source();
        const unsigned n = src >> 8;
        const unsigned rd = src & 0xFFu;
        if (n != 0u && s.slice <= n && rd < 4u && map_ != nullptr) {
          const std::uint16_t f = static_cast<std::uint16_t>(frame);
          (void)ctx.emit(unit_, dsp::param_change(Vocoder::region_param(rd, Vocoder::RegionField::Start), static_cast<float>(map_->start[s.slice - 1u]), f));
          (void)ctx.emit(unit_, dsp::param_change(Vocoder::region_param(rd, Vocoder::RegionField::Len), static_cast<float>(map_->len[s.slice - 1u]), f));
          (void)ctx.emit(unit_, dsp::param_change(Vocoder::region_param(rd, Vocoder::RegionField::Trig), 1.0f, f));
          fired_ = static_cast<std::uint8_t>(rd);
          playing_slice_.store(s.slice, std::memory_order_relaxed);
        }
      }
      std::uint8_t notes[kChordMaxNotes] = {};
      Chord prev;
      const Chord* pp = nullptr;
      if (last_count_ != 0u) {
        for (unsigned n = 0; n < last_count_; ++n) {
          prev.notes[n] = last_[n];
        }
        prev.count = last_count_;
        pp = &prev;
      }
      const unsigned count = step_notes(s, key_.load(std::memory_order_relaxed), scale_.load(std::memory_order_relaxed), notes, pp);
      const std::uint8_t vel = step_velocity(s);
      for (unsigned n = 0; n < count; ++n) {
        (void)ctx.emit(unit_, dsp::note_on(notes[n], vel, Vocoder::kNoteLocal, static_cast<std::uint16_t>(frame)));
        last_[n] = notes[n];
      }
      if (count != 0u) {
        last_count_ = static_cast<std::uint8_t>(count);
        sounding_ = true;
        on_frame = static_cast<int>(frame);
      }
      if (mouth_step(ctx, t, static_cast<std::uint16_t>(frame), get_mouth(p, i), count != 0u)) {
        close_frame = static_cast<int>(frame);
      }
    });
    // The gate: 180 ticks after each step edge.
    dsp::for_each_step(t, dsp::kTicksPerSixteenth, kGateTicks, 0, [&](std::uint32_t frame, std::uint64_t) {
      if (sounding_ && (on_frame < 0 || static_cast<int>(frame) >= on_frame)) {
        release(ctx, static_cast<std::uint16_t>(frame));
      }
      if (closing_ && close_gate_ && (close_frame < 0 || static_cast<int>(frame) >= close_frame) && mouth_emit(ctx, TractParam::Close, 0.0f, static_cast<std::uint16_t>(frame))) {
        closing_ = false;
      }
    });
  }

 private:
  static constexpr std::uint32_t kGateTicks = 180;  // 75 % of a sixteenth (offset + swing < period)

  std::uint32_t source() const noexcept { return map_ != nullptr ? __atomic_load_n(&map_->source, __ATOMIC_ACQUIRE) : 0u; }
  void note_lap() noexcept { laps_.store(static_cast<std::uint8_t>(laps_.load(std::memory_order_relaxed) + 1u), std::memory_order_relaxed); }
  void take_next() noexcept {
    const std::uint8_t n = next_.exchange(kNoPattern, std::memory_order_relaxed);
    if (n < kBankPatterns) {
      cur_.store(n, std::memory_order_relaxed);
    }
  }
  // The tract unit's parameter ids the hook writes (TractUnit's enum, restated: the app layer's header does not include the unit's).
  enum class TractParam : std::uint16_t { Vowel = 0, Close = 4, Slide = 10 };
  // One Param event for the tract unit; false (nothing emitted) with no unit or while a strip pad is held.
  bool mouth_emit(o3sdk::dsp::BlockContext& ctx, TractParam id, float v, std::uint16_t frame) noexcept {
    if (tract_ == o3sdk::dsp::kNoUnit || mouth_hold_.load(std::memory_order_relaxed)) {
      return false;
    }
    mouth_dirty_ = true;
    (void)ctx.emit(tract_, o3sdk::dsp::param_change(static_cast<std::uint16_t>(id), v, frame));
    return true;
  }
  // The MOUTH lane's events for the step starting at `frame` (see the header comment). Returns true when it began a closure.
  bool mouth_step(o3sdk::dsp::BlockContext& ctx, const o3sdk::dsp::Transport& t, std::uint16_t frame, std::uint8_t m, bool has_note) noexcept {
    if (tract_ == o3sdk::dsp::kNoUnit || mouth_hold_.load(std::memory_order_relaxed)) {
      return false;
    }
    const bool closure = mouth_is_closure(m);
    if (closing_ && !closure && mouth_emit(ctx, TractParam::Close, 0.0f, frame)) {  // a closure with no gate end yet ends with its step
      closing_ = false;
    }
    if (mouth_is_vowel(m)) {
      if (mouth_gld(m)) {
        const float bpm = t.tempo_bpm();
        const float ms = bpm > 0.0f ? 15000.0f / bpm : 2000.0f;  // a sixteenth, in ms
        (void)mouth_emit(ctx, TractParam::Slide, slide_for(ms), frame);
        slid_ = true;
      } else if (slid_ && mouth_emit(ctx, TractParam::Slide, 0.0f, frame)) {
        slid_ = false;
      }
      (void)mouth_emit(ctx, TractParam::Vowel, mouth_vowel(m), frame);
    } else if (closure) {
      (void)mouth_emit(ctx, TractParam::Close, static_cast<float>(mouth_close(m)), frame);
      closing_ = true;
      close_gate_ = has_note;
      return true;
    }
    return false;
  }
  // A Stop: CLOSE 0 (not under a held strip pad: the pad owns it) and SLIDE 0 (always), once, when the line wrote since the last one.
  void mouth_stop(o3sdk::dsp::BlockContext& ctx) noexcept {
    if (!mouth_dirty_ || tract_ == o3sdk::dsp::kNoUnit) {
      return;
    }
    if (!mouth_hold_.load(std::memory_order_relaxed)) {
      (void)ctx.emit(tract_, o3sdk::dsp::param_change(static_cast<std::uint16_t>(TractParam::Close), 0.0f, 0));
    }
    (void)ctx.emit(tract_, o3sdk::dsp::param_change(static_cast<std::uint16_t>(TractParam::Slide), 0.0f, 0));
    mouth_dirty_ = false;
    closing_ = false;
    slid_ = false;
  }
  void release(o3sdk::dsp::BlockContext& ctx, std::uint16_t frame) noexcept {
    if (!sounding_) {
      return;
    }
    for (unsigned n = 0; n < last_count_; ++n) {
      (void)ctx.emit(unit_, Vocoder::note_off_local(last_[n], frame));
    }
    sounding_ = false;
  }

  o3sdk::dsp::UnitId unit_ = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId tract_ = o3sdk::dsp::kNoUnit;  // the MOUTH lane's unit (set_tract; main loop, once)
  const Bank* bank_ = nullptr;
  // Render context only (one thread): the next step of the playing pattern, the notes of the last step (kept after their off for SMOOTH), the flags.
  std::uint8_t pos_ = 0;
  std::uint8_t last_[kChordMaxNotes] = {};
  std::uint8_t last_count_ = 0;
  bool sounding_ = false;
  bool changed_ = false;
  bool closing_ = false;      // a closure is on (CLOSE 1..3 written, no CLOSE 0 yet)
  bool close_gate_ = false;   // ...and it ends at its gate (the step had a note) rather than at the step's end
  bool slid_ = false;         // SLIDE is above 0 (a GLD step wrote it, no plain vowel has written 0 since)
  bool mouth_dirty_ = false;  // the line wrote since the last Stop
  std::uint8_t fired_ = kNoReader;  // the reader a lane slice was fired on since the last Stop
  // Shared with the main loop (relaxed, single writer each): next_ and key_ / scale_ / picked_ are written by the main loop, cur_ and step_ by
  // the render context.
  std::atomic<std::uint8_t> next_{kNoPattern};
  static constexpr std::uint32_t kPosValid = 1u << 31;
  std::atomic<std::uint32_t> pos_word_{0};  // position(): the render context writes it, the main loop reads it
  std::atomic<std::uint8_t> cur_{0};
  std::atomic<std::uint8_t> step_{0};
  std::atomic<std::uint8_t> laps_{0};  // render context only writes it (note_lap)
  std::atomic<std::uint8_t> picked_{1};
  std::atomic<std::uint8_t> key_{0};
  std::atomic<std::uint8_t> scale_{0};
  std::atomic<bool> mouth_hold_{false};  // main loop writes (PERFORM's strip), render context reads
  // The SPEAKS slot's slices: map_ is set at init and written by the main loop only while its source is 0 (see slices_open); source is written
  // by the main loop, read by the render context. playing_slice_ is written by the render context.
  SliceMap* map_ = nullptr;
  std::atomic<std::uint8_t> playing_slice_{0};
};

}  // namespace o3_voc03er
