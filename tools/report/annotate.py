"""Call-outs on the program's captures: where a concept of the report is used in the scene.

Each spec: the source capture (report/figures/raw/<src>.png), an optional crop (fractions x0, y0, x1, y1) and call-outs
(target x, y and label x, y as fractions of the cropped image, label text). Writes report/figures/img/annot/<name>.jpg
(1920 px wide). Positions were read off the captures; re-check them after a recapture.
Run: ~/.local/bin/uv run -q --with matplotlib --with numpy --with pillow python tools/report/annotate.py [name ...]"""
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.patheffects as pe
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RAW = os.path.join(ROOT, "report", "figures", "raw")
OUT = os.path.join(ROOT, "report", "figures", "img", "annot")
plt.rcParams.update({"font.family": "serif", "font.serif": ["Times New Roman", "Times", "Nimbus Roman", "DejaVu Serif"],
                     "mathtext.fontset": "stix"})

SPECS = {
    # Section 9: the tank's and the UAV's group nodes (Figure 9.x scene graph)
    "hier_tank": ("scene/iso_tank", (0.22, 0.12, 0.82, 0.84), [
        ((0.62, 0.36), (0.98, 0.10), "turret group: $R_y$ (traverse)"),
        ((0.28, 0.52), (0.04, 0.22), "gun group: $R_x$ (elevation),\nchild of the turret"),
        ((0.70, 0.73), (0.98, 0.95), "road-wheel groups: $R_x(\\omega t)$"),
        ((0.50, 0.55), (0.10, 0.88), "hull = root: $T$, $R_y$ of the tank"),
    ]),
    "hier_uav": ("scene/iso_uav", (0.24, 0.20, 0.80, 0.80), [
        ((0.12, 0.35), (0.03, 0.08), "motor mount $\\times4$: rotor group $R_y(\\omega t)$"),
        ((0.40, 0.84), (0.05, 0.95), "gimbal yaw $\\rightarrow$ pitch $\\rightarrow$ camera"),
        ((0.70, 0.73), (0.98, 0.95), "launcher rail: rounds (nested design)"),
        ((0.52, 0.27), (0.98, 0.08), "fuselage = root: $T$, $R$ of the UAV"),
    ]),
    # Section 17: which shading model each design uses (export field "shading")
    "shade_scene": ("insitu/insitu_guard_booth", None, [
        ((0.50, 0.58), (0.03, 0.80), "Flat: HESCO wall (one normal per face)"),
        ((0.68, 0.67), (0.98, 0.93), "Flat: concertina wire"),
        ((0.70, 0.45), (0.98, 0.32), "Gouraud: sandbags"),
        ((0.93, 0.22), (0.98, 0.06), "Gouraud: guard tower"),
        ((0.90, 0.44), (0.98, 0.58), "Gouraud: ammunition bunker mound"),
        ((0.525, 0.29), (0.03, 0.06), "Phong: soldiers (curved, glossy)"),
    ], 14),
    # Section 18: procedural (computed in the fragment shader from the world position) vs image textures (UV-mapped)
    "tex_scene": ("showcase/sc_camp_sam", None, [
        ((0.55, 0.30), (0.98, 0.06), "procedural: camouflage blobs (net)"),
        ((0.24, 0.84), (0.03, 0.94), "procedural: sand and soil (revetment)"),
        ((0.41, 0.74), (0.03, 0.70), "procedural: wood planks (crates)"),
        ((0.12, 0.60), (0.03, 0.50), "procedural: HESCO wire grid over sand bags"),
        ((0.30, 0.15), (0.03, 0.06), "image: CC0 tree (glTF, UV-mapped)"),
        ((0.92, 0.93), (0.98, 0.80), "image: CC0 grass atlas on each blade"),
        ((0.93, 0.68), (0.98, 0.55), "procedural: concrete and worn paint"),
    ], 14),
    # Section 20: the same night frame in three palettes (HUD rows cropped away)
    "pal_ironbow": ("v5b/scene_thermal", (0.0, 0.10, 1.0, 0.80), []),
    "pal_whitehot": ("v5b/scene_thermal_whitehot", (0.0, 0.10, 1.0, 0.80), []),
    "pal_rainbow": ("v5b/scene_thermal_rainbow", (0.0, 0.10, 1.0, 0.80), []),
    # Section 22: curved primitives in one view (primitives from the export)
    "curved_scene": ("insitu/insitu_aa_gatling_gun", None, [
        ((0.40, 0.37), (0.03, 0.08), "hemisphere: radome"),
        ((0.58, 0.43), (0.98, 0.08), "cylinder $\\times6$: Gatling barrels"),
        ((0.64, 0.60), (0.98, 0.30), "capsule $\\times37$: sandbag ring"),
        ((0.875, 0.63), (0.98, 0.80), "helix: concertina wire coils"),
        ((0.47, 0.64), (0.03, 0.92), "cylinders: pedestal, turntable, base plate"),
    ], 14),
}


def render(name, spec):
    src, crop, calls = spec[:3]
    fs = spec[3] if len(spec) > 3 else 22   # label size: 22 for half-width figures, ~14 for full width
    im = Image.open(os.path.join(RAW, src + ".png")).convert("RGB")
    if crop:
        W, H = im.size
        im = im.crop((int(crop[0] * W), int(crop[1] * H), int(crop[2] * W), int(crop[3] * H)))
    a = np.asarray(im)
    h, w = a.shape[:2]
    fig = plt.figure(figsize=(9.6, 9.6 * h / w), dpi=200)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.imshow(a)
    ax.set_xlim(0, w)
    ax.set_ylim(h, 0)
    ax.axis("off")
    stroke = [pe.withStroke(linewidth=2.6, foreground="black")]
    for (tx, ty), (lx, ly), label in calls:
        ax.annotate(label, (tx * w, ty * h), (lx * w, ly * h), fontsize=fs, color="white", ha="right" if lx > 0.55 else "left",
                    va="center",
                    bbox=dict(boxstyle="round,pad=0.3", fc=(0.08, 0.10, 0.14, 0.78), ec="#ffd24a", lw=1.2),
                    arrowprops=dict(arrowstyle="-|>", color="#ffd24a", lw=2.6, shrinkA=2, shrinkB=2, path_effects=stroke))
        ax.plot([tx * w], [ty * h], "o", ms=10, mfc="#ffd24a", mec="black", mew=1.2)
    os.makedirs(OUT, exist_ok=True)
    tmp = os.path.join(OUT, name + ".png")
    fig.savefig(tmp, dpi=200)
    plt.close(fig)
    out = Image.open(tmp).convert("RGB")
    out.thumbnail((1920, 1920), Image.LANCZOS)
    out.save(os.path.join(OUT, name + ".jpg"), quality=85)
    os.remove(tmp)
    print("wrote", name)


if __name__ == "__main__":
    names = sys.argv[1:] or list(SPECS)
    for n in names:
        render(n, SPECS[n])
