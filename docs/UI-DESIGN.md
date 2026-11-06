# O3-VOC03ER: the user interface and the skin (design)

Status: design, 2026-11-06, nothing built. It answers the maintainer's asks of this date: an intuitive interface that uses every button under its
printed name and a skin of its own; a sample that can be cut into slices and played from the pads; any of line in, mic, USB in, radio or a sample
as the source of either side; an internal carrier synth made for vocoding, with chords from the pads as the first thing a new user reaches. The
mock is `docs/ui-mock.html` (every main screen at 800 x 480 in the skin). What the engine is stays `../o3-sdk/docs/VOCODER-DESIGN.md` and `docs/PLAN.md`; this document changes how it is played and
names the engine work the new parts need (section 8). Every cost is [est] until a unit reads it.

## 1. The idea in one line

**Something speaks, something sounds.** The instrument is one signal path, drawn left to right on every screen: SPEAKS (the modulator) on the
left, the VOICEPRINT (the 16 bands, the formant curve or the pitch ladder, live) in the middle, SOUNDS (the carrier) on the right. Each side
picks its source:

| Side | Picks from |
|---|---|
| SPEAKS | LIVE (the one live source: LINE, LEFT, RIGHT, MIC LOW, MIC HIGH, RADIO or USB), SLOT 1 to 4 (a sample, whole or by slices) |
| SOUNDS | SYNTH (section 6), NOISE, LIVE, SLOT 1 to 4 |

The maintainer's two modes are the two common pairs: **REALTIME** is LIVE > SYNTH (sing, play chords), **SAMPLE** is SLOT > anything (a recording or
a WAV speaks through a carrier chosen apart). They are not a switch: the header always shows the pair in force ("MIC > SYNTH", "SLOT 2 > LINE"),
PERFORM changes it with two keys, and the two homes (PERFORM for playing, SAMPLE for the slots and slices) are reached by PERFORMANCE and SAMPLE
PLAY without touching the routing. BANDS, TALK and TUNE stay as they are: the three ways the voiceprint is taken and applied, on F1 of either
home. TUNE corrects what SPEAKS and runs no carrier.

**The four loops become the four slots.** A slot is 16 s of mono audio in the existing 5.6 MB bank, recorded (SAMPLE REC, or a capture) or loaded
(SAMPLE LOAD), cut into up to 24 slices (SAMPLE EDIT), played whole as a bar-aligned loop or slice by slice from the pads (SAMPLE), on whichever
side picked it; a slot on no side speaks when its slices are played, so slices work in either role. Nothing of the loop engine is lost (speed,
direction, level, the per-step carrier) and no new SDRAM goes to audio.

## 2. The one rule, drawn

The codec has one ADC, shared by the microphone, the line jack and the radio; the USB return, today, replaces it rather than joining it
(`AudioIn::select` picks one source; `in_l` / `in_r` carry that one; the return next to the codec is a kit change, section 9). So the machine has
**one LIVE** at a time, and the interface says so instead of refusing: both pickers show a single LIVE row with the source's name, and a second
live source on the other side is listed dimmed with the words CAPTURE FIRST. The ways out are shown on the same row:

- **USB is stereo.** With USB as the live source SPEAKS takes USB L and SOUNDS takes USB R: a computer can feed both sides live at once. The LIVE
  row reads "USB L" on one side and "USB R" on the other.
- **Capture, one gesture.** REC on PERFORM records the live source into the next empty slot (at the bar, LENGTH bars, 2 by default) and, when the
  take ends, the side that held LIVE takes that slot: LIVE is free. The chip under the keys says what will happen before it does ("REC: CAPTURE
  MIC > SLOT 2"). Picking a dimmed live source on the other side pre-arms the same thing: after the capture the picker switches the ADC to the
  new source and gives it to that side. Two presses from "mic speaks through the synth" to "the captured voice speaks through the radio".

The same rule explains why a live carrier cannot change per step (the ADC settles 200 ms): the PATTERN's CARRIER lane offers SYNTH, NOISE and
the slots, and LIVE only as the value the pattern leaves alone (FOLLOW).

## 3. Every physical button

The 33 buttons are `app::Button` (`../o3-sdk/kit/app/include/o3sdk/app/panel.h`): F1 to F8 are eight of the 33 (codes 0 to 7, the soft keys under
the screen), then 25 others. The printed caps are the ones the kit's HELP draws (`help_overlay.cpp`, `kKeys`): PARAMS, PERFORMANCE, FILE, CONFIG,
MASTER; SAMPLE LOAD, SAMPLE PLAY, SAMPLE EDIT, SAMPLE REC, SONG; NOTE, INSTR, VOL, FX; PATTERN; PLAY, REC, COPY, INSERT, DELETE, SHIFT; UP, DOWN,
LEFT, RIGHT. The maintainer's words "instrument", "perform", "loader" and "backspace" are PARAMS, PERFORMANCE, SAMPLE LOAD and DELETE in that table;
the jobs below read true under either wording. CONFIG is the kit's and gets no job.

Rules the table follows: a screen button opens its screen (a second press pages where there are pages); the arrows mean the same everywhere
(UP / DOWN: *which*, a slot, a lane, a row, a page; LEFT / RIGHT: *where*, a step, an octave, a cursor); DELETE removes what the cursor is on,
with SHIFT all of it; REC always records; PLAY is always the transport.

| Printed | What it does | Where |
|---|---|---|
| F1 .. F8 | The screen's eight values or actions, the kit's convention: a tap latches the encoder on it, hold + turn moves it for a moment, two held move together | every screen (section 4 lists each row) |
| PARAMS | Opens PARAMS, the engine's values in three pages (CARRIER, SHAPE, TUNE); pressed again, the next page | everywhere |
| PERFORMANCE | Opens PERFORM, the home: chords, the voiceprint, the routing | everywhere |
| FILE | Opens FILE: the projects on the card (load, save, save as, new) | everywhere |
| CONFIG | The kit's system Config; SHIFT + CONFIG is HELP, drawn by the kit from each screen's `help()` | the kit's |
| MASTER | Opens MASTER: the live source and gain, monitor, dry, output level, the USB return, the radio's station; the tall IN and OUT meters | everywhere |
| SAMPLE LOAD | Opens the WAV picker and loads the chosen file into the selected slot | everywhere |
| SAMPLE PLAY | Opens SAMPLE: the slots and their slices on the pads | everywhere |
| SAMPLE EDIT | Opens SAMPLE EDIT: trim and cut the selected slot (by hand, evenly, by beats, by hits) | everywhere |
| SAMPLE REC | Opens SAMPLE REC: record the live source into the selected slot at the bar | everywhere |
| SONG | Opens SONG: the chain of patterns A to H (slice U9). Until U9 it opens the stub, like every screen not yet drawn | everywhere |
| NOTE | The pattern's NOTE lane: opens PATTERN with that lane selected (the pads write notes or chords into the step) | everywhere |
| INSTR | The pattern's CARRIER lane (what sounds on that step: FOLLOW, SYNTH, NOISE, SLOT 1 to 4) | everywhere |
| VOL | The pattern's LEVEL lane (the step's velocity) | everywhere |
| FX | The pattern's SLICE lane (the slice of the SPEAKS slot that fires on that step). The weakest of the four caps: HELP says "LANE: SLICE" | everywhere |
| PATTERN | Opens PATTERN, the 16-step sequencer (note or chord, carrier, level, slice per step) | everywhere |
| PLAY | Starts or stops the transport: the pattern, the bar lines that takes and loop starts wait for | everywhere |
| REC | PERFORM: capture (section 2). SAMPLE REC: start the take at the bar, again to stop at the bar. SAMPLE and SAMPLE EDIT: arm the selected slot (SAMPLE REC opens armed). PATTERN: write arm, the pads record into the step under the playhead | all but the lists |
| COPY | PATTERN: copies the step under the cursor (SHIFT: the whole pattern). SAMPLE EDIT: copies the slot, sample and cuts, to the next empty slot (F1 confirms) | PATTERN, SAMPLE EDIT |
| INSERT | PATTERN: pastes the copied step at the cursor, the rest moving right (SHIFT: pastes the pattern). SAMPLE EDIT: inserts a cut at the cursor | PATTERN, SAMPLE EDIT |
| DELETE | What the cursor is on: PATTERN the step (SHIFT: the pattern); SAMPLE EDIT the cut at the cursor (SHIFT: the slot's sample, F1 confirms); PERFORM the latched chord; SAMPLE every sounding slice (SHIFT: and the latched chord); SAMPLE REC the take just made; the lists nothing, dimmed in HELP | as listed |
| SHIFT | Held: with a pad latches it (a chord stays while the hands leave); with the encoder coarse steps; with DELETE, COPY, INSERT their whole-object form; with CONFIG HELP. Four combinations and the kit's two, no more | everywhere |
| UP / DOWN | Which: the slot (SAMPLE, SAMPLE EDIT, SAMPLE REC), the lane (PATTERN), the row (FILE, SAMPLE LOAD, SONG), the page (PARAMS, MASTER); on PERFORM the chord type of the bottom row (section 7) | everywhere |
| LEFT / RIGHT | Where: the octave of the keys or the chord grid (PERFORM, SAMPLE, PATTERN); the cursor's step (PATTERN); the previous or next cut (SAMPLE EDIT); the take's length (SAMPLE REC); a page of rows (the lists). Held, they repeat (`repeat_buttons`) | everywhere |
| Encoder | The latched F value; in a list the row; in SAMPLE EDIT the cursor, a sample fine, SHIFT coarse (a 64th of the sample) | everywhere |

## 4. The screens

Ten screens, all with the skin's own chrome (`Chrome::None`, `Screen::theme()` the skin's theme, section 5). Each fills `help()` so SHIFT + CONFIG
explains it: a row for every button the screen answers (a button with no row draws hollow, which says it does nothing), and a legend with the groups of the page in force only. The kit's Config and HELP stay the kit's and come up with full chrome.

### PERFORM (PERFORMANCE): the home

Left, SPEAKS: the source chip (LIVE with its name, or SLOT n), a vertical IN meter with a CLIP lamp, the capture chip ("REC: CAPTURE MIC > SLOT 2").
Middle: the voiceprint with the MODE word over it. Right, SOUNDS: the carrier chip, the chord being played in big type ("Dm7", "C5") with its
notes as blue tiles, the OUT meter. Under all of it a drawing of the pads as they are now: the chord grid or the keys, with the octave.

- F1 MODE (BANDS, TALK, TUNE) · F2 SPEAKS (LIVE, SLOT 1 to 4) · F3 SOUNDS (SYNTH, NOISE, LIVE, SLOT 1 to 4) · F4 KEY · F5 SCALE · F6 CHORD
  (TRIAD, 7TH, OPEN, POWER: the kind of the bottom chord row, section 7) · F7 PADS (CHORDS, CHORDS + BEND, KEYS) · F8 by mode: BANDS SPREAD (the synth's unison), TALK QUALITY, TUNE SPEED.
  A tap on F2 or F3 opens its picker as a list over the column (UP / DOWN or the encoder, the key again or F1 takes it, F8 backs out); the
  dimmed rows are the rule of section 2.
  Built in U3b: this replaces the second-tap-steps rule for F2 and F3 only (the other F keys keep it); the same key again takes the row the cursor is
  on, F1 too, F2 and F3 switch lists, F4 to F7 and SHIFT close the list and act, the pads, LEFT / RIGHT and the other keys work as usual under it.
  A dimmed LIVE source arms a capture (the chip reads "REC > SLOT, THEN <source>"; REC runs it); a dimmed slot only says IN USE. REC takes the
  first slot that is empty, on no side and not recording (none: "NO EMPTY SLOT"; there is no re-record), records 2 bars (`kCapBars`; the
  stop is written in the middle of the second bar while the transport plays, so the unit stops at the bar line, and at once when it is stopped),
  and REC again cancels while it waits for the bar or stops early while it records. The ADC and the route change in one step, when the loop holds audio.
  The pickers have no NONE and no FOLLOW row: section 2 lists neither; a slot on no side is SAMPLE's F2 SIDE, and FOLLOW is a value of the pattern's CARRIER lane.
- Pads: CHORDS (default, section 7) or KEYS (a four-octave keyboard, today's `pad_map.h`); LEFT / RIGHT an octave; SHIFT + pad latches; DELETE
  releases. REC captures.
- LEDs: section 7 for CHORDS; KEYS: white keys `kLedDim`, held or latched `kLedOn`, the pattern's notes `kLedMid` while it plays.

### SAMPLE (SAMPLE PLAY): the slots

Left: the four slots as a column (the selected framed: name, length, 0.5X / 1X / 2X, FWD / REV, the side it is on in that side's ink). Middle:
the selected slot's overview waveform with its cuts, the play line moving, the sounding slices lit. Right: SOUNDS as on PERFORM, the chord.

- F1 CHORD (the same value as PERFORM's F6; MODE stays on PERFORM and PARAMS) · F2 SIDE (SPEAKS, SOUNDS, NONE: which picker this slot goes to; the pickers of PERFORM see the same thing) · F3 SPEED (0.5X, 1X, 2X)
  · F4 DIR (FWD, REV) · F5 LOOP (the whole slot plays round, from the bar: ON, OFF) · F6 TRIG (GATE: a slice stops at the pad's release; ONE SHOT:
  to its end) · F7 CHOKE (ON: a new slice of this slot cuts the one sounding; OFF: they overlap) · F8 LEVEL (-24 to +12 dB). UP / DOWN the slot.
- **Pads, the split.** The top two rows (pads 0 to 23) are slices 1 to 24 of the selected slot. The bottom two rows are the chord grid's two
  lower rows (the 12 roots as triads and as sevenths, section 7) or, with PADS = KEYS, a two-octave keyboard. A slice held with a chord pad is the
  gesture of the instrument: the slice sings that chord. SHIFT + a chord pad latches it so one hand plays slices over it. The split, not a
  toggle, because the two things are played together; a toggle would cost a press per phrase. The octave or the chord row moves with LEFT / RIGHT.
- LEDs: a slice that exists `kLedDim`, the one sounding `kLedOn`, the SLICE lane's `kLedMid` on its step; the chord rows as section 7. An empty
  slot: the top rows dark and the screen says "EMPTY: SAMPLE REC OR SAMPLE LOAD".
- Polyphony: four slice voices in all, the four loop readers (section 8). A fifth press takes the oldest, with the reader's 64-sample level
  ramp, so it cannot click. CHOKE ON (the default) gives a slot one voice, the sliced-vocal feel.

### SAMPLE REC

The slot's name and state in big type (EMPTY, ARMED, blinking REC with the bars counted, STOPPING, then the length), the IN meter tall, the live
source and its gain, the transport's bar counter. REC arms (the take starts at the next bar line, at once with the transport stopped) and REC
again stops at the bar; LENGTH stops it by itself. After the take the slot plays whole, looped (today's behaviour), on the side AFTER says,
so what was sung is heard at once.

- F1 SOURCE (LINE, LEFT, RIGHT, MIC LOW, MIC HIGH, RADIO, USB; a line source starts at gain 0) · F2 GAIN · F3 LENGTH (FREE, 1, 2, 4, 8 BARS; also
  LEFT / RIGHT) · F4 AFTER (the side the slot takes: SPEAKS, SOUNDS, NONE) · F5 AUTOPLAY (ON, OFF) · F6 MONITOR (the kit's: the dry input to the output while
  recording) · F7 SLOT (1 to 4; also UP / DOWN) · F8 blank. DELETE discards the take.
- Pads: the chord grid or the keys, so the carrier is played while recording (the take is the input, not the output). While recording the whole
  grid pulses `kLedLow` on the beat (`PadLeds::blink`), the one place the LEDs do that.

Built in U4a. DELETE empties the slot in the unit (Rec 2 CLEAR, then back to 0), so every screen sees it empty; in a take it throws the take away.
Built in U4b. No progress bar: LoopStatus has no play position, so the middle shows the slot name, length, state and side. F2 SIDE moves the slot (it leaves the other side first); it refuses only an empty slot or a route `allowed` rejects, and says why. F1 shares PERFORM's chord kind; LEFT/RIGHT is the octave; SHIFT and DELETE latch and release as on PERFORM. Only pads of the lower two rows reach the chord; F6 and F7 came in U5b.
A refused AFTER leaves the slot on no side and the screen says which side holds it; AUTOPLAY OFF places nothing; a slot already on a side just plays
again. Leaving the screen cancels an armed take and stops a running one at the bar, on no side. F2, F3, F6 and F7 step on a second tap, F5 on every tap.

### SAMPLE EDIT

The overview waveform across the screen, tall, the cuts as marks numbered 1 to 24, the trim as two brackets, the cursor a bright line with its
time under it. The encoder moves the cursor; LEFT / RIGHT jump cut to cut; INSERT makes a cut at the cursor, DELETE removes the one at it. A pad
of the top rows auditions that slice through the vocoder with the chord of the bottom rows (the same split as SAMPLE).

- F1 CUT EVEN (OFF, 8, 16, 24 equal parts; turning re-cuts) · F2 CUT BEATS (OFF, 4, 8, 16 a bar, on the bars the take was recorded on; dimmed for
  a loaded WAV) · F3 CUT HITS (0 to 100: the sensitivity of the onset detector, 0 off; the cuts follow it as it turns) · F4 START and F5 END (the
  trim) · F6 NORMALISE (action, F1 confirms) · F7 SAVE WAV (`Samples/slotN_NNN.wav` by the kit's `save_wav`, the strip shows the progress) · F8 CLR
  CUTS. COPY copies the slot to the next empty one (F1 confirms).
- F1 to F3 replace the table and say so before the value is taken ("REPLACES 5 CUTS"); INSERT and DELETE edit it. A cut lands on a zero crossing
  within 64 samples when one is there.

### SAMPLE LOAD

The kit's `FilePicker` (`app/file_picker.h`) over the app's folder, `.wav` only, with the file's size and seconds in the value column; F1 OPEN loads
it into the selected slot (named at the top, UP / DOWN change it), F8 BACK. 16-bit PCM at 44.1 kHz, mono or stereo (mixed to mono), at most 16 s
(a longer file gives its first 16 s and says so). The listing is `Apps/o3_voc03er/`, because the kit loads from the app's folder only (section 9);
the card as a USB disk (Config, CARD AS DISK) is how files get there from a computer.

### PATTERN

Sixteen steps as cells with four lanes under each (NOTE, CARRIER, LEVEL, SLICE), the playhead in the voice ink, the cursor framed, the lane in
force highlighted, the pattern letter A to H large at the left. The NOTE lane holds a chord (root, type, voicing, from the grid) or single notes
(from the keys). Pads: rows 2 and 3, columns 0 to 7, are steps 1 to 16 (two rows of eight; `kLedDim` with content, `kLedMid` the cursor, `kLedOn`
the playhead); columns 8 to 11 of those rows are patterns A to H (`kLedMid` the current, `kLedOn` the one playing); rows 0 and 1 are the lane's
palette: NOTE the chord grid's two lower rows or two octaves of keys, CARRIER six pads (FOLLOW, SYNTH, NOISE, SLOT 1 to 4), LEVEL 24 steps of
velocity, SLICE slices 1 to 24. A palette pad writes into the cursor's step and moves the cursor on (the tracker's step entry); a step pad moves
the cursor there (SHIFT + step pad: a rest in the lane).

- F1 PATTERN (A to H) · F2 LANE (also NOTE, INSTR, VOL, FX and UP / DOWN) · F3 LENGTH (1 to 16 steps) · F4 TEMPO · F5 VALUE (the cursor step's
  value in the lane, the encoder edits it) · F6 CLEAR LANE (action) · F7 FOLLOW (the cursor rides the playhead) · F8 SAVE (FILE's save, here).

Built in U6a (data and sequencer, no screen; `stepseq.h`, `tests/stepseq_test.cpp`): a step is one 32-bit word (note, kind, voicing, carrier, level,
slice), the bank of eight patterns is `O3SDK_BIG`, and the hook reads one word per step with a relaxed atomic, so an edit is heard whole at the
step's next visit. A chord step stores its root as the note, so the old PLAY screen keeps working on pattern A. Settled where the spec was open:
the gate is 75 % of a sixteenth, notes also end at Stop; carrier 0 (FOLLOW) emits nothing, and a Stop restores the carrier the user picked only
if a step changed it (LIVE is never a lane value); LEVEL 0 means 100 and is sent as the NoteOn velocity: the last note sets the synth's level, 100 and up being full, so a level above 100 sounds as 100 and the
lane only turns a step down; a soft note also lowers what is still held, because it is one gain and not one a voice. It acts on the SYNTH and NOISE carriers, not on a slot or LIVE; a requested pattern starts at the playing pattern's end (or at the next Play); SMOOTH voicing leads from the
previous step's notes; `pattern.o3a` v2 adds a BANK chunk after the v1 chunks (an older reader gets pattern A), a v1 file loads into A only,
and a file that fails any check loads nothing. The console has `pat` (print, `len`, `note`, `chord`, `car`, `lvl`, `slc`): a step's slice (0 none, 1 to 24) fires that slice of the SPEAKS slot (U6c).

Built in U6b (`pattern_screen.cpp`, pure logic in `pattern_logic.h`, `tests/pattern_screen_test.cpp`, `app/scenarios/pattern.txt`): the cells show a
lane per row (NOTE name and chord kind, CARRIER, LEVEL, SLICE drawn empty), the palette pads write the cursor's step and move it on, a step pad moves
the cursor, SHIFT + step pad rests the lane in force, a pattern pad shows that pattern and asks the sequencer for it at the end of the one playing.
F1..F5 point the encoder at a key (a second tap steps it, wrapping), F6 clears the lane at once, F7 is FOLLOW, F8 saves through the app's FILE job.
The F6 label is `CLR LANE` (it does not fit the key in full); HELP spells it out. SLICE is read-only (U6c). No new unit parameter.

Built in U7a (`sample_edit_screen.cpp`, pure logic in `sample_edit_logic.h`, `tests/sample_edit_test.cpp`, `app/scenarios/sample_edit.txt`): the cursor and the
cut table of the selected slot. The encoder moves the cursor a sample at a time, SHIFT + encoder a 64th of the slot; INSERT cuts at the cursor (it snaps to
the nearest zero crossing within 64 samples when there is one), and a cut less than 64 samples from another cut, or from the slot's end, is refused (TOO CLOSE).
DELETE needs the cursor exactly on the cut it removes (LEFT and RIGHT put it there); the cut before takes its samples, and the first cut stays. F1 is CUT EVEN
(OFF, 8, 16, 24); when the cuts are the hand's (INSERT or DELETE since the take or the last CUT EVEN) the first try on F1 or F8 only says REPLACES n CUTS and a
second one takes it; any other key closes the warning. The pads audition the cuts as on SAMPLE, and a new take of another length starts again from eight even
cuts. The cuts live in RAM until U8 and U9 save them, and the screen draws no waveform until U7b. F1 tap points the encoder at CUT EVEN; UP, DOWN, LEFT, RIGHT,
INSERT and DELETE return it to the cursor. F2 to F7 say COMES LATER. Rec does nothing special here, as on SAMPLE. No new unit parameter.

Built in U7b (`overview.{h,cpp}`, pure logic in `overview_logic.h`, `tests/overview_test.cpp`, `app/scenarios/overview.txt`): the overview waveform of each slot. Per slot

Built in U7c1 (`sample_edit_screen.cpp`, `overview.{h,cpp}`, logic in `sample_edit_logic.h` and `overview_logic.h`): the big picture is 744 x 96 and spans the rectangle, one function maps a sample to x for the picture, the cursor and the ruler; the small picture comes from the same peaks, and a slot goes up in chunks, one a frame, shown only after the last. F2 CUT BEATS re-cuts OFF, 4, 8, 16 a bar under F1's rules and caps at 24 with a note on the strip. Nothing records that a slot came from a take, so a slot of a whole number of bars at the current tempo counts as one; a take at another tempo reads as a WAV and F2 is dimmed. F6 NORMALISE scans and scales in slices, F1 confirms, to -1 dBFS, refused while recording and on silence. SHIFT + DELETE empties the slot through Rec value 2, F1 confirms.
the main loop keeps 384 columns of min and max (int16, SDRAM) and two L1 pictures in the app's RAM_G: 384 x 48 (2,304 bytes) and 240 x 48 (1,440 bytes, the same
columns merged), side by side, 3,744 bytes a slot, from byte 10,240 of the app region (after the skin's assets), four slots up to byte 25,216, each slot sent
in one upload. They are drawn through bitmap handle 0 (`kHandleApp`, the one the kit leaves to the app) as one sprite tinted in the side's ink (SPEAKS, SOUNDS,
or the neutral rule colour); the kit cannot scale a bitmap, so every picture is 1x: SAMPLE EDIT centres the 384 one in its rectangle, SAMPLE draws the 240 one above
the play line, SAMPLE REC beside the slot cards, and the cuts, the cursor and the play line stay rects over it. Full height is the slot's own peak, never below
about -40 dB (silence stays a flat line); a column lights the rows from its minimum to its maximum, one row when they are the same. A rebuild starts when a slot's
length changes (a take starts it, length 0 clears the picture at once), one slot at a time in slot order, 4,096 samples a main-loop pass (about 170 passes for a
16 s slot); the old picture stays until the new one is up. The upload runs from `draw()` (the only place with the display), one a pass; a refused one is tried
again 3 times, then dropped until the next length change. Nothing runs in the render. RAM cost: about 7 KB of SDRAM, a few bytes of SRAM_U. A sprite costs 7 display-list words. No new unit parameter.

Built in U6c (`sample_logic.h` publish_slices, `stepseq.h`, `tests/slice_lane_test.cpp`, `app/scenarios/slice_lane.txt`): a step's slice fires a cut of the
SPEAKS slot only, on that slot's own reader (Start, Len, Trig 1 at the step's frame), always as one shot (TRIG and CHOKE do not apply). Stop releases it
(Trig 0, once). A slice above the slot's cut count stays silent, and LEVEL does not touch a slice. The palette's two upper rows are slices 1 to 24, lit up to
the count; with no slices a palette pad or the encoder says SPEAKS HAS NO SLICES and writes nothing. No new unit parameter.

### PARAMS

Three pages of eight values on the F row, the voiceprint small above them so a change is seen. CARRIER: the synth of section 6 (SHAPE, SPREAD,
SUB, TILT, NOISE, ATTACK, RELEASE, LEVEL). SHAPE: MODE, QUALITY (24 shows "OVER BUDGET AT 8 NOTES" in the Rec ink: `docs/PLAN.md` 4g), FREEZE,
DRY, VOICING, then the band parameters when they exist. TUNE: KEY, SCALE (shared with the chords), SPEED, AMOUNT, NOTES IN. PARAMS again or UP / DOWN page.

### MASTER

The signal path drawn large with the one rule written on it: LIVE (SOURCE, GAIN, MONITOR, the tall IN meter with CLIP, "ONE LIVE SOURCE: MIC,
LINE AND RADIO SHARE THE ADC; USB IS STEREO") → the mode → OUTPUT (DRY, LEVEL, RETURN LEVEL for USB, the tall OUT meter) and, with RADIO, FREQ
with F7 / F8 seeking. F1 SOURCE · F2 GAIN · F3 MONITOR · F4 DRY · F5 LEVEL · F6 RETURN · F7 SEEK DOWN · F8 SEEK UP (dimmed unless RADIO).

### FILE

The projects: the folders under `Apps/o3_voc03er/` (the picker with folders), F1 LOAD, F2 SAVE, F3 SAVE AS (the kit's `NameEntry`), F4 NEW, F5 SAVE
WAVS, F8 BACK. A project is one `project.o3a` (the eight patterns, the chain, the routing, each slot's cuts, trim, speed, side and the WAV it came
from, the engine's values) plus the slots' WAVs saved on request (4 x 1.4 MB at most; on a slow card about 17 s a slot, the strip shows it). A load
reads the `.o3a`, then the WAVs it names from the project folder, one job after another, a slot greyed until its file has landed.

### SONG (U9)

The chain: up to 16 entries of a pattern letter and a repeat count, a row each; F1 ADD, F2 REPEATS, F3 LOOP CHAIN, F8 BACK; PLAY plays the chain
from the cursor while this screen is up. Before U9 the one-line screen of section 3.

## 5. The skin

**OR NOIR, the only skin, chosen by the maintainer on 2026-11-06 ("me gusta el OR NOIR", "solo ese, por defecto").** Its idea, frames, tokens and costs are the OR NOIR section of `docs/ui-skins.html`, which replaces 5.1, the token table of 5.2 and the look of 5.3 below: gold speaks, chrome sounds, on a black wall of round LEDs, the voiceprint as sixteen upright columns of dots. There is no skin byte and no seam: the screens draw OR NOIR directly. What still stands below: the rule of the two inks, what each mode's voiceprint shows and where its values come from, the grid and type of 5.4, the budget of 5.5, the memory of 5.6. Text uses `>`, since the faces hold U+0020 to U+007E only. F6 on PERFORM is CHORD (section 4), whatever the pictures show.

**The F row, by the maintainer on 2026-11-06.** The metal is gold, not steel: pale gold, gold and brass (`Fx` is brass). Each key's name sits on a gold bar under its value window, y 453 to 478, right above the physical key, so the name is below y 452 and the kit's strip hides it for about 3 seconds at a time, while the value stays. That frees 15 px above: the chord grid's cells are 27 px high, with their names in the Label face, and one line under the grid, y 403, holds the pads' gestures on the left and, on the right, what the F key under the encoder does. This replaces the F row of 5.4 and the picture in `docs/ui-skins.html`.

### 5.1 Point of view

A **voiceprint reader**: a dark slate instrument whose only bright things are the signals. One rule gives it its character and its legibility:
**whatever speaks is drawn in the voice ink, whatever sounds in the carrier ink, and a slot changes ink with the side that holds it.** No glass,
no knobs, no seven-segment windows, no texture bitmaps: flat panels, 1 px rules, round-cornered chips, discs and lines, every one a primitive the
kit's `Renderer` and `Canvas` draw today. Nothing imitates any product's screen; the layout is the signal path, which is this instrument's idea.

### 5.2 Tokens

Text is drawn in theme tokens (`Renderer::text` takes a `Color`), shapes in any RGB with alpha (`Canvas`). The theme is the app's `ui::Theme`
returned from `Screen::theme()`; it must pass `kContrastPairs` (a host test like o3-303's `skin_theme_test`) and keep Chrome visibly apart from Pane
for HELP's caps.

| Token | RGB | Role in this skin |
|---|---|---|
| Pane | 11, 16, 32 | the slate: the frame's clear and every pane |
| Chrome | 22, 29, 52 | panels, chips, the F-row slots, HELP's caps |
| ChromeText | 232, 236, 245 | text on a chip |
| ChromeDim | 140, 150, 184 | captions on a chip |
| Text | 232, 236, 245 | values |
| TextDim | 140, 150, 184 | captions, units, ticks |
| Empty | 40, 50, 84 | rules, empty cells, black keys, out-of-scale roots |
| Play | 200, 245, 49 | **the voice ink**: the voiceprint, the IN meter, SPEAKS and whatever is on that side, the playhead, PLAY |
| PlayText | 11, 16, 32 | text on the voice ink |
| Sel | 76, 125, 255 | **the carrier ink**: SOUNDS and whatever is on that side, the chord's notes, the selected row |
| SelText | 255, 255, 255 | text on it |
| Accent | 255, 210, 63 | the cursor and the latched F key, nowhere else |
| AccentText | 11, 16, 32 | text on it |
| Rec | 255, 59, 92 | recording, CLIP, a refusal, an over-budget value |
| Instr | 255, 138, 91 | the sample's own colour where no side applies: a cut mark, a slice number, a slot's name |
| Vol, Fx | 140, 150, 184 | unused by the skin |

Alpha is used in one place: the latched chord's tiles (the carrier ink at 160 under a held one at 255).

### 5.3 The signature: the voiceprint

The middle of PERFORM, PARAMS and MASTER is one 320 x 224 panel that shows what the engine is doing, in the voice ink, from values the unit
publishes once a block (section 8, `View`):

- **BANDS**: sixteen horizontal bars, 125 Hz at the bottom to 8 kHz at the top, each bar's length the band's follower (log, 48 dB across the
  panel). One `Canvas::rect` a band: 32 words. The unvoiced path lights a small "SSS" chip at the top right above a quarter of noise.
- **TALK**: the LPC envelope as a polyline of 32 `Canvas::line`s (64 words) over the same axis, from the published reflection coefficients: the
  main loop steps them up to direct form once per publish (16 x 16 multiplications) and evaluates the magnitude at 32 frequencies (32 x 16 complex
  terms), about 2,000 floating operations at 25 Hz. FREEZE turns the line the carrier ink.
- **TUNE**: a vertical pitch ladder of the scale's notes (rungs in Empty, the KEY's roots in TextDim), the detected pitch a voice-ink disc, the
  target rung in the carrier ink, the correction a short line between them; unvoiced: the disc goes dim. From `shift_cents`, `target_note`,
  `voiced`, published as words.
- **SAMPLE, SAMPLE EDIT, SAMPLE REC**: the slot's overview waveform, an L1 bitmap the app renders in the main loop into its RAM_G region
  (`Display::upload`, as the kit's wave view does: 384 x 48 px a slot, 2,304 bytes, four slots 9.2 KB) and draws as one sprite tinted in the side's
  ink; the cuts, the trim, the cursor and the play line are `Canvas::line`s over it. The bitmap is rebuilt in slices of 4,096 samples a pass of the
  main loop after a take, a load or a trim (about 170 passes, the old picture shown meanwhile), never in the render.

### 5.4 Type and grid

The kit's four faces, nothing else: Display 16 x 32 (the mode word, the chord name, the slot's state on SAMPLE REC), Data 10 x 20 (values), Label
8 x 16 (captions), Micro 6 x 12 (ticks, the pads line). Upper case throughout (HELP demands it; the screens match it), except the chord names,
whose small letters are the convention ("Dm7").

The grid: an 8 px module. Header y 0 to 40: the wordmark `VOC03ER` in Display (live text, no bitmap), the routing chip "MIC > SYNTH", the MODE word,
TEMPO, the transport, the CPU figure (`Context::cpu_percent`). Content y 40 to 404, three columns 240 / 320 / 240 on PERFORM and SAMPLE, 16 px
gutters. The F row y 404 to 480, eight slots of 100 px: the key number as a 6 x 12 tab, the name in Label, the value in Data at y 424, a 4 px bar
at y 446; the latched slot's bar and tab in Accent. A key's name is at most 9 characters (the slot's width in the Label face). Nothing that matters below y 452: the kit's system strip is drawn there (`strip_area()`).

### 5.5 Budget, and the half-speed main loop

A frame is at most 2,048 display-list words. Estimated worst frames: PERFORM BANDS about 1,100 (header 120, SPEAKS 180, voiceprint 32 + its glyphs
60, SOUNDS 160, the pads drawing 110, F row 320, chrome 120); SAMPLE EDIT about 1,000 (the overview is one sprite; the cuts 24 x 2, the numbers
24 x 2); PATTERN about 1,400 (16 x 4 cells of 3 glyphs). The simulator's line load and frame digests measure every skinned screen in each slice; a
screen past 1,800 words loses its glyph count first.

`docs/PLAN.md` 4g: at the worst audio load the main loop runs at about half speed. So the UI's work per frame is kept small and bounded: the
voiceprint reads eight words; nothing is recomputed that did not change (the chord name, the F row's texts, the slot chips are cached in the
screen and redrawn from the cache); the overview and the onset scan run in 4,096-sample slices; and when `cpu_percent()` is above 60 the screens
frame at 15 Hz (every other frame skipped by the app's own counter; `redraw()` still forces one), which halves the main loop's own share.

### 5.6 Memory

SRAM_U has about 0.7 KiB above its floor, so no screen keeps more than its cursor there: every table (the slice tables, 4 x 24 x 8 B; the eight
patterns with their chords, 8 x 16 x 6 B; the chain; the picker's 24 entries, 1.2 KB; the hop energies of the onset detector, 4 x 2,756 x 2 B =
22 KB; the overview staging) lives in one namespace-scope `O3SDK_BIG` struct in the SDRAM (about 36 KB) that the screens hold a pointer to; a
main-loop read there is 29 cycles. The screens themselves are 20 to 60 bytes each, under 400 bytes in all; `docs/PLAN.md` 4e still stands: the
first slice moves cold unit state out before any of this is linked.

## 6. The carrier synth

What a vocoder wants of its carrier is not what a bass synth wants: **energy in every band at every pitch** (the top bands starve on a plain saw
above middle C, so the sibilant bands have nothing to shape), **density** (unison makes the voice wide and the robot a choir), **noise** for the
consonants (shared with the unvoiced path, as today), a **pitch that does not wander** (TALK's lattice rings on a stable spectrum), and
paraphony of eight notes for chords. Today's `Carrier` has two polyBLEP oscillators a voice, DETUNE and NOISE; it is correct and thin.

The replacement keeps the `Carrier` API (`note_on`, `render`, the shared `NoiseSource`) and the budget, **about 12.7 % of a block at eight
notes** (33,000 cycles, 64 a sample a voice [est], the measured cost of two oscillators at 34 each), and offers:

| Control | Range | Why |
|---|---|---|
| SHAPE | SAW .. PULSE .. NARROW (a morph, with the pulse width on it) | the harmonic series' tilt |
| SPREAD | 0 .. 100 | unison: a third oscillator a voice at -SPREAD and the second at +SPREAD cents, mixed under the first; 0 keeps two oscillators and today's cost |
| SUB | 0 .. 100 | an octave below, a square: the chest of the voice in the lowest bands |
| TILT | -12 .. +12 dB | a one-pole shelf about 1 kHz: brightness without a filter sweep, the top bands fed |
| NOISE | 0 .. 100 | today's noise mix, gated by the notes |
| ATTACK, RELEASE | today's | |

Three oscillators at 64 cycles a sample a voice is the budget's edge; the design pays for SPREAD with the slice readers' room (section 8) and
reads it in U10. The source is a later `dsp-scout` job through the `dsp-port` licence gate: the candidates are a Plaits-style virtual-analogue
oscillator (MIT; `../o3-sdk/docs/FREAK-DESIGN.md` names it as the kit's first port) or an original stack of polyBLEP oscillators with the known
unison technique. Nothing is recreated by a product's name and nothing comes from the official firmware. The synth's controls live on PARAMS
page CARRIER; SPREAD is also PERFORM's F8 in BANDS, because it is the one that is played.

## 7. Chords from the pads

The first thing a new user reaches. KEY and SCALE are the TUNE parameters, so the chords and the pitch correction agree by construction.

**The grid** (PADS = CHORDS): the 12 columns are the 12 roots, C at the left of the octave LEFT / RIGHT chose; the row is the chord's kind.
Row 3 (bottom): the triad the scale gives on that root (major, minor or diminished, by the scale: a chord is never out of key unless the root
is). Row 2: the seventh the scale gives. Row 1: the open chord, root, fifth, ninth (sus2-like, the airy vocoder pad). Row 0 (top): the power
chord, root, fifth, octave (the classic robot fifth). A root outside the scale still plays (its nearest diatonic quality) and is lit dimmer.
F6 CHORD picks what the bottom chord row is (TRIAD, 7TH, OPEN, POWER; on SAMPLE it is F1; UP / DOWN on PERFORM do the same), so a kind is one press away when fewer rows are chords: the
SAMPLE split, or the bend strip of section 7b. TRIAD is the default.

**Voicing** (PARAMS, page SHAPE): CLOSE (as stacked), OPEN (the third up an octave), WIDE (across two octaves), SMOOTH (the inversion nearest the chord before:
voice-leading for free, the default). The chord's notes become `note_on`s to the unit, up to five, inside its eight voices.

**LEDs**: roots in the scale `kLedDim`, roots out of it off (visible by their silence in the row), the held or latched pad `kLedOn`, the other
kinds of the same root `kLedLow` while it is held (the column lights up: "this root"), the pattern's chord `kLedMid` on its step. Monochrome
LEDs cannot name a chord: the screen does, in Display type, with the notes as tiles.

**Keys** (PADS = KEYS) are a piano on PERFORM (PLAY keeps its chromatic map). Row 2 (from the top, 0) holds the white keys, C D E F G A B C D E F G of the octave LEFT / RIGHT chose; row 1 the black keys on the column of the white key to their right (C# D# F# G# A# C# D# F#: columns 1, 2, 4, 5, 6, 8, 9, 11); row 0 the controls, below; row 3 is dark and silent, or the strip when BEND ROW is on. LEDs: the key's root `kLedLow`, the scale `kLedDim`, outside the scale off, held `kLedOn`.

Row 0 by column: 0 OCT-, 1 OCT+, 3 VOL-, 4 VOL+, 6 ROOT-, 7 ROOT+, 9 MOD, 10 BND-, 11 BND+; columns 2, 5 and 8 are dark and silent. No control plays a note, and a note held when the octave moves ends with the note it began with. OCT steps the octave. VOL steps the unit's `Level` (PLAY's OUTPUT LEVEL) by its coarse step, 6 dB, held to -48..6. ROOT steps `Key` round the twelve. The hint line shows the new OCT, VOL or ROOT for a moment. A VOL or OCT pad at the end of its range is off; ROOT, BND and MOD stay `kLedDim`; a held control `kLedOn`. BND- and BND+ and MOD act while held (section 7b). F7 PADS has two values, CHORDS and KEYS; BEND ROW is independent of them, SHIFT + F7 toggles it and F7's value shows "+B".

## 7b. Pitch bend

Asked by the maintainer on 2026-11-06, in two forms. Both bend the SYNTH carrier only: a live source has no pitch to move, and a slot reads at three fixed speeds. Bending the carrier
under a held vowel is the classic robot glide. The unit gets one parameter, `Bend` in semitones, appended after the ids of section 8, applied to the oscillators' increments through a
`dsp::Ramp` once a block, so a stepped input arrives as a glide; the cost is a multiply a voice a block [est].

- **The encoder.** On PERFORM the encoder bends once it is free: PERFORMANCE pressed while PERFORM is up frees it, and a tap on any F key takes it back (a second tap on the focused F key keeps stepping its value). Boot focus is F6. A detent is range / 8, SHIFT coarse range / 2, held to +-range. An encoder has no spring, so the bend returns to 0 about 120 ms after the last detent. BEND RANGE (+-2, +-5, +-12 semitones, default +-2) is set with the console command `voc bendrange <2|5|12>`. SAMPLE keeps its own encoder.
- **The pad strip.** BEND ROW (SHIFT + F7), on either F7 value: the bottom row becomes a strip: columns 6 and 7, the middle pair, are 0, and each pad outwards is one semitone, -5 at the left end and +5 at the right. Pressing bends, with the glide of the ramp; releasing returns to 0; two held, the last one wins. In CHORDS the chord rows move up one and the row above the strip plays F6 CHORD's kind, so no kind is lost: the three rows are that kind, 7TH and OPEN, or the next ones when that kind is one of them. In KEYS the piano does not move (row 3 was free). LEDs: the middle pair `kLedDim`, the held pad `kLedOn`.
- **BND- and BND+ (KEYS, row 0).** While held, the local bend grows linearly to -BEND RANGE or +BEND RANGE in 400 ms (the main loop's `uptime_ms`, written only when it changes); on release it returns to 0 through the unit's ramp. The last pressed of these and the strip wins. It sums with the MIDI wheel (clamped +-12).
- **MOD (KEYS, col 9) and `Vib` (id 43).** The unit adds a vibrato to the bend: about 5.5 Hz, +-0.5 semitone at depth 1. At Vib 0 the output is bit-identical to Bend 0 and the phase resets. Held, the MOD pad raises the depth linearly to 1 in 400 ms; released, it falls to 0 in 400 ms. MIDI CC1 writes Vib too (when it is not learned): the larger of the pad's and the wheel's depth wins.

### The SHIFT row of PERFORM

Asked by the maintainer on 2026-11-06 ("mete todos"): FORMANT, FREEZE, GATE, ARP, GLIDE and presets, with no new pad modes, since the maintainer wants few. They live on a second row of the F keys, on both F7 values. While SHIFT is held the F row shows this row, names and values. SHIFT + an F key focuses its value, the same rule as the plain row: the encoder moves it, a second SHIFT + tap steps it, and the row stays shown while one of its values has the focus.

| Key | Value | What it does |
|---|---|---|
| SHIFT + F1 | FREEZE | Holds the vowel (`Freeze`), on or off, an action |
| SHIFT + F2 | GATE | Cuts the carrier in time: OFF, 1/4, 1/8, 1/16, 1/32 |
| SHIFT + F3 | ARP | Plays held notes one at a time: OFF, UP, DOWN, UP-DOWN, RANDOM |
| SHIFT + F4 | GLIDE | Slides between the carrier's notes, 0 to 500 ms |
| SHIFT + F5 | FORMANT | `Formant`, -12 to +12 st |
| SHIFT + F6 | THRU | `Thru`, 0 to 100 |
| SHIFT + F7 | BEND ROW | As above, an action |
| SHIFT + F8 | PRESET | One of eight factory sounds |

- **GATE and ARP** run in the unit on one step clock taken from the tempo, so pads and MIDI notes go the same way and a step lands on its frame. The clock runs with the transport stopped and restarts at the bar when it starts. GATE has soft edges of about 2 ms against clicks, a 50 % duty, and costs a multiply a sample only while on. ARP's rate is GATE's when GATE is on and 1/16 otherwise. GATE cuts any carrier, a loop playing as the carrier too. With the transport stopped and the arp idle the first key pressed restarts the step clock and sounds at once; with it running the note waits for the grid.
- **GLIDE** slews each voice's increment once a run, so it costs nothing in the sample loop.
- **FORMANT strip.** On KEYS with BEND ROW off, row 4 picks the formant: -12, -9, -6, -4, -2, 0, 0, +2, +4, +6, +9, +12. A tap sets it and it stays, the pad lit, because a formant is a voice to pick and not a gesture to spring back from. FORMANT at 0 is the cheap path.
- **Presets** are a constexpr table of parameter values in flash, each a whole sound: ROBOT, CHOIR, WHISPER, GIANT, ELF, PULSE, ARP, FROZEN (`app/include/o3_voc03er/presets.h`, starting points for the maintainer's ear). Each sets the same 15 parameters (Mode, CarSrc, Wave, Noise, Attack, Release, Detune, Quality, Formant, Thru, Glide, Gate, Arp, Vib, Freeze) and never KEY, SCALE, the source, the loops or the transport; the encoder or a second SHIFT + F8 tap applies one at once. Saved user sounds wait for FILE in U9.

Still proposed and not decided: SCALE (only the scale's notes), STACK (a pad turns its note on and off) and VOICE (the carrier plays the sung note and pads add intervals). VOICE needs TUNE's pitch detector beside BANDS, about 4 to 5 points of CPU [est, TUNE 45.3 % against 40.9 % idle], which takes BANDS 8 from 57.3 % to about 62 %, past the 60 % budget.

## 7c. MIDI in

Asked by the maintainer on 2026-11-06: the carrier, the slices and the patterns played from MIDI in, pitch bend included, so that the machine can sequence what it recorded, from an outside sequencer or from its own PATTERN.

- **The carrier.** `ctx.midi().route(unit, 0)`: the notes of the user's channel reach the unit at their frame, about 1.5 ms after they came in, and play the SYNTH carrier beside the pads, sharing its voices. `Vocoder::apply` takes them already and also gives them to TUNE, so they serve NOTES-IN too. Only SYNTH sounds them: another carrier has no notes to play.
- **Pitch bend.** `App::midi()` scales the message into BEND RANGE and keeps it as the wheel's bend, held apart from the local one (encoder, strip, BND pads); `Bend` is their sum, clamped +-12. The encoder's spring does not touch the wheel's part: the sender holds its own wheel. CC1 (not learned) is the vibrato depth, `Vib`. As in 7b it moves the SYNTH carrier only; a slot reads at three fixed speeds, and bending one needs a variable-speed read that this design does not have.
- **Controllers.** The kit's MIDI learn, up to 16 bindings, on any parameter of the unit.
- **Clock.** FOLLOW in the system Config, as the kit gives it.
- **Slices.** A note fires a slice, the velocity sets its level. These go by `route` and not through `App::midi()`, because at the worst load the main loop runs at about half speed. The unit tells them from the carrier's notes by a note range: a unit has one route, so a range and not a channel (settled in M2, below).
  Built in M2 (`vocoder.h` `bind_slices`, `tests/midi_slices_test.cpp`, `app/scenarios/midi_slices.txt`): the SPEAKS slot only, as the SLICE lane (same published SliceMap). Unmarked notes from 36 fire one slice each (36 is slice 1, up to the count); every other note stays the carrier's. One shot: the note's release does not end the slice. Velocity 100 and up is full level, below that v/100, taken when the slice starts. A slice is refused (the note does nothing) while the reader records. The app's own senders mark their notes with `Vocoder::kNoteLocal`, so a MIDI note and a pad note of the same number are told apart. Not run on a unit.
- **Patterns.** A program change picks pattern A to H at the next bar line, through `App::midi()`, where the main loop's delay does not matter. MIDI notes record into PATTERN's lanes as the pads do, quantised.
- **Ports.** USB was measured in o3-303 on 2026-11-01, not at this load. DIN has not been run: there is no interface at the bench.

## 8. What the unit and the app need

**The unit (`units/vocoder/`, parameters appended, never inserted; ids 40 and 41 are FORMANT and THRU, slice V2f of `docs/PLAN.md`; 42 and 43 are Bend and Vib, slice U2b; 44 to 46 are Glide, Gate and Arp, slice P2, with no StepSync: the unit reads the tempo and the transport from `ProcessArgs::transport`; these from 47 on):**

| Id | Name | What |
|---|---|---|
| 42 | Bend | semitones, -12..12: the SYNTH carrier's increments, once a block through a `dsp::Ramp` (20 ms) |
| 43 | Vib | 0..1: a ~5.5 Hz vibrato, +-0.5 st at 1, added to Bend; 0 is bit-identical to no vibrato |
| 47 | ModSrc | 0 LIVE (today), 1 NONE, 3 .. 6 SLOT 1 to 4 (the values of CarSrc, for one table): what speaks. With a slot, the live input leaves the modulator sum (no microphone bleed) |
| 48 | LiveCh | 0 L, 1 R: the channel a live carrier reads. The app writes R with USB (section 2): SPEAKS takes USB L, SOUNDS USB R. Built in U3c |
| 49 .. 60 | Reader n Start, Len, Trig (3 x 4) | a region play: Trig 1 at a frame starts reader n at Start for Len samples at the reader's speed and direction, Trig 0 releases it (GATE) or it ends by itself (ONE SHOT). Start counts from the start of the bank, not of a slot (both exact in a float up to 16.7 M; the bank is 2,822,400), so any reader plays any slot and CHOKE OFF can overlap slices of one slot. Speed, direction, level and role are the reader's own `Loop n` values: the app copies the source slot's before the Trig. A whole-loop play on that reader pauses under the slice and resumes at its position |
| 61 | Load | the length of a sample the app wrote into a slot's memory from the main loop while the slot was stopped (seam fades baked by the app), so the render adopts it (`Loop::adopt`; refused while the slot records, plays or has a region): one value, slot x 705,600 + length with length 1..705,600, exact in a float (slot 3, 16 s is 2,822,400, so a value reads one way: slot = (v - 1) / 705,600). 0 does nothing, and the app writes 0 back after every Load so a saved project holds none. Built in U7c2a. One id and not four: `kMaxParamsPerUnit` is 64, and U10's SUB and TILT need 62 and 63 (`Wave` and `Detune` become SHAPE and SPREAD). That fills the table |
| CarSrc | value 7 LIVE | `in_l` on the carrier bus: the line, the radio or USB R sounds. A routing inside the unit, instant and click-free with the same 5 ms ramp; the ADC source itself is the app's one `AudioIn::select` |

The slots' roles (`Loop n Role`) stay the unit's mechanism; the app sets them from the two pickers. `Loop` grows `start_region(i, a, b)` and a
per-reader end; `mix()` is unchanged in cost. The carrier synth of section 6 replaces `Carrier` behind its API. Published for the screens, as
`LoopStatus` is today (a single writer, word-sized atomics, once a block): `View` with the sixteen band followers as 8-bit logs in four words, the
unvoiced mix, the talk coefficients as 16 x 8-bit in four words at each publish, the tune triple, and each slot's position in a word. About 60
bytes of atomics in the unit, no SDRAM; the packing under 300 cycles a block [est].

Built in U5a, ids 49 to 60 (R1Start on). One command a block and reader, the last wins; a release in the block of its start cancels it. A whole loop asked for while a region sounds waits at position 0 and starts when the region ends, off the bar line. Regions under 128 samples can end on a step of up to 2/64, and under 256 samples the loop under them is not ducked: both far below the shortest cut SAMPLE EDIT makes. A region stays in its memory: CLEAR on a slot does not stop another reader that is reading it.
Built in U5b. Pads 0 to 23 are slices 1 to 24 of the selected slot (even cuts, 8, rebuilt when the slot's length changes; a slot shorter than the count gets one sample a cut). A press picks a reader (`pick_reader`: CHOKE ON the slot's own, OFF a free one, else the oldest slice voice; never a recording reader or another slot's whole loop) and writes Speed, Dir, Level and Role from the source slot, then Start, Len, Trig 1: at most 7 writes, 56 for eight pads against 64 a block. Role is CAR only for a slot on SOUNDS. The first slice on a reader keeps its Speed, Dir, Level and Role and the end of the region writes them back; SAMPLE shows slot k from the kept values while k is borrowed, and its edits wait there. Known edge, unit check: a borrowed reader whose own slot is on a side follows that side (CarSrc/ModSrc beat Role in `run_loops`). F6 TRIG (GATE, ONE SHOT) and F7 CHOKE (ON, OFF) are per slot, not saved. The play line is a bar, a tick per cut and a head per reader inside the slot.

**Polyphony and cost.** A slice voice is a loop reader: 3,000 to 8,000 cycles a block from the unit reads (1x to 2x, `docs/PLAN.md` 4f), so four at
once are 1.1 to 3 % each, the 4.6 to 12 % the four loops already cost; past four the oldest is stolen. BANDS with eight notes and four readers at
2x is the 72.9 % peak already measured (4g): slices add no new worst case, and SAMPLE's defaults (CHOKE ON, a four-note chord) keep the ordinary
case near 55 %. A fifth reader is not planned; SPREAD's third oscillator is read on the unit before it is kept at eight notes.

**The app:** the two pickers and the capture gesture (the routing as pure state, host-tested: every pair and what the screen says of it); the chord
engine (root, kind, scale quality, voicing, SMOOTH's nearest inversion: pure, host-tested); the slice tables and their edits; the onset detector
(one main-loop scan of the slot in 4,096-sample slices computing a 256-sample hop energy, then peaks above the running mean by the sensitivity, 50
ms apart at least: pure, host-tested on a drum WAV and a sung phrase); the WAV reader (RIFF, 16-bit PCM, mono or stereo to mono, through
`Files::load` into an SDRAM staging region and then into the stopped slot, with the 64-sample seam fades `Loop` expects); the overview renderer;
the patterns A to H with chords, and the chain; `project.o3a` (the chunk container, as `pattern.h`); the LPC envelope and the voiceprint drawing;
the skin's theme and its contrast test; every screen's `help()` checked with `help_check`.

## 9. What would need the kit, and the smallest alternative

| Need | Kit change | Alternative taken |
|---|---|---|
| A live analogue source and USB in at once (mic speaks, the computer sounds) | the USB return reaching the units as a second input pair beside the codec's (`VOCODER-DESIGN.md` section 7, second item): `ProcessArgs` grows `usb_l` / `usb_r`, or the engine mixes the return into `in_r` | USB as the one live source is stereo (USB L speaks, USB R sounds), and a capture frees LIVE for the other side in one gesture (section 2) |
| Load a WAV from the card's `Samples/` | `Files::load` and `list` admitting "Samples" as a folder (today only `save_wav` may name it) | SAMPLE LOAD lists `Apps/o3_voc03er/` and its project folders; files get there with CARD AS DISK. Saves still go to `Samples/` |
| The IN peak while the app's screen is up | none: `AudioIn::peaks()` polled by the app (today's `InputMeter`) | |
| A theme of the app's own, no kit chrome, shapes, a wave bitmap | none: `Screen::theme`, `Chrome::None`, `Canvas`, `Display::upload` exist | |
| The F row under `Chrome::None` | none: the screen draws it (`knob_strip` is for full chrome), one pure function for every screen | |
| A take that starts on a level | none in the kit; the unit's recorder would grow a level start | not in this design: a take starts at the bar or at once |

## 10. The first five minutes

No manual, the unit just switched on. PERFORM comes up: LIVE > SYNTH, BANDS, PADS = CHORDS, KEY C, SCALE MAJOR, the stored source (LINE on the first
boot, section 11).

**To a vocoded chord.** Play something into the line jack, or pick MIC on MASTER and speak: the IN meter moves and the voiceprint flickers in the voice ink; nothing sounds yet, the carrier has no note. Press
one pad of the bottom row: a C major chord speaks your words; the screen says "C" and lights its three notes; the next pad to the right is D minor.
Zero button presses. SHIFT + that pad latches it; both hands are free for the microphone. The top row is the robot fifth.

**To a recorded loop as the carrier.** REC (1): the chip reads "CAPTURE MIC > SLOT 1, 2 BARS"; sing; the take ends at the bar by itself, the slot
plays looped and takes SPEAKS (the side that held LIVE), so you hear it through the chord you hold. F3 SOUNDS (2), DOWN to SLOT 1 (3), F3 again
(4): the recording now sounds and the microphone, free again, speaks through it. Four presses. (SAMPLE REC does the same with every setting
visible, for a take of a chosen length into a chosen slot.)

**To slices.** SAMPLE EDIT (1): the take across the screen. F2 CUT BEATS (2), turn to 16 (3): sixteen marks. SAMPLE PLAY (4): the top two rows
light; a pad plays its slice through the chord held on the bottom rows; hold a slice and change the chord under it. F2 SIDE is already SPEAKS;
on PERFORM F3 SOUNDS to NOISE makes the same slices whisper. Four presses; a WAV instead of a take is SAMPLE LOAD, UP / DOWN, F1 OPEN, three more.

**To an external carrier.** On PERFORM, F3 SOUNDS: the picker lists SYNTH, NOISE, LIVE "MIC (SAME AS SPEAKS)", SLOT 1. With the microphone on
SPEAKS the row LINE reads "CAPTURE FIRST": take it (2 presses) and REC (3): the voice is captured to a slot and speaks from there, the ADC moves
to LINE, and the external synth on the jack sounds under your recorded voice. Three presses, and the screen said what would happen before it did.

## 11. Choices, as the maintainer made them on 2026-11-06

1. **The first boot's live source:** LINE at gain 0. After that the kit stores the last one.
2. **The skin:** OR NOIR, the only one and the default. Its voice ink is gold, which replaces the chartreuse chosen earlier for the first skin.
3. **The split in SAMPLE:** 24 slices over two chord rows.
4. **After a take or a capture the slot plays by itself:** yes, AUTOPLAY ON.
5. **The chord rows' order:** as designed, power at the top, triad at the bottom.
6. **SONG:** a chain of patterns, built in U9.

## 12. Slices

Each: ctest green, no doubles in the cross build, the SRAM_U figure in the commit message, a scenario whose PNGs are looked at, HELP opened on every
screen it touches. The unit checks owed are named, not run here.

| Slice | What | Verified by |
|---|---|---|
| U1 | The skin's frame: theme and its contrast test, `Chrome::None` header and F row as pure functions, PERFORM on it with the BANDS voiceprint and the chord grid (the chord engine, KEY and SCALE shared with TUNE, voicings, the LEDs); the unit's `View` publish | `perform.txt` (a voice WAV in, a chord pad, SHIFT latch, each MODE, KEYS), the frame's line load |
| U2 | The buttons of section 3: every screen button opens a screen (stubs for the ones not yet drawn, each saying its name), PLAY the transport, DELETE, the arrows, SHIFT's four; `help()` on each | `buttons.txt` (every button once, a snap each), `help.txt` on every screen, `help_check` in the test |
| U2b | Pitch bend (section 7b): `Bend`, the free encoder and its spring, the pad strip, BEND RANGE | `bend.txt` (a held chord, the encoder and each strip pad, a WAV showing the glide and the return to 0) |
| P1 | The SHIFT row on PERFORM, FREEZE, FORMANT, THRU, the formant strip, HELP | `shift_row.txt` |
| P2 | The unit: `Glide`, the step clock, `Gate`, `Arp`, host tests | a WAV of each, by a doctest |
| P3 | GATE, ARP, GLIDE on the SHIFT row; the eight presets | `presets.txt` |
| U3 | The routing: the two pickers, `ModSrc`, CarSrc LIVE, the roles derived, the one-rule rows, the capture gesture on REC and from a dimmed row | `route.txt` (every pair, the dimmed rows snapped, a capture to a slot with its WAV) |
| U4 | SAMPLE REC and SAMPLE on the four slots as they are (whole play, side, speed, direction, level) | `sample_rec.txt`, `sample.txt` (a take through the chord, then the same as the carrier) |
| U5 | Slices: the unit's region play and triggers, the slice tables, SAMPLE's split pads, CHOKE and TRIG (the SLICE lane moved to U6) | `slices.txt` (even cuts, pads 0 to 7 in turn over a latched chord, a fifth voice stolen; WAV) |
| U6 | PATTERN with lanes, chords in the NOTE lane, pads, patterns A to H in the SDRAM; LEVEL as velocity | `pattern.txt` |
| U7a | SAMPLE EDIT: the cursor and the cut table (INSERT, DELETE, LEFT, RIGHT, CUT EVEN, CLR CUTS) | `sample_edit.txt` |
| U7b | SAMPLE EDIT: the waveform bitmap of the slot | `sample_edit.txt` grows (the snaps show the waveform) |
| U7c1 | SAMPLE EDIT: the picture across the rectangle, CUT BEATS, NORMALISE, SHIFT + DELETE | `sample_edit.txt` grows |
| U7c2 | SAMPLE EDIT: `Load` (id 61), START, END, COPY | `sample_edit.txt` grows |
| U7d | SAMPLE EDIT: CUT HITS with the scan job | `sample_edit.txt` (a drum WAV in, hits at 40 and 80, the snaps count the marks) |
| U8 | SAMPLE LOAD and SAVE WAV: the WAV reader, the picker, the load into a slot, the staging region | `sample_load.txt` (`--card` with a mono and a stereo WAV, a 48 kHz one refused) |
| U9 | TALK and TUNE voiceprints, PARAMS, MASTER with the radio's keys; FILE projects; SONG | `perform_talk.txt`, `perform_tune.txt`, `params.txt`, `master.txt`, `file.txt` (save, reboot, load, `expect file`), `song.txt` |
| U10 | The carrier synth: `dsp-scout` card and licence gate, the port or the original stack behind the `Carrier` API, SHAPE, SPREAD, SUB, TILT; a host oracle test; WAVs byte-identical with SPREAD 0 and SUB 0 and TILT 0 is not required, a sweep per band is | `carrier.txt` (each control, a sweep's WAV); the cycles a sample read in U11 |
| M1 | MIDI in, first step (section 7c): notes to the SYNTH carrier and to NOTES-IN, right after U1; then pitch bend to `Bend`, after U2b; learn and FOLLOW checked on this app | `midi.txt` (notes and a bend in, a WAV of each); on the unit, USB notes and a bend at the 72.9 % load |
| M2 | Slices by MIDI note, velocity as level, after U5 | `midi_slices.txt` (notes over a latched chord, a WAV); timing on the unit against the pads |
| M3 | Patterns by program change at the bar line, MIDI recorded into PATTERN, after U6 | `midi_pattern.txt`; on the unit, an outside sequencer with FOLLOW on |
| U11 | The unit round: the skin's frame time against the audio at the 72.9 % case and the 15 Hz policy, the LEDs' levels by eye, HELP's caps legible, the microphone, a capture through a real source, the slices heard, the synth's cost at eight notes with SPREAD | `hw-session` checklist; not a slice of code |
