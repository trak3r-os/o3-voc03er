# Changelog

## Unreleased

### Added

- **SAMPLE with one finger (rd-sam): every pad sounds alone, and the keys nobody needed are gone.** (1) *A slice pad alone brings the note:* in BANDS, TALK and ROBOT (the modes whose voice needs a synth note; not TUNE, not TRACT), with the slot on SPEAKS, no lower pad held (a SAMPLE EDIT latched note counts as held) and the sequencer not playing notes, a top pad also plays the last note or chord a lower pad played on this screen (at first the row B root chord at PERFORM's octave, through `PerformScreen::audition_press` with its own id, 24). It ends on the slice pad's release (GATE), when the slice reaches its end (ONE SHOT), on a lower pad press and on leaving the screen; another slice replaces the slice and the note stays. (2) *A lower pad alone starts the picked slice:* with no slice pad held, a slot that holds audio and speaks (or is HELD) and is neither looping nor repeating, a lower pad plays its note or chord and also starts the picked slice, the last a top pad fired on that slot (slice 1 at first, back to slice 1 when the slot or its cuts change); the last lower pad's release releases it (stops in GATE, plays out in ONE SHOT). The two decisions are pure functions in `sample_logic.h` (`slice_press_brings_note`, `lower_press_starts_slice`) with tests. (3) *F row:* F1 is SLOT (first tap lights, the second steps and wraps, the encoder stops at the ends, UP and DOWN still choose the slot), F2 SPEAKS, F3 SPEED, F4 DIR, F5 LOOP, F6 TRIG, F7 blank (no name, no value, no focus), F8 LEVEL; SHIFT + F2 SLICE CH and SHIFT + F3 OCTAVE stay. F1 LOW PADS (the lower rows play whatever the notes palette holds, set on PATTERN) and F7 SLICES MONO / POLY are removed: the screen is always MONO. The project file keeps `choke_off`, written as 0; a loaded 1 is ignored. (4) *House rule H2:* F4 DIR and F6 TRIG now light on the first tap and step on the second (they changed on the first); F2 SPEAKS and F5 LOOP are actions and act on the tap. (5) *Words:* a slot that neither speaks nor sounds reads `HELD` (tile), `HELD: ITS PADS PLAY IT` (centre line, and SAMPLE LOAD's tail) instead of PADS; the right panel adds `SLICE n`; the hint lines say the model in few words. (6) *SAMPLE EDIT arrows:* with a value key lit (F1-F5) and no slice picked LEFT / RIGHT are one encoder step of that key; with no key lit they jump cut to cut; with a slice picked they step the pick; SHIFT + LEFT / RIGHT always jump the cursor cut to cut and give the focus back to the cursor; hints and HELP say it. Scenario `sample_one_finger.txt`; `slices`, `sample`, `sample_layout`, `sample_repeat`, `file_wavs` and `ux3_load` follow the new keys. MANUAL and UI-DESIGN (SAMPLE, SAMPLE EDIT, SAMPLE LOAD) are rewritten. Needs more testing on the unit: how the pads feel under the fingers and the note's level against the slice.
- **Arrows and pads, the same everywhere; and the screens say why nothing sounds (rd-ctl).** The maintainer's wish ("pads and the way the F keys are controlled cohesive on every screen... F keys whose value can be changed with the arrows where possible") as four rules written once in `docs/UI-DESIGN.md` section 3: H1 the lower two rows are the keyboard, the top two the screen's own; H2 an F value key lights on a tap, steps on a second, the encoder changes the lit key and LEFT / RIGHT are exactly one encoder step of it (SHIFT as the encoder's, held they repeat); H3 UP / DOWN choose which (slot, page, row, lane); H4 with no key lit LEFT / RIGHT step what the encoder holds. Built: PARAMS and MASTER LEFT / RIGHT (MASTER with none lit says TAP AN F KEY FIRST) and MASTER plays PERFORM's pads (it played none); SONG LEFT / RIGHT step the row's pattern, or its repeats while F2 is lit; FILE and SAMPLE LOAD page the list with LEFT / RIGHT (the kit's picker; a pad picks a row there, the one exception to H1) and their HELP rows say so; SAMPLE REC's F4 AUTOPLAY takes the light under H2 (RIGHT is ON, LEFT is OFF); every HELP and hint line names the arrows. Exceptions named: PERFORM refuses the arrows on F1 MODE, F8 QUALITY in TALK and SHIFT + F8 PRESET, and SAMPLE EDIT keeps UP / DOWN as the zoom. *Say why nothing sounds:* in BANDS, TALK and ROBOT a voice needs a note and something speaking (TUNE only the speech, TRACT only the note); after half a second of trying (a lower pad down, a MIDI note held, the transport playing) PERFORM's hint line and PATTERN's strip note say `NO NOTE: HOLD A LOWER PAD OR PLAY A PATTERN` or `NOTHING SPEAKS: TALK INTO THE INPUT OR MAKE A SLOT SPEAK (SAMPLE F2)`; the logic is `app/src/silence.h`, fed once a pass by `App::feed_silence` from values the screens already read, nothing in the audio path. Tests: `silence_test` (new), `params_test`, `master_test`, `song_test`, `sample_rec_test`, `perform_screen_test`; scenario `app/scenarios/arrows_everywhere.txt`. The MANUAL has a short "The same on every screen". Needs more testing on the unit: the hold of a repeating arrow on each screen, and the two lines by eye and ear with a real input.
- **PATTERN redesigned: the hand, the flipped pads, REC.** (1) The pads flip: the top two rows are the screen's own (steps 1 to 16 on pads 0 to 7 and 12 to 19, patterns A to H on pads 8 to 11 and 20 to 23), the lower two rows are the lane's palette, as SAMPLE has its keyboard. (2) A palette pad sounds while it is held and becomes the lane's *hand* (each lane has its own); it writes nothing by itself. A step pad puts the hand on an empty step, chooses a full step (cursor there, it sounds, its value becomes the hand, nothing written) and clears a full step that is already under the cursor; holding step pads and pressing a palette pad writes every held step. SHIFT + a step pad is a TIE on NOTE (again: a rest) and clears the lane's value on the others. (3) REC toggles recording from the palette pads (a REC tag on the screen, off when you leave): stopped, at the cursor and then on; playing, on the step that plays (the MIDI recording rule). (4) LEFT / RIGHT are one encoder step of the lit F key (F3 VALUE is lit on entry), SHIFT + LEFT / RIGHT the cursor, UP / DOWN the lane (NOTE, CARRIER, LEVEL, SLICE, MOUTH; no wrap), SHIFT + UP / DOWN the octave, which moves the NOTE hand with it. (5) F row: F1 LENGTH, F2 TEMPO, F3 VALUE, F4 CHORD (NOTE: row B's TRIAD, 7TH, OPEN or POWER, row A on NOTE; MOUTH: SET), F5 to F7 blank, F8 SAVE; SHIFT + F6 CLR LANE, F7 FOLLOW, F8 CAPTURE. F1 PATTERN, F2 LANE, F6 TOP PADS and F7 FILL are gone (the fill code and its tests stay, unreached; `kLowA`, `kLowB` and `low_pads_*` stay for SAMPLE). The palette is normalised to a notes row and a chord row on entry. Tests `pattern_screen_test`, `pattern_audition_test` rewritten for the model; scenario `pattern_hand.txt` new, twelve others moved to the new pads. Needs more testing on the unit (the feel of the hand and REC while playing, the tag, the pad LEDs).
- **The pattern generator exists as logic; the PATTERN keys that call it follow.** `app/src/pattern_gen.h` (`o3_voc03er::pattern_gen`, header-only, no state, no heap, no `double`) rolls one lane of steps 0..len-1 and keeps the other lanes and every step past len: `roll_notes` (BASS, LEAD, CHORDS, LONG over the key and scale, with ties after playing steps), `roll_slices` (CHOP, ORDER; false when the slot has no slices), `roll_mouth` (VOWELS, WORDS), `roll_level` (ACCENT) and `roll_carrier` (NOISE hits). DENSITY 1..8 is a ranking by a weight table per style plus a seeded jitter, so with one seed a higher DENSITY only adds steps; an even length of 8 or more is a phrase (a ranked first half, repeated, with a freshly ranked tail), and the NOTE styles always play step 0. Same arguments, same pattern. `tests/pattern_gen_test.cpp` holds determinism, lane isolation, the density subset rule, every scale and key at three root notes (every note in the scale and in 1..127), the BASS, LEAD, CHORDS and LONG rules and every length 1..16. Nothing on the screen calls it yet.
- **SAMPLE REC: AFTER is gone, AUTOPLAY says what a take does when it ends.** F4 AFTER (SPEAKS, SOUNDS, PADS) repeated PERFORM's F2 SPEAKS and F3 SOUNDS lists, so the F row is now F1 SOURCE, F2 GAIN, F3 LENGTH, F4 AUTOPLAY, F5 MONITOR, F6 SLOT (F7 and F8 blank). AUTOPLAY is ON by default. ON: a slot on SPEAKS or SOUNDS plays there again ("PLAYING ON SPEAKS" or "PLAYING ON SOUNDS"); a slot on neither side becomes the SPEAKS source and loops; if the route refuses, the take stays where it is ("TAKEN: THE ROUTE REFUSES IT"). OFF: nothing is routed, the take waits in the slot and the live input keeps speaking ("TAKEN, NOT PLAYING"). A take never goes to SOUNDS by itself: pick SLOT n in PERFORM's F3 SOUNDS list. The line under the bar counter reads "ON SPEAKS  PLAYING" and so on, "HELD IN THE SLOT" for a slot on neither side (not "PADS ONLY"), and before and during a take "THE TAKE WILL SPEAK", "THE TAKE WILL PLAY ON SOUNDS" or "THE TAKE WILL WAIT IN THE SLOT"; the line and the AUTOPLAY key stay dim, with the reason, when the side the take plays on is not heard in the mode (UX3-1). Nothing is said about AFTER when REC arms. `sample_rec_logic.h` (`take_end`), `sample_rec_screen.cpp`; tests in `sample_rec_test` and the helpers of `tests/rec_take.h` (the SAMPLE REC state persists across test cases), the F5..F7 keys moved in `sample_test`, `file_test`, `pattern_audition_test`, `sample_edit_test` and in the scenarios that open SAMPLE REC; scenario `ux3_rec_after.txt` is now `ux3_rec_autoplay.txt`. LEFT / RIGHT on SAMPLE REC are one encoder step of the lit F key (`encoder_step`, the encoder's call, SHIFT as it passes it, hold repeats) instead of LENGTH's alone: LENGTH is lit when the screen opens, so the first RIGHT still reads 1 BAR; F4 AUTOPLAY took no light in this round (a tap always switched it; the later "Arrows and pads, the same everywhere" entry puts it under the same rule as the other keys) and MONITOR is the switch the arrows set (RIGHT on, LEFT off); the HELP rows and the two hint lines say it; tests `sample_rec_test` (arrows on LENGTH, GAIN, MONITOR, a held arrow), scenario `ux3_rec_arrows.txt`. Needs more testing on the unit (the new lines and the dim key on the display, the arrow hold-repeat feel).
- **TIE step, engine side (no screen yet).** A step with note 0 and kind 7 is a TIE (`kTieKind`, `is_tie`, `set_tie`; kinds 5 and 6 stay invalid, kind 7 with a note or a voicing is refused by `step_valid`; `set_note`, `set_rest` and `set_chord` clear it; the lanes carrier, level and slice are kept). The hook: a TIE holds whatever is sounding, and with nothing sounding it is a rest. At its edge it sends no release and no note-on; at the gate of the step before it the release is skipped (the next step is read after the edge and any pattern change, so a tie across the loop or a pattern change holds too); Stop and every other release path still release. CARRIER and SLICE act on a TIE as on any step, LEVEL does nothing; in TRACT a held note stays audible across a TIE and a TIE with its own mouth value still moves the mouth. BANK carries the word unchanged: a file with a tie loads, and an older image refuses such a file (as for LIVE); no version bump. The `pat` console command shows a tie as `T`. Tests in `stepseq_test` (validity, edit functions, bank file and clipboard, hook cases: tied chord, tie over nothing, loop, pattern change, Stop, ons equal offs, mouth). The PATTERN screen does not offer a TIE yet. Needs more testing on the unit (a tied chord holds with no click at the tie edge, the mouth closure across ties in TRACT, a tie across a pattern change and the loop point).
- **PATTERN's F6 TOP PADS, SONG's hint lines.** (1) F6 on PATTERN's NOTE lane is one key for both palette rows, TOP PADS, with the six values of SAMPLE's F1 LOW PADS (NOTE+TRIAD, NOTE, TRIAD, 7TH, OPEN, POWER): the first tap steps, a tap wraps, the encoder stops at the ends, both rows flash once, and there is no OFF (a pair outside the six reads MIXED and steps to NOTE+TRIAD). F7 is blank, except FILL on the MOUTH lane, where F6 stays SET. The ROW / PLAYS keys and the pattern screen's `row_` are gone; PATTERN and SAMPLE play and write the same notes for rows of the same kind (both call `pads::pad_play`). (2) SONG shows two hint lines under the list, saying what its keys do today (UP and DOWN, the encoder, F2 repeats, F1 or INSERT, DELETE twice, F3 LOOP); no behaviour changed. Tests: `tests/pattern_screen_test.cpp`; scenarios `pattern.txt`, `song.txt`, `ux3_song.txt`. Unit check owed: F6 on PATTERN by hand, and SONG's hint lines for legibility.
- **SE1-SE3: SAMPLE EDIT picks, zooms, latches and lets a slot speak.** (1) *Pick:* a slice pad (or LEFT / RIGHT, which stop at the ends) picks one slice from any state and never toggles it off; its span is filled on the cut strip, its start line is the brightest and the line under the picture reads `SLICE 3 OF 8   START 0.500 S   LEN 0.250 S`. The cursor stays a separate state (a pick puts it on the slice start; an F key other than F8, or SHIFT + UP / DOWN, lets the pick go). (2) *DELETE* with a pick removes that slice's start cut, picks the slice that swallowed it and says `CUT 5 REMOVED: 7 SLICES` (slice 1 cannot go); with no pick it removes the cut nearest the cursor inside the view, or says there is none. INSERT with a pick splits it in its middle. (3) *Zoom:* UP in, DOWN out, a halving ladder (every step x2 from WHOLE SLOT to at least 344 samples across; a 16 s slot has eleven; label `ZOOM 2.0 S`, `ZOOM 250 MS`, `ZOOM 7 MS`, `WHOLE SLOT`), centred on the picked start or the cursor, and following the bracket F4 START or F5 END moves (focusing one brings it into view); cuts in the window carry numbers; the encoder detent follows the zoom (one picture pixel, one sample at the deepest rung, SHIFT finer or coarser); a rebuild reads at most 22,134 samples, from `draw()` only (one per frame however many detents arrive) and only when the window or the audio changes. The slot before / after moved to SHIFT + UP / DOWN. (4) *F8 SPEAKS:* the key reads `LIVE` or `SLOT n` like SAMPLE's F2 and never CONFIRM (a confirm label is SHIFT + F8's, CLR CUTS); the slot speaks (the vocoder's modulator) or the route goes back to LIVE; an empty slot refuses; SHIFT + F8 still clears the cuts. The header names the slot's place in the route (SPEAKS, SOUNDS or nothing). (5) *Latch:* SHIFT + a lower pad latches its note or chord (the LED shows it; SHIFT + the same pad lets go, SHIFT + another moves it, a plain press plays over it); leaving the screen, a mode change and a project load release it (and end its note); a slice pad pressed in BANDS, TALK or ROBOT with no note sounding says how (the hint cannot see a note the sequencer plays). Scenarios `sample_edit.txt`, `sample_edit_slice.txt` (updated), `sample_edit_zoom.txt` and `sample_edit_long.txt` (a 16 s slot, 24 cuts, the longest header and info lines, the window following F4 / F5); tests in `sample_edit_test` (the ladder, the key texts, the latch's note-off on every road, the ends of LEFT / RIGHT, DELETE with no pick, the pick after NORMALISE). Needs more testing on the unit (the latch by ear, the zoom redraw cost, the one-sample detent).
- **SAMPLE, fixed after the unit test of dev e0ad89f: LOW PADS on both rows, F2 SPEAKS, a slice that repeats, F7 SLICES.** (1) F1 LOW PADS sets both lower rows at once and has no OFF: NOTE+TRIAD (the default), NOTE, TRIAD, 7TH, OPEN, POWER; a tap steps and wraps, the encoder stops at the ends; both rows flash; SHIFT + F1 ROW is gone. The pair is PERFORM's notes pair, so a pair outside the list shows row A's kind; with one kind on both rows the rows play the same kind, row A one octave above row B. (2) F2 SIDE is F2 SPEAKS: LIVE, SLOT 1 to 4. A tap makes the selected slot speak (its loop starts), a second tap returns to LIVE and stops that loop; the encoder steps over slots that hold audio; an empty slot says EMPTY: NOTHING TO PLAY; a slot on SOUNDS can still be made to speak. There is no NONE value: the cue line says what the slot does now (it fits 800 px), the dim key and its reason (UX2, UX3) stay. A slot that neither speaks nor sounds is PADS (the slot badge, SAMPLE REC's AFTER value) and the centre says PADS ONLY; SAMPLE LOAD's note reads PADS ONLY: F2 SPEAKS ON SAMPLE; F5 LOOP ON on such a slot makes it SPEAK first ("SLOT n SPEAKS"), on a SOUNDS slot it changes nothing in the route. (3) F5 LOOP ON plus a slice pad repeats that slice (one repeating slice a slot, the key reads ON SL n): with TRIG GATE it repeats while the pad is held and then the whole loop goes on, with ONE SHOT until another pad or F5; F5 OFF stops everything of the slot. A repeating slice goes on after leaving SAMPLE (coming back, F5 still reads ON SL n from the unit's status; F5 OFF, a project load or a new take in the slot ends it) and the span is latched at the trigger (a later Start or Len does not move a running repeat). The unit's `RegionField::Trig` has a third value, 2 (REPEAT: the region begins again at its natural end), and `LoopStatus` reports it; the sequencer's `fire_slice` keeps its one shot, and SAMPLE EDIT's pad does not repeat. (4) F7 CHOKE is F7 SLICES: MONO (default, a new slice cuts the one sounding) or POLY. (5) The first-player walk (BANDS, TALK, ROBOT, TUNE, TRACT) fixed the HELP pages and legend of SAMPLE. Scenarios `sample_repeat.txt` (new), `sample.txt`, `sample_layout.txt`, `ux3_sample_cue.txt`; tests in `sample_test` and `region_test`. Needs more testing on the unit (the repeat on a real card, the feel of F2 SPEAKS, the CPU of a repeating slice).
- **PM1: PARAMS is the in-depth editor of the mode PERFORM is in.** The four fixed pages (CARRIER, SHAPE, TUNE, TRACT) are gone: each of the five modes has its own pages, with only the values it hears
  (`params::key_live` with the carrier source; SPEED outside TUNE only with the VOICE carrier), grouped by meaning, one-word tab names, a key in the same cell in every mode and whatever the carrier is (SPEED's cell, NOTES F3, is blank until the carrier is VOICE), blank cells only at the end of a page, no one-key page.
  BANDS and ROBOT: CARRIER, VOICE (FORMANT, THRU, DRY, LEVEL), NOTES (KEY, SCALE, SPEED, GATE, ARP, VOICING); TALK: the same with QUALITY and FREEZE after LEVEL; TUNE: one page, TUNE; TRACT: CARRIER, MOUTH, NOTES, MIX (MOUTH, DRY, LEVEL). The tabs adapt to one to four,
  the title names the mode (`PARAMS  BANDS`), no key is dim and there is no "HEARD IN ..." line. MODE left PARAMS: PERFORM's F1 changes it, and PARAMS follows any change of the mode (PERFORM, a preset, a project
  load, a MIDI program change) by showing the new mode's first page with a valid focus; a learn armed on a value no longer shown is cancelled; page and focus are not remembered per mode. `params::kTable`, `key_def`, `KeyDef`, `key_modes` and `key_live` are kept; the pages are
  fixed `PageDef`s per mode (`params::build_layout`) over the table, with `static_assert`s (every heard key exactly once, none that is not heard, no cell moves with the carrier). MIDI learn, the saved bindings, the project file and the presets are unchanged: the binding stays on the parameter (the table's
  coordinates), not on a screen position. The unreachable FREEZE refusal text is removed (app.cpp clears FREEZE on a mode change). TRACT's MOUTH key moved from the SHAPE page to the MIX page (F1).
  Repacked by meaning (coordinator's follow-up): the first pack-in-eights layout gave one-key pages and moved keys with the carrier; VOICING stays on NOTES in every mode and QUALITY and FREEZE are TALK's two end cells of VOICE, so DRY and LEVEL keep their cells. Tests in `params_test` (layout coverage, the mode and carrier follow, learn kept and dropped), scenarios `params.txt`,
  `params_modes.txt` (the five modes and the VOICE carrier, every page), and the PARAMS-driving ones (`liveness`, `midi_learn`, `tract_live`, `ux3_sample_cue`, `manual_screens`); the manual's PARAMS section and images are redone. Needs more testing on the unit (the tabs and pages by hand in each mode, learn after a mode change).
- **UX3: the cues and refusals of SAMPLE, SAMPLE REC, SAMPLE LOAD, SONG and FILE say what they mean.** (1) *Mode cue:* in TUNE a SOUNDS slot, and in TRACT a SPEAKS slot or a slot on no side, is not heard
  (with MOUTH on PADS TRACT hears a slot only through DRY; with MOUTH on LIVE the slot moves the mouth and is not dimmed, the line says so): SAMPLE's side badge and the side value show dim with a reason line, SAMPLE REC's AFTER key and side line do the same (MANUAL section 10, "The mode cue"). (3) *FILE:* F5 without a
  name says NAME THE PROJECT FIRST; SAVE says "AUDIO NOT SAVED: F5" when a slot holds audio the project's WAVs do not carry (a re-record of the same length is noticed through a per-slot flag set by a take or a load and cleared by F5); NEW says its slots keep their audio, on the
  ask, after it and on the screen; the pads are unlit and F6 and F7 are blank; the unsaved list in the MANUAL names TRACT's Close, Slide, Accent and Mix; `p.swing` is no longer set on capture (the format byte stays).
  (4) *Help:* SAMPLE REC's SHIFT gives coarser gain steps, LENGTH reads "FREE, 1, 2, 4 OR 8 BARS", SONG F3 is LOOP. (5) *SAMPLE LOAD:* the refusal names what to stop (take, loop or slice), the list says when it is capped
  at 64, and a load into a slot that neither speaks nor sounds says PADS ONLY: F2 SPEAKS ON SAMPLE (NO SIDE: F2 SIDE ON SAMPLE when UX3 was built), on a second line when the note would not fit. (6) *SAMPLE REC:* an AFTER the slot cannot follow is said when REC arms, not after the take. (7) *SONG:* DELETE takes a second tap on the
  same row within two seconds; PLAY while the chain runs stops it and the line says so (while a MIDI master is followed it says "STOP ASKED OF THE MIDI MASTER": the stop is only a request; the next PLAY runs it from the cursor); F8 is labelled PERFORM. (8) *SAMPLE:* a slice pad past the count says "n SLICES", NO FREE
  READER names its cause (RECORDING or ARMED), a slot on NONE says whether its loop speaks too, and a length change that resets edited cuts says so. Verified and dropped: F8 BACK on FILE and LOAD, CANCELLED on DELETE while arming and the LENGTH fit check already existed; the stale-WAV
  removal on F5 needs a kit file delete that does not exist (the MANUAL says the old numbered WAVs stay and how to remove them); SAMPLE EDIT's CLR CUTS move is lane 2's. Scenarios `ux3_sample_cue.txt`, `ux3_rec_autoplay.txt`,
  `ux3_song.txt`, `ux3_file.txt`, `ux3_load.txt`, tests in `sample_test`, `sample_rec_test`, `sample_load_test`, `song_test`, `file_test`. Needs more testing on the unit (the dim cues, the strip widths, a card over 64 files).
- **UX2: one liveness table, dim means not heard.** `app/src/liveness.h` says in which modes each value is heard; PERFORM's SHIFT row, the strips' row C, PARAMS and the PATTERN lane headers read it. FREEZE refuses outside TALK on PERFORM, PARAMS and a learned CC (and every mode change clears it); SPEED is lit in any mode with the VOICE carrier; the FROZEN preset is SWELL, without the latch; the PATTERN idle hint moved up off the F row; row C FORMANT is dark and refuses in TUNE and TRACT; dim PARAMS and SHIFT keys still set the value and say where it is heard. PATTERN: "SONG PLAYS THE CHAIN" on a pattern pad while a chain plays, one LEVEL range (ACCENT in TRACT), reasons on F6/F7 taps, a lane header that shows a lane is not heard here, VALUE on an empty SLICE lane says why. MASTER opens with nothing focused. PARAMS gets GLIDE, GATE, ARP, FORMANT and THRU keys (MIDI-learnable); `Kind::Later` is gone. MANUAL and UI-DESIGN updated. Tests: `tests/liveness_test.cpp`, `params_test`, `perform_screen_test`, `pattern_screen_test`, `strip_logic_test`; scenario `app/scenarios/liveness.txt`. Needs more testing on the unit (hint lengths, dim legibility, FREEZE clearing).
- **SP1: strips on PERFORM only, one notes palette for the rest.** TP1 had put the parameter strips into the per-mode layout that PATTERN, SAMPLE, SAMPLE EDIT, FILE and SAMPLE LOAD also read,
  so in TRACT the SAMPLE pads played no notes and F1 PLAYS showed strip values. Now PATTERN's NOTE lane, SAMPLE, SAMPLE EDIT, FILE and SAMPLE LOAD share one pair of note rows of their own, A NOTE and B TRIAD at start
  (NOTE, TRIAD, 7TH, OPEN, POWER, OFF), the same in every mode; PERFORM's per-mode layouts never change it and it never changes them (setting a row on PERFORM no longer changes PATTERN's palette). No strip
  code is reachable from those screens. PATTERN's F7 and SAMPLE's F1 step the pair; SAMPLE's F1 is now LOW PADS (value "A NOTE") and the screen says "UPPER PADS: SLICES. LOWER PADS: A NOTE  B TRIAD". The pair is
  RAM only (A NOTE, B TRIAD again after a restart, a project load or F4 NEW); the project's PADS chunk is unchanged. Holding a slice and a note pad still sings the note.
  **FX1:** on PERFORM F7 follows a pad only while it is physically held (a latched pad or a stale held bit does not count); with no pad down it steps the row F6 shows, and F6's row never moves by itself.
  F7's value now names the row and what it plays ("A VOWEL"), and "NAME n/N" (the entry's number of its list) while the encoder moves it. **Defaults:** BANDS A NOTE, B TRIAD, C FORMANT; TALK A NOTE, B 7TH, C FORMANT; TUNE A NOTE, B SCALE, C SPEED
  (NOTES IN is on by default; KEY would be silent at the default CHROM scale); ROBOT A NOTE, B SHAPE, C FORMANT; TRACT A NOTE, B TONGUE, C MOUTH (every row does something out of the box, and a mode that sounds from notes has a note row). A saved PADS chunk keeps
  its layouts: an existing project keeps its old layout until F4 NEW. Scenarios `sample_layout.txt` and the PERFORM ones, tests in `sample_test`, `perform_screen_test`, `pattern_screen_test`, `strip_logic_test`.
  Needs more testing on the unit.
- **SE1: beat slices by hand in SAMPLE EDIT.** A tap on a top pad picks that slice (it plays, the pad is lit high) and the picked slice takes the encoder ahead of the lit F key: its start
  moves between its neighbours with the quiet-crossing snap and the kit's acceleration, SHIFT + encoder one sample a detent. Slice 1's start is the trim START bracket (one notion of start;
  the table's first cut stays at 0, and pad 1 plays from START while it is moved, so what moves is what is heard). LEFT / RIGHT pick the previous / next slice. The same pad, an F key or UP / DOWN lets it go (that key counts as a first tap, so it does not also step or
  trim). A pad past the last slice says INSERT ADDS A CUT. While a slice is picked the waveform zooms to about 0.5 s around its start (186 cached min / max columns, no bitmap scaling) and the
  brackets are remapped. F1 reads CONFIRM while something waits for it (the key a REPLACES warning waits on reads it instead) and the trim and NORMALISE say NO UNDO. CLR CUTS moves to SHIFT + F8
  (a hand-edited table asks twice, "REPLACES n CUTS: SHIFT + F8 AGAIN"); F8 alone changes nothing. Scenario `sample_edit_slice.txt`; tests in `sample_edit_test` (selection, clamping, slice 1 =
  trim START, deselect, SHIFT + F8). There was no hit overlay to remap. Needs more testing on the unit (the feel of the step, the snap and the acceleration, the zoom). Cost to read there: the zoom rebuilds
  at most once per frame while the encoder turns (cached by position), about 22,050 SDRAM reads a rebuild, estimated 1 to 3 ms in the main loop (not the audio path); estimate, not measured.
- **LC1: LIVE in PATTERN's CARRIER lane.** A step can sound through the live audio input, as PERFORM's SOUNDS LIVE does (the pattern's carrier ids are the engine's: 7 is LIVE, appended, the saved ids 0 to 6 never move;
  `kCarrierMax` 6 to 7, the three-bit carrier field already held it). INSTR gets a LIVE pad (pad 7, cell "LIV") and playback switches the carrier to LIVE on that step and to the next step's carrier after it, a Stop
  restoring the user's pick. It works with SPEAKS on a slot or on the input. Of the PATTERN lanes only SLICE needs a slot (it says SPEAKS HAS NO SLICES); MOUTH capture reads a recorded slot by design. No format
  change: an older image refuses a pattern file that holds a LIVE step, an older file loads here. Scenario `pattern_live_carrier.txt`, tests in `carrier_step_test`, `pattern_screen_test`, `stepseq_test`. Needs more testing on the unit
  (a click at the switch).
- **PR1: TRACT presets on PERFORM SHIFT + F8.** In TRACT the PRESET value offers eight mouths of its own (AH, OO, EE, WOW, BREATH, TIN MAN, TINY, BIG), each writing all twelve
  tract values (Vowel to Accent, never SOURCE), so none leaves anything behind; the vocoder's eight are unchanged and the vocoder is untouched in TRACT. The project's one saved
  preset name resolves against the table of the last choice (`project_tract_preset_index`), an unknown name opens as none. Scenario `tract_presets.txt`, tests in
  `presets_test`, `project_test`, `perform_screen_test`. Review fold: BREATH now narrows the tube (I, jaw -0.3, lips -0.3) so HISS sounds (`presets_test` measures 0.0072 rms of hiss against none before); EE spreads its lips (+0.3), OO rounds them (-0.1), TINY loses its clamped tongue bend; a project load and NEW put the tract's CLOSE, SLIDE, ACCENT and MIX back to their defaults (MIX 1 in TRACT, 0 elsewhere; the file format is unchanged; two tests in `file_test`); with no tract unit SHIFT + F8 changes nothing and shows ---. The sounds are starting points for the ear, chosen without listening: needs more testing on the unit.
- **ML1: MIDI learn on PARAMS.** SHIFT + an F key arms the kit's learn for that value, on all four pages (the TRACT page learns the tract unit's own values); the same
  keys again cancel, and so does leaving the page or the screen. The armed key lights and the screen says LEARN and the value's name, MOVE A KNOB; the strip says
  LEARNED CC n. A value that is not a live-safe unit parameter (VOICING, MODE, QUALITY) shows "-" on the held-SHIFT row and cannot be armed. `App::midi_learned_moved` redraws. MANUAL no
  longer claims learn on any value: it is the PARAMS values. Scenario `midi_learn.txt`, tests in `params_test`. Needs more testing with a real controller.
- **TP1: parameter strips in PLAYS, one saved layout per mode (NP1 and AK1 folded in).** F7 PLAYS gains a strip for every parameter PARAMS shows in the
  mode that makes sense played live: twelve pads, pad 1 the minimum and pad 12 the maximum (a bipolar range has the middle pair at 0), one pad per choice up to
  twelve choices and twelve pads spread over more. TRACT gets all its parameters, the closures CL LIPS, CL TIP and CL BACK (held, momentary, the last pad pressed
  wins) and MOUTH. Left out: MODE, QUALITY, LEVEL, VIB, BEND, FORMANT, the carrier and modulator sources, the live channel, TRACT's CLOSE as a number, SLIDE and MIX.
  Pad LEDs show the value, a strip row plays no notes, the screen names the strip beside the row, a strip of another mode's parameter is dark with "... IN <MODE>".
  F7 with a long list: a tap steps and wraps, the encoder scrolls, SHIFT + encoder jumps by group. Each of the five modes keeps its own three rows; TRACT starts as
  A VOWEL, B TONGUE, C CL TIP and the other modes as A NOTE, B TRIAD, C FORMANT. Saved in a new `PADS` 1.0 chunk (a strip by a stable id, unit and parameter), STAT still
  carries the BANDS layout, so an older image reads a layout from a new project; an older project loads its layout on BANDS, TALK, TUNE and ROBOT and the TRACT default.
  F4 NEW resets all five. No parameter or `config.o3a` change. Partial closure: the kit's `set_closure` takes a bool, so only the held full closures exist;
  a graded closure is a kit follow-up. Recording strips into PATTERN lanes is not built. **NP1:** every PIANO value, its code, drawing and tests are removed; a saved
  piano value loads as the plain value of the same kind; the perform-keys.png picture is no longer referenced. **AK1:** LEFT, RIGHT, UP and DOWN step the focused F value on
  PERFORM; the octave moves to SHIFT + F7 (SAMPLE: SHIFT + F3); PATTERN keeps UP / DOWN as octave and LEFT / RIGHT as the cursor; the arrows skip F1 MODE, F8 QUALITY on TALK and SHIFT + F8 PRESET. A closure strip held on SAMPLE opens when SAMPLE is left, and a dark strip pressed there shows its "... IN <MODE>" note on SAMPLE's own line. Tests: `strip_logic_test`, `pads_logic_test`,
  `perform_logic_test`, `perform_screen_test`, `project_test`, `file_test`; scenarios `perform_strips.txt`, `arrows.txt`, `perform_tract.txt`. Needs more testing on the unit.
- **WI1: WAV import converts on load.** SAMPLE LOAD accepts 8, 16, 24 and 32-bit PCM and 32-bit float WAVs (plain or WAVE_FORMAT_EXTENSIBLE headers, unknown
  chunks skipped, odd chunks padded), mono or stereo, from 22.05 to 96 kHz, and converts them to the slot format (mono int16 at 44.1 kHz) while loading; the
  16 s limit applies to the converted length. 16-bit 44.1 kHz files take the old integer path and load sample for sample as before. Refused with a clear strip
  text: other codings (ADPCM, MP3), more than 2 channels, rates outside 22.05 to 96 kHz, bad block align, a damaged or empty file. The strip says what was done
  ("48K 24B TO 44.1K 16B", "FIRST 16.000 S LOADED, FILE HAS 20.000 S, ...") or why it refused. Rounding to nearest, no dither (rounding noise about -98 dBFS);
  a stereo mix of 16-bit files truncates as before. Resampler (`app/src/wav_convert.h`): Kaiser (beta 7) windowed sinc, 16 zero crossings a side, 64 phases,
  cut at 0.86 of the lower Nyquist, Q15 table, float ring, float only, no `double`, no heap, output length exactly ceil(n x 44100 / rate), the kernel is
  renormalised so the ends do not droop. Measured on the host: pass-band ripple within 0.002 dB to 15.4 kHz, residual -82 to -90 dB, -14.6 dB at 20 kHz, -67 dB at
  22 kHz, aliases from above Nyquist -77 to -92 dB. Work is bounded per pass (the budget of 80,000 taps, about 5 ms [estimate], is checked before every input frame and the sink returns a short
  count, or WouldBlock at the top of a call, so a pass overshoots by one frame's work, under 140 taps, whatever the size of the chunk the kit offers) and the progress
  display is kept. The sinc table (1,026 entries, 2 KB) is built once, on the first converted load after boot, inside the main loop: about 0.9 M cycles, 5 ms [estimate]. Memory: the
  converter (header parser, table, ring, 2.8 KB) is in the SDRAM, built on first use in zeroed `O3SDK_BIG` storage, and not in the SRAM singleton: SRAM_U free under the
  stack is 39,256 B after (0x2002a000 - __bss_end 0x200206a8, 6,488 B above the linker's 32 KiB floor) against 36,456 B with the converter in the SRAM; the SDRAM stage
  shrinks from 2.8 MB to 1.41 MB (int16 frames). Cost [estimate]: about 36 taps an output at 48 kHz, 12 to 15 cycles a tap, so a 16 s stereo 24-bit 48 kHz file is about 4 s
  of CPU and a 5 to 6 s load; to be measured on the unit. Behaviour changes against the old loader: a file whose block align disagrees with its format is now refused
  ("BROKEN WAV FILE"); the refusal texts for the formats are "NEEDS PCM OR FLOAT", "NEEDS 8 TO 32 BIT" and "NEEDS 22 TO 96 KHZ" (were "NEEDS PCM, NOT FLOAT",
  "NEEDS 16 BIT", "NEEDS 44.1 KHZ"), in the same order (coding, bits, rate, channels, then the block align); float, 8, 24 and 32-bit and other rates, which were refused, load. The 16-bit 44.1 kHz
  result is pinned in the test by digests of what the loader of f8dd91a made of the same mono and stereo files. SAVE WAV
  still writes 16-bit mono at 44.1 kHz. No parameter, `config.o3a` or pattern/project format change. Tests: `tests/sample_load_test.cpp` (rewritten),
  `tests/wav_convert_test.cpp`, scenario `app/scenarios/sample_load.txt` with 24-bit 48 kHz, 20 s and 192 kHz files.
- **SB1: bend a slot.** The Bend value (MIDI wheel, encoder, strip, BND pads) now also bends every sounding slot, loop or region, main and slice channel,
  by the same semitones (rate = speed x 2^(bend/12), up to 12 st either way; a slot used to play at 0.5x, 1x or 2x only). The synth's vibrato does not move
  slots. The reader keeps a fixed-point phase (Q33 samples in a `uint64`, a Q24 ratio, no float to 64-bit integer conversion, no `double`) and reads with linear
  interpolation (cheap, and clean enough for the use: 68 dB SNR on a 441 Hz sine at +2 st, 43 dB on 2 kHz at +1 st, more aliasing on a bend up
  of a bright source); the 64-sample seam and end fades still apply in both directions, and the interpolator reads only valid samples at the loop seam and at
  a slice's end. The ratio glides with Bend's own 20 ms ramp and is stepped per 64-sample chunk in integers, so there is no zipper. Bend exactly 0 runs
  HEAD's integer path unchanged (a branch a block), pinned by a digest of the playback over loops, regions, three speeds and both directions
  (0x5451E258, equal to HEAD's). A bent whole-bar loop drifts by design; when the bend is back at 0 the loop lands on a whole-sample grid (a half-sample one only at 0.5x
  forward, where HEAD's phase alternates) in a short settle chunk, so reverse, 2x and 0.5x return to HEAD's exact output and no two-sample average is
  left behind (the test requires integer parity and an unbent output for every speed and direction), then the first bar line re-syncs it with the existing
  snap or crossfade (worst step 0.018 in the test, under the crossfade's 0.039; no runaway). The loop's `on_bar` status bit goes false while the bend has
  it off the bar and true again at that re-sync. A Bend below 0.02 st either way is read as exactly 0 by the slots (a dead zone: the wheel's rest position
  and the strip's centre keep HEAD's bit-exact path; the synth still takes the true value). A region whose bent end lands on a zero length ends
  instead of wrapping, and `stop()` while bent starts the tail on the sample the bent read would have played next (no sample-sized step). No new parameter (64 of 64), `config.o3a` and the pattern format are untouched. Measured on the host: the frequency of a bent sine is exact
  to better than 1e-7 relative (+12, -12, +2, -5, +7 st), the reader against a double-precision oracle differs by at most 4.5e-8, no read outside a slot
  (sentinel tests over odd lengths, an exact-size bank, the last slot, every speed and direction), a region's end fades. Cost [estimate]: an unbent
  slot pays about 3 compares a block more; a bent one about 20 cycles a sample plus a few hundred cycles set-up per 64-sample chunk (about 1,500 to 2,000
  cycles a chunk of 64 samples, under 1 % of the time it plays a slot at 180 MHz), memory 520 B of bent-read span in the bent call and 88 B of state; stack measured with `-fstack-usage` on the cross build:
  `mix_bent` 904 B on top of `mix_plain` 600 B and `mix` 24 B, so the deepest loop path is 1,528 B against 896 B at HEAD (`mix_slow`), and `region_chunk_bent`
  912 B under `mix_region` 56 B and `region_chunk` 24 B (992 B, HEAD 648 B): about 630 B more at the worst point of a 24 KiB stack; the real cost is for the unit
  to read. `tests/bend_slot_test.cpp`, `app/scenarios/bend_slot.txt` (the WAVs read 220, 246, 196 and 220 Hz with the wheel up, down and centred).
  Needs more testing on the unit: the sound by ear, the return to the bar, several bent slots at once and the CPU.
- **LT1: the live voice drives TRACT's tube.** TRACT's SOURCE (PADS by default, or LIVE; PARAMS, SHAPE page, F6 MOUTH; console `tract source live|pads`)
  makes the mouth follow the voice at the input. The vocoder reads the input with TALK's own analysis (the LPC code moved to `lpc.cpp`, shared and unchanged:
  TALK is bit for bit as before), orders 11 to 22 tube areas (each section twice, floor 0.05, smoothing 0.5, silence holds the last shape) and hands them,
  through a one-struct link between the two units (same render context, no atomics, one block of latency), to the tract kit's new `Processor::set_areas`
  (extras/tract in o3-sdk; the vowel path is bit for bit the same, pinned by digest). No new vocoder parameter (64 of 64): SOURCE is TRACT's thirteenth,
  appended; it is not saved with the project (`TRCT` keeps eight values) and boots as PADS; `config.o3a` and the pattern format are untouched. Default
  behaviour is bit for bit as before (the chain digest 0x3546222B, unbound and bound). Measured on a synthetic voice (tilted source, five vowels A, E, I, O, U):
  worst formant error F1 12.6 % (U), F2 3.6 %, F3 1.8 %; a flat source reads F1 up to 104 % high, F2 57 % and F3 34 % off (the analysis assumes a falling spectrum; bounded loosely in the test). Silence
  holds a shape of sane level, DC, clipping and 1e-20 noise stay finite, no click when switching mid-note (`tests/mouth_test.cpp`). Cost [estimate]: about
  12,000 cycles a block, 15,000 on a hop block, in LIVE only; memory about 100 B, 110 B link and 216 B more in the tube. `app/scenarios/tract_live.txt` shows
  the key and that the chord holds, not the mouth following (the voiceprint still draws the pads' parameters). Needs more testing with a real voice and room
  on the unit: the sound, the CPU, a click on entering LIVE, a low F1.
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

- **One SPEAKS, and small texts that said the wrong thing.** (1) SAMPLE EDIT's F8 SPEAKS is SAMPLE's F2 tap itself (`SampleScreen::speaks_tap`), not a copy of it: the same route change, the same refusals and the same note on both screens (a slot that SOUNDS is freed to the synth first, a mode that cannot hear SPEAKS says so). (2) SAMPLE LOAD: while it asks "REPLACES SLOT n: F1 CONFIRMS" F1 reads CONFIRM and stays lit (it was drawn dim), and the hint says F1 OPENS the file, as the key reads. (3) SAMPLE REC: with the transport stopped the right-hand note is two lines (TRANSPORT STOPPED / A TAKE STARTS AT ONCE); as one line it ran to the display's edge. (4) HELP's button list said "PARAMS: FOUR PAGES" on six screens; since PM1 the pages are the mode's (one, three or four), and it reads "PARAMS: THE MODE'S PAGES". (5) The manual's pictures are taken again from `manual_screens.txt` (seventeen of them had fallen behind the screens). Unit check owed: F8 on SAMPLE EDIT and F2 on SAMPLE by hand.
- **RP1: PERFORM's F6 ROW and F7 PLAYS agree.** F7 always acts on the row F6 shows (a held pad no longer takes it), and a tap opens a list of
  what that row can play, with group headings and the value in force marked; UP, DOWN or the encoder move, SHIFT + encoder jumps a group, F7 or
  F1 takes, F8 backs out. F6 steps on its first tap, UP is the row above and DOWN the row below (they were swapped), and the chosen row's name
  is lit while F6 or F7 is in use. F7's value is what the row plays, without the row's letter (F6 shows the row, and the cell was too narrow for
  "C FORMANT"). The list offers only what acts in the mode: FORMANT is gone from TUNE and TRACT and MOUTH from every mode
  but TRACT, so no choice leaves a row dark but OFF; a project that holds such a value, or a strip the mode no longer uses (GLIDE, GATE, ARP or
  VOICING in TUNE), loads with that row on the mode's default. Tests in `perform_screen_test`, `strip_logic_test`, `liveness_test`; scenario
  `perform_layout.txt`. Needs more testing on the unit (the list on the display, the lit row name).
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
