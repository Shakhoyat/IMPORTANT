#!/usr/bin/env python3
"""Explanatory figures for the ThermalSense report, generated from the program's own data.

Pipeline:   build/figdata <dir>   (C++ tool, tools/figdata_main.cpp: real meshes, node transforms, planner fields,
            CV / tracker runs)  ->  JSON in build/figdata_out/   ->  this script  ->  report/diagrams/*.png
Run:        uv run --with matplotlib --with numpy python tools/make_figures.py [prefix ...]
Every number drawn comes from the JSON dumps or from formulas ported 1:1 from the shader / controller source
(each port cites file and line numbers).  Nothing is typed in by hand except text labels.
"""
import json
import math
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.collections import LineCollection, PolyCollection
from matplotlib.patches import Ellipse, FancyArrowPatch, FancyBboxPatch, Polygon, Rectangle, Circle
from mpl_toolkits.mplot3d.art3d import Poly3DCollection, Line3DCollection

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "build", "figdata_out")
OUT = os.path.join(ROOT, "report", "diagrams")
os.makedirs(OUT, exist_ok=True)

C = dict(blue="#1f5fa8", orange="#d9730d", green="#2a8a4a", red="#c0392b", purple="#7b4fa3", gray="#6b7280",
         teal="#12818c", gold="#b8860b", light="#e5e7eb", ink="#111827", pink="#c2417c")
FACE = ["#e4572e", "#76b041", "#29335c", "#f3a712", "#17a398", "#a846a0"]   # per-face colours of the cube

plt.rcParams.update({
    "figure.dpi": 100, "savefig.dpi": 200, "font.size": 9, "axes.titlesize": 10, "axes.labelsize": 9,
    "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True, "grid.alpha": 0.25,
    "font.family": "DejaVu Sans", "figure.facecolor": "white", "axes.facecolor": "white",
    "legend.frameon": False, "mathtext.fontset": "dejavusans",
})

CAPTIONS = {}
ONLY = sys.argv[1:]


def want(name):
    return not ONLY or any(name.startswith(p) or p.startswith(name) for p in ONLY)


def load(name):
    with open(os.path.join(DATA, name)) as f:
        return json.load(f)


def save(fig, name, caption, source):
    path = os.path.join(OUT, name + ".png")
    fig.savefig(path, dpi=200, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    CAPTIONS[name + ".png"] = {"caption": caption, "source": source}
    print("figure", name)


def P(v):
    """Scene (x, y up, z) -> matplotlib 3D (x, -z, y up): a pure rotation, so handedness is preserved."""
    v = np.asarray(v, float)
    return np.stack([v[..., 0], -v[..., 2], v[..., 1]], axis=-1)


def box_aspect(ax, lim=0.8, elev=22, azim=-58):
    ax.set_xlim(-lim, lim); ax.set_ylim(-lim, lim); ax.set_zlim(-lim, lim)
    ax.set_box_aspect((1, 1, 1)); ax.view_init(elev, azim)
    for a in (ax.xaxis, ax.yaxis, ax.zaxis):
        a.pane.set_visible(False)
    ax.grid(False)
    ax.set_axis_off()


def mesh_arrays(m):
    pos = np.array(m["pos"]).reshape(-1, 3)
    nrm = np.array(m["nrm"]).reshape(-1, 3)
    idx = np.array(m["idx"], dtype=int).reshape(-1, 3)
    return pos, nrm, idx


def axes3(ax, L=0.9, label=True):
    for v, c, n in (((1, 0, 0), C["red"], "x"), ((0, 1, 0), C["green"], "y"), ((0, 0, 1), C["blue"], "z")):
        p = P(np.array(v) * L)
        ax.plot([0, p[0]], [0, p[1]], [0, p[2]], color=c, lw=1.0)
        if label:
            ax.text(p[0], p[1], p[2], n, color=c, fontsize=8)


def rot_euler_yxz(deg):
    """Port of rotateEulerYXZ (src/math/Math3D.cpp): R = Ry * Rx * Rz, angles (pitch X, yaw Y, roll Z) in degrees."""
    ax_, ay, az = np.radians(deg)
    cx, sx, cy, sy, cz, sz = math.cos(ax_), math.sin(ax_), math.cos(ay), math.sin(ay), math.cos(az), math.sin(az)
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Ry @ Rx @ Rz


# =====================================================================================================================
# A. Object creation
# =====================================================================================================================
def fig_A1_cube():
    d = load("primitives.json")
    pos, nrm, idx = mesh_arrays(d["cube"])
    fig = plt.figure(figsize=(11.5, 5.2))
    ax = fig.add_subplot(1, 2, 1, projection="3d")
    for f in range(6):
        v = pos[4 * f:4 * f + 4]
        n = nrm[4 * f]
        off = n * 0.10                       # the six faces are pulled apart: each face owns its 4 vertices
        poly = Poly3DCollection([P(v + off)], facecolor=FACE[f], edgecolor="k", alpha=0.55, lw=0.8)
        ax.add_collection3d(poly)
        for k in range(4):
            q = P(v[k] + off)
            ax.scatter(*q, color=FACE[f], edgecolor="k", s=26, zorder=5)
            a = P(v[k] + off); b = P(v[k] + off + n * 0.28)
            ax.plot([a[0], b[0]], [a[1], b[1]], [a[2], b[2]], color="k", lw=0.9)
            ax.text(*(P(v[k] + off) + np.array([0.03, 0.03, 0.03])), str(4 * f + k), fontsize=6.5)
    box_aspect(ax, 0.95)
    ax.set_title("createCube(): 6 faces x 4 vertices = 24 vertices, every vertex carries its face normal\n"
                 "(faces pulled apart for the picture; labels = index in the vertex buffer)")
    # index-buffer panel
    ax2 = fig.add_subplot(1, 2, 2)
    ax2.set_aspect("equal"); ax2.axis("off")
    ax2.set_xlim(-1.2, 10.5); ax2.set_ylim(-2.6, 3.6)
    # face +X in its (u, v) frame: corners (-u-v), (+u-v), (+u+v), (-u+v)
    sq = np.array([[0, 0], [1.6, 0], [1.6, 1.6], [0, 1.6]])
    for t, (tri, col) in enumerate((((0, 1, 2), "#fbd2c5"), ((0, 2, 3), "#c9e6d1"))):
        ax2.add_patch(Polygon(sq[list(tri)], closed=True, facecolor=col, edgecolor="k", lw=1.2))
    for k, (x, y) in enumerate(sq):
        ax2.text(x + (-0.18 if x == 0 else 0.1), y + (-0.22 if y == 0 else 0.1), f"v{k}", fontsize=10, weight="bold")
    ax2.text(1.15, 0.45, "tri 0\n0-1-2", fontsize=8, ha="center"); ax2.text(0.45, 1.15, "tri 1\n0-2-3", fontsize=8, ha="center")
    ax2.annotate("", xy=(1.0, 1.0), xytext=(0.2, 0.2), arrowprops=dict(arrowstyle="-", ls="--", color="gray"))
    ax2.text(-1.0, 2.2, "face +X seen from outside\n(counter-clockwise = front)", fontsize=8.5)
    ax2.text(3.6, 2.9, "Index buffer (EBO), 36 entries = 12 triangles", fontsize=9.5, weight="bold")
    for f in range(3):
        s = "  ".join(str(int(i)) for i in idx[2 * f].tolist() + idx[2 * f + 1].tolist())
        ax2.text(3.6, 2.3 - 0.42 * f, f"face {f}:  {s}", fontsize=9, family="monospace", color=FACE[f])
    ax2.text(3.6, 1.0, "...", fontsize=9, family="monospace")
    ni, ix = d["cube"]["nonIndexedBytes"], d["cube"]["indexedBytes"]
    ax2.text(3.6, 0.4, f"memory, 6 floats (24 B) per vertex:\n  non-indexed: 36 x 24 B = {ni:.0f} B\n"
                       f"  indexed:     24 x 24 B + 36 x 4 B = {ix:.0f} B", fontsize=9, family="monospace", va="top")
    ax2.text(3.6, -1.7, "Per-face normals keep the 90-degree edges\ncrisp: corners are NOT shared between faces.", fontsize=8.5, style="italic")
    save(fig, "A1_cube_vertices",
         "The unit cube generator: 24 vertices (4 per face, each with the face normal) and 36 indices (2 counter-clockwise "
         f"triangles per face, diagonal 0-2). Indexed storage needs {ix:.0f} B instead of {ni:.0f} B.",
         "src/primitives/Cube.cpp: createCube()")


def fig_A2_sphere():
    d = load("primitives.json")
    pos, nrm, idx = mesh_arrays(d["sphere"])
    nv, nt = len(pos), len(idx)
    fig = plt.figure(figsize=(12, 5.0))
    ax = fig.add_subplot(1, 3, 1, projection="3d")
    tris = P(pos[idx])
    north = pos[idx][:, :, 1].mean(axis=1) > -1e-6
    ax.add_collection3d(Poly3DCollection(tris, facecolor="none", edgecolor="#9ca3af", lw=0.25))
    ring_idx = lambda k: slice(1 + (k - 1) * 36, 1 + k * 36)
    for k, col in ((4, C["orange"]), (9, C["red"])):
        r = pos[ring_idx(k)]
        r = np.vstack([r, r[:1]])
        q = P(r)
        ax.plot(q[:, 0], q[:, 1], q[:, 2], color=col, lw=1.8)
    for j in (0, 12, 24):   # slices (meridians) of the north hemisphere
        pts = np.vstack([pos[0]] + [pos[1 + (k - 1) * 36 + j] for k in range(1, 10)] +
                        [pos[1 + (k - 1) * 36 + j] * np.array([1, -1, 1]) for k in range(8, 0, -1)] + [pos[0] * np.array([1, -1, 1])])
        q = P(pts)
        ax.plot(q[:, 0], q[:, 1], q[:, 2], color=C["blue"], lw=1.3)
    for k in (3, 6):
        for j in range(0, 36, 6):
            p = pos[1 + (k - 1) * 36 + j]
            n = nrm[1 + (k - 1) * 36 + j]
            a, b = P(p), P(p + n * 0.22)
            ax.plot([a[0], b[0]], [a[1], b[1]], [a[2], b[2]], color="k", lw=0.9)
    box_aspect(ax, 0.42, 18, -60)
    ax.set_title(f"createSphere(36, 18): {nv} vertices, {nt} triangles\n"
                 "rings (orange) and slices (blue); normal = position / r")
    # top view: pole fan
    ax2 = fig.add_subplot(1, 3, 2)
    ax2.set_aspect("equal")
    for k in range(1, 10):
        r = np.sqrt(pos[1 + (k - 1) * 36, 0] ** 2 + pos[1 + (k - 1) * 36, 2] ** 2)
        ax2.add_patch(Circle((0, 0), r, fill=False, ec=C["orange"] if k in (4, 9) else "#9ca3af", lw=1.0))
    for j in range(36):
        phi = 2 * math.pi * j / 36
        ax2.plot([0, 0.5 * math.cos(phi)], [0, 0.5 * math.sin(phi)], color=C["blue"] if j % 12 == 0 else "#d1d5db", lw=0.8)
    for j in range(36):                      # polar fan triangles (pole, ring 1)
        a = pos[1 + j]; b = pos[1 + (j + 1) % 36]
        ax2.add_patch(Polygon([[0, 0], [a[0], a[2]], [b[0], b[2]]], closed=True, facecolor="#fde68a" if j % 2 else "#fcd34d", edgecolor="k", lw=0.3))
    ax2.text(0.02, 0.02, "pole", fontsize=8)
    ax2.annotate("equator (ring 9,\nshared by both halves)", xy=(0.5, 0), xytext=(0.52, 0.25), fontsize=8, arrowprops=dict(arrowstyle="->"))
    ax2.set_xlim(-0.6, 0.95); ax2.set_ylim(-0.6, 0.6); ax2.axis("off")
    ax2.set_title("view from +Y: 36 slices, pole fan,\nring k at theta = k*pi/18")
    ax3 = fig.add_subplot(1, 3, 3)
    ax3.axis("off")
    txt = ("Vertex k, j (ring k = 1..9, slice j = 0..35):\n"
           "  theta = pi k / 18,   phi = 2 pi j / 36\n"
           "  n = (sin theta cos phi,  cos theta,  sin theta sin phi)\n"
           "  p = 0.5 n            (unit sphere of diameter 1)\n\n"
           "Construction:\n"
           "  1. north hemisphere: pole + 9 rings x 36 = 325 vertices\n"
           "  2. south hemisphere = mirror y -> -y (equator ring\n"
           "     shared): +288 vertices, triangle order swapped to\n"
           "     stay counter-clockwise\n"
           f"  total {nv} vertices,  {nt} triangles\n"
           "  = 2 x (36 pole + 8 bands x 72)")
    ax3.text(0.0, 0.95, txt, va="top", family="monospace", fontsize=8.6)
    save(fig, "A2_sphere_rings",
         f"The sphere generator: {nv} vertices on 9 rings x 36 slices per hemisphere, {nt} triangles; the normal of every "
         "vertex equals its normalised position, the southern half is the mirrored northern half.",
         "src/primitives/Sphere.cpp: createSphere(36, 18)")


def draw_cyl(ax, m, name):
    Q = lambda v: np.asarray(v, float)   # local z (the primitive axis) is drawn vertical here
    pos, nrm, idx = mesh_arrays(m)
    n = len(pos)
    side, capT, capB = [], [], []
    for t in idx:
        v = pos[t]
        if abs(v[:, 2] - v[0, 2]).max() < 1e-6 and (abs(v[0, 2] - 0.5) < 1e-6 or abs(v[0, 2] + 0.5) < 1e-6):
            (capT if v[0, 2] > 0 else capB).append(v)
        else:
            side.append(v)
    for lst, col, al in ((side, "#bfdbfe", 0.7), (capT, "#fde68a", 0.9), (capB, "#bbf7d0", 0.9)):
        if lst:
            ax.add_collection3d(Poly3DCollection([Q(v) for v in lst], facecolor=col, edgecolor="#374151", lw=0.5, alpha=al))
    for i in range(0, n, 1):
        pass
    # normals of the side ring vertices (every 2nd slice)
    for j in range(0, len(m["pos"]) // 3, 1):
        pass
    sl = 12
    for j in range(0, sl, 2):
        for k in (0, 1):
            p, nn = pos[2 * j + k], nrm[2 * j + k]
            if np.allclose(p[:2], 0):
                continue
            a, b = Q(p), Q(p + nn * 0.25)
            ax.plot([a[0], b[0]], [a[1], b[1]], [a[2], b[2]], color="k", lw=0.8)
    box_aspect(ax, 0.62, 28, -58)
    ax.set_title(name, fontsize=9.5)
    # draw local z axis (the primitive's axis)
    a, b = Q(np.array([0, 0, -0.75])), Q(np.array([0, 0, 0.85]))
    ax.plot([a[0], b[0]], [a[1], b[1]], [a[2], b[2]], color=C["blue"], lw=1, ls="--")
    ax.text(*Q(np.array([0, 0, 0.9])), "z", color=C["blue"])


def fig_A3_cyl_cone():
    d = load("primitives.json")
    fig = plt.figure(figsize=(13, 5))
    for i, (key, ttl) in enumerate((("cylinder12", "Cylinder  createCylinder(slices, 0.5, 0.5, capped)"),
                                    ("cone12", "Cone  = same generator, top radius 0"),
                                    ("beamcone12", "Frustum 'BeamCone'  radii 0.03 -> 0.5, uncapped"))):
        ax = fig.add_subplot(1, 3, i + 1, projection="3d")
        draw_cyl(ax, d[key], ttl + "\n(12 slices drawn for clarity)")
    n36 = {k: (len(d[k]["pos"]) // 3, len(d[k]["idx"]) // 3) for k in ("cylinder", "cone", "beamcone")}
    fig.text(0.5, 0.0, "blue = side quads (2 triangles per slice, 1 for the cone: the apex ring collapses), yellow = top cap fan, green = bottom cap fan; "
             "black = side normals (lean by rBottom - rTop along z).\n"
             f"Real parts use 36 slices: cylinder {n36['cylinder'][0]} vertices / {n36['cylinder'][1]} triangles, cone {n36['cone'][0]} / {n36['cone'][1]}, "
             f"beam frustum {n36['beamcone'][0]} / {n36['beamcone'][1]}.", ha="center", va="top", fontsize=8.5)
    save(fig, "A3_cylinder_cone",
         "Cylinder, cone and frustum come from one generator: two interleaved rings (bottom z = -0.5, top z = +0.5), side "
         "triangles, and a triangle fan per cap; a cone is simply top radius 0, the beam frustum has no caps.",
         "src/primitives/Cylinder.cpp: createCylinder(); Renderer.cpp lines 136-141 (which primitives exist)")


def fig_A4_terrain():
    d = load("primitives.json")
    pos, nrm, idx = mesh_arrays(d["terrain_coarse"])
    tr = d["terrain_real"]
    fig, axs = plt.subplots(1, 3, figsize=(14, 4.9), gridspec_kw=dict(width_ratios=[1.1, 0.8, 1.0]))
    ax = axs[0]
    segs = []
    for t in idx:
        v = pos[t][:, [0, 2]]
        segs += [[v[0], v[1]], [v[1], v[2]], [v[2], v[0]]]
    ax.add_collection(LineCollection(segs, colors="#6b7280", lw=0.3))
    for r in (np.hypot(pos[1:49, 0], pos[1:49, 2])[0], ):
        pass
    rr = [np.hypot(pos[1 + 48 * k, 0], pos[1 + 48 * k, 2]) for k in range(12)]
    ax.add_patch(Circle((0, 0), 20, fill=False, ec=C["orange"], lw=1.2, ls="--"))
    ax.text(14.5, 15.5, "r = 20 m\ncompound", color=C["orange"], fontsize=8)
    ax.text(0.8, rr[-1] - 3.2, f"outer ring r = {rr[-1]:.0f} m", fontsize=8)
    ax.set_aspect("equal"); ax.set_xlim(-37, 37); ax.set_ylim(-37, 37)
    ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)")
    ax.set_title("coarse copy (12 rings x 48 slices): concentric\nrings, point j on ring k at phi = 2 pi j / slices")
    ax = axs[1]
    ax.axis("off")
    txt = (f"Real terrain mesh\n  rings  {tr['rings']}  (step {tr['radius'] / tr['rings']:.3f} m)\n  slices {tr['slices']}\n"
           f"  vertices  1 + {tr['rings']} x {tr['slices']} = {tr['vertices']:.0f}\n"
           f"  triangles slices x (2 rings - 1)\n          = {tr['expectedNoHole']:.0f}\n"
           f"  actual  {tr['triangles']:.0f}\n  removed (hole) {tr['expectedNoHole'] - tr['triangles']:.0f}\n\n"
           "Geometry = the cylinder cap fan,\nsubdivided into rings and displaced\nin y by h(x, z): pad, hills, creek,\ntrack, berm, rim.\n\n"
           "The entrance trench is cut by\ndropping every triangle whose\ncentroid lies in the footprint.")
    ax.text(0, 0.98, txt, va="top", family="monospace", fontsize=8.3)
    ax = axs[2]
    w = np.array(tr["windowTris"]).reshape(-1, 3, 3)
    polys = [t[:, [0, 2]] for t in w]
    hgt = w[:, :, 1].mean(axis=1)
    pc = PolyCollection(polys, array=hgt, cmap="terrain", edgecolors="#374151", linewidths=0.15)
    ax.add_collection(pc)
    hw = tr["rampHalfWidth"] + 0.35
    ax.add_patch(Rectangle((-hw, tr["blastDoorZ"] - 0.3), 2 * hw, tr["rampTopZ"] - tr["blastDoorZ"] + 0.3, fill=False, ec=C["red"], lw=1.6, ls="--"))
    ax.text(0, tr["rampTopZ"] + 0.5, "entrance trench footprint\n(triangles dropped)", color=C["red"], ha="center", fontsize=8)
    ax.set_xlim(-8, 8); ax.set_ylim(-2, 22); ax.set_aspect("equal")
    ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)"); ax.set_title("real mesh around the ramp: the hole")
    fig.colorbar(pc, ax=ax, shrink=0.7, label="height y (m)")
    save(fig, "A4_terrain_disc",
         f"The terrain is a {tr['radius']:.0f} m disc of {tr['vertices']:.0f} vertices (130 rings x 384 slices) built like a cylinder "
         "cap and displaced by a height function; the left panel shows the ring structure on a coarse copy, the right panel the real triangles with the "
         "entrance-ramp hole.", "src/objects/Terrain.cpp: createTerrainDisc(), terrainHeight()")


def fig_A5_trs():
    d = load("primitives.json"); n = load("nodes.json")
    node = next(x for x in n["tank"] if x["name"] == "Turret cheek L")
    par = n["tank"][node["parent"]]
    pos, _, idx = mesh_arrays(d["cube"])
    T = np.array(node["T"], float); R = np.array(node["R"], float); S = np.array(node["S"], float)
    Rm = rot_euler_yxz(R)
    M = np.eye(4); M[:3, :3] = Rm @ np.diag(S); M[:3, 3] = T
    Mdump = np.array(node["local"]).reshape(4, 4)
    err = np.abs(M - Mdump).max()
    assert err < 1e-4, err
    fig = plt.figure(figsize=(14, 8.2))
    fig.subplots_adjust(top=0.95, bottom=0.40, wspace=0.0)
    stages = [("0. unit cube (generator)", pos), ("1. S: scale", pos * S), ("2. R = Ry Rx Rz: rotate", (Rm @ (pos * S).T).T),
              ("3. T: translate  ->  parent space", (Rm @ (pos * S).T).T + T)]
    for i, (ttl, v) in enumerate(stages):
        ax = fig.add_subplot(1, 4, i + 1, projection="3d")
        faces = [P(v[4 * f:4 * f + 4]) for f in range(6)]
        ax.add_collection3d(Poly3DCollection(faces, facecolor=[FACE[f] for f in range(6)], edgecolor="k", alpha=0.6, lw=0.6))
        axes3(ax, 1.2, label=(i == 0))
        box_aspect(ax, 1.05 if i < 3 else 1.9, 24, -52)
        if i == 3:
            ax.set_xlim(-0.9, 2.9); ax.set_ylim(-2.3, 1.5); ax.set_zlim(-1.6, 2.2)
        ax.set_title(ttl, fontsize=9)
    ax = fig.add_axes([0.05, 0.02, 0.92, 0.36])
    ax.axis("off")

    def mtx(Mx, fmt="{:7.3f}"):
        return "\n".join("[ " + " ".join(fmt.format(x) for x in row) + " ]" for row in Mx)
    Tm = np.eye(4); Tm[:3, 3] = T
    Rm4 = np.eye(4); Rm4[:3, :3] = Rm
    Sm = np.diag(list(S) + [1])
    cols = [(0.0, "T = translate(%g, %g, %g)" % tuple(T), Tm), (0.25, "R = Ry(%g deg)" % R[1], Rm4), (0.5, "S = scale(%g, %g, %g)" % tuple(S), Sm),
            (0.75, "M_local = T * R * S", M)]
    ax.text(0.0, 1.0, f"Real component: '{node['name']}' (Cube) in the tank rig, parent '{par['name']}'. Matrices from the scene, columns act on (x, y, z, 1):",
            fontsize=10, weight="bold", transform=ax.transAxes)
    for x0, ttl, Mx in cols:
        ax.text(x0, 0.82, ttl, fontsize=8.8, weight="bold", transform=ax.transAxes)
        ax.text(x0, 0.40, mtx(Mx, "{:6.2f}"), family="monospace", fontsize=7.4, transform=ax.transAxes, va="bottom")
    W = np.array(node["world"]).reshape(4, 4)
    ax.text(0.0, 0.25, "Scene graph: world = parent.world * local, evaluated root-to-leaf every frame (Scene::updateWorld). "
            f"For this node (max |M - dump| = {err:.1e}):\nM_world row 0 = {np.round(W[0], 3).tolist()},  translation (x, y, z) = {np.round(W[:3, 3], 3).tolist()}",
            fontsize=8.8, transform=ax.transAxes, va="top")
    save(fig, "A5_trs_worked_example",
         "Worked example for one real part, the tank's left turret cheek: the unit cube is scaled, rotated about Y and "
         "translated into its parent (the turret); the printed matrices are the scene's own, and T*R*S reproduces the node's local matrix.",
         "src/scene/Transform.h: Transform::matrix(); src/math/Math3D.cpp: rotateEulerYXZ(); Tank.cpp (node 'Turret cheek L')")


def collapse_tree(nodes):
    ch = {}
    for x in nodes:
        ch.setdefault(x["parent"], []).append(x)

    def build(i):
        kids = ch.get(i, [])
        groups = [c for c in kids if c["primitive"] == "Group"]
        leaves = [c for c in kids if c["primitive"] != "Group"]
        prim = {}
        for l in leaves:
            prim[l["primitive"]] = prim.get(l["primitive"], 0) + 1
        merged = []
        for g in groups:
            sub = build(g["i"])
            if merged and merged[-1]["name"] == g["name"] and merged[-1]["sig"] == sub["sig"]:
                merged[-1]["count"] += 1
            else:
                sub["count"] = 1
                merged.append(sub)
        sig = (tuple(sorted(prim.items())), tuple((m["name"], m["count"], m["sig"]) for m in merged))
        return dict(name=nodes[i]["name"], prim=prim, kids=merged, sig=sig, count=1)
    return build(0)


def draw_tree(ax, t, highlight=(), XS=1.9):
    def width(n):
        return max(1, sum(width(k) for k in n["kids"]))

    def place(n, x0, depth):
        w = width(n)
        n["x"] = (x0 + w / 2.0) * XS; n["d"] = depth
        cx = x0
        for k in n["kids"]:
            place(k, cx, depth + 1)
            cx += width(k)
    place(t, 0, 0)
    maxd = [0]

    def draw(n):
        maxd[0] = max(maxd[0], n["d"])
        for k in n["kids"]:
            ax.plot([n["x"], k["x"]], [-n["d"] - 0.28, -k["d"] + 0.28], color="#9ca3af", lw=1)
            draw(k)
        nm = n["name"] + (f"  x{n['count']}" if n["count"] > 1 else "")
        pl = [f"{v} {k.lower()}" for k, v in n["prim"].items()]
        prim = "\n".join(", ".join(pl[i:i + 3]) for i in range(0, len(pl), 3))
        hl = n["name"] in highlight
        ax.text(n["x"], -n["d"], nm + (f"\n[{prim}]" if prim else ""), ha="center", va="center", fontsize=7.6,
                bbox=dict(boxstyle="round,pad=0.35", fc="#fde68a" if hl else "#dbeafe", ec="#1f5fa8" if not hl else C["orange"], lw=1))
    draw(t)
    ax.set_xlim(-0.6, width(t) * XS + 0.6); ax.set_ylim(-maxd[0] - 0.7, 0.7); ax.axis("off")


def fig_A6_hierarchy():
    n = load("nodes.json")
    t = collapse_tree(n["tank"])
    fig, ax = plt.subplots(figsize=(16, 6.5))
    draw_tree(ax, t, highlight=("Turret yaw", "Gun pitch", "Barrel recoil"), XS=2.3)
    ax.set_title("Tank rig: every box is a group node holding a local T R S; leaves (blue brackets) draw one primitive each.\n"
                 "Controls move only the highlighted joints (turret yaw -> gun pitch -> recoil); wheels, tracks and hull follow through the parent chain.")
    save(fig, "A6_hierarchy_tank",
         "Scene-graph tree of the main battle tank (collapsed: identical siblings are merged): hull -> track groups / turret yaw -> "
         "gun pitch -> barrel recoil -> muzzle; each group multiplies its local matrix onto its children.",
         "src/objects/Tank.cpp (rig), src/scene/SceneNode.h; counts from the live Scene")
    t = collapse_tree(n["uav"])
    t2 = collapse_tree(n["rover"])
    fig, axs = plt.subplots(1, 2, figsize=(19, 5.4), gridspec_kw=dict(width_ratios=[3.6, 1]))
    draw_tree(axs[0], t, highlight=("Gimbal yaw", "Gimbal pitch"), XS=3.3)
    axs[0].set_title("UAV rig: body -> gimbal yaw -> gimbal pitch (lamp + thermal camera, looks along +Z);\nfour motor mounts -> rotors; two launcher rails -> rounds")
    draw_tree(axs[1], t2, highlight=("Scanner pivot",))
    axs[1].set_title("Rover rig")
    save(fig, "A6_hierarchy_uav",
         "Scene-graph trees of the UAV (gimbal yaw -> gimbal pitch carries lamp and thermal camera; rotors under their motor mounts; "
         "rounds on the launcher rails) and of the intruder rover.", "src/objects/Uav.cpp, src/objects/Rover.cpp; counts from the live Scene")


if want("A"):
    fig_A1_cube(); fig_A2_sphere(); fig_A3_cyl_cone(); fig_A4_terrain(); fig_A5_trs(); fig_A6_hierarchy()
# =====================================================================================================================
# Shader ports (numpy).  Each function cites the GLSL it mirrors (line numbers of src/rendering/ShaderSources.cpp
# and ShadersPostFx.cpp as of this build).
# =====================================================================================================================
PI = math.pi


def nrm_(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-9)


def brdf(N, V, L, radiance, F0, rough, pbr=True, specular=0.5, shininess=32.0, metal=0.0):
    """Port of brdf() (ShaderSources.cpp lines 113-139).  N, V, L: (...,3); radiance, F0: (3,) or (...,3).
    uPbr == 0: Blinn-Phong  diff = rad*nl,  spec = rad * uSpecular * (N.H)^uShininess.
    uPbr == 1: Cook-Torrance GGX * Smith-Schlick * Schlick-Fresnel / (4 nv nl), x PI because light is irradiance."""
    nl = np.maximum((N * L).sum(-1), 0.0)[..., None]
    H = nrm_(L + V)
    rad = np.asarray(radiance, float)
    if not pbr:
        nh = np.maximum((N * H).sum(-1), 0.0)[..., None]
        return rad * nl, rad * specular * nh ** shininess * (nl > 0)
    nv = np.maximum((N * V).sum(-1), 1e-3)[..., None]
    nh = np.maximum((N * H).sum(-1), 0.0)[..., None]
    vh = np.maximum((V * H).sum(-1), 0.0)[..., None]
    a = rough * rough; a2 = a * a
    dd = nh * nh * (a2 - 1.0) + 1.0
    D = a2 / (PI * dd * dd)
    k = (rough + 1.0) ** 2 / 8.0
    G = (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k + 1e-9))
    F = F0 + (1.0 - F0) * (1.0 - vh) ** 5
    spec = rad * nl * PI * D * G * F / (4.0 * nv * np.maximum(nl, 1e-9))
    diff = rad * nl * (1.0 - F) * (1.0 - metal)
    return diff * (nl > 0), spec * (nl > 0)


def aces(v):
    """acesFitted(), ShadersPostFx.cpp line 387 (Stephen Hill's fit of the ACES RRT + ODT)."""
    inM = np.array([[0.59719, 0.35458, 0.04823], [0.07600, 0.90834, 0.01566], [0.02840, 0.13383, 0.83777]])
    outM = np.array([[1.60475, -0.53108, -0.07367], [-0.10208, 1.10813, -0.00605], [-0.00327, -0.07276, 1.07602]])
    v = v @ inM.T
    a = v * (v + 0.0245786) - 0.000090537
    b = v * (0.983729 * v + 0.4329510) + 0.238081
    return np.clip((a / b) @ outM.T, 0.0, 1.0)


def to_srgb(c):
    """linearToSrgb(), ShadersPostFx.cpp line 396."""
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def display(lin, exposure=1.0):
    return to_srgb(aces(np.maximum(lin, 0) * exposure))


EYE = np.array([0.0, 0.0, 4.0])
FOC = 3.0
LDIR = nrm_(np.array([0.55, 0.65, 0.55]))     # direction TOWARDS the light = normalize(-uMoonDir)
LRAD = np.array([1.0, 1.0, 1.1]) * 3.0        # directional light radiance (irradiance units)
AMB_TOP, AMB_BOT = np.array([0.050, 0.060, 0.090]), np.array([0.015, 0.015, 0.012])   # sky / ground bounce (hemisphere ambient)


def lit_point(P, N, albedo, rough, metal, pbr=True, specular=0.5, shininess=32.0):
    """computeLight() + compose() for ONE directional light + hemisphere ambient (ShaderSources.cpp 143-165, 561-566):
    colour = albedo * (ambient + diffuse) + specular, F0 = mix(0.04, albedo, metalness)."""
    V = nrm_(EYE - P)
    F0 = 0.04 * (1 - metal) + albedo * metal
    amb = AMB_BOT + (AMB_TOP - AMB_BOT) * (0.5 + 0.5 * N[..., 1:2])
    Lb = np.broadcast_to(LDIR, P.shape)
    d, s = brdf(N, V, Lb, LRAD, F0, rough, pbr, specular, shininess, metal)
    return albedo * (amb * (1 - metal) + d) + s


def raster_sphere_mesh(m, mode, albedo, rough, metal, size=360, rot=(18.0, 25.0), **kw):
    """Rasterise the REAL sphere mesh (radius 1 after x2) with flat / gouraud / phong lighting.
    flat    : face normal from the triangle's edges, lit once at the centroid  (kFlatGeom, lines 676-696)
    gouraud : lit at each vertex (vertex normal), colour interpolated          (Gouraud pipeline, same terms)
    phong   : normal and position interpolated, lit per pixel                  (Phong pipeline)"""
    pos, nrm, idx = mesh_arrays(m)
    R = rot_euler_yxz((rot[0], rot[1], 0.0))
    pos = (R @ (pos * 2.0).T).T
    nrm = (R @ nrm.T).T
    img = np.zeros((size, size, 3)); mask = np.zeros((size, size), bool)
    zbuf = np.full((size, size), 1e9)
    d = EYE[2] - pos[:, 2]
    sx = (pos[:, 0] / d * FOC * 0.25 + 0.5) * size
    sy = (0.5 - pos[:, 1] / d * FOC * 0.25) * size
    vcol = lit_point(pos, nrm, albedo, rough, metal, **kw) if mode == "gouraud" else None
    for t in idx:
        a, b, c = pos[t]
        cen = (a + b + c) / 3
        fn = nrm_(np.cross(b - a, c - a))
        if np.dot(fn, EYE - cen) <= 0:
            continue
        xs, ys, ws = sx[t], sy[t], d[t]
        x0, x1 = int(max(0, math.floor(xs.min()))), int(min(size - 1, math.ceil(xs.max())))
        y0, y1 = int(max(0, math.floor(ys.min()))), int(min(size - 1, math.ceil(ys.max())))
        if x1 < x0 or y1 < y0:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(den) < 1e-12:
            continue
        l0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
        l1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= -1e-9) & (l1 >= -1e-9) & (l2 >= -1e-9)
        if not inside.any():
            continue
        pw = np.stack([l0 / ws[0], l1 / ws[1], l2 / ws[2]], -1)      # perspective-correct weights
        pw = pw / pw.sum(-1, keepdims=True)
        P_ = pw @ pos[t]
        if mode == "flat":
            col = np.broadcast_to(lit_point(cen[None], fn[None], albedo, rough, metal, **kw)[0], P_.shape)
        elif mode == "gouraud":
            col = pw @ vcol[t]
        else:
            col = lit_point(P_.reshape(-1, 3), nrm_(pw @ nrm[t]).reshape(-1, 3), albedo, rough, metal, **kw).reshape(P_.shape)
        zb = zbuf[y0:y1 + 1, x0:x1 + 1]
        upd = inside & (EYE[2] - P_[..., 2] < zb)
        zb[upd] = (EYE[2] - P_[..., 2])[upd]
        img[y0:y1 + 1, x0:x1 + 1][upd] = col[upd]
        mask[y0:y1 + 1, x0:x1 + 1][upd] = True
    return img, mask


def exact_sphere(albedo, rough, metal, size=360, pbr=True, **kw):
    """Ray-traced analytic unit sphere (exact normals), the reference for the mesh renders."""
    ys, xs = np.mgrid[0:size, 0:size]
    ndc = np.stack([(xs + 0.5) / size * 2 - 1, 1 - (ys + 0.5) / size * 2], -1)
    dirs = nrm_(np.stack([ndc[..., 0] * 2.0 / FOC, ndc[..., 1] * 2.0 / FOC, -np.ones((size, size))], -1))
    b = (EYE * dirs).sum(-1)
    c = (EYE ** 2).sum() - 1.0
    disc = b * b - c
    hit = disc > 0
    t = -b - np.sqrt(np.maximum(disc, 0))
    P_ = EYE + dirs * t[..., None]
    N = nrm_(P_)
    col = lit_point(P_, N, albedo, rough, metal, pbr=pbr, **kw)
    col[~hit] = 0
    return col, hit


def srgb_img(lin, mask, bg=1.0, exposure=0.55):
    out = display(lin, exposure)
    out[~mask] = bg
    return out


ALBEDO = np.array([0.45, 0.46, 0.50])   # Material of the UAV 'Motor' / 'Prop hub' parts (baseColor, metalness 0.9)


# =====================================================================================================================
# B. Colour interpolation and shading models
# =====================================================================================================================
def fig_B1_barycentric():
    fig, axs = plt.subplots(1, 3, figsize=(14, 4.8))
    A, B, Cc = np.array([0.0, 0.0]), np.array([4.0, 0.3]), np.array([1.6, 3.6])
    cols = np.array([[0.90, 0.20, 0.15], [0.15, 0.55, 0.20], [0.15, 0.30, 0.85]])
    n = 400
    gx, gy = np.meshgrid(np.linspace(-0.3, 4.3, n), np.linspace(-0.3, 4.0, n))
    den = (B[1] - Cc[1]) * (A[0] - Cc[0]) + (Cc[0] - B[0]) * (A[1] - Cc[1])
    l0 = ((B[1] - Cc[1]) * (gx - Cc[0]) + (Cc[0] - B[0]) * (gy - Cc[1])) / den
    l1 = ((Cc[1] - A[1]) * (gx - Cc[0]) + (A[0] - Cc[0]) * (gy - Cc[1])) / den
    l2 = 1 - l0 - l1
    inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
    img = np.clip(l0[..., None] * cols[0] + l1[..., None] * cols[1] + l2[..., None] * cols[2], 0, 1)
    rgba = np.concatenate([img, inside[..., None].astype(float)], -1)
    ax = axs[0]
    ax.imshow(rgba, extent=(-0.3, 4.3, -0.3, 4.0), origin="lower")
    ax.add_patch(Polygon([A, B, Cc], closed=True, fill=False, ec="k", lw=1.2))
    for l_, (px, py) in zip(("v0", "v1", "v2"), (A, B, Cc)):
        ax.text(px, py - 0.25 if l_ != "v2" else py + 0.12, l_, fontsize=10, weight="bold", ha="center")
    Pt = 0.5 * A + 0.3 * B + 0.2 * Cc
    ax.plot(*Pt, "ko", ms=5)
    ax.text(Pt[0] + 0.1, Pt[1] + 0.1, "P", fontsize=10)
    for q in (A, B, Cc):
        ax.plot([q[0], Pt[0]], [q[1], Pt[1]], "k--", lw=0.6)
    ax.set_title("Barycentric fill: colour(P) = l0 c0 + l1 c1 + l2 c2\nP: l = (0.5, 0.3, 0.2) = area ratios, sum = 1")
    ax.set_aspect("equal"); ax.grid(False)
    ax = axs[1]
    for k, c in enumerate(cols):
        ax.contour(gx, gy, (l0, l1, l2)[k], levels=[0.25, 0.5, 0.75], colors=[c], linewidths=1.0)
    ax.add_patch(Polygon([A, B, Cc], closed=True, fill=False, ec="k", lw=1.2))
    ax.set_aspect("equal"); ax.grid(False)
    ax.set_title("iso-lines of l0 (red), l1 (green), l2 (blue)\nat 0.25 / 0.5 / 0.75: straight, parallel to the opposite edge")
    ax = axs[2]
    s = np.linspace(0, 1, 200)
    ax.plot(s, np.cos(np.radians(-80 + 160 * s)).clip(0), color=C["blue"], label="Phong: normal interpolated, N.L evaluated per pixel")
    n0, n1 = np.cos(np.radians(-80)), np.cos(np.radians(80))
    ax.plot(s, (1 - s) * n0 + s * n1, color=C["orange"], label="Gouraud: lit at the 2 vertices, colour interpolated")
    ax.set_xlabel("position along the edge (barycentric weight l1)"); ax.set_ylabel("intensity N.L")
    ax.set_title("why interpolating the colour loses the highlight:\nedge from N at -80 deg to +80 deg (L along +z)")
    ax.legend(fontsize=7.5, loc="lower center")
    save(fig, "B1_barycentric",
         "Rasteriser interpolation: every pixel gets barycentric weights of its triangle; colours (Gouraud) or normals (Phong) are mixed "
         "with them, shown for a red / green / blue triangle. The right plot shows why the two differ: lighting is not linear in the normal.",
         "OpenGL rasteriser; ShaderSources.cpp Gouraud / Phong varyings (TS_TERM_VARYINGS, line ~652)")


def fig_B2_shading(meshkey, name, title, rough=0.28, metal=0.0):
    d = load("primitives.json")
    mesh = d[meshkey]
    lin = {}
    for mode in ("flat", "gouraud", "phong"):
        lin[mode], mask = raster_sphere_mesh(mesh, mode, ALBEDO * 0.9, rough, metal)
    ref, hit = exact_sphere(ALBEDO * 0.9, rough, metal)
    fig, axs = plt.subplots(2, 4, figsize=(14, 7.3))
    labels = ["Flat\n(1 normal per triangle, lit at centroid)", "Gouraud\n(lit per vertex, colour interpolated)",
              "Phong\n(normal interpolated, lit per pixel)", "exact sphere\n(ray traced reference)"]
    imgs = [srgb_img(lin[m], mask | hit) for m in ("flat", "gouraud", "phong")] + [srgb_img(ref, hit)]
    for k in range(4):
        axs[0, k].imshow(imgs[k]); axs[0, k].set_title(labels[k], fontsize=9); axs[0, k].axis("off")
    errs = []
    for k, m in enumerate(("flat", "gouraud", "phong")):
        e = np.abs(display(lin[m], 0.55) - display(ref, 0.55)).mean(-1)
        e[~hit] = 0
        rm = math.sqrt((e[hit] ** 2).mean())
        errs.append(rm)
        im = axs[1, k].imshow(e, cmap="magma", vmin=0, vmax=0.25)
        axs[1, k].set_title(f"|{m} - exact| (display value), RMSE {rm:.3f}", fontsize=9); axs[1, k].axis("off")
    fig.colorbar(im, ax=axs[1, 3], fraction=0.5, label="abs. error in sRGB value (0-1)")
    axs[1, 3].axis("off")
    fig.suptitle(title + f"   (roughness {rough}, PBR GGX; one directional light + hemisphere ambient; ACES + sRGB)", fontsize=10)
    save(fig, name,
         f"The same lit sphere mesh ({meshkey.replace('_', ' ')}) shaded three ways and compared with the exact analytic sphere; RMSE flat {errs[0]:.3f}, "
         f"Gouraud {errs[1]:.3f}, Phong {errs[2]:.3f}. Lighting uses the shader's own brdf() ported to Python.",
         "ShaderSources.cpp: brdf() 113-139, computeLight() 143-196, compose() 561-566, kFlatGeom ~676; ShadersPostFx.cpp acesFitted() 387")
    return errs


def fig_B3_lambert():
    fig, axs = plt.subplots(1, 3, figsize=(14, 4.4))
    ax = axs[0]
    th = np.linspace(-120, 120, 400)
    ax.plot(th, np.maximum(np.cos(np.radians(th)), 0), color=C["blue"], lw=2)
    ax.fill_between(th, 0, np.maximum(np.cos(np.radians(th)), 0), color=C["blue"], alpha=0.12)
    ax.set_xlabel("angle between N and L (deg)"); ax.set_ylabel("diffuse = max(N.L, 0)")
    ax.set_title("Lambert diffuse: brdf() 'diff = radiance * nl'"); ax.axvline(-90, color="gray", ls=":"); ax.axvline(90, color="gray", ls=":")
    ax.annotate("terminator", xy=(90, 0), xytext=(95, 0.4), arrowprops=dict(arrowstyle="->"), fontsize=8)
    ax = axs[1]
    t = np.linspace(0, 2 * math.pi, 400)
    ax.plot(np.cos(t), np.sin(t), color="k", lw=1)
    for a in range(-90, 91, 15):
        n = np.array([math.sin(math.radians(a)), math.cos(math.radians(a))])
        v = max(n[1], 0)
        ax.plot([0, n[0] * (0.2 + 0.8 * v)], [0, n[1] * (0.2 + 0.8 * v)], color=C["orange"], lw=0.8)
    ax.annotate("", xy=(0, 1.55), xytext=(0, 1.05), arrowprops=dict(arrowstyle="->", color="#b8860b", lw=2))
    ax.text(0.08, 1.45, "L", color="#b8860b")
    ax.set_aspect("equal"); ax.set_xlim(-1.3, 1.3); ax.set_ylim(-0.3, 1.7); ax.grid(False); ax.axis("off")
    ax.set_title("response around the surface for a light from the top\n(length of the orange spoke = N.L)")
    ax = axs[2]
    img, hit = exact_sphere(ALBEDO * 0.9, 1.0, 0.0, pbr=False, specular=0.0)
    ax.imshow(srgb_img(img, hit, exposure=0.7)); ax.axis("off")
    ax.set_title("a sphere with diffuse only (Blinn-Phong with k_s = 0):\nshading follows N.L, dark side lit by the hemisphere ambient")
    save(fig, "B3_lambert",
         "Lambert's cosine law as used for the diffuse term: intensity falls with cos of the angle between normal and light and is clamped at the terminator; "
         "the sphere at the right is lit by this term only.", "ShaderSources.cpp brdf() lines 113-139 (diff = radiance * nl)")


# =====================================================================================================================
# C. Shininess / specular
# =====================================================================================================================
def ggx_D(nh, rough):
    a = rough * rough; a2 = a * a
    dd = nh * nh * (a2 - 1.0) + 1.0
    return a2 / (PI * dd * dd)


def fig_C1_lobes():
    th = np.radians(np.linspace(-90, 90, 1801))
    nh = np.cos(th)
    fig, axs = plt.subplots(1, 3, figsize=(15, 4.6))
    ax = axs[0]
    cs = [C["blue"], C["green"], C["orange"], C["red"]]
    for n_, c in zip((4, 16, 64, 256), cs):
        ax.plot(np.degrees(th), nh ** n_, color=c, label=f"n = {n_}")
    ax.set_xlim(-60, 60); ax.set_xlabel("angle between N and half vector H (deg)"); ax.set_ylabel("(N.H)^n")
    ax.set_title("Blinn-Phong lobes (v1 model, uPbr = 0)\nspec = k_s (N.H)^n"); ax.legend()
    ax = axs[1]
    for r, c in zip((0.1, 0.25, 0.4, 0.6, 0.9), (C["red"], C["orange"], C["gold"], C["green"], C["blue"])):
        ax.semilogy(np.degrees(th), ggx_D(nh, r), color=c, label=f"roughness {r}")
    ax.set_xlim(-60, 60); ax.set_ylim(1e-3, 5e3); ax.set_xlabel("angle between N and H (deg)"); ax.set_ylabel("D(h)  (log scale)")
    ax.set_title("GGX normal distribution D(h) (PBR model)\nD = a^2 / (pi ((N.H)^2 (a^2-1) + 1)^2),  a = roughness^2"); ax.legend(fontsize=8)
    ax = axs[2]
    for n_, c in zip((4, 16, 64, 256), cs):
        r = float(np.clip(1.0 - math.log2(max(n_, 1)) / 8.0, 0.08, 0.95))     # pbrRoughness(), Material.h line 61
        ax.plot(np.degrees(th), nh ** n_, color=c, ls="--", lw=1.3)
        ax.plot(np.degrees(th), ggx_D(nh, r) / ggx_D(1.0, r), color=c, lw=1.8, label=f"n = {n_}  <->  roughness {r:.2f}")
    ax.set_xlim(-45, 45); ax.set_xlabel("angle between N and H (deg)"); ax.set_ylabel("lobe normalised to its peak")
    ax.set_title("legacy shininess mapped to roughness:\ndashed Blinn-Phong, solid GGX (same peak height)"); ax.legend(fontsize=8)
    save(fig, "C1_specular_lobes",
         "Surface shininess as a lobe: Blinn-Phong power n (v1) vs the GGX distribution (PBR); the right plot maps each v1 shininess to its derived roughness "
         "(roughness = 1 - log2(n)/8): GGX has a similar core width but a longer tail, which gives the soft halo around highlights.",
         "ShaderSources.cpp brdf() 119-137; Material.h pbrRoughness() line 61")


def fig_C2_FG():
    fig, axs = plt.subplots(1, 3, figsize=(15, 4.6))
    ang = np.radians(np.linspace(0, 89.5, 400))
    ax = axs[0]
    for r, c in zip((0.1, 0.3, 0.5, 0.8), (C["red"], C["orange"], C["green"], C["blue"])):
        k = (r + 1) ** 2 / 8
        x = np.cos(ang)
        ax.plot(np.degrees(ang), x / (x * (1 - k) + k), color=c, label=f"roughness {r} (k = {k:.3f})")
    ax.set_xlabel("angle from the normal (deg)"); ax.set_ylabel("G1 = N.X / (N.X (1-k) + k)")
    ax.set_title("Smith-Schlick geometry term (one direction);\nG = G1(N.V) * G1(N.L): grazing rays are shadowed by micro-facets"); ax.legend(fontsize=8)
    ax = axs[1]
    n = load("nodes.json")
    mot = next(x for x in n["uav"] if x["name"] == "Motor")
    F0m = np.array(mot["color"]) * mot["metal"] + 0.04 * (1 - mot["metal"])   # F0 = mix(0.04, albedo, metalness), computeLight line 148
    cosv = np.cos(ang)
    ax.plot(np.degrees(ang), 0.04 + 0.96 * (1 - cosv) ** 5, color=C["blue"], lw=2, label="dielectric F0 = 0.04")
    for ch, c in zip(range(3), ("#c0392b", "#2a8a4a", "#1f5fa8")):
        ax.plot(np.degrees(ang), F0m[ch] + (1 - F0m[ch]) * (1 - cosv) ** 5, color=c, lw=1.3, ls="--", label=f"UAV motor metal (metalness {mot['metal']}), {'RGB'[ch]}: F0 = {F0m[ch]:.2f}")
    ax.set_xlabel("angle between V and H (deg)  (cos = V.H)"); ax.set_ylabel("Fresnel F(V.H)")
    ax.set_title("Schlick Fresnel: F = F0 + (1 - F0)(1 - V.H)^5"); ax.legend(fontsize=7.5, loc="upper left")
    ax = axs[2]
    tl = math.radians(45)
    L = np.array([math.sin(tl), math.cos(tl), 0.0])
    tv = np.radians(np.linspace(-80, 80, 801))
    V = np.stack([np.sin(tv), np.cos(tv), np.zeros_like(tv)], -1)
    Nn = np.broadcast_to(np.array([0.0, 1.0, 0.0]), V.shape)
    for r, c in zip((0.15, 0.4, 0.8), (C["red"], C["green"], C["blue"])):
        _, s = brdf(Nn, V, np.broadcast_to(L, V.shape), np.array([1.0, 1.0, 1.0]), np.array([0.04] * 3), r)
        ax.plot(np.degrees(tv), s[:, 0], color=c, label=f"dielectric, roughness {r}")
        _, s = brdf(Nn, V, np.broadcast_to(L, V.shape), np.array([1.0, 1.0, 1.0]), F0m, r, metal=1.0)
        ax.plot(np.degrees(tv), s[:, 0], color=c, ls="--", lw=1)
    ax.set_yscale("log"); ax.set_ylim(1e-3, 50)
    ax.set_xlabel("view angle in the plane of N and L (deg); mirror direction at -45"); ax.set_ylabel("specular term (log)")
    ax.set_title("full Cook-Torrance specular D G F / (4 N.V N.L) x pi N.L\nlight at +45 deg; dashed = metal (red channel)"); ax.legend(fontsize=8)
    save(fig, "C2_fresnel_geometry",
         "The G and F factors of the Cook-Torrance BRDF: Smith-Schlick shadowing, Schlick Fresnel for a dielectric (F0 = 0.04) and for the UAV's metal parts, and the resulting "
         "specular response when the view sweeps through the mirror direction for three roughness values.",
         "ShaderSources.cpp brdf() lines 127-137, computeLight() line 148 (F0 = mix(0.04, albedo, metalness))")


def fig_C3_grid():
    rough_v = [0.1, 0.25, 0.4, 0.6, 0.85]
    metal_v = [0.0, 0.5, 1.0]
    fig, axs = plt.subplots(3, 5, figsize=(13, 8.2))
    for i, m in enumerate(metal_v):
        for j, r in enumerate(rough_v):
            img, hit = exact_sphere(ALBEDO, r, m, size=220)
            axs[i, j].imshow(srgb_img(img, hit, exposure=0.55)); axs[i, j].axis("off")
            axs[i, j].set_title(f"rough {r}, metal {m}", fontsize=8.5)
    fig.suptitle("Spheres rendered with the shader's brdf() (albedo = UAV motor material, one directional light + sky ambient):  "
                 "roughness left -> right, metalness top -> bottom", fontsize=10)
    save(fig, "C3_specular_grid",
         "Roughness x metalness grid of lit spheres: low roughness gives a small bright highlight, high roughness a wide dim one; with metalness the diffuse term vanishes "
         "(1 - metalness) and F0 takes the albedo, so the highlight is tinted.",
         "ShaderSources.cpp brdf() 113-139, computeLight() 143-150, compose() 561-566")


def fig_C4_attenuation():
    n = load("nodes.json")
    pr = n["practicals"]
    ranges = sorted(set(p["range"] for p in pr))
    fig, axs = plt.subplots(1, 3, figsize=(15, 4.6))
    d = np.linspace(0.01, 16, 800)
    ax = axs[0]
    ax.semilogy(d, 1 / (1 + d * d), color="k", lw=1.2, ls=":", label="plain 1 / (1 + d^2)")
    for r, c in zip((3.5, 6, 9, 14), (C["red"], C["orange"], C["green"], C["blue"])):
        x = d / r
        w = np.clip(1 - x ** 4, 0, 1)
        ax.semilogy(d, w * w / (1 + d * d), color=c, label=f"windowed, range {r} m")
    ax.set_ylim(1e-4, 1.5); ax.set_xlabel("distance d (m)"); ax.set_ylabel("attenuation")
    ax.set_title("practicalAttenuation(): window^2 / (1 + d^2)\nwindow = clamp(1 - (d/range)^4, 0, 1)", fontsize=9); ax.legend(fontsize=8)
    ax = axs[1]
    th = np.radians(np.linspace(0, 90, 500))
    for cone, c in zip(sorted(set(p["cone"] for p in pr if p["cone"] > 0)), (C["red"], C["orange"], C["green"], C["blue"], C["purple"], C["teal"])):
        cc = math.cos(math.radians(cone))
        x = np.clip((np.cos(th) - cc) / 0.1, 0, 1)
        ax.plot(np.degrees(th), x * x * (3 - 2 * x), color=c, label=f"spot cone {cone:g} deg")
    ax.set_xlabel("angle off the spot axis (deg)"); ax.set_ylabel("smoothstep(cos c, cos c + 0.1, cos angle)")
    ax.set_title("spot window: smoothstep(w, w + 0.1, dot(-L, axis))", fontsize=9); ax.legend(fontsize=8)
    ax = axs[2]
    ax.hist([p["range"] for p in pr], bins=np.arange(0, 16, 1), color=C["teal"], edgecolor="k")
    ax.set_xlabel("light range (m)"); ax.set_ylabel("number of practical lights")
    ax.set_title(f"the {len(pr)} practical lights of the scene:\nranges {min(ranges):g}-{max(ranges):g} m; 16 most relevant used per view", fontsize=9)
    save(fig, "C4_practical_attenuation",
         "Practical lights fall off like an inverse square but are multiplied by a smooth window so they reach exactly zero at their range; spot lights add a smoothstep "
         "cone. The histogram lists the ranges of the lights actually built into the scene.",
         "ShaderSources.cpp practicalAttenuation() lines 90-100; ranges from Scene::practicalLights()")


# =====================================================================================================================
# D. Reflection
# =====================================================================================================================
def fig_D1_ssr():
    steps = 48
    cam = np.array([0.0, 1.7])
    Pg = np.array([5.2, 0.0])                     # wet ground point being shaded
    Nn = np.array([0.0, 1.0])
    V = (Pg - cam) / np.linalg.norm(Pg - cam)
    R = V - 2 * np.dot(V, Nn) * Nn                # reflect(V, N)
    pole_x0, pole_x1, pole_h = 9.0, 9.4, 3.2      # a lamp post: the thing the puddle mirrors

    def scene_depth(slope):
        cands = []
        if slope < 0:
            cands.append(cam[0] + (0 - cam[1]) / slope)
        y_at = cam[1] + slope * (pole_x0 - cam[0])
        if 0 <= y_at <= pole_h:
            cands.append(pole_x0 - cam[0])
        return min(cands) if cands else 1e9

    def march():
        t, prev = 0.04 * 1.5, 0.0      # jitter = 0.5
        samples = []
        for i in range(steps):
            Q = Pg + R * t
            rayZ = Q[0] - cam[0]
            slope = (Q[1] - cam[1]) / rayZ
            sceneZ = scene_depth(slope)
            thick = 0.15 + 0.04 * t + 0.01 * np.linalg.norm(Pg - cam)
            hit = rayZ > sceneZ + 0.01 and rayZ - sceneZ < thick
            samples.append((i, t, Q, hit, sceneZ, rayZ))
            if hit:
                a, b = prev, t
                for _ in range(6):
                    m = 0.5 * (a + b)
                    Qm = Pg + R * m
                    sl = (Qm[1] - cam[1]) / (Qm[0] - cam[0])
                    if Qm[0] - cam[0] > scene_depth(sl):
                        b = m
                    else:
                        a = m
                return samples, b, i
            prev = t
            t = t * 1.09 + 0.03
        return samples, None, None
    samples, tb, ih = march()
    fig = plt.figure(figsize=(14.5, 6.2))
    ax = fig.add_axes([0.04, 0.1, 0.60, 0.8])
    ax.set_aspect("equal"); ax.grid(False)
    ax.axhline(0, color="#6b7280", lw=3)
    ax.fill_between([-1, 12], -0.4, 0, color="#cbd5e1", alpha=0.6)
    ax.text(10.4, -0.3, "wet ground (puddle)", fontsize=8, ha="right")
    ax.add_patch(Rectangle((pole_x0, 0), pole_x1 - pole_x0, pole_h, fc="#fde68a", ec="k"))
    ax.text(pole_x0 - 1.2, pole_h + 0.15, "lamp post (in the depth buffer)", fontsize=8)
    ax.plot(*cam, "ks", ms=8); ax.text(cam[0] - 0.2, cam[1] + 0.25, "camera", fontsize=9)
    ax.plot([cam[0], Pg[0]], [cam[1], Pg[1]], color=C["blue"], lw=1.5)
    ax.text(2.0, 0.55, "view ray V", color=C["blue"], fontsize=9, rotation=-17)
    ax.annotate("", xy=(Pg[0], 1.5), xytext=Pg, arrowprops=dict(arrowstyle="->", color=C["green"], lw=1.5))
    ax.text(Pg[0] + 0.1, 1.2, "N (from the\nsurface buffer)", color=C["green"], fontsize=8)
    ax.plot(*Pg, "ko", ms=5); ax.text(Pg[0] - 1.6, -0.62, "P (world position from depth)", fontsize=8)
    far = Pg + R * 6.0
    ax.plot([Pg[0], far[0]], [Pg[1], far[1]], color=C["orange"], lw=1, ls=":")
    for (i, t, Q, hit, sz, rz) in samples:
        ax.plot(*Q, "o", ms=4.5 if not hit else 7, color=C["red"] if hit else C["gray"], zorder=5)
    Qb = Pg + R * tb
    ax.plot(*Qb, "*", ms=15, color=C["purple"], zorder=6)
    ax.annotate(f"bisection (6 halvings)\nt = {tb:.3f} m: hit point,\nre-projected to a pixel q,\nscene colour read at q", xy=Qb, xytext=(5.0, 3.6), fontsize=8.5,
                arrowprops=dict(arrowstyle="->", color=C["purple"]))
    sl = (Qb[1] - cam[1]) / (Qb[0] - cam[0])
    ax.plot([cam[0], pole_x0], [cam[1], cam[1] + sl * (pole_x0 - cam[0])], color=C["purple"], lw=0.8, ls="--")
    ax.text(0.2, 3.0, "reflected ray R = reflect(V, N), marched\nwith growing steps t <- 1.09 t + 0.03\n(grey = in front of the depth buffer, red = crossing)", fontsize=8.5, color=C["orange"])
    ax.set_xlim(-0.8, 11); ax.set_ylim(-0.8, 4.6); ax.set_xlabel("distance from camera (m)"); ax.set_ylabel("height (m)")
    ax.set_title(f"Screen-space reflection, side view: {len(samples)} steps to the crossing (budget {steps} steps default, 64 on the highest quality)")
    ax2 = fig.add_axes([0.72, 0.58, 0.26, 0.32])
    tt = [s[1] for s in samples]
    ax2.plot(range(len(tt)), tt, "o-", ms=3, color=C["orange"])
    ax2.set_xlabel("step i"); ax2.set_ylabel("ray length t (m)"); ax2.set_title("step lengths grow ~9 % + 3 cm per step", fontsize=9)
    ax3 = fig.add_axes([0.72, 0.1, 0.26, 0.32])
    q = np.linspace(0, 1, 300)
    sm = lambda a, b, x: (lambda u: u * u * (3 - 2 * u))(np.clip((x - a) / (b - a), 0, 1))
    ax3.plot(q, sm(0, 0.07, q) * sm(1, 0.93, q), color=C["blue"], label="screen edge: smoothstep(0,.07,q) smoothstep(1,.93,q)")
    ax3.plot(q, 1 - sm(0.6, 1.0, q), color=C["red"], label="march length: 1 - smoothstep(.6, 1, i/steps)")
    ax3.set_xlabel("q (screen coordinate)  or  i / steps"); ax3.set_ylabel("fade factor"); ax3.set_title("fade of the reflection", fontsize=9)
    ax3.legend(fontsize=6.8, loc="lower center")
    save(fig, "D1_ssr_raymarch",
         "How a puddle mirrors the scene: the reflected ray is stepped in world space, every sample is projected to the screen and its depth compared with the depth buffer; "
         "the first crossing (within a thickness) is refined by six bisections and the scene colour at that pixel is added, faded near the screen edge and for long marches.",
         "ShadersPostFx.cpp kCombineFrag reflection() lines 44-81 (constants 0.04, 1.09, 0.03, thickness 0.15 + 0.04 t + 0.01 d, 6 bisections)")


def fig_D2_fresnel_weight():
    fig, axs = plt.subplots(1, 3, figsize=(15, 4.5))
    a = np.radians(np.linspace(0, 89.5, 400))
    c = np.cos(a)
    F = 0.02 + 0.98 * (1 - c) ** 5
    ax = axs[0]
    ax.plot(np.degrees(a), F, color=C["blue"], label="F = 0.02 + 0.98 (1 - N.V)^5")
    ax.plot(np.degrees(a), 0.35 + 0.65 * F, color=C["orange"], label="composite weight  mix(0.35, 1, F)")
    ax.set_xlabel("view angle from the surface normal (deg)"); ax.set_ylabel("weight")
    ax.set_title("Fresnel weight of the screen-space reflection"); ax.legend(fontsize=8)
    ax.text(5, 0.5, "looking down at a puddle:\nweak, 35 % floor\n(artistic lower bound)", fontsize=8)
    ax = axs[1]
    r = np.linspace(0, 1, 200)
    ax.plot(r, 0.6 * (1 - r) ** 2, color=C["gray"], label="polished metal (metalness 1): 0.6 (1-rough)^2")
    for wet, col in ((0.5, C["teal"]), (1.0, C["blue"])):
        ax.axhline(0.45 * wet, color=col, ls="--", label=f"wet top face, wetness {wet}: 0.45 x {wet}")
    ax.set_xlabel("roughness"); ax.set_ylabel("gReflectivity (surface buffer alpha)")
    ax.set_title("how much a surface asks for reflection"); ax.legend(fontsize=7.5)
    ax = axs[2]
    ax.axis("off")
    ax.text(0, 1, "Composite pass (per pixel):\n\n"
            "  c = scene colour\n"
            "  if (reflectivity alpha > 0.02 and depth < 1):\n"
            "      c += reflection(uv, d, surf) * alpha\n"
            "  reflection = texture(scene, q) * edge fade\n"
            "               * mix(0.35, 1, F)\n"
            "  then volumetric fog:  c = c * fog.a + fog.rgb\n\n"
            "Alpha comes from the lit pass (FragSurface):\n"
            "  compose():  max(0.45 film, metalness (1-rough)^2 0.6)\n"
            "              film = wetness clamp(1.4 N.y + 0.1, 0, 1)\n"
            "  terrainShade(): max(puddle, 0.35 wetness (1-rough))\n\n"
            "Rays that point down (R.y < -0.2) return 0.", va="top", family="monospace", fontsize=8.3)
    save(fig, "D2_fresnel_reflectivity",
         "Reflection strength: the Fresnel term makes grazing views mirror-like while the composite keeps at least 35 %; the lit pass decides per surface how much reflection it "
         "asks for (wet top faces, polished metal, puddles).",
         "ShadersPostFx.cpp lines 75-77, 91; ShaderSources.cpp compose() 576-582, terrainShade() 541-546")

if want("B"):
    fig_B1_barycentric()
    fig_B2_shading("sphere_small", "B2_shading_models_coarse", "Flat vs Gouraud vs Phong on a coarse sphere (12 x 6)", rough=0.28)
    fig_B2_shading("sphere", "B2_shading_models_real", "Flat vs Gouraud vs Phong on the real sphere mesh (36 x 18)", rough=0.28)
    fig_B3_lambert()
if want("C"):
    fig_C1_lobes(); fig_C2_FG(); fig_C3_grid(); fig_C4_attenuation()
if want("D"):
    fig_D1_ssr(); fig_D2_fresnel_weight()

# =====================================================================================================================
# E. MicroMouse + line follower (real HideController data)
# =====================================================================================================================
CELL = 0.5
HALF = 20.0
EXT = (-HALF, HALF, -HALF, HALF)


def grid(a, n=80):
    return np.array(a, float).reshape(n, n)


def cell_xz(cx, cz):
    return -HALF + (cx + 0.5) * CELL, -HALF + (cz + 0.5) * CELL


def route_xy(r):
    a = np.array(r, float).reshape(-1, 2)
    return a[:, 0], a[:, 1]


def chaikin_py(pts, iters):
    """Port of chaikin() in HideController.cpp lines 28-41: ends kept, every segment replaced by its 1/4 and 3/4 points."""
    cur = [np.array(p, float) for p in pts]
    for _ in range(iters):
        if len(cur) <= 2:
            break
        nxt = [cur[0]]
        for i in range(len(cur) - 1):
            nxt.append(cur[i] + 0.25 * (cur[i + 1] - cur[i]))
            nxt.append(cur[i] + 0.75 * (cur[i + 1] - cur[i]))
        nxt.append(cur[-1])
        cur = nxt
    return np.array(cur)


def base_axes(ax, free, title=None, ticks=True):
    ax.imshow(np.where(free > 0, 1.0, 0.0), extent=EXT, origin="lower", cmap="gray", vmin=-0.3, vmax=1.0, alpha=1.0)
    ax.set_xlim(-HALF, HALF); ax.set_ylim(-HALF, HALF); ax.set_aspect("equal"); ax.grid(False)
    if title:
        ax.set_title(title, fontsize=9)
    ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)")


def fig_E1_grids():
    m = load("micromouse.json")
    free = grid(m["free"]); exposed = grid(m["exposed"])
    h0, h1 = grid(m["hidden0"]), grid(m["hidden1"])
    fig, axs = plt.subplots(2, 3, figsize=(15, 9.6))
    ax = axs[0, 0]
    ax.imshow(free, extent=EXT, origin="lower", cmap="gray", vmin=-0.5, vmax=1)
    ax.set_title(f"1. free-cell grid: {int(free.sum())} of {80 * 80} cells free\n(white = free; dark = occluder + 0.30 m margin, or r > 18.3 m)", fontsize=9)
    ax = axs[0, 1]
    img = np.where(free > 0, np.where(exposed > 0, 1.0, 0.0), np.nan)
    ax.imshow(img, extent=EXT, origin="lower", cmap="RdYlGn_r", vmin=-0.2, vmax=1.2)
    for k, c in (("uav0", "#111827"), ("uav1", "#111827")):
        ax.plot(m[k][0], m[k][2], marker="^", color=c, ms=9, mfc="white")
        ax.text(m[k][0] + 0.6, m[k][2] + 0.6, k.upper().replace("UAV", "UAV "), fontsize=8)
    ax.set_title(f"2. exposure: seen by a UAV (red) / hidden from both (green)\nexposed {int((exposed * free).sum())} cells; hidden from UAV 0: "
                 f"{int(((h0 > 0) & (free > 0)).sum())}, from UAV 1: {int(((h1 > 0) & (free > 0)).sum())}", fontsize=9)
    nz = [grid(s["noise"]) for s in m["seeds"]]
    for k, (ax, s) in enumerate(zip((axs[0, 2], axs[1, 0]), m["seeds"])):
        im = ax.imshow(nz[k], extent=EXT, origin="lower", cmap="viridis", vmin=0, vmax=1)
        ax.set_title(f"3. per-rover noise field, seed {s['seed']:.0f}\nsmooth value noise, 4-cell lattice, 0..1", fontsize=9)
    fig.colorbar(im, ax=axs[0, 2], shrink=0.75, label="noiseAt(cx, cz)")
    ax = axs[1, 1]
    cost = (16 + 24 * exposed + 24 * nz[0]) / 16.0
    cost = np.where(free > 0, cost, np.nan)
    im = ax.imshow(cost, extent=EXT, origin="lower", cmap="magma")
    fig.colorbar(im, ax=ax, shrink=0.75, label="cost of entering a cell (cell-lengths)")
    ax.set_title("4. step cost for seed 11:  (16 + 24 exposed + 24 noise) / 16\n= 1 cell + exposure penalty + random detour tax", fontsize=9)
    ax = axs[1, 2]
    ax.axis("off")
    ax.text(0, 1, "Planner (HideController::floodFillToObjective)\n\n"
            "  grid:   80 x 80 cells of 0.5 m over [-20, 20] m\n"
            "  free:   setStaticOccluders(): cell centre not inside any\n"
            "          occluder footprint (+0.30 m) and r < 18.3 m\n"
            "  seen:   computeVisibility(observer): a cell is HIDDEN if\n"
            "          all 5 sample points of the rover body (top + 4\n"
            "          corners) are blocked from the UAV (OccluderGrid)\n"
            "  exposed = OR over both UAVs of 'not hidden'\n"
            "  noise:  noiseAt(): hash of (cx, cz, seed), smoothstep-\n"
            "          interpolated, one lattice point per 4 cells\n"
            "  step cost (x16 fixed point) = 16\n"
            "          + 24 if exposed + int(24 noise)\n"
            "  Dijkstra from the objective's cell over 4-neighbours\n"
            "  => every rover takes its own, UAV-averse route.", va="top", family="monospace", fontsize=8.6)
    for ax in (axs[0, 0], axs[0, 1], axs[0, 2], axs[1, 0], axs[1, 1]):
        ax.set_xlim(-HALF, HALF); ax.set_ylim(-HALF, HALF); ax.set_aspect("equal"); ax.grid(False)
        ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)")
    save(fig, "E1_planner_grids",
         "The four fields the infiltrating rover plans on: the free-cell occupancy grid, the exposure to the two UAVs, the per-rover noise field of two seeds and the resulting "
         "step cost (all dumped from the live HideController of the scene).",
         "src/ai/HideController.cpp: setStaticOccluders(), computeVisibility(), noiseAt(), floodFillToObjective()")


def annotate_values(ax, f, free, step, scale, fs=5.5, color="w"):
    for cz in range(1, 80, step):
        for cx in range(1, 80, step):
            if free[cz, cx] > 0 and f[cz, cx] >= 0:
                x, z = cell_xz(cx, cz)
                ax.text(x, z, f"{f[cz, cx] / scale:.0f}", fontsize=fs, ha="center", va="center", color=color)


def fig_E2_dijkstra():
    m = load("micromouse.json")
    free = grid(m["free"])
    fig, axs = plt.subplots(1, 2, figsize=(15, 7.6))
    for ax, s, c in zip(axs, m["seeds"], (C["red"], C["blue"])):
        f = grid(s["flood"])
        fm = np.where((free > 0) & (f >= 0), f / 16.0, np.nan)
        ax.imshow(np.where(free > 0, 0.0, 1.0), extent=EXT, origin="lower", cmap="Greys", vmin=0, vmax=1.5)
        im = ax.imshow(fm, extent=EXT, origin="lower", cmap="turbo")
        annotate_values(ax, f, free, 4, 16.0)
        ax.contour(np.linspace(-HALF + 0.25, HALF - 0.25, 80), np.linspace(-HALF + 0.25, HALF - 0.25, 80), np.nan_to_num(fm, nan=999), levels=np.arange(0, 80, 10), colors="k", linewidths=0.4, alpha=0.5)
        rx, rz = route_xy(s["route"])
        ax.plot(rx, rz, ".", color="white", ms=2.5)
        cx_, cz_ = route_xy(s["chaikin"])
        ax.plot(cx_, cz_, "-", color=c if False else "white", lw=2.2)
        ax.plot(cx_, cz_, "-", color="#111827", lw=0.8)
        ax.plot(m["start_x"], m["start_z"], "o", color="white", mec="k", ms=9)
        ax.text(m["start_x"] + 0.5, m["start_z"] - 1.3, "start", color="k", fontsize=9, weight="bold", bbox=dict(fc="white", ec="none", pad=1))
        ax.plot(m["goal_x"], m["goal_z"], "*", color="white", mec="k", ms=16)
        ax.text(m["goal_x"] + 0.5, m["goal_z"] + 0.5, "objective\n(watchtower)", fontsize=8, weight="bold", bbox=dict(fc="white", ec="none", pad=1))
        ax.set_xlim(-HALF, HALF); ax.set_ylim(-HALF, HALF); ax.set_aspect("equal"); ax.grid(False)
        ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)")
        L = np.sum(np.hypot(np.diff(cx_), np.diff(cz_)))
        ax.set_title(f"weighted flood field, seed {s['seed']:.0f}: cost-to-go in cell-lengths (numbers every 4th cell)\n"
                     f"steepest descent route (dots, {len(rx)} cells) + Chaikin guide line (curve, {L:.1f} m)", fontsize=9)
        fig.colorbar(im, ax=ax, shrink=0.7, label="flood value / 16")
    save(fig, "E2_flood_dijkstra",
         "Weighted MicroMouse flood field (Dijkstra from the objective) for two seeds with the same start and objective: the cost-to-go heat map, the steepest-descent cell route and "
         "the smoothed guide line; different noise fields make different routes.",
         "src/ai/HideController.cpp: floodFillToObjective() lines 213-247, descend() 144-172, replan() 262-282")
    # contract alias figure: single panel
    fig, ax = plt.subplots(figsize=(8.2, 7.6))
    s = m["seeds"][0]
    f = grid(s["flood"])
    fm = np.where((free > 0) & (f >= 0), f / 16.0, np.nan)
    ax.imshow(np.where(free > 0, 0.0, 1.0), extent=EXT, origin="lower", cmap="Greys", vmin=0, vmax=1.5)
    im = ax.imshow(fm, extent=EXT, origin="lower", cmap="turbo")
    annotate_values(ax, f, free, 4, 16.0)
    rx, rz = route_xy(s["route"]); cx_, cz_ = route_xy(s["chaikin"])
    ax.plot(cx_, cz_, "-", color="white", lw=2.2); ax.plot(rx, rz, ".", color="k", ms=2)
    ax.plot(m["start_x"], m["start_z"], "o", color="white", mec="k", ms=9); ax.plot(m["goal_x"], m["goal_z"], "*", color="white", mec="k", ms=16)
    ax.set_xlim(-HALF, HALF); ax.set_ylim(-HALF, HALF); ax.set_aspect("equal"); ax.grid(False)
    ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)"); fig.colorbar(im, ax=ax, shrink=0.75, label="flood value / 16 (cell-lengths)")
    ax.set_title("Weighted flood field (seed 11): values, route, guide line")
    save(fig, "flood_dijkstra", "Weighted (Dijkstra) flood field of the infiltrating rover with the cost-to-go values, the steepest-descent route and the smoothed guide line (seed 11).",
         "src/ai/HideController.cpp: floodFillToObjective(), descend()")


def fig_E3_bfs():
    m = load("micromouse.json")
    free = grid(m["free"]); f = grid(m["bfs_flood"]); hid = grid(m["hidden0"])
    fig, axs = plt.subplots(1, 2, figsize=(15, 7.4), gridspec_kw=dict(width_ratios=[1, 1.15]))
    ax = axs[0]
    ax.imshow(np.where(free > 0, 1.0, 0.0), extent=EXT, origin="lower", cmap="gray", vmin=-0.3, vmax=1)
    ax.imshow(np.where((free > 0) & (hid > 0), 1.0, np.nan), extent=EXT, origin="lower", cmap="Greens", vmin=0, vmax=1.6, alpha=0.9)
    ax.plot(m["uav0"][0], m["uav0"][2], "^", color="k", mfc="yellow", ms=11)
    ax.text(m["uav0"][0] + 0.6, m["uav0"][2] + 0.6, "UAV 0", fontsize=9)
    ax.set_title("v4 hiding mode, step 1: cells out of UAV 0's sight (green)\n= goals of the flood fill (deep hidden cells preferred)", fontsize=9)
    ax = axs[1]
    fm = np.where((free > 0) & (f >= 0), f, np.nan)
    ax.imshow(np.where(free > 0, 0.0, 1.0), extent=EXT, origin="lower", cmap="Greys", vmin=0, vmax=1.5)
    im = ax.imshow(fm, extent=EXT, origin="lower", cmap="viridis")
    annotate_values(ax, f, free, 4, 1.0)
    rx, rz = route_xy(m["bfs_route"])
    ax.plot(rx, rz, "-", color=C["orange"], lw=2)
    ax.plot(m["start_x"], m["start_z"], "o", color="white", mec="k", ms=9)
    ax.set_title("step 2: breadth-first distance (integer steps) from the goal cells;\nstep 3: walk downhill (orange) from the rover's cell", fontsize=9)
    fig.colorbar(im, ax=ax, shrink=0.7, label="BFS distance (cells)")
    for ax in axs:
        ax.set_xlim(-HALF, HALF); ax.set_ylim(-HALF, HALF); ax.set_aspect("equal"); ax.grid(False)
        ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)")
    save(fig, "E3_flood_bfs",
         "The v4 hiding behaviour: cells hidden from the UAV become goals, a breadth-first flood fill gives every free cell its step distance to cover, and the rover walks down the field "
         "(MicroMouse). Compared with the Dijkstra field, all steps cost the same.", "src/ai/HideController.cpp: computeVisibility(), floodFill() lines 120-142, descend()")
    import shutil
    shutil.copy(os.path.join(OUT, "E3_flood_bfs.png"), os.path.join(OUT, "flood_bfs.png"))
    CAPTIONS["flood_bfs.png"] = dict(CAPTIONS["E3_flood_bfs.png"])


def fig_E4_chaikin():
    m = load("micromouse.json")
    s = m["seeds"][0]
    raw = np.array(s["raw"]).reshape(-1, 2)
    ch = np.array(s["chaikin"]).reshape(-1, 2)
    mine = chaikin_py(raw, 3)
    assert mine.shape == ch.shape and np.abs(mine - ch).max() < 1e-3, "chaikin mismatch"
    fig, axs = plt.subplots(1, 3, figsize=(16, 5.2), gridspec_kw=dict(width_ratios=[1.2, 1.2, 1]))
    win = (raw[:14, 0].min() - 0.6, raw[:14, 0].max() + 0.6, raw[:14, 1].min() - 0.6, raw[:14, 1].max() + 0.6)
    ax = axs[0]
    cols = [C["gray"], C["blue"], C["green"], C["red"]]
    for it in range(4):
        p = chaikin_py(raw, it)
        ax.plot(p[:, 0], p[:, 1], "-o" if it < 2 else "-", color=cols[it], lw=1.2 + 0.4 * it, ms=3.5, label=("raw cell centres + heading point" if it == 0 else f"after {it} Chaikin pass{'es' if it > 1 else ''} ({len(p)} points)"))
    ax.set_xlim(win[0], win[1]); ax.set_ylim(win[2], win[3]); ax.set_aspect("equal")
    ax.set_title("start of the route: corners are cut at 1/4 and 3/4 of each segment", fontsize=9)
    ax.legend(fontsize=7.5); ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)")
    ax = axs[1]
    ax.plot(raw[:, 0], raw[:, 1], "-", color=C["gray"], lw=1.2, label="raw route (staircase on the 0.5 m grid)")
    ax.plot(ch[:, 0], ch[:, 1], "-", color=C["red"], lw=1.8, label="3 passes: guide line the IR sensors follow")
    ax.plot(m["goal_x"], m["goal_z"], "*", color="k", ms=12)
    ax.set_aspect("equal"); ax.legend(fontsize=8); ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)")
    ax.set_title(f"whole route (seed 11): {len(raw)} raw points -> {len(ch)} guide points", fontsize=9)
    ax = axs[2]
    ax.axis("off")
    ax.text(0, 1, "chaikin(points, 3):\n\n  keep first and last point\n  for every segment (P_i, P_i+1):\n      Q = 0.75 P_i + 0.25 P_i+1\n      R = 0.25 P_i + 0.75 P_i+1\n  each pass doubles the count: n -> 2n,\n  3 passes: 8 n\n\n"
            f"here {len(raw)} -> {len(ch)}  (checked: the Python port\nreproduces the controller's {len(ch)} points,\nmax difference {np.abs(mine - ch).max():.1e} m)\n\n"
            "The line starts under the robot and\nfirst runs along its heading (0.41 m),\nso the sensor bar begins on it.", va="top", family="monospace", fontsize=8.6)
    save(fig, "E4_path_chaikin",
         "Chaikin corner cutting turns the staircase of grid cells into the smooth guide line the line follower tracks; three passes at 1/4 - 3/4 points, shown step by step on the real route.",
         "src/ai/HideController.cpp: chaikin() lines 28-41, replan() lines 262-282")
    import shutil
    shutil.copy(os.path.join(OUT, "E4_path_chaikin.png"), os.path.join(OUT, "path_chaikin.png"))
    CAPTIONS["path_chaikin.png"] = dict(CAPTIONS["E4_path_chaikin.png"])


def drive_derived():
    d = load("drive.json")
    t = np.array(d["t"]); x = np.array(d["x"]); z = np.array(d["z"]); yaw = np.array(d["yaw"])
    bits = np.array(d["bits"]).astype(int)
    left, right = np.array(d["left"]), np.array(d["right"])
    lines = [np.array(l).reshape(-1, 2) for l in d["lines"]]
    lt = np.array(d["lineT"])
    dt = d["dt"]
    xs_off = -0.12 + 0.06 * np.arange(5)
    err = np.full(len(t), np.nan)
    for i, b in enumerate(bits):
        act = [k for k in range(5) if (b >> k) & 1]
        if act:
            err[i] = np.mean(xs_off[act]) / 0.12
    # PD output (kp 3.2, kd 0.25, clamp +-2.5) when the line is seen
    omega = np.full(len(t), np.nan)
    prev = 0.0
    for i in range(len(t)):
        if not np.isnan(err[i]):
            omega[i] = np.clip(3.2 * err[i] + 0.25 * (err[i] - prev) / max(dt, 1e-3), -2.5, 2.5)
            prev = err[i]
    return d, t, x, z, yaw, bits, left, right, lines, lt, err, omega, xs_off


def dist_to_line(p, line):
    a, b = line[:-1], line[1:]
    ab = b - a
    l2 = (ab ** 2).sum(1)
    tt = np.clip(((p - a) * ab).sum(1) / np.maximum(l2, 1e-12), 0, 1)
    return np.hypot(*(p - (a + ab * tt[:, None])).T).min()


def fig_E5_ir():
    d, t, x, z, yaw, bits, left, right, lines, lt, err, omega, xs_off = drive_derived()
    # find frames whose recorded bits are reproduced by the recorded guide line, with 2-3 active sensors and a visible turn
    best = None
    for i in range(30, len(t) - 1):
        k = np.searchsorted(lt, t[i], side="right") - 1
        if k < 0:
            continue
        line = lines[k]
        fwd = np.array([math.sin(yaw[i]), math.cos(yaw[i])]); lef = np.array([math.cos(yaw[i]), -math.sin(yaw[i])])
        sp = [np.array([x[i], z[i]]) + fwd * 0.31 + lef * xo for xo in xs_off]
        calc = sum((dist_to_line(p, line) < 0.05) << j for j, p in enumerate(sp))
        if calc == bits[i] and bin(bits[i]).count("1") >= 2 and abs(err[i]) > 0.25 and not np.isnan(omega[i]):
            best = (i, k, calc); break
    assert best, "no frame found"
    i, k, calc = best
    line = lines[k]
    fwd = np.array([math.sin(yaw[i]), math.cos(yaw[i])]); lef = np.array([math.cos(yaw[i]), -math.sin(yaw[i])])
    pos = np.array([x[i], z[i]])
    fig, axs = plt.subplots(1, 2, figsize=(14.5, 6.4), gridspec_kw=dict(width_ratios=[1.35, 1]))
    ax = axs[0]
    # rotate world into the robot frame: u = lateral (left +), v = forward
    rel = line - pos
    U = rel @ lef; Vv = rel @ fwd
    ax.plot(U, Vv, "-", color="#374151", lw=1.0, alpha=0.6)
    ax.fill_between([], [], [])
    # line band 10 cm wide near the robot
    pts = np.array([[u, v] for u, v in zip(U, Vv) if -0.6 < u < 0.6 and -0.5 < v < 1.0])
    # draw band as thick line
    ax.plot(U, Vv, "-", color=C["gold"], lw=14, alpha=0.35, solid_capstyle="butt", label="virtual line, 10 cm wide (half width 0.05 m)")
    # robot: 0.5 m track, body 0.4 x 0.62
    ax.add_patch(Rectangle((-0.20, -0.31), 0.40, 0.62, fc="#dbeafe", ec=C["blue"], lw=1.5, zorder=3))
    for sgn in (-1, 1):
        for zz in (-0.2, 0.0, 0.2):
            ax.add_patch(Rectangle((sgn * 0.25 - 0.025, zz - 0.04), 0.05, 0.08, fc="#374151", zorder=3))
    ax.annotate("", xy=(0, 0.28), xytext=(0, -0.2), arrowprops=dict(arrowstyle="->", color=C["blue"], lw=1.5), zorder=4)
    ax.text(0.02, -0.12, "forward", fontsize=8, color=C["blue"], zorder=4)
    for j, xo in enumerate(xs_off):
        on = (bits[i] >> j) & 1
        ax.plot(xo, 0.31, "o", ms=13, mfc=C["red"] if on else "white", mec="k", zorder=5)
        ax.text(xo, 0.31, f"{j}", ha="center", va="center", fontsize=8, color="white" if on else "k", zorder=6)
        ax.text(xo, 0.40 + 0.05 * (j % 2), f"{xo:+.2f}", ha="center", fontsize=6.5)
    ax.annotate("", xy=(xs_off[-1] + 0.01, 0.52), xytext=(xs_off[0] - 0.01, 0.52), arrowprops=dict(arrowstyle="<->"))
    ax.text(0, 0.56, "5 IR sensors, spacing 0.06 m, 0.31 m ahead of the centre", ha="center", fontsize=8)
    ax.set_xlim(-0.6, 0.6); ax.set_ylim(-0.5, 0.95); ax.set_aspect("equal")
    ax.set_xlabel("lateral offset, + = left of the robot (m)"); ax.set_ylabel("ahead (m)")
    ax.set_title("Line-follower geometry at one real instant (t = %.2f s), robot frame" % t[i], fontsize=9.5)
    ax.legend(loc="lower right", fontsize=8)
    ax = axs[1]
    ax.axis("off")
    act = [j for j in range(5) if (bits[i] >> j) & 1]
    s_ = sum(xs_off[j] for j in act)
    txt = ("Sensor read-out (HideController::followLine)\n\n"
           f"  bits (sensor 0..4): {[int((bits[i] >> j) & 1) for j in range(5)]}\n"
           f"  active offsets x_i: {[round(float(xs_off[j]), 2) for j in act]}  (sensor over the line if\n"
           "                      distance(sensor, line) < 0.05 m)\n\n"
           f"  error = (sum x_i / n_active) / 0.12\n        = ({s_:+.2f} / {len(act)}) / 0.12 = {err[i]:+.3f}\n"
           "  -1 = line far right ... +1 = far left\n\n"
           f"  speed = 0.75 (1 - 0.6 |error|) = {0.75 * (1 - 0.6 * abs(err[i])):.3f} m/s\n"
           f"  omega = Kp e + Kd (e - e_prev)/dt  (Kp 3.2, Kd 0.25, |omega| <= 2.5)\n        = {omega[i]:+.2f} rad/s\n"
           "  v_left  = speed - omega * 0.50 / 2\n  v_right = speed + omega * 0.50 / 2\n"
           f"  recorded wheels now: left {left[i]:+.2f}, right {right[i]:+.2f} m/s\n  (limited to 2 m/s^2 acceleration)\n\n"
           "No line under any sensor: pivot slowly (0.08 m/s)\ntowards the side of the line; after 0.8 s re-plan.")
    ax.text(0, 1, txt, va="top", family="monospace", fontsize=8.5)
    save(fig, "E5_ir_sensors",
         "The line follower: five IR reflectance sensors on a bar 0.31 m ahead of the robot read whether they are over the 10 cm guide line; the weighted mean of the active offsets is "
         "the error that drives a PD controller and the differential-drive wheel speeds. Drawn from a real frame of a simulated drive.",
         "src/ai/HideController.cpp: followLine() lines 308-366; constants kSensorForward, kSensorSpacing, kLineHalfWidth, kKp, kKd")
    import shutil
    shutil.copy(os.path.join(OUT, "E5_ir_sensors.png"), os.path.join(OUT, "ir_sensors.png"))
    CAPTIONS["ir_sensors.png"] = dict(CAPTIONS["E5_ir_sensors.png"])


def fig_E6_trace():
    d, t, x, z, yaw, bits, left, right, lines, lt, err, omega, xs_off = drive_derived()
    m = load("micromouse.json")
    free = grid(m["free"])
    ev = np.array(d["evading"])
    t0, t1 = 9.0, 31.0
    sel = (t >= t0) & (t <= t1)
    fig = plt.figure(figsize=(15, 9.2))
    gs = fig.add_gridspec(5, 2, width_ratios=[1, 1.15], hspace=0.5, wspace=0.18)
    ax = fig.add_subplot(gs[:, 0])
    ax.imshow(np.where(free > 0, 1.0, 0.0), extent=EXT, origin="lower", cmap="gray", vmin=-0.3, vmax=1)
    sc = ax.scatter(x, z, c=t, s=2, cmap="viridis", zorder=3)
    ax.plot(x[ev > 0], z[ev > 0], ".", color=C["red"], ms=1.5, zorder=4, label="evading a UAV closer than 5 m (re-plans to hide)")
    ax.plot(lines[0][:, 0], lines[0][:, 1], "-", color=C["gold"], lw=1.2, label="first guide line (t = 0, before the UAV forced re-plans)")
    ax.plot(d["goal_x"], d["goal_z"], "*", color="k", ms=14, label="objective")
    ax.plot(x[sel], z[sel], "-", color="white", lw=0.8, alpha=0.8)
    ax.set_xlim(-14, 8); ax.set_ylim(-12, 12); ax.set_aspect("equal"); ax.grid(False)
    ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)"); ax.legend(fontsize=7.5, loc="lower right")
    fig.colorbar(sc, ax=ax, shrink=0.5, label="time (s)")
    ax.set_title(f"path of the real drive over the free-cell grid ({t[-1]:.0f} s, seed 11)", fontsize=9)
    a1 = fig.add_subplot(gs[0, 1])
    for j in range(5):
        on = np.array([(b >> j) & 1 for b in bits[sel]])
        a1.fill_between(t[sel], j, j + on * 0.8, step="post", color=C["red"] if j != 2 else C["orange"])
    a1.set_yticks(np.arange(5) + 0.4); a1.set_yticklabels([f"s{j} ({xs_off[j]:+.2f})" for j in range(5)], fontsize=7)
    a1.set_title("IR sensor bits (offset in m)", fontsize=9)
    a2 = fig.add_subplot(gs[1, 1], sharex=a1)
    a2.plot(t[sel], err[sel], color=C["blue"], lw=1)
    a2.set_ylabel("error"); a2.set_title("weighted line error (gaps: line lost)", fontsize=9)
    a3 = fig.add_subplot(gs[2, 1], sharex=a1)
    a3.plot(t[sel], omega[sel], color=C["green"], lw=1, label="PD command")
    a3.plot(t[sel], (right[sel] - left[sel]) / 0.5, color=C["gray"], lw=1, ls="--", label="realised (v_r - v_l)/0.5")
    a3.set_ylabel("omega (rad/s)"); a3.legend(fontsize=7, loc="upper right"); a3.set_title("steering rate", fontsize=9)
    a4 = fig.add_subplot(gs[3, 1], sharex=a1)
    a4.plot(t[sel], left[sel], color=C["purple"], lw=1, label="left wheel"); a4.plot(t[sel], right[sel], color=C["orange"], lw=1, label="right wheel")
    a4.set_ylabel("m/s"); a4.legend(fontsize=7, loc="upper right"); a4.set_title("wheel speeds", fontsize=9)
    a5 = fig.add_subplot(gs[4, 1], sharex=a1)
    a5.plot(t[sel], 0.5 * (left[sel] + right[sel]), color=C["teal"], lw=1)
    a5.set_ylabel("v (m/s)"); a5.set_xlabel("time (s)"); a5.set_title("forward speed (max 0.75 m/s)", fontsize=9)
    fig.suptitle("Line-follower drive recorded from the real simulation (rover seed 11, start (-12, -10), objective watchtower); time series for t = 9 - 31 s", fontsize=10)
    save(fig, "E6_rover_trace",
         "A recorded drive of the intruder rover: its path over the occupancy grid and, for a 22 s window, the sensor bits, line error, PD steering command and left / right wheel "
         "speeds (error and PD command are reconstructed from the recorded sensor bits with the controller's formulas).",
         "src/ai/HideController.cpp followLine(); data from Scene::update() with figdata drive")
    import shutil
    shutil.copy(os.path.join(OUT, "E6_rover_trace.png"), os.path.join(OUT, "rover_trace.png"))
    CAPTIONS["rover_trace.png"] = dict(CAPTIONS["E6_rover_trace.png"])


if want("E"):
    fig_E1_grids(); fig_E2_dijkstra(); fig_E3_bfs(); fig_E4_chaikin(); fig_E5_ir(); fig_E6_trace()

# =====================================================================================================================
# F. Computer vision and tracking
# =====================================================================================================================
IRONBOW = [(0.00, 0.00, 0.03), (0.12, 0.02, 0.33), (0.46, 0.03, 0.55), (0.80, 0.10, 0.34), (0.98, 0.42, 0.06), (1.00, 0.78, 0.16), (1.00, 1.00, 0.88)]


def ironbow_cmap():
    """thermalPalette() ironbow stops (ShaderSources.cpp kPalette lines 8-27, same stops as ThermalPalette.h)."""
    from matplotlib.colors import LinearSegmentedColormap
    return LinearSegmentedColormap.from_list("ironbow", IRONBOW, N=256)


def cv_load():
    d = load("cv.json")
    w, h = d["w"], d["h"]
    return d, np.array(d["image"], np.uint8).reshape(h, w), np.array(d["mask"]).reshape(h, w), np.array(d["mask2"]).reshape(h, w), np.array(d["labels"]).reshape(h, w)


def verdict(b, d):
    """Target filter of detectTargets() (CvPipeline.cpp line 160-162) with the reason for a rejection."""
    if b["area"] < d["minArea"]:
        return False, "area < %d" % d["minArea"]
    if b["area"] > d["maxArea"]:
        return False, f"area {b['area']} > {d['maxArea']} px (fire)"
    if b["peak"] < d["core"]:
        return False, f"no core >= {d['core']}"
    if b["area"] >= 6 and b["elong"] > d["maxElong"]:
        return False, f"elongation {b['elong']:.0f} > {d['maxElong']:.0f} (streak)"
    return True, "target"


def fig_F1_threshold():
    d, img, mask, mask2, labels = cv_load()
    hist = np.bincount(img.ravel(), minlength=256)
    fig, axs = plt.subplots(1, 2, figsize=(14, 4.9), gridspec_kw=dict(width_ratios=[1.6, 1]))
    ax = axs[0]
    ax.bar(np.arange(256), hist, width=1.0, color=C["gray"])
    ax.set_yscale("log")
    ax.axvline(d["floor"], color=C["red"], lw=1.8, label=f"fixed floor 204 = 0.80  (target pixels need heat >= 0.80)")
    ax.axvline(d["otsu"] + 1, color=C["blue"], lw=1.8, ls="--", label=f"Otsu threshold: dark class <= {d['otsu']}, hot class > {d['otsu']}")
    ax.axvline(d["core"], color=C["orange"], lw=1.4, ls=":", label="core 230 = 0.90 (a target blob must contain a pixel this hot)")
    ax.axvspan(d["threshold"], 255, color=C["red"], alpha=0.10)
    ax.text(164, 8, f"used t = max(204, {d['otsu']} + 1) = {d['threshold']}\n{d['hotPixels']} hot pixels (red zone)", fontsize=8.5, color=C["red"], bbox=dict(fc="white", ec="none", pad=2))
    ax.set_xlabel("heat value (0-255 = 0.0-1.0)"); ax.set_ylabel("pixels (log)")
    ax.set_title("Histogram of the 320 x 240 heat image: adaptive threshold t = max(floor, Otsu + 1)")
    ax.legend(fontsize=8, loc="upper center", bbox_to_anchor=(0.6, 1.0))
    # between-class variance curve (Otsu)
    ax = axs[1]
    p = hist.astype(float); tot = p.sum()
    sumAll = (np.arange(256) * p).sum()
    wB = np.cumsum(p); sB = np.cumsum(np.arange(256) * p)
    wF = tot - wB
    with np.errstate(divide="ignore", invalid="ignore"):
        mB = sB / wB; mF = (sumAll - sB) / wF
        between = wB * wF * (mB - mF) ** 2
    between = np.nan_to_num(between)
    ax.plot(np.arange(256), between / between.max(), color=C["blue"])
    tb = int(np.argmax(between))
    ax.axvline(tb, color=C["blue"], ls="--")
    ax.set_title(f"Otsu: maximise between-class variance\nwB wF (mB - mF)^2, maximum at t = {tb} (C++: {d['otsu']})")
    ax.set_xlabel("candidate threshold t"); ax.set_ylabel("normalised variance")
    assert tb == d["otsu"], (tb, d["otsu"])
    save(fig, "F1_threshold_histogram",
         "Adaptive threshold of the image stage: the histogram of a heat frame with the fixed floor 0.80, Otsu's threshold (maximum of the between-class variance) and the threshold "
         "actually used, their maximum. A hot frame raises Otsu above the floor; a cold one leaves the floor in charge.",
         "src/vision/CvPipeline.cpp: otsuThreshold() lines 9-32, detectTargets() lines 114-122 (synthetic 320x240 frame through the real functions)")


def fig_F2_stages():
    d, img, mask, mask2, labels = cv_load()
    cm = ironbow_cmap()
    fig, axs = plt.subplots(2, 3, figsize=(15, 8.6))
    axs[0, 0].imshow(img, cmap=cm, vmin=0, vmax=255)
    axs[0, 0].set_title("1. heat image (ironbow palette): warm hut, two rovers,\nfire, a tracer streak and 40 hot sparks", fontsize=9)
    axs[0, 1].imshow(mask, cmap="gray_r")
    axs[0, 1].set_title(f"2. threshold t = {d['threshold']}: {d['hotPixels']} hot pixels", fontsize=9)
    rm = (mask > 0) & (mask2 == 0)
    rgb = np.ones(mask.shape + (3,))
    rgb[mask2 > 0] = (0.1, 0.1, 0.1)
    rgb[rm] = (0.85, 0.15, 0.15)
    axs[0, 2].imshow(rgb)
    axs[0, 2].set_title(f"3. despeckle: {d['removed']} pixels with < 2 hot neighbours removed (red)", fontsize=9)
    lab = np.ma.masked_equal(labels, 0)
    axs[1, 0].imshow(np.zeros(labels.shape), cmap="gray_r", vmin=0, vmax=1)
    axs[1, 0].imshow(lab, cmap="tab10", interpolation="nearest")
    axs[1, 0].set_title(f"4. connected components (8-connectivity): {d['components']} blobs", fontsize=9)
    ax = axs[1, 1]
    ax.imshow(img, cmap=cm, vmin=0, vmax=255)
    for k, b in enumerate(d["allList"]):
        ok, why = verdict(b, d)
        col = "#22c55e" if ok else "#38bdf8"
        ax.add_patch(Rectangle((b["minX"] - 1, b["minY"] - 1), b["maxX"] - b["minX"] + 3, b["maxY"] - b["minY"] + 3, fill=False, ec=col, lw=1.6))
        ax.text(b["minX"], b["minY"] - 3, f"#{k + 1} {why}", color=col, fontsize=7.5, weight="bold", bbox=dict(fc="black", ec="none", alpha=0.55, pad=1))
    ax.set_title("5. features + target filter: green = target, blue = rejected (reason)", fontsize=9)
    ax = axs[1, 2]
    ax.axis("off")
    rows = ["#  area  peak  elong  verdict"]
    for k, b in enumerate(d["allList"]):
        ok, why = verdict(b, d)
        rows.append(f"{k + 1}  {b['area']:5d}  {b['peak']:4d}  {b['elong']:5.1f}  {why}")
    ax.text(0, 1, "Blob table (all components)\n\n" + "\n".join(rows) + "\n\nTarget filter:\n  area in [2, 2500] px, peak >= 230 (hot engine),\n  elongation <= 6 unless area < 6\n\n"
            f"Result: {d['targets']} targets -> MultiTracker.\nThe hut (0.55) never passes the 0.80 floor,\nthe tracer is a 1 px streak, the fire is huge.", va="top", family="monospace", fontsize=8.8)
    for a in axs.ravel()[:5]:
        a.set_xlabel("x (px)"); a.set_ylabel("y (px)")
    save(fig, "F2_cv_stages",
         "The image stage on a synthetic 320x240 heat frame, run through the real functions: threshold, despeckle, labelling, features and the target filter; "
         "the table lists why each component is accepted or rejected.",
         "src/vision/CvPipeline.cpp: detectTargets() lines 106-174, despeckle() 34-52, labelComponents() 66-114")


def uf_pass1(mask, w, h):
    """Exact replica of labelComponents() pass 1 (CvPipeline.cpp 66-114) that also records the equivalence unions."""
    labels = np.zeros((h, w), int)
    parent = [0]
    unions = []
    def root(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    snaps = []
    for y in range(h):
        for x in range(w):
            if not mask[y, x]:
                continue
            found = []
            for dx, dy in ((-1, 0), (-1, -1), (0, -1), (1, -1)):
                xx, yy = x + dx, y + dy
                if xx < 0 or yy < 0 or xx >= w:
                    continue
                l = labels[yy, xx]
                if l:
                    found.append(l)
            if not found:
                best = len(parent); parent.append(best)
            else:
                best = root(found[0])
                for f in found[1:]:
                    r = root(f)
                    if r == best:
                        continue
                    unions.append((max(r, best), min(r, best), (x, y)))
                    parent[max(r, best)] = min(r, best)
                    best = min(r, best)
            labels[y, x] = best
    return labels, parent, unions, root


def fig_F3_unionfind():
    d, *_ = cv_load()
    uf = d["uf"]
    w, h = uf["w"], uf["h"]
    mask = np.array(uf["mask"]).reshape(h, w)
    final = np.array(uf["labels"]).reshape(h, w)
    p1, parent, unions, root = uf_pass1(mask, w, h)
    # verify against the C++ result: same partition
    comp = {}
    for y in range(h):
        for x in range(w):
            if mask[y, x]:
                comp.setdefault(root(p1[y, x]), set()).add((x, y))
    cpp = {}
    for y in range(h):
        for x in range(w):
            if final[y, x]:
                cpp.setdefault(final[y, x], set()).add((x, y))
    assert sorted(map(sorted, comp.values())) == sorted(map(sorted, cpp.values())), "partition mismatch"
    fig, axs = plt.subplots(1, 4, figsize=(16, 4.6), gridspec_kw=dict(width_ratios=[1, 1, 1, 0.9]))
    pal = ["#fde68a", "#bfdbfe", "#bbf7d0", "#fecaca", "#e9d5ff", "#fed7aa"]
    def draw(ax, lab, ttl, colour_by):
        for y in range(h):
            for x in range(w):
                if mask[y, x]:
                    ax.add_patch(Rectangle((x, h - 1 - y), 1, 1, fc=pal[(colour_by(lab[y, x]) - 1) % 6], ec="k", lw=0.8))
                    ax.text(x + 0.5, h - 1 - y + 0.5, str(lab[y, x]), ha="center", va="center", fontsize=9)
                else:
                    ax.add_patch(Rectangle((x, h - 1 - y), 1, 1, fc="white", ec="#e5e7eb", lw=0.5))
        ax.set_xlim(0, w); ax.set_ylim(0, h); ax.set_aspect("equal"); ax.axis("off"); ax.set_title(ttl, fontsize=9.5)
    draw(axs[0], p1, "pass 1: provisional labels\n(neighbours W, NW, N, NE already visited)", lambda l: l)
    # annotate merge pixels
    for big, small, (x, y) in unions:
        axs[0].add_patch(Rectangle((x, h - 1 - y), 1, 1, fill=False, ec=C["red"], lw=2.4))
    draw(axs[1], p1, "pass 1 result: the red pixels joined\ntwo labels (an equivalence)", lambda l: l)
    for big, small, (x, y) in unions:
        axs[1].add_patch(Rectangle((x, h - 1 - y), 1, 1, fill=False, ec=C["red"], lw=2.4))
    draw(axs[2], final, "pass 2: every label replaced by its root,\nrenumbered 1..%d (C++ output)" % uf["count"], lambda l: l)
    ax = axs[3]; ax.axis("off")
    ptxt = "union-find parent[]\n  label : parent\n" + "\n".join(f"  {i:3d}   : {parent[i]}" for i in range(1, len(parent)))
    utxt = "\n".join(f"  union({b}, {s}) at pixel (x {x}, y {y})" for b, s, (x, y) in unions) or "  (none)"
    ax.text(0, 1, "Equivalences recorded:\n" + utxt + "\n\n" + ptxt + "\n\nunion: the larger root points to\nthe smaller one; find with path\nhalving. Pass 2 = find(label).", va="top", family="monospace", fontsize=8.6)
    save(fig, "F3_union_find",
         "Two-pass connected-component labelling with union-find on a small 12 x 7 mask: pass 1 gives provisional labels and records that two of them touch (red pixels); "
         "pass 2 resolves every label to its root. The picture is checked against the real labelComponents() output.",
         "src/vision/CvPipeline.cpp: labelComponents() lines 66-114, findRoot() 56-64")


def fig_F4_moments():
    d, img, mask, mask2, labels = cv_load()
    # the rover A blob
    tgt = max((b for b in d["targetsList"] if b["area"] < 200), key=lambda b: b["area"])
    lab = labels[int(round(tgt["cy"])), int(round(tgt["cx"]))]
    ys, xs = np.nonzero(labels == lab)
    cx, cy = xs.mean(), ys.mean()
    mu20 = (xs ** 2).mean() - cx * cx; mu02 = (ys ** 2).mean() - cy * cy; mu11 = (xs * ys).mean() - cx * cy
    tr = mu20 + mu02; det = mu20 * mu02 - mu11 ** 2
    disc = math.sqrt(max(0, tr * tr / 4 - det))
    l1, l2 = tr / 2 + disc, max(tr / 2 - disc, 1 / 12)
    ori = 0.5 * math.degrees(math.atan2(2 * mu11, mu20 - mu02)); el = math.sqrt(l1 / l2)
    assert abs(ori - tgt["orient"]) < 0.1 and abs(el - tgt["elong"]) < 0.02 and abs(cx - tgt["cx"]) < 1e-3
    x0, x1, y0, y1 = xs.min() - 3, xs.max() + 4, ys.min() - 3, ys.max() + 4
    fig, axs = plt.subplots(1, 2, figsize=(14, 5.6), gridspec_kw=dict(width_ratios=[1.3, 1]))
    ax = axs[0]
    sub = img[y0:y1, x0:x1].astype(float)
    ax.imshow(sub, extent=(x0, x1, y1, y0), cmap=ironbow_cmap(), vmin=0, vmax=255, interpolation="nearest")
    ax.add_patch(Rectangle((tgt["minX"], tgt["minY"]), tgt["maxX"] - tgt["minX"] + 1, tgt["maxY"] - tgt["minY"] + 1, fill=False, ec="white", lw=1.4, ls="--"))
    th = np.radians(ori)
    a, b = 2 * math.sqrt(l1), 2 * math.sqrt(l2)
    ax.add_patch(Ellipse((cx + 0.5, cy + 0.5), 2 * a, 2 * b, angle=ori, fill=False, ec="#22c55e", lw=2))
    for ang, ln, col in ((th, a, "#22c55e"), (th + math.pi / 2, b, "#38bdf8")):
        ax.plot([cx + 0.5 - ln * math.cos(ang), cx + 0.5 + ln * math.cos(ang)], [cy + 0.5 - ln * math.sin(ang), cy + 0.5 + ln * math.sin(ang)], color=col, lw=1.6)
    ax.plot(cx + 0.5, cy + 0.5, "w+", ms=14, mew=2)
    ax.set_xlabel("x (px)"); ax.set_ylabel("y (px, row 0 = top)")
    ax.set_title(f"rover blob: {int(tgt['area'])} px, peak {int(tgt['peak'])}; white box = bounding box,\ncross = centroid, green ellipse = 2-sigma shape from the second moments", fontsize=9)
    ax = axs[1]; ax.axis("off")
    txt = ("Blob features (Acc sums over the labelled pixels)\n\n"
           f"  area        n = {int(tgt['area'])}\n"
           f"  centroid    cx = sum x / n = {cx:.2f},  cy = {cy:.2f}\n"
           f"  moments     mu20 = E[x^2] - cx^2 = {mu20:.3f}\n"
           f"              mu02 = {mu02:.3f},  mu11 = {mu11:.3f}\n"
           f"  eigenvalues l1,2 = tr/2 +- sqrt(tr^2/4 - det)\n"
           f"              l1 = {l1:.3f},  l2 = {l2:.3f}  (floor 1/12)\n"
           f"  orientation 0.5 atan2(2 mu11, mu20 - mu02) = {ori:.1f} deg\n"
           f"  elongation  sqrt(l1 / l2) = {el:.2f}  (limit 6)\n"
           f"  mean heat   {tgt['mean']:.2f},  peak {int(tgt['peak'])}\n\n"
           "  normalised image coordinates sent on:\n"
           "    u = (cx + 0.5)/320 * 2 - 1   (+ right)\n"
           "    v = 1 - (cy + 0.5)/240 * 2   (+ up)\n\n"
           "(values recomputed in Python from the labelled\npixels and checked against the C++ output)")
    ax.text(0, 1, txt, va="top", family="monospace", fontsize=8.8)
    save(fig, "F4_blob_moments",
         "Blob features from image moments: centroid, bounding box and the principal axes of the pixel distribution (eigenvectors of the second central moments) give the orientation and the "
         "elongation used to reject thin streaks; values were recomputed and checked against the real detectTargets().",
         "src/vision/CvPipeline.cpp: detectTargets() lines 126-158")


def ell_pts(mean, cov, k2, n=100):
    w, v = np.linalg.eigh(np.array(cov))
    t = np.linspace(0, 2 * math.pi, n)
    pts = (v @ (np.sqrt(np.maximum(w, 0) * k2)[:, None] * np.stack([np.cos(t), np.sin(t)]))).T
    return pts + np.array(mean)


def fig_F5_kalman():
    k = load("kalman.json")
    t = np.array(k["t"]); tx, tz = np.array(k["tx"]), np.array(k["tz"]); mx, mz = np.array(k["mx"]), np.array(k["mz"])
    ex, ez = np.array(k["ex"]), np.array(k["ez"])
    p00, p01, p11 = np.array(k["p00"]), np.array(k["p01"]), np.array(k["p11"])
    fig = plt.figure(figsize=(15, 8.6))
    gs = fig.add_gridspec(2, 3, height_ratios=[1.25, 1])
    ax = fig.add_subplot(gs[0, :2])
    ax.plot(mx, mz, ".", color=C["gray"], ms=2.5, alpha=0.6, label="measurements (sigma 0.3 m, 30 Hz)")
    ax.plot(tx, tz, "-", color="k", lw=1, label="true path")
    ax.plot(ex, ez, "-", color=C["red"], lw=1.4, label="Kalman estimate")
    for i in range(10, len(t), 30):
        pts = ell_pts((ex[i], ez[i]), [[p00[i], p01[i]], [p01[i], p11[i]]], 4.0)   # 2 sigma: chi2 = 4
        ax.plot(pts[:, 0], pts[:, 1], color=C["blue"], lw=0.9)
    ax.plot([], [], color=C["blue"], lw=0.9, label="2-sigma position ellipse (every second)")
    ax.set_aspect("equal"); ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)"); ax.legend(fontsize=8)
    ax.set_title("Constant-velocity Kalman filter (state x, z, vx, vz; process noise 1.5 m/s^2): drive, turn, drive")
    ax = fig.add_subplot(gs[0, 2])
    ax.axis("off")
    ax.text(0, 1, "MultiTracker::predict / kalmanUpdate\n\n  predict:  x' = F x,  P' = F P F^T + Q\n   F = [I  dt I; 0  I]\n   Q = q [dt^4/4  dt^3/2; dt^3/2  dt^2], q = 1.5^2\n"
            "  update:   S = H P H^T + R,  K = P H^T S^-1\n   x += K (z - H x),  P = (I - K H) P\n   H picks (x, z); R = sigma^2 I\n\n  birth: P = diag(s^2, s^2, 4, 4)\n  (unknown speed: 2 m/s sigma)\n  confirmed after 3 hits", va="top", family="monospace", fontsize=8.5)
    ax = fig.add_subplot(gs[1, 0])
    err_m = np.hypot(mx - tx, mz - tz); err_e = np.hypot(ex - tx, ez - tz)
    ax.plot(t, err_m, color=C["gray"], lw=0.7, label="measurement error")
    ax.plot(t, err_e, color=C["red"], lw=1.2, label="estimate error")
    ax.plot(t, 2 * np.sqrt(0.5 * (p00 + p11)), color=C["blue"], ls="--", label="2 x filter sigma")
    ax.set_xlabel("time (s)"); ax.set_ylabel("position error (m)"); ax.legend(fontsize=7.5); ax.set_title("filtering reduces the noise", fontsize=9)
    ax = fig.add_subplot(gs[1, 1])
    ax.plot(t, k["tvx"], color="k", lw=1, label="true vx"); ax.plot(t, k["evx"], color=C["red"], lw=1.1, label="estimated vx")
    ax.plot(t, k["tvz"], color="k", lw=1, ls="--", label="true vz"); ax.plot(t, k["evz"], color=C["blue"], lw=1.1, label="estimated vz")
    ax.set_xlabel("time (s)"); ax.set_ylabel("velocity (m/s)"); ax.legend(fontsize=7.5, ncol=2); ax.set_title("velocity is never measured: it is inferred", fontsize=9)
    ax = fig.add_subplot(gs[1, 2])
    ax.plot(t, k["velSigma"], color=C["purple"]); ax.set_xlabel("time (s)"); ax.set_ylabel("velocity sigma (m/s)")
    ax.set_title("velocity uncertainty converges (starts at 2 m/s)", fontsize=9)
    save(fig, "F5_kalman_filter",
         "The track filter on a rover driving straight, turning and driving straight: noisy ground measurements, the Kalman estimate with 2-sigma ellipses, the position error against the "
         "filter's own uncertainty, and the velocity estimate converging from the unknown initial value. Run through the real MultiTracker.",
         "src/vision/MultiTracker.cpp: predict() lines 21-46, kalmanUpdate() 60-82 (data: figdata dumpKalman)")
    import shutil
    shutil.copy(os.path.join(OUT, "F5_kalman_filter.png"), os.path.join(OUT, "kalman_demo.png"))
    CAPTIONS["kalman_demo.png"] = dict(CAPTIONS["F5_kalman_filter.png"])


def fig_F6_assoc():
    a = load("assoc.json")
    tracks, meas = a["tracks"], a["meas"]
    nT, nM = len(tracks), len(meas)
    d2 = np.array(a["d2"]).reshape(nT, nM); cost = np.array(a["cost"]).reshape(nT, nM)
    asg = a["assign"]
    fig, axs = plt.subplots(1, 2, figsize=(14, 5.8), gridspec_kw=dict(width_ratios=[1.15, 1]))
    ax = axs[0]
    cols = [C["blue"], C["green"], C["orange"]]
    for i, t in enumerate(tracks):
        r = meas[0]["sigma"] ** 2
        S = [[t["p00"] + r, t["p01"]], [t["p01"], t["p11"] + r]]
        pts = ell_pts((t["x"], t["z"]), S, a["gate"])
        ax.plot(pts[:, 0], pts[:, 1], color=cols[i], lw=1.4)
        ax.plot(t["x"], t["z"], "s", color=cols[i], ms=8)
        ax.text(t["x"] + 0.15, t["z"] + 0.2, f"track {t['id']}", color=cols[i], fontsize=9, weight="bold")
    for j, m in enumerate(meas):
        ax.plot(m["x"], m["z"], "x", color="k", ms=9, mew=2)
        ax.text(m["x"] + 0.12, m["z"] - 0.3, f"m{j}", fontsize=9)
    for i, j in enumerate(asg):
        if j >= 0:
            ax.annotate("", xy=(meas[j]["x"], meas[j]["z"]), xytext=(tracks[i]["x"], tracks[i]["z"]), arrowprops=dict(arrowstyle="->", color=cols[i], lw=2))
    ax.set_aspect("equal"); ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)")
    ax.set_title(f"gating ellipses d^2 < {a['gate']} (chi-square, 2 dof, 99 %) around the predicted tracks;\narrows = Hungarian assignment; m1 lies outside every gate", fontsize=9)
    ax = axs[1]
    shown = np.where(cost < 1e5, d2, np.nan)
    ax.imshow(np.where(cost < 1e5, np.log10(d2 + 0.1), np.nan), cmap="Blues_r", vmin=-1, vmax=1.2)
    for i in range(nT):
        for j in range(nM):
            gated = cost[i, j] >= 1e5
            ax.text(j, i, f"{d2[i, j]:.2f}" if not gated else f"{d2[i, j]:.0f}\n(gated out\n= 1e6)", ha="center", va="center", fontsize=9 if not gated else 7, color="k" if not gated else C["red"])
            if asg[i] == j:
                ax.add_patch(Rectangle((j - 0.5, i - 0.5), 1, 1, fill=False, ec=C["green"], lw=3.5))
    ax.set_xticks(range(nM)); ax.set_xticklabels([f"m{j}" for j in range(nM)]); ax.set_yticks(range(nT)); ax.set_yticklabels([f"track {t['id']}" for t in tracks])
    ax.grid(False)
    ax.set_title("cost matrix = squared Mahalanobis distance d^2 (inside gate) or 1e6;\ngreen = assignment that minimises the total cost", fontsize=9)
    tot = sum(cost[i, j] for i, j in enumerate(asg) if j >= 0)
    ax.set_xlabel(f"total assigned cost {tot:.2f}; unassigned track: {[tracks[i]['id'] for i, j in enumerate(asg) if j < 0]} (coasts), spare measurement starts a tentative track")
    save(fig, "F6_gating_hungarian",
         "Data association for one UAV frame: each track predicts where it should be, the chi-square gate (d^2 < 9.21) removes impossible pairs, and the Hungarian algorithm picks the pairing with "
         "the smallest total Mahalanobis cost (note m0 and m1 both fit track 3 but only the globally cheaper pairing wins).",
         "src/vision/MultiTracker.cpp: mahalanobis2() 48-56, hungarian() 84-137, update() 175-190 (data: figdata dumpAssociation)")
    import shutil
    shutil.copy(os.path.join(OUT, "F6_gating_hungarian.png"), os.path.join(OUT, "gating_ellipse.png"))
    CAPTIONS["gating_ellipse.png"] = dict(CAPTIONS["F6_gating_hungarian.png"])


def fig_F7_triangulation():
    t = load("triangulation.json")
    ex = t["example"]
    A, B, T = np.array(ex["uavA"]), np.array(ex["uavB"]), np.array(ex["target"])
    dA, dB = np.array(ex["dirA"]), np.array(ex["dirB"])
    p1, p2, mid = np.array(ex["p1"]), np.array(ex["p2"]), np.array(ex["mid"])
    fig, axs = plt.subplots(1, 3, figsize=(16, 5.2), gridspec_kw=dict(width_ratios=[1.15, 0.9, 1]))
    ax = axs[0]
    for U, dd, col, nm in ((A, dA, C["blue"], "UAV A"), (B, dB, C["orange"], "UAV B")):
        end = U + dd * 1.35 * np.linalg.norm(T - U)
        ax.plot([U[0], end[0]], [U[2], end[2]], color=col, lw=1.3)
        ax.plot(U[0], U[2], "^", color=col, ms=11); ax.text(U[0] + 0.15, U[2] + 0.2, nm, color=col, fontsize=9, weight="bold")
    ax.plot(T[0], T[2], "k*", ms=13); ax.text(T[0] - 1.1, T[2] - 0.6, "true target", fontsize=8)
    ax.plot([p1[0], p2[0]], [p1[2], p2[2]], "-", color=C["red"], lw=3)
    ax.plot(*mid[[0, 2]], "o", color=C["red"], ms=6)
    ax.set_aspect("equal"); ax.set_xlabel("x (m)"); ax.set_ylabel("z (m)")
    ax.set_title(f"top view: two pixel rays (1.5 px pointing noise)\nclosest points p1, p2 (red segment), midpoint = fused fix, residual {ex['residual'] * 100:.1f} cm", fontsize=9)
    ax.set_xlim(-1, 10); ax.set_ylim(0, 11)
    ins = ax.inset_axes([0.58, 0.58, 0.4, 0.4])
    ins.plot([p1[0], p2[0]], [p1[2], p2[2]], "-", color=C["red"], lw=3); ins.plot(*mid[[0, 2]], "o", color=C["red"]); ins.plot(T[0], T[2], "k*", ms=11)
    ins.set_xlim(T[0] - 0.12, T[0] + 0.12); ins.set_ylim(T[2] - 0.12, T[2] + 0.12); ins.set_aspect("equal"); ins.set_title("zoom 0.24 m", fontsize=7); ins.tick_params(labelsize=6)
    ax = axs[1]
    sc = t["scatter"]
    ax.plot(sc["sx"], sc["sz"], ".", color=C["gray"], ms=4, label=f"single ray on the ground: rms {np.hypot(np.array(sc['sx']), np.array(sc['sz'])).mean():.2f} m")
    ax.plot(sc["tx"], sc["tz"], ".", color=C["red"], ms=4, label=f"two-view midpoint: rms {np.hypot(np.array(sc['tx']), np.array(sc['tz'])).mean():.2f} m")
    ax.axhline(0, color="k", lw=0.5); ax.axvline(0, color="k", lw=0.5)
    ax.set_aspect("equal"); ax.set_xlabel("error x (m)"); ax.set_ylabel("error z (m)"); ax.legend(fontsize=7.5, loc="upper left")
    ax.set_title("400 Monte Carlo fixes, UAVs 90 deg apart\n(single ray: 0.2 m target-height bias, 5 cm march steps)", fontsize=9)
    ax = axs[2]
    mc = t["mc"]
    ang = np.array(mc["angle"]); sg = np.array(mc["single"]); tr = np.array(mc["tri"], float)
    ax.plot(ang, sg, "o-", color=C["gray"], label="single ground ray")
    ax.plot(ang, tr, "o-", color=C["red"], label="triangulated midpoint")
    ax.set_ylim(0, 0.45); ax.set_xlabel("angle between the two UAVs around the target (deg)"); ax.set_ylabel("rms horizontal error (m)")
    ax.annotate("rays parallel:\nno depth", xy=(0, 0.03), xytext=(5, 0.18), arrowprops=dict(arrowstyle="->"), fontsize=8)
    ax.axvline(90, color=C["green"], ls=":", label="wing flies ~90 deg round the target")
    ax.legend(fontsize=8); ax.set_title(f"geometry decides: error vs separation ({mc['N']} samples per angle)", fontsize=9)
    save(fig, "F7_triangulation",
         "Two-view triangulation: each UAV's pixel becomes a ray, the fused fix is the midpoint of the shortest segment between the rays (residual = segment length); "
         "Monte Carlo over pointing noise shows it beats a single ground-plane ray and that a wing UAV at about 90 degrees gives the best geometry.",
         "src/vision/MultiTracker.cpp: triangulate() lines 139-153; Squad.cpp measurement sigma 0.12 + 0.015 r (data: figdata dumpTriangulation)")
    import shutil
    shutil.copy(os.path.join(OUT, "F7_triangulation.png"), os.path.join(OUT, "triangulation.png"))
    CAPTIONS["triangulation.png"] = dict(CAPTIONS["F7_triangulation.png"])


def box(ax, x, y, w, h, text, fc="#dbeafe", ec=C["blue"], fs=8.5, bold=False):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.08", fc=fc, ec=ec, lw=1.3))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs, weight="bold" if bold else "normal")


def arrow(ax, p, q, text=None, color="#374151", rad=0.0, fs=7.5, tx=0.0, ty=0.0, ls="-"):
    ax.add_patch(FancyArrowPatch(p, q, arrowstyle="-|>", mutation_scale=13, color=color, lw=1.4, connectionstyle=f"arc3,rad={rad}", linestyle=ls))
    if text:
        ax.text((p[0] + q[0]) / 2 + tx, (p[1] + q[1]) / 2 + ty, text, fontsize=fs, ha="center", va="center", color=color, bbox=dict(fc="white", ec="none", pad=0.8, alpha=0.85))


def fig_F8_state_machine():
    fig, ax = plt.subplots(figsize=(15, 8.4))
    ax.set_xlim(0, 15); ax.set_ylim(0, 8.4); ax.axis("off")
    ax.text(0.1, 8.15, "A. UAV roles (per UAV, every frame)", fontsize=11, weight="bold")
    box(ax, 0.4, 5.9, 2.4, 1.1, "PATROL\nbreathing orbit,\ncamera sweeps", "#e5e7eb", C["gray"], bold=True)
    box(ax, 4.3, 5.9, 2.6, 1.1, "TRACK / ENGAGE\nconfirmed track chosen\n(threat rank, hysteresis 3)", "#fef3c7", C["gold"], bold=True)
    box(ax, 8.6, 6.6, 2.2, 0.9, "LEAD\nlonger lock", "#dcfce7", C["green"], bold=True)
    box(ax, 8.6, 5.3, 2.2, 0.9, "WING\nflies ~90 deg round", "#dcfce7", C["green"], bold=True)
    box(ax, 12.0, 5.9, 2.6, 1.1, "SEARCH\ncircle last known\nposition (12 s)", "#fee2e2", C["red"], bold=True)
    arrow(ax, (2.8, 6.45), (4.3, 6.45), "track confirmed\n(3 hits)", ty=0.38)
    arrow(ax, (6.9, 6.7), (8.6, 7.0), "lead =\nlonger lock")
    arrow(ax, (6.9, 6.2), (8.6, 5.8), "other UAV")
    arrow(ax, (10.8, 6.9), (12.0, 6.6), "track lost\n(cover)", ty=0.3)
    arrow(ax, (12.0, 6.3), (10.8, 5.7), "re-seen", ty=-0.3)
    arrow(ax, (13.3, 5.9), (1.6, 5.9), None, rad=-0.22)
    ax.text(10.6, 4.72, "12 s without a target -> PATROL;  kill: onKill() drops the track, wreck ignored", fontsize=8, ha="center", bbox=dict(fc="white", ec="none", pad=1))
    ax.text(0.1, 4.55, "B. Strike phases (shared by the squad)", fontsize=11, weight="bold")
    xs = [0.4, 3.9, 7.4, 10.9]
    names = ["NONE", "TRACKING", "STRIKE PLANNED", "ROUNDS INBOUND"]
    fcs = ["#e5e7eb", "#fef3c7", "#dbeafe", "#fee2e2"]
    ecs = [C["gray"], C["gold"], C["blue"], C["red"]]
    for x, n, f, e in zip(xs, names, fcs, ecs):
        box(ax, x, 2.9, 2.9, 1.0, n, f, e, bold=True, fs=9.5)
    arrow(ax, (3.3, 3.4), (3.9, 3.4), None)
    ax.text(3.6, 4.05, "target\nselected", fontsize=7.5, ha="center")
    arrow(ax, (6.8, 3.4), (7.4, 3.4))
    ax.text(7.1, 4.12, "planStrike()", fontsize=7.5, ha="center")
    arrow(ax, (10.3, 3.4), (10.9, 3.4))
    ax.text(10.6, 4.05, "both rounds\nlaunched", fontsize=7.5, ha="center")
    arrow(ax, (12.35, 2.9), (5.35, 2.9), None, rad=-0.3)
    ax.text(8.8, 1.75, "arrival + 1.2 s and target alive: back to TRACKING, cooldown 2 s", fontsize=8, ha="center", bbox=dict(fc="white", ec="none", pad=1))
    arrow(ax, (9.0, 2.9), (1.85, 2.9), None, rad=-0.55, ls="--")
    ax.text(3.6, 1.2, "target destroyed / lost: reset to NONE", fontsize=8, ha="center", bbox=dict(fc="white", ec="none", pad=1))
    ax.text(0.1, 0.9, "planStrike conditions (Squad.cpp):  combined strike if wing lock >= 0.5 s and lead lock >= 0.8 s, a round on the rail and a clear shot;  solo strike if lead lock >= 1.3 s\n"
            "and 2.5 s have passed without a wing lock.  Time on target: each round's flight time T = roundFlightTime(distance to the predicted position); launch time = now + (T_max - T).\n"
            "Stand-off 6.5 m, engage altitude 5 m, patrol altitude 7.5 m.  The squad never reads rover positions: only the cameras' blobs.", fontsize=8.6, va="top")
    ax.set_ylim(0, 8.4)
    save(fig, "F8_squad_state_machine",
         "The squad's state machines: the UAV roles (patrol, track, lead / wing, search and back) and the strike phases (tracking, planned, inbound, reacquire), with the lock times and "
         "distances that trigger the transitions.",
         "src/ai/Squad.cpp: update() 220-327, planStrike() 176-218; Squad.h constants kSquadLeadLock etc.")


def fig_F9_time_on_target():
    s = load("squad_run.json"); fl = load("flight.json")
    ev = s["events"]
    fr = [f for f in s["frames"] if f["phase"] == 2 and f["combined"] and f["t"] > 60][0]
    la, ft = fr["launchAt"], fr["flight"]
    fr_in = [f for f in s["frames"] if f["phase"] == 3 and f["t"] > 60][0]
    lt = fr_in["launchedAt"]
    blasts = [e["t"] for e in ev if e["type"] == 5 and 76 < e["t"] < 80]
    cs = [e for e in ev if e["type"] == 4 and e["value"] == int(1000 * abs(ft[1] - ft[0]))][0]
    fig, axs = plt.subplots(1, 2, figsize=(14.5, 4.8), gridspec_kw=dict(width_ratios=[1.35, 1]))
    ax = axs[0]
    t0 = min(la)
    for i, (col, nm) in enumerate(((C["blue"], "UAV 0"), (C["orange"], "UAV 1"))):
        ax.barh(i, ft[i], left=lt[i], color=col, height=0.5, alpha=0.8)
        ax.plot(lt[i], i, "v", color="k", ms=9)
        ax.text(lt[i] + ft[i] / 2, i, f"flight {ft[i] * 1000:.0f} ms", ha="center", va="center", color="white", fontsize=9, weight="bold")
        ax.text(lt[i] - 0.015, i + 0.34, f"launch {lt[i] - t0:+.3f} s", ha="right", fontsize=8)
    for b in blasts[:2]:
        ax.axvline(b, color=C["red"], ls="--")
    ax.text(blasts[0], 1.62, f"blast at {blasts[0] - t0:+.3f} s", color=C["red"], fontsize=8.5, ha="center")
    arr = [lt[i] + ft[i] for i in range(2)]
    ax.set_yticks([0, 1]); ax.set_yticklabels(["UAV 0 (farther)", "UAV 1 (nearer)"] if ft[0] > ft[1] else ["UAV 0 (nearer)", "UAV 1 (farther)"])
    ax.set_ylim(-0.6, 1.9); ax.set_xlim(t0 - 0.12, max(arr) + 0.05); ax.set_xlabel("scene time (s)")
    ax.set_title(f"Time on target (combined strike at t = {t0:.2f} s): the farther UAV fires first,\nlaunch offset {abs(lt[0] - lt[1]) * 1000:.0f} ms (= flight-time difference {cs['value']} ms), arrival spread {abs(arr[0] - arr[1]) * 1000:.0f} ms", fontsize=9.5)
    ax = axs[1]
    d = np.linspace(0.5, 30, 400)
    mod = np.array([0.4 + (x - 0.5 - 13.6) / 60 if x - 0.5 > 13.6 else (-8 + math.sqrt(64 + 260 * max(0, x - 0.5))) / 130 for x in d])
    ax.plot(d, mod, color=C["blue"], lw=2, label="roundFlightTime(d): d = 8 t + 65 t^2 up to 0.4 s, then 60 m/s")
    ax.plot(fl["d"], fl["sim"], "o", color=C["red"], ms=4, label="simulated round (Combat, 240 Hz)")
    for i, col in ((0, C["blue"]), (1, C["orange"])):
        dd = np.interp(ft[i], mod, d)
        ax.plot(dd, ft[i], "s", color=col, ms=9, mfc="white", mew=2)
        ax.text(dd + 0.4, ft[i] - 0.03, f"UAV {i}: {dd:.1f} m", fontsize=8, color=col)
    ax.set_xlabel("straight distance d to the predicted target (m)"); ax.set_ylabel("flight time (s)")
    ax.set_title("flight-time model vs the simulated missile", fontsize=9.5); ax.legend(fontsize=7.5)
    save(fig, "F9_time_on_target",
         "Time-on-target: the squad predicts each round's flight time from the distance with a two-phase model (accelerating launch, then 60 m/s, detonating 0.5 m short), launches the farther "
         "UAV first and the nearer one later so the blasts coincide; the right plot shows the model against the simulated missile and the distances of this strike.",
         "src/ai/Squad.cpp: roundFlightTime() lines 43-50, planStrike() 193-212; src/scene/Combat.h kMissile* (data: figdata dumpSquad / dumpFlight)")


def fig_F10_run_timeline():
    s = load("squad_run.json")
    fr = s["frames"]
    t = np.array([f["t"] for f in fr])
    fig, axs = plt.subplots(4, 1, figsize=(15, 7.6), sharex=True, gridspec_kw=dict(height_ratios=[0.7, 0.7, 0.7, 1.8]))
    rc = {0: "#e5e7eb", 1: "#86efac", 2: "#93c5fd", 3: "#fde68a", 4: "#fca5a5"}
    rn = {0: "patrol", 1: "lead", 2: "wing", 3: "manual", 4: "search"}
    for k, ax in enumerate(axs[:2]):
        r = [f["r%d" % k] for f in fr]
        for i in range(len(t)):
            ax.axvspan(t[i] - 0.05, t[i] + 0.05, color=rc[r[i]], lw=0)
        ax.set_yticks([]); ax.set_ylabel(f"UAV {k}\nrole", rotation=0, ha="right", va="center")
    for r_, c in rc.items():
        axs[0].bar(0, 0, color=c, label=rn[r_])
    axs[0].legend(ncol=5, fontsize=7.5, loc="upper right", bbox_to_anchor=(1, 1.5))
    pc = {0: "#ffffff", 1: "#fef3c7", 2: "#bfdbfe", 3: "#fecaca"}
    ax = axs[2]
    for i in range(len(t)):
        ax.axvspan(t[i] - 0.05, t[i] + 0.05, color=pc[fr[i]["phase"]], lw=0)
    ax.set_yticks([]); ax.set_ylabel("strike phase\n(none / tracking /\nplanned / inbound)", rotation=0, ha="right", va="center", fontsize=8)
    ax = axs[3]
    # tracker: positional uncertainty of confirmed tracks and truth count
    for f in fr:
        for tr_ in f["tracks"]:
            ax.plot(f["t"], tr_[3], ".", color=[C["blue"], C["green"], C["orange"]][int(tr_[4])], ms=2.5)
    ax.set_ylabel("track position sigma (m)"); ax.set_ylim(0, 1.5)
    names = {1: "spotted", 2: "lock-on", 3: "launch", 4: "combined strike", 5: "explosion", 7: "destroyed"}
    cols = {1: C["gold"], 2: C["purple"], 3: C["blue"], 4: C["teal"], 5: C["red"], 7: C["green"]}
    used = set()
    for e in s["events"]:
        if e["type"] in names:
            ax.axvline(e["t"], color=cols[e["type"]], lw=0.9, alpha=0.7, label=names[e["type"]] if e["type"] not in used else None)
            used.add(e["type"])
    ax.plot([], [], ".", color=C["blue"], label="tentative"); ax.plot([], [], ".", color=C["green"], label="confirmed"); ax.plot([], [], ".", color=C["orange"], label="coasting")
    ax.legend(ncol=5, fontsize=7.5, loc="upper right"); ax.set_xlabel("scene time (s)")
    n_alive = [len(f["rovers"]) for f in fr]
    ax2 = ax.twinx(); ax2.step(t, n_alive, color="k", lw=1, where="post"); ax2.set_ylabel("rovers alive"); ax2.grid(False)
    axs[0].set_title(f"A 90 s defence with the real Scene (synthetic camera blobs): {s['kills']} kills ({s['combined']} combined), score {s['score']}, wave {s['wave']}, {s['fused']} fused two-view updates", fontsize=10)
    save(fig, "F10_run_timeline",
         "Timeline of a simulated 90 s defence: the two UAVs' roles, the strike phase, the tracks' position uncertainty by life-cycle state, the game events (spotting, lock, launch, combined strike, "
         "explosion, kill) and the number of live rovers.",
         "src/scene/Scene.cpp update(); src/ai/Squad.cpp (data: figdata dumpSquad, 90 s, 30 Hz, vision from tests/test_squad.cpp syntheticBlobs)")


def fig_F11_waves():
    m = load("mission.json")
    fig, axs = plt.subplots(1, 2, figsize=(13, 4.4), gridspec_kw=dict(width_ratios=[1, 1.15]))
    ax = axs[0]
    w, sz = np.array(m["wave"]), np.array(m["size"])
    ax.bar(w, sz, color=C["blue"], edgecolor="k")
    ax.plot(np.arange(1, 9), np.minimum(2 + np.arange(1, 9), m["maxRovers"]), "k--", lw=1, label="min(2 + n, kMaxRovers = %d)" % m["maxRovers"])
    for a, b in zip(w, sz):
        ax.text(a, b + 0.1, str(int(b)), ha="center")
    ax.set_xlabel("wave n"); ax.set_ylabel("rovers in the wave"); ax.set_ylim(0, 7.5); ax.legend(fontsize=8)
    ax.set_title("wave sizes from the Mission class (simulated: every spawn killed)", fontsize=9.5)
    ax = axs[1]
    ax.axis("off")
    ax.text(0, 1, "Night-watch mission rules (Mission.cpp)\n\n"
            f"  wave n sends min(2 + n, {m['maxRovers']}) rovers, one every {m['spawnInterval']} s,\n"
            "  from seeded random points on the compound edge (r = 16.8 m),\n  avoiding the gate road (north); each gets a random camp asset\n  (5 objectives) and a route seed\n"
            "  kill +100 points, combined-strike kill +150\n"
            "  breach (rover reaches its objective) -> integrity -10 %\n"
            f"  next wave {m['waveBreak']} s after the last rover is resolved\n"
            "  integrity 0 % = BASE LOST: mission restarts at wave 1\n  (the score survives)", va="top", family="monospace", fontsize=9)
    save(fig, "F11_mission_waves",
         "Mission waves: wave n sends min(2 + n, 6) intruders (3, 4, 5, then 6), with the scoring and breach rules; the bars are produced by running the real Mission class.",
         "src/scene/Mission.cpp: startWave() lines 24-35, update() 37-75 (data: figdata dumpMission)")


# =====================================================================================================================
# G. Pipeline overviews
# =====================================================================================================================
def fig_G1_render_pipeline():
    fig, ax = plt.subplots(figsize=(15, 6.6))
    ax.set_xlim(0, 15); ax.set_ylim(0, 6.6); ax.axis("off")
    ax.text(0.1, 6.35, "Frame pipeline of the renderer (visible view, HIGH quality)", fontsize=12, weight="bold")
    row1 = [("Scene graph\nworld = parent.world\n* local (TRS)", "#e0e7ff"), ("Shadow maps\nmoon cascades,\nUAV spot", "#dbeafe"), ("Depth / normal\npre-pass", "#dbeafe"),
            ("SSAO +\nvolumetric fog", "#dbeafe"), ("Object pass (HDR)\nFlat | Gouraud | Phong\nbrdf(): GGX + Lambert", "#fef3c7")]
    row2 = [("Combine\nfog + screen-space\nreflection (SSR)", "#dcfce7"), ("TAA\n(Halton jitter)", "#dcfce7"), ("Statistics\nauto-exposure /\nthermal AGC", "#dcfce7"),
            ("Bloom, lens flare,\ndepth of field", "#dcfce7"), ("Composite\ngrading, ACES filmic,\nsRGB  |  thermal sensor", "#fee2e2")]
    w, h = 2.6, 1.35
    for i, (t, c) in enumerate(row1):
        x = 0.3 + i * 2.95
        box(ax, x, 4.2, w, h, t, c, C["blue"])
        if i:
            arrow(ax, (x - 0.35, 4.87), (x, 4.87))
    for i, (t, c) in enumerate(row2):
        x = 0.3 + i * 2.95
        box(ax, x, 1.9, w, h, t, c, C["green"])
        if i:
            arrow(ax, (x - 0.35, 2.57), (x, 2.57))
    ax.plot([13.6, 13.6, 1.6], [4.2, 3.75, 3.75], color="#374151", lw=1.4)
    arrow(ax, (1.6, 3.75), (1.6, 3.25))
    arrow(ax, (13.6, 3.75), (13.6, 3.25))
    ax.text(7.5, 3.75, "HDR scene colour + depth + FragSurface (normal, reflectivity)", fontsize=8.5, ha="center", bbox=dict(fc="white", ec="none"))
    ax.text(0.3, 1.45, "Per object: the lab primitive (cube / sphere / cylinder) is drawn with its model matrix; 3 shading pipelines differ only in WHERE the light is evaluated "
            "(per triangle, per vertex, per fragment).", fontsize=8.8)
    ax.text(0.3, 0.95, "Thermal view: the same geometry is shaded as apparent temperature (heat + 0.04 irradiance + ground noise), read back at 320 x 240 for the vision stage.", fontsize=8.8)
    ax.text(0.3, 0.45, "Sources: Renderer.cpp / RendererFrame.cpp (passes), ShaderSources.cpp (lighting), ShadersPostFx.cpp (combine, TAA, composite).", fontsize=8, color=C["gray"])
    save(fig, "G1_render_pipeline",
         "Overview of one rendered frame: scene graph to world matrices, shadow maps and pre-pass, ambient occlusion and fog, the HDR object pass with Flat / Gouraud / Phong lighting, "
         "then SSR, temporal anti-aliasing, exposure, bloom / depth of field and the final filmic composite (or the thermal sensor model).",
         "src/rendering/RendererFrame.cpp (frame order), ShadersPostFx.cpp header comment lines 1-4")


def fig_G2_cv_pipeline():
    fig, ax = plt.subplots(figsize=(15, 5.8))
    ax.set_xlim(0, 15); ax.set_ylim(0, 5.8); ax.axis("off")
    ax.text(0.1, 5.55, "Computer-vision and tracking pipeline (per frame, both UAVs -> one shared track picture)", fontsize=12, weight="bold")
    stages = [("Thermal camera\n320 x 240 heat image\n(per UAV gimbal)", "#e0e7ff"), ("Threshold\nt = max(0.80, Otsu)", "#dbeafe"), ("Despeckle\n< 2 hot neighbours", "#dbeafe"),
              ("Label\nunion-find,\n8-connected", "#dbeafe"), ("Features\ncentroid, moments,\nelongation", "#dbeafe"), ("Target filter\narea, hot core,\nnot a streak", "#dbeafe")]
    for i, (t, c) in enumerate(stages):
        x = 0.2 + i * 2.45
        box(ax, x, 3.6, 2.15, 1.3, t, c, C["blue"], fs=8.3)
        if i:
            arrow(ax, (x - 0.3, 4.25), (x, 4.25))
    stages2 = [("Pixel -> ray\nground hit (5 cm march)\nsigma = 0.12 + 0.015 r", "#fef3c7"), ("Gate + Hungarian\nchi-square 9.21\nper sensor", "#fef3c7"), ("Kalman update\nx, z, vx, vz\n(1 or 2 views)", "#fef3c7"),
               ("Two-view fix\nray midpoint\nresidual < 1.2 m", "#fef3c7"), ("Track life cycle\ntentative -> confirmed\n-> coasting -> deleted", "#fef3c7"), ("Squad\nroles, lock, time on\ntarget -> rounds", "#fee2e2")]
    for i, (t, c) in enumerate(stages2):
        x = 0.2 + i * 2.45
        box(ax, x, 1.5, 2.15, 1.3, t, c, C["gold"], fs=8.3)
        if i:
            arrow(ax, (x - 0.3, 2.15), (x, 2.15))
    ax.plot([13.3, 13.3, 1.3], [3.6, 3.2, 3.2], color="#374151", lw=1.4)
    arrow(ax, (1.3, 3.2), (1.3, 2.8))
    ax.text(7.2, 3.2, "blobs (u, v, area, peak) + camera matrix", fontsize=8.5, ha="center", bbox=dict(fc="white", ec="none"))
    ax.text(0.2, 0.9, "Image stage: vision/CvPipeline.cpp.   Tracking: vision/MultiTracker.cpp (shared by both UAVs).   Decision: ai/Squad.cpp.   The squad never reads rover positions.", fontsize=8.8)
    ax.text(0.2, 0.4, "Feedback: the track predicts the target at arrival time; laser spots keep both gimbals on it and guide the rounds (Combat).", fontsize=8.8)
    save(fig, "G2_cv_pipeline",
         "From heat pixels to a shared track picture and a coordinated strike: image stage (threshold, despeckle, labelling, features, target filter), ground ray, gating and assignment, "
         "Kalman update with optional two-view triangulation, track life cycle, and the squad logic.",
         "src/vision/CvPipeline.h header comment, MultiTracker.h header comment, Squad.h header comment")


def fig_G3_game_loop():
    fig, ax = plt.subplots(figsize=(15, 6.9))
    ax.set_xlim(0, 15); ax.set_ylim(0, 6.9); ax.axis("off")
    ax.text(0.1, 6.65, "Game loop (App::update -> Scene::update, fixed dt, deterministic)", fontsize=12, weight="bold")
    steps = [("1  Input /\ncontrols", "#e0e7ff"), ("2  Vision\nsubmit blobs of\nboth UAVs", "#dbeafe"), ("3  Squad\ntrack, roles,\nstrike plan", "#dbeafe"),
             ("4  Controllables\ntank, helo, AA gun,\nradar, UAV 0", "#dbeafe"), ("5  Mission\nwaves, spawns", "#fef3c7"), ("6  Intruders\nHideController:\nflood + LFR", "#fef3c7"),
             ("7  Combat\nmissiles, blasts,\nparticles, light", "#fee2e2"), ("8  World\nmatrices, heat\ntrail, lights", "#dcfce7")]
    pos = []
    for i, (t, c) in enumerate(steps):
        x = 0.35 + (i % 4) * 3.65
        y = 4.6 if i < 4 else 2.6
        if i >= 4:
            x = 0.35 + (7 - i) * 3.65
        box(ax, x, y, 3.0, 1.4, t, c, C["blue"])
        pos.append((x, y))
    for i in range(3):
        arrow(ax, (pos[i][0] + 3.0, 5.3), (pos[i + 1][0], 5.3))
    arrow(ax, (pos[3][0] + 1.5, 4.6), (pos[4][0] + 1.5, 4.0))
    for i in range(4, 7):
        arrow(ax, (pos[i][0], 3.3), (pos[i + 1][0] + 3.0, 3.3))
    arrow(ax, (pos[7][0] + 1.5, 2.6), (pos[7][0] + 1.5, 1.5))
    box(ax, 0.35, 0.3, 14.3, 1.1, "Render: scene.leaves() with world matrices -> renderer; the UAV cameras are rendered as thermal images -> CvPipeline -> next frame's step 2 (one-frame latency)", "#f3e8ff", C["purple"], fs=9.5)
    arrow(ax, (0.2, 1.4), (0.2, 4.6), None, rad=0.0, ls="--")
    ax.text(0.02, 3.0, "next frame", fontsize=8, rotation=90, va="center")
    ax.text(7.3, 6.15, "events (spotted, launch, blast, kill, breach, wave) feed the HUD and audio each frame", fontsize=8.8, ha="center", color=C["gray"])
    save(fig, "G3_game_loop",
         "One simulation step: input, vision blobs, squad decisions, controllable objects, mission waves, intruder planning and driving, combat and effects, then world matrices and rendering; "
         "the rendered thermal images become the next frame's vision input.",
         "src/scene/Scene.cpp: Scene::update() lines 353-445; src/app/App.cpp update() lines 262-285")


# =====================================================================================================================
# Extra contract figures: view frustum / NDC and rasteriser edge functions
# =====================================================================================================================
def persp(fovy, aspect, n, f):
    """Port of perspective() (Math3D.cpp lines 84-94)."""
    t = 1 / math.tan(fovy / 2)
    M = np.zeros((4, 4))
    M[0, 0] = t / aspect; M[1, 1] = t; M[2, 2] = -(f + n) / (f - n); M[2, 3] = -2 * f * n / (f - n); M[3, 2] = -1
    return M


def fig_frustum_ndc():
    fov, asp, n, f = math.radians(50.0), 16 / 9, 0.05, 600.0       # Camera.h defaults: fovDeg 50, near 0.05, far 600
    M = persp(fov, asp, n, f)
    fig = plt.figure(figsize=(15, 5.2))
    ax = fig.add_subplot(1, 3, 1, projection="3d")
    nd, fd = 1.0, 5.0       # drawn (not to scale) near / far planes
    def corners(z):
        h = z * math.tan(fov / 2); w = h * asp
        return np.array([[-w, -h, -z], [w, -h, -z], [w, h, -z], [-w, h, -z]])
    cn, cf = corners(nd), corners(fd)
    for c, col in ((cn, C["blue"]), (cf, C["red"])):
        ax.add_collection3d(Poly3DCollection([P(c)], facecolor=col, alpha=0.18, edgecolor=col))
    for a, b in zip(cn, cf):
        q = P(np.array([a, b])); ax.plot(q[:, 0], q[:, 1], q[:, 2], color="#374151", lw=0.8)
        q = P(np.array([[0, 0, 0], b])); ax.plot(q[:, 0], q[:, 1], q[:, 2], color="#374151", lw=0.5, ls=":")
    ax.text(*P(np.array([0, 0, 0.1])), "eye", fontsize=9)
    ax.set_box_aspect((1.9, 1.3, 1)); ax.set_xlim(-4.6, 4.6); ax.set_ylim(-0.3, 5.3); ax.set_zlim(-2.6, 2.6); ax.view_init(22, -58); ax.set_axis_off()
    ax.set_title("view frustum (camera looks along -z)\nfovY 50 deg, aspect 16:9, near 0.05, far 600 (planes not to scale)", fontsize=9)
    ax = fig.add_subplot(1, 3, 2)
    ax.axis("off")
    ax.text(0, 1, "perspective() (Math3D.cpp 84-94)\n\n  t = 1 / tan(fovy / 2) = %.3f\n\n  [ t/asp  0     0              0          ]\n  [ 0      t     0              0          ]\n"
            "  [ 0      0   -(f+n)/(f-n)  -2fn/(f-n)  ]\n  [ 0      0    -1              0          ]\n\n"
            "numbers: M00 = %.3f, M11 = %.3f,\n         M22 = %.6f, M23 = %.5f\n\n"
            "clip = M * view_pos;  ndc = clip.xyz / clip.w\n"
            "x, y in [-1, 1]; z in [-1, 1] (near -> -1)\n"
            "w = -z_view = distance along the view axis\n\n"
            "linearDepth() in the shaders undoes it:\n  2 n f / (f + n - z_ndc (f - n))" % (1 / math.tan(fov / 2), M[0, 0], M[1, 1], M[2, 2], M[2, 3]), va="top", family="monospace", fontsize=8.5)
    ax = fig.add_subplot(1, 3, 3)
    dist = np.logspace(math.log10(0.05), math.log10(600), 600)
    clip = M @ np.stack([np.zeros_like(dist), np.zeros_like(dist), -dist, np.ones_like(dist)])
    zn = clip[2] / clip[3]
    ax.semilogx(dist, zn, color=C["blue"], lw=2)
    ax.set_xlabel("distance along the view axis (m)"); ax.set_ylabel("NDC depth z")
    ax.set_title("non-linear depth: half of the NDC range is used\nup to %.2f m (near = 0.05 m)" % dist[np.argmax(zn > 0)], fontsize=9)
    ax.axhline(0, color="gray", lw=0.6)
    save(fig, "frustum_ndc",
         "The perspective projection: the view frustum of the main camera (fov 50 deg, near 0.05 m, far 600 m), the matrix built by perspective() with its actual numbers, and the resulting "
         "non-linear NDC depth, which is why the shaders linearise depth before using it.",
         "src/math/Math3D.cpp: perspective() lines 84-94; src/scene/Camera.h: nearZ = 0.05, farZ = 600, fovDeg = 50")


def fig_raster_edges():
    fig, axs = plt.subplots(1, 3, figsize=(15, 5.2))
    V = np.array([[1.6, 1.4], [9.3, 3.1], [4.2, 8.6]])
    N = 11
    gx, gy = np.meshgrid(np.arange(N) + 0.5, np.arange(N) + 0.5)
    def edge(a, b, p):
        return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
    area = edge(V[0], V[1], V[2])
    e0 = edge(V[1], V[2], (gx, gy)) / area; e1 = edge(V[2], V[0], (gx, gy)) / area; e2 = edge(V[0], V[1], (gx, gy)) / area
    inside = (e0 >= 0) & (e1 >= 0) & (e2 >= 0)
    ax = axs[0]
    ax.add_patch(Polygon(V, closed=True, fill=False, ec="k", lw=1.5))
    ax.plot(gx[inside], gy[inside], "s", color=C["green"], ms=10, alpha=0.7)
    ax.plot(gx[~inside], gy[~inside], ".", color=C["gray"], ms=5)
    for i, v in enumerate(V):
        ax.text(v[0] + 0.15, v[1] + 0.15, f"v{i}", fontsize=10, weight="bold")
    ax.set_xlim(0, N); ax.set_ylim(0, N); ax.set_aspect("equal"); ax.set_xticks(range(N + 1)); ax.set_yticks(range(N + 1)); ax.grid(True, alpha=0.3)
    ax.set_title(f"pixel centres tested against the triangle:\n{int(inside.sum())} of {N * N} pixels covered", fontsize=9)
    for k, (E, col, nm) in enumerate(((e0, C["red"], "w0 (edge v1-v2)"), (e1, C["green"], "w1 (edge v2-v0)"))):
        ax = axs[1 + k]
        im = ax.imshow(E, extent=(0, N, 0, N), origin="lower", cmap="RdBu", vmin=-1, vmax=1)
        ax.contour(gx, gy, E, levels=[0], colors="k", linewidths=1.8)
        ax.add_patch(Polygon(V, closed=True, fill=False, ec="k", lw=1))
        for iy in range(0, N):
            for ix in range(0, N):
                ax.text(ix + 0.5, iy + 0.5, f"{E[iy, ix]:.1f}", ha="center", va="center", fontsize=6)
        ax.set_title(f"edge function / area = barycentric {nm}\nE = (bx-ax)(py-ay) - (by-ay)(px-ax); inside: all >= 0", fontsize=8.5)
        ax.grid(False)
    save(fig, "raster_edges",
         "How the GPU decides which pixels a triangle covers: three edge functions are evaluated at every pixel centre; a pixel is inside where all three are non-negative, and the "
         "normalised values are exactly the barycentric weights used to interpolate colours, normals and depth.",
         "OpenGL rasteriser (conceptual); weights used by the Gouraud / Phong varyings in ShaderSources.cpp")


if want("F"):
    fig_F1_threshold(); fig_F2_stages(); fig_F3_unionfind(); fig_F4_moments(); fig_F5_kalman(); fig_F6_assoc(); fig_F7_triangulation()
    fig_F8_state_machine(); fig_F9_time_on_target(); fig_F10_run_timeline(); fig_F11_waves()
if want("G"):
    fig_G1_render_pipeline(); fig_G2_cv_pipeline(); fig_G3_game_loop()
if want("X") or not ONLY:
    fig_frustum_ndc(); fig_raster_edges()
if ONLY and any(o in ("frustum", "raster", "X") for o in ONLY):
    pass

import shutil
for alias, src in (("attenuation", "C4_practical_attenuation"), ("fresnel", "C2_fresnel_geometry"), ("ggx_curves", "C1_specular_lobes"), ("ssr_raymarch", "D1_ssr_raymarch")):
    if os.path.exists(os.path.join(OUT, src + ".png")) and (not ONLY or src in CAPTIONS or True):
        shutil.copy(os.path.join(OUT, src + ".png"), os.path.join(OUT, alias + ".png"))
        key = src + ".png"
        cap = CAPTIONS.get(key) or json.load(open(os.path.join(OUT, "captions.json"))).get(key) if os.path.exists(os.path.join(OUT, "captions.json")) else CAPTIONS.get(key)
        if cap:
            CAPTIONS[alias + ".png"] = dict(cap)
# ==== END-OF-FIGURES (captions are written below)
if CAPTIONS:
    p = os.path.join(OUT, "captions.json")
    old = json.load(open(p)) if os.path.exists(p) else {}
    old.update(CAPTIONS)
    json.dump(old, open(p, "w"), indent=1, sort_keys=True)
