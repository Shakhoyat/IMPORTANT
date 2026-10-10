"""Figure for Section 25: the CV pipeline as a story on one UAV frame (replaces the 3x3 debug montage).

Inputs (captured by the program, lab group): report/figures/raw/lab/cv_u2_*.png (the 320x240 sensor stages drawn x3 with
a header line), cv_f_external.png (the same moment seen from outside) and cv_frame.json (threshold, blob features).
Top row: where the UAV looks, the raw heat image with the zoom window, the histogram with Otsu's threshold and the fixed
floor. Bottom row: the same window through each stage, nearest-neighbour enlarged, joined by arrows.
Run: ~/.local/bin/uv run -q --with matplotlib --with numpy --with pillow python tools/report/gen_cv_story.py"""
import json
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import FancyArrowPatch, Rectangle
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAB = os.path.join(ROOT, "report", "figures", "raw", "lab")
OUT = os.path.join(ROOT, "report", "figures", "plots", "p_cv_story.pdf")
plt.rcParams.update({"font.family": "serif", "font.serif": ["Times New Roman", "Times", "Nimbus Roman", "DejaVu Serif"],
                     "mathtext.fontset": "stix", "font.size": 8.5})

F = json.load(open(os.path.join(LAB, "cv_frame.json")))
U = F["uavs"][1]
B = U["blobs"][0]
S = 3                       # the stage images are the 320x240 frame drawn x3
HDR = 0                     # header text is drawn over the image (top rows), not above it


def stage(name):
    return np.asarray(Image.open(os.path.join(LAB, "cv_u2_%s.png" % name)).convert("RGB"))


x0, y0, x1, y1 = B["bbox"]
cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
W, H = 60, 45                                     # zoom window in sensor pixels (4:3)
zx0, zy0 = int(cx - W / 2), int(cy - H / 2)
win = (zx0 * S, zy0 * S, (zx0 + W) * S, (zy0 + H) * S)

fig = plt.figure(figsize=(7.0, 6.2))
gs = fig.add_gridspec(3, 1, height_ratios=[1.0, 0.42, 0.62], hspace=0.42)
top = gs[0].subgridspec(1, 2, width_ratios=[1.33, 1.0], wspace=0.05)
bot = gs[2].subgridspec(1, 5, wspace=0.32)

# (a) the scene from outside
a = fig.add_subplot(top[0])
ext = np.asarray(Image.open(os.path.join(LAB, "cv_f_external.png")).convert("RGB"))
a.imshow(ext[28:, :])
a.set_title("(a) the scene at night", fontsize=8.5)
a.annotate("intruder rover", (800, 500), (560, 330), color="white", fontsize=7.5, arrowprops=dict(arrowstyle="->", color="white", lw=0.8))
a.axis("off")

# (b) raw sensor frame with the zoom window
b = fig.add_subplot(top[1])
raw = stage("a_raw_ironbow")
b.imshow(raw[30:, :], extent=(0, raw.shape[1], raw.shape[0], 30))
b.add_patch(Rectangle((win[0], win[1]), win[2] - win[0], win[3] - win[1], fill=False, ec="#39e639", lw=1.3))
b.annotate("rover engine", (x0 * S - 6, cy * S), (win[0] - 330, win[1] - 60), color="white", fontsize=7.5,
           arrowprops=dict(arrowstyle="->", color="white", lw=0.8))
b.set_xlim(0, raw.shape[1])
b.set_ylim(raw.shape[0], 30)
b.set_title("(b) UAV 2's heat image (320$\\times$240)", fontsize=8.5)
b.axis("off")

# (c) histogram of the grey frame: Otsu's threshold and the floor actually used
c = fig.add_subplot(gs[1])
grey = np.asarray(Image.open(os.path.join(LAB, "cv_u2_a_raw_grey.png")).convert("L"))[30::S, ::S]
h = np.bincount(grey.ravel(), minlength=256)
t_used, otsu = U["threshold_used"], U["otsu"]
c.bar(np.arange(256), np.maximum(h, 0.8), width=1.0, color=["#c0504d" if v >= t_used else "#7f9cc7" for v in range(256)], lw=0)
c.set_yscale("log")
c.axvline(otsu, color="#4f8a3a", lw=1.0, ls="--")
c.axvline(t_used, color="#c0504d", lw=1.2)
c.text(otsu + 3, h.max() * 0.25, "Otsu's threshold %d" % otsu, color="#4f8a3a", fontsize=7.5,
       bbox=dict(fc="white", ec="none", pad=0.5))
c.text(t_used - 3, 25, "threshold used %d = max(floor 204, Otsu + 1)" % t_used, color="#c0504d", fontsize=7.5, ha="right",
       bbox=dict(fc="white", ec="none", pad=0.5))
c.text(t_used + 3, 6.0, "%d hot pixels" % U["hot_pixels"], color="#c0504d", fontsize=7.5)
c.set_xlim(0, 255)
c.set_xlabel("heat (grey level)", fontsize=7.5)
c.set_ylabel("pixels (log)", fontsize=7.5)
c.tick_params(labelsize=6.5)
c.set_title("(c) histogram of (b): most pixels are cool ground; the target is the small hot tail", fontsize=8.5)
for sp in ("top", "right"):
    c.spines[sp].set_visible(False)

# bottom row: the window through every stage
steps = [("a_raw_ironbow", "(d) raw"), ("b_mask", "(e) threshold"),
         ("c_despeckle", "(f) despeckle"), ("d_components", "(g) components"), ("e_overlay", "(h) target")]
axes = []
for k, (nm, title) in enumerate(steps):
    ax = fig.add_subplot(bot[k])
    im = stage(nm)[win[1]:win[3], win[0]:win[2]]
    ax.imshow(im, interpolation="nearest")
    ax.set_title(title, fontsize=7.5)
    ax.set_xticks([])
    ax.set_yticks([])
    for sp in ax.spines.values():
        sp.set_color("#39e639")
        sp.set_linewidth(1.0)
    axes.append(ax)
notes = ["green box in (b)", "pixels $\\geq$ %d" % t_used, "a hot pixel needs\n2 hot neighbours", "1 blob, %d px" % int(B["area_px"]),
         "range %.1f m\ntrack %d" % (B["range_m"], B["track_id"])]
for ax, n in zip(axes, notes):
    ax.text(0.5, -0.06, n, transform=ax.transAxes, ha="center", va="top", fontsize=7)
fig.canvas.draw()
for k in range(4):
    p0 = axes[k].get_position()
    p1 = axes[k + 1].get_position()
    fig.add_artist(FancyArrowPatch((p0.x1 + 0.004, (p0.y0 + p0.y1) / 2), (p1.x0 - 0.004, (p1.y0 + p1.y1) / 2),
                                   transform=fig.transFigure, arrowstyle="-|>", mutation_scale=9, color="0.25", lw=1.0))
fig.savefig(OUT, bbox_inches="tight", dpi=300)
fig.savefig(OUT.replace(".pdf", ".png"), bbox_inches="tight", dpi=110)
print("wrote", OUT)
