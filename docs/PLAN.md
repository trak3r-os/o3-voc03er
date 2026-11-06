# O3-VOC03ER: the plan

The design is `../o3-sdk/docs/VOCODER-DESIGN.md` (what the engine is, its sources, loops, TUNE, the 2040 layer, costs); the research cards are
`../o3-sdk/docs/ports/`. This file records what the design left open and the order of work. Every cost marked [est] is an estimate until
`o3console.py cpu` has measured it on the unit.

## 1. Decisions on the design's open questions (section 11)

Made by the orchestrator on 2026-11-05 under the maintainer's "no questions" rule. Any of them can be overruled.

| Question | Decision | Why |
|---|---|---|
| Home and name | Its own app: repository `o3-voc03er`, id `o3_voc03er`, shown as `O3-VOC03ER` | The maintainer created that directory. Own SDRAM for the loops and a full CPU budget |
| Recorder for the loops | The unit records `in_l` into its own `O3SDK_BIG` buffer | Sample-exact loop points, overdub, and `in_l` is gated during a source switch (design section 3) |
| Loops | 4 loops of up to 16 s, mono int16: 5.6 MB of SDRAM | 8 bars at 120 BPM each; the design's sizing |
| Internal carrier | 8 voices, paraphonic: saw and pulse (polyBLEP), plus a noise mix | 4-6 % of a block [est]; enough for chords from the pads |
| Kit changes (section 7) | None for now; each one becomes an o3-sdk slice when a slice here needs it | The base needs none |
| TUNE | Clean-room YIN and period-jump shifter; no benchmark of cycfi/q | No candidate ports (section 4) |
| mda Talkbox licence | The MIT option, Paul Kellett's copyright kept | The port card's verdict |
| Harmonizer | Deferred to the 2040 layer | The weakest part of the design; 4-6 % a voice [est] |
| TUNE with no note held in NOTES-IN | Falls back to KEY and SCALE | A held note overrides the scale and the voice keeps being corrected between notes |

## 1b. The design review

`docs/DESIGN-REVIEW.md` is an outside review. Its arithmetic was rechecked and holds. Its section 2 is the design of each part, with these changes:

| Point | Decision |
|---|---|
| Bands geometry (review 2) | Taken: centres 125 x 2^(0.4 i), Q 2.3. Checked: V2's Q 3 leaves each band at -5 dB at the crossover, this leaves -3 dB |
| Intelligibility (review 1.3) | Taken and pulled forward to V2b: 80 Hz high-pass, slow AGC, unvoiced noise path |
| Second oscillator (review 2, 3) | Taken: DETUNE 0-20 cents. The carrier's budget is 12 %, not 4-6 % [est] |
| TALK structure (review 1.2) | Taken: streaming lattice, Hann 768, hop 384, order 16, reflection coefficients interpolated, freeze-a-vowel in V3. mda's own batch structure costs 55 % of a block in one block and 16 ms [est] |
| TUNE window (review 1.5) | Taken: W 256, the bass range at 8:1 |
| Loops in SDRAM (review 1.7) | Taken: copy the span to SRAM once a block, interpolate there |
| Measure first (review 4) | Taken: slice G0 |
| FFT in `dsp` (review 3) | Not needed: nothing here asks for it. Design section 7 stays as written until o3-sdk is next edited |
| Bar clock (review 3, an open question) | Answered by the code: `ProcessArgs::transport` gives the position at any frame (`Transport::at`), so the unit finds a bar line sample-exactly. No kit change, and the unit keeps the recorder |
| Licence of a zita port (review 1.9) | Dropped: the review had no licence for this app. It is GPL-3.0-or-later, so GPL-2.0-or-later and MIT code both fit. The shifter is still written fresh (section 1) |
| Free SRAM (review 1.1) | Answered by the link of the V1 image: 16,960 B free in SRAM_L, 88,800 B in SRAM_U. The parts want under 30 KiB [est] |
| In-to-out latency (G0 item 7) | Deferred: it needs a loopback cable and only sets expectations, nothing depends on it |

## 2. Shape

- One Instrument unit, `units/vocoder/`. `in_l` / `in_r` are the modulator, notes play the carrier. It depends on `dsp` only.
- Its parts are plain classes with no engine knowledge, each with a host test: `Carrier`, `Bands`, `Talk`, later `Loop`, `Tune`.
- Audio is at 44.1 kHz in 64-frame blocks. The render budget is 261,224 cycles a block; no per-block spikes (heavy analysis is sliced across blocks).
- State lives in the unit. Big buffers go in the SDRAM with `O3SDK_BIG`; hot state stays in SRAM.

## 3. Order of work

| Slice | What | Done when | State |
|---|---|---|---|
| V0 | The repository from the template, this plan | Host green | Done |
| V1 | The audio-in gate on the template app: console `ain <source> [gain]`, input peaks in `state` | Host test, gate CANDIDATE, unit round run | Done (section 4); the microphone left for later |
| V2 | `Carrier` and `Bands` (16 bands) in `units/vocoder/`, against a synthetic voice | Host tests, no doubles | Done on the host; revised by V2a and V2b |
| G0 | Microbench on the unit: the engine's own share, the real `Bands` and `Carrier`, a biquad bank, the YIN and autocorrelation inner loops, SDRAM reads | Numbers from `cpu` on the unit; they fix the band count and TALK's order before V4 | Done (section 4b), on V2's code; bands and TALK's order settled at 16 in section 4d |
| V2a | `Carrier`: second oscillator a voice (DETUNE), noise shared with the unvoiced path | Host tests, no doubles | Done on the host |
| V2b | `Bands`: the review's geometry and followers, high-pass and AGC, the unvoiced path | Host tests, summed bands flat within 3 dB in a sweep, no doubles | Done on the host |
| V2c | `Bands` and `Carrier` in fewer instruction cycles (section 4b): same sound, no fused multiply-adds in the sample loops, `b0` folded out of the sections, envelope segments out of the oscillator loop | Host tests unchanged, no doubles, each sample loop's instructions counted before and after; the cycles are read in V4's round | Done on the host: Bands' loop 81 to 47 cycles, Carrier's 122 to 66, by their instructions [est]; output differs by rounding only (3.8e-5 of peak at most) |
| V3 | `Talk`: streaming lattice, analysis sliced across blocks, freeze-a-vowel | Host tests, no block spike, the carrier path not delayed | Done on the host, no doubles; its CPU, its level (`kTalkGain` is a placeholder) and listening are owed to V4's round |
| V4 | The unit in the app: PLAY screen with the input peak, pads play chords, MODE and the source on the keys, line sources start at gain 0, `ain` clears its peaks, a scenario | Scenario, image, CPU of each mode measured on the unit, a spoken sentence intelligible to the maintainer | Built on the host and for mk66 (2026-11-05): the Vocoder attached as slot 0, PLAY screen with IN peak / CLIP, MODE and SOURCE (PLAY and SAMPLE PLAY keys, F1 is MODE), pads play chords, line sources at gain 0, `ain` clears its peaks (app-side meter reset plus a 300 ms holdoff, no kit change), console `voc`; the unit round (CPU of each mode, line silence, a spoken sentence) is owed |
| V2d | The last per-sample work: ramps out of the sample loop when settled, the unvoiced mix once a block, the follower every second sample, TALK's ramp every 8 | Host tests unchanged, no doubles; the cycles read on the unit | Done (section 4d); listening owed |
| V5a | Loops: record, play, roles, bar lines from the transport | Host tests, scenario, unit | Built 2026-11-05, host tests green (loop_test: bar-exact start and stop, seam, roles, full loop, transport stopped), no doubles, SDRAM 5.7 MB of 16. Owed on the unit: the CPU of a playing loop, recording through a real source, listening to the roles. CPU on the unit: section 4f; sound not heard. |
| V5b | Loops: speed and reverse, the span copy | Host tests, unit | Built 2026-11-06, host tests green (every speed and direction against an integer oracle, both wraps, loops of 1 to 100 samples, no drift over 16 s, changes while playing), mk66 image builds, no doubles. Per loop speed 0.5x/1x/2x, direction, level (ids 22..33). Owed on the unit: the CPU at each speed (BANDS, 8 notes), listening (melody kept, seam, level balance). CPU on the unit: section 4f; sound not heard. |
| V5c | Per-step carrier switch | Host tests, scenario, unit | Built (host tests, scenario carrier_steps, cross build); the unit check is owed: CPU, click on real sources, a MOD-role loop picked as carrier CPU on the unit: section 4g; sound not heard. |
| V6a | TUNE: detector and gate | Host tests against synthetic voices and three sung phrases; aubio as a host oracle only, never shipped |Built 2026-11-06: `Tune` (YIN, 4:1 and bass 8:1 decimation, native refine, 60 ms voiced gate), 13000 bytes, 10 host tests on synthetic voices (saw and glottal through formants, 80 to 800 Hz, worst 0.12 cent; clean sweep 0 octave errors; breathy 70 to 200 Hz 0.65%; vibrato no flips), voiced after 11 to 48 ms, ends 79 ms. Owed: CPU on the unit, the three sung phrases, breathy voice below 80 Hz. CPU on the unit: section 4f; sound not heard. |
| V6b | TUNE: shifter, snap, retune | Host tests, unit |Built 2026-11-06: `Autotune` (period-jump shifter on Tune's own native ring, 16-sample control step, snap to KEY and SCALE or to NOTES-IN, SPEED 0 = stepped), Mode 2 = TUNE, params 35..39, `voc tune`, F keys and PLAY screen, scenario `tune.txt`; 10 host tests (snap error ~0 cent, no click, block independent, NaN clean, unvoiced bit-exact delay, vibrato no flip). 13 KiB; SRAM_U 138,088 B (about 1.2 KiB above the 32 KiB free floor). Formants move with the shift (centroid ratio equals the pitch ratio); Range Normal only. Owed: CPU of TUNE on the unit [est about 7,100 cycles a block], a sung voice, listening. CPU on the unit: section 4f; sound not heard. |
| V2e | `Bands`: bands 0 to 12 at half rate (`docs/ports/vocoder-survey.md` item 1) | Host band response, alias rejection; unit read; ears | Host and unit done 2026-11-06 (4b89ce6, section 4h); ears owed |
| V2f | `Bands`: FORMANT, the spectral envelope moved up or down (survey item 5, about 1.2 % of a block [est]), a new value on PARAMS page SHAPE; graded follower rates and the high-band modulator thru, THRU (items 2 and 3), all three accepted by the maintainer 2026-11-06. Parameter ids 40 and 41. After V2e, whose restructure it builds on | Host tests, unit, ears | Host and unit done 2026-11-06 (416bea0, section 4h); ears owed, `kThruGain` an estimate, no scenario until PARAMS |
| V7 | The 2040 layer, slice by slice | | |
| U1 to U11 | The new interface, the sample slots and slices, the chord pads and the carrier synth: `docs/UI-DESIGN.md` section 12. The maintainer accepted the design and its section 11 recommendations on 2026-11-06, with the skin still to choose (`docs/ui-skins.html`) | Each slice as that table says | U1 started 2026-11-06: SRAM relief and the chord engine first |
| M1 to M3 | MIDI in: the carrier by notes, pitch bend, controllers, the clock, then slices and patterns: `docs/UI-DESIGN.md` section 7c and section 12 | Scenarios, unit over USB at the worst load; DIN owed | Asked by the maintainer 2026-11-06 |

## 4. The audio-in gate (design section 8)

Nothing else is relied on until this round says what the unit really hears. Order: USB return (no hardware needed), line, radio, microphone.

| Row | Needs | Pass |
|---|---|---|
| USB return | Direct USB, no hub | A -12 dBFS tone in the left channel only reads a peak within 1 dB of 0.25 left and below 0.01 right: the stereo claim of design section 2 |
| Line | A cable from the unit's output to line in | Peak above 0.03 with a note sounding, below 0.003 without |
| Radio | An antenna lead, a known station | `radio` reports tuned, peak above 0.03 |
| Microphone | A person at the unit | Peak above 0.03 on speech at `MicHigh`, below 0.01 in silence |
| Every row | | `cpu`: no underruns, no faults; a source switch never crashes and `settling` clears in about 200 ms |

Thresholds are starting values: a row that fails them with a plausible signal is reported with its numbers, not called broken.

### First round, 2026-11-05

Image `201b32b13f8b / o3sdk 33e170c7d8a5`, CANDIDATE 11/11, CRC E07CCB60. The kit is 33e170c because o3-sdk's HEAD does not compile for mk66 (`dd4a6cd`, which also forbids images on it). The unit was restored to what the round found.

| Row | Result | Measured |
|---|---|---|
| USB return | Pass | 1 kHz at -12 dBFS, left only, direct USB: peak 0.251 left, 0 right, four reads. USB return streaming, 0 underruns. The stereo claim of design section 2 holds for this source |
| Line | Signal yes; level and silence owed | A drum source: 0.75 to 0.88 at gain 0, 0.90 to 1.00 at gain 10, saturated from gain 15. Left and right equal in every read |
| Source switch | Pass | usb, line, left, right, miclow, michigh, radio, line: no crash, `settling` 1 straight after `ain` and 0 at +0.35 s all 8 times, `failed` 0 |
| `cpu` | Pass | Render at most 16.6 % of a block with the template voice, 0 underruns, 0 over budget, 0 late, 0 hardware faults |
| Radio | Owed | Switching only: chip ready at 87.50 MHz, no failure. Signal not judged, no known station |
| Microphone | Owed | Switching only, needs a person |

What it changes:

- Gain 0 is the ADC's 0 dB, not mute, and the ADC has no attenuation: a hot source can only be turned down at the source. This one had about 1.4 dB of headroom at gain 0. V4 starts line sources at gain 0, not 50, and the PLAY screen shows the input peak so that clipping can be seen.
- Line left and right are not proven separate: every read was equal, and most were saturated. The source may be mono. A later round needs a hard-panned source at a level below clipping.
- A peak read within a second of a switch still carries the previous source: the buckets of `poll_peaks()` are not cleared. Read at least a second after `ain` until `ain` clears them (V4).
- The maintainer turned the source down and says line and radio work (2026-11-05): their word, not a measurement here, and enough to go on. The microphone is left for later. Line silence, line left and right, and a known radio station are not measured; V4's own unit round reads them if it needs them.

## 4b. G0 on the unit, 2026-11-05

Image `bddfe4bed8f4 / o3sdk f14f247b0847`, CANDIDATE 11/11, CRC BA3A693C, on branch `g0`. Nine steps of three `cpu` reads: 27 reads, 0 underruns, 0 over budget, 0 late, 0 slips, 0 hardware faults. The unit was restored to what the round found.

**These are V2's parts.** The branch left main at 7b9dfad, before V2a and V2b: `bands` has no high-pass, AGC or unvoiced path, and `carrier` has one oscillator a voice with 8 notes held. The bench ran beside the template voice.

| Mode | Work a block | Render last, mean | Net of `off` | % of a block | Highest max | Per unit of work |
|---|---|---|---|---|---|---|
| `off` (6 reads) | The app, the template voice, the bench idle | 38,958 | | 14.9 | 45,959 = 17.6 % | |
| `biquad` | 64 sections x 64 samples | 135,193 | 96,235 | 36.8 | 141,922 = 54.3 % | 23.5 cycles a section-sample |
| `bands` | V2's 16 bands | 138,694 | 99,736 | 38.2 | 151,748 = 58.1 % | 97 a band-sample |
| `carrier` | 8 voices, one oscillator each | 72,011 | 33,053 | 12.7 | 82,160 = 31.5 % | 65 an oscillator-sample |
| `yin` | 10,048 squared differences | 131,492 | 92,534 | 35.4 | 145,568 = 55.7 % | 9.2 a term |
| `autocorr` | 2,176 multiply-adds, order 16 | 59,306 | 20,348 | 7.8 | 63,254 = 24.2 % | 9.4 a term |
| `sdram_seq` | 256 reads | 46,347 | 7,389 | 2.8 | 55,112 = 21.1 % | 29 a read |
| `sdram_rand` | 64 reads | 41,199 | 2,241 | 0.9 | 46,992 = 18.0 % | 35 a read |

What it changes:

- **The ceiling is broken.** Audio stays at 60 % of a block, worst case (`rt-optimize`). BANDS on V2's parts is 65.8 % on average and 72 % at worst (the highest max of `bands`, plus `carrier`'s over `off`). The second oscillator that V2a added doubles the carrier: 78 % and 86 % [est], with eight notes. It fits the block, not the ceiling.
- **The cost is instruction cycles, not memory.** The bench's data is in SRAM_U and the loop is 13 instructions. The Cortex-M4 issues one at a time, and the sum of their cycles (`vfma` 3, `vldr` 2, as recalled from the TRM) is 21 to 23, against 23.5 measured. Interleaving independent filters would hide nothing. Fewer, cheaper instructions will: V2c. Bands was estimated at 22 %, Carrier at 30 cycles an oscillator: both were about half the truth.
- **Band count and TALK's order stay at 16 for now.** Neither can be fixed from V2's parts. If V4's round is still over the ceiling after V2c, the fallbacks, in order: one section on the modulator side (48 sections, about a quarter of Bands [est]), then 14 bands.
- **TALK does not run Bands**, so it has room: autocorrelation is 7.8 % at order 16 and 11.5 % at 24 [est]. The lattice is unmeasured; V4 reads it.
- **TUNE's YIN** at 35.4 % is too much beside a carrier: V6a unrolls it or slices it over 8 blocks.
- **Loops**: the review's span copy holds, four loops' 256 reads are 2.8 % at 29 cycles a read.
- **The base is unexplained**: `stage` accounts for about 1,500 of `off`'s 38,958 cycles and shows 0 for render. V4 reads it again with the real voice in place of the template's.

## 4c. V4 on the unit, 2026-11-05

Image `3d659532e661 / o3sdk f14f247b0847`, CANDIDATE 11/11, CRC 4A194638: the real unit in the app, after V2c. Modulator: the USB return, 1 kHz at -12 dBFS, left only, or uniform white noise at the same peak. Three `cpu` reads a step, 30 reads: 0 underruns, 0 late, 0 slips, 0 hardware faults, `guard` shed 0 blocks. The unit was restored to what the round found.

The kit's budget is 60 % of a block, 156,734 cycles (`kGovernorPercent`): `over-budget` counts renders past it.

| Step | Render last, mean | Highest max | Over budget |
|---|---|---|---|
| `voc bands 0` | 119,447 | 127,557 = 48.8 % | 0 |
| `voc bands 1` | 122,437 | 135,860 = 52.0 % | 0 |
| `voc bands 4` | 137,521 | 150,103 = 57.5 % | 0 |
| `voc bands 8` | 154,243 | 167,235 = 64.0 % | 7 by the third read |
| `voc talk 0` | 111,087 | 123,942 = 47.4 % | 0 |
| `voc talk 4` | 130,922 | 144,491 = 55.3 % | 0 |
| `voc talk 8` | 150,210 | 165,124 = 63.2 % | 2 |
| `voc bands 8`, noise | 163,798 | 177,521 = 68.0 % | About every block: 1,600 between reads |
| `voc talk 8`, noise | 146,142 | 158,458 = 60.7 % | 4 |

What it says:

- **V2c worked where it can be read.** The carrier is 34 cycles an oscillator-sample, from 65: eight notes with two oscillators cost 13.3 % where V2's one oscillator cost 12.7 %. Four notes, the ordinary case, stay under the budget in both modes.
- **Bands cannot be split out.** Nothing read the base with this app: on G0's base of 38,958, Bands and the unit's shell are about 80,500 cycles, 79 a band-sample at most against V2's 97. V2c's count was 47. Either the base grew or the count was optimistic.
- **Eight notes are still over**: by 4 points at worst on a tone, 8 on noise. V2d takes the small per-sample work out, with no change of design. Band count and TALK's order stay at 16 until it is read.
- **Peaks**: the read straight after `ain` no longer carries the previous source. Line, left and right read 0.39 to 0.47 at gain 0 with the source still plugged in, L equal to R in every read: line silence and line separation stay unmeasured.
- **Owed, needing a person**: a spoken sentence the maintainer can understand, TALK's level (`kTalkGain`), the microphone.

## 4d. V2d on the unit, 2026-11-05

Image `2a1af86c1d05 / o3sdk f14f247b0847`, CANDIDATE 11/11, CRC F6373160. Same method as section 4c, 24 reads: 0 underruns, 0 late, 0 slips, 0 hardware faults, `guard` shed 0 blocks. The unit was restored to what the round found.

| Step | Render last, third read | Highest max | Before (4c) | Over budget |
|---|---|---|---|---|
| `voc bands 0` | 112,078 | 118,832 = 45.5 % | 48.8 % | 0 |
| `voc bands 4` | 128,341 | 143,798 = 55.0 % | 57.5 % | 0 |
| `voc bands 8` | 147,819 | 157,017 = 60.1 % | 64.0 % | 1 |
| `voc talk 0` | 91,550 | 107,929 = 41.3 % | 47.4 % | 0 |
| `voc talk 4` | 115,339 | 133,553 = 51.1 % | 55.3 % | 0 |
| `voc talk 8` | 131,839 | 145,334 = 55.6 % | 63.2 % | 0 |
| `voc bands 8`, noise | 154,379 | 167,665 = 64.2 % | 68.0 % | 3, from about every block |
| `voc talk 8`, noise | 133,185 | 148,486 = 56.8 % | 60.7 % | 0 |

Decisions:

- **16 bands and TALK at order 16 stay.** Every step's ordinary render is under the budget. What is left is single blocks: eight notes on white noise, the worst input there is, passed the budget 3 times in the round, and nothing was shed. The fallbacks of section 4b change the sound, so they wait for the maintainer's ears, and the optimising stops here.
- **QUALITY 24 is unmeasured**: `voc` does not set it. Eight notes at order 24 may pass the budget [est].
- **Headroom for V5**: BANDS with 8 notes has none. Four loops at 2.8 % are fine at 4 notes; the loops' round reads it.
- **V2d changed the output a little**: the follower every second sample moves the envelopes by 0.3 to 2.1 % of a band's peak, most in the top bands, and the frame level by 0.13 dB at most with the noise path off. TALK's stepped reflection coefficients move it by 2 to 4 % rms. Listening is owed on both, on unvoiced sounds above all.

## 4e. Memory after V6b, 2026-11-06

SRAM_U has about 1.2 KiB left above the link's 32 KiB free floor, and the first V6b link failed it. The next SRAM growth trips the assert: V7 starts by moving something out, for instance the loops' or Talk's cold state to the SDRAM. Autotune reads `Tune`'s native ring instead of its own 4 KB one for this reason.

## 4f. Loops and TUNE on the unit, 2026-11-06

The first unit read of both, on images eb42e3f and 55ff640 (the word reads), o3-sdk f14f247. Signals came in over the USB return at -12 dBFS peak, one player at a
time, with `pgrep -a aplay` logged before each read and `in_peak_l` 251 in every log. Loops 1 to 4 were recorded on the 1 kHz tone and played on the carrier side. An
earlier pass on eb42e3f leaked player processes, so which signal it read cannot be proved: its numbers are discarded.

Mean is of three `render last` reads, peak is the window's `max`, of 261,224 cycles a block. The budget is 156,734 cycles, 60 %.

| Case | Signal | eb42e3f mean | eb42e3f peak | 55ff640 mean | 55ff640 peak |
| --- | --- | --- | --- | --- | --- |
| BANDS 8 notes, 4 loops 1x | tone | 161,917 | 66.8 % | 159,358 | 65.1 % |
| BANDS 8 notes, 4 loops 1x | noise | 165,168 | 66.2 % | 163,518 | 67.3 % |
| BANDS 8 notes, 4 loops 2x | tone | 176,980 | 71.6 % | 171,907 | 71.9 % |
| BANDS 8 notes, 4 loops 2x | noise | 181,272 | 75.0 % | 177,051 | 72.1 % |
| BANDS 8 notes, 4 loops half, reverse | noise | not read | not read | 171,280 | 69.8 % |
| TALK 8 notes, 4 loops 2x | noise | 164,610 | 65.8 % | 157,085 | 65.7 % |
| TUNE, 4 loops 2x | noise | alternates | 57.1 % | alternates | 55.0 % |

On eb42e3f, with no loops: TUNE peaked at 51.7 % on the 1 kHz tone, 52.7 % on 233 Hz and 46.6 % on noise, its `render last` alternating between about 65,000 and 128,000
cycles. BANDS with no notes and the loops halted peaked at 47.4 %. All 51 reads: 0 underruns, mutes, late blocks, slips or hardware faults, 0 blocks shed, no crash.

What it says:

- TUNE fits the budget, alone and under four loops at 2x.
- Four loops recorded, played, changed role, speed and direction with no early stop or wrong state. Nothing has been listened to yet.
- BANDS at 8 notes with four carrier loops is over budget on every block, on tone and on noise. Its worst peak is 72.1 %.
- TALK at 8 notes with four loops sits on the budget, so a small gain moved its over-budget count a long way: 7,311 to 2,498 at the third read. That count is a threshold, not a
  measure of how much was saved.
- The word reads saved 1,700 to 7,500 cycles a block, against an estimate of 8,000 to 16,000. The estimate priced a read per bus access, but the SDRAM port is 16 bits
  (`sdramc_config.h`), so a word is still two beats: the likely reason, not measured. Peaks move about a point between windows (the 1x noise peak rose while its mean fell),
  so compare the means.

Decision: optimising stops here, as in section 4d, and the word reads stay. Capping notes cannot reach 60 %: a note is about 1.3 points (section 4d) and the excess is 12. What
is left is fewer loops on the carrier side or fewer bands, and both are sound choices that wait for the maintainer's ears. The UI and MIDI were not measured at this load.

## 4g. The rest of the unit reads, 2026-11-06

Image b1283df on o3-sdk ef6b1a0, same method as section 4f. Mean is of three `render last` reads, peak is the window's `max`. All 18 reads: 0 underruns, mutes, late blocks,
slips or hardware faults, 0 blocks shed, no crash.

| Case | Signal | Mean | Peak | Over budget |
| --- | --- | --- | --- | --- |
| BANDS 8 notes, 4 loops 2x | noise | 176,893 | 72.9 % | every block |
| TALK 8 notes, 4 loops 2x | noise | 158,685 | 64.6 % | about a third of blocks |
| TALK 8 notes, QUALITY 16, no loops | noise | 137,238 | 59.3 % | none |
| TALK 8 notes, QUALITY 24, no loops | noise | 159,304 | 66.0 % | about a third of blocks |
| TALK 8 notes, QUALITY 24, 4 loops 2x | noise | 189,488 | 74.4 % | every block |
| BANDS 4 notes, 3 carrier steps, transport running | tone | 120,868 | 50.4 % | none |

- The kit's move from f14f247 to ef6b1a0 changed nothing beyond the reads' spread. It took 512 B of SRAM_U, which leaves about 0.7 KiB above the floor.
- QUALITY 16 stays the default: it is the only TALK order inside the budget at 8 notes. QUALITY 24 costs about 8 points.
- The per-step carrier (V5c) reached `carriers=3` from the console and ran with the transport. Clicks at a switch are for the ears.
- The main loop gets what the audio leaves. `shot` took 4.6 s with the render near 46 % and 9.2 s near 73 %, the same 2:1 as the free time. `state` round trips went from 1.9 ms to 2.8 ms
  mean and 3.9 ms to 6.0 ms max. Key and pad commands were acknowledged in 1 to 4 ms and none was lost. So at the worst case the UI runs at about half speed; whether that shows needs eyes.
- `midi` reports the kit's service in the image with no traffic. The app has no MIDI code of its own, and whether notes reach the vocoder is untested.
- `usb` counted 1 output underrun over 7 stream starts during the session. When it happened is unknown, and nothing in `cpu` or `log` matches it. It stays open.

## 4h. V2e, V2f and PERFORM on the unit, 2026-11-06

Image 1bb3c54 on o3-sdk ef6b1a0, CRC 1092751B, tester o3-303-b1. Three reads a row after `cpu reset`; mean and peak of `render last`, 261,224 cycles a block, budget 60 % = 156,734. Shed 0 and underruns 0 in every row. Noise from USB at -12 dBFS.

| Row | Mean | Peak | Peak % | Over budget |
|---|---|---|---|---|
| Idle on PERFORM, no player | 93.6k | 106.9k | 40.9 | 0 |
| BANDS 8 notes alone | 141.3k | 149.8k | 57.3 | 0 |
| BANDS 8 + 4 loops x2, noise | 166.6k | 175.8k | 67.2 | every block |
| The same, FORMANT +12 | 172.2k | 182.7k | 69.9 | every block |
| The same, FORMANT -12 | 172.4k | 186.4k | 71.3 | every block |
| The same, FORMANT 0, THRU 100 | 169.2k | 180.0k | 68.9 | every block |
| The same, FORMANT 0, THRU 0 | 167.4k | 177.5k | 67.9 | every block |
| TALK 8 Q16, loops halted | 138.6k | 150.6k | 57.6 | 0 |
| TALK 8 Q24 + 4 loops x2 | 179.6k | 191.7k | 73.3 | every block |
| TUNE alone | 99.2k | 118.4k | 45.3 | 0 |

- **V2e saved 5.7 points of peak** on the worst BANDS case: 72.9 % on b1283df, 67.2 % here. Row 2's mean, 63.8 %, is still past the 60 % budget, so every loop row counts over budget on every block. Nothing passed 75 %.
- **V2f:** FORMANT costs 2.7 to 4.1 points, THRU 1.7, and both come back to the first figures at 0. The ears are owed.
- **PERFORM at row 2's load:** `state` mean 2.3 to 2.5 ms, max 5.9 (it was 1.9 to 2.8 mean); key and pad acks 1 to 6 ms, none lost; 69.2 % peak with three screenshots and key and pad taps. A chord pad sounded its notes and lit.
- **Open: one peak of 245,294 cycles, 93.9 %,** no shed, no underrun, in a run that stepped MODE through BANDS, TALK and TUNE with F1 and took three screenshots. A rerun without the MODE key peaked at 69.2 %. Not isolated. The likely cause is the mode crossfade, during which two engines run (`runs()` in `Vocoder::process`), but that is unmeasured. If it is, a MODE change at TALK Q24 with loops could pass 100 %. The next round times `voc talk` and `voc tune` at this load with no screenshots, and screenshots with no mode change.
- **Project save and load with the buffers in the SDRAM: passed.** Four steps saved, reset, deleted, loaded back.
- `files=failed` shows after boot with no config.o3a on the card, probably the quiet config load; not confirmed.
- The console has no main-loop pass rate and cannot delete a card file: `pattern.o3a` and `.bak` of this round stay in `/Apps/o3_voc03er/`, and the next boot of this app loads them.

## 4i. U2 to U3 on the unit, 2026-11-06

Image e0ac557 on o3-sdk ef6b1a0, CRC D33D8674, SHA-256 81b41127...3013, tester o3-303-b1, raw log kept outside the repository. No hang, 0 underruns, 0 shed in any row. Means and peaks of `render last` computed from the raw log; budget 60 % = 156,734.

| Row | Mean | Peak | Peak % |
|---|---|---|---|
| BANDS 8 notes, 4 loops 2x, noise | 166.7k | 176.6k | 67.6 |
| TALK, the same load | 164.6k | 182.1k | 69.7 |
| TUNE, the same load | 139.9k | 154.2k | 59.0 |
| BANDS at that load, with screenshots | 168.1k | 181.2k | 69.4 |
| The same, GATE 3 | 167.4k | 178.0k | 68.1 |
| The same, ARP 1 | 142.2k | 153.3k | 58.7 |
| The same, GLIDE 100 | 163.4k | 172.5k | 66.0 |
| USB L > USB R, both LIVE, GATE 3 | 114.7k | 124.5k | 47.7 |
| A capture, no loops playing | 111.6k | 121.4k | 46.5 |

The worst case stays over the 60 % budget on the mean, as in 4g, and below that round's 72.9 %. GATE costs about 3k, GLIDE nothing visible. ARP reads about 22k lower because it sounds one note of the chord at a time. The 93.9 % peak of the last round did not come back with mode changes or screenshots, so its cause stays unknown.

Read on the unit: the buttons and HELP; USB notes, bend and CC1 reaching the app; the lists of F2 and F3, the dimmed rows and the armed chip; a capture at the bar with the ADC moving right after it; USB L and USB R on the header.

- A capture with the transport stopped gave 192,000 samples against 176,400 running: 2 bars at 48 kHz. That is the `bar_samples` error, fixed in 8f12755, and this image predates it. To read again.
- USB L noise with a silent USB R still gave output at -4 dB. By the code this is the unvoiced path (`bands.cpp`), which adds its own noise to the upper bands when the modulator is mostly high: a sibilance is heard over a silent carrier by design. Not measured on the unit. A tone on the left with a silent right is silent on the host.
- `files=failed` after boot is the quiet load of a `config.o3a` that is not on the card yet.
- The USB hub 3-1.4 dropped at 13:38:19 during the first upload and reset the unit (0x0082, power-on + low-voltage); the retry installed cleanly.

Tool findings, not fixed: `o3console --put` spins at 100 % CPU on a deleted tty instead of exiting; `--update` refuses a path over 30 characters; `state` reports screen=PERFORM on every screen the tester read.

Owed to the maintainer's ear and hand: glides, gate edges, vibrato and MOD, the eight presets' levels, clicks at the capture switch, the carrier sound, the wheel against the spring, BEND ROW, the formant steps, VOL, ROOT and BND on KEYS (no visible effect on screen).

## 4j. The engines' rework on the unit, 2026-11-06

Candidate 6a19f36 (BANDS with the slow AGC, TALK at fs/4), kit ef6b1a0, SRAM_U margin 10,504 B. Rows 20 to 24, 1, 2, 3 and 10 were read over USB; rows 4 to 9 and 11 to 19 need hands, an output capture or the maintainer's ears, and stay owed. The figures are means of `render last`; the full table and the reading of them are in `docs/ENGINE-REALISM.md`.

- **CPU.** BANDS with 8 notes and 4 loops 170k, 69.5 % at its peak, unchanged. TALK at `Quality` 16 with the same load 160k, mean about 61 %, peak 67.0 %: TALK did not fall as estimated, and the estimate in `talk.h` was wrong. `Quality` 24 with loops 171k, 72.2 %, every block over budget. Underruns 0 and shed 0. The worst peak of the round was 230,435 cycles, 88.2 %.
- **The 93.9 % of 4h is explained.** Single blocks of 76 to 88 % on each change of MODE: the crossfade runs both engines, for one block's peak, and nothing hung. The 3 dB level check of that row was not run.
- **AGC: inconclusive.** The OUT meter on a spoken signal, 10 s after each change, read -34, -28 and -6 dB in BANDS and -19, -29 and -5 dB in TALK for inputs at -6, -18 and -30 dBFS. The host's real engines are flat within 0.3 dB over the same range, on speech and on a sine. To read again with a steady signal and repeated reads.
- **THRU** costs nothing in TALK (132.5k, 133k, 131.7k).
- **A stopped capture reads 176,448, not 176,400.** The app polls the length once a block, and 176,448 is the first whole block past the target, so this is the smallest a poll can give. The take is 1.09 ms long, and a loop does not resync at the bar, so it would drift by that much every 2 bars once the transport runs. U7c2a: SAMPLE REC now sends Load (then 0) with the exact length after a stopped, auto-stopped take; a length cut short gets a new fade at its end (`Loop::adopt`), since the one `end_record` baked is cut off.
- **Row 10:** `pat`, `len`, `note`, `lvl`, `slc` and the bank print work. `pat A 3 car 2` was a mistake in the row: `car` takes a word, `follow`, `synth`, `noise`, `s1` to `s4`. The usage line now says so.
- The tester's found state was restored: the image of the session that holds the unit, one `.ptf` in `Firmware/`. `live.o3a` and `pattern.o3a` were not copied in this round, only their sizes read; the image changes no format.
- **Console findings:** `o3console --update` rejects a path over 30 characters; `state` reports `screen=` unreliably; a screenshot lags one redraw; `o3console --put` spins on a deleted tty.

## 4k. TALK's autocorrelation on the unit, 2026-11-06

Candidate 37fa85f, kit ef6b1a0, SRAM_U margin 10,408 B. The figures are in `docs/ENGINE-REALISM.md`.

- **TALK at `Quality` 16 with 8 notes and 4 loops: 142.8k, down from 160k,** inside the budget of 156,734 by its mean, one block over it. `Quality` 24 150.3k, 492 blocks over. BANDS at the same load 165.4k, still over at every peak: accepted by the maintainer, its limit is now 66 % at the peak. Underruns 0, shed 0, worst peak 70.3 %.
- **AGC: passed at the meter's resolution** on a steady vowel: within 2 dB over 24 dB of input. The spoken reading of 4j does not stand.
- **`pat A 3 car synth`** sets the step and a wrong word prints the words.
- **SAMPLE EDIT's picture was not read.** The tester stayed on the old PLAY screen, which keeps the Sample keys for itself until U9, and reported that the image had no such screen. It has: open PERFORM first. The chunked upload on the FT812 is still owed.

## 5. How the work runs

Lanes of one or two subagents, Sonnet for code and review, Haiku for mechanical checks. A brief names the files; agents use LSP and targeted reads
instead of reading whole files. The unit is shared: a round asks for the turn first (the maintainer named the session that keeps it). Commit after each
verified slice, adding files by name; push and tag only when the maintainer asks.
