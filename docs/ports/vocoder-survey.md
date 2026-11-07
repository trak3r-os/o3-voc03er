# Open-source channel vocoders: what `Bands` could learn or reuse

Researched 2026-11-06. Nothing cloned, built or run; every source was read on the web, mostly through a fetch tool that summarises the page, so details marked "as read" come from that summary and not from my own reading of the raw file. TALK (LPC, after mda Talkbox) and TUNE are covered by the cards in `../o3-sdk/docs/ports/` and are not repeated.

## 0. Where we stand (facts, from the repo)

- `units/vocoder/src/bands.cpp`: 16 bands, centre 125 Hz x 2^(0.4 i) (125 Hz to 8 kHz), 0.4 octave apart. Per band, two cascaded RBJ bandpass sections (Q 2.3) on the modulator and two on the carrier, transposed direct form II, float. Rectified second-section output into a one-pole follower with separate attack and release, updated every second sample; release is slower for bands 0 to 2. Front end: 80 Hz high-pass, AGC with gate and knee, pre-emphasis 0.7. Unvoiced path: noise added to the carrier of bands 11 to 15 when the high-band ratio of the followers crosses 0.35 (closes at 0.28), faded over 5 ms.
- Measured on the unit (`docs/PLAN.md` sections 4b to 4d): `bands` 97 cycles a band-sample and 38.2 % of a block on G0's parts; Bands plus the unit's shell about 80,500 cycles (79 a band-sample at most, about 31 % of a block) on V4's image; the bench's `biquad` 23.5 cycles a section-sample; the whole app peaks at 60.1 % with 8 notes after V2d. The FPU's `vfma` costs 3 cycles, which is why the code uses separate multiply and add (`mulb`).
- Licence bar: MIT, BSD, Apache-2.0, GPL-2.0-or-later, GPL-3.0 usable; GPL-2.0-only not.

## 1. Cards

### 1.1 Mutable Instruments Warps, vocoder mode

- URL: https://github.com/pichenettes/eurorack (directory `warps/dsp`: `vocoder.cc`, `vocoder.h`, `filter_bank.h`, `filter_bank.cc`; filters in the `stmlib` submodule, https://github.com/pichenettes/stmlib). Read from the `master` branch; I did not obtain a commit hash.
- **Licence: MIT** (permissive). Read in: the README's License section ("Code (STM32F projects): MIT license"; the AVR projects are GPL-3.0, not this one) and the headers of `vocoder.cc`, `vocoder.h`, `filter_bank.h`, `filter_bank.cc` and stmlib's `dsp/filter.h` ("Copyright 2014 Emilie Gillet", MIT text), as read. A top-level LICENSE file returned 404. The README asks derivatives not to use the Mutable Instruments name or the module names. https://github.com/pichenettes/eurorack
- Algorithm (as read):
  - **20 bands** (`kNumBands`), each analysed on modulator and carrier by its own filter bank. Per band, two `CrossoverSvf` objects in series; `CrossoverSvf` is itself two identical Chamberlin-type SVF stages (Linkwitz-Riley style), so about four SVF stages a band a path. First band low-pass, last band high-pass, the rest normalised band-pass. Centres and Q are in a generated table (`resources`), which I could not read: **band spacing is unverified.**
  - **Decimation**: bands are grouped by decimation factor; the bank has two sample-rate converters, a factor 3 stage and a factor 4 stage (so groups at 1, 3 and 12, as read from the constants; how many bands sit in each group is unverified). Low bands run at 1/12 of the rate and the bank is rebuilt with delay lines so all bands line up in time.
  - **Follower**: per band, rectified input scaled by `sqrt(kNumBands)`, one-pole with attack set to twice and decay to half the base rate; the base rate runs over a six-octave range with the release control and rises a third of an octave per band (higher bands faster). A freeze flag (release near maximum) sets both coefficients to zero and holds the envelope.
  - **Formant shift**: each output band reads the follower peaks at a fractional position between two neighbouring bands (linear interpolation); the control scales the read step, so the spectral envelope is stretched or compressed. A crossfade between a "vocoder" gain and a static "carrier" gain per band follows from the same control. Per-band gains ramp linearly across the block.
  - Output limiter. No unvoiced or sibilance path in the file.
- Float: all `float`, no `double` (as read). No heap visible in these files; filter bank buffers are fixed arrays (6,144-float shared delay pool, 960 + 192 floats of scratch, about 29 KB of explicit buffers). Block size capped at 96 frames.
- Cost [est]. Undecimated, one band-sample is about 16 multiplies and 20 adds a path from the `CrossoverSvf` tally (8 multiplies, 10 adds a stage, two stages, two objects = 16 and 20), say 40 cycles a path, 80 for both, about 100 with follower and gain ramp. 20 bands undecimated would be 2,000 cycles a sample, 128,000 a block, 49 % of 261,224. With an assumed split of 10 bands at full rate, 6 at 1/3, 4 at 1/12 (my assumption, not read) about 1,230 a sample, 79,000 a block, 30 %. So Warps' bank is not cheaper than ours before decimation; the gain is entirely in the decimation.
- Ideas ours lacks: (1) running the low bands at a lower rate; (2) formant shift by fractional band read with per-block gain ramps.
- Fit: MIT, C++ with `float`, no heap: portable in principle, but the block size is 96, the filters are heavier than ours, and the SRC and delay-line alignment come as a package. Take the ideas, not the code.

### 1.2 mda Vocoder (Paul Kellett), mda-vst

- URL: https://sourceforge.net/p/mda-vst/code/HEAD/tree/src/mdaVocoder.cpp
- **Licence: dual MIT / GPL-2.0-or-later, licensee's choice** (permissive in effect). Read in `readme.license` at the repository root: "The mda VST plug-ins are released under the MIT license or under the GPL", the GPL option "either version 2 of the License, or (at your option) any later version" (https://sourceforge.net/p/mda-vst/code/HEAD/tree/readme.license?format=raw). `mdaVocoder.cpp` carries only a "Copyright Paul Kellett (maxim digital audio)" notice and a VST SDK line, no per-file grant (as read); the repository-level file is the grant, as in the existing `mda-talkbox.md` card, which has the KVR confirmation from the author.
- Algorithm (as read):
  - **16 bands** (a switch gives 8). Band 0 is a separate high-band path; bands 1 to 15 centres 5000, 4000, 3250, 2750, 2300, 2000, 1750, 1500, 1250, 1000, 750, 540, 350, 195, 95 Hz (a table, uneven, closer in the 1 to 3 kHz region), all scaled by a "Mid Freq" control (2^(3p-1)).
  - **Filter**: per band, two cascaded **all-pole two-pole resonators** per path, `y[n] = a0 y[n-1] + a1 y[n-2] + x[n]`, 4 poles a band; the second resonator is detuned to 0.96 of the first to flatten the peak; common pole radius from a "Filter Q" control. The input of both paths first goes through a **second difference shared by all bands** (double zeros at DC) and the modulator additionally through a first difference. Coefficients are computed in `double` with `acos`, `cos` and `pow` in `resume()` only, stored as `float`.
  - **Follower**: `|y|` into a one-pole, attack equals release; the rate is capped per band, `min(k0, 0.025 - 0.022 i / N)`, so **bands at low frequencies get slower envelopes in a smooth gradient**. Freeze if the control is near zero.
  - **Half-rate bank**: the 15 resonator bands run only on every second sample and the summed output is held for the other one (as read; no anti-alias filter on the input is visible, and my own reading is that realised centres land at about half the table values unless the table was tuned for it; I have not checked).
  - **High band / sibilance**: band 0 uses the first-differenced modulator and its own follower; output term is `env0 x (HiThru x d + HiBand x (carrier - carrier[-1]))`: that is the modulator's own high-passed signal ("Hi Thru") and the carrier's high-passed signal ("Hi Band"), both gated by the modulator's high-frequency envelope. There is no noise generator.
  - Denormals: states zeroed when stage-1 output under 1e-10; blow-up guard (output over 10) resets states.
- Float: per-sample state `float`; setup in `double`. No heap, no libm in the sample loop (as read).
- Cost [est]: per band-update about 21 flops; as separate multiply and add that is about 30 cycles with loads and stores, every second sample = 15 cycles a band-sample. 15 bands = 225 plus about 30 for the high band and shell = 255 cycles a sample = 16,300 a block = **6.2 % of a block**. About a quarter of our 79 a band-sample; the gap is mostly the half rate and the 2-FMA-chain resonators, not a better filter.
- Ideas ours lacks: (1) **modulator thru in the high band**, gated by its own envelope, next to (not instead of) noise; (2) **graded follower rate by band** (ours has a step at band 3); (3) all-pole resonators with the zeros pulled out into one shared difference: 4 arithmetic ops a section against our 5 (sub, 2 multiply, 2 add), at the price of numerical care for poles near z = 1 at 125 Hz [est: about 20 % of the filter arithmetic; the bench decides].
- Verdict: **reimplement ideas**; the MIT option would also allow porting the loop, but ours is already a different structure.

### 1.3 Surge XT vocoder effect

- URL: https://github.com/surge-synthesizer/surge (`src/common/dsp/effects/VocoderEffect.cpp`, `VocoderEffect.h`)
- **Licence: GPL-3.0-or-later** (compatible). Read in: the header of both files ("released under the GNU General Public Licence v3 or later (GPL-3.0-or-later)", "Copyright 2018-2024, various authors", as read) and the repository LICENSE (GPL v3 text).
- Algorithm (as read): **20 bands** (`n_vocoder_bands`), active count rounded to a multiple of 4; band centres log-spaced between Min and Max in semitones, or the default hand-tuned table from the VSM-201 (about 180 Hz to 7.5 kHz); `Q = 20 x (1 + 0.5 q)`; each band a `VectorizedSVFilter` used as a band-pass (`CalcBPF`), four bands a SIMD vector, so five vector filters a path; the filter's internal order is **not verified**. Follower: modulator band, squared, clamped to a maximum, gated against a threshold from Gate and Gain, one-pole smoothed (`Rate = 0.001 x 2^(4 Env Follow)`), then a fast square root back to amplitude. The carrier is multiplied by that envelope **before** its band-pass and the bands are summed. Coefficients refreshed every 64 samples. Voiced/unvoiced detection code is present but commented out. `float`, a few `double` literals in setup; SSE/NEON abstraction (`vFloat`).
- Cost [est]: the Cortex-M4F has no float SIMD, so the four lanes become scalar. If one SVF stage a path: 20 bands x 2 paths x about 10 cycles + 20 square roots (`vsqrt.f32` about 14 cycles) + sum = about 900 cycles a sample = 58,000 a block = 22 %, more if the filter is a cascade. Not cheaper than ours.
- Ideas ours lacks: the **noise gate on the squared band level** (a per-band floor under which the envelope is zero, so filter noise and hum between words do not open bands) and the sqrt-of-power follower (RMS-like detection against our mean-abs rectifier). Neither is a clear win for us: our AGC gate already acts on the whole analysis path, and a per-band gate is 16 compares a sample (about 1.0k cycles a block, 0.4 % [est]) for an effect I cannot judge.
- Verdict: **inspiration only** (GPL-3.0-or-later would allow a port, but the code is SIMD-shaped and gives nothing cheaper).

### 1.4 Calf Studio Gear, Vocoder

- URL: https://github.com/calf-studio-gear/calf (`src/calf/modules_filter.h`, `src/modules_filter.cpp`, class `vocoder_audio_module`)
- **Licence: LGPL-2.1-or-later** for the file read: `modules_filter.h` header, "version 2 of the License, or (at your option) any later version", copyright Foltman, Schmidt, Johansen and others (as read). The repository also carries a GPL text; I did not map which files fall under it; check the header of the exact file before any use. LGPL-2.1-or-later is compatible with a GPL-3.0-or-later kit.
- Algorithm (as read): 8, 12, 16, 24 or 32 bands, log-spaced between a lower and upper frequency with a tilt control; **an RBJ band-pass biquad per band, with the number of cascaded biquads user-set up to 8** ("order"), the same on both paths, coefficients shared across stages; follower is `|x|` with separate attack and release in ms (coefficient reaches 1 % of the target in that time); the follower's output scaled by a compensation term that grows with order; **per-band noise level** (uniform `rand()` in [0, 1], not zero mean, added to the carrier before the band filters, one noise pair a sample for all bands); per-band solo, volume, pan; a stereo link of the two detectors. No sibilance path.
- Float / heap: parameters and buffers `float`, **the per-sample audio path is `double`** (as read): unusable on the M4F as written (software double). No SIMD seen.
- Cost [est]: structurally ours at order 2; with `double` and 32 slots of LED/meters it is far above; not worth an estimate beyond that.
- Ideas ours lacks: per-band noise level (our noise goes to bands 11 to 15 at one level; a per-band slope is 5 constants, free). Order as a control is a quality knob we can already emulate with the section count.
- Verdict: **inspiration only**.

### 1.5 Faust `ve.vocoder` (faustlibraries)

- URL: https://github.com/grame-cncm/faustlibraries (`vaeffects.lib`, function `vocoder`, author Romain Michon)
- **Licence: LGPL-2.1-or-later** for the GRAME section the vocoder sits in: header "Except where noted otherwise, Copyright (C) 2003-2017 by GRAME", the Lesser GPL "version 2.1 ... or, at the user's option, any later version" (as read; the STK-4.3 licence in another section of that file does not cover the vocoder). Compatible. https://raw.githubusercontent.com/grame-cncm/faustlibraries/master/vaeffects.lib
- Algorithm (as read): `vocoder(nBands, att, rel, BWRatio, source, excitation)`: **log-spaced bands, 9 octaves from 25 Hz** (`25 x 2^((band+1) x 9/nBands)`), bandwidth the band's width times a ratio (0.1 to 2), one `fi.resonbp` per band per path (2nd order), `an.amp_follower_ar(att, rel)` per band, carrier bands scaled and summed. No pre-emphasis, no unvoiced path. The doc calls it "a very simple vocoder".
- Float: Faust compiles to `float` by default or `double` on request; nothing there to port (generated code, not a source). Internals of `resonbp` and `amp_follower_ar` **not read**.
- Cost [est]: 16 bands x 2 second-order sections x about 25 cycles + followers, about 960 cycles a sample, 61,000 a block, 23 %.
- Ideas ours lacks: none. It is a teaching vocoder, simpler than ours.
- Verdict: **inspiration only**; nothing to learn.

### 1.6 sonoTron (Teensy 3.6 vocoder)

- URL: https://github.com/ErikDorstel/sonoTron
- **Licence: GPL-3.0**: the repository sidebar and a `LICENSE` file in the root (as read). Whether the source headers say "or later" I did not see, so treat it as **GPL-3.0-only until the headers are read**; that is not a problem for reading, but code from it could not simply join a GPL-3.0-or-later tree.
- Why it matters: the Teensy 3.6 is the same MK66FX1M0 at 180 MHz as the Tracker. Facts from the README: **16 bands**, log-spaced, cascaded filters with a 24 dB/octave slope, built on the Teensy Audio Library blocks (filters and peak measurement), pink/white noise sources on both paths, MIDI control of filter resonance, envelope speed and limiter. "The CPU usage of a Teensy 3.6 is up to 80 percent." The page gives no conditions or method.
- Cost: the 80 % claim is unverified (no stated sample rate, block size or load); it is the only published datapoint for a 16-band bank on this CPU, and ours (31 to 38 % of a block, measured, before the V2d trim) is under half of it. The Audio Library's filters are fixed-point CMSIS biquads (my knowledge, not read here), a different route from our float one.
- Ideas ours lacks: none visible. No algorithm description beyond the above.
- Verdict: **inspiration only** (a datapoint).

### 1.7 yu2924 channelvocoder and cwilso/Vocoder

- URLs: https://github.com/yu2924/channelvocoder (JUCE plug-in) and https://github.com/cwilso/Vocoder (Web Audio).
- **Licence: MIT** for both, stated in the README and a LICENSE file in each repository (as read; licence text itself not read).
- channelvocoder (as read from the README): 16 bands, 1/3-octave spacing, band-pass banks, "each band a second-order pair, built by cascading first-order band-pass pairs", the carrier gain per band controlled by the modulator's level; follower design not described; white noise can be mixed into the high-frequency channels "to emphasise sibilant sounds" (the README's wording, in Japanese terms). cwilso: 28 bands, variable. Both are built on desktop frameworks (JUCE, Web Audio nodes), no embedded concerns.
- Ideas ours lacks: none; the manual noise-into-HF idea is what ours already does, with an automatic detector.
- Verdict: **inspiration only**; the repositories don't document anything beyond the README.

### 1.8 Looked at and set aside

- **LADSPA / LV2 "vocoder" (Achim Settelmeier, Josh Green, Nedko Arnaudov)**: https://sources.debian.org/src/lv2vocoder/1-3/vocoder.c. 16 bands, two cascaded SVF stages a path, one-pole follower with a table of coefficients. **GPL-2.0-only**: the header read says "the Free Software Foundation; version 2 of the License" with no "or later" (as read). Not usable; not studied further.
- **Bespoke Synth Vocoder**: GPL-3.0 per a Debian listing of `Source/Vocoder.h` (https://sources.debian.org/src/bespokesynth/1.3.0%2Bdfsg-2/Source/Vocoder.h), reported by a search result and not opened; it is FFT-based by my recollection (not verified) and an FFT in a block is excluded by our brief.
- **VocoGroot** (Teensy plus Faust, https://github.com/andeuxun/VocoGroot): licence field empty in the result I saw. Skipped for lack of a clear licence.
- **BogAudio, ZynAddSubFX**: I looked for a vocoder in both and found none (ZynAddSubFX's `src/Effects` listing has no Vocoder file; no BogAudio vocoder turned up). Nothing claimed.
- **Closed products**: skipped.

## 2. What the survey says about `Bands` (analysis, not facts)

- Every design read here is a bank of bandpass sections on both paths with a one-pole follower per band: the same structure as ours. None has an AGC, a hysteretic unvoiced detector or a gated noise path like ours; the nearest are mda's high-band thru and the noise-into-high-bands of Calf and channelvocoder, which are static.
- On cost, only two ideas appear that ours lacks: **running part of the bank at a lower rate** (Warps by 3 and 12 with real SRC; mda by dropping every second input sample) and **cheaper per-section arithmetic** (mda's all-pole form). Everything else (Surge, Calf, Faust) is as heavy or heavier.
- Nothing found replaces our design. What follows are tunings.

## 3. Ranked: worth trying in `Bands`

All costs are *estimates* until the unit measures them. The baseline for arithmetic is 79 cycles a band-sample (V4's upper bound), 16 bands x 64 samples x 79 = 80,900 cycles a block (31 %).

1. **Run the low bands at half rate (decimate by 2).** Bands 0 to 12 (125 Hz to 3.5 kHz) between a polyphase-allpass halfband decimator on the pre-emphasised modulator and one on the carrier, and one halfband interpolator on the sum of the 13 bands; bands 13 to 15 stay at full rate.
   - Benefit [est]: 13 x 64 x 79 / 2 = 32,900 cycles saved a block (12.6 %).
   - Cost [est]: three halfbands, a 4-coefficient polyphase IIR about 25 cycles per output sample each, 32 outputs a block: about 2,400 cycles (0.9 %) plus buffers. Net about 11 % of a block, which is the whole of the 8-note headroom problem in `PLAN.md` 4d. The follower already runs every second sample, so the follower side is unchanged.
   - Risks: aliasing and imaging from the transition band of a cheap halfband; group delay differing between the half-rate and full-rate groups (Warps lines its groups up with delay lines; here the difference is a few samples in an effect that sums bands that are not phase-locked anyway); band 12 at 3.5 kHz with a bandwidth of about 1.5 kHz leaves room under the 11 kHz Nyquist, band 13 (4.6 kHz) would too, which is why the split can move. Biquad coefficients must be designed for 22.05 kHz.
   - **Reimplements an idea** (Warps' decimated groups; polyphase IIR halfbands are textbook, I fetched no source for them). HIIR (Laurent de Soras) is the well-known library; its licence I could not verify and it is not needed.

2. **Graded follower rates by band** (mda's cap `min(k0, 0.025 - 0.022 i / N)`; Warps' third-octave step).
   - Benefit: finer than our three slow low bands; a smoother low-band envelope without a global release change. I cite no source for the sound, only for the design.
   - Cost: 0 cycles in the loop (constants in `set_envelope`).
   - **Reimplements an idea.** A scenario with a spoken sample and low pitch should judge it.

3. **High-band modulator thru** (mda's Hi Thru): add the high-passed modulator, scaled by the follower sum of bands 13 to 15, to the output next to the noise path, with its own level.
   - Benefit: the real fricative where the noise path only imitates it. This is my reasoning, not a source's claim; it needs listening.
   - Cost [est]: a one-pole high-pass, an abs and a multiply-add a sample, about 15 to 20 cycles a sample = 1,000 to 1,300 a block (0.4 to 0.5 %).
   - **Reimplements an idea.** Note the modulator then reaches the output, so the app's feedback and monitoring rules apply.

4. **mda-style all-pole resonator sections with a shared second difference.**
   - Benefit [est]: 4 arithmetic ops a section against 5, so about 20 % of the section arithmetic; if the loop's cycles follow, up to about 10 % of Bands' filter cost (3 % of a block). The bench's 23.5 cycles for 13 instructions suggests the real saving is lower, so measure with `biquad` before building.
   - Cost: near 0 cycles added (one shared difference a sample); the bands' gain table changes.
   - Risks: poles near z = 1 at 125 Hz in a direct all-pole form (precision, gain tilt of the double-zero difference, per-band level compensation); our current form was chosen for its behaviour. A bench with a sine sweep first.
   - **Reimplements an idea** (mda's code is MIT-usable, but the structure differs).

5. **Formant shift by fractional band read** (Warps).
   - Benefit: a new control (the spectral envelope moved up or down), no sound claim needed beyond the author's design intent.
   - Cost [est]: the envelope read must come from the previous block's followers and be applied as a per-band gain ramp, about 16 x 64 x 3 cycles = 3,100 cycles a block (1.2 %), plus a restructure of the per-sample loop (the follower and the synthesis currently share one pass).
   - **Reimplements an idea.** It is a feature, not a saving: over the 2 % bar only if the maintainer wants it, and it conflicts with item 1's restructure; do item 1 first.

If the maintainer wants one change only: item 1.

## 4. Not verified

- Warps: band centres, Q and the number of bands per decimation group (they sit in a generated table); commit hash; a top-level LICENSE file (404; licence read from the README and file headers).
- Surge: the filter class's order and structure. Calf: default order and which files the repository's GPL text covers. Faust: internals of `resonbp` and `amp_follower_ar`. sonoTron: filter topology, "or later" wording, conditions of the 80 % claim. mda: per-file licence grant (repository-level only); whether its half-rate bank was tuned for half the centres.
- Every cycle count is an estimate; every sound claim is the sources' or absent. No source says anything about how any of these sound, and I have not claimed it.

## Sources

- https://github.com/pichenettes/eurorack (README; `warps/dsp/vocoder.cc`, `vocoder.h`, `filter_bank.h`, `filter_bank.cc`; `warps/resources.h`)
- https://github.com/pichenettes/stmlib (`dsp/filter.h`)
- https://sourceforge.net/p/mda-vst/code/HEAD/tree/src/mdaVocoder.cpp
- https://sourceforge.net/p/mda-vst/code/HEAD/tree/readme.license?format=raw
- https://www.kvraudio.com/forum/viewtopic.php?p=3109032 (via `../o3-sdk/docs/ports/mda-talkbox.md`)
- https://github.com/surge-synthesizer/surge (`src/common/dsp/effects/VocoderEffect.cpp`, `VocoderEffect.h`, `LICENSE`)
- https://github.com/calf-studio-gear/calf (`src/calf/modules_filter.h`, `src/modules_filter.cpp`)
- https://raw.githubusercontent.com/grame-cncm/faustlibraries/master/vaeffects.lib
- https://github.com/ErikDorstel/sonoTron
- https://github.com/yu2924/channelvocoder
- https://github.com/cwilso/Vocoder
- https://sources.debian.org/src/lv2vocoder/1-3/vocoder.c
- https://sources.debian.org/src/bespokesynth/1.3.0%2Bdfsg-2/Source/Vocoder.h
- https://github.com/andeuxun/VocoGroot
- `units/vocoder/src/bands.cpp`, `docs/PLAN.md`
