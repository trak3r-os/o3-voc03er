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
| G0 | Microbench on the unit: the engine's own share, the real `Bands` and `Carrier`, a biquad bank, the YIN and autocorrelation inner loops, SDRAM reads | Numbers from `cpu` on the unit; they fix the band count and TALK's order before V4 | Done (section 4b), on V2's code; bands and order stay at 16 until V4's round |
| V2a | `Carrier`: second oscillator a voice (DETUNE), noise shared with the unvoiced path | Host tests, no doubles | Done on the host |
| V2b | `Bands`: the review's geometry and followers, high-pass and AGC, the unvoiced path | Host tests, summed bands flat within 3 dB in a sweep, no doubles | Done on the host |
| V2c | `Bands` and `Carrier` in fewer instruction cycles (section 4b): same sound, no fused multiply-adds in the sample loops, `b0` folded out of the sections, envelope segments out of the oscillator loop | Host tests unchanged, no doubles, each sample loop's instructions counted before and after; the cycles are read in V4's round | Next |
| V3 | `Talk`: streaming lattice, analysis sliced across blocks, freeze-a-vowel | Host tests, no block spike, the carrier path not delayed | Done on the host, no doubles; its CPU, its level (`kTalkGain` is a placeholder) and listening are owed to V4's round |
| V4 | The unit in the app: PLAY screen with the input peak, pads play chords, MODE and the source on the keys, line sources start at gain 0, `ain` clears its peaks, a scenario | Scenario, image, CPU of each mode measured on the unit, a spoken sentence intelligible to the maintainer | Built on the host and for mk66 (2026-11-05): the Vocoder attached as slot 0, PLAY screen with IN peak / CLIP, MODE and SOURCE (PLAY and SAMPLE PLAY keys, F1 is MODE), pads play chords, line sources at gain 0, `ain` clears its peaks (app-side meter reset plus a 300 ms holdoff, no kit change), console `voc`; the unit round (CPU of each mode, line silence, a spoken sentence) is owed |
| V5a | Loops: record, play, roles, bar lines from the transport | Host tests, scenario, unit | |
| V5b | Loops: speed and reverse, the span copy | Host tests, unit | |
| V5c | Per-step carrier switch | Host tests, scenario, unit | |
| V6a | TUNE: detector and gate | Host tests against synthetic voices and three sung phrases; aubio as a host oracle only, never shipped | |
| V6b | TUNE: shifter, snap, retune | Host tests, unit | |
| V7 | The 2040 layer, slice by slice | | |

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

## 5. How the work runs

Lanes of one or two subagents, Sonnet for code and review, Haiku for mechanical checks. A brief names the files; agents use LSP and targeted reads
instead of reading whole files. The unit is shared: a round asks for the turn first (the maintainer named the session that keeps it). Commit after each
verified slice, adding files by name; push and tag only when the maintainer asks.
