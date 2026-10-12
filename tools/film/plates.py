"""Explanatory plates of the film: each idea drawn one step at a time, in step with the narration.

A plate is a function p_<name>(c) that returns its states [(time in the beat, png)]: every state is the picture from that
moment on (assemble.py dissolves from one to the next). c.s[i] is the time the i-th sentence of the narration starts, so a
state can be tied to the sentence that explains it. All numbers are read from the scene export and the workbook."""
import json
import math
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib.collections import PatchCollection, PolyCollection  # noqa: E402
from matplotlib.patches import Circle, FancyArrowPatch, FancyBboxPatch, Polygon, Rectangle  # noqa: E402

import dsl  # noqa: E402

ROOT = dsl.ROOT
BG, PANEL, LINE = "#0c121c", "#162234", "#2a3b55"
FG, DIM, AMB, CYAN, GREEN, RED, VIOLET = "#eef2f7", "#93a4ba", "#ffb02e", "#4cc9f0", "#6fd08c", "#ff6b6b", "#c58af9"
FONT = "DejaVu Sans"
MONO = "DejaVu Sans Mono"
plt.rcParams["font.family"] = [FONT, "DejaVu Sans"]
plt.rcParams["mathtext.fontset"] = "dejavusans"

_cache = {}


def export():
    if "exp" not in _cache:
        _cache["exp"] = json.load(open(os.path.join(ROOT, "report", "data", "scene_export_v10.json")))
    return _cache["exp"]


def lowres():
    if "low" not in _cache:
        d = json.load(open(os.path.join(ROOT, "report", "data", "prims_lowres.json")))
        _cache["low"] = {p["name"]: p for p in d["primitives"]}
    return _cache["low"]


PRIM_RGB = {"cube": (0, 70, 144), "cuboid": (0, 109, 178), "wedge": (0, 118, 133), "tapered box": (80, 101, 147), "pyramid": (0, 88, 80),
            "sphere": (255, 160, 50), "hemisphere": (255, 220, 69), "capsule": (255, 200, 120), "cylinder": (87, 180, 93),
            "frustum": (148, 180, 70), "cone": (51, 174, 141), "torus": (210, 71, 144), "annulus": (130, 104, 192),
            "helix": (189, 107, 186), "grass blade": (149, 147, 142), "terrain disc": (175, 137, 103)}


def rgb(name):
    r, g, b = PRIM_RGB[name]
    return (r / 255.0, g / 255.0, b / 255.0)


# ------------------------------------------------------------------------------------------------------------ canvas
def canvas():
    fig = plt.figure(figsize=(19.2, 10.8), dpi=100, facecolor=BG)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 1920)
    ax.set_ylim(1080, 0)
    ax.axis("off")
    return fig, ax


def txt(ax, x, y, s, size=30, color=FG, weight="normal", ha="left", va="center", alpha=1.0, mono=False, **kw):
    return ax.text(x, y, s, fontsize=size * 0.72, color=color, fontweight=weight, ha=ha, va=va, alpha=alpha,
                   family=MONO if mono else FONT, **kw)


def box(ax, x, y, w, h, fc=PANEL, ec=LINE, lw=2.0, r=16, alpha=1.0, z=1):
    p = FancyBboxPatch((x + r, y + r), w - 2 * r, h - 2 * r, boxstyle="round,pad=%d" % r, fc=fc, ec=ec, lw=lw, alpha=alpha, zorder=z)
    ax.add_patch(p)
    return p


def arrow(ax, x0, y0, x1, y1, color=FG, lw=3.0, head=18, z=5, style="-|>", alpha=1.0):
    p = FancyArrowPatch((x0, y0), (x1, y1), arrowstyle=style, mutation_scale=head, color=color, lw=lw, zorder=z, alpha=alpha,
                        shrinkA=0, shrinkB=0)
    ax.add_patch(p)
    return p


def line(ax, xs, ys, color=FG, lw=2.5, z=4, alpha=1.0, ls="-"):
    return ax.plot(xs, ys, color=color, lw=lw, zorder=z, alpha=alpha, ls=ls, solid_capstyle="round")


def note(ax, x, y, s, color=AMB, size=30, w=None):
    """A highlighted remark with a bar on its left."""
    ax.add_patch(Rectangle((x, y - size * 0.75), 6, size * 1.5, fc=color, ec="none", zorder=6))
    txt(ax, x + 24, y, s, size=size, color=FG)


class Ctx:
    def __init__(self, beat, sent, out):
        self.beat, self.dur, self.out = beat, beat["dur"], out
        self.s = [a for a, _, _ in sent] or [0.0]
        self.n = len(self.s)
        self.k = 0

    def t(self, i):
        """Start of sentence i (clamped), a little early so the picture leads the word."""
        return max(0.0, self.s[min(i, self.n - 1)] - 0.15)

    def save(self, fig):
        path = os.path.join(self.out, "%s_%02d.png" % (self.beat["id"], self.k))
        self.k += 1
        fig.savefig(path, facecolor=BG)
        plt.close(fig)
        return path


def reveal(c, steps, base=None):
    """steps = [(time, draw(ax, now))]: state k shows steps 0..k; draw gets now=True for the newest one."""
    out = []
    for k in range(len(steps)):
        fig, ax = canvas()
        if base:
            base(ax)
        for j in range(k + 1):
            steps[j][1](ax, j == k)
        out.append((steps[k][0] if k else 0.0, c.save(fig)))
    return out


# ------------------------------------------------------------------------------------------------------ small 3D kit
def rot(az, el):
    a, e = math.radians(az), math.radians(el)
    ry = np.array([[math.cos(a), 0, math.sin(a)], [0, 1, 0], [-math.sin(a), 0, math.cos(a)]])
    rx = np.array([[1, 0, 0], [0, math.cos(e), -math.sin(e)], [0, math.sin(e), math.cos(e)]])
    return rx @ ry


def mesh(ax, P, I, cx, cy, scale, color, az=35, el=25, edges=True, alpha=1.0, light=(0.4, 0.8, 0.45), lw=0.8, z=3, M=None):
    """Draws an indexed triangle mesh: orthographic view, back-to-front, Lambert shading per triangle."""
    P = np.asarray(P, float)
    if M is not None:
        P = P @ np.asarray(M, float)[:3, :3].T + np.asarray(M, float)[:3, 3]
    V = P @ rot(az, el).T
    T = np.asarray(I, int).reshape(-1, 3)
    tri = V[T]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    ln = np.linalg.norm(n, axis=1)
    ln[ln == 0] = 1
    n = n / ln[:, None]
    L = np.asarray(light, float)
    L = L / np.linalg.norm(L)
    sh = 0.32 + 0.68 * np.clip(np.abs(n @ L), 0, 1)
    order = np.argsort(tri[:, :, 2].mean(axis=1))
    polys = [[(cx + tri[k, j, 0] * scale, cy - tri[k, j, 1] * scale) for j in range(3)] for k in order]
    base = np.array(matplotlib.colors.to_rgb(color))
    cols = [tuple(base * sh[k]) + (alpha,) for k in order]
    pc = PolyCollection(polys, facecolors=cols, edgecolors=(1, 1, 1, 0.55 * alpha) if edges else cols, linewidths=lw if edges else 0.3, zorder=z)
    ax.add_collection(pc)
    return V


def sphere_mesh(slices, stacks, r=0.5):
    P = [(0, r, 0)]
    for k in range(1, stacks):
        ph = math.pi * k / stacks
        for s in range(slices):
            th = 2 * math.pi * s / slices
            P.append((r * math.sin(ph) * math.cos(th), r * math.cos(ph), r * math.sin(ph) * math.sin(th)))
    P.append((0, -r, 0))
    I = []
    for s in range(slices):
        I += [0, 1 + (s + 1) % slices, 1 + s]
    for k in range(stacks - 2):
        a, b = 1 + k * slices, 1 + (k + 1) * slices
        for s in range(slices):
            s2 = (s + 1) % slices
            I += [a + s, a + s2, b + s, a + s2, b + s2, b + s]
    last, a = len(P) - 1, 1 + (stacks - 2) * slices
    for s in range(slices):
        I += [a + s, a + (s + 1) % slices, last]
    return P, I


# ================================================================================================= act 0 / 1 plates
def p_map(c):
    e = export()
    fam = {d["name"]: d["family"] for d in e["designs"]}
    colour = {"buildings": "#7f93ad", "fortifications": "#566579", "vehicles and aircraft": AMB, "weapons and munitions": RED,
              "people": CYAN, "vegetation": "#2f6b45", "props": "#9a8f7a"}
    cube = np.array([[x, y, z, 1] for x in (-.5, .5) for y in (-.5, .5) for z in (-.5, .5)])
    rects = {}
    for comp in e["components"]:
        f = fam.get(comp.get("design"), "")
        if comp["primitive"] in ("Group", "Terrain disc") or f not in colour or comp.get("hidden_at_export"):
            continue
        Mw = np.array(comp["world_matrix"]).reshape(4, 4)
        q = cube @ Mw.T
        if q[:, 1].max() < -0.6 or abs(q[:, 0]).max() > 40 or abs(q[:, 2]).max() > 40:
            continue
        rects.setdefault(f, []).append((q[:, 0].min(), q[:, 2].min(), q[:, 0].max() - q[:, 0].min(), q[:, 2].max() - q[:, 2].min()))
    S, CX, CY = 11.6, 700, 510     # px per metre, centre

    def px(x, z):
        return CX + x * S, CY - z * S

    def base(ax):
        ax.add_patch(Circle(px(0, 0), 36 * S, fc="#141d2b", ec=LINE, lw=2, zorder=1))
        ax.add_patch(Circle(px(0, 0), 34 * S, fc="none", ec="#3d506b", lw=1.5, ls=(0, (6, 5)), zorder=2))
        for f in ("vegetation", "fortifications", "buildings", "props", "weapons and munitions", "vehicles and aircraft", "people"):
            pc = PatchCollection([Rectangle((px(x, z + d)[0], px(x, z + d)[1]), w * S, d * S) for x, z, w, d in rects.get(f, [])],
                                 fc=colour[f], ec="none", alpha=0.9 if f != "vegetation" else 0.7, zorder=3)
            ax.add_collection(pc)
        txt(ax, 110, 90, "The outpost", size=50, weight="bold")
        txt(ax, 110, 142, "drawn from the program's own part list: %s parts" % format(e["leaf_count"], ","), size=26, color=DIM)
        line(ax, [px(-36, -38)[0], px(-26, -38)[0]], [1000, 1000], color=DIM, lw=3)
        txt(ax, px(-31, 0)[0], 972, "10 m", size=22, color=DIM, ha="center")
        txt(ax, px(0, 0)[0], px(0, 37.4)[1], "N", size=24, color=DIM, ha="center")

    lay = e["layout"]
    labels = [("Command bunker (under the ground)", 0, -2, 1), ("Helipad", lay["helipad"][0], lay["helipad"][2], 2),
              ("Air-defence radar", lay["radar_site"][0], lay["radar_site"][2], 2), ("Anti-aircraft gun", lay["aa_gun_site"][0], lay["aa_gun_site"][2], 2),
              ("Surface-to-air missile launcher", 25.1, 6.7, 2), ("Rocket launcher truck", -11.5, 22.5, 2),
              ("Tanks (3)", lay["motor_pool"][0], lay["motor_pool"][2], 2), ("Barracks hall", 23.1, -11.8, 2),
              ("Outer gate", 0, 34, 2), ("Drone pads", 0, 12.5, 1)]
    labels.sort(key=lambda q: -q[2])
    ys = np.linspace(215, 905, len(labels))

    def lab(k_lo, k_hi):
        def draw(ax, now):
            for i, (name, x, z, g) in enumerate(labels):
                if not (k_lo <= g <= k_hi):
                    continue
                col = AMB if now else FG
                X, Y = px(x, z)
                ax.add_patch(Circle((X, Y), 9, fc="none", ec=col, lw=2.5, zorder=8))
                line(ax, [X + 9, 1275, 1300], [Y, ys[i], ys[i]], color=col, lw=1.6, z=7, alpha=0.8)
                txt(ax, 1312, ys[i], name, size=29, color=col)
        return draw
    return reveal(c, [(0, lambda ax, now: None), (c.t(1), lab(1, 1)), (c.t(2), lab(2, 2))], base)


def p_roadmap(c):
    acts = [("Geometry", "triangles\nprimitives\ntransformations"), ("Viewing", "camera\nprojection\nclipping, depth"),
            ("Light", "illumination\nshading, shadows\nray tracing"), ("World", "day and night\nweather\nsound"),
            ("Control", "walk, drive, fly\njump into\nany object"), ("Vision", "thermal sensor\nimage pipeline\ntracking"),
            ("Agents", "drones\nintruders\nsoldiers"), ("Proof", "tests, numbers\nbenchmarks")]
    lit_at = [1, 2, 3, 4, 4, 4, 4, 4]
    w, gap, x0, y0 = 196, 26, 76, 400

    def state(upto):
        def draw(ax, now):
            txt(ax, 96, 150, "One frame, followed through the program", size=50, weight="bold")
            txt(ax, 96, 208, "each stage: the idea first, then what it does in the outpost", size=28, color=DIM)
            for i, (a, sub) in enumerate(acts):
                on = lit_at[i] <= upto
                x = x0 + i * (w + gap)
                box(ax, x, y0, w, 250, fc=PANEL if on else "#111926", ec=AMB if on and lit_at[i] == upto else LINE, lw=3 if on else 1.5)
                txt(ax, x + w / 2, y0 + 52, a, size=33, weight="bold", ha="center", color=FG if on else "#4a5a70")
                txt(ax, x + w / 2, y0 + 150, sub, size=21, ha="center", color=DIM if on else "#3a485c", linespacing=1.5)
                txt(ax, x + w / 2, y0 - 34, str(i + 1), size=26, ha="center", color=AMB if on else "#4a5a70", weight="bold")
                if i:
                    arrow(ax, x - gap + 3, y0 + 125, x - 3, y0 + 125, color=FG if on else "#3a485c", lw=2.5, head=14)
        return draw
    out = []
    for k, u in enumerate((0, 1, 2, 3, 4)):
        fig, ax = canvas()
        state(u)(ax, True)
        out.append((c.t(k) if k else 0.0, c.save(fig)))
    return out


def p_triangle(c):
    cubeP, cubeI = lowres()["cube"]["positions"], lowres()["cube"]["indices"]
    prim = {p["name"]: p for p in export()["primitives"]}["cube"]

    def head(ax, now):
        txt(ax, 96, 110, "Everything is triangles", size=50, weight="bold")

    def tri(ax, now):
        pts = np.array([(250, 720), (610, 760), (400, 400)])
        ax.add_patch(Polygon(pts, fc="#1f4f7a", ec=FG, lw=3, zorder=3))
        for i, (x, y) in enumerate(pts):
            ax.add_patch(Circle((x, y), 11, fc=AMB, ec="none", zorder=5))
            txt(ax, x + (-34 if i == 0 else 24), y + (30 if i < 2 else -32), "v%d" % i, size=28, color=AMB, weight="bold")
        arrow(ax, 420, 630, 500, 520, color=CYAN, lw=3)
        txt(ax, 512, 506, "normal", size=24, color=CYAN)
        txt(ax, 250, 850, "a corner = position (x, y, z) + normal (x, y, z)", size=26, color=DIM)

    def quad(ax, now):
        o = np.array([(820, 440), (1120, 440), (820, 740), (1120, 740)])
        ax.add_patch(Polygon(o[[0, 1, 2]], fc="#1f4f7a", ec=FG, lw=3, zorder=3))
        ax.add_patch(Polygon(o[[2, 1, 3]], fc="#27628f", ec=FG, lw=3, zorder=3))
        line(ax, [820, 1120], [740, 440], color=AMB, lw=5, z=4)
        for i, (x, y) in enumerate(o):
            ax.add_patch(Circle((x, y), 11, fc=AMB, ec="none", zorder=5))
            txt(ax, x + (-36 if i % 2 == 0 else 22), y + (-28 if i < 2 else 30), str(i), size=28, color=AMB, weight="bold")
        txt(ax, 820, 850, "one face: 4 corners, indices 0 1 2 | 2 1 3", size=26, color=DIM)

    def cube(ax, now):
        mesh(ax, cubeP, cubeI, 1520, 590, 300, "#2f7fc1", az=32, el=24, lw=2.2)
        txt(ax, 1370, 850, "a cube: 6 faces x 2 triangles", size=26, color=DIM)

    def table(ax, now):
        box(ax, 96, 905, 1728, 120, ec=AMB)
        txt(ax, 130, 965, "indexed: %d corners + %d indices = %d bytes" % (prim["vertices"], prim["indices"], prim["bytes_indexed"]), size=30, color=FG)
        txt(ax, 1010, 965, "not indexed: %d corners = %d bytes" % (prim["indices"], prim["bytes_non_indexed"]), size=30, color=DIM)
    return reveal(c, [(0, head), (0.2, tri), (c.t(1), quad), (c.t(2), cube), (c.t(3), table)])


def p_sphere(c):
    prim = {p["name"]: p for p in export()["primitives"]}["sphere"]
    S, K = prim["params"]["slices"], prim["params"]["stacks"]
    sets = [(8, 4), (16, 8), (S, K)]

    def head(ax, now):
        txt(ax, 96, 110, "A curved shape is many flat pieces", size=50, weight="bold")
        txt(ax, 96, 168, "s slices round the axis, k stacks from pole to pole", size=28, color=DIM)

    def ball(i):
        def draw(ax, now):
            s, k = sets[i]
            P, I = sphere_mesh(s, k)
            cx = 340 + i * 620
            mesh(ax, P, I, cx, 500, 440, rgb("sphere"), az=25, el=22, lw=1.4 if i < 2 else 0.5)
            v, t = (k - 1) * s + 2, 2 * s * (k - 1)
            txt(ax, cx, 770, "%d x %d" % (s, k), size=36, weight="bold", ha="center", color=AMB if now else FG)
            txt(ax, cx, 822, "%s corners, %s triangles" % (format(v, ","), format(t, ",")), size=27, ha="center", color=DIM)
        return draw

    def formula(ax, now):
        box(ax, 96, 890, 1728, 130, ec=AMB)
        txt(ax, 140, 955, r"$V=(k-1)\,s+2=%d$" % prim["vertices"], size=38)
        txt(ax, 800, 955, r"$T=2\,s\,(k-1)=%s$" % format(prim["triangles"], ","), size=38)
        txt(ax, 1400, 955, "used for %d parts" % prim["instances"], size=28, color=DIM)
    return reveal(c, [(0, head), (c.t(1), ball(0)), (c.t(2), ball(1)), (c.t(3), ball(2)), (c.t(4), formula)])


def p_primgrid(c):
    low = lowres()
    prim = {p["name"]: p for p in export()["primitives"]}
    groups = [("Boxes", ["cube", "cuboid", "wedge", "tapered box", "pyramid"]), ("Round", ["sphere", "hemisphere", "capsule"]),
              ("Turned about an axis", ["cylinder", "frustum", "cone"]), ("Rings", ["torus", "annulus", "helix"]),
              ("Special", ["grass blade", "terrain disc"])]
    cell, x0 = 212, 96
    pos, x = {}, x0
    rows = [groups[:2], groups[2:]]
    ys = [360, 740]
    for r, row in enumerate(rows):
        x = x0
        for g, names in row:
            for n in names:
                pos[n] = (x + cell / 2, ys[r], g)
                x += cell
            x += 36

    def head(ax, now):
        txt(ax, 96, 110, "Sixteen generators, nothing else", size=50, weight="bold")

    def grp(gi):
        def draw(ax, now):
            g, names = groups[gi]
            xa = pos[names[0]][0] - cell / 2
            yy = pos[names[0]][1]
            txt(ax, xa + 8, yy - 168, g, size=28, color=AMB if now else DIM, weight="bold")
            line(ax, [xa + 8, xa + cell * len(names) - 20], [yy - 144, yy - 144], color=AMB if now else LINE, lw=2)
            for n in names:
                cx, cy, _ = pos[n]
                m = low.get(n) or low.get({"cuboid": "cube"}.get(n, n))
                M = np.diag([1.5, 0.6, 0.9, 1]) if n == "cuboid" else None
                P = np.asarray(m["positions"], float)
                ext = (P.max(axis=0) - P.min(axis=0)).max() * (1.5 if n == "cuboid" else 1.0)
                mesh(ax, P - (P.max(axis=0) + P.min(axis=0)) / 2, m["indices"], cx, cy, 168 / ext, rgb(n), az=32, el=26,
                     lw=0.9 if len(P) < 200 else 0.35, M=M)
                txt(ax, cx, cy + 138, n, size=25, ha="center", weight="bold")
                k = prim[n]["instances"]
                txt(ax, cx, cy + 172, ("%s parts" % format(k, ",")) if k else "450,000 blades" if n == "grass blade" else "", size=20, ha="center", color=DIM)
        return draw
    return reveal(c, [(0, head)] + [(c.t(i + 1), grp(i)) for i in range(5)])


def _frame2d(ax, ox, oy, s, title):
    """A small 2-D frame with axes: origin (ox, oy), s px per unit."""
    box(ax, ox - 200, oy - 440, 800, 540, fc="#101a28")
    arrow(ax, ox - 170, oy, ox + 570, oy, color=DIM, lw=2, head=12)
    arrow(ax, ox, oy + 70, ox, oy - 410, color=DIM, lw=2, head=12)
    txt(ax, ox - 190, oy - 478, title, size=32, weight="bold")
    ax.add_patch(Circle((ox, oy), 6, fc=DIM, ec="none", zorder=4))


def _quad2d(ax, ox, oy, s, M, fc, ec=FG, alpha=1.0, lw=2.5):
    q = np.array([[-.5, -.5, 1], [.5, -.5, 1], [.5, .5, 1], [-.5, .5, 1]]) @ np.asarray(M).T
    ax.add_patch(Polygon([(ox + x * s, oy - y * s) for x, y, _ in q], fc=fc, ec=ec, lw=lw, alpha=alpha, zorder=5))
    return q


def p_trs(c):
    s = 80
    Sm = np.array([[3.0, 0, 0], [0, 0.5, 0], [0, 0, 1]])
    a = math.radians(40)
    Rm = np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1]])
    Tm = np.array([[1, 0, 4.2], [0, 1, 1.6], [0, 0, 1]])

    def head(ax, now):
        txt(ax, 96, 110, "One matrix places a part", size=50, weight="bold")
        txt(ax, 96, 168, "every generator makes its shape one unit big, at the origin", size=28, color=DIM)

    def left(stage):
        def draw(ax, now):
            ox, oy = 310, 790
            if stage == 0:
                _frame2d(ax, ox, oy, s, "M = T R S")
            Ms = [np.eye(3), Sm, Rm @ Sm, Tm @ Rm @ Sm]
            names = ["unit shape", "S  scale", "R  rotate", "T  translate"]
            cols = ["#5b6f8a", "#2f7fc1", "#2f7fc1", GREEN]
            for j in range(stage + 1):
                _quad2d(ax, ox, oy, s, Ms[j], cols[j] if j == stage else "none", ec=cols[j], alpha=1.0 if j == stage else 0.45, lw=2.5 if j == stage else 1.5)
            txt(ax, ox - 170, oy + 62, "  ->  ".join(names[:stage + 1]), size=26, color=AMB if now else DIM)
        return draw

    def right(ax, now):
        ox, oy = 1190, 790
        _frame2d(ax, ox, oy, s, "the other order: R T S")
        _quad2d(ax, ox, oy, s, Tm @ Sm, "none", ec="#2f7fc1", alpha=0.5, lw=1.5)
        _quad2d(ax, ox, oy, s, Rm @ Tm @ Sm, RED, alpha=0.9)
        th = np.linspace(0, a, 30)
        r = math.hypot(4.2, 1.6)
        a0 = math.atan2(1.6, 4.2)
        line(ax, ox + r * s * np.cos(a0 + th), oy - r * s * np.sin(a0 + th), color=RED, lw=2.5, ls="--")
        txt(ax, ox - 170, oy + 62, "moved first, then swung round the origin", size=26, color=RED)
    return reveal(c, [(0, head), (c.t(1), left(0)), (c.t(1) + 1.6, left(1)), (c.t(1) + 3.0, left(2)), (c.t(1) + 4.2, left(3)), (c.t(3), right)])


def p_graph(c):
    nodes = [("Body", "placed in the world", 0), ("Arm", "placed on the body", 1), ("Hub", "placed on the arm, turns", 2), ("Blade", "placed on the hub", 3)]

    def head(ax, now):
        txt(ax, 96, 110, "Parts hang on each other", size=50, weight="bold")

    def chain(upto):
        def draw(ax, now):
            for name, sub, i in nodes[:upto + 1]:
                x, y = 110 + i * 150, 250 + i * 150
                box(ax, x, y, 330, 104, ec=AMB if (now and i == upto) else LINE, lw=3)
                txt(ax, x + 26, y + 38, name, size=32, weight="bold")
                txt(ax, x + 26, y + 76, sub, size=20, color=DIM)
                if i:
                    line(ax, [x - 90, x - 90, x], [y - 46, y + 52, y + 52], color=DIM, lw=2.5)
        return draw

    def drone(angle, show):
        def draw(ax, now):
            cx, cy = 1380, 520
            box(ax, cx - 420, cy - 330, 840, 680, fc="#101a28")
            ax.add_patch(Rectangle((cx - 70, cy - 46), 140, 92, fc="#3a4d68", ec=FG, lw=2, zorder=4))
            for k in range(4):
                a = math.radians(45 + 90 * k)
                hx, hy = cx + 250 * math.cos(a), cy - 250 * math.sin(a)
                line(ax, [cx + 70 * math.cos(a), hx], [cy - 70 * math.sin(a), hy], color="#7f93ad", lw=12, z=3)
                ax.add_patch(Circle((hx, hy), 22, fc=AMB if k == 0 else "#7f93ad", ec=FG, lw=2, zorder=6))
                for b in (0, 180):
                    t = math.radians(angle * (1 if k % 2 == 0 else -1) + b)
                    line(ax, [hx, hx + 120 * math.cos(t)], [hy, hy - 120 * math.sin(t)], color=CYAN if k == 0 else "#55708f", lw=9, z=5)
            if show:
                txt(ax, cx, cy + 300, "turn the hub: the blades follow", size=28, color=AMB, ha="center")
        return draw

    def formula(ax, now):
        box(ax, 96, 905, 1728, 120, ec=AMB)
        txt(ax, 140, 965, r"$M_{world}(blade)=M_{body}\;M_{arm}\;M_{hub}\;M_{blade}$", size=40)
        txt(ax, 1260, 965, "parent first, one product per level", size=26, color=DIM)
    steps = [(0, head), (0.2, chain(0)), (c.t(1), chain(1)), (c.t(2), chain(3)), (c.t(2) + 0.1, drone(20, False)), (c.t(3), formula)]
    out = reveal(c, steps)
    # the hub turning: three more states of the same picture
    for k, ang in enumerate((65, 110, 155)):
        fig, ax = canvas()
        for _, fn in steps[:4] + [steps[5]]:
            fn(ax, False)
        drone(ang, True)(ax, True)
        out.append((c.t(4) + 1.1 * k, c.save(fig)))
    return out
