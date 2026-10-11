"""Curves for the pipeline sections (lighting, shadows, Fresnel, tone mapping, fog, noise, curves, anti-aliasing).
Every curve uses the formula and the constants of the C++ / GLSL source named next to it, in the style of
tools/v10/plots.py. Run: ~/.local/bin/uv run -q --with matplotlib --with numpy python tools/report/gen_pipeline_plots.py [name ...]
Writes report/figures/plots/p_*.pdf (names below)."""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "v10"))
import plots  # noqa: E402  (style, colours, save, smoothstep)

plt, C, save, smoothstep = plots.plt, plots.C, plots.save, plots.smoothstep


# ------------------------------------------------------------ rendering/ShaderSources.cpp computeLight, practicalAttenuation
def attenuation():
    d = np.linspace(0, 30, 600)
    fig, ax = plt.subplots(1, 3, figsize=(7.0, 2.3))
    a = ax[0]
    for (kl, kq), name, col in [((0.35, 0.45), "beacon $k_l$=0.35, $k_q$=0.45", C[1]),
                                ((0.05, 0.03), "searchlight $k_l$=0.05, $k_q$=0.03", C[0])]:
        a.plot(d, 1 / (1 + kl * d + kq * d * d), color=col, label=name)
    a.set_xlabel("distance $d$ (m)")
    a.set_ylabel("$f_{att}$")
    a.set_title("(a) point / spot: $1/(1+k_ld+k_qd^2)$")
    a.legend(frameon=False, fontsize=7)
    a = ax[1]
    dd = np.linspace(0.0, 15, 600)
    a.plot(dd, 1 / (1 + dd * dd), color="0.5", ls=":", label="no window")
    for r, col in [(4.5, C[2]), (7.5, C[3]), (13.0, C[4])]:
        x = dd / r
        w = np.clip(1 - x ** 4, 0, 1)
        a.plot(dd, w * w / (1 + dd * dd), color=col, label="range %.1f m" % r)
    a.set_yscale("log")
    a.set_ylim(1e-4, 1.2)
    a.set_xlabel("distance $d$ (m)")
    a.set_title("(b) lamps: $(1-(d/r)^4)^2/(1+d^2)$")
    a.legend(frameon=False, fontsize=7)
    a = ax[2]
    g = np.linspace(0, 70, 1400)
    ci, co = math.cos(math.radians(9)), math.cos(math.radians(14))
    cone = np.clip((np.cos(np.radians(g)) - co) / (ci - co), 0, 1) ** 2
    a.plot(g, cone, color=C[0], label="searchlight, $\\theta_i$=9$^\\circ$, $\\theta_o$=14$^\\circ$")
    for hc, col in [(38, C[3]), (55, C[1])]:
        c = math.cos(math.radians(hc))
        a.plot(g, smoothstep(c, c + 0.1, np.cos(np.radians(g))), color=col, ls="--",
               label="hooded lamp, cone %d$^\\circ$" % hc)
    a.set_xlabel("angle from the axis $\\gamma$ (deg)")
    a.set_title("(c) cone factor")
    a.set_ylim(-0.05, 1.55)
    a.set_yticks([0, 0.25, 0.5, 0.75, 1.0])
    a.legend(frameon=False, fontsize=7, loc="upper right")
    save(fig, "p_attenuation")


# ------------------------------------------------------------ Phong vs Blinn-Phong, GGX (ShaderSources.cpp brdf)
def phong_blinn():
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.4))
    a = ax[0]
    # 2-D: N up, L at 40 deg from N; the view direction sweeps; the mirror direction R is at -40 deg.
    thL = math.radians(40)
    thV = np.radians(np.linspace(-90, 90, 721))
    R = -thL
    for n, col in [(16, C[0]), (64, C[1])]:
        phong = np.maximum(np.cos(thV - R), 0) ** n
        H = (thV + thL) / 2                     # half vector angle for unit L, V in the plane
        blinn = np.maximum(np.cos(H), 0) ** n
        a.plot(np.degrees(thV), phong, color=col, label="Phong $(R\\cdot V)^{%d}$" % n)
        a.plot(np.degrees(thV), blinn, color=col, ls="--", label="Blinn $(N\\cdot H)^{%d}$" % n)
    a.axvline(-40, color="0.6", lw=0.8, ls=":")
    a.text(-88, 0.92, "dotted: mirror\ndirection $R$", fontsize=7, color="0.4")
    a.set_xlabel("view angle from $N$ (deg), light at $+40^\\circ$")
    a.set_title("(a) the two specular lobes")
    a.legend(frameon=False, fontsize=7, loc="upper right")
    a = ax[1]
    th = np.radians(np.linspace(0, 60, 400))
    for rough, col in [(0.3, C[0]), (0.5, C[2]), (0.8, C[3])]:
        al = rough * rough
        a2 = al * al
        nh = np.cos(th)
        D = a2 / (math.pi * (nh * nh * (a2 - 1) + 1) ** 2)
        a.plot(np.degrees(th), D / D[0], color=col, label="GGX $D$, roughness %.1f" % rough)
    a.plot(np.degrees(th), np.cos(th) ** 64, color=C[1], ls="--", label="Blinn $(N\\cdot H)^{64}$")
    a.set_xlabel("angle between $N$ and $H$ (deg)")
    a.set_ylabel("lobe / peak")
    a.set_title("(b) GGX lobe (default at HIGH and above)")
    a.legend(frameon=False, fontsize=7)
    save(fig, "p_phong_blinn")


# ------------------------------------------------------------ raytrace/Optics.h fresnelDielectric, fresnelSchlick
def fresnel_exact(cosi, eta):
    sin2t = eta * eta * (1 - cosi * cosi)
    out = np.ones_like(cosi)
    ok = sin2t < 1
    cost = np.sqrt(1 - sin2t[ok])
    ci = cosi[ok]
    rs = (eta * ci - cost) / (eta * ci + cost)
    rp = (ci - eta * cost) / (ci + eta * cost)
    out[ok] = 0.5 * (rs * rs + rp * rp)
    return out


def fresnel():
    th = np.linspace(0, 89.9, 900)
    c = np.cos(np.radians(th))
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.3))
    a = ax[0]
    a.plot(th, fresnel_exact(c, 1 / 1.5), color=C[0], label="exact, air $\\to$ glass ($\\eta$=1/1.5)")
    a.plot(th, 0.04 + 0.96 * (1 - c) ** 5, color=C[1], ls="--", label="Schlick, $F_0$=0.04")
    a.set_xlabel("angle of incidence $\\theta_i$ (deg)")
    a.set_ylabel("reflectance $F$")
    a.set_title("(a) entering glass")
    a.legend(frameon=False, fontsize=7)
    a = ax[1]
    a.plot(th, fresnel_exact(c, 1.5), color=C[0], label="exact, glass $\\to$ air ($\\eta$=1.5)")
    a.plot(th, 0.04 + 0.96 * (1 - c) ** 5, color=C[1], ls="--", label="Schlick, $F_0$=0.04")
    crit = math.degrees(math.asin(1 / 1.5))
    a.axvline(crit, color="0.5", ls=":", lw=0.9)
    a.text(crit + 1.5, 0.45, "critical angle\n%.1f$^\\circ$: total internal\nreflection beyond" % crit, fontsize=7, color="0.35")
    a.set_xlabel("angle of incidence $\\theta_i$ (deg)")
    a.set_title("(b) leaving glass")
    a.legend(frameon=False, fontsize=7, loc="upper left")
    save(fig, "p_fresnel")


# ------------------------------------------------------------ ShaderSources.cpp kPoisson, cascadeShadow; math/ShadowMath.*
POISSON = [(-0.326, -0.406), (0.962, -0.195), (-0.203, 0.621), (0.473, -0.480), (-0.840, -0.074), (0.519, 0.767),
           (0.185, -0.893), (-0.696, 0.457), (0.507, 0.064), (0.896, 0.412), (-0.322, -0.933), (-0.792, -0.598),
           (0.100, 0.330), (-0.480, 0.150), (0.300, -0.200), (-0.100, -0.300)]


def shadows():
    fig, ax = plt.subplots(1, 3, figsize=(7.0, 2.35), gridspec_kw={"width_ratios": [1, 1.25, 1.25]})
    a = ax[0]
    p = np.array(POISSON) * 1.6
    a.add_patch(plt.Circle((0, 0), 1.6, fc="none", ec="0.6", ls=":"))
    for x in np.arange(-2, 2.01, 1):
        a.axvline(x, color="0.9", lw=0.6)
        a.axhline(x, color="0.9", lw=0.6)
    a.scatter(p[8:, 0], p[8:, 1], s=16, color=C[3], label="taps 9-16 (ULTRA, EPIC)", zorder=3)
    a.scatter(p[:8, 0], p[:8, 1], s=16, color=C[0], label="taps 1-8 (HIGH)", zorder=4)
    a.plot(0, 0, "+", color="k")
    a.set_aspect("equal")
    a.set_xlim(-2.1, 2.1)
    a.set_ylim(-2.1, 2.6)
    a.set_xlabel("shadow-map texels")
    a.set_title("(a) PCF Poisson taps, radius 1.6")
    a.legend(frameon=False, fontsize=6.5, loc="upper center", ncol=1, handletextpad=0.2)
    a = ax[1]
    gap = np.linspace(0, 10, 400)
    for deg, col, nm in [(1.6, C[3], "sun (source $1.6^\\circ$)"), (0.53, C[0], "moon ($0.53^\\circ$)")]:
        a.plot(gap, gap * 2 * math.tan(math.radians(deg) / 2) * 100, color=col, label=nm)
    a.set_xlabel("blocker to receiver gap (m)")
    a.set_ylabel("penumbra width (cm)")
    a.set_title("(b) PCSS: $w=\\Delta d\\cdot2\\tan(\\theta_s/2)$")
    a.legend(frameon=False, fontsize=7)
    a = ax[2]
    slope = np.linspace(0, 1, 200)
    # EPIC: 4096 maps; near cascade = sphere around the 0.05-14 m slice of a 50 deg, 16:9 view; world = 39 m sphere
    for texel, col, nm in [(0.00734, C[0], "near cascade, texel 0.73 cm"), (0.01905, C[2], "world cascade, texel 1.9 cm")]:
        a.plot(np.degrees(np.arccos(1 - slope)), texel * (1 + 2 * slope) * 100, color=col, label=nm)
    a.set_xlabel("angle between $N$ and $L$ (deg)")
    a.set_ylabel("normal offset (cm)")
    a.set_title("(c) bias: $t\\,(1+2(1-N\\cdot L))$")
    a.legend(frameon=False, fontsize=7)
    save(fig, "p_shadows")


# ------------------------------------------------------------ math/ToneMap.cpp, ShadersPostFx.cpp kCompositeFrag
INSET = np.array([0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772,
                  0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104]).reshape(3, 3).T
OUTSET = np.array([1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417,
                   -0.0980434501171241, -0.0990297440797205, -0.0989611768448433, 1.15107367264116]).reshape(3, 3).T
POLY = [15.5, -40.14, 31.96, -6.868, 0.4298, 0.1191, -0.00232]
MIN_EV, MAX_EV = -12.47393, 4.026069


def agx_grey(x):
    v = INSET @ np.vstack([x, x, x])
    l = (np.clip(np.log2(np.maximum(v, 1e-10)), MIN_EV, MAX_EV) - MIN_EV) / (MAX_EV - MIN_EV)
    s = np.zeros_like(l)
    for c in POLY:
        s = s * l + c
    o = OUTSET @ s
    return np.clip(o[1], 0, 1) ** 2.2


def aces_grey(x):
    inm = np.array([0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777]).reshape(3, 3).T
    outm = np.array([1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602]).reshape(3, 3).T
    v = inm @ np.vstack([x, x, x])
    a = v * (v + 0.0245786) - 0.000090537
    b = v * (0.983729 * v + 0.4329510) + 0.238081
    return np.clip((outm @ (a / b))[1], 0, 1)


def srgb(c):
    c = np.maximum(c, 0)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * c ** (1 / 2.4) - 0.055)


def tonemap():
    ev = np.linspace(-10, 6, 600)
    x = 0.18 * 2.0 ** ev
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.4))
    a = ax[0]
    a.plot(ev, srgb(agx_grey(x * 1.0)), color=C[0], label="AgX (day), exposure 1.0")
    a.plot(ev, srgb(aces_grey(x * 1.6)), color=C[1], label="ACES fit (night), exposure 1.6")
    a.plot(ev, srgb(np.clip(x * 1.6, 0, 1)), color="0.55", ls=":", label="no curve: clip at 1")
    a.plot(ev, srgb(x / (1 + x)), color=C[2], ls="--", label="Reinhard $x/(1+x)$ (explorer panels)")
    a.axvline(0, color="0.7", lw=0.7)
    a.text(0.2, 0.05, "mid grey 0.18", fontsize=7, color="0.4")
    a.set_xlabel("scene luminance, stops from mid grey 0.18 (EV)")
    a.set_ylabel("display value (sRGB-encoded)")
    a.set_title("(a) tone curves after exposure")
    a.legend(frameon=False, fontsize=7, loc="upper left")
    a = ax[1]
    c = np.linspace(0, 1, 400)
    a.plot(c, srgb(c), color=C[0], label="sRGB OETF (composite, linearToSrgb)")
    a.plot(c, c ** (1 / 2.2), color=C[3], ls="--", label="$c^{1/2.2}$")
    a.plot(c, c ** 2.2, color=C[1], label="$c^{2.2}$: base colour to linear (toLinear)")
    a.plot(c, c, color="0.6", ls=":", lw=0.9)
    a.set_xlabel("input")
    a.set_ylabel("output")
    a.set_title("(b) gamma: encode and decode")
    a.legend(frameon=False, fontsize=7)
    save(fig, "p_tonemap")


# ------------------------------------------------------------ ShaderSources.cpp fogAmount; scene/DayCycle.cpp densities
def fog():
    d = np.linspace(0, 400, 800)
    fig, ax = plt.subplots(figsize=(3.4, 2.3))
    for rho, col, nm in [(0.018, C[0], "night $\\rho$=0.018"), (0.0042, C[3], "day $\\rho$=0.0042")]:
        ax.plot(d, 1 - np.exp(-(rho * d) ** 2), color=col, label="exp$^2$, " + nm)
        ax.plot(d, 1 - np.exp(-rho * d), color=col, ls="--", lw=1.0, label="exp, " + nm)
    ax.set_xlabel("distance from the eye $d$ (m)")
    ax.set_ylabel("fog factor $f$")
    ax.set_title("Fog factor (exp$^2$ used)")
    ax.legend(frameon=False, fontsize=6.5, loc="lower right")
    save(fig, "p_fog")


# ------------------------------------------------------------ ShaderSources.cpp kPalette: mixBits, latticeHash, valueNoise, fbm
M32 = 0xFFFFFFFF


def mix_bits(h):
    h = h.astype(np.uint64) & M32
    h ^= h >> 16
    h = (h * 0x7feb352d) & M32
    h ^= h >> 15
    h = (h * 0x846ca68b) & M32
    h ^= h >> 16
    return h


def lattice(ix, iy):
    ux = ix.astype(np.int64).astype(np.uint64) & M32
    uy = iy.astype(np.int64).astype(np.uint64) & M32
    h = mix_bits((ux + mix_bits((uy + 0x9e3779b9) & M32)) & M32)
    return (h >> 8).astype(np.float64) / 16777216.0


def value_noise(px, py, fade=True):
    ix, iy = np.floor(px), np.floor(py)
    fx, fy = px - ix, py - iy
    if fade:
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = lattice(ix, iy), lattice(ix + 1, iy)
    c, d = lattice(ix, iy + 1), lattice(ix + 1, iy + 1)
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def fbm(px, py, octaves=4):
    s, amp = np.zeros_like(px), 0.5
    layers = []
    for _ in range(octaves):
        v = amp * value_noise(px, py)
        layers.append(v)
        s = s + v
        px, py = px * 2.03 + 17.1, py * 2.03 + 9.7
        amp *= 0.5
    return s, layers


def noise():
    fig = plt.figure(figsize=(7.0, 2.35))
    gs = fig.add_gridspec(1, 3, width_ratios=[1.35, 1, 1])
    a = fig.add_subplot(gs[0])
    x = np.linspace(0, 6, 1200)
    y0 = np.zeros_like(x) + 0.5
    a.plot(x, value_noise(x, y0 * 0 + 3.0, fade=False), color="0.55", ls="--", lw=1.0, label="linear weights")
    a.plot(x, value_noise(x, y0 * 0 + 3.0), color=C[0], label="smoothstep $3f^2-2f^3$")
    xi = np.arange(0, 7)
    a.plot(xi, lattice(xi.astype(float), np.full(7, 3.0)), "o", color=C[1], ms=3.5, label="lattice hash")
    a.set_xlabel("$x$ (lattice units)")
    a.set_title("(a) value noise along one row")
    a.legend(frameon=False, fontsize=7, loc="upper center", bbox_to_anchor=(0.5, -0.28), ncol=3, columnspacing=0.8)
    g = np.linspace(0, 8, 256)
    X, Y = np.meshgrid(g, g)
    total, layers = fbm(X, Y)
    a = fig.add_subplot(gs[1])
    a.imshow(layers[0] / 0.5, cmap="gray", origin="lower", extent=(0, 8, 0, 8))
    a.set_title("(b) octave 1 (amplitude 0.5)")
    a.grid(False)
    a.set_xticks([0, 4, 8])
    a.set_yticks([0, 4, 8])
    a = fig.add_subplot(gs[2])
    a.imshow(total, cmap="gray", origin="lower", extent=(0, 8, 0, 8))
    a.set_title("(c) fbm, 4 octaves, $\\times$2.03")
    a.grid(False)
    a.set_xticks([0, 4, 8])
    a.set_yticks([0, 4, 8])
    save(fig, "p_noise")


# ------------------------------------------------------------ math/Spline.cpp (centripetal Catmull-Rom), ai/HideController.cpp (Chaikin)
def catmull(points, alpha, n=40):
    P = np.array(points, float)
    out = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        t0 = 0.0
        t1 = t0 + np.linalg.norm(p1 - p0) ** alpha
        t2 = t1 + np.linalg.norm(p2 - p1) ** alpha
        t3 = t2 + np.linalg.norm(p3 - p2) ** alpha
        for t in np.linspace(t1, t2, n):
            a1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1
            a2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2
            a3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3
            b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2
            b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3
            out.append((t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2)
    return np.array(out)


def chaikin(P, passes):
    P = np.array(P, float)
    for _ in range(passes):
        Q = [P[0]]
        for a, b in zip(P[:-1], P[1:]):
            Q += [0.75 * a + 0.25 * b, 0.25 * a + 0.75 * b]
        Q.append(P[-1])
        P = np.array(Q)
    return P


def curves():
    # Both frames are placed by hand with one size (inches), so the axes and the titles of (a) and (b) line up; the scale
    # is equal in x and y in both, and the page keeps the size it had (7.12 x 2.60 in).
    FW, FH = 7.1167, 2.6028
    bw, bh = (FW - 0.08 - 0.22 - 0.06) / 2, FH - 0.08 - 0.26
    fig = plt.figure(figsize=(FW, FH), layout="none")
    a = fig.add_axes([0.08 / FW, 0.08 / FH, bw / FW, bh / FH])
    pts = [(0, 0), (1, 2.2), (1.4, 2.25), (3.6, 0.4), (5.2, 1.6), (5.4, 1.75), (6.4, 0.0)]
    for al, col, nm, ls in [(0.0, C[1], "uniform $\\alpha=0$", "--"), (0.5, C[0], "centripetal $\\alpha=0.5$ (used)", "-"),
                            (1.0, C[2], "chordal $\\alpha=1$", ":")]:
        cr = catmull(pts, al)
        a.plot(cr[:, 0], cr[:, 1], color=col, ls=ls, label=nm)
    P = np.array(pts)
    a.plot(P[:, 0], P[:, 1], "o", color="k", ms=3.5)
    xr = 7.0                                   # 0 .. 6.4 with a margin; the height follows from the frame
    yr = xr * bh / bw
    a.set_xlim(-0.3, -0.3 + xr)
    a.set_ylim(3.15 - yr, 3.15)
    a.set_aspect("equal")
    a.set_title("(a) Catmull-Rom through the same waypoints")
    a.legend(frameon=False, fontsize=7, loc="lower center", ncol=2)      # inside the frame, under the curves
    a.set_xticks([])
    a.set_yticks([])
    a = fig.add_axes([(0.08 + bw + 0.22) / FW, 0.08 / FH, bw / FW, bh / FH])
    path = [(0, 0), (0, 2), (1, 2), (1, 3), (3, 3), (3, 1), (5, 1)]
    a.plot(*np.array(path).T, "-o", color="0.6", ms=3, lw=1.0, label="cell path")
    for k, col in [(1, C[3]), (3, C[1])]:
        c = chaikin(path, k)
        a.plot(c[:, 0], c[:, 1], color=col, label="%d pass%s" % (k, "es" if k > 1 else "") + (" (rover)" if k == 3 else ""))
    yr = 3.6
    xr = yr * bw / bh
    a.set_xlim(2.5 - xr / 2, 2.5 + xr / 2)
    a.set_ylim(-0.3, -0.3 + yr)
    a.set_aspect("equal")
    a.set_title("(b) Chaikin corner cutting (3/4, 1/4)")
    a.legend(frameon=False, fontsize=7, loc="lower right")
    a.set_xticks([])
    a.set_yticks([])
    with plt.rc_context({"savefig.bbox": "standard"}):
        save(fig, "p_curves")


# ------------------------------------------------------------ RendererFrame.cpp halton + kTaaFrag feedback 0.9; DDA / Bresenham
def halton(i, b):
    f, r = 1.0, 0.0
    while i > 0:
        f /= b
        r += f * (i % b)
        i //= b
    return r


def antialias():
    fig, ax = plt.subplots(1, 3, figsize=(7.0, 2.3), gridspec_kw={"width_ratios": [1, 1.2, 1.25]})
    a = ax[0]
    pts = np.array([(halton(k, 2) - 0.5, halton(k, 3) - 0.5) for k in range(1, 9)])
    a.add_patch(plt.Rectangle((-0.5, -0.5), 1, 1, fc="#f2f2f2", ec="0.5"))
    a.plot(pts[:, 0], pts[:, 1], "-", color="0.75", lw=0.8)
    for k, (x, y) in enumerate(pts, 1):
        a.plot(x, y, "o", color=C[0], ms=4)
        a.text(x + 0.03, y + 0.03, str(k), fontsize=7)
    a.set_aspect("equal")
    a.set_xlim(-0.6, 0.6)
    a.set_ylim(-0.6, 0.6)
    a.set_title("(a) Halton(2,3) jitter, 8 frames")
    a.set_xlabel("pixel offset")
    a = ax[1]
    k = np.arange(0, 30)
    for fb, col, nm in [(0.9, C[0], "still camera, $\\beta$=0.9"), (0.75, C[1], "fast motion, $\\beta$=0.75")]:
        a.bar(k + (0.2 if fb < 0.8 else -0.2), (1 - fb) * fb ** k, width=0.4, color=col, label=nm)
    a.set_xlabel("frames ago $k$")
    a.set_ylabel("weight $(1-\\beta)\\beta^k$")
    a.set_title("(b) TAA history weights")
    a.legend(frameon=False, fontsize=7)
    a = ax[2]
    x0, y0, x1, y1 = 1, 1, 12, 6
    dx, dy = x1 - x0, y1 - y0
    for gx in range(0, 14):
        a.axvline(gx - 0.5, color="0.9", lw=0.6)
    for gy in range(0, 8):
        a.axhline(gy - 0.5, color="0.9", lw=0.6)
    p = 2 * dy - dx
    y = y0
    for x in range(x0, x1 + 1):
        a.add_patch(plt.Rectangle((x - 0.5, y - 0.5), 1, 1, fc=C[0], alpha=0.35, ec="none"))
        yd = y0 + dy / dx * (x - x0)
        a.plot(x, round(yd + 1e-9), "x", color=C[1], ms=4)
        if p >= 0:
            y += 1
            p += 2 * dy - 2 * dx
        else:
            p += 2 * dy
    a.plot([x0, x1], [y0, y1], color="k", lw=1.0)
    a.set_aspect("equal")
    a.set_xlim(-0.5, 13.5)
    a.set_ylim(-0.5, 7.5)
    a.set_title("(c) line (1,1)$\\to$(12,6)")
    a.text(0, 7.0, "squares: Bresenham, x: DDA round$(y)$", fontsize=7, color="0.3")
    a.grid(False)
    save(fig, "p_antialias")


if __name__ == "__main__":
    ALL = [attenuation, phong_blinn, fresnel, shadows, tonemap, fog, noise, curves, antialias]
    want = sys.argv[1:]          # no name = all; a name is a function above, e.g. curves
    for f in ALL:
        if not want or f.__name__ in want:
            f()
