# Carrier oscillators (U10): recommendation (b), an original polyBLEP stack written here; no third-party code

Researched 2026-11-07. Nothing cloned, built or run; no code copied. Every cost is *[est]*: only the unit measures it (U11). Block = 64 frames = 261,224 cycles at 44.1 kHz.

## 0. Facts from our repo (read)
- `units/vocoder/include/o3_voc03er/units/vocoder/carrier.h` and `src/carrier.cpp`: 8 voices, 2 polyBLEP oscillators each (saw or 50 % pulse), float only, `Voice` = 32 bytes. Specialised on Two x Saw x Gate x segment, so SPREAD 0 costs nothing extra. The API (`note_on`, `render`, `set_wave`, `set_detune_cents`, shared `NoiseSource`) stays.
- Cost today: `carrier.h` says about 30 cycles an oscillator [est]; `docs/UI-DESIGN.md` line 397 says 34 each, measured. At 34: 8 x 64 x 2 x 34 = 34,816 a block (13.3 %).
- Room: BANDS 8 notes + 4 loops peaks at 65.5 %, limit 69 %: 3.5 % x 261,224 = 9,143 cycles (`UI-DESIGN.md` lines 395-412, 508).
- The design disagrees with itself: line 397 puts 64 a voice-sample on TWO oscillators (34 + 34 = 68), line 408 calls THREE at 64 the edge. Three at 34 is 102; 64 for three means about 21 an oscillator, which nothing has measured. This card works from 34.

## 1. Candidate (a): the Mutable Instruments virtual-analogue oscillators (Plaits)
- Licence: the repository README says code of the STM32F projects is MIT, AVR projects GPL-3.0, hardware CC BY-SA 3.0, and the maker's name is a registered trademark not to be used on derivatives (https://github.com/pichenettes/eurorack). The file I checked, `plaits/dsp/oscillator/variable_saw_oscillator.h` (https://raw.githubusercontent.com/pichenettes/eurorack/master/plaits/dsp/oscillator/variable_saw_oscillator.h), has the header "Copyright 2016 Emilie Gillet", the MIT permission text and the line "See http://creativecommons.org/licenses/MIT/". Both pages were read through a fetch summary, not as raw text. Class: permissive (MIT), attribution required, compatible with GPL-3.0-or-later. NOT checked: the `stmlib` headers it includes (`dsp.h`, `parameter_interpolator.h`, `polyblep.h`; a separate repository), the sibling `variable_shape_oscillator.h` (the nearer match for saw to pulse), the exact README-to-directory mapping. Do not take anything from the AVR (GPL-3.0) directories by accident. A port needs those headers' licences read first.
- Shape: C++, float only (no `double`, no tables in the file), no heap, no virtuals. State about 7 floats plus a flag, about 32 bytes an oscillator (summary). Parameters are three smoothed values (frequency, pulse width, waveshape) interpolated across a block: fits our 64-frame block.
- Cost [est]: per sample three interpolator steps, slope terms (maybe a division, 14 cycles on the FPU), two BLEP corrections, a naive-wave function: about 60 to 80 cycles an oscillator, twice ours. Three oscillators x 8 voices x 64 x 70 = 107,520 a block, 41 % of a block: nowhere near the 9,143 of room.
- Fit: its waveform is a variable-slope saw with a notch, not our saw to pulse, so SHAPE would be rebuilt anyway; what it adds is parameter smoothing, which a 64-frame block does not need.

## 2. Candidate (b): an original polyBLEP stack (recommended)
- Licence: ours, GPL-3.0-or-later; polyBLEP and detuned unison are published techniques. No attribution file.
- Shape: the existing `blep` and `osc` in `carrier.cpp`: float, no tables, no libm in the loop. State a voice: three phases (12 bytes) + a sub flag: `Voice` grows from 32 to about 40 bytes, 320 bytes at 8 voices, SRAM. The TILT state lives once in the carrier.
- Cost [est], at 34 an oscillator: the third oscillator is 34 x 64 = 2,176 a voice-block; 9,143 / 2,176 = 4.2 voices with nothing else added.
- **Honest answer: three oscillators on all eight voices do not fit.** 8 x 2,176 = 17,408 extra, 1.9 x the room. Even at the unmeasured 21 an oscillator it is 10,752, still over.

## 3. SUB, TILT, SHAPE and SPREAD, cheaply
- SUB (square an octave down): a flag flipped at each wrap of oscillator 1; its edge reuses the wrap residual `blep(phase, e)` already computed for oscillator 1 (same edge width): a sign and a multiply-add, about 5 cycles a voice-sample [est] = 2,560 a block at 8 voices. No extra phase, no extra BLEP.
- TILT: a one-pole low-pass `lp`, output = low gain x lp + high gain x (x - lp), **once on the sum of the voices** before the noise is added: about 12 cycles x 64 = 770 a block [est], against about 6,100 for one per voice. The sum is linear, so it matches a per-voice shelf up to the slow envelope. Gains smoothed per block.
- SHAPE: keep today's exact paths at both ends (templated, no cost). Between them, saw(p) - m x saw(p + width), gain 1/(1+m): a second BLEP test, about 8 to 10 extra cycles an oscillator [est], only while SHAPE is strictly between the ends.
- SPREAD, an oscillator cap: at most 18 oscillators sound in total. With n sounding voices the third oscillator runs on min(n, 18 - 2n) of the newest voices (n of 6 or fewer: all; 7 voices: 4; 8 voices: 2). A chord of three or four notes gets the full unison. The third oscillator's level ramps over about 5 ms and starts at an offset phase, so a voice entering or leaving the rule does not click. Detune pair at -SPREAD and +SPREAD cents as designed; SPREAD 0 is today's path.
- Rejected: extra oscillators at half rate (aliasing, plus an interpolation cost that eats the saving); a non-BLEP third oscillator (it folds into the top bands).

## 4. Budget at eight notes [est]
| Item | Cycles a block | Share of 9,143 |
|---|---|---|
| Cap of 18 oscillators (+2 over today's 16) | 4,352 | 48 % |
| SUB on 8 voices | 2,560 | 28 % |
| TILT once | 770 | 8 % |
| Total | 7,682 | 84 %, margin about 1,460 |
- SHAPE strictly between the ends on all 18 oscillators adds about 10,400 more: it does not fit with SPREAD and SUB at eight notes. Morph oscillator 1 only (8 x 64 x 9 = 4,600, still over the margin) or accept that at eight notes the cap drops; U11 reads it.

## 5. Risks
- All figures are estimates; the 34 comes from one measurement of the current loop. U11 reads the cost at eight notes with SPREAD and SUB on before the cap is kept.
- Two-point polyBLEP leaves some aliasing at high notes, not measured here; the U10 host oracle sweep per band decides, a higher-order residual is the fallback.
- Maintainer decision: the design's "64 for three oscillators" (section 0) versus 34 measured; the cap and what gives way when SHAPE and SPREAD and SUB are all on.

## 6. Verdict
(b), original polyBLEP stack, behind the unchanged `Carrier` API. (a) is licence-clean (MIT, stmlib still to read) but about twice the cost and a different waveform. Inspiration only from (a).
