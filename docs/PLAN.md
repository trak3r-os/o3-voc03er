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

## 2. Shape

- One Instrument unit, `units/vocoder/`. `in_l` / `in_r` are the modulator, notes play the carrier. It depends on `dsp` only.
- Its parts are plain classes with no engine knowledge, each with a host test: `Carrier`, `Bands`, `Talk`, later `Loop`, `Tune`.
- Audio is at 44.1 kHz in 64-frame blocks. The render budget is 261,224 cycles a block; no per-block spikes (heavy analysis is sliced across blocks).
- State lives in the unit. Big buffers go in the SDRAM with `O3SDK_BIG`; hot state stays in SRAM.

## 3. Order of work

| Slice | What | Done when | State |
|---|---|---|---|
| V0 | The repository from the template, this plan | Host green | Done |
| V1 | The audio-in gate on the template app: console `ain <source> [gain]`, input peaks in `state` | Host test, gate CANDIDATE, unit round run | First round run (section 4); line silence, radio and microphone owed |
| V2 | `Carrier` and `Bands` (16 bands) in `units/vocoder/`, against a synthetic voice | Host tests, no doubles | Done on the host; CPU and a listening check owed at V4 |
| V3 | `Talk` (mda Talkbox DSP, autocorrelation sliced across blocks) | Host tests, no block spike | |
| V4 | The unit in the app: PLAY screen, pads play chords, MODE and the source on the keys, a scenario | Scenario, image, CPU of each mode measured on the unit | |
| V5 | Loops: record, play, speed, reverse, roles, per-step carrier switch | Host tests, scenario, unit | |
| V6 | TUNE | Host tests against synthetic voices, unit | |
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
- Owed in the next round, batched: line silence (the maintainer stops the source), a radio station with an antenna, the microphone with a person at the unit.

## 5. How the work runs

Lanes of one or two subagents, Sonnet for code and review, Haiku for mechanical checks. A brief names the files; agents use LSP and targeted reads
instead of reading whole files. The unit is shared: a round asks for the turn first (the maintainer named the session that keeps it). Commit after each
verified slice, adding files by name; push and tag only when the maintainer asks.
