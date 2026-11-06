// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// OR NOIR, the app's one skin (docs/UI-DESIGN.md): a black pane, a warm-chrome panel, amber (PLAY) for what speaks, ice blue (SEL) for what
// sounds, and the pinhole look of lit dots. The screens that wear it are Chrome::None (800 x 480, no kit header, no kit soft-key row): the
// header and the F row are drawn here as pure functions of a small state, in two halves because the Renderer and the Canvas may not
// interleave: *_shapes() inside one Canvas section, *_text() outside it. Nothing here keeps state, allocates or reads the engine.
//
// Pixels (800 x 480): header 0..38 with its rules at 38 and 39; the voiceprint frame (240,48,320,224); SPEAKS left of it and SOUNDS right;
// the pads 64..784 x 292..389; the hints at 391; the F row 404..480 (the runtime's system strip draws over its bottom 28). Nothing that
// matters is below y 452.

#include <o3sdk/ui/plot/canvas.h>
#include <o3sdk/ui/renderer.h>
#include <o3sdk/ui/theme.h>

#include <cstdint>

namespace o3_voc03er::ornoir {

// The tokens, as the theme gives them (theme()): tests/skin_theme_test.cpp holds them to the kit's kContrastPairs.
const o3sdk::ui::Theme& theme() noexcept;

// A filled rectangle in a theme token, for screens that draw their own body (SAMPLE REC).
void fill_rect(o3sdk::ui::plot::Canvas& c, int x, int y, int w, int h, o3sdk::ui::Color k) noexcept;

// ---- header -------------------------------------------------------------------------------------------------------------------------

struct HeaderState {
  const char* speaks = "";  // the signal path: what speaks (the audio source) ...
  const char* sounds = "";  // ... and what sounds (the carrier); drawn "SPEAKS > SOUNDS" as two chips and a chevron
  const char* mode = "";    // BANDS, TALK, TUNE
  bool playing = false;
  unsigned tempo_x10 = 1200;  // 120.0 BPM
  unsigned cpu_percent = 0;
};

// The width of a chip for `text` (8 px a character in the Label face, plus 20), the chevron's room, the chip of SOUNDS' x.
int chip_width(const char* text) noexcept;
int sounds_chip_x(const char* speaks) noexcept;
// "120.0" into out[8]; returns its length.
unsigned format_tempo(unsigned tempo_x10, char* out) noexcept;
// "37%" into out[8].
unsigned format_percent(unsigned percent, char* out) noexcept;

void header_shapes(o3sdk::ui::plot::Canvas& c, const HeaderState& h) noexcept;
void header_text(o3sdk::ui::Renderer& r, const HeaderState& h) noexcept;  // includes the wordmark (banded under a scissor)

// ---- F row --------------------------------------------------------------------------------------------------------------------------

enum class KeyRole : std::uint8_t { Plain, Voice, Carrier };  // the value's colour: Text, Play (what speaks), Sel (what sounds)

struct FKey {
  const char* name = "";
  char value[12] = {};
  int pct = -1;  // 0..100: the little bar under the value; -1 none
  bool hot = false;  // the latched slot: the window turns Accent
  bool dim = false;  // a value that does not exist yet (the SHIFT row's waiting keys): name and value in the dim ink
  KeyRole role = KeyRole::Plain;
};
inline constexpr unsigned kFKeys = 8;

void frow_shapes(o3sdk::ui::plot::Canvas& c, const FKey (&keys)[kFKeys]) noexcept;
void frow_text(o3sdk::ui::Renderer& r, const FKey (&keys)[kFKeys]) noexcept;

// ---- voiceprint and ladders ---------------------------------------------------------------------------------------------------------

inline constexpr unsigned kBandCount = 16;

struct Voiceprint {
  std::uint8_t level[kBandCount] = {};  // 0..255 (0 is -60 dB, 255 is 0 dBFS), the unit's View
  std::uint8_t peak[kBandCount] = {};   // the held peaks, same scale
  bool bars = true;                     // false (TALK, TUNE): the frame and the unlit grid only
};

struct Ladder {
  std::uint8_t level = 0;  // 0..255
  std::uint8_t peak = 0;
};

// Rows of the voiceprint a level lights (1..22) and of a held peak (0..21): the arithmetic the shapes use, for the tests.
unsigned bar_rows(std::uint8_t level) noexcept;
unsigned peak_rows(std::uint8_t peak) noexcept;
unsigned ladder_height(std::uint8_t level, unsigned full_px) noexcept;  // whole 6 px rows

// ---- SPEAKS and SOUNDS columns ------------------------------------------------------------------------------------------------------

// A picker over a column (docs/UI-DESIGN.md section 3, PERFORM): up to kPickerRows rows, the cursor one framed hot, the row in force marked, a dimmed
// row with its reason in `note`. Plain bytes, no initialisers: the screen keeps one in the SDRAM (O3SDK_BIG, zero at start).
inline constexpr unsigned kPickerRows = 14;
struct PickerRow {
  char name[12];
  char note[16];  // a dimmed row: why (CAPTURE FIRST); else the source or the slot's state
  bool dimmed;
  bool on;  // the value in force
};
struct PickerView {
  unsigned count;
  unsigned cursor;
  PickerRow row[kPickerRows];
};

struct SpeaksState {
  const char* source = "";  // the chip: what speaks (MIC LOW, SLOT 2 ...)
  const char* live = nullptr;  // the ADC's source under LIVE (null: `source`)
  const char* capture = "";    // the capture chip under the column ("REC: CAPTURE MIC > SLOT 2"); empty: none
  std::uint8_t slot[4] = {};   // the slot boxes: 0 empty, 1 holds audio, 2 on a side, 3 recording
  const PickerView* picker = nullptr;  // open: the list replaces the column's body
  unsigned gain = 0;
  Ladder in;
  bool clipping = false;
};

struct SoundsState {
  const char* carrier = "";  // the chip: SYNTH, NOISE, LOOP1 ...
  unsigned spread = 0;       // the SPREAD value in cents
  const char* chord = "";    // the readout ("Dm7"); empty: dots off
  std::uint8_t notes[5] = {};
  unsigned note_count = 0;
  const char* voicing = "";
  Ladder out;
  int out_db = -60;
  const char* held = "";  // "HELD", "LATCHED" or ""
  const PickerView* picker = nullptr;  // open: the list replaces the column's body
};

// ---- pads ---------------------------------------------------------------------------------------------------------------------------

struct PadsView {
  const char (*label)[12] = nullptr;  // 48 labels, pad = row * 12 + col, row 0 at the top
  std::uint16_t scale_cols = 0;       // bit n: column n is in the scale (the column is tinted)
  std::uint64_t held = 0;             // bit pad: held
  int latched = -1;                   // the latched pad, or -1
  const char* row_name[4] = {"", "", "", ""};
  bool column_in_scale_known = true;
  const char* hint_left = "";
  const char* key_hint = "";  // right of the same line: what the F key under the encoder does
};

// The whole frame's shapes and text of the PERFORM screen, in the order the draw needs them. The screen calls these two once.
void perform_shapes(o3sdk::ui::plot::Canvas& c, const HeaderState& h, const FKey (&keys)[kFKeys], const Voiceprint& vp, const SpeaksState& sp,
                    const SoundsState& so, const PadsView& pads) noexcept;
void perform_text(o3sdk::ui::Renderer& r, const HeaderState& h, const FKey (&keys)[kFKeys], const Voiceprint& vp, const SpeaksState& sp,
                  const SoundsState& so, const PadsView& pads, const char* placeholder) noexcept;

// "C3", "F#4", "A-1" into out[8] (60 is C4, as the kit's help and the PLAY screen name them); returns the length.
unsigned note_name(int note, char* out) noexcept;

// Whether the atlas holds every character of `name` and it fits the readout (at most 6): else the readout is drawn in the Display face.
bool atlas_can_spell(const char* name) noexcept;

}  // namespace o3_voc03er::ornoir
