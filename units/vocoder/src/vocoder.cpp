// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <cmath>

namespace o3_voc03er {

namespace {

using o3sdk::core::ParamCurve;
using o3sdk::core::ParamUnit;

constexpr const char* kWaveNames[] = {"SAW", "PULSE"};

// name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page
constexpr o3sdk::core::ParamDesc kParams[Vocoder::kParamCount] = {
    {"WAVE", "", kWaveNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"NOISE", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"ATTACK", "ms", nullptr, nullptr, 1.0f, 500.0f, 5.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"RELEASE", "ms", nullptr, nullptr, 5.0f, 2000.0f, 80.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"DRY", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"LEVEL", "dB", nullptr, nullptr, -48.0f, 6.0f, -6.0f, 108, 12, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0},
};

constexpr o3sdk::dsp::UnitInfo kInfo{"VOCODER", o3sdk::dsp::UnitKind::Instrument, kParams, Vocoder::kParamCount, nullptr, 0};

constexpr float kRampMs = 5.0f;
constexpr float kWetGain = 32.0f;  // the bands' sum is well below the carrier's level (each band passes a slice of it)

// Cubic soft clip: linear near 0, flat at +-1.5 where it reaches 0.999: the peak never goes above it.
inline float soft_clip(float x) noexcept {
  x = x > 1.5f ? 1.5f : (x < -1.5f ? -1.5f : x);
  return (x - 0.14814815f * x * x * x) * 0.999f;
}

}  // namespace

const o3sdk::dsp::UnitInfo& Vocoder::info() const noexcept { return kInfo; }

void Vocoder::prepare(float sample_rate) noexcept {
  carrier_.prepare(sample_rate);
  bands_.prepare(sample_rate);
  const auto ramp = static_cast<std::uint32_t>(kRampMs * 0.001f * sample_rate);
  level_.set_length(ramp);
  dry_.set_length(ramp);
  for (unsigned p = 0; p < kParamCount; ++p) {
    set(static_cast<std::uint16_t>(p), kParams[p].def);
  }
  level_.jump(level_.target_value());
  dry_.jump(dry_.target_value());
}

void Vocoder::set(std::uint16_t id, float v) noexcept {
  if (id >= kParamCount) {
    return;
  }
  v = o3sdk::core::param_clamp(kParams[id], v);
  switch (id) {
    case Wave:
      carrier_.set_wave(v >= 0.5f ? Carrier::Wave::Pulse : Carrier::Wave::Saw);
      break;
    case Noise:
      carrier_.set_noise(v);
      break;
    case Attack:
      carrier_.set_attack_ms(v);
      break;
    case Release:
      carrier_.set_release_ms(v);
      break;
    case Dry:
      dry_.target(v);
      break;
    case Level:
      level_.target(std::exp(v * 0.11512925f));  // dB -> linear
      break;
    default:
      break;
  }
}

void Vocoder::apply(const o3sdk::dsp::Event& e) noexcept {
  using o3sdk::dsp::EventKind;
  switch (e.kind) {
    case EventKind::NoteOn:
      carrier_.note_on(e.note);
      break;
    case EventKind::NoteOff:
      carrier_.note_off(e.note);
      break;
    case EventKind::Param:
      set(e.param, e.value);
      break;
    case EventKind::AllNotesOff:
      carrier_.all_notes_off();
      break;
  }
}

void Vocoder::process(const o3sdk::dsp::ProcessArgs& args) noexcept {
  const std::uint32_t n = args.frames < Bands::kMaxFrames ? args.frames : Bands::kMaxFrames;
  float mod[Bands::kMaxFrames];
  float car[Bands::kMaxFrames];
  float wet[Bands::kMaxFrames];
  for (std::uint32_t i = 0; i < n; ++i) {
    mod[i] = args.in_l != nullptr ? Bands::clean(args.in_l[i]) : 0.0f;
  }
  o3sdk::dsp::ProcessArgs run = args;
  run.frames = n;
  o3sdk::dsp::for_each_run(
      run, [&](std::uint32_t from, std::uint32_t to) { carrier_.render(car + from, to - from); },
      [&](const o3sdk::dsp::Event& e) { apply(e); });
  bands_.process(mod, car, wet, n);
  for (std::uint32_t i = 0; i < n; ++i) {
    const float g = level_.next();
    const float d = dry_.next();
    const float y = soft_clip(g * (wet[i] * kWetGain + d * mod[i]));
    args.out_l[i] = y;
    args.out_r[i] = y;
  }
}

}  // namespace o3_voc03er
