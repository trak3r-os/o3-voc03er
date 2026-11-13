// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE's pure logic (docs/UI-DESIGN.md, "SAMPLE (SAMPLE PLAY): the slots"): SPEAKS against route::allowed, LOW PADS pairs, the SPEED and LEVEL steps, the
// dB text, the pads' split, and the two rules that make one finger sound (a slice pad brings a note, a lower pad starts the picked slice). Apart from the screen so tests/sample_test.cpp holds them. Nothing here allocates; tract_live is the one engine read.

#include "pads_logic.h"
#include "liveness.h"
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

// Which side of the route a slot is on: the vocoder's input (SPEAKS), the carrier (SOUNDS), or neither (HELD: only its pads play it).
enum class Side : std::uint8_t { Speaks, Sounds, None };
inline constexpr unsigned kSideCount = 3;
inline constexpr const char* kSideNames[kSideCount] = {"SPEAKS", "SOUNDS", "HELD"};

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

// ---- F2 SPEAKS --------------------------------------------------------------------------------------------------------------------------
// F2 shows and chooses what the vocoder listens to, as PERFORM's SPEAKS chip names it: position 0 is LIVE, 1..4 are SLOT 1..4. SOUNDS for a slot is
// PERFORM's to choose, so there is no HELD choice here: a slot that does not speak is simply not the one that speaks.

inline constexpr unsigned kSpeaksPositions = 1u + route::kSlots;

// The position of `r.speaks` (0 LIVE, 1..4 a slot).
constexpr unsigned speaks_pos(route::Route r) noexcept { return route::is_slot(r.speaks) ? static_cast<unsigned>(r.speaks - route::kFirstSlot) + 1u : 0u; }

// A tap on F2 for `slot`: it speaks, or, when it already does, SPEAKS goes back to LIVE (Side::None frees the slot's side, which is SPEAKS only).
constexpr Side tap_speaks_target(route::Route r, unsigned slot) noexcept { return side_of(r, slot) == Side::Speaks ? Side::None : Side::Speaks; }

// The encoder: `delta` steps along LIVE, SLOT 1..4 over the slots that hold audio (bit i of `audio`: slot i + 1), stopping at the ends.
constexpr unsigned step_speaks(unsigned pos, int delta, unsigned audio) noexcept {
  unsigned p = pos < kSpeaksPositions ? pos : 0u;
  int left = delta;
  while (left != 0) {
    const int dir = left > 0 ? 1 : -1;
    int q = static_cast<int>(p) + dir;
    while (q >= 1 && q <= static_cast<int>(route::kSlots) && ((audio >> (q - 1)) & 1u) == 0u) {
      q += dir;
    }
    if (q < 0 || q > static_cast<int>(route::kSlots)) {
      break;
    }
    p = static_cast<unsigned>(q);
    left -= dir;
  }
  return p;
}

// What the F2 note says (static strings, one per slot): the slot speaks now; and the same when it was the SOUNDS source, which a slot cannot be
// at the same time (plan_side frees SOUNDS to the synth).
inline constexpr const char* kSpeaksNote[route::kSlots] = {"SLOT 1 SPEAKS", "SLOT 2 SPEAKS", "SLOT 3 SPEAKS", "SLOT 4 SPEAKS"};
inline constexpr const char* kSpeaksFreedNote[route::kSlots] = {"SLOT 1 SPEAKS. SOUNDS BACK TO SYNTH", "SLOT 2 SPEAKS. SOUNDS BACK TO SYNTH",
                                                                "SLOT 3 SPEAKS. SOUNDS BACK TO SYNTH", "SLOT 4 SPEAKS. SOUNDS BACK TO SYNTH"};
inline constexpr const char* kSpeaksLiveNote = "SPEAKS: BACK TO LIVE";

// ---- one finger sounds ------------------------------------------------------------------------------------------------------------------
// In BANDS, TALK and ROBOT a slice is the voice and is heard only while a synth note sounds; a lower pad is a synth note, heard only while something speaks.
// So each half of the pads brings the other half when it is pressed alone (docs/UI-DESIGN.md, "SAMPLE"):
//   RULE 1  a slice pad alone also plays "the note" (the last note or chord a lower pad played on the screen) while the slice sounds;
//   RULE 2  a lower pad alone also starts "the picked slice" (the last slice a top pad fired on the slot) while the pad is held.

// The modes whose voice needs a synth note under a slice: the ones liveness.h lists as BANDS, TALK and ROBOT (TUNE plays no synth, TRACT no note).
constexpr bool mode_needs_note(unsigned mode) noexcept { return mode < o3_voc03er::kModeCount && ((o3_voc03er::live::kBandsTalkRobot >> mode) & 1u) != 0u; }

// Rule 1: does a slice pad pressed now bring the note? `side` is the pressed slice's slot's, `lower_held` any lower pad down (a latched SAMPLE EDIT note counts),
// `seq_notes` the sequencer is running a pattern that holds a note (it plays its own).
constexpr bool slice_press_brings_note(unsigned mode, Side side, bool lower_held, bool seq_notes) noexcept {
  return mode_needs_note(mode) && side != Side::Sounds && !lower_held && !seq_notes;
}

// Rule 2: does a lower pad pressed now start the picked slice? `has_audio` the selected slot holds audio, `slice_held` any top pad down, `looping` the slot's
// loop plays or waits for the bar, `repeating` a slice repeats on the slot (LOOP ON + a pad). Every mode: the slice is the voice of a speaking slot, the
// lower pad's note makes it heard where it needs one, and in TUNE and TRACT it simply plays.
constexpr bool lower_press_starts_slice(Side side, bool has_audio, bool slice_held, bool looping, bool repeating) noexcept {
  return has_audio && side != Side::Sounds && !slice_held && !looping && !repeating;
}

// The picked slice after a top pad fired slice `fired` (0..23) of a slot with `count` slices: that one when it exists, else the pick stays.
constexpr unsigned picked_after(unsigned pick, unsigned fired, unsigned count) noexcept { return fired < count ? fired : pick; }
// The pick held inside the slot's slices (slice 1 for a slot without them, or after its cuts changed).
constexpr unsigned picked_valid(unsigned pick, unsigned count) noexcept { return pick < count ? pick : 0u; }

// ---- F1 LOW PADS ------------------------------------------------------------------------------------------------------------------------
// One key sets both lower rows: a pair (row A, row B). The first is the palette's own default; the others give both rows the same kind (row A sounds
// an octave above row B in the default palette, so the two are not duplicates). There is no OFF.
inline constexpr unsigned kLowPadsCount = 6;
inline constexpr pads::Plays kLowA[kLowPadsCount] = {pads::Plays::Note, pads::Plays::Note, pads::Plays::Triad, pads::Plays::Seventh, pads::Plays::Open, pads::Plays::Power};
inline constexpr pads::Plays kLowB[kLowPadsCount] = {pads::Plays::Triad, pads::Plays::Note, pads::Plays::Triad, pads::Plays::Seventh, pads::Plays::Open, pads::Plays::Power};
inline constexpr const char* kLowPadsNames[kLowPadsCount] = {"NOTE+TRIAD", "NOTE", "TRIAD", "7TH", "OPEN", "POWER"};
// The same for the F1 key's value: the Data face holds about eight characters in a 100 px key.
inline constexpr const char* kLowPadsKey[kLowPadsCount] = {"NOTE+TRI", "NOTE", "TRIAD", "7TH", "OPEN", "POWER"};

// The pair the palette holds when it is one of the six, else -1 (a pair PATTERN set that the key has no place for, such as A NOTE with B 7TH, or a row that is OFF
// or a parameter strip): the key says MIXED for it.
constexpr int low_pads_index(pads::Plays a, pads::Plays b) noexcept {
  for (unsigned i = 0; i < kLowPadsCount; ++i) {
    if (kLowA[i] == a && kLowB[i] == b) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// The pair after a step: a tap goes on and wraps, the encoder stops at the ends. From MIXED (-1, a pair the list has no place for) the first step lands on NOTE+TRIAD, either way.
constexpr unsigned low_pads_step(int cur, int delta, bool wrap) noexcept {
  if (cur < 0) {
    return 0u;
  }
  const int n = static_cast<int>(kLowPadsCount);
  const int v = cur + delta;
  if (wrap) {
    return static_cast<unsigned>((v % n + n) % n);
  }
  return v < 0 ? 0u : (v >= n ? kLowPadsCount - 1u : static_cast<unsigned>(v));
}

// The key's value: the pair's name; a pair outside the list is MIXED (never row A's kind alone: row B may play something else).
constexpr const char* low_pads_name(pads::Plays a, pads::Plays b) noexcept {
  const int i = low_pads_index(a, b);
  return i >= 0 ? kLowPadsNames[i] : "MIXED";
}
constexpr const char* low_pads_key(pads::Plays a, pads::Plays b) noexcept {
  const int i = low_pads_index(a, b);
  return i >= 0 ? kLowPadsKey[i] : "MIXED";
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
// nothing new costs two compares. `slot` >= kReaders (SPEAKS LIVE) or an empty table publishes none (reader 0xFF, count 0). The hook
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
// What the cue line says in one plain line about the selected slot (at most 52 characters; `out` holds 64): what the slot does now, or why it is quiet in this
// mode (UX3-1). Verified in PerformScreen::set_route: a slot that leaves a side has its Play turned off, so a slot on no side plays only its slices (role MOD);
// its loop, if F5 starts it, is not heard (the cue says so). `speaks` is the route's SPEAKS value: a slot on no side speaks its pads over LIVE, or together with
// the slot that speaks.
inline void cue_line(char (&out)[64], unsigned slot, Side side, Cue cue, bool loop_on, std::uint8_t speaks) noexcept {
  const auto put = [&out](unsigned& n, const char* t) {
    for (unsigned i = 0; t[i] != '\0' && n + 1u < sizeof out; ++i) {
      out[n++] = t[i];
    }
  };
  unsigned n = 0;
  put(n, "SLOT ");
  out[n++] = static_cast<char>('1' + (slot & 3u));
  put(n, side == Side::Speaks ? " SPEAKS: " : (side == Side::Sounds ? " SOUNDS: " : ": "));
  if (cue == Cue::TuneSilent) {
    put(n, "TUNE PLAYS NO SYNTH, NOT HEARD");
  } else if (cue == Cue::TractDry) {
    put(n, "TRACT HEARS IT ONLY THROUGH DRY");
  } else if (cue == Cue::TractLive) {
    put(n, "IT MOVES THE MOUTH (LIVE) AND DRY");
  } else if (side == Side::Speaks) {
    put(n, "THE VOCODER HEARS IT, NOT THE INPUT");
  } else if (side == Side::Sounds) {
    put(n, "IT IS THE CARRIER (PERFORM)");
  } else {
    put(n, loop_on ? "LOOP NOT HEARD, PADS SPEAK " : "ITS PADS SPEAK ");
    if (route::is_slot(speaks)) {
      put(n, "WITH SLOT ");
      out[n++] = static_cast<char>('1' + ((speaks - route::kFirstSlot) & 3u));
    } else {
      put(n, "OVER LIVE");
    }
  }
  out[n] = '\0';
}

// F5's value: "OFF", "ON", or "ON SL 5" while a pad repeats that slice (`slice1` is the slice + 1, 0 none; the key shows about 8 characters, so "ON SL 12" at most).
inline void loop_text(bool on, unsigned slice1, char (&out)[12]) noexcept {
  const char* t = on ? "ON" : "OFF";
  unsigned n = 0;
  for (; t[n] != '\0'; ++n) {
    out[n] = t[n];
  }
  if (on && slice1 != 0u) {
    const char* w = " SL ";
    for (unsigned i = 0; w[i] != '\0'; ++i) {
      out[n++] = w[i];
    }
    if (slice1 >= 10u) {
      out[n++] = static_cast<char>('0' + (slice1 / 10u) % 10u);
    }
    out[n++] = static_cast<char>('0' + slice1 % 10u);
  }
  out[n] = '\0';
}

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

// The reader a slice of `slot` plays on; -1 when none can. `choke` (SLICES MONO): the slot's own reader, which a new slice retriggers. SLICES POLY: the own
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
