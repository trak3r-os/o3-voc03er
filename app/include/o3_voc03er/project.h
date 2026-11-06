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
//   SLOT 1.0  u8 count (4), then per slot u16 record length and u32 length (samples), u8 cuts, cuts x (u32 start, u32 len), u32 trim start, u32 trim end,
//             i8 speed (-1 0.5X, 0 1X, +1 2X), u8 reverse, i8 level (half-dB), u8 name length, the WAV's file name (no NUL)
//   VALU 1.0  u16 count, then count x (u16 parameter id, f32 value): the engine's state, by id
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

#include <o3_voc03er/route.h>
#include <o3_voc03er/stepseq.h>
#include <o3_voc03er/units/vocoder/vocoder.h>
#include <o3sdk/formats/chunk_file.h>
#include <o3sdk/hal/save_guard.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

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
};

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
};

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
inline constexpr std::size_t kSlotRecMax = 4 + 1 + 8 * sample::kMaxSlices + 8 + 4 + kWavNameMax;
inline constexpr std::size_t kPtnsMax = 2 + 1 + kBankPatterns * (2 + 1 + 4 * kPatternSteps);
inline constexpr std::size_t kChaiMax = 2 + 2 + 2 * kChainMax;
inline constexpr std::size_t kRoutMax = 2 + 3;
inline constexpr std::size_t kSlotMax = 2 + 1 + route::kSlots * (2 + kSlotRecMax);
inline constexpr std::size_t kValuMax = 2 + 2 + 6 * kEngineIds;
inline constexpr std::size_t kWorst = o3sdk::formats::chunk::kHeaderBytes + o3sdk::formats::chunk::kCrcChunkBytes + chunk_bytes(kPtnsMax) + chunk_bytes(kChaiMax) +
                                      chunk_bytes(kRoutMax) + chunk_bytes(kSlotMax) + chunk_bytes(kValuMax);

}  // namespace project_detail

// The buffer for a project file: the worst case (every chunk full: 8 patterns, 16 chain entries, 4 slots of 24 cuts and a 31-byte name, 64 engine values) is
// kWorst bytes, and the rest is room for chunks a later app adds.
inline constexpr std::size_t kProjectFileMax = 2560;
static_assert(project_detail::kWorst <= kProjectFileMax, "the worst-case project file must fit its buffer");
static_assert(kProjectFileMax % 4 == 0, "a word-aligned buffer of this size is whole words");
static_assert(sizeof(Project) <= 2560, "read_project stages a Project on the stack");
static_assert(Project{}.chain.count == 0u && Project{}.engine_set == 0u && Project{}.sounds == 0u && Project{}.slot[0].cuts.length == 0u && Project{}.bank.pattern[0].length == 0u,
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
  if (s.speed < -1 || s.speed > 1 || s.reverse > 1u || s.level_half < -48 || s.level_half > 24 || s.name_len > kWavNameMax) {
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

// A stored pattern: the length byte 0..16 (0 reads as 16), every step in range.
inline bool pattern_valid(const PatternData& p) noexcept {
  if (p.length > kPatternSteps) {
    return false;
  }
  for (const std::uint32_t w : p.step) {
    if (!step_valid(unpack_step(w)) || pack_step(unpack_step(w)) != w) {
      return false;
    }
  }
  return true;
}

inline bool project_valid(const Project& p) noexcept {
  if (!chain_valid(p.chain) || !project_route_valid(p)) {
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

namespace project_detail {

constexpr std::uint16_t slot_rec_bytes(const ProjectSlot& s) noexcept {
  return static_cast<std::uint16_t>(4 + 1 + 8u * s.cuts.count + 8 + 4 + s.name_len);
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
inline bool open_chunk(const o3sdk::formats::chunk::Chunk& c, o3sdk::formats::chunk::Fields& f) noexcept {
  f = o3sdk::formats::chunk::Fields(c);
  const std::uint16_t v = f.u16();
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
    if (i < kBankPatterns) {
      got.bank.pattern[i] = pd;
    }
  }
  return f.ok();
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
    if (!f.ok() || !slot_valid(s) || !rec_done(f, start, rec_len)) {
      return false;
    }
    if (i < route::kSlots) {
      got.slot[i] = s;
    }
  }
  return f.ok();
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
  if (!w.end() || !w.begin("CHAI") || !w.u16(ver) || !w.u8(p.chain.count) || !w.u8(p.chain.loop)) {
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
  if (!w.begin("SLOT") || !w.u16(ver) || !w.u8(static_cast<std::uint8_t>(route::kSlots))) {
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
        !w.u8(static_cast<std::uint8_t>(s.level_half)) || !w.u8(s.name_len) || !w.bytes(s.name, s.name_len)) {
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
  return w.end() ? w.finish() : 0;
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
                                       {"SLOT", project_detail::read_slot}, {"VALU", project_detail::read_valu}};
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
