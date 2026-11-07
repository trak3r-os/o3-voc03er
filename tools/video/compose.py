#!/usr/bin/env python3
"""The picture of the video and the three mp4 files (PIL and ffmpeg).

usage: compose.py <work dir> <out dir> frames|encode|all

frames: one 1920x1080 PNG per cue (the simulator's screenshot, scaled with nearest neighbour, centred on black, with a caption strip in the
skin's colours) and the two title cards, in <work>/frames/, with <work>/frames.ffconcat.
encode: <out>/o3-voc03er-v0.1.0_full.mp4 (H.264 CRF 18, AAC 192k), _19MB.mp4 and _9MB.mp4 (two-pass, under the size), with sizes and lengths.
"""
import json
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

# the OR NOIR skin (app/src/skin/ornoir.cpp)
CHROME = (40, 34, 24)
CHROME_TEXT = (246, 240, 226)
CHROME_DIM = (192, 176, 136)
PANE = (0, 0, 0)
TEXT = (248, 244, 232)
GOLD = (255, 190, 36)       # the voice's ink
BLUE = (196, 218, 238)      # the carrier's ink
BAND = (228, 186, 92)
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
W, H = 1920, 1080
STRIP = 96
FPS = 30
NAME = "o3-voc03er-v0.1.0"


def font(px):
    return ImageFont.truetype(FONT, px)


def text_w(d, s, f):
    return d.textlength(s, font=f)


def centred(d, y, s, f, fill, width=W):
    d.text(((width - text_w(d, s, f)) / 2, y), s, font=f, fill=fill)


def strip(d, caption):
    y0 = H - STRIP
    d.rectangle([0, y0, W, H], fill=CHROME)
    d.rectangle([0, y0, W, y0 + 3], fill=BAND)
    f = font(46)
    head, sep, rest = caption.partition(":")
    x = 56
    cy = y0 + 3 + (STRIP - 3 - 46) / 2 - 6
    d.text((x, cy), head + sep, font=f, fill=GOLD)
    d.text((x + text_w(d, head + sep, f), cy), rest, font=f, fill=CHROME_TEXT)
    g = font(26)
    s = "O3-VOC03ER"
    d.text((W - 40 - text_w(d, s, g), y0 + 3 + (STRIP - 3 - 26) / 2 - 4), s, font=g, fill=CHROME_DIM)


def screen_frame(png, caption, path):
    im = Image.open(png).convert("RGB")
    hh = H - STRIP - 12
    ww = round(im.width * hh / im.height)
    im = im.resize((ww, hh), Image.NEAREST)
    fr = Image.new("RGB", (W, H), PANE)
    fr.paste(im, ((W - ww) // 2, 6))
    strip(ImageDraw.Draw(fr), caption)
    fr.save(path)


def card_frame(kind, path):
    fr = Image.new("RGB", (W, H), PANE)
    d = ImageDraw.Draw(fr)
    if kind == "title":
        centred(d, 330, "O3-VOC03ER", font(170), GOLD)
        d.rectangle([W / 2 - 520, 560, W / 2 + 520, 566], fill=BAND)
        centred(d, 610, "a vocoder and talkbox for the Tracker".upper(), font(46), CHROME_TEXT)
    else:
        centred(d, 250, "O3-VOC03ER", font(120), GOLD)
        d.rectangle([W / 2 - 520, 420, W / 2 + 520, 426], fill=BAND)
        centred(d, 470, "github.com/trak3r-os/o3-voc03er", font(50), CHROME_TEXT)
        centred(d, 560, "v0.1.0", font(50), BLUE)
        centred(d, 670, "unofficial: not affiliated with Polyend", font(38), CHROME_DIM)
        centred(d, 960, "needs more testing", font(24), CHROME_DIM)
    fr.save(path)


def frames_stage(work):
    tl = json.load(open(os.path.join(work, "timeline.json")))
    fd = os.path.join(work, "frames")
    os.makedirs(fd, exist_ok=True)
    seq = []
    for sg in tl["segments"]:
        nn = f"{sg['n']:02d}"
        t0 = sg["start_bar"] * tl["bar_ms"] / 1000.0
        for c in sg["cues"]:
            p = os.path.join(fd, f"{nn}_{c['snap'] or c['card']}.png")
            if c["card"]:
                card_frame(c["card"], p)
            else:
                screen_frame(os.path.join(work, "sim", nn, c["snap"] + ".png"), c["caption"], p)
            seq.append((t0 + c["t"] / 1000.0, p))
    total = tl["total_bars"] * tl["bar_ms"] / 1000.0
    seq.sort()
    with open(os.path.join(work, "frames.ffconcat"), "w") as f:
        f.write("ffconcat version 1.0\n")
        for i, (t, p) in enumerate(seq):
            end = seq[i + 1][0] if i + 1 < len(seq) else total
            f.write(f"file '{p}'\nduration {end - t:.6f}\n")
        f.write(f"file '{seq[-1][1]}'\n")
    print(f"{len(seq)} frames, {total:.1f} s")


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(["nice", "-n", "10"] + cmd, check=True)


def duration(path):
    r = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path], capture_output=True, text=True, check=True)
    return float(r.stdout.strip())


def encode_stage(work, out):
    os.makedirs(out, exist_ok=True)
    concat = os.path.join(work, "frames.ffconcat")
    master = os.path.join(work, "master.wav")
    total = duration(master)
    base = ["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", concat]
    fade = f"fade=t=in:st=0:d=0.5,fade=t=out:st={total - 1.2:.3f}:d=1.2"

    def vf(w, h):
        s = "" if w == W else f"scale={w}:{h}:flags=lanczos,"
        return s + fade + ",format=yuv420p"

    full = os.path.join(out, f"{NAME}_full.mp4")
    run(base + ["-i", master, "-r", str(FPS), "-vf", vf(W, H), "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-c:a", "aac", "-b:a", "192k",
                "-movflags", "+faststart", "-shortest", full])
    results = [(full, None)]
    for label, limit, w, h, abr in (("19MB", 19e6, W, H, 128), ("9MB", 9e6, 1280, 720, 96)):
        path = os.path.join(out, f"{NAME}_{label}.mp4")
        target = limit * 0.95
        for attempt in range(5):
            vbr = int(target * 8 / total / 1000 - abr)
            log = os.path.join(work, f"pass_{label}")
            common = ["-r", str(FPS), "-vf", vf(w, h), "-c:v", "libx264", "-preset", "slow", "-b:v", f"{vbr}k", "-passlogfile", log]
            run(base + common + ["-pass", "1", "-an", "-f", "null", os.devnull])
            run(base + ["-i", master] + common + ["-pass", "2", "-c:a", "aac", "-b:a", f"{abr}k", "-movflags", "+faststart", "-shortest", path])
            size = os.stat(path).st_size
            print(f"{label}: attempt {attempt}: video {vbr} kb/s, {size} bytes")
            if size < limit:
                break
            target *= 0.92
        else:
            raise SystemExit(f"{label} stays over {limit:.0f} bytes")
        results.append((path, limit))
    for path, limit in results:
        print(f"{os.path.basename(path)}: {os.stat(path).st_size} bytes, {duration(path):.2f} s" + (f" (limit {limit:.0f})" if limit else ""))


def main():
    work, out, stage = sys.argv[1], sys.argv[2], sys.argv[3]
    if stage in ("frames", "all"):
        frames_stage(work)
    if stage in ("encode", "all"):
        encode_stage(work, out)


if __name__ == "__main__":
    main()
