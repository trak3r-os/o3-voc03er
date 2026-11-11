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
  for (unsigned mode = 0; mode < kModeCount; ++mode) {
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
  CHECK(std::string(pl::key_hint(7, kModeTract)).find("VOWEL") != std::string::npos);
  CHECK(std::string(pl::key_hint(7, 3)).find("SPREAD") != std::string::npos);  // ROBOT keeps BANDS' F8
}

TEST_CASE("perform: the hint of the free encoder fits") {
  const std::string hint = pl::key_hint(pl::kBendFocus, 0);
  CHECK(hint.size() <= pl::kKeyHintMax);
  CHECK(hint.find("ENCODER") != std::string::npos);
}

namespace {
pads::Layout layout_of(pads::Plays a, pads::Plays b, pads::Plays c) {
  pads::Layout l;
  l.row[0] = a;
  l.row[1] = b;
  l.row[2] = c;
  return l;
}
}  // namespace

TEST_CASE("perform: row 0 is the controls in every layout, the gaps dark, and no control plays a note") {
  using F = pl::PadFn;
  const F ctl[12] = {F::OctDown, F::OctUp, F::None, F::VolDown, F::VolUp, F::None, F::RootDown, F::RootUp, F::None, F::Mod, F::BndDown, F::BndUp};
  const pads::Layout layouts[3] = {pads::Layout{}, layout_of(pads::Plays::PianoNote, pads::Plays::PianoNote, pads::Plays::Bend),
                                   layout_of(pads::Plays::Off, pads::Plays::Seventh, pads::Plays::Off)};
  for (const pads::Layout& l : layouts) {
    for (unsigned c = 0; c < 12; ++c) {
      const pl::PadSpec sp = pl::pad_spec(l, 0, c, 0);
      CHECK(sp.fn == ctl[c]);
      CHECK(sp.play.note == -1);
      CHECK(std::string(pl::kControlName[c]).empty() == (ctl[c] == F::None));
    }
  }
  CHECK(pl::is_control(F::Mod));
  CHECK_FALSE(pl::is_control(F::Play));
  CHECK_FALSE(pl::is_control(F::None));
  CHECK_FALSE(pl::is_control(F::Formant));
  CHECK_FALSE(pl::is_control(F::Strip));
  CHECK(pl::is_held_control(F::BndUp));
  CHECK_FALSE(pl::is_held_control(F::OctUp));
}

TEST_CASE("perform: rows 1, 2 and 3 are A, B and C, and a pad plays what pads::pad_play says") {
  using F = pl::PadFn;
  const pads::Layout def;  // A NOTE, B TRIAD, C FORMANT
  for (unsigned c = 0; c < 12; ++c) {
    const pl::PadSpec a = pl::pad_spec(def, 1, c, 0);
    const pl::PadSpec b = pl::pad_spec(def, 2, c, 0);
    CHECK(a.fn == F::Play);
    CHECK(a.play.note == pads::pad_play(def, pads::Row::A, c, 0).note);
    CHECK(a.play.kind == 0u);
    CHECK(b.fn == F::Play);
    CHECK(b.play.kind == 1u);
    CHECK(b.play.note == pads::pad_play(def, pads::Row::B, c, 0).note);
    CHECK(a.play.note == b.play.note + 12);  // C plays no note, so B is at +0 and A one octave above it
  }
}

TEST_CASE("perform: the piano is B's white keys and A's black keys, from the same C") {
  using F = pl::PadFn;
  const pads::Layout piano = layout_of(pads::Plays::PianoNote, pads::Plays::PianoNote, pads::Plays::Formant);
  const int white[12] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19};
  for (unsigned c = 0; c < 12; ++c) {
    const pl::PadSpec w = pl::pad_spec(piano, 2, c, 0);
    REQUIRE(w.fn == F::Play);
    CHECK(w.play.note == kChordBaseNote + 12 * pads::row_octave(piano, pads::Row::B) + white[c]);
  }
  const unsigned black_cols[8] = {1, 2, 4, 5, 6, 8, 9, 11};
  const int black[8] = {1, 3, 6, 8, 10, 13, 15, 18};
  unsigned n = 0;
  for (unsigned c = 0; c < 12; ++c) {
    const pl::PadSpec p = pl::pad_spec(piano, 1, c, 0);
    if (p.fn == F::Play) {
      REQUIRE(n < 8u);
      CHECK(c == black_cols[n]);
      CHECK(p.play.note == kChordBaseNote + 12 * pads::row_octave(piano, pads::Row::A) + black[n]);
      ++n;
    } else {
      CHECK(p.fn == F::None);
    }
  }
  CHECK(n == 8u);
}

TEST_CASE("perform: row C is the formant strip, the bend strip, a note row or dark") {
  using F = pl::PadFn;
  const int want[12] = {-12, -9, -6, -4, -2, 0, 0, 2, 4, 6, 9, 12};
  const pads::Layout formant;  // C FORMANT
  const pads::Layout bend = layout_of(pads::Plays::Note, pads::Plays::Triad, pads::Plays::Bend);
  const pads::Layout notes = layout_of(pads::Plays::Note, pads::Plays::Triad, pads::Plays::Seventh);
  const pads::Layout off = layout_of(pads::Plays::Note, pads::Plays::Triad, pads::Plays::Off);
  for (unsigned c = 0; c < 12; ++c) {
    CHECK(pl::pad_spec(formant, 3, c, 0).fn == F::Formant);
    CHECK(pl::pad_spec(formant, 3, c, 0).offset == want[c]);
    CHECK(pl::pad_spec(formant, 3, c, 0).play.note == -1);
    CHECK(pl::pad_spec(bend, 3, c, 0).fn == F::Strip);
    CHECK(pl::pad_spec(notes, 3, c, 0).fn == F::Play);
    CHECK(pl::pad_spec(notes, 3, c, 0).play.kind == 2u);
    CHECK(pl::pad_spec(off, 3, c, 0).fn == F::None);
  }
  CHECK(pl::strip_on(bend));
  CHECK_FALSE(pl::strip_on(formant));
  CHECK_FALSE(pl::strip_on(notes));
  CHECK(pl::layout_row(1) == pads::Row::A);
  CHECK(pl::layout_row(3) == pads::Row::C);
}

TEST_CASE("perform: the mouth strip is vowels on columns 0..8 and closures on 9..11, the map pure") {
  using F = pl::PadFn;
  const pads::Layout mouth = layout_of(pads::Plays::Note, pads::Plays::Triad, pads::Plays::Mouth);
  for (unsigned c = 0; c < 12; ++c) {
    const pl::PadSpec sp = pl::pad_spec(mouth, 3, c, 0);
    CAPTURE(c);
    CHECK(sp.play.note == -1);
    if (c < 9u) {
      CHECK(sp.fn == F::Vowel);
      CHECK(sp.offset == static_cast<int>(c));
      CHECK(pl::mouth_vowel(c) == doctest::Approx(static_cast<float>(c) / 8.0f));
    } else {
      CHECK(sp.fn == F::Closure);
      CHECK(sp.offset == static_cast<int>(c) - 8);  // LIPS 1, TIP 2, BACK 3
    }
    CHECK_FALSE(pl::is_control(sp.fn));
    CHECK(std::string(pl::kMouthLabel[c]).size() <= 5u);
  }
  CHECK(pl::mouth_vowel(0) == 0.0f);
  CHECK(pl::mouth_vowel(8) == 1.0f);
  CHECK(pl::mouth_vowel(99) == 1.0f);
  // Only row C: the same layout's rows A and B stay what they were; the mouth is not the strip.
  CHECK(pl::pad_spec(mouth, 1, 0, 0).fn == F::Play);
  CHECK(pl::pad_spec(mouth, 2, 0, 0).fn == F::Play);
  CHECK_FALSE(pl::strip_on(mouth));
  // Rows A and B sit on C's octave: C plays no note.
  CHECK(pl::pad_spec(mouth, 2, 0, 0).play.note == pads::pad_play(mouth, pads::Row::B, 0, 0).note);
  CHECK(pl::pad_spec(mouth, 2, 0, 0).play.note == pads::pad_play(layout_of(pads::Plays::Note, pads::Plays::Triad, pads::Plays::Off), pads::Row::B, 0, 0).note);
}

TEST_CASE("perform: a vowel pad is sticky: the nearest column to VOWEL is the lit one") {
  // The press writes mouth_vowel(col); reading it back lights that same column, for every column.
  for (unsigned c = 0; c < 9; ++c) {
    CAPTURE(c);
    CHECK(pl::mouth_nearest_col(pl::mouth_vowel(c)) == c);
  }
  CHECK(pl::mouth_nearest_col(0.06f) == 0u);
  CHECK(pl::mouth_nearest_col(0.07f) == 1u);  // 0.07 * 8 = 0.56: past the half
  CHECK(pl::mouth_nearest_col(1.7f) == 8u);
  CHECK(pl::mouth_nearest_col(-3.0f) == 0u);
  // The pure vowels: A 0, E 2, I 4, O 6, U 8 are 0..4 (the F8 key's letter).
  CHECK(pl::mouth_nearest_vowel(0.0f) == 0u);
  CHECK(pl::mouth_nearest_vowel(0.25f) == 1u);
  CHECK(pl::mouth_nearest_vowel(0.5f) == 2u);
  CHECK(pl::mouth_nearest_vowel(0.75f) == 3u);
  CHECK(pl::mouth_nearest_vowel(1.0f) == 4u);
}

TEST_CASE("perform: the mouth strip's closures are momentary: the last pressed of those held wins") {
  pl::MouthHold h;
  CHECK(h.current() == 0u);  // OPEN
  CHECK(h.mask() == 0u);
  h.press(1);  // LIPS
  CHECK(h.current() == 1u);
  h.press(3);  // BACK joins: it is the one written
  CHECK(h.current() == 3u);
  CHECK(h.mask() == ((1u << 1) | (1u << 3)));
  h.release(3);  // the LIPS is still down: it comes back
  CHECK(h.current() == 1u);
  h.press(2);
  h.release(1);  // an older pad released: the newer one stays
  CHECK(h.current() == 2u);
  h.release(2);
  CHECK(h.current() == 0u);
  CHECK(h.mask() == 0u);
  // A pad pressed twice without a release counts once; a release of one never pressed does nothing.
  h.press(1);
  h.press(1);
  h.release(2);
  CHECK(h.n == 1u);
  h.release(1);
  CHECK(h.current() == 0u);
  // Out-of-range values are ignored.
  h.press(0);
  h.press(4);
  CHECK(h.n == 0u);
}

TEST_CASE("perform: the mouth strip's LEDs: nearest column on, pure vowels mid, the rest dim, closures on while held") {
  namespace a = o3sdk::app;
  for (unsigned vowel_col = 0; vowel_col < 9; ++vowel_col) {
    const float v = pl::mouth_vowel(vowel_col);
    for (unsigned c = 0; c < 9; ++c) {
      CAPTURE(vowel_col);
      CAPTURE(c);
      const std::uint8_t want = c == vowel_col ? a::kLedOn : ((c % 2u) == 0u ? a::kLedMid : a::kLedDim);
      CHECK(pl::mouth_led(c, v, 0) == want);
    }
    for (unsigned c = 9; c < 12; ++c) CHECK(pl::mouth_led(c, v, 0) == a::kLedDim);  // the vowel never lights a closure
  }
  // Closures: bit CLOSE value of the held mask.
  CHECK(pl::mouth_led(9, 0.0f, 1u << 1) == a::kLedOn);
  CHECK(pl::mouth_led(10, 0.0f, 1u << 1) == a::kLedDim);
  CHECK(pl::mouth_led(10, 0.0f, (1u << 2) | (1u << 3)) == a::kLedOn);
  CHECK(pl::mouth_led(11, 0.0f, (1u << 2) | (1u << 3)) == a::kLedOn);
  // A held closure does not touch the vowel LEDs.
  CHECK(pl::mouth_led(4, 0.5f, 1u << 2) == a::kLedOn);
}

TEST_CASE("perform: OFF is dark and silent on every row") {
  const pads::Layout l = layout_of(pads::Plays::Off, pads::Plays::Off, pads::Plays::Off);
  for (unsigned r = 1; r < 4; ++r) {
    for (unsigned c = 0; c < 12; ++c) {
      CHECK(pl::pad_spec(l, r, c, 0).fn == pl::PadFn::None);
    }
  }
}

TEST_CASE("perform: octave end stops, and a note is not moved by the octave") {
  const pads::Layout def;
  const pl::PadSpec c = pl::pad_spec(def, 2, 0, 0);
  const int n0 = c.play.note;
  REQUIRE(n0 >= 0);
  CHECK(pl::pad_spec(def, 2, 0, 1).play.note == n0 + 12);
  CHECK(pl::pad_spec(def, 2, 0, -1).play.note == n0 - 12);
  // The end of the range reads -1 (no note), never wraps.
  CHECK(pl::pad_spec(def, 2, 0, 100).fn == pl::PadFn::None);
  CHECK(pl::pad_spec(def, 2, 0, -100).fn == pl::PadFn::None);
  // clamp_octave holds the end stops the OCT pads dim at.
  CHECK(clamp_octave(1000) != 1000);
  CHECK(clamp_octave(-1000) != -1000);
}

TEST_CASE("perform: VOL clamps and ROOT wraps") {
  CHECK(pl::step_wrapped(11, 1, 12) == 0);
  CHECK(pl::step_wrapped(0, -1, 12) == 11);
  char t[12] = {};
  pl::format_db(-6.0f, t);
  CHECK(std::string(t) == "-6.0");
  pl::format_db(0.5f, t);
  CHECK(std::string(t) == "0.5");
  pl::format_db(-48.0f, t);
  CHECK(std::string(t) == "-48.0");
  pl::format_db(6.0f, t);
  CHECK(std::string(t) == "6.0");
}

TEST_CASE("perform: the BND and MOD curves are linear in the time held and full after 400 ms") {
  CHECK(pl::bend_time_semitones(0, 1, 5) == doctest::Approx(0.0f));
  CHECK(pl::bend_time_semitones(200, 1, 5) == doctest::Approx(2.5f));
  CHECK(pl::bend_time_semitones(200, -1, 12) == doctest::Approx(-6.0f));
  CHECK(pl::bend_time_semitones(400, 1, 2) == doctest::Approx(2.0f));
  CHECK(pl::bend_time_semitones(5000, -1, 2) == doctest::Approx(-2.0f));
  CHECK(pl::vib_rise(100) == doctest::Approx(0.25f));
  CHECK(pl::vib_rise(900) == doctest::Approx(1.0f));
  CHECK(pl::vib_fall(1.0f, 100) == doctest::Approx(0.75f));
  CHECK(pl::vib_fall(0.5f, 400) == doctest::Approx(0.0f));
  CHECK(pl::vib_fall(0.2f, 1000) == doctest::Approx(0.0f));
  CHECK(pl::vib_depth(0.3f, 127) == doctest::Approx(1.0f));
  CHECK(pl::vib_depth(0.8f, 0) == doctest::Approx(0.8f));
  CHECK(pl::vib_depth(0.0f, 64) == doctest::Approx(64.0f / 127.0f));
}

TEST_CASE("perform: the bend sums the wheel and the local bend, and the encoder steps by range") {
  CHECK(pl::bend_step(0.0f, 1, false, 2) == doctest::Approx(0.25f));
  CHECK(pl::bend_step(0.0f, 1, true, 12) == doctest::Approx(6.0f));
  CHECK(pl::bend_step(5.0f, 3, true, 5) == doctest::Approx(5.0f));
  CHECK(pl::bend_sum(10.0f, 5.0f) == doctest::Approx(12.0f));
  CHECK(pl::bend_sum(-10.0f, -5.0f) == doctest::Approx(-12.0f));
  CHECK(pl::wheel_semitones(0, 64, 5) == doctest::Approx(0.0f));
  CHECK(pl::wheel_semitones(127, 127, 5) == doctest::Approx(5.0f * 8191.0f / 8192.0f));
  CHECK(pl::wheel_semitones(0, 0, 12) == doctest::Approx(-12.0f));
}

TEST_CASE("perform: the lit formant pad is the value in force, both middle pads at 0") {
  for (unsigned c = 0; c < 12; ++c) {
    CHECK(pl::formant_lit(0, c) == (c == 5u || c == 6u));
    CHECK(pl::formant_lit(4, c) == (c == 8u));
    CHECK_FALSE(pl::formant_lit(1, c));  // a value the strip has no pad for lights none
  }
}

TEST_CASE("perform: the SHIFT row's steps hold their ranges, and all eight keys are live") {
  CHECK(pl::formant_step(0, 1, false) == 1);
  CHECK(pl::formant_step(0, -1, true) == -4);
  CHECK(pl::formant_step(11, 1, true) == 12);
  CHECK(pl::formant_step(-12, -1, false) == -12);
  CHECK(pl::thru_step(0, 1, false) == 5);
  CHECK(pl::thru_step(98, 1, false) == 100);
  CHECK(pl::thru_step(3, -1, false) == 0);
  CHECK(pl::thru_step(50, 1, true) == 70);
  CHECK(pl::glide_step(0, 1, false) == 10);
  CHECK(pl::glide_step(0, 1, true) == 50);
  CHECK(pl::glide_step(495, 1, false) == 500);
  CHECK(pl::glide_step(20, -1, true) == 0);
  CHECK(pl::preset_step(pl::kNoPreset, 1, 8) == 0u);
  CHECK(pl::preset_step(pl::kNoPreset, -1, 8) == 7u);
  CHECK(pl::preset_step(7, 1, 8) == 0u);
  CHECK(pl::preset_step(0, -1, 8) == 7u);
  CHECK(std::string(pl::kArpNames[3]) == "UP-DOWN");
  for (unsigned i = 0; i < 8; ++i) {
    CHECK(std::string(pl::kShiftKeys[i].name).size() <= 9u);
  }
  for (const unsigned k : {1u, 2u, 3u, 7u}) {
    CHECK(pl::kShiftKeys[k].fn == pl::ShiftFn::Value);
    CHECK(std::string(pl::key_hint(5, 0, k)).size() <= pl::kKeyHintMax);
  }
  CHECK(pl::kShiftKeys[0].fn == pl::ShiftFn::Freeze);
  CHECK(pl::kShiftKeys[4].fn == pl::ShiftFn::Value);
  CHECK(pl::kShiftKeys[5].fn == pl::ShiftFn::Value);
  CHECK(pl::kShiftKeys[6].fn == pl::ShiftFn::None);  // BEND ROW went with the layout: the key is empty
  CHECK(std::string(pl::kShiftKeys[6].name).empty());
  CHECK(std::string(pl::key_hint(5, 0, 4)).size() <= pl::kKeyHintMax);
  CHECK(std::string(pl::key_hint(5, 0, 5)).size() <= pl::kKeyHintMax);
}
