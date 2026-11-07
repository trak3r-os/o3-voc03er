// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FILE (docs/UI-DESIGN.md, "FILE"), slice U9c2: the pure part of the project file's screen. What a save keeps of a slot, which values a restore sends next, the
// defaults of a new project and the lines the screen says. No engine and no card here (tests/file_test.cpp).

#include <o3_voc03er/project.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace o3_voc03er::file {

inline constexpr unsigned kValuesPerFrame = 16;  // a restore sends this many values a frame: the engine's command ring holds 512, and a project is about 60

// The next engine id from `from` on that the project holds and this build restores; kEngineIds when there is none.
inline unsigned next_id(const Project& p, unsigned from) noexcept {
  for (unsigned id = from; id < kEngineIds; ++id) {
    if (project_has_engine(p, id) && engine_id_known(id)) {
      return id;
    }
  }
  return kEngineIds;
}

// A slot's record is restored only onto audio of the same length: the cuts mean nothing on another one.
constexpr bool slot_fits(const ProjectSlot& rec, std::uint32_t live_length) noexcept { return rec.cuts.length != 0u && rec.cuts.length == live_length; }

// The record a save writes for a slot: the live cuts of `live_length` samples (even cuts when the table has none yet); the kept record's speed, side, level, trim
// and name when it is for the same length; an empty slot keeps the record as it was (the next slice restores it). `out` may be `kept`.
inline void merge_slot(ProjectSlot& out, const ProjectSlot& kept, const sample::SliceTable& live, std::uint32_t live_length) noexcept {
  if (live_length == 0u) {
    out = kept;
    return;
  }
  const bool same = slot_fits(kept, live_length);
  out = same ? kept : ProjectSlot{};
  if (live.count == 0u) {
    sample::even_cuts(out.cuts, live_length);
  } else {
    out.cuts = live;
  }
  out.cuts.length = live_length;
}

// The STAT fields of a project that was just made: the first boot's tempo, pattern A, octave 0, the first chord kind, pads.
inline void default_stat(Project& p) noexcept {
  p.stat = 1;
  p.tempo_mbpm = 120000u;
  p.swing = 0;
  p.pattern = 0;
  p.octave = 0;
  p.chord = 0;
  p.keys = 0;
}

inline void cat(char* dst, std::size_t cap, const char* s) noexcept {
  std::size_t n = std::strlen(dst);
  while (*s != '\0' && n + 1u < cap) {
    dst[n++] = *s++;
  }
  dst[n] = '\0';
}

inline void cat_uint(char* dst, std::size_t cap, unsigned v) noexcept {
  char t[12];
  unsigned n = 0;
  do {
    t[n++] = static_cast<char>('0' + v % 10u);
    v /= 10u;
  } while (v != 0u && n < sizeof t);
  char out[12];
  for (unsigned i = 0; i < n; ++i) {
    out[i] = t[n - 1u - i];
  }
  out[n] = '\0';
  cat(dst, cap, out);
}

// "LOADED. SLOTS 2 RESTORED, 1 KEPT FOR LATER" (restored: records put on audio of their length; kept: records that wait).
inline void loaded_note(char* dst, std::size_t cap, unsigned restored, unsigned kept) noexcept {
  dst[0] = '\0';
  cat(dst, cap, "LOADED. SLOTS ");
  cat_uint(dst, cap, restored);
  cat(dst, cap, " RESTORED, ");
  cat_uint(dst, cap, kept);
  cat(dst, cap, " KEPT FOR LATER");
}

}  // namespace o3_voc03er::file
