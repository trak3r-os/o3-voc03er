# Third-party material

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
