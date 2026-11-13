// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FILE (docs/UI-DESIGN.md, "FILE"), slice U9c2: the pure part of the project file's screen. What a save keeps of a slot, which values a restore sends next, the
// defaults of a new project and the lines the screen says. No engine and no card here (tests/file_test.cpp).

#include <o3_voc03er/project.h>

#include "strip_logic.h"

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

inline constexpr unsigned kWavSlots = route::kSlots;

// A slot's record is restored only onto audio of the same length: the cuts mean nothing on another one.
constexpr bool slot_fits(const ProjectSlot& rec, std::uint32_t live_length) noexcept { return rec.cuts.length != 0u && rec.cuts.length == live_length; }

// The record a save writes for a slot: the live cuts of `live_length` samples (even cuts when the table has none yet); the kept record's speed, side, level, trim
// and name when it is for the same length; an empty slot keeps the record as it was (a load of its WAV restores it). `out` may be `kept`.
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

// UX3-3: the slots whose audio a save does not carry: live audio (`live[s]` != 0) that the project's record does not name a WAV for, at this length (the record
// after merge_slot: it keeps a WAV name only for audio it still fits), or that a take or a load has replaced since the last WAV save (`dirty` bit s, audio_dirty.h:
// that catches a re-record of the same length). Bit s set per slot.
inline unsigned unsaved_audio_mask(const Project& p, const std::uint32_t (&live)[kWavSlots], unsigned dirty = 0) noexcept {
  unsigned mask = 0;
  for (unsigned s = 0; s < kWavSlots; ++s) {
    if (live[s] != 0u && (((dirty >> s) & 1u) != 0u || !(p.slot[s].name_len != 0u && slot_fits(p.slot[s], live[s])))) {
      mask |= 1u << s;
    }
  }
  return mask;
}
inline constexpr const char* kSavedUnsavedAudio = "SAVED. AUDIO NOT SAVED: F5";
inline constexpr const char* kNameFirstLine = "NAME THE PROJECT FIRST: F2 SAVES IT";
inline constexpr const char* kNewAsk = "NEW CLEARS THE PROJECT, SLOTS KEEP THEIR AUDIO: F1 CONFIRMS";
inline constexpr const char* kNewDone = "NEW PROJECT. SLOTS KEEP THEIR AUDIO";

// What a slot holds live that a project keeps beyond the cuts: the SAMPLE screen's SPEED (0 0.5X, 1 1X, 2 2X), DIR and LEVEL (dB), and SAMPLE EDIT's trim brackets
// (both 0: not moved).
struct SlotValues {
  unsigned speed = 1;
  bool reverse = false;
  float level_db = 0.0f;
  std::uint32_t trim_start = 0;
  std::uint32_t trim_end = 0;
};

// dB to the file's half-dB steps, rounded away from zero and held to -48..+24 (the unit's LEVEL is -24..+12 dB); NaN is 0 dB.
constexpr std::int8_t level_to_half(float db) noexcept {
  if (!(db == db)) {
    return 0;
  }
  const float h = db * 2.0f;
  const int r = h >= 0.0f ? static_cast<int>(h + 0.5f) : -static_cast<int>(0.5f - h);
  return static_cast<std::int8_t>(r < -48 ? -48 : (r > 24 ? 24 : r));
}
constexpr float half_to_level(std::int8_t half) noexcept { return static_cast<float>(half) * 0.5f; }

// Put the live values in the record of a slot that holds audio (merge_slot has set its cuts and length): speed, reverse, level, and the trim when it is a pair the
// file accepts for this length; `values` says they were captured, so a load restores them.
inline void capture_values(ProjectSlot& out, const SlotValues& v) noexcept {
  out.speed = static_cast<std::int8_t>((v.speed > 2u ? 1u : v.speed) - 1u);
  out.reverse = v.reverse ? 1u : 0u;
  out.level_half = level_to_half(v.level_db);
  const bool trim = v.trim_start < v.trim_end && v.trim_end <= out.cuts.length;
  out.trim_start = trim ? v.trim_start : 0u;
  out.trim_end = trim ? v.trim_end : 0u;
  out.values = 1;
}

// The live values a record restores onto audio of `live_length` samples; false when the record has none (a file from before, or a slot that does not fit).
inline bool restore_values(const ProjectSlot& rec, std::uint32_t live_length, SlotValues& v) noexcept {
  if (rec.values == 0u || !slot_fits(rec, live_length)) {
    return false;
  }
  v.speed = static_cast<unsigned>(rec.speed + 1);
  v.reverse = rec.reverse != 0u;
  v.level_db = half_to_level(rec.level_half);
  v.trim_start = rec.trim_start;
  v.trim_end = rec.trim_end;
  return true;
}

// The five layouts a project means (TP1): the PADS chunk's, each with an unknown strip dropped to the mode's default; a file without PADS (an older image's)
// gives its one STAT layout to BANDS, TALK, TUNE and ROBOT and the TRACT default to TRACT.
inline void project_layouts(const Project& p, pads::Layout (&out)[kModeCount]) noexcept {
  for (unsigned m = 0; m < kModeCount; ++m) {
    pads::Layout l = strip::default_layout(m);
    if (p.pads != 0u) {
      l = project_pads_layout(p, m);
    } else if (m != kModeTract) {
      l = project_layout(p);
    }
    out[m] = strip::sanitize(l, m);
  }
}

// The STAT fields of a project that was just made: the first boot's tempo, pattern A, octave 0, the first chord kind, pads; voicing SMOOTH, bend row off,
// range 2, FOLLOW off, no preset, the default layouts (strip::default_layout, one per mode).
inline void default_stat(Project& p) noexcept {
  p.stat = 1;
  p.tempo_mbpm = 120000u;
  p.swing = 0;
  p.pattern = 0;
  p.octave = 0;
  p.chord = 0;
  p.keys = 0;
  p.voicing = 3;
  p.bend_row = 0;
  p.bend_range = 2;
  p.follow = 0;
  set_project_preset(p, kPresetCount);  // none
  pads::Layout l[kModeCount];
  for (unsigned m = 0; m < kModeCount; ++m) {
    l[m] = strip::default_layout(m);
  }
  set_project_pads(p, l);
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

// ---- SAVE WAVS and the WAVs of a LOAD (U9c3) ----

// The next slot from `from` on that holds audio (lens[s] != 0), the order a save visits them in; kWavSlots when there is none.
inline unsigned next_save_slot(const std::uint32_t (&lens)[kWavSlots], unsigned from) noexcept {
  for (unsigned s = from; s < kWavSlots; ++s) {
    if (lens[s] != 0u) {
      return s;
    }
  }
  return kWavSlots;
}

// The next slot from `from` on whose record names a WAV, the order a load reads them in; kWavSlots when there is none.
inline unsigned next_load_slot(const Project& p, unsigned from) noexcept {
  for (unsigned s = from; s < kWavSlots; ++s) {
    if (p.slot[s].name_len != 0u) {
      return s;
    }
  }
  return kWavSlots;
}

inline char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// `name` is "<stem>_NNN.wav" (the case of the card's listing is not told apart): its NNN, else 0.
inline unsigned wav_number(const char* stem, const char* name) noexcept {
  const std::size_t n = std::strlen(stem);
  if (std::strlen(name) != n + 8u) {
    return 0;
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (lower(name[i]) != lower(stem[i])) {
      return 0;
    }
  }
  if (name[n] != '_') {
    return 0;
  }
  unsigned v = 0;
  for (std::size_t i = 1; i <= 3; ++i) {
    const char c = name[n + i];
    if (c < '0' || c > '9') {
      return 0;
    }
    v = v * 10u + static_cast<unsigned>(c - '0');
  }
  const char* ext = name + n + 4u;
  if (ext[0] != '.' || lower(ext[1]) != 'w' || lower(ext[2]) != 'a' || lower(ext[3]) != 'v') {
    return 0;
  }
  return v;
}

// The number the kit's save_wav picks for `stem` (the first free NNN from 001, up to 999) among the `count` names of the folder's listing; 0 when all are taken.
// `name_at(i)` is the i-th file name. Bounded by count * (count + 1) tests.
template <typename NameAt>
inline unsigned first_free_wav_number(const char* stem, std::size_t count, NameAt name_at) noexcept {
  unsigned cand = 1;
  for (std::size_t round = 0; round <= count; ++round) {
    bool taken = false;
    for (std::size_t i = 0; i < count; ++i) {
      if (wav_number(stem, name_at(i)) == cand) {
        taken = true;
        break;
      }
    }
    if (!taken) {
      return cand <= 999u ? cand : 0u;
    }
    ++cand;
  }
  return 0;
}

// "slot1" .. "slot4": the stem a slot's WAV is saved under.
inline void wav_stem(char (&out)[6], unsigned slot) noexcept {
  std::memcpy(out, "slot", 4);
  out[4] = static_cast<char>('1' + (slot & 3u));
  out[5] = '\0';
}

// The name save_wav gives: "<stem>_NNN.wav".
inline void wav_file_name(char (&out)[kWavNameMax + 1], const char* stem, unsigned number) noexcept {
  out[0] = '\0';
  cat(out, sizeof out, stem);
  cat(out, sizeof out, "_");
  const char d[4] = {static_cast<char>('0' + (number / 100u) % 10u), static_cast<char>('0' + (number / 10u) % 10u), static_cast<char>('0' + number % 10u), '\0'};
  cat(out, sizeof out, d);
  cat(out, sizeof out, ".wav");
}

// The record's name once its WAV is on the card: false (the record unchanged) when `name` is not one a record may hold.
inline bool set_wav_name(ProjectSlot& rec, const char* name) noexcept {
  const std::size_t n = std::strlen(name);
  if (n == 0u || n > kWavNameMax) {
    return false;
  }
  rec.name_len = static_cast<std::uint8_t>(n);
  std::memcpy(rec.name, name, n);
  rec.name[n] = '\0';
  return true;
}

// A load whose WAV did not land (missing file, refused, slot busy) leaves the record as it is, name and cuts, for the next save: the record still names a WAV.
constexpr bool wav_missing_keeps_record(const ProjectSlot& rec) noexcept { return rec.name_len != 0u; }

// "SAVING WAV: SLOT 2 (1 OF 3)" and "LOADING WAV: SLOT 2 (1 OF 3)": `done` jobs finished before this one, `total` the jobs of the run.
inline void wav_job_line(char* dst, std::size_t cap, const char* verb, unsigned slot, unsigned done, unsigned total) noexcept {
  dst[0] = '\0';
  cat(dst, cap, verb);
  cat(dst, cap, " WAV: SLOT ");
  cat_uint(dst, cap, slot + 1u);
  cat(dst, cap, " (");
  cat_uint(dst, cap, done + 1u);
  cat(dst, cap, " OF ");
  cat_uint(dst, cap, total);
  cat(dst, cap, ")");
}

// "SAVED. 3 WAVS AND THE PROJECT".
inline void wavs_saved_note(char* dst, std::size_t cap, unsigned saved) noexcept {
  dst[0] = '\0';
  cat(dst, cap, "SAVED. ");
  cat_uint(dst, cap, saved);
  cat(dst, cap, saved == 1u ? " WAV AND THE PROJECT" : " WAVS AND THE PROJECT");
}

// "LOADED. 2 WAVS, 1 SKIPPED: SLOT 3 FILE NOT FOUND" (skipped = 0: no tail; the first skipped slot and its reason are told).
inline void wavs_loaded_note(char* dst, std::size_t cap, unsigned loaded, unsigned skipped, unsigned first_slot, const char* why) noexcept {
  dst[0] = '\0';
  cat(dst, cap, "LOADED. ");
  cat_uint(dst, cap, loaded);
  cat(dst, cap, loaded == 1u ? " WAV" : " WAVS");
  if (skipped != 0u) {
    cat(dst, cap, ", ");
    cat_uint(dst, cap, skipped);
    cat(dst, cap, " SKIPPED: SLOT ");
    cat_uint(dst, cap, first_slot + 1u);
    cat(dst, cap, " ");
    cat(dst, cap, why);
  }
}

// The line while a slot waits for its WAV (a greyed slot, in words).
inline void wav_wait_line(char* dst, std::size_t cap, unsigned slot) noexcept {
  dst[0] = '\0';
  cat(dst, cap, "SLOT ");
  cat_uint(dst, cap, slot + 1u);
  cat(dst, cap, " IS GREY UNTIL ITS WAV HAS LANDED");
}

}  // namespace o3_voc03er::file
