"""v10 report plots (vector PDF, one style). Every curve is computed with the formula the C++ code uses; the source is
named next to each function. Run: ~/.local/bin/uv run -q --with matplotlib --with numpy --with pillow python tools/v10/plots.py
Outputs: report/figures/plots/*.pdf (+ data-driven plots when their inputs exist: thermal histogram, benchmark)."""
import csv
import json
import math
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "report", "figures", "plots")
os.makedirs(OUT, exist_ok=True)

plt.rcParams.update({
    "font.family": "serif", "font.serif": ["Times New Roman", "Times", "Nimbus Roman", "DejaVu Serif"],
    "mathtext.fontset": "stix", "font.size": 9, "axes.titlesize": 9, "axes.labelsize": 9, "legend.fontsize": 8,
    "xtick.labelsize": 8, "ytick.labelsize": 8, "axes.grid": True, "grid.alpha": 0.3, "lines.linewidth": 1.4,
    "figure.dpi": 150, "savefig.bbox": "tight", "figure.constrained_layout.use": True, "axes.spines.top": False, "axes.spines.right": False,
})
C = ["#1f4e79", "#c0504d", "#4f8a3a", "#d08c1a", "#7a4f9a", "#3a8a8a"]


def save(fig, name):
    fig.savefig(os.path.join(OUT, name + ".pdf"))
    plt.close(fig)
    print("wrote", name)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


# ---- scene/DayCycle.{h,cpp}: solar model, key light, transmittance ------------------------------------------------
LAT, DECL = 22.8, -4.6
SUN_I, SUN_TAU = 3.2, np.array([0.06, 0.11, 0.21])
MOON_I = 0.95  # rendering/Lighting.h DirectionalLight (moon) intensity, export "lights.moon"


def sun_dir(hour):
    H = np.radians(15.0 * (hour - 12.0))
    phi, d = math.radians(LAT), math.radians(DECL)
    up = math.sin(phi) * math.sin(d) + math.cos(phi) * math.cos(d) * np.cos(H)
    east = -math.cos(d) * np.sin(H)
    north = math.cos(phi) * math.sin(d) - math.sin(phi) * math.cos(d) * np.cos(H)
    n = np.sqrt(up ** 2 + east ** 2 + north ** 2)
    return east / n, up / n, north / n


def elevation(hour):
    return np.degrees(np.arcsin(np.clip(sun_dir(hour)[1], -1, 1)))


def air_mass(e):
    e = np.maximum(e, -1.0)
    return 1.0 / (np.sin(np.radians(e)) + 0.50572 * np.power(e + 6.07995, -1.6364))


def transmittance(e):
    m = air_mass(e) - 1.0
    return np.exp(-np.outer(m, SUN_TAU))


def _srgb(c):
    c = np.clip(np.asarray(c, dtype=float), 0, 1)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def plot_day():
    h = np.linspace(0, 24, 2401)
    e = elevation(h)
    sun_key = SUN_I * smoothstep(-1.0, 10.0, e)
    moon_key = MOON_I * (1.0 - smoothstep(-6.0, -1.0, e))
    fig = plt.figure(figsize=(7.0, 3.6))
    gs = fig.add_gridspec(2, 2, height_ratios=[3.2, 1.0])
    a = fig.add_subplot(gs[0, 0])
    a.plot(h, e, color=C[0], label="sun elevation $e$")
    a.axhline(4, color="0.5", ls=":", lw=0.8)
    a.axhline(-6, color="0.5", ls=":", lw=0.8)
    a.fill_between(h, -90, 90, where=e < -6, color="#1f2a44", alpha=0.12, lw=0, label="NIGHT ($e<-6^\\circ$)")
    a.fill_between(h, -90, 90, where=(e >= -6) & (e <= 4), color=C[3], alpha=0.22, lw=0, label="DAWN / DUSK")
    a.set_xlim(0, 24)
    a.set_ylim(-75, 75)
    a.set_xticks(range(0, 25, 3))
    a.set_ylabel("degrees")
    a.legend(loc="lower center", frameon=True, framealpha=0.9, edgecolor="none", fontsize=7)
    a.set_title("(a) sun elevation")
    sr, ss = h[np.argmax(e > 0)], h[len(h) - 1 - np.argmax(e[::-1] > 0)]
    a.annotate(f"sunrise {int(sr)}:{int(round((sr % 1) * 60)):02d}", (sr, 0), (sr - 5.8, 32), fontsize=8,
               arrowprops=dict(arrowstyle="->", lw=0.6))
    a.annotate(f"sunset {int(ss)}:{int(round((ss % 1) * 60)):02d}", (ss, 0), (ss + 0.4, 45), fontsize=8,
               arrowprops=dict(arrowstyle="->", lw=0.6))
    b = fig.add_subplot(gs[0, 1])
    tr = transmittance(e)
    for i, (nm, col) in enumerate(zip("RGB", ["#c0392b", "#27ae60", "#2e60c0"])):
        b.plot(h, sun_key * tr[:, i], color=col, label=f"sun {nm}: $3.2\\,s(e)\\,T_{nm}(e)$")
    b.plot(h, moon_key, color="0.25", ls="--", label="moon (below $-1^\\circ$)")
    b.set_xlim(0, 24)
    b.set_xticks(range(0, 25, 3))
    b.set_ylabel("key-light intensity")
    b.legend(frameon=True, framealpha=0.9, edgecolor="none", loc="center", fontsize=7)
    b.set_title("(b) key light per channel")
    # (c) the colours the renderer uses, read from the export (DayCycle grid, 15 min steps), shown sRGB-encoded
    exp = os.path.join(ROOT, "build", "f_export.json")
    if os.path.exists(exp):
        grid = json.load(open(exp))["v6"]["day_cycle"]["grid"]
        hrs = np.array([g["hour"] for g in grid], dtype=float)
        rows = [("zenith", [g["sky_zenith"] for g in grid]), ("horizon", [g["fog_color"] for g in grid]),
                ("key light", [np.array(g["key_color"]) * min(g["key_intensity"], 1.0) for g in grid])]
        img = np.stack([_srgb(np.array(r)) for _, r in rows])
        c = fig.add_subplot(gs[1, :])
        c.imshow(img, aspect="auto", interpolation="bilinear", extent=(hrs[0], hrs[-1], len(rows), 0))
        c.set_yticks(np.arange(len(rows)) + 0.5)
        c.set_yticklabels([n for n, _ in rows], fontsize=7)
        c.set_xticks(range(0, 25, 3))
        c.set_xlim(0, 24)
        c.grid(False)
        for s in c.spines.values():
            s.set_visible(False)
        c.set_xlabel("in-game hour")
        c.set_title("(c) sky and key-light colour per hour, from the export", fontsize=8)
    else:
        a.set_xlabel("in-game hour")
        b.set_xlabel("in-game hour")
    save(fig, "p_day_cycle")

    # time scale: hours per real second (DayCycle::hoursPerSecond), the real length of one day
    rate = np.where(e > 0, 1 / 10.0, 1 / 13.3)
    rate = np.where(np.abs(e) < 6, rate / 1.5, rate)
    secs = np.sum((24.0 / len(h)) / rate)
    return secs


def plot_depth():
    # math/Math3D.cpp perspective(): z_ndc = (f+n)/(f-n) + 2fn/((f-n) z_eye), z_eye < 0; window depth = (z_ndc+1)/2
    f = 600.0
    d = np.logspace(np.log10(0.05), np.log10(600), 600)
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.6))
    for n, col in [(0.05, C[0]), (0.5, C[1])]:
        dd = d[d >= n]
        zndc = (f + n) / (f - n) - 2 * f * n / ((f - n) * dd)
        ax[0].semilogx(dd, (zndc + 1) / 2, color=col, label=f"near = {n} m")
        # depth step per 24-bit code: dz_eye = (1/2^24) / (d window / d z_eye)
        dw = f * n / ((f - n) * dd ** 2)
        ax[1].loglog(dd, (1 / 2 ** 24) / dw, color=col, label=f"near = {n} m")
    ax[0].set_xlabel("distance from the eye (m)")
    ax[0].set_ylabel("window depth $z_w \\in [0,1]$")
    ax[0].set_title("(a) window depth $z_w$")
    ax[0].legend(frameon=False, loc="lower right")
    ax[1].axhline(0.01, color="0.5", ls=":", lw=0.8)
    ax[1].text(0.06, 0.012, "1 cm", fontsize=8, color="0.4")
    ax[1].set_xlabel("distance from the eye (m)")
    ax[1].set_ylabel("depth step (m)")
    ax[1].set_title("(b) smallest depth step")
    ax[1].legend(frameon=False, loc="lower right")
    save(fig, "p_depth_precision")


def plot_specular():
    th = np.linspace(0, 90, 400)
    fig, ax = plt.subplots(figsize=(3.4, 2.3))
    for n, col in zip([2, 4, 16, 21.1, 64], C):
        ax.plot(th, np.cos(np.radians(th)) ** n, color=col, label=f"$n$ = {n:g}")
    ax.set_xlabel("angle between $N$ and $H$ (deg)")
    ax.set_ylabel("$(N\\cdot H)^n$")
    ax.set_xlim(0, 90)
    ax.legend(frameon=False)
    ax.set_title("Blinn-Phong lobe")
    save(fig, "p_specular_falloff")


def plot_gouraud_phong():
    # one edge of a coarse triangle (vertex A at x=0, B at x=1); the light's highlight peak falls between the vertices
    x = np.linspace(0, 1, 400)
    nA, nB = np.radians(-28), np.radians(28)          # vertex normals tilted apart (a curved surface)
    Lang, n = 0.0, 32                                   # light / half vector straight up, shininess 32
    kd, ks, ka = 0.6, 0.6, 0.08

    def shade(theta):
        nd = np.cos(theta - Lang)
        return ka + kd * np.maximum(nd, 0) + ks * np.maximum(nd, 0) ** n

    gour = (1 - x) * shade(nA) + x * shade(nB)          # intensities at the vertices, interpolated
    ph = shade((1 - x) * nA + x * nB)                   # normals interpolated, lit per fragment
    fig, ax = plt.subplots(figsize=(3.4, 2.3))
    ax.plot(x, ph, color=C[1], label="Phong (normal interpolated)")
    ax.plot(x, gour, color=C[0], ls="--", label="Gouraud (intensity interpolated)")
    ax.fill_between(x, gour, ph, color=C[1], alpha=0.12, lw=0)
    ax.set_xlabel("position along the edge A $\\rightarrow$ B")
    ax.set_ylabel("intensity")
    ax.legend(frameon=False, loc="upper center", bbox_to_anchor=(0.5, -0.3), ncol=1)
    ax.set_title("Gouraud misses a highlight between vertices")
    save(fig, "p_gouraud_vs_phong")


def plot_gaussian(sigma=1.0, size=5):
    r = size // 2
    xs = np.arange(-r, r + 1)
    X, Y = np.meshgrid(xs, xs)
    K = np.exp(-(X ** 2 + Y ** 2) / (2 * sigma ** 2))
    K /= K.sum()
    fig = plt.figure(figsize=(3.4, 2.6))
    ax = fig.add_subplot(111, projection="3d")
    ax.bar3d(X.ravel() - 0.4, Y.ravel() - 0.4, np.zeros(K.size), 0.8, 0.8, K.ravel(), color=C[0], alpha=0.85, shade=True)
    ax.set_title(f"{size}x{size} Gaussian, $\\sigma$ = {sigma:g}")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_zlabel("weight")
    save(fig, "p_gaussian_kernel")
    # cost: 2D k*k vs separable 2k multiply-adds per pixel
    k = np.arange(3, 32, 2)
    fig, ax = plt.subplots(figsize=(3.4, 2.2))
    ax.plot(k, k * k, color=C[1], marker="o", ms=3, label="2D kernel: $k^2$")
    ax.plot(k, 2 * k, color=C[0], marker="s", ms=3, label="separable: $2k$")
    ax.set_xlabel("kernel width $k$")
    ax.set_ylabel("multiply-adds per pixel")
    ax.legend(frameon=False)
    ax.set_title("Cost of a $k\\times k$ blur")
    save(fig, "p_separable_cost")


def _sphere_mesh(s):
    """primitives/Sphere.cpp createSphere(s, s/2): pole fans and quad bands, r = 0.5."""
    k = s // 2
    th = np.pi * np.arange(k + 1) / k
    ph = 2 * np.pi * np.arange(s) / s
    P = lambda i, j: np.array([np.sin(th[i]) * np.cos(ph[j % s]), np.cos(th[i]), np.sin(th[i]) * np.sin(ph[j % s])])
    tris = []
    for i in range(k):
        for j in range(s):
            if i == 0:
                tris.append((P(0, 0), P(1, j + 1), P(1, j)))
            elif i == k - 1:
                tris.append((P(i, j), P(i, j + 1), P(k, 0)))
            else:
                tris += [(P(i, j), P(i, j + 1), P(i + 1, j)), (P(i, j + 1), P(i + 1, j + 1), P(i + 1, j))]
    return np.array(tris)


def _draw_sphere(ax, s, rgb):
    from matplotlib.collections import PolyCollection
    tris = _sphere_mesh(s)
    a, el = np.radians(30), np.radians(20)
    R1 = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
    R2 = np.array([[1, 0, 0], [0, np.cos(el), -np.sin(el)], [0, np.sin(el), np.cos(el)]])
    v = tris @ (R2 @ R1).T
    n = np.cross(v[:, 1] - v[:, 0], v[:, 2] - v[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    if np.mean(n[:, 2]) < 0:
        n = -n
    front = n[:, 2] > 0
    L = np.array([-0.45, 0.6, 0.66]); L /= np.linalg.norm(L)
    lam = 0.25 + 0.75 * np.clip(n @ L, 0, 1)
    order = np.argsort(v[:, :, 2].mean(1))
    order = [i for i in order if front[i]]
    cols = [tuple(np.clip(np.array(rgb) * lam[i], 0, 1)) for i in order]
    ax.add_collection(PolyCollection([v[i, :, :2] for i in order], facecolors=cols, edgecolors="#3a2a10", linewidths=0.3))
    ax.add_patch(plt.Circle((0, 0), 1.0, fill=False, ls="--", lw=0.6, color=C[1]))
    ax.set_xlim(-1.08, 1.08); ax.set_ylim(-1.08, 1.08); ax.set_aspect("equal"); ax.axis("off")


def plot_tessellation():
    # primitives/Sphere.cpp createSphere(slices, stacks): triangles = 2 slices (stacks - 1); stacks = slices / 2 here.
    # Largest gap between the unit sphere and its polygon: the chord sagitta r (1 - cos(pi / slices)).
    s = np.arange(6, 97, 2)
    tri = 2 * s * (s // 2 - 1)
    err = 1 - np.cos(np.pi / s)
    fig = plt.figure(figsize=(7.0, 2.5))
    gs = fig.add_gridspec(1, 5, width_ratios=[2.4, 0.08, 1, 1, 1])
    ax = fig.add_subplot(gs[0, 0])
    ax.loglog(tri, err * 100, color=C[0], marker="o", ms=2.5, label="exact")
    ax.loglog(tri, (np.pi ** 2 / (2 * s ** 2)) * 100, color="0.55", ls="--", lw=0.9, label="$\\pi^2/2s^2$")
    for sl in (8, 16, 36, 72):
        t = 2 * sl * (sl // 2 - 1)
        e = (1 - np.cos(np.pi / sl)) * 100
        ax.plot(t, e, "o", ms=5, mfc=C[3] if sl == 36 else "white", mec=C[0], zorder=4)
        ax.annotate(f"{sl}" + (" (scene)" if sl == 36 else ""), (t, e), (t * 1.25, e * 1.25), fontsize=8)
    ax.set_xlabel("triangles per sphere")
    ax.set_ylabel("silhouette error (% of $r$)")
    ax.legend(frameon=False, loc="lower left", fontsize=7)
    ax.set_title("(a) error against cost")
    rgb = (255 / 255, 160 / 255, 50 / 255)  # PrimitiveColors.h: Sphere
    for i, sl in enumerate((8, 16, 36)):
        b = fig.add_subplot(gs[0, 2 + i])
        _draw_sphere(b, sl, rgb)
        t = 2 * sl * (sl // 2 - 1)
        b.set_title(f"({'bcd'[i]}) {sl} slices\n{t} tri, $\\varepsilon$ = {(1 - np.cos(np.pi / sl)) * 100:.2f}%", fontsize=8)
    save(fig, "p_tessellation")


def plot_histogram(png):
    """Histogram + CDF of a real thermal frame (lab CV figure, 8-bit grey) and of its histogram-equalised version."""
    from PIL import Image
    img = np.asarray(Image.open(png).convert("L"), dtype=np.uint8)
    hist = np.bincount(img.ravel(), minlength=256)
    cdf = np.cumsum(hist) / img.size
    lut = np.round(255 * (cdf - cdf[hist > 0][0]) / max(1 - cdf[hist > 0][0], 1e-9)).clip(0, 255).astype(np.uint8)
    eq = lut[img]
    h2 = np.bincount(eq.ravel(), minlength=256)
    c2 = np.cumsum(h2) / eq.size
    Image.fromarray(eq).save(os.path.join(OUT, "thermal_equalised.png"))
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.2))
    for a, hh, cc, t, loc in [(ax[0], hist, cdf, "(a) raw sensor frame", "center right"),
                              (ax[1], h2, c2, "(b) after equalisation", "upper left")]:
        a.bar(np.arange(256), hh / hh.max(), width=1.0, color=C[0], lw=0)
        a.plot(np.arange(256), cc, color=C[1], label="CDF")
        a.set_xlim(0, 255)
        a.set_xlabel("grey level")
        a.set_title(t)
        a.legend(frameon=False, loc=loc)
    ax[0].set_ylabel("count (normalised) / CDF")
    save(fig, "p_thermal_histogram")


def plot_palettes():
    # rendering/ShaderSources.cpp thermalPalette: 7 stops, mix() between neighbours (6 linear segments); white-hot t^0.9
    iron = [(0.00, 0.00, 0.03), (0.12, 0.02, 0.33), (0.46, 0.03, 0.55), (0.80, 0.10, 0.34), (0.98, 0.42, 0.06),
            (1.00, 0.78, 0.16), (1.00, 1.00, 0.88)]
    rain = [(0.00, 0.00, 0.05), (0.05, 0.10, 0.60), (0.00, 0.70, 0.90), (0.10, 0.85, 0.20), (1.00, 0.95, 0.10),
            (1.00, 0.20, 0.05), (1.0, 1.0, 1.0)]
    t = np.linspace(0, 1, 600)

    def stops(c):
        c = np.array(c)
        u = t * 6
        k = np.minimum(u.astype(int), 5)
        f = (u - k)[:, None]
        return c[k] * (1 - f) + c[k + 1] * f
    white = np.repeat(np.power(t, 0.9)[:, None], 3, axis=1)
    fig, ax = plt.subplots(3, 1, figsize=(7.0, 1.35), sharex=True)
    for a, (name, rgb, st) in zip(ax, [("ironbow", stops(iron), iron), ("white-hot", white, None), ("rainbow", stops(rain), rain)]):
        enc = np.where(rgb <= 0.0031308, 12.92 * rgb, 1.055 * np.power(np.clip(rgb, 0, 1), 1 / 2.4) - 0.055)  # sRGB for display
        a.imshow(enc[None, :, :], aspect="auto", extent=(0, 1, 0, 1))
        if st:
            for k in range(7):
                a.axvline(k / 6, color="black", lw=0.6, ymin=0, ymax=0.25)
        a.set_yticks([])
        a.set_ylabel(name, rotation=0, ha="right", va="center", fontsize=8)
        a.grid(False)
        for sp in a.spines.values():
            sp.set_visible(False)
    ax[-1].set_xlabel("normalised heat $t$ (ticks: the 7 colour stops; between them $\\mathrm{mix}(c_k, c_{k+1}, 6t-k)$)")
    ax[-1].set_xticks(np.linspace(0, 1, 7))
    ax[-1].set_xticklabels(["0", "1/6", "2/6", "3/6", "4/6", "5/6", "1"])
    save(fig, "p_palettes")


def plot_bench(csv_path):
    rows = list(csv.DictReader(open(csv_path)))
    if not rows:
        return
    lab = lambda c: c.replace("v1 look", "classic look")
    def group(c):
        if c.split()[0] in ("LOW", "HIGH", "ULTRA") or c == "EPIC day 10:30":
            return 0
        if "off" in c or "non-indexed" in c or "look" in c:
            return 2
        return 1
    names = [lab(r["config"]) for r in rows]
    g = [group(r["config"]) for r in rows]
    rank = {"LOW": 0, "HIGH": 1, "EPIC": 2, "ULTRA": 3}
    order = sorted(range(len(rows)), key=lambda i: (g[i], rank.get(rows[i]["config"].split()[0], 0) if g[i] == 0
                                                    else float(rows[i]["avg_ms"])))
    ms = np.array([float(rows[i]["avg_ms"]) for i in order])
    p95 = np.array([float(rows[i]["p95_ms"]) for i in order])
    fps = np.array([float(rows[i]["avg_fps"]) for i in order])
    dc = np.array([float(rows[i]["draw_calls"]) for i in order])
    tri = np.array([float(rows[i]["triangles"]) for i in order]) / 1e6
    gcol = [C[0], C[5], C[1]]
    gname = ["quality presets", "views (day/night, thermal)", "ablations"]
    col = [gcol[g[i]] for i in order]
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 0.24 * len(rows) + 1.0), gridspec_kw={"width_ratios": [2.3, 1]},
                           sharey=True)
    y = np.arange(len(rows))
    a = ax[0]
    a.axvspan(0, 1000 / 60, color=C[2], alpha=0.08, lw=0)
    a.barh(y, ms, color=col, height=0.7)
    a.scatter(p95, y, color="0.15", marker="|", s=70, zorder=3, label="95th percentile")
    for yi, m, p, f in zip(y, ms, p95, fps):
        a.text(max(m, p) + 0.8, yi, f"{f:.0f} fps", va="center", fontsize=7, color="0.2")
    a.axvline(1000 / 60, color=C[2], ls="--", lw=0.8)
    a.text(1000 / 60, -0.65, " 60 fps", fontsize=7, color=C[2], va="bottom")
    a.set_yticks(y)
    a.set_yticklabels([names[i] for i in order], fontsize=7.5)
    a.set_ylim(len(rows) - 0.5, -1.1)
    a.set_xlim(0, max(p95) * 1.18)
    a.set_xlabel("ms per frame (mean bar, 95th percentile tick)")
    a.set_title("(a) frame time, 1920$\\times$1080, RTX 4070")
    from matplotlib.patches import Patch
    a.legend(handles=[Patch(color=c, label=n) for c, n in zip(gcol, gname)], frameon=True, framealpha=0.9,
             edgecolor="none", loc="center right", fontsize=7)
    a.grid(axis="y", visible=False)
    b = ax[1]
    b.scatter(dc, y, color=col, s=22, zorder=3, label="draw calls")
    b.set_xscale("log")
    b.set_xlabel("draw calls per frame (log)")
    b.set_title("(b) draw calls / triangles")
    for yi, d, t in zip(y, dc, tri):
        b.text(d * 1.25, yi, f"{t:.1f} M", va="center", fontsize=6.5, color="0.35")
    b.set_xlim(30, 3e4)
    b.grid(axis="y", visible=False)
    save(fig, "p_benchmark")


if __name__ == "__main__":
    secs = plot_day()
    print(f"one in-game day = {secs:.1f} real seconds ({secs / 60:.2f} min) in auto mode")
    plot_depth()
    plot_specular()
    plot_gouraud_phong()
    plot_gaussian()
    plot_tessellation()
    plot_palettes()
    hist_src = os.path.join(ROOT, "report", "figures", "raw", "lab", "cv_u1_a_raw_grey.png")
    if os.path.exists(hist_src):
        plot_histogram(hist_src)
    else:
        print("skip histogram (no", hist_src, ")")
    bench = os.path.join(ROOT, "report", "data", "bench.csv")
    if os.path.exists(bench):
        plot_bench(bench)
    else:
        print("skip benchmark (no", bench, ")")
    json.dump({"day_seconds": secs}, open(os.path.join(OUT, "plot_facts.json"), "w"), indent=1)
