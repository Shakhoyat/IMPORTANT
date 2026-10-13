"""Scan of the assembled picture for visual faults, beat by beat (called by build.py qc).

For every frame the mean change from the frame before is measured on a small copy of the picture (ffmpeg signalstats,
YDIF). Per beat of program footage this gives: the mean change, the share of frames that do not change at all (a shot
that stands still), and how strongly the change repeats every eight frames (the period of the anti-aliasing offsets:
a high value on a fixed camera is a shake). Flags are printed and written to build/film5/qc_report.txt."""
import os
import subprocess

import numpy as np

import dsl


def ydif(path, B):
    r = subprocess.run([B.FF, "-v", "error", "-i", path, "-vf", "scale=320:180,signalstats,metadata=print:key=lavfi.signalstats.YDIF:file=-",
                        "-an", "-f", "null", "-"], capture_output=True, text=True).stdout
    return np.array([float(ln.split("=")[1]) for ln in r.splitlines() if "YDIF" in ln])


def period8(x):
    """Strength of an eight-frame repeat in a series of frame changes (0 = none, 1 = perfect)."""
    if len(x) < 40:
        return 0.0
    x = x - x.mean()
    d = float((x * x).sum())
    if d <= 1e-9:
        return 0.0
    a8 = float((x[8:] * x[:-8]).sum()) / d
    a4 = float((x[4:] * x[:-4]).sum()) / d
    return max(0.0, a8 - max(a4, 0.0))


def run(B):
    total = B.timing()
    pic = os.path.join(dsl.OUT, "picture.mp4")
    d = ydif(pic, B)
    lines = ["picture %.1f s, %d frames measured (script %.1f s)" % (len(d) / B.FPS, len(d), total), ""]
    flags = 0
    for b in dsl.beats():
        if b["kind"] not in ("live", "clip", "split"):
            continue
        i0, n = int(round(b["start"] * B.FPS)), b["frames"]
        x = d[i0 + 2:i0 + n - 2]
        if len(x) < 10:
            continue
        frozen = float((x < 0.02).mean())
        p8 = period8(x)
        note = []
        if frozen > 0.5:
            note.append("stands still %.0f %% of its length" % (100 * frozen))
        if p8 > 0.35 and x.mean() > 1.0:
            note.append("eight-frame repeat %.2f at mean change %.1f (shake?)" % (p8, x.mean()))
        lines.append("%-12s %5s  %-10s mean %5.2f  frozen %3.0f %%  period8 %.2f  %s" % (
            b["id"], B.clock(b["start"]), b.get("take") or str(b.get("src", "split"))[:10], x.mean(), 100 * frozen, p8, "<-- " + "; ".join(note) if note else ""))
        flags += bool(note)
    lines += ["", "%d beats flagged" % flags]
    open(os.path.join(dsl.OUT, "qc_report.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(ln for ln in lines if "<--" in ln or "flagged" in ln or ln.startswith("picture")))
