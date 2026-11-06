// SPDX-License-Identifier: GPL-3.0-or-later
// The eight factory sounds of PERFORM's SHIFT + F8 (app/include/o3_voc03er/presets.h): every preset lists the same fifteen sound parameters
// in the same order, every value is inside the unit's range, none touches KEY, SCALE or the loops, and each one sounds through the unit.
#include <doctest/doctest.h>

#include <o3_voc03er/presets.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>

#include <cmath>
#include <cstdint>
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
  CHECK(std::strcmp(kPresets[7].name, "FROZEN") == 0);
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
  CHECK(kPresets[7].v[14].value == 1.0f);   // FROZEN
  CHECK(kPresets[2].v[1].value == 2.0f);    // WHISPER: the NOISE carrier
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
