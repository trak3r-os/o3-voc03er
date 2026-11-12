// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The project file (docs/UI-DESIGN.md, "FILE"): one `project.o3a` in a project folder. Pure data and a codec over memory, no screen, no engine, no card: the
// FILE screen (U9c2) moves the bytes with Files::save / Files::load and applies a Project to the app. tests/project_test.cpp covers it.
//
// The file is the kit's chunk container (o3sdk/formats/chunk_file.h, type "VOCP"), one chunk per kind of data, each starting with its u16 version (major in the
// high byte): a reader refuses a newer major of a chunk it knows, reads a newer minor as far as it knows it, skips a chunk it does not know, and loads a file that
// lacks a chunk with that part at its defaults. A damaged file (CRC, cut short, a length that does not fit, a value out of range, a chunk twice) is refused
// whole and the destination is untouched (the checks run on a copy).
//
//   PTNS 1.0  u8 count, then per pattern u16 record length, u8 length (0..16, 0 reads as 16), 16 x u32 packed step (stepseq.h)
//   CHAI 1.0  u8 count (0..16), u8 loop (0/1), then count x (u8 pattern 0..7, u8 repeats 1..kChainRepeatMax): the SONG chain
//   ROUT 1.0  u8 speaks, u8 sounds (route.h values; the pair must be allowed), u8 live source (route::LiveSrc). Absent: the default route
//   MOUT 1.1  (TRACT V3, after PTNS; written only when a step has a mouth byte; 1.1 since V3b, 1.0 loads) u8 count (8), then per pattern its 16 mouth bytes (stepseq.h
//             mouth_valid: 0 FOLLOW, 1..9 the vowels, 10..12 the closures, 13 and 14 a WORD and a WOB with their number in bits 4..6, bit 7 GLD on a vowel). Absent: every step FOLLOWs. A count other than 8 or a byte that is not valid refuses the file
//   TRCT 1.0  (TRACT V3, after STAT; written when the capture holds the tract) 8 x f32: VOWEL, TONGUE, JAW, LIPS, GLIDE, SIZE, RES, HISS (kTractSaved, the unit's own
//             units). Not CLOSE (an action), MIX (the mode owns it) or SLIDE (the sequencer's). Absent: the unit's defaults. A value out of its range, or not a
//             number, refuses the file
//   PADS 1.0  (TP1, after STAT; written with STAT) u8 count (5 = modes.h kModeCount: BANDS, TALK, TUNE, ROBOT, TRACT), then per mode its layout: 3 x u8 row
//             (pads::Plays: 0..5, 11..14) and 3 x u16 strip id (pads::strip_id: tag << 8 | parameter; 0 for a row that is not a STRIP, a STRIP row has one). An id
//             this build does not know is not an error: the screen drops that row to the mode's default. Absent: the STAT layout serves BANDS, TALK, TUNE and ROBOT and
//             TRACT gets its default (A VOWEL, B TONGUE, C TIP). An older image skips the chunk and reads the BANDS layout from STAT. A count other than 5 refuses the file
//   SLOT 1.1  u8 count (4), then per slot u16 record length and u32 length (samples), u8 cuts, cuts x (u32 start, u32 len), u32 trim start, u32 trim end,
//             i8 speed (-1 0.5X, 0 1X, +1 2X), u8 reverse, i8 level (half-dB), u8 name length, the WAV's file name (no NUL),
//             then u8 values (appended: 1 = the trim, speed, reverse and level above were captured live and are restored; 0 or absent = a file from before, they are left alone),
//             then, appended in 1.1, u8 TRIG (SAMPLE's, 0 ONE SHOT, 1 GATE) and u8 CHOKE (1 = OFF, 0 = ON: the zero is the default). A record without them (1.0) is
//             ONE SHOT and CHOKE ON, whatever the previous project left
//   VALU 1.0  u16 count, then count x (u16 parameter id, f32 value): the engine's state, by id
//   STAT 1.1  u32 tempo (milli-BPM), u8 swing (0: none yet), u8 current pattern 0..7, i8 octave, u8 chord kind 0..3, u8 pads mode (0 pads, 1 keys) (all 1.0),
//             then, appended in 1.1: u8 voicing 0..3, u8 bend row 0/1, u8 bend range (2, 5 or 12), u8 FOLLOW 0/1 (PATTERN's), u8 preset name length 0..16 and the
//             name (matched against presets.h on load; length 0 or an unknown name is no preset: a name survives a reordered table, an index would not),
//             then the pads' layout, 5 bytes: rows A, B, C of the BANDS layout (a parameter strip is written as the row's default: STAT has no id for it) and two
//             bytes that older images used for the piano's `last` (written 0, read and ignored). A saved 6..10 is a retired piano value and loads as the plain value
//             of the same kind (pads::plays_from_saved); each row must offer its value (pads_logic.h `offered`).
//             A 1.0 STAT sets the 1.1 fields to the defaults of today (voicing SMOOTH, bend row off, range 2, FOLLOW off, no preset) and the layout to
//             pads::layout_from_legacy(pads mode, chord kind, false), not to what the previous project left. Absent STAT: those stay as they are.
//             On save the 1.0 fields chord kind, pads mode and bend row are derived from the layout (pads::legacy_fields) so an older reader finds something
//             sensible; on load of a 1.1 STAT the layout wins and those three are ignored
//
// Records inside PTNS and SLOT carry their own length, so a later minor appends fields to a record (read only when the record is long enough) and a later app
// may hold more patterns or slots than this one reads. The WAV data is not in this file; the slot's `name` says which WAV of the project folder it came from.
//
// The engine values (VALU) are the unit's parameters by id (Vocoder::Param, append-only), so a file written before an id existed still loads, and an id this
// build's table does not know (>= Vocoder::kParamCount) is ignored. Left out, as actions or as state another chunk owns:
//   Freeze (9)                         a performance hold
//   Loop n Rec, Play, Role (10..21)    recording, transport, and the role the route gives a slot
//   Loop n Speed, Dir, Level (22..33)  the SLOT record
//   CarSrc (34), ModSrc (47)           the ROUT chunk
//   R n Start, Len, Trig (49..60)      the last slice played; the cuts are in SLOT
//   Load (61)                          an action
// Every other id is state, 62 and 63 (SUB, TILT) included once the unit has them. The unit's table has at most 64 ids (the kit's limit).
//
// Memory: a Project is plain data whose every default is zero (route::Route's default is not, so the route is kept as raw bytes and project_route() maps zero to
// the default): it can live in `O3SDK_BIG Project g_project;`. read_project stages the file on the stack (sizeof(Project), asserted below).
// Contexts: main loop. No heap, no float arithmetic.

#include <o3_voc03er/modes.h>
#include <o3_voc03er/presets.h>
#include <o3_voc03er/route.h>
#include <o3_voc03er/stepseq.h>
#include <o3_voc03er/units/tract/tract.h>
#include <o3_voc03er/units/vocoder/vocoder.h>
#include <o3sdk/formats/chunk_file.h>
#include <o3sdk/hal/save_guard.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../src/pads_logic.h"
#include "../../src/sample_logic.h"

namespace o3_voc03er {

// --- the SONG chain: up to 16 entries of a pattern letter and a repeat count, plus LOOP CHAIN. The SONG slice (U9) uses this struct as it is. ---
inline constexpr unsigned kChainMax = 16;
inline constexpr unsigned kChainRepeatMax = 64;

struct ChainEntry {
  std::uint8_t pattern = 0;  // 0..7: A..H
  std::uint8_t repeats = 0;  // 1..kChainRepeatMax (0 only in an unused entry)
};

struct Chain {
  std::uint8_t count = 0;  // the entries in use
  std::uint8_t loop = 0;   // LOOP CHAIN: 1 plays the chain again from its first entry
  ChainEntry entry[kChainMax];
};

// --- a slot ---
inline constexpr std::size_t kWavNameMax = o3sdk::hal::storage::save::kNameMax;  // the kit's limit for a WAV file's name
static_assert(kWavNameMax == 31, "the file's name field is sized for the kit's name limit");

struct ProjectSlot {
  sample::SliceTable cuts;       // length = the samples of the slot's WAV (0: nothing in the slot), the cuts it was sliced with
  std::uint32_t trim_start = 0;  // the trim brackets; both 0: not moved (the slot's own ends)
  std::uint32_t trim_end = 0;
  std::int8_t speed = 0;         // relative to 1X so that zero is the default: -1 0.5X, 0 1X, +1 2X (sample::kSpeedNames index - 1)
  std::uint8_t reverse = 0;      // 1: DIR REV
  std::int8_t level_half = 0;    // half-dB, -48..+24 (the unit's L n LEVEL is -24..+12 dB); 0 is 0 dB
  std::uint8_t name_len = 0;     // 0: no WAV (an empty slot, or a take that was not saved)
  char name[kWavNameMax + 1] = {};  // the WAV's file name in the project folder, name_len bytes, printable, no '/' or '\\'
  std::uint8_t values = 0;       // 1: trim_*, speed, reverse and level_half were captured at save and are put back on load; 0: they are not (a file from before)
  std::uint8_t gate = 0;         // SAMPLE's TRIG for this slot: 1 GATE, 0 ONE SHOT (the default); kept whether or not the slot holds audio (1.1)
  std::uint8_t choke_off = 0;    // SAMPLE's CHOKE: 1 OFF, 0 ON (the default) (1.1)
};

inline constexpr std::size_t kPresetNameMax = 16;  // the longest preset name a file may hold (presets.h's are at most 8)
inline constexpr unsigned kLayoutBytes = 5;        // STAT's layout: rows A, B, C of the BANDS layout, then two bytes kept for older readers (written 0)

// TRCT's parameters, in file order (TractUnit::Param ids), and the range a file may hold for each (the unit's own descriptors; tests/project_test.cpp compares them).
inline constexpr unsigned kTractSaved = 8;
inline constexpr std::uint16_t kTractSavedId[kTractSaved] = {TractUnit::Vowel, TractUnit::Tongue, TractUnit::Jaw,  TractUnit::Lips,
                                                              TractUnit::Glide, TractUnit::Size,   TractUnit::Res, TractUnit::Hiss};
inline constexpr float kTractSavedMin[kTractSaved] = {0.0f, -1.0f, -1.0f, -1.0f, 5.0f, 0.0f, 0.0f, 0.0f};
inline constexpr float kTractSavedMax[kTractSaved] = {1.0f, 1.0f, 1.0f, 1.0f, 500.0f, 4.0f, 1.0f, 1.0f};

inline constexpr unsigned kEngineIds = 64;  // the unit's table is at most 64 parameters (the kit's limit): the ids a Project can hold

// What a project restores. All defaults are zero.
struct Project {
  Bank bank = {};                                  // the eight patterns
  Chain chain;                                     // the SONG chain
  std::uint8_t speaks = 0;                         // the route as the unit's values; sounds 0 (FOLLOW) is not a route: none stored, the default route
  std::uint8_t sounds = 0;
  std::uint8_t live_src = 0;                       // route::LiveSrc
  ProjectSlot slot[route::kSlots];
  float engine[kEngineIds] = {};                   // the engine's state by parameter id, valid where `engine_set` has the bit
  std::uint64_t engine_set = 0;
  std::uint8_t stat = 0;                           // 1 when the file has STAT (the fields below are then valid), else they are zero and not used
  std::uint32_t tempo_mbpm = 0;
  std::uint8_t swing = 0;
  std::uint8_t pattern = 0;                        // the pattern shown
  std::int8_t octave = 0;
  std::uint8_t chord = 0;
  std::uint8_t keys = 0;
  // STAT 1.1 (valid with `stat`; a 1.0 file gets the defaults of today, see the header; none of them is zero by default, so a Project that is to be written
  // takes them from file::default_stat or a capture):
  std::uint8_t voicing = 0;                        // 0..3 (the Voicing enum; 3 is SMOOTH)
  std::uint8_t bend_row = 0;                       // 0/1: C is BEND (derived from the layout on save, ignored on load)
  std::uint8_t bend_range = 0;                     // 2, 5 or 12 semitones
  std::uint8_t follow = 0;                         // 0/1: PATTERN's FOLLOW
  std::uint8_t preset_len = 0;                     // 0: no preset
  char preset[kPresetNameMax + 1] = {};            // the factory sound's name, preset_len bytes, printable
  std::uint8_t layout[kLayoutBytes] = {};          // STAT's bytes: pads::Plays (saved values) of rows A, B, C, then two spare
  // PADS (TP1): 1 when the five layouts below are held (a file without PADS, and a zero Project, are 0: the STAT layout and the TRACT default serve).
  std::uint8_t pads = 0;
  std::uint8_t pad_row[kModeCount][pads::kRows] = {};            // pads::Plays per mode and row
  std::uint16_t pad_strip[kModeCount][pads::kRows] = {};         // pads::strip_id of a STRIP row, else 0
  // TRCT (V3): 1 when the tract's values below are held (a file without TRCT, and a zero Project, are 0: the unit's defaults, which are not zero).
  std::uint8_t trct = 0;
  float tract[kTractSaved] = {};                   // by kTractSavedId, the unit's own units
};

// STAT's layout (the BANDS layout; a file from before this image's pads kept one layout for every mode), and the way back: a strip row is written as the
// row's default value, the two spare bytes as 0.
inline pads::Layout project_layout(const Project& p) noexcept {
  pads::Layout l;
  for (unsigned i = 0; i < pads::kRows; ++i) {
    l.row[i] = pads::plays_from_saved(p.layout[i]);
  }
  return l;
}
inline void set_project_layout(Project& p, const pads::Layout& l) noexcept {
  const pads::Layout def;
  for (unsigned i = 0; i < pads::kRows; ++i) {
    p.layout[i] = static_cast<std::uint8_t>(l.row[i] == pads::Plays::Strip ? def.row[i] : l.row[i]);
  }
  p.layout[3] = 0;
  p.layout[4] = 0;
}

// The PADS chunk's layout of a mode (only with `p.pads`), and the way back; set_project_pads also writes STAT's layout from mode 0 (BANDS).
inline pads::Layout project_pads_layout(const Project& p, unsigned mode) noexcept {
  pads::Layout l;
  if (mode < kModeCount) {
    for (unsigned r = 0; r < pads::kRows; ++r) {
      l.row[r] = static_cast<pads::Plays>(p.pad_row[mode][r]);
      l.strip[r] = p.pad_strip[mode][r];
    }
  }
  return l;
}
inline void set_project_pads(Project& p, const pads::Layout (&l)[kModeCount]) noexcept {
  for (unsigned m = 0; m < kModeCount; ++m) {
    for (unsigned r = 0; r < pads::kRows; ++r) {
      p.pad_row[m][r] = static_cast<std::uint8_t>(l[m].row[r]);
      p.pad_strip[m][r] = l[m].row[r] == pads::Plays::Strip ? l[m].strip[r] : pads::kNoStrip;
    }
  }
  p.pads = 1;
  set_project_layout(p, l[0]);
}

// The preset a project names: its index in presets.h, or -1 for none (no name, or a name this build does not know).
inline int project_preset_index(const Project& p) noexcept {
  if (p.preset_len == 0u || p.preset_len > kPresetNameMax) {
    return -1;
  }
  for (std::size_t i = 0; i < kPresetCount; ++i) {
    const char* n = kPresets[i].name;
    if (std::strlen(n) == p.preset_len && std::memcmp(n, p.preset, p.preset_len) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}
// Names the preset `index` of presets.h (an index past the table: none).
inline void set_project_preset(Project& p, unsigned index) noexcept {
  p.preset_len = 0;
  std::memset(p.preset, 0, sizeof p.preset);
  if (index < kPresetCount) {
    const std::size_t n = std::strlen(kPresets[index].name);
    p.preset_len = static_cast<std::uint8_t>(n > kPresetNameMax ? kPresetNameMax : n);
    std::memcpy(p.preset, kPresets[index].name, p.preset_len);
  }
}

// The route a project holds (the default route when none is stored).
constexpr route::Route project_route(const Project& p) noexcept {
  route::Route r;
  if (p.sounds != 0u) {
    r.speaks = p.speaks;
    r.sounds = p.sounds;
  }
  return r;
}
inline void set_project_route(Project& p, route::Route r) noexcept {
  p.speaks = r.speaks;
  p.sounds = r.sounds;
}

namespace project_detail {

constexpr bool in(unsigned v, unsigned lo, unsigned hi) noexcept { return v >= lo && v <= hi; }

}  // namespace project_detail

// An engine id the project keeps (state), see the header comment. Not a test of the id being in the unit's table.
constexpr bool engine_id_is_state(unsigned id) noexcept {
  namespace d = project_detail;
  return id < kEngineIds && id != Vocoder::Freeze && !d::in(id, Vocoder::Loop1Rec, Vocoder::Loop4Level) && id != Vocoder::CarSrc && id != Vocoder::ModSrc &&
         !d::in(id, Vocoder::R1Start, Vocoder::Load);
}
static_assert(Vocoder::kParamCount <= kEngineIds, "a project holds the ids of a 64-parameter table; widen kEngineIds and the file's VALU limit together");
static_assert(Vocoder::Loop4Level + 1 == Vocoder::CarSrc && Vocoder::Freeze + 1 == Vocoder::Loop1Rec && Vocoder::R1Start == Vocoder::LiveCh + 1 && Vocoder::Load < Vocoder::kParamCount,
              "engine_id_is_state is written against this order: revisit it when the parameter table changes");

// An id this build writes and reads: state and in the unit's table.
constexpr bool engine_id_known(unsigned id) noexcept { return id < Vocoder::kParamCount && engine_id_is_state(id); }

inline bool project_has_engine(const Project& p, unsigned id) noexcept { return id < kEngineIds && ((p.engine_set >> id) & 1u) != 0u; }
// Sets an engine value (a non-finite value or an id that is not state is refused).
inline bool project_set_engine(Project& p, unsigned id, float v) noexcept {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof bits);
  if (!engine_id_is_state(id) || (bits & 0x7F800000u) == 0x7F800000u) {
    return false;
  }
  p.engine[id] = v;
  p.engine_set |= std::uint64_t{1} << id;
  return true;
}

// --- the file's size ---
namespace project_detail {

constexpr std::size_t chunk_bytes(std::size_t payload) noexcept { return 8 + payload + (payload & 1u); }
inline constexpr std::size_t kSlotRecMax = 4 + 1 + 8 * sample::kMaxSlices + 8 + 4 + kWavNameMax + 1 + 2;
inline constexpr std::size_t kPtnsMax = 2 + 1 + kBankPatterns * (2 + 1 + 4 * kPatternSteps);
inline constexpr std::size_t kChaiMax = 2 + 2 + 2 * kChainMax;
inline constexpr std::size_t kRoutMax = 2 + 3;
inline constexpr std::size_t kMoutMax = 2 + 1 + kBankPatterns * kPatternSteps;
inline constexpr std::size_t kTrctMax = 2 + 4 * kTractSaved;
inline constexpr std::size_t kSlotMax = 2 + 1 + route::kSlots * (2 + kSlotRecMax);
inline constexpr std::size_t kValuMax = 2 + 2 + 6 * kEngineIds;
inline constexpr std::size_t kStatMax = 2 + 4 + 5 + 4 + 1 + kPresetNameMax + kLayoutBytes;
inline constexpr std::size_t kPadsMax = 2 + 1 + kModeCount * pads::kRows * 3;
inline constexpr std::size_t kWorst = o3sdk::formats::chunk::kHeaderBytes + o3sdk::formats::chunk::kCrcChunkBytes + chunk_bytes(kPtnsMax) + chunk_bytes(kChaiMax) +
                                      chunk_bytes(kRoutMax) + chunk_bytes(kSlotMax) + chunk_bytes(kValuMax) + chunk_bytes(kStatMax) + chunk_bytes(kMoutMax) + chunk_bytes(kTrctMax) + chunk_bytes(kPadsMax);

}  // namespace project_detail

// The buffer for a project file: the worst case (every chunk full: 8 patterns, 16 chain entries, 4 slots of 24 cuts and a 31-byte name, 64 engine values) is
// kWorst bytes, and the rest is room for chunks a later app adds.
inline constexpr std::size_t kProjectFileMax = 2560;
static_assert(project_detail::kWorst <= kProjectFileMax, "the worst-case project file must fit its buffer");
static_assert(kProjectFileMax % 4 == 0, "a word-aligned buffer of this size is whole words");
static_assert(sizeof(Project) <= 2560, "read_project stages a Project on the stack");
static_assert(Project{}.chain.count == 0u && Project{}.engine_set == 0u && Project{}.stat == 0u && Project{}.tempo_mbpm == 0u && Project{}.sounds == 0u && Project{}.slot[0].cuts.length == 0u && Project{}.bank.pattern[0].length == 0u,
              "every default of a Project is zero, so it can sit in the SDRAM's bss (O3SDK_BIG)");
inline constexpr const char* kProjectFile = "project.o3a";

// --- the checks, shared by the writer and the reader: what the writer would not write and the reader would not accept ---
inline bool chain_valid(const Chain& c) noexcept {
  if (c.count > kChainMax || c.loop > 1u) {
    return false;
  }
  for (unsigned i = 0; i < c.count; ++i) {
    if (c.entry[i].pattern >= kBankPatterns || c.entry[i].repeats < 1u || c.entry[i].repeats > kChainRepeatMax) {
      return false;
    }
  }
  return true;
}

inline bool slot_valid(const ProjectSlot& s) noexcept {
  const sample::SliceTable& t = s.cuts;
  if (t.length > Loop::kMaxSamples || t.count > sample::kMaxSlices || (t.length == 0u && t.count != 0u)) {
    return false;
  }
  for (unsigned i = 0; i < t.count; ++i) {
    if (t.cut[i].len < 1u || t.cut[i].start > t.length || t.cut[i].len > t.length - t.cut[i].start) {
      return false;
    }
  }
  const bool trimmed = s.trim_start != 0u || s.trim_end != 0u;
  if (trimmed && !(s.trim_start < s.trim_end && s.trim_end <= t.length)) {
    return false;
  }
  if (s.speed < -1 || s.speed > 1 || s.reverse > 1u || s.level_half < -48 || s.level_half > 24 || s.name_len > kWavNameMax || s.gate > 1u || s.choke_off > 1u) {
    return false;
  }
  for (unsigned i = 0; i < s.name_len; ++i) {
    const char ch = s.name[i];
    if (ch < 0x20 || ch > 0x7E || ch == '/' || ch == '\\') {
      return false;
    }
  }
  return true;
}

inline bool project_route_valid(const Project& p) noexcept {
  if (p.live_src >= route::kLiveSrcCount) {
    return false;
  }
  return p.sounds == 0u ? p.speaks == 0u : route::allowed(route::Route{p.speaks, p.sounds});
}

// The tract's saved values: each a number inside the unit's range (SIZE a whole number). Checked only with `trct`.
inline bool trct_valid(const Project& p) noexcept {
  for (unsigned i = 0; i < kTractSaved; ++i) {
    const float v = p.tract[i];
    if (!(v >= kTractSavedMin[i] && v <= kTractSavedMax[i])) {  // false for a NaN too
      return false;
    }
    if (kTractSavedId[i] == TractUnit::Size && static_cast<float>(static_cast<int>(v)) != v) {
      return false;
    }
  }
  return true;
}

// A stored pattern: the length byte 0..16 (0 reads as 16), every step in range, every mouth byte valid (stepseq.h).
inline bool pattern_valid(const PatternData& p) noexcept {
  if (p.length > kPatternSteps) {
    return false;
  }
  for (unsigned i = 0; i < kPatternSteps; ++i) {
    if (!mouth_valid(get_mouth(p, i))) {
      return false;
    }
  }
  for (const std::uint32_t w : p.step) {
    if (!step_valid(unpack_step(w)) || pack_step(unpack_step(w)) != w) {
      return false;
    }
  }
  return true;
}

// STAT's fields: the tempo the engine takes (20 to 400 BPM), a pattern of the bank, an octave PERFORM can hold (clamped there), a chord kind and a mode flag.
inline constexpr std::uint32_t kStatTempoMin = 20000u;
inline constexpr std::uint32_t kStatTempoMax = 400000u;
// The 1.1 fields: a voicing, two flags, a range PERFORM offers, a printable preset name and a layout whose every row offers its value (a saved piano value
// counts as the plain one; a strip has no id in STAT, so it is not one of them).
inline bool stat_layout_valid(const Project& p) noexcept {
  for (unsigned i = 0; i < kLayoutBytes; ++i) {
    if (!pads::valid_saved(p.layout[i])) {
      return false;
    }
  }
  const pads::Layout l = project_layout(p);
  for (unsigned r = 0; r < pads::kRows; ++r) {
    if (l.row[r] == pads::Plays::Strip || !pads::offered(static_cast<pads::Row>(r), l.row[r])) {
      return false;
    }
  }
  return true;
}
// PADS: every mode's rows offer their value, a STRIP row has an id and no other row has one. An id this build does not know passes (the screen drops it).
inline bool pads_valid(const Project& p) noexcept {
  if (p.pads == 0u) {
    return true;
  }
  if (p.pads != 1u) {
    return false;
  }
  for (unsigned m = 0; m < kModeCount; ++m) {
    for (unsigned r = 0; r < pads::kRows; ++r) {
      const std::uint8_t b = p.pad_row[m][r];
      if (!(b <= 5u || (b >= 11u && b <= 14u))) {
        return false;
      }
      const pads::Plays v = static_cast<pads::Plays>(b);
      if (!pads::offered(static_cast<pads::Row>(r), v) || (v == pads::Plays::Strip) != (p.pad_strip[m][r] != pads::kNoStrip)) {
        return false;
      }
    }
  }
  return true;
}
inline bool stat_valid(const Project& p) noexcept {
  if (p.preset_len > kPresetNameMax) {
    return false;
  }
  for (unsigned i = 0; i < p.preset_len; ++i) {
    if (p.preset[i] < 0x20 || p.preset[i] > 0x7E) {
      return false;
    }
  }
  return p.stat == 1u && p.tempo_mbpm >= kStatTempoMin && p.tempo_mbpm <= kStatTempoMax && p.pattern < kBankPatterns && p.octave >= -8 && p.octave <= 8 && p.chord < 4u &&
         p.keys < 2u && p.voicing <= 3u && p.bend_row <= 1u && (p.bend_range == 2u || p.bend_range == 5u || p.bend_range == 12u) && p.follow <= 1u && stat_layout_valid(p);
}

inline bool project_valid(const Project& p) noexcept {
  if (!chain_valid(p.chain) || !project_route_valid(p) || (p.stat != 0u && !stat_valid(p)) || !pads_valid(p) || p.trct > 1u || (p.trct == 1u && !trct_valid(p))) {
    return false;
  }
  for (const PatternData& pd : p.bank.pattern) {
    if (!pattern_valid(pd)) {
      return false;
    }
  }
  for (const ProjectSlot& s : p.slot) {
    if (!slot_valid(s)) {
      return false;
    }
  }
  for (unsigned id = 0; id < kEngineIds; ++id) {
    if (project_has_engine(p, id) && !engine_id_is_state(id)) {
      return false;
    }
  }
  return true;
}

// --- the file ---
inline constexpr std::uint16_t kProjectChunkVersion = 0x0100;
inline constexpr std::uint16_t kMoutVersion = 0x0101;  // MOUT 1.1 (V3b): WORD and WOB values may be in it; a 0x0100 file loads (any minor of major 1 does)
inline constexpr std::uint16_t kSlotVersion = 0x0101;  // SLOT 1.1: TRIG and CHOKE after `values`
inline constexpr std::uint16_t kStatVersion = 0x0101;  // STAT 1.1: voicing, bend row and range, FOLLOW, preset, layout
inline constexpr std::uint16_t kPadsVersion = 0x0100;  // PADS 1.0: the five layouts (TP1)

namespace project_detail {

constexpr std::uint16_t slot_rec_bytes(const ProjectSlot& s) noexcept {
  return static_cast<std::uint16_t>(4 + 1 + 8u * s.cuts.count + 8 + 4 + s.name_len + 1 + 2);
}

// The tail of a record: skips what a newer minor appended; false when the record's known fields ran past its length.
inline bool rec_done(o3sdk::formats::chunk::Fields& f, std::size_t left_at_start, std::size_t rec_len) noexcept {
  const std::size_t used = left_at_start - f.left();
  if (!f.ok() || used > rec_len) {
    return false;
  }
  f.skip(rec_len - used);
  return f.ok();
}

// Opens a known chunk's payload: the version's major must be this one.
// `minor` (when given) gets the file's minor version.
inline bool open_chunk(const o3sdk::formats::chunk::Chunk& c, o3sdk::formats::chunk::Fields& f, unsigned* minor = nullptr) noexcept {
  f = o3sdk::formats::chunk::Fields(c);
  const std::uint16_t v = f.u16();
  if (minor != nullptr) {
    *minor = v & 0xFFu;
  }
  return f.ok() && (v >> 8) == (kProjectChunkVersion >> 8);
}

inline bool read_ptns(const o3sdk::formats::chunk::Chunk& c, Project& got) noexcept {
  o3sdk::formats::chunk::Fields f(c);
  if (!open_chunk(c, f)) {
    return false;
  }
  const unsigned count = f.u8();
  for (unsigned i = 0; i < count; ++i) {
    const std::size_t rec_len = f.u16();
    const std::size_t start = f.left();
    if (!f.ok()) {
      return false;
    }
    PatternData pd = {};
    if (i < kBankPatterns) {  // a later app's extra patterns are skipped by their record length
      pd.length = f.u8();
      for (std::uint32_t& w : pd.step) {
        w = f.u32();
      }
      if (!f.ok() || !pattern_valid(pd)) {
        return false;
      }
    }
    if (!rec_done(f, start, rec_len)) {
      return false;
    }
    if (i < kBankPatterns) {  // the steps and the length only: MOUT may have been read before this chunk
      got.bank.pattern[i].length = pd.length;
      std::memcpy(got.bank.pattern[i].step, pd.step, sizeof pd.step);
    }
  }
  return f.ok();
}

inline bool read_mout(const o3sdk::formats::chunk::Chunk& c, Project& got) noexcept {
  o3sdk::formats::chunk::Fields f(c);
  if (!open_chunk(c, f)) {
    return false;
  }
  if (f.u8() != kBankPatterns || !f.ok()) {
    return false;
  }
  for (PatternData& pd : got.bank.pattern) {
    for (std::uint8_t& m : pd.mouth) {
      m = f.u8();
      if (!f.ok() || !mouth_valid(m)) {
        return false;
      }
    }
  }
  return f.ok();
}

inline bool read_trct(const o3sdk::formats::chunk::Chunk& c, Project& got) noexcept {
  o3sdk::formats::chunk::Fields f(c);
  if (!open_chunk(c, f)) {
    return false;
  }
  for (float& v : got.tract) {
    v = f.f32();
  }
  got.trct = 1;
  return f.ok() && trct_valid(got);
}

inline bool read_pads(const o3sdk::formats::chunk::Chunk& c, Project& got) noexcept {
  o3sdk::formats::chunk::Fields f(c);
  if (!open_chunk(c, f)) {
    return false;
  }
  if (f.u8() != kModeCount) {
    return false;
  }
  for (unsigned m = 0; m < kModeCount; ++m) {
    for (unsigned r = 0; r < pads::kRows; ++r) {
      got.pad_row[m][r] = f.u8();
    }
    for (unsigned r = 0; r < pads::kRows; ++r) {
      got.pad_strip[m][r] = f.u16();
    }
  }
  got.pads = 1;
  return f.ok() && pads_valid(got);
}

inline bool read_chai(const o3sdk::formats::chunk::Chunk& c, Project& got) noexcept {
  o3sdk::formats::chunk::Fields f(c);
  if (!open_chunk(c, f)) {
    return false;
  }
  Chain ch;
  ch.count = f.u8();
  ch.loop = f.u8();
  if (!f.ok() || ch.count > kChainMax) {
    return false;
  }
  for (unsigned i = 0; i < ch.count; ++i) {
    ch.entry[i].pattern = f.u8();
    ch.entry[i].repeats = f.u8();
  }
  if (!f.ok() || !chain_valid(ch)) {
    return false;
  }
  got.chain = ch;
  return true;
}

inline bool read_rout(const o3sdk::formats::chunk::Chunk& c, Project& got) noexcept {
  o3sdk::formats::chunk::Fields f(c);
  if (!open_chunk(c, f)) {
    return false;
  }
  got.speaks = f.u8();
  got.sounds = f.u8();
  got.live_src = f.u8();
  return f.ok() && project_route_valid(got) && got.sounds != 0u;
}

inline bool read_slot(const o3sdk::formats::chunk::Chunk& c, Project& got) noexcept {
  o3sdk::formats::chunk::Fields f(c);
  unsigned minor = 0;
  if (!open_chunk(c, f, &minor)) {
    return false;
  }
  const unsigned count = f.u8();
  for (unsigned i = 0; i < count; ++i) {
    const std::size_t rec_len = f.u16();
    const std::size_t start = f.left();
    if (!f.ok()) {
      return false;
    }
    ProjectSlot s;
    s.cuts.length = f.u32();
    const unsigned n = f.u8();
    if (!f.ok() || n > sample::kMaxSlices) {
      return false;
    }
    s.cuts.count = static_cast<std::uint8_t>(n);
    for (unsigned k = 0; k < n; ++k) {
      s.cuts.cut[k].start = f.u32();
      s.cuts.cut[k].len = f.u32();
    }
    s.trim_start = f.u32();
    s.trim_end = f.u32();
    s.speed = static_cast<std::int8_t>(f.u8());
    s.reverse = f.u8();
    s.level_half = static_cast<std::int8_t>(f.u8());
    s.name_len = f.u8();
    if (!f.ok() || s.name_len > kWavNameMax) {
      return false;
    }
    for (unsigned k = 0; k < s.name_len; ++k) {
      s.name[k] = static_cast<char>(f.u8());
    }
    if (!f.ok()) {
      return false;
    }
    if (minor >= 1u && start - f.left() + 3u > rec_len) {
      return false;  // a 1.1 record carries values, TRIG and CHOKE: one that is shorter is damaged
    }
    if (start - f.left() < rec_len) {
      s.values = f.u8() == 1u ? 1u : 0u;  // appended in this minor: a record from before ends at the name; any other byte is not a flag
    }
    if (minor >= 1u && start - f.left() + 2u <= rec_len) {
      s.gate = f.u8();  // appended in 1.1: a 1.0 record ends at `values` and keeps the defaults (ONE SHOT, CHOKE ON)
      s.choke_off = f.u8();
    }
    if (!f.ok() || !slot_valid(s) || !rec_done(f, start, rec_len)) {
      return false;
    }
    if (i < route::kSlots) {
      got.slot[i] = s;
    }
  }
  return f.ok();
}

inline bool read_stat(const o3sdk::formats::chunk::Chunk& c, Project& got) noexcept {
  o3sdk::formats::chunk::Fields f(c);
  unsigned minor = 0;
  if (!open_chunk(c, f, &minor)) {
    return false;
  }
  got.tempo_mbpm = f.u32();
  got.swing = static_cast<std::uint8_t>(f.u8());
  got.pattern = static_cast<std::uint8_t>(f.u8());
  got.octave = static_cast<std::int8_t>(f.u8());
  got.chord = static_cast<std::uint8_t>(f.u8());
  got.keys = static_cast<std::uint8_t>(f.u8());
  got.stat = 1;
  if (!f.ok()) {
    return false;
  }
  if (minor >= 1u) {  // 1.1 (a later minor's own fields follow and are not read)
    got.voicing = f.u8();
    got.bend_row = f.u8();
    got.bend_range = f.u8();
    got.follow = f.u8();
    got.preset_len = f.u8();
    if (!f.ok() || got.preset_len > kPresetNameMax) {
      return false;
    }
    for (unsigned k = 0; k < got.preset_len; ++k) {
      got.preset[k] = static_cast<char>(f.u8());
    }
    for (std::uint8_t& b : got.layout) {
      b = f.u8();
    }
    return f.ok() && stat_valid(got);
  }
  // 1.0: the defaults of today, and the layout the old mode and chord kind meant (not what the previous project left).
  got.voicing = 3;
  got.bend_row = 0;
  got.bend_range = 2;
  got.follow = 0;
  got.preset_len = 0;
  if (got.chord < 4u) {
    set_project_layout(got, pads::layout_from_legacy(got.keys == 1u, static_cast<ChordKind>(got.chord), false));
  }
  return stat_valid(got);
}

inline bool read_valu(const o3sdk::formats::chunk::Chunk& c, Project& got) noexcept {
  o3sdk::formats::chunk::Fields f(c);
  if (!open_chunk(c, f)) {
    return false;
  }
  const unsigned count = f.u16();
  for (unsigned i = 0; i < count; ++i) {
    const unsigned id = f.u16();
    const float v = f.f32();
    if (!f.ok()) {
      return false;
    }
    if (engine_id_known(id) && !project_set_engine(got, id, v)) {
      return false;  // a value that is not a number: a damaged file
    }
  }
  return true;
}

}  // namespace project_detail

// The project as a file in `out` (`cap` bytes, kProjectFileMax is enough): its size, or 0 when the project is not valid (project_valid) or does not fit.
// Every pattern, the chain, the slots and the engine's state are written; the route only when it is one (a zero Project has none).
inline std::size_t write_project(const Project& p, std::uint8_t* out, std::size_t cap) noexcept {
  namespace chunk = o3sdk::formats::chunk;
  if (out == nullptr || !project_valid(p)) {
    return 0;
  }
  chunk::Writer w(out, cap, "VOCP");
  const std::uint16_t ver = kProjectChunkVersion;
  if (!w.begin("PTNS") || !w.u16(ver) || !w.u8(static_cast<std::uint8_t>(kBankPatterns))) {
    return 0;
  }
  for (const PatternData& pd : p.bank.pattern) {
    if (!w.u16(1 + 4 * kPatternSteps) || !w.u8(pd.length)) {
      return 0;
    }
    for (const std::uint32_t word : pd.step) {
      if (!w.u32(word)) {
        return 0;
      }
    }
  }
  bool any_mouth = false;
  for (const PatternData& pd : p.bank.pattern) {
    any_mouth = any_mouth || mouths_used(pd) != 0u;
  }
  if (!w.end()) {
    return 0;
  }
  if (any_mouth) {
    if (!w.begin("MOUT") || !w.u16(kMoutVersion) || !w.u8(static_cast<std::uint8_t>(kBankPatterns))) {
      return 0;
    }
    for (const PatternData& pd : p.bank.pattern) {
      for (unsigned i = 0; i < kPatternSteps; ++i) {
        if (!w.u8(get_mouth(pd, i))) {
          return 0;
        }
      }
    }
    if (!w.end()) {
      return 0;
    }
  }
  if (!w.begin("CHAI") || !w.u16(ver) || !w.u8(p.chain.count) || !w.u8(p.chain.loop)) {
    return 0;
  }
  for (unsigned i = 0; i < p.chain.count; ++i) {
    if (!w.u8(p.chain.entry[i].pattern) || !w.u8(p.chain.entry[i].repeats)) {
      return 0;
    }
  }
  if (!w.end()) {
    return 0;
  }
  if (p.sounds != 0u && (!w.begin("ROUT") || !w.u16(ver) || !w.u8(p.speaks) || !w.u8(p.sounds) || !w.u8(p.live_src) || !w.end())) {
    return 0;
  }
  if (!w.begin("SLOT") || !w.u16(kSlotVersion) || !w.u8(static_cast<std::uint8_t>(route::kSlots))) {
    return 0;
  }
  for (const ProjectSlot& s : p.slot) {
    if (!w.u16(project_detail::slot_rec_bytes(s)) || !w.u32(s.cuts.length) || !w.u8(s.cuts.count)) {
      return 0;
    }
    for (unsigned k = 0; k < s.cuts.count; ++k) {
      if (!w.u32(s.cuts.cut[k].start) || !w.u32(s.cuts.cut[k].len)) {
        return 0;
      }
    }
    if (!w.u32(s.trim_start) || !w.u32(s.trim_end) || !w.u8(static_cast<std::uint8_t>(s.speed)) || !w.u8(s.reverse) ||
        !w.u8(static_cast<std::uint8_t>(s.level_half)) || !w.u8(s.name_len) || !w.bytes(s.name, s.name_len) ||
        !w.u8(s.values != 0u ? 1u : 0u) || !w.u8(s.gate) || !w.u8(s.choke_off)) {
      return 0;
    }
  }
  if (!w.end() || !w.begin("VALU") || !w.u16(ver)) {
    return 0;
  }
  unsigned n = 0;
  for (unsigned id = 0; id < kEngineIds; ++id) {
    n += project_has_engine(p, id) && engine_id_known(id) ? 1u : 0u;
  }
  if (!w.u16(static_cast<std::uint16_t>(n))) {
    return 0;
  }
  for (unsigned id = 0; id < kEngineIds; ++id) {
    if (project_has_engine(p, id) && engine_id_known(id) && (!w.u16(static_cast<std::uint16_t>(id)) || !w.f32(p.engine[id]))) {
      return 0;
    }
  }
  if (!w.end()) {
    return 0;
  }
  if (p.stat != 0u) {
    // chord, keys and bend_row are derived from the layout by the capture (pads::legacy_fields); the layout below is what a 1.1 load uses.
    if (!w.begin("STAT") || !w.u16(kStatVersion) || !w.u32(p.tempo_mbpm) || !w.u8(p.swing) || !w.u8(p.pattern) || !w.u8(static_cast<std::uint8_t>(p.octave)) ||
        !w.u8(p.chord) || !w.u8(p.keys) || !w.u8(p.voicing) || !w.u8(p.bend_row) || !w.u8(p.bend_range) || !w.u8(p.follow) || !w.u8(p.preset_len) ||
        !w.bytes(p.preset, p.preset_len)) {
      return 0;
    }
    for (const std::uint8_t b : p.layout) {
      if (!w.u8(b)) {
        return 0;
      }
    }
    if (!w.end()) {
      return 0;
    }
  }
  if (p.pads != 0u) {
    if (!w.begin("PADS") || !w.u16(kPadsVersion) || !w.u8(static_cast<std::uint8_t>(kModeCount))) {
      return 0;
    }
    for (unsigned m = 0; m < kModeCount; ++m) {
      for (unsigned r = 0; r < pads::kRows; ++r) {
        if (!w.u8(p.pad_row[m][r])) {
          return 0;
        }
      }
      for (unsigned r = 0; r < pads::kRows; ++r) {
        if (!w.u16(p.pad_strip[m][r])) {
          return 0;
        }
      }
    }
    if (!w.end()) {
      return 0;
    }
  }
  if (p.trct != 0u) {
    if (!w.begin("TRCT") || !w.u16(ver)) {
      return 0;
    }
    for (const float v : p.tract) {
      if (!w.f32(v)) {
        return 0;
      }
    }
    if (!w.end()) {
      return 0;
    }
  }
  return w.finish();
}

// `dst` is replaced only when the whole file is good (the file is decoded into a copy on the stack): then every part the file lacks is at its default (zero).
// A chunk of a newer major version, or a known chunk twice, refuses the file; an unknown chunk is skipped.
inline bool read_project(const std::uint8_t* data, std::size_t n, Project& dst) noexcept {
  namespace chunk = o3sdk::formats::chunk;
  chunk::Reader r;
  chunk::Chunk c{};
  if (data == nullptr || r.open(data, n, "VOCP") != chunk::Error::None) {
    return false;
  }
  Project got = {};
  unsigned seen = 0;
  while (r.next(c)) {
    struct Known {
      const char* id;
      bool (*read)(const chunk::Chunk&, Project&) noexcept;
    };
    static constexpr Known kKnown[] = {{"PTNS", project_detail::read_ptns}, {"CHAI", project_detail::read_chai}, {"ROUT", project_detail::read_rout},
                                       {"SLOT", project_detail::read_slot}, {"VALU", project_detail::read_valu}, {"STAT", project_detail::read_stat},
                                       {"MOUT", project_detail::read_mout}, {"TRCT", project_detail::read_trct}, {"PADS", project_detail::read_pads}};
    for (unsigned i = 0; i < sizeof kKnown / sizeof kKnown[0]; ++i) {
      if (c.id[0] != kKnown[i].id[0] || c.id[1] != kKnown[i].id[1] || c.id[2] != kKnown[i].id[2] || c.id[3] != kKnown[i].id[3]) {
        continue;
      }
      if ((seen & (1u << i)) != 0u || !kKnown[i].read(c, got)) {
        return false;
      }
      seen |= 1u << i;
    }
  }
  dst = got;
  return true;
}

}  // namespace o3_voc03er
