"""Plates of Act 6 (vision) and Act 7 (agents), and the animated map of the intruder's planner.

The night plates are drawn from build/film5/takes/n_wide_algo.jsonl: what the tracker, the squad and the rovers' planners
held, ten times a second, while the night was filmed (recorder option TS_FILM_ALGO)."""
import json
import math
import os

import numpy as np
from matplotlib.patches import Circle, Ellipse, Polygon, Rectangle

import dsl
from plates import (AMB, BG, CYAN, DIM, FG, FONT, GREEN, LINE, PANEL, RED, ROOT, VIOLET, arrow, box, canvas, export, line, plt, reveal, txt)  # noqa: F401
from plates_b import head
from plates_c import _axes

LAB = os.path.join(ROOT, "report", "figures_lab")
_night = {}


def night(name="n_wide"):
    if name not in _night:
        path = os.path.join(dsl.OUT, "takes", name + "_algo.jsonl")
        _night[name] = [json.loads(ln) for ln in open(path)] if os.path.exists(path) else []
    return _night[name]


def times():
    try:
        return json.load(open(os.path.join(dsl.OUT, "takes", "night_times.json")))
    except Exception:
        return {"single": 7.75, "combined": 18.0, "hide": 12.0}


def image(ax, path, x, y, w, h=None, z=3):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    h = h or w * im.size[1] / im.size[0]
    ax.imshow(np.asarray(im), extent=(x, x + w, y + h, y), zorder=z, interpolation="lanczos")
    ax.set_xlim(0, 1920)
    ax.set_ylim(1080, 0)
    return h


# ------------------------------------------------------------------------------------------------------------- vision
def p_sensor(c):
    from scipy.ndimage import gaussian_filter
    rng = np.random.default_rng(5)
    H, W = 120, 160
    yy, xx = np.mgrid[0:H, 0:W]
    x = 0.22 + 0.06 * yy / H
    x += 0.75 * np.exp(-(((xx - 55) / 7.0) ** 2 + ((yy - 70) / 4.5) ** 2))          # an engine
    x += 0.38 * np.exp(-(((xx - 112) / 3.0) ** 2 + ((yy - 52) / 7.0) ** 2))         # a soldier
    blur = gaussian_filter(x, 1.6)
    fpn = rng.normal(0, 0.035, (H, W)) + rng.normal(0, 0.03, (1, W))
    raw = blur * (1 + rng.normal(0, 0.03, (H, W))) + fpn + rng.normal(0, 0.02, (H, W))
    nuc = raw - fpn * 0.9
    stages = [("the heat in the scene", x, "gray"), ("through the lens: blurred", blur, "gray"), ("pixel errors, stripes, noise", raw, "gray"),
              ("after calibration against a shutter", nuc, "gray"), ("stretched and coloured", nuc, "inferno")]

    def st(i):
        def draw(ax, now):
            name, img, cm = stages[i]
            x0 = 96 + i * 352
            ax.imshow(np.clip(img, 0, 1), extent=(x0, x0 + 320, 560, 320), cmap=cm, vmin=0.1, vmax=1.0 if cm == "gray" else 0.85, zorder=3)
            ax.set_xlim(0, 1920)
            ax.set_ylim(1080, 0)
            txt(ax, x0 + 160, 600, name, size=21, ha="center", color=AMB if now else FG)
            if i:
                arrow(ax, x0 - 30, 440, x0 - 4, 440, color=DIM, lw=3, head=12)
        return draw

    def eq(ax, now):
        box(ax, 96, 730, 1728, 250, ec=AMB)
        txt(ax, 140, 800, r"$y=Q\,(\,g\cdot(\mathrm{PSF}*x)+o+c+n\,)$", size=44)
        txt(ax, 140, 880, "x  heat of the scene      PSF  blur of the lens      g, o  gain and offset of each pixel", size=24, color=DIM)
        txt(ax, 140, 930, "c  column stripes      n  noise that changes every frame      Q  rounding to 8 bits", size=24, color=DIM)
    return reveal(c, [(0, head("What a thermal camera delivers")), (c.t(2), st(0)), (c.t(4), st(1)), (c.t(4) + 2.5, st(2)), (c.t(4) + 5.5, eq),
                      (c.t(4) + 7.0, st(3)), (c.t(4) + 8.5, st(4))])


def p_cvsteps(c):
    tiles = [("the heat picture", "cv_u1_a_raw_grey.png"), ("1  threshold", "cv_u1_b_mask.png"), ("2  stray pixels removed", "cv_u1_c_despeckle.png"),
             ("3  regions", "cv_u1_d_components.png"), ("4, 5  measured and named", "cv_u1_e_overlay.png")]
    pos = [(110, 250), (730, 250), (1350, 250), (420, 672), (1040, 672)]

    def tile(i):
        def draw(ax, now):
            name, f = tiles[i]
            x, y = pos[i]
            hh = image(ax, os.path.join(LAB, f), x, y, 440)
            ax.add_patch(Rectangle((x, y), 440, hh, fc="none", ec=AMB if now else LINE, lw=3, zorder=5))
            txt(ax, x + 6, y - 26, name, size=25, color=AMB if now else FG, weight="bold")
        return draw

    def hist(ax, now):
        image(ax, os.path.join(LAB, "cv_u1_b0_histogram.png"), 110, 700, 270)
        txt(ax, 110, 676, "its histogram", size=20, color=DIM)
    return reveal(c, [(0, head("From a heat picture to a list of things", "a real frame of UAV 1")), (0.4, tile(0)), (c.t(1), tile(1)), (c.t(2) - 2.0, hist),
                      (c.t(3), tile(2)), (c.t(4), tile(3)), (c.t(5), tile(4))])


def p_classify(c):
    def blob(ax, cx, cy, a, b, ang, hot, col):
        ax.add_patch(Ellipse((cx, cy), a, b, angle=ang, fc=col, ec=FG, lw=2, zorder=4))
        if hot:
            ax.add_patch(Circle((cx, cy), 12, fc="#ffffff", ec="none", zorder=5))

    def measure(ax, now):
        blob(ax, 330, 520, 210, 120, 20, True, "#d9722b")
        ax.add_patch(Circle((330, 520), 5, fc="#0c121c", ec="none", zorder=6))
        line(ax, [330 - 100, 330 + 100], [520 + 36, 520 - 36], color=FG, lw=2, ls="--")
        ax.add_patch(Rectangle((218, 444), 224, 152, fc="none", ec=CYAN, lw=2, zorder=6))
        for i, s in enumerate(("area: pixels in the region", "centre: mean position", "peak: its hottest pixel", "elongation: long axis / short axis")):
            txt(ax, 110, 700 + i * 52, "·  " + s, size=24)

    def rule(i, name, cond, col, kind):
        def draw(ax, now):
            y = 300 + i * 220
            box(ax, 760, y, 1070, 180, ec=col, lw=3)
            if kind == 0:
                blob(ax, 870, y + 90, 110, 70, 15, True, "#d9722b")
            elif kind == 1:
                blob(ax, 870, y + 90, 46, 110, 0, False, "#b0803a")
            else:
                blob(ax, 870, y + 90, 150, 26, 0, False, "#7a5a33")
            txt(ax, 990, y + 62, name, size=32, weight="bold", color=col)
            txt(ax, 990, y + 122, cond, size=23)
        return draw
    return reveal(c, [(0, head("What is this warm spot?")), (0.4, measure), (c.t(2), rule(0, "TARGET", "small, a very hot core, not too stretched", RED, 0)),
                      (c.t(3), rule(1, "PERSON  (never fired at)", "warm, no hot core, upright or compact", GREEN, 1)),
                      (c.t(4), rule(2, "CLUTTER", "anything else: a sun-warmed wall, a pipe", DIM, 2))])


def _gauss(ax, mu, sig, col, x0, y0, w, h, lo, hi, label="", up=0):
    x = np.linspace(lo, hi, 300)
    y = np.exp(-0.5 * ((x - mu) / sig) ** 2) / sig
    line(ax, x0 + (x - lo) / (hi - lo) * w, y0 + h - y * h * 0.33, color=col, lw=4)
    if label:
        txt(ax, x0 + (mu - lo) / (hi - lo) * w, y0 + h - h * 0.33 / sig - 30 - up, label, size=22, color=col, ha="center")


def p_kalman(c):
    x0, y0, w, h = 150, 300, 1000, 440

    def axes(ax, now):
        _axes(ax, x0, y0, w, h, "position along the track (m)")

    def prior(ax, now):
        _gauss(ax, 3.0, 0.5, CYAN, x0, y0, w, h, 0, 10, "track at time t")

    def predict(ax, now):
        _gauss(ax, 5.0, 0.9, AMB, x0, y0, w, h, 0, 10, "predicted: moved, wider")
        arrow(ax, x0 + 0.33 * w, y0 + h - 60, x0 + 0.47 * w, y0 + h - 60, color=AMB, lw=3)

    def measure(ax, now):
        _gauss(ax, 6.6, 0.6, RED, x0, y0, w, h, 0, 10, "detection")

    def update(ax, now):
        k = 0.9 ** 2 / (0.9 ** 2 + 0.6 ** 2)
        mu, sg = 5.0 + k * (6.6 - 5.0), math.sqrt((1 - k) * 0.9 ** 2)
        _gauss(ax, mu, sg, GREEN, x0, y0, w, h, 0, 10, "corrected: between, narrower", up=40)

    def maths(ax, now):
        box(ax, 1260, 300, 580, 440, ec=AMB)
        txt(ax, 1290, 356, "predict", size=26, weight="bold", color=AMB)
        txt(ax, 1290, 416, r"$\hat x = F x \qquad P = F P F^{T} + Q$", size=28)
        txt(ax, 1290, 506, "correct", size=26, weight="bold", color=GREEN)
        txt(ax, 1290, 566, r"$K = P H^{T}(H P H^{T}+R)^{-1}$", size=28)
        txt(ax, 1290, 626, r"$x = \hat x + K\,(z - H\hat x)$", size=28)
        txt(ax, 1290, 700, "state: x, z, vx, vz", size=22, color=DIM)
    return reveal(c, [(0, head("A track is a guess with an uncertainty")), (0.3, axes), (c.t(1), prior), (c.t(2), predict), (c.t(3), measure),
                      (c.t(3) + 2.2, update), (c.t(4), maths)])


def p_assoc(c):
    T = [(380, 430), (640, 640)]
    D = [(420, 470), (700, 600), (900, 380)]

    def tracks(ax, now):
        for i, (x, y) in enumerate(T):
            ax.add_patch(Ellipse((x, y), 230, 150, angle=20, fc="none", ec=CYAN, lw=3, ls=(0, (7, 5)), zorder=4))
            ax.add_patch(Circle((x, y), 11, fc=CYAN, ec="none", zorder=5))
            txt(ax, x - 26, y - 34, "track %d" % (i + 1), size=22, color=CYAN, ha="right")
        txt(ax, 200, 800, "gate: only detections inside it may match (chi-square 9.21)", size=23, color=CYAN)

    def dets(ax, now):
        for i, (x, y) in enumerate(D):
            ax.add_patch(Rectangle((x - 10, y - 10), 20, 20, fc=RED, ec="none", zorder=6))
        line(ax, [T[0][0], D[0][0]], [T[0][1], D[0][1]], color=GREEN, lw=4)
        line(ax, [T[1][0], D[1][0]], [T[1][1], D[1][1]], color=GREEN, lw=4)
        txt(ax, D[2][0] + 22, D[2][1], "outside every gate:\na new track", size=22, color=RED, va="center", linespacing=1.3)
        txt(ax, 200, 850, "pairs chosen so the total distance is smallest (Hungarian method)", size=23, color=GREEN)

    def two(ax, now):
        box(ax, 1150, 280, 690, 520, fc="#101a28")
        u1, u2, tg = (1250, 380), (1730, 420), (1500, 700)
        for u, n in ((u1, "UAV 1"), (u2, "UAV 2")):
            ax.add_patch(Circle(u, 16, fc=AMB, ec="none", zorder=6))
            txt(ax, u[0], u[1] - 36, n, size=22, color=AMB, ha="center")
            line(ax, [u[0], tg[0] + (tg[0] - u[0]) * 0.25], [u[1], tg[1] + (tg[1] - u[1]) * 0.25], color=AMB, lw=2.5)
        ax.add_patch(Circle(tg, 12, fc=GREEN, ec=FG, lw=2, zorder=7))
        txt(ax, tg[0] + 26, tg[1] + 6, "where the two\nlines of sight cross", size=22, color=GREEN, va="center", linespacing=1.3)
    return reveal(c, [(0, head("Which detection belongs to which track?")), (0.3, tracks), (c.t(1), dets), (c.t(2), two)])


def p_tracklog(c):
    d = night()
    T0 = times()
    ids = {"the squad's target": []}
    for q in d:
        for tr in q["tracks"]:
            if tr["id"] == q["target"]:
                ids["the squad's target"].append((q["t"], tr["sig"], tr["s"], tr["seen"]))
    tid = "the squad's target" if ids["the squad's target"] else None
    x0, y0, w, h = 190, 300, 1500, 480

    def plot(ax, now):
        _axes(ax, x0, y0, w, h, "seconds into the night take", "position uncertainty of %s (m)" % tid)
        if not tid:
            return
        a = np.array(ids[tid], float)
        t1 = max(a[:, 0].max(), 1.0)
        smax = max(a[:, 1].max(), 0.2)
        X = lambda t: x0 + t / t1 * w       # noqa: E731
        Y = lambda s: y0 + h - s / smax * h * 0.9   # noqa: E731
        for k in range(len(a) - 1):
            if a[k + 1, 0] - a[k, 0] > 0.35:
                continue                     # no target between these two samples
            if a[k, 3] == 0:                 # no camera measured it in this step: coasting
                ax.add_patch(Rectangle((X(a[k, 0]), y0), X(a[k + 1, 0]) - X(a[k, 0]) + 1, h, fc=RED, ec="none", alpha=0.16, zorder=2))
        cut = [0] + [k + 1 for k in range(len(a) - 1) if a[k + 1, 0] - a[k, 0] > 0.35] + [len(a)]
        for i0, i1 in zip(cut[:-1], cut[1:]):
            line(ax, [X(t) for t in a[i0:i1, 0]], [Y(s) for s in a[i0:i1, 1]], color=AMB, lw=3.5)
        for s in np.linspace(0, smax, 4):
            txt(ax, x0 - 14, Y(s), "%.2f" % s, size=18, color=DIM, ha="right")
        for t in range(0, int(t1) + 1, 5):
            txt(ax, X(t), y0 + h + 16, str(t), size=18, color=DIM, ha="center")
        for key, name in (("single", "single strike"), ("combined", "combined strike")):
            if T0[key] <= t1:
                line(ax, [X(T0[key])] * 2, [y0, y0 + h], color=FG, lw=1.5, ls="--")
                txt(ax, X(T0[key]) + 8, y0 + 18, name, size=18)
        txt(ax, x0 + 10, y0 + h + 100, "red bands: no camera saw the target (the track coasts on its prediction)", size=23, color=RED)
    return reveal(c, [(0, head("The tracker, measured", "recorded from the program while the night was filmed")), (0.3, plot)])


# ------------------------------------------------------------------------------------------------------------- drones
def p_duty(c):
    names = [("DOCKED", "by day, on its pad"), ("TAKE-OFF", "dusk: rotors spool up 2 s"), ("PATROL", "night: opposite sides of a circle"),
             ("RETURN", "dawn, last rover gone"), ("LANDING", "hover, then descend")]

    def st(k):
        def draw(ax, now):
            x = 100 + k * 352
            box(ax, x, 300, 310, 150, ec=AMB if now else LINE, lw=3)
            txt(ax, x + 155, 350, names[k][0], size=30, weight="bold", ha="center")
            txt(ax, x + 155, 404, names[k][1], size=19, color=DIM, ha="center")
            if k:
                arrow(ax, x - 38, 375, x - 4, 375, color=FG, lw=3, head=14)
        return draw

    def orbit(ax, now):
        cx, cy = 960, 760
        ax.add_patch(Circle((cx, cy), 170, fc="none", ec=LINE, lw=2, ls=(0, (6, 5)), zorder=2))
        ax.add_patch(Rectangle((cx - 40, cy - 30), 80, 60, fc="#3a4d68", ec=FG, lw=2, zorder=3))
        txt(ax, cx, cy + 62, "bunker", size=18, color=DIM, ha="center")
        for a, n in ((35, "UAV 1"), (215, "UAV 2")):
            x, y = cx + 170 * math.cos(math.radians(a)), cy - 170 * math.sin(math.radians(a))
            ax.add_patch(Circle((x, y), 15, fc=AMB, ec="none", zorder=5))
            txt(ax, x + (24 if a < 90 else -24), y, n, size=22, color=AMB, ha="left" if a < 90 else "right")
    return reveal(c, [(0, head("A drone's day"))] + [(c.t(1) + (0 if k == 0 else 0) if k == 0 else c.t(min(k + 1, 4)) if k < 3 else c.t(4) + (k - 3) * 1.5, st(k)) for k in range(5)]
                  + [(c.t(3) + 0.2, orbit)])


def p_decide1(c):
    def roles(ax, now):
        tg = (560, 640)
        ax.add_patch(Circle(tg, 16, fc=RED, ec=FG, lw=2, zorder=6))
        txt(ax, tg[0], tg[1] + 46, "intruder", size=22, color=RED, ha="center")
        for (x, y), n, col in (((300, 420), "LEAD (nearer)", AMB), ((820, 380), "WING", CYAN)):
            ax.add_patch(Circle((x, y), 18, fc=col, ec="none", zorder=6))
            txt(ax, x, y - 40, n, size=24, color=col, ha="center", weight="bold")
            line(ax, [x, tg[0]], [y, tg[1]], color=col, lw=2.5, ls="--")
        txt(ax, tg[0], tg[1] - 150, "about 90° apart", size=20, color=DIM, ha="center")

    def locks(ax, now):
        box(ax, 1080, 300, 760, 300, ec=AMB)
        txt(ax, 1110, 356, "a drone may fire when", size=26, weight="bold", color=AMB)
        txt(ax, 1110, 426, "lead: lock held  0.8 s", size=28)
        txt(ax, 1110, 486, "wing: lock held  0.5 s", size=28)
        txt(ax, 1110, 546, "and nothing is in the line of fire", size=24, color=DIM)

    def wait(ax, now):
        box(ax, 1080, 640, 760, 220, ec=GREEN)
        txt(ax, 1110, 700, "one intruder, wing in the pair:", size=26)
        txt(ax, 1110, 762, "the lead waits up to 8 s for the wing", size=28, color=GREEN, weight="bold")
        txt(ax, 1110, 820, "wing busy elsewhere: 2.5 s, then alone", size=22, color=DIM)
    return reveal(c, [(0, head("Who fires, and when?")), (c.t(0) + 2.0, roles), (c.t(1), locks), (c.t(2), wait)])


def p_tot(c):
    tg = (1500, 640)
    U = [((330, 360), "far drone", AMB, 3.2), ((1080, 880), "near drone", CYAN, 1.4)]

    def scene(ax, now):
        ax.add_patch(Circle(tg, 18, fc=RED, ec=FG, lw=2, zorder=6))
        txt(ax, tg[0] + 30, tg[1], "target", size=24, color=RED)
        for (x, y), n, col, tt in U:
            ax.add_patch(Circle((x, y), 18, fc=col, ec="none", zorder=6))
            txt(ax, x, y - 40, n, size=24, color=col, ha="center", weight="bold")
            line(ax, [x, tg[0]], [y, tg[1]], color=col, lw=2, ls="--")
            txt(ax, (x + tg[0]) / 2, (y + tg[1]) / 2 - 26, "flight %.1f s" % tt, size=22, color=col, ha="center")

    def timeline(ax, now):
        x0, y0, w = 200, 250, 900
        box(ax, x0 - 40, y0 - 90, w + 400, 130, fc="#101a28")
        line(ax, [x0, x0 + w], [y0, y0], color=DIM, lw=2)
        for (_, n, col, tt) in U:
            xs = x0 + w - tt / 4.0 * w
            ax.add_patch(Rectangle((xs, y0 - 46 if col == AMB else y0 + 6), tt / 4.0 * w, 16, fc=col, ec="none", zorder=4))
            txt(ax, xs - 12, y0 - 38 if col == AMB else y0 + 14, "fires", size=20, color=col, ha="right")
        line(ax, [x0 + w, x0 + w], [y0 - 70, y0 + 40], color=RED, lw=3)
        txt(ax, x0 + w + 16, y0 - 14, "both arrive", size=24, color=RED, weight="bold")
    return reveal(c, [(0, head("The farther drone fires first")), (0.3, scene), (c.t(1), timeline)])


def p_decide2(c):
    def tree(ax, now):
        box(ax, 110, 280, 560, 110, ec=AMB)
        txt(ax, 140, 335, "a second intruder is tracked", size=26, weight="bold")

    def q(i, text, yes, no):
        def draw(ax, now):
            y = 440 + i * 170
            box(ax, 110, y, 760, 110, ec=AMB if now else LINE, lw=3)
            txt(ax, 140, y + 55, text, size=26)
            if i < 2:
                arrow(ax, 870, y + 55, 990, y + 55, color=RED, lw=3, head=14)
                txt(ax, 930, y + 26, "no", size=20, color=DIM, ha="center")
                txt(ax, 1010, y + 55, no, size=24, color=DIM)
            if i < 2:
                arrow(ax, 400, y + 110, 400, y + 170, color=GREEN, lw=3, head=14)
                txt(ax, 424, y + 140, "yes", size=20, color=GREEN)
            else:
                txt(ax, 140, y + 150, yes, size=26, color=GREEN, weight="bold")
        return draw

    def rejoin(ax, now):
        box(ax, 1180, 740, 660, 200, ec=CYAN)
        txt(ax, 1210, 800, "the pair forms again when", size=24, color=CYAN, weight="bold")
        txt(ax, 1210, 856, "that target is destroyed or lost,", size=23)
        txt(ax, 1210, 904, "or the two come within 9 m", size=23)
    return reveal(c, [(0, head("Two intruders: split, or stay together?")), (0.3, tree), (c.t(1), q(0, "more than 14 m from the first target?", "", "stay together")),
                      (c.t(1) + 3.2, q(1, "urgent: within 10 m of what it could damage?", "", "leave it for later")),
                      (c.t(2), q(2, "SPLIT: the nearer drone takes it alone", "each drone now strikes its own target", "")), (c.t(4), rejoin)])


# ----------------------------------------------------------------------------------------------------------- intruder
G, CELL = 80, 0.5


def grid_frame(name="n_wide", at=None, need_line=True):
    """The dump entry with a grid nearest to `at` (and the grid of free cells, sent once)."""
    d = night(name) or night("n_wide")
    free = next((q["grid"]["free"] for q in d if "grid" in q and "free" in q["grid"]), "1" * (G * G))
    gs = [q for q in d if "grid" in q and (not need_line or len(q["grid"]["line"]) > 3)]
    if not gs:
        return None, free
    at = times()["hide"] - 3.0 if at is None else at
    return min(gs, key=lambda q: abs(q["t"] - at)), free


def _cells(s):
    return np.array([int(ch) for ch in s], float).reshape(G, G)


def draw_map(ax, q, free, x0, y0, size, show=("free", "exposed", "flood", "line", "rover", "uav"), flood_upto=None, zoom=None, label=True):
    """The rover's 80 x 80 grid: north (+z) up. zoom = (cx, cz, half-width in m)."""
    cx, cz, hw = zoom or (0.0, 0.0, 20.0)
    S = size / (2 * hw)
    X = lambda x: x0 + (x - cx + hw) * S       # noqa: E731
    Y = lambda z: y0 + (cz + hw - z) * S       # noqa: E731
    fr = _cells(free)
    img = np.zeros((G, G, 3))
    img[...] = np.array([0.07, 0.11, 0.17])
    img[fr == 0] = np.array([0.62, 0.68, 0.76])
    g = q["grid"] if q and "grid" in q else None
    if g and "exposed" in show:
        hid = _cells(g["hidden"])
        img[(fr == 1) & (hid == 0)] = np.array([0.40, 0.13, 0.13])
        img[(fr == 1) & (hid == 1)] = np.array([0.06, 0.16, 0.12])
    if g and "flood" in show:
        fl = np.array(g["flood"], float).reshape(G, G)
        ok = (fl >= 0) & (fr == 1)
        if ok.any():
            mx = np.percentile(fl[ok], 97)
            lim = mx if flood_upto is None else mx * flood_upto
            lit = ok & (fl <= lim)
            v = np.clip(fl / mx, 0, 1)
            img[lit] = img[lit] * 0.45 + 0.55 * np.stack([0.15 + 0.5 * (1 - v[lit]), 0.35 + 0.45 * (1 - v[lit]), 0.75 - 0.3 * v[lit]], axis=1)
    ext = (X(-20), X(20), Y(-20), Y(20))
    ax.imshow(img, extent=ext, origin="lower", zorder=2, interpolation="nearest")
    ax.set_xlim(0, 1920)
    ax.set_ylim(1080, 0)
    clipr = Rectangle((x0, y0), size, size, transform=ax.transData)
    ax.images[-1].set_clip_path(clipr)
    ax.add_patch(Rectangle((x0, y0), size, size, fc="none", ec=LINE, lw=2, zorder=9))

    def put(artist):
        artist.set_clip_path(clipr)
        return artist
    if q:
        if g and "line" in show and len(g["line"]) > 1:
            L = np.array(g["line"])
            for ln in ax.plot(X(L[:, 0]), Y(L[:, 1]), color=AMB, lw=3.5, zorder=6):
                put(ln)
        if "uav" in show:
            for u in q["uav"]:
                put(ax.add_patch(Circle((X(u["p"][0]), Y(u["p"][2])), 5.0 * S, fc="none", ec=CYAN, lw=2, ls=(0, (5, 4)), zorder=7)))
                put(ax.add_patch(Circle((X(u["p"][0]), Y(u["p"][2])), 9, fc=CYAN, ec=FG, lw=1.5, zorder=8)))
        if "tracks" in show:
            for tr in q["tracks"]:
                for ln in ax.plot([X(tr["x"])], [Y(tr["z"])], marker="+", ms=18, mew=3, color=GREEN if tr["s"] == 1 else "#b8c4d4", zorder=8):
                    put(ln)
        if "rover" in show:
            for r in q["rovers"]:
                me = g and r["i"] == g["i"]
                col = (VIOLET if r["ev"] else AMB) if me else "#c9a36a"
                put(ax.add_patch(Circle((X(r["x"]), Y(r["z"])), 11 if me else 7, fc=col, ec=FG, lw=1.5, zorder=9)))
                if me:
                    hx, hz = math.sin(r["yaw"]), math.cos(r["yaw"])
                    for ln in ax.plot([X(r["x"]), X(r["x"] + hx * 1.6)], [Y(r["z"]), Y(r["z"] + hz * 1.6)], color=FG, lw=2.5, zorder=9):
                        put(ln)
                    if label:
                        put(ax.add_patch(Circle((X(r["obj"][0]), Y(r["obj"][1])), 12, fc="none", ec=GREEN, lw=3, zorder=8)))
    return X, Y


def p_rgrid(c):
    q, free = grid_frame()

    def m(show):
        def draw(ax, now):
            draw_map(ax, q, free, 150, 240, 760, show=show)
        return draw

    def notes(ax, now):
        box(ax, 1020, 300, 800, 380, ec=AMB)
        txt(ax, 1050, 356, "the rover's own map of the camp", size=28, weight="bold", color=AMB)
        txt(ax, 1050, 430, "80 x 80 cells of 0.5 m:  40 m x 40 m", size=26)
        txt(ax, 1050, 490, "light cells: inside an obstacle, blocked", size=24)
        txt(ax, 1050, 546, "dark cells: free ground", size=24)
        txt(ax, 1050, 630, "yellow dot: the rover     green ring: its goal", size=22, color=DIM)
    return reveal(c, [(0, head("The intruder's map")), (c.t(1), m(("free", "rover"))), (c.t(1) + 3.0, notes)])


def p_rcost(c):
    q, free = grid_frame()

    def m(show):
        def draw(ax, now):
            X, Y = draw_map(ax, q, free, 150, 240, 760, show=show)
            if "exposed" in show and q:
                for u in q["uav"]:
                    for a in np.linspace(0, 2 * math.pi, 14, endpoint=False):
                        line(ax, [X(u["p"][0]), X(u["p"][0] + 9 * math.cos(a))], [Y(u["p"][2]), Y(u["p"][2] + 9 * math.sin(a))], color=CYAN, lw=1, alpha=0.35, z=5)
        return draw

    def formula(ax, now):
        box(ax, 1020, 300, 800, 420, ec=AMB)
        txt(ax, 1050, 360, "cost of a cell =", size=30, weight="bold", color=AMB)
        txt(ax, 1050, 440, "1", size=30)
        txt(ax, 1150, 440, "for the step", size=24, color=DIM)
        txt(ax, 1050, 510, "+ exposure", size=30, color=RED)
        txt(ax, 1300, 510, "if a drone can see it", size=24, color=DIM)
        txt(ax, 1050, 580, "+ noise", size=30, color=VIOLET)
        txt(ax, 1300, 580, "the rover's own random field", size=24, color=DIM)
        txt(ax, 1050, 670, "red: seen by a drone     dark green: in cover", size=22, color=DIM)
    return reveal(c, [(0, head("Being seen has a price")), (0.3, m(("free", "rover"))), (c.t(2), m(("free", "exposed", "rover", "uav"))), (c.t(1), formula)])


def p_rflood(c):
    q, free = grid_frame()
    out = []
    fr = [0.0, 0.12, 0.28, 0.48, 0.72, 1.0]
    for k, f in enumerate(fr):
        fig, ax = canvas()
        head("Flood fill: numbers spread out from the goal")(ax, True)
        draw_map(ax, q, free, 150, 240, 760, show=("free", "flood", "rover") + (("line",) if k == len(fr) - 1 else ()), flood_upto=max(f, 0.001))
        box(ax, 1020, 300, 800, 400, ec=AMB)
        txt(ax, 1050, 360, "start at the goal with 0", size=28, weight="bold", color=AMB)
        txt(ax, 1050, 440, "each cell = cheapest neighbour + its own cost", size=24)
        txt(ax, 1050, 500, "bright: near the goal     dim: far", size=24, color=DIM)
        if k == len(fr) - 1:
            txt(ax, 1050, 600, "route: always step to the smaller number", size=26, color=AMB, weight="bold")
        t = c.t(1) + (c.t(3) - c.t(1)) * k / (len(fr) - 1) if k else 0.0
        out.append((t, c.save(fig)))
    return out


def p_lfr(c):
    def robot(err):
        def draw(ax, now):
            box(ax, 110, 260, 900, 600, fc="#101a28")
            th = np.linspace(-1, 1, 80)
            lx = 560 + 150 * np.sin(th * 1.5) + 40
            ly = 560 - th * 280
            line(ax, lx, ly, color=AMB, lw=10, z=3)
            cx, cy = 560, 620
            ax.add_patch(Rectangle((cx - 80, cy - 20), 160, 190, fc="#3a4d68", ec=FG, lw=2, zorder=4))
            for sx in (-1, 1):
                ax.add_patch(Rectangle((cx + sx * 92 - 14, cy + 30), 28, 110, fc="#1c2738", ec=FG, lw=2, zorder=5))
            line_x = 560 + 150 * math.sin(((560 - (cy - 40)) / 280.0) * 1.5) + 40
            for k in range(5):
                x = cx + (k - 2) * 38
                on = abs(x - line_x) < 26
                ax.add_patch(Circle((x, cy - 40), 13, fc=GREEN if on else "#253247", ec=FG, lw=1.5, zorder=6))
                txt(ax, x, cy - 82, "%+d" % (k - 2), size=18, color=DIM, ha="center")
            if err:
                txt(ax, 150, 300, "sensors over the line light up", size=22, color=GREEN)
        return draw

    def maths(k):
        def draw(ax, now):
            rows = [(r"error $e$ = weighted mean of the lit sensors", FG), (r"turn rate $\omega = K_p\,e + K_d\,\dot e$      $K_p=3.2,\ K_d=0.25$", AMB),
                    (r"left wheel $= v-\omega\,b/2$      right wheel $= v+\omega\,b/2$", CYAN), (r"speed $v$ falls as $|e|$ grows: slower in bends", DIM)]
            box(ax, 1060, 300, 790, 110 + 90 * (k + 1), ec=AMB if now else LINE)
            for i in range(k + 1):
                txt(ax, 1090, 370 + i * 90, rows[i][0], size=25, color=rows[i][1])
        return draw
    return reveal(c, [(0, head("Following the route like a line-follower robot")), (0.3, robot(False)), (c.t(2), robot(True)), (c.t(3), maths(0)), (c.t(4), maths(1)),
                      (c.t(4) + 3.5, maths(3))])


def p_revade(c):
    T0 = times()
    qa, free = grid_frame(at=T0["hide"] - 2.5)
    qb, _ = grid_frame(at=T0["hide"] + 1.0, need_line=False)
    zoom = None
    if qb and "grid" in qb:
        me = [r for r in qb["rovers"] if r["i"] == qb["grid"]["i"]]
        if me:
            zoom = (float(np.clip(me[0]["x"], -11, 11)), float(np.clip(me[0]["z"], -11, 11)), 9.0)

    def m(q, show, cap, layer):
        def draw(ax, now):
            draw_map(ax, q, free, 150, 240, 760, show=show, zoom=zoom, label=False)
            # each caption covers the one before it (the states are drawn on top of each other)
            ax.add_patch(Rectangle((140, 1006), 900, 50, fc=BG, ec="none", zorder=10 + 2 * layer))
            txt(ax, 150, 1030, cap, size=24, color=AMB if now else DIM, zorder=11 + 2 * layer)
        return draw

    def rule(k):
        def draw(ax, now):
            rows = ["a drone within 5 m (the dashed ring)", "new goal: every cell no drone can see", "flood fill from those cells, drive to the nearest",
                    "wait 4 s in cover, then plan again"]
            box(ax, 1020, 300, 810, 100 + 80 * (k + 1), ec=VIOLET if now else LINE)
            for i in range(k + 1):
                txt(ax, 1050, 365 + i * 80, "%d   %s" % (i + 1, rows[i]), size=25, color=VIOLET if i == k and now else FG)
        return draw
    sh = ("free", "exposed", "line", "rover", "uav")
    return reveal(c, [(0, head("It hears the rotors")), (0.3, m(qa, sh, "before: on its way in", 0)), (c.t(1), rule(0)), (c.t(3), rule(1)),
                      (c.t(4), m(qb or qa, sh, "hiding: red = seen, dark = cover, yellow = its route", 1)), (c.t(4) + 0.1, rule(2)), (c.t(5), rule(3))])


def a_rovermap(at, dur, out, fps=15):
    """Animated map of one rover's planner for [at, at + dur] of the night take (the middle pane of the three views)."""
    import subprocess
    d = night()
    _, free = grid_frame()
    frames = [q for q in d if at - 0.05 <= q["t"] <= at + dur + 0.2]
    grids = [q for q in d if "grid" in q]
    tmp = out[:-4] + "_f"
    os.makedirs(tmp, exist_ok=True)
    n = int(dur * fps)
    for k in range(n):
        t = at + k / float(fps)
        q = min(frames, key=lambda z: abs(z["t"] - t)) if frames else None
        g = max([z for z in grids if z["t"] <= t + 0.01] or grids[:1], key=lambda z: z["t"]) if grids else None
        fig = plt.figure(figsize=(6.4, 6.4), dpi=100, facecolor=BG)
        ax = fig.add_axes([0, 0, 1, 1])
        ax.axis("off")
        if q and g:
            qq = dict(q)
            qq["grid"] = g["grid"]
            me = [r for r in q["rovers"] if r["i"] == g["grid"]["i"]]
            zoom = (float(np.clip(me[0]["x"], -9, 9)), float(np.clip(me[0]["z"], -9, 9)), 11.0) if me else None
            # draw_map works in the 1920 x 1080 canvas; here the axes are scaled so that its square fills the figure
            draw_map(ax, qq, free, 0, 0, 1080, show=("free", "exposed", "line", "rover", "uav", "tracks"), zoom=zoom, label=False)
            ax.set_xlim(0, 1080)
            ax.set_ylim(1080, 0)
            st = "HIDING" if me and me[0]["ev"] else "PAUSED IN COVER" if me and me[0]["pause"] else "INFILTRATING"
            ax.text(30, 50, st, fontsize=19, color=VIOLET if st == "HIDING" else AMB, fontweight="bold", family=FONT)
        fig.savefig(os.path.join(tmp, "f%04d.png" % k), facecolor=BG)
        plt.close(fig)
    subprocess.run([os.path.expanduser("~/.local/bin/ffmpeg"), "-y", "-loglevel", "error", "-framerate", str(fps), "-i", os.path.join(tmp, "f%04d.png"),
                    "-vf", "fps=30,format=yuv420p", "-c:v", "libx264", "-crf", "15", "-preset", "fast", out], check=True)
    return out


# ------------------------------------------------------------------------------------------------------ game, physics
def p_game(c):
    rows = [("waves", "2 + n rovers, at most 6", AMB), ("armour", "100 points; a hit takes about 30", FG), ("score", "kill +100     combined kill +150", GREEN),
            ("breach", "a rover at its goal: base -10 %", RED)]

    def row(i):
        def draw(ax, now):
            y = 300 + i * 150
            box(ax, 200, y, 1520, 120, ec=rows[i][2] if now else LINE, lw=3)
            txt(ax, 240, y + 60, rows[i][0], size=34, weight="bold", color=rows[i][2])
            txt(ax, 560, y + 60, rows[i][1], size=32)
        return draw
    return reveal(c, [(0, head("Night Watch"))] + [(c.t(i + 1) if i < 2 else c.t(3) + (i - 2) * 4.2, row(i)) for i in range(4)])


def p_particles(c):
    rng = np.random.default_rng(11)
    n = 260
    ang = rng.uniform(0.15, math.pi - 0.15, n)
    spd = rng.uniform(3.0, 11.0, n)
    vx, vy = spd * np.cos(ang), spd * np.sin(ang)
    life = rng.uniform(1.5, 2.8, n)

    def ramp(u):
        """Blackbody-like colour for a cooling particle: u = 1 hot ... 0 cold."""
        r = np.clip(0.25 + 1.4 * u, 0, 1)
        g = np.clip(1.6 * u - 0.35, 0, 1)
        b = np.clip(2.6 * u - 1.7, 0, 1)
        return np.stack([r, g, b], axis=1)

    def frame(t):
        def draw(ax, now):
            box(ax, 110, 250, 1020, 640, fc="#070b12")
            ox, oy, s = 620, 830, 46
            line(ax, [140, 1100], [oy, oy], color=DIM, lw=2)
            k = 0.9
            x = vx * (1 - np.exp(-k * t)) / k
            y = (vy + 9.8 / k) * (1 - np.exp(-k * t)) / k - 9.8 * t / k
            y = np.abs(y) * np.where(y < 0, 0.25, 1.0)
            u = np.clip(1 - t / life, 0, 1)
            alive = u > 0
            ax.scatter(ox + x[alive] * s, oy - y[alive] * s, s=14 + 60 * u[alive], c=ramp(u[alive]), zorder=5, linewidths=0)
            txt(ax, 140, 286, "t = %.1f s" % t, size=22, color=DIM)
        return draw

    def maths(ax, now):
        box(ax, 1200, 300, 640, 500, ec=AMB)
        txt(ax, 1230, 356, "each particle, each step", size=26, weight="bold", color=AMB)
        txt(ax, 1230, 430, r"$\mathbf{v}\ \leftarrow\ \mathbf{v}+(\mathbf{g}-k\,\mathbf{v})\,\Delta t$", size=30)
        txt(ax, 1230, 500, r"$\mathbf{p}\ \leftarrow\ \mathbf{p}+\mathbf{v}\,\Delta t$", size=30)
        txt(ax, 1230, 570, "on the ground: bounce, keep a quarter", size=22, color=DIM)
        for i in range(60):
            u = 1 - i / 59.0
            ax.add_patch(Rectangle((1230 + i * 9.5, 640), 10, 44, fc=tuple(ramp(np.array([u]))[0]), ec="none", zorder=4))
        txt(ax, 1230, 716, "hot", size=20, color=DIM)
        txt(ax, 1800, 716, "cool", size=20, color=DIM, ha="right")
        txt(ax, 1230, 760, "colour follows temperature", size=22, color=DIM)
    out = []
    ts = [0.05, 0.2, 0.4, 0.65, 0.95, 1.3]
    for i, t in enumerate(ts):
        fig, ax = canvas()
        head("An explosion is particles")(ax, True)
        frame(t)(ax, True)
        if i >= 2:
            maths(ax, i == 2)
        out.append((c.t(1) + (c.dur - c.t(1) - 1.0) * i / len(ts) if i else 0.0, c.save(fig)))
    return out


# -------------------------------------------------------------------------------------------------- engineering, close
def p_hard(c):
    rows = [("Far surfaces flicker", "depth is most precise near the camera:\nabout 1 m at 100 m in 24 bits", "decals lifted 2-4 mm; the near plane\nstays at 0.05 m for the rifle", AMB),
            ("Stripes in the shadows", "a surface shadows itself when the\nbias is too small", "an offset along the normal that\ngrows with the slope", CYAN),
            ("Thin wires shimmer", "a wire thinner than a pixel is hit\nin one frame and missed in the next", "temporal anti-aliasing and a\nsofter highlight on thin parts", VIOLET),
            ("Soldiers walk through walls", "walls were only pictures", "floors, walls and doors in a walk map;\ndoors open and close for a soldier", GREEN)]

    def hdr(ax, now):
        for x, s in ((120, "problem"), (640, "cause"), (1230, "fix")):
            txt(ax, x, 250, s, size=24, color=DIM, weight="bold")

    def row(i):
        def draw(ax, now):
            y = 290 + i * 172
            box(ax, 96, y, 1728, 150, ec=rows[i][3] if now else LINE, lw=3)
            txt(ax, 120, y + 75, rows[i][0], size=29, weight="bold", color=rows[i][3])
            txt(ax, 640, y + 75, rows[i][1], size=22, linespacing=1.4)
            txt(ax, 1230, y + 75, rows[i][2], size=22, linespacing=1.4)
        return draw
    return reveal(c, [(0, head("What went wrong on the way")), (0.3, hdr)] + [(c.t(i + 1), row(i)) for i in range(4)])


def p_proof(c):
    rows = [("396", "automatic tests, run on every build", AMB), ("same seed", "the same night, event for event", CYAN), ("replay", "a recorded play session runs again exactly", GREEN)]

    def row(i):
        def draw(ax, now):
            y = 300 + i * 190
            box(ax, 200, y, 1520, 150, ec=rows[i][2] if now else LINE, lw=3)
            txt(ax, 250, y + 75, rows[i][0], size=52, weight="bold", color=rows[i][2])
            txt(ax, 700, y + 75, rows[i][1], size=32)
        return draw
    return reveal(c, [(0, head("How it is checked"))] + [(c.t(i + 1), row(i)) for i in range(3)])


def p_numbers(c):
    e = export()
    items = [(str(len(e["primitives"])), "primitives"), (format(e["leaf_count"], ","), "parts"), (str(len(e["designs"])), "designs"),
             (str(sum(1 for q in e["controllables"] if q["possessable"])), "things to operate"),
             ("396", "tests"), ("23", "sheets in the workbook,\none row for every component")]

    def draw(ax, now):
        for i, (n, s) in enumerate(items):
            x, y = 330 + (i % 3) * 630, 330 + (i // 3) * 330
            txt(ax, x, y, n, size=110, weight="bold", ha="center", color=AMB)
            txt(ax, x, y + 110, s, size=30, ha="center", color=FG, va="top", linespacing=1.3)
    return reveal(c, [(0, draw)])
