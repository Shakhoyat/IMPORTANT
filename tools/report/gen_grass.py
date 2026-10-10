"""Grass card for Section 7 (the grass field is GPU-instanced, not a scene part, so it has no shape-view capture).

Rebuilds blades the way rendering/Grass.cpp does: the 7-vertex grass-blade primitive (export --export-prims-lowres),
tufts of 7..12 blades fanned out within 7 cm of a centre, short turf or tall meadow heights, lean growing towards the
tuft rim, and the vertex shader's taper (1 - 0.96 t) and curved bend (t -> mix(t, t^2, 0.3)). Wind is left out (a still
frame). Drawn in the primitive's key colour with edges; blade widths are drawn x4 (x9 for the single blades) so the
strips can be seen at page size.
Run: ~/.local/bin/uv run -q --with matplotlib --with numpy python tools/report/gen_grass.py
Outputs: report/figures/img/shapes/grass_{blade,tuft,patch}.png and atlas_grass_field.jpg"""
import json
import math
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.collections import PolyCollection

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
import primkey  # noqa: E402

IMG = os.path.join(ROOT, "report", "figures", "img", "shapes")
LOW = json.load(open(os.path.join(ROOT, "build", "f_prims_lowres.json")))
BLADE = [p for p in LOW["primitives"] if p["name"] == "grass blade"][0]
P = np.array(BLADE["positions"], dtype=float)
TRI = np.array(BLADE["indices"]).reshape(-1, 3)
KEY = np.array(primkey.rgb01("Grass blade"))
WIDEN = 4.0
import glob as _glob
import matplotlib.font_manager as _fm
for _f in _glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "report", "fonts", "*.otf")):
    _fm.fontManager.addfont(_f)   # the report's Times face (TeX Gyre Termes X), so plot text matches the body text
plt.rcParams.update({"font.family": "serif", "font.serif": ["TeX Gyre TermesX", "Times New Roman", "Times", "Nimbus Roman", "DejaVu Serif"],
                     "mathtext.fontset": "stix", "font.size": 9})


def blade(root, yaw, h, w, lean, widen=WIDEN):
    """World vertices of one blade (Grass.cpp vertex shader without wind / pushers / LOD widening)."""
    t = P[:, 1] + 0.5
    taper = 1.0 - 0.96 * t
    p = np.stack([P[:, 0] * w * widen * taper, t * h, P[:, 2] * w * 0.3], axis=1)
    c, s = math.cos(yaw + math.pi / 2), math.sin(yaw + math.pi / 2)
    p = np.stack([c * p[:, 0] + s * p[:, 2], p[:, 1], -s * p[:, 0] + c * p[:, 2]], axis=1)
    bend = np.array([math.cos(yaw), math.sin(yaw)]) * lean
    amount = min(np.linalg.norm(bend), 1.4)
    tb = t * (1 - 0.3) + t * t * 0.3
    p[:, 0] += bend[0] * tb * h
    p[:, 2] += bend[1] * tb * h
    p[:, 1] -= amount * amount * tb * h * 0.3
    return p + np.asarray(root)


def tuft(rng, cx, cz, tall):
    """One tuft (Grass.cpp GrassField::build): 7..12 blades, radius 7 cm, 'tall' in [0, 1] mixes turf and meadow."""
    out = []
    n = 7 + int(rng.random() * 6)
    short_h = (0.12 + 0.20 * rng.random()) * 1.0
    th = short_h + (0.50 + 0.40 * rng.random() - short_h) * tall
    for _ in range(n):
        a, rr = rng.random() * 2 * math.pi, 0.07 * math.sqrt(rng.random())
        h = th * (0.55 + 0.6 * rng.random())
        w = (0.007 + 0.008 * rng.random()) * (0.7 + 0.9 * th)
        lean = (0.15 + 0.9 * rr / 0.07 * (0.4 + 0.6 * rng.random())) * (0.8 + 0.5 * tall)
        out.append(blade((cx + math.cos(a) * rr, 0.0, cz + math.sin(a) * rr), a, h, w, lean))
    return out


def draw(ax, blades, az=35.0, el=18.0, ground=None):
    """Painter's algorithm with Lambert shading (the shader turns the normal to the viewer: two-sided)."""
    A, E = math.radians(az), math.radians(el)
    R1 = np.array([[math.cos(A), 0, -math.sin(A)], [0, 1, 0], [math.sin(A), 0, math.cos(A)]])
    R2 = np.array([[1, 0, 0], [0, math.cos(E), -math.sin(E)], [0, math.sin(E), math.cos(E)]])
    M = R2 @ R1
    L = np.array([-0.4, 0.8, 0.45])
    L /= np.linalg.norm(L)
    polys, cols, depth = [], [], []
    if ground is not None:
        g = np.array(ground) @ M.T
        ax.add_collection(PolyCollection([g[:, :2]], facecolors=["#d9d2c3"], edgecolors="#b8ae98", linewidths=0.5, zorder=0))
    for b in blades:
        for tri in TRI:
            v = b[tri]
            n = np.cross(v[1] - v[0], v[2] - v[0])
            nn = np.linalg.norm(n)
            lam = 0.45 + 0.55 * abs(n @ L) / nn if nn > 0 else 0.6
            vv = v @ M.T
            polys.append(vv[:, :2])
            cols.append(tuple(np.clip(KEY * lam * 1.08, 0, 1)))
            depth.append(vv[:, 2].mean())
    order = np.argsort(depth)
    ax.add_collection(PolyCollection([polys[i] for i in order], facecolors=[cols[i] for i in order], edgecolors="#4a4740",
                                     linewidths=0.25, zorder=1))
    allp = np.concatenate(polys)
    if ground is not None:
        allp = np.concatenate([allp, (np.array(ground) @ M.T)[:, :2]])
    lo, hi = allp.min(0), allp.max(0)
    pad = 0.06 * (hi - lo).max()
    ax.set_xlim(lo[0] - pad, hi[0] + pad)
    ax.set_ylim(lo[1] - pad, hi[1] + pad)
    ax.set_aspect("equal")
    ax.axis("off")


def save(fig, name):
    fig.savefig(os.path.join(IMG, name), dpi=220, bbox_inches="tight", pad_inches=0.02, facecolor="white")
    plt.close(fig)
    print("wrote", name)


rng = np.random.default_rng(7)
# (a) the primitive, then one short and one tall blade instance (front view, widths x4)
fig, ax = plt.subplots(1, 3, figsize=(5.6, 2.6), gridspec_kw={"width_ratios": [1, 1, 1]})
prim = np.stack([P[:, 0], P[:, 1] + 0.5, P[:, 2]], axis=1) * 0.5
for a_, bl, title in [(ax[0], [prim], "primitive\n$V=7$, $T=5$"), (ax[1], [blade((0, 0, 0), -0.6, 0.25, 0.011, 0.2, 9.0)], "turf blade\n$h$ 0.25 m"),
                      (ax[2], [blade((0, 0, 0), -0.6, 0.8, 0.013, 0.7, 9.0)], "meadow blade\n$h$ 0.80 m, lean")]:
    draw(a_, bl, az=0.0 if a_ is ax[0] else 20.0, el=0.0 if a_ is ax[0] else 8.0)
    a_.set_title(title, fontsize=13)
ax[0].set_ylim(-0.03, 0.55)
for a_ in ax[1:]:   # one scale for both instances
    a_.set_xlim(-0.2, 0.5)
    a_.set_ylim(-0.03, 0.85)
save(fig, "grass_blade.png")

# (b) one tall tuft
fig, ax = plt.subplots(figsize=(2.4, 2.4))
draw(ax, tuft(np.random.default_rng(3), 0.0, 0.0, 1.0), az=30.0, el=14.0,
     ground=[(-0.35, 0, -0.35), (0.35, 0, -0.35), (0.35, 0, 0.35), (-0.35, 0, 0.35)])
save(fig, "grass_tuft.png")

# (c) a 1.6 m x 1.6 m patch: short turf with a band of tall meadow
blades = []
for _ in range(60):
    cx, cz = rng.uniform(-0.8, 0.8, 2)
    blades += tuft(rng, cx, cz, float(np.clip(0.5 + 1.6 * cx, 0, 1)))
fig, ax = plt.subplots(figsize=(4.2, 2.6))
draw(ax, blades, az=30.0, el=22.0, ground=[(-0.9, 0, -0.9), (0.9, 0, -0.9), (0.9, 0, 0.9), (-0.9, 0, 0.9)])
save(fig, "grass_patch.png")
print("blades in the patch:", len(blades))

# atlas tile (same look as the other atlas thumbnails: light grey background)
from PIL import Image  # noqa: E402
im = Image.open(os.path.join(IMG, "grass_patch.png")).convert("RGB")
bg = Image.new("RGB", im.size, (236, 236, 238))
mask = Image.fromarray((np.asarray(im).min(axis=2) < 250).astype(np.uint8) * 255)
bg.paste(im, (0, 0), mask)
bg.save(os.path.join(IMG, "grass_field_model.jpg"), quality=90)

# ---- the card (report/sections/gen_grass.tex, \grasscard): numbers parsed from the C++ sources
import re  # noqa: E402
hdr = open(os.path.join(ROOT, "src", "primitives", "GrassBlade.h")).read()
cpp = open(os.path.join(ROOT, "src", "rendering", "Grass.cpp")).read()
budget = {k: int(v) for k, v in re.findall(r"kGrassBlades(\w+) = (\d+);", hdr)}
grid = int(re.search(r"kGrid = (\d+);", cpp).group(1))
compound = float(re.search(r"kCompoundRadius = ([\d.]+)f;", cpp).group(1))
fmt = lambda n: "{:,}".format(n).replace(",", "\\,")
rows = [
    ("Primitive", "grass blade (Section~\\ref{sec:prims}): $V=7$, $T=5$, a folded strip of three rows and a tip"),
    ("Blades", "budget per preset: LOW %s, HIGH %s, ULTRA %s, EPIC %s" % (fmt(budget["Low"]), fmt(budget["High"]), fmt(budget["Ultra"]), fmt(budget["Epic"]))),
    ("Tuft", "7--12 blades within 7\\,cm of a centre; yaw $=$ the direction out from the centre; lean grows to the rim"),
    ("Height", "short turf 0.12--0.32\\,m; tall meadow 0.50--0.90\\,m where a large-scale noise says ``uncut''"),
    ("Instance", "per blade: root $(x,y,z)$, yaw, height, width, lean, seed (two \\code{vec4} attributes)"),
    ("Shape", "width $\\times(1-0.96t)$, bend $\\propto\\operatorname{mix}(t,t^2,0.3)$ along the lean plus wind gusts"),
    ("Culling, LOD", "%d$\\times$%d cells; every blade within 26\\,m, 40\\,\\%% at 75\\,m, 24\\,\\%% past 120\\,m (the kept blades widen)" % (grid, grid)),
    ("Area", "full density inside the %.0f\\,m compound, 45\\,\\%% on the apron to the base edge" % compound),
]
tex = ["% generated by tools/report/gen_grass.py: do not edit",
       "\\newcommand{\\grasscard}{%",
       "\\needspace{12cm}",
       "\\begin{center}",
       "\\fcolorbox{black!30}{white}{\\begin{minipage}{0.975\\linewidth}\\centering\\vspace{0.6mm}",
       "\\begin{minipage}[b]{0.37\\linewidth}\\centering\\includegraphics[width=\\linewidth]{figures/img/shapes/grass_blade.png}\\\\{\\footnotesize (a) primitive and two instances}\\end{minipage}\\hfill",
       "\\begin{minipage}[b]{0.17\\linewidth}\\centering\\includegraphics[width=\\linewidth]{figures/img/shapes/grass_tuft.png}\\\\{\\footnotesize (b) one tuft}\\end{minipage}\\hfill",
       "\\begin{minipage}[b]{0.42\\linewidth}\\centering\\includegraphics[width=\\linewidth]{figures/img/shapes/grass_patch.png}\\\\{\\footnotesize (c) a 1.6\\,m patch: turf (left), meadow (right)}\\end{minipage}\\\\[1.5mm]",
       "\\includegraphics[width=\\linewidth,height=4.2cm,keepaspectratio]{figures/img/scene/m6_grass_close.jpg}\\\\{\\footnotesize (d) the same blades rendered in the scene (EPIC, textured, alpha-to-coverage)}",
       "\\\\[0.8mm]{\\fontsize{7}{8.5}\\selectfont \\primchipn{grassblade}{%s} \\quad blades over the whole base, one instanced draw call per visible cell}\\vspace{0.6mm}\\end{minipage}}" % fmt(budget["Epic"]),
       "\\captionof{figure}{Grass field. (a--c) are drawn in the key colour from the primitive and the rules of "
       "\\code{rendering/Grass.cpp} (widths drawn $\\times4$, single blades $\\times9$, wind left out).}\\label{fig:d-grass}",
       "\\end{center}\\vspace{-3mm}",
       "\\noindent\\textbf{Grass field.}\\label{des:grass} One primitive, instanced on the GPU: the grass is not a scene-graph part and is "
       "not counted in the %s parts. Its rules:" % "\\sceneParts{}",
       "\\begin{center}\\fontsize{7.5}{9}\\selectfont\\begin{tabularx}{\\linewidth}{@{}>{\\bfseries}lX@{}}\\toprule",
       "\n".join("%s & %s\\\\" % r for r in rows),
       "\\bottomrule\\end{tabularx}\\end{center}}"]
open(os.path.join(ROOT, "report", "sections", "gen_grass.tex"), "w").write("\n".join(tex) + "\n")
print("wrote gen_grass.tex")
