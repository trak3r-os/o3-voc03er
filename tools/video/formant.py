#!/usr/bin/env python3
"""A small formant synthesizer for the modulator voice (numpy only): "o three vo co der" and "ro bot", sung on pitches.

A pulse train (the voice source) and noise go through a time-varying filter of three moving formants and a fixed fourth, built frame by frame in
the spectrum (STFT, Hann, 75 % overlap). It is made to give the vocoder something that moves like speech, not to be intelligible.
The melody is an original four-bar phrase, one bar per chord of the loop (Dm7, Fmaj7, Am7, Em7).

usage: formant.py <work dir>      reads <work dir>/timeline.json, writes <work dir>/mod/NN.wav (44.1 kHz stereo, 16 bit)
"""
import json
import os
import sys
import wave

import numpy as np

SR = 44100
N = 1024
HOP = 256
BEAT = 0.5          # seconds at 120 BPM
BAR = 4 * BEAT

# nucleus targets: (F1, F2, F3)
VOW = {"o": (460, 850, 2650), "i": (280, 2250, 3000), "e": (530, 1840, 2480), "a": (730, 1090, 2440)}
R = (310, 1060, 1380)
STOP = {"k": (2500, 0.7, 0.0), "d": (3200, 0.55, 0.15), "b": (900, 0.5, 0.2), "t": (4500, 0.65, 0.0)}
# syllable: onset phonemes, nucleus, coda phonemes
SYL = {
    "o": ([], "o", []),
    "three": ([("th", 0.07), ("r", 0.08)], "i", []),
    "vo": ([("v", 0.07)], "o", []),
    "co": ([("k", 0.07)], "o", []),
    "der": ([("d", 0.06)], "e", [("r", 0.13)]),
    "ro": ([("r", 0.09)], "o", []),
    "bot": ([("b", 0.06)], "o", [("t", 0.08)]),
}
# the sung phrase, one bar per chord: (syllable, MIDI note, beats)
PHRASE = [
    [("o", 57, .5), ("three", 62, .5), ("vo", 65, .5), ("co", 69, .5), ("der", 67, 2)],
    [("o", 65, .5), ("three", 69, .5), ("vo", 72, .5), ("co", 69, .5), ("der", 65, 2)],
    [("ro", 64, 1), ("bot", 69, 1), ("ro", 72, 1), ("bot", 69, 1)],
    [("ro", 59, .5), ("bot", 64, .5), ("ro", 67, .5), ("bot", 71, .5), ("o", 67, 2)],
]
PHRASE_OF = {"Dm7": 0, "Fmaj7": 1, "Am7": 2, "Em7": 3}


def hz(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)


class Tracks:
    def __init__(self):
        self.k = {n: ([], []) for n in ("av", "an", "f1", "f2", "f3", "fn", "f0")}

    def add(self, name, t, v):
        self.k[name][0].append(t)
        self.k[name][1].append(v)

    def ev(self, a, b, av, an, f=None, fn=None):
        m = min(0.015, (b - a) / 2.5)
        for t in (a + m, b - m):
            self.add("av", t, av)
            self.add("an", t, an)
            if f is not None:
                for nm, v in zip(("f1", "f2", "f3"), f):
                    self.add(nm, t, v)
            if fn is not None:
                self.add("fn", t, fn)

    def at(self, name, t):
        kt, kv = self.k[name]
        return np.interp(t, kt, kv)


def sing(tr, notes):
    """notes: (t0, syllable, midi, dur). Fills the tracks."""
    for t0, syl, midi, dur in notes:
        on, nuc, cod = SYL[syl]
        t1 = t0 + dur * 0.93
        a = t0
        for ph, d in on:
            a = _phoneme(tr, ph, a, d)
        z = t1 - sum(d for _, d in cod)
        tr.ev(a, max(z, a + 0.05), 1.0, 0.03, VOW[nuc], 3000)
        a = max(z, a + 0.05)
        for ph, d in cod:
            a = _phoneme(tr, ph, a, d)
        tr.ev(t1, t0 + dur, 0.0, 0.0)
        tr.add("f0", t0 + 0.03, hz(midi))
        tr.add("f0", t0 + dur, hz(midi))


def _phoneme(tr, ph, a, d):
    b = a + d
    if ph == "r":
        tr.ev(a, b, 0.8, 0.02, R, 3000)
    elif ph == "th":
        tr.ev(a, b, 0.0, 0.5, None, 6500)
    elif ph == "v":
        tr.ev(a, b, 0.5, 0.25, (250, 1100, 2400), 5000)
    else:
        fn, burst, avc = STOP[ph]
        c = d * 0.62
        tr.ev(a, a + c, avc, 0.0)
        tr.ev(a + c, b, avc * 0.5, burst, None, fn)
    return b


def render(notes, length, detune=None, gain=None, seed=1):
    tr = Tracks()
    sing(tr, notes)
    n = int(length * SR)
    t = np.arange(n) / SR
    f0 = tr.at("f0", t)
    cents = np.zeros(n)
    # vibrato, ramped in after each note starts
    cents += 14.0 * np.sin(2 * np.pi * 5.6 * t)
    if detune:
        cents += detune["offset"] + detune["depth"] * (np.sin(2 * np.pi * 0.37 * t) + 0.6 * np.sin(2 * np.pi * 0.91 * t + 1.0))
    f0 = f0 * 2.0 ** (cents / 1200.0)
    phase = np.cumsum(f0 / SR)
    src = 2.0 * (phase % 1.0) - 1.0
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal(n).astype(np.float64)
    voiced = src * tr.at("av", t)
    noisy = noise * tr.at("an", t)
    win = np.hanning(N + 1)[:-1]
    nf = (n - N) // HOP + 1
    centre = (np.arange(nf) * HOP + N / 2) / SR
    f = np.fft.rfftfreq(N, 1.0 / SR)[None, :]
    fm = [tr.at(nm, centre)[:, None] for nm in ("f1", "f2", "f3")]
    fn = tr.at("fn", centre)[:, None]
    bw = (90.0, 110.0, 160.0)
    h = np.ones((nf, f.shape[1]))
    for fi, b in zip(fm, bw):
        q = fi / b
        h = h * 1.0 / np.sqrt((1 - (f / fi) ** 2) ** 2 + (f / (fi * q)) ** 2)
    h = h / np.sqrt((1 + (f / 3500.0) ** 2) ** 2 + (f / 3500.0 * 18.0) ** 2) * 1.0
    h = h / (1.0 + f / 300.0)
    h = h / np.sqrt(np.mean(h ** 2 / (1.0 + f / 300.0), axis=1, keepdims=True))
    hn = np.exp(-0.5 * ((np.log(f + 1.0) - np.log(fn)) / 0.45) ** 2)
    vw = np.lib.stride_tricks.sliding_window_view(voiced, N)[::HOP][:nf] * win
    nw = np.lib.stride_tricks.sliding_window_view(noisy, N)[::HOP][:nf] * win
    y_frames = np.fft.irfft(np.fft.rfft(vw, axis=1) * h * 0.04 + np.fft.rfft(nw, axis=1) * hn * 0.35, N, axis=1)
    y = np.zeros(n)
    for i in range(nf):
        y[i * HOP:i * HOP + N] += y_frames[i]
    y /= 2.0
    if gain == "alt_half":
        g = np.where(((t / (BAR / 2)).astype(int)) % 2 == 0, 1.0, 0.1)
        k = np.ones(int(0.02 * SR)) / int(0.02 * SR)
        y *= np.convolve(g, k, mode="same")
    return y


def notes_for(chords, extra_bars, special=None):
    notes = []
    seq = list(chords)
    last = PHRASE_OF[seq[-1]]
    seq += [list(PHRASE_OF)[(last + 1 + i) % 4] for i in range(extra_bars)]
    for b, c in enumerate(seq):
        t = b * BAR
        if b == 0 and special == "hold_o":
            notes.append((t, "o", 62, BAR))
            continue
        for syl, midi, beats in PHRASE[PHRASE_OF[c]]:
            notes.append((t, syl, midi, beats * BEAT))
            t += beats * BEAT
    return notes


def write_wav(path, y):
    y = y / max(1e-9, np.max(np.abs(y))) * 0.5
    pcm = (np.clip(y, -1, 1) * 32767).astype("<i2")
    st = np.repeat(pcm[:, None], 2, axis=1)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(st.tobytes())


def main():
    work = sys.argv[1]
    tl = json.load(open(os.path.join(work, "timeline.json")))
    os.makedirs(os.path.join(work, "mod"), exist_ok=True)
    for sg in tl["segments"]:
        v = sg["voice"]
        length = sg["bars"] * BAR + 0.08 + 1.5
        notes = notes_for(sg["chords"], 1, v.get("special"))
        detune = {"offset": -32.0, "depth": 28.0} if v.get("drift") else None
        y = render(notes, length, detune, v.get("gain"), seed=sg["n"])
        write_wav(os.path.join(work, "mod", f"{sg['n']:02d}.wav"), y)
    print("voices written")


if __name__ == "__main__":
    main()
