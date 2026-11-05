# Design review of the vocoder

An outside review by a Fable agent, 2026-11-05, requested by the maintainer. It read `../o3-sdk/docs/VOCODER-DESIGN.md`, `docs/PLAN.md` and the two research cards, not the code or the V1 results. `docs/PLAN.md` section 1b records what was taken, what was corrected and what was dropped: where they differ, the plan wins.

Block = 261,224 cycles; 1 % = 2,612 cycles. Every figure I did not derive is [est].

## 1. What is wrong, worst first

1. **Every percentage is a share of 261,224 cycles, but no document says how many of those the unit gets.** The engine's own per-block cost, the headroom rule, the free SRAM after the framework and the codec-plus-DMA in-to-out latency are stated nowhere; "two or three BANDS pairs fit" rests on an unmeasured budget.

2. **TALK copied from mda has 16 ms of latency and a bigger spike than the card counts.** The card's cost table treats the lattice as streaming, but its latency line ("about one window") describes mda's batch structure: at each wrap `lpc()` analyses the window *and* filters the captured carrier window, then overlap-adds. Copied as is, the wrap block pays autocorrelation + Levinson + 720 × 22 lattice taps: 50,000 + 95,000 = 145,000 cycles [est] = 55 % of a block, and the chord is heard 16 ms late. Build it streaming (section 2): one lattice every sample, the analysis only moves coefficients. The hop must be a block multiple (360 is not).

3. **BANDS without an unvoiced path is not intelligible.** The design puts "intelligibility" (s, t, f as noise) in the 2040 layer. A saw carrier has little energy above 5 kHz and no noise, so sibilants vanish. About 200 cycles a block [est]; belongs in V2. Same for a modulator high-pass and a slow AGC: output that follows the singer's distance from the mic sounds broken.

4. **12 cycles a biquad is the optimistic end.** Unrolled CMSIS-class code reaches 10-12, plain C in a loop 18-25 [est]. BANDS 16 at 12: 64 × 12 × 64 = 49,152, followers 16 × 6 × 64 = 6,144, gain-and-sum 16 × 3 × 64 = 3,072, total 58,368 = 22 %. At 20: 81,920 + 9,216 = 91,136 = 35 %.

5. **YIN window against range.** W = 192 at 11.025 kHz with τmax = 157 (70 Hz) satisfies W ≥ τmax but sees 1.2 periods at the low end; with threshold 0.15 a breathy low voice takes the first spurious dip (a yodel). Use W = 256 (23 ms): C = 157 × 256 × 4 = 160,768 a hop, / 4 blocks = 40,200 = 15.4 % [est] against 11.5 %. Nothing fetched covers sung voice, as the card says.

6. **Single-precision Levinson at order 22.** A steady low vowel gives a near-singular autocorrelation. mda's r₀ × 1.001 and the ±0.995 clamp are right; in float32 add a Gaussian lag window (60 Hz [est]) and default to order 16. Host test: a synthetic 90 Hz vowel at order 24.

7. **SDRAM arithmetic.** The K66 has no data cache; a non-sequential int16 read costs 15-30 core cycles [est]. "64 reads, under 1 %" holds at 1× speed; at 2× with a cubic read done sample by sample it is 4 × 128 reads ≈ 10,000 cycles = 4 % [est]. Copy the needed span (speed × 64 + 3 samples) to SRAM once a block and interpolate there. No filter or ring state in SDRAM.

8. **The one-ADC table.** "live mic × USB: no, one ADC" is mislabelled: the USB return is digital; the limit is that the engine selects one source (a routing change, as section 7 says). The line jack is two analogue channels through the one stereo ADC, so two live analogue inputs are possible with the stereo-line change. The conclusions hold; the labels do not. Not stated: whether the mic feeds both ADC channels.

9. **Licences.** mda Talkbox under MIT with Kellett's line: fine. YIN from the paper: fine. zita-at1 is GPL-2.0-or-later: "a port with Adriaensen's notice is equally licence-clean" holds only if this app ships GPL-compatible, which no document states. Write the shifter fresh. aubio (GPL-3) as a host oracle only, never shipped.

## 2. Design of each part

**Bands.** Agree: 16 bands, time domain, two biquads a band on both sides, one follower a band, no FFT (10 ms latency and a kit change). Numbers: centres f_i = 125 × 2^(0.4 i), i = 0..15 (125 Hz to 8 kHz, 2.5 bands an octave); each band two cascaded RBJ band-passes with Q = 2.3 each, so the cascade's -3 dB width is 0.4 octave. Follower: full-wave rectify, one-pole with attack 2 ms, release 25 ms, 40 ms on the three lowest bands so 2f ripple stays under -30 dB [est]. Check the summed synthesis bands with a host sweep under a flat envelope: ripple over ±3 dB means the crossover phases are wrong; flip the sign of alternate bands. Front end: 80 Hz high-pass + AGC (100 ms). Unvoiced: (envelopes of bands 13-15) / (sum of all) > 0.35 [est] with hysteresis mixes white noise into the carrier for bands 11-15. All per sample, no slicing. Budget 58,400 cycles = 22 % [est]. SRAM about 2 KiB; SDRAM none. Over 30 % on the unit: synthesis to one biquad a band (-25 %), then 12 bands.

**Carrier.** Agree: 8 paraphonic voices, saw/pulse polyBLEP, noise mix. Add a second oscillator a voice (DETUNE 0-20 cents): 16 × 30 × 64 = 30,720 = 11.8 % [est]; this is the fat sound. The noise is shared with the unvoiced path.

**Talk.** Agree on mda's DSP steps (pre-emphasis, Hann, autocorrelation, Levinson, lattice, gain = √error, the allpass paths that spread the carrier) and the MIT option. Differ on structure: Hann 768 (17.4 ms), hop 384 = 6 blocks, order 16 default (8-24 as QUALITY), ring of 2048 floats in SRAM (8 KiB, ≥ window + hop + block, so no snapshot copy). Slicing: 17 lags × 768 = 13,056 MAC over 6 blocks = 2,176 × 3 = 6,528 cycles a block; Levinson (1,500 cycles) in the sixth block; lattice per sample 16 × 6 × 64 = 6,144; coefficient ramp 16 × 64 = 1,024 (interpolating reflection coefficients is stable because |k| < 1 is kept; interpolating direct-form a[] is not); window, pre-emphasis, ring 512; carrier allpass 640; gain and mix 256. Total 16,600 = 6.4 % at order 16, about 8.5 % at order 24 [est]. Carrier path not delayed; envelope lag half a window (9 ms). Numerics: r₀ × 1.0001, lag window, k clamped ±0.995, silence gate on r₀. "Freeze a vowel" is one flag (stop updating k[]): take it here, not in 2040.

**Tune.** Agree with the card's whole pipeline (4:1 decimation, YIN sliced over 4 blocks, native refine, gate with 60 ms hold, snap with bias, one-pole retune, period-jump shifter), 14 KiB SRAM, no SDRAM, no cycfi/q benchmark. Differ: W = 256 not 192; RANGE "bass" (55-400 Hz) runs the detector at 8:1 (5.5 kHz) instead of growing the lags; hop stays 256. Worst case at W 256: C 41,200 + A 1,500 + B 1,600 + D 3,600 + E 300 + F 5,100 + G 500 + H 500 = 54,300 = 20.8 % [est]; typical with the early exit (about 90 lags) about 37,000 = 14 % [est]. TUNE replaces BANDS or TALK, so it fits.

**Loops.** Agree: 4 × 16 s mono int16 in 5.6 MB SDRAM, unit-owned, roles swapped. Playback: copy the span to SRAM each block (at 2× speed 131 reads × 20 cycles = 2,600 [est]), cubic interpolation 64 × 20 = 1,280: about 1.5 % a loop. Recording: 64 int16 writes of `in_l` to SDRAM. Not stated: how the unit learns the bar position (option (b) needs a transport event).

**Scene totals [est].** BANDS + carrier + two loops: 58,400 + 31,400 + 7,800 = 97,600 = 37 %. TALK + carrier + loops: 55,800 = 21 %. TUNE + loops: 62,100 = 24 %. SRAM under 30 KiB; the free amount is not stated.

## 3. PLAN section 1 decisions to overrule

None outright; three to amend:

- **Loops recorded by the unit.** Keep, but the slice must say how the bar clock reaches the unit. If the engine cannot send it, option (a), the kit recorder with `stop_at(Align::Bar)`, is the one that works.
- **Internal carrier, 8 voices.** Keep; add the second detuned oscillator and share the noise with the unvoiced path. Budget 12 %, not 4-6 %.
- **Kit changes: none.** Keep, and strike the FFT in `dsp` from section 7: nothing here needs it.

The rest stand, including the no-note fallback to KEY and SCALE (pass-through would make TUNE seem dead while the pads are idle).

## 4. Order

**Before V2 is relied on, a microbench slice G0** in the template app under `o3console.py cpu`: (1) an empty unit, for the engine's own cost and the free cycles; (2) 64 biquads × 64 samples; (3) the 157 × 256 YIN inner loop; (4) a 17 × 768 autocorrelation; (5) 256 sequential and 64 random int16 SDRAM reads; (6) free SRAM from the linker map; (7) in-to-out latency with the V1 line cable and a click. G0 runs alongside V2 and fixes the band count and TALK order before V4.

- **V2 → V2a Carrier, V2b Bands** with the front end and the unvoiced path. Done when a host-rendered sentence is intelligible (listened to) and the sweep ripple is under 3 dB.
- **V3 Talk**, streaming. Done when no block spikes and the carrier path is not delayed.
- **V4** as planned.
- **V5 → V5a** record, play, roles, bar clock; **V5b** speed and reverse (the span copy); **V5c** per-step carrier.
- **V6 → V6a** detector and gate on host against synthetic voices and three recorded sung phrases (aubio as a host-only oracle); **V6b** shifter, snap, retune.
- **Cut:** BANDS by FFT, 32 bands, lower-rate followers, the q benchmark. **Pull forward:** intelligibility to V2b, freeze-a-vowel to V3.

## 5. The sound

Good means: a bright, detuned carrier with energy in every band; noise wherever the voice is unvoiced; a follower fast enough for consonants (2 ms) and slow enough not to flutter (25 ms); bands whose sum is flat; a modulator at a steady level; for TALK an order that models three or four formants and no more; for TUNE a detector that never flips octaves, which matters more than the shifter.

Done badly, in the order a listener notices: words unintelligible (no noise path, dull carrier); pumping or flutter under the voice (follower too fast: 2f ripple on the low bands; too slow: mush); whistling at band centres (Q too high, or LPC order too high without bandwidth expansion); a hollow comb at the crossovers (phases); level that rides the mic distance (no AGC); in TUNE, yodels (octave errors) and a scooped onset in hard mode; in TALK, a mouth 16 ms behind the chord if mda's batch structure is copied.
