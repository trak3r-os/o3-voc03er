# Third-party material

This project is GPL-3.0-or-later. The firmware image links the o3-sdk kit statically (GPL-3.0-or-later), so the image as a whole is GPL.
The table lists every outside source this repository's code, assets or documents draw on, with its licence and what was taken. Where the
code is original, it says so. No code in this tree is derived from any official firmware, and none has an unstated or incompatible licence.

| Source | Licence | What was taken | Where |
|---|---|---|---|
| mda Talkbox (Paul Kellett) | dual MIT / GPL-2.0-or-later; the MIT option is used | the DSP steps of the talkbox (no source text) | `units/vocoder/src/talk.cpp`, `talk.h` |
| YIN pitch detector (de Cheveigne and Kawahara, JASA 2002) | a published paper; no code | the algorithm; written clean room from the paper | `units/vocoder/.../tune.h` |
| Period-jump pitch shifter | none: written fresh here | nothing | `units/vocoder/.../autotune.h` |
| Carrier synth oscillators (polyBLEP stack) | none: original code | the polyBLEP technique is published; no implementation was read | `units/vocoder/` (see `docs/ports/carrier-osc.md`) |
| RBJ audio EQ cookbook biquads, polyphase allpass halfband | published formulas (cookbook, textbook halfband) | the formulas and two textbook coefficients (0.127, 0.566); code is original | `units/vocoder/.../bands.h`, `halfband.h` |
| Spleen 8x16 bitmap font 2.2.0 (Frederic Cambus) | BSD-2-Clause | the dot atlas of the skin is derived from its glyph shapes | `app/src/skin/ornoir_assets.h` |
| o3-sdk kit and platform | GPL-3.0-or-later | linked into the image: Spleen and Terminus (SIL OFL 1.1) glyph tables, FatFs R0.15a (ChaN, BSD-style) | `../o3-sdk` |
| Surveyed projects (Warps, mda Vocoder, Surge, Calf, Faust libraries, sonoTron, LADSPA vocoder, web vocoders, others) | various | nothing: read as research only, no code taken | `docs/ports/vocoder-survey.md` |

## mda Talkbox (Paul Kellett): the DSP steps of the vocoder's Talk mode

- Used in: `units/vocoder/src/talk.cpp`, `units/vocoder/include/o3_voc03er/units/vocoder/talk.h`.
- What is taken: the DSP steps only (pre-emphasis, window, autocorrelation, Levinson-Durbin with reflection coefficients clamped
  to +-0.995, lattice filter, gain = sqrt(error), a first difference of the carrier); the analysis and lattice run at fs/4 behind
  a halfband pair of this project's own. No mda source is in this tree: the
  code is written here from the algorithm's description, as a streaming design (the structure differs from mda's batch one).
- Licence of the original: dual MIT / GPL-2.0-or-later at the licensee's choice (`readme.license` of the mda-vst repository,
  https://sourceforge.net/p/mda-vst/code/HEAD/tree/). This project takes the **MIT option**.
- Copyright lines to keep: "Copyright (c) 2008 Paul Kellett", as `MIT License.txt` of mda-vst has it, and "Copyright (c) 1999-2000 Paul
  Kellett (maxim digital audio)", as the header of `mdaTalkBox.cpp` has it (the card `../o3-sdk/docs/ports/mda-talkbox.md` records that one).
- The licence, as shipped in `MIT License.txt`, fetched from the repository above on 2026-11-05:

```
mda VST plug-ins

Copyright (c) 2008 Paul Kellett

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

The rest of the code stays GPL-3.0-or-later.

## YIN (de Cheveigne and Kawahara, "YIN, a fundamental frequency estimator for speech and music", JASA 111(4), 2002)

- Used in: `units/vocoder/include/o3_voc03er/units/vocoder/tune.h` and its source. The detector is written clean room from the paper; the
  header says that no code of aubio, the Teensy Audio library or any other implementation was read or copied. A paper is not code and
  carries no licence obligation here; it is cited as the source of the method.

## Pitch shifter, carrier oscillators, band filters and halfbands: original code

- The period-jump shifter (`autotune.h`) was written fresh: no other shifter or tuner was read or copied, and there is no FFT.
- The carrier synth's polyBLEP oscillators are an original stack (`docs/ports/carrier-osc.md`, recommendation b: no third-party code).
- The band filters are RBJ cookbook biquads and the halfbands are the textbook polyphase allpass pair; both are published formulas
  implemented here. `docs/ports/vocoder-survey.md` records what other vocoders do; Warps' half-rate band groups are an idea this project
  re-implemented, and no source of it was fetched.

## Spleen 8x16 (Frederic Cambus), version 2.2.0

- Used in: the dot atlas of the NOIR skin (`app/src/skin/ornoir_assets.h`) is derived from the Spleen 8x16 glyphs; the kit also ships
  the face for text.
- Licence: BSD-2-Clause, copyright (c) 2018-2026 Frederic Cambus. The licence text is `third_party/fonts/spleen/LICENSE` in o3-sdk, and
  a distributed image's documentation should reproduce it with the Terminus (SIL OFL 1.1, `third_party/fonts/terminus/OFL.TXT`) text as o3-sdk's own
  `THIRD_PARTY.md` requires.

## o3-sdk

- The kit, units, platform code and simulator are `../o3-sdk`, GPL-3.0-or-later, and are not copied into this repository. Its own
  `THIRD_PARTY.md` lists what the kit holds (Spleen, Terminus, FatFs in the image; doctest and stb_image_write for host tests only).

## Looked at, nothing taken

- `docs/ports/vocoder-survey.md` and `docs/ports/engine-realism-research.md` record the vocoders, talkbox sources and papers that were read as
  research, with their licences. None of their code or data was copied into this tree.
