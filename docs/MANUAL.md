# O3-VOC03ER player's manual

O3-VOC03ER turns the Tracker into a voice machine. Your voice, or a recording, shapes a synth you play with the 48 pads: a vocoder, a
talkbox, a pitch corrector, a robot voice and a played mouth in one instrument, with a sampler, a step sequencer and a song chain beside it. This manual
starts with what makes sound fastest and goes deeper as it goes on. You can stop reading after section 3 and play.

The pictures come from the O3-VOC03ER simulator, which draws the same screens as the unit. Where a picture also shows the pads, the grid
under the screen is the simulator's drawing of the pad lights. The simulator is fed plain tones for a voice, so the voiceprint in the
pictures is thinner than a real voice would make it.

> **Unofficial.** O3-VOC03ER is not affiliated with or endorsed by Polyend. "Tracker" is used only to describe the hardware it runs on.

## Contents

1. [What O3-VOC03ER is](#1-what-o3-voc03er-is)
2. [Install it, and go back to the stock firmware](#2-install-it-and-go-back-to-the-stock-firmware)
3. [The first five minutes](#3-the-first-five-minutes)
4. [The screens, one by one](#4-the-screens-one-by-one)
5. [The five modes](#5-the-five-modes)
   - [5b. TRACT: a mouth after the carrier](#5b-tract-a-mouth-after-the-carrier)
6. [Samples and slices](#6-samples-and-slices)
7. [Patterns and songs](#7-patterns-and-songs)
8. [MIDI](#8-midi)
9. [Projects and the card](#9-projects-and-the-card)
10. [Known limits, and what needs more testing](#10-known-limits-and-what-needs-more-testing)

## 1. What O3-VOC03ER is

O3-VOC03ER is a whole firmware for the Tracker hardware. It replaces the Tracker's own software while it is installed (section 2 says how
to go back). It is one instrument built on one idea: **something speaks, something sounds.**

- **SPEAKS** is the voice: a microphone, a line input, the radio, USB audio, or a recording in one of four slots.
- **SOUNDS** is what that voice plays through: a synth, noise, the live input, or a recording.
- In the middle is the **voiceprint**: sixteen bars that show, live, the shape of the voice that is speaking.

![PERFORM in BANDS with a chord held: the voice on the left, the voiceprint in the middle, the synth and its chord on the right](manual/img/hero.png)

*PERFORM while a chord is held. SPEAKS is at the left, the voiceprint in the middle, SOUNDS at the right, a drawing of the pads under them.*

What you can do with it:

- **A vocoder.** Sing or speak, hold a chord on the pads, and the chord speaks your words.
- **A talkbox.** The same, taken as a smooth shape of the mouth, closer to a voice coming out of a tube.
- **Pitch correction.** Your voice, pulled to the notes of a key and scale you pick.
- **A robot voice.** The vocoder with a fast leveller after it, for a flat, hard, mechanical voice.
- **A played mouth.** TRACT: nothing is analysed. The sound you play goes through a model of a mouth that you shape with the pads, the
  encoder or a pattern, to make it say vowels and simple sung words.
- **Recordings that speak.** Record a take, cut it into slices, and play the slices from the pads through the chord you hold.
- **A pattern and a song.** Sixteen steps of chords, up to eight patterns, chained into a song.
- **MIDI in.** Play the synth and the slices from a keyboard or a sequencer.

## 2. Install it, and go back to the stock firmware

O3-VOC03ER comes as one file, the image the Tracker's bootloader takes from the SD card: `o3-voc03er-<version>.ptf`. The project's
release notes say what a unit has shown of that version and what it has not.

O3-VOC03ER writes in two places on the card: its own folder `Apps/o3_voc03er/` (your projects and, when you ask, a few small settings
files) and the card's `Samples/` folder (the WAV files you save from SAMPLE EDIT). Your Tracker projects stay as they are.

### Install by the card

1. **Back up the whole card** on a computer.
2. **Keep your way back**: the image the unit runs now (the official `PolyendTracker_<version>.ptf`, or another firmware you use) stays
   safe on your computer.
3. On the card, move every `.ptf` file out of the `Firmware/` folder (for example into a new folder `Firmware-old/`).
4. Copy the image into `Firmware/` and rename the copy `PolyendTracker_0.0.1.ptf`. It must be the only `.ptf` in `Firmware/`: the reset
   button flashes the first `.ptf` in the card's directory order, which need not be the alphabetical order a computer shows, so with two
   files you cannot tell which one it takes.
5. Put the card back and press the Tracker's hidden reset button (the maker's emergency-flashing instructions say where it is). The
   bootloader writes the image and the unit starts O3-VOC03ER.

### Go back to the stock firmware

1. Move every `.ptf` out of `Firmware/`.
2. Put the official `PolyendTracker_<version>.ptf` alone in `Firmware/`.
3. Press the hidden reset button.

The bootloader is separate from the firmware, and an image that does not start does not remove it: the card and the reset button are the
way back. Keep the card backup and the old image at hand.

## 3. The first five minutes

You need a sound source. A line cable from a synth, a phone or a mixer is the easiest. A microphone works too. Nothing else is needed.

**1. Plug in and switch on.** The screen that comes up is PERFORM, the home screen. At the top you see the route: `LINE > SYNTH`. The first
time, the live source is LINE with its gain at 0, so a quiet source is not boosted by surprise.

**2. Check that the voice arrives.** Play something into the jack, or press MASTER and pick MIC on F1 and speak. The IN meter at the left
moves and the voiceprint in the middle flickers. Nothing sounds yet: the synth has no note.

**3. Press one pad of the middle row of the three.** A chord speaks your words. The screen names the chord and lights its notes. The next
pad to the right is another chord. Hold the pad while you sing. That row, B, is one chord per key: C major, D minor, and so on, all in the key
and scale that F4 and F5 say. The row above it, A, plays single notes, and the pads layout (section 4, PERFORM) says how to change both.

**4. Free your hands.** Hold a chord pad and press SHIFT: the chord is latched and keeps sounding after you let go. A new latch replaces
the old one. DELETE lets it go. (SHIFT first and then a pad latches nothing.)

**5. Try the other voices.** Press F1 (MODE) to step from BANDS to TALK, TUNE, ROBOT and TRACT. Section 5 says what each does.

**6. Record yourself.** Press REC on PERFORM and sing. The take is two bars long and ends by itself at the bar line. It plays back
looped, through the chord you hold. (SAMPLE REC does the same with every setting in sight, section 6.)

If the IN meter shows CLIP, lower the gain on MASTER. If it barely moves, raise it.

## 4. The screens, one by one

Every screen button on the Tracker opens its screen from anywhere. PERFORMANCE always takes you home. The row of eight keys under the
display is F1 to F8; what they do is written above them on the screen and changes with the screen. A second tap on a focused F key steps
its value; turning the encoder changes it too. SHIFT held shows a second row of values on PERFORM (below).

HELP is SHIFT + CONFIG: it lists every button and every F key of the screen you came from.

![HELP: every button, and below it the F keys of the screen it was opened from](manual/img/help.png)

*HELP, opened from PERFORM. F1 BUTTONS and F2 PADS switch between its two pages; F8 closes it.*

### PERFORM

The home screen. SPEAKS on the left (the source, its gain meter and a CLIP lamp), the voiceprint in the middle with the mode over it, SOUNDS
on the right (the synth, the chord you play in big letters and its notes, the output meter). Under that, a drawing of the pads as they are
now.

| Key | What it does |
|---|---|
| F1 MODE | BANDS, TALK, TUNE, ROBOT or TRACT (section 5). A tap steps round, from TRACT back to BANDS; the encoder stops at the ends |
| F2 SPEAKS | A list: LINE, LEFT, RIGHT, MIC LOW, MIC HIGH, RADIO, USB, or SLOT 1 to 4. UP and DOWN move, F1 takes the row, F8 backs out |
| F3 SOUNDS | A list: SYNTH, NOISE, LIVE, or SLOT 1 to 4 |
| F4 KEY | The root of the key. Chords and pitch correction share it |
| F5 SCALE | The scale: the chords and TUNE stay in it |
| F6 ROW | Which pad row F7 sets: A, B or C. Every tap steps and wraps, the first one too; the encoder stops at the ends; UP is the row above, DOWN the row below. The row's pads flash once, and its name stays lit (with a `>`) while F6 or F7 is in use |
| F7 PLAYS | A list of what F6's row can play in this mode: notes, chords or a parameter strip (below). UP, DOWN or the encoder move, SHIFT with the encoder jumps a group, F7 or F1 takes the row, F8 backs out. A held pad does not change which row it sets. Its value is what F6's row plays (for example VOWEL) |
| F8 | By mode: BANDS and ROBOT SPREAD, TALK QUALITY, TUNE SPEED, TRACT VOWEL |
| SHIFT + F1 | FREEZE the vowel. TALK only: in any other mode it says HEARD IN TALK ONLY and does nothing, and it clears when you leave TALK |
| SHIFT + F2 | GATE: chops the sound in time (OFF, 1/4, 1/8, 1/16, 1/32) |
| SHIFT + F3 | ARP: plays held notes one at a time (OFF, UP, DOWN, UP-DOWN, RANDOM) |
| SHIFT + F4 | GLIDE: slides between notes |
| SHIFT + F5 | FORMANT: moves the voice's colour up or down, as if the singer were smaller or bigger. Heard in BANDS, TALK and ROBOT |
| SHIFT + F6 | THRU: lets some of the voice's top through. Heard in BANDS, TALK and ROBOT |
| SHIFT + F7 | OCTAVE: the octave of the pads, -3 to +2 (the same value the OCT- and OCT+ pads step) |
| SHIFT + F8 | PRESET: eight factory sounds (in TRACT, TRACT's own eight) |

**Dim means not heard in this mode.** A key or pad that is dim is not heard in the mode you are in. A tap on a dim key still sets its value, for the modes where it is heard, and
shows one line saying where (for example HEARD IN TALK ONLY). This holds on PERFORM's SHIFT row and on PATTERN's lanes. PARAMS has no dim keys: it shows only what the mode you are in hears. The
exceptions are the pads of a strip and of row C's FORMANT, which go dark where they are not heard and refuse with the same line, and FREEZE, which latches and so
refuses outside TALK instead of waiting for it (on PERFORM's SHIFT row, and for a controller learned onto it, too). A lit key may be heard only with some carriers (for example, the CARRIER page and ARP are lit in TRACT, whichever carrier plays). GATE, ARP, GLIDE and the CARRIER page are not heard in TUNE (it has no carrier); FORMANT and THRU are heard in BANDS, TALK and ROBOT;
AMOUNT and NOTES IN are TUNE's, and so is SPEED, which is also heard in any mode while the CARRIER source is VOICE (it smooths the note you sing); the mouth values are TRACT's. DRY and LEVEL are heard everywhere. This needs more testing on the unit, for how dim reads on the LEDs and the display.

![PERFORM in TALK mode with a chord held](manual/img/perform-talk.png)

*PERFORM in TALK: the voiceprint is now a smooth curve, the shape of the mouth.*

**The pads layout.** This is the one place that explains the pads; PATTERN, SAMPLE REC and PARAMS play the same layout and
point here (SAMPLE, SAMPLE EDIT, FILE, SAMPLE LOAD and PATTERN's NOTE lane have two note rows of their own, below and section 6). On PARAMS and SAMPLE REC the pads are PERFORM's. The top pad row is always the controls: OCT- and OCT+, VOL- and VOL+, ROOT- and ROOT+, MOD (adds a little vibrato while held) and
BND- and BND+ (bend while held). Below it are three rows, A, B and C from the top, and the twelve columns are the twelve roots, C at the left, in
the key and scale of F4 and F5. Each row has a PLAYS value. F6 ROW chooses the row (the row's pads flash once) and F7 PLAYS chooses what it
plays, in this order:

- **Rows A and B:** NOTE, TRIAD, 7TH, OPEN, POWER, OFF, then the parameter strips of the current mode (below).
- **Row C:** NOTE, TRIAD, 7TH, OPEN, POWER, FORMANT (BANDS, TALK and ROBOT only), BEND, MOUTH (TRACT only), OFF, then the parameter strips.

A tap on F7 opens the list in the SOUNDS panel, headed by group (NOTES, CARRIER, SHAPE, TUNE, TRACT, PLAY), with the row's current value
marked and each strip's current value beside its name. UP, DOWN and the encoder move the cursor round the list; SHIFT with the encoder jumps
to the next group or back to the start of one; F7 or F1 takes the entry, F8 backs out and changes nothing. The list only offers what acts in
the current mode, so no choice leaves a row dark except OFF. A project that holds a value this mode does not offer (from an older version)
loads with that row on the mode's default. The row's name sits beside it on the screen: a note value shows its kind, a strip shows the
parameter's name. The list needs more testing on the unit.

- NOTE: one note per pad. TRIAD, 7TH (the seventh chord), OPEN (root, fifth, ninth: open and airy) and POWER (root, fifth, octave: the
  classic robot fifth): a chord per pad, the one the scale gives on that root.
- **A parameter strip** (any row): the twelve pads of the row are one parameter of the current mode, the same value PARAMS shows. A press sets
  the value and it stays when you let go; PARAMS shows it and the project saves it, as a turn of the encoder would. Pad 1 is the lowest value
  and pad 12 the highest; a value that crosses zero (TILT, TONGUE, JAW, LIPS) has the middle pair both at 0. A parameter with twelve choices or
  fewer gets one pad per choice (the rest dark); one with more spreads the twelve pads over the choices. The nearest pad to the current value is
  the brighter one. A strip row plays no notes, like MOUTH, so the rows above keep the octave to themselves.
  - BANDS, TALK and ROBOT offer SHAPE, SPREAD, SUB, TILT, NOISE, ATTACK, RELEASE, DRY, VOICING, KEY, SCALE, THRU, GLIDE, GATE and ARP; TALK also
    FREEZE. TUNE offers DRY, VOICING, KEY, SCALE, SPEED, AMOUNT, NOTES IN, GLIDE, GATE and ARP.
  - TRACT offers DRY, VOICING, KEY, SCALE, GLIDE, GATE, ARP, the mouth's VOWEL, TONGUE, JAW, LIPS, T GLIDE, SIZE, RES, HISS, ACCENT and SOURCE,
    and the three closures CL LIPS, CL TIP and CL BACK (the whole row is that closure: held while a pad is down, open again on let-go, the
    last pad pressed wins), besides the MOUTH value.
  - Left out, because a wrong pad would do harm or another control owns it: MODE, QUALITY (a cost and a reallocation), LEVEL (the VOL pads), VIB (the
    MOD pad), BEND and FORMANT (their own values), the carrier and modulator sources and the live channel (SOUNDS, SPEAKS and SAMPLE), TRACT's CLOSE
    as a number (the closure strips), SLIDE and MIX (written by the word line and the mode change).
- FORMANT and BEND (row C only): FORMANT is a strip of semitones: a tap sets the formant and nothing is held. BEND is the pitch-bend strip:
  the middle pair is zero, each pad outwards is a semitone, and it springs back when you let go.
- MOUTH (row C only): the mouth strip of TRACT (section 5b). In the other modes the row is dark and the screen says MOUTH SPEAKS IN TRACT.
- OFF: the row is dark and silent.

**Each mode keeps its own three rows.** Switching mode with F1 shows that mode's layout and keeps the edits of every other. The defaults are below. A strip of a parameter another mode uses is
dark and the screen says where it lives (for example "... IN TRACT").

**Each row that plays notes sits one octave above the playing rows under it**, so a note on row B is one octave over the same pad on row C. The
OCT- and OCT+ pads and SHIFT + F7 move the octave of all of them. VOICING SMOOTH keeps to the pad's own octave: a progression along a row still
moves smoothly, and a row higher sounds higher.

**The arrow keys.** LEFT and RIGHT step the value of the F key that has the focus, one click of the encoder each; UP and DOWN do the same (RIGHT and
UP raise, LEFT and DOWN lower). F6 ROW is the exception: UP is the row above and DOWN the row below, as the rows are drawn. On PATTERN the arrows keep their own jobs (LEFT and RIGHT the cursor, UP and DOWN the octave); on SAMPLE and SAMPLE REC LEFT and
RIGHT step the F value and UP and DOWN pick the slot. The arrows leave three values to a tap of their key or the encoder, since a stray press there
changes the sound's character: F1 MODE, F8 QUALITY on TALK and SHIFT + F8 PRESET.

**F7 always sets the row F6 shows.** A pad you hold, of any row, does not change that, and F6's row never moves by itself. SHIFT tapped while a
pad is held latches it, on any row that plays notes; DELETE lets it go. The screen names each row's value beside it (A VOWEL, B 7TH, C MOUTH), and
F7's own value is what F6's row plays. **The five layouts are saved with the project** (section 9) and F4 NEW puts them back to the
default. A project you saved earlier keeps its own layouts until you press NEW.

**The defaults.** Every row does something out of the box: BANDS is A NOTE, B TRIAD, C FORMANT (melody, chords, formant); TALK is A NOTE, B 7TH,
C FORMANT; TUNE is A NOTE, B SCALE, C SPEED (NOTES IN is on, so the pads' notes are the targets; the scale, then the retune speed; KEY would be silent at the default CHROM scale); ROBOT is A NOTE,
B SHAPE, C FORMANT; TRACT is A NOTE, B TONGUE, C MOUTH (the pads' notes are the voice's pitch, the tongue and the MOUTH strip are the mouth).

**Strips live on PERFORM only.** A strip is a PERFORM thing: PATTERN, SAMPLE, SAMPLE EDIT, FILE and SAMPLE LOAD never show one. They share
one pair of note rows of their own, A and B, with NOTE, TRIAD, 7TH, OPEN, POWER or OFF (A NOTE and B TRIAD at first), the same in every mode.
Setting a row on PERFORM does not change that pair, and changing the pair (F6 TOP PADS on PATTERN, F1 LOW PADS on SAMPLE) does not touch PERFORM's
layouts. The pair is not saved in the project: it is A NOTE, B TRIAD again after a restart or a project load (needs more testing). Recording a
strip into a PATTERN lane is not built: a strip sets the value live, on PERFORM.

**Pitch bend.** The encoder bends the synth once it is free: press PERFORMANCE while on PERFORM to free it, and tap any F key to take it
back. It springs back to the middle about a tenth of a second after you stop turning. SHIFT makes the steps larger. Set row C to BEND
(F6, then F7) for a strip of pads: the middle pair is zero, each pad outwards is a semitone. A bend moves the synth and every sounding slot (a
loop, a slice, a region): a slot plays faster or slower by the same number of semitones, up to an octave either way, so its pitch and its length
change together, as with a tape, and the glide of the bend is smooth. The live input has no pitch of its own to move, and the vibrato moves the synth
only. A loop that is on the bar drifts off it while it is bent; when the bend is back in the middle the loop is put back on the bar at the next bar line
(with the same short crossfade as any re-sync). A bend of zero plays a slot exactly as before, and a bend smaller than a fiftieth of a semitone counts as
zero for the slots. A bent slot is noisier in the highs: the read is a straight line between samples, which is cheap but lets some aliasing through (about
43 dB below the signal on a 2 kHz tone and 68 dB on a 441 Hz tone, measured on the host), so a bright loop bent up sounds rougher than a dull one.

### PARAMS

PARAMS is the in-depth editor of the mode PERFORM is in. The title names the mode (PARAMS TALK), and the F keys show only the values that mode hears, in
pages that group what belongs together, with PERFORM's small voiceprint above so you see each change. Under the voiceprint one tab per page names it in one word (CARRIER, VOICE,
NOTES, ...); the lit tab is the page you are on. Press PARAMS again, or DOWN, for the next page, and UP for the one before; the last page goes round to the first. A tap on an F key points the
encoder at it (the key stays lit), a second tap steps it on, and SHIFT with the encoder takes coarse steps. The pads play PERFORM's layout here too.

MODE is not here. Change the mode on PERFORM (F1), as before: PARAMS follows it, and shows the new mode's first page. It does the same when a preset, a project or a MIDI program
change moves the mode. A value keeps its page and its key in every mode that hears it (KEY is always NOTES' F1), and a blank key at the end of a page is only a value that mode does not hear.

| Mode | Pages (tab names) and their keys |
|---|---|
| BANDS, ROBOT | CARRIER (SHAPE, SPREAD, SUB, TILT, NOISE, ATTACK, RELEASE, GLIDE), VOICE (FORMANT, THRU, DRY, LEVEL), NOTES (KEY, SCALE, SPEED, GATE, ARP, VOICING) |
| TALK | CARRIER (as BANDS), VOICE (FORMANT, THRU, DRY, LEVEL, QUALITY, FREEZE), NOTES (as BANDS) |
| TUNE | TUNE (KEY, SCALE, SPEED, AMOUNT, NOTES IN, DRY, LEVEL): one page, because TUNE has no carrier, no gate, no arp and no glide |
| TRACT | CARRIER (as BANDS), MOUTH (VOWEL, TONGUE, JAW, LIPS, T GLIDE, SIZE, RES, HISS), NOTES (as BANDS), MIX (MOUTH, DRY, LEVEL) |

SPEED is on NOTES' third key whatever the carrier is, but it is blank until the CARRIER source is VOICE (it smooths the note you sing); TUNE always has it. Nothing else
moves when the carrier changes, and no page has a single key. VOICING sits on NOTES in every mode: it is how the chords are laid out, so it goes with the notes, not with the mix.
When the mode changes while PARAMS is open it shows the new mode's first page. When only the VOICE carrier changes, the page stays, the focus stays on the same value if the key is still there, and goes to the page's first key if not.
FREEZE is TALK's, so it is on TALK's page only, and it clears when you leave TALK.

**Learn a controller.** Hold SHIFT and press a value's F key: the key lights, the line under the big name says LEARN and the value's name, MOVE A KNOB,
and the next controller you move (CC 0 to 119, on DIN or USB) is bound to that value; the strip says LEARNED CC and its number. From then
on the knob moves the value, and a knob does nothing until it has reached the value, so the value never jumps. The same SHIFT + F key again cancels (LEARN OFF), and
so does leaving the page or the screen, and so does a change of mode that takes the value off the page. While you hold SHIFT the F row reads LEARN on every value that can take a controller and "-" on VOICING, which is the
app's own setting and cannot, and on QUALITY, which is not a live gesture (it reallocates TALK's LPC order). A controller stays bound to the value, not to the key's place on the page, so it moves
the same value whichever mode PARAMS shows (a value the mode does not hear changes nothing you can hear). A value takes one controller and a controller moves one value; learning a value again replaces its controller. The
bindings are kept with the unit's settings, not with the project. Binding CC 1 takes the mod wheel away from the vibrato (a bound controller no longer reaches the app). This needs more testing on the unit with a real controller.

![PARAMS in TALK, page CARRIER: the synth's eight values](manual/img/params.png)

*CARRIER: SHAPE (the wave), SPREAD (how many detuned copies stack), SUB, TILT (dark to bright), NOISE (for the consonants), ATTACK, RELEASE and GLIDE (the slide between notes).*

![PARAMS in TALK, page VOICE](manual/img/params-voice.png)

*VOICE in TALK: FORMANT and THRU, DRY (the voice mixed in as it is), LEVEL, QUALITY (TALK's detail) and FREEZE (it holds the vowel). NOTES, the next page, has KEY, SCALE, SPEED, GATE, ARP and VOICING.*

VOICING has four settings. CLOSE stacks the notes as written. OPEN lifts the third an octave. WIDE spreads the chord over two octaves.
SMOOTH, the default, picks the arrangement nearest the chord before, within the pad's octave, so a progression moves without jumps.

MOUTH is on TRACT's MIX page (F1): PADS, the default, or LIVE. It says what moves the mouth (section 5b, "Your voice as the mouth"). It is not kept with the project and starts as PADS each time; a controller learned onto it moves it live, and the project does not keep that value either.

![PARAMS in TUNE: its one page, TUNE](manual/img/params-tune.png)

*TUNE: KEY, SCALE, SPEED (how fast a note is pulled), AMOUNT, NOTES IN (follow the notes you play), DRY and LEVEL. F8 is blank.*

![PARAMS in TRACT, page MOUTH](manual/img/params-tract.png)

*TRACT's MOUTH page: VOWEL, TONGUE, JAW, LIPS, T GLIDE, SIZE, RES and HISS (section 5b says what each does).*

### MASTER

MASTER draws the whole path large: LIVE on the left, the mode in the middle, OUTPUT on the right. Only one live source runs at a time: the
microphone, the line and the radio share one converter; USB audio is stereo.

![MASTER: the live source and its meter, the mode, the output and its meter](manual/img/master.png)

*MASTER with the monitor on, so you hear the live input directly while you set it.*

| Key | What it does |
|---|---|
| F1 SOURCE | LINE, LEFT, RIGHT, MIC LOW, MIC HIGH, RADIO or USB |
| F2 GAIN | The input gain. The meter shows what arrives |
| F3 MONITOR | Plays the live input straight to the output while you set levels |
| F4 DRY | How much of the original voice is mixed in |
| F5 LEVEL | The output level |
| F6 RETURN | The level of the sound sent back over USB |
| F7 SEEK DOWN, F8 SEEK UP | Radio only: look for the next station |

MASTER opens with no key chosen, so the encoder moves nothing and the strip says TAP AN F KEY FIRST: tap SOURCE, GAIN or the value you want, then turn; a second tap on the same key steps it on. Leaving MASTER forgets the choice. SHIFT makes the encoder coarse; the F row ignores SHIFT here, and so do the arrows and the pads. The MODE panel in the middle only shows the mode (it says SET ON PERFORM F1).

### SAMPLE, SAMPLE REC, SAMPLE EDIT, SAMPLE LOAD

These four screens are the sampler. Section 6 explains them with their pictures.

### PATTERN and SONG

Section 7.

### FILE

Section 9.

## 5. The five modes

The mode is the way the sound is made from the voice, or, in TRACT, played without it. F1 on PERFORM steps through them.

**BANDS.** The classic vocoder. The voice is split into bands, each band's loudness is read, and the same bands of the synth are raised and
lowered to follow. It is clear and robotic, and it works on any voice or any sound. A note at the top of the
range has fewer bright harmonics to shape, so SPREAD, TILT and NOISE help the consonants.

**TALK.** The voice is described as a smooth curve of the mouth's resonances, and the synth is played through that curve. It sounds more
like a voice and less like a filter bank, the nearest thing here to a talkbox. It likes steady, voiced sounds. QUALITY sets how fine the
curve is. FREEZE holds a vowel while you play. FORMANT (SHIFT + F5, or the strip on row C) moves the curve: up makes the voice smaller and
brighter, down bigger and darker, from -12 to +12 semitones; 0 is TALK as it was. It works best within about 6 semitones either way. Going up follows well at QUALITY 16 and
24 up to +6; at +12 the first formant falls short and the top one is gone. Going down is weaker: the first formant follows, the second sits a
little high and the top one fades. QUALITY 8 is only useful going up; use QUALITY 16 or 24 to go down. FORMANT needs more testing by ear.

![PERFORM in TUNE: the pitch ladder](manual/img/perform-tune.png)

*TUNE: the voiceprint is a pitch ladder showing the note sung and the note it is pulled to.*

**TUNE.** Pitch correction. It follows the pitch of what SPEAKS and pulls it to the nearest note of the key and scale you set on F4 and F5.
It plays no synth. SPEED is how fast it pulls: at the fastest it steps from note to note, at the slowest it only leans. With NOTES IN on
(on TUNE's page of PARAMS), it pulls to the notes you hold on the pads or play by MIDI.

![PERFORM in ROBOT with three pads held](manual/img/robot.png)

*ROBOT: BANDS with a fast leveller after it. The voice is flattened and hard.*

**ROBOT.** BANDS with a fast leveller after it. Loud and soft syllables come out at about the same level, which gives the flat, mechanical
voice. Hold a fifth, or set a row to POWER (F6 then F7) and press one of its pads, for the classic robot sound.

**TRACT.** A fifth mode that does not listen to the voice. The synth speaks through a model of a mouth, and you play the mouth. It has its own
section, 5b.

## 5b. TRACT: a mouth after the carrier

![PERFORM in TRACT: row C is the mouth strip, a chord is held on row B and the vowel I is pressed](manual/img/tract.png)

*TRACT: row C set to MOUTH is the strip (the nine vowels, then LIPS, TIP and BACK), the voiceprint draws the mouth and SPEAKS is dimmed.*

**What it is.** The other four modes listen to the voice. TRACT listens to nothing. The sound you play (the synth, the noise or a slot, as
F3 SOUNDS has it) goes through a model of a mouth, a tube from the throat to the lips, and you shape the tube with your hands: a vowel, a jaw,
the lips, a closure. The live voice, if DRY lets it through, goes through the tube too. The voiceprint draws the tube as a profile of the mouth,
and SPEAKS is dimmed: it says NO INPUT: THE MOUTH IS PLAYED. GATE, ARP, GLIDE, BEND, VIB, SUB, TILT, NOISE and the carrier lane work as in the
other modes. The vocoder's eight presets never pick TRACT: you press F1 until it says TRACT. Once there, SHIFT + F8 offers TRACT's own eight (below).

**TRACT presets.** In TRACT, SHIFT + F8 PRESET steps through eight mouths instead of the vocoder's eight: AH, OO, EE (the vowel singers), WOW (a slow glide, a
large tube and an accent that opens the jaw: a talking bass), BREATH (a whisper: the vowel I with the jaw down and the lips rounded so the tube is narrow, HISS 0.9, MIX 0.7; the hiss only sounds while the tube is narrow somewhere), TIN MAN (a small, ringing robot mouth), TINY (a small
throat) and BIG (a large one). Each writes all twelve of TRACT's values (VOWEL, TONGUE, JAW, LIPS, CLOSE, GLIDE, SIZE, RES, HISS, MIX, SLIDE, ACCENT), so nothing
stays from the one before. The source (PADS or LIVE), KEY, SCALE, the loops and the transport are left as they are. CLOSE, MIX, SLIDE and ACCENT are in no project file: opening a project or pressing NEW puts them back to their defaults (MIX 1 in TRACT, 0 elsewhere), so a value a preset set never stays behind. The window shows the name, or --- before the
first. The project keeps the name of the last preset chosen, and a name this build does not know opens as no preset. These are starting points for your ear, chosen without
listening on the unit (AH and BIG the least sure): they need more testing.

**When to pick it.** Pick BANDS or TALK when the sound should follow a voice. Pick TRACT when there is no voice to follow, or when you want to
play the words yourself: a synth line that sings vowels, a bass that goes WOW, a pad that opens from U to A. It gives you a mouth you can write
into a pattern; it does not give you a voice you can talk through.

**Playing it by hand.** There are three ways, and they all move the same mouth.

- **The strips.** In TRACT row A starts as NOTE (the voice's pitch), B as a TONGUE strip and C as the MOUTH strip (section 4, PERFORM: every
  parameter of the mode is a strip, and the layout is kept for TRACT apart from the other modes); change a row with F6 and F7. Row C starts as MOUTH, which puts the vowels and the closures
  on one row (to bring it back: F6 until it says C, then F7 until it says MOUTH). Columns 1 to 9 of the row are the vowels A, A-E, E, E-I, I,
  I-O, O, O-U and U: a press sets the vowel and it stays when you let go. Columns 10, 11 and 12 are the closures LIPS, TIP and BACK: the mouth
  shuts while the pad is held and opens when you let go, and if two are held the last pressed wins. The row plays no notes, so the rows above it
  keep the octave to themselves. Outside TRACT the row is dark and the screen says MOUTH SPEAKS IN TRACT.
- **F8 VOWEL.** In TRACT F8 is VOWEL, the same value as the strip's, one percent a click, with SHIFT ten.
- **PARAMS, TRACT's MOUTH page** (section 4, PARAMS; T GLIDE is the table's GLIDE). Its eight values are the whole mouth:

| Value | What it does |
|---|---|
| VOWEL, 0 to 100 % | The vowel, a sweep through A, E, I, O, U. 0 is A |
| TONGUE, -100 to 100 % | Where the tongue sits, added to the vowel's shape. 0 leaves the vowel as it is |
| JAW, -100 to 100 % | How far the jaw opens, added to the vowel's shape |
| LIPS, -100 to 100 % | The rounding and opening of the lips, added to the vowel's shape |
| GLIDE, 5 to 500 ms | How long the vowel, tongue, jaw and lips take to move. The default is 30 ms: lower is a jump, higher is a slide |
| SIZE | The length of the tube: BIG, LARGE, NORMAL, SMALL or TINY. BIG is a longer tube with lower resonances, TINY a shorter one with higher resonances. Changing it makes a very short dip |
| RES, 0 to 100 | How long the mouth rings. The default is 50 |
| HISS, 0 to 100 | Noise at the narrowest point, and only while the mouth is narrow: the breath of a consonant. The default is 30 |

The mouth values belong to TRACT: PARAMS shows them in TRACT only, where the mouth speaks. They keep their value when you leave TRACT, and a controller learned onto one still moves it.
A strip pad that is held wins over the pattern's mouth line while it is held.

**Your voice as the mouth.** TRACT can also take its mouth from the voice at the input. In TRACT, on PARAMS' MIX page, press F1 (MOUTH) and step it to
LIVE: the program reads the voice into the shape of a tube a few milliseconds at a time (from a window of about 23 ms), and the carrier speaks
through it. Say an "ah" and then an "ee" and the synth does the same. PADS, the default, puts the mouth back in your hands. In LIVE the strip, the lane and
VOWEL, TONGUE, JAW and LIPS do not move the mouth (SIZE, RES and HISS still work, and a closure still mutes), and when the voice goes quiet the mouth
stays where it was. It reads the input the other modes read, set by MASTER's SOURCE and GAIN. It costs more processor than PADS: an estimate of about
12,000 cycles a block, about 5 % of the processor, to be measured on the unit. Honest limits: the reading assumes the usual voice, whose spectrum falls
toward the high end. A flat or very bright source, or a noisy room, reads the mouth wrong, the first formant most of all (a perfectly flat source reads F1 up to
104 % too high and F2 and F3 up to 57 % and 34 % off); a close microphone in a quiet room reads best, and low vowels (U) have the biggest error. A synthetic voice reads within about
13 % on the first formant and 4 % on the second and third. SOURCE LIVE needs more testing with a real voice and room on the unit.

**Writing it: the MOUTH lane.** In PATTERN, F2 (or FX, a second press after SLICE) picks the fifth lane, MOUTH. Each step holds one mouth:
FOLLOW (empty: the step leaves the mouth as it is), one of the nine vowels, a closure, a word, or a wobble. The palette is the top two rows of
pads, and F6 SET flips it between two pages.

- **VOWELS.** Row 0 is the strip: the nine vowels, then LIPS, TIP and BACK. Row 1, columns 1 to 9, is the same vowels with **GLD**, a glide: the
  mouth slides to that vowel and arrives by the end of the step instead of jumping there (the cell shows a `>` before the vowel). A slide is
  capped at 500 ms.
- **WORDS.** Row 0 is eight words: WOW, YEAH, YOI, WAH, OW, BA, DA and GA. Each is a short movement of the mouth that fits the step. BA, DA and GA
  close the mouth on their consonant. Row 1, columns 1 to 6, is **WOB**, a wobble between the neighbouring vowels at 1/4, 1/8, 1/16, 1/32, 1/8T
  or 1/16T, running with the line until the next mouth step. A word pad is heard once when you press it; a WOB pad sounds nothing (the strip
  says WOB PLAYS WITH THE LINE).
- **Closures.** A closure shuts the mouth at the step and opens it when the note's gate ends (on a step with no note, at the step's end).
- **ACCENT.** A step with a note and a LEVEL above 100 (the LEVEL lane) opens the jaw a little more: up to half at 127. It needs no mouth
  value on that step.
- **F7 FILL** writes a random line of vowels, some FOLLOW, some glided, over LENGTH. If the lane holds something you wrote, it asks first (F1
  confirms, any other key leaves it); pressing F7 again rolls another.

A palette pad writes the cursor step, moves on and, in TRACT, sounds the mouth while it is held. With step pads held it writes those steps and
the cursor stays. SHIFT + a palette pad only sounds; SHIFT + a step pad clears the step; SHIFT + F6 empties the lane. Playing writes the mouth in
every mode, but it speaks in TRACT only: outside TRACT the lane is dim, it still edits, and the strip says MOUTH SPEAKS IN TRACT. Stopping the
transport opens the mouth.

**Capturing a voice.** Instead of writing the mouth by hand, let the instrument listen to a recording and write it for you. Record a take in SAMPLE
(or load a WAV into a slot) and leave that slot selected there. In PATTERN pick the MOUTH lane (F2), hold SHIFT and press F8 CAPTURE. The strip shows
READING THE SLOT and a percentage, and a second SHIFT + F8 cancels. When it ends the MOUTH lane holds a line: at every step the nearest of the nine
vowels to what the slot sounds like there, and a closure (LIPS) where the voice goes quiet. Play it in TRACT, on any carrier and at any tempo, and the
mouth says the phrase.

- **Which steps.** A take that is a whole number of bars at the current tempo gives 16 steps a bar, and the pattern takes the first bar; any other
  take is spread over the pattern's LENGTH. Set LENGTH first if you want fewer steps.
- **What it writes.** Only the MOUTH lane, and only when the analysis has finished: cancelling, leaving the screen or a silent take leaves the
  lane as it was. A slot that is empty, still recording or shorter than 1,024 samples (23 ms) says so and does nothing. It writes the pattern that was shown when you pressed it, and as many steps as it was long then (changing LENGTH afterwards does not matter);
  if you switch to another pattern, or the slot is recorded over or reloaded, while it runs, it cancels and writes nothing.
- **The console.** `mcap <A-H> [1-4]` does the same from the USB console for that pattern and slot (the slot SAMPLE shows if you leave it out) and
  prints the line it wrote. It runs the whole analysis in one go, so the main loop (screen, keys) waits while it works, about 50 ms [est].
- **Honest limits.** It picks the nearest of nine vowels, so it hears vowels and nothing finer. Consonants become closures where they are quiet
  and are otherwise read as whatever vowel they sit next to. O and U, and E and I, are the pairs it confuses most. It works best on slow, clear
  speech or singing, one syllable to a few steps; fast or whispered speech will give a rough line, which you then edit by hand. The bar count
  uses the tempo as it is now, so a take recorded at another tempo may be read as one spread over LENGTH. A recording of several bars gives its
  first bar. It needs more testing on real voices: it is tested against a synthetic voice, not yet against people.

**What the project keeps.** The MOUTH lane of the eight patterns, and the eight values of PARAMS' MOUTH page (VOWEL, TONGUE, JAW, LIPS, GLIDE, SIZE, RES and
HISS). It does not keep CLOSE (the closure held now), MIX, SLIDE or ACCENT: the line writes those as it plays, and F4 NEW puts them back. A project
saved with a word or a WOB does not open in the build before this one.

**What it can say, and what it cannot.** TRACT says vowels, the glides w and y (WOW, WAH, YEAH, YOI), and the stops b, d and g (BA, DA, GA) and p,
t and k (the closures LIPS, TIP and BACK, with a short pop). Put a few together and a listener who is told the words hears them: sung words, not
speech. A closure is a mute with a plosive, never an m or an n, so nasals are out of reach; so are l and r, and an s held on: HISS only sounds
while the mouth is closing or on a very narrow vowel. FORMANT on row C does nothing in TRACT. TRACT needs more testing by ear (section 10).

## 6. Samples and slices

A **slot** holds up to 16 seconds of mono sound. There are four. A slot is recorded (SAMPLE REC, or REC on PERFORM) or loaded from a WAV
file (SAMPLE LOAD), cut into up to 24 **slices** (SAMPLE EDIT) and played from the pads (SAMPLE). A slot can be on a side: it can speak,
or it can sound, so a recording can be the voice or the instrument.

### SAMPLE REC

![SAMPLE REC while a take runs](manual/img/sample-rec.png)

*A take in progress. The slot's name and state are in big type; the bar counter is at the right.*

SAMPLE REC makes a take with every setting visible. REC starts it at the next bar line (at once if the transport is stopped); REC again ends
it at the bar. LENGTH can end it by itself.
The pads play PERFORM's layout (section 4, PERFORM), so you can play the synth under the take.

| Key | What it does |
|---|---|
| F1 SOURCE | LINE, LEFT, RIGHT, MIC LOW, MIC HIGH, RADIO or USB |
| F2 GAIN | The input gain |
| F3 LENGTH | FREE, or 1, 2, 4 or 8 bars (the lit key when the screen opens) |
| F4 AUTOPLAY | What the take does when it ends. ON (the default): it plays, looped, where the slot already is, or on SPEAKS if the slot is on neither side. OFF: it waits in the slot and the live input keeps speaking |
| F5 MONITOR | Hear the live input while you record |
| F6 SLOT | Which slot, 1 to 4 (UP and DOWN pick it whatever key is lit) |

A tap on an F key lights it and changes nothing; a second tap steps a value (GAIN by 5, MONITOR on or off, LENGTH and SLOT step on and wrap). LEFT and RIGHT are one encoder click of the lit key (held, they repeat; SHIFT makes GAIN's step 5): LENGTH when the screen opens, GAIN after a tap on F2, MONITOR on (RIGHT) or off (LEFT) after a tap on F5, and so on. F4 AUTOPLAY is the exception: it never takes the light, every tap on it switches ON and OFF, so the arrows do not act on it.

A take of 1, 2, 4 or 8 bars plays back **on the bar**: the loop is brought back to the exact frame of every bar line, with a short
crossfade, so it stays with the tempo (and at half speed, double speed or reverse). A FREE take, and a WAV that is not a whole number of
bars, run free. If the tempo falls, the loop wraps and plays its start again until the line; if it rises, the loop is cut at the line; there
is no time-stretch. Stopping the transport does not stop the loop: it goes on free-running and the next START puts it back on bar 1. A
slot holds 16 seconds, so 8 bars need at least 120 BPM, 4 bars 60 and 2 bars 30. Below that REC does not arm and the strip says
TOO LONG AT THIS TEMPO (you can still choose the LENGTH). Only the LENGTH decides, not every tempo change: if a change makes the
LENGTH no longer fit (more than 0.5 BPM under the limit; REC itself arms right at the limit), an armed take is cancelled and one that is
recording ends at the next bar line, shorter than LENGTH, and the strip says TEMPO CUT THE TAKE SHORT. If the tempo falls so late that the slot
fills before the next bar line, the take ends there instead and runs free, not on the bar, and the strip says TEMPO FILLED THE SLOT. A tempo
change that still fits leaves the take alone (it keeps its bars). A WAV or a project's slot counts as whole bars if its length is a whole
number of bars at the tempo of the moment (a project sets its tempo before its WAVs load); a slot whose length does not change keeps its bar
count, and a copy of a slot with the same length keeps it too. At a tempo that is not a whole number of samples per bar (123 BPM, say), a
bar line can fall a few samples off: a slip of 8 half-samples or less is absorbed without a crossfade, a larger one crossfades. Stopping a loop
fades it over a few milliseconds instead of cutting it.

DELETE while a take waits for the bar line cancels it, as REC does, and the slot keeps its sound; the strip says CANCELLED. DELETE while it records ends it at the
bar and throws it away. DELETE with no take running empties the slot.

AUTOPLAY decides what a take does when it ends, and the line under the bar counter says it before and during the take. ON: if the slot is on SPEAKS or SOUNDS it plays there
again ("THE TAKE WILL SPEAK" or "THE TAKE WILL PLAY ON SOUNDS" before, "PLAYING ON SPEAKS" or "PLAYING ON SOUNDS" after); if it is on neither side it becomes the SPEAKS
source and loops, so what was sung is heard at once. OFF: nothing is routed ("THE TAKE WILL WAIT IN THE SLOT", then "TAKEN, NOT PLAYING"); the take waits in the slot, shown
as "HELD IN THE SLOT", and the live input keeps speaking. A take never goes to SOUNDS by itself: pick SLOT n in PERFORM's F3 SOUNDS list (or F2 SPEAKS on SAMPLE to put it on SPEAKS).
A slot is one reader: if the other side already holds it, the take stays where the slot is and the route's reason shows ("TAKEN: THE ROUTE REFUSES IT" when nothing could be done).
In TUNE the AUTOPLAY key and the line read dim when the take will play on SOUNDS, and in TRACT with MOUTH on PADS when it will play on SPEAKS: see the mode cue in section 10 (this needs more testing on a unit).
SHIFT on the GAIN key gives coarser gain steps, not finer.

### SAMPLE

![SAMPLE: four slots on the left, the selected slot's waveform and cuts, the slices on the pads](manual/img/sample.png)

*SAMPLE: the top two pad rows are slices 1 to 24 of the slot. The bottom two rows play notes, and the screen says which (LOW PADS). One slice is sounding.*

The top two pad rows are the slices of the selected slot. The bottom two rows always play notes, in a pair of rows of their own (the shared notes palette of section 4): row A on the
upper one (NOTE at first) and row B on the lower one (TRIAD), at PERFORM's octave (SHIFT + F3 moves it). Whatever mode PERFORM is in, and whatever its pads
play there (the strips included), these two rows do not change. F1 LOW PADS sets both at once. The right column (LOW PADS) and the line under the slots (for example "LOW
PADS: A NOTE  B TRIAD") say what each row plays. The pair is not saved in the project: it is A NOTE, B TRIAD again after a restart or a project load. A pad sounds while it is held, and
leaving the screen lets everything go. **Hold a slice and a layout pad together, and the slice sings that note or chord.** There is no latch on
this screen: the hand holds the pad. Four slices can sound at once.

With LOOP OFF a slice pad plays its own cut once, from its start cut to its end cut, and then stops; a new hit on it starts it again from its start cut. A slice from
PATTERN's SLICE lane or from MIDI plays its cut once in the same way.

**LOOP ON and a slice pad: the slice repeats.** While the slot's loop is on, a slice pad repeats that slice instead of playing it once; F5 reads ON SL 5 (the slice's number).
With TRIG ONE SHOT it repeats until you press another slice pad (that one repeats instead) or turn LOOP off with F5. With TRIG GATE it repeats while you hold the pad, and
when you let go the slot's whole loop goes on from where it is. A slot has one repeating slice at a time. LOOP OFF stops everything of the slot, the repeat included. A ONE SHOT
repeat goes on after you leave the screen, until F5 or another pad stops it; when you come back F5 still reads ON SL 5, and F5 OFF, loading a project or a new take in the slot also ends it. A repeating slice is heard where the slot is heard: on SPEAKS the vocoder hears it.

| Key | What it does |
|---|---|
| F1 LOW PADS | What both lower pad rows play, never off: NOTE+TRIAD (the default: row A plays notes, row B triads), NOTE, TRIAD, 7TH, OPEN, POWER. A tap steps to the next and wraps to the first; the encoder steps and stops at the ends. Both rows flash dark for a moment. With one kind (NOTE, TRIAD, 7TH, OPEN or POWER) both rows play the same kind, row A one octave above row B. A pair set elsewhere that is not in the list (for example PATTERN's) shows row A's kind |
| SHIFT + F2 SLICE CH | The MIDI channel that fires slices: SAME, 1 to 16 or OFF (section 8). Hold SHIFT and turn the encoder; its value shows in the SHIFT row, and the screen's right column reads SLICE CH |
| F2 SPEAKS | What the vocoder listens to, as PERFORM's SPEAKS chip names it: LIVE or SLOT 1 to 4. A tap makes the selected slot speak; a second tap goes back to LIVE. The encoder steps LIVE, SLOT 1, 2, 3, 4 over the slots that hold a sound and stops at the ends. An empty slot says "EMPTY: NOTHING TO PLAY". The line under the slots says what the slot does now. The key is dim where SPEAKS is silent (TUNE's SOUNDS slot, TRACT), and a tap says why |
| F3 SPEED | 0.5X, 1X or 2X |
| F4 DIR | FWD or REV |
| F5 LOOP | The whole slot plays round from the bar. ON SL 5 while a slice pad repeats that slice (see above) |
| F6 TRIG | ONE SHOT (the default): the slice plays to its end cut. GATE: it also stops when you let go of the pad |
| F7 SLICES | MONO (the default): a hit fades out the slot's sounding slice and starts the new one. POLY: up to four slices overlap |
| F8 LEVEL | -24 to +12 dB |

A slice pad past the slot's count says how many slices there are ("5 SLICES", "NO SLICES: SLOT IS EMPTY" on an empty slot). A slot that does not speak and is not the carrier (the slot panel reads PADS, and the centre says PADS ONLY) has its loop stopped and only its slice pads
speak, over LIVE or together with the slot that speaks ("SLOT 1: ITS PADS SPEAK OVER LIVE" says so); if you turn its F5 LOOP on, the slot is made to speak first ("SLOT 1 SPEAKS"), so the loop is heard; F2 takes it back to LIVE. A slot that sounds (SOUNDS) keeps its side when LOOP goes on. While the slot itself records, a slice pad says "SLOT IS RECORDING: NO SLICE", and while it is armed to record "SLOT IS ARMED TO RECORD: NO SLICE". When a change of the slot's length
puts hand-edited cuts back to even ones, the strip says "LENGTH CHANGED: CUTS EVEN AGAIN". In TUNE a SOUNDS slot, and in TRACT a SPEAKS or PADS slot with MOUTH on PADS, shows dim with a line saying why (section 10, "The mode cue"); with MOUTH on LIVE the slot is not dim and the line says it moves the mouth.

UP and DOWN pick the slot. F1 LOW PADS changes the shared pair of note rows (PATTERN's NOTE lane uses it too); PERFORM's F6 and F7 are separate. SAMPLE EDIT, FILE and SAMPLE LOAD play the same two rows.

### SAMPLE EDIT

![SAMPLE EDIT: the take across the screen with its cuts numbered](manual/img/sample-edit.png)

*SAMPLE EDIT: the take across the screen, cuts as numbered marks, trim as brackets.*

SAMPLE EDIT shows the take across the whole screen. Cuts are marks numbered 1 to 24. The encoder moves the cursor, about one picture pixel a click
(SHIFT: coarser); LEFT and RIGHT jump from cut to cut. INSERT makes a cut at the cursor, DELETE removes one. A pad of the top rows plays that slice;
the bottom rows play SAMPLE's two note rows (A NOTE, B TRIAD at first; F1 LOW PADS on SAMPLE changes them). The header names the slot's place in the
route: `SLOT 2  SPEAKS` (the slot is the vocoder's voice), `SLOT 3  SOUNDS` (it is the carrier) or nothing.

**Picking a slice.** Tap a top pad: that slice plays, its pad lights bright, and the screen marks it three ways: its span is filled on the cut strip,
its start line is the brightest, and the line under the picture reads `SLICE 3 OF 8   START 0.500 S   LEN 0.250 S`. LEFT and RIGHT pick the previous or
next slice and stop at the ends; a tap on another pad picks that one. The encoder now moves the picked slice's start (it stays between its neighbours and
lands on a quiet crossing point; SHIFT + encoder moves one sample a click). An F key or SHIFT + UP / DOWN lets the slice go (that F key counts as a first tap).
Slice 1's start is the trim START (F4): moving one moves the other, and pad 1 plays from START while it is moved (up to the second slice). A pad past
the last slice does not make a slice; INSERT does, and the line says so.

**Cutting and deleting.** With a slice picked, INSERT cuts it in its middle (`CUT 5 ADDED: 9 SLICES`) and DELETE removes its start cut: the pick
moves to the slice that swallowed it (`CUT 5 REMOVED: 7 SLICES`). Slice 1 has no cut to remove, and the line says so. With nothing picked, DELETE
removes the cut nearest the cursor inside the picture, or says there is none there. SHIFT + DELETE still empties the slot and asks first.

**Zoom.** UP zooms in and DOWN out, in steps of two, every step half (or double) the one before, from WHOLE SLOT down to about 8 ms across (the label reads `ZOOM 2.0 S`, `ZOOM 250 MS`, `ZOOM 7 MS` or `WHOLE SLOT`;
a slot that is not a power of two long shows odd values such as `ZOOM 3.4 S`). The
picture is centred on the picked start (or on the cursor) and stays inside the slot; with F4 START or F5 END focused it follows the bracket the encoder moves, and focusing one brings it into view. The cuts in view carry their numbers, and the encoder step follows the
zoom: at the deepest zoom a click is one sample. SHIFT + UP and SHIFT + DOWN pick the slot before and after (the one SAMPLE shows).

**Hearing a slice.** A slot's slices are the vocoder's voice, unless the slot is the SOUNDS source. In BANDS, TALK and ROBOT a slice pad is heard
only while a synth note sounds, and the live input is still mixed in unless the slot speaks. So: **SHIFT + a lower pad latches its note or chord**
(the pad's LED shows it; SHIFT + the same pad lets it go, SHIFT + another pad moves it; a plain press plays over it), and **F8 SPEAKS** makes the slot
the vocoder's voice (the live input leaves the mix; press it again to give the voice back to LIVE; an empty slot says so). The latch is let go
when you leave the screen, change the mode or load a project. When a slice pad is pressed in BANDS, TALK or ROBOT with no note sounding, the screen says
`SHIFT + LOW PAD LATCHES A NOTE` (it cannot see a note the step sequencer is playing). The new keys and the zoom need more testing on the unit.

| Key | What it does |
|---|---|
| F1 CUT EVEN | Cuts into 8, 16 or 24 equal parts |
| F2 CUT BEATS | Cuts on the beats the take was recorded on (a recording only) |
| F3 CUT HITS | Cuts at the loud starts of sounds. Higher is more sensitive |
| F4 START, F5 END | Trim the take |
| F6 NORMALISE | Raises the level to the top. F1 confirms |
| F7 SAVE WAV | Saves the slot as a WAV file in the card's `Samples/` folder |
| F8 SPEAKS | The slot speaks (the vocoder's voice) or goes back to LIVE |
| SHIFT + F8 CLR CUTS | Removes every cut (asks twice on a table you edited) |

F1 to F3 and SHIFT + F8 replace the cuts you have; the screen says how many it replaces, and the key reads CONFIRM: press it again to commit. While
something waits for its F1 (NORMALISE, the trim, COPY, SHIFT + DELETE) F1 reads CONFIRM, and the trim and NORMALISE say NO UNDO. A cut lands on a quiet crossing point close by
so it does not click. COPY copies the slot, cuts and all, to the next empty slot.

### SAMPLE LOAD

![SAMPLE LOAD: the WAV files on the card](manual/img/sample-load.png)

*SAMPLE LOAD with an empty card: it says so. Put WAV files in the app's folder and they are listed here.*

SAMPLE LOAD lists the WAV files in `Apps/o3_voc03er/` on the card. To put files there, use Config's CARD AS DISK to see the card from a
computer, or put the card in a computer. UP and DOWN pick a file, F1 loads it into the selected slot, F8 goes back. A slot that already
holds a sample asks first: F1 confirms, any other key cancels. A slot that cannot take a load says what to stop: "SLOT IS RECORDING: STOP THE TAKE FIRST", "SLOT LOOP IS ON: STOP THE LOOP FIRST"
(the loop playing or queued) or "A SLICE IS SOUNDING: WAIT FOR IT" (a slice of any slot borrows the slot's memory while it sounds). The list shows at most 64 files; a folder with more says
"LIST CUT AT 64 FILES: THE FIRST 64 SHOW" (keep the files you want in the first 64 by name, or move the rest into another folder). A file loaded into a slot that is on PADS says "PADS ONLY":
only its slices speak, and F2 SPEAKS on SAMPLE makes it speak.

What loads: PCM WAV files of 8, 16, 24 or 32 bits, and 32-bit float files, mono or stereo (stereo is mixed to mono), at any rate from 22.05 to 96 kHz
(so 48 kHz, 24-bit files from a computer load as they are). A slot is always mono, 16-bit, 44.1 kHz, so a file that is not that is converted while
it loads: the rate with a windowed-sinc resampler (a flat pass band to about 15 kHz, content above 44.1 kHz's half rate is filtered out before it can
fold back as noise), the bit depth by rounding to the nearest 16-bit value (no dither: the rounding noise is about 98 dB below full scale). A file that
is already 16-bit 44.1 kHz is copied unchanged. The 16-second limit counts the converted sound, so a 20 second file gives its first 16 seconds. The strip
says what happened: "LOADED 1.500 S, 48K 24B TO 44.1K 16B", "FIRST 16.000 S LOADED, FILE HAS 20.000 S, ..." for a cut file, or why the file was refused:
"NEEDS PCM OR FLOAT" (ADPCM, MP3 or another coding), "NEEDS 8 TO 32 BIT", "NEEDS 22 TO 96 KHZ", "NEEDS MONO OR STEREO", "BROKEN WAV FILE", "NOT A WAV FILE",
"EMPTY WAV FILE". A refused file leaves the slot as it was. A converted file takes longer to load, since every sample is resampled by the processor
(a 16 second stereo 24-bit 48 kHz file is estimated at 5 to 6 seconds; this needs measuring on the unit); the strip shows the progress and F8 cancels.
SAVE WAV in SAMPLE EDIT still writes 16-bit mono at 44.1 kHz, the rate of the slot. The conversion needs more testing on the unit.

## 7. Patterns and songs

### PATTERN

![PATTERN: sixteen steps with five lanes under each](manual/img/pattern.png)

*PATTERN playing with FOLLOW on: the bright frame is the playhead.*

Eight patterns, A to H, of up to sixteen steps each. Each step has five lanes: **NOTE** (a chord or a note), **CARRIER** (what sounds on that
step: FOLLOW, SYNTH, NOISE, a slot or LIVE), **LEVEL**, **SLICE** (which slice of the speaking slot fires) and **MOUTH** (TRACT mode's mouth). NOTE, INSTR (carrier), VOL (level)
and FX (slice, then MOUTH on a second press, then slice again) pick the lane; so does F2. UP and DOWN are the octave (the same one PERFORM and SAMPLE use; the strip says it).

The pads: the top two rows are the lane's palette (chords or notes for NOTE, the carriers for CARRIER, steps of level, slices). The CARRIER palette has eight pads: FOLLOW, SYNTH, NOISE, SLOT 1 to 4 and LIVE.
LIVE sounds that step through the audio input, as SOUNDS LIVE does on PERFORM (in TRACT the input goes through the tube); it works whether SPEAKS is a slot or the input (with no input ever chosen on F2 SPEAKS it plays whatever the input gives at power-up, likely silence or noise), and the next step with a carrier
switches away from it (FOLLOW leaves the carrier as it is). A pattern file with a LIVE step does not load on an older image. Switching to LIVE mid-pattern needs more testing on the unit (a click at the switch is possible). A palette
pad writes into the step under the cursor and moves the cursor on; with one or more step pads held it writes every held step instead and the cursor stays (on every lane, not only NOTE). The lower two rows, columns 1 to 8, are the sixteen steps up to LENGTH
(a step pad past LENGTH does nothing). The lower right columns pick the pattern, A to H. SHIFT + a step pad makes it a rest.

On the VOL lane a palette pad also sounds the step's note or chord at that level while it is held (a rest sounds nothing); on the FX lane it
fires that slice of the slot SPEAKS is on, as SAMPLE's pads do (nothing with SPEAKS on LIVE or above the slice count). INSTR stays silent.

On the NOTE lane the palette is the shared pair of note rows, A and B (the notes palette of section 4, the same one SAMPLE plays; PERFORM's layout does not change it). F6 TOP PADS sets both rows at once to one of six kinds, the same six as F1 LOW PADS on SAMPLE: NOTE+TRIAD (A plays notes, B triads), NOTE, TRIAD, 7TH, OPEN and POWER. A tap steps to the next and wraps from POWER to NOTE+TRIAD, the encoder stops at the ends, both rows flash once, and there is no OFF. If the pair is none of the six, the key reads MIXED and the first tap sets NOTE+TRIAD. A row of the same kind plays and writes the same notes here and on SAMPLE.
A pad writes what its row plays: a note or a chord of the row's kind. **For a bass line of
single notes, set the row to NOTE**: sixteen presses are sixteen notes. A palette pad sounds while it is held, and keeps sounding when the pattern plays the same note
(its gate off, a repeat or Stop does not cut it; needs more testing); SHIFT + a palette pad sounds
and writes nothing.

| Key | What it does |
|---|---|
| F1 PATTERN | A to H |
| F2 LANE | NOTE, CARRIER, LEVEL (ACCENT in TRACT, where the level drives the jaw), SLICE or MOUTH |
| F3 LENGTH | 1 to 16 steps |
| F4 TEMPO | The tempo |
| F5 VALUE | The cursor step's value in the lane; the encoder edits it. LEVEL runs 1 to 100, and 1 to 127 in TRACT; the palette's last pad is the top |
| F6 TOP PADS | NOTE lane: both palette rows at once, NOTE+TRIAD, NOTE, TRIAD, 7TH, OPEN or POWER (a tap steps and wraps, the encoder stops at the ends, both rows flash, never OFF). MOUTH lane: SET, the palette VOWELS or WORDS (outside TRACT it flips the page and says where the lane is heard). Dim on the other lanes; a tap there says NOTE LANE ONLY |
| F7 | Blank, except FILL on the MOUTH lane (outside TRACT it fills and says HEARD IN TRACT ONLY) |
| SHIFT + F6 CLR LANE | Clears the lane. F1 confirms |
| SHIFT + F7 FOLLOW | The cursor rides the playhead |
| SHIFT + F8 CAPTURE | MOUTH lane only: reads the slot SAMPLE shows into the lane. Pressed again while it reads, it cancels |
| F8 SAVE | Saves the eight patterns to the card (`pattern.o3a`). The project is FILE's: F2 SAVE there |

**The MOUTH lane** writes TRACT's mouth, one value a step. Its palette has two pages, VOWELS and WORDS (F6 SET flips them), and F7 FILL writes a
line for you. Section 5b says what each value does and how the pads write it. SHIFT + F6 empties the lane, as on the others.
SHIFT + F8 CAPTURE writes the lane from a recording: see "Capturing a voice" in section 5b.

PLAY starts and stops the transport. COPY copies the step under the cursor; INSERT puts the copy there and moves the steps after it one
to the right (the last step inside LENGTH falls off). DELETE empties the step. With SHIFT they work on the whole pattern: SHIFT + COPY
copies it, SHIFT + INSERT pastes it over the current one, LENGTH too, and SHIFT + DELETE empties it after F1 confirms (any other key
leaves it as it is). A different pattern starts when the one that plays ends. While a SONG chain plays, a pattern pad says SONG PLAYS THE CHAIN and changes nothing.

Under the strip two lines tell where the pads are: rows A and B above (F6 TOP PADS), steps and patterns below, SHIFT + a step for a rest.

The lane's header says where the lane is heard: CARRIER and LEVEL are not heard in TUNE, and the MOUTH lane speaks in TRACT only. The lane still edits, and its name is dim. With nothing under the cursor, the strip says F8 SAVES and that holding SHIFT shows the SHIFT row (CLR LANE, FOLLOW, CAPTURE). On the SLICE lane, VALUE and the palette say SPEAKS HAS NO SLICES when the slot SPEAKS is on has none.

### SONG

![SONG: a chain of three entries, the first one playing](manual/img/song.png)

*SONG playing: the bar at the left of a row says it is the lap in progress.*

SONG chains patterns: up to sixteen rows, each a pattern letter and a number of repeats. UP and DOWN move the cursor; the encoder changes the
row's letter, and after F2 its repeats (1 to 64). F1 ADD or INSERT adds a row after the cursor, DELETE removes one, on a second tap: the first tap says
"DELETE AGAIN TO REMOVE THE ROW" and any other key, moving the cursor or two seconds of waiting cancels it (a chain row has no undo). F8 is labelled PERFORM: SONG has no screen to go back to. PLAY with SONG open
starts from the cursor's row; with the transport already running PLAY is the transport's, as everywhere: it stops the chain and the screen says "CHAIN STOPPED" (while a MIDI master is followed, PLAY is only a request to the master: the screen says "STOP ASKED OF THE MIDI MASTER" and the chain goes on until the master stops), and the next PLAY starts it from the cursor
(it never restarts in the middle of a lap, which would cut the lap). The chain plays on when you leave SONG, so you can play over it from PERFORM; PLAY stops it. A change lands
at the end of the pattern that plays. While the chain plays it owns what plays: a pattern you pick in PATTERN only changes what that screen shows and edits. Opening a project while it plays starts the new project's chain at its first row after the lap in progress. At the end of the chain the transport stops, unless F3 LOOP is on, which runs the chain round.

Two lines at the foot of the screen say the keys (they give way to a status message while one shows): UP and DOWN pick a row; the encoder sets its pattern, A to H; F2 points the encoder at the row's repeats (1 to 64) until F2 again; F1 or INSERT adds a row after the cursor with the same pattern and one repeat; DELETE twice removes it; F3 LOOP restarts the chain at row 1 when it ends. LEFT, RIGHT, SHIFT with an F key and F4 to F7 do nothing here.

## 8. MIDI

MIDI notes play the synth beside the pads and share its voices. They also feed TUNE when NOTES IN is on. Pitch bend moves the synth and the sounding slots; the
mod wheel (CC 1) adds vibrato (to the synth). A controller can be learned onto a value of PARAMS, sixteen at most (section 4, PARAMS: SHIFT + the value's F key).

Slices: notes from 36 up fire one slice each of the SPEAKS slot (36 is slice 1). Velocity 100 and above is full level; lower is softer.
The slice plays to its end; letting go of the key does not stop it. Every other note plays the synth.

SLICE CH (SAMPLE, SHIFT + F2, or `slicech` in the USB console) chooses where those slice notes come from:

- **SAME** (the default): as above, on the channel you set for MIDI in Config.
- **1 to 16**: the notes of your channel always play the synth. The notes of the slice channel fire slices (note 36 is slice 1, same
  velocity rule, one shot), never play the synth and are never written into PATTERN's NOTE lane. A slice channel equal to your channel
  works as SAME.
- **OFF**: MIDI never fires a slice, and every note on your channel plays the synth.

SLICE CH needs your own channel set to 1 to 16 (IN CHANNEL in Config, MIDI): with the channel on OMNI the notes of the slice channel
play the synth as every other note does, and SAMPLE shows SET MIDI CH next to the setting. The setting is saved with the project's
configuration (`config.o3a`); a file from an earlier version loads as SAME. `slicech` alone prints it; `slicech same`, `slicech 10`
and `slicech off` set it. Slices on their own channel need more testing with a real controller, USB and DIN.

Patterns: a program change from 0 to 7 picks pattern A to H, at the end of the pattern that plays. A higher number is ignored. While
a SONG chain plays, it owns the pattern, so a program change then does nothing. Notes you play are written into PATTERN's lanes the way
the pads write them.

Clock: set FOLLOW in Config, and FOLLOW PORT to the port the clock comes from. USB clock has been tried with a host; the DIN port has not.

## 9. Projects and the card

A project is a folder under `Apps/o3_voc03er/` that holds one project file. It keeps the eight patterns, the song chain, the route, the
vocoder's settings (PERFORM's and PARAMS' values), the tempo, the current pattern, the octave, VOICING, the BEND ROW and its range,
the pads layout of each of the five modes (the three rows' plays, a strip being kept by the parameter it sets), the preset's name, PATTERN's FOLLOW, the MOUTH lane, TRACT's
mouth values (section 5b says what is not kept), and each slot's cuts,
trim, speed, direction, level, TRIG, SLICES (MONO or POLY) and the name of the WAV it came from. It does not keep the WAV sound itself: F5 SAVE WAVS
writes the sounds into the project folder. TRACT's CLOSE, SLIDE, ACCENT and MIX, and SOURCE set from a strip, are not saved with the project. A project saved by v0.2.0 opens with its KEYS or CHORDS mapping as it was (the chord kind
becomes the layout's chord pads), VOICING SMOOTH, FOLLOW off, no preset, TRIG ONE SHOT and SLICES MONO. The preset is kept by name: a
name this build does not know opens as no preset. A project saved before the layouts were per mode opens with its one layout on BANDS, TALK, TUNE
and ROBOT and the TRACT default (A NOTE, B TONGUE, C MOUTH); a saved piano value opens as the plain note, chord or OFF of the same kind. An older
image that opens a new project reads the BANDS layout and ignores the rest (needs more testing). F4 NEW puts all of these back to their defaults (a project you saved earlier keeps its own pads layouts until NEW; the defaults are the ones in section 4).

![FILE: the list of projects](manual/img/file.png)

*FILE after a save: one project, and the strip says SAVED.*

| Key | What it does |
|---|---|
| F1 OPEN | Opens the project under the cursor, then its WAV files one by one; a slot is grey until its sound has landed |
| F2 SAVE | Saves the current project. The first time it asks for a name |
| F3 SAVE AS | Saves under a new name |
| F4 NEW | Starts an empty project: the pattern, song, route and settings go back to their defaults, **the slots keep their audio** (the screen says "SLOTS KEEP THEIR AUDIO"); it asks first, F1 confirms |
| F5 SAVE WAVS | Writes each slot's sound next to the project file. A slot can take a while on a slow card. Without a project name it says "NAME THE PROJECT FIRST" (F2 SAVE names it) |
| F8 BACK | Leaves FILE. While a save, an open, SAVE WAVS or an open's WAVs runs it reads CANCEL and gives the job up: a cancelled save leaves the old file whole, a cancelled open changes nothing, a half-written WAV is removed and the rest are not written. The last moment of an open (RESTORING) cannot be stopped: it ends on its own |

![SAVE AS: a name typed with the pads' keyboard on screen](manual/img/file-name.png)

*SAVE AS: the name is typed with the on-screen keys. F1 changes the type, F7 accepts, F8 cancels.*

A save or an open goes on when another button leaves FILE, and what it said is on the strip at your next visit. SAMPLE LOAD does not
open while FILE's job runs, and the screen you are on says FILE IS BUSY. A slot grey in PERFORM is LOADING in SAMPLE, SAMPLE EDIT and SAMPLE REC, and REC and DELETE leave it alone, and SAMPLE EDIT edits nothing, until its sound has landed.

F6 and F7 are blank on the list (they do nothing there) and the pads are unlit on FILE. A save that finds a slot holding audio the project does not carry says
"SAVED. AUDIO NOT SAVED: F5": the project file holds the cuts and the WAVs' names, not the sound. Press F5 to write the sounds. The check compares lengths and also remembers which slots a take or a load has replaced since the last F5, so a new take of exactly the length of a saved
WAV is noticed too (the flag lives until the app restarts: a project opened from the card has its slots saved, and F5 clears them). Saving with the project file alone (F2) keeps the old WAV's name for such a slot, with the warning: press F5 after any take you want to keep. Each F5 writes new numbered WAVs (slot1_001.wav, slot1_002.wav, ...) and the old ones stay on the card (the app has no
way to delete a file there): clear them by hand with CARD AS DISK when the folder gets long.

Nothing is saved by itself: FILE saves only when you tell it. The app also keeps two small files of its own in `Apps/o3_voc03er/`, `pattern.o3a` (the working patterns) and
`config.o3a` (settings); the previous copy of the patterns is kept beside it as `pattern.o3a.bak`. A damaged project file is refused
whole.

## 10. Known limits, and what needs more testing

This is a first public pre-release. The firmware has been measured on a unit for the load it puts on the processor, and the screens have
been read there, but several things need more testing, most of all by ear. In particular:

- **The mode cue.** A slot's side means nothing to the ear in two modes. TUNE plays no synth, so a slot on SOUNDS (its loop, its slices) is not heard there. TRACT with MOUTH on PADS listens to nothing,
  so a SPEAKS slot, and the slices of a slot on PADS, are heard only through DRY. With MOUTH on LIVE the same sides also move the mouth (the TRACT analyses what the modulator carries, a SPEAKS slot included), so they are not dim and the line says "MOVES THE MOUTH (LIVE) AND DRY". SAMPLE, SAMPLE REC (its AUTOPLAY key and line) and the side badge show such a side dim with a line saying why; the slot and its
  route are untouched and come back as they were in the other modes. This is a hint on screen: the ear on the unit decides, and it needs more testing there.
- **The sound.** How the modes sound with a real voice, how the synth sounds at the top of its range, the level of TALK, and the
  eight presets are for your ears. Nothing here says a sound is right.
- **TRACT.** The words, the closures, WOB, the accent and the strip by hand need more testing by ear; the pop of a closure too. Entering TRACT may
  let the raw carrier through for a few milliseconds before the mouth fades in. On a unit, TRACT with the mouth at rest takes about a fifth of the
  processor; the load while the mouth moves, with many notes and loops, needs more testing.
- **SLICE CH.** Slices on their own MIDI channel have been run in the simulator only. With IN CHANNEL on OMNI the setting cannot work (the
  screen says so). It needs more testing with a real controller, on USB and on the DIN port.
- **The microphone and the radio.** Switching between them has been seen; how they sound needs a person.
- **Clicks** at the switch when a take is captured from a live source need more testing.
- **Glide, gate, arp, vibrato and the bend** need more testing by hand and ear, and so do the formant steps and the VOL, ROOT and BND
  pads of the controls row.
- **MIDI.** USB MIDI has been seen working. A DIN cable has not been run: no interface was at hand.
- **SONG.** The exact step where a change lands, the stop at the end of a chain and a pattern of length 1 at a boundary need more testing
  on the unit.
- **Heavy use.** With every mode, a long chain and a full set of slots on screen at once, the processor load has not been measured in all
  its combinations.
- **WAV loading** converts 8, 16, 24 and 32-bit PCM and 32-bit float files of 22.05 to 96 kHz (mono or stereo) to the slot's 16-bit 44.1 kHz mono; other
  codings, more than two channels and other rates are refused (SAMPLE LOAD). A file longer than 16 seconds (after conversion) is cut to its first 16. The
  conversion needs more testing on the unit, in particular its load time and how it sounds on bright material.
- **Bending a slot** changes its speed, so its pitch and its length move together: there is no pitch shift that keeps the length. The sound of a bent
  slot (the interpolation is linear, so a bright recording can sound duller or carry some aliasing, most on a bend up), the return to the bar after a bend,
  and the load of several bent slots at once need more testing on a unit.
- **Loops on the bar.** The re-sync at each bar line (the crossfade and the snap by ear, the load on the bar-line blocks, a tempo change during a take, the
  edge of 8 bars at 120 BPM, a slow or fast tempo against a loop, FOLLOW jitter around the edge, which two slots of one length count as a copy) needs more testing on a unit. A REC press a little after a bar line
  waits for the next line; there is no early-press tolerance.
- **The voiceprint in the pictures** comes from plain tones. A voice draws more bars.

If something here fails on your unit, the card and the reset button are the way back (section 2).

*This manual follows the firmware as it is in this repository. The pictures are made by `app/scenarios/manual_screens.txt`.*
