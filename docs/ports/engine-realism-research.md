# Engine realism research: channel vocoder (BANDS) and LPC talkbox (TALK)

Researched 2026-11-06. Nothing cloned, built or run; no code copied. Read status of every source: **raw** = I read the text itself; **summary** = a fetch tool returned a summary of the page; **snippet** = only a search-result excerpt. Almost everything below is *summary* or *snippet*; treat each figure as a lead to check, not as a measurement. An empty cell means not found. Costs are *[est]*; the unit measures them. Block = 64 frames = 261,224 cycles at 44.1 kHz.

Licence note: mda Talkbox (the only code read, as a fetch summary) is dual MIT / GPL-2.0-or-later (https://sourceforge.net/p/mda-vst/code/HEAD/tree/readme.license?format=raw). Nothing from it is copied here. The older card `/home/anortes/Code/o3-sdk/docs/ports/mda-talkbox.md` is the licence record.

## 0. Where the engines stand (facts from our own headers, raw)

| Item | BANDS (`bands.h`) | TALK (`talk.h`, `talk.cpp`) |
|---|---|---|
| Analysis | 16 bands, 125 Hz x 2^(0.4 i) to 8 kHz; 2 cascaded RBJ bandpass per path, Q 2.3 each | Hann 768 samples (17.4 ms), hop 384 (8.7 ms), order 8..24 (default 16), at 44.1 kHz, no decimation |
| Envelope | rectifier + one-pole, attack 2 ms, release 25 ms mid-range, graded across bands | gain = sqrt(Levinson error), ramped; reflection coefficients ramped every 8 samples |
| Front end | 80 Hz high-pass, gated AGC, first-order pre-emphasis | pre-emphasis 0.9 (`kPreEmph`), r0 x 1.0001, Gaussian lag window 60 Hz, \|k\| <= 0.995 |
| Unvoiced | noise into bands 11..15 when bands 13..15 / total > 0.35 (closes 0.28), 5 ms fade; THRU path; FORMANT +-12 st | none (the carrier decides); carrier goes through allpass paths |
| Cost | ~47 cycles per band-sample after V2c [est] (97 measured at V2) | 6.4 % of a block at order 16 [est] |

## 1. Hardware channel vocoders

Sources: S = service manual, schematic, patent or paper (primary); B = brochure or maker page; W = wiki, forum, database (secondary). No primary source for any row was read in full.

| Machine | Bands and centres | Filter order, slope, Q | Lowest / highest bands | Follower | V/UV detector and what it switches | Sibilance / HF path | Noise source | Modulator compressor | Carrier processing | Source class and read status |
|---|---|---|---|---|---|---|---|---|---|---|
| Dudley vocoder (1939) | 10 bandpass, speech about 250 to 3000 Hz, about 300 Hz wide in the patent text | not stated | | rectifier + lowpass (cut-off 25 Hz commonly quoted: NOT confirmed) | pitch (F0) and a voiced/unvoiced decision; chooses buzz (relaxation oscillator) or hiss | | hiss source | | matched synthesis filters summed | S: patent US 2,151,091 https://www.cryptomuseum.com/crypto/usa/sigsaly/pat/US2151091.pdf (garbled OCR, summary); description https://ccrma.stanford.edu/~jos/sasp/Dudley_s_Channel_Vocoder.html (summary) |
| Voder (1939) | 10 resonator bands | | | | operator foot pedal for pitch; keys choose buzz or hiss | | noise source | | | W: https://en.wikipedia.org/wiki/Vocoder (summary) |
| EMS Vocoder (c. 1970) | 22 channels: 20 bandpass + 1 high-pass + 1 low-pass at the edges; quarter-octave spacing, 3 % tolerance; listed centres 205, 249, 303, 367, 444, 539, 653, 791, 958, 1161, 1406, 1703, 2064, 2500, 3030, 3670, 4447, 5388, 7888 Hz (19 values; 7888 may be a typo or a gap) | 8-pole bandpass, 24 dB/oct ultimate, 16 dB rejection at the adjacent centre, S/N 72 dB | edge channels are plain high-pass and low-pass | lowpass smoothing proportional to each filter's centre frequency; 54 dB range; slew limiter; freeze droop < 0.1 dB/s | no controls; gates external excitation, noise and VCOs (green voiced, orange unvoiced); pitch extractor 50 to 1000 Hz, 6 % accuracy in 5 cycles or 20 ms | | gated noise with level and a "colour" (variable low-Q lowpass) | | | B: https://www.synthforbreakfast.nl/ems-vocoder-brochure/ (a transcription of the brochure, summary) |
| EMS Vocoder 2000 | 16 channels: 14 sixth-order active bandpass + 1 low-pass + 1 high-pass | 6th order | | 16 followers | noise and pulse sources gated by the V/UV detector | | | | | B/W: https://www.vintagesynth.com/electronic-music-studios-ems/vocoder-2000 (summary). Service manual https://manuals.plus/m/ac2056b5faeea80dc93bcd950354afe6de4138160bced96651cd5cc94cec2796 and https://pinelectronics.com/data/Vocoder2000_Manual.pdf not read (over 10 MB) |
| Bode 7702 / Moog 16-channel (1978-79; dates conflict) | 16 bands, 50 to 5,080 Hz, patchable; separate HF channel about 5,080 to 15,000 Hz | not stated | HF channel is its own band | analyzer response about 6 ms (secondary, unverified) | voice-activated switching of carrier and noise; "Hiss" and "Buzz" controls | HF channel added in DIRECT mode, or switched in on sibilance only | noise | | | W: https://www.polynominal.com/moog-vocoder-bode/ (summary; text from a brochure, not a manual) |
| Sennheiser VSM-201 | 20 channels, 100 Hz to 8 kHz | not found | | | speech/pause threshold control; voice-mode-detector output | | | | | W: https://www.radiomuseum.org/r/sennheiser_vocoder_vsm201vsm_20.html (summary). The manual PDF URL 404'd. A Surge-table claim of 180 Hz to 7.5 kHz is unverified |
| Roland VP-330 | 10 bands, paraphonic, with string machine (1979-80) | not found | | | key-trigger detector and release control in the service notes (content unread) | | | | | W: https://en.wikipedia.org/wiki/Roland_VP-330 (not fetched; mirror summary); service notes https://archive.org/details/roland_VP-330_SERVICE_NOTES_2nd (unread). SVC-350: nothing found |
| Korg VC-10 (1978) | 20-band analyzer and 20-band synthesis section with a divide-down synth | not found | | | | highest four filters fed only by noise (snippet, unverified) | | | | W: https://en.wikipedia.org/wiki/Korg_VC-10 (summary) |
| Synton Syntovox 221 | 20 channels, about 20 units built | not found | | | | | | | | W: https://www.matrixsynth.com/2024/03/syntovox-221-vocoder.html (summary). The 222 is a simpler cousin: https://www.vintagesynth.com/synton/syntovox_222_Vocoder |
| Electro-Harmonix EH-0300 (1978) | 14 listed: 216, 298, 361, 478, 493, 614, 797, 1013, 1142, 1381, 1677, 2031, 2461, 2982 Hz (478 and 493 look like a typo) | | | | | Hi Boost control | | 20 dB compression range, 35 dB preamp gain | | W: https://www.effectsdatabase.com/model/eh/vocoder (fan database, unofficial; snippet). Schematics at https://www.matrixsynth.com/2024/01/1979-electro-harmonix-eh-0300-w.html not read |
| Doepfer A-129 | 15 filters: 13 bandpass + low-pass + high-pass | not stated (plot only) | edge filters are low-pass and high-pass | envelope follower per band drives the synthesis VCA | A-129/5: adjustable pre-amp and treble boost, voltage-controlled switch between voiced and unvoiced carriers, gate output | | | | | B: https://doepfer.de/a129e.htm (summary) |
| Korg MS2000 / microKORG | microKORG: internally 16 bandpass filters in pairs; 8-channel editable bank; Formant Hold; Formant Shift -2..+2 filters | | | | | | | | | W: https://manualmachine.com/korg/microkorgs/18992769-owners-manual (snippet). MS2000: not read |

What the table supports (inference from the rows above, not a source claim):

1. Every analog design has a separate treatment of the lowest and highest bands: edge filters are high-pass and low-pass (EMS, A-129), or the top is a wide HF channel with its own switch (Bode 7702), or the top bands are driven by noise only (VC-10, snippet). Our bands 13..15 plus THRU already sit near this.
2. The voiced/unvoiced decision switches the carrier source (buzz, noise, VCOs). It does not change the band filters. Ours adds noise to bands 11..15 only; the EMS gates the whole excitation. A whole-spectrum switch is a possible trait, not yet a fact about preference.
3. The EMS follower smoothing scales with centre frequency (summary). Ours is graded across bands in the same direction.
4. Spacing is quarter-octave on the EMS (about 4 bands an octave, 19 to 20 bands over 205 Hz to 8 kHz); ours is 2.5 bands an octave. Band overlap and slope are the audible differences. 24 dB/oct per filter with 16 dB adjacent rejection (EMS) means narrower, more isolated bands than ours (two cascaded Q 2.3 sections, about 0.28 octave wide at -3 dB).

## 2. Speech science for band count and placement

| Finding | Number | Source | Read |
|---|---|---|---|
| Shannon 1995: slow temporal envelope plus few bands gives speech recognition | envelope cut-offs tested 16, 50, 160, 500 Hz; slow envelope under about 50 Hz suffices; 3 to 4 bands near ceiling for sentences | https://www.cns.nyu.edu/events/earclub/artfiles/Shannon1995.pdf (scanned image, no text layer); abstract https://auditory.org/asamtgs/asa94mit/2pSP/2pSP38.html | snippet |
| Consonants vs vowels | consonants gain from 1 to 2 channels, less from 2 to 4; vowels gain from 1 to 3 | same abstract | snippet |
| Match of analysis and carrier bands | with four bands the exact cut-offs matter little if analysis and synthesis bands cover the same frequency span; a mismatch costs heavily | Shannon et al. 1998 https://sonicfield.org/library/speech-recognition-with-altered-spectral-distribution-of-envelope-cues (site later returned 410) | snippet |
| Cochlear-implant sentence plateau | about 6 to 8 channels in quiet (Dorman and Loizou 1997); about 12 for HINT sentences and 16 for word tasks (Friesen et al. 2001) | https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2016.00517/pdf ; https://pmc.ncbi.nlm.nih.gov/articles/PMC4336249 | snippet |
| Noise | intelligibility in noise stays below unprocessed speech even with 20 channels | same two | snippet |
| ANSI S3.5-1997 octave importance | 250 Hz 0.0617; 500 Hz 0.1671; 1 kHz 0.2373; 2 kHz 0.2648; 4 kHz 0.2142; 8 kHz 0.0549 (sum about 1) | R package SII https://rdrr.io/cran/SII/f/inst/doc/SII.pdf | summary |

Consequences (inference): intelligibility is carried by 500 Hz to 4 kHz (about 0.87 of the weight), peaking at 1 to 2 kHz; the bands below 250 Hz and above 6 kHz carry about 0.06 each. Sixteen bands are for naturalness, not intelligibility. Our 125 Hz bottom band carries almost no weight; the 5 to 8 kHz region matters for fricative identity but little for the SII. Dudley's 250 to 3000 Hz was enough for communication speech. Not found: the one-third-octave SII table (Annex B) and the 25 Hz Dudley cut-off.

## 3. Talkbox: sound in the mouth

### 3.1 Devices and practice

| Item | Fact | Source | Read |
|---|---|---|---|
| History | Sonovox 1939 (throat transducers, US2273078); Alvino Rey 1939; Pete Drake 1964 (8-inch cone driver and a funnel); Kustom "The Bag" 1969 (30 W driver); Heil 1974; Stevie Wonder on TV 1972 with the Bag | https://en.wikipedia.org/wiki/Talk_box | summary |
| Heil driver | 250 W JBL driver, lowpass filter, tubing; Frampton pinches the tube partly to improve enunciation; a gritty 1970s Marshall, no echo. Wikipedia gives 100 W for the first unit: the two conflict | https://vintageguitar.com/16339/heil-talk-box | summary |
| DIY tube | about 6 ft (1.8 m) of 1/2 in (12.7 mm) inner bore, midrange driver; distortion or fuzz for sustain | https://generalguitargadgets.com/effects-projects/filters-other/talkbox/building-a-talkbox/ | summary (hobbyist, no acoustics) |
| Troutman | Minimoog then Yamaha DX100 through an Electro-Harmonix "Golden Throat"; sawtooth customary; pitch bend and slurring; portamento settings not found | https://forum.vintagesynth.com/viewtopic.php?t=53164 ; https://www.attackmagazine.com/technique/tutorials/creating-a-daft-punk-style-talk-box-effect-with-waves-ovox-vocal-resynthesis/ | summary |
| Consonants | H, S, T hard because they rely on breath, not tract sound; B, D, G, K, P, T need practice; the tube sits between the teeth | https://manuals.online/mxr/music-pedal/m222-talk-box/user-manual/nciq (snippet); https://krykos.newgrounds.com/news/post/68104 ; https://blog.discmakers.com/2017/09/meet-the-talk-box/ | snippet, forum |
| Frampton, Wonder, Riley, Daft Punk | only name-level mentions (Riley as an ElectroSpit user on Wikipedia); no technique sources | | |

No measured talkbox spectrum and no AES paper were found.

### 3.2 Tube acoustics (calculations, not sourced)

Using c = 343 m/s and the DIY tube (1.8 m, 12.7 mm bore); the real tube and its damping are unknown.

| Quantity | Calculation | Result |
|---|---|---|
| Delay | 1.8 / 343 | about 5.3 ms |
| Resonance spacing | c / (2 L) for a tube closed or open at both ends | about 95 Hz (a closed-open tube gives odd multiples of c/4L, about 48 Hz apart, at roughly 48 Hz x odd) |
| First cross mode | c / (1.7 d) for d = 12.7 mm | about 15.9 kHz |

Inference: a plane-wave tube of this length has tens of closely spaced resonances under 4 kHz, damped by the driver cone, the tube wall and a thin tube's viscous loss, so the tube acts as a mild comb, not as a vowel-like filter. The mouth supplies the vowel. Driver response, tube loss and horn loading are not sourced.

### 3.3 Mouth source versus glottal source

| Point | Evidence | Source | Read |
|---|---|---|---|
| Electrolarynx direct noise masks formants | direct radiated noise often masks one or more of F1 to F3; neck placement changes amplitudes | Meltzner, Hillman, Stevens 2001; thesis https://dspace.mit.edu/bitstream/1721.1/27199/1/meltzner_geoff_phd_2003.pdf | snippet (thesis: HTTP 429) |
| Placement-dependent zeros, low-frequency deficit | spectral zeros depend on where the source couples; a supraglottal source removed the zeros and fixed low frequencies | same thesis; 2013 IEEE study | snippet |
| Formants shift little, intensities change | Weiss, Yeni-Komshian, Heinz 1975 | | snippet |
| Voicing cue survives a continuous source | Kobayashi, Horiguchi, Baer 1985: voiceless consonants probably cued by higher intraoral pressure | | snippet |
| F0 variation is the largest naturalness gain | Meltzner and Hillman 2005, JSLHR | | snippet |
| Transcutaneous excitation near the glottis | Fujimura and Lindqvist 1971, JASA 49, 541-558: sweep tone 100 to 5000 Hz in about 8.5 s | PDF 404'd | snippet |
| Fricative source in the front cavity | poles do not depend on source location; back cavity adds zeros; front source gives zeros that cancel back-cavity resonances | https://www.coli.uni-saarland.de/Phonetics/icphs/ICPhS1991/12_ICPhS_1991_Vol_1/p12.1_028.pdf (summary); Stevens' Acoustic Phonetics not read | summary |

Inference only: in a talkbox the source enters at the mouth (above the tongue, behind the teeth or at the lips side), so the pharynx and larynx act as a closed side branch. A side branch gives antiresonances (zeros) and weakens the back-cavity formants (F1 especially); front-cavity formants (F2 and up) dominate. The talkbox is therefore all-pole-ish above F1 and weak below it. This matches the electrolarynx low-frequency deficit but is not proven for the talkbox.

### 3.4 Consonants and gating speed (inference, no sourced measurement)

- Voiced consonants and vowel transitions are shaped by tongue and lips and carry through the tube. Closures (B, P, D, T, K, G) interrupt the airway of the mouth, so the tube sound drops by the closure time (tens of ms). Fricatives (S, F, SH) need turbulent noise made at the constriction, which the tube cannot provide; the performer's own breath may leak through, which is the "direct leak" (the mouth's own sound reaches the microphone). No source quantifies leak level or mic proximity effect.
- Closure and release times in speech are typically 10 to 100 ms (general phonetics, not sourced here), so an envelope follower at 2 to 25 ms can follow them.

## 4. LPC practice

### 4.1 What mda Talkbox does (summary of raw source; https://sourceforge.net/p/mda-vst/code/HEAD/tree/src/mdaTalkBox.cpp?format=raw)

| Item | mda | Ours |
|---|---|---|
| Analysis rate | toggle K stores every second input sample, so analysis is at fs/2 with no visible anti-alias filter | full rate, no decimation |
| Window | Hann, N = 0.01633 x fs = about 720 stored samples at 44.1 kHz; N counts half-rate samples, so a frame spans about 2N host samples = about 32.7 ms | Hann 768 samples = 17.4 ms |
| Hop | N/2 buffer offset in half-rate samples, about 16.3 ms | 384 samples = 8.7 ms |
| Order | O = (0.0001 + 0.0004 q) x fs, about 4 to 22, counted at the decimated rate | 8..24, default 16 at full rate |
| Pre-emphasis | first difference (about 6 dB/oct), modulator only | 0.9 coefficient (the header says pre-emphasis; coefficient read from `talk.cpp`) |
| Conditioning | r0 x 1.001; frame zeroed if r0 < 1e-5; \|k\| <= 0.995 | r0 x 1.0001, Gaussian lag window 60 Hz, \|k\| <= 0.995 |
| Gain | sqrt of final Durbin error | the same |
| Carrier | the other input; two first-order allpass-like sections (0.3, 0.77) summed, before storage and on the output | the same idea, allpass paths |
| Voiced/unvoiced | none; character from the carrier | none |
| Reset | lattice state reset each frame, windowed overlap-add; wet lags by about one frame | streaming lattice, no frame lag on the carrier |

This corrects `mda-talkbox.md` (it called the window 720 samples, 16.3 ms; at the host rate it is about 32.7 ms with 16.3 ms hop). Effect: mda's 22nd order at fs/2 resolves a 11 kHz band with 11 pole pairs, which is the fs(kHz)+2..4 rule at the decimated rate. Ours at order 16 over 22.05 kHz bandwidth has 8 pole pairs: about 2.75 kHz per pole pair against mda's 1 kHz per pole pair. Both are inference from the rule and the header numbers.

### 4.2 Order, rate, warping

| Topic | Fact | Source | Read |
|---|---|---|---|
| Order rule | one complex pole pair per kHz of bandwidth (fs in kHz) plus 2 to 4 | https://www.dsprelated.com/showthread/speechcoding/965-1.php ; https://phonlab.readthedocs.io/en/latest/_modules/phonlab/acoustic/choose_order_.html | snippet |
| At 44.1 kHz | the rule gives about 46 to 48: impractical, hence decimation (mda) | calculation | calc |
| Warped LPC | Strube 1980: warp 0.47 at 10 kHz approximates Bark well; warp about 0.576 at 16 kHz, 0.646 at 22.05 kHz, 0.756 at 44.1 kHz, 0.766 at 48 kHz | https://sevagh.github.io/warped-linear-prediction/Why%20warp%3F%20A%20brief%20introduction%20to%20psychoacoustics.html | summary |
| Warped LPC order saving | a review reports less than half the order for comparable performance (study unnamed) | | snippet, unverified |
| Warped cost | Härmä and Laine 2001, IEEE TSAP 9(5) 579-588: not read. General knowledge: unit delays in the filter and autocorrelation become allpass sections, roughly doubling the per-stage cost | | not read |

### 4.3 Interpolation, gain, excitation

| Topic | Fact | Source | Read |
|---|---|---|---|
| Stable domains | direct-form coefficients lose stability when interpolated; reflection coefficients, LAR, arcsine RC and LSP keep it | https://www.isca-archive.org/eurospeech_1995/paliwal95b_eurospeech.html | snippet |
| Best domain | LSF ranked best of eight for interpolation performance (Paliwal 1995) | same | snippet |
| LAR | lower spectral sensitivity | https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/5351338 | patent, weak |
| Plosive smearing | interpolating energy over widely spaced samples smears plosives; LPC-10 uses an impulse doublet on a strong gain rise; per-subframe gain plus a plosive flag is the fix | https://patents.google.com/patent/US6304842 ; https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/5953697 | patents, summary |
| True envelope | iterative, an FFT per iteration, slow (Röbel and Rodet, DAFx 2005) | https://dafx.de/paper-archive/details/EDSTSaO17Gd_0LV03iwDfw | summary |
| Plain LPC weakness | biased toward harmonic peaks at high F0; plain LP weakest of the compared methods for pitched signals (Röbel, Villavicencio, Rodet 2007) | https://www.bohrium.com/paper-details/on-cepstral-and-all-pole-based-spectral-envelope-modeling-with-unknown-model-order/812004242205179905-2594 | summary |
| Cepstral | discrete cepstrum fits only harmonic peaks; a real-time TE comparison used order 60 for LPC and 70 for cepstral models | same | summary |

Inference for our design: a reflection-domain ramp is stable but is not the smoothest perceptually; ramping LSF needs a conversion per frame (costly); a log-area ramp is cheap (one `tanh`-type map per coefficient per hop). The error-gain approach matches mda; an attack-aware gain (per 64 samples, not per hop) is the plosive fix the patents describe. True envelope is out of budget (inference).

Not found in any source: carrier tilt and whitening for LPC cross-synthesis (a practitioner's rule: whiten or flatten the carrier so that the all-pole filter, not the carrier's own spectrum, sets the tilt; no citation).

## 5. Ranked audible traits (inference from sections 1 to 4; costs [est])

Rank is audible gain per cost, judged from the evidence above and not heard. "Control" = a character the player may want; "correct" = simply more right.

| # | Cause | DSP change | Host measure with a synthetic known-formant signal | Cost [est] | Class |
|---|---|---|---|---|---|
| 1 | TALK sees 22 kHz of bandwidth with 8 pole pairs; formants blur | analyse at fs/2 (halfband already in BANDS) with order 16..22 | synthesise vowels with known F1..F3 (an all-pole filter on a buzz), compare the peak positions of the LPC envelope to truth (error in Hz) at order 12 / 16 / 22, full rate vs half rate | halfband about 2 x 20 cycles x 64 = 2,560 per block; autocorrelation halves (about -3,000); about neutral or cheaper | correct |
| 2 | Carrier spectrum leaks into the shape: a saw has a -6 dB/oct tilt on top of the analysis | pre-whiten or tilt-compensate the carrier before the lattice (one-pole, coefficient 0.9 to 0.95) | carrier of known tilt through a flat modulator; measure the output tilt against the target | one biquad-ish section x 64 = about 600 cycles | correct, with a TILT control |
| 3 | Gain per hop of 8.7 ms smears onsets and closures | gain per 64-sample block from the block's own energy (within the hop's LPC shape), and a short attack | synthetic gated vowel with a 10 ms closure; measure the closure depth and the onset time | per block 64 x 2 cycles = 128 [est] | correct |
| 4 | BANDS: noise only in bands 11..15; EMS gates the whole excitation | optional whole-spectrum V/UV switch (buzz to hiss) | voiced then fricative synthetic signal; measure the spectral centroid of the output for each | a mix ramp: about 500 cycles | control |
| 5 | BANDS: band slope and spacing differ from the EMS (24 dB/oct, 4 bands an octave) | optional bands count (up to 20..22) and a sharper section | swept-sine sum ripple and adjacent-band rejection; compare to 16 dB at the neighbour centre | +25 % band cost per 4 bands: about 47 x 4 x 64 = 12,000 [est] | control |
| 6 | BANDS: graded release is already EMS-like | check the gradation proportional to centre frequency (EMS: smoothing proportional to centre) | impulse in each band, measure the release time against 1/f_c | none | correct |
| 7 | TALK: antiresonance of the mouth-entry source and F1 weakness | optional low-frequency shelf cut before the lattice (a talkbox is weak under F1, inference) | formant signal through a model with a side-branch zero; spectral error | one biquad: about 600 | control (TALKBOX CHARACTER) |
| 8 | Direct leak of the player's breath and mic (consonants in a real talkbox) | optional mix of a high-passed modulator (THRU) into TALK | fricative synthetic signal: measure energy above 4 kHz at the output | like THRU: about 300 | control |
| 9 | Coefficient motion in reflection domain is not perceptually even | ramp LAR or LSF | step between two vowels; measure the envelope transition against a straight-line spectral path | per hop 16 maps about 800 cycles / 6 blocks | correct (small) |
| 10 | Plain LPC bias to harmonic peaks at high F0 | a lag window of 60 Hz is already there; widen to 100 Hz with a bandwidth-expansion option | synthesise high-F0 vowels (300 to 500 Hz), measure the bias of the peak toward harmonics | none | control |
| 11 | No pitch-adaptive window | window length by F0 | measure only against a pitch glide | needs a pitch tracker: over 5,000 per block [est] | not recommended |
| 12 | Warped LPC | warp 0.65 to 0.75 | Bark-spaced error of the envelope | doubles the per-stage cost: lattice 6,144 to about 12,000 [est] | defer |

The sample test bench for rows 1, 2, 3 and 7: a synthetic buzz of known F0 through an all-pole filter of known F1..F3 and bandwidths, then the engine's output spectrum against the known filter response, in dB error over 200 Hz to 6 kHz. This is all host work; the cost column is only for the unit to measure.

## 6. Not verified or not found

- Primary circuit data for any machine in Section 1 beyond the EMS brochure transcription: filter orders, Q and slopes for the VSM-201, VP-330, SVC-350, Syntovox 221, Bode 7702, MS2000, Doepfer A-129.
- The full text of Shannon 1995 (scanned), Härmä and Laine 2001, Stevens' Acoustic Phonetics, Sondhi 1975, Fujimura and Lindqvist 1971 (404), the Meltzner thesis (429).
- The one-third-octave SII table; Dudley's 25 Hz follower figure.
- Any measured talkbox spectrum, driver and tube response, mic technique and proximity effect, the leak level, Daft Punk or Teddy Riley practice (only name-level or tutorial).
- Why the 1:2 decimation in mda has no anti-alias filter (read from a summary; the effect on quality is unmeasured).
- All cost figures are estimates; measured baselines come only from `docs/ports/vocoder-survey.md` (bands about 97 cycles per band-sample at V2).
