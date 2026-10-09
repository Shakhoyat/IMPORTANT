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
    "figure.dpi": 150, "savefig.bbox": "tight", "axes.spines.top": False, "axes.spines.right": False,
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


def plot_day():
    h = np.linspace(0, 24, 2401)
    e = elevation(h)
    sun_key = SUN_I * smoothstep(-1.0, 10.0, e)
    moon_key = MOON_I * (1.0 - smoothstep(-6.0, -1.0, e))
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.5))
    a = ax[0]
    a.plot(h, e, color=C[0], label="sun elevation $e$")
    a.axhline(4, color="0.5", ls=":", lw=0.8)
    a.axhline(-6, color="0.5", ls=":", lw=0.8)
    a.fill_between(h, -90, 90, where=e < -6, color="#1f2a44", alpha=0.10, lw=0, label="NIGHT ($e<-6^\\circ$)")
    a.fill_between(h, -90, 90, where=(e >= -6) & (e <= 4), color=C[3], alpha=0.18, lw=0, label="DAWN / DUSK")
    a.set_xlim(0, 24)
    a.set_ylim(-75, 75)
    a.set_xticks(range(0, 25, 3))
    a.set_xlabel("in-game hour")
    a.set_ylabel("degrees")
    a.legend(loc="lower center", ncol=1, frameon=False)
    a.set_title("(a) solar elevation, latitude $22.8^\\circ$, $\\delta=-4.6^\\circ$")
    sr, ss = h[np.argmax(e > 0)], h[len(h) - 1 - np.argmax(e[::-1] > 0)]
    a.annotate(f"sunrise {int(sr)}:{int(round((sr % 1) * 60)):02d}", (sr, 0), (sr - 5.5, 30), fontsize=7,
               arrowprops=dict(arrowstyle="->", lw=0.6))
    a.annotate(f"sunset {int(ss)}:{int(round((ss % 1) * 60)):02d}", (ss, 0), (ss - 1.5, 45), fontsize=7,
               arrowprops=dict(arrowstyle="->", lw=0.6))
    b = ax[1]
    tr = transmittance(e)
    for i, (nm, col) in enumerate(zip("RGB", ["#c0392b", "#27ae60", "#2e60c0"])):
        b.plot(h, sun_key * tr[:, i], color=col, label=f"sun key {nm}: $3.2\\,s(e)\\,T_{nm}(e)$")
    b.plot(h, moon_key, color="0.25", ls="--", label="moon key (fades in below $-1^\\circ$)")
    b.set_xlim(0, 24)
    b.set_xticks(range(0, 25, 3))
    b.set_xlabel("in-game hour")
    b.set_ylabel("key-light intensity")
    b.legend(frameon=False, loc="upper left", fontsize=7)
    b.set_title("(b) key light per channel: blue is lost first near the horizon")
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
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.4))
    for n, col in [(0.05, C[0]), (0.5, C[1])]:
        dd = d[d >= n]
        zndc = (f + n) / (f - n) - 2 * f * n / ((f - n) * dd)
        ax[0].semilogx(dd, (zndc + 1) / 2, color=col, label=f"near = {n} m")
        # depth step per 24-bit code: dz_eye = (1/2^24) / (d window / d z_eye)
        dw = f * n / ((f - n) * dd ** 2)
        ax[1].loglog(dd, (1 / 2 ** 24) / dw, color=col, label=f"near = {n} m")
    ax[0].set_xlabel("distance from the eye (m)")
    ax[0].set_ylabel("window depth $z_w \\in [0,1]$")
    ax[0].set_title("(a) $z_w$ is hyperbolic in distance (far = 600 m)")
    ax[0].legend(frameon=False)
    ax[1].axhline(0.01, color="0.5", ls=":", lw=0.8)
    ax[1].text(0.06, 0.012, "1 cm", fontsize=7, color="0.4")
    ax[1].set_xlabel("distance from the eye (m)")
    ax[1].set_ylabel("smallest depth step (m), 24-bit")
    ax[1].set_title("(b) depth resolution: z-fighting grows as $d^2/n$")
    ax[1].legend(frameon=False)
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
    ax.set_title("Blinn-Phong lobe for the scene's shininess values")
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
    ax.legend(frameon=False, loc="lower center")
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
    ax.set_title(f"{size}x{size} Gaussian, $\\sigma$ = {sigma:g} (sum = 1)")
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
    ax.set_title("Separable Gaussian: $O(k)$ instead of $O(k^2)$")
    save(fig, "p_separable_cost")


def plot_tessellation():
    # primitives/Sphere.cpp createSphere(slices, stacks): triangles = 2 slices (stacks - 1); stacks = slices / 2 here.
    # Largest gap between the unit sphere and its polygon: the chord sagitta r (1 - cos(pi / slices)).
    s = np.arange(6, 97, 2)
    tri = 2 * s * (s // 2 - 1)
    err = 1 - np.cos(np.pi / s)
    fig, ax = plt.subplots(figsize=(3.4, 2.3))
    ax.loglog(tri, err * 100, color=C[0], marker="o", ms=2.5)
    for sl in (8, 16, 36, 72):
        t = 2 * sl * (sl // 2 - 1)
        e = (1 - np.cos(np.pi / sl)) * 100
        ax.annotate(f"{sl} slices" + (" (scene)" if sl == 36 else ""), (t, e), (t * 1.3, e * 1.3), fontsize=7)
    ax.set_xlabel("triangles per sphere")
    ax.set_ylabel("max. silhouette error (% of radius)")
    ax.set_title("Sphere tessellation: error $\\propto$ 1/triangles")
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
    for a, hh, cc, t in [(ax[0], hist, cdf, "(a) raw sensor frame"), (ax[1], h2, c2, "(b) after equalisation")]:
        a.bar(np.arange(256), hh / hh.max(), width=1.0, color=C[0], lw=0)
        a.plot(np.arange(256), cc, color=C[1], label="CDF")
        a.set_xlim(0, 255)
        a.set_xlabel("grey level")
        a.set_title(t)
        a.legend(frameon=False, loc="center right")
    ax[0].set_ylabel("count (normalised) / CDF")
    save(fig, "p_thermal_histogram")


def plot_bench(csv_path):
    rows = list(csv.DictReader(open(csv_path)))
    if not rows:
        return
    names = [r["config"] for r in rows]
    ms = [float(r["avg_ms"]) for r in rows]
    p95 = [float(r["p95_ms"]) for r in rows]
    fig, ax = plt.subplots(figsize=(7.0, 0.22 * len(rows) + 0.8))
    y = np.arange(len(rows))
    ax.barh(y, ms, color=C[0], label="mean frame time")
    ax.scatter(p95, y, color=C[1], marker="|", s=80, label="95th percentile", zorder=3)
    ax.set_yticks(y)
    ax.set_yticklabels(names, fontsize=7)
    ax.invert_yaxis()
    ax.axvline(1000 / 60, color="0.4", ls=":", lw=0.8)
    ax.text(1000 / 60, -0.8, " 60 fps", fontsize=7, color="0.4")
    ax.set_xlabel("ms per frame (1920x1080, EPIC, RTX 4070)")
    ax.legend(frameon=False, loc="lower right")
    save(fig, "p_benchmark")


if __name__ == "__main__":
    secs = plot_day()
    print(f"one in-game day = {secs:.1f} real seconds ({secs / 60:.2f} min) in auto mode")
    plot_depth()
    plot_specular()
    plot_gouraud_phong()
    plot_gaussian()
    plot_tessellation()
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
