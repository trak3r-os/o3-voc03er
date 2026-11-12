// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/tract/tract.h>

#include <cmath>
#include <cstring>

namespace o3_voc03er {

namespace {

using o3sdk::core::ParamCurve;
using o3sdk::core::ParamUnit;

constexpr const char* kCloseNames[] = {"OPEN", "LIPS", "TIP", "BACK"};
constexpr const char* kSizeNames[] = {"BIG", "LARGE", "NORMAL", "SMALL", "TINY"};
constexpr const char* kSourceNames[] = {"PADS", "LIVE"};

// name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page
constexpr o3sdk::core::ParamDesc kParams[TractUnit::kParamCount] = {
    {"VOWEL", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"TONGUE", "", nullptr, nullptr, -1.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::Pan, ParamCurve::Linear, 0, 0},
    {"JAW", "", nullptr, nullptr, -1.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::Pan, ParamCurve::Linear, 0, 0},
    {"LIPS", "", nullptr, nullptr, -1.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::Pan, ParamCurve::Linear, 0, 0},
    {"CLOSE", "", kCloseNames, nullptr, 0.0f, 3.0f, 0.0f, 3, 1, 4, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"GLIDE", "ms", nullptr, nullptr, 5.0f, 500.0f, 30.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"SIZE", "", kSizeNames, nullptr, 0.0f, 4.0f, 2.0f, 4, 1, 5, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"RES", "%", nullptr, nullptr, 0.0f, 1.0f, 0.5f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"HISS", "%", nullptr, nullptr, 0.0f, 1.0f, 0.3f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"MIX", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"SLIDE", "ms", nullptr, nullptr, 0.0f, 2000.0f, 0.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"ACCENT", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"SOURCE", "", kSourceNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
};

constexpr o3sdk::dsp::UnitInfo kInfo{"TRACT", o3sdk::dsp::UnitKind::Effect, kParams, TractUnit::kParamCount, nullptr, 0};

float clamp1(float x) noexcept { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }

float abs_f(float x) noexcept { return x < 0.0f ? -x : x; }

// The largest absolute value of n samples (a NaN counts as loud: it must wake the tube, which sanitises it).
float peak(const float* p, std::uint32_t from, std::uint32_t to) noexcept {
  float m = 0.0f;
  for (std::uint32_t i = from; i < to; ++i) {
    const float a = abs_f(p[i]);
    m = (a > m || a != a) ? a : m;
  }
  return m;
}

int round_index(float v, int hi) noexcept {
  const int i = static_cast<int>(v + 0.5f);
  return i < 0 ? 0 : (i > hi ? hi : i);
}

}  // namespace

TractUnit::TractUnit() noexcept : proc_(o3sdk::extras::tract::Processor::Quality::Full) {}

float TractUnit::bent_jaw() const noexcept { return clamp1(values_[Jaw] + values_[Accent]); }

const o3sdk::dsp::UnitInfo& TractUnit::info() const noexcept { return kInfo; }

void TractUnit::prepare(float sample_rate) noexcept {
  fs_ = sample_rate;
  hold_frames_ = static_cast<std::uint32_t>(kEnterHoldMs * 0.001f * fs_);
  hold_left_ = 0;
  quiet_ = 0;
  processed_ = 0;
  asleep_ = false;
  engaged_ = false;
  mix_ = 0.0f;
  live_ = false;
  proc_.prepare(sample_rate);
  proc_.set_mix(0.0f);
  for (unsigned p = 0; p < kParamCount; ++p) {
    set(static_cast<std::uint16_t>(p), kParams[p].def);
  }
  proc_.reset();  // the defaults land at once, and MIX 0 is a settled bypass
}

void TractUnit::set(std::uint16_t id, float v) noexcept {
  if (id >= kParamCount) {
    return;
  }
  values_[id] = v;
  switch (id) {
    case Vowel:
      proc_.set_vowel(v);
      break;
    case Tongue:
    case Jaw:
    case Lips:
    case Accent:  // the jaw opens by ACCENT more, within the Processor's own range
      proc_.set_bend(values_[Tongue], bent_jaw(), values_[Lips]);
      break;
    case Close: {
      const int c = round_index(v, 3);
      for (unsigned which = 0; which < 3; ++which) {
        proc_.set_closure(which, c == static_cast<int>(which) + 1);
      }
      break;
    }
    case Glide:
    case Slide:  // SLIDE above 0 is the sequencer's glide and wins over GLIDE; 0 gives the knob back its say
      proc_.set_glide_ms(values_[Slide] > 0.0f ? values_[Slide] : values_[Glide]);  // the Processor clamps to 5..500 ms
      break;
    case Size:
      proc_.set_size(round_index(v, 4) - 2);  // BIG is -2 (the longest tube), TINY +2
      break;
    case Res:
      proc_.set_res(v);
      break;
    case Hiss:
      proc_.set_hiss(v);
      break;
    case Mix: {
      mix_ = (v != v || v < 1.0e-3f) ? 0.0f : (v > 1.0f ? 1.0f : v);
      if (mix_ == 0.0f) {
        hold_left_ = 0;
        engaged_ = false;
        proc_.set_mix(0.0f);
        if (asleep_) {  // nothing runs to ramp it down: land on the bypass
          proc_.reset();
          asleep_ = false;
          quiet_ = 0;
        }
      } else if (engaged_) {
        proc_.set_mix(mix_);
      } else if (!proc_.bypassed()) {  // still ringing down from a drop to 0: take it back at once
        engaged_ = true;
        hold_left_ = 0;
        proc_.set_mix(mix_);
      } else if (hold_left_ == 0) {
        hold_left_ = hold_frames_ != 0u ? hold_frames_ : 1u;  // the Processor stays at MIX 0 until the hold ends
      }
      update_live();
      break;
    }
    case Source:
      update_live();
      break;
    default:
      break;
  }
}

// SOURCE LIVE, a link and a MIX above 0: the Vocoder is asked for the voice's shape. Anything else is the pads' vowel, through the kit's way back
// (the diameters glide from the areas to the vowel's shape).
void TractUnit::update_live() noexcept {
  live_ = link_ != nullptr && round_index(values_[Source], 1) == 1 && mix_ > 0.0f;
  if (!live_) {
    proc_.use_shape();   // a no-op unless the areas rule
    if (link_ != nullptr) link_->valid = false;   // the shape of a voice that is gone must not come back when LIVE is entered again (same render context as the Vocoder)
  }
}

// Once a block, before the tube runs: ask for the voice, and hand over its shape if there is one (the call is cheap, about 200 cycles: the
// Processor glides to the areas by itself). Without a shape yet (nothing heard) the pads' vowel sounds; once there is one it holds in silence.
void TractUnit::drive_mouth() noexcept {
  if (!live_) return;
  link_->want = true;
  if (link_->valid) {
    proc_.set_areas(link_->area, link_->n);
  } else {
    proc_.use_shape();
  }
}

void TractUnit::apply(const o3sdk::dsp::Event& e) noexcept {
  if (e.kind == o3sdk::dsp::EventKind::Param) {
    set(e.param, e.value);
  }
}

// Frames [from, to) pass untouched (a copy when the planes are not in place).
void TractUnit::pass(const o3sdk::dsp::ProcessArgs& a, std::uint32_t from, std::uint32_t to) noexcept {
  const std::uint32_t n = to - from;
  if (a.out_l != a.in_l) {
    std::memmove(a.out_l + from, a.in_l + from, n * sizeof(float));
  }
  if (a.out_r != a.in_r) {
    std::memmove(a.out_r + from, a.in_r + from, n * sizeof(float));
  }
}

void TractUnit::run(const o3sdk::dsp::ProcessArgs& a, std::uint32_t from, std::uint32_t to) noexcept {
  while (from < to) {
    if (hold_left_ != 0u) {  // MIX was raised: a bypass until the old engine's crossfade is over
      const std::uint32_t n = (to - from) < hold_left_ ? (to - from) : hold_left_;
      pass(a, from, from + n);
      from += n;
      hold_left_ -= n;
      if (hold_left_ == 0u) {
        engaged_ = true;
        proc_.set_mix(mix_);
      }
      continue;
    }
    if (!engaged_ && proc_.bypassed()) {  // MIX 0 and settled: the planes pass bit for bit, no state runs
      pass(a, from, to);
      return;
    }
    const std::uint32_t n = to - from;
    const float pl = peak(a.in_l, from, to);
    const float pr = peak(a.in_r, from, to);
    const float in_peak = (pl > pr || pl != pl) ? pl : pr;
    if (asleep_) {
      if (in_peak <= kSilent) {  // silent in, nothing in the tube: silent out
        std::memset(a.out_l + from, 0, n * sizeof(float));
        std::memset(a.out_r + from, 0, n * sizeof(float));
        return;
      }
      proc_.reset();  // a clean tube: its state was under -80 dB, so the reset steps nothing
      asleep_ = false;
      quiet_ = 0;
    }
    for (std::uint32_t i = 0; i < n; ++i) {
      mono_[i] = 0.5f * (a.in_l[from + i] + a.in_r[from + i]);
    }
    drive_mouth();
    proc_.process(mono_, mono_, n);
    processed_ += n;
    std::memcpy(a.out_l + from, mono_, n * sizeof(float));
    std::memcpy(a.out_r + from, mono_, n * sizeof(float));
    if (in_peak <= kSilent && peak(mono_, 0, n) <= kSilent && engaged_) {
      quiet_ += n;
      if (quiet_ >= kSleepFrames) {
        asleep_ = true;
      }
    } else {
      quiet_ = 0;
    }
    return;
  }
}

void TractUnit::process(const o3sdk::dsp::ProcessArgs& args) noexcept {
  if (live_) link_->want = true;   // the Vocoder analyses the voice in the block after one that asked, so the shape is there when the tube wakes
  o3sdk::dsp::for_each_run(
      args, [&](std::uint32_t from, std::uint32_t to) { run(args, from, to); }, [&](const o3sdk::dsp::Event& e) { apply(e); });
  if (!live_ && link_ != nullptr) link_->valid = false;   // an analysis that was in flight when LIVE was left lands one block late: dropped here, so no old shape survives
}

}  // namespace o3_voc03er
