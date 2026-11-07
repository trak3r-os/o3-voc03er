# Engine realism: BANDS and TALK

What makes the two engines sound real, what was wrong with them, and what changed. The sources are in `docs/ports/engine-realism-research.md`; this file is the analysis and the decisions.

## How it was measured

The sound figures are host measurements, and nothing here has been heard on a unit. The CPU figures under "Unit readings" were read on one.

- **Input.** A synthetic voice (flite, 7.8 s of speech) as the modulator and a 110 Hz saw as the carrier. Vowels with known formants at f0 120 Hz for the shape error.
- **Engines.** The real `bands.cpp` and `talk.cpp` run over the files, through a small bench. Prototypes are double precision and are labelled.
- **Intelligibility.** `STOI~` is an approximation of STOI: envelope correlation in 15 third-octave bands from 150 Hz to 4.3 kHz. It is good for comparing engines against each other and says nothing about 5 kHz and up.
- **Lag.** The shift that gives the best `STOI~`, scanned in 1.4 ms steps. It is the delay of the envelope, not only of the signal.
- **Shape.** RMS error in dB of the output over the carrier at each harmonic, against the true formant filter, with the tilt fitted out.
- **Spectrum.** Long-term spectrum in seven bands from 250 Hz to 11 kHz, each in dB below the loudest band, against the voice's own.

The voice is synthetic and the metric is ours, so the numbers rank changes. They do not prove that a person hears a real vocoder.

## What the machines do

From the research, in the order it matters here:

1. **A channel vocoder keeps the voice's dynamics.** Hardware passes the envelopes through; compression, where it exists, is a separate input stage with a stated range (20 dB on a 14-band pedal-format unit).
2. **The edges are treated apart.** Hardware gives the top a separate path: a wide HF channel switched on sibilance (the 16-band modular unit), noise-only top bands (the 20-band unit, unverified), high-pass and low-pass edge filters (the 22-channel studio unit, the 15-filter modular unit).
3. **Followers scale with the band.** The 22-channel studio unit smooths in proportion to centre frequency. BANDS already grades its followers that way.
4. **Sixteen bands is for naturalness, not intelligibility.** Sentences survive on 4 to 8 bands; speech importance sits between 500 Hz and 4 kHz.
5. **In a talkbox the mouth adds formants and lip radiation, not a voice's source.** The carrier's spectrum stays. Consonants that rely on breath cannot go down the tube: the microphone hears the player's own air. The tube is a mild comb, not a vowel filter. These are inferences from physics and from electrolarynx work: **no measured talkbox spectrum was found**.
6. **LPC needs one pole pair per kHz of bandwidth.** Full rate at order 16 gives 8 pole pairs over 22 kHz. mda Talkbox analyses at fs/2 for this reason.

## BANDS

**The filters were never the problem.** A prototype without the polarity flip of `syn_gain_` suggested that an 8-pole carrier path was worth 0.09 of `STOI~`. A model of the real topology, with the flip, shows no such gain:

| Model of the real topology | Sum ripple | STOI~ | Shape |
|---|---|---|---|
| Today: 2 sections Q 2.3 on both paths | 1.4 dB | 0.865 | 11.3 dB |
| Carrier 4 sections Q 1.6 | 1.1 dB | 0.854 | 11.5 dB |
| Carrier 4 sections Q 1.3 | 0.3 dB | 0.855 | 11.5 dB |
| Carrier 3 sections Q 1.8 | 0.6 dB | 0.859 | 11.4 dB |

The same model came out at 0.865 while the real engine measured 0.756, and the gap is two constants:

| Real `bands.cpp` | STOI~ | Lag | HF vs voice |
|---|---|---|---|
| Today | 0.756 | 5.4 ms | +9.4 dB |
| No noise path | 0.785 | 5.4 ms | -1.3 dB |
| No noise path, no AGC | 0.866 | 13.6 ms | -7.4 dB |
| Noise 0.12, AGC 50/500 ms | 0.860 | 10.9 ms | -4.4 dB |
| Noise 0.18, AGC 50/500 ms, followers 1/15/25 ms | 0.866 | 6.8 ms | -2.7 dB |

- **The AGC was a compressor.** With a 5 ms attack and a 100 ms release it followed syllables and flattened the very envelopes the bands carry. At 50/500 ms it levels a singer, not a word. Today's 5.4 ms lag is not a virtue: it comes from the AGC lifting onsets.
- **The noise path was 20 dB too loud** in the long-term spectrum. At 0.18 it sits within about 4 dB of the voice from 250 Hz to 8 kHz.
- **Faster followers** (1/15/25 ms against 2/25/40 ms) take the lag down by 4 ms. The price is more ripple at the voice's pitch: 3.8 dB against 2.2 dB at band 11 on a steady vowel. 1/10/20 ms reaches 5.9 dB and is rejected.

Cost: none. Output rms stays where it was and peaks fall from 5.2 to 3.0 before the soft clip.

The AGC's slower release has a price: from a reset, a voice 10 dB under the target takes about 0.6 s to reach full gain, and the gain stays up for about a second after a phrase ends.

**Tried and rejected on BANDS:**

| Idea | Result |
|---|---|
| Mel spacing | Worse than the log spacing in the prototype (0.768 against 0.794) |
| 2-pole follower | No gain, more lag |
| Output tilt of -3 or -6 dB | Worse |
| 20 or 24 bands | +0.014, +0.022 of `STOI~` for 25 % or 50 % more band work, and the View and PERFORM assume 16 |
| Sharper carrier sections | No gain (above) |

**Kept for later:** a third section on the modulator path takes shape error from 11.3 to 9.3 dB and tilt from +8.0 to +5.2 dB/oct, at 25 % more filter work [est]. BANDS is already over the 60 % budget with 8 notes and 4 loops, so this would need paying for first, by running the lowest bands at fs/4 or fs/8 between more halfbands. That rewrite was estimated at about a third less band work, and has not been built.

## TALK

Three separate causes, each one measured.

**Too little resolution.** Order 16 at 44.1 kHz spreads 8 pole pairs over 22 kHz. At fs/4 the same work sits where the formants are: pole error on known vowels falls from 162/305/164 Hz (F1/F2/F3, full rate, order 16) to 12/23/30 Hz (fs/4, order 12). Order 14 at this rate starts fitting harmonics.

**Too much delay.** The window was 17.4 ms with a hop of 8.7 ms, and the analysis was spread over the next hop, so the coefficients started moving 8.7 ms after the window closed. A short hop and a window that leans towards its newest samples take lag from 27 ms to about 13 ms.

**Too dull.** The all-pole filter keeps the voice's tilt, and the saw carries its own: 15 dB too little at 4 to 8 kHz. A first difference on the carrier brings the 2 to 5 kHz region level with the voice. It was 0.4 in the prototype and is 0.2 in the engine, because the AGC below lifts quiet consonants by itself.

**No consonants.** Nothing above 5.5 kHz can come from a carrier at fs/4, and in a real talkbox it does not come from the tube anyway. The breath path adds the modulator's own air above 5 kHz, gated by the carrier, so it stops when the carrier stops. THRU now scales it in TALK as well (`0.5 + 0.5 x THRU`).

| Prototype, real halfbands | STOI~ | Lag | Shape | HF vs voice |
|---|---|---|---|---|
| Real `talk.cpp` today, order 16 | 0.843 | 27.2 ms | 12.5 dB | -16.0 dB |
| Real `talk.cpp` today, order 24 | 0.861 | 25.9 ms | 12.2 dB | -16.8 dB |
| fs/4, order 12, hop 5.8 ms | 0.912 | 15.0 ms | 9.9 dB | -5.1 dB |
| fs/4, order 12, hop 2.9 ms | 0.913 | 12.2 ms | 9.9 dB | -5.1 dB |
| + carrier difference 0.4, breath 0.5 at 5 kHz (hop 5.8 ms) | 0.911 | 15.0 ms | 10.3 dB | +0.9 dB |

Cost: 10 KiB back in SRAM_U (the margin went from 296 B to about 10,500 B), and much less CPU saved than estimated. The count in `talk.h` said about 5,800 cycles a block against 16,600. The unit said otherwise in 6a19f36: 8 notes at `Quality` 16 went from about 138k to 132k, and with 4 loops the mean stayed near 160k, over the budget of 156,734. Each stage cost about 2,000 cycles a block, mostly the autocorrelation, whose one-lag dot product compiled to a single `vfma` loop with two loads a multiply-add and a sum that waits on itself. The fix is under "Unit readings".

**Level.** TALK had no AGC and `kTalkGain` was a placeholder, so a change of MODE fell 15 dB on a tone and about 27 dB on speech: at the default LEVEL, speech in TALK came out at about -40 dBFS. TALK now shares BANDS' slow AGC (`agc.h`), and `kTalkGain` is 28, set so speech on a one-note saw has the same rms in both modes. In `vocoder.txt` the same chord reads 0.27 in BANDS and 0.26 in TALK. A change of MODE hands the AGC's level to the engine coming in; without that, a hot input came in about 20 dB too loud until the follower caught up. From silence the filter now lands at once, so a phrase starts on its formants and not on a glide from flat.

Quality keeps its id and range. The order is `Quality x 3 / 4`, so the default 16 gives 12 and 24 gives 18.

**Tried and rejected on TALK:**

| Idea | Result |
|---|---|
| Removing mda's allpass spread alone | Nothing either way (0.498 against 0.499 before lag was compensated): it was dropped as dead weight, not as a fix |
| Fast gain follower, 1/12 ms | 0.862 against 0.912: it adds amplitude modulation at the voice's pitch |
| Hiss by the modulator's HF envelope | 0.890, shape 13.4 dB, and fills 8 to 11 kHz where a voice has nothing |
| Carrier HF by a follower | 0.909, shape 11.4 dB, and a much weaker result at 5.6 to 8 kHz than the breath path |
| Warped LPC, true envelope, pitch-adaptive window | Over budget by the research's estimate; not tried |

**Not built, for lack of evidence:** a driver's saturation (drive 2: no measured change), a 150 Hz cut (no measured change), the tube's comb at 10.5 ms (+2.6 dB around 500 Hz to 1 kHz, not checkable). They are cheap. With no talkbox spectrum to aim at they would be guesses, so they wait for the maintainer's ear or for a real recording to measure against.

## Result on the real code

Flite speech on a 110 Hz saw, the real code at `Quality` 16 for TALK.

| Engine | STOI~ | Lag | Shape | HF vs voice |
|---|---|---|---|---|
| BANDS before | 0.756 | 5.4 ms | 11.5 dB | +9.4 dB |
| **BANDS now** | **0.866** | **6.8 ms** | 11.5 dB | **-2.7 dB** |
| TALK before | 0.843 | 27.2 ms | 12.5 dB | -16.0 dB |
| **TALK now** | **0.903** | **13.6 ms** | **10.2 dB** | **+0.6 dB** |

`Quality` 8 gives 0.870 and `Quality` 24 gives 0.921, both at about the same lag.

Long-term spectrum, dB below the loudest band:

| Band (Hz) | 250-500 | 500-1k | 1k-2k | 2k-4k | 4k-5.6k | 5.6k-8k | 8k-11k |
|---|---|---|---|---|---|---|---|
| Voice | 0 | -3.8 | -15.5 | -24.3 | -24.8 | -23.6 | -64.1 |
| BANDS before | 0 | -5.7 | -19.1 | -13.5 | -7.5 | -7.3 | -15.9 |
| BANDS now | 0 | -5.3 | -18.6 | -24.5 | -20.5 | -20.9 | -29.6 |
| TALK before | 0 | -10.4 | -24.7 | -36.9 | -38.5 | -36.5 | -68.8 |
| TALK now | 0 | -5.2 | -17.5 | -23.4 | -21.3 | -26.0 | -65.8 |

BANDS keeps more above 8 kHz than a voice has, because its noise goes in whatever the voice's band edge is. That is a vocoder's character and was left alone.

## Unit readings

Candidate 6a19f36, cycles of `render last`, 261,224 a block, budget 60 % = 156,734. Underruns 0, shed 0.

| Row | No notes | 8 notes | 8 notes + 4 loops |
|---|---|---|---|
| BANDS | 107k | 142k | 170k |
| TALK, `Quality` 8 | | | 147k |
| TALK, `Quality` 16 | 96.5k | 132k | 160k |
| TALK, `Quality` 24 | | | 171k |

A change of MODE costs one block of 76 to 88 %: the crossfade runs both engines. That settles the 93.9 % left open in `docs/PLAN.md`. THRU costs nothing.

The AGC reading was inconclusive: the OUT meter on a spoken signal gave figures that ran against the input, and they do not hold on the host, where the real engines are flat within 0.3 dB from -6 to -30 dBFS on speech and on a sine, and the whole unit holds its level over 24 dB of a voice (`talk_test.cpp`). It needs a steady signal and repeated reads.

Candidate 37fa85f: `Talk::slice` runs four lags in a pass and `talk.cpp` is built without contraction, so 16 multiply-adds take 8 loads, about 3.5 cycles each against about 8 [est, from the disassembly]. The ramp's step shares the lattice's pass and the breath filter keeps its state in locals. The bench's figures do not move. The estimate was 8k less at `Quality` 16; the unit read 17k less.

| Row | No notes | 8 notes | 8 notes + 4 loops |
|---|---|---|---|
| BANDS | 106.0k | 142.0k | 165.4k |
| TALK, `Quality` 8 | | | 138.4k |
| TALK, `Quality` 16 | 82.2k | 116.1k | 142.8k |
| TALK, `Quality` 24 | | | 150.3k |

TALK is now inside the budget at every `Quality` by its mean. `Quality` 16 went over it in one block; `Quality` 24 peaks at 166,762 and counted 492 blocks over. Underruns 0 and shed 0 on every read. The worst peak of the round was 183,755 cycles, 70.3 %.

The AGC on a steady vowel, OUT meter, five reads a level at -6, -18 and -30 dBFS: BANDS -12.8, -12.4, -11.6 dB; TALK -13.8, -13.0, -11.8 dB. The means hold within 1.2 and 2.0 dB over 24 dB of input, and the spread from read to read, 1 to 5 dB, does not follow the level. That passes at the meter's resolution; the host's 0.3 dB is not something this meter can show.

## E1: instant TUNE at SPEED 0, a driver on the TALK carrier, ROBOT

Three sound changes, all built and host-tested, none heard. Every cost below is an estimate; the figures are host figures.

- **SPEED 0 decides at once.** With `alpha_ == 1` the TUNE note decision was still smoothed (`m_s_` followed `m_`); now `m_s_ = m_`, so a stepped correction lands on the first voiced block. Other speeds are untouched. Test: `autotune_test.cpp` (the glide reaches the target note sooner at 0 than at 5 ms, and a wobble inside the hysteresis does not flicker).
- **A talkbox driver on the TALK carrier** (`CarDrive` in `talk.cpp`). A 250 Hz one-pole high-pass and a cubic soft clip (knee 0.75, gain 1.6) at the full rate, before the halfband decimators, with `mulb` and no libm or division per sample. A compression driver does not pass the low end and clips what is left, which is the point of the sound; whether this one sounds like one is for the ear. No new parameter. Level kept close: the unit's talk/bands ratio went from 0.7928 to 0.8422 (talk rms 0.16763), the AGC test's rms from 0.01519 to 0.01237, the 12 and 24 dB rows hold at 0.12187. Driver gain at 60 Hz 0.230, 250 Hz 0.692, 1 kHz 0.952 (small signal); a hot carrier peaks at 0.8000.
- **ROBOT, the fourth MODE value** (parameter 7 now runs 0 to 3; 62 of 64 parameter ids used, no id added). It is BANDS (the same `Bands` instance and wet sum, so it costs no second filter bank) followed by `RobotComp`: a feed-forward peak compressor, 1 ms attack, 50 ms release, 4:1 above a threshold, make-up 3.0, the gain computed every 16 samples (two divides and three square roots per block) and ramped linearly across it. ROBOT draws what BANDS draws (the screens use `mode_page()` for behaviour and `mode_index()` for the name); the preset named ROBOT uses it. BANDS stays bit-identical: `talk_test.cpp` takes a digest of a fixed run (E55118E5, the same before and after the change), and the BANDS output loops are the original code, branched on ROBOT.
  Measured on the host with the voice alternating 12 dB every 174 ms (faster than the AGC's 500 ms release): the quiet/loud level ratio is 0.319 in BANDS and 0.751 in ROBOT (the input's is 0.253). Threshold 0.0008 on the raw wet sum and make-up 3.0 are first guesses; ROBOT's loud level came out 2.8 dB under BANDS' there.

Estimated cost: well under 1 % of a core for ROBOT, the driver a few cycles per carrier sample. Estimates only. The unit object grows by 998 bytes of text (talk +88, autotune +16, `robot.cpp` +356, `vocoder.cpp` +538), no bss.

Owed: by ear (the driver, ROBOT's threshold, make-up and release, SPEED 0 on a real voice) and the CPU on the unit. ROBOT has to be measured on the BANDS 8 notes + 4 loops row, whose peak was 65.5 % against a limit now 69 %, because it runs the BANDS engine and the compressor on top.

## E2: the VOICE carrier

CarSrc 8, VOICE (no new parameter id; parameter 34 now runs 0 to 8): the synth carrier plays the pitch the singer sings, snapped by KEY and SCALE, in any MODE. Built and host-tested, nothing heard.

- **Detector.** `Autotune::detect` runs the `Tune` detector alone (no shifter) while VOICE is on, outside TUNE too; in TUNE the same detector feeds both, so there is one copy. With VOICE off it costs nothing, and every other output is bit-identical (the BANDS digest in `talk_test.cpp` is unchanged).
- **Note.** `Autotune::voice_step`, per 16-sample segment (so it does not depend on the block length), takes the detector's frequency, converts it to a note number and snaps it with the same `pick()` and `kSnapBias` as TUNE; SPEED 0 decides at once, and a slower SPEED smooths the pitch first. The note is kept apart from TUNE's, so TUNE with NOTES IN does not steer it. When the voice stops, the note is held `kVoiceHoldMs` (60 ms, 165 segments) so a consonant or a breath does not cut it, then released.
- **Carrier.** `Vocoder::voice_sync` runs before each carrier render and turns the wanted note list into note offs and ons, so a change goes through GLIDE and the carrier's legato, with no click. No pad held: the sung note alone. Pads held: each sounds at sung + (held - lowest held), within `Carrier::kVoices` (8). No sung note, pads held: silence. ARP maps the arp's current note the same way, and GATE is a gain on the carrier output, so both work unchanged. The decision lands one block (about 1.45 ms) after the audio, small against the detector's 11 to 48 ms.
- **Left out.** The per-step carrier of a pattern (`pat ... car`) does not offer VOICE: it is a pad index, not a CarSrc. With TUNE alone (no carrier render) VOICE has nothing to sound.
- **Cost (estimates).** Control rate only: one `log2f` per new detector estimate (a hop, 512 samples) and a few compares per segment; no libm, division or allocation per sample. The unit object grows by about 33 bytes of state (Autotune 24, Vocoder 9) plus the code.

Test: `tests/voice_carrier_test.cpp` (a sung sine outside the scale gives the snapped note, silence releases after the hold, a held triad follows the voice, VOICE off changes nothing), and `app/scenarios/voice_carrier.txt` (the picker row, the header, the WAV).

Owed: by ear, CPU on the unit, onset lag and octave errors on a real microphone.

## TALK as a talkbox, S1, S1b and S2

A real talkbox puts the carrier through the mouth, so a consonant that is air (s, f, sh) is never the carrier: the sound is the player's own breath. The LPC route gave the opposite, because the carrier's level follows the residual (`gain_t_` is sqrt of the prediction error), and noise is the least predictable input there is. Two changes, no new parameter:

- S1, the duck. Per hop (128 frames) the 5 kHz breath high-pass's h^2 is summed (hf, per full-rate frame) and set against the mean square of the un-emphasised full-rate modulator over the same hop (ms): `ratio = hf / (ms + eps)`, one divide per hop (S1b; S1 divided by r0 of the pre-emphasised, decimated window, see below). `gd = clamp((kDuckT1 - ratio) / (kDuckT1 - kDuckT0), kDuckFloor, 1)` multiplies `gain_t_`, and the breath level becomes `kBreath x (kBreathVoiced + (1 - kBreathVoiced) x (1 - gd))` of the old rule, so the breath is the larger part of a fricative and a quarter of its old share under a vowel. The hf read at a publish is the one of the hop before the last one (a hop and a half old, the same for any call split, so the output stays independent of it).
- The absolute gate (from the review). A hop counts as unvoiced only when `lf + hf` is above `kUnvGate` = 10 x `kGate`. A silent modulator's gap noise has a flat spectrum and would read as unvoiced; below the gate `gd = kDuckFloor` and the breath is 0 (`breath_level()`). The gate is a mean power (2.5e-9), about -86 dBFS rms for white noise; a -60 dBFS noise is above it, so it is judged by its spectrum, not gated.
- S2. A one-pole low-pass at 3.5 kHz on the low-rate carrier before the lattice (coefficient at prepare, no libm per sample), and `kCarEmph` 0.2 to 0.1: the lattice's input is no longer rich in the highs that a consonant's noise should carry alone.

S1b, the scales. Against the TALK before S1, S1 cut the 100-3000 Hz band of real speech by a median 7.2 dB on active frames (p10 -20 dB): it closed on vowels. The pre-emphasis (0.9) shrinks the lows in the window's power by 15 to 20 dB, so `hf / power` read high wherever the lows carry the voice. Logged per hop on 8 s of real speech (44.1 kHz mono, 2745 hops, 2085 active; voiced = autocorrelation of the 1 kHz low-passed frame above 0.5, 1139 hops; unvoiced = autocorrelation below 0.3 and more than 30 % of the frame's power above 4 kHz, 152 hops):

| ratio | voiced p50 / p90 / p95 / p99 | unvoiced p10 / p25 / p50 |
|---|---|---|
| S1: hf / pre-emphasised power | 0.24 / 1.6 / 2.4 / 5.2 | 0.23 / 0.40 / 0.78 |
| S1b: hf / mean square of the modulator | 0.009 / 0.13 / 0.25 / 0.62 | 0.39 / 0.59 / 0.65 |

The two distributions of S1 overlap almost completely (a vowel's median is a third of an "s" median's); S1b separates them by a factor of 70 at the medians. Smoothing hf and ms over two to six hops did not move the voiced tail (it sits on the transitions to and from fricatives, where the label itself is mixed). The thresholds, as starting values: `kDuckT0` 0.28 (95.7 % of voiced hops are at gd = 1) and `kDuckT1` 0.45 (88 % of unvoiced hops reach the floor, 91 % are below gd 0.5). The ms is one more multiply-add per frame in the two breath loops (`ms_acc_`, `ms_done_`, 8 bytes; `duck_` is the last gd, for the tests).

Constants, all starting values that the maintainer's ear and a CPU read on the unit must confirm: `kDuckT0` 0.28 and `kDuckT1` 0.45 as above (the ratio at which the carrier is fully on and at its floor; a held synthetic vowel reads about 0.001, a breathy one 0.01, held "sss" 0.7), `kDuckFloor` 0.05 (-26 dB), `kBreath` 0.5, `kBreathVoiced` 0.25, `kUnvGate`, `kCarLpHz` 3500, `kCarEmph` 0.1. The floor is the briefed 0.05: lowering it to 0.003 gave -54 dB in the test but that is a bench number, not a judgement of what sounds right.

Tests (`tests/talk_test.cpp`): a pulse train through three formant resonators against white noise high-passed above 4 kHz, with the same carrier. The carrier part (the output with the saw minus the output with a constant carrier that opens the breath but is removed by the driver's high-pass, so the difference is the carrier path alone) is 30 dB under the vowel's with floor 0.05, and the breath is the larger part of the output under sss. A modulator below the gate leaves `breath_level()` at 0 and the output silent. S1b adds `Talk::duck_gain()` (the last publish's gd) and two tests: vowels at f0 100, 150, 200 and 250 Hz ("ah", a high-F2 "ee" with F1 280, F2 2250, F3 2900, and "oo"), clean and with 10 % white noise on top (1 % of the power, about 0.8 % of it above 5 kHz, as in real speech), keep gd within 1 dB of 1 on every publish after the first 120 blocks; "sss" (noise above 4 kHz) and "sh" (above 2.5 kHz) sit at the floor. The breathy vowels are what separates the two cues: the S1 ratio ducks them to 0.05 ("ee" and "oo") or 0.62 ("ah"), the S1b ratio leaves them at 1; the clean synthetic vowels pass under both, which is why S1 got through its own test. The first draft of the sss test took a small saw as the breath reference; it was a different breath (the gate reads the carrier's peak), which hid the duck.

Cost [estimate, not a measurement]: per 64-frame block one multiply-add more per frame in the breath loops (64 x about 3), the low-pass on 16 low-rate samples (about 50), one divide a hop; about 250 cycles a block, under 0.2 % of the 138k read in 37fa85f. S1b adds one multiply-add per frame (about 64 cycles a block, about 0.05 %). To be read on the unit.

S1b, measured against V0 on the renders (metrics2.py, saw110 / Am): tilt -1.27 / -2.03 (V0 -1.00 / -1.41); voiced 100-3000 Hz mean +0.49 / +0.41 dB over V0; above 12 kHz -36.3 / -39.8 dB (V0 -44.5 / -46.1), not met. On voiced frames the >12 kHz level matches V0 (-45.6 against -47.2); the excess is in unvoiced frames (93 % of the >12 kHz energy), where the breath (`kBreath` 0.5 x the full share, a wideband 5 kHz high-pass) is above V0's 0.3. Options, not done: a lower `kBreath` or a low-pass on the breath. These are measurements, not a claim about how it sounds; the ear on the unit decides. Image margin to 0x20022000: 9,752 B (9,768 before, 16 B for the new members).

Owed: by ear on a real microphone (fricatives, plosives, whisper, the hop-and-a-half lag of the duck on a fast "s" to vowel), and the CPU on the unit.

**S1c, the breath's top.** With S1b the consonants carry the breath (kBreath 0.5) instead of the carrier, and >12 kHz rose to -36.3 / -39.8 dB (saw110 / Am) against -44.5 / -46.1 before S1, 93 % of it on unvoiced frames. A one-pole low-pass at 7 kHz on the breath (`kBreathTopHz`, a starting value: the player's air at the microphone, not a full-band hiss; one multiply-add per frame) brings it to -43.3 / -47.3 dB. Same speech.wav, metrics2.py labelled from the speech: tilt -1.56 / -2.30 dB/oct (V0 -1.00 / -1.41), contrast 26.4 / 26.0 (V0 26.6 / 26.3), >8 kHz -24.7 / -28.8 (V0 -24.4 / -28.5), gap -59.8 / -63.4 dB (V0 -49.4 / -53.3). The 4-8 kHz unvoiced-over-voiced share is +9.4 / +9.1 dB (V0 +5.9 / +6.4): the consonants are now the breath and the vowels lost their breath floor, which is the point of S1; whether that reads as a talkbox is for the ear.

## Owed

- Accepted by the maintainer, 2026-11-06: BANDS with 8 notes and 4 loops stays at 165.4k, 63.3 % mean and 65.5 % peak, over the 60 % budget, with no underrun and no shed. That row's limit was 66 % at its peak, and the maintainer raised it to 69 % the same day, leaving room for ROBOT. Multi-rate bands wait, and the third modulator section with them.
- That load has not been read together with card work or dense MIDI, nor with FORMANT and THRU on this image.
- Levels by ear, since a slower AGC lets a phrase's dynamics through to the soft clip, and `kTalkGain` is a bench figure.
- By ear: follower speed, breath level against a real microphone, hop-rate zipper noise on fast vowels.
- A real talkbox recording, if one can be had for measuring. It would settle the driver, the tube and the top of the spectrum.
