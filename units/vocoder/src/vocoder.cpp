// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <cmath>
#include <cstring>

namespace o3_voc03er {

namespace {

using o3sdk::core::ParamCurve;
using o3sdk::core::ParamUnit;
using o3sdk::dsp::Ramp;

constexpr const char* kModeNames[] = {"BANDS", "TALK", "TUNE", "ROBOT", "TRACT"};
constexpr const char* kFreezeNames[] = {"OFF", "ON"};
constexpr const char* kKeyNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr const char* kScaleNames[] = {"CHROM", "MAJOR", "MINOR", "MAJ-P", "MIN-P", "BLUES", "DORIAN"};
constexpr const char* kNotesInNames[] = {"OFF", "ON"};
constexpr const char* kGateNames[] = {"OFF", "1/4", "1/8", "1/16", "1/32"};
constexpr const char* kModNames[] = {"LIVE", "NONE", "-", "SLOT1", "SLOT2", "SLOT3", "SLOT4"};
constexpr const char* kArpNames[] = {"OFF", "UP", "DOWN", "UP-DOWN", "RANDOM"};
constexpr const char* kLiveChNames[] = {"L", "R"};

// name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page
constexpr const char* kRecNames[] = {"OFF", "ARM", "CLEAR"};
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
constexpr o3sdk::core::ParamDesc rec_row(const char* name) {
  return {name, "", kRecNames, nullptr, 0.0f, 2.0f, 0.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
}
constexpr o3sdk::core::ParamDesc level_row(const char* name) {
  return {name, "dB", nullptr, nullptr, -24.0f, 12.0f, 0.0f, 72, 12, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0};
}

// Region play: positions and lengths in samples of the bank (2,822,400: exact in a float), written before the Trig.
constexpr float kBankMax = static_cast<float>(Loop::kBankSamples);
constexpr o3sdk::core::ParamDesc region_start_row(const char* name) {
  return {name, "", nullptr, nullptr, 0.0f, kBankMax - 1.0f, 0.0f, 2048, 16, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
}
constexpr o3sdk::core::ParamDesc region_len_row(const char* name) {
  return {name, "", nullptr, nullptr, 0.0f, kBankMax, 0.0f, 2048, 16, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
}
constexpr const char* kTrigNames[] = {"OFF", "TRIG"};
constexpr o3sdk::core::ParamDesc region_trig_row(const char* name) { return loop_row(name, kTrigNames); }

constexpr const char* kCarNames[] = {"FOLLOW", "SYNTH", "NOISE", "LOOP1", "LOOP2", "LOOP3", "LOOP4", "LIVE", "VOICE"};

constexpr o3sdk::core::ParamDesc kParams[Vocoder::kParamCount] = {
    {"WAVE", "", nullptr, nullptr, 0.0f, 2.0f, 0.0f, 40, 4, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0},  // 0 SAW, 1 PULSE, 2 NARROW; a morph between (names: the app)
    {"NOISE", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"ATTACK", "ms", nullptr, nullptr, 1.0f, 500.0f, 5.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"RELEASE", "ms", nullptr, nullptr, 5.0f, 2000.0f, 80.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"DRY", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"LEVEL", "dB", nullptr, nullptr, -48.0f, 6.0f, -6.0f, 108, 12, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0},
    {"DETUNE", "ct", nullptr, nullptr, 0.0f, 100.0f, 8.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},  // SPREAD: oscillator 2 at +ct, 3 at -ct
    {"MODE", "", kModeNames, nullptr, 0.0f, 4.0f, 0.0f, 4, 1, 5, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"QUALITY", "", nullptr, nullptr, 8.0f, 24.0f, 16.0f, 16, 2, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"FREEZE", "", kFreezeNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    rec_row("L1 REC"), loop_row("L1 PLAY", kPlayNames), loop_row("L1 ROLE", kRoleNames),
    rec_row("L2 REC"), loop_row("L2 PLAY", kPlayNames), loop_row("L2 ROLE", kRoleNames),
    rec_row("L3 REC"), loop_row("L3 PLAY", kPlayNames), loop_row("L3 ROLE", kRoleNames),
    rec_row("L4 REC"), loop_row("L4 PLAY", kPlayNames), loop_row("L4 ROLE", kRoleNames),
    speed_row("L1 SPEED"), dir_row("L1 DIR"), level_row("L1 LEVEL"), speed_row("L2 SPEED"), dir_row("L2 DIR"), level_row("L2 LEVEL"),
    speed_row("L3 SPEED"), dir_row("L3 DIR"), level_row("L3 LEVEL"), speed_row("L4 SPEED"), dir_row("L4 DIR"), level_row("L4 LEVEL"),
    {"CARRIER", "", kCarNames, nullptr, 0.0f, 8.0f, 0.0f, 8, 1, 9, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"KEY", "", kKeyNames, nullptr, 0.0f, 11.0f, 0.0f, 11, 1, 12, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"SCALE", "", kScaleNames, nullptr, 0.0f, 6.0f, 0.0f, 6, 1, 7, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"SPEED", "ms", nullptr, nullptr, 0.0f, 500.0f, 25.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"AMOUNT", "%", nullptr, nullptr, 0.0f, 1.0f, 1.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"NOTES-IN", "", kNotesInNames, nullptr, 0.0f, 1.0f, 1.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"FORMANT", "st", nullptr, nullptr, -12.0f, 12.0f, 0.0f, 24, 4, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"THRU", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"BEND", "st", nullptr, nullptr, -12.0f, 12.0f, 0.0f, 24, 4, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"VIB", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"GLIDE", "ms", nullptr, nullptr, 0.0f, 500.0f, 0.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"GATE", "", kGateNames, nullptr, 0.0f, 4.0f, 0.0f, 4, 1, 5, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"ARP", "", kArpNames, nullptr, 0.0f, 4.0f, 0.0f, 4, 1, 5, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"MOD SRC", "", kModNames, nullptr, 0.0f, 6.0f, 0.0f, 6, 1, 7, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"LIVE CH", "", kLiveChNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    region_start_row("R1 START"), region_len_row("R1 LEN"), region_trig_row("R1 TRIG"),
    region_start_row("R2 START"), region_len_row("R2 LEN"), region_trig_row("R2 TRIG"),
    region_start_row("R3 START"), region_len_row("R3 LEN"), region_trig_row("R3 TRIG"),
    region_start_row("R4 START"), region_len_row("R4 LEN"), region_trig_row("R4 TRIG"),
    // Load: slot x Loop::kMaxSamples + length, length 1..kMaxSamples (see load_event). 0 does nothing: the default that prepare() and
    // Engine::start() replay is harmless.
    {"LOAD", "", nullptr, nullptr, 0.0f, kBankMax, 0.0f, 2048, 16, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"SUB", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"TILT", "dB", nullptr, nullptr, -12.0f, 12.0f, 0.0f, 48, 4, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0},
};

constexpr o3sdk::dsp::UnitInfo kInfo{"VOCODER", o3sdk::dsp::UnitKind::Instrument, kParams, Vocoder::kParamCount, nullptr, 0};

constexpr float kRampMs = 5.0f;
constexpr float kVibHz = 5.5f;     // the vibrato's rate
constexpr float kVibPeakSt = 0.5f;  // its peak at depth 1
constexpr float kBendMs = 20.0f;  // the glide of a stepped Bend
constexpr float kGateEdgeMs = 2.0f;   // the soft edges of GATE
constexpr float kHalfBeats[4] = {0.5f, 0.25f, 0.125f, 0.0625f};  // half a step in beats for 1/4, 1/8, 1/16, 1/32
constexpr float kModeFadeMs = 10.0f;  // crossfade between the modes
constexpr float kTuneGain = 1.0f;     // Tune returns the voice itself: unity
// S3, TALK's output compression (the RobotComp of ROBOT with other constants): starting values, a bench's, by ear on the unit.
constexpr float kTalkCompAttackMs = 5.0f;
constexpr float kTalkCompReleaseMs = 120.0f;
constexpr float kTalkCompMakeup = 2.0f;
constexpr float kTalkGain = 28.0f;    // speech on a one-note saw has the rms in TALK that it has in BANDS, so a change of MODE keeps the level (bench, docs/ENGINE-REALISM.md); by ear on the unit
constexpr float kTalkCompThreshold = 0.125893f / kTalkGain;  // -18 dBFS (peak) of the TALK wet sum after kTalkGain, in TALK's own units; 2:1 above it
constexpr float kCarrierGain = 1.0f;  // TRACT: the carrier bus as it is (the tube's own level match keeps its output near the input's rms); [est] by ear on the unit
constexpr float kWetGain = 128.0f;  // the followers read a modulator the AGC holds at a mean-abs of 0.1, and each band passes a slice of the carrier: a vowel on a three-note saw chord peaks at about 0.9 at LEVEL 0 dB (0.47 at the default -6)

// Cubic soft clip: linear near 0, flat at +-1.5 where it reaches 0.999: the peak never goes above it.
inline float soft_clip(float x) noexcept {
  x = x > 1.5f ? 1.5f : (x < -1.5f ? -1.5f : x);
  return (x - 0.14814815f * x * x * x) * 0.999f;
}

constexpr std::uint32_t kBarTicks = 4u * o3sdk::dsp::kPpqn;  // 4/4: the transport has no time signature

// The frame of this block (below n) at which the transport reaches a bar line, -1 if none, and the line's bar index in `bar`. A block is far
// shorter than a bar.
int bar_frame(const o3sdk::dsp::Transport& t, std::uint32_t n, std::uint32_t& bar) noexcept {
  if (!t.playing || t.frames == 0u) {
    return -1;
  }
  const std::uint32_t k = static_cast<std::uint32_t>(t.pos >> 32) / kBarTicks;
  for (std::uint32_t c = k; c <= k + 1u; ++c) {
    const int f = o3sdk::dsp::frame_at(t, static_cast<std::uint64_t>(c) * kBarTicks);
    if (f >= 0 && static_cast<std::uint32_t>(f) < n) {
      bar = c;
      return f;
    }
  }
  return -1;
}

}  // namespace

const o3sdk::dsp::UnitInfo& Vocoder::info() const noexcept { return kInfo; }

void Vocoder::prepare(float sample_rate) noexcept {
  fs_ = sample_rate;
  gate_step_ = 1.0f / (kGateEdgeMs * 0.001f * sample_rate);
  gate_g_ = 1.0f;
  clock_on_ = false;
  nheld_ = 0;
  owners_reset();
  arp_note_ = 0xFFu;
  arp_pos_ = 0xFFu;
  glide_n_ = 0;
  carrier_.set_noise_source(&noise_);  // one generator for the carrier's noise and the unvoiced path
  bands_.set_noise_source(&noise_);
  carrier_.prepare(sample_rate);
  bands_.prepare(sample_rate);
  talk_.prepare(sample_rate);
  const auto fade = static_cast<std::uint32_t>(kModeFadeMs * 0.001f * sample_rate);
  bands_mix_.set_length(fade);
  talk_mix_.set_length(fade);
  tune_mix_.set_length(fade);
  robot_mix_.set_length(fade);
  carrier_mix_.set_length(fade);
  comp_.prepare(sample_rate);
  talk_comp_.configure(kTalkCompAttackMs, kTalkCompReleaseMs, kTalkCompThreshold, kTalkCompMakeup, true);
  talk_comp_.prepare(sample_rate);
  const auto ramp = static_cast<std::uint32_t>(kRampMs * 0.001f * sample_rate);
  level_.set_length(ramp);
  dry_.set_length(ramp);
  bend_.set_length(static_cast<std::uint32_t>(kBendMs * 0.001f * sample_rate));
  for (unsigned p = 0; p < kParamCount; ++p) {
    set(static_cast<std::uint16_t>(p), kParams[p].def);
  }
  level_.jump(level_.target_value());
  dry_.jump(dry_.target_value());
  vib_.set_length(static_cast<std::uint32_t>(kBendMs * 0.001f * sample_rate));
  vib_inc_ = kVibHz / sample_rate;
  vib_.jump(0.0f);
  vib_phase_ = 0.0f;
  vib_was_ = false;
  bend_.jump(0.0f);
  carrier_.set_bend_ratio(1.0f);
  bands_mix_.jump(1.0f);
  talk_mix_.jump(0.0f);
  tune_mix_.jump(0.0f);
  robot_mix_.jump(0.0f);
  carrier_mix_.jump(0.0f);
  tune_.reset();
  loops_.clear();
  for (Slot& s : slot_) {
    s = Slot{};
  }
  sel_ = 0;
  tune_.set_voice(false);
  vn_ = 0;
  msel_ = 0;
  live_car_.set_length(ramp);
  live_car_.jump(0.0f);
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
  if (id == Load) {
    load_event(v);
    return;
  }
  if (id >= R1Start && id < Sub) {
    region_param_event(id, v, 0);
    return;
  }
  switch (id) {
    case ModSrc:
      msel_ = static_cast<unsigned>(v + 0.5f);
      break;
    case LiveCh:
      live_r_ = v >= 0.5f;
      break;
    case Wave:
      carrier_.set_shape(v);
      break;
    case Sub:
      carrier_.set_sub(v);
      break;
    case Tilt:
      carrier_.set_tilt_db(v);
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
      const unsigned m = v >= 3.5f ? 4u : (v >= 2.5f ? 3u : (v >= 1.5f ? 2u : (v >= 0.5f ? 1u : 0u)));
      Ramp* const ramps[5] = {&bands_mix_, &talk_mix_, &tune_mix_, &robot_mix_, &carrier_mix_};
      for (unsigned i = 0; i < 5; ++i) {
        const float t = i == m ? 1.0f : 0.0f;
        Ramp& r = *ramps[i];
        if (t == r.target_value()) continue;
        if (t > 0.5f && r.value() <= 0.0f) {
          // The mode was not running: what it remembers is stale. BANDS and ROBOT are one engine: it restarts only when neither was heard.
          const float lvl = talk_mix_.value() > 0.0f ? talk_.agc_level() : bands_.agc_level();  // from the engine that has been hearing the input
          if (i == 0u || i == 3u) {
            if (bands_mix_.value() <= 0.0f && robot_mix_.value() <= 0.0f) {
              bands_.restart();
              bands_.adopt_level(lvl);
            }
            if (i == 3u) comp_.reset();
          } else if (i == 1u) {
            talk_.reset();
            talk_comp_.reset();
            talk_.adopt_level(lvl);
          } else if (i == 2u) {
            tune_.reset();
          }  // i == 4 (TRACT): the carrier bus has no engine of its own, nothing to restart
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
      talk_.set_formant(v);                       // Talk rounds and clamps itself; 0 is the exact old path
      break;
    case Thru:
      bands_.set_thru(v);
      talk_.set_thru(v);
      break;
    case Bend:
      bend_.target(v);
      break;
    case Vib:
      vib_.target(v);
      break;
    case Glide:
      glide_ms_ = v;
      glide_n_ = 0;
      break;
    case Gate: {
      const bool was_on = clock_on_;
      gate_ = static_cast<std::uint8_t>(v + 0.5f);
      clock_on_ = gate_ != 0u || arp_ != 0u;
      if (clock_on_ && !was_on) {
        clock_start_now();
      }
      break;
    }
    case Arp:
      set_arp(static_cast<unsigned>(v + 0.5f));
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

// An unmarked NoteOn in the slice range fires a slice of the published source at e.frame (see kNoteLocal), when set_midi_slices allows it; a
// kNoteSlice NoteOn does in every mode and is never the carrier's. Returns true when the note was taken, fired or refused, so it never reaches
// the carrier; false when it is the carrier's.
bool Vocoder::midi_slice_on(const o3sdk::dsp::Event& e) noexcept {
  const bool marked = (e.flags & kNoteSlice) != 0u;
  if ((e.flags & kNoteLocal) != 0u) {
    return false;
  }
  if (!marked && !midi_slices_.load(std::memory_order_relaxed)) {
    return false;
  }
  if (slices_ == nullptr || e.note < kSliceNoteLow) {
    return marked;
  }
  const std::uint32_t src = __atomic_load_n(&slices_->source, __ATOMIC_ACQUIRE);
  const unsigned idx = e.note - kSliceNoteLow;
  const unsigned count = (src >> 8) & 0xFFu;
  const unsigned rd = src & 0xFFu;
  if (idx >= count || idx >= kSliceMax || rd >= Loop::kCount || !loops_.bound()) {
    return marked;  // a marked note is never the carrier's
  }
  if (!marked) {
    midi_fired_ |= 1u << idx;  // its NoteOff is the slice's, even when the slot refuses (a stuck carrier note cannot follow)
  }
  if (slot_[rd].rec != Slot::Rec::Idle) {
    return true;
  }
  loops_.set_region_start(rd, slices_->start[idx]);
  loops_.set_region_len(rd, slices_->len[idx]);
  (void)loops_.trig_region(rd, e.frame, e.velocity >= 100u ? std::uint8_t{100} : e.velocity);
  return true;
}

void Vocoder::apply(const o3sdk::dsp::Event& e) noexcept {
  using o3sdk::dsp::EventKind;
  switch (e.kind) {
    case EventKind::NoteOn: {
      if (midi_slice_on(e)) {
        break;
      }
      if ((e.flags & kNoteLocal) == 0u) {  // a MIDI note of the carrier: PATTERN may record it (take_note_in)
        const std::uint32_t w = notein_w_.load(std::memory_order_relaxed);
        NoteIn n;
        n.note = e.note;
        n.velocity = e.velocity;
        n.playing = grid_playing_;
        n.sixteenth = nearest_sixteenth(grid_ticks_);
        notein_[w % kNoteInDepth].store(note_in_pack(n), std::memory_order_relaxed);
        notein_w_.store(w + 1u, std::memory_order_release);
      }
      // Another owner already holds the note (NO1): the voice is shared, so the note sounds on as it is (no retrigger, no new level). Only
      // while a voice really holds it: a note whose voice was stolen (a ninth note) or that the arp plays in turns is taken again as before.
      const unsigned owner = (static_cast<unsigned>(e.flags) >> kOwnerShift) & 3u;
      const unsigned others = note_owners(e.note) & ~(1u << owner);
      owner_set(e.note, owner);
      if (others != 0u && carrier_.holds(e.note)) {
        break;
      }
      // The last note sets the synth's level (one gain: a chord step shares its velocity). 100 and up is the full level, so pads and
      // old patterns are unchanged, and it costs nothing a sample: the gain is the synth_gain_ ramp that is there anyway.
      const float vg = e.velocity >= 100u ? 1.0f : static_cast<float>(e.velocity) * 0.01f;
      if (vg != vel_gain_) {
        vel_gain_ = vg;
        synth_gain_.target(synth_target());
      }
      held_insert(e.note);
      if (arp_ == 0u) {
        car_on(e.note);
      } else if (!playing_ && arp_note_ == 0xFFu && nheld_ != 0u) {
        clock_start_now();  // transport stopped, nothing sounding: the first note restarts the step clock and sounds at once
      }
      tune_.note_on(e.note);
      break;
    }
    case EventKind::NoteOff:
      if ((e.flags & kNoteSlice) != 0u) {
        break;  // the slice channel's release: a slice is one shot
      }
      if ((e.flags & kNoteLocal) == 0u && e.note >= kSliceNoteLow && e.note - kSliceNoteLow < 32u && (midi_fired_ & (1u << (e.note - kSliceNoteLow))) != 0u) {
        midi_fired_ &= ~(1u << (e.note - kSliceNoteLow));  // a fired slice is one shot: its key's release is not the carrier's
        break;
      }
      {  // NO1: the note ends when its last owner lets go (an unowned NoteOff of a note nobody holds is what it was: harmless)
        const unsigned owner = (static_cast<unsigned>(e.flags) >> kOwnerShift) & 3u;
        owner_clear(e.note, owner);
        if (note_owners(e.note) != 0u) {
          break;
        }
      }
      held_remove(e.note);
      if (arp_ == 0u) {
        car_off(e.note);
      } else if (nheld_ == 0u && arp_note_ != 0xFFu) {  // the last note let go: the arp's voice releases
        car_off(arp_note_);
        arp_note_ = 0xFFu;
      }
      tune_.note_off(e.note);
      break;
    case EventKind::Param:
      if (e.param == CarSrc) {
        select_carrier(o3sdk::core::param_clamp(kParams[CarSrc], e.value), e.frame);
      } else if (e.param >= Loop1Rec && e.param < CarSrc) {
        loop_param_event(e.param, e.value, e.frame);
      } else if (e.param >= R1Start && e.param < Load) {
        region_param_event(e.param, e.value, e.frame);
      } else {
        set(e.param, e.value);
      }
      break;
    case EventKind::AllNotesOff:
      midi_fired_ = 0;
      owners_reset();
      carrier_.all_notes_off();
      nheld_ = 0;
      arp_note_ = 0xFFu;
      arp_pos_ = 0xFFu;
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
      if (v >= 1.5f) {  // CLEAR: empty at once, whatever it was doing; a Rec or Play after this starts from nothing
        s.want_rec = false;
        s.want_play = false;
        s.rec = Slot::Rec::Idle;
        loops_.forget(n);
        break;
      }
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

// A region parameter (ids 49..60). Start and Len are staged in the Loop; Trig 1 starts the reader's region at `frame` unless its slot is
// armed or recording (the recording has priority), Trig 0 releases it.
// Load = slot x kMaxSamples + length, length 1..kMaxSamples, so slot 3 with a full 16 s is 2,822,400 and every value has one reading
// (v > 0: slot = (v - 1) / kMaxSamples). The app wrote that sample into the slot's memory from the main loop while the slot was stopped.
// Like the other action-like parameters (a Rec off, a Trig 0) its default, 0, is a no-op, which is all prepare() and Engine::start() ever
// replay (the engine keeps no restart); a stored value is only ever the one the app just sent.
void Vocoder::load_event(float v) noexcept {
  if (!(v >= 1.0f) || !loops_.bound()) {
    return;
  }
  const auto w = static_cast<std::uint32_t>(v);  // exact: v is a whole number below 2^24, and clamped
  const std::uint32_t n = (w - 1u) / Loop::kMaxSamples;
  const std::uint32_t len = w - n * Loop::kMaxSamples;
  if (n >= Loop::kCount || slot_[n].rec != Slot::Rec::Idle || slot_[n].want_play) {
    return;
  }
  const std::uint32_t old_len = loops_.length(n);
  const std::uint32_t old_bars = loops_.bars(n);
  if (!loops_.adopt(n, len)) {
    return;
  }
  // Whole bars, in this order, so that an edit, a copy or a trim does not forget what the loop was and the outcome never hangs on the tempo of
  // the moment unless the loop is new:
  //  1. the slot's length did not change (a resent Load, an edit that kept the length): the count it had;
  //  2. another slot holds exactly this length with a count (a copy: the app copies slot to slot): the same count, whatever the tempo is now;
  //  3. the slot had a count and the new length is a whole number of its own bars (a trim to bars) within two samples a bar: that many bars;
  //  4. else a WAV is on the bars if its length is a whole number of bars at the transport's tempo as of the last block (within two samples a
  //     bar's worth of tempo rounding and a writer's fade padding: the tolerance is 2 * mbpm in the units of len * mbpm), else it is free.
  //     len / bar = len * mbpm / (240000 * fs); fs rounded to a whole number. A project's WAVs are judged here, at the project's own tempo:
  //     FileScreen::apply sets it before the first WAV load.
  if (old_len == len && old_bars != 0u) {
    loops_.set_bars(n, old_bars);
    return;
  }
  for (unsigned k = 0; k < Loop::kCount; ++k) {
    if (k != n && loops_.length(k) == len && loops_.bars(k) != 0u) {
      loops_.set_bars(n, loops_.bars(k));
      return;
    }
  }
  if (old_bars != 0u && old_len != 0u) {
    const std::uint64_t lb = static_cast<std::uint64_t>(len) * old_bars;
    const std::uint64_t nb = (2u * lb + old_len) / (2u * old_len);
    const std::uint64_t exact = nb * old_len;
    const std::uint64_t diff = lb > exact ? lb - exact : exact - lb;
    if (nb >= 1u && nb < 0xFFFFu && diff <= 2ull * old_bars) {
      loops_.set_bars(n, static_cast<std::uint32_t>(nb));
      return;
    }
  }
  const std::uint64_t num = 240000ull * static_cast<std::uint64_t>(static_cast<std::uint32_t>(fs_ + 0.5f));
  const std::uint64_t lm = static_cast<std::uint64_t>(len) * tempo_mbpm_;
  const std::uint64_t bars = (2u * lm + num) / (2u * num);
  const std::uint64_t exact = bars * num;
  const std::uint64_t diff = lm > exact ? lm - exact : exact - lm;
  if (bars >= 1u && bars < 0xFFFFu && diff <= 2ull * tempo_mbpm_) {
    loops_.set_bars(n, static_cast<std::uint32_t>(bars));
  }
}

void Vocoder::region_param_event(std::uint16_t id, float v, std::uint32_t frame) noexcept {
  v = o3sdk::core::param_clamp(kParams[id], v);
  const unsigned k = id - R1Start;
  const unsigned n = k / 3u;
  if (!loops_.bound() || n >= Loop::kCount) {
    return;
  }
  switch (static_cast<RegionField>(k % 3u)) {
    case RegionField::Start:
      loops_.set_region_start(n, static_cast<std::uint32_t>(v + 0.5f));
      break;
    case RegionField::Len:
      loops_.set_region_len(n, static_cast<std::uint32_t>(v + 0.5f));
      break;
    case RegionField::Trig:
      if (v < 0.5f) {
        loops_.release_region(n, frame);
      } else if (slot_[n].rec == Slot::Rec::Idle) {
        (void)loops_.trig_region(n, frame);
      }
      break;
  }
}

// The carrier source changes at `frame`: the synth's gain and the noise amount start their ramps now (the run before this event has been
// rendered), the loops' at the same frame through pend_.
void Vocoder::select_carrier(float v, std::uint32_t frame) noexcept {
  const bool was_voice = sel_ == kCarVoice;
  sel_ = static_cast<unsigned>(v + 0.5f);
  if ((sel_ == kCarVoice) != was_voice) voice_switch(sel_ == kCarVoice);
  synth_gain_.target(synth_target());
  live_car_.target(sel_ == 7u ? 1.0f : 0.0f);
  noise_amt_.target(noise_target());
  retarget(frame);
}

// Entering VOICE the pads' notes in the carrier let go (they were played as such); leaving it the sung notes let go and the pads sound again.
void Vocoder::voice_switch(bool on) noexcept {
  tune_.set_voice(on);
  if (on) {
    carrier_.all_notes_off();
    vn_ = 0;
    return;
  }
  for (unsigned i = 0; i < vn_; ++i) carrier_.note_off(vsnd_[i]);
  vn_ = 0;
  if (arp_ == 0u) {
    for (unsigned i = 0; i < nheld_; ++i) carrier_.note_on(held_[i]);
  } else if (arp_note_ != 0xFFu) {
    carrier_.note_on(arp_note_);
  }
}

// VOICE, called before each render of the carrier: the notes it should sound are the sung note alone with no pad held; else each sounding pad
// (the held ones, or the arp's one) moved by sung - lowest held; none when nothing is sung. Only the difference with what it was given goes
// to the carrier, so a steady note costs a compare and a change is a note_off and a note_on (GLIDE and the envelopes do the rest). Once a
// render call, no libm, no division.
void Vocoder::voice_sync() noexcept {
  std::uint8_t want[Carrier::kVoices];
  unsigned nw = 0;
  const int base = tune_.voice_note();
  if (base >= 0 && base <= 127) {
    if (nheld_ == 0u) {
      want[nw++] = static_cast<std::uint8_t>(base);
    } else {
      const int lo = held_[0];
      const auto add = [&](std::uint8_t pad) {
        const int m = base + static_cast<int>(pad) - lo;
        if (m >= 0 && m <= 127 && nw < Carrier::kVoices) want[nw++] = static_cast<std::uint8_t>(m);
      };
      if (arp_ == 0u) {
        for (unsigned i = 0; i < nheld_; ++i) add(held_[i]);
      } else if (arp_note_ != 0xFFu) {
        add(arp_note_);
      }
    }
  }
  if (nw == vn_) {
    unsigned i = 0;
    while (i < nw && want[i] == vsnd_[i]) ++i;
    if (i == nw) return;
  }
  for (unsigned i = 0; i < vn_; ++i) {
    bool keep = false;
    for (unsigned j = 0; j < nw; ++j) keep = keep || want[j] == vsnd_[i];
    if (!keep) carrier_.note_off(vsnd_[i]);
  }
  for (unsigned j = 0; j < nw; ++j) {
    bool had = false;
    for (unsigned i = 0; i < vn_; ++i) had = had || vsnd_[i] == want[j];
    if (!had) carrier_.note_on(want[j]);
  }
  for (unsigned j = 0; j < nw; ++j) vsnd_[j] = want[j];
  vn_ = static_cast<std::uint8_t>(nw);
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
// Advances the Bend ramp over `n` samples and gives the carrier the ratio at their end: once a run, a multiply-free loop while it moves and one exp2
// then, nothing at rest (the ratio stays 1.0f or the last one).
void Vocoder::step_bend(std::uint32_t n) noexcept {
  if (!bend_.moving() && !vib_.moving() && vib_.value() == 0.0f && !vib_was_) {
    return;
  }
  for (std::uint32_t i = 0; bend_.moving() && i < n; ++i) {
    (void)bend_.next();
  }
  for (std::uint32_t i = 0; vib_.moving() && i < n; ++i) {
    (void)vib_.next();
  }
  float st = bend_.value();
  const float depth = vib_.value();
  if (depth > 0.0f) {
    vib_phase_ += vib_inc_ * static_cast<float>(n);
    vib_phase_ -= std::floor(vib_phase_);
    st += depth * kVibPeakSt * std::sin(vib_phase_ * 6.2831853f);
  } else {
    vib_phase_ = 0.0f;
  }
  vib_was_ = depth > 0.0f;
  carrier_.set_bend_ratio(st == 0.0f ? 1.0f : std::exp2(st * (1.0f / 12.0f)));
}

// Once a run: the carrier's glide coefficient for `n` samples (an exp only when the run length or GLIDE changed), then one slew step.
void Vocoder::step_glide(std::uint32_t n) noexcept {
  if (glide_ms_ <= 0.0f) {
    carrier_.set_glide(0.0f);
    return;
  }
  if (n != glide_n_) {
    glide_n_ = n;
    glide_coef_ = 1.0f - std::exp(-4000.0f * static_cast<float>(n) / (glide_ms_ * fs_));  // four time constants over glide_ms_
  }
  carrier_.set_glide(glide_coef_);
  carrier_.step_glide();
}

// The held notes, ascending by pitch (the arp's UP is the list's order). A full list ignores a ninth note: the carrier steals for it as ever.
void Vocoder::held_insert(std::uint8_t note) noexcept {
  unsigned i = 0;
  while (i < nheld_ && held_[i] < note) {
    ++i;
  }
  if ((i < nheld_ && held_[i] == note) || nheld_ >= Carrier::kVoices) {
    return;
  }
  for (unsigned j = nheld_; j > i; --j) {
    held_[j] = held_[j - 1u];
  }
  held_[i] = note;
  ++nheld_;
  if (arp_pos_ != 0xFFu && i <= arp_pos_) {
    ++arp_pos_;  // a note entered at or below the sounding one: the index follows its note, the run neither repeats nor skips
  }
}

void Vocoder::held_remove(std::uint8_t note) noexcept {
  for (unsigned i = 0; i < nheld_; ++i) {
    if (held_[i] == note) {
      for (unsigned j = i; j + 1u < nheld_; ++j) {
        held_[j] = held_[j + 1u];
      }
      --nheld_;
      if (nheld_ == 0u) {
        arp_pos_ = 0xFFu;  // the next chord starts from its bottom
        arp_down_ = false;
      } else if (arp_pos_ != 0xFFu) {
        if (i < arp_pos_) {
          --arp_pos_;  // a note below the sounding one left: the index follows its note
        } else if (i == arp_pos_) {
          // The sounding note itself left. Going down, the next step takes the one below (index - 1) as ever. Going up, the note that took its
          // place is the next of the run: step the index back one (0xFF = before the bottom) so the step lands on it.
          const bool down = arp_ == 2u || (arp_ == 3u && arp_down_);
          if (!down || (arp_ == 3u && arp_pos_ == 0u)) {
            arp_pos_ = arp_pos_ == 0u ? 0xFFu : static_cast<std::uint8_t>(arp_pos_ - 1u);
          }
        }
      }
      return;
    }
  }
}

// A step begins now: the first half (gate open) runs for half_len_ samples.
void Vocoder::clock_start_now() noexcept {
  second_half_ = false;
  clock_cnt_ = half_len_;
  if (arp_ != 0u) {
    arp_step();
  }
}

// The end of a half step; the end of the second one is the step edge.
void Vocoder::clock_edge() noexcept {
  second_half_ = !second_half_;
  clock_cnt_ += half_len_;
  if (!second_half_ && arp_ != 0u) {
    arp_step();
  }
}

void Vocoder::set_arp(unsigned mode) noexcept {
  const unsigned was = arp_;
  if (mode == was) {
    return;
  }
  if (mode == 0u) {  // off: what the arp had on goes, every held note sounds again
    arp_ = 0;
    if (arp_note_ != 0xFFu) {
      car_off(arp_note_);
      arp_note_ = 0xFFu;
    }
    for (unsigned i = 0; i < nheld_; ++i) {
      car_on(held_[i]);
    }
    arp_pos_ = 0xFFu;
    arp_down_ = false;
    clock_on_ = gate_ != 0u;
    return;
  }
  arp_ = static_cast<std::uint8_t>(mode);
  if (was == 0u) {  // on: the held notes were sounding all together; the arp takes them one at a time from the bottom
    for (unsigned i = 0; i < nheld_; ++i) {
      car_off(held_[i]);
    }
    arp_pos_ = 0xFFu;
    arp_down_ = false;
    const bool clock_was_on = clock_on_;
    clock_on_ = true;
    if (!clock_was_on) {
      clock_start_now();  // a step begins here and sounds the first note
    } else {
      arp_step();
    }
  }
}

// One step of the arp: release the note it had on, sound the next of the pattern; with nothing held it only releases.
void Vocoder::arp_step() noexcept {
  if (arp_note_ != 0xFFu) {
    car_off(arp_note_);
    arp_note_ = 0xFFu;
  }
  const unsigned n = nheld_;
  if (n == 0u) {
    arp_pos_ = 0xFFu;
    return;
  }
  unsigned pos = arp_pos_;
  unsigned next = 0;
  switch (arp_) {
    case 1:  // UP
      next = (pos == 0xFFu || pos + 1u >= n) ? 0u : pos + 1u;
      break;
    case 2:  // DOWN
      next = (pos == 0xFFu || pos == 0u || pos > n) ? n - 1u : pos - 1u;
      break;
    case 3:  // UP-DOWN, the ends sounded once
      if (pos == 0xFFu) {
        next = 0;
        arp_down_ = false;
      } else {
        pos = pos < n ? pos : n - 1u;
        if (!arp_down_) {
          if (pos + 1u < n) {
            next = pos + 1u;
          } else {
            arp_down_ = n > 1u;
            next = n > 1u ? pos - 1u : 0u;
          }
        } else if (pos > 0u) {
          next = pos - 1u;
        } else {
          arp_down_ = false;
          next = n > 1u ? 1u : 0u;
        }
      }
      break;
    default:  // RANDOM, xorshift32; the top 16 bits times n, shifted: no division
      rng_ ^= rng_ << 13;
      rng_ ^= rng_ >> 17;
      rng_ ^= rng_ << 5;
      next = ((rng_ >> 16) * n) >> 16;
      break;
  }
  arp_pos_ = static_cast<std::uint8_t>(next);
  arp_note_ = held_[next];
  car_on(arp_note_);
}

// GATE on frames [from, from + k) of the carrier and its held-notes gate: the gain slews to 0 in the second half of a step and back to 1, 2 ms edges.
// While GATE is on (or its gain is still returning to 1) the gain of each frame is also written to `gain` (the whole array is set to 1 the first
// time in a block, `recorded` marks it), so process() can cut the loops that play as the carrier by the same envelope.
void Vocoder::apply_gate(float* car, float* gate, std::uint32_t from, std::uint32_t k, float* gain, bool& recorded) noexcept {
  const bool rec = recorded || gate_ != 0u || gate_g_ != 1.0f;
  if (rec && !recorded) {
    for (std::uint32_t i = 0; i < Bands::kMaxFrames; ++i) {
      gain[i] = 1.0f;
    }
    recorded = true;
  }
  const float tgt = (clock_on_ && gate_ != 0u && second_half_) ? 0.0f : 1.0f;
  float g = gate_g_;
  const float st = gate_step_;
  for (std::uint32_t i = from; i < from + k; ++i) {
    if (g < tgt) {
      g += st;
      g = g > tgt ? tgt : g;
    } else if (g > tgt) {
      g -= st;
      g = g < tgt ? tgt : g;
    }
    car[i] *= g;
    gate[i] *= g;
    if (rec) {
      gain[i] = g;
    }
  }
  gate_g_ = g;
}

void Vocoder::render_carrier(float* out, float* gate, std::uint32_t n) noexcept {
  if (sel_ == kCarVoice) {
    voice_sync();
  }
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
    loops_.play(loop, car + from, n - from, true);
    return;
  }
  loops_.play(loop, tmp + from, n - from, false);
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
    const std::uint32_t w = rec | (loops_.playing(i) ? 4u : 0u) | (s.want_play ? 8u : 0u) | (s.carrier ? 16u : 0u) | (loops_.region_sounding(i) ? 32u : 0u) | (loops_.bars(i) != 0u ? 64u : 0u) | (loops_.length(i) << 8);
    pos_[i].store(loops_.bank_position(i), std::memory_order_relaxed);
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
  st.region = (w & 32u) != 0u;
  st.on_bar = (w & 64u) != 0u;
  st.length = w >> 8;
  st.position = pos_[loop].load(std::memory_order_relaxed);
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
  const auto drop_live = [&] {  // a speaking slot or NONE: the live input leaves the modulator sum
    if (msel_ != 0u) {
      for (std::uint32_t k = 0; k < n; ++k) mod[k] = 0.0f;
    }
  };
  if (!loops_.bound()) {
    drop_live();
    npend_ = 0;
    return;
  }
  tempo_mbpm_ = t.tempo_mbpm;
  float tmp[Bands::kMaxFrames];
  std::uint32_t play_from[Loop::kCount] = {};
  // The block's bar line (frame and bar index), found once and only when something asks: -1 none. A block holds at most one.
  bool line_known = false;
  int line_frame = -1;
  std::uint32_t line_bar = 0;
  const auto line = [&]() -> int {
    if (!line_known) {
      line_known = true;
      line_frame = bar_frame(t, n, line_bar);
    }
    return line_frame;
  };
  // Where a loop action asked at frame `min` of this block happens: at the next bar line, or at once while the transport is stopped.
  const auto locate = [&](std::uint32_t min) -> int {
    if (!t.playing) {
      return static_cast<int>(min);
    }
    const int f = line();
    return f >= static_cast<int>(min) ? f : -1;
  };
  // The bar-0 line (the transport started again) anchors every slot's bar grid at 0, whatever the slot is doing (a loop under a region, one that
  // is paused or free keeps no stale anchor). Only the first bar of a run looks: later blocks cost nothing here.
  if (t.playing && (t.pos >> 32) < kBarTicks && line() >= 0 && line_bar == 0u) {
    for (unsigned i = 0; i < Loop::kCount; ++i) {
      loops_.reanchor(i);
    }
  }
  for (unsigned i = 0; i < Loop::kCount; ++i) {
    Slot& s = slot_[i];
    std::uint32_t from = 0;
    bool began = false;
    if (s.rec == Slot::Rec::Armed) {
      if (!s.want_rec) {
        s.rec = Slot::Rec::Idle;
      } else {
        const int f = locate(s.req);
        if (f >= 0) {
          loops_.begin_record(i);
          s.rec = Slot::Rec::Recording;
          s.want_play = false;
          s.rec_ok = t.playing;
          s.rec_bars = 0;
          from = static_cast<std::uint32_t>(f);
          began = true;
        }
      }
    }
    if (s.rec == Slot::Rec::Recording) {
      if (!began) {  // a take is a count of whole bars only if the transport ran under all of it
        if (!t.playing) {
          s.rec_ok = false;
        } else if (s.rec_ok && line() >= 0) {
          ++s.rec_bars;
        }
      }
      std::uint32_t end = n;
      bool stop = false;
      if (!s.want_rec) {
        const int f = locate(s.req);
        if (f >= 0) {
          end = static_cast<std::uint32_t>(f);
          stop = true;
        }
      }
      const std::uint32_t want = end > from ? end - from : 0u;
      const std::uint32_t taken = want != 0u ? loops_.record(i, mod + from, want) : 0u;
      if (!loops_.recording(i)) {  // full: the loop ended itself; it is on the bars only if the line that stops it is the sample that filled it
        s.rec = Slot::Rec::Idle;
        s.want_rec = false;
        if (stop && taken == want && s.rec_ok && s.rec_bars != 0u) {
          loops_.set_bars(i, s.rec_bars);
        }
      } else if (stop) {
        loops_.end_record(i);
        s.rec = Slot::Rec::Idle;
        if (s.rec_ok && s.rec_bars != 0u) {
          loops_.set_bars(i, s.rec_bars);
        }
      }
    }
  }
  drop_live();  // after the recordings: a take is the live input as heard
  bool modulated = false;
  for (unsigned i = 0; i < Loop::kCount; ++i) {
    Slot& s = slot_[i];
    if (s.want_play && s.rec == Slot::Rec::Idle) {
      const int f = locate(s.req);
      if (f >= 0 && loops_.start(i, t.playing ? line_bar : 0u)) {
        s.want_play = false;
        play_from[i] = static_cast<std::uint32_t>(f);
      }
    }
    // sel_ is the block's last word: a loop leaving the carrier stays on that side until its fade has run from the event's frame.
    const bool on_car = sel_ == 3u + i || (msel_ != 3u + i && (s.carrier || loop_car_[i].value() > 0.0f));
    const bool sounds = loops_.sounding(i) || loops_.region(i);
    if (sounds) {
      const std::uint32_t pf = loops_.region(i) ? 0u : play_from[i];  // a region's command frames count from the block's start
      if (t.playing && loops_.bars(i) != 0u && loops_.playing(i) && !loops_.region(i)) {  // a loop of whole bars restarts with the bar
        const int lf = line();
        if (lf >= static_cast<int>(pf)) {
          loops_.bar_line(i, static_cast<std::uint32_t>(lf) - pf, line_bar);
        }
      }
      if (on_car) {
        mix_car(i, pf, n, car, tmp);
      } else {
        loops_.play(i, mod + pf, n - pf, modulated);
        modulated = true;
      }
    }
    if (!sounds || !on_car) {
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

namespace {
// A reflection coefficient as Q15; NaN and anything at or past +-0.9995 (the engine clamps at 0.995) publish as 0.
std::int16_t q15(float k) noexcept { return (k > -0.9995f && k < 0.9995f) ? static_cast<std::int16_t>(k * 32767.0f) : std::int16_t{0}; }
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
  // TALK's lattice and TUNE's correction, copied (see View).
  const std::uint32_t order = talk_.lattice_order();
  for (unsigned w = 0; w < 9; ++w) {
    const unsigned m = w * 2u + 1u;
    const std::uint32_t lo = static_cast<std::uint16_t>(m <= order ? q15(talk_.reflection(m)) : std::int16_t{0});
    const std::uint32_t hi = static_cast<std::uint16_t>(m + 1u <= order ? q15(talk_.reflection(m + 1u)) : std::int16_t{0});
    lpc_[w].store(lo | (hi << 16), std::memory_order_relaxed);
  }
  const float sc = tune_.shift_cents();
  const std::int32_t cents = (sc >= -1200.0f && sc <= 1200.0f) ? static_cast<std::int32_t>(sc) : (sc > 1200.0f ? 1200 : (sc < -1200.0f ? -1200 : 0));
  const int note = tune_.target_note();
  const std::uint32_t tn = static_cast<std::uint32_t>(note < 0 ? 0 : (note > 127 ? 128 : note + 1));
  lpc_meta_.store((order & 31u) | (talk_.freeze() ? 32u : 0u) | (tune_.voiced() ? 64u : 0u) | (tn << 8) |
                      (static_cast<std::uint32_t>(static_cast<std::uint16_t>(cents)) << 16),
                  std::memory_order_relaxed);
}

Vocoder::View Vocoder::view() const noexcept {
  View v;
  for (unsigned w = 0; w < 4; ++w) {
    const std::uint32_t word = view_[w].load(std::memory_order_relaxed);
    for (unsigned k = 0; k < 4; ++k) v.band[w * 4u + k] = static_cast<std::uint8_t>(word >> (8u * k));
  }
  for (unsigned w = 0; w < 9; ++w) {
    const std::uint32_t word = lpc_[w].load(std::memory_order_relaxed);
    v.refl[w * 2u] = static_cast<std::int16_t>(static_cast<std::uint16_t>(word));
    v.refl[w * 2u + 1u] = static_cast<std::int16_t>(static_cast<std::uint16_t>(word >> 16));
  }
  const std::uint32_t meta = lpc_meta_.load(std::memory_order_relaxed);
  v.order = static_cast<std::uint8_t>(meta & 31u);
  v.freeze = (meta & 32u) != 0u;
  v.voiced = (meta & 64u) != 0u;
  v.target_note = static_cast<std::int8_t>(static_cast<int>((meta >> 8) & 255u) - 1);
  v.shift_cents = static_cast<std::int16_t>(static_cast<std::uint16_t>(meta >> 16));
  return v;
}

void Vocoder::process(const o3sdk::dsp::ProcessArgs& args) noexcept {
  const std::uint32_t n = args.frames < Bands::kMaxFrames ? args.frames : Bands::kMaxFrames;
  float mod[Bands::kMaxFrames];
  float car[Bands::kMaxFrames];
  float wet[Bands::kMaxFrames];
  float gate[Bands::kMaxFrames];
  float gain[Bands::kMaxFrames];  // GATE's gain per frame, set only when a gate ran (gated)
  bool gated = false;
  playing_ = args.transport->playing;
  grid_playing_ = args.transport->playing;  // the notes-in stamp (apply() reads it for this block's events): where this block ends
  grid_ticks_ = args.transport->playing ? args.transport->end() >> 32 : 0u;
  for (std::uint32_t i = 0; i < n; ++i) {
    mod[i] = args.in_l != nullptr ? Bands::clean(args.in_l[i]) : 0.0f;
  }
  {  // the step clock: half a step in samples at this block's tempo (one division a block); the grid goes back to the bar when the transport starts
    const float mbpm = static_cast<float>(args.transport->tempo_mbpm < 20000u ? 20000u : args.transport->tempo_mbpm);
    const unsigned rate = gate_ != 0u ? gate_ : 3u;
    half_len_ = fs_ * 60000.0f / mbpm * kHalfBeats[rate - 1u];
    half_len_ = half_len_ < 4.0f ? 4.0f : half_len_;
    clock_cnt_ = clock_cnt_ > half_len_ ? half_len_ : clock_cnt_;
    if (args.transport->started && clock_on_) {
      second_half_ = true;
      clock_cnt_ = 0.0f;  // the first edge is frame 0: a new step, the pattern from its start
      arp_pos_ = 0xFFu;
      arp_down_ = false;
    }
  }
  o3sdk::dsp::ProcessArgs run = args;
  run.frames = n;
  o3sdk::dsp::for_each_run(
      run, [&](std::uint32_t from, std::uint32_t to) {
        step_bend(to - from);
        step_glide(to - from);
        const bool tune_only = tune_mix_.value() >= 1.0f && tune_mix_.target_value() >= 1.0f;  // TUNE alone: the carrier is not heard
        if (!clock_on_ && gate_g_ == 1.0f) {  // no rhythm: one render, the path of before
          if (tune_only) {
            for (std::uint32_t i = from; i < to; ++i) car[i] = gate[i] = 0.0f;
          } else {
            render_carrier(car + from, gate + from, to - from);
          }
          return;
        }
        while (from < to) {  // split at the clock's edges so a step lands on its frame
          std::uint32_t k = to - from;
          if (clock_on_) {
            while (clock_cnt_ <= 0.0f) {
              clock_edge();
            }
            std::uint32_t c = static_cast<std::uint32_t>(clock_cnt_);
            c += static_cast<float>(c) < clock_cnt_ ? 1u : 0u;
            k = c < k ? c : k;
          }
          if (tune_only) {
            for (std::uint32_t i = from; i < from + k; ++i) car[i] = gate[i] = 0.0f;
          } else {
            render_carrier(car + from, gate + from, k);
          }
          apply_gate(car, gate, from, k, gain, gated);
          if (clock_on_) {
            clock_cnt_ -= static_cast<float>(k);
          }
          from += k;
        }
      },
      [&](const o3sdk::dsp::Event& e) { apply(e); });
  if (live_car_.value() > 0.0f || live_car_.target_value() > 0.0f) {  // CarSrc LIVE: the live input, before run_loops takes it out of mod
    const float* right = live_r_ ? args.in_r : nullptr;  // USB: SOUNDS takes the right channel, SPEAKS stays on the left
    for (std::uint32_t i = 0; i < n; ++i) {
      const float x = right != nullptr ? Bands::clean(right[i]) : mod[i];
      car[i] += live_car_.next() * x * (gated ? gain[i] : 1.0f);  // GATE cuts it like any carrier
    }
  }
  if (gated) {  // GATE cuts the loops that play as the carrier by the same envelope as the synth
    float lcar[Bands::kMaxFrames];
    for (std::uint32_t i = 0; i < n; ++i) lcar[i] = 0.0f;
    run_loops(*args.transport, n, mod, lcar);
    for (std::uint32_t i = 0; i < n; ++i) car[i] += lcar[i] * gain[i];
  } else {
    run_loops(*args.transport, n, mod, car);
  }
  // Only the active mode runs; two or three run while a crossfade is under way.
  const auto runs = [](const Ramp& r) { return r.value() > 0.0f || r.target_value() > 0.0f; };
  float tw[Bands::kMaxFrames];
  float tn[Bands::kMaxFrames];
  const bool robot = runs(robot_mix_);
  if (runs(bands_mix_) || robot) {  // ROBOT is BANDS through comp_: one engine runs for both ramps
    bands_.process(mod, car, wet, n, gate);
  } else {
    for (std::uint32_t i = 0; i < n; ++i) wet[i] = 0.0f;
  }
  if (runs(talk_mix_)) {
    talk_.process(mod, car, tw, n);
    talk_comp_.process(tw, n);
  } else {
    for (std::uint32_t i = 0; i < n; ++i) tw[i] = 0.0f;
  }
  if (runs(tune_mix_)) {
    tune_.process(mod, tn, n);  // with VOICE on it decides the sung note too: one detector for both
  } else {
    for (std::uint32_t i = 0; i < n; ++i) tn[i] = 0.0f;
    if (sel_ == kCarVoice) {
      tune_.detect(mod, n);  // VOICE outside TUNE: the detector alone
    }
  }
  publish_view();  // the BANDS followers for the PERFORM screen, once a block
  float rw[Bands::kMaxFrames];  // ROBOT: the wet sum after the compressor (a copy: the BANDS term reads the wet sum itself while they crossfade)
  if (robot) {
    for (std::uint32_t i = 0; i < n; ++i) rw[i] = wet[i];
    comp_.process(rw, n);
  }
  if (runs(carrier_mix_)) {
    // TRACT, alone or crossfading with another mode: carrier x LEVEL, the carrier bus after the CarSrc switch (the synth, the noise, a
    // loop, the live input or the voice: whatever CarSrc picked) and after GATE, ARP and the rest of the carrier side. The app's TractUnit
    // (an insert after this unit) is the tube; here the carrier only has to reach the output, through the same LEVEL and soft clip.
    // The other modes' terms stay in the sum for the crossfade (zero arrays while they do not run). Modes 0..3 never come here, so their
    // samples are what they were before TRACT existed.
    const bool others_off = !runs(bands_mix_) && !runs(talk_mix_) && !runs(tune_mix_) && !robot;
    if (others_off && !level_.moving() && !dry_.moving() && !carrier_mix_.moving()) {
      const float g = level_.value();
      const float d = dry_.value();
      const float cm = kCarrierGain * carrier_mix_.value();
      for (std::uint32_t i = 0; i < n; ++i) {
        const float y = soft_clip(g * (car[i] * cm + d * mod[i]));
        args.out_l[i] = y;
        args.out_r[i] = y;
      }
      return;
    }
    if (!robot) {
      for (std::uint32_t i = 0; i < n; ++i) rw[i] = 0.0f;
    }
    for (std::uint32_t i = 0; i < n; ++i) {
      const float g = level_.next();
      const float d = dry_.next();
      const float wm = bands_mix_.next();
      const float tm = talk_mix_.next();
      const float um = tune_mix_.next();
      const float rm = robot_mix_.next();
      const float cm = carrier_mix_.next();
      const float y = soft_clip(g * ((wet[i] * kWetGain) * wm + (rw[i] * kWetGain) * rm + (tw[i] * kTalkGain) * tm + (tn[i] * kTuneGain) * um +
                                     (car[i] * kCarrierGain) * cm + d * mod[i]));
      args.out_l[i] = y;
      args.out_r[i] = y;
    }
    return;
  }
  if (!level_.moving() && !dry_.moving() && !bands_mix_.moving() && !talk_mix_.moving() && !tune_mix_.moving() && !robot_mix_.moving()) {
    const float g = level_.value();
    const float d = dry_.value();
    const float wm = kWetGain * bands_mix_.value();
    const float tm = kTalkGain * talk_mix_.value();
    const float um = kTuneGain * tune_mix_.value();
    if (robot) {
      const float rm = kWetGain * robot_mix_.value();
      for (std::uint32_t i = 0; i < n; ++i) {
        const float y = soft_clip(g * (wet[i] * wm + rw[i] * rm + tw[i] * tm + tn[i] * um + d * mod[i]));
        args.out_l[i] = y;
        args.out_r[i] = y;
      }
      return;
    }
    for (std::uint32_t i = 0; i < n; ++i) {
      const float y = soft_clip(g * (wet[i] * wm + tw[i] * tm + tn[i] * um + d * mod[i]));
      args.out_l[i] = y;
      args.out_r[i] = y;
    }
    return;
  }
  if (robot) {
    for (std::uint32_t i = 0; i < n; ++i) {
      const float g = level_.next();
      const float d = dry_.next();
      const float wm = bands_mix_.next();
      const float tm = talk_mix_.next();
      const float um = tune_mix_.next();
      const float rm = robot_mix_.next();
      const float y = soft_clip(g * ((wet[i] * kWetGain) * wm + (rw[i] * kWetGain) * rm + (tw[i] * kTalkGain) * tm + (tn[i] * kTuneGain) * um + d * mod[i]));
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
