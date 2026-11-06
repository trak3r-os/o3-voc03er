// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// OR NOIR's bitmaps, made at compile time (no binary asset in the repository) and put in the app's region of RAM_G by the runtime at boot
// (App::assets(), o3sdk/app/app_assets.h): "generated at boot into RAM_G" is this table. Three L1 bitmaps (a set bit is drawn in the colour
// the Canvas gives, the clear bits not at all):
//   coarse mask  288 x 192: a 6 x 6 pitch, Pane ink on the rim of each cell, leaving a 5 x 5 round pinhole (the voiceprint, the ladders)
//   fine mask    8 x 8, repeated: a 4 px pitch, 3 x 3 pinholes (the chord readout)
//   dot atlas    20 cells of 32 x 40: a blank cell, then the characters a chord's name can hold, the kit's Label face (Spleen 8 x 16, BSD-2-Clause,
//                third_party/fonts in o3-sdk) rows 2 to 11, each bit a 3 x 3 px dot at a 4 px pitch. No character has a descender: chords.h says "M7" and "+".
// Pure data and constexpr code: a host test checks the pixels (tests/perform_logic_test.cpp).

#include <o3sdk/app/app_assets.h>
#include <o3sdk/eve/dl.h>
#include <o3sdk/ui/plot/canvas.h>
#include <o3sdk/ui/plot/ram_g_map.h>

#include <cstddef>
#include <cstdint>

namespace o3_voc03er::ornoir {

inline constexpr unsigned kMaskW = 288, kMaskH = 192, kMaskStride = kMaskW / 8;
inline constexpr unsigned kMaskBytes = kMaskStride * kMaskH;  // 6912
inline constexpr unsigned kFineBytes = 8;
inline constexpr unsigned kCellW = 32, kCellH = 40, kCellStride = kCellW / 8, kCellBytes = kCellStride * kCellH;  // 160
inline constexpr const char kGlyphChars[] = "ABCDEFG#M257bdimsu+";  // cell 1 + index; cell 0 is blank
inline constexpr unsigned kGlyphCount = sizeof kGlyphChars - 1;
inline constexpr unsigned kAtlasCells = kGlyphCount + 1;
inline constexpr unsigned kAtlasBytes = kAtlasCells * kCellBytes;  // 3200

namespace detail {

// Row `r` of the mask's 6 x 6 cell; '#' is ink.
inline constexpr const char* kMaskRows[6] = {"#...##", ".....#", ".....#", ".....#", "#...##", "######"};

// The Label face's rows 2 to 11 of each character of kGlyphChars, the left dot in the high bit.
inline constexpr unsigned kGlyphRows = 10;
inline constexpr std::uint8_t kGlyphs[kGlyphCount][kGlyphRows] = {
    {0x7C, 0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6},  // A
    {0xFC, 0xC6, 0xC6, 0xC6, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0xFC},  // B
    {0x7E, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x7E},  // C
    {0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xFC},  // D
    {0x7E, 0xC0, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xC0, 0xC0, 0x7E},  // E
    {0x7E, 0xC0, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0},  // F
    {0x7E, 0xC0, 0xC0, 0xC0, 0xDE, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E},  // G
    {0x6C, 0x6C, 0xFE, 0x6C, 0x6C, 0x6C, 0x6C, 0xFE, 0x6C, 0x6C},  // #
    {0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6},  // M
    {0x7C, 0xC6, 0x06, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC6, 0xFE},  // 2
    {0xFE, 0xC6, 0xC0, 0xC0, 0xFC, 0x06, 0x06, 0x06, 0xC6, 0x7C},  // 5
    {0xFE, 0xC6, 0x06, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x30},  // 7
    {0xC0, 0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xFC},  // b
    {0x06, 0x06, 0x06, 0x7E, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E},  // d
    {0x18, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1C},  // i
    {0x00, 0x00, 0x00, 0xEC, 0xD6, 0xD6, 0xD6, 0xD6, 0xC6, 0xC6},  // m
    {0x00, 0x00, 0x00, 0x7E, 0xC0, 0xC0, 0x7C, 0x06, 0x06, 0xFC},  // s
    {0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E},  // u
    {0x00, 0x00, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},  // +
};

template <std::size_t N>
struct alignas(4) Bytes {
  std::uint8_t b[N] = {};
};

constexpr void set_pixel(std::uint8_t* plane, unsigned stride, unsigned x, unsigned y) noexcept {
  plane[y * stride + x / 8u] = static_cast<std::uint8_t>(plane[y * stride + x / 8u] | (0x80u >> (x % 8u)));  // L1: the left pixel is the high bit
}

constexpr Bytes<kMaskBytes> make_mask() noexcept {
  Bytes<kMaskBytes> m;
  for (unsigned y = 0; y < kMaskH; ++y) {
    for (unsigned x = 0; x < kMaskW; ++x) {
      if (kMaskRows[y % 6u][x % 6u] == '#') {
        set_pixel(m.b, kMaskStride, x, y);
      }
    }
  }
  return m;
}

constexpr Bytes<kFineBytes> make_fine() noexcept {
  Bytes<kFineBytes> m;
  for (unsigned y = 0; y < 8; ++y) {
    for (unsigned x = 0; x < 8; ++x) {
      if (y % 4u == 3u || x % 4u == 3u) {  // ink on the rim of each 4 x 4 cell: a 3 x 3 hole
        set_pixel(m.b, 1, x, y);
      }
    }
  }
  return m;
}

constexpr Bytes<kAtlasBytes> make_atlas() noexcept {
  Bytes<kAtlasBytes> a;
  for (unsigned g = 0; g < kGlyphCount; ++g) {
    std::uint8_t* cell = a.b + (g + 1u) * kCellBytes;
    for (unsigned row = 0; row < kGlyphRows; ++row) {
      for (unsigned col = 0; col < 8; ++col) {
        if ((kGlyphs[g][row] & (0x80u >> col)) == 0u) {
          continue;
        }
        for (unsigned dy = 0; dy < 3; ++dy) {
          for (unsigned dx = 0; dx < 3; ++dx) {
            set_pixel(cell, kCellStride, col * 4u + dx, row * 4u + dy);
          }
        }
      }
    }
  }
  return a;
}

}  // namespace detail

inline constexpr detail::Bytes<kMaskBytes> kMask = detail::make_mask();
inline constexpr detail::Bytes<kFineBytes> kFine = detail::make_fine();
inline constexpr detail::Bytes<kAtlasBytes> kAtlas = detail::make_atlas();

// Where they sit in the app's region (offsets from kRamGApp, multiples of 4) and the descriptors that draw them.
inline constexpr std::uint32_t kMaskOffset = 0;
inline constexpr std::uint32_t kFineOffset = kMaskBytes;
inline constexpr std::uint32_t kAtlasOffset = kFineOffset + kFineBytes;

inline constexpr o3sdk::app::AppAsset kAssets[] = {
    {kMaskOffset, kMask.b, kMaskBytes},
    {kFineOffset, kFine.b, kFineBytes},
    {kAtlasOffset, kAtlas.b, kAtlasBytes},
};
inline constexpr std::size_t kAssetCount = sizeof kAssets / sizeof kAssets[0];
static_assert(o3sdk::app::app_assets_valid(kAssets, kAssetCount), "the skin's bitmaps fit the app's region of RAM_G, aligned and apart");

// The coarse mask over a region of w x h pixels (at most 288 x 192), drawn with its origin at the region's origin.
constexpr o3sdk::ui::plot::BitmapDesc coarse_mask(std::uint16_t w, std::uint16_t h) noexcept {
  return {o3sdk::ui::plot::kRamGApp + kMaskOffset, o3sdk::eve::BitmapFormat::L1, kMaskStride, kMaskW, kMaskH, w, h};
}
constexpr o3sdk::ui::plot::BitmapDesc fine_mask(std::uint16_t w, std::uint16_t h) noexcept {
  return {o3sdk::ui::plot::kRamGApp + kFineOffset, o3sdk::eve::BitmapFormat::L1, 1, 8, 8, w, h, o3sdk::eve::Wrap::Repeat, o3sdk::eve::Wrap::Repeat};
}
inline constexpr o3sdk::ui::plot::BitmapDesc kAtlasDesc = {
    o3sdk::ui::plot::kRamGApp + kAtlasOffset, o3sdk::eve::BitmapFormat::L1, kCellStride, kCellW, kCellH, kCellW, kCellH};

static_assert(o3sdk::ui::plot::bitmap_desc_valid(coarse_mask(kMaskW, 174)) && o3sdk::ui::plot::bitmap_desc_valid(coarse_mask(12, 150)) &&
                  o3sdk::ui::plot::bitmap_desc_valid(coarse_mask(216, 12)),
              "the coarse mask's descriptors are valid");
static_assert(o3sdk::ui::plot::bitmap_desc_valid(fine_mask(216, 48)) && o3sdk::ui::plot::bitmap_desc_valid(kAtlasDesc), "the fine mask's and the atlas' descriptors are valid");

// The atlas cell of a character (0, the blank one, for a character the atlas does not hold).
constexpr unsigned glyph_cell(char c) noexcept {
  for (unsigned i = 0; i < kGlyphCount; ++i) {
    if (kGlyphChars[i] == c) {
      return i + 1u;
    }
  }
  return 0;
}

}  // namespace o3_voc03er::ornoir
