#!/usr/bin/env python3
# The WAVs sample_load.txt loads (SAMPLE LOAD, U8a), written into the folder given as the one argument (the card's Apps/o3_voc03er/):
#   a_mono 1.5 s mono, b_stereo 1.5 s stereo (the same length), c_48k at 48 kHz, d_8bit at 8 bit, e_long20 20 s of stereo, f_broken with its data cut short.
# Run with `python3 -I`. Nothing is written to the repository: the card is a scratch directory.
import math
import os
import struct
import sys


def wav(path, frames, ch, rate, bits, gen, cut=0):
    b = bytearray()
    for i in range(frames):
        for c in range(ch):
            v = gen(i, c)
            if bits == 16:
                b += struct.pack('<h', max(-32768, min(32767, int(v * 32767))))
            else:
                b += struct.pack('B', max(0, min(255, int(v * 127) + 128)))
    fmt = struct.pack('<HHIIHH', 1, ch, rate, rate * ch * bits // 8, ch * bits // 8, bits)
    data = bytes(b)
    out = b'RIFF' + struct.pack('<I', 4 + 8 + 16 + 8 + len(data)) + b'WAVE' + b'fmt ' + struct.pack('<I', 16) + fmt + b'data' + struct.pack('<I', len(data)) + data
    if cut:
        out = out[:-cut]
    with open(path, 'wb') as f:
        f.write(out)


def sine(freq, amp):
    return lambda i, c: amp * math.sin(2 * math.pi * freq * i / 44100)


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: sample_load_wavs.py <folder>')
    d = sys.argv[1]
    os.makedirs(d, exist_ok=True)
    wav(os.path.join(d, 'a_mono.wav'), 66150, 1, 44100, 16, sine(440, 0.8))
    wav(os.path.join(d, 'b_stereo.wav'), 66150, 2, 44100, 16,
        lambda i, c: (0.8 * math.sin(2 * math.pi * 220 * i / 44100) if c == 0 else 0.4 * math.sin(2 * math.pi * 330 * i / 44100)) * (1 - i / 66150))
    wav(os.path.join(d, 'c_48k.wav'), 48000, 1, 48000, 16, sine(440, 0.5))
    wav(os.path.join(d, 'd_8bit.wav'), 44100, 1, 44100, 8, sine(440, 0.5))
    wav(os.path.join(d, 'e_long20.wav'), 20 * 44100, 2, 44100, 16,
        lambda i, c: 0.7 * math.sin(2 * math.pi * (110 + c * 55) * i / 44100) * (0.5 + 0.5 * math.sin(2 * math.pi * i / 44100)))
    wav(os.path.join(d, 'f_broken.wav'), 44100, 1, 44100, 16, sine(440, 0.5), cut=30000)


main()
