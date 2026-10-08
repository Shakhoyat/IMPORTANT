#!/usr/bin/env python3
"""v6 explanatory figures (day cycle, splines, gait / IK, random routes, sensor P_d) -> report/diagrams/H*.png.

Data: build/scene_export.json ("v6" key: day-cycle grid, gait cycle, routes, constants) and
      build/figdata_out/cv_noise.json (P_d sweep, WP-D figdata tool). Formulas that are not exported are ported 1:1
      from the C++ source (file and lines cited in each caption's source); constants are parsed from the headers.
Run : uv run --with matplotlib --with numpy python tools/make_figures_v6.py
Captions are merged into report/diagrams/captions.json (\\autofig{diagrams}{H..} in the report).
"""
import json
import math
import os
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Circle, Wedge

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "report", "diagrams")
C = dict(blue="#1f5fa8", orange="#d9730d", green="#2a8a4a", red="#c0392b", purple="#7b4fa3", gray="#6b7280",
         teal="#12818c", gold="#b8860b", ink="#111827")
plt.rcParams.update({"figure.dpi": 100, "savefig.dpi": 200, "font.size": 9, "axes.titlesize": 10, "axes.labelsize": 9,
                     "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True, "grid.alpha": 0.25,
                     "font.family": "DejaVu Sans", "legend.frameon": False})
CAPTIONS = {}
scene = json.load(open(os.path.join(ROOT, "build", "scene_export.json")))
V6 = scene["v6"]


def src(path):
    return open(os.path.join(ROOT, path), encoding="utf-8").read()


def const(path, name):
    m = re.search(name + r"\s*=\s*([-0-9.eE]+)f?", src(path))
    return float(m.group(1))


def save(fig, name, caption, source):
    fig.savefig(os.path.join(OUT, name + ".png"), dpi=200, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    CAPTIONS[name + ".png"] = {"caption": caption, "source": source}
    print("figure", name)


PHASE_COL = {"NIGHT": "#d9dde8", "DAWN": "#f6e3c8", "DAY": "#ffffff", "DUSK": "#f6d6c0"}


def phase_bands(ax, grid):
    h0 = grid[0]["hour"]
    for a, b in zip(grid, grid[1:] + [grid[-1]]):
        ax.axvspan(a["hour"], b["hour"] if b is not a else a["hour"] + 0.25, color=PHASE_COL[a["phase"]], lw=0, zorder=0)
    _ = h0


# --------------------------------------------------------------------------------------------------- H1 day cycle
def fig_H1():
    dc = V6["day_cycle"]
    g = dc["grid"]
    h = np.array([r["hour"] for r in g])
    fig, axs = plt.subplots(3, 1, figsize=(10, 7.6), sharex=True, gridspec_kw=dict(height_ratios=[1.25, 1, 0.22], hspace=0.32))
    ax = axs[0]
    phase_bands(ax, g)
    ax.plot(h, [r["elevation_deg"] for r in g], color=C["blue"], lw=2, label="sun elevation $e$")
    cfg = dc["config"]
    for y, lab, x, va in ((cfg["day_above_deg"], "DAY above %+g$^\\circ$" % cfg["day_above_deg"], 12, "bottom"),
                          (cfg["beams_below_deg"], "searchlights below %+g$^\\circ$" % cfg["beams_below_deg"], 2.5, "bottom"),
                          (cfg["night_below_deg"], "NIGHT (waves) below %+g$^\\circ$" % cfg["night_below_deg"], 21.5, "top")):
        ax.axhline(y, color=C["gray"], lw=0.8, ls="--")
        ax.text(x, y + (0.8 if va == "bottom" else -0.8), lab, ha="center", va=va, fontsize=7.5, color=C["gray"],
                bbox=dict(fc="white", ec="none", pad=0.5, alpha=0.8))
    for e in dc["events"]:
        ax.axvline(e["hour"], color=C["orange"], lw=0.8, ls=":")
        ax.text(e["hour"], 66, e["event"], rotation=90, va="top", ha="right", fontsize=7.5, color=C["orange"])
    ax.set_ylabel("elevation (deg)")
    ax.set_ylim(-75, 70)
    ax.set_title("Sun elevation over 24 h (lat %.1f$^\\circ$ N, decl %.1f$^\\circ$) and the phases NIGHT / DAWN / DAY / DUSK (shading)"
                 % (cfg["latitude_deg"], cfg["declination_deg"]), fontsize=9.5)
    ax = axs[1]
    phase_bands(ax, g)
    series = [("key_intensity", "key light $I$ (sun / moon)", C["orange"], "-", 1 / 3.2, "o"),
              ("daylight", "daylight weight", C["blue"], "-", 1, None),
              ("practical_scale", "practical lamps scale", C["purple"], "--", 1, None),
              ("cloud_shadow_mean", "mean cloud shadow factor", C["green"], ":", 1, None)]
    for k, lab, col, ls, sc, mk in series:
        ax.plot(h, [r[k] * sc for r in g], color=col, ls=ls, lw=1.8, label=lab + (" / 3.2" if sc != 1 else ""),
                marker=mk, markevery=8, ms=3.5)
    ax.plot(h, [(r["exposure_key_log2"] + 6) / 4 for r in g], color=C["red"], lw=1.6, ls="-.",
            label="exposure key $(\\log_2 k + 6) / 4$")
    ax.set_ylabel("normalised value")
    ax.set_ylim(-0.05, 1.12)
    ax.legend(ncol=3, fontsize=7.5, loc="lower center", bbox_to_anchor=(0.5, 0.99), borderaxespad=0.2)
    ax = axs[2]
    col = np.array([[min(1, c / max(r["key_color"])) for c in r["key_color"]] if r["key_is_sun"] else [0.25, 0.28, 0.4] for r in g])
    ax.imshow(col[None, :, :], aspect="auto", extent=(h[0], h[-1] + 0.25, 0, 1))
    ax.set_yticks([])
    ax.grid(False)
    ax.set_xlabel("clock hour")
    ax.set_xticks(range(0, 25, 2))
    ax.text(0.2, 0.5, "key colour (hue)", va="center", fontsize=7.5, color="white")
    save(fig, "H1_daycycle_curves",
         "Day cycle from the exported 24 h grid (%d rows): sun elevation with the phase thresholds and the four phase events, "
         "and the lighting quantities that follow it (key intensity, daylight weight, practical lamps, exposure key, mean cloud "
         "shadow); bottom: the key-light hue (sun reddening by air mass, moon blue at night)." % len(g),
         "build/scene_export.json v6.day_cycle.grid (DayCycle::applyTo, src/scene/DayCycle.cpp)")


# --------------------------------------------------------------------------------------------------- H2 Kasten-Young
def fig_H2():
    hdr = "src/scene/DayCycle.h"
    od = [float(x) for x in re.search(r"kSunOpticalDepth\[3\]\s*=\s*\{([^}]*)\}", src(hdr)).group(1).replace("f", "").split(",")]
    e = np.linspace(-1, 90, 600)
    m = 1.0 / (np.sin(np.radians(e)) + 0.50572 * (e + 6.07995) ** -1.6364)   # DayCycle.cpp airMass
    fig, axs = plt.subplots(1, 2, figsize=(10, 3.6))
    ax = axs[0]
    ax.semilogy(e, m, color=C["blue"], lw=2, label="Kasten--Young $m(e)$")
    ep = e[e > 2]
    ax.semilogy(ep, 1 / np.sin(np.radians(ep)), color=C["gray"], ls="--", lw=1.2, label="plane-parallel $1/\\sin e$")
    ax.set_xlabel("sun elevation $e$ (deg)")
    ax.set_ylabel("relative air mass")
    ax.set_xlim(-1, 90)
    ax.legend(fontsize=8)
    ax.annotate("m(0) = %.1f" % m[np.argmin(abs(e))], (0, m[np.argmin(abs(e))]), (12, 25), fontsize=8, arrowprops=dict(arrowstyle="->"))
    ax = axs[1]
    for k, (lab, col, ls) in enumerate((("R", C["red"], "-"), ("G", C["green"], "--"), ("B", C["blue"], ":"))):
        ax.plot(e, np.exp(-od[k] * (m - 1)), color=col, ls=ls, lw=2, label="%s: $\\tau$ = %.2f" % (lab, od[k]))
    ax.set_xlabel("sun elevation $e$ (deg)")
    ax.set_ylabel("transmittance $T_c = e^{-\\tau_c (m-1)}$")
    ax.set_xlim(-1, 90)
    ax.legend(fontsize=8, loc="lower right")
    strip = np.stack([np.exp(-od[k] * (m - 1)) for k in range(3)], -1)
    strip = strip / strip.max(axis=1, keepdims=True)
    ax.imshow(strip[None], extent=(-1, 90, -0.12, -0.04), aspect="auto")
    ax.set_ylim(-0.13, 1.03)
    save(fig, "H2_kasten_young",
         "Left: the Kasten--Young air mass against the plane-parallel 1/sin(e), which diverges at the horizon. Right: the "
         "per-channel sun transmittance relative to the zenith path and the normalised disc colour (strip): white at noon, "
         "orange-red near the horizon.",
         "src/scene/DayCycle.cpp airMass / sunTransmittance (ported), kSunOpticalDepth from DayCycle.h")


# --------------------------------------------------------------------------------------------------- H3 Catmull-Rom
def cr_curve(P, alpha, n=60):
    """Port of evalSegment (src/math/Spline.cpp): open path, mirrored phantom end points."""
    P = np.asarray(P, float)
    Q = np.vstack([2 * P[0] - P[1], P, 2 * P[-1] - P[-2]])
    pts, d1s, d2s = [], [], []
    for s in range(len(P) - 1):
        P0, P1, P2, P3 = Q[s], Q[s + 1], Q[s + 2], Q[s + 3]
        kn = lambda a, b: max(np.linalg.norm(b - a) ** alpha, 1e-6)
        t01, t12, t23 = kn(P0, P1), kn(P1, P2), kn(P2, P3)
        m1 = ((P1 - P0) / t01 - (P2 - P0) / (t01 + t12) + (P2 - P1) / t12) * t12
        m2 = ((P2 - P1) / t12 - (P3 - P1) / (t12 + t23) + (P3 - P2) / t23) * t12
        for u in np.linspace(0, 1, n, endpoint=(s == len(P) - 2)):
            u2, u3 = u * u, u ** 3
            pts.append(P1 * (2 * u3 - 3 * u2 + 1) + m1 * (u3 - 2 * u2 + u) + P2 * (-2 * u3 + 3 * u2) + m2 * (u3 - u2))
            d1s.append(P1 * (6 * u2 - 6 * u) + m1 * (3 * u2 - 4 * u + 1) + P2 * (-6 * u2 + 6 * u) + m2 * (3 * u2 - 2 * u))
            d2s.append(P1 * (12 * u - 6) + m1 * (6 * u - 4) + P2 * (-12 * u + 6) + m2 * (6 * u - 2))
    p, d1, d2 = map(np.array, (pts, d1s, d2s))
    kappa = np.abs(d1[:, 0] * d2[:, 1] - d1[:, 1] * d2[:, 0]) / np.maximum(np.linalg.norm(d1, axis=1) ** 3, 1e-12)
    return p, kappa


def fig_H3():
    P = [(0, 0), (1.0, 2.2), (1.4, 2.4), (4.5, 2.0), (5.0, 0.2), (8.0, 0.0), (8.3, 2.6)]
    vmax = 1.3
    kmin = const("src/scene/Garrison.cpp", "kMinWalkSpeed")
    fig, axs = plt.subplots(1, 2, figsize=(11, 3.9), gridspec_kw=dict(width_ratios=[1.25, 1]))
    ax = axs[0]
    for a, lab, col, ls in ((0.0, "uniform ($\\alpha$ = 0)", C["red"], "--"), (0.5, "centripetal ($\\alpha$ = 0.5, used)", C["blue"], "-"),
                            (1.0, "chordal ($\\alpha$ = 1)", C["green"], ":")):
        p, _ = cr_curve(P, a)
        ax.plot(p[:, 0], p[:, 1], color=col, ls=ls, lw=2 if a == 0.5 else 1.4, label=lab)
    Pa = np.array(P)
    ax.plot(Pa[:, 0], Pa[:, 1], "o", color=C["ink"], ms=5, label="control points")
    ax.plot(Pa[:, 0], Pa[:, 1], color=C["gray"], lw=0.6)
    ax.set_aspect("equal")
    ax.set_title("Knot spacing $t_{i+1} = t_i + |P_{i+1}-P_i|^{\\alpha}$", fontsize=9.5)
    ax.legend(fontsize=7.5, loc="upper center", ncol=2, bbox_to_anchor=(0.5, -0.08))
    ax = axs[1]
    p, kap = cr_curve(P, 0.5, 200)
    s = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(p, axis=0), axis=1))])
    ax.plot(s, kap, color=C["purple"], lw=1.4, label="curvature $\\kappa(s)$ (1/m)")
    v = np.maximum(kmin, vmax / (1 + 1.5 * kap))
    ax.plot(s, v, color=C["blue"], lw=2, ls="--", label="$v = \\max(%.2f,\\ v_{max}/(1+1.5\\kappa))$" % kmin)
    ax.axhline(vmax, color=C["gray"], lw=0.8, ls=":")
    ax.text(s[-1], vmax + 0.05, "$v_{max}$ = %.1f m/s" % vmax, ha="right", fontsize=7.5, color=C["gray"])
    ax.set_xlabel("arc length $s$ (m)")
    ax.set_ylim(0, max(4.0, 1.1 * kap.max()) if kap.max() < 8 else 8)
    ax.legend(fontsize=7.5, loc="upper left")
    ax.set_title("Speed law on the centripetal curve (before braking look-ahead)", fontsize=9.5)
    save(fig, "H3_catmull_rom",
         "Left: Catmull--Rom curves through the same unevenly spaced control points; the uniform parameterisation overshoots "
         "next to the short edges, the centripetal one stays tight without cusps. Right: curvature along the centripetal curve "
         "and the walking speed it allows (soldier walk speed 1.3 m/s).",
         "src/math/Spline.cpp evalSegment / curvatureAtDistance and src/scene/Garrison.cpp stepRoute (ported); control points illustrative")


# --------------------------------------------------------------------------------------------------- H4 gait + IK
def fig_H4():
    gt = V6["garrison"]["gait"]
    cyc = gt["cycle"]
    w = gt["walk"]
    ph = np.array([c["phase"] for c in cyc])
    fig = plt.figure(figsize=(11, 6.6))
    gs = fig.add_gridspec(2, 2, height_ratios=[1, 1.05], width_ratios=[1.3, 1], hspace=0.45)
    ax = fig.add_subplot(gs[0, 0])
    duty = w["duty"]
    ax.axvspan(0, duty, color="#e8eef8", lw=0, label="left stance (duty %.2f)" % duty)
    for k, lab, col, ls, mk in (("hip_l", "hip L", C["blue"], "-", "o"), ("knee_l", "knee L", C["red"], "--", "s"),
                                ("ankle_l", "ankle L", C["green"], ":", "^"), ("hip_r", "hip R", C["blue"], "-.", None),
                                ("shoulder_l", "shoulder L", C["purple"], "-", None)):
        ax.plot(ph, [c[k] for c in cyc], color=col, ls=ls, lw=1.6, marker=mk, ms=3, markevery=4, label=lab)
    ax.set_xlabel("gait phase (cycles)")
    ax.set_ylabel("angle (deg)")
    ax.set_title("Walk cycle joint angles (%.1f m/s, %d steps/min)" % (w["speed"], w["cadence_steps_per_min"]), fontsize=9.5)
    ax.legend(fontsize=7, ncol=3, loc="upper center", bbox_to_anchor=(0.5, -0.2))
    ax = fig.add_subplot(gs[0, 1])
    fx = np.array([c["foot_x_l"] for c in cyc])
    fy = np.array([c["foot_y_l"] for c in cyc])
    st = ph < duty
    ax.plot(fx[st], fy[st], "o-", color=C["blue"], ms=3, lw=1.2, label="stance (world-locked)")
    ax.plot(np.r_[fx[~st], fx[st][0]], np.r_[fy[~st], fy[st][0]], "s--", color=C["orange"], ms=3, lw=1.2, label="swing (cycloid + lift)")
    ax.set_xlabel("ankle x from hip (m, forward)")
    ax.set_ylabel("ankle y from hip (m)")
    ax.set_title("Ankle path in the pelvis frame", fontsize=9.5)
    ax.legend(fontsize=7.5)
    ax.set_aspect("equal", adjustable="datalim")
    # stick figures by forward kinematics of the exported angles
    ax = fig.add_subplot(gs[1, :])
    L1, L2 = w["thigh"], w["shin"]
    for i, c in enumerate(cyc[::4]):
        x0 = i * 0.55 + c["phase"] * w["stride"] * 0.0
        hy = w["hip_height"] + c["pelvis_y"]
        for side, col in (("l", C["blue"]), ("r", C["red"])):
            hp, kn = math.radians(c["hip_" + side]), math.radians(c["knee_" + side])
            kx, ky = x0 + L1 * math.sin(hp), hy - L1 * math.cos(hp)
            ax_, ay = kx + L2 * math.sin(hp - kn), ky - L2 * math.cos(hp - kn)
            ax.plot([x0, kx, ax_], [hy, ky, ay], "-o", color=col, ms=2.5, lw=1.6)
        ax.plot([x0, x0 + 0.5 * math.sin(math.radians(4))], [hy, hy + 0.55], color=C["ink"], lw=2)
        ax.text(x0, -0.12, "%.3g" % c["phase"], ha="center", fontsize=7)
    ax.axhline(0, color=C["gray"], lw=0.8)
    ax.set_aspect("equal")
    ax.set_yticks([0, 0.5, 1.0, 1.5])
    ax.set_xticks([])
    ax.set_title("Forward kinematics of the exported cycle every 1/8 (blue = left leg, red = right leg; numbers = phase)", fontsize=9.5)
    save(fig, "H4_gait_cycle",
         "The walk cycle as the program computes it: joint angles over one cycle (left stance shaded), the ankle path relative to the "
         "hip (flat world-locked stance, cycloid swing with lift) and stick figures rebuilt by forward kinematics from the same angles.",
         "build/scene_export.json v6.garrison.gait (gaitPose, footInPelvis, legIK in src/scene/Gait.cpp)")


def fig_H5():
    L1 = V6["garrison"]["gait"]["walk"]["thigh"]
    L2 = V6["garrison"]["gait"]["walk"]["shin"]
    tx, ty = 0.32, -0.74
    d = math.hypot(tx, ty)
    knee = math.pi - math.acos((L1 * L1 + L2 * L2 - d * d) / (2 * L1 * L2))
    A = math.acos((L1 * L1 + d * d - L2 * L2) / (2 * L1 * d))
    hip = math.atan2(tx, -ty) + A
    kx, ky = L1 * math.sin(hip), -L1 * math.cos(hip)
    fig, axs = plt.subplots(1, 2, figsize=(10, 4.2))
    ax = axs[0]
    ax.plot([0, kx, tx], [0, ky, ty], "-o", color=C["blue"], lw=2.5, ms=6)
    ax.plot([0, tx], [0, ty], "--", color=C["gray"], lw=1)
    ax.plot([0, 0], [0, -0.95], ":", color=C["gray"], lw=1)
    ax.text(0.02, 0.03, "hip", fontsize=8)
    ax.text(kx + 0.02, ky, "knee", fontsize=8)
    ax.text(tx + 0.02, ty - 0.03, "ankle target (x, y)", fontsize=8)
    ax.text(kx / 2 + 0.02, ky / 2 + 0.02, "$L_1$ = %.2f" % L1, fontsize=8, color=C["blue"])
    ax.text((kx + tx) / 2 + 0.02, (ky + ty) / 2, "$L_2$ = %.2f" % L2, fontsize=8, color=C["blue"])
    ax.text(tx / 2 - 0.12, ty / 2, "$d$", fontsize=9, color=C["gray"])
    ax.text(-0.22, -0.5, "$\\theta_h = \\mathrm{atan2}(x,-y) + A$", fontsize=8, color=C["red"])
    ax.text(kx + 0.08, ky + 0.02, "knee bend\n$\\pi - \\arccos\\frac{L_1^2+L_2^2-d^2}{2L_1L_2}$", fontsize=7.5, color=C["red"])
    ax.set_aspect("equal")
    ax.set_title("Two-link leg IK (hip %.1f$^\\circ$, knee %.1f$^\\circ$)" % (math.degrees(hip), math.degrees(knee)), fontsize=9.5)
    ax.set_xlim(-0.25, 0.75)
    ax.set_ylim(-1.0, 0.12)
    ax = axs[1]
    th = np.linspace(0, 2 * np.pi, 200)
    ax.fill(np.r_[(L1 + L2) * np.cos(th), abs(L1 - L2) * np.cos(th[::-1])], np.r_[(L1 + L2) * np.sin(th), abs(L1 - L2) * np.sin(th[::-1])],
            color="#dbeafe", label="reachable: $|L_1-L_2| \\leq d \\leq L_1+L_2$")
    xs, ys = np.meshgrid(np.linspace(-1, 1, 25), np.linspace(-1, 1, 25))
    err = []
    for x, y in zip(xs.ravel(), ys.ravel()):
        dd = math.hypot(x, y)
        if not (abs(L1 - L2) + 1e-4 < dd < (L1 + L2) * 0.9995):
            continue
        kn = math.pi - math.acos((L1 * L1 + L2 * L2 - dd * dd) / (2 * L1 * L2))
        hp = math.atan2(x, -y) + math.acos((L1 * L1 + dd * dd - L2 * L2) / (2 * L1 * dd))
        fx = L1 * math.sin(hp) + L2 * math.sin(hp - kn)
        fy = -L1 * math.cos(hp) - L2 * math.cos(hp - kn)
        err.append(math.hypot(fx - x, fy - y))
    ax.plot(0, 0, "ko", ms=4)
    ax.set_aspect("equal")
    ax.legend(fontsize=7.5, loc="upper center", bbox_to_anchor=(0.5, -0.06))
    ax.set_title("Workspace; FK(IK(p)) round trip on %d grid targets: max error %.1e m" % (len(err), max(err)), fontsize=9)
    save(fig, "H5_two_link_ik",
         "Two-link IK by the law of cosines: the knee angle from the triangle (hip, knee, ankle), the hip angle from the direction to "
         "the target plus the triangle's angle at the hip. The same solver (with a pole vector instead of the planar knee rule) poses "
         "the arms on the rifle.",
         "src/scene/Gait.cpp legIK / legFK (ported); thigh and shin lengths from v6.garrison.gait.walk")


# --------------------------------------------------------------------------------------------------- H6 spawn ring
def fig_H6():
    rr = {r["key"]: r["value"] for r in V6["random_routes"]}
    R = const("src/objects/BaseLayout.h", "kSpawnRadius")
    objs = [tuple(map(float, re.findall(r"[-0-9.]+", t)))[::2] for t in
            re.findall(r"\{(-?[0-9.]+f, 0\.0f, -?[0-9.]+f)\}", re.search(r"kObjectives\[5\][^;]*;", src("src/objects/BaseLayout.h")).group(0))]
    rng = np.random.default_rng(42)
    fig, axs = plt.subplots(1, 2, figsize=(11, 5.0))
    ax = axs[0]
    ax.add_patch(Circle((0, 0), R + rr["kSpawnJitter"], fc="#fdecd2", ec="none"))
    ax.add_patch(Circle((0, 0), R - rr["kSpawnJitter"], fc="white", ec="none"))
    gap = rr["kNorthGapDeg"]
    ax.add_patch(Wedge((0, 0), R + 2.5, 90 - gap, 90 + gap, fc="#f3d1d1", ec=C["red"], lw=0.8, alpha=0.8))
    ax.text(0, R + 3.0, "no spawns $\\pm$%g$^\\circ$ of the gate road (+Z)" % gap, ha="center", fontsize=7.5, color=C["red"])
    a = math.radians(205)
    ax.add_patch(Wedge((0, 0), rr["kViaMaxR"], math.degrees(a) - rr["kViaSectorDeg"], math.degrees(a) + rr["kViaSectorDeg"],
                       width=rr["kViaMaxR"] - rr["kViaMinR"], fc="#dbeafe", ec=C["blue"], lw=0.8))
    sx, sz = R * math.cos(a), R * math.sin(a)
    ax.plot(sx, sz, "o", color=C["orange"], ms=8, label="spawn on the ring r = %.1f $\\pm$ %.1f m" % (R, rr["kSpawnJitter"]))
    vias = []
    while len(vias) < 3:
        va = a + (2 * rng.random() - 1) * math.radians(rr["kViaSectorDeg"])
        vr = rr["kViaMinR"] + rng.random() * (rr["kViaMaxR"] - rr["kViaMinR"])
        vias.append((vr * math.cos(va), vr * math.sin(va)))
    vias.sort(key=lambda p: -(p[0] ** 2 + p[1] ** 2))
    ob = objs[1]
    chain = [(sx, sz)] + vias + [ob]
    ax.plot([p[0] for p in chain], [p[1] for p in chain], "--", color=C["ink"], lw=1)
    ax.plot([p[0] for p in vias], [p[1] for p in vias], "s", color=C["blue"], ms=6,
            label="via points (max %d, sector $\\pm$%g$^\\circ$, r %g--%g m, outermost first)" % (rr["kMaxVia"], rr["kViaSectorDeg"], rr["kViaMinR"], rr["kViaMaxR"]))
    ax.plot([o[0] for o in objs], [o[1] for o in objs], "*", color=C["red"], ms=11, label="objectives (camp assets)")
    ax.set_xlim(-22, 22)
    ax.set_ylim(-22, 24)
    ax.set_aspect("equal")
    ax.set_xlabel("x (m, east)")
    ax.set_ylabel("z (m, north)")
    ax.legend(fontsize=7, loc="upper center", bbox_to_anchor=(0.5, -0.12))
    ax.set_title("Geometry of one random route (schematic draw)", fontsize=9.5)
    ax = axs[1]
    bear = []
    while len(bear) < 20000:
        b = rng.random() * 2 * math.pi
        if abs(math.atan2(math.cos(b), math.sin(b))) > math.radians(gap):
            bear.append(b)
    deg = (90 - np.degrees(bear)) % 360   # compass bearing from +Z towards +X
    ax.hist(deg, bins=36, range=(0, 360), color=C["blue"], edgecolor="white")
    ax.set_xlabel("spawn bearing from north (deg)")
    ax.set_ylabel("count (20 000 draws)")
    ax.set_xticks(range(0, 361, 45))
    ax.set_title("Uniform bearing with rejection of the north gap", fontsize=9.5)
    save(fig, "H6_spawn_ring",
         "Random intruder routes: a spawn bearing drawn uniformly on the ring outside the gate-road gap, up to three via points in a "
         "sector around the spawn bearing (driven outermost first, so the chain closes in on the camp), then the objective. Right: "
         "the rejection sampler gives a flat bearing histogram with an empty gap at north.",
         "src/scene/Mission.cpp randomRoute (ported, Python RNG); constants from v6.random_routes and BaseLayout.h")


# --------------------------------------------------------------------------------------------------- H7 P_d vs NETD
def fig_H7():
    cv = json.load(open(os.path.join(ROOT, "build", "figdata_out", "cv_noise.json")))
    n = np.array(cv["netd"], float)
    fig = plt.figure(figsize=(11, 6.4))
    gs = fig.add_gridspec(2, 5, height_ratios=[1.25, 1], wspace=0.55)
    ax = fig.add_subplot(gs[0, :3])
    for k, lab, col, ls, mk in (("pd", "$P_d$ rover", C["blue"], "-", "o"), ("pdFar", "$P_d$ rover at half size", C["teal"], "--", "s"),
                                ("personRecognised", "person recognised", C["green"], "-.", "^")):
        ax.plot(n, cv[k], color=col, ls=ls, marker=mk, lw=1.8, ms=5, label=lab)
    nn = cv["noNuc"]
    ax.plot([nn["netd"]], [nn["pdFar"]], "x", color=C["red"], ms=9, mew=2, label="no NUC (NETD %g): $P_d$ far" % nn["netd"])
    ax.axvline(cv["config"]["netdDn"], color=C["gray"], ls=":", lw=1)
    ax.text(cv["config"]["netdDn"] + 0.1, 0.99, "default NETD", fontsize=7.5, color=C["gray"])
    ax.set_ylim(0.9, 1.005)
    ax.set_xlabel("NETD (DN, 8-bit)")
    ax.set_ylabel("probability per frame")
    ax.legend(fontsize=7.5, loc="lower left")
    ax.set_title("Detection (%d frames per point, after NUC)" % cv["frames"], fontsize=9.5)
    ax = fig.add_subplot(gs[0, 3:])
    ax.plot(n, cv["falsePerFrame"], "o-", color=C["red"], lw=1.8, label="false targets / frame")
    ax.plot(n, cv["personAsTarget"], "s--", color=C["orange"], lw=1.8, label="person classed as target")
    ax.plot([nn["netd"]], [nn["falsePerFrame"]], "x", color=C["red"], ms=9, mew=2, label="no NUC")
    ax.set_xlabel("NETD (DN)")
    ax.set_ylabel("rate per frame")
    ax.legend(fontsize=7.5, loc="upper left")
    ax.set_title("False alarms", fontsize=9.5)
    W, H = cv["cropW"], cv["cropH"]
    cmap = plt.get_cmap("inferno")
    for i, ex in enumerate(cv["examples"]):
        ax = fig.add_subplot(gs[1, i])
        img = np.array(ex["crop"], float).reshape(H, W)
        ax.imshow(img, cmap=cmap, vmin=0, vmax=255)
        for b in ex["blobs"]:
            col = {"target": "#22d3ee", "person": "#86efac"}.get(b["cls"], "white")
            r = math.sqrt(b["area"]) * 0.9 + 3
            ax.add_patch(plt.Rectangle((b["cx"] - cv["cropX"] - r, b["cy"] - cv["cropY"] - r), 2 * r, 2 * r, fill=False, ec=col, lw=1.2))
            ax.text(b["cx"] - cv["cropX"] - r, b["cy"] - cv["cropY"] - r - 2, "%s %d px" % (b["cls"], b["area"]), color=col, fontsize=6)
        ax.set_title("NETD %g DN" % ex["netd"], fontsize=8.5)
        ax.axis("off")
    save(fig, "H7_pd_netd",
         "Real-CV statistics against the temporal noise level (NETD), full sensor model (PSF, fixed pattern after NUC, stuck pixels): "
         "detection probability of a rover (full size and half size), person recognition, false targets and persons misread as "
         "targets per frame; crosses: the same at the default NETD without NUC. Bottom: the synthetic test crop at each NETD with the "
         "classified blobs (target = cyan, person = green).",
         "build/figdata_out/cv_noise.json (tools/figdata_main.cpp dumpCvNoise; SensorModel + cv::detectAll)")


for f in (fig_H1, fig_H2, fig_H3, fig_H4, fig_H5, fig_H6, fig_H7):
    f()
p = os.path.join(OUT, "captions.json")
old = json.load(open(p)) if os.path.exists(p) else {}
old.update(CAPTIONS)
json.dump(old, open(p, "w"), indent=1, sort_keys=True)
