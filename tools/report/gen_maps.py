"""Site maps for the report: the soldiers' guard map (day and night duty) and the intruder rovers' route map.

Inputs: a fresh export (`ts --export-docs build/routes_export.json`, schema 9 + garrison "plan" and simulation tracks)
for the routes, and report/data/scene_export_v10.json for the site plan (every part's top-view footprint). The route
data are copied to report/data/routes.json so the figures can be rebuilt without the program.
Run: ~/.local/bin/uv run -q --with matplotlib --with numpy python tools/report/gen_maps.py [build/routes_export.json]"""
import ast
import json
import math
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.collections import PolyCollection
from matplotlib.lines import Line2D

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "report", "figures", "plots")
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
    for name, key, dx, dz in [("outer gate", "outer_gate", 1.5, -1.0), ("helipad", "helipad", 0, 5.8), ("radar", "radar_site", -1, 4),
                              ("AA gun", "aa_gun_site", 0, -4.5), ("motor pool", "motor_pool", 0, -6.5), ("barracks", "barracks", 0, 7.5),
                              ("fuel depot", "fuel_depot", 0, -3.5)]:
        p = L[key]
        ax.text(p[0] + dx, p[2] + dz, name, fontsize=7, color="0.30", ha="center", va="center", zorder=6, clip_on=True,
                bbox=dict(fc="white", ec="none", alpha=0.7, pad=0.6))
    ax.text(0, -2.4, "command bunker\n(underground)", fontsize=6.5, color="0.25", ha="center", va="center", zorder=6)
    ax.set_xlim(-r, r)
    ax.set_ylim(r, -r)            # +z (the gate) at the bottom: north up with x to the right is not the scene's convention
    ax.set_aspect("equal")
    ax.set_xticks([])
    ax.set_yticks([])
    for s in ax.spines.values():
        s.set_visible(False)
    ax.set_title(title, fontsize=9)
    ax.plot([r - 13, r - 3], [r - 2.5, r - 2.5], color="black", lw=1.5)
    ax.text(r - 8, r - 3.8, "10 m", ha="center", fontsize=7)


def main():
    COL = ["#1f4e79", "#c0504d", "#4f8a3a", "#d08c1a", "#7a4f9a", "#3a8a8a", "#8c564b", "#d4508a"]
    G = routes["garrison"]["soldiers"]
    roster = routes["roster"]


    def short(name):
        return name.split(" ", 2)[-1].title()


    fig, axs = plt.subplots(1, 2, figsize=(7.0, 3.85))
    for ax, shift, title in [(axs[0], 1, "(a) day shift (06:24 to 17:36)"), (axs[1], 0, "(b) night shift")]:
        base(ax, title)
        handles = []
        used = []
        for s in G:
            i = s["index"]
            sh = s["shifts"][shift]
            col = COL[i % len(COL)]
            for r in sh["routes"]:
                if len(r) > 1:
                    a = np.array(r)
                    ax.plot(a[:, 0], a[:, 1], color=col, lw=1.4, alpha=0.9, zorder=4, solid_capstyle="round")
            for st in sh["stops"]:
                if sh["loop"]:
                    continue
                ax.plot(st["p"][0], st["p"][1], marker="v" if st.get("underground") else "o", ms=5.5, mfc=col, mec="white", mew=0.8, zorder=5)
                if st.get("label"):
                    lx, lz = st["p"][0] + 1.0, st["p"][1] - 1.0
                    while any(abs(lx - ux) < 7 and abs(lz - uz) < 1.9 for ux, uz in used):   # stack labels that would overlap
                        lz += 2.1
                    used.append((lx, lz))
                    ax.text(lx, lz, st["label"].title(), fontsize=6, color=col, zorder=6,
                            bbox=dict(fc="white", ec="none", alpha=0.75, pad=0.3))
            handles.append(Line2D([], [], color=col, lw=2, label="S%d %s" % (i + 1, short(roster[i]["name"]))))
        if shift == 1:
            fig.legend(handles=handles, loc="lower center", ncol=8, frameon=False, fontsize=6.6, handlelength=1.3, columnspacing=0.8)
    fig.text(0.5, 0.075, "lines: walked routes (Catmull-Rom splines from A* over the site plan); dots: posts; triangles: posts in the bunker",
             ha="center", fontsize=7, color="0.3")
    fig.subplots_adjust(left=0.01, right=0.99, top=0.93, bottom=0.12, wspace=0.03)
    fig.savefig(os.path.join(OUT, "p_guard_map.pdf"))
    fig.savefig(os.path.join(OUT, "p_guard_map.png"), dpi=200)
    plt.close(fig)
    print("wrote p_guard_map")

    # ---- rover route map: each life of each intruder (split where it respawns), the objectives, the UAV paths
    fig, ax = plt.subplots(figsize=(7.0, 4.6))
    base(ax, "", r=24)
    rc = ["#c0504d", "#d08c1a", "#7a4f9a", "#1f4e79", "#3a8a8a", "#4f8a3a"]
    handles = []
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
        handles.append(Line2D([], [], color=rc[i], lw=2, label="rover %d" % (i + 1)))
    for u, tr in enumerate(routes["uav_tracks"]):
        a = np.array(tr)
        ax.plot(a[:, 1], a[:, 2], color="0.35", lw=0.7, ls=":", zorder=3)
    handles.append(Line2D([], [], color="0.35", lw=1, ls=":", label="UAV paths"))
    for j, o in enumerate(routes["objectives"]):
        ax.plot(o[0], o[2], marker="*", ms=12, mfc="#ffd24a", mec="black", mew=0.7, zorder=6)
        ax.text(o[0] + 1.2, o[2] + 1.6, "O%d" % (j + 1), fontsize=7.5, fontweight="bold", zorder=7)
    handles.append(Line2D([], [], marker="*", color="none", mfc="#ffd24a", mec="black", ms=11, label="objectives"))
    handles.append(Line2D([], [], marker="o", color="none", mfc="white", mec="0.2", ms=6, label="spawn"))
    handles.append(Line2D([], [], marker="s", color="none", mfc="0.2", mec="white", ms=6, label="end of a life"))
    handles.append(Line2D([], [], color="0.2", lw=1.5, ls=(0, (4, 2)), label="later life (respawned)"))
    ax.legend(handles=handles, loc="center left", bbox_to_anchor=(1.0, 0.5), frameon=False, fontsize=7.5)
    fig.subplots_adjust(left=0.0, right=0.76, top=0.99, bottom=0.01)
    fig.savefig(os.path.join(OUT, "p_rover_map.pdf"))
    fig.savefig(os.path.join(OUT, "p_rover_map.png"), dpi=200)
    plt.close(fig)
    print("wrote p_rover_map")


if __name__ == "__main__":
    main()
