"""Plates of Act 4 (world) and Act 5 (control)."""
import csv
import heapq
import math
import os

import numpy as np
from matplotlib.patches import Circle, Polygon, Rectangle

from plates import (AMB, CYAN, DIM, FG, GREEN, LINE, PANEL, RED, ROOT, VIOLET, arrow, box, canvas, export, line, lowres, mesh, reveal, rgb, txt)  # noqa: F401
from plates_b import head


def _axes(ax, x0, y0, w, h, xl="", yl=""):
    box(ax, x0 - 70, y0 - 40, w + 120, h + 110, fc="#101a28")
    line(ax, [x0, x0 + w], [y0 + h, y0 + h], color=DIM, lw=2)
    line(ax, [x0, x0], [y0, y0 + h], color=DIM, lw=2)
    if xl:
        txt(ax, x0 + w / 2, y0 + h + 46, xl, size=22, color=DIM, ha="center")
    if yl:
        txt(ax, x0 - 8, y0 - 18, yl, size=22, color=DIM)


# -------------------------------------------------------------------------------------------------------------- world
def p_sun(c):
    g = export()["v6"]["day_cycle"]["grid"]
    cfg = export()["v6"]["day_cycle"]["config"]
    hrs = np.array([q["hour"] for q in g])
    el = np.array([q["elevation_deg"] for q in g])
    x0, y0, w, h = 190, 300, 1000, 420
    X = lambda t: x0 + t / 24.0 * w        # noqa: E731
    Y = lambda e: y0 + h / 2 - e / 80.0 * (h / 2)   # noqa: E731

    def curve(ax, now):
        _axes(ax, x0, y0, w, h, "hour of the day", "height of the sun (degrees)")
        line(ax, [x0, x0 + w], [Y(0), Y(0)], color=FG, lw=1.5, ls="--")
        txt(ax, x0 + w + 10, Y(0), "horizon", size=20, color=DIM)
        for t in range(0, 25, 6):
            txt(ax, X(t), y0 + h + 18, "%02d:00" % (t % 24), size=18, color=DIM, ha="center")
        line(ax, X(hrs), [Y(e) for e in el], color=AMB, lw=4)
        txt(ax, x0, y0 - 70, "latitude %.1f° N, noon height %.1f°" % (cfg["latitude_deg"], el.max()), size=26)

    def colours(ax, now):
        for q in g:
            col = np.clip(np.array(q["key_color"]) * (0.35 + 0.65 * min(1.0, q["key_intensity"] / 3.2)), 0, 1)
            ax.add_patch(Rectangle((X(q["hour"]), y0 + h + 70), w / 96.0 + 1, 46, fc=tuple(col), ec="none", zorder=4))
            sky = np.clip(np.array(q["sky_zenith"]) * 1.6, 0, 1)
            ax.add_patch(Rectangle((X(q["hour"]), y0 + h + 126), w / 96.0 + 1, 46, fc=tuple(sky), ec="none", zorder=4))
        txt(ax, x0 - 20, y0 + h + 93, "light", size=20, color=DIM, ha="right")
        txt(ax, x0 - 20, y0 + h + 149, "sky", size=20, color=DIM, ha="right")

    def drives(ax, now):
        box(ax, 1330, 300, 500, 480, ec=AMB)
        txt(ax, 1360, 356, "the height of the sun sets", size=26, weight="bold", color=AMB)
        for i, s in enumerate(("colour and strength of the light", "colour of the sky and the fog", "lamps on below the horizon",
                               "searchlights below -2°", "exposure of the camera", "day shift / night shift", "intruders only at night")):
            txt(ax, 1360, 420 + i * 50, "·  " + s, size=23)
    return reveal(c, [(0, head("Time of day")), (c.t(1), curve), (c.t(3), colours), (c.t(4), drives)])


def p_instancing(c):
    blade = lowres()["grass blade"]

    def one(ax, now):
        P = np.asarray(blade["positions"], float)
        mesh(ax, P - (P.max(0) + P.min(0)) / 2, blade["indices"], 300, 560, 520 / (P.max(0) - P.min(0)).max(), "#6fb04a", az=20, el=8, lw=2.5)
        txt(ax, 300, 870, "one blade: %d corners, %d triangles" % (blade["V"], blade["T"]), size=26, ha="center")

    def many(ax, now):
        rng = np.random.default_rng(3)
        P = np.asarray(blade["positions"], float)
        P = P - (P.max(0) + P.min(0)) / 2
        for k in range(150):
            x, y = 640 + rng.random() * 560, 380 + rng.random() * 380
            s = 60 + 55 * (y - 380) / 380.0
            mesh(ax, P, blade["indices"], x, y, s, (0.25 + 0.2 * rng.random(), 0.55 + 0.25 * rng.random(), 0.2), az=rng.random() * 360, el=10, lw=0, edges=False, z=3 + y / 1000)
        txt(ax, 920, 870, "+ a list of 450,000 places (one matrix each)", size=26, ha="center", color=AMB if now else FG)
        txt(ax, 920, 920, "one draw call, not 450,000", size=24, ha="center", color=DIM)

    def lod(ax, now):
        box(ax, 1310, 300, 530, 520, ec=AMB if now else LINE)
        txt(ax, 1340, 356, "trees (public-domain meshes):", size=24, color=DIM)
        txt(ax, 1340, 396, "distance decides the detail", size=26, weight="bold")
        for i, (a, b) in enumerate((("near", "full mesh"), ("further", "simplified mesh"), ("far", "a flat picture of the tree"))):
            txt(ax, 1340, 480 + i * 105, a, size=26, color=AMB)
            txt(ax, 1500, 480 + i * 105, b, size=26)
            line(ax, [1340, 1810], [528 + i * 105, 528 + i * 105], color=LINE, lw=1.5)
    return reveal(c, [(0, head("Too many things to draw one by one")), (c.t(2), one), (c.t(2) + 4.0, many), (c.t(3), lod)])


def p_bench(c):
    rows = {r["config"]: r for r in csv.DictReader(open(os.path.join(ROOT, "report", "data", "bench.csv")))}
    pick = [("LOW", "LOW day"), ("HIGH", "HIGH day"), ("ULTRA", "ULTRA day"), ("EPIC", "EPIC day 10:30"), ("EPIC, night battle", "EPIC night 23:00 (game: waves, UAVs)")]

    def bars(ax, now):
        txt(ax, 96, 110, "Frame time per preset", size=50, weight="bold")
        txt(ax, 96, 168, "1920 x 1080 on the test machine; 16.7 ms is 60 frames a second", size=28, color=DIM)
        x0, y0 = 520, 290
        line(ax, [x0 + 16.7 * 60, x0 + 16.7 * 60], [y0 - 30, y0 + 5 * 110], color=RED, lw=2, ls="--")
        txt(ax, x0 + 16.7 * 60 + 12, y0 - 30, "16.7 ms", size=20, color=RED)
        for i, (name, key) in enumerate(pick):
            ms, fps = float(rows[key]["avg_ms"]), float(rows[key]["avg_fps"])
            y = y0 + i * 110
            txt(ax, x0 - 30, y + 32, name, size=30, ha="right", weight="bold")
            ax.add_patch(Rectangle((x0, y), ms * 60, 64, fc=AMB if "EPIC" in name else "#3f6fa0", ec="none", zorder=3))
            txt(ax, x0 + max(ms, 17.6) * 60 + 18, y + 32, "%.1f ms   (%.0f frames a second)" % (ms, fps), size=26)
    return reveal(c, [(0, bars)])


def p_sound(c):
    import soundfile as sf
    from scipy.signal import spectrogram
    aud = os.path.join(ROOT, "build", "audio_v7")
    items = [("a cannon shot and its echoes", "v7_cannon_echoes.wav", 1), ("a helicopter passing", "v7_helicopter_flyby.wav", 2),
             ("open air, then inside the bunker", "v7_bunker_vs_open_reverb.wav", 5)]

    def spec(i):
        def draw(ax, now):
            name, f, _ = items[i]
            x, sr = sf.read(os.path.join(aud, f))
            x = x.mean(axis=1) if x.ndim > 1 else x
            x = x[: int(min(len(x) / sr, 5.0) * sr)]
            fr, tt, S = spectrogram(x, sr, nperseg=1024, noverlap=768)
            S = 10 * np.log10(S[fr < 6000] + 1e-10)
            y0 = 250 + i * 250
            ax.imshow(S[::-1], extent=(150, 1250, y0 + 190, y0), aspect="auto", cmap="magma", vmin=S.max() - 70, vmax=S.max(), zorder=3)
            ax.set_xlim(0, 1920)
            ax.set_ylim(1080, 0)
            txt(ax, 1290, y0 + 60, name, size=26, color=AMB if now else FG)
            txt(ax, 1290, y0 + 110, "%.1f s, 0 to 6 kHz" % (len(x) / sr), size=20, color=DIM)
        return draw

    def rules(ax, now):
        txt(ax, 150, 1010, "delay = distance / 343 m/s      high tones fade with distance      echoes from the wall and the treeline", size=24, color=DIM)
    return reveal(c, [(0, head("No sound files", "spectrograms of what the program's own synthesiser writes")), (c.t(1), spec(0)), (c.t(2), spec(1)),
                      (c.t(3), rules), (c.t(5), spec(2))])


# ------------------------------------------------------------------------------------------------------------ control
def p_walkspace(c):
    w = export()["walkspace"]
    S, CX, CY = 26.0, 620, 600

    def px(x, z):
        return CX + x * S, CY - z * S

    def floors(ax, now):
        for f in w["floors"]:
            if abs(f["x0"]) > 13 or abs(f["z0"] - 2) > 13:
                continue
            x, y = px(f["x0"], f["z1"])
            ax.add_patch(Rectangle((x, y), (f["x1"] - f["x0"]) * S, (f["z1"] - f["z0"]) * S, fc="#1f4f7a", ec="#4cc9f0", lw=1.5, alpha=0.85, zorder=3))
        txt(ax, 150, 990, "floors (blue): where feet may stand", size=24, color=CYAN)

    def walls(ax, now):
        for q in w["walls"]:
            a, b = q["min"], q["max"]
            if abs(a[0]) > 13 or abs(a[2] - 2) > 13:
                continue
            x, y = px(a[0], b[2])
            ax.add_patch(Rectangle((x, y), (b[0] - a[0]) * S, (b[2] - a[2]) * S, fc="#c9d3e0", ec="none", zorder=5))
        txt(ax, 640, 990, "walls (white): they stop him", size=24)

    def door(ax, now):
        x, y = px(0, 4)
        ax.add_patch(Circle((x, y), 30, fc="none", ec=AMB, lw=4, zorder=8))
        txt(ax, x + 48, y, "blast gate", size=24, color=AMB)
        x2, y2 = px(1.8, 1.2)
        ax.add_patch(Circle((x2, y2), 22, fc="none", ec=AMB, lw=4, zorder=8))
        txt(ax, x2 + 40, y2, "generator room door", size=24, color=AMB)
        box(ax, 1250, 300, 580, 330, ec=AMB)
        txt(ax, 1280, 356, "a door is a gate in this map", size=26, weight="bold", color=AMB)
        txt(ax, 1280, 430, "shut: a wall", size=26)
        txt(ax, 1280, 490, "open: a doorway", size=26)
        txt(ax, 1280, 570, "it opens when a soldier is\nclose and facing it", size=24, color=DIM, va="top", linespacing=1.4)
    return reveal(c, [(0, head("The map under the soldiers' feet", "the command bunker, seen from above")), (c.t(0) + 3.0, floors), (c.t(0) + 6.0, walls), (c.t(1), door)])


def p_control(c):
    ctl = [q for q in export()["controllables"] if q["possessable"]]
    names = ["select", "take control", "its own keys", "V: change the camera", "Esc: give it back"]
    subs = ["click it, or Tab", "Enter, or E beside it", "drive  aim  fire  action", "outside / gunner's sight", ""]

    def chain(k):
        def draw(ax, now):
            x = 110 + k * 350
            box(ax, x, 280, 310, 150, ec=AMB if now else LINE, lw=3)
            txt(ax, x + 155, 334, names[k], size=28, weight="bold", ha="center")
            txt(ax, x + 155, 388, subs[k], size=20, color=DIM, ha="center")
            if k:
                arrow(ax, x - 38, 355, x - 4, 355, color=FG, lw=3, head=14)
        return draw

    def roster(ax, now):
        txt(ax, 110, 520, "%d things can be operated" % len(ctl), size=30, weight="bold", color=AMB)
        seen = []
        for q in ctl:
            n = q["name"].split(" (")[0]
            nice = {"UAV-1 DRONE": "Drone", "MBT-1 TANK": "Tank", "MH-6 HELICOPTER": "Helicopter", "AA-1 GATLING": "Anti-aircraft gun",
                    "AIR-DEFENCE RADAR": "Air-defence radar", "BLAST DOORS": "Blast gate of the bunker", "SAM-1 LAUNCHER": "Missile launcher",
                    "MLRS-1 ROCKET TRUCK": "Rocket launcher truck", "GATE BARRIER": "Gate barrier", "SKY / TIME OF DAY": "The clock (time of day)",
                    "BARRACKS DOOR": "Barracks door", "GENERATOR ROOM DOOR": "Generator room door"}
            n = "Soldiers (8)" if n.startswith("SOLDIER") else nice.get(n, n.title())
            if n not in seen:
                seen.append(n)
        for i, n in enumerate(seen[:18]):
            txt(ax, 110 + (i % 3) * 580, 600 + (i // 3) * 62, "·  " + n, size=25)
    return reveal(c, [(0, head("One rule for everything you can operate"))] + [(c.t(2) + 1.3 * k if k < 3 else c.t(3) + (k - 3) * 3.4, chain(k)) for k in range(5)]
                  + [(c.t(1) - 0.1, roster)])


def p_picking(c):
    sx, sy, sw, sh_ = 170, 280, 620, 380
    cam = (1040.0, 760.0)
    objs = [((1380.0, 420.0), 46, "soldier", True), ((1650.0, 300.0), 58, "tank", False), ((1560.0, 620.0), 40, "drone", False)]

    def screen(ax, now):
        ax.add_patch(Rectangle((sx, sy), sw, sh_, fc="#13233a", ec=FG, lw=3, zorder=2))
        txt(ax, sx, sy - 34, "the screen", size=24, color=DIM)
        px, py = sx + 380, sy + 150
        ax.add_patch(Polygon([(px, py), (px, py + 44), (px + 30, py + 30)], fc=FG, ec="#0c121c", lw=2, zorder=6))
        ax.add_patch(Circle((px, py), 20, fc="none", ec=AMB, lw=3, zorder=5))
        txt(ax, px + 40, py - 10, "click", size=24, color=AMB)

    def world(ax, now):
        ax.add_patch(Circle(cam, 16, fc=AMB, ec="none", zorder=6))
        txt(ax, cam[0], cam[1] + 46, "camera", size=22, color=AMB, ha="center")
        for (x, y), r, n, hit in objs:
            ax.add_patch(Circle((x, y), r, fc="none", ec=GREEN if hit else DIM, lw=3, ls=(0, (6, 4)), zorder=4))
            ax.add_patch(Circle((x, y), 12, fc=CYAN if hit else "#55708f", ec="none", zorder=5))
            txt(ax, x, y + r + 28, n, size=22, color=FG if hit else DIM, ha="center")

    def ray(ax, now):
        (x, y), r, _, _ = objs[0]
        arrow(ax, cam[0], cam[1], x - 26, y + 26, color=AMB, lw=4)
        d = np.array([x - cam[0], y - cam[1]])
        d = d / np.linalg.norm(d)
        line(ax, [x, x + d[0] * 300], [y, y + d[1] * 300], color=AMB, lw=2, ls="--", alpha=0.5)
        box(ax, 170, 760, 700, 220, ec=AMB)
        txt(ax, 200, 815, "pixel  ->  ray:   inverse of (projection x view)", size=25)
        txt(ax, 200, 870, "test the ray against a sphere round each object", size=25)
        txt(ax, 200, 925, "the nearest hit is the one you become", size=25, color=AMB)
    return reveal(c, [(0, head("Every moving thing has a point of view")), (c.t(1), screen), (c.t(1) + 2.5, world), (c.t(2), ray)])


def _grid_world():
    g = np.zeros((14, 22), int)
    g[3:11, 6:8] = 1
    g[0:6, 12:14] = 1
    g[8:14, 12:14] = 1
    g[5:7, 16:20] = 1
    return g


def _astar(g, s, t):
    H, W = g.shape
    hq, came, cost = [(0, s)], {s: None}, {s: 0.0}
    order = []
    while hq:
        _, u = heapq.heappop(hq)
        order.append(u)
        if u == t:
            break
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            v = (u[0] + dx, u[1] + dy)
            if not (0 <= v[0] < W and 0 <= v[1] < H) or g[v[1], v[0]] or (dx and dy and (g[u[1], v[0]] or g[v[1], u[0]])):
                continue
            nc = cost[u] + math.hypot(dx, dy)
            if v not in cost or nc < cost[v]:
                cost[v] = nc
                came[v] = u
                heapq.heappush(hq, (nc + math.hypot(t[0] - v[0], t[1] - v[1]), v))
    path, u = [], t
    while u is not None:
        path.append(u)
        u = came[u]
    return path[::-1], order


def p_astar(c):
    g = _grid_world()
    s, t = (1, 11), (20, 2)
    path, order = _astar(g, s, t)
    S, x0, y0 = 56, 130, 250
    P = lambda q: (x0 + (q[0] + .5) * S, y0 + (q[1] + .5) * S)   # noqa: E731

    def grid(ax, now):
        H, W = g.shape
        for j in range(H):
            for i in range(W):
                ax.add_patch(Rectangle((x0 + i * S + 1, y0 + j * S + 1), S - 2, S - 2, fc="#c9d3e0" if g[j, i] else "#141f30", ec="none", zorder=2))
        for q, col, n in ((s, CYAN, "soldier"), (t, GREEN, "post")):
            ax.add_patch(Circle(P(q), 17, fc=col, ec="none", zorder=7))
            txt(ax, P(q)[0], P(q)[1] + 44, n, size=20, color=col, ha="center")

    def search(ax, now):
        for q in order:
            ax.add_patch(Rectangle((x0 + q[0] * S + 1, y0 + q[1] * S + 1), S - 2, S - 2, fc="#27456b", ec="none", zorder=3))
        txt(ax, 1420, 320, "1   search the grid", size=28, weight="bold")
        txt(ax, 1420, 372, "cells opened: %d of %d" % (len(order), int((g == 0).sum())), size=22, color=DIM)

    def route(ax, now):
        xs, ys = zip(*[P(q) for q in path])
        line(ax, xs, ys, color=AMB, lw=4, z=6)
        txt(ax, 1420, 470, "2   shortest way: %d cells" % len(path), size=28, weight="bold", color=AMB)

    def smooth(ax, now):
        keep = [path[0]]
        for k in range(1, len(path) - 1):
            a, b, d = keep[-1], path[k], path[k + 1]
            if (b[0] - a[0]) * (d[1] - b[1]) != (b[1] - a[1]) * (d[0] - b[0]):
                keep.append(b)
        keep.append(path[-1])
        pts = np.array([P(q) for q in keep], float)
        pts = np.vstack([pts[0], pts, pts[-1]])
        out = []
        for k in range(1, len(pts) - 2):                       # Catmull-Rom through the corners
            for u in np.linspace(0, 1, 16):
                p0, p1, p2, p3 = pts[k - 1], pts[k], pts[k + 1], pts[k + 2]
                out.append(0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u + (-p0 + 3 * p1 - 3 * p2 + p3) * u ** 3))
        out = np.array(out)
        line(ax, out[:, 0], out[:, 1], color=GREEN, lw=5, z=7)
        txt(ax, 1420, 570, "3   straighten and round", size=28, weight="bold", color=GREEN)
        txt(ax, 1420, 640, r"$v=\dfrac{v_{max}}{1+1.5\,\kappa}$", size=34)
        txt(ax, 1420, 710, "slower where the curve is tight", size=22, color=DIM)
    return reveal(c, [(0, head("How a soldier gets to his post")), (0.3, grid), (c.t(2), search), (c.t(2) + 3.0, route), (c.t(2) + 5.5, smooth)])


def p_gait(c):
    L1, L2 = 150.0, 150.0
    gy = 800.0

    def leg(ax, hip, foot, col, lw=10):
        d = np.array(foot) - np.array(hip)
        dist = min(np.linalg.norm(d), L1 + L2 - 1)
        a = math.acos((L1 ** 2 + dist ** 2 - L2 ** 2) / (2 * L1 * dist))
        base = math.atan2(d[1], d[0])
        knee = (hip[0] + L1 * math.cos(base - a), hip[1] + L1 * math.sin(base - a))
        line(ax, [hip[0], knee[0], foot[0]], [hip[1], knee[1], foot[1]], color=col, lw=lw, z=5)
        ax.add_patch(Circle(knee, 11, fc=FG, ec="none", zorder=6))
        return knee

    def pose(ph, k):
        def draw(ax, now):
            line(ax, [120, 1120], [gy, gy], color=FG, lw=3)
            hx = 330 + ph * 420
            hip = (hx, gy - 265 + 10 * math.cos(ph * 2 * math.pi))
            line(ax, [hip[0], hip[0] + 12], [hip[1], hip[1] - 170], color="#7f93ad", lw=16, z=4)
            ax.add_patch(Circle((hip[0] + 16, hip[1] - 205), 30, fc="#7f93ad", ec="none", zorder=4))
            stance = (470.0, gy)
            sw = (270 + ph * 620, gy - 70 * math.sin(ph * math.pi))
            leg(ax, hip, stance, AMB)
            leg(ax, hip, sw, CYAN)
            ax.add_patch(Circle(stance, 13, fc=AMB, ec=FG, lw=2, zorder=7))
            if k == 0:
                txt(ax, stance[0], gy + 44, "planted foot stays", size=22, color=AMB, ha="center")
            if k >= 2:
                th = np.linspace(0, math.pi, 30)
                line(ax, 270 + th / math.pi * 620, gy - 70 * np.sin(th), color=CYAN, lw=2, ls="--", alpha=0.6)
                txt(ax, 900, gy - 96, "swing arc", size=22, color=CYAN)
        return draw

    def maths(ax, now):
        box(ax, 1220, 300, 610, 400, ec=AMB)
        txt(ax, 1250, 356, "two-link leg, solved backwards", size=26, weight="bold", color=AMB)
        txt(ax, 1250, 440, r"$\cos\theta_{knee}=\dfrac{a^{2}+b^{2}-d^{2}}{2ab}$", size=36)
        txt(ax, 1250, 540, "a  thigh     b  shin", size=24)
        txt(ax, 1250, 590, "d  distance hip to foot", size=24)
        txt(ax, 1250, 660, "law of cosines", size=22, color=DIM)
    out = []
    phases = [(0.1, 0, c.t(0)), (0.1, 0, c.t(1)), (0.35, 1, c.t(2)), (0.6, 2, c.t(3)), (0.85, 2, c.t(3) + 1.6)]
    for i, (ph, k, t) in enumerate(phases):
        fig, ax = canvas()
        head("Walking")(ax, True)
        pose(ph, k)(ax, True)
        if i >= 2:
            maths(ax, i == 2)
        out.append((t if i else 0.0, c.save(fig)))
    return out
