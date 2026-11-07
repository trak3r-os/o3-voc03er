#!/usr/bin/env python3
"""The presentation video's plan: 16 segments, one simulator scenario each, and timeline.json.

The simulator's clock moves only by `wait`, and every panel command costs a little (a tap is 61 ms), so the generator keeps its own clock in
milliseconds and emits the `wait` that reaches each target time. The recording (`record on`) starts at t = 0 of a segment, the same instant
the modulator WAV (`input wav`) starts, so voice, chords and pictures share one clock. timeline.json is the single source of truth for the
beat, the voices and the compositor.

usage: plan.py <work dir> <scenario dir>
"""
import json
import os
import sys

BAR = 2000          # one bar at 120 BPM, ms
TAIL = 80           # recorded past a segment's end, for the crossfade into the next one
TAP = 61            # a tap costs hold (60) plus the command (1)

PAD = {"Dm7": 26, "Fmaj7": 29, "Am7": 33, "Em7": 28}          # 7TH row of the chord grid, KEY C
LOOP = ["Dm7", "Fmaj7", "Am7", "Em7"]


class Scn:
    """Events are collected at their target times (in any order) and emitted sorted, with the `wait` between them."""

    def __init__(self, sg):
        self.sg = sg
        self.pre = []
        self.ev = []          # (t, seq, line, cost)
        self.cur = 0.0
        self.cues = []

    # setup (before the recording starts, time does not matter)
    def setup(self, *cmds):
        self.pre.extend(cmds)

    def hold(self, chord):
        self.setup(f"paddown {PAD[chord]}")

    def begin(self):
        pass

    # timed
    def at(self, ms):
        self.cur = float(ms)

    def cmd(self, line, cost=1.0):
        self.ev.append((self.cur, len(self.ev), line, cost))
        self.cur += cost

    def tap(self, key):
        self.cmd(f"tap {key}", TAP)

    def turn(self, n):
        self.cmd(f"turn {n}")

    def shift_focus(self, key):
        self.cmd("press shift")
        self.tap(key)
        self.cmd("release shift")

    def cue(self, t_ms, caption, card=None):
        """A picture from t_ms on: a snap of the screen (or a full-frame card) with its caption."""
        name = f"c{len(self.cues):02d}"
        if card is None:
            self.ev.append((float(t_ms), len(self.ev), f"snap {name}", 0.0))
        self.cues.append({"t": t_ms, "snap": None if card else name, "caption": caption, "card": card})

    def ramp(self, t0, t1, n, cmd):
        for i in range(n):
            self.at(t0 + (t1 - t0) * i / n)
            self.cmd(cmd)

    def pad_hit(self, pad, gap=40):
        self.at(self.cur + gap)
        self.cmd(f"paddown {pad}")
        self.at(self.cur + gap)
        self.cmd(f"padup {pad}")

    def emit(self, end_ms):
        lines = ["wait 300"] + self.pre + [f"input wav mod/{self.sg['n']:02d}.wav", "record on"]
        clock = 2.0
        for t, _, line, cost in sorted(self.ev):
            n = int(round(t - clock))
            if n > 0:
                lines.append(f"wait {n}")
                clock += n
            lines.append(line)
            clock += cost
        n = int(round(end_ms - clock))
        if n > 0:
            lines.append(f"wait {n}")
            clock += n
        lines.append(f"wav seg{self.sg['n']:02d}")
        self.cues.sort(key=lambda c: c["t"])
        return lines, clock

    def chord_changes(self, seq):
        """seq: one chord per bar; the pads change on the bar line (new pad down, old pad up)."""
        for b in range(1, len(seq)):
            if seq[b] != seq[b - 1]:
                self.at(b * BAR - 1)
                self.cmd(f"paddown {PAD[seq[b]]}")
                self.cmd(f"padup {PAD[seq[b - 1]]}")


def loop_chords(bars, start=0):
    return [LOOP[(start + i) % 4] for i in range(bars)]


SEGS = []


def seg(n, name, bars, chords, intensity=None, voice=None):
    def deco(fn):
        SEGS.append({"n": n, "name": name, "bars": bars, "chords": chords,
                     "intensity": intensity or [1.0] * bars, "voice": voice or {}, "fn": fn})
        return fn
    return deco


@seg(1, "title", 3, ["Dm7", "Fmaj7", "Am7"], [0.0, 0.45, 0.8])
def s01(s):
    s.hold("Dm7"); s.begin()
    s.chord_changes(s.sg["chords"])
    s.cue(0, "", card="title")


@seg(2, "bands", 7, ["Dm7", "Dm7", "Fmaj7", "Am7", "Em7", "Dm7", "Fmaj7"], [0.6] + [1.0] * 6)
def s02(s):
    s.hold("Dm7"); s.begin()
    s.cue(500, "BANDS: A CHORD HELD ON THE PADS")
    s.cue(2500, "BANDS: THE VOICE SHAPES THE CHORD")
    s.chord_changes(s.sg["chords"])
    for b in range(2, 7):
        s.cue(b * BAR + 300, "BANDS: A NEW PAD, A NEW CHORD")


@seg(3, "voicings", 4, loop_chords(4))
def s03(s):
    s.setup("tap params", "tap down", "tap f5")
    s.hold("Dm7"); s.begin()
    s.cue(400, "VOICING: SMOOTH")
    s.chord_changes(s.sg["chords"])
    for b, name in enumerate(["WIDE", "OPEN", "CLOSE"], 1):
        s.at(b * BAR + 100)
        s.turn(-1)
        s.cue(b * BAR + 400, f"VOICING: {name}")


@seg(4, "shape", 4, loop_chords(4))
def s04(s):
    s.setup("tap params")
    s.hold("Dm7"); s.begin()
    s.cue(300, "SHAPE: SAW")
    s.chord_changes(s.sg["chords"])
    marks = {10: "SHAPE: SAW → PULSE", 20: "SHAPE: PULSE", 30: "SHAPE: PULSE → NARROW", 40: "SHAPE: NARROW"}
    for k in range(40):
        s.at(500 + 150 * k)
        s.turn(1)
        if k + 1 in marks:
            s.cue(500 + 150 * k + 100, marks[k + 1])
    s.ramp(7000, 7900, 40, "turn -1")
    s.cue(7950, "SHAPE: BACK TO SAW")


@seg(5, "spread", 4, loop_chords(4))
def s05(s):
    s.setup("tap params", "tap f2")
    s.hold("Dm7"); s.begin()
    s.cue(300, "SPREAD: 0")
    s.chord_changes(s.sg["chords"])
    marks = {10: "SPREAD: 0 → 30", 20: "SPREAD: 0 → 30", 30: "SPREAD: 30"}
    for k in range(30):
        s.at(500 + 150 * k)
        s.turn(1)
        if k + 1 in marks:
            s.cue(500 + 150 * k + 100, marks[k + 1])
    s.cue(7000, "SPREAD: 30")


@seg(6, "sub", 4, loop_chords(4))
def s06(s):
    s.setup("tap params", "tap f3")
    s.hold("Dm7"); s.begin()
    s.cue(300, "SUB: OFF")
    s.chord_changes(s.sg["chords"])
    marks = {17: "SUB: AN OCTAVE DOWN, COMING IN", 33: "SUB: AN OCTAVE DOWN, COMING IN", 50: "SUB: FULL"}
    for k in range(50):
        s.at(500 + 120 * k)
        s.turn(1)
        if k + 1 in marks:
            s.cue(500 + 120 * k + 100, marks[k + 1])
    s.cue(7200, "SUB: FULL")


@seg(7, "tilt", 4, loop_chords(4))
def s07(s):
    s.setup("tap params", "tap f4")
    s.hold("Dm7"); s.begin()
    s.cue(300, "TILT: FLAT")
    s.chord_changes(s.sg["chords"])
    s.ramp(500, 2500, 24, "turn 1")
    s.cue(2600, "TILT: UP")
    s.ramp(3000, 5500, 48, "turn -1")
    s.cue(4250, "TILT: UP → DOWN")
    s.cue(5600, "TILT: DOWN")
    s.ramp(6000, 7800, 24, "turn 1")
    s.cue(7900, "TILT: BACK TO FLAT")


@seg(8, "formant", 4, loop_chords(4))
def s08(s):
    s.setup("press shift", "tap f5", "release shift")
    s.hold("Dm7"); s.begin()
    s.cue(300, "FORMANT: 0 ST")
    s.chord_changes(s.sg["chords"])
    s.ramp(500, 2000, 12, "turn -1")
    s.cue(2100, "FORMANT: DOWN TO -12 ST")
    s.ramp(2500, 6000, 24, "turn 1")
    s.cue(4250, "FORMANT: UP")
    s.cue(6100, "FORMANT: UP TO +12 ST")
    s.ramp(6500, 7800, 12, "turn -1")
    s.cue(7900, "FORMANT: BACK TO 0")


@seg(9, "freeze", 4, loop_chords(4), voice={"special": "hold_o"})
def s09(s):
    s.hold("Dm7"); s.begin()
    s.cue(400, "FREEZE: OFF")
    s.chord_changes(s.sg["chords"])
    s.at(1500)
    s.shift_focus("f1")
    s.cue(1800, "FREEZE: ON, THE SPECTRUM IS HELD")
    s.cue(3500, "FREEZE: THE VOICE MOVES, THE SOUND STAYS")
    s.cue(5000, "FREEZE: THE VOICE MOVES, THE SOUND STAYS")
    s.at(6000)
    s.shift_focus("f1")
    s.cue(6300, "FREEZE: OFF")


@seg(10, "talk", 4, loop_chords(4))
def s10(s):
    s.hold("Dm7"); s.begin()
    s.cue(400, "BANDS: FOR COMPARISON")
    s.chord_changes(s.sg["chords"])
    s.at(BAR - 1)
    s.tap("f1"); s.tap("f1")
    s.cue(BAR + 400, "TALK: THE VOICE'S ENVELOPE ON THE CARRIER")
    s.cue(4400, "TALK: THE VOICE'S ENVELOPE ON THE CARRIER")
    s.cue(6400, "TALK: THE VOICE'S ENVELOPE ON THE CARRIER")


@seg(11, "tune", 5, loop_chords(5), voice={"drift": True})
def s11(s):
    s.setup("tap f1", "tap f1", "tap f1")
    s.begin()
    s.cue(400, "TUNE: THE SUNG VOICE PULLED TO THE SCALE")
    s.cue(2400, "TUNE: THE SUNG VOICE PULLED TO THE SCALE")
    s.at(4000 - 1)
    s.tap("f4"); s.turn(10)
    s.cue(4400, "TUNE: SPEED UP")
    s.at(6000 - 1)
    s.tap("f2"); s.turn(3)
    s.cue(6400, "TUNE: KEY CHANGE")
    s.cue(8400, "TUNE: KEY CHANGE")


@seg(12, "robot", 4, loop_chords(4), voice={"gain": "alt_half"})
def s12(s):
    s.setup("tap f1", "tap f1", "tap f1", "tap f1")
    s.hold("Dm7"); s.begin()
    s.chord_changes(s.sg["chords"])
    for k in range(8):
        s.cue(400 + 1000 * k, "ROBOT: LOUD VOICE IN" if k % 2 == 0 else "ROBOT: QUIET VOICE IN")


@seg(13, "arp_glide_gate", 6, loop_chords(6))
def s13(s):
    s.hold("Dm7"); s.begin()
    s.chord_changes(s.sg["chords"])
    s.at(200)
    s.shift_focus("f2"); s.turn(2)
    s.cue(600, "GATE: 1/8")
    s.cue(2600, "GATE: 1/8")
    s.at(BAR * 2 - 120)
    s.shift_focus("f2"); s.turn(-2)
    s.shift_focus("f3"); s.turn(1)
    s.cue(4400, "ARP: UP")
    s.cue(6400, "ARP: UP")
    s.at(BAR * 4 - 120)
    s.shift_focus("f3"); s.turn(-1)
    s.shift_focus("f4"); s.turn(10)
    s.cue(8400, "GLIDE: THE CHORDS SLIDE")
    s.cue(10400, "GLIDE: THE CHORDS SLIDE")


SLICE_ORDER = [0, 1, 2, 3, 4, 5, 6, 7,
               0, 0, 2, 2, 4, 5, 7, 7,
               3, 3, 3, 3, 1, 1, 6, 6,
               7, 6, 5, 4, 3, 2, 1, 0]


@seg(14, "samples", 7, ["Dm7", "Fmaj7"] + ["Dm7"] * 5)
def s14(s):
    s.setup("tap sampleplay", f"paddown {PAD['Dm7']}", "tap shift", f"padup {PAD['Dm7']}",
            "tap samplerec", "tap f3", "tap f4", "tap f4", "tap f4")
    s.begin()
    s.tap("play"); s.tap("rec")
    for t, c in ((300, "SAMPLE: ARMED, WAITING FOR THE BAR"), (1200, "SAMPLE: ARMED, WAITING FOR THE BAR"),
                 (2300, "SAMPLE: RECORDING THE VOICE, ONE BAR"), (3300, "SAMPLE: THE TAKE STOPS AT THE BAR"),
                 (4100, "SAMPLE: A TAKE OF TWO SECONDS")):
        s.cue(t, c)
    s.at(4200)
    s.tap("sampleplay")
    s.tap("f2"); s.tap("f2"); s.tap("f2")
    s.cue(5000, "SLICES: THE TAKE CUT IN EIGHT")
    for i, p in enumerate(SLICE_ORDER):
        s.at(6000 + 250 * i)
        s.cmd(f"paddown {p}")
        if i in (1, 9, 17, 25):
            s.cue(6000 + 250 * i + 100, "SLICES: PADS PLAY THE CUTS")
        s.at(6000 + 250 * i + 190)
        s.cmd(f"padup {p}")


NOTE_PAD = {"Dm7": 2, "Fmaj7": 5, "Am7": 9, "Em7": 4}


@seg(15, "pattern_song", 9, ["Dm7"] * 9)
def s15(s):
    s.setup("tap pattern")
    s.begin()
    s.cue(200, "PATTERN: AN EMPTY LANE")
    seq = [c for c in LOOP for _ in range(4)]
    for i, c in enumerate(seq):
        s.at(300 + 95 * i)
        s.cmd(f"paddown {NOTE_PAD[c]}")
        s.at(300 + 95 * i + 45)
        s.cmd(f"padup {NOTE_PAD[c]}")
        if i == 7:
            s.cue(300 + 95 * i + 60, "PATTERN: NOTE LANE, ONE CHORD PER PAD")
    s.cue(1900, "PATTERN: SIXTEEN STEPS WRITTEN")
    s.at(2100)
    s.tap("instr")
    for p in (1, 2, 1):
        s.pad_hit(p)
    s.cue(2700, "PATTERN: CARRIER LANE")
    s.tap("vol")
    for p in (23, 15, 23):
        s.pad_hit(p)
    s.cue(3300, "PATTERN: LEVEL LANE")
    s.tap("note")
    s.tap("f7")
    s.at(4000 - 1)
    s.tap("play")
    s.cue(4500, "PATTERN: PLAYING")
    s.cue(6500, "PATTERN: PLAYING")
    s.at(8000 - 1)
    s.tap("play")
    s.tap("song")
    s.at(8400)
    s.tap("insert"); s.tap("insert"); s.tap("insert")
    s.cue(8800, "SONG: A CHAIN OF PATTERNS")
    s.tap("f3")
    s.tap("up"); s.tap("up"); s.tap("up")
    s.at(10000 - 1)
    s.tap("play")
    s.cue(10400, "SONG: THE CHAIN PLAYS")
    s.cue(12400, "SONG: THE CHAIN PLAYS")
    s.cue(14400, "SONG: THE CHAIN PLAYS")
    s.cue(16400, "SONG: THE CHAIN PLAYS")


@seg(16, "closing", 3, ["Dm7", "Fmaj7", "Am7"], [0.7, 0.4, 0.0])
def s16(s):
    s.hold("Dm7"); s.begin()
    s.chord_changes(s.sg["chords"])
    s.cue(0, "", card="closing")


def main():
    work, scen = sys.argv[1], sys.argv[2]
    os.makedirs(work, exist_ok=True)
    start = 0
    out = []
    for sg in SEGS:
        s = Scn(sg)
        sg["fn"](s)
        lines, clock = s.emit(sg["bars"] * BAR + TAIL)
        head = [f"# Video segment {sg['n']:02d} ({sg['name']}), generated by tools/video/plan.py: do not edit by hand.",
                "# Run from the video work dir (make_video.py does): the modulator is mod/NN.wav, written by tools/video/formant.py.",
                "# A host check of the screens and of the sound's changes, not a measurement of anything on the unit."]
        path = os.path.join(scen, f"video_{sg['n']:02d}_{sg['name']}.txt")
        with open(path, "w") as f:
            f.write("\n".join(head + lines) + "\n")
        out.append({"n": sg["n"], "name": sg["name"], "scenario": os.path.basename(path), "start_bar": start,
                    "bars": sg["bars"], "chords": sg["chords"], "intensity": sg["intensity"], "voice": sg["voice"],
                    "cues": s.cues, "sim_end_ms": clock})
        start += sg["bars"]
    with open(os.path.join(work, "timeline.json"), "w") as f:
        json.dump({"bar_ms": BAR, "tail_ms": TAIL, "total_bars": start, "segments": out}, f, indent=1)
    print(f"{len(out)} segments, {start} bars, {start * BAR / 1000:.0f} s")


if __name__ == "__main__":
    main()
