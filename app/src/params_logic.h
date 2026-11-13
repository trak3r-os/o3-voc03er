// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PARAMS' pure logic (docs/UI-DESIGN.md, "PARAMS"): the table of every value, the per-mode pages (the layouts), which key the encoder
// drives, how a key steps and how its value reads. Apart from the screen so a host test (tests/params_test.cpp) holds it. Nothing here allocates or
// reads the engine; values go through the unit's own parameter ids, in the units PERFORM writes them (so the two screens never disagree).
//
// A key is blank (None) or live: Value (an engine parameter of the vocoder), Tract (a parameter of the tract unit: the screen knows its id) or App (a setting the app holds, because the unit's table is full: VOICING, kept by PerformScreen). A live
// key has a "display value", an int: a percent for a 0..1 parameter, tenths of a dB for the level, otherwise the parameter itself.

#include "liveness.h"
#include "perform_logic.h"

#include <o3_voc03er/units/tract/tract.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <cstddef>
#include <cstdint>

namespace o3_voc03er::params {

inline constexpr unsigned kPages = 4;
inline constexpr unsigned kTractPage = 3;  // the page of the tract unit's parameters (Kind::Tract)
inline constexpr unsigned kKeys = 8;

enum class Kind : std::uint8_t { None, Value, App, Tract };
// How a live key reads and steps.
enum class Fmt : std::uint8_t {
  Pct,      // a 0..1 parameter as 0..100 %
  SPct,     // a -1..+1 parameter as -100..+100 %, written with its sign
  Size,     // TRACT's SIZE, 0..4 (BIG, LARGE, NORMAL, SMALL, TINY), clamped
  Wave,     // SHAPE, in hundredths of Wave's 0..2: SAW, PULSE and NARROW by name, the morph between them as a number
  MsLog,    // milliseconds on a log curve: a step is a tenth of the value (a third coarse), at least 1
  MsLin,    // milliseconds, a fixed step
  Db,       // tenths of a dB
  Int,      // the parameter itself, clamped
  Mode,     // 0..kModeCount - 1 (BANDS, TALK, TUNE, ROBOT, TRACT), clamped
  Key,      // 0..11, round
  Scale,    // 0..kScaleTableCount - 1, round
  OnOff,    // 0 / 1
  Voicing,  // 0..3 (CLOSE, OPEN, WIDE, SMOOTH: the chords' Voicing), clamped
  Source,   // TRACT's SOURCE, 0 / 1 (PADS, LIVE): what drives the tube's shape, round
  Semi,     // FORMANT, -12..+12 semitones, written with its sign
  Gate,     // GATE, 0..4 (OFF, 1/4, 1/8, 1/16, 1/32), round
  Arp,      // ARP, 0..4 (OFF, UP, DOWN, UP-DOWN, RANDOM), round
};

struct KeyDef {
  const char* name;
  Kind kind;
  std::uint16_t param;  // Vocoder::Param (Value) or TractUnit::Param (Tract); an App key has no parameter
  Fmt fmt;
  int lo, hi;           // display values
  int step, coarse;     // per detent, and per detent with SHIFT (and a second tap on the key)
};

inline constexpr KeyDef kBlank{"", Kind::None, 0, Fmt::Int, 0, 0, 0, 0};
inline constexpr KeyDef app_key(const char* name, Fmt fmt, int lo, int hi) noexcept { return KeyDef{name, Kind::App, 0, fmt, lo, hi, 1, 1}; }

inline constexpr const char* kSizeNames[5] = {"BIG", "LARGE", "NORMAL", "SMALL", "TINY"};
inline constexpr const char* kMouthSourceNames[2] = {"PADS", "LIVE"};

// The table below is the catalogue of every value PARAMS can set (and a controller can learn): the fixed pages the project file and the saved
// bindings were written against, and the coordinates (page, key) a binding names. The screen shows it through a per-mode layout (further down): only
// what the mode hears, in pages of its own by meaning. SHAPE is Wave as a morph, SPREAD is Detune, SUB and TILT the carrier's (U10). VOICING is the app's
// own (Kind::App).
inline constexpr KeyDef kTable[kPages][kKeys] = {
    {{"SHAPE", Kind::Value, Vocoder::Wave, Fmt::Wave, 0, 200, 5, 25},
     {"SPREAD", Kind::Value, Vocoder::Detune, Fmt::Int, 0, 100, 1, 10},
     {"SUB", Kind::Value, Vocoder::Sub, Fmt::Pct, 0, 100, 2, 10},
     {"TILT", Kind::Value, Vocoder::Tilt, Fmt::Db, -120, 120, 5, 20},
     {"NOISE", Kind::Value, Vocoder::Noise, Fmt::Pct, 0, 100, 2, 10},
     {"ATTACK", Kind::Value, Vocoder::Attack, Fmt::MsLog, 1, 500, 1, 1},
     {"RELEASE", Kind::Value, Vocoder::Release, Fmt::MsLog, 5, 2000, 1, 1},
     {"LEVEL", Kind::Value, Vocoder::Level, Fmt::Db, -480, 60, 5, 60}},
    {{"MODE", Kind::Value, Vocoder::Mode, Fmt::Mode, 0, static_cast<int>(kModeCount) - 1, 1, 1},
     {"QUALITY", Kind::Value, Vocoder::Quality, Fmt::Int, 8, 24, 1, 4},
     {"FREEZE", Kind::Value, Vocoder::Freeze, Fmt::OnOff, 0, 1, 1, 1},
     {"DRY", Kind::Value, Vocoder::Dry, Fmt::Pct, 0, 100, 2, 10},
     app_key("VOICING", Fmt::Voicing, 0, 3),
     // LT1: what drives TRACT's tube, the pads and the lane or the live voice at the input. Not saved (the project keeps TRACT's first eight values).
     {"MOUTH", Kind::Tract, TractUnit::Source, Fmt::Source, 0, 1, 1, 1},
     // UX2: the values PERFORM's SHIFT row and the strips set, so PARAMS shows (and a controller learns) all of them.
     {"FORMANT", Kind::Value, Vocoder::Formant, Fmt::Semi, -12, 12, 1, 4},
     {"THRU", Kind::Value, Vocoder::Thru, Fmt::Pct, 0, 100, 5, 20}},
    {{"KEY", Kind::Value, Vocoder::Key, Fmt::Key, 0, 11, 1, 1},
     {"SCALE", Kind::Value, Vocoder::Scale, Fmt::Scale, 0, static_cast<int>(kScaleTableCount) - 1, 1, 1},
     {"SPEED", Kind::Value, Vocoder::Speed, Fmt::MsLin, 0, 500, 5, 25},
     {"AMOUNT", Kind::Value, Vocoder::Amount, Fmt::Pct, 0, 100, 2, 10},
     {"NOTES IN", Kind::Value, Vocoder::NotesIn, Fmt::OnOff, 0, 1, 1, 1},
     {"GLIDE", Kind::Value, Vocoder::Glide, Fmt::MsLin, 0, 500, 10, 50},
     {"GATE", Kind::Value, Vocoder::Gate, Fmt::Gate, 0, 4, 1, 1},
     {"ARP", Kind::Value, Vocoder::Arp, Fmt::Arp, 0, 4, 1, 1}},
    // TRACT's mouth (the tract unit's own table): settable in every mode, it speaks in TRACT only (dim elsewhere, liveness.h).
    {{"VOWEL", Kind::Tract, TractUnit::Vowel, Fmt::Pct, 0, 100, 1, 10},
     {"TONGUE", Kind::Tract, TractUnit::Tongue, Fmt::SPct, -100, 100, 2, 10},
     {"JAW", Kind::Tract, TractUnit::Jaw, Fmt::SPct, -100, 100, 2, 10},
     {"LIPS", Kind::Tract, TractUnit::Lips, Fmt::SPct, -100, 100, 2, 10},
     {"GLIDE", Kind::Tract, TractUnit::Glide, Fmt::MsLog, 5, 500, 1, 1},
     {"SIZE", Kind::Tract, TractUnit::Size, Fmt::Size, 0, 4, 1, 1},
     {"RES", Kind::Tract, TractUnit::Res, Fmt::Pct, 0, 100, 2, 10},
     {"HISS", Kind::Tract, TractUnit::Hiss, Fmt::Pct, 0, 100, 2, 10}},
};

inline constexpr const KeyDef& key_def(unsigned page, unsigned key) noexcept { return kTable[page % kPages][key % kKeys]; }

// The modes that hear key `k` (liveness.h, the one table the strips and PERFORM's SHIFT row read too): a Value key by its parameter, a Tract key in TRACT
// only, the app's VOICING where the carrier plays. A key that is no parameter at all (blank, MASTER's own rows) is heard everywhere.
inline constexpr std::uint8_t key_modes(const KeyDef& k, unsigned car_src = 0) noexcept {
  switch (k.kind) {
    case Kind::Value:
      return live::voice_modes(k.param, car_src);
    case Kind::Tract:
      return live::kTractOnly;
    case Kind::App:
      return live::kVoicing;
    case Kind::None:
      break;
  }
  return live::kAllModes;
}

// Whether a key is heard in mode `mode` (mode_index) with carrier source `car_src`: the layout below shows exactly these keys. MASTER's rows (param 0)
// never come here.
inline constexpr bool key_live(const KeyDef& k, unsigned mode, unsigned car_src = 0) noexcept { return live::heard(key_modes(k, car_src), mode); }

inline constexpr bool live_kind(Kind k) noexcept { return k == Kind::Value || k == Kind::App || k == Kind::Tract; }

inline constexpr bool focusable(unsigned page, unsigned key) noexcept { return key < kKeys && live_kind(key_def(page, key).kind); }

// ML1, MIDI learn (SHIFT + an F key): only a unit parameter can be bound to a controller, the vocoder's (Value) or the tract unit's (Tract). An App key
// (VOICING: PerformScreen's own setting) and a blank key cannot; their SHIFT cell reads "-". MODE and QUALITY are not live gestures either
// (MODE swaps the layout and resets the engine, QUALITY reallocates TALK's LPC order: the same two the strips refuse, strip_logic's kExcludedVoice), so a
// controller cannot move them. LEVEL may be learned: pickup stops a jump.
inline constexpr bool live_safe_voice(std::uint16_t param) noexcept { return param != Vocoder::Mode && param != Vocoder::Quality; }
inline constexpr bool learnable(unsigned page, unsigned key) noexcept {
  if (key >= kKeys) {
    return false;
  }
  const KeyDef& d = key_def(page, key);
  return d.kind == Kind::Tract || (d.kind == Kind::Value && live_safe_voice(d.param));
}

// What SHIFT + F does with key `key` when `armed_here` says learn is armed on this very key (this page): arm it, cancel it (the same keys again), or
// nothing a controller can bind (the screen says CANNOT LEARN).
enum class LearnTap : std::uint8_t { Cannot, Arm, Cancel };
inline constexpr LearnTap learn_tap(bool can, bool armed_here) noexcept { return !can ? LearnTap::Cannot : (armed_here ? LearnTap::Cancel : LearnTap::Arm); }

// ---- the per-mode pages ---------------------------------------------------------------------------------------------------------------
//
// PARAMS is the in-depth editor of the mode PERFORM is in, and it shows only what that mode hears (key_live, with the carrier source). The pages are
// written by meaning, one fixed definition each (a PageDef: a one-word tab and eight cells, each the table coordinate (page, key) of a value or blank),
// and a mode is a list of them. A cell whose value the mode does not hear is blank, so a key keeps its page and its cell whatever the carrier source
// or the mode is: SPEED has a reserved cell on NOTES (blank until the carrier is VOICE, a real cell on TUNE), QUALITY and FREEZE are TALK's two cells
// at the end of VOICE. Blank cells may sit anywhere; a page never holds a single key. MODE is not here: PERFORM's F1 changes the mode (the screen's
// mode is PERFORM's). The binding of a MIDI controller and the project file name the table coordinate, never a position in a layout.
//
//   CARRIER  SHAPE SPREAD SUB TILT NOISE ATTACK RELEASE GLIDE         BANDS TALK ROBOT TRACT
//   VOICE    FORMANT THRU DRY LEVEL QUALITY FREEZE                    BANDS TALK ROBOT (QUALITY and FREEZE: TALK)
//   NOTES    KEY SCALE SPEED GATE ARP VOICING                         BANDS TALK ROBOT TRACT
//   TUNE     KEY SCALE SPEED AMOUNT NOTES IN DRY LEVEL                TUNE
//   MOUTH    VOWEL TONGUE JAW LIPS T GLIDE SIZE RES HISS              TRACT
//   MIX      MOUTH (what drives the tube) DRY LEVEL                   TRACT

inline constexpr unsigned kMaxTabs = 4;

struct Cell {
  std::uint8_t page;  // kTable's page; kPages and above: blank
  std::uint8_t key;
};
inline constexpr Cell kNoCell{0xFFu, 0xFFu};
constexpr bool blank(Cell c) noexcept { return c.page >= kPages || c.key >= kKeys; }
constexpr bool same(Cell a, Cell b) noexcept { return a.page == b.page && a.key == b.key; }
constexpr const KeyDef& def_of(Cell c) noexcept { return key_def(c.page, c.key); }

// The name a cell shows: the table's, except TRACT's GLIDE (the mouth's own), which would read like the carrier's GLIDE.
constexpr const char* view_name(Cell c) noexcept { return c.page == 3u && c.key == 4u ? "T GLIDE" : def_of(c).name; }

struct PageDef {
  const char* name;  // the tab, one word
  Cell cell[kKeys];
};

inline constexpr PageDef kCarrierPage{"CARRIER", {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {2, 5}}};  // SHAPE SPREAD SUB TILT NOISE ATTACK RELEASE GLIDE
inline constexpr PageDef kVoicePage{"VOICE", {{1, 6}, {1, 7}, {1, 3}, {0, 7}, {1, 1}, {1, 2}, kNoCell, kNoCell}};      // FORMANT THRU DRY LEVEL QUALITY FREEZE
inline constexpr PageDef kNotesPage{"NOTES", {{2, 0}, {2, 1}, {2, 2}, {2, 6}, {2, 7}, {1, 4}, kNoCell, kNoCell}};      // KEY SCALE SPEED GATE ARP VOICING
inline constexpr PageDef kTunePage{"TUNE", {{2, 0}, {2, 1}, {2, 2}, {2, 3}, {2, 4}, {1, 3}, {0, 7}, kNoCell}};         // KEY SCALE SPEED AMOUNT NOTES-IN DRY LEVEL
inline constexpr PageDef kMouthPage{"MOUTH", {{3, 0}, {3, 1}, {3, 2}, {3, 3}, {3, 4}, {3, 5}, {3, 6}, {3, 7}}};         // VOWEL TONGUE JAW LIPS T-GLIDE SIZE RES HISS
inline constexpr PageDef kMixPage{"MIX", {{1, 5}, {1, 3}, {0, 7}, kNoCell, kNoCell, kNoCell, kNoCell, kNoCell}};        // MOUTH DRY LEVEL

inline constexpr unsigned kTuneMode = 2;  // the unit's Mode value of TUNE (liveness.h's kTuneOnly)
static_assert(live::kTuneOnly == live::mode_bit(kTuneMode), "TUNE is mode 2 in the liveness table too");
inline constexpr unsigned kTractMode = o3_voc03er::kModeTract;
static_assert(live::kTractOnly == live::mode_bit(kTractMode), "TRACT is the mode of the tract-only keys");

// The pages of a mode, in the order the tabs run: how many, and which definitions.
constexpr unsigned mode_pages(unsigned mode, const PageDef* (&out)[kMaxTabs]) noexcept {
  for (const PageDef*& p : out) {
    p = nullptr;
  }
  if (mode == kTuneMode) {
    out[0] = &kTunePage;
    return 1;
  }
  if (mode == kTractMode) {
    out[0] = &kCarrierPage;
    out[1] = &kMouthPage;
    out[2] = &kNotesPage;
    out[3] = &kMixPage;
    return 4;
  }
  out[0] = &kCarrierPage;  // BANDS, TALK, ROBOT
  out[1] = &kVoicePage;
  out[2] = &kNotesPage;
  return 3;
}

struct Layout {
  std::uint8_t pages;  // 1..kMaxTabs
  std::uint8_t count;  // the cells shown in all
  Cell cell[kMaxTabs][kKeys];
  const char* name[kMaxTabs];  // the tabs
};

constexpr Layout build_layout(unsigned mode, bool voice_carrier) noexcept {
  Layout l{};
  const PageDef* def[kMaxTabs] = {};
  const unsigned n = mode_pages(mode, def);
  unsigned shown = 0;
  for (unsigned p = 0; p < kMaxTabs; ++p) {
    l.name[p] = p < n ? def[p]->name : "";
    for (unsigned k = 0; k < kKeys; ++k) {
      const Cell c = p < n ? def[p]->cell[k] : kNoCell;
      const bool heard = !blank(c) && key_live(def_of(c), mode, voice_carrier ? live::kCarrierVoice : 0u);
      l.cell[p][k] = heard ? c : kNoCell;
      shown += heard ? 1u : 0u;
    }
  }
  l.pages = static_cast<std::uint8_t>(n);
  l.count = static_cast<std::uint8_t>(shown);
  return l;
}

// One layout per mode and carrier source (VOICE or not): ten, in rodata.
struct Layouts {
  Layout v[kModeCount][2];
};
constexpr Layouts build_layouts() noexcept {
  Layouts all{};
  for (unsigned m = 0; m < kModeCount; ++m) {
    all.v[m][0] = build_layout(m, false);
    all.v[m][1] = build_layout(m, true);
  }
  return all;
}
inline constexpr Layouts kLayouts = build_layouts();

// The layout of mode `mode` (mode_index; held in range) with carrier source `car_src` (Vocoder::CarSrc).
constexpr const Layout& layout_of(unsigned mode, unsigned car_src) noexcept { return kLayouts.v[mode < kModeCount ? mode : 0u][car_src == live::kCarrierVoice ? 1u : 0u]; }

// What a layout shows is exactly what the mode hears, each value once (MODE aside: PERFORM changes the mode), and the pages keep their rules: checked
// for every mode and carrier.
constexpr bool shown_in_params(const KeyDef& d) noexcept { return live_kind(d.kind) && !(d.kind == Kind::Value && d.param == Vocoder::Mode); }
constexpr unsigned occurrences(const Layout& l, Cell c) noexcept {
  unsigned n = 0;
  for (unsigned p = 0; p < l.pages; ++p) {
    for (unsigned k = 0; k < kKeys; ++k) {
      n += same(l.cell[p][k], c) ? 1u : 0u;
    }
  }
  return n;
}
constexpr bool text_equal(const char* a, const char* b) noexcept {
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}
// The position of `c` on page `p` of the layout: its key index, or kKeys when the page does not show it.
constexpr unsigned position_on(const Layout& l, unsigned p, Cell c) noexcept {
  for (unsigned k = 0; k < kKeys; ++k) {
    if (same(l.cell[p][k], c)) {
      return k;
    }
  }
  return kKeys;
}
constexpr bool layouts_cover_exactly() noexcept {
  for (unsigned m = 0; m < kModeCount; ++m) {
    for (unsigned v = 0; v < 2; ++v) {
      const Layout& l = kLayouts.v[m][v];
      if (l.pages < 1u || l.pages > kMaxTabs) {
        return false;
      }
      unsigned heard = 0;
      for (unsigned p = 0; p < kPages; ++p) {
        for (unsigned k = 0; k < kKeys; ++k) {
          const KeyDef& d = key_def(p, k);
          const bool want = shown_in_params(d) && key_live(d, m, v != 0u ? live::kCarrierVoice : 0u);
          const Cell c{static_cast<std::uint8_t>(p), static_cast<std::uint8_t>(k)};
          if (occurrences(l, c) != (want ? 1u : 0u)) {
            return false;
          }
          heard += want ? 1u : 0u;
        }
      }
      if (l.count != heard) {
        return false;
      }
      for (unsigned p = 0; p < l.pages; ++p) {  // never a page with a single key (or none), a one-word tab
        unsigned keys = 0;
        for (unsigned k = 0; k < kKeys; ++k) {
          keys += blank(l.cell[p][k]) ? 0u : 1u;
        }
        if (keys < 2u || l.name[p][0] == '\0') {
          return false;
        }
        for (unsigned i = 0; l.name[p][i] != '\0'; ++i) {
          if (l.name[p][i] == ' ') {
            return false;
          }
        }
        const unsigned dry = position_on(l, p, Cell{1, 3});
        const unsigned lvl = position_on(l, p, Cell{0, 7});
        if (dry < kKeys && lvl < kKeys && dry > lvl) {  // DRY, then LEVEL
          return false;
        }
        if ((text_equal(l.name[p], "NOTES") || text_equal(l.name[p], "TUNE")) && !same(l.cell[p][0], Cell{2, 0})) {  // KEY is always F1
          return false;
        }
      }
    }
  }
  return true;
}
static_assert(layouts_cover_exactly(), "every table key a mode hears is on one of its pages once, nothing it does not hear is, no page has a single key");

// A key never moves: whatever the carrier source, a cell shown without the VOICE carrier stands in the same place with it (SPEED's reserved cell is
// the one that fills in), and a page of the same name puts the same key in the same cell in every mode.
constexpr bool cells_hold_still() noexcept {
  for (unsigned m = 0; m < kModeCount; ++m) {
    const Layout& a = kLayouts.v[m][0];
    const Layout& b = kLayouts.v[m][1];
    if (a.pages != b.pages) {
      return false;
    }
    for (unsigned p = 0; p < a.pages; ++p) {
      for (unsigned k = 0; k < kKeys; ++k) {
        if (!blank(a.cell[p][k]) && !same(a.cell[p][k], b.cell[p][k])) {
          return false;
        }
        if (blank(a.cell[p][k]) && !blank(b.cell[p][k]) && !same(b.cell[p][k], Cell{2, 2})) {  // only SPEED comes in
          return false;
        }
      }
    }
    for (unsigned n = 0; n < kModeCount; ++n) {
      const Layout& c = kLayouts.v[n][1];
      for (unsigned p = 0; p < b.pages; ++p) {
        for (unsigned q = 0; q < c.pages; ++q) {
          if (!text_equal(b.name[p], c.name[q])) {
            continue;
          }
          for (unsigned k = 0; k < kKeys; ++k) {
            if (!blank(b.cell[p][k]) && position_on(c, q, b.cell[p][k]) != kKeys && position_on(c, q, b.cell[p][k]) != k) {
              return false;
            }
          }
        }
      }
    }
  }
  return true;
}
static_assert(cells_hold_still(), "a key keeps its page and its cell whatever the carrier source or the mode is");

// A cell of the layout, or blank outside it.
constexpr Cell cell_at(const Layout& l, unsigned page, unsigned key) noexcept { return page < l.pages && key < kKeys ? l.cell[page][key] : kNoCell; }
constexpr bool focusable_at(const Layout& l, unsigned page, unsigned key) noexcept { return !blank(cell_at(l, page, key)); }
// MIDI learn (ML1) of the cell: the table's rule on its coordinates.
constexpr bool learnable_at(const Layout& l, unsigned page, unsigned key) noexcept {
  const Cell c = cell_at(l, page, key);
  return !blank(c) && learnable(c.page, c.key);
}
// The key the encoder points at when a page opens: its first one.
constexpr unsigned first_focus_at(const Layout& l, unsigned page) noexcept {
  for (unsigned k = 0; k < kKeys; ++k) {
    if (focusable_at(l, page, k)) {
      return k;
    }
  }
  return 0;
}
// Where table cell `c` stands in the layout: true and its page and key, or false when the mode does not show it.
constexpr bool find_cell(const Layout& l, Cell c, unsigned& page, unsigned& key) noexcept {
  for (unsigned p = 0; p < l.pages; ++p) {
    for (unsigned k = 0; k < kKeys; ++k) {
      if (same(l.cell[p][k], c) && !blank(c)) {
        page = p;
        key = k;
        return true;
      }
    }
  }
  return false;
}
// PARAMS pressed again, or DOWN: the next page; UP: the one before. Round, inside the layout's own pages.
constexpr unsigned next_page_in(const Layout& l, unsigned page, int delta = 1) noexcept { return perform::step_wrapped(page < l.pages ? page : 0u, delta, l.pages); }

// The tab strip under the voiceprint, the voiceprint's width (x = 26..774): `n` tabs (1..kMaxTabs) with an 8 px gap, so four are 181 wide.
inline constexpr int kTabX0 = 26;
inline constexpr int kTabsWidth = 748;
inline constexpr int kTabGap = 8;
constexpr int tab_w(unsigned n) noexcept { return (kTabsWidth - kTabGap * (static_cast<int>(n) - 1)) / static_cast<int>(n < 1u ? 1u : n); }
constexpr int tab_x(unsigned n, unsigned i) noexcept { return kTabX0 + static_cast<int>(i) * (tab_w(n) + kTabGap); }

// ---- values ---------------------------------------------------------------------------------------------------------------------------

// The unit's parameter value of a display value, and back.
inline int to_display(Fmt f, float v) noexcept {
  switch (f) {
    case Fmt::Pct:
    case Fmt::SPct:
    case Fmt::Wave:
      return static_cast<int>(v * 100.0f + (v < 0.0f ? -0.5f : 0.5f));
    case Fmt::Db:
      return static_cast<int>(v * 10.0f + (v < 0.0f ? -0.5f : 0.5f));
    default:
      return static_cast<int>(v + (v < 0.0f ? -0.5f : 0.5f));
  }
}

inline float to_param(Fmt f, int d) noexcept {
  switch (f) {
    case Fmt::Pct:
    case Fmt::SPct:
    case Fmt::Wave:
      return static_cast<float>(d) * 0.01f;
    case Fmt::Db:
      return static_cast<float>(d) * 0.1f;
    default:
      return static_cast<float>(d);
  }
}

// LEVEL's tap stops at the top: a wrap would go from the loudest (+6 dB) to -48 dB in one tap, so three taps on a focused key would go from
// normal to full to silence (MASTER's GAIN does the same, master_logic.h).
inline constexpr bool tap_holds(const KeyDef& k) noexcept { return k.kind == Kind::Value && k.param == Vocoder::Level; }

// `cur` stepped by `delta` detents (SHIFT or a second tap: `coarse`). Round keys wrap; the rest stop at their ends, unless `wrap` (a tap) is set
// and the value already stands at the end it moves towards, which sends it to the other end (not LEVEL: tap_holds).
inline int step_display(const KeyDef& k, int cur, int delta, bool coarse, bool wrap = false) noexcept {
  if (delta == 0 || !live_kind(k.kind)) {
    return cur;
  }
  wrap = wrap && !tap_holds(k);
  const int n = k.hi - k.lo + 1;
  if (k.fmt == Fmt::Key || k.fmt == Fmt::Scale || k.fmt == Fmt::OnOff || k.fmt == Fmt::Source || k.fmt == Fmt::Gate || k.fmt == Fmt::Arp) {
    int v = (cur - k.lo + delta) % n;
    return k.lo + (v < 0 ? v + n : v);
  }
  int next = cur;
  if (k.fmt == Fmt::MsLog) {
    int mag = cur / (coarse ? 3 : 10);
    mag = mag < 1 ? 1 : mag;
    next = cur + (delta > 0 ? mag : -mag) * (delta > 0 ? delta : -delta);
  } else {
    next = cur + delta * (coarse ? k.coarse : k.step);
  }
  if (wrap && delta > 0 && cur >= k.hi) {
    return k.lo;
  }
  if (wrap && delta < 0 && cur <= k.lo) {
    return k.hi;
  }
  return next < k.lo ? k.lo : (next > k.hi ? k.hi : next);
}

// The key's value as text (at most 11 characters), into out.
inline void format_value(const KeyDef& k, int d, char (&out)[12]) noexcept {
  out[0] = '\0';
  const auto put = [&out](const char* s) noexcept {
    std::size_t n = 0;
    while (out[n] != '\0') {
      ++n;
    }
    while (*s != '\0' && n + 1u < sizeof out) {
      out[n++] = *s++;
    }
    out[n] = '\0';
  };
  char num[12] = {};
  const unsigned u = static_cast<unsigned>(d < 0 ? 0 : d);
  switch (k.fmt) {
    case Fmt::Pct:
      (void)perform::format_uint(u, num);
      put(num);
      put("%");
      break;
    case Fmt::SPct:
      (void)perform::format_uint(static_cast<unsigned>(d < 0 ? -d : d), num);
      put(d < 0 ? "-" : (d > 0 ? "+" : ""));
      put(num);
      put("%");
      break;
    case Fmt::Size:
      put(kSizeNames[u < 5u ? u : 4u]);
      break;
    case Fmt::MsLog:
    case Fmt::MsLin:
      (void)perform::format_uint(u, num);
      put(num);
      put("MS");
      break;
    case Fmt::Db:
      perform::format_db(static_cast<float>(d) * 0.1f, num);
      put(num);
      break;
    case Fmt::Int:
      (void)perform::format_uint(u, num);
      put(num);
      break;
    case Fmt::Wave:
      if (u % 100u == 0u) {
        put(u == 0u ? "SAW" : (u == 100u ? "PULSE" : "NARROW"));
      } else {
        num[0] = static_cast<char>('0' + u / 100u);
        num[1] = '.';
        num[2] = static_cast<char>('0' + u / 10u % 10u);
        num[3] = static_cast<char>('0' + u % 10u);
        num[4] = '\0';
        put(num);
      }
      break;
    case Fmt::Mode:
      put(perform::kModeNames[u < kModeCount ? u : 0u]);
      break;
    case Fmt::Key:
      put(perform::kKeyNames[u % 12u]);
      break;
    case Fmt::Scale:
      put(perform::kScaleNames[u < kScaleTableCount ? u : 0u]);
      break;
    case Fmt::OnOff:
      put(d != 0 ? "ON" : "OFF");
      break;
    case Fmt::Voicing:
      put(perform::kVoicingNames[u < 4u ? u : 3u]);
      break;
    case Fmt::Source:
      put(kMouthSourceNames[u < 2u ? u : 1u]);
      break;
    case Fmt::Semi:
      (void)perform::format_uint(static_cast<unsigned>(d < 0 ? -d : d), num);
      put(d < 0 ? "-" : (d > 0 ? "+" : ""));
      put(num);
      put(" ST");
      break;
    case Fmt::Gate:
      put(perform::kGateNames[u < perform::kGateCount ? u : 0u]);
      break;
    case Fmt::Arp:
      put(perform::kArpNames[u < perform::kArpCount ? u : 0u]);
      break;
  }
}

// The value's bar under its window, 0..100, or -1 for a key that is a word.
inline int bar_of(const KeyDef& k, int d) noexcept {
  switch (k.fmt) {
    case Fmt::Pct:
    case Fmt::SPct:
    case Fmt::Wave:
    case Fmt::MsLin:
    case Fmt::Db:
    case Fmt::Int:
    case Fmt::Semi:
      return perform::bar_percent(static_cast<float>(d), static_cast<float>(k.lo), static_cast<float>(k.hi));
    case Fmt::MsLog: {  // the bar is the position in steps, so it moves evenly under the encoder
      int steps = 0, at = 0;
      for (int probe = k.lo; probe < k.hi && steps < 200; ++steps) {
        probe += probe / 10 < 1 ? 1 : probe / 10;
        if (probe <= d) {
          ++at;
        }
      }
      return steps > 0 ? at * 100 / steps : 0;
    }
    default:
      return -1;
  }
}

}  // namespace o3_voc03er::params
