// SPDX-License-Identifier: GPL-3.0-or-later
// PERFORM's pure logic (app/src/perform_logic.h) and OR NOIR's arithmetic and bitmaps (app/src/skin/).
#include <string>
#include <doctest/doctest.h>

#include "../app/src/perform_logic.h"
#include "../app/src/skin/ornoir.h"
#include "../app/src/skin/ornoir_assets.h"

#include <cstring>

using namespace o3_voc03er;
namespace pl = o3_voc03er::perform;

TEST_CASE("perform: steps clamp or wrap") {
  CHECK(pl::step_clamped(2, 5, 0, 6) == 6);
  CHECK(pl::step_clamped(1, -5, 0, 6) == 0);
  CHECK(pl::step_wrapped(11, 1, 12) == 0u);
  CHECK(pl::step_wrapped(0, -1, 12) == 11u);
  CHECK(pl::step_wrapped(3, -27, 12) == 0u);
  CHECK(pl::mode_index(0.0f) == 0u);
  CHECK(pl::mode_index(1.0f) == 1u);
  CHECK(pl::mode_index(2.0f) == 2u);
}

TEST_CASE("perform: scale columns follow the key") {
  CHECK(pl::scale_cols(0, 0) == 0xFFFu);  // chromatic
  const std::uint16_t major_c = pl::scale_cols(0, 1);
  CHECK(major_c == ((1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 11)));
  const std::uint16_t major_d = pl::scale_cols(2, 1);
  CHECK(((major_d >> 2) & 1u) == 1u);   // the tonic
  CHECK(((major_d >> 3) & 1u) == 0u);   // D# is not in D major
  CHECK(((major_d >> 6) & 1u) == 1u);   // F#
}

TEST_CASE("perform: numbers, bars and levels") {
  char s[12] = {};
  CHECK(pl::format_uint(0, s) == 1u);
  CHECK(std::strcmp(s, "0") == 0);
  CHECK(pl::format_uint(500, s) == 3u);
  CHECK(std::strcmp(s, "500") == 0);
  CHECK(pl::bar_percent(10.0f, 0.0f, 20.0f) == 50);
  CHECK(pl::bar_percent(99.0f, 0.0f, 20.0f) == 100);
  CHECK(pl::bar_percent(-1.0f, 0.0f, 20.0f) == 0);
  CHECK(pl::level_byte(0.0f) == 0);
  CHECK(pl::level_byte(1.0f) == 255);
  CHECK(pl::level_byte(0.5f) > 200);  // -6 dB
  CHECK(pl::level_byte(0.5f) < 235);
  CHECK(pl::level_byte(0.01f) < pl::level_byte(0.1f));
}

TEST_CASE("or noir: formats and rows") {
  char s[8] = {};
  ornoir::format_tempo(1200, s);
  CHECK(std::strcmp(s, "120.0") == 0);
  ornoir::format_percent(37, s);
  CHECK(std::strcmp(s, "37%") == 0);
  CHECK(ornoir::note_name(60, s) == 2u);
  CHECK(std::strcmp(s, "C4") == 0);
  CHECK(ornoir::bar_rows(0) >= 1u);
  CHECK(ornoir::bar_rows(255) == 22u);
  CHECK(ornoir::peak_rows(255) <= 21u);
  CHECK(ornoir::atlas_can_spell("Dm7"));
  CHECK_FALSE(ornoir::atlas_can_spell("Csus2x"));
  CHECK(ornoir::chip_width("LINE") > ornoir::chip_width(""));
}

TEST_CASE("or noir: the bitmaps hold the pinhole and the glyphs") {
  // coarse mask: the cell (0,0) has ink on the rim and a hole inside
  CHECK((ornoir::kMask.b[0] & 0x80u) != 0u);                   // x 0, y 0: '#'
  CHECK((ornoir::kMask.b[1 * ornoir::kMaskStride] & 0x40u) == 0u);  // x 1, y 1: hole
  CHECK((ornoir::kMask.b[5 * ornoir::kMaskStride] & 0x80u) != 0u);  // y 5: the bottom rim
  // atlas: cell 0 blank, the 'A' cell has lit dots
  unsigned blank = 0, a = 0;
  for (unsigned i = 0; i < ornoir::kCellBytes; ++i) {
    blank += ornoir::kAtlas.b[i];
    a += ornoir::kAtlas.b[ornoir::kCellBytes + i];
  }
  CHECK(blank == 0u);
  CHECK(a > 0u);
  CHECK(ornoir::glyph_cell('A') == 1u);
  CHECK(ornoir::glyph_cell('?') == 0u);
}

TEST_CASE("perform: the hint line says what every F key does and fits beside the pads' gestures") {
  for (unsigned mode = 0; mode < 3; ++mode) {
    for (unsigned focus = 0; focus < 8; ++focus) {
      const std::string hint = pl::key_hint(focus, mode);
      CHECK(hint.size() > 3u);
      CHECK(hint.size() <= pl::kKeyHintMax);
      CHECK(hint[0] == 'F');
      CHECK(hint[1] == static_cast<char>('1' + focus));
    }
  }
  CHECK(std::string(pl::key_hint(7, 1)).find("QUALITY") != std::string::npos);
  CHECK(std::string(pl::key_hint(7, 2)).find("SPEED") != std::string::npos);
}
