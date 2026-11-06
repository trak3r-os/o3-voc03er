// SPDX-License-Identifier: GPL-3.0-or-later
// OR NOIR's theme (app/src/skin/ornoir.cpp): its tokens hold every pair of ui::kContrastPairs, the check the kit holds its themes to.
#include <doctest/doctest.h>

#include "../app/src/skin/ornoir.h"

#include <o3sdk/ui/style.h>
#include <o3sdk/ui/theme.h>

#include <cmath>
#include <string>

namespace ui = o3sdk::ui;

namespace {

double channel(std::uint8_t v) {
  const double c = static_cast<double>(v) / 255.0;
  return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double luminance(const ui::Rgb& c) { return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b); }
double contrast(const ui::Rgb& a, const ui::Rgb& b) {
  const double la = luminance(a);
  const double lb = luminance(b);
  return (std::fmax(la, lb) + 0.05) / (std::fmin(la, lb) + 0.05);
}

const char* kNames[ui::kColorCount] = {"Chrome", "ChromeText", "ChromeDim", "Pane", "Text", "TextDim", "Empty", "Instr", "Vol", "Fx", "Sel", "SelText", "Accent", "AccentText", "Play", "PlayText", "Rec"};

}  // namespace

TEST_CASE("or noir theme: every pair of kContrastPairs meets its ratio") {
  const ui::Theme& t = o3_voc03er::ornoir::theme();
  for (const ui::ContrastPair& p : ui::kContrastPairs) {
    const double ratio = contrast(t.rgb(p.fg), t.rgb(p.bg));
    const std::string what = std::string(kNames[static_cast<unsigned>(p.fg)]) + " on " + kNames[static_cast<unsigned>(p.bg)] + ": " + std::to_string(ratio);
    INFO(what);
    switch (p.role) {
      case ui::PairRole::Text: CHECK(ratio >= static_cast<double>(ui::kMinTextContrast)); break;
      case ui::PairRole::Large: CHECK(ratio >= static_cast<double>(ui::kMinLargeContrast)); break;
      case ui::PairRole::Dim:
        CHECK(ratio >= static_cast<double>(ui::kDimContrastLow));
        CHECK(ratio <= static_cast<double>(ui::kDimContrastHigh));
        break;
    }
  }
}

TEST_CASE("or noir theme: the pane is black, what speaks is amber and what sounds is blue") {
  const ui::Theme& t = o3_voc03er::ornoir::theme();
  const ui::Rgb& pane = t.rgb(ui::Color::Pane);
  CHECK(pane.r == 0);
  CHECK(pane.g == 0);
  CHECK(pane.b == 0);
  const ui::Rgb& play = t.rgb(ui::Color::Play);
  const ui::Rgb& sel = t.rgb(ui::Color::Sel);
  CHECK(play.r > play.b);  // amber
  CHECK(sel.b > sel.r);    // ice blue
}
