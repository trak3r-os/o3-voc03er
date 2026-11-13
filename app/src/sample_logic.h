// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE's pure logic (docs/UI-DESIGN.md, "SAMPLE (SAMPLE PLAY): the slots"): SIDE against route::allowed, the SPEED and LEVEL steps, the
// dB text, the pads' split. Apart from the screen so tests/sample_test.cpp holds them. Nothing here allocates; tract_live is the one engine read.

#include "strip_logic.h"

#include <o3_voc03er/modes.h>
#include <o3_voc03er/route.h>
#include <o3_voc03er/stepseq.h>
#include <o3_voc03er/units/tract/tract.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_voc03er::sample {

inline constexpr unsigned kSpeedCount = 3;
inline constexpr const char* kSpeedNames[kSpeedCount] = {"0.5X", "1X", "2X"};  // LoopField::Speed 0..2
inline constexpr const char* kDirNames[2] = {"FWD", "REV"};                    // LoopField::Dir 0..1

inline constexpr float kLevelMinDb = -24.0f;
inline constexpr float kLevelMaxDb = 12.0f;

// F2 SIDE: which picker the slot goes to.
enum class Side : std::uint8_t { Speaks, Sounds, None };
inline constexpr unsigned kSideCount = 3;
inline constexpr const char* kSideNames[kSideCount] = {"SPEAKS", "SOUNDS", "NONE"};

// The top two rows (pads 0..23) are the slices of the selected slot.
inline constexpr unsigned kTopPads = 24;
constexpr bool layout_pad(unsigned pad) noexcept { return pad >= kTopPads; }

constexpr std::uint8_t slot_value(unsigned slot) noexcept { return static_cast<std::uint8_t>(route::kFirstSlot + slot); }

// The side `slot` is on in `r` (a slot is one reader, so at most one).
constexpr Side side_of(route::Route r, unsigned slot) noexcept {
  const std::uint8_t v = slot_value(slot);
  return r.speaks == v ? Side::Speaks : (r.sounds == v ? Side::Sounds : Side::None);
}

// What F2 does: `to` the route after it, `change` when it differs, `refused` with `why` (a static string) when nothing may change.
// A slot sits on one side: moving it frees the side it leaves (LIVE on SPEAKS, SYNTH on SOUNDS). An empty slot cannot be placed.
struct SidePlan {
  route::Route to;
  bool change = false;
  bool refused = false;
  const char* why = "";
};

constexpr SidePlan plan_side(route::Route cur, Side target, unsigned slot, bool empty) noexcept {
  SidePlan p;
  p.to = cur;
  if (slot >= route::kSlots || side_of(cur, slot) == target) {
    return p;
  }
  const std::uint8_t v = slot_value(slot);
  if (target != Side::None && empty) {
    p.refused = true;
    p.why = "EMPTY: NOTHING TO PLAY";
    return p;
  }
  route::Route n = cur;
  if (n.speaks == v) {
    n.speaks = route::kSpeaksLive;
  }
  if (n.sounds == v) {
    n.sounds = route::kSoundsSynth;
  }
  if (target == Side::Speaks) {
    n.speaks = v;
  } else if (target == Side::Sounds) {
    n.sounds = v;
  }
  if (!route::allowed(n)) {
    p.refused = true;
    p.why = "THE OTHER SIDE HOLDS IT";
    return p;
  }
  p.to = n;
  p.change = true;
  return p;
}

// The next side by `delta`, wrapping SPEAKS, SOUNDS, NONE.
constexpr Side step_side(Side s, int delta) noexcept {
  const int n = static_cast<int>(kSideCount);
  return static_cast<Side>(((static_cast<int>(s) + delta) % n + n) % n);
}

// SPEED held inside 0.5X..2X by a detent; a tap wraps.
constexpr unsigned step_speed(unsigned index, int delta, bool wrap) noexcept {
  const int n = static_cast<int>(kSpeedCount);
  const int v = static_cast<int>(index) + delta;
  if (wrap) {
    return static_cast<unsigned>((v % n + n) % n);
  }
  return v < 0 ? 0u : (v >= n ? kSpeedCount - 1u : static_cast<unsigned>(v));
}

// LEVEL by `delta_half` half-dB, held inside -24..+12 dB.
constexpr float step_level(float db, int delta_half) noexcept {
  const float v = db + static_cast<float>(delta_half) * 0.5f;
  return v < kLevelMinDb ? kLevelMinDb : (v > kLevelMaxDb ? kLevelMaxDb : v);
}

// The level in half-dB, rounded (-48..24).
constexpr int level_halves(float db) noexcept { return static_cast<int>(db * 2.0f + (db >= 0.0f ? 0.5f : -0.5f)); }

// "+3.5", "-12", "0" into out[8] (the unit's dB, without the unit).
inline void level_text(float db, char (&out)[8]) noexcept {
  const int h = level_halves(db);
  const unsigned a = static_cast<unsigned>(h < 0 ? -h : h);
  unsigned n = 0;
  out[n++] = h < 0 ? '-' : (h > 0 ? '+' : '0');
  if (h != 0) {
    const unsigned w = a / 2u;
    if (w >= 10u) {
      out[n++] = static_cast<char>('0' + w / 10u);
    }
    out[n++] = static_cast<char>('0' + w % 10u);
    if ((a & 1u) != 0u) {
      out[n++] = '.';
      out[n++] = '5';
    }
  }
  out[n] = '\0';
}

// 0..100 for the little bar under the LEVEL value.
constexpr int level_percent(float db) noexcept { return static_cast<int>((db - kLevelMinDb) * 100.0f / (kLevelMaxDb - kLevelMinDb) + 0.5f); }

// ---- slices (U5b) -----------------------------------------------------------------------------------------------------------------------

inline constexpr unsigned kMaxSlices = kTopPads;
inline constexpr unsigned kDefaultCuts = 8;
inline constexpr unsigned kReaders = 4;

struct Cut {
  std::uint32_t start = 0;  // samples from the start of the slot
  std::uint32_t len = 0;
};

// A slot's cuts. Until SAMPLE EDIT (U7) they are even; `length` is the slot's length they were built for (0: none yet).
struct SliceTable {
  std::uint32_t length = 0;
  std::uint8_t count = 0;
  Cut cut[kMaxSlices];
};

// `want` equal cuts of a slot of `length` samples (one slot is at most Loop::kMaxSamples, so length * 24 stays inside 32 bits). A slot shorter
// than `want` gets one cut a sample; an empty slot none. The last cut ends at the slot's end, so the cuts tile it.
inline void even_cuts(SliceTable& t, std::uint32_t length, unsigned want = kDefaultCuts) noexcept {
  const unsigned cap = want < kMaxSlices ? want : kMaxSlices;
  const unsigned n = length < cap ? static_cast<unsigned>(length) : cap;
  t.length = length;
  t.count = static_cast<std::uint8_t>(n);
  for (unsigned i = 0; i < n; ++i) {
    const std::uint32_t a = length * i / n;
    const std::uint32_t b = length * (i + 1u) / n;
    t.cut[i] = {a, b - a};
  }
}

// The Start the unit takes: the bank's samples, slot after slot of `per_loop` (the size bind_loops was given).
constexpr std::uint32_t region_start(unsigned slot, std::uint32_t per_loop, const Cut& c) noexcept { return slot * per_loop + c.start; }

// What the step sequencer's SLICE lane was last given (main loop): the slot + 1 (0 none) and the length its cuts were built for.
struct SlicePublished {
  std::uint8_t slot1 = 0;
  std::uint32_t len = 0;
};

// The SPEAKS slot's slices to the step sequencer (U6c), only when the slot or its table's length changed since the last call: a pass that finds
// nothing new costs two compares. `slot` >= kReaders (SPEAKS LIVE or NONE) or an empty table publishes none (reader 0xFF, count 0). The hook
// never reads the map while it is rewritten (StepSeq::slices_open stores count 0 first). Returns true when it published. Only the length is
// compared because even cuts follow from it: SAMPLE EDIT (U7) moves cuts at the same length and has to clear `st` to force a publish.
inline bool publish_slices(StepSeq& seq, SlicePublished& st, unsigned slot, const SliceTable* table, std::uint32_t per_loop) noexcept {
  const bool have = slot < kReaders && table != nullptr && table->length != 0u;
  const std::uint8_t want = have ? static_cast<std::uint8_t>(slot + 1u) : std::uint8_t{0};
  const std::uint32_t len = have ? table->length : 0u;
  if (st.slot1 == want && st.len == len) {
    return false;
  }
  SliceMap* m = seq.slices_open();
  unsigned count = 0;
  if (m != nullptr && have) {
    count = table->count < kMaxSlices ? table->count : kMaxSlices;
    for (unsigned i = 0; i < count; ++i) {
      m->start[i] = region_start(slot, per_loop, table->cut[i]);
      m->len[i] = table->cut[i].len;
    }
  }
  seq.slices_close(have ? slot : StepSeq::kNoReader, count);
  st.slot1 = want;
  st.len = len;
  return true;
}

// A slice sounds on the side of its slot; a slot on no side speaks (design section 1). true: the reader's Role is CAR.
constexpr bool slice_is_carrier(Side s) noexcept { return s == Side::Sounds; }

// UX3-1: a slot's side means nothing to the ear in two modes. TUNE plays no synth, so a SOUNDS slot (the carrier: its loop, its slices) is not heard; TRACT
// listens to nothing, so what SPEAKS (a SPEAKS slot, and the slices of a slot on NO side, which speak) is heard through DRY, and with the MOUTH source on
// LIVE it also moves the mouth (the vocoder feeds the modulator, a SPEAKS slot included, to the live voice's analysis: vocoder.cpp, `mouth_live_`). The screens
// draw a silent side dim with the reason below; the slot itself is untouched.
using o3_voc03er::strip::kModeTune;  // the unit's Mode value of TUNE (strip_logic.h), not a copy
enum class Cue : std::uint8_t { Heard, TuneSilent, TractDry, TractLive };
constexpr Cue mode_cue(unsigned mode, Side side, bool mouth_live = false) noexcept {
  if (mode == kModeTune && side == Side::Sounds) {
    return Cue::TuneSilent;
  }
  if (mode == o3_voc03er::kModeTract && side != Side::Sounds) {
    return mouth_live ? Cue::TractLive : Cue::TractDry;
  }
  return Cue::Heard;
}
// A side the ear does not get in this mode: drawn dim. TractLive is heard (through DRY and as the mouth), so it is not dim, but it still has a line.
constexpr bool cue_dim(Cue c) noexcept { return c == Cue::TuneSilent || c == Cue::TractDry; }
// The reason line for a cue (at most 40 characters: the Micro face in SAMPLE's middle pane); nullptr for Heard.
constexpr const char* cue_reason(Cue c) noexcept {
  switch (c) {
    case Cue::TuneSilent:
      return "TUNE PLAYS NO SYNTH: NOT HEARD";
    case Cue::TractDry:
      return "TRACT HEARS IT ONLY THROUGH DRY";
    case Cue::TractLive:
      return "MOVES THE MOUTH (LIVE) AND DRY";
    case Cue::Heard:
      break;
  }
  return nullptr;
}
// MOUTH = LIVE on the engine's TRACT unit (found by its name: the screens do not hold its id); false with no TRACT unit.
inline bool tract_live(const o3sdk::dsp::Engine& e) noexcept {
  for (unsigned i = 0; i < e.unit_count(); ++i) {
    const o3sdk::dsp::UnitInfo* in = e.unit_info(static_cast<o3sdk::dsp::UnitId>(i));
    if (in != nullptr && in->name != nullptr && in->name[0] == 'T' && in->name[1] == 'R' && in->name[2] == 'A' && in->name[3] == 'C' && in->name[4] == 'T' && in->name[5] == '\0') {
      return e.param(static_cast<o3sdk::dsp::UnitId>(i), o3_voc03er::TractUnit::Source) >= 0.5f;
    }
  }
  return false;
}
// UX3-5: what a slot on NO side does, for the line under the badge. Verified in PerformScreen::set_route: a slot that leaves a side has its Play turned
// off, so its whole loop is stopped; its slices (role MOD) still speak when they are played. F5 LOOP can start the loop of a NO side slot, and then it
// speaks as a modulator too (vocoder.cpp run_loops: a loop that is not on the carrier plays into the modulator), so the line follows the loop.
inline constexpr const char* kNoneLine = "NO SIDE: ONLY SLICES SPEAK. F2 SIDE";
inline constexpr const char* kNoneLoopLine = "NO SIDE: LOOP AND SLICES SPEAK";
constexpr const char* none_line(bool loop_on) noexcept { return loop_on ? kNoneLoopLine : kNoneLine; }

// UX3-8: a slice pad past the slot's count says how many slices there are ("5 SLICES", "1 SLICE", "NO SLICES: SLOT IS EMPTY" for an empty slot); `out` holds 32.
inline void slices_line(unsigned count, char (&out)[32]) noexcept {
  if (count == 0u) {
    const char* t = "NO SLICES: SLOT IS EMPTY";
    unsigned i = 0;
    for (; t[i] != '\0'; ++i) {
      out[i] = t[i];
    }
    out[i] = '\0';
    return;
  }
  unsigned n = 0;
  if (count >= 10u) {
    out[n++] = static_cast<char>('0' + (count / 10u) % 10u);
  }
  out[n++] = static_cast<char>('0' + count % 10u);
  const char* w = count == 1u ? " SLICE" : " SLICES";
  for (unsigned i = 0; w[i] != '\0'; ++i) {
    out[n++] = w[i];
  }
  out[n] = '\0';
}

// UX3-8: pick_reader gives -1 for one cause only (the slot's own reader has a take on it: armed, waiting for its start, or recording), so the line names
// the state: an armed take has not started to record yet.
inline constexpr const char* kNoReaderRecLine = "SLOT IS RECORDING: NO SLICE";
inline constexpr const char* kNoReaderArmedLine = "SLOT IS ARMED TO RECORD: NO SLICE";
constexpr const char* no_reader_line(bool armed) noexcept { return armed ? kNoReaderArmedLine : kNoReaderRecLine; }
// UX3-8: a slot's length changed under cuts that were edited by hand: they are even again.
inline constexpr const char* kCutsResetLine = "LENGTH CHANGED: CUTS EVEN AGAIN";

// What pick_reader needs of each reader (= the slot of the same number).
struct ReaderState {
  bool recording = false;  // a take is in it: never touched
  bool loop_on = false;    // its own slot's whole loop plays (or waits for the bar)
  bool region = false;     // a slice sounds on it
  bool sided = false;      // its slot is on a side: the unit routes that reader by CarSrc or ModSrc, not by Role, so a borrowed slice would land there
  std::uint32_t seq = 0;   // when that slice started (higher: newer)
};

// The reader a slice of `slot` plays on; -1 when none can. CHOKE ON: the slot's own reader, which a new slice retriggers. CHOKE OFF: the own
// reader if no slice sounds on it, else a free one, else the oldest slice voice is stolen. A reader that records is never taken, nor one whose
// whole loop plays while it is another slot's (a slice would pause it), nor one whose slot is on a side.
constexpr int pick_reader(const ReaderState (&r)[kReaders], unsigned slot, bool choke) noexcept {
  if (slot >= kReaders || r[slot].recording) {
    return -1;
  }
  if (choke || !r[slot].region) {
    return static_cast<int>(slot);
  }
  int oldest = -1;
  for (unsigned k = 0; k < kReaders; ++k) {
    if (k == slot || r[k].recording || r[k].loop_on || r[k].sided) {
      continue;
    }
    if (!r[k].region) {
      return static_cast<int>(k);
    }
    if (oldest < 0 || r[k].seq < r[static_cast<unsigned>(oldest)].seq) {
      oldest = static_cast<int>(k);
    }
  }
  if (oldest < 0 || r[slot].seq < r[static_cast<unsigned>(oldest)].seq) {
    oldest = static_cast<int>(slot);  // the own reader sounds a slice too: it competes for the steal
  }
  return oldest;
}

// A slice that has not shown in LoopStatus::region yet has been asked at most this long ago (the render context publishes once a block).
inline constexpr std::uint64_t kStartGraceMs = 60;

// A slice voice has ended: it was seen sounding and no longer is, or it was never seen within the grace.
constexpr bool region_ended(bool seen, bool region_now, std::uint64_t age_ms) noexcept {
  return seen ? !region_now : (!region_now && age_ms >= kStartGraceMs);
}

// x of `offset` along a bar `w` wide for a slot of `length` samples (offset past the end clamps).
constexpr int bar_x(std::uint32_t offset, std::uint32_t length, int w) noexcept {
  return length == 0u ? 0 : static_cast<int>((offset < length ? offset : length) * static_cast<std::uint32_t>(w) / length);
}

}  // namespace o3_voc03er::sample
