"""Section 6 primitive cards: one card per primitive (cube and cuboid share one), drawn from the REAL generators'
low-resolution output (ts --export-prims-lowres -> report/data/prims_lowres.json) and the scene export (library
parameters, V / T, instance counts). Each card:
  (a) the program's gallery tile in the colour key, with its triangle edges (ts --capture prims)
  (b) a TikZ drawing of the low-res mesh: axes, numbered vertices (all for flat primitives; poles, first ring and the
      highlighted cell for curved ones), parameter angles, normals, the seam, caps as separate fans
  (c) the highlighted cell split into its two triangles with the generator's index formula (CCW from outside)
  (d) surface / normal formulas, V and T with a one-line derivation, the scene's parameters and counts
  (e) "used for": three part tiles of different designs (ts --capture tile:...) and the instance count
Every index named in (b) / (c) is checked against the low-res index array (the script stops if a triangle is missing).
Writes report/sections/gen_primcards.tex and report/figures/img/primcards/*.jpg.
Run: <venv with numpy + pillow> python tools/report/gen_prim_cards.py"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import primkey  # noqa: E402
from capture_shapes import tile_friendly, tokens_tiles  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LOW = json.load(open(os.path.join(ROOT, "report", "data", "prims_lowres.json")))
EXP = json.load(open(os.path.join(ROOT, "report", "data", "scene_export_v10.json")))
RAW = os.path.join(ROOT, "report", "figures", "raw", "shapes")
IMG = os.path.join(ROOT, "report", "figures", "img", "primcards")
OUT = os.path.join(ROOT, "report", "sections", "gen_primcards.tex")
os.makedirs(IMG, exist_ok=True)
LOWP = {p["name"]: p for p in LOW["primitives"]}
LIB = {p["name"]: p for p in EXP["primitives"]}
EXPORT_KEY = {"cube": "Cube", "cuboid": "Cuboid", "wedge": "Wedge", "tapered box": "Tapered box", "pyramid": "Pyramid",
              "sphere": "Sphere", "hemisphere": "Hemisphere", "capsule": "Capsule", "cylinder": "Cylinder",
              "frustum": "Cylinder (frustum)", "cone": "Cone", "torus": "Torus", "annulus": "Annulus", "helix": "Helix",
              "grass blade": "Grass blade", "terrain disc": "Terrain disc"}


def fmt_int(n):
    return "{:,}".format(n).replace(",", "\\,")


# ------------------------------------------------------------------------------------------------ projection
class View:
    def __init__(self, az, el, scale):
        a, e = math.radians(az), math.radians(el)
        self.to_viewer = np.array([math.cos(e) * math.sin(a), math.sin(e), math.cos(e) * math.cos(a)])
        fwd = -self.to_viewer
        self.right = np.cross(fwd, [0, 1, 0])
        self.right /= np.linalg.norm(self.right)
        self.up = np.cross(self.right, fwd)
        self.s = scale

    def p(self, v):
        v = np.asarray(v, float)
        return (float(np.dot(v, self.right)) * self.s, float(np.dot(v, self.up)) * self.s)

    def depth(self, v):
        return float(np.dot(np.asarray(v, float), self.to_viewer))


def c2(q):
    return "(%.3f,%.3f)" % q


def lighten(rgb, t):
    return tuple(int(round(c + (255 - c) * t)) for c in rgb)


# ------------------------------------------------------------------------------------------------ the mesh drawing (b)
LAST = {}   # the mesh drawn for the card being built (its numbered vertices go into the card's vertex table)


def vertex_table(name, labels, cell):
    """The numbered vertices of a low-res drawing and the highlighted cell's triangles, as the flat cards' corner tables."""
    P = LOWP[name]["positions"]
    seen, idx = set(), []
    for i in labels:
        if i not in seen:
            seen.add(i)
            idx.append(i)
    idx = idx[:12]
    big = max(abs(c) for i in idx for c in P[i]) > 2.0   # the terrain disc is in metres
    f = (lambda v: "%.1f" % v) if big else (lambda v: "%.2f" % v)
    clean = lambda t: "0" if t in ("-0.00", "-0.0", "0.00", "0.0") else t.rstrip("0").rstrip(".") if "." in t else t  # noqa: E731
    o = ["{\\fontsize{7}{8}\\selectfont\\setlength{\\tabcolsep}{2.5pt}\\begin{tabular}{@{}rrrr@{}}\\toprule $i$ & $x$ & $y$ & $z$\\\\\\midrule"]
    for i in idx:
        o.append("%d & %s & %s & %s\\\\" % ((i,) + tuple(clean(f(c)) for c in P[i])))
    tri = ", ".join("(%s)" % " ".join(str(v) for v in t) for t in cell)
    o.append("\\midrule\\multicolumn{4}{@{}p{2.9cm}@{}}{\\raggedright cell: %s%s}\\\\\\bottomrule\\end{tabular}}"
             % (tri, "" if len(set(labels)) <= 12 else "; first 12 numbered vertices"))
    return "\n".join(o)


def draw_mesh(name, key, view, labels, cell, normals, seam, fans=(), arcs=(), axes=0.75, label_dist=0.16, extra=(),
              fan_centres=(), label_set="sel"):
    LAST.clear()
    LAST.update(name=name, labels=list(labels), cell=[tuple(t) for t in cell])
    m = LOWP[name]
    P = np.array(m["positions"], float)
    N = np.array(m["normals"], float)
    I = m["indices"]
    tris = [tuple(I[t:t + 3]) for t in range(0, len(I), 3)]
    tset = set(tuple(t) for t in tris)

    def has_tri(t):
        return any(t[k:] + t[:k] in tset for k in range(3)) if isinstance(t, tuple) else False

    for t in cell:
        t = tuple(t)
        if not any((t[k], t[(k + 1) % 3], t[(k + 2) % 3]) in tset for k in range(3)):
            sys.exit("%s: cell triangle %s is not in the generator's index array" % (name, t))
    rgb = primkey.BY_KEY[key]["rgb"]
    o = ["\\begin{tikzpicture}[font=\\fontsize{7}{8}\\selectfont,line join=round]",
         "\\definecolor{cf}{RGB}{%d,%d,%d}\\definecolor{cb}{RGB}{%d,%d,%d}\\definecolor{cc}{RGB}{%d,%d,%d}"
         % (lighten(rgb, 0.62) + lighten(rgb, 0.85) + lighten(rgb, 0.25))]
    # axes first (the mesh is drawn over them half transparent)
    for v, lab, col in (((1, 0, 0), "x", "red!70!black"), ((0, 1, 0), "y", "green!45!black"), ((0, 0, 1), "z", "blue!70!black")):
        q = view.p(np.array(v) * axes)
        o.append("\\draw[-{Latex[length=1.3mm]},%s,line width=0.5pt] (0,0) -- %s node[pos=1.12] {$%s$};" % (col, c2(q), lab))
    fanset = set(tuple(t) for t in fans)
    cellset = set(tuple(t) for t in cell)

    def tri_normal(t):
        a, b, c = P[t[0]], P[t[1]], P[t[2]]
        return np.cross(b - a, c - a)

    order = sorted(tris, key=lambda t: view.depth(P[list(t)].mean(axis=0)))
    for t in order:
        n = tri_normal(t)
        if np.linalg.norm(n) < 1e-9:
            continue
        front = np.dot(n, view.to_viewer) > 0
        pts = " -- ".join(c2(view.p(P[i])) for i in t)
        if not front:
            o.append("\\draw[black!28,line width=0.25pt,densely dashed] %s -- cycle;" % pts)
            continue
        if t in cellset:
            o.append("\\filldraw[fill=cc,draw=black,line width=0.9pt] %s -- cycle;" % pts)
        elif t in fanset:
            o.append("\\filldraw[fill=cb,draw=black!75,line width=0.35pt] %s -- cycle;" % pts)
        else:
            o.append("\\filldraw[fill=cf,fill opacity=0.88,draw=black!75,line width=0.35pt] %s -- cycle;" % pts)
    # seam
    for chain in seam:
        o.append("\\draw[red!80!black,line width=1.0pt] %s;" % " -- ".join(c2(view.p(P[i])) for i in chain))
    # parameter arcs: (centre, u, v, radius, a0, a1, label) in 3D
    for (ctr, u, v, r, a0, a1, lab) in arcs:
        ctr, u, v = np.array(ctr, float), np.array(u, float), np.array(v, float)
        pts = [ctr + r * (math.cos(a) * u + math.sin(a) * v) for a in np.linspace(a0, a1, 24)]
        o.append("\\draw[-{Latex[length=1.2mm]},orange!85!black,line width=0.8pt] %s;" % " -- ".join(c2(view.p(q)) for q in pts))
        mid = ctr + r * 1.25 * (math.cos((a0 + a1) / 2) * u + math.sin((a0 + a1) / 2) * v)
        o.append("\\node[orange!60!black] at %s {%s};" % (c2(view.p(mid)), lab))
    for e in extra:
        o.append(e(view, P))
    # normals
    for i in normals:
        a = view.p(P[i])
        b = view.p(P[i] + N[i] * 0.28)
        o.append("\\draw[-{Latex[length=1.3mm]},violet,line width=0.7pt] %s -- %s;" % (c2(a), c2(b)))
    # vertex numbers
    ctr2 = np.mean([view.p(p) for p in P], axis=0)
    for i in labels:
        q = np.array(view.p(P[i]))
        d = q - ctr2
        nrm = np.linalg.norm(d)
        d = d / nrm * label_dist if nrm > 1e-6 else np.array([0, label_dist])
        o.append("\\fill %s circle (0.6pt);" % c2(tuple(q)))
        o.append("\\node[inner sep=0.2pt,fill=white,fill opacity=0.8,text opacity=1] at %s {%d};" % (c2(tuple(q + d)), i))
    o.append("\\end{tikzpicture}")
    return "\n".join(o)


# ------------------------------------------------------------------------------------------------ the cell (c)
def cell_fig(corners, tris, note):
    """corners: 4 labels (a, b, c, d) placed at bottom-left, bottom-right, top-left, top-right of a unit square;
    tris: two tuples of corner letters, CCW as the generator emits them."""
    pos = {"a": (0, 0), "b": (2.1, 0), "c": (0, 1.6), "d": (2.1, 1.6)}
    lab = dict(corners)
    o = ["\\begin{tikzpicture}[font=\\fontsize{7}{8}\\selectfont]"]
    shades = ["black!8", "black!18"]
    for k, t in enumerate(tris):
        o.append("\\filldraw[fill=%s,draw=black,line width=0.6pt] %s -- cycle;" % (shades[k], " -- ".join(c2(pos[x]) for x in t)))
        cx = sum(pos[x][0] for x in t) / 3
        cy = sum(pos[x][1] for x in t) / 3
        o.append("\\draw[-{Latex[length=1.1mm]},blue!60!black] ([shift={(0.22,0)}]%.2f,%.2f) arc (0:300:0.22);" % (cx, cy))
        o.append("\\node at (%.2f,%.2f) {\\textbf{%d}};" % (cx, cy, k + 1))
    for x, (px, py) in pos.items():
        anchor = {"a": "north east", "b": "north west", "c": "south east", "d": "south west"}[x]
        o.append("\\node[anchor=%s,inner sep=1pt] at (%.2f,%.2f) {$%s$};" % (anchor, px, py, lab[x]))
    o.append("\\node[anchor=north,align=center,text width=3.4cm] at (1.05,-0.32) {%s};" % note)
    o.append("\\end{tikzpicture}")
    return "\n".join(o)


# ------------------------------------------------------------------------------------------------ helpers
def ring_idx(s, k, j):  # sphere / terrain north rings
    return 1 + (k - 1) * s + (j % s)


def axis_seam(name, axis, pts_filter):
    m = LOWP[name]
    P = np.array(m["positions"])
    idx = [i for i in range(len(P)) if pts_filter(P[i])]
    return sorted(idx, key=lambda i: P[i][axis])


def table(rows):
    o = ["{\\fontsize{7}{8.4}\\selectfont\\setlength{\\tabcolsep}{3pt}\\renewcommand{\\arraystretch}{1.08}",
         "\\begin{tabularx}{\\linewidth}{@{}>{\\bfseries}l>{\\raggedright\\arraybackslash}X@{}}\\toprule"]
    o += ["%s & %s\\\\" % r for r in rows]
    o.append("\\bottomrule\\end{tabularx}}")
    return "\n".join(o)


def scene_row(lib_name):
    p = LIB[lib_name]
    return ("scene", "\\texttt{%s}: $V=%s$, $T=%s$; %s parts in the scene" %
            (p["function"].replace("_", "\\_"), fmt_int(p["vertices"]), fmt_int(p["triangles"]), fmt_int(p["instances"])))


def low_row(name):
    p = LOWP[name]
    return ("drawn", "\\texttt{%s}: $V=%d$, $T=%d$" % (p["call"], p["V"], p["T"]))


# ------------------------------------------------------------------------------------------------ images
def crop_tile(src, dst, pad=0.06):
    im = Image.open(src).convert("RGB")
    a = np.asarray(im).astype(int)
    bg = a[2, 2]
    mask = np.abs(a - bg).sum(axis=2) > 18
    ys, xs = np.nonzero(mask)
    if len(xs):
        x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
        side = max(x1 - x0, y1 - y0) * (1 + 2 * pad)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        box = [int(cx - side / 2), int(cy - side / 2), int(cx + side / 2), int(cy + side / 2)]
        box = [max(0, box[0]), max(0, box[1]), min(im.width, box[2]), min(im.height, box[3])]
        im = im.crop(box)
    im.thumbnail((700, 700))
    im.save(dst, quality=90)


def gallery(entry_name):
    pj = json.load(open(os.path.join(RAW, "prims", "prims.json")))["primitives"]
    e = next(x for x in pj if x["name"] == entry_name)
    dst = os.path.join(IMG, "prim_%02d.jpg" % e["number"])
    crop_tile(os.path.join(RAW, "prims", e.get("key", e["wire"].replace("_wire", "_key"))), dst)
    return "figures/img/primcards/prim_%02d.jpg" % e["number"]


TILES = {}
for tok in tokens_tiles(EXP):
    obj, part = tok[5:].split("|", 1)
    TILES.setdefault(obj, []).append(part)


def slug(t):
    o = ""
    for ch in t:
        if ch.isalnum():
            o += ch.lower()
        elif o and o[-1] != "_":
            o += "_"
    return o.rstrip("_")


def used_for(export_key):
    """Tiles of the 3 most frequent part kinds (different designs) of this primitive + their designs."""
    import collections
    by = collections.Counter()
    first = {}
    for c in EXP["components"]:
        if c["primitive"] == "Group" or c["hidden_at_export"]:
            continue
        key = c["primitive"]
        if key == "Cube":
            s = c["S_rest"]
            if max(s) - min(s) > 1e-3 * max(max(s), 1e-6):
                key = "Cuboid"
        if key != export_key or not tile_friendly(c):
            continue
        by[(c["design"], c["part_kind"])] += 1
        first.setdefault((c["design"], c["part_kind"]), c)
    out, seen = [], set()
    for (design, kind), n in by.most_common():
        if design in seen:
            continue
        seen.add(design)
        c = first[(design, kind)]
        src = os.path.join(RAW, "tiles", "%s__%s.png" % (slug(c["object"]), slug(kind)))
        if os.path.exists(src):
            dst = os.path.join(IMG, "tile_%s__%s.jpg" % (slug(c["object"]), slug(kind)))
            crop_tile(src, dst)
            S = c["S_rest"]
            out.append(("figures/img/primcards/" + os.path.basename(dst), kind, design, n,
                        "%s\\,$\\times$\\allowbreak\\,%s\\,$\\times$\\allowbreak\\,%s" % tuple(("%.3g" % (v * 100)) for v in S)))
        if len(seen) == 3:
            break
    return out


def used_strip(export_key, total):
    items = used_for(export_key)
    cells = []
    for img, kind, design, n, size in items:
        cells.append("\\begin{tabular}[t]{@{}c@{}}\\img{%s}{1.3cm}\\\\[-0.4mm]\\parbox[t]{1.95cm}{\\centering %s ($\\times$%d)\\\\ \\textit{%s}\\\\ %s\\,cm}\\end{tabular}"
                     % (img, kind, n, design, size))
    return ("{\\fontsize{7}{8}\\selectfont\\textbf{(e) Used for}: %s parts in the scene\\\\[0.5mm]%s}" %
            (fmt_int(total), "\\hfill".join(cells)))


# ------------------------------------------------------------------------------------------------ the cards
cards = []


def card(title, label, key, img, mesh_tex, cell_tex, rows, used, extra_tex=""):
    """Half a page: title; (a) | (b) | [corner table] | (c); (d) | (e)."""
    curved = False
    if not extra_tex and LAST:   # curved primitives: the drawing's numbered vertices as a table
        extra_tex = vertex_table(LAST["name"], LAST["labels"], LAST["cell"])
        curved = True
    LAST.clear()
    if curved:
        row1 = ("\\begin{minipage}[c]{0.15\\linewidth}\\centering\\img{%s}{\\linewidth}\\\\{\\fontsize{7}{8}\\selectfont (a) scene mesh}\\end{minipage}\\hfill"
                "\\begin{minipage}[c]{0.34\\linewidth}\\centering %s\\\\{\\fontsize{7}{8}\\selectfont (b) low-resolution mesh}\\end{minipage}\\hfill"
                "\\begin{minipage}[c]{0.2\\linewidth}\\centering %s\\end{minipage}\\hfill"
                "\\begin{minipage}[c]{0.27\\linewidth}\\centering %s\\\\{\\fontsize{7}{8}\\selectfont (c) the highlighted cell}\\end{minipage}"
                % (img, mesh_tex, extra_tex, cell_tex))
    elif extra_tex:   # flat primitives: the corner table between the drawing and the cell
        row1 = ("\\begin{minipage}[c]{0.17\\linewidth}\\centering\\img{%s}{\\linewidth}\\\\{\\fontsize{7}{8}\\selectfont (a) scene mesh}\\end{minipage}\\hfill"
                "\\begin{minipage}[c]{0.27\\linewidth}\\centering %s\\\\{\\fontsize{7}{8}\\selectfont (b) corners}\\end{minipage}\\hfill"
                "\\begin{minipage}[c]{0.22\\linewidth}\\centering %s\\end{minipage}\\hfill"
                "\\begin{minipage}[c]{0.3\\linewidth}\\centering %s\\\\{\\fontsize{7}{8}\\selectfont (c) one face}\\end{minipage}"
                % (img, mesh_tex, extra_tex, cell_tex))
    else:   # (no table: not used)
        row1 = ("\\begin{minipage}[c]{0.2\\linewidth}\\centering\\img{%s}{\\linewidth}\\\\{\\fontsize{7}{8}\\selectfont (a) scene mesh}\\end{minipage}\\hfill"
                "\\begin{minipage}[c]{0.44\\linewidth}\\centering %s\\\\{\\fontsize{7}{8}\\selectfont (b) low-resolution mesh}\\end{minipage}\\hfill"
                "\\begin{minipage}[c]{0.32\\linewidth}\\centering %s\\\\{\\fontsize{7}{8}\\selectfont (c) the highlighted cell}\\end{minipage}"
                % (img, mesh_tex, cell_tex))
    if used:
        row2 = ("\\begin{minipage}[t]{0.6\\linewidth}{\\fontsize{7}{8}\\selectfont\\textbf{(d)}}\\\\[-0.5mm]%s\\end{minipage}\\hfill"
                "\\begin{minipage}[t]{0.37\\linewidth}%s\\end{minipage}" % (table(rows), used))
    else:
        row2 = "{\\fontsize{7}{8}\\selectfont\\textbf{(d)}}\\\\[-0.5mm]" + table(rows)
    o = ["\\begin{figure}[tbp]\\centering",
         "\\fcolorbox{black!35}{white}{\\begin{minipage}{0.975\\linewidth}\\vspace{0.8mm}",
         "{\\large\\bfseries %s}\\hfill {\\small\\primchip{%s}}\\\\[0.5mm]" % (title, primkey.tex_key(key)),
         row1 + "\\\\[1.5mm]", row2, "\\vspace{0.8mm}\\end{minipage}}",
         "\\caption{%s card.}\\label{fig:card-%s}" % (title, label),
         "\\end{figure}"]
    cards.append("\n".join(o))


V = View(35, 22, 2.6)

# ---- cube / cuboid --------------------------------------------------------------------------------------------
cube = LOWP["cube"]
Pc = np.array(cube["positions"])
cube_labels = list(range(len(Pc)))
card("Cube and cuboid", "cube", "Cuboid", gallery("cuboid"),
     "\\cornerfigCube",
     cell_fig([("a", "v_0"), ("b", "v_1"), ("c", "v_3"), ("d", "v_2")], [("a", "b", "d"), ("a", "d", "c")],
              "face $(v_0,v_1,v_2,v_3)$ $\\to$ triangles\\\\$(0,1,2)$ and $(0,2,3)$"),
     [("corners", "8 corners $(\\pm\\frac12,\\pm\\frac12,\\pm\\frac12)$; a cuboid is the same mesh with a non-uniform $S$"),
      ("normal", "one per face: $\\pm\\vect{e}_x,\\pm\\vect{e}_y,\\pm\\vect{e}_z$ (flat)"),
      ("$V$", "$6\\cdot4=24$: every corner is stored once per face it belongs to (3 normals)"),
      ("$T$", "$6\\cdot2=12$: each face is the two triangles $(0,1,2),(0,2,3)$ of its corners"),
      scene_row("cube"), ("cuboid", "%s parts (cube parts with $S_x,S_y,S_z$ not all equal)" % fmt_int(LIB["cuboid"]["instances"]))],
     used_strip("Cuboid", LIB["cube"]["instances"] + LIB["cuboid"]["instances"]),
     "{\\fontsize{7}{8}\\selectfont\\cornersCube}")

# the remaining flat primitives: corner drawing + table from gen_m3, one face split into two triangles
for nm, macro, lab, Vt, Tt in [
        ("wedge", "Wedge", "wedge", "$3\\cdot4+2\\cdot3=18$: three quads (bottom, back, slope) and two triangular sides",
         "$3\\cdot2+2\\cdot1=8$"),
        ("tapered box", "TaperedBox", "taperedbox", "$6\\cdot4=24$, as the cube (the top face is %g$\\times$%g)" %
         (LOWP["tapered box"]["params"]["topX"], LOWP["tapered box"]["params"]["topZ"]), "$6\\cdot2=12$"),
        ("pyramid", "Pyramid", "pyramid", "$4+4\\cdot3=16$: the base quad and four triangular sides", "$2+4=6$")]:
    key = EXPORT_KEY[nm]
    p = LIB[nm]
    card(nm.capitalize(), lab, key, gallery(nm),
         "\\cornerfig%s" % macro,
         cell_fig([("a", "v_0"), ("b", "v_1"), ("c", "v_3"), ("d", "v_2")], [("a", "b", "d"), ("a", "d", "c")],
                  "every quad face $(v_0,v_1,v_2,v_3)$ $\\to$ $(0,1,2)$, $(0,2,3)$; a triangle face is one triangle"),
         [("corners", "{\\fontsize{7}{8}\\selectfont\\corners%s}" % macro if False else "%d corners, listed with the faces at the right" % len(p["corners"])),
          ("normal", "one per face (flat): the face's outward normal"),
          ("$V$", Vt), ("$T$", Tt), scene_row(nm)],
         used_strip(key, p["instances"]),
         "{\\fontsize{7}{8}\\selectfont\\corners%s}" % macro)

# ---- sphere ---------------------------------------------------------------------------------------------------
s, K = LOWP["sphere"]["params"]["s"], LOWP["sphere"]["params"]["k"]
a, b, c_, d_ = ring_idx(s, 1, 0), ring_idx(s, 1, 1), ring_idx(s, 2, 0), ring_idx(s, 2, 1)
Ps = np.array(LOWP["sphere"]["positions"])
seam = axis_seam("sphere", 1, lambda q: abs(q[2]) < 1e-6 and q[0] >= -1e-6)
card("Sphere", "sphere", "Sphere", gallery("sphere"),
     draw_mesh("sphere", "Sphere", View(25, 18, 2.3), [0] + [ring_idx(s, 1, j) for j in range(s)] + [c_, d_],
               [(a, b, c_), (b, d_, c_)], [0, ring_idx(s, 1, 2), c_], [seam],
               arcs=[((0, 0, 0), (1, 0, 0), (0, 0, 1), 0.62, 0.0, 2 * math.pi / s, "$\\varphi$"),
                     ((0, 0, 0), (0, 1, 0), (1, 0, 0), 0.62, 0.0, math.pi / K, "$\\theta$")], axes=0.82),
     cell_fig([("a", "%d" % a), ("b", "%d" % b), ("c", "%d" % c_), ("d", "%d" % d_)], [("a", "b", "c"), ("b", "d", "c")],
              "ring$(k,j)=1+(k-1)s+(j \\bmod s)$; $a$=ring$(k,j)$, $b$=ring$(k,j{+}1)$, $c$=ring$(k{+}1,j)$, $d$=ring$(k{+}1,j{+}1)$"),
     [("surface", "$\\vect{p}(\\theta,\\varphi)=\\tfrac12(\\sin\\theta\\cos\\varphi,\\ \\cos\\theta,\\ \\sin\\theta\\sin\\varphi)$, "
                  "$\\theta=\\pi k/K$ from the north pole, $\\varphi=2\\pi j/s$"),
      ("normal", "$\\vect{n}=2\\vect{p}$ (unit, shared by every triangle at the vertex: smooth)"),
      ("$V$", "$(K-1)s+2$: $K-1$ rings of $s$ vertices + 2 poles; the south half is the mirror image, the equator shared"),
      ("$T$", "$2s(K-1)$: two pole fans of $s$ + $(K-2)$ bands of $2s$"),
      low_row("sphere"), scene_row("sphere")],
     used_strip("Sphere", LIB["sphere"]["instances"]))

# ---- hemisphere -----------------------------------------------------------------------------------------------
s, K = LOWP["hemisphere"]["params"]["s"], LOWP["hemisphere"]["params"]["k"]
pole, ctr = K * s, K * s + 1
ha, hb, hc, hd = 0, 1, s, s + 1          # band between ring 1 (0..s-1) and ring 2 (s..2s-1)
cap = [(ctr, ctr + 1 + (j + 1) % s, ctr + 1 + j) for j in range(s)]
card("Hemisphere", "hemisphere", "Hemisphere", gallery("hemisphere"),
     draw_mesh("hemisphere", "Hemisphere", View(30, 20, 2.1), [pole, ctr] + list(range(s)) + [hc, hd], [(ha, hc, hd), (ha, hd, hb)],
               [pole, 2, ctr + 3], [axis_seam("hemisphere", 1, lambda q: abs(q[2]) < 1e-6 and q[0] > 1e-6)[:K + 1]],
               fans=cap, axes=0.8),
     cell_fig([("a", "%d" % ha), ("b", "%d" % hb), ("c", "%d" % hc), ("d", "%d" % hd)], [("a", "c", "d"), ("a", "d", "b")],
              "ring $k$ starts at $(k-1)s$; quad $(a,c,d)$, $(a,d,b)$ with $a=(k{-}1)s{+}j$, $c=a+s$; pole $=Ks$, base centre $=Ks{+}1$"),
     [("surface", "$\\vect{p}=(\\tfrac12\\sin\\theta\\cos\\varphi,\\ -\\tfrac12+\\cos\\theta,\\ -\\tfrac12\\sin\\theta\\sin\\varphi)$, "
                  "$\\theta=\\frac{\\pi}{2}k/K$, $\\varphi=2\\pi j/s$; base disc at $y=-\\frac12$"),
      ("normal", "$\\vect{n}\\propto(\\sin\\theta\\cos\\varphi/0.5,\\ \\cos\\theta,\\ -\\sin\\theta\\sin\\varphi/0.5)$ (half-ellipsoid, semi-axes 0.5, 1, 0.5); base $-\\vect{e}_y$"),
      ("$V$", "$s(K+1)+2$: $K$ rings + the base ring again with the flat normal + pole + base centre"),
      ("$T$", "$2sK$: $(K-1)$ bands of $2s$ + the pole fan + the base fan"),
      low_row("hemisphere"), scene_row("hemisphere")],
     used_strip("Hemisphere", LIB["hemisphere"]["instances"]))

# ---- capsule --------------------------------------------------------------------------------------------------
s, cst = LOWP["capsule"]["params"]["s"], LOWP["capsule"]["params"]["c"]
nr = 2 * cst
tp, bp = nr * s, nr * s + 1
ka, kb, kc, kd = (cst - 1) * s, (cst - 1) * s + 1, cst * s, cst * s + 1   # the cylindrical band between the two caps
card("Capsule", "capsule", "Capsule", gallery("capsule"),
     draw_mesh("capsule", "Capsule", View(30, 18, 2.1), [tp, bp] + list(range(s)) + [kc, kd], [(ka, kc, kd), (ka, kd, kb)],
               [tp, kc + 2, bp], [axis_seam("capsule", 2, lambda q: abs(q[1]) < 1e-6 and q[0] > -1e-6)], axes=0.78),
     cell_fig([("a", "%d" % ka), ("b", "%d" % kb), ("c", "%d" % kc), ("d", "%d" % kd)], [("a", "c", "d"), ("a", "d", "b")],
              "the same revolve pattern as the hemisphere; this cell is the straight band between the two caps"),
     [("surface", "rings about $z$: top cap $r=\\tfrac12\\sin\\theta$, $z=z_c+h\\cos\\theta$; bottom cap mirrored; "
                  "$z_c=\\tfrac12-h$, $\\theta=\\frac{\\pi}{2}k/c$"),
      ("normal", "$\\vect{n}\\propto(\\sin\\theta\\cos\\varphi/0.5,\\ \\sin\\theta\\sin\\varphi/0.5,\\ \\pm\\cos\\theta/h)$ (ellipsoidal caps)"),
      ("$V$", "$2cs+2$: $c$ rings per cap + 2 poles (no extra ring for the straight part)"),
      ("$T$", "$4cs$: $(2c-1)$ bands of $2s$ + 2 pole fans of $s$"),
      low_row("capsule"), scene_row("capsule")],
     used_strip("Capsule", LIB["capsule"]["instances"]))

# ---- cylinder / frustum / cone --------------------------------------------------------------------------------
for nm, title, lab in (("cylinder", "Cylinder", "cylinder"), ("frustum", "Frustum (light beam)", "frustum"), ("cone", "Cone", "cone")):
    p = LOWP[nm]
    s = p["params"]["s"]
    rb, rt, capped = p["params"]["rb"], p["params"]["rt"], p["params"]["capped"]
    labels = [2 * j for j in range(s)] + [1, 3]
    fans, fan_lab = [], []
    if capped:
        if rt > 0:
            tc = 2 * s
            fans += [(tc, tc + 1 + j, tc + 1 + (j + 1) % s) for j in range(s)]
            fan_lab.append(tc)
            bc = 3 * s + 1
        else:
            bc = 2 * s
        fans += [(bc, bc + 1 + (j + 1) % s, bc + 1 + j) for j in range(s)]
        fan_lab.append(bc)
    cell = [(0, 2, 1)] + ([(2, 3, 1)] if rt > 0 else [])
    cells = ([("a", "0"), ("b", "2"), ("c", "1"), ("d", "3")])
    seam = [[0, 1]]
    card(title, lab, EXPORT_KEY[nm], gallery(nm),
         draw_mesh(nm, EXPORT_KEY[nm], View(30, 25, 2.1), labels + fan_lab, cell, [2, 3 if rt > 0 else 2] + fan_lab[:1], seam, fans=fans,
                   arcs=[((0, 0, -0.5), (1, 0, 0), (0, 1, 0), max(rb, 0.3) * 1.25, 0.0, 2 * math.pi / s, "$\\varphi$")], axes=0.8),
         cell_fig(cells, [("a", "b", "c"), ("b", "d", "c")] if rt > 0 else [("a", "b", "c")],
                  ("bottom ring $b_j=2j$, top ring $t_j=2j{+}1$ (interleaved); triangles $(b_j,b_{j+1},t_j)$, $(b_{j+1},t_{j+1},t_j)$"
                   if rt > 0 else "$t_j$ all sit at the apex, so only $(b_j,b_{j+1},t_j)$ is emitted")),
         [("surface", "$\\vect{p}(\\varphi,z)=(r(z)\\cos\\varphi,\\ r(z)\\sin\\varphi,\\ z)$, $z\\in[-\\frac12,\\frac12]$, "
                      "$r(-\\frac12)=%g$, $r(\\frac12)=%g$; axis $z$" % (rb, rt)),
          ("normal", "side $\\vect{n}\\propto(\\cos\\varphi,\\sin\\varphi,r_b-r_t)$ (smooth around, shared by both rings); caps $\\pm\\vect{e}_z$ on their own vertices"),
          ("$V$", {"cylinder": "$4s+2$: $2s$ side + two caps of $s+1$ (centre + own ring, flat normal)",
                   "frustum": "$2s$: the side only (the light beam has no caps)",
                   "cone": "$3s+1$: $2s$ side (the top ring all at the apex) + the base cap $s+1$"}[nm]),
          ("$T$", {"cylinder": "$4s$: $2s$ side + $s$ per cap", "frustum": "$2s$: two per quad of the side",
                   "cone": "$2s$: $s$ side (one per slice) + $s$ base"}[nm]),
          low_row(nm), scene_row(nm)],
         used_strip(EXPORT_KEY[nm], LIB[nm]["instances"]))

# ---- torus ----------------------------------------------------------------------------------------------------
u, v = LOWP["torus"]["params"]["u"], LOWP["torus"]["params"]["v"]
at = lambda i, j: (i % u) * v + (j % v)  # noqa: E731
ta, tb, tc, td = at(0, 0), at(1, 0), at(0, 1), at(1, 1)
card("Torus", "torus", "Torus", gallery("torus"),
     draw_mesh("torus", "Torus", View(18, 28, 2.4), [at(0, j) for j in range(v)] + [tb, td], [(ta, tb, td), (ta, td, tc)],
               [at(0, 0), at(2, 3)], [[at(0, j) for j in range(v)] + [at(0, 0)], [at(i, 0) for i in range(u)] + [at(0, 0)]],
               arcs=[((0, 0, 0), (1, 0, 0), (0, 1, 0), 0.62, 0.0, 2 * math.pi / u, "$a$")], axes=0.75),
     cell_fig([("a", "%d" % ta), ("b", "%d" % tb), ("c", "%d" % tc), ("d", "%d" % td)], [("a", "b", "d"), ("a", "d", "c")],
              "at$(i,j)=(i\\bmod u)\\,v+(j\\bmod v)$; $(i,j)\\to(i{+}1,j)\\to(i{+}1,j{+}1)$, then $(i,j),(i{+}1,j{+}1),(i,j{+}1)$"),
     [("surface", "$\\vect{p}(a,b)=((R+r\\cos b)\\cos a,\\ (R+r\\cos b)\\sin a,\\ r\\sin b)$, $a=2\\pi i/u$, $b=2\\pi j/v$, "
                  "$R=%g$, $r=%g$" % (LOWP["torus"]["params"]["R"], LOWP["torus"]["params"]["r"])),
      ("normal", "$\\vect{n}=(\\cos b\\cos a,\\ \\cos b\\sin a,\\ \\sin b)$"),
      ("$V$", "$uv$: the grid wraps in both directions, so no seam vertices are repeated"),
      ("$T$", "$2uv$: two per grid cell"), low_row("torus"), scene_row("torus")],
     used_strip("Torus", LIB["torus"]["instances"]))

# ---- annulus --------------------------------------------------------------------------------------------------
n = LOWP["annulus"]["params"]["n"]
card("Annulus", "annulus", "Annulus", gallery("annulus"),
     draw_mesh("annulus", "Annulus", View(15, 25, 2.6), list(range(2 * n)), [(1, 0, 2), (1, 2, 3)], [0, 5], [[0, 1]],
               arcs=[((0, 0, 0), (1, 0, 0), (0, 1, 0), 0.7, 0.0, 2 * math.pi / n, "$\\varphi$")], axes=0.75),
     cell_fig([("a", "1"), ("b", "3"), ("c", "0"), ("d", "2")], [("a", "c", "d"), ("a", "d", "b")],
              "outer $o_j=2j$, inner $i_j=2j{+}1$; front $(i_j,o_j,o_{j+1})$, $(i_j,o_{j+1},i_{j+1})$; the back face repeats them reversed"),
     [("surface", "outer $(\\frac12\\cos\\varphi,\\frac12\\sin\\varphi,0)$, inner $(r_i\\cos\\varphi,r_i\\sin\\varphi,0)$, $\\varphi=2\\pi j/n$, $r_i=%g$" % LOWP["annulus"]["params"]["inner"]),
      ("normal", "$+\\vect{e}_z$ on the front copy, $-\\vect{e}_z$ on the back copy"),
      ("$V$", "$4n$: 2 rings of $n$, stored once per face (two faces, opposite normals)"),
      ("$T$", "$4n$: $2n$ per face"), low_row("annulus"), scene_row("annulus")],
     used_strip("Annulus", LIB["annulus"]["instances"]))

# ---- helix ----------------------------------------------------------------------------------------------------
hp = LOWP["helix"]["params"]
mm, nn = hp["m"], hp["segments"]
hat = lambda i, j: i * mm + (j % mm)  # noqa: E731
ea, eb, ec, ed = hat(0, 0), hat(0, 1), hat(1, 0), hat(1, 1)
end0 = (nn + 1) * mm
card("Helix", "helix", "Helix", gallery("helix"),
     draw_mesh("helix", "Helix", View(88, 12, 3.0), [0, 1, 2, ec, ed], [(ea, eb, ed), (ea, ed, ec)], [hat(4, 0), hat(8, 0)],
               [[hat(i, 0) for i in range(nn + 1)]], fans=[(end0, end0 + 1 + (j + 1) % mm, end0 + 1 + j) for j in range(mm)],
               axes=0.75, label_dist=0.11),
     cell_fig([("a", "%d" % ea), ("b", "%d" % eb), ("c", "%d" % ec), ("d", "%d" % ed)], [("a", "b", "d"), ("a", "d", "c")],
              "at$(i,j)=im+(j\\bmod m)$; $(i,j),(i,j{+}1),(i{+}1,j{+}1)$ and $(i,j),(i{+}1,j{+}1),(i{+}1,j)$"),
     [("surface", "centre $\\vect{c}(t)=(R\\cos2\\pi Nt,\\ R\\sin2\\pi Nt,\\ L(t-\\frac12))$, $L=1-2r$; "
                  "$\\vect{p}=\\vect{c}+r(\\vect{N}\\cos\\psi+\\vect{B}\\sin\\psi)$, $t=i/n$, $\\psi=2\\pi j/m$"),
      ("normal", "$\\vect{N}\\cos\\psi+\\vect{B}\\sin\\psi$ ($\\vect{N}$ towards the axis' outside, $\\vect{B}=\\vect{T}\\times\\vect{N}$); end discs $\\pm\\vect{T}$"),
      ("$V$", "$(n+1)m+2(m+1)$: $n+1$ rings of $m$ along the tube + two end discs (centre + own ring)"),
      ("$T$", "$2nm+2m$: two per tube cell + an $m$-fan per end"), low_row("helix"), scene_row("helix")],
     used_strip("Helix", LIB["helix"]["instances"]))

# ---- grass blade ----------------------------------------------------------------------------------------------
gb = LOWP["grass blade"]
card("Grass blade", "grassblade", "Grass blade", gallery("grass blade"),
     draw_mesh("grass blade", "Grass blade", View(20, 10, 2.4), list(range(7)), [(0, 1, 3), (0, 3, 2)], [0, 1, 6], [], axes=0.7),
     cell_fig([("a", "0"), ("b", "1"), ("c", "2"), ("d", "3")], [("a", "b", "d"), ("a", "d", "c")],
              "rows $(0,1)$, $(2,3)$, $(4,5)$ and the tip 6; each row pair is one quad, the tip one triangle"),
     [("surface", "three rows at $t=0,\\,0.38,\\,0.72$: $(\\pm0.625,\\ t-\\frac12,\\ 0)$, tip $(0,\\frac12,0)$"),
      ("normal", "folded: $(\\mp0.55,0,1)/\\sqrt{1.3025}$ at the edges, $+\\vect{e}_z$ at the tip"),
      ("$V$", "$2\\cdot3+1=7$"), ("$T$", "$2\\cdot2+1=5$"),
      ("scene", "never a scene part: the grass field draws it instanced on the GPU (not counted in the %s parts)" % fmt_int(EXP["leaf_count"]))],
     "")

# ---- terrain disc ---------------------------------------------------------------------------------------------
tp_ = LOWP["terrain disc"]["params"]
s, K = tp_["s"], tp_["k"]
ta_, tb_, tc_, td_ = ring_idx(s, 1, 0), ring_idx(s, 1, 1), ring_idx(s, 2, 0), ring_idx(s, 2, 1)
Pt = np.array(LOWP["terrain disc"]["positions"])
R0 = tp_["radius"]
card("Terrain disc", "terraindisc", "Terrain disc", gallery("terrain disc"),
     draw_mesh("terrain disc", "Terrain disc", View(30, 35, 2.3 / R0), [0] + [ring_idx(s, 1, j) for j in range(s)] + [tc_, td_],
               [(ta_, tb_, tc_), (tb_, td_, tc_)], [0, tc_], [[0] + [ring_idx(s, k, 0) for k in range(1, K + 1)]], axes=0.75 * R0,
               label_dist=0.18),
     cell_fig([("a", "%d" % ta_), ("b", "%d" % tb_), ("c", "%d" % tc_), ("d", "%d" % td_)], [("a", "b", "c"), ("b", "d", "c")],
              "the sphere's pattern on a disc: ring$(k,j)=1+(k-1)s+(j\\bmod s)$, centre 0"),
     [("surface", "$\\vect{p}=(\\rho\\cos\\varphi,\\ h(x,z),\\ \\rho\\sin\\varphi)$, $\\rho=R\\,k/K$, $\\varphi=2\\pi j/s$, "
                  "$R=%g$\\,m; $h$ = the terrain height function" % R0),
      ("normal", "the terrain's normal $\\vect{n}(x,z)$ from the height function"),
      ("$V$", "$1+Ks$: the centre + $K$ rings of $s$"),
      ("$T$", "$s(2K-1)-D$: the centre fan $s$ + $(K-1)$ bands of $2s$, minus the $D$ triangles dropped inside the entrance trench"),
      low_row("terrain disc"), scene_row("terrain disc")],
     "")

# ------------------------------------------------------------------------------------------------ gallery + camp
def crop_box(src, box, dst, maxw=2000):
    im = Image.open(src).convert("RGB").crop(tuple(box))
    if im.width > maxw:
        im = im.resize((maxw, int(im.height * maxw / im.width)), Image.LANCZOS)
    im.save(dst, quality=90)


pj = json.load(open(os.path.join(RAW, "prims", "prims.json")))["primitives"]
cells = []
for e in sorted(pj, key=lambda e: e["number"]):
    path = gallery(e["name"])
    key = "Cuboid" if e["name"] == "cuboid" else EXPORT_KEY[e["name"]]
    cells.append("\\begin{tabular}[t]{@{}c@{}}\\img{%s}{1.95cm}\\\\[-0.6mm]{\\fontsize{7}{8}\\selectfont %d\\,\\primchip{%s}}\\end{tabular}"
                 % (path, e["number"], primkey.tex_key(key)))
gallery_tex = "\\noindent " + "\\hfill".join(cells[:8]) + "\\\\[1mm]\n\\noindent " + "\\hfill".join(cells[8:])
cj = json.load(open(os.path.join(RAW, "shapeview", "camp.json")))
crop_box(os.path.join(RAW, "shapeview", cj["prims"]), cj["crop_prims"], os.path.join(IMG, "camp_prims.jpg"))
camp_key = "\\ ".join("\\primchipn{%s}{%s}" % (primkey.tex_key(k), fmt_int(cj["legend"][k])) for k in primkey.ORDER if k in cj["legend"])

# ------------------------------------------------------------------------------------------------ output
o = ["% generated by tools/report/gen_prim_cards.py: do not edit"]
o.append("\\newcommand{\\primgridkey}{%s}" % gallery_tex)
o.append("\\newcommand{\\campkey}{%s}" % camp_key)
o.append("\\newcommand{\\campparts}{%s}\\newcommand{\\campinst}{%d}" % (fmt_int(cj["parts"]), cj["instances"]))
# The two small cards without a "used for" strip share one float (two captions), so no card stands alone on a page.
last = cards.pop()
cards[-1] = cards[-1].replace("\\end{figure}", "\\vspace{3mm}\n" + last.replace("\\begin{figure}[tbp]\\centering\n", ""), 1)
cards[-1] = cards[-1].rsplit("\\end{figure}", 1)[0] + "\\end{figure}"
o.append("\\newcommand{\\primcards}{%")
o.append("\n\n".join(cards))
o.append("}")
open(OUT, "w").write("\n".join(o) + "\n")
print("wrote", OUT, len(cards), "cards")
