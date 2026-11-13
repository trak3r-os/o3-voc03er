// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The parameter strips of PLAYS (TP1), pure: a row of PERFORM's layout (pads_logic.h) set to a STRIP gives its twelve pads to one parameter. This file
// holds the table of the parameters that make sense played live, what each pad of a strip means, the pad LEDs, and the list F7 walks (the notes, then
// the mode's parameters by group). No engine, no screen, no heap; tests/strip_logic_test.cpp holds it. Values go through params_logic.h's KeyDef, so
// a strip and PARAMS agree on ranges and text.
//
// Shapes. Cont: twelve pads over the range, pad 1 the minimum and pad 12 the maximum (a log range spaces them by ratio). Bipolar: a range that
// crosses zero has the middle pair both 0, as BEND does. Enum: at most twelve choices take one pad each (the rest dark); more take twelve pads
// spread over the choices. Hold: a closure of the tract, held while a pad of the row is down, released on let-go (the kit's closure is on or off;
// a graded closure is a kit follow-up, see the CHANGELOG).
//
// Never on a strip, and why (kExcludedVoice and kExcludedTract list them; tests/strip_logic_test.cpp enforces it):
//   MODE      it swaps the layout the strip lives on;      QUALITY  TALK's LPC order: a cost and a reallocation, not a live gesture;
//   LEVEL     a wrong pad is a +6 dB jump, the VOL pads own it;  VIB   the MOD pad and CC1 rewrite it;
//   BEND      the BEND strip (row C) and BND pads own it;  FORMANT  its own PLAYS value on row C (and SHIFT + F5);
//   CLOSE     as a number it is the closure strips;        SLIDE, MIX  the word line and apply_mode write them;
//   the loop, region, CarSrc, ModSrc and LiveCh values belong to the pickers and SAMPLE; the project's own values (CARRIER lane) are not parameters.

#include "liveness.h"
#include "params_logic.h"

#include <cmath>
#include <cstdint>

namespace o3_voc03er::strip {

namespace pd = o3_voc03er::pads;
using o3_voc03er::kModeCount;
using o3_voc03er::kModeTract;
using params::Fmt;
using params::Kind;

enum class Shape : std::uint8_t { Cont, Bipolar, Enum, Hold };

// Tags of the stable id (pads::strip_id): the owner of the parameter.
inline constexpr unsigned kTagVoice = 1;
inline constexpr unsigned kTagTract = 2;
inline constexpr unsigned kTagApp = 3;
inline constexpr unsigned kTagHold = 4;  // a held closure: the parameter byte is the CLOSE value (1 LIPS, 2 TIP, 3 BACK)

// The groups F7's list steps through (SHIFT + the encoder jumps from one to the next).
inline constexpr unsigned kGroupNotes = 0;
inline constexpr unsigned kGroupCount = 6;
inline constexpr const char* kGroupNames[kGroupCount] = {"NOTES", "CARRIER", "SHAPE", "TUNE", "TRACT", "PLAY"};

// The mode masks are the liveness table's (liveness.h): the strips, PARAMS and PERFORM's SHIFT row read the same facts.
using live::kAllModes;
using live::kBandsTalkRobot;
using live::kNotTune;
using live::kTalkOnly;
using live::kTractOnly;
using live::kTuneOnly;
using live::mode_bit;

struct StripDef {
  const char* name;     // the row's name beside the strip (at most 8 characters: "A " and the name fit the margin)
  Shape shape;
  std::uint8_t group;   // kGroupNames
  std::uint8_t modes;   // the modes that use the parameter (mode_bit)
  params::KeyDef key;   // the parameter, its range (display values) and its format; Hold: only the owner, Tract / Close
  std::uint8_t hold;    // Hold: the CLOSE value
};

namespace detail {
constexpr params::KeyDef vk(const char* n, std::uint16_t p, Fmt f, int lo, int hi) noexcept { return {n, Kind::Value, p, f, lo, hi, 1, 1}; }
constexpr params::KeyDef tk(const char* n, std::uint16_t p, Fmt f, int lo, int hi) noexcept { return {n, Kind::Tract, p, f, lo, hi, 1, 1}; }
}  // namespace detail

inline constexpr StripDef kStrips[] = {
    // CARRIER
    {"SHAPE", Shape::Cont, 1, kNotTune, detail::vk("SHAPE", Vocoder::Wave, Fmt::Wave, 0, 200), 0},
    {"SPREAD", Shape::Cont, 1, kNotTune, detail::vk("SPREAD", Vocoder::Detune, Fmt::Int, 0, 100), 0},
    {"SUB", Shape::Cont, 1, kNotTune, detail::vk("SUB", Vocoder::Sub, Fmt::Pct, 0, 100), 0},
    {"TILT", Shape::Bipolar, 1, kNotTune, detail::vk("TILT", Vocoder::Tilt, Fmt::Db, -120, 120), 0},
    {"NOISE", Shape::Cont, 1, kNotTune, detail::vk("NOISE", Vocoder::Noise, Fmt::Pct, 0, 100), 0},
    {"ATTACK", Shape::Cont, 1, kNotTune, detail::vk("ATTACK", Vocoder::Attack, Fmt::MsLog, 1, 500), 0},
    {"RELEASE", Shape::Cont, 1, kNotTune, detail::vk("RELEASE", Vocoder::Release, Fmt::MsLog, 5, 2000), 0},
    // SHAPE
    {"FREEZE", Shape::Enum, 2, kTalkOnly, detail::vk("FREEZE", Vocoder::Freeze, Fmt::OnOff, 0, 1), 0},
    {"DRY", Shape::Cont, 2, kAllModes, detail::vk("DRY", Vocoder::Dry, Fmt::Pct, 0, 100), 0},
    {"VOICING", Shape::Enum, 2, kNotTune, params::app_key("VOICING", Fmt::Voicing, 0, 3), 0},
    // TUNE
    {"KEY", Shape::Enum, 3, kAllModes, detail::vk("KEY", Vocoder::Key, Fmt::Key, 0, 11), 0},
    {"SCALE", Shape::Enum, 3, kAllModes, detail::vk("SCALE", Vocoder::Scale, Fmt::Scale, 0, static_cast<int>(kScaleTableCount) - 1), 0},
    {"SPEED", Shape::Cont, 3, kTuneOnly, detail::vk("SPEED", Vocoder::Speed, Fmt::MsLin, 0, 500), 0},
    {"AMOUNT", Shape::Cont, 3, kTuneOnly, detail::vk("AMOUNT", Vocoder::Amount, Fmt::Pct, 0, 100), 0},
    {"NOTES IN", Shape::Enum, 3, kTuneOnly, detail::vk("NOTES IN", Vocoder::NotesIn, Fmt::OnOff, 0, 1), 0},
    // TRACT (the mouth's own table), then its three closures
    {"VOWEL", Shape::Cont, 4, kTractOnly, detail::tk("VOWEL", TractUnit::Vowel, Fmt::Pct, 0, 100), 0},
    {"TONGUE", Shape::Bipolar, 4, kTractOnly, detail::tk("TONGUE", TractUnit::Tongue, Fmt::SPct, -100, 100), 0},
    {"JAW", Shape::Bipolar, 4, kTractOnly, detail::tk("JAW", TractUnit::Jaw, Fmt::SPct, -100, 100), 0},
    {"LIPS", Shape::Bipolar, 4, kTractOnly, detail::tk("LIPS", TractUnit::Lips, Fmt::SPct, -100, 100), 0},
    {"T GLIDE", Shape::Cont, 4, kTractOnly, detail::tk("T GLIDE", TractUnit::Glide, Fmt::MsLog, 5, 500), 0},
    {"SIZE", Shape::Enum, 4, kTractOnly, detail::tk("SIZE", TractUnit::Size, Fmt::Size, 0, 4), 0},
    {"RES", Shape::Cont, 4, kTractOnly, detail::tk("RES", TractUnit::Res, Fmt::Pct, 0, 100), 0},
    {"HISS", Shape::Cont, 4, kTractOnly, detail::tk("HISS", TractUnit::Hiss, Fmt::Pct, 0, 100), 0},
    {"ACCENT", Shape::Cont, 4, kTractOnly, detail::tk("ACCENT", TractUnit::Accent, Fmt::Pct, 0, 100), 0},
    {"SOURCE", Shape::Enum, 4, kTractOnly, detail::tk("SOURCE", TractUnit::Source, Fmt::Source, 0, 1), 0},
    {"CL LIPS", Shape::Hold, 4, kTractOnly, detail::tk("CL LIPS", TractUnit::Close, Fmt::Int, 0, 3), 1},
    {"CL TIP", Shape::Hold, 4, kTractOnly, detail::tk("CL TIP", TractUnit::Close, Fmt::Int, 0, 3), 2},
    {"CL BACK", Shape::Hold, 4, kTractOnly, detail::tk("CL BACK", TractUnit::Close, Fmt::Int, 0, 3), 3},
    // PLAY (the SHIFT row's values)
    {"THRU", Shape::Cont, 5, kBandsTalkRobot, detail::vk("THRU", Vocoder::Thru, Fmt::Pct, 0, 100), 0},
    {"GLIDE", Shape::Cont, 5, kNotTune, detail::vk("GLIDE", Vocoder::Glide, Fmt::MsLin, 0, 500), 0},
    {"GATE", Shape::Enum, 5, kNotTune, detail::vk("GATE", Vocoder::Gate, Fmt::Gate, 0, 4), 0},
    {"ARP", Shape::Enum, 5, kNotTune, detail::vk("ARP", Vocoder::Arp, Fmt::Arp, 0, 4), 0},
};
inline constexpr unsigned kStripCount = sizeof kStrips / sizeof kStrips[0];

// The vocoder parameters PARAMS shows (or the shift row) that no strip may take, with the reasons in the header above.
inline constexpr std::uint16_t kExcludedVoice[] = {Vocoder::Mode, Vocoder::Quality, Vocoder::Level, Vocoder::Vib, Vocoder::Bend, Vocoder::Formant,
                                                   Vocoder::CarSrc, Vocoder::ModSrc, Vocoder::LiveCh};
inline constexpr std::uint16_t kExcludedTract[] = {TractUnit::Close, TractUnit::Slide, TractUnit::Mix};  // CLOSE only as a Hold, never as a number

constexpr unsigned tag_of(const StripDef& d) noexcept {
  return d.shape == Shape::Hold ? kTagHold : (d.key.kind == Kind::Value ? kTagVoice : (d.key.kind == Kind::Tract ? kTagTract : kTagApp));
}
// The id the project saves: tag and parameter (a Hold's parameter byte is its CLOSE value).
constexpr std::uint16_t id_of(const StripDef& d) noexcept { return pd::strip_id(tag_of(d), d.shape == Shape::Hold ? d.hold : d.key.param); }

// The strip of a saved id, or nullptr (an id from a newer file, or none).
constexpr const StripDef* find(std::uint16_t id) noexcept {
  if (id == pd::kNoStrip) {
    return nullptr;
  }
  for (unsigned i = 0; i < kStripCount; ++i) {
    if (id_of(kStrips[i]) == id) {
      return &kStrips[i];
    }
  }
  return nullptr;
}

constexpr bool in_mode(const StripDef& d, unsigned mode) noexcept { return mode < kModeCount && (d.modes & mode_bit(mode)) != 0u; }

// ---- the pads of a strip --------------------------------------------------------------------------------------------------------------

// How many pads of the strip's twelve carry a value (an enumeration of n <= 12 choices leaves the rest dark); a Hold uses all twelve.
constexpr unsigned pad_count(const StripDef& d) noexcept {
  if (d.shape == Shape::Enum) {
    const unsigned n = static_cast<unsigned>(d.key.hi - d.key.lo + 1);
    return n < 12u ? n : 12u;
  }
  return 12u;
}

// The display value (params_logic.h's units) of pad `col` 0..11; the strip must not be a Hold. A log range is spaced by ratio.
inline int pad_value(const StripDef& d, unsigned col) noexcept {
  const int lo = d.key.lo;
  const int hi = d.key.hi;
  const int c = static_cast<int>(col < 11u ? col : 11u);
  switch (d.shape) {
    case Shape::Bipolar:
      return c < 6 ? lo * (5 - c) / 5 : hi * (c - 6) / 5;  // pads 6 and 7 (columns 5 and 6) are both 0
    case Shape::Enum: {
      const int n = hi - lo + 1;
      return n <= 12 ? lo + (c < n ? c : n - 1) : lo + ((n - 1) * c + 5) / 11;
    }
    case Shape::Hold:
      return 0;
    case Shape::Cont:
      break;
  }
  if (d.key.fmt == Fmt::MsLog && lo > 0) {
    const float v = static_cast<float>(lo) * std::pow(static_cast<float>(hi) / static_cast<float>(lo), static_cast<float>(c) * (1.0f / 11.0f));
    const int r = static_cast<int>(v + 0.5f);
    return r < lo ? lo : (r > hi ? hi : r);
  }
  return lo + ((hi - lo) * c + 5) / 11;
}

// Whether pad `col` carries a value (a dark pad of a short enumeration does not).
constexpr bool pad_used(const StripDef& d, unsigned col) noexcept { return col < pad_count(d); }

// The columns lit for the current value `cur` (display): the pad nearest to it, and, on a bipolar strip, both middle pads when it is 0 (a mask of 12 bits).
inline std::uint16_t lit_mask(const StripDef& d, int cur) noexcept {
  if (d.shape == Shape::Hold) {
    return 0;
  }
  int best = 0;
  long best_dist = -1;
  for (unsigned c = 0; c < pad_count(d); ++c) {
    const long diff = static_cast<long>(pad_value(d, c)) - cur;
    const long dist = diff < 0 ? -diff : diff;
    if (best_dist < 0 || dist < best_dist) {
      best_dist = dist;
      best = pad_value(d, c);
    }
  }
  std::uint16_t m = 0;
  for (unsigned c = 0; c < pad_count(d); ++c) {
    if (pad_value(d, c) == best) {
      m = static_cast<std::uint16_t>(m | (1u << c));
    }
  }
  return m;
}

// A pad's LED: the lit pads on, the others dim, a dark pad (past a short enumeration) off. A held pad is lit by the screen.
inline std::uint8_t pad_led(const StripDef& d, unsigned col, std::uint16_t lit) noexcept {
  if (!pad_used(d, col)) {
    return o3sdk::app::kLedOff;
  }
  return ((lit >> col) & 1u) != 0u ? o3sdk::app::kLedOn : o3sdk::app::kLedDim;
}

// The text of pad `col` (at most 11 characters): the value as PARAMS writes it; a Hold has its closure's name.
inline void pad_text(const StripDef& d, unsigned col, char (&out)[12]) noexcept {
  out[0] = '\0';
  if (d.shape == Shape::Hold) {
    const char* n = perform::kMouthLabel[8u + d.hold];  // LIPS, TIP, BACK
    std::size_t i = 0;
    while (n[i] != '\0' && i + 1u < sizeof out) {
      out[i] = n[i];
      ++i;
    }
    out[i] = '\0';
    return;
  }
  if (!pad_used(d, col)) {
    return;
  }
  params::format_value(d.key, pad_value(d, col), out);
}

// The current value as text (the flash line and the row's readout).
inline void value_text(const StripDef& d, int cur, char (&out)[12]) noexcept {
  params::format_value(d.key, cur, out);
}

// "VOWEL IN TRACT" for a strip that only one mode uses, else "SIZE NOT IN TUNE": the line when a pad of a dark strip is pressed.
inline void elsewhere_text(const StripDef& d, unsigned mode, char (&out)[40]) noexcept {
  std::size_t n = 0;
  const auto put = [&out, &n](const char* s) noexcept {
    while (*s != '\0' && n + 1u < sizeof out) {
      out[n++] = *s++;
    }
    out[n] = '\0';
  };
  put(d.name);
  unsigned bits = 0;
  unsigned only = 0;
  for (unsigned m = 0; m < kModeCount; ++m) {
    if (in_mode(d, m)) {
      ++bits;
      only = m;
    }
  }
  if (bits == 1u) {
    put(" IN ");
    put(perform::kModeNames[only]);
  } else {
    put(" NOT IN ");
    put(perform::kModeNames[mode < kModeCount ? mode : 0u]);
  }
}

// ---- F7's list: the notes, then the mode's parameters by group ------------------------------------------------------------------------

// What an entry of the list is: a PLAYS value (strip id 0) or a strip.
struct Entry {
  pd::Plays plays = pd::Plays::Note;
  std::uint16_t strip = pd::kNoStrip;
  unsigned group = kGroupNotes;
};

// The number of entries for row `r` in `mode`: the row's notes (pads::order_count, OFF last of them), then every strip the mode uses.
constexpr unsigned entry_count(pd::Row r, unsigned mode) noexcept {
  unsigned n = pd::order_count(r);
  for (unsigned i = 0; i < kStripCount; ++i) {
    n += in_mode(kStrips[i], mode) ? 1u : 0u;
  }
  return n;
}

constexpr Entry entry_at(pd::Row r, unsigned mode, unsigned index) noexcept {
  Entry e;
  const unsigned notes = pd::order_count(r);
  if (index < notes) {
    e.plays = pd::order_of(r)[index];
    return e;
  }
  unsigned k = index - notes;
  for (unsigned i = 0; i < kStripCount; ++i) {
    if (!in_mode(kStrips[i], mode)) {
      continue;
    }
    if (k == 0u) {
      e.plays = pd::Plays::Strip;
      e.strip = id_of(kStrips[i]);
      e.group = kStrips[i].group;
      return e;
    }
    --k;
  }
  return e;
}

// The place of a row's value in the list (a strip the mode does not use, or an unknown one, is taken as the first entry).
constexpr unsigned entry_index(pd::Row r, unsigned mode, pd::Plays p, std::uint16_t strip) noexcept {
  const unsigned n = entry_count(r, mode);
  for (unsigned i = 0; i < n; ++i) {
    const Entry e = entry_at(r, mode, i);
    if (e.plays == p && (p != pd::Plays::Strip || e.strip == strip)) {
      return i;
    }
  }
  return 0;
}

// The encoder: `delta` places, stopping at the ends. A tap: the next, wrapping.
constexpr unsigned entry_step(pd::Row r, unsigned mode, unsigned cur, int delta) noexcept {
  const int n = static_cast<int>(entry_count(r, mode));
  int i = static_cast<int>(cur) + delta;
  i = i < 0 ? 0 : (i > n - 1 ? n - 1 : i);
  return static_cast<unsigned>(i);
}
constexpr unsigned entry_tap(pd::Row r, unsigned mode, unsigned cur) noexcept { return (cur + 1u) % entry_count(r, mode); }

// SHIFT + the encoder: the first entry of the next group (delta > 0; the last group stays) or of the group before (delta < 0: the start of the current
// group when inside one, else the previous group's start).
constexpr unsigned entry_jump(pd::Row r, unsigned mode, unsigned cur, int delta) noexcept {
  const unsigned n = entry_count(r, mode);
  const unsigned g = entry_at(r, mode, cur).group;
  if (delta > 0) {
    for (unsigned i = cur + 1u; i < n; ++i) {
      if (entry_at(r, mode, i).group != g) {
        return i;
      }
    }
    return n - 1u;
  }
  unsigned start = cur;
  while (start > 0u && entry_at(r, mode, start - 1u).group == g) {
    --start;
  }
  if (start < cur) {
    return start;
  }
  if (start == 0u) {
    return 0;
  }
  const unsigned prev = entry_at(r, mode, start - 1u).group;
  unsigned i = start - 1u;
  while (i > 0u && entry_at(r, mode, i - 1u).group == prev) {
    --i;
  }
  return i;
}

// Sets row `r` of `l` to entry `index` of the mode's list.
constexpr void apply_entry(pd::Layout& l, pd::Row r, unsigned mode, unsigned index) noexcept {
  const Entry e = entry_at(r, mode, index);
  if (e.plays == pd::Plays::Strip) {
    (void)pd::set_strip(l, r, e.strip);
  } else {
    (void)pd::set_plays(l, r, e.plays);
  }
}

// The name of what a row plays, for the row's label and the F7 key (a strip's own name, else the PLAYS value's).
constexpr const char* row_name(const pd::Layout& l, pd::Row r) noexcept {
  const pd::Plays p = l.row[pd::idx(r)];
  if (p == pd::Plays::Strip) {
    const StripDef* d = find(l.strip[pd::idx(r)]);
    return d != nullptr ? d->name : "PARAM";
  }
  return pd::kPlaysShort[pd::idx(p)];
}

namespace detail {
// Appends `s` to `out` (capacity `cap`, always terminated) from `n` on; returns the new length.
constexpr std::size_t cat(char* out, std::size_t cap, std::size_t n, const char* s) noexcept {
  for (; *s != '\0' && n + 1 < cap; ++s) {
    out[n++] = *s;
  }
  out[n] = '\0';
  return n;
}
constexpr std::size_t cat_uint(char* out, std::size_t cap, std::size_t n, unsigned v) noexcept {
  char d[12] = {};
  std::size_t k = 0;
  do {
    d[k++] = static_cast<char>('0' + v % 10u);
    v /= 10u;
  } while (v != 0u && k < sizeof d - 1u);
  while (k != 0u && n + 1 < cap) {
    out[n++] = d[--k];
  }
  out[n] = '\0';
  return n;
}
}  // namespace detail

// F7's value on PERFORM (FX1): the row it acts on and what that row plays, "A VOWEL", "B 7TH", "C MOUTH".
constexpr void plays_label(const pd::Layout& l, pd::Row r, char (&out)[16]) noexcept {
  std::size_t n = detail::cat(out, sizeof out, 0, pd::kRowNames[pd::idx(r)]);
  n = detail::cat(out, sizeof out, n, " ");
  (void)detail::cat(out, sizeof out, n, row_name(l, r));
}

// The flash while the encoder or a tap moves F7: what the row plays now and its place in the list, "VOWEL 7/30" (entry `index` of `total`, one-based).
constexpr void plays_position(const pd::Layout& l, pd::Row r, unsigned index, unsigned total, char (&out)[16]) noexcept {
  std::size_t n = detail::cat(out, sizeof out, 0, row_name(l, r));
  n = detail::cat(out, sizeof out, n, " ");
  n = detail::cat_uint(out, sizeof out, n, index + 1u);
  n = detail::cat(out, sizeof out, n, "/");
  (void)detail::cat_uint(out, sizeof out, n, total);
}

// ---- layouts per mode ---------------------------------------------------------------------------------------------------------------------

// The default of a mode: every row does something audible or visible out of the box, with the mode's default values, and a mode that sounds from notes
// has a note row (the maintainer's "whatever makes most sense", 2026-11-13):
//   BANDS  A NOTE, B TRIAD, C FORMANT    melody, chords and the formant
//   TALK   A NOTE, B 7TH,   C FORMANT    talkbox lines and seventh chords
//   TUNE   A NOTE, B SCALE, C SPEED      NOTES IN defaults to ON, so the pads' notes are the targets; the scale and the retune speed are strips (KEY
//                                        is silent at the default CHROM scale, SCALE is not)
//   ROBOT  A NOTE, B SHAPE, C FORMANT    ROBOT is BANDS with a leveller, so the carrier's SHAPE and the formant both act
//   TRACT  A NOTE, B TONGUE, C MOUTH     SOURCE defaults to PADS (it picks where the tube's shape comes from, pads or the live voice, not the
//                                        pitch): the notes pitch the voice, the tongue and the MOUTH strip (the vowels and the three closures) shape the tube
// A saved project keeps its own layouts; F4 NEW puts these back.
inline constexpr unsigned kModeTalk = 1;
inline constexpr unsigned kModeTune = 2;
inline constexpr unsigned kModeRobot = 3;
inline constexpr std::uint16_t kIdVowel = pd::strip_id(kTagTract, TractUnit::Vowel);
inline constexpr std::uint16_t kIdTongue = pd::strip_id(kTagTract, TractUnit::Tongue);
inline constexpr std::uint16_t kIdTip = pd::strip_id(kTagHold, 2);
inline constexpr std::uint16_t kIdScale = pd::strip_id(kTagVoice, Vocoder::Scale);
inline constexpr std::uint16_t kIdSpeed = pd::strip_id(kTagVoice, Vocoder::Speed);
inline constexpr std::uint16_t kIdShape = pd::strip_id(kTagVoice, Vocoder::Wave);
constexpr pd::Layout default_layout(unsigned mode) noexcept {
  pd::Layout l;  // A NOTE, B TRIAD, C FORMANT
  switch (mode) {
    case kModeTalk:
      l.row[1] = pd::Plays::Seventh;
      break;
    case kModeTune:
      (void)pd::set_strip(l, pd::Row::B, kIdScale);
      (void)pd::set_strip(l, pd::Row::C, kIdSpeed);
      break;
    case kModeRobot:
      (void)pd::set_strip(l, pd::Row::B, kIdShape);
      break;
    case kModeTract:
      (void)pd::set_strip(l, pd::Row::B, kIdTongue);
      l.row[2] = pd::Plays::Mouth;
      break;
    default:
      break;
  }
  return l;
}

// A saved layout is acceptable when every row's value is offered there and every strip id names a strip.
constexpr bool layout_valid(const pd::Layout& l) noexcept {
  for (unsigned r = 0; r < pd::kRows; ++r) {
    if (!pd::offered(static_cast<pd::Row>(r), l.row[r])) {
      return false;
    }
    if (l.row[r] == pd::Plays::Strip ? find(l.strip[r]) == nullptr : l.strip[r] != pd::kNoStrip) {
      return false;
    }
  }
  return true;
}

// A layout read from a file: a STRIP row whose id this build does not know (a newer image's strip) drops to the mode's default row, the others stay.
constexpr pd::Layout sanitize(pd::Layout l, unsigned mode) noexcept {
  const pd::Layout def = default_layout(mode);
  for (unsigned r = 0; r < pd::kRows; ++r) {
    const bool bad = l.row[r] == pd::Plays::Strip ? find(l.strip[r]) == nullptr : l.strip[r] != pd::kNoStrip;
    if (bad || !pd::offered(static_cast<pd::Row>(r), l.row[r])) {
      l.row[r] = def.row[r];
      l.strip[r] = def.strip[r];
    }
  }
  return l;
}

}  // namespace o3_voc03er::strip
