# Changelog

## Unreleased

### Changed

- PERFORM plays the layout (Q6): row 0 is always the controls (OCT, VOL, ROOT, MOD, BND), and rows A, B and C below it play their PLAYS
  value, a note, a chord (TRIAD, 7TH, OPEN, POWER), a piano (A the black keys, B the white keys), FORMANT or BEND (C only) or nothing (OFF).
  F6 is ROW (A, B or C; the row's pads flash once) and F7 is PLAYS; holding a pad of A, B or C makes F7 change that pad's row at once.
  The default is A NOTE, B TRIAD, C FORMANT. F6 CHORD, F7 PADS (CHORDS or KEYS) and SHIFT + F7 BEND ROW are gone: BEND is a value of row C,
  and SHIFT + F7 is empty. SHIFT latches a pad on every row that plays notes. SAMPLE REC, SAMPLE EDIT and PARAMS play the same layout.
  Saving writes the 1.0 chord kind, pads mode and bend row derived from the layout, and a 1.1 file loads through the layout alone.
- A tap on a focused LEVEL key (PARAMS) or on MASTER's GAIN and LEVEL stops at the top instead of wrapping round to the bottom, so a
  third tap no longer silences the output.
- PARAMS dims QUALITY outside TALK, where it does nothing.
- PATTERN's NOTE palette is PERFORM's layout (Q2): row 0 is A and row 1 is B as they play, so a pad writes what PERFORM would play, a note or
  a chord of the layout's kind, a piano key or nothing (a pad that plays nothing writes nothing). F6 is now ROW (A or B) and F7 PLAYS (that
  row's value, NOTE, TRIAD, 7TH, OPEN, POWER, the piano values, OFF); both are dim outside the NOTE lane, and the row flashes when it
  changes. CLEAR LANE moved to SHIFT + F6 and FOLLOW to SHIFT + F7. The NOTE lane's LEDs mirror PERFORM's rows: the key's root low, the
  scale dim, the rest dark, the pad of the cursor's step mid. SHIFT + a palette pad writes nothing.

### Added

- The project keeps more (P3, Q8): STAT 1.1 adds VOICING, the BEND ROW and its range, FOLLOW, the preset (by name, so a reordered
  table does not move it), and the pads layout (rows A, B, C and the two last-used chord pads); SLOT 1.1 adds TRIG and CHOKE per slot.
  A v0.2.0 project loads with SMOOTH, bend row off, range 2, FOLLOW off, no preset, ONE SHOT and CHOKE ON, and its KEYS or chord kind
  becomes the equivalent layout. An out-of-range value refuses the file whole. F4 NEW resets all of them. Saving also writes the 1.0
  chord kind, pads mode and bend row, derived from the layout, for an older build to read.
- PATTERN, NOTE lane (Q3): a palette pad sounds while it is held (a note, or its chord in PERFORM's voicing) and its LED is on; the bar's
  note-offs do not cut it. SHIFT + a palette pad sounds and writes nothing. Leaving the screen lets a held pad go.
- PATTERN (Q4): with step pads held, a palette pad writes every held step and the cursor stays. UP and DOWN are now the octave (PERFORM's and
  SAMPLE's, one press one step, the strip says it) instead of the lane; the lane is NOTE, INSTR, VOL, FX and F2.
- PATTERN, LEVEL and SLICE lanes (Q5): a LEVEL palette pad sounds the cursor step's note or chord at that level while held (a rest sounds
  nothing); a SLICE palette pad fires that slice of the SPEAKS slot, the code shared with SAMPLE's pads. CARRIER stays silent.
- PATTERN: COPY copies the cursor's step, INSERT puts the copy there and moves the steps after it right (the last one inside LENGTH
  falls off), DELETE empties the step. With SHIFT they work on the whole pattern: SHIFT + COPY copies it, SHIFT + INSERT pastes it
  over the current one (LENGTH too), SHIFT + DELETE empties it after F1 confirms.

### Fixed

- SAMPLE REC: DELETE while a take only waits for the bar line cancels it, as REC does, and the slot keeps its sound; it used to empty the
  slot. A second DELETE empties it. DELETE while the take records still throws it away.
- SAMPLE slices (issue #3): a slice pad plays its cut once and stops at its end cut (TRIG ONE SHOT is the default now; GATE also stops it
  at the release), and a new hit restarts it from its start cut (CHOKE ON fades out the slot's sounding slice). A slice no longer pauses
  the slot's whole loop and lets it resume one slice behind: it turns the loop off, LOOP reads OFF, and F5 starts it again. The same holds
  for a slice from PATTERN's SLICE lane or from MIDI. SAMPLE's play line shows what is left of each sounding slice.
- PATTERN: a step pad past LENGTH no longer moves the cursor off the pattern.
- Work no longer stops when a screen button leaves its screen:
  - A save, an open or a WAV run on FILE goes on to its end; it used to be cancelled silently, and an open left part of the old
    project's values. What it said is on the strip at the next visit.
  - A take recorded with REC on PERFORM ends at its bars on any screen and the slot takes its side; it used to record on to its 16 s
    limit on PATTERN, SONG, MASTER or FILE.
  - The SONG chain plays on under any screen; PLAY stops it.
- PERFORM: a tap on F1 MODE steps round (ROBOT back to BANDS); it used to stop at ROBOT. The encoder still stops at the ends.
- The boot screen and the header strip read the project's version (it said 0.1.0). PERFORM's HELP no longer names a development slice.
- The manual: what a project keeps (the patterns, the chain, the route, the vocoder settings, the tempo, the current pattern, the
  octave, the chord kind, the pad mode, the slot data) and what it does not keep yet; PATTERN's F8 SAVE writes the patterns, not the
  project; a chord latches when SHIFT is pressed while a chord pad is held, not the other way round; QUALITY is TALK's detail, not
  a number of bands.
- HELP: PATTERN, SAMPLE and SAMPLE EDIT no longer list REC, which does nothing there; PARAMS no longer speaks of a later slice, names
  F8 LEVEL on every page and leaves blank keys unlabelled; SHIFT + PARAMS no longer reads "the old play screen" on PARAMS, PATTERN and
  the SAMPLE screens.

## v0.2.0 (pre-release)

A sound release: TALK is reworked to behave like a talkbox, and BANDS is darker. It is a pre-release and needs more testing.

### Changed

- TALK as a talkbox (`docs/ENGINE-REALISM.md`, "TALK as a talkbox"):
  - On consonants the carrier ducks (down to -26 dB) and the voice's own air, low-passed, comes through instead, as when the
    mouth closes on a real tube. Vowels keep the carrier; a level gate keeps the breath shut in the pauses.
  - The carrier's top is rolled off (a one-pole at 3.5 kHz, a smaller first difference), like a driver and a tube.
  - A damped comb on the carrier, as a 0.9 m tube between the driver and the mouth.
  - A gentle 2:1 compressor on TALK's output (5 ms attack, 120 ms release).
  - A microphone's proximity lift (+3 dB below 200 Hz) and the carrier's leak from the box at -32 dB, heard even with the mouth shut.
- BANDS: less noise injected in the top bands on consonants, and a one-pole low-pass after the bands, so the sum is less bright.
  THRU and DRY are not dulled by it.
- Every new value is a starting point for the ear. Measured on the host against a read voice and five dry/processed vocoder pairs
  (measurements only; no audio is in the repository).

### Fixed

- `app_test` passes again: the presentation video's scenarios moved to `tools/video/scenarios/`.
- The CHANGELOG's CPU line for v0.1.0 now gives the silent readings.

### Added

- A GitHub Actions workflow that runs `tools/ci.sh host` and `mk66` only when a release tag is pushed, against the o3-sdk commit in
  `tools/o3sdk.ref`.
- `tools/video/`: the scripts that rebuild the presentation video from the simulator.

### What a unit has shown

Read on a unit on the release candidate (`v0.1.0-17-g2de46c7`, the same code as the tag), each row a silent read (`cpu reset`, 60 s with
nothing sent, one `cpu` read); the render's limit is 180,240 cycles at the peak (69 % of the block):

- BANDS, eight notes: peak 143,521 (54.9 %); with four loops playing: 170,596 (65.3 %, as in v0.1.0).
- TALK at Quality 16, a held chord: 132,863 (50.8 %); at Quality 24: 143,499 (54.9 %); at Quality 24 with four loops playing:
  173,741 (66.5 %, about 7k over v0.1.0's 166,762, inside the limit).
- ROBOT, a held chord: 144,404 (55.2 %).
- No underrun, mute, late block, hardware fault or shed in any row; no crash record.

Not tried on a unit: how the new TALK and BANDS sound (the values are starting points for the ear), DIN MIDI, an outside hardware
sequencer. The app still swaps `system.o3s` and its `.bak` on the card at boot (known since v0.1.0).

## v0.1.0 (pre-release)

The first public release. It is a pre-release and needs more testing.

### What it does

- Four modes: BANDS (a 16-band channel vocoder), TALK (an LPC talkbox), TUNE (pitch correction to a key and scale) and ROBOT (the bands
  with a fast compressor).
- Sources: the line input, the microphone, USB audio, the radio, and samples on either side of the vocoder.
- An internal carrier synth of eight voices (SHAPE, SPREAD, SUB, TILT, NOISE), played from chord pads: twelve roots, triad, seventh, open
  and power rows, four voicings (CLOSE, OPEN, WIDE, SMOOTH), a KEYS mode and a bend row. The SHIFT row holds FREEZE, GATE, ARP, GLIDE,
  FORMANT, THRU, BEND ROW and PRESET.
- Four sample slots of 16 s, with up to 24 slices each, edited on the unit (trim, speed, direction, slices).
- A step sequencer: eight patterns (A to H) of 1 to 16 steps with NOTE, CARRIER, LEVEL and SLICE lanes, and a SONG of up to 16 rows
  with repeats.
- MIDI in: notes, pitch bend, mod wheel vibrato, program change (picks the pattern), notes from 36 that fire slices, MIDI learn and a
  clock to follow.
- Projects saved to the card as `project.o3a` under `Apps/o3_voc03er/`, with the sample slots as WAVs.

### What a unit has shown

Read on a unit during development (`docs/PLAN.md`, sections 4 to 4l):

- The render's CPU stays under its limit on the readings taken, with BANDS, eight carrier notes and four loops running (about 62 to 67 % of
  the block at the worst peak on the last readings), with no underrun and no shed in any of them. TALK at order 16 is inside the budget by
  its mean.
- A project round trip works: speed, direction, level and trim of a slot come back after a save and a load, and the other slots stay as
  they were. Saving the WAVs takes about 2 s a slot, and so does loading.
- MIDI over USB works.

### Needs more testing

- The sound is judged by ear on the unit, and that listening is still owed for most of it: the glides, the gate edges, the vibrato, the
  presets' levels, the carrier's tone, the formant steps and the clicks at the source switch. A spoken sentence has to be checked for how
  well it can be understood, and the microphone source has had little use.
- The radio's signal was not measured on a known station. Line and radio were reported as working by the maintainer, not by a reading.
- The CPU rows were read in silence (nothing sent to the console during the window, which was found to add about 10k cycles to the peak)
  for BANDS only. TALK, TUNE and QUALITY 24 were last read with console traffic, inside the limit by their means, and have not been read in
  silence.
- MIDI has been read over USB only; a DIN cable has not.
- Many controls and screens were checked in the simulator and over the console, not by hand on the unit.
- The manual and its pictures are new.
