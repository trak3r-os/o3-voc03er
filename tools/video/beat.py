#!/usr/bin/env python3
"""The audio of the video: the simulator's vocoder WAVs joined with short crossfades, and a simple four-on-the-floor beat under them (numpy only).

The beat is synthesised here (kick, off-beat hats, clap on 2 and 4, a ducked bass on the root of each chord): it is an original, plain
accompaniment at 120 BPM. Each bar of a segment has an intensity (timeline.json) that switches layers on: kick from 0.25, hats from 0.5,
bass from 0.6, clap from 0.7. The beat is mixed about 3 dB under the vocoder, then the master is normalised to about -1 dBFS.

usage: beat.py <work dir>      reads <work>/timeline.json and <work>/sim/NN/segNN.wav, writes <work>/master.wav (44.1 kHz stereo, 16 bit)
"""
import json
import os
import sys
import wave

import numpy as np

SR = 44100
BEAT = 0.5
BAR = 4 * BEAT
ROOT = {"Dm7": 38, "Fmaj7": 41, "Am7": 45, "Em7": 40}
VOC_TARGET_DB = -18.0       # per segment, active parts, before the master
BEAT_BELOW_DB = 3.0         # the beat's RMS under the vocoder's


def read_wav(path):
    with wave.open(path) as w:
        n, ch = w.getnframes(), w.getnchannels()
        y = np.frombuffer(w.readframes(n), "<i2").reshape(-1, ch).astype(np.float64) / 32768.0
    return y if ch == 2 else np.repeat(y[:, :1], 2, axis=1)


def write_wav(path, y):
    pcm = (np.clip(y, -1, 1) * 32767.0).round().astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def active_rms(y, floor_db=-50.0):
    m = y.mean(axis=1)
    n = int(0.05 * SR)
    k = len(m) // n
    fr = m[:k * n].reshape(k, n)
    p = np.mean(fr ** 2, axis=1)
    on = p > 10 ** (floor_db / 10)
    return float(np.sqrt(np.mean(p[on]))) if on.any() else 0.0


def bandpass_noise(rng, n, lo, hi):
    x = rng.standard_normal(n)
    s = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / SR)
    s[(f < lo) | (f > hi)] = 0
    y = np.fft.irfft(s, n)
    return y / np.max(np.abs(y))


def make_kick():
    n = int(0.36 * SR)
    t = np.arange(n) / SR
    ph = 2 * np.pi * (48.0 * t + 120.0 * 0.03 * (1 - np.exp(-t / 0.03)))
    y = np.sin(ph) * np.exp(-t / 0.13) * np.minimum(1.0, t / 0.002)
    y += 0.25 * np.sin(2 * np.pi * 900 * t) * np.exp(-t / 0.004)
    return y


def make_hat(rng):
    n = int(0.09 * SR)
    t = np.arange(n) / SR
    return bandpass_noise(rng, n, 6500, 16000) * np.exp(-t / 0.022) * 0.5


def make_clap(rng):
    n = int(0.28 * SR)
    t = np.arange(n) / SR
    env = np.zeros(n)
    for o in (0.0, 0.011, 0.022):
        m = t >= o
        env[m] += np.exp(-(t[m] - o) / 0.006)
    m = t >= 0.03
    env[m] += 0.7 * np.exp(-(t[m] - 0.03) / 0.07)
    return bandpass_noise(rng, n, 900, 4200) * env * 0.5


def make_bass(midi):
    n = int(0.24 * SR)
    t = np.arange(n) / SR
    f = 440.0 * 2 ** ((midi - 69) / 12.0)
    y = np.sin(2 * np.pi * f * t) + 0.45 * np.sin(4 * np.pi * f * t) + 0.18 * np.sin(6 * np.pi * f * t)
    return y * np.exp(-t / 0.11) * np.minimum(1.0, t / 0.004) * np.minimum(1.0, (n / SR - t) / 0.02)


def put(buf, x, t):
    i = int(round(t * SR))
    if i < 0 or i >= len(buf):
        return
    j = min(len(buf), i + len(x))
    buf[i:j] += x[:j - i]


def main():
    work = sys.argv[1]
    tl = json.load(open(os.path.join(work, "timeline.json")))
    segs = tl["segments"]
    nbars = tl["total_bars"]
    total = nbars * BAR
    n = int((total + 0.5) * SR)
    rng = np.random.default_rng(7)

    # the vocoder: crop or pad each segment, one gain per segment, crossfade the 80 ms tails
    voc = np.zeros((n, 2))
    tail = int(tl["tail_ms"] / 1000 * SR)
    for i, sg in enumerate(segs):
        nn = f"{sg['n']:02d}"
        y = read_wav(os.path.join(work, "sim", nn, f"seg{nn}.wav"))
        want = int(sg["bars"] * BAR * SR) + tail
        y = y[:want] if len(y) >= want else np.vstack([y, np.zeros((want - len(y), 2))])
        r = active_rms(y)
        g = 10 ** (VOC_TARGET_DB / 20) / r if r > 0 else 1.0
        g = float(np.clip(g, 0.5, 6.0))
        y = y * g
        fi = tail if i else int(0.005 * SR)
        k = np.arange(fi) / fi
        y[:fi] *= np.sin(k * np.pi / 2)[:, None]
        if i < len(segs) - 1:
            k = np.arange(tail) / tail
            y[-tail:] *= np.cos(k * np.pi / 2)[:, None]
        else:
            fo = int(2.0 * SR)
            y[-fo:] *= (0.5 + 0.5 * np.cos(np.arange(fo) / fo * np.pi))[:, None]
        a = int(sg["start_bar"] * BAR * SR)
        voc[a:a + len(y)] += y
        print(f"segment {nn}: active rms {20 * np.log10(max(r, 1e-9)):.1f} dB, gain {20 * np.log10(g):+.1f} dB")

    # the beat
    kick, clap, hat = make_kick(), make_clap(rng), make_hat(rng)
    drums = np.zeros(n)
    bass = np.zeros(n)
    duck = np.ones(n)
    tt = np.arange(n) / SR
    for sg in segs:
        for b in range(sg["bars"]):
            x = sg["intensity"][b]
            t0 = (sg["start_bar"] + b) * BAR
            chord = sg["chords"][b] if b < len(sg["chords"]) else sg["chords"][-1]
            bs = make_bass(ROOT[chord])
            for q in range(4):
                tb = t0 + q * BEAT
                if x >= 0.25:
                    put(drums, kick, tb)
                    i0 = int(round(tb * SR))
                    seg_t = np.arange(int(0.25 * SR)) / SR
                    duck[i0:i0 + len(seg_t)] = np.minimum(duck[i0:i0 + len(seg_t)], (1.0 - 0.7 * np.exp(-seg_t / 0.09))[:len(duck) - i0])
                if x >= 0.5:
                    put(drums, hat, tb + BEAT / 2)
                if x >= 0.6:
                    put(bass, bs, tb + BEAT / 2)
                if x >= 0.7 and q in (1, 3):
                    put(drums, clap, tb)
    beat = drums + 0.8 * bass * duck

    # mix: the beat 3 dB under the vocoder, both measured where they sound
    vr = active_rms(voc)
    br = active_rms(np.stack([beat, beat], axis=1))
    gb = vr / br * 10 ** (-BEAT_BELOW_DB / 20)
    y = voc + np.stack([beat, beat], axis=1) * gb
    y = y[:int(total * SR)]
    y *= 10 ** (-16.0 / 20) / active_rms(y)
    y = np.tanh(y)
    y *= 10 ** (-1.0 / 20) / np.max(np.abs(y))
    write_wav(os.path.join(work, "master.wav"), y)
    print(f"master: {len(y) / SR:.3f} s, vocoder {20 * np.log10(vr):.1f} dB, beat {20 * np.log10(br * gb):.1f} dB, peak {20 * np.log10(np.max(np.abs(y))):.2f} dBFS")


if __name__ == "__main__":
    main()
