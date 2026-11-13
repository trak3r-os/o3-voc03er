// SPDX-License-Identifier: GPL-3.0-or-later
// The eight factory sounds of PERFORM's SHIFT + F8 (app/include/o3_voc03er/presets.h): every preset lists the same fifteen sound parameters
// in the same order, every value is inside the unit's range, none touches KEY, SCALE or the loops, and each one sounds through the unit.
#include <doctest/doctest.h>

#include <o3_voc03er/presets.h>
#include <o3_voc03er/units/tract/tract.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

TEST_CASE("presets: each lists the same fifteen parameters, in range, and none is KEY, SCALE, the loops or the source") {
  Vocoder v;
  const dsp::UnitInfo& info = v.info();
  const std::uint16_t want[kPresetParams] = {Vocoder::Mode,   Vocoder::CarSrc, Vocoder::Wave, Vocoder::Noise,   Vocoder::Attack,
                                             Vocoder::Release, Vocoder::Detune, Vocoder::Quality, Vocoder::Formant, Vocoder::Thru,
                                             Vocoder::Glide,  Vocoder::Gate,   Vocoder::Arp,  Vocoder::Vib,     Vocoder::Freeze};
  CHECK(std::strcmp(kPresets[0].name, "ROBOT") == 0);
  CHECK(std::strcmp(kPresets[7].name, "SWELL") == 0);
  for (const Preset& p : kPresets) {
    INFO(p.name);
    CHECK(std::strlen(p.name) <= 8u);
    for (std::size_t i = 0; i < kPresetParams; ++i) {
      CHECK(p.v[i].id == want[i]);
      REQUIRE(p.v[i].id < info.param_count);
      const o3sdk::core::ParamDesc& d = info.params[p.v[i].id];
      INFO(d.name);
      CHECK(p.v[i].value >= d.min);
      CHECK(p.v[i].value <= d.max);
      if (d.names != nullptr) {  // an enum value is a whole number
        CHECK(p.v[i].value == std::floor(p.v[i].value));
      }
    }
  }
  // the specific character the brief names
  CHECK(kPresets[3].v[8].value == -9.0f);   // GIANT formant
  CHECK(kPresets[4].v[8].value == 9.0f);    // ELF formant
  CHECK(kPresets[5].v[11].value == 3.0f);   // PULSE gate 1/16
  CHECK(kPresets[6].v[12].value == 1.0f);   // ARP up
  CHECK(kPresets[6].v[11].value == 0.0f);   // ... gate off
  CHECK(kPresets[7].v[14].value == 0.0f);   // SWELL: no preset latches FREEZE (UX2)
  for (const Preset& p : kPresets) {
    INFO(p.name);
    CHECK(p.v[14].value == 0.0f);  // FREEZE is TALK's and a mode change clears it: a preset that set it would be dim in BANDS or lost on the mode change
  }
  CHECK(kPresets[2].v[1].value == 2.0f);    // WHISPER: the NOISE carrier
}

TEST_CASE("presets: TRACT's eight list the twelve tract values in the unit's order, in range, with names that are no vocoder preset's") {
  TractUnit t;
  const dsp::UnitInfo& info = t.info();
  const std::uint16_t want[kTractPresetParams] = {TractUnit::Vowel, TractUnit::Tongue, TractUnit::Jaw,   TractUnit::Lips,  TractUnit::Close, TractUnit::Glide,
                                                  TractUnit::Size,  TractUnit::Res,    TractUnit::Hiss,  TractUnit::Mix,   TractUnit::Slide, TractUnit::Accent};
  static_assert(kTractPresetParams == TractUnit::Source, "every value before SOURCE, and not SOURCE (a preset leaves PADS or LIVE as it is)");
  for (std::size_t n = 0; n < kTractPresetCount; ++n) {
    const TractPreset& p = kTractPresets[n];
    INFO(p.name);
    CHECK(std::strlen(p.name) >= 1u);
    CHECK(std::strlen(p.name) <= 8u);
    for (std::size_t i = 0; i < kTractPresetParams; ++i) {
      CHECK(p.v[i].id == want[i]);
      REQUIRE(p.v[i].id < info.param_count);
      const o3sdk::core::ParamDesc& d = info.params[p.v[i].id];
      INFO(d.name);
      CHECK(p.v[i].value >= d.min);
      CHECK(p.v[i].value <= d.max);
      if (d.names != nullptr) {
        CHECK(p.v[i].value == std::floor(p.v[i].value));
      }
    }
    CHECK(p.v[TractUnit::Close].value == 0.0f);   // never a closure: the sound is open
    CHECK(p.v[TractUnit::Slide].value == 0.0f);   // GLIDE rules (the MOUTH line writes SLIDE)
    for (std::size_t m = n + 1; m < kTractPresetCount; ++m) {
      CHECK(std::strcmp(p.name, kTractPresets[m].name) != 0);
    }
    for (const Preset& q : kPresets) {
      CHECK(std::strcmp(p.name, q.name) != 0);  // a project saves one name for the two tables
    }
  }
}

TEST_CASE("presets: every one sounds a held chord over a steady modulator") {
  constexpr float kFs = 44100.0f;
  constexpr std::size_t kBlock = 64;
  constexpr std::size_t kBlocks = 700;
  Vocoder v;
  v.prepare(kFs);
  std::vector<float> mod(kBlocks * kBlock);
  for (std::size_t i = 0; i < mod.size(); ++i) {
    const float t = static_cast<float>(i) / kFs;
    mod[i] = 0.12f * (std::sin(6.2831853f * 500.0f * t) + std::sin(6.2831853f * 1000.0f * t) + std::sin(6.2831853f * 2000.0f * t) +
                      std::sin(6.2831853f * 3000.0f * t));
  }
  dsp::Transport tr;
  tr.tempo_mbpm = 120'000;
  for (std::size_t pi = 0; pi < kPresetCount; ++pi) {
    std::vector<dsp::Event> ev;
    for (const PresetValue& pv : kPresets[pi].v) {
      ev.push_back(dsp::param_change(pv.id, pv.value, 0));
    }
    if (pi == 0) {
      for (const int n : {45, 48, 52}) {
        ev.push_back(dsp::note_on(static_cast<std::uint8_t>(n), 100, 0, 0));
      }
    }
    float peak = 0.0f;
    for (std::size_t b = 0; b < kBlocks; ++b) {
      float l[kBlock];
      float r[kBlock];
      dsp::ProcessArgs a{};
      a.events = b == 0 ? ev.data() : nullptr;
      a.event_count = b == 0 ? static_cast<std::uint32_t>(ev.size()) : 0u;
      a.in_l = a.in_r = mod.data() + b * kBlock;
      a.out_l = l;
      a.out_r = r;
      a.frames = static_cast<std::uint32_t>(kBlock);
      a.transport = &tr;
      v.process(a);
      if (b > 200) {
        for (std::size_t i = 0; i < kBlock; ++i) {
          peak = std::fmax(peak, std::fabs(l[i]));
          REQUIRE(std::isfinite(l[i]));
        }
      }
    }
    INFO(kPresets[pi].name);
    CHECK(peak > 0.005f);
  }
}

namespace {
// A TractUnit at one TRACT preset's values (HISS optionally forced), a saw at its input; the output of the last half second.
std::vector<float> tract_run(const TractPreset& p, float hiss_override) {
  constexpr float kFs = 44100.0f;
  constexpr std::size_t kBlock = 64;
  constexpr std::size_t kBlocks = 700;
  static TractUnit t;
  t.prepare(kFs);
  std::vector<dsp::Event> ev;
  for (const PresetValue& pv : p.v) {
    const float v = (pv.id == TractUnit::Hiss && hiss_override >= 0.0f) ? hiss_override : pv.value;
    ev.push_back(dsp::param_change(pv.id, v, 0));
  }
  dsp::Transport tr;
  float ph = 0.0f;
  std::vector<float> out;
  for (std::size_t b = 0; b < kBlocks; ++b) {
    float in[kBlock], l[kBlock], r[kBlock];
    for (float& x : in) {
      ph += 110.0f / kFs;
      ph -= std::floor(ph);
      x = 0.25f * (2.0f * ph - 1.0f);
    }
    dsp::ProcessArgs a{};
    a.events = b == 0 ? ev.data() : nullptr;
    a.event_count = b == 0 ? static_cast<std::uint32_t>(ev.size()) : 0u;
    a.in_l = a.in_r = in;
    a.out_l = l;
    a.out_r = r;
    a.frames = static_cast<std::uint32_t>(kBlock);
    a.transport = &tr;
    t.process(a);
    if (b >= kBlocks / 2) {
      out.insert(out.end(), l, l + kBlock);
    }
  }
  return out;
}

// RMS of the sound with the preset's HISS minus the same run with HISS 0: the same input and the same tube, so what is left is the hiss alone.
float hiss_rms(const TractPreset& p, float* dry_rms) {
  const std::vector<float> on = tract_run(p, -1.0f);
  const std::vector<float> off = tract_run(p, 0.0f);
  double d = 0.0, o = 0.0;
  for (std::size_t i = 0; i < on.size(); ++i) {
    REQUIRE(std::isfinite(on[i]));
    d += static_cast<double>(on[i] - off[i]) * static_cast<double>(on[i] - off[i]);
    o += static_cast<double>(off[i]) * static_cast<double>(off[i]);
  }
  *dry_rms = static_cast<float>(std::sqrt(o / static_cast<double>(on.size())));
  return static_cast<float>(std::sqrt(d / static_cast<double>(on.size())));
}
}  // namespace

TEST_CASE("presets: TRACT's BREATH makes hiss you can hear (the tube is narrow enough), and the open presets make none") {
  const TractPreset* breath = nullptr;
  for (const TractPreset& p : kTractPresets) {
    if (std::strcmp(p.name, "BREATH") == 0) {
      breath = &p;
    }
  }
  REQUIRE(breath != nullptr);
  float dry = 0.0f;
  const float h = hiss_rms(*breath, &dry);
  std::printf("BREATH: hiss rms %.5f against %.5f of the tube alone\n", static_cast<double>(h), static_cast<double>(dry));
  CHECK(h > 0.005f);        // about -46 dBFS against a saw of 0.25 at the input: a whisper that is there (measured 0.0072)
  CHECK(h > 0.05f * dry);   // and about 24 dB under the voiced tube at most (the kit's hiss follows the input's envelope)
  TractPreset was = *breath;  // the first BREATH (E, jaw +0.1) never narrowed the tube under 0.3: no hiss at all
  for (PresetValue& pv : was.v) {
    if (pv.id == TractUnit::Vowel) pv.value = 0.25f;
    if (pv.id == TractUnit::Jaw) pv.value = 0.1f;
    if (pv.id == TractUnit::Lips) pv.value = 0.0f;
  }
  float d1 = 0.0f;
  CHECK(hiss_rms(was, &d1) < 0.0005f);
  for (const TractPreset& p : kTractPresets) {  // the open vowels stay clean: no section under 0.3
    if (std::strcmp(p.name, "AH") == 0 || std::strcmp(p.name, "OO") == 0) {
      float d2 = 0.0f;
      INFO(p.name);
      CHECK(hiss_rms(p, &d2) < 0.001f);
    }
  }
}
