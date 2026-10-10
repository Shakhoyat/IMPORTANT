"""Animated route map for the presentation film: the intruder rovers drive their exported tracks (130 s of the export's
simulation in a few seconds), with trails, the objectives and the two UAVs; dashed lines from both UAVs to each rover
show the shared track picture. Frames are piped to ffmpeg (1280x720, 30 fps).
Run: ~/.local/bin/uv run -q --with matplotlib --with numpy python tools/report/gen_rover_anim.py [seconds] [out.mp4]"""
import math
import os
import subprocess
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import gen_maps as gm  # noqa: E402  site plan, routes and base()

ROOT = gm.ROOT
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 8.0
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "build", "film", "rover_map.mp4")
FPS = 30
W, H = (int(v) for v in os.environ.get("ANIM_SIZE", "1280x720").split("x"))
R = gm.routes
rc = ["#ff5a4f", "#ffb02e", "#c18cff", "#59a7ff", "#3ad1c8", "#7bd36b"]
tracks = [np.array(t) if t else np.zeros((0, 3)) for t in R["rover_tracks"]]
uavs = [np.array(t) for t in R["uav_tracks"]]
t_end = max(float(t[-1, 0]) for t in tracks if len(t))
plt.rcParams.update({"font.family": "DejaVu Sans"})


def at(track, t):
    """Position at time t, or None when the rover is not alive then (gap in its samples)."""
    if len(track) == 0 or t < track[0, 0] or t > track[-1, 0]:
        return None
    k = np.searchsorted(track[:, 0], t)
    k = min(max(k, 1), len(track) - 1)
    a, b = track[k - 1], track[k]
    if b[0] - a[0] > 1.0:
        return None
    f = (t - a[0]) / max(b[0] - a[0], 1e-6)
    return a[1:] + f * (b[1:] - a[1:])


DPI = 100 * H / 720
fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI, facecolor="#0e141b")
ax = fig.add_axes([0.0, 0.0, 1.0, 1.0])
cmd = [os.path.expanduser("~/.local/bin/ffmpeg"), "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba", "-s",
       "%dx%d" % (W, H), "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-crf", "16", "-pix_fmt", "yuv420p", out]
pipe = subprocess.Popen(cmd, stdin=subprocess.PIPE)
n = int(secs * FPS)
for f in range(n):
    t = t_end * (f + 1) / n
    ax.clear()
    ax.set_facecolor("#0e141b")
    gm.base(ax, "", r=25)
    for coll in ax.collections:
        coll.set_alpha(0.75)
    for j, o in enumerate(R["objectives"]):
        ax.plot(o[0], o[2], marker="*", ms=16, mfc="#ffd24a", mec="black", mew=0.8, zorder=6)
    ups = []
    for u, tr in enumerate(uavs):
        k = min(np.searchsorted(tr[:, 0], t), len(tr) - 1)
        p = tr[k, 1:]
        ups.append(p)
        ax.plot(p[0], p[1], marker="^", ms=11, mfc="white", mec="black", mew=0.8, zorder=8)
        ax.text(p[0] + 0.8, p[1] - 0.8, "UAV %d" % (u + 1), color="white", fontsize=10, zorder=9,
                bbox=dict(fc="black", ec="none", alpha=0.5, pad=1))
    for i, tr in enumerate(tracks):
        if len(tr) == 0:
            continue
        seen = tr[(tr[:, 0] <= t) & (tr[:, 0] >= t - 25.0)]
        if len(seen) > 1:   # fading trail of the last 25 s, broken where the rover respawned
            for k in range(1, len(seen)):
                if seen[k, 0] - seen[k - 1, 0] > 1.0:
                    continue
                age = (t - seen[k, 0]) / 25.0
                ax.plot(seen[k - 1:k + 1, 1], seen[k - 1:k + 1, 2], color=rc[i], lw=3.0, alpha=max(0.12, 1 - age), zorder=4)
        p = at(tr, t)
        if p is not None:
            for q in ups:   # the shared track picture: both UAVs hold this rover
                ax.plot([q[0], p[0]], [q[1], p[1]], color=rc[i], lw=1.0, ls=(0, (3, 3)), alpha=0.7, zorder=5)
            ax.plot(p[0], p[1], marker="o", ms=11, mfc=rc[i], mec="white", mew=1.5, zorder=7)
    ax.text(-24.2, -23.0, "t = %5.1f s" % t, color="white", fontsize=14, family="DejaVu Sans Mono", zorder=10)
    fig.canvas.draw()
    pipe.stdin.write(np.asarray(fig.canvas.buffer_rgba()).tobytes())
pipe.stdin.close()
pipe.wait()
print("wrote", out)
