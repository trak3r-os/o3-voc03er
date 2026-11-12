# Changelog

## Unreleased

### Added

- **MC1: capture the mouth.** PATTERN, MOUTH lane, SHIFT + F8 CAPTURE reads the slot SAMPLE shows and writes the lane: per step the nearest of
  TRACT's nine vowels (MOUTH 1 to 9) by the distance between mean-removed log-areas, or a closure (LIPS) where the step's level is 30 dB under
  the take's loudest step. The analysis is TALK's at QUALITY 16 (fs/4, order 12, pre-emphasis 0.9, the same window and lag window, the TF1
  reflection-to-area conversion) on a window centred in each step. The nine references are what that same analysis reads from the TRACT tube
  itself for each vowel (built once per sample rate, cached), so there is one source of truth for the vowels. It runs from update() one piece a pass
  (a level chunk of 4096 samples about 0.06 ms, a step 20k to 30k cycles, the references 9 passes of about 4 ms once: estimates that assume the
  SDRAM read cost, to be measured on the unit) and shows READING THE SLOT n %; a second press cancels, and nothing is written before the end. The
  level pass first removes the take's mean (a DC offset would hide the closures). If the slot is recorded over or reloaded while it runs, or the
  pattern shown changes, it cancels and the lane stays as it was. The console command blocks the main loop while it runs (about 50 ms [est]). A slot of bars gives 16 steps a bar, any other take is spread over
  the pattern's LENGTH. Console: `mcap <A-H> [1-4]`. No new parameter (64 of 64), nothing in `config.o3a`, the pattern file is unchanged; about 5.2 KB of
  SDRAM. Tests in a synthetic TRACT: nine places at several pitches with noise and a vibrato, at 44.1 and 48 kHz, closures (also under a DC offset), short and silent takes, the step mapping and
  the pieces equal to the one shot (`tests/mouth_capture_test.cpp`); `app/scenarios/mouth_capture.txt` shows the flow only (a tone has one shape). Needs
  more testing on real voices on the unit: nearest of nine vowels, consonants only as closures, O and U, E and I confused most.
- **TF1: FORMANT works in TALK.** It was set and ignored there. Once per analysis hop the reflection coefficients become the areas of the
  lossless tube the lattice is, the area function is stretched along the tube by 2^(st/12) (linear in area, a transparent extension past the
  open end) and goes back to reflection coefficients, so the voice's formants move up or down by the same factor as in BANDS, and the gain
  follows the shifted filter so the level holds within 2 dB. Going down, the lattice runs more sections (ceil(n / s), at most 18) so the
  mouth end is not cut off. The shift is rounded half up like BANDS' (2.5 is 3, -2.5 is -2). At 0 nothing runs and the output is bit for bit as
  before (pinned in the test against digests of the previous `talk.cpp`). It works best within about +-6: at QUALITY 16 and 24 up to +6 the
  first two formants land within 5 % (the third within 12 %), +12 pulls the first formant 12 to 19 % low and loses the third; down is
  weaker (the first formant within 20 % at QUALITY 16 and 6 % at 24, the second 13 to 25 % high, the top one fades), and QUALITY 8 (order 6)
  is only useful going up (+3, +6). Cost [est]: 0.5k to 1.0k cycles on the hop's block (every second block, not averaged) plus up to 0.6k on
  every block going down (the longer lattice); the CPU is owed on the unit. No new parameter (id 40). Needs more testing by ear; `tests/talk_test.cpp`,
  `docs/ENGINE-REALISM.md`.
- **SLICE CH: slices on their own MIDI channel.** A setting with the values SAME (the default, today's behaviour: notes from 36 on your
  channel fire slices), 1 to 16 and OFF. With a channel, the notes of that channel fire the slices of the SPEAKS slot (36 is slice 1) and
  never play the synth or write into PATTERN's NOTE lane, while the notes of your channel always play the synth; OFF never fires a slice.
  Set it on SAMPLE with SHIFT + F2 or in the USB console with `slicech [same|1..16|off]`; saved in `config.o3a` (an older file loads as
  SAME). With IN CHANNEL on OMNI it cannot work, and SAMPLE shows SET MIDI CH. Needs more testing with a real controller, USB and DIN;
  run in the simulator only (`app/scenarios/midi_slice_channel.txt`, `tests/midi_slices_test.cpp`, `tests/midi_slice_logic_test.cpp`).

### Fixed

- **NO1: a note sounds until its last holder lets go.** The vocoder unit keeps an owner mask per note (0 MIDI, 1 pads, 2 step sequencer,
  3 console; carried in `Event.flags` bits 4-5, 64 B of SRAM). A NoteOn under another owner's hold only sets the owner's bit and does not
  retrigger; a NoteOff clears the bit and the note ends only when no owner is left; AllNotesOff and the unit's reset clear the masks. The
  pad, sequencer (including ratchets) and console senders set their owner on both the on and the off. So a held audition pad no longer
  loses its note to the step's gate off, to Stop, or to a repeat of the same note in the pattern, and the other way round. Per event the
  cost is an OR over four words; nothing per sample. Two things follow from it: a shared note keeps the level it started with (a step's
  LEVEL does nothing to a note a pad already holds), and a MIDI note whose release never arrived (a cable pulled) is no longer ended by
  pressing its pad: play the key again, or send all notes off. Needs more testing on the unit (`tests/note_owner_test.cpp`,
  `tests/pattern_audition_test.cpp`; `tests/stepseq_test.cpp` now expects the sequencer's owner flags).
- **LS1: a slot's loop stays on the bar.** A playing loop that was recorded (or loaded as a WAV) in whole bars is re-synced at the exact
  frame of every bar line, with a crossfade of up to 64 samples, so it no longer drifts against the tempo; at half speed, double speed and
  reverse too. A FREE take and a WAV that is not a whole number of bars run free as before; at a constant tempo whose bar is a whole number of samples (120 BPM) the loop is bit-exact, at others (123 BPM) a seam off by up to 8 half-samples is snapped without a crossfade and a larger one crossfades. No time-stretch: when
  the tempo drops the loop wraps and replays its start until the line, when it rises the loop is cut at the line. On transport Stop the loop
  keeps sounding free-running, and the next Start re-anchors it at bar 1. The loop's cost on a block without a bar line is one load and
  compare per slot.
- **LS1: a take that cannot fit is refused.** LENGTH 8 needs at least 120 BPM, 4 at least 60, 2 at least 30 (a slot holds 16 s). REC does
  not arm and the strip says TOO LONG AT THIS TEMPO (PERFORM: TOO LONG, AT THIS TEMPO). Only the LENGTH no longer fitting acts (a tempo change that still fits
  leaves the take alone), and with a 0.5 BPM margin under the limit against FOLLOW jitter (REC arms at the exact limit): an armed take is cancelled; a
  recording one ends at the next bar line, shorter than LENGTH, and says TEMPO CUT THE TAKE SHORT; if the slot fills first it ends free and says
  TEMPO FILLED THE SLOT.
- **LS1: whole-bar status is judged again at every load.** A WAV, a project slot or a take is whole bars if its length is a whole number of
  bars at the transport tempo of that moment; the count survives a load of the same length, a copy of a slot of the same length carries it, and
  every slot's anchor is reset when bar 0 passes. A new status bit (`on_bar`) tells the screen which take ended on the bar. Two slots of exactly
  equal length count as a copy: the one history dependence of a project load.
- **LS1: stopping a loop is a 64-sample ramp**, not a cut, so it no longer clicks; starting it again during the ramp works and `playing()`
  is false from the stop on.
  Tests: `tests/loop_sync_test.cpp` (123 BPM over 40 bars, seam snap, adopt and copy, re-anchor, stop tail, take outcomes), `tests/sample_rec_test.cpp`
  (`fits`, `still_fits` margin, screen cases), `tests/loop_test.cpp` (tail drained); scenario `app/scenarios/sample_rec_long.txt` (TOO LONG, and
  TEMPO CUT THE TAKE SHORT under a MIDI master). Needs more testing on a unit: the crossfade and the snap by ear, FOLLOW jitter around the edge, the CPU on bar-line blocks, the edge at
  8 bars and 120 BPM. Left out on purpose: a tolerance for a REC press just after the line (it would break the whole-bar length).

## v0.4.0 (pre-release)

A mode release: TRACT, a fifth mode that plays a mouth after the carrier, with a MOUTH lane in the pattern; PERFORM plays a layout of
rows; and a long list of fixes. It is a pre-release and needs more testing, most of all TRACT by ear. (There is no v0.3.0.)

### What a unit has shown

Read on a unit on the candidate `v0.2.0-40-g7327b2a` (TRACT V1 to V3b; v0.4.0 adds only the documents and the console's `tract accent`):
it boots and draws; BANDS idle 92.0k cycles a block (35 %); TRACT idle 52.4k (20 %, the mouth at rest with no carrier); the highest read
since boot 115.1k (44.0 %, at a mode change); no underrun and no block shed. Not tried on a unit yet: TRACT by ear (the words, WOB, the
closures, the accent), the CPU while the mouth moves with eight notes and four loops, a project with a mouth line saved and opened on the card.

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
- SAMPLE plays the layout (Q7): pads 0 to 23 stay the slice pads, and the bottom two rows play PERFORM's rows A and B (the upper one A, the
  lower one B) at PERFORM's octave, each as it is set there: note, chord, piano or nothing. A pad sounds while it is held and leaving the screen lets
  everything go. F1 is PLAYS (it sets the chosen row, or the row of a held bottom pad) and SHIFT + F1 is ROW (A or B; the row's pads go dark
  for a moment). The chord page and the latch of a chord on SAMPLE are gone (a chord is held with the hand, as on PATTERN's
  palette), and SAMPLE no longer handles DELETE. SAMPLE EDIT's lower pads play the same rows.


### Added

- **TRACT, a fifth mode: a mouth after the carrier.** Press F1 until it says TRACT. Nothing is analysed: the sound you play goes through a
  model of a vocal tract that you shape with your hands. You play a vowel, a jaw, the lips, a closure, and the pattern can write them.
  - PERFORM: set row C to MOUTH (F6, then F7) and the row is a strip: nine vowels from A to U, which stay when you let go, and the closures
    LIPS, TIP and BACK while held. F8 is VOWEL. The voiceprint draws the mouth. Outside TRACT the row is dark.
  - PARAMS has a fourth page, TRACT: VOWEL, TONGUE, JAW, LIPS, GLIDE, SIZE, RES and HISS. The values are live in every mode and speak in TRACT.
  - PATTERN has a fifth lane, MOUTH (F2, or FX on a second press). A step holds a vowel, a vowel that glides (GLD), a closure, one of eight
    words (WOW, YEAH, YOI, WAH, OW, BA, DA, GA) or a wobble between vowels (WOB, six rates). F6 SET flips the palette between the vowels and the
    words, and F7 FILL writes a random line of vowels. On a step with a note, a LEVEL above 100 opens the jaw a little more.
  - The project keeps the MOUTH lane and the TRACT page. A project saved with a word or a WOB does not open in the earlier TRACT build.
  - TRACT says vowels, w, y, b, d, g, p, t and k, as sung words a listener recognises when told what they are. It cannot say nasals, l, r or a
    held s. It needs more testing by ear, and so does its load on the processor while the mouth moves.
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

- FILE: F8 gives up a running save or open (CANCEL on the soft key and in HELP). A cancelled save leaves the old file whole and a cancelled
  open changes nothing; the restore that follows an open finishes on its own (HELP says so), as stopping half way would mix two projects.
- FILE: opening a project while a SONG chain plays starts the new chain at its first row once the lap in progress ends. The chain used to keep
  its old row, past the end of a shorter new chain, and stop there.
- PATTERN: a pattern picked by hand (a pattern pad, the PATTERN key) while a SONG chain plays no longer asks the sequencer for it. The pick used
  to play for one lap against the chain, and on the chain's last lap it kept the chain from ending. PATTERN still shows and edits the pick.
- SAMPLE, SAMPLE EDIT and SAMPLE REC treat a slot whose WAV is still on its way after a project open as taken, as PERFORM does: they say
  LOADING, REC and DELETE refuse it (REC could record into it, and the landing then overwrote the take or was overwritten), and SAMPLE
  EDIT's edits (COPY, TRIM, NORMALISE, SAVE WAV) wait until the last WAV has landed (COPY took the slot for an empty target).
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
- SAMPLE LOAD pressed while FILE runs a save, an open or a WAV run says FILE IS BUSY on the screen that is up; it used to do nothing.
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
