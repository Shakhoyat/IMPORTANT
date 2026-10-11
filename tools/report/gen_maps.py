"""Site maps for the report: the soldiers' guard map (day and night duty) and the intruder rovers' route map.

Inputs: a fresh export (`ts --export-docs build/routes_export.json`, schema 9 + garrison "plan" and simulation tracks)
for the routes, and report/data/scene_export_v10.json for the site plan (every part's top-view footprint). The route
data are copied to report/data/routes.json so the figures can be rebuilt without the program.
Run: ~/.local/bin/uv run -q --with matplotlib --with numpy python tools/report/gen_maps.py [build/routes_export.json]"""
import ast
import json
import math
import os
import re
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.collections import PolyCollection
from matplotlib.lines import Line2D

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.environ.get("TS_PLOT_OUT") or os.path.join(ROOT, "report", "figures", "plots")   # TS_PLOT_OUT: a proof directory
DATA = os.path.join(ROOT, "report", "data", "routes.json")
import glob as _glob
import matplotlib.font_manager as _fm
for _f in _glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "report", "fonts", "*.otf")):
    _fm.fontManager.addfont(_f)   # the report's Times face (TeX Gyre Termes X), so plot text matches the body text
plt.rcParams.update({"font.family": "serif", "font.serif": ["TeX Gyre TermesX", "Times New Roman", "Nimbus Roman", "DejaVu Serif"],
                     "mathtext.fontset": "stix", "font.size": 8.5})

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "routes_export.json")
if os.path.exists(src):
    E = json.load(open(src))
    routes = {"garrison": E["v6"]["garrison"]["plan"], "roster": [{"name": s["name"], "day": s["roster_day"], "night": s["roster_night"]}
                                                                  for s in E["v6"]["garrison"]["soldiers"]],
              "rover_tracks": E["simulation"]["rover_tracks"], "uav_tracks": E["simulation"]["uav_tracks"],
              "objectives": E["v5"]["objectives"], "layout": E["layout"], "events": E["simulation"]["events"]}
    json.dump(routes, open(DATA, "w"))
else:
    routes = json.load(open(DATA))
SITE = json.load(open(os.path.join(ROOT, "report", "data", "scene_export_v10.json")))
L = routes["layout"]
V = lambda x: ast.literal_eval(x) if isinstance(x, str) else x

# ---- site plan: top-view footprint of every part (convex hull of its unit box's 8 corners), backdrop left out
SKIP_FAM = {"terrain and backdrop"}
fam = {d["name"]: d["family"] for d in SITE["designs"]}
skip_names = {"Camouflage net"}   # nets would cover the vehicles under them


def hull(pts):
    pts = sorted(set(pts))
    if len(pts) < 3:
        return pts
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], p) <= 0:
            up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]


polys, shades = [], []
for c in SITE["components"]:
    d = c.get("design")
    if not d or fam.get(d) in SKIP_FAM or d in skip_names or c["primitive"] in ("Group", "TerrainDisc", "BeamCone"):
        continue
    if str(c.get("hidden_at_export")) == "True":
        continue
    m = V(c["world_matrix"])
    if m[7] < -1.0:          # underground (bunker rooms): drawn as an outline below
        continue
    pts = [(round(m[0] * x + m[1] * y + m[2] * z + m[3], 2), round(m[8] * x + m[9] * y + m[10] * z + m[11], 2))
           for x in (-0.5, 0.5) for y in (-0.5, 0.5) for z in (-0.5, 0.5)]
    h = hull(pts)
    if len(h) >= 3:
        polys.append(h)
        shades.append(0.80 if fam.get(d) in ("vegetation",) else 0.62)


def base(ax, title, r=37):
    ax.add_patch(plt.Circle((0, 0), L["perimeter_radius"], fill=False, ec="0.55", lw=0.8, ls="--"))
    ax.add_collection(PolyCollection(polys, facecolors=[str(s) for s in shades], edgecolors="none", alpha=0.55, zorder=1))
    # bunker rooms (underground, dashed) from the layout
    ax.add_patch(plt.Rectangle((L["bunker_min_x"], L["bunker_min_z"]), L["bunker_max_x"] - L["bunker_min_x"],
                               L["bunker_max_z"] - L["bunker_min_z"], fill=False, ec="0.25", lw=0.7, ls=(0, (3, 2)), zorder=2))
    ax.site_labels = {}           # place name -> (text, site x, site z); settle_site_labels() moves them clear of the drawing
    for name, key, dx, dz in [("outer gate", "outer_gate", 1.5, -1.0), ("helipad", "helipad", 0, 5.8), ("radar", "radar_site", -1, 4),
                              ("anti-aircraft gun", "aa_gun_site", 0, -4.5), ("motor pool", "motor_pool", 0, -6.5), ("barracks", "barracks", 0, 7.5),
                              ("fuel depot", "fuel_depot", 0, -3.5)]:
        p = L[key]
        ax.site_labels[name] = (ax.text(p[0] + dx, p[2] + dz, name, fontsize=7, color="0.30", ha="center", va="center", zorder=6, clip_on=True,
                                        bbox=dict(fc="white", ec="none", alpha=0.7, pad=0.6)), p[0], p[2])
    ax.bunker_label = ax.text(0, -2.4, "command bunker\n(underground)", fontsize=6.5, color="0.25", ha="center", va="center", zorder=6)
    ax.set_xlim(-r, r)
    ax.set_ylim(r, -r)            # +z (the gate) at the bottom: north up with x to the right is not the scene's convention
    ax.set_aspect("equal")
    ax.set_xticks([])
    ax.set_yticks([])
    for s in ax.spines.values():
        s.set_visible(False)
    ax.set_title(title, fontsize=9)
    ax.scale_bar = (ax.plot([r - 13, r - 3], [r - 2.5, r - 2.5], color="black", lw=1.5)[0],
                    ax.text(r - 8, r - 3.8, "10 m", ha="center", fontsize=7))


# ---- label placement: no label on a line, a marker, another label or the frame
class Labels:
    """Greedy label placement for one axes, worked out in display pixels after the layout is final.

    The drawing is registered first: lines as sampled points with a weight (line()), markers and finished labels as boxes
    that nothing may cover (marker(), box()). put() then tries a list of positions for a label and keeps the cheapest: a
    covered box or the frame costs most, then covered line points, then the distance from the wanted position. A label
    that ends up away from its marker gets a thin leader."""

    def __init__(self, ax, clear=1.5):
        self.ax, self.fig = ax, ax.figure
        self.fig.canvas.draw()                       # fixes the axes box (equal aspect), so data -> pixel is final
        self.rn = self.fig.canvas.get_renderer()
        self.px = self.fig.dpi / 72.0                # pixels per point
        self.clear = clear * self.px                 # free rim kept round every label
        self.hard = []                               # (x0, y0, x1, y1) in pixels
        self.soft = np.zeros((0, 3))                 # x, y, weight
        self.frame = ax.get_window_extent(self.rn).extents

    def pix(self, xy):
        return self.ax.transData.transform(np.atleast_2d(np.asarray(xy, float)))

    def line(self, xy, w=1.0):
        """A polyline in data coordinates, sampled about every pixel."""
        p = self.pix(xy)
        if len(p) < 2:
            return
        seg = np.hypot(*np.diff(p, axis=0).T)
        d = np.concatenate([[0.0], np.cumsum(seg)])
        if d[-1] <= 0:
            return
        q = np.linspace(0.0, d[-1], int(d[-1]) + 2)
        pts = np.column_stack([np.interp(q, d, p[:, 0]), np.interp(q, d, p[:, 1]), np.full(len(q), w)])
        self.soft = np.vstack([self.soft, pts])

    def marker(self, x, y, r):
        """A marker of radius r points at a data position."""
        (cx, cy), r = self.pix((x, y))[0], r * self.px
        self.hard.append((cx - r, cy - r, cx + r, cy + r))

    def box(self, artist, grow=0.0):
        """A drawn artist (a text, a legend) as a box."""
        e = artist.get_window_extent(self.rn).extents
        g = grow * self.px
        self.hard.append((e[0] - g, e[1] - g, e[2] + g, e[3] + g))

    def band(self, x0, x1):
        """A shaded band over the whole height of the axes, in data x."""
        a, b = self.pix((x0, 0))[0][0], self.pix((x1, 0))[0][0]
        self.hard.append((min(a, b), self.frame[1], max(a, b), self.frame[3]))

    def size(self, t):
        """Width and height of a text in pixels, with the rim of its background box."""
        e = t.get_window_extent(self.rn)
        bp = t.get_bbox_patch()
        pad = 0.0 if bp is None else bp.get_boxstyle().pad * t.get_fontsize() * self.px
        return e.width + 2 * pad, e.height + 2 * pad

    def cost(self, x0, y0, x1, y1):
        c = self.clear
        f = (self.frame[0] + c, self.frame[1] + c, self.frame[2] - c, self.frame[3] - c)
        out = max(0.0, f[0] - x0) + max(0.0, x1 - f[2]) + max(0.0, f[1] - y0) + max(0.0, y1 - f[3])
        hard = 0.0
        for a0, b0, a1, b1 in self.hard:
            w, h = min(x1 + c, a1) - max(x0 - c, a0), min(y1 + c, b1) - max(y0 - c, b0)
            if w > 0 and h > 0:
                hard += 40.0 + w * h
        s = self.soft
        soft = s[(s[:, 0] > x0 - c) & (s[:, 0] < x1 + c) & (s[:, 1] > y0 - c) & (s[:, 1] < y1 + c), 2].sum() if len(s) else 0.0
        return 1e4 * out + 30.0 * hard + 6.0 * soft

    def leader_cost(self, a, b):
        """A leader from pixel a to pixel b: it may cross a line but should not, and must not cross a label or another marker."""
        n = int(np.hypot(b[0] - a[0], b[1] - a[1])) + 2
        q = np.column_stack([np.linspace(a[0], b[0], n), np.linspace(a[1], b[1], n)])[1:-1]
        c = 0.0
        for a0, b0, a1, b1 in self.hard:
            if a0 < a[0] < a1 and b0 < a[1] < b1:    # the marker the leader starts in (and one drawn on top of it)
                continue
            c += 25.0 * np.sum((q[:, 0] > a0) & (q[:, 0] < a1) & (q[:, 1] > b0) & (q[:, 1] < b1))
        if len(self.soft) and len(q):
            dd = np.hypot(self.soft[:, None, 0] - q[None, :, 0], self.soft[:, None, 1] - q[None, :, 1])
            c += 2.0 * (self.soft[:, 2] * (dd.min(axis=1) < 1.0)).sum()
        return c

    @staticmethod
    def around(r, gaps=(1.5, 4, 7, 11, 16, 22, 29), n=16, first=0.0):
        """Positions round a marker of radius r points: (angle, gap) pairs, the nearest ring first, starting at `first` degrees."""
        order = sorted(range(n), key=lambda k: min(k, n - k))        # the wanted side first, then alternately either way
        return [(math.radians(first + 360.0 * k / n), r + g) for g in gaps for k in order]

    def put(self, t, x, y, cands, leader=None, own=0.0, far=3.0, dry=False):
        """Move the text t (centre-anchored) to the cheapest candidate round the data point (x, y).

        cands: (angle, distance in points) pairs, see around(); the box touches the circle of that radius on that side.
        leader: line properties for a leader, drawn when the label is more than `far` points away from the marker edge.
        own: the marker radius in points (the leader starts at its edge). dry: return the price, move nothing."""
        t.set_ha("center")
        t.set_va("center")
        w, h = self.size(t)
        ax0, ay0 = self.pix((x, y))[0]
        best = None
        for k, (ang, dist) in enumerate(cands):
            d = dist * self.px
            ca, sa = math.cos(ang), math.sin(ang)
            cx = ax0 + d * ca + 0.5 * w * max(-1.0, min(1.0, 2.2 * ca))
            cy = ay0 + d * sa + 0.5 * h * max(-1.0, min(1.0, 2.2 * sa))
            x0, y0, x1, y1 = cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2
            c = self.cost(x0, y0, x1, y1) + 0.9 * dist + 0.02 * k
            tip = None
            if leader is not None and dist - own > far:
                tip = (min(max(ax0, x0), x1), min(max(ay0, y0), y1))        # the point of the box nearest the marker
                c += self.leader_cost((ax0, ay0), tip)
            if best is None or c < best[0]:
                best = (c, cx, cy, tip, (x0, y0, x1, y1))
        c, cx, cy, tip, rect = best
        if dry:                                      # only the price: to choose between two wordings or line breaks
            return c
        inv = self.ax.transData.inverted()
        t.set_position(inv.transform((cx, cy)))
        if tip is not None:
            a = inv.transform((ax0, ay0))
            b = inv.transform(tip)
            self.ax.plot([a[0], b[0]], [a[1], b[1]], **leader)
            self.line([a, b], w=3.0)
        self.hard.append(rect)
        return c

    def shift(self, t, max_pt=40.0, step=2.0, n=16, leader_to=None, leader=None):
        """Move a text as little as possible from where it stands until it is clear (place names: no marker to hold on to).

        leader_to: (x0, z0, x1, z1), the rectangle the name belongs to; a leader runs to it if the name leaves it."""
        x, y = t.get_position()
        cands = [(0.0, 0.0)] + [(2 * math.pi * k / n, g) for g in np.arange(step, max_pt + 0.1, step) for k in range(n)]
        t.set_ha("center")
        t.set_va("center")
        w, h = self.size(t)
        ax0, ay0 = self.pix((x, y))[0]
        best = None
        if leader_to is not None:
            p = self.pix([(leader_to[0], leader_to[1]), (leader_to[2], leader_to[3])])
            bx0, bx1, by0, by1 = min(p[:, 0]), max(p[:, 0]), min(p[:, 1]), max(p[:, 1])
        for ang, dist in cands:
            cx, cy = ax0 + dist * self.px * math.cos(ang), ay0 + dist * self.px * math.sin(ang)
            rect = (cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
            c = self.cost(*rect) + 0.9 * dist
            ends = None
            if leader_to is not None and (rect[2] < bx0 or rect[0] > bx1 or rect[3] < by0 or rect[1] > by1):   # outside the rectangle
                a = (min(max(cx, bx0), bx1), min(max(cy, by0), by1))
                ends = (a, (min(max(a[0], rect[0]), rect[2]), min(max(a[1], rect[1]), rect[3])))
                c += self.leader_cost(*ends) + 0.5 * math.hypot(ends[0][0] - ends[1][0], ends[0][1] - ends[1][1]) / self.px
            if best is None or c < best[0]:
                best = (c, cx, cy, rect, ends)
        c, cx, cy, rect, ends = best
        inv = self.ax.transData.inverted()
        t.set_position(inv.transform((cx, cy)))
        if ends is not None:
            a, b = inv.transform(ends[0]), inv.transform(ends[1])
            self.ax.plot([a[0], b[0]], [a[1], b[1]], **leader)
            self.line([a, b], w=3.0)
        self.hard.append(rect)
        return c


def settle_site_labels(lab, ax, bunker=True):
    """Move the place names of base() clear of what is registered in `lab`; names of places outside the view are dropped."""
    x0, x1 = sorted(ax.get_xlim())
    z0, z1 = sorted(ax.get_ylim())
    lab.box(ax.scale_bar[1], grow=0.5)
    lab.line(ax.scale_bar[0].get_xydata(), w=4.0)
    ring = np.linspace(0.0, 2 * math.pi, 721)
    lab.line(np.column_stack([L["perimeter_radius"] * np.cos(ring), L["perimeter_radius"] * np.sin(ring)]), w=0.5)   # the wall
    for name, (t, sx, sz) in list(ax.site_labels.items()):
        if not (x0 < sx < x1 and z0 < sz < z1):
            t.remove()
            del ax.site_labels[name]
            continue
        t.set_clip_on(False)
        lab.shift(t)
    if bunker and ax.bunker_label is not None:
        ax.bunker_label.set_bbox(dict(fc="white", ec="none", alpha=0.8, pad=0.35))
        lab.shift(ax.bunker_label, max_pt=60.0, leader_to=(L["bunker_min_x"], L["bunker_min_z"], L["bunker_max_x"], L["bunker_max_z"]),
                  leader=dict(color="0.25", lw=0.5, zorder=5.5))


# ---- post names of the roster: lower case like the place names, short forms written out, compass letters kept capital
FULL = {"aa": "anti-aircraft", "mg": "machine-gun", "ammo": "ammunition"}


def post_label(raw):
    words = []
    for w in raw.lower().split():
        m = re.fullmatch(r"mbt-(\d+)", w)             # MBT: main battle tank
        if m:
            words += ["tank", m.group(1)]
        else:
            words.append(FULL.get(w, w))
    if len(words) > 1 and words[-1] in ("n", "e", "s", "w", "ne", "nw", "se", "sw"):
        words[-1] = words[-1].upper()
    return " ".join(words)


def two_lines(name, least=13):
    """A long name broken at the space nearest its middle."""
    cut = [k for k, ch in enumerate(name) if ch == " "]
    if len(name) < least or not cut:
        return name
    k = min(cut, key=lambda q: abs(q - len(name) / 2.0))
    return name[:k] + "\n" + name[k + 1:]


def main():
    COL = ["#1f4e79", "#c0504d", "#4f8a3a", "#d08c1a", "#7a4f9a", "#3a8a8a", "#8c564b", "#d4508a"]
    G = routes["garrison"]["soldiers"]
    roster = routes["roster"]
    BUNKER = [(L["bunker_min_x"], L["bunker_min_z"]), (L["bunker_max_x"], L["bunker_min_z"]), (L["bunker_max_x"], L["bunker_max_z"]),
              (L["bunker_min_x"], L["bunker_max_z"]), (L["bunker_min_x"], L["bunker_min_z"])]
    MS = 5.5            # post marker size (points)

    def short(name):
        return name.split(" ", 2)[-1].title()

    fig, axs = plt.subplots(1, 2, figsize=(7.0, 3.85))
    fig.subplots_adjust(left=0.01, right=0.99, top=0.93, bottom=0.12, wspace=0.03)     # before the labels: they are placed in pixels
    panels = [(axs[0], 1, "(a) day shift (06:24 to 17:36)"), (axs[1], 0, "(b) night shift")]
    lines, posts = {0: [], 1: []}, {0: [], 1: []}
    for ax, shift, title in panels:
        base(ax, title)
        handles = []
        for s in G:
            i = s["index"]
            sh = s["shifts"][shift]
            col = COL[i % len(COL)]
            for r in sh["routes"]:
                if len(r) > 1:
                    a = np.array(r)
                    ax.plot(a[:, 0], a[:, 1], color=col, lw=1.4, alpha=0.9, zorder=4, solid_capstyle="round")
                    lines[shift].append(a[:, :2])
            for st in sh["stops"]:
                if sh["loop"]:
                    continue
                ax.plot(st["p"][0], st["p"][1], marker="v" if st.get("underground") else "o", ms=MS, mfc=col, mec="white", mew=0.8, zorder=5)
                posts[shift].append((st["p"][0], st["p"][1], st.get("label"), col))
            handles.append(Line2D([], [], color=col, lw=2, label="S%d %s" % (i + 1, short(roster[i]["name"]))))
        if shift == 1:
            fig.legend(handles=handles, loc="lower center", ncol=8, frameon=False, fontsize=6.6, handlelength=1.3, columnspacing=0.8)
    fig.text(0.5, 0.075, "lines: walked routes (Catmull-Rom splines from A* over the site plan); dots: posts; triangles: posts in the bunker",
             ha="center", fontsize=7, color="0.3")
    for ax, shift, title in panels:
        # place names first, then the post names: clear of this shift's routes and posts and of each other
        lab = Labels(ax)
        for a in lines[shift]:
            lab.line(a)
        for x, z, _, _ in posts[shift]:
            lab.marker(x, z, MS / 2 + 0.4)
        lab.line(BUNKER)
        settle_site_labels(lab, ax)
        todo = [(x, z, post_label(name), col) for x, z, name, col in posts[shift] if name]
        # the posts with the least room first: those with most routes and posts within 8 m
        crowd = lambda q: sum(np.sum(np.hypot(a[:, 0] - q[0], a[:, 1] - q[1]) < 8.0) for a in lines[shift]) \
            + 50 * sum(math.hypot(o[0] - q[0], o[1] - q[1]) < 8.0 for o in posts[shift])
        for x, z, name, col in sorted(todo, key=crowd, reverse=True):
            kw = dict(cands=Labels.around(MS / 2 + 0.8, first=22.5), leader=dict(color=col, lw=0.5, zorder=4.5), own=MS / 2 + 0.8)
            ts = [ax.text(x, z, v, fontsize=6, color=col, zorder=6, linespacing=0.95, bbox=dict(fc="white", ec="none", alpha=0.8, pad=0.3))
                  for v in dict.fromkeys([name, two_lines(name)])]
            price = [lab.put(t, x, z, dry=True, **kw) + 4.0 * k for k, t in enumerate(ts)]     # one line unless two lines fit clearly better
            for k, t in enumerate(ts):
                if k == int(np.argmin(price)):
                    lab.put(t, x, z, **kw)
                else:
                    t.remove()
    fig.savefig(os.path.join(OUT, "p_guard_map.pdf"))
    fig.savefig(os.path.join(OUT, "p_guard_map.png"), dpi=200)
    plt.close(fig)
    print("wrote p_guard_map")

    # ---- rover route map: each life of each intruder (split where it respawns), the objectives, the UAV paths
    fig, ax = plt.subplots(figsize=(7.0, 4.6))
    base(ax, "", r=24)
    rc = ["#c0504d", "#d08c1a", "#7a4f9a", "#1f4e79", "#3a8a8a", "#4f8a3a"]
    handles = []
    drawn, marks = [], []          # for the label placement: (polyline, weight), (x, z, radius in points)
    for i, tr in enumerate(routes["rover_tracks"]):
        if not tr:
            continue
        lives, cur = [], [tr[0]]
        for p in tr[1:]:
            if p[0] - cur[-1][0] > 1.0 or math.hypot(p[1] - cur[-1][1], p[2] - cur[-1][2]) > 3.0:
                lives.append(cur)
                cur = [p]
            else:
                cur.append(p)
        lives.append(cur)
        for k, life in enumerate(lives):
            a = np.array(life)
            if len(a) < 2:
                continue
            ax.plot(a[:, 1], a[:, 2], color=rc[i], lw=1.6, alpha=0.95 if k == 0 else 0.6, ls="-" if k == 0 else (0, (4, 2)), zorder=4)
            ax.plot(a[0, 1], a[0, 2], "o", ms=5, mfc="white", mec=rc[i], mew=1.4, zorder=5)
            ax.plot(a[-1, 1], a[-1, 2], "s", ms=4.5, mfc=rc[i], mec="white", mew=0.6, zorder=5)
            ax.annotate("", (a[len(a) // 2 + 1, 1], a[len(a) // 2 + 1, 2]), (a[len(a) // 2, 1], a[len(a) // 2, 2]),
                        arrowprops=dict(arrowstyle="-|>", color=rc[i], lw=1.2), zorder=5)
            drawn.append((a[:, 1:3], 1.0))
            marks += [(a[0, 1], a[0, 2], 3.4), (a[-1, 1], a[-1, 2], 2.8), (a[len(a) // 2 + 1, 1], a[len(a) // 2 + 1, 2], 3.0)]
        handles.append(Line2D([], [], color=rc[i], lw=2, label="rover %d" % (i + 1)))
    for u, tr in enumerate(routes["uav_tracks"]):
        a = np.array(tr)
        ax.plot(a[:, 1], a[:, 2], color="0.35", lw=0.7, ls=":", zorder=3)
        drawn.append((a[:, 1:3], 0.12))
    handles.append(Line2D([], [], color="0.35", lw=1, ls=":", label="UAV paths"))
    for j, o in enumerate(routes["objectives"]):
        ax.plot(o[0], o[2], marker="*", ms=12, mfc="#ffd24a", mec="black", mew=0.7, zorder=6)
        marks.append((o[0], o[2], 4.6))
    handles.append(Line2D([], [], marker="*", color="none", mfc="#ffd24a", mec="black", ms=11, label="objectives"))
    handles.append(Line2D([], [], marker="o", color="none", mfc="white", mec="0.2", ms=6, label="spawn"))
    handles.append(Line2D([], [], marker="s", color="none", mfc="0.2", mec="white", ms=6, label="end of a life"))
    handles.append(Line2D([], [], color="0.2", lw=1.5, ls=(0, (4, 2)), label="later life (respawned)"))
    leg = ax.legend(handles=handles, loc="center left", bbox_to_anchor=(1.0, 0.5), frameon=False, fontsize=7.5)
    # the map (a square as high as the figure) and the legend beside it, centred together on the figure width
    fig.subplots_adjust(left=0.0, right=0.76, top=0.99, bottom=0.01)
    fig.canvas.draw()
    W = fig.bbox.width
    mp, lg = ax.get_window_extent(), leg.get_window_extent()
    left = 0.5 * (W - (lg.x1 - mp.x0))
    ax.set_position([left / W, 0.01, mp.width / W, 0.98])
    lab = Labels(ax)
    for a, w in drawn:
        lab.line(a, w)
    for x, z, r in marks:
        lab.marker(x, z, r)
    lab.line(BUNKER, w=0.4)
    for j, o in enumerate(routes["objectives"]):
        t = ax.text(o[0], o[2], "O%d" % (j + 1), fontsize=7.5, fontweight="bold", zorder=7, bbox=dict(fc="white", ec="none", alpha=0.75, pad=0.2))
        lab.put(t, o[0], o[2], Labels.around(4.6, gaps=(1.5, 3, 5, 8), first=-45.0))
    settle_site_labels(lab, ax)
    fig.savefig(os.path.join(OUT, "p_rover_map.pdf"))
    fig.savefig(os.path.join(OUT, "p_rover_map.png"), dpi=200)
    plt.close(fig)
    print("wrote p_rover_map")


if __name__ == "__main__":
    main()
