// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <cmath>
#include <cstring>

namespace o3_voc03er {

namespace {

using o3sdk::core::ParamCurve;
using o3sdk::core::ParamUnit;
using o3sdk::dsp::Ramp;

constexpr const char* kWaveNames[] = {"SAW", "PULSE"};
constexpr const char* kModeNames[] = {"BANDS", "TALK", "TUNE"};
constexpr const char* kFreezeNames[] = {"OFF", "ON"};
constexpr const char* kKeyNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr const char* kScaleNames[] = {"CHROM", "MAJOR", "MINOR", "MAJ-P", "MIN-P", "BLUES", "DORIAN"};
constexpr const char* kNotesInNames[] = {"OFF", "ON"};

// name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page
constexpr const char* kRecNames[] = {"OFF", "ARM"};
constexpr const char* kPlayNames[] = {"OFF", "ON"};
constexpr const char* kRoleNames[] = {"MOD", "CAR"};

constexpr o3sdk::core::ParamDesc loop_row(const char* name, const char* const* names) {
  return {name, "", names, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
}

constexpr const char* kSpeedNames[] = {"0.5X", "1X", "2X"};
constexpr const char* kDirNames[] = {"FWD", "REV"};
constexpr o3sdk::core::ParamDesc speed_row(const char* name) {
  return {name, "", kSpeedNames, nullptr, 0.0f, 2.0f, 1.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
}
constexpr o3sdk::core::ParamDesc dir_row(const char* name) { return loop_row(name, kDirNames); }
constexpr o3sdk::core::ParamDesc level_row(const char* name) {
  return {name, "dB", nullptr, nullptr, -24.0f, 12.0f, 0.0f, 72, 12, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0};
}

constexpr const char* kCarNames[] = {"FOLLOW", "SYNTH", "NOISE", "LOOP1", "LOOP2", "LOOP3", "LOOP4"};

constexpr o3sdk::core::ParamDesc kParams[Vocoder::kParamCount] = {
    {"WAVE", "", kWaveNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"NOISE", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"ATTACK", "ms", nullptr, nullptr, 1.0f, 500.0f, 5.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"RELEASE", "ms", nullptr, nullptr, 5.0f, 2000.0f, 80.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"DRY", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"LEVEL", "dB", nullptr, nullptr, -48.0f, 6.0f, -6.0f, 108, 12, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0},
    {"DETUNE", "ct", nullptr, nullptr, 0.0f, 20.0f, 8.0f, 20, 5, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"MODE", "", kModeNames, nullptr, 0.0f, 2.0f, 0.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"QUALITY", "", nullptr, nullptr, 8.0f, 24.0f, 16.0f, 16, 2, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"FREEZE", "", kFreezeNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    loop_row("L1 REC", kRecNames), loop_row("L1 PLAY", kPlayNames), loop_row("L1 ROLE", kRoleNames),
    loop_row("L2 REC", kRecNames), loop_row("L2 PLAY", kPlayNames), loop_row("L2 ROLE", kRoleNames),
    loop_row("L3 REC", kRecNames), loop_row("L3 PLAY", kPlayNames), loop_row("L3 ROLE", kRoleNames),
    loop_row("L4 REC", kRecNames), loop_row("L4 PLAY", kPlayNames), loop_row("L4 ROLE", kRoleNames),
    speed_row("L1 SPEED"), dir_row("L1 DIR"), level_row("L1 LEVEL"), speed_row("L2 SPEED"), dir_row("L2 DIR"), level_row("L2 LEVEL"),
    speed_row("L3 SPEED"), dir_row("L3 DIR"), level_row("L3 LEVEL"), speed_row("L4 SPEED"), dir_row("L4 DIR"), level_row("L4 LEVEL"),
    {"CARRIER", "", kCarNames, nullptr, 0.0f, 6.0f, 0.0f, 6, 1, 7, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"KEY", "", kKeyNames, nullptr, 0.0f, 11.0f, 0.0f, 11, 1, 12, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"SCALE", "", kScaleNames, nullptr, 0.0f, 6.0f, 0.0f, 6, 1, 7, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"SPEED", "ms", nullptr, nullptr, 0.0f, 500.0f, 25.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"AMOUNT", "%", nullptr, nullptr, 0.0f, 1.0f, 1.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"NOTES-IN", "", kNotesInNames, nullptr, 0.0f, 1.0f, 1.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"FORMANT", "st", nullptr, nullptr, -12.0f, 12.0f, 0.0f, 24, 4, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"THRU", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
};

constexpr o3sdk::dsp::UnitInfo kInfo{"VOCODER", o3sdk::dsp::UnitKind::Instrument, kParams, Vocoder::kParamCount, nullptr, 0};

constexpr float kRampMs = 5.0f;
constexpr float kModeFadeMs = 10.0f;  // crossfade between the modes
constexpr float kTuneGain = 1.0f;     // Tune returns the voice itself: unity
constexpr float kTalkGain = 1.0f;     // Talk's output is a carrier-level signal shaped by the modulator's rms; not tuned (needs the unit's ears)
constexpr float kWetGain = 128.0f;  // the followers read a modulator the AGC holds at a mean-abs of 0.1, and each band passes a slice of the carrier: a vowel on a three-note saw chord peaks at about 0.9 at LEVEL 0 dB (0.47 at the default -6)

// Cubic soft clip: linear near 0, flat at +-1.5 where it reaches 0.999: the peak never goes above it.
inline float soft_clip(float x) noexcept {
  x = x > 1.5f ? 1.5f : (x < -1.5f ? -1.5f : x);
  return (x - 0.14814815f * x * x * x) * 0.999f;
}

constexpr std::uint32_t kBarTicks = 4u * o3sdk::dsp::kPpqn;  // 4/4: the transport has no time signature

// The frame of this block (below n) at which the transport reaches a bar line, -1 if none. A block is far shorter than a bar.
int bar_frame(const o3sdk::dsp::Transport& t, std::uint32_t n) noexcept {
  if (!t.playing || t.frames == 0u) {
    return -1;
  }
  const std::uint32_t k = static_cast<std::uint32_t>(t.pos >> 32) / kBarTicks;
  for (std::uint32_t c = k; c <= k + 1u; ++c) {
    const int f = o3sdk::dsp::frame_at(t, static_cast<std::uint64_t>(c) * kBarTicks);
    if (f >= 0 && static_cast<std::uint32_t>(f) < n) {
      return f;
    }
  }
  return -1;
}

// Where a loop action asked at frame `min` of this block happens: at the next bar line, or at once while the transport is stopped.
int locate(const o3sdk::dsp::Transport& t, std::uint32_t n, std::uint32_t min) noexcept {
  if (!t.playing) {
    return static_cast<int>(min);
  }
  const int f = bar_frame(t, n);
  return f >= static_cast<int>(min) ? f : -1;
}

}  // namespace

const o3sdk::dsp::UnitInfo& Vocoder::info() const noexcept { return kInfo; }

void Vocoder::prepare(float sample_rate) noexcept {
  carrier_.set_noise_source(&noise_);  // one generator for the carrier's noise and the unvoiced path
  bands_.set_noise_source(&noise_);
  carrier_.prepare(sample_rate);
  bands_.prepare(sample_rate);
  talk_.prepare(sample_rate);
  const auto fade = static_cast<std::uint32_t>(kModeFadeMs * 0.001f * sample_rate);
  bands_mix_.set_length(fade);
  talk_mix_.set_length(fade);
  tune_mix_.set_length(fade);
  const auto ramp = static_cast<std::uint32_t>(kRampMs * 0.001f * sample_rate);
  level_.set_length(ramp);
  dry_.set_length(ramp);
  for (unsigned p = 0; p < kParamCount; ++p) {
    set(static_cast<std::uint16_t>(p), kParams[p].def);
  }
  level_.jump(level_.target_value());
  dry_.jump(dry_.target_value());
  bands_mix_.jump(1.0f);
  talk_mix_.jump(0.0f);
  tune_mix_.jump(0.0f);
  tune_.reset();
  loops_.clear();
  for (Slot& s : slot_) {
    s = Slot{};
  }
  sel_ = 0;
  npend_ = 0;
  synth_gain_.set_length(ramp);
  noise_amt_.set_length(ramp);
  synth_gain_.jump(1.0f);
  noise_amt_.jump(noise_user_);
  for (Ramp& g : loop_car_) {
    g.set_length(ramp);
    g.jump(0.0f);
  }
  publish_loops();
}

void Vocoder::set(std::uint16_t id, float v) noexcept {
  if (id >= kParamCount) {
    return;
  }
  v = o3sdk::core::param_clamp(kParams[id], v);
  if (id == CarSrc) {
    select_carrier(v, 0);
    return;
  }
  if (id >= Loop1Rec && id < CarSrc) {
    return;  // the loops' parameters carry a frame: loop_param_event
  }
  switch (id) {
    case Wave:
      carrier_.set_wave(v >= 0.5f ? Carrier::Wave::Pulse : Carrier::Wave::Saw);
      break;
    case Noise:
      noise_user_ = v;
      if (sel_ == 0u) {
        noise_amt_.jump(v);  // the knob acts at once, as it always did; only a carrier switch ramps
      }
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
      const unsigned m = v >= 1.5f ? 2u : (v >= 0.5f ? 1u : 0u);
      Ramp* const ramps[3] = {&bands_mix_, &talk_mix_, &tune_mix_};
      for (unsigned i = 0; i < 3; ++i) {
        const float t = i == m ? 1.0f : 0.0f;
        Ramp& r = *ramps[i];
        if (t == r.target_value()) continue;
        if (t > 0.5f && r.value() <= 0.0f) {
          // The mode was not running: what it remembers is stale.
          if (i == 0u) bands_.restart();
          else if (i == 1u) talk_.reset();
          else tune_.reset();
        }
        r.target(t);
      }
      break;
    }
    case Key:
      tune_.set_key(static_cast<unsigned>(v + 0.5f));
      break;
    case Scale:
      tune_.set_scale(static_cast<unsigned>(v + 0.5f));
      break;
    case Speed:
      tune_.set_speed_ms(v);
      break;
    case Amount:
      tune_.set_amount(v);
      break;
    case NotesIn:
      tune_.set_notes_in(v >= 0.5f);
      break;
    case Formant:
      bands_.set_formant(std::floor(v + 0.5f));  // integer semitones
      break;
    case Thru:
      bands_.set_thru(v);
      break;
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
      tune_.note_on(e.note);
      break;
    case EventKind::NoteOff:
      carrier_.note_off(e.note);
      tune_.note_off(e.note);
      break;
    case EventKind::Param:
      if (e.param == CarSrc) {
        select_carrier(o3sdk::core::param_clamp(kParams[CarSrc], e.value), e.frame);
      } else if (e.param >= Loop1Rec && e.param < CarSrc) {
        loop_param_event(e.param, e.value, e.frame);
      } else {
        set(e.param, e.value);
      }
      break;
    case EventKind::AllNotesOff:
      carrier_.all_notes_off();
      tune_.all_notes_off();
      break;
  }
}

void Vocoder::loop_param_event(std::uint16_t id, float v, std::uint32_t frame) noexcept {
  v = o3sdk::core::param_clamp(kParams[id], v);
  const bool play_ids = id >= Loop1Speed;
  const unsigned base = play_ids ? Loop1Speed : Loop1Rec;
  const unsigned n = (id - base) / 3u;
  const auto field = static_cast<LoopField>((id - base) % 3u + (play_ids ? 3u : 0u));
  Slot& s = slot_[n];
  const bool on = v >= 0.5f;
  if (!loops_.bound() && field != LoopField::Role) {
    return;  // no memory: the loops do nothing
  }
  switch (field) {
    case LoopField::Rec:
      s.want_rec = on;
      if (on || s.rec != Slot::Rec::Idle) {  // a Rec off on an idle slot must not move a pending Play's frame past its bar line
        s.req = static_cast<std::uint16_t>(frame);
      }
      if (on && s.rec == Slot::Rec::Idle) {
        s.rec = Slot::Rec::Armed;
        s.want_play = false;  // recording replaces the loop
      }
      break;
    case LoopField::Play:
      if (!on) {
        s.want_play = false;
        loops_.stop(n);
      } else if (s.rec == Slot::Rec::Idle && loops_.length(n) != 0u && !loops_.playing(n)) {
        s.want_play = true;
        s.req = static_cast<std::uint16_t>(frame);
      }
      break;
    case LoopField::Role:
      s.carrier = on;
      retarget(frame);
      break;
    case LoopField::Speed:
      s.speed = static_cast<std::uint8_t>(v + 0.5f);
      loops_.set_speed(n, s.speed);
      break;
    case LoopField::Dir:
      s.reverse = on;
      loops_.set_reverse(n, on);
      break;
    case LoopField::Level:
      s.level_db = v;
      loops_.set_gain(n, std::exp(v * 0.11512925f));  // dB -> linear, once an event
      break;
  }
}

// The carrier source changes at `frame`: the synth's gain and the noise amount start their ramps now (the run before this event has been
// rendered), the loops' at the same frame through pend_.
void Vocoder::select_carrier(float v, std::uint32_t frame) noexcept {
  sel_ = static_cast<unsigned>(v + 0.5f);
  synth_gain_.target(sel_ >= 3u ? 0.0f : 1.0f);
  noise_amt_.target(noise_target());
  retarget(frame);
}

void Vocoder::retarget(std::uint32_t frame) noexcept {
  if (npend_ == sizeof pend_ / sizeof pend_[0]) {
    --npend_;  // full: the last entry is replaced, the final state still wins
  }
  Retarget& r = pend_[npend_++];
  r.frame = static_cast<std::uint16_t>(frame);
  for (unsigned i = 0; i < Loop::kCount; ++i) {
    r.car[i] = car_target(i);
  }
}

// The synth for `n` samples: while a ramp is moving, in slices of 16 so the noise amount follows it (the Carrier takes one value a call) and the
// gain per sample; settled, one call and at most one multiply.
void Vocoder::render_carrier(float* out, float* gate, std::uint32_t n) noexcept {
  if (!synth_gain_.moving() && !noise_amt_.moving()) {
    carrier_.set_noise(noise_amt_.value());
    carrier_.render(out, n, gate);
    const float g = synth_gain_.value();
    if (g != 1.0f) {
      for (std::uint32_t i = 0; i < n; ++i) {
        out[i] *= g;
      }
    }
    return;
  }
  for (std::uint32_t off = 0; off < n; off += 16u) {
    const std::uint32_t k = n - off < 16u ? n - off : 16u;
    for (std::uint32_t j = 0; j < k; ++j) {
      (void)noise_amt_.next();
    }
    carrier_.set_noise(noise_amt_.value());
    carrier_.render(out + off, k, gate + off);
    for (std::uint32_t j = 0; j < k; ++j) {
      out[off + j] *= synth_gain_.next();
    }
  }
}

// A loop on the carrier side, from frame `from` of this block, through its gain ramp; pend_'s targets start the ramp at their frames. The loop
// is read in full (its position must advance) even when it is muted.
void Vocoder::mix_car(unsigned loop, std::uint32_t from, std::uint32_t n, float* car, float* tmp) noexcept {
  Ramp& g = loop_car_[loop];
  if (npend_ == 0u && !g.moving() && g.value() == 1.0f) {
    loops_.mix(loop, car + from, n - from, true);
    return;
  }
  loops_.mix(loop, tmp + from, n - from, false);
  std::uint32_t pos = from;
  const auto run = [&](std::uint32_t a, std::uint32_t b) {
    if (g.moving()) {
      for (std::uint32_t k = a; k < b; ++k) {
        car[k] += tmp[k] * g.next();
      }
    } else if (g.value() != 0.0f) {
      const float v = g.value();
      for (std::uint32_t k = a; k < b; ++k) {
        car[k] += tmp[k] * v;
      }
    }
  };
  for (unsigned k = 0; k < npend_; ++k) {
    if (pend_[k].frame > pos) {
      run(pos, pend_[k].frame);
      pos = pend_[k].frame;
    }
    g.target(pend_[k].car[loop]);
  }
  run(pos, n);
}

void Vocoder::publish_loops() noexcept {
  for (unsigned i = 0; i < Loop::kCount; ++i) {
    const Slot& s = slot_[i];
    std::uint32_t rec = 0;
    if (s.rec == Slot::Rec::Armed) {
      rec = 1;
    } else if (s.rec == Slot::Rec::Recording) {
      rec = s.want_rec ? 2u : 3u;
    }
    const std::uint32_t w = rec | (loops_.playing(i) ? 4u : 0u) | (s.want_play ? 8u : 0u) | (s.carrier ? 16u : 0u) | (loops_.length(i) << 8);
    status_[i].store(w, std::memory_order_relaxed);
    const std::uint32_t half_db = static_cast<std::uint32_t>(static_cast<int>(std::floor(s.level_db * 2.0f + 0.5f)) + 64);
    opts_[i].store(s.speed | (s.reverse ? 4u : 0u) | (half_db << 8), std::memory_order_relaxed);
  }
}

Vocoder::LoopStatus Vocoder::loop_status(unsigned loop) const noexcept {
  LoopStatus st;
  if (loop >= Loop::kCount) {
    return st;
  }
  const std::uint32_t w = status_[loop].load(std::memory_order_relaxed);
  st.rec = static_cast<LoopStatus::Rec>(w & 3u);
  st.playing = (w & 4u) != 0u;
  st.pending = (w & 8u) != 0u;
  st.carrier = (w & 16u) != 0u;
  st.length = w >> 8;
  const std::uint32_t o = opts_[loop].load(std::memory_order_relaxed);
  st.speed = static_cast<std::uint8_t>(o & 3u);
  st.reverse = (o & 4u) != 0u;
  st.level_db = static_cast<float>(static_cast<int>(o >> 8) - 64) * 0.5f;
  return st;
}

// The loops of one block, after the events and the carrier, before Bands or Talk. `mod` holds the live modulator and `car` the synth: a
// recording takes the live modulator first (a loop is recorded as heard at the input, whatever plays), then each playing loop replaces the
// modulator (a MOD loop; the second one adds) or adds to the carrier (a CAR loop). A loop that does not record or play costs a few compares.
void Vocoder::run_loops(const o3sdk::dsp::Transport& t, std::uint32_t n, float* mod, float* car) noexcept {
  if (!loops_.bound()) {
    npend_ = 0;
    return;
  }
  float tmp[Bands::kMaxFrames];
  std::uint32_t play_from[Loop::kCount] = {};
  for (unsigned i = 0; i < Loop::kCount; ++i) {
    Slot& s = slot_[i];
    std::uint32_t from = 0;
    if (s.rec == Slot::Rec::Armed) {
      if (!s.want_rec) {
        s.rec = Slot::Rec::Idle;
      } else {
        const int f = locate(t, n, s.req);
        if (f >= 0) {
          loops_.begin_record(i);
          s.rec = Slot::Rec::Recording;
          s.want_play = false;
          from = static_cast<std::uint32_t>(f);
        }
      }
    }
    if (s.rec == Slot::Rec::Recording) {
      std::uint32_t end = n;
      bool stop = false;
      if (!s.want_rec) {
        const int f = locate(t, n, s.req);
        if (f >= 0) {
          end = static_cast<std::uint32_t>(f);
          stop = true;
        }
      }
      if (end > from) {
        (void)loops_.record(i, mod + from, end - from);
      }
      if (!loops_.recording(i)) {  // full: the loop ended itself
        s.rec = Slot::Rec::Idle;
        s.want_rec = false;
      } else if (stop) {
        loops_.end_record(i);
        s.rec = Slot::Rec::Idle;
      }
    }
  }
  bool modulated = false;
  for (unsigned i = 0; i < Loop::kCount; ++i) {
    Slot& s = slot_[i];
    if (s.want_play && s.rec == Slot::Rec::Idle) {
      const int f = locate(t, n, s.req);
      if (f >= 0 && loops_.start(i)) {
        s.want_play = false;
        play_from[i] = static_cast<std::uint32_t>(f);
      }
    }
    // sel_ is the block's last word: a loop leaving the carrier stays on that side until its fade has run from the event's frame.
    const bool on_car = s.carrier || sel_ == 3u + i || loop_car_[i].value() > 0.0f;
    if (loops_.playing(i)) {
      const std::uint32_t pf = play_from[i];
      if (on_car) {
        mix_car(i, pf, n, car, tmp);
      } else {
        loops_.mix(i, mod + pf, n - pf, modulated);
        modulated = true;
      }
    }
    if (!loops_.playing(i) || !on_car) {
      loop_car_[i].jump(car_target(i));  // not on the carrier side: nothing to fade, the ramp waits at where it will start
    }
    s.req = 0;
  }
  npend_ = 0;
  publish_loops();
}

namespace {
// log2 of a positive float from its exponent and a linear mantissa (under 0.09 octave off: a meter, not a measurement), no libm.
inline float fast_log2(float x) noexcept {
  std::uint32_t b;
  std::memcpy(&b, &x, sizeof b);
  const int e = static_cast<int>((b >> 23) & 0xFFu) - 127;
  const std::uint32_t m = (b & 0x7FFFFFu) | 0x3F800000u;
  float f;
  std::memcpy(&f, &m, sizeof f);
  return static_cast<float>(e) + (f - 1.0f);
}
}  // namespace

void Vocoder::publish_view() noexcept {
  for (unsigned w = 0; w < 4; ++w) {
    std::uint32_t word = 0;
    for (unsigned k = 0; k < 4; ++k) {
      const float env = bands_.envelope(w * 4u + k);
      float db = env > 1.0e-6f ? fast_log2(env) * 6.0206f : -60.0f;
      db = db < -60.0f ? -60.0f : (db > 0.0f ? 0.0f : db);
      word |= static_cast<std::uint32_t>((db + 60.0f) * (255.0f / 60.0f) + 0.5f) << (8u * k);
    }
    view_[w].store(word, std::memory_order_relaxed);
  }
}

Vocoder::View Vocoder::view() const noexcept {
  View v;
  for (unsigned w = 0; w < 4; ++w) {
    const std::uint32_t word = view_[w].load(std::memory_order_relaxed);
    for (unsigned k = 0; k < 4; ++k) v.band[w * 4u + k] = static_cast<std::uint8_t>(word >> (8u * k));
  }
  return v;
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
      run, [&](std::uint32_t from, std::uint32_t to) {
        if (tune_mix_.value() >= 1.0f && tune_mix_.target_value() >= 1.0f) {  // TUNE alone: the carrier is not heard
          for (std::uint32_t i = from; i < to; ++i) car[i] = gate[i] = 0.0f;
        } else {
          render_carrier(car + from, gate + from, to - from);
        }
      },
      [&](const o3sdk::dsp::Event& e) { apply(e); });
  run_loops(*args.transport, n, mod, car);
  // Only the active mode runs; two or three run while a crossfade is under way.
  const auto runs = [](const Ramp& r) { return r.value() > 0.0f || r.target_value() > 0.0f; };
  float tw[Bands::kMaxFrames];
  float tn[Bands::kMaxFrames];
  if (runs(bands_mix_)) {
    bands_.process(mod, car, wet, n, gate);
  } else {
    for (std::uint32_t i = 0; i < n; ++i) wet[i] = 0.0f;
  }
  if (runs(talk_mix_)) {
    talk_.process(mod, car, tw, n);
  } else {
    for (std::uint32_t i = 0; i < n; ++i) tw[i] = 0.0f;
  }
  if (runs(tune_mix_)) {
    tune_.process(mod, tn, n);
  } else {
    for (std::uint32_t i = 0; i < n; ++i) tn[i] = 0.0f;
  }
  publish_view();  // the BANDS followers for the PERFORM screen, once a block
  if (!level_.moving() && !dry_.moving() && !bands_mix_.moving() && !talk_mix_.moving() && !tune_mix_.moving()) {
    const float g = level_.value();
    const float d = dry_.value();
    const float wm = kWetGain * bands_mix_.value();
    const float tm = kTalkGain * talk_mix_.value();
    const float um = kTuneGain * tune_mix_.value();
    for (std::uint32_t i = 0; i < n; ++i) {
      const float y = soft_clip(g * (wet[i] * wm + tw[i] * tm + tn[i] * um + d * mod[i]));
      args.out_l[i] = y;
      args.out_r[i] = y;
    }
    return;
  }
  for (std::uint32_t i = 0; i < n; ++i) {
    const float g = level_.next();
    const float d = dry_.next();
    const float wm = bands_mix_.next();
    const float tm = talk_mix_.next();
    const float um = tune_mix_.next();
    const float y = soft_clip(g * ((wet[i] * kWetGain) * wm + (tw[i] * kTalkGain) * tm + (tn[i] * kTuneGain) * um + d * mod[i]));
    args.out_l[i] = y;
    args.out_r[i] = y;
  }
}

}  // namespace o3_voc03er
