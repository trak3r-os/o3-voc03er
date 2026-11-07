#!/usr/bin/env python3
"""Rebuild the presentation video from scratch.

usage: make_video.py [--work DIR] [--out DIR] [--stage plan|voices|sim|mix|frames|encode|all]

Stages: plan (scenarios and timeline.json), voices (modulator WAVs), sim (the simulator, one run per segment, in parallel), mix (beat and
audio), frames and encode (picture and the three mp4 files). Nothing but the scenarios in app/scenarios/ is meant for git.
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))


def run(cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(["nice", "-n", "10"] + cmd, check=True, **kw)


def sim_stage(work):
    import json
    tl = json.load(open(os.path.join(work, "timeline.json")))
    run(["cmake", "-S", ROOT, "-B", os.path.join(ROOT, "build-host")], stdout=subprocess.DEVNULL)
    run(["cmake", "--build", os.path.join(ROOT, "build-host"), "--target", "o3_voc03er_sim", "-j4"], stdout=subprocess.DEVNULL)
    sim = os.path.join(ROOT, "build-host", "app", "o3_voc03er_sim")

    def one(sg):
        nn = f"{sg['n']:02d}"
        out = os.path.join(work, "sim", nn)
        os.makedirs(out, exist_ok=True)
        card = os.path.join(work, "card", nn)
        os.makedirs(card, exist_ok=True)
        r = subprocess.run(["nice", "-n", "10", sim, "--scenario", os.path.join(ROOT, "app", "scenarios", sg["scenario"]),
                            "--card", card, "--out", out], cwd=work, capture_output=True, text=True)
        tail = (r.stdout.strip().splitlines() or ["(no output)"])[-1]
        print(f"segment {nn}: exit {r.returncode}: {tail}", flush=True)
        if r.returncode:
            print(r.stdout[-2000:], r.stderr[-2000:])
        return r.returncode

    with ThreadPoolExecutor(max_workers=8) as ex:
        if any(ex.map(one, tl["segments"])):
            raise SystemExit("a simulator run failed")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", default=os.path.join(os.environ.get("TMPDIR", "/tmp"), "o3-voc03er-video"))
    ap.add_argument("--out", default=os.path.expanduser("~/Videos/o3-voc03er"))
    ap.add_argument("--stage", default="all")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    os.makedirs(work, exist_ok=True)
    st = a.stage
    py = [sys.executable]
    if st in ("plan", "all"):
        run(py + [os.path.join(HERE, "plan.py"), work, os.path.join(ROOT, "app", "scenarios")])
    if st in ("voices", "all"):
        run(py + [os.path.join(HERE, "formant.py"), work])
    if st in ("sim", "all"):
        sim_stage(work)
    if st in ("mix", "all"):
        run(py + [os.path.join(HERE, "beat.py"), work])
    if st in ("frames", "encode", "all"):
        run(py + [os.path.join(HERE, "compose.py"), work, a.out, st])


if __name__ == "__main__":
    main()
