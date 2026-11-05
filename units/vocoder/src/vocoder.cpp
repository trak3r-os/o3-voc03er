// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <cmath>

namespace o3_voc03er {

namespace {

using o3sdk::core::ParamCurve;
using o3sdk::core::ParamUnit;

constexpr const char* kWaveNames[] = {"SAW", "PULSE"};
constexpr const char* kModeNames[] = {"BANDS", "TALK"};
constexpr const char* kFreezeNames[] = {"OFF", "ON"};

// name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page
constexpr o3sdk::core::ParamDesc kParams[Vocoder::kParamCount] = {
    {"WAVE", "", kWaveNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"NOISE", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"ATTACK", "ms", nullptr, nullptr, 1.0f, 500.0f, 5.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"RELEASE", "ms", nullptr, nullptr, 5.0f, 2000.0f, 80.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"DRY", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"LEVEL", "dB", nullptr, nullptr, -48.0f, 6.0f, -6.0f, 108, 12, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0},
    {"DETUNE", "ct", nullptr, nullptr, 0.0f, 20.0f, 8.0f, 20, 5, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"MODE", "", kModeNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"QUALITY", "", nullptr, nullptr, 8.0f, 24.0f, 16.0f, 16, 2, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"FREEZE", "", kFreezeNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
};

constexpr o3sdk::dsp::UnitInfo kInfo{"VOCODER", o3sdk::dsp::UnitKind::Instrument, kParams, Vocoder::kParamCount, nullptr, 0};

constexpr float kRampMs = 5.0f;
constexpr float kModeFadeMs = 10.0f;  // Bands <-> Talk crossfade
constexpr float kTalkGain = 1.0f;     // Talk's output is a carrier-level signal shaped by the modulator's rms; not tuned (needs the unit's ears)
constexpr float kWetGain = 128.0f;  // the followers read a modulator the AGC holds at a mean-abs of 0.1, and each band passes a slice of the carrier: a vowel on a three-note saw chord peaks at about 0.9 at LEVEL 0 dB (0.47 at the default -6)

// Cubic soft clip: linear near 0, flat at +-1.5 where it reaches 0.999: the peak never goes above it.
inline float soft_clip(float x) noexcept {
  x = x > 1.5f ? 1.5f : (x < -1.5f ? -1.5f : x);
  return (x - 0.14814815f * x * x * x) * 0.999f;
}

}  // namespace

const o3sdk::dsp::UnitInfo& Vocoder::info() const noexcept { return kInfo; }

void Vocoder::prepare(float sample_rate) noexcept {
  carrier_.set_noise_source(&noise_);  // one generator for the carrier's noise and the unvoiced path
  bands_.set_noise_source(&noise_);
  carrier_.prepare(sample_rate);
  bands_.prepare(sample_rate);
  talk_.prepare(sample_rate);
  talk_mix_.set_length(static_cast<std::uint32_t>(kModeFadeMs * 0.001f * sample_rate));
  const auto ramp = static_cast<std::uint32_t>(kRampMs * 0.001f * sample_rate);
  level_.set_length(ramp);
  dry_.set_length(ramp);
  for (unsigned p = 0; p < kParamCount; ++p) {
    set(static_cast<std::uint16_t>(p), kParams[p].def);
  }
  level_.jump(level_.target_value());
  dry_.jump(dry_.target_value());
  talk_mix_.jump(0.0f);
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
    case Detune:
      carrier_.set_detune_cents(v);
      break;
    case Mode: {
      const float t = v >= 0.5f ? 1.0f : 0.0f;
      if (t != talk_mix_.target_value()) {
        if (t > 0.5f && talk_mix_.value() <= 0.0f) {
          talk_.reset();  // Talk was not running: its ring is stale
        } else if (t < 0.5f && talk_mix_.value() >= 1.0f) {
          bands_.restart();  // nor were Bands: their followers still hold the voice from before
        }
        talk_mix_.target(t);
      }
      break;
    }
    case Quality:
      talk_.set_order(static_cast<std::uint32_t>(v + 0.5f));
      break;
    case Freeze:
      talk_.set_freeze(v >= 0.5f);
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
  float gate[Bands::kMaxFrames];
  for (std::uint32_t i = 0; i < n; ++i) {
    mod[i] = args.in_l != nullptr ? Bands::clean(args.in_l[i]) : 0.0f;
  }
  o3sdk::dsp::ProcessArgs run = args;
  run.frames = n;
  o3sdk::dsp::for_each_run(
      run, [&](std::uint32_t from, std::uint32_t to) { carrier_.render(car + from, to - from, gate + from); },
      [&](const o3sdk::dsp::Event& e) { apply(e); });
  // Only the active mode runs; both run while the crossfade is under way.
  const bool to_talk = talk_mix_.target_value() > 0.5f;
  const bool fading = talk_mix_.value() != talk_mix_.target_value();
  const bool run_bands = !to_talk || fading;
  const bool run_talk = to_talk || fading;
  float tw[Bands::kMaxFrames];
  if (run_bands) {
    bands_.process(mod, car, wet, n, gate);
  } else {
    for (std::uint32_t i = 0; i < n; ++i) wet[i] = 0.0f;
  }
  if (run_talk) {
    talk_.process(mod, car, tw, n);
  } else {
    for (std::uint32_t i = 0; i < n; ++i) tw[i] = 0.0f;
  }
  for (std::uint32_t i = 0; i < n; ++i) {
    const float g = level_.next();
    const float d = dry_.next();
    const float m = talk_mix_.next();
    const float y = soft_clip(g * ((wet[i] * kWetGain) * (1.0f - m) + (tw[i] * kTalkGain) * m + d * mod[i]));
    args.out_l[i] = y;
    args.out_r[i] = y;
  }
}

}  // namespace o3_voc03er
