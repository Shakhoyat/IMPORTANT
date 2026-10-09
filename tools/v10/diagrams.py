"""v10 M4: explanatory diagrams, one per Part A / Part B concept: the maths of a section drawn simply.
These are teaching drawings (geometry of the method), not measurements; every constant that comes from the code is named
next to it (e.g. the spot cone, the shader's blackbody ramp, the particle integrators of scene/Combat.cpp).
Run: ~/.local/bin/uv run -q --with matplotlib --with numpy python tools/v10/diagrams.py   -> report/figures/diagrams/*.pdf"""
import math
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Arc, FancyArrowPatch, Polygon, Rectangle, Circle

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "report", "figures", "diagrams")
os.makedirs(OUT, exist_ok=True)
plt.rcParams.update({
    "font.family": "serif", "font.serif": ["Times New Roman", "Times", "Nimbus Roman", "DejaVu Serif"],
    "mathtext.fontset": "stix", "font.size": 9, "axes.titlesize": 9, "legend.fontsize": 8,
    "figure.dpi": 150, "savefig.bbox": "tight", "figure.constrained_layout.use": True,
})
C = {"blue": "#1f4e79", "red": "#c0504d", "green": "#4f8a3a", "orange": "#d08c1a", "purple": "#7a4f9a",
     "grey": "#7f7f7f", "light": "#dce6f1", "teal": "#2e8b8b"}


def save(fig, name):
    fig.savefig(os.path.join(OUT, name + ".pdf"))
    plt.close(fig)
    print("wrote", name)


def arrow(ax, a, b, col="k", lw=1.4, ms=10, ls="-", z=3):
    ax.add_patch(FancyArrowPatch(a, b, arrowstyle="-|>", mutation_scale=ms, color=col, lw=lw, linestyle=ls, zorder=z))


def label(ax, xy, text, col="k", **kw):
    kw.setdefault("ha", "center")
    kw.setdefault("va", "center")
    ax.text(xy[0], xy[1], text, color=col, fontsize=kw.pop("fs", 9), zorder=6,
            bbox=dict(facecolor="white", edgecolor="none", pad=0.6, alpha=0.9), **kw)


def clean(ax, lim=None):
    ax.set_aspect("equal")
    ax.axis("off")
    if lim:
        ax.set_xlim(lim[0], lim[1])
        ax.set_ylim(lim[2], lim[3])


# ---------------------------------------------------------------------------------------------------- A02 TRS order
def trs():
    sq = np.array([[-0.5, -0.5], [0.5, -0.5], [0.5, 0.5], [-0.5, 0.5]])
    S = np.diag([1.6, 0.6])
    a = math.radians(35)
    R = np.array([[math.cos(a), -math.sin(a)], [math.sin(a), math.cos(a)]])
    t = np.array([1.8, 1.0])
    stages = [("unit square", sq), ("$S$: scale", sq @ S.T), ("$RS$: rotate", sq @ S.T @ R.T),
              ("$TRS$: translate", sq @ S.T @ R.T + t)]
    wrong = (sq + t) @ S.T @ R.T  # S R T applied in the wrong order: translate first
    fig, ax = plt.subplots(1, 5, figsize=(7.0, 1.9))
    for i, (name, P) in enumerate(stages):
        a_ = ax[i]
        a_.axhline(0, color="0.8", lw=0.8)
        a_.axvline(0, color="0.8", lw=0.8)
        a_.add_patch(Polygon(P, closed=True, fc=C["light"], ec=C["blue"], lw=1.4))
        a_.plot(*P[0], "o", color=C["red"], ms=3.5, zorder=5)
        a_.set_title(name)
        clean(a_, (-1.4, 3.3, -1.3, 3.0))
    a_ = ax[4]
    a_.axhline(0, color="0.8", lw=0.8)
    a_.axvline(0, color="0.8", lw=0.8)
    a_.add_patch(Polygon(stages[3][1], closed=True, fc="none", ec=C["blue"], lw=1.0, ls="--"))
    a_.add_patch(Polygon(wrong, closed=True, fc="#f3d9d8", ec=C["red"], lw=1.4))
    a_.set_title("$SRT$: wrong order")
    clean(a_, (-1.4, 3.3, -1.3, 3.0))
    save(fig, "d_trs_order")


# ------------------------------------------------------------------------------------------------- A03 hierarchy
def hierarchy():
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.4))
    for k, (a1, a2, title) in enumerate([(20, 30, "shoulder $20^\\circ$, elbow $30^\\circ$"),
                                          (55, 30, "shoulder $55^\\circ$ (elbow unchanged)")]):
        a_ = ax[k]
        base = np.array([0.0, 0.0])
        p1 = base + 1.6 * np.array([math.cos(math.radians(a1)), math.sin(math.radians(a1))])
        p2 = p1 + 1.2 * np.array([math.cos(math.radians(a1 + a2)), math.sin(math.radians(a1 + a2))])
        a_.plot([base[0], p1[0]], [base[1], p1[1]], color=C["blue"], lw=5, solid_capstyle="round")
        a_.plot([p1[0], p2[0]], [p1[1], p2[1]], color=C["orange"], lw=5, solid_capstyle="round")
        for p, nm in [(base, "shoulder"), (p1, "elbow")]:
            a_.add_patch(Circle(p, 0.09, fc="white", ec="k", lw=1.2, zorder=5))
        a_.plot(*p2, "o", color=C["red"], ms=5, zorder=6)
        label(a_, base + (-0.05, -0.32), "parent frame $W_0$", fs=8)
        label(a_, p1 + (0.55, -0.25), "$W_1=W_0M_1$", fs=8)
        label(a_, p2 + (0.78, 0.0), "$W_2=W_0M_1M_2$", fs=8)
        a_.set_title(title)
        clean(a_, (-0.8, 3.8, -0.6, 3.0))
    save(fig, "d_hierarchy")


# ------------------------------------------------------------------------------------------- A04 camera basis
def view_basis():
    fig, ax = plt.subplots(figsize=(3.8, 2.6))
    eye, tgt = np.array([0.8, 2.2]), np.array([3.4, 0.6])
    ax.add_patch(Polygon([(2.8, 0.0), (4.1, 0.0), (4.1, 0.9), (2.8, 0.9)], fc="#e8e8e8", ec="0.5"))
    label(ax, (3.45, 0.2), "scene", "0.3", fs=8)
    d = tgt - eye
    n = -d / np.linalg.norm(d)
    v = np.array([-n[1], n[0]])
    if v[1] > 0:
        v = -v
    arrow(ax, eye, eye + 1.0 * n, C["red"])
    arrow(ax, eye, eye + 0.9 * v, C["green"])
    ax.plot([eye[0], tgt[0]], [eye[1], tgt[1]], color="0.4", lw=1.0, ls="--")
    ax.plot(*tgt, "o", color="k", ms=4)
    ax.plot(*eye, marker="o", ms=11, mfc="white", mec=C["blue"], mew=1.4, zorder=5)
    ax.plot(*eye, "o", color=C["blue"], ms=3, zorder=6)
    label(ax, eye + (0.45, 0.3), "eye $\\mathbf{e}$")
    label(ax, tgt + (1.25, 0.0), "target $\\mathbf{a}$")
    label(ax, eye + 1.0 * n + (-0.1, 0.38), "$\\mathbf{n}=\\frac{\\mathbf{e}-\\mathbf{a}}{|\\mathbf{e}-\\mathbf{a}|}$", C["red"])
    label(ax, eye + 0.9 * v + (-0.15, -0.25), "$\\mathbf{v}=\\mathbf{n}\\times\\mathbf{u}$", C["green"])
    label(ax, (1.95, 1.2), "$\\mathbf{u}$ out of the page ($\\odot$)", C["blue"], fs=8)
    label(ax, (2.6, 1.75), "camera looks along $-\\mathbf{n}$", "0.3", fs=8)
    clean(ax, (-0.7, 5.2, -0.2, 3.3))
    save(fig, "d_view_basis")


# ---------------------------------------------------------------------------------------------- A05 projections
def projections():
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.3))
    objs = [(2.0, 0.6), (3.6, 0.6)]  # (z distance, half height): two equal posts
    for k, title in enumerate(["perspective: rays meet at the eye", "orthographic: parallel rays"]):
        a_ = ax[k]
        a_.plot([1.0, 1.0], [-1.0, 1.0], color="k", lw=2)
        label(a_, (1.0, -1.25), "image plane", fs=8)
        for z, h in objs:
            a_.plot([z, z], [-h, h], color=C["blue"], lw=4)
            for y in (-h, h):
                if k == 0:
                    a_.plot([0, z], [0, y], color=C["orange"], lw=0.9)
                    yi = y / z * 1.0
                    a_.plot(1.0, yi, "o", color=C["red"], ms=3.5)
                else:
                    a_.plot([0.2, z], [y, y], color=C["orange"], lw=0.9)
                    a_.plot(1.0, y, "o", color=C["red"], ms=3.5)
        if k == 0:
            a_.plot(0, 0, "o", color="k", ms=5)
            label(a_, (0.0, 0.32), "eye", fs=8)
            label(a_, (2.9, 1.05), "far post looks smaller: $y'=y\\,d/z$", fs=8)
        else:
            label(a_, (2.6, 1.05), "both posts keep their size: $y'=y$", fs=8)
        a_.set_title(title)
        clean(a_, (-0.3, 4.2, -1.45, 1.35))
    save(fig, "d_projections")


# -------------------------------------------------------------------------------------------- A06 frustum -> NDC
def frustum_ndc():
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.5), gridspec_kw=dict(width_ratios=[1.5, 1]))
    a_ = ax[0]
    n, f, t = 1.0, 4.0, math.tan(math.radians(25))
    poly = [(n, -n * t), (f, -f * t), (f, f * t), (n, n * t)]
    a_.add_patch(Polygon(poly, fc=C["light"], ec=C["blue"], lw=1.4))
    a_.plot([0, f], [0, f * t], color="0.6", lw=0.8, ls="--")
    a_.plot([0, f], [0, -f * t], color="0.6", lw=0.8, ls="--")
    a_.plot(0, 0, "o", color="k", ms=5)
    label(a_, (0, 0.3), "eye", fs=8)
    label(a_, (n, -n * t - 0.3), "near $N$", fs=8)
    label(a_, (f, -f * t - 0.3), "far $F$", fs=8)
    for z in (1.4, 2.4, 3.4):
        a_.plot([z, z], [-z * t, z * t], color=C["orange"], lw=0.8)
    label(a_, (2.45, 0.0), "view frustum\n(eye space)", fs=8)
    a_.set_title("equal steps in depth ...")
    clean(a_, (-0.3, 4.5, -2.3, 2.1))
    b = ax[1]
    b.add_patch(Rectangle((-1, -1), 2, 2, fc=C["light"], ec=C["blue"], lw=1.4))
    zs = [1.4, 2.4, 3.4]
    for z in zs:
        zn = (f + n) / (f - n) - 2 * f * n / ((f - n) * z)
        b.plot([zn, zn], [-1, 1], color=C["orange"], lw=0.8)
    label(b, (0, -1.3), "$z_{ndc}=\\frac{F+N}{F-N}-\\frac{2FN}{(F-N)\\,d}$", fs=8)
    label(b, (-1.0, 1.25), "$-1$", fs=8)
    label(b, (1.0, 1.25), "$+1$", fs=8)
    b.set_title("... crowd towards $z_{ndc}=1$ (cube)")
    clean(b, (-1.6, 1.6, -1.65, 1.5))
    arrow(fig.axes[0], (4.25, 0.0), (4.5, 0.0), "k")
    save(fig, "d_frustum_ndc")


# ---------------------------------------------------------------------------------------------- A07 clipping
def clipping():
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.6))
    a_ = ax[0]
    xmin, xmax, ymin, ymax = 0, 4, 0, 3
    codes = {(-1, 1): "1001", (0, 1): "1000", (1, 1): "1010", (-1, 0): "0001", (0, 0): "0000", (1, 0): "0010",
             (-1, -1): "0101", (0, -1): "0100", (1, -1): "0110"}
    xs = {-1: -1.5, 0: 2.0, 1: 5.5}
    ys = {-1: -1.2, 0: 1.5, 1: 4.2}
    a_.add_patch(Rectangle((xmin, ymin), 4, 3, fc=C["light"], ec=C["blue"], lw=1.6))
    for (i, j), code in codes.items():
        label(a_, (xs[i], ys[j]), code, "0.35" if code != "0000" else C["blue"], fs=8)
    for v in (xmin, xmax):
        a_.axvline(v, color="0.6", lw=0.8, ls=":")
    for v in (ymin, ymax):
        a_.axhline(v, color="0.6", lw=0.8, ls=":")
    p0, p1 = np.array([-1.2, 0.6]), np.array([5.0, 3.6])
    a_.plot(*zip(p0, p1), color=C["red"], lw=1.0, ls="--")
    # clipped segment (Liang-Barsky)
    d = p1 - p0
    ts = [0.0, 1.0]
    for p, q in [(-d[0], p0[0] - xmin), (d[0], xmax - p0[0]), (-d[1], p0[1] - ymin), (d[1], ymax - p0[1])]:
        r = q / p
        if p < 0:
            ts[0] = max(ts[0], r)
        else:
            ts[1] = min(ts[1], r)
    c0, c1 = p0 + ts[0] * d, p0 + ts[1] * d
    a_.plot(*zip(c0, c1), color=C["red"], lw=2.4)
    a_.plot(*c0, "o", color=C["red"], ms=4)
    a_.plot(*c1, "o", color=C["red"], ms=4)
    label(a_, p0 + (0.15, -0.5), "end code 0001", C["red"], fs=7)
    label(a_, p1 + (0.2, -0.75), "end code 1010", C["red"], fs=7)
    a_.set_title("Cohen–Sutherland codes; Liang–Barsky $t\\in[t_0,t_1]$")
    clean(a_, (-2.3, 6.3, -1.9, 4.9))
    b = ax[1]
    tri = np.array([[-0.8, 0.5], [3.0, -0.9], [2.4, 3.6]])
    b.add_patch(Rectangle((0, 0), 3, 3, fc=C["light"], ec=C["blue"], lw=1.6))
    b.add_patch(Polygon(tri, fc="none", ec=C["red"], lw=1.0, ls="--"))

    def clip(poly, inside, inter):
        out = []
        for i in range(len(poly)):
            s, e = poly[i - 1], poly[i]
            if inside(e):
                if not inside(s):
                    out.append(inter(s, e))
                out.append(e)
            elif inside(s):
                out.append(inter(s, e))
        return out

    def ix(v):
        return lambda s, e: s + (e - s) * ((v - s[0]) / (e[0] - s[0]))

    def iy(v):
        return lambda s, e: s + (e - s) * ((v - s[1]) / (e[1] - s[1]))

    P = list(tri)
    P = clip(P, lambda p: p[0] >= 0, ix(0))
    P = clip(P, lambda p: p[0] <= 3, ix(3))
    P = clip(P, lambda p: p[1] >= 0, iy(0))
    P = clip(P, lambda p: p[1] <= 3, iy(3))
    b.add_patch(Polygon(np.array(P), fc="#f3d9d8", ec=C["red"], lw=1.8))
    for k, p in enumerate(P):
        b.plot(*p, "o", color=C["red"], ms=3.5)
    label(b, (1.5, -1.25), f"triangle clipped edge by edge: {len(P)} vertices", fs=8)
    b.set_title("Sutherland–Hodgman")
    clean(b, (-1.2, 4.0, -1.6, 3.9))
    save(fig, "d_clipping")


# ------------------------------------------------------------------------------------------- A08 back faces / z
def hsr():
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.4))
    a_ = ax[0]
    poly = np.array([[1.2, 0.0], [0.6, 1.04], [-0.6, 1.04], [-1.2, 0.0], [-0.6, -1.04], [0.6, -1.04]])
    eye = np.array([-3.4, 0.2])
    for i in range(6):
        p, q = poly[i], poly[(i + 1) % 6]
        m = (p + q) / 2
        nrm = np.array([q[1] - p[1], -(q[0] - p[0])])
        nrm = nrm / np.linalg.norm(nrm)
        front = np.dot(nrm, eye - m) > 0
        a_.plot(*zip(p, q), color=C["blue"] if front else "0.7", lw=3 if front else 1.5, ls="-" if front else "--")
        arrow(a_, m, m + 0.45 * nrm, C["green"] if front else "0.6", lw=1.0, ms=7)
    a_.plot(*eye, "o", color="k", ms=5)
    label(a_, eye + (0, 0.35), "eye", fs=8)
    label(a_, (0.0, -1.55), "solid: $\\mathbf{N}\\cdot(\\mathbf{e}-\\mathbf{p})>0$ kept; dashed: culled", fs=8)
    a_.set_title("back-face culling")
    clean(a_, (-3.9, 2.1, -1.9, 1.6))
    b = ax[1]
    b.set_title("Z-buffer: per pixel the nearest wins")
    cols = [C["light"], "#f3d9d8"]
    for k, (z, name, col) in enumerate([(7.0, "wall, $z=7$", C["light"]), (3.0, "crate, $z=3$", "#f3d9d8")]):
        x0 = 0.2 + 1.3 * k
        b.add_patch(Rectangle((x0, 0.3 + 0.5 * k), 2.2, 1.3, fc=col, ec="0.3", lw=1.0))
        label(b, (x0 + 1.1, 0.3 + 0.5 * k + 1.5), name, fs=8)
    for k, (zb, res) in enumerate([(7.0, "7 (wall)"), (3.0, "3 (crate)")]):
        label(b, (5.2, 1.5 - 0.6 * k), f"pixel {k + 1}: $Z$ = {res}", fs=8)
    for k, xy in enumerate([(0.7, 0.65), (2.0, 1.25)]):
        b.plot(*xy, "s", color="k", ms=4, zorder=6)
        label(b, (xy[0] + 0.25, xy[1] + 0.05), str(k + 1), fs=8, ha="left")
    label(b, (5.2, 0.3), "write iff $z<Z[x,y]$", fs=8)
    clean(b, (0.0, 6.6, 0.0, 2.6))
    save(fig, "d_hsr")


# --------------------------------------------------------------------------- A09/A12 raster + colour interpolation
def raster():
    a, b, c = np.array([1.2, 0.8]), np.array([8.6, 2.2]), np.array([3.6, 6.6])
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.9))

    def edge(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])

    area = edge(a, b, c)
    a_ = ax[0]
    for i in range(10):
        for j in range(8):
            pc = np.array([i + 0.5, j + 0.5])
            inside = edge(a, b, pc) >= 0 and edge(b, c, pc) >= 0 and edge(c, a, pc) >= 0
            a_.add_patch(Rectangle((i, j), 1, 1, fc=C["light"] if inside else "white", ec="0.75", lw=0.5))
            a_.plot(*pc, "o", ms=2.2, color=C["blue"] if inside else "0.7")
    a_.add_patch(Polygon([a, b, c], fc="none", ec=C["red"], lw=1.6))
    for p, nm, off in [(a, "$\\mathbf{a}$", (-0.45, -0.3)), (b, "$\\mathbf{b}$", (0.45, -0.2)), (c, "$\\mathbf{c}$", (-0.2, 0.42))]:
        label(a_, p + off, nm, C["red"])
    label(a_, (5.0, -0.75), "pixel centre $\\mathbf{p}$ is inside iff $E_{ab},E_{bc},E_{ca}$ have the same sign", fs=8)
    label(a_, (5.0, -1.45), "$E_{ab}(\\mathbf{p})=(\\mathbf{p}-\\mathbf{a})\\times(\\mathbf{b}-\\mathbf{a})$", fs=8)
    a_.set_title("edge functions: which pixels are covered")
    clean(a_, (-0.6, 10.6, -1.9, 8.4))
    b_ = ax[1]
    n = 240
    xs, ys = np.meshgrid(np.linspace(0, 10, n), np.linspace(0, 8, n))
    P = np.stack([xs, ys], -1)
    la = edge(b, c, P.T).T / area
    lb = edge(c, a, P.T).T / area
    lc = 1 - la - lb
    img = np.ones((n, n, 3))
    inside = (la >= 0) & (lb >= 0) & (lc >= 0)
    cols = np.array([[0.85, 0.15, 0.12], [0.15, 0.6, 0.2], [0.15, 0.3, 0.85]])
    mix = la[..., None] * cols[0] + lb[..., None] * cols[1] + lc[..., None] * cols[2]
    img[inside] = mix[inside]
    b_.imshow(img, origin="lower", extent=(0, 10, 0, 8), interpolation="bilinear")
    p = np.array([4.4, 3.0])
    for q, r in [(b, c), (c, a), (a, b)]:
        b_.plot(*zip(p, q), color="white", lw=0.9)
    b_.plot(*p, "o", color="k", ms=4)
    L = [edge(b, c, p) / area, edge(c, a, p) / area]
    L.append(1 - L[0] - L[1])
    label(b_, p + (0.0, -0.5), "$\\mathbf{p}$", fs=8)
    for v, nm, off, col in [(a, "$\\mathbf{a}$ red", (-0.6, -0.5), cols[0]), (b, "$\\mathbf{b}$ green", (0.1, -0.55), cols[1]),
                            (c, "$\\mathbf{c}$ blue", (0.0, 0.45), cols[2])]:
        label(b_, v + off, nm, col, fs=8)
    label(b_, (5.0, -0.75), f"$\\lambda_a,\\lambda_b,\\lambda_c$ = areas opposite each vertex / total = "
          f"{L[0]:.2f}, {L[1]:.2f}, {L[2]:.2f}", fs=8)
    label(b_, (5.0, -1.45), "colour$(\\mathbf{p})=\\lambda_a\\mathbf{c}_a+\\lambda_b\\mathbf{c}_b+\\lambda_c\\mathbf{c}_c$  (same weights for $z$, $\\mathbf{N}$, $uv$)", fs=8)
    b_.set_title("barycentric weights: colour interpolation")
    clean(b_, (-0.6, 10.6, -1.9, 8.4))
    save(fig, "d_raster")


# --------------------------------------------------------------------------------------------- A10 lighting vectors
def lighting():
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.5), gridspec_kw=dict(width_ratios=[1.25, 1]))
    a_ = ax[0]
    xs = np.linspace(-2.2, 2.2, 100)
    a_.fill_between(xs, -0.6, -0.04 * xs ** 2, color="#e8e8e8")
    a_.plot(xs, -0.04 * xs ** 2, color="0.3", lw=1.4)
    p = np.array([0.0, 0.0])
    N = np.array([0, 1.0])
    L = np.array([-math.sin(math.radians(40)), math.cos(math.radians(40))])
    V = np.array([math.sin(math.radians(60)), math.cos(math.radians(60))])
    H = (L + V) / np.linalg.norm(L + V)
    R = 2 * np.dot(N, L) * N - L
    for vec, nm, col, k, off in [(N, "$\\mathbf{N}$", "k", 1.5, (0.0, 0.18)), (L, "$\\mathbf{L}$ (to light)", C["orange"], 1.5, (-0.5, 0.12)),
                                 (V, "$\\mathbf{V}$ (to eye)", C["blue"], 1.5, (0.55, 0.05)), (H, "$\\mathbf{H}$", C["green"], 1.25, (0.18, 0.12)),
                                 (R, "$\\mathbf{R}$", C["red"], 1.35, (0.22, -0.02))]:
        arrow(a_, p, p + k * vec, col)
        label(a_, p + k * vec + off, nm, col, fs=8)
    a_.add_patch(Arc(p, 1.0, 1.0, theta1=90, theta2=130, color=C["orange"], lw=1.0))
    label(a_, (-0.2, 0.68), "$\\theta$", C["orange"], fs=8)
    label(a_, (0.0, -0.38), "$\\mathbf{H}=\\frac{\\mathbf{L}+\\mathbf{V}}{|\\mathbf{L}+\\mathbf{V}|}$,  $\\mathbf{R}=2(\\mathbf{N}\\cdot\\mathbf{L})\\mathbf{N}-\\mathbf{L}$", fs=8)
    a_.set_title("the five vectors at a surface point")
    clean(a_, (-2.4, 2.4, -0.6, 1.9))
    b_ = ax[1]
    b_.set_aspect("auto")
    th = np.linspace(0, 90, 200)
    b_.plot(th, np.cos(np.radians(th)), color=C["orange"], lw=1.6, label="diffuse $\\max(\\mathbf{N}\\cdot\\mathbf{L},0)=\\cos\\theta$")
    for s, ls in [(8, "--"), (64, "-")]:
        b_.plot(th, np.cos(np.radians(th)) ** s, color=C["green"], lw=1.3, ls=ls, label=f"specular $(\\mathbf{{N}}\\cdot\\mathbf{{H}})^{{{s}}}$")
    b_.set_xlabel("angle to $\\mathbf{N}$ (degrees)")
    b_.set_ylabel("intensity")
    b_.set_xlim(0, 90)
    b_.set_ylim(0, 1.6)
    b_.set_yticks([0, 0.25, 0.5, 0.75, 1.0])
    b_.grid(alpha=0.3)
    b_.legend(loc="upper right", frameon=True, fontsize=7.5)
    b_.set_title("how fast each term falls off")
    save(fig, "d_lighting")


# ----------------------------------------------------------------------------------------- A11 texture mapping / mip
def texture():
    fig, ax = plt.subplots(1, 3, figsize=(7.0, 2.3), gridspec_kw=dict(width_ratios=[1, 1.25, 1.35]))
    a_ = ax[0]
    for i in range(8):
        for j in range(8):
            a_.add_patch(Rectangle((i / 8, j / 8), 1 / 8, 1 / 8, fc="0.25" if (i + j) % 2 else "0.92", ec="none"))
    uv = np.array([[0.1, 0.1], [0.9, 0.2], [0.4, 0.9]])
    a_.add_patch(Polygon(uv, fc="none", ec=C["red"], lw=1.6))
    for q, nm, off in zip(uv, ["$(u_a,v_a)$", "$(u_b,v_b)$", "$(u_c,v_c)$"], [(0.0, -0.1), (0.0, -0.1), (0.0, 0.09)]):
        a_.plot(*q, "o", color=C["red"], ms=3.5)
        label(a_, q + off, nm, C["red"], fs=7.5)
    a_.set_title("texture space $(u,v)$")
    clean(a_, (-0.1, 1.1, -0.15, 1.1))
    b_ = ax[1]
    scr = np.array([[0.2, 0.2], [2.4, 0.5], [0.9, 1.8]])
    M = np.linalg.solve(np.c_[uv, np.ones(3)], np.c_[scr, np.ones(3)])  # affine uv -> screen
    for i in range(8):
        for j in range(8):
            sq = np.array([[i, j], [i + 1, j], [i + 1, j + 1], [i, j + 1]]) / 8
            sp = (np.c_[sq, np.ones(4)] @ M)[:, :2]
            b_.add_patch(Polygon(sp, fc="0.25" if (i + j) % 2 else "0.92", ec="none", alpha=0.25))
    from matplotlib.path import Path
    tri = Polygon(scr, fc="none", ec=C["red"], lw=1.6)
    b_.add_patch(tri)
    for i in range(8):
        for j in range(8):
            sq = np.array([[i, j], [i + 1, j], [i + 1, j + 1], [i, j + 1]]) / 8
            sp = (np.c_[sq, np.ones(4)] @ M)[:, :2]
            pg = Polygon(sp, fc="0.25" if (i + j) % 2 else "0.92", ec="none")
            b_.add_patch(pg)
            pg.set_clip_path(tri)
    label(b_, (1.3, -0.25), "$uv$ interpolated with $\\lambda$ (perspective-correct)", fs=7.5)
    b_.set_title("on screen")
    clean(b_, (0.0, 2.6, -0.45, 2.0))
    c_ = ax[2]
    x = 0.0
    lev = np.array([[0.92 if ((i // 4 + j // 4) % 2 == 0) else 0.25 for i in range(16)] for j in range(16)])
    for k, sz in enumerate([1.6, 0.8, 0.4, 0.2, 0.1]):
        c_.imshow(lev, cmap="gray", vmin=0, vmax=1, extent=(x, x + sz, 0, sz), interpolation="nearest")
        c_.add_patch(Rectangle((x, 0), sz, sz, fc="none", ec="0.3", lw=0.6))
        label(c_, (x + sz / 2, -0.15), f"{k}", fs=7.5)
        x += sz + 0.08
        if lev.shape[0] > 1:
            lev = lev.reshape(lev.shape[0] // 2, 2, lev.shape[1] // 2, 2).mean(axis=(1, 3))
    label(c_, (1.45, 1.85), "level $\\lambda=\\log_2\\rho$; $\\rho$ = texels under one pixel", fs=7.5)
    label(c_, (1.45, -0.42), "each level: half the size, 2$\\times$2 average; fine detail turns grey", fs=7.5)
    c_.set_title("mip pyramid (trilinear between levels)")
    clean(c_, (-0.05, 3.35, -0.6, 2.05))
    save(fig, "d_texture")


# --------------------------------------------------------------------------------------------- A13 ray tracing
def raytrace():
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.7))
    a_ = ax[0]
    o, d = np.array([-2.6, -0.6]), np.array([1.0, 0.28])
    d = d / np.linalg.norm(d)
    cc, r = np.array([1.2, 0.3]), 1.1
    a_.add_patch(Circle(cc, r, fc=C["light"], ec=C["blue"], lw=1.4))
    oc = o - cc
    bq = np.dot(oc, d)
    cq = np.dot(oc, oc) - r * r
    disc = bq * bq - cq
    t0, t1 = -bq - math.sqrt(disc), -bq + math.sqrt(disc)
    p0, p1 = o + t0 * d, o + t1 * d
    arrow(a_, o, o + 0.9 * d, "k")
    a_.plot(*zip(o, o + (t1 + 1.0) * d), color="k", lw=0.9, ls="--")
    a_.plot(*o, "o", color="k", ms=4)
    for q, nm, off in [(p0, "$t_0$", (-0.1, 0.3)), (p1, "$t_1$", (0.12, 0.3))]:
        a_.plot(*q, "o", color=C["red"], ms=4.5, zorder=6)
        label(a_, q + off, nm, C["red"], fs=8)
    Nn = (p0 - cc) / r
    arrow(a_, p0, p0 + 0.75 * Nn, C["green"], lw=1.2)
    label(a_, p0 + 0.85 * Nn + (-0.15, 0.1), "$\\mathbf{N}$", C["green"], fs=8)
    a_.plot(*cc, "+", color=C["blue"], ms=6)
    label(a_, cc + (0.2, -0.25), "$\\mathbf{c}$", C["blue"], fs=8)
    label(a_, o + (0.0, -0.32), "$\\mathbf{o}$", fs=8)
    label(a_, (-0.3, -1.45), "$\\mathbf{p}(t)=\\mathbf{o}+t\\mathbf{d}$,  $|\\mathbf{p}-\\mathbf{c}|=r$", fs=8)
    label(a_, (-0.3, -1.95), "$t=-b\\pm\\sqrt{b^2-c}$,  $b=\\mathbf{d}\\cdot(\\mathbf{o}-\\mathbf{c})$, $c=|\\mathbf{o}-\\mathbf{c}|^2-r^2$", fs=8)
    a_.set_title("ray–sphere: nearest root $t_0$ is the hit")
    clean(a_, (-3.0, 2.8, -2.25, 1.75))
    b_ = ax[1]
    b_.add_patch(Rectangle((-2.2, -1.3), 4.4, 1.3, fc="#dfeef6", ec="none"))
    b_.plot([-2.2, 2.2], [0, 0], color="0.3", lw=1.2)
    th1 = math.radians(50)
    I = np.array([math.sin(th1), -math.cos(th1)])
    inc0 = -1.5 * I
    arrow(b_, inc0, (0, 0), "k")
    Rv = I - 2 * np.dot(I, [0, 1]) * np.array([0, 1])
    arrow(b_, (0, 0), 1.5 * Rv, C["red"])
    th2 = math.asin(math.sin(th1) / 1.5)
    T = np.array([math.sin(th2), -math.cos(th2)])
    arrow(b_, (0, 0), 1.2 * T, C["blue"])
    b_.plot([0, 0], [-1.25, 1.35], color="0.5", lw=0.8, ls=":")
    label(b_, (0.0, 1.5), "$\\mathbf{N}$", fs=8)
    label(b_, inc0 + (-0.1, 0.18), "$\\mathbf{I}$", fs=8)
    label(b_, 1.5 * Rv + (0.2, 0.15), "$\\mathbf{R}=\\mathbf{I}-2(\\mathbf{N}\\cdot\\mathbf{I})\\mathbf{N}$", C["red"], fs=8)
    label(b_, 1.2 * T + (0.75, 0.05), "$\\mathbf{T}$: $n_1\\sin\\theta_1=n_2\\sin\\theta_2$", C["blue"], fs=8)
    b_.add_patch(Arc((0, 0), 0.9, 0.9, theta1=90, theta2=90 + 50, color="k", lw=0.9))
    b_.add_patch(Arc((0, 0), 0.9, 0.9, theta1=270, theta2=270 + math.degrees(th2), color=C["blue"], lw=0.9))
    label(b_, (-0.2, 0.6), "$\\theta_1$", fs=8)
    label(b_, (0.18, -0.62), "$\\theta_2$", C["blue"], fs=8)
    label(b_, (-1.55, 0.25), "air $n_1=1$", fs=7.5)
    label(b_, (-1.45, -1.05), "glass $n_2=1.5$", fs=7.5)
    label(b_, (0.0, -1.62), f"total internal reflection (glass to air) above $\\theta_c=\\arcsin(1/1.5)={math.degrees(math.asin(1/1.5)):.1f}^\\circ$", fs=7.5)
    b_.set_title("secondary rays: reflection and refraction")
    clean(b_, (-2.3, 2.9, -1.85, 1.75))
    save(fig, "d_raytrace")


# ------------------------------------------------------------------------------------- explosion: colour from heat
def bb(T):
    T = np.clip(T, 0, 1)
    return np.stack([T ** 0.45, T ** 1.9, 0.85 * T ** 4.5], -1)


def blackbody():
    fig = plt.figure(figsize=(7.0, 2.7))
    gs = fig.add_gridspec(2, 2, height_ratios=[2.2, 1], width_ratios=[1, 1])
    a_ = fig.add_subplot(gs[0, 0])
    T = np.linspace(0, 1, 300)
    B = bb(T)
    for k, (nm, col) in enumerate([("R $=T^{0.45}$", C["red"]), ("G $=T^{1.9}$", C["green"]), ("B $=0.85\\,T^{4.5}$", C["blue"])]):
        a_.plot(T, B[:, k], color=col, lw=1.6, label=nm)
    a_.set_xlabel("temperature $T$ (0 = cold, 1 = core)")
    a_.set_ylabel("channel")
    a_.set_xlim(0, 1)
    a_.set_ylim(0, 1.02)
    a_.grid(alpha=0.3)
    a_.legend(loc="upper left", fontsize=7.5)
    a_.set_title("(a) shader ramp $\\mathrm{bb}(T)$: red first, then green, then blue")
    s_ = fig.add_subplot(gs[1, 0])
    s_.imshow(B[None, :, :], aspect="auto", extent=(0, 1, 0, 1))
    s_.set_yticks([])
    s_.set_xlabel("$\\mathrm{bb}(T)$: black, deep red, orange, yellow, white-yellow")
    b_ = fig.add_subplot(gs[0, 1])
    t = np.linspace(0, 1, 300)
    fire = 1.0 * (1 - t) ** 0.6                     # core of a flame quad: T = field * life^0.6
    spark = np.clip(1.0 * (1 - 0.9 * t), 0, 1)      # spark Tn = heat (1 - 0.9 t)
    b_.plot(t, fire, color=C["orange"], lw=1.6, label="fire core $T=(1-t)^{0.6}$")
    b_.plot(t, 0.1 + 0.9 * spark, color=C["red"], lw=1.4, ls="--", label="spark $0.1+0.9\\,T_n$, $T_n=1-0.9t$")
    age = np.linspace(0, 8, 300)
    b_.set_xlabel("normalised life $t$ = age / life")
    b_.set_ylabel("$T$ fed to bb")
    b_.set_xlim(0, 1)
    b_.set_ylim(0, 1.05)
    b_.grid(alpha=0.3)
    b_.legend(loc="lower left", fontsize=7.5)
    b_.set_title("(b) temperature over a particle's life")
    s2 = fig.add_subplot(gs[1, 1])
    col_f = bb(fire) * np.clip(0.5 + 5 * fire ** 2, 0, None)[:, None]
    col_f = col_f / (1 + col_f)                     # Reinhard, only to show HDR colours on paper
    col_s = bb(0.1 + 0.9 * spark) * (2 + 9 * spark)[:, None]
    col_s = col_s / (1 + col_s)
    s2.imshow(np.stack([col_f, col_s]), aspect="auto", extent=(0, 1, 0, 2))
    s2.set_yticks([0.5, 1.5])
    s2.set_yticklabels(["spark", "fire"], fontsize=7.5)
    s2.set_xlabel("emitted colour over life (bb $\\times$ intensity, tone-mapped)")
    save(fig, "d_blackbody")


# ----------------------------------------------------------------------------------- explosion: particle physics
def particles():
    g = 9.81
    wind = np.array([0.8 * 0.6, 0.0])
    dt = 1 / 60

    def run(kind, v, life):
        p = np.array([0.0, 1.0])
        v = np.array(v, float)
        out = [p.copy()]
        for _ in range(int(life / dt)):
            if kind == "fire":
                v[1] += 2.0 * dt
                v *= math.exp(-1.8 * dt)
            elif kind == "smoke":
                v[1] = min(v[1] + 0.9 * dt, 3.0)
                k = 1 - math.exp(-0.6 * dt)
                v[0] += (wind[0] - v[0]) * k
            elif kind == "spark":
                v[1] -= g * dt
                v *= math.exp(-0.4 * dt)
            elif kind == "ember":
                v[1] -= 0.6 * g * dt
                v *= math.exp(-0.3 * dt)
            elif kind == "dust":
                v *= math.exp(-1.6 * dt)
                v[1] += 0.15 * dt
            p = p + v * dt
            if kind in ("spark", "ember") and p[1] < 0.02:
                p[1] = 0.02
                v[1] = -v[1] * 0.25
                v[0] *= 0.6
            out.append(p.copy())
        return np.array(out)

    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.6), gridspec_kw=dict(width_ratios=[1.6, 1]))
    a_ = ax[0]
    a_.axhline(0, color="0.4", lw=1.0)
    rows = [("spark", (11.0, 6.0), 1.6, C["red"], "spark: $g$, drag $e^{-0.4\\Delta t}$, bounce $\\times0.25$"),
            ("ember", (2.5, 6.0), 4.5, C["orange"], "ember: $0.6g$, $e^{-0.3\\Delta t}$"),
            ("fire", (3.5, 2.5), 1.3, "#e0a020", "fire: rises $+2\\Delta t$, $e^{-1.8\\Delta t}$"),
            ("smoke", (-1.0, 2.0), 12.0, "0.35", "smoke: buoyant $\\leq 3$ m/s, drifts with wind"),
            ("dust", (-5.0, 0.8), 4.0, "#8a6d3b", "dust: $e^{-1.6\\Delta t}$")]
    for kind, v, life, col, nm in rows:
        P = run(kind, v, life)
        a_.plot(P[:, 0], P[:, 1], color=col, lw=1.4, label=nm)
        step = int(0.25 / dt)
        a_.plot(P[::step, 0], P[::step, 1], "o", color=col, ms=2.4)
    a_.plot(0, 1, "*", color="k", ms=9, zorder=6)
    a_.set_xlabel("horizontal distance (m)")
    a_.set_ylabel("height (m)")
    a_.set_ylim(-0.5, 22)
    a_.set_xlim(-6.5, 22)
    a_.grid(alpha=0.25)
    a_.legend(loc="upper right", fontsize=7, frameon=True, ncol=1)
    a_.set_title("(a) one particle of each kind (dots every 0.25 s)")
    b_ = ax[1]
    y, vy, vx, x = 1.2, 5.0, 3.0, 0.0
    tt, hh, ss = [0.0], [y], [math.hypot(vx, vy)]
    h = 1 / 120
    t = 0.0
    while t < 2.6:
        vy -= 9.81 * h
        y += vy * h
        x += vx * h
        if y < 0:
            y = 0.0
            if vy < 0:
                vy = -0.3 * vy
            vx *= 0.7
            if math.hypot(vx, vy) < 0.25:
                vx = vy = 0.0
        t += h
        tt.append(t)
        hh.append(y)
        ss.append(math.hypot(vx, vy))
    b_.plot(tt, hh, color=C["blue"], lw=1.5, label="height (m)")
    b_.plot(tt, ss, color=C["red"], lw=1.1, ls="--", label="speed (m/s)")
    b_.set_xlabel("time (s)")
    b_.grid(alpha=0.3)
    b_.legend(loc="upper right", fontsize=7.5)
    b_.set_title("(b) debris: 1/120 s steps, restitution 0.3,\nfriction 0.7, at rest below 0.25 m/s")
    save(fig, "d_particles")


# --------------------------------------------------------------------------------- intruder: flood fill + line
def floodfill():
    W, H = 14, 9
    walls = set()
    for y in range(1, 7):
        walls.add((5, y))
    for x in range(8, 12):
        walls.add((x, 3))
    walls |= {(9, 6), (9, 7)}
    goals = {(12, 1), (13, 1), (12, 2), (13, 2)}   # cells hidden from every UAV
    INF = 10 ** 9
    dist = {(x, y): INF for x in range(W) for y in range(H)}
    from collections import deque
    q = deque()
    for gc in goals:
        dist[gc] = 0
        q.append(gc)
    while q:
        cx, cy = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nb = (cx + dx, cy + dy)
            if nb in dist and nb not in walls and dist[nb] == INF:
                dist[nb] = dist[(cx, cy)] + 1
                q.append(nb)
    start = (1, 4)
    path = [start]
    cur = start
    while dist[cur] > 0:
        nbs = [(cur[0] + dx, cur[1] + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))]
        cur = min((n for n in nbs if n in dist and n not in walls), key=lambda n: dist[n])
        path.append(cur)
    pts = np.array(path, float) + 0.5

    def chaikin(P, it):
        for _ in range(it):
            Q = [P[0]]
            for i in range(len(P) - 1):
                Q += [0.75 * P[i] + 0.25 * P[i + 1], 0.25 * P[i] + 0.75 * P[i + 1]]
            Q.append(P[-1])
            P = np.array(Q)
        return P

    sm = chaikin(pts, 3)
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.8), gridspec_kw=dict(width_ratios=[1.45, 1]))
    a_ = ax[0]
    vmax = max(v for v in dist.values() if v < INF)
    cmap = plt.get_cmap("Blues_r")
    for (x, y), v in dist.items():
        if (x, y) in walls:
            a_.add_patch(Rectangle((x, y), 1, 1, fc="0.3", ec="white", lw=0.5))
            continue
        fc = "#9fd49a" if (x, y) in goals else cmap(0.35 + 0.6 * v / vmax)
        a_.add_patch(Rectangle((x, y), 1, 1, fc=fc, ec="white", lw=0.5))
        a_.text(x + 0.5, y + 0.5, str(v), ha="center", va="center", fontsize=6.5, color="k")
    a_.plot(pts[:, 0], pts[:, 1], color=C["orange"], lw=1.0, ls="--")
    a_.plot(sm[:, 0], sm[:, 1], color=C["red"], lw=2.0)
    a_.plot(*pts[0], "o", color=C["red"], ms=7, mec="k")
    a_.set_title("(a) BFS from hidden cells (0); descend; Chaikin $\\times3$ = guide line")
    clean(a_, (0, W, 0, H))
    b_ = ax[1]
    xs = np.linspace(-0.45, 0.75, 200)
    line = lambda x: 0.07 * np.sin(4.0 * x + 0.3)
    b_.fill_between(xs, line(xs) - 0.05, line(xs) + 0.05, color="0.15")
    label(b_, (0.62, 0.17), "guide line, 10 cm", fs=7.5)
    rp = np.array([0.0, -0.06])
    hd = math.radians(8)
    fwd = np.array([math.cos(hd), math.sin(hd)])
    lft = np.array([-fwd[1], fwd[0]])
    body = [rp + 0.22 * fwd + 0.15 * lft, rp + 0.22 * fwd - 0.15 * lft, rp - 0.22 * fwd - 0.15 * lft, rp - 0.22 * fwd + 0.15 * lft]
    b_.add_patch(Polygon(body, fc=C["light"], ec=C["blue"], lw=1.2, zorder=4, alpha=0.9))
    on_offs = []
    for i in range(5):
        off = -0.12 + 0.06 * i
        sp = rp + 0.31 * fwd + off * lft
        on = abs(sp[1] - line(sp[0])) < 0.05
        if on:
            on_offs.append(off)
        b_.plot(*sp, "o", ms=6, color=C["red"] if on else "white", mec=C["orange"] if not on else "k", zorder=6)
    arrow(b_, rp, rp + 0.2 * fwd, "k", lw=1.0, ms=8, z=5)
    e = (sum(on_offs) / len(on_offs)) / 0.12 if on_offs else 0.0
    label(b_, (0.15, -0.25), "5 IR sensors 31 cm ahead, 6 cm apart", fs=7.5)
    label(b_, (0.15, -0.33), f"red = over the line: here $e$ = {e:+.2f} (turn left)", fs=7.5)
    label(b_, (0.15, -0.41), "$e=\\overline{\\mathrm{offset}}/0.12$,  $\\omega=\\mathrm{clamp}(3.2e+0.25\\,\\dot e,\\pm2.5)$", fs=7.5)
    label(b_, (0.15, -0.49), "$v=0.75(1-0.6|e|)$ m/s,  wheels $v\\mp\\omega\\,w/2$", fs=7.5)
    b_.set_title("(b) PD line follower (top view, metres)")
    clean(b_, (-0.45, 0.75, -0.53, 0.22))
    save(fig, "d_floodfill")


# --------------------------------------------------------------------------------- game: triangulation + gating
def triangulation():
    from mpl_toolkits.mplot3d import Axes3D  # noqa: F401
    fig = plt.figure(figsize=(7.0, 2.9))
    a_ = fig.add_subplot(1, 2, 1, projection="3d")
    A, B = np.array([-6.0, 0.0, 8.0]), np.array([7.0, 5.0, 9.0])
    X = np.array([0.0, 1.0, 0.5])
    dA = X - A + np.array([0.0, 0.9, 0.0])
    dB = X - B + np.array([0.4, -0.6, 0.0])
    dA /= np.linalg.norm(dA)
    dB /= np.linalg.norm(dB)
    w = A - B
    a, b, c, d, e = dA @ dA, dA @ dB, dB @ dB, dA @ w, dB @ w
    s = (b * e - c * d) / (a * c - b * b)
    t = (a * e - b * d) / (a * c - b * b)
    PA, PB = A + s * dA, B + t * dB
    mid = (PA + PB) / 2
    xx, yy = np.meshgrid(np.linspace(-7, 8, 2), np.linspace(-3, 7, 2))
    a_.plot_surface(xx, yy, np.zeros_like(xx), color="#d8e4c8", alpha=0.5)
    for P0, dd, col, nm in [(A, dA, C["blue"], "UAV 1"), (B, dB, C["purple"], "UAV 2")]:
        L = np.array([P0, P0 + dd * (np.linalg.norm(X - P0) * 1.25)])
        a_.plot(L[:, 0], L[:, 1], L[:, 2], color=col, lw=1.4)
        a_.scatter(*P0, color=col, s=25)
        a_.text(P0[0], P0[1], P0[2] + 1.0, nm, color=col, fontsize=8)
    a_.plot(*zip(PA, PB), color=C["red"], lw=2.0)
    a_.scatter(*mid, color=C["red"], s=30, marker="*")
    a_.text(mid[0] + 2.0, mid[1] - 1.0, mid[2] + 3.0, "shortest segment\nmidpoint = fused", color=C["red"], fontsize=8)
    a_.view_init(elev=18, azim=-62)
    a_.set_axis_off()
    a_.set_title("(a) two pixel rays (skew): fused point =\nmidpoint of the shortest segment", fontsize=8.5)
    b_ = fig.add_subplot(1, 2, 2)
    from matplotlib.patches import Ellipse
    S = np.array([[1.0, 0.45], [0.45, 0.55]])
    vals, vecs = np.linalg.eigh(S)
    ang = math.degrees(math.atan2(vecs[1, 1], vecs[0, 1]))
    k = math.sqrt(9.21)
    pred = np.array([0.0, 0.0])
    b_.add_patch(Ellipse(pred, 2 * k * math.sqrt(vals[1]), 2 * k * math.sqrt(vals[0]), angle=ang, fc=C["light"], ec=C["blue"], lw=1.3))
    b_.add_patch(Ellipse(pred, 2 * math.sqrt(vals[1]), 2 * math.sqrt(vals[0]), angle=ang, fc="none", ec=C["blue"], lw=0.8, ls="--"))
    arrow(b_, (-3.6, -1.8), pred, "0.4", lw=1.0, ms=8)
    label(b_, (-3.2, -2.2), "track: $\\hat x$ predicted by\nconstant velocity", fs=7.5)
    b_.plot(*pred, "o", color=C["blue"], ms=5)
    Sinv = np.linalg.inv(S)
    for m in [np.array([1.6, 0.9]), np.array([-1.0, 0.6]), np.array([2.4, -1.6])]:
        d2 = m @ Sinv @ m
        inside = d2 < 9.21
        b_.plot(*m, "x" if not inside else "o", color=C["green"] if inside else C["red"], ms=6, mew=1.6)
        label(b_, m + (0.0, 0.4), f"$d^2$={d2:.1f}", C["green"] if inside else C["red"], fs=7.5)
    label(b_, (0.9, 2.65), "gate $d^2=\\nu^\\top S^{-1}\\nu<9.21$ ($\\chi^2_2$, 99 %)", fs=7.5)
    b_.set_title("(b) Mahalanobis gate, then Hungarian assignment")
    clean(b_, (-4.6, 4.0, -2.9, 3.0))
    save(fig, "d_triangulation")


if __name__ == "__main__":
    trs()
    hierarchy()
    view_basis()
    projections()
    frustum_ndc()
    clipping()
    hsr()
    raster()
    lighting()
    texture()
    raytrace()
    blackbody()
    particles()
    floodfill()
    triangulation()
