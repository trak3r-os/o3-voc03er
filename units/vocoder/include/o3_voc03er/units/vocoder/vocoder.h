// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// VOCODER: the Instrument unit. `in_l` is the modulator (`in_r` is unused for now), notes play the Carrier, both go through Bands, the
// result (plus the dry modulator, when asked) leaves mono on both planes through a soft clip whose peak is below 1.0.
//
// Parameters (the id is the index in the table: append, never insert):
//   0 Wave     0 SAW, 1 PULSE
//   1 Noise    0..1: the carrier's oscillators fade to white noise (gated by the held notes)
//   2 Attack   ms, 1..500: the carrier voices' attack
//   3 Release  ms, 5..2000: the carrier voices' release
//   4 Dry      0..1: the dry modulator mixed into the output (default 0)
//   5 Level    dB, -48..+6
//   6 Detune   cents, 0..20 (default 8): the second oscillator of each carrier voice, sharp of the first
//   7 Mode     0 BANDS, 1 TALK, 2 TUNE (default BANDS): the Carrier goes through Bands or through Talk (LPC talkbox); TUNE is the voice
//              itself, pitch-corrected (autotune.h): the carrier, Bands and Talk do not run (the notes still count: they are the
//              target of NOTES-IN). A 10 ms crossfade between any two modes (one ramp per mode), and only the active mode runs,
//              except during the crossfade; a mode that was not running starts from a clean state when it is entered
//   8 Quality  LPC order 8..24 (default 16), Talk only
//   9 Freeze   0 off, 1 on: Talk stops updating its reflection coefficients (a held vowel)
//  10..21    Loops, three for each of the four loops n = 0..3 (ids 10 + 3n + field; Vocoder::loop_param):
//            +0 Rec   0 OFF, 1 ARM, 2 CLEAR (empties the loop at once): records `in_l` into the loop. ARM starts at the next bar line of the transport (sample-exact, 4/4: four
//                     quarters, Transport::at); 0 stops at the next bar line. With the transport stopped both happen at once. A full loop
//                     (16 s) stops by itself. Recording empties the loop. The engine's stored value stays 1 after such a stop: send 0 then 1
//                     to record again (every Param event is delivered).
//            +1 Play  0 OFF, 1 ON: plays the loop at 1x from its first sample, from the next bar line (at once when the transport is
//                     stopped); OFF stops at once. An empty loop does not play.
//            +2 Role  0 MOD, 1 CAR: a playing MOD loop is the modulator in place of the live input (several are summed); a playing CAR loop is
//                     summed with what the internal synth plays. Both feed Bands or Talk as usual; DRY mixes the modulator that is used.
//  22..33    Loop playback, three for each loop n = 0..3 (ids 22 + 3n + field):
//            +0 Speed 0 0.5X, 1 1X (default), 2 2X: the loop plays at that speed; the pitch of a loop in TALK or BANDS comes from the
//                     carrier, so the melody stays. Changes act from the block they land in, the position carries over.
//            +1 Dir   0 FWD (default), 1 REV: plays backwards (a start begins at the last sample).
//            +2 Level dB, -24..+12 (default 0): the loop's gain, ramped over a chunk of 64 samples; at 0 dB the sound is what it was.
//            The loop memory comes from the app (bind_loops); without it the loops do nothing.
//  34        CarSrc  0 FOLLOW (default), 1 SYNTH, 2 NOISE, 3..6 LOOP1..LOOP4: which source is the carrier, set by the app's pattern step by step
//            (a Param event, so it lands on the event's frame). FOLLOW changes nothing: the synth with the NOISE knob as set, and the loops
//            whose role is CAR. SYNTH and NOISE play the synth with noise 0 or 1 and mute the CAR loops; LOOPn mutes the synth and every
//            other CAR loop, and loop n is the carrier even if its role is MOD (its modulator part then stops at the block, not on the
//            frame: set it to CAR first for a clean change). Nothing here touches the live input: it cannot be a carrier (its source
//            switch settles for 200 ms). No click: the synth's gain, the noise amount and each loop's carrier gain move in a 5 ms ramp
//            from the event's frame (the noise amount in slices of 16 samples, since the Carrier takes it once a run).
//            7 LIVE: the live input (in_l, or in_r with LiveCh) is the carrier: the synth and the CAR loops are muted, the same 5 ms ramp
//            (from the start of the block the event lands in). GATE cuts it like any carrier, ARP does not act on it. The ADC source itself is the app's.
//  47        ModSrc  0 LIVE (default, the modulator as before), 1 NONE, 3..6 SLOT 1..4 (CarSrc's values): what speaks. NONE or a slot take the live
//            input out of the modulator sum (no bleed; a recording still takes it); a slot plays as the modulator whatever its role, unless it
//            is also the CarSrc. Acts from the block it lands in. 2 acts as NONE.
//  48        LiveCh  0 L (default), 1 R: the channel CarSrc 7 reads. The app sets R with USB, the one stereo source, so SPEAKS takes USB L and SOUNDS USB R.
//  35..39    TUNE (Autotune; they act in TUNE only, but are stored in every mode):
//            35 Key     C..B (default C); 36 Scale CHROM, MAJOR, MINOR, MAJ-P, MIN-P, BLUES, DORIAN (default CHROM): the notes to snap to
//            37 Speed   ms, 0..500 (default 25): the one-pole of the correction; 0 is the hard, stepped sound
//            38 Amount  0..1 (default 1): how much of the error is corrected (0 passes the voice, delayed 1 ms)
//            39 NotesIn 0 OFF, 1 ON (default): the most recent held note is the target, in the octave nearest the voice; none held: the scale
//            Unvoiced input passes unshifted. Range is the Normal detector range (70 to 919 Hz).
//  40..41    FORMANT acts in BANDS only; THRU acts in BANDS and TALK:
//            40 Formant st, -12..+12 (default 0, integer steps): the spectral envelope moved against the carrier; +12 raises the formants an octave
//                     (0.4 octave a band: st / 4.8 bands, linear between bands). 0 is the exact unshifted path; other values run two passes and
//                     ramp each band's gain across the block.
//            41 Thru  %, 0..100 (default 0). BANDS: the high-passed modulator added to the output, scaled by the followers of bands 13..15
//                     (sibilants come through), next to the noise path; 0 is the output without it, bit for bit. TALK: the strength of the breath
//                     (the modulator above 5 kHz, only while the carrier sounds): 0.5x at 0, 1x at 100 % (Talk::set_thru).
//  42        CARRIER:
//            42 Bend    st, -12..+12 (default 0): the SYNTH carrier's pitch, every oscillator's increment times 2^(Bend / 12), once a block through
//                     a 20 ms dsp::Ramp (a stepped input glides). Only SYNTH moves: a live source has no pitch and a slot reads at fixed speeds.
//                     0 is the exact unbent path.
//            43 Vib     0..1 (default 0): vibrato on the SYNTH carrier, a ~5.5 Hz sine LFO stepped once a block (one sin a block while on),
//                     peak +-0.5 st at 1, added to Bend before the increments. 0 is the exact path of Bend alone and resets the phase.
//  44..46    RHYTHM (the carrier's time and pitch; all three at 0 are bit for bit the sound without them):
//            44 Glide   ms, 0..500 (default 0): each carrier voice's oscillator increment slews exponentially toward its note (a time constant of
//                     ms / 4: the pitch is within 2 % of the step after Glide ms), once a run; a new voice starts from the newest sounding one's pitch. 0 jumps.
//            45 Gate    0 OFF, 1 1/4, 2 1/8, 3 1/16, 4 1/32: the carrier is cut on a 50 % duty clock (on the first half of a step), edges of 2 ms,
//                     one multiply a sample for the carrier and its held-notes gate while on. CAR loops are not gated.
//            46 Arp     0 OFF, 1 UP, 2 DOWN, 3 UP-DOWN, 4 RANDOM: the held notes (up to Carrier::kVoices, kept in pitch order here, whatever the
//                     source: pads, MIDI, console) are sounded one at a time on the step clock, GATE's rate or 1/16 with GATE off. With none held the
//                     voice releases. Off with notes held sounds them all again. The clock comes from the transport's tempo, runs with the transport
//                     stopped (its own grid), restarts at the bar when the transport starts, and splits a run at each half step.
// The bands' followers are fixed: 1 ms attack and 15 ms release in the middle of the range, graded geometrically (Bands::set_envelope).
//
// Cost [est]: Bands 58,400 a 64-frame block (front end and unvoiced path included) + Carrier up to 30,720 + mixing 1,300: 90,400 a
// block, 35 % of the 261,224 a block has at 44.1 kHz. TALK replaces Bands: Talk about 16,600 at order 16 (talk.h) + the same Carrier
// and mixing. TUNE: Tune at most 5,500 difference terms a block (tune.h), about 25,000 cycles [est] + the shifter, about 25 cycles a sample (about 1,600 a block, 2 reads in a crossfade) +
// a control step per 16 samples; no carrier. [est] Not measured on a unit.

#include <o3_voc03er/units/vocoder/autotune.h>
#include <o3_voc03er/units/vocoder/bands.h>
#include <o3_voc03er/units/vocoder/carrier.h>
#include <o3_voc03er/units/vocoder/loop.h>
#include <o3_voc03er/units/vocoder/talk.h>

#include <o3sdk/dsp/smooth.h>
#include <o3sdk/dsp/unit.h>

#include <atomic>
#include <cstdint>

namespace o3_voc03er {

// The slices of the SPEAKS slot (U6c, M2): `start` already counts from the start of the bank (what the Start parameter takes), `len` in samples.
// `source` = reader | count << 8, 0 for none: one word, so a reader never meets another's count. The main loop is the single writer (StepSeq::
// slices_open stores source 0 with release, rewrites the words, slices_close stores source with release); the step sequencer's hook and the unit
// load it with acquire. Plain words (no std::atomic) so that `SampleData{}` keeps working. Lives in the SDRAM (O3SDK_BIG).
inline constexpr unsigned kSliceMax = 24;
struct SliceMap {
  std::uint32_t start[kSliceMax];
  std::uint32_t len[kSliceMax];
  std::uint32_t source;
};

class Vocoder final : public o3sdk::dsp::Unit {
 public:
  enum Param : std::uint16_t { Wave, Noise, Attack, Release, Dry, Level, Detune, Mode, Quality, Freeze,
                            Loop1Rec, Loop1Play, Loop1Role, Loop2Rec, Loop2Play, Loop2Role, Loop3Rec, Loop3Play, Loop3Role, Loop4Rec, Loop4Play, Loop4Role,
                            Loop1Speed, Loop1Dir, Loop1Level, Loop2Speed, Loop2Dir, Loop2Level, Loop3Speed, Loop3Dir, Loop3Level, Loop4Speed, Loop4Dir, Loop4Level,
                            CarSrc, Key, Scale, Speed, Amount, NotesIn, Formant, Thru, Bend, Vib, Glide, Gate, Arp, ModSrc, LiveCh,
                            R1Start, R1Len, R1Trig, R2Start, R2Len, R2Trig, R3Start, R3Len, R3Trig, R4Start, R4Len, R4Trig, kParamCount };
  enum class LoopField : std::uint16_t { Rec, Play, Role, Speed, Dir, Level };
  static constexpr std::uint16_t loop_param(unsigned loop, LoopField f) noexcept {
    const unsigned k = static_cast<unsigned>(f);
    return static_cast<std::uint16_t>(k < 3u ? Loop1Rec + 3u * loop + k : Loop1Speed + 3u * loop + (k - 3u));
  }

  // Region play (U5a): ids 49..60, three for each reader n = 0..3 (ids 49 + 3n + field). Start counts samples from the start of the whole bank
  // (0 .. Loop::kBankSamples - 1), Len is in samples; both are read at the Trig. Trig 1 starts the reader on that region at the event's frame at
  // the reader's own speed, direction and level; Trig 0 releases it; otherwise it ends at its end. See Loop (Regions).
  enum class RegionField : std::uint16_t { Start, Len, Trig };
  static constexpr std::uint16_t region_param(unsigned reader, RegionField f) noexcept {
    return static_cast<std::uint16_t>(R1Start + 3u * reader + static_cast<unsigned>(f));
  }

  // MIDI slices (M2). A unit has one route, so the user's channel arrives as plain NoteOn / NoteOff and the unit tells them from the app's own
  // senders (pads, the step sequencer, the chord latch) by flags: those carry kNoteLocal on both the NoteOn and the NoteOff (note_off_local).
  // An unmarked NoteOn of the notes kSliceNoteLow .. kSliceNoteLow + count - 1 fires slice 1 .. count of the published source (bind_slices) on
  // that source's reader at the note's frame, one shot (the NoteOff does not end it), its velocity the slice's level (100 and up full,
  // below v * 0.01). Any other note, marked or not, is the carrier's. A fired note never touches held notes, the carrier or the velocity
  // gain. A reader that is armed or recording refuses the note (it does nothing).
  static constexpr std::uint8_t kNoteLocal = 1u << 7;  // the kit uses bits 0 and 1 of flags
  static constexpr unsigned kSliceNoteLow = 36;
  static constexpr o3sdk::dsp::Event note_off_local(std::uint8_t note, std::uint16_t frame = 0) noexcept {
    return o3sdk::dsp::Event{frame, o3sdk::dsp::EventKind::NoteOff, note, 0u, kNoteLocal, 0u, 0.0f};
  }
  // Main loop, before the engine starts: the map the unit reads `source` of (its address never moves); null: no MIDI slices.
  void bind_slices(const SliceMap* map) noexcept { slices_ = map; }

  // What a loop is doing, for the screen and the console: written by the render context once a block, read by the main loop (one atomic
  // word per loop, so a read is never torn).
  struct LoopStatus {
    enum class Rec : std::uint8_t { Idle, Armed, Recording, Stopping };  // Armed: waiting for the bar line; Stopping: until the bar line
    Rec rec = Rec::Idle;
    bool playing = false;
    bool pending = false;  // Play asked, waiting for the bar line
    bool carrier = false;  // the role
    std::uint8_t speed = 1;    // 0 0.5X, 1 1X, 2 2X
    bool reverse = false;
    float level_db = 0.0f;
    std::uint32_t length = 0;  // samples recorded so far (while recording) or in the loop
    bool region = false;       // sounding a region (the whole loop, if it was playing, is paused under it)
    std::uint32_t position = 0;  // where the reader is, in samples from the start of the bank (the region's, else the loop's), as of the block's end
  };
  LoopStatus loop_status(unsigned loop) const noexcept;
  // Main loop, before the engine starts: the loop memory, Loop::kCount * per_loop int16 (per_loop up to Loop::kMaxSamples), 2-byte aligned,
  // that outlives the unit (the app's O3SDK_BIG array). Not touched by prepare().
  void bind_loops(std::int16_t* mem, std::uint32_t per_loop) noexcept { loops_.bind(mem, per_loop); }
  const Loop& loops() const noexcept { return loops_; }

  const o3sdk::dsp::UnitInfo& info() const noexcept override;
  void prepare(float sample_rate) noexcept override;
  void process(const o3sdk::dsp::ProcessArgs& args) noexcept override;

  // What the PERFORM screen shows of BANDS (docs/UI-DESIGN.md section 8): the sixteen followers as 8-bit logs (0 is -60 dBFS or less, 255 is 0 dBFS),
  // band 0 (125 Hz) first. Packed four to a word, written by the render context once a block, one relaxed word-sized atomic each: a single writer like
  // LoopStatus, a read is never torn (the four words may be a block apart, which a meter does not show).
  struct View {
    std::uint8_t band[Bands::kBands] = {};
  };
  View view() const noexcept;

  const Carrier& carrier() const noexcept { return carrier_; }
  const Bands& bands() const noexcept { return bands_; }
  const Talk& talk() const noexcept { return talk_; }
  const Autotune& tune() const noexcept { return tune_; }

 private:
  void apply(const o3sdk::dsp::Event& e) noexcept;
  bool midi_slice_on(const o3sdk::dsp::Event& e) noexcept;
  void set(std::uint16_t id, float v) noexcept;
  void loop_param_event(std::uint16_t id, float v, std::uint32_t frame) noexcept;
  void region_param_event(std::uint16_t id, float v, std::uint32_t frame) noexcept;
  void run_loops(const o3sdk::dsp::Transport& t, std::uint32_t n, float* mod, float* car) noexcept;
  void publish_loops() noexcept;
  void publish_view() noexcept;
  void select_carrier(float v, std::uint32_t frame) noexcept;
  void retarget(std::uint32_t frame) noexcept;
  float noise_target() const noexcept { return sel_ == 2u ? 1.0f : (sel_ == 1u ? 0.0f : noise_user_); }
  float car_target(unsigned loop) const noexcept { return (sel_ == 0u ? slot_[loop].carrier : sel_ == 3u + loop) ? 1.0f : 0.0f; }
  void step_bend(std::uint32_t n) noexcept;
  void step_glide(std::uint32_t n) noexcept;
  void held_insert(std::uint8_t note) noexcept;
  void held_remove(std::uint8_t note) noexcept;
  void set_arp(unsigned mode) noexcept;
  void arp_step() noexcept;
  void clock_start_now() noexcept;
  void clock_edge() noexcept;
  void apply_gate(float* car, float* gate, std::uint32_t from, std::uint32_t k, float* gain, bool& recorded) noexcept;
  void render_carrier(float* out, float* gate, std::uint32_t n) noexcept;
  void mix_car(unsigned loop, std::uint32_t from, std::uint32_t n, float* car, float* tmp) noexcept;

  // One loop's control state (render context only; the main loop reads status_).
  struct Slot {
    enum class Rec : std::uint8_t { Idle, Armed, Recording };
    Rec rec = Rec::Idle;
    bool want_rec = false;    // the Rec parameter; false while Recording means stop at the bar line
    bool want_play = false;   // Play asked and not started yet
    bool carrier = false;
    std::uint8_t speed = 1;
    bool reverse = false;
    float level_db = 0.0f;
    std::uint16_t req = 0;    // the frame of the event that asked, this block only (0 afterwards): a bar line before it does not count
  };

  Carrier carrier_;
  Bands bands_;
  Talk talk_;
  Autotune tune_;
  NoiseSource noise_;
  o3sdk::dsp::Ramp level_;
  o3sdk::dsp::Ramp dry_;
  o3sdk::dsp::Ramp bands_mix_{1.0f};  // one ramp per mode: 1 for the mode that is on, the others 0
  o3sdk::dsp::Ramp talk_mix_;
  o3sdk::dsp::Ramp tune_mix_;
  Loop loops_;
  // The carrier switch (render context only). sel_ is the CarSrc value; the ramps are 5 ms. pend_: the carrier-side loop gains each
  // event of this block set, in order, so run_loops can start the ramp at the same frame (the oldest are overwritten past 8).
  unsigned sel_ = 0;
  unsigned msel_ = 0;                // ModSrc: 0 LIVE, 1 NONE, 3..6 the slot that speaks (any other value acts as NONE)
  o3sdk::dsp::Ramp live_car_;        // the live input on the carrier bus (CarSrc 7), 5 ms
  bool live_r_ = false;              // LiveCh: that input is in_r
  float vel_gain_ = 1.0f;            // the last NoteOn's velocity as the synth's level, 1 from 100 up
  float noise_user_ = 0.0f;
  o3sdk::dsp::Ramp synth_gain_{1.0f};
  o3sdk::dsp::Ramp noise_amt_;
  o3sdk::dsp::Ramp bend_;  // semitones; the carrier's ratio follows it (step_bend)
  o3sdk::dsp::Ramp vib_;   // vibrato depth 0..1 (20 ms glide); an LFO of kVibHz, peak +-0.5 st at 1, added to the bend (step_bend)
  float vib_phase_ = 0.0f;  // cycles, 0..1; reset while the depth is 0
  float vib_inc_ = 0.0f;    // cycles a sample
  bool vib_was_ = false;    // the last step_bend had a vibrato, so the next one returns the ratio to the bend alone
  // GLIDE, GATE, ARP: the render context only. One step clock for GATE and ARP: clock_cnt_ samples to the next half-step edge (fractional carry),
  // second_half_ the off half of the step; the step edge is the end of the second half. held_ is the sorted list of held notes.
  float fs_ = 44100.0f;
  float glide_ms_ = 0.0f;
  float glide_coef_ = 0.0f;   // for glide_n_ samples
  std::uint32_t glide_n_ = 0;
  float clock_cnt_ = 0.0f;
  float half_len_ = 1000.0f;  // samples in half a step
  float gate_g_ = 1.0f;       // the gate's gain, slewed 2 ms
  float gate_step_ = 0.01f;
  std::uint32_t rng_ = 0x1F123BB5u;
  std::uint8_t gate_ = 0;
  std::uint8_t arp_ = 0;
  std::uint8_t nheld_ = 0;
  std::uint8_t arp_pos_ = 0xFFu;   // index in held_ of the note sounding, 0xFF none yet
  std::uint8_t arp_note_ = 0xFFu;  // the note the arp has on in the carrier, 0xFF none
  bool arp_down_ = false;
  bool second_half_ = true;
  bool clock_on_ = false;
  bool playing_ = false;  // the transport's state at this block's start, for the arp's first note
  std::uint8_t held_[Carrier::kVoices] = {};
  o3sdk::dsp::Ramp loop_car_[Loop::kCount];
  struct Retarget {
    std::uint16_t frame = 0;
    float car[Loop::kCount] = {};
  };
  Retarget pend_[8];
  unsigned npend_ = 0;
  Slot slot_[Loop::kCount];
  const SliceMap* slices_ = nullptr;  // bound once; `source` is read with acquire (see SliceMap)
  std::uint32_t midi_fired_ = 0;      // render context only: bit (note - kSliceNoteLow) of the MIDI notes that fired a slice (their NoteOff is theirs)
  std::atomic<std::uint32_t> opts_[Loop::kCount] = {};    // packed speed | reverse << 2 | (level in half dB + 64) << 8, same protocol
  std::atomic<std::uint32_t> status_[Loop::kCount] = {};  // packed LoopStatus: single writer (the render context), word-sized reads
  std::atomic<std::uint32_t> pos_[Loop::kCount] = {};     // LoopStatus::position: same protocol
  static_assert(Bands::kBands == 16, "the View packs the sixteen followers into four words");
  std::atomic<std::uint32_t> view_[4] = {};  // View::band, four to a word: single writer (the render context), word-sized reads
};

}  // namespace o3_voc03er
