"""Sheets 24-27 of the workbook (v7): Ray Tracing, Frame Explorer, Game Camera & Controls, v7 Tests.

Sources: scene_export.json key "v7" (DocExport, WP-W: raytrace / explorer / pov_sources / camera_rig / keys, all read from
the code the program runs), the measured GPU timings raytrace_perf.json (WP-H final runs in docs/v7_notes/wph_shots/ and
CAP's figure runs in report/figures_v7/), the ray debugger's tree report/figures_v7/raytrace_tree.json, the Frame
Explorer's explorer_stages.json (layer names), the T-ids found in tests/*.cpp and the test results (sheet 10).
Every block degrades to a "pending: <what>" row when its data is missing. Blue = imported, white = Excel formula,
amber = labelled authored setting or design-note text.
"""
import glob
import json
import math
import os
import re

from openpyxl.chart import Reference, ScatterChart, Series
from openpyxl.formatting.rule import CellIsRule
from openpyxl.styles import Alignment

from wb_common import (S_CTRL, S_FX, S_GAME, S_RT, S_TEST, S_V7T, BOLD, FAIL_FILL, FMT_INT, INPUT_FILL, NOTE_FILL, PASS_FILL,
                       PENDING_FILL, TOTAL_FILL, col, define_name, file_link, finish, header, load_json, path, put, section,
                       style_chart, title, widths)
from wb_v6 import pending

V7_SHEETS = [S_RT, S_FX, S_GAME, S_V7T]
LUMA = (0.2126, 0.7152, 0.0722)   # Rec. 709 luminance weights (as rt::luminance)


def _ok_rule(ws, rng):
    ws.conditional_formatting.add(rng, CellIsRule(operator="equal", formula=['"OK"'], fill=PASS_FILL))
    ws.conditional_formatting.add(rng, CellIsRule(operator="equal", formula=['"CHECK"'], fill=FAIL_FILL))


def _kpi(ws, items, row=3):
    """KPI strip: (label, formula, number format) pairs from column A."""
    for k, (lab, f, fmt) in enumerate(items):
        put(ws, row, 1 + 2 * k, lab, font=BOLD, fill=TOTAL_FILL)
        put(ws, row, 2 + 2 * k, f, fmt, font=BOLD)


def _fit(ws, rows, colw, min_h=15, line_h=13.5):
    """Explicit heights for wrapped text rows (Excel keeps openpyxl rows at the default height otherwise)."""
    for r in rows:
        lines = 1
        for c, w in colw.items():
            v = ws.cell(row=r, column=c).value
            if isinstance(v, str) and not v.startswith("="):
                per = max(1.0, w * 1.0)
                lines = max(lines, sum(max(1, math.ceil(len(part) / per)) for part in v.split("\n")))
        ws.row_dimensions[r].height = max(min_h, lines * line_h + 2)


def _lum(cells):
    return "+".join(f"{w}*{c}" for w, c in zip(LUMA, cells))


# ---- 24 Ray Tracing -------------------------------------------------------------------------------------------------------
PERF_RUNS = [("WP-H final, day 16:30", "docs/v7_notes/wph_shots/day/raytrace_perf.json"),
             ("WP-H final, night 23:00", "docs/v7_notes/wph_shots/night/raytrace_perf.json"),
             ("CAP figure run, day", "report/figures_v7/raytrace_perf.json"),
             ("CAP figure run, night", "report/figures_v7/rt_night/raytrace_perf.json")]
TREE_JSON = "report/figures_v7/raytrace_tree.json"


def build_raytracing(wb, ctx):
    ws = wb[S_RT]
    refs = ctx["refs"]
    rt = (ctx["scene"].get("v7") or {}).get("raytrace") or {}
    title(ws, "24  Ray Tracing",
          "v7 WP-H: the five render modes cycled with ';' (raster, hybrid, Whitted, path tracing, light tracing = reverse ray "
          "tracing), the measured GPU timings on the RTX 4070 (D3D12), the ray tree of one pixel from the ray debugger ('), "
          "Russian roulette and next-event estimation of the path tracer, the a-trous denoiser kernel and the SAH BVH that "
          "every ray walks. Blue = exported by the program (DocExport v7.raytrace, raytrace_perf.json, raytrace_tree.json); "
          "white = Excel formulas; amber = a labelled example setting.", span=14)
    modes = rt.get("modes") or []
    runs = [(lab, rel, load_json(rel)) for lab, rel in PERF_RUNS]
    runs = [(lab, rel, d) for lab, rel, d in runs if d and d.get("modes")]
    # Layout: modes summary (rows 6..), then the timings table it reads with AVERAGEIFS / INDEX-MATCH.
    m0 = 6
    m1 = m0 + max(len(modes), 1) - 1
    t_head = m1 + 4
    t0 = t_head + 1
    n_t = sum(len(d["modes"]) for _, _, d in runs)
    t1 = t0 + max(n_t, 1) - 1
    TR = lambda c: f"${c}${t0}:${c}${t1}"  # noqa: E731
    section(ws, 4, 1, "Render modes (key ';'), from rtModeName / rtModeCaption; timings of the run named in 'Run shown' + the mean of all runs")
    if not modes:
        pending(ws, m0, "v7.raytrace.modes not exported - rebuild build/export_docs and re-run `make docs`", span=10)
    else:
        header(ws, m0 - 1, ["#", "Mode", "What it does (rtModeCaption)", "Internal W", "Internal H", "Scale (W / output W)",
                            "Pixels traced", "ms / frame (run shown)", "RT pass ms (run shown)", "FPS", "Speed-up vs raster",
                            "RT ns / pixel", "Mean ms, all runs", "Runs"], height=40)
        sel = f"$B${m1 + 1}"
        put(ws, m1 + 1, 1, "Run shown", font=BOLD)
        put(ws, m1 + 1, 2, next((r[0] for r in runs if r[0] == "CAP figure run, day"), runs[0][0] if runs else None), fill=NOTE_FILL, font=BOLD)  # newest: CAP day run
        put(ws, m1 + 1, 3, "labelled choice: type any run name of column A below (the WP-H rows are the timings of wp-h.md)")
        for i, m in enumerate(modes):
            r = m0 + i
            put(ws, r, 1, m.get("index"), "0", INPUT_FILL)
            put(ws, r, 2, m.get("name"), fill=INPUT_FILL, font=BOLD)
            put(ws, r, 3, m.get("caption"), fill=INPUT_FILL)
            if runs:
                put(ws, r, 4, f'=IFERROR(INDEX({TR("G")},MATCH(B{r},{TR("F")},0)),"-")', FMT_INT)
                put(ws, r, 5, f'=IFERROR(INDEX({TR("H")},MATCH(B{r},{TR("F")},0)),"-")', FMT_INT)
                put(ws, r, 6, f"=D{r}/$D${t0}", "0.00")
                put(ws, r, 7, f"=D{r}*E{r}", FMT_INT)
                put(ws, r, 8, f'=AVERAGEIFS({TR("I")},{TR("F")},B{r},{TR("A")},{sel})', "0.0")
                put(ws, r, 9, f'=AVERAGEIFS({TR("J")},{TR("F")},B{r},{TR("A")},{sel})', "0.0")
                put(ws, r, 10, f"=1000/H{r}", "0.0")
                put(ws, r, 11, f"=$H${m0}/H{r}", "0.00")
                put(ws, r, 12, f'=IF(N(G{r})>0,I{r}*1000000/G{r},"-")', "0.0")   # raster reports 0 x 0 traced pixels
                put(ws, r, 13, f'=AVERAGEIFS({TR("I")},{TR("F")},B{r})', "0.0")
                put(ws, r, 14, f'=COUNTIF({TR("F")},B{r})', "0")
        _fit(ws, range(m0, m1 + 1), {3: 44})
    section(ws, t_head - 1, 1, "Measured GPU timings (wall clock with glFinish, 1280 x 720 output, EPIC; raytrace_perf.json)")
    if not runs:
        pending(ws, t0, "no raytrace_perf.json found (WP-H wph_shots / CAP report/figures_v7)", span=10)
    else:
        header(ws, t_head, ["Run", "Source file", "Renderer", "Output W", "Output H", "Mode", "Internal W", "Internal H",
                            "ms / frame", "RT pass ms", "FPS", "Pixels traced", "RT ns / pixel", "Speed-up vs raster (same run)",
                            "RT share of frame"], height=40)
        r = t0
        for lab, rel, d in runs:
            size = d.get("size") or [None, None]
            for m in d["modes"]:
                inn = m.get("internal") or [None, None]
                for c, v, fmt in ((1, lab, None), (2, rel, None), (3, d.get("renderer"), None), (4, size[0], "0"), (5, size[1], "0"),
                                  (6, m.get("mode"), None), (7, inn[0], "0"), (8, inn[1], "0"), (9, m.get("ms_per_frame"), "0.00"),
                                  (10, m.get("rt_pass_ms"), "0.00")):
                    put(ws, r, c, v, fmt, INPUT_FILL)
                put(ws, r, 11, f"=1000/I{r}", "0.0")
                put(ws, r, 12, f"=G{r}*H{r}", FMT_INT)
                put(ws, r, 13, f'=IF(L{r}>0,J{r}*1000000/L{r},"-")', "0.0")   # raster: no traced pixels
                put(ws, r, 14, f'=SUMIFS({TR("I")},{TR("A")},A{r},{TR("F")},"RASTER")/I{r}', "0.00")
                put(ws, r, 15, f"=J{r}/I{r}", "0%")
                ws.row_dimensions[r].height = 15
                for cc in range(1, 16):
                    ws.cell(row=r, column=cc).alignment = Alignment(vertical="top")
                r += 1
    r = t1 + 3
    # ---- trace settings
    section(ws, r, 1, "Tracer settings (rt::TraceSettings defaults of the CPU reference; the GPU runs the same algorithms)")
    r += 1
    tsd = rt.get("trace_settings") or {}
    cells = {}
    if not tsd:
        r = pending(ws, r, "v7.raytrace.trace_settings not exported", span=6)
    else:
        notes = {"whitted_depth": "Whitted recursion limit (reflection + refraction)", "max_bounces": "path / light tracing bounces",
                 "rr_start": "Russian roulette from this bounce on", "soft_shadows": "shadow-ray targets jittered over the light's size",
                 "emit_radius_m": "light tracing: radius of the disc sun / moon / sky photons start from"}
        for k, v in tsd.items():
            put(ws, r, 1, k, font=BOLD)
            put(ws, r, 2, ("yes" if v else "no") if isinstance(v, bool) else v, "0.##" if isinstance(v, (int, float)) and not isinstance(v, bool) else None, INPUT_FILL)
            put(ws, r, 3, notes.get(k, ""))
            cells[k] = f"$B${r}"
            r += 1
    r += 1
    r = _ray_tree(ws, r, refs)
    r = _roulette(ws, r + 1, cells)
    r = _lights(ws, r + 1, rt, cells)
    r = _atrous(ws, r + 1)
    r = _bvh(ws, r + 1, rt, refs)
    # KPI strip
    items = [("RT modes", f"=COUNTA(B{m0}:B{m1})" if modes else "pending", "0")]
    if "bvh_prims" in refs:
        items += [("BVH primitives", f"={refs['bvh_prims']}", FMT_INT), ("SAH cost: median / SAH", f"={refs['bvh_sah_gain']}", "0.00"),
                  ("Prim tests / ray (BVH)", f"={refs['bvh_tests_ray']}", "0.0")]
    if runs and modes:
        items.append(("Fastest full-RT mode (ms)", f"=MIN(H{m0 + 2}:H{m1})", "0.0"))
    items.append(("Checks OK", '=COUNTIF(A4:Q400,"OK")', "0"))
    _kpi(ws, items)
    refs["rt_ok"] = f"'{S_RT}'!${col(2 * len(items))}$3"
    refs["rt_modes"] = f"'{S_RT}'!$B$3"
    define_name(wb, "RtChecksOK", S_RT, f"{col(2 * len(items))}3")
    widths(ws, [24, 40, 40, 11, 11, 15, 12, 12, 12, 11, 14, 12, 12, 14, 11])
    finish(ws, 5, freeze="A4")


def _ray_tree(ws, r, refs):
    section(ws, r, 1, "Ray tree of one pixel (ray debugger ', CPU Whitted tree; report/figures_v7/raytrace_tree.json)")
    r += 1
    tree = load_json(TREE_JSON)
    if not tree or not tree.get("rays"):
        return pending(ws, r, TREE_JSON + " not captured yet (CAP raytrace_tree shot)", span=8) + 1
    px, rad = tree.get("pixel") or [None, None], tree.get("radiance") or [None] * 3
    put(ws, r, 1, "Pixel (u, v)", font=BOLD)
    put(ws, r, 2, px[0], "0.0000", INPUT_FILL)
    put(ws, r, 3, px[1], "0.0000", INPUT_FILL)
    r += 1
    put(ws, r, 1, "Radiance (r, g, b)", font=BOLD)
    for k in range(3):
        put(ws, r, 2 + k, rad[k], "0.00000", INPUT_FILL)
    rr = r
    r += 1
    put(ws, r, 1, "Luminance 0.2126 r + 0.7152 g + 0.0722 b", font=BOLD)
    put(ws, r, 2, "=" + _lum([f"B{rr}", f"C{rr}", f"D{rr}"]), "0.00000")
    lum = f"B{r}"
    r += 2
    header(ws, r, ["Ray id", "Kind", "Parent", "Depth", "t (m)", "Hit / light visible", "Object", "Contribution (luminance)",
                   "Sum of children", "Local term"], height=40)
    a = r + 1
    rays = tree["rays"]
    b = a + len(rays) - 1
    for i, ray in enumerate(rays):
        q = a + i
        vals = [(ray.get("id"), "0"), (ray.get("kind"), None), (ray.get("parent"), "0"), (ray.get("depth"), "0"), (ray.get("t"), "0.000"),
                ("yes" if ray.get("hit") else "no", None), (ray.get("object"), None), (ray.get("contribution"), "0.000000")]
        for k, (v, fmt) in enumerate(vals):
            put(ws, q, 1 + k, v, fmt, INPUT_FILL)
        put(ws, q, 9, f"=SUMIFS($H${a}:$H${b},$C${a}:$C${b},A{q})", "0.000000")
        put(ws, q, 10, f"=H{q}-I{q}", "0.000000")
    s = b + 1
    put(ws, s, 1, "Rays in the tree", font=BOLD, fill=TOTAL_FILL)
    put(ws, s, 2, f"=COUNT(A{a}:A{b})", "0", TOTAL_FILL)
    put(ws, s, 3, "Max depth", font=BOLD, fill=TOTAL_FILL)
    put(ws, s, 4, f"=MAX(D{a}:D{b})", "0", TOTAL_FILL)
    for k, kind in enumerate(("primary", "reflection", "refraction", "shadow")):
        put(ws, s + 1 + k // 2, 1 + 2 * (k % 2), f"{kind} rays", font=BOLD, fill=TOTAL_FILL)
        put(ws, s + 1 + k // 2, 2 + 2 * (k % 2), f'=COUNTIF(B{a}:B{b},"{kind}")', "0", TOTAL_FILL)
    put(ws, s + 3, 1, "Primary contribution = pixel luminance?", font=BOLD)
    put(ws, s + 3, 2, f'=IF(ABS(INDEX(H{a}:H{b},MATCH("primary",B{a}:B{b},0))-{lum})<0.00001,"OK","CHECK")', font=BOLD)
    _ok_rule(ws, f"B{s + 3}")
    put(ws, s + 3, 3, "the primary ray carries the whole Whitted sum of the pixel")
    refs["rt_tree_depth"] = f"'{S_RT}'!$D${s}"
    return s + 5


def _roulette(ws, r, cells):
    section(ws, r, 1, "Russian roulette (path tracer): terminate with q = max(0.05, 1 - max(beta)), survivors carry beta / (1 - q)")
    r += 1
    if "rr_start" not in cells or "max_bounces" not in cells:
        return pending(ws, r, "trace settings not exported", span=6) + 1
    put(ws, r, 1, "Example albedo a (grey Lambert)", font=BOLD)
    put(ws, r, 2, 0.6, "0.00", NOTE_FILL)
    put(ws, r, 3, "labelled example setting: each bounce multiplies the throughput by a (cosine sampling: f cos / pdf = a)")
    alb = f"$B${r}"
    r += 2
    header(ws, r, ["Bounce k", "Throughput without RR a^(k+1)", "q_k (stop probability)", "P(alive after k)", "Weight of a survivor",
                   "E[weight] = P x weight", "Unbiased?"], height=40)
    a = r + 1
    n = 9
    for k in range(n):
        q = a + k
        put(ws, q, 1, k, "0")
        put(ws, q, 2, f"={alb}^(A{q}+1)", "0.000000")
        prevW = f"E{q - 1}" if k else "1"
        prevP = f"D{q - 1}" if k else "1"
        put(ws, q, 3, f"=IF(A{q}>={cells['rr_start']},MAX(0.05,1-{prevW}*{alb}),0)", "0.000")
        put(ws, q, 4, f"={prevP}*(1-C{q})", "0.000000")
        put(ws, q, 5, f"={prevW}*{alb}/(1-C{q})", "0.000000")
        put(ws, q, 6, f"=D{q}*E{q}", "0.000000")
        put(ws, q, 7, f'=IF(ABS(F{q}-B{q})<0.000000001,"OK","CHECK")', font=BOLD)
    b = a + n - 1
    _ok_rule(ws, f"G{a}:G{b}")
    put(ws, b + 1, 1, "Expected bounces traced", font=BOLD, fill=TOTAL_FILL)
    put(ws, b + 1, 4, f"=SUM(D{a}:D{b})", "0.000", TOTAL_FILL)
    put(ws, b + 1, 5, "(without RR every path runs to max_bounces)")
    ws.merge_cells(start_row=b + 1, start_column=5, end_row=b + 1, end_column=10)
    return b + 3


def _lights(ws, r, rt, cells):
    section(ws, r, 1, "Next-event estimation: the lights every surface hit samples (night scene, t = 0) and the light tracer's emitter choice")
    r += 1
    lights = rt.get("lights") or []
    power = rt.get("emitter_power") or []
    env = rt.get("env") or {}
    if not lights or "emit_radius_m" not in cells:
        return pending(ws, r, "v7.raytrace.lights / emitter_power not exported", span=8) + 1
    put(ws, r, 1, "Sample distance d for E (m)", font=BOLD)
    put(ws, r, 2, 5, "0.0", NOTE_FILL)
    put(ws, r, 3, "labelled setting: irradiance E = pi L / (1 + kl d + kq d^2) on the light's axis (practicals: smooth window)")
    d = f"$B${r}"
    R = cells["emit_radius_m"]
    r += 1
    env_cells = []
    for lab, key in (("Sky zenith (r, g, b)", "zenith"), ("Sky bounce (r, g, b)", "bounce")):
        put(ws, r, 1, lab, font=BOLD)
        for k in range(3):
            put(ws, r, 2 + k, (env.get(key) or [None] * 3)[k], "0.0000", INPUT_FILL)
        put(ws, r, 5, "=" + _lum([f"B{r}", f"C{r}", f"D{r}"]), "0.0000")
        put(ws, r, 6, "luminance")
        env_cells.append(f"$E${r}")
        r += 1
    r += 1
    header(ws, r, ["#", "Type", "Colour r", "g", "b", "Luminance", "k linear", "k quadratic", "cos outer", "Range (m)",
                   "E at d (W/m2 units)", "Emitter power (exported)", "Emitter power (Excel)", "Check", "Pick probability"], height=40)
    a = r + 1
    rows = list(lights) + [{"type": "Sky", "color": None}]
    b = a + len(rows) - 1
    for i, L in enumerate(rows):
        q = a + i
        put(ws, q, 1, i + 1, "0")
        put(ws, q, 2, L["type"], fill=INPUT_FILL, font=BOLD)
        if L["type"] == "Sky":
            put(ws, q, 3, "sky: 0.5 (L zenith + L bounce)")
            put(ws, q, 6, f"=0.5*({env_cells[0]}+{env_cells[1]})", "0.0000")
            f_pow = f"=F{q}*PI()*PI()*{R}^2"
            put(ws, q, 11, "-")
        else:
            for k in range(3):
                put(ws, q, 3 + k, L["color"][k], "0.000", INPUT_FILL)
            put(ws, q, 6, "=" + _lum([f"C{q}", f"D{q}", f"E{q}"]), "0.0000")
            for c, key, fmt in ((7, "linear", "0.###"), (8, "quadratic", "0.###"), (9, "cos_outer", "0.0000"), (10, "range", "0.0")):
                put(ws, q, c, L.get(key), fmt, INPUT_FILL)
            t = L["type"]
            if t == "Directional":
                f_pow = f"=PI()*F{q}*PI()*{R}^2"
                put(ws, q, 11, f"=PI()*F{q}", "0.000")
            elif t in ("Point", "Spot"):
                f_pow = f"=PI()*F{q}*4*PI()" if t == "Point" else f"=PI()*F{q}*2*PI()*(1-I{q})"
                put(ws, q, 11, f"=PI()*F{q}/(1+G{q}*{d}+H{q}*{d}^2)", "0.000")
            else:
                f_pow = f"=PI()*F{q}*4*PI()"
                put(ws, q, 11, f"=IF(J{q}>0,PI()*F{q}*MAX(0,1-({d}/J{q})^4)^2/(1+{d}^2),0)", "0.000")
        put(ws, q, 12, power[i] if i < len(power) else None, "0.000", INPUT_FILL)
        put(ws, q, 13, f_pow, "0.000")
        put(ws, q, 14, f'=IF(ABS(M{q}-L{q})<=0.0001*MAX(1,ABS(L{q})),"OK","CHECK")', font=BOLD)
        put(ws, q, 15, f"=M{q}/SUM($M${a}:$M${b})", "0.00%")
    _ok_rule(ws, f"N{a}:N{b}")
    put(ws, b + 1, 1, "Lights", font=BOLD, fill=TOTAL_FILL)
    put(ws, b + 1, 2, f'=COUNTA(B{a}:B{b})-1', "0", TOTAL_FILL)
    put(ws, b + 1, 12, "Sum of pick probabilities", font=BOLD, fill=TOTAL_FILL)
    ws.merge_cells(start_row=b + 1, start_column=12, end_row=b + 1, end_column=14)
    put(ws, b + 1, 15, f"=SUM(O{a}:O{b})", "0.00%", TOTAL_FILL)
    put(ws, b + 2, 1, "Power by type (Excel): directional pi L pi R^2, point / practical pi L 4 pi, spot pi L 2 pi (1 - cos outer), "
                      "sky pi^2 R^2 L_sky (R = emit radius). Light tracing picks an emitter with probability power / total.")
    ws.merge_cells(start_row=b + 2, start_column=1, end_row=b + 2, end_column=12)
    return b + 4


def _atrous(ws, r):
    section(ws, r, 1, "A-trous wavelet denoiser (Dammertz et al. 2010): B3-spline kernel h_k = C(4, k) / 16, holes 2^i")
    r += 1
    put(ws, r, 1, "Passes (denoiseAtrous default)", font=BOLD)
    put(ws, r, 2, 5, "0", NOTE_FILL)
    put(ws, r, 3, "labelled: iterations = 5 in RayTracer.h; the GPU path tracer uses 5 / 3 / 1 passes by quality")
    npass = f"$B${r}"
    r += 2
    header(ws, r, ["j", "Offset (taps)", "h_j = 4! / (j! (4-j)!) / 16"], height=30)
    a = r + 1
    for j in range(5):
        put(ws, a + j, 1, j, "0")
        put(ws, a + j, 2, f"=A{a + j}-2", "0")
        put(ws, a + j, 3, f"=FACT(4)/(FACT(A{a + j})*FACT(4-A{a + j}))/16", "0.0000")
    b = a + 4
    put(ws, b + 1, 2, "Sum", font=BOLD, fill=TOTAL_FILL)
    put(ws, b + 1, 3, f"=SUM(C{a}:C{b})", "0.0000", TOTAL_FILL)
    # 5 x 5 kernel = outer product
    put(ws, r - 1, 5, "5 x 5 weights h_i h_j", font=BOLD, border=False)
    ws.merge_cells(start_row=r - 1, start_column=5, end_row=r - 1, end_column=10)
    header(ws, r, [""] + [f"{j - 2}" for j in range(5)], start_col=5, height=30)
    for i in range(5):
        put(ws, a + i, 5, f"=B{a + i}", "0", font=BOLD)
        for j in range(5):
            put(ws, a + i, 6 + j, f"=C{a + i}*C{a + j}", "0.0000")
    put(ws, b + 1, 5, "Sum", font=BOLD, fill=TOTAL_FILL)
    put(ws, b + 1, 6, f"=SUM(F{a}:J{b})", "0.0000", TOTAL_FILL)
    put(ws, b + 1, 7, f'=IF(ABS(F{b + 1}-1)<0.000001,"OK","CHECK")', font=BOLD)
    _ok_rule(ws, f"G{b + 1}")
    # passes
    header(ws, r, ["Pass i", "Hole spacing 2^(i-1)", "Footprint (px)"], start_col=12, height=30)
    for i in range(5):
        q = a + i
        put(ws, q, 12, i + 1, "0")
        put(ws, q, 13, f"=2^(L{q}-1)", "0")
        put(ws, q, 14, f"=1+4*SUM(M${a}:M{q})", "0")
    put(ws, b + 1, 12, "After the passes", font=BOLD, fill=TOTAL_FILL)
    put(ws, b + 1, 14, f"=INDEX(N{a}:N{b},{npass})", "0", TOTAL_FILL)
    put(ws, b + 2, 1, "Each tap is also weighted by edge stops: normal (n.n')^k, depth exp(-|z - z'| / (0.02 z step)), "
                      "luminance exp(-|l - l'| / (sigma (l + 0.02))) on the albedo-demodulated colour.")
    ws.merge_cells(start_row=b + 2, start_column=1, end_row=b + 2, end_column=14)
    return b + 4


def _box(ws, q, label, box, n=None):
    put(ws, q, 1, label, font=BOLD)
    lo, hi = box["min"], box["max"]
    for k in range(3):
        put(ws, q, 2 + k, lo[k], "0.00", INPUT_FILL)
        put(ws, q, 5 + k, hi[k], "0.00", INPUT_FILL)
    put(ws, q, 8, f"=E{q}-B{q}", "0.00")
    put(ws, q, 9, f"=F{q}-C{q}", "0.00")
    put(ws, q, 10, f"=G{q}-D{q}", "0.00")
    put(ws, q, 11, f"=2*(H{q}*I{q}+I{q}*J{q}+J{q}*H{q})", "0.0")
    put(ws, q, 12, n, FMT_INT, INPUT_FILL)


def _bvh(ws, r, rt, refs):
    section(ws, r, 1, "Bounding volume hierarchy (binned SAH, 16 bins) of the camp at t = 0, as SceneBridge::sync builds it")
    r += 1
    bvh, cst = rt.get("bvh") or {}, rt.get("bvh_constants") or {}
    if not bvh:
        return pending(ws, r, "v7.raytrace.bvh not exported", span=8) + 1
    header(ws, r, ["Constant", "Value", "Meaning"], height=20)
    r += 1
    notes = {"traversal_cost": "C_trav: cost of visiting an interior node", "intersect_cost": "C_isect: cost of one primitive test",
             "bins": "SAH candidate planes per axis", "max_leaf": "largest leaf when splitting does not pay",
             "rebuild_ratio": "rebuild when the refit SAH cost exceeds this x the build cost",
             "heightfield_samples": "terrain height field samples per side (0.25 m cells)"}
    c = {}
    for k, v in cst.items():
        put(ws, r, 1, k, font=BOLD)
        put(ws, r, 2, v, "0.##", INPUT_FILL)
        put(ws, r, 3, notes.get(k, ""))
        c[k] = f"$B${r}"
        r += 1
    r += 1
    header(ws, r, ["Tree", "Primitives", "Nodes", "Leaves", "Depth", "SAH cost (rel. root area)", "Prims / leaf", "Interior nodes"], height=40)
    med = bvh.get("median") or {}
    sa, me = r + 1, r + 2
    for q, lab, d in ((sa, "SAH (the program's build)", bvh), (me, "Median split (comparison)", med)):
        put(ws, q, 1, lab, font=BOLD)
        put(ws, q, 2, bvh.get("prims"), FMT_INT, INPUT_FILL)
        for k, key in enumerate(("nodes", "leaves", "depth")):
            put(ws, q, 3 + k, d.get(key), FMT_INT, INPUT_FILL)
        put(ws, q, 6, d.get("sah_cost"), "0.000", INPUT_FILL)
        put(ws, q, 7, f"=B{q}/D{q}", "0.00")
        put(ws, q, 8, f"=C{q}-D{q}", FMT_INT)
    q = me + 1
    put(ws, q, 1, "Median / SAH cost", font=BOLD, fill=TOTAL_FILL)
    put(ws, q, 6, f"=F{me}/F{sa}", "0.00", TOTAL_FILL)
    put(ws, q, 7, "the SAH tree is this much cheaper to traverse (expected cost per ray)")
    ws.merge_cells(start_row=q, start_column=7, end_row=q, end_column=12)
    refs["bvh_prims"] = f"'{S_RT}'!$B${sa}"
    refs["bvh_sah_gain"] = f"'{S_RT}'!$F${q}"
    r = q + 1
    put(ws, r, 1, "Full tree check", font=BOLD)
    put(ws, r, 2, f'=IF(C{sa}=2*D{sa}-1,"OK","CHECK")', font=BOLD)
    put(ws, r, 3, "a binary tree with L leaves has 2L - 1 nodes")
    _ok_rule(ws, f"B{r}")
    r += 1
    shapes = bvh.get("shapes") or {}
    if shapes:
        r += 1
        header(ws, r, ["Analytic shape", "Primitives", "Share"], height=20)
        a = r + 1
        for i, (k, v) in enumerate(shapes.items()):
            put(ws, a + i, 1, k, font=BOLD)
            put(ws, a + i, 2, v, FMT_INT, INPUT_FILL)
        b = a + len(shapes) - 1
        for i in range(len(shapes)):
            put(ws, a + i, 3, f"=B{a + i}/SUM($B${a}:$B${b})", "0.0%")
        for k, (lab, key) in enumerate((("Dynamic (refit per frame)", "dynamic_prims"), ("Foliage (alpha-tested)", "foliage_prims"))):
            put(ws, b + 1 + k, 1, lab, font=BOLD)
            put(ws, b + 1 + k, 2, bvh.get(key), FMT_INT, INPUT_FILL)
        put(ws, b + 3, 1, "Sum = primitives?", font=BOLD)
        put(ws, b + 3, 2, f'=IF(SUM(B{a}:B{b})=B{sa},"OK","CHECK")', font=BOLD)
        _ok_rule(ws, f"B{b + 3}")
        r = b + 5
    rs = bvh.get("root_split")
    if rs and "intersect_cost" in c:
        section(ws, r, 1, "Worked SAH example: the root split. C = C_trav + A(L)/A(N) n_L C_isect + A(R)/A(N) n_R C_isect versus a leaf n C_isect")
        r += 1
        header(ws, r, ["Box", "min x", "min y", "min z", "max x", "max y", "max z", "dx", "dy", "dz", "Area 2(dxdy+dydz+dzdx)",
                       "Primitives"], height=40)
        n_, l_, r_ = r + 1, r + 2, r + 3
        _box(ws, n_, "Root N", rs["root"], rs.get("n"))
        _box(ws, l_, "Left child L", rs["left"], rs.get("n_left"))
        _box(ws, r_, "Right child R", rs["right"], rs.get("n_right"))
        r = r_ + 1
        rows = [("Cost of the split", f"={c['traversal_cost']}+K{l_}/K{n_}*L{l_}*{c['intersect_cost']}+K{r_}/K{n_}*L{r_}*{c['intersect_cost']}"),
                ("Cost as one leaf", f"=L{n_}*{c['intersect_cost']}"),
                ("Decision", f'=IF(B{r}<B{r + 1},"split","leaf")'),
                ("n_L + n_R = n?", f'=IF(L{l_}+L{r_}=L{n_},"OK","CHECK")')]
        for k, (lab, f) in enumerate(rows):
            put(ws, r + k, 1, lab, font=BOLD)
            put(ws, r + k, 2, f, "0.0" if k < 2 else None, font=BOLD if k >= 2 else None)
        _ok_rule(ws, f"B{r + 3}")
        r += 5
    pr = bvh.get("probe")
    if pr:
        section(ws, r, 1, "Probe: a 64 x 36 grid of primary rays from the Frame Explorer's default view, BVH versus brute force (TraceStats)")
        r += 1
        a = r
        items = [("Rays", pr.get("rays"), FMT_INT), ("Rays that hit", pr.get("hits"), FMT_INT),
                 ("Same hit as brute force", pr.get("agree_with_brute_force"), FMT_INT), ("BVH node visits", pr.get("node_visits"), FMT_INT),
                 ("Primitive tests (BVH)", pr.get("prim_tests"), FMT_INT)]
        for k, (lab, v, fmt) in enumerate(items):
            put(ws, a + k, 1, lab, font=BOLD)
            put(ws, a + k, 2, v, fmt, INPUT_FILL)
        q = a + len(items)
        f = [("Node visits / ray", f"=B{a + 3}/B{a}", "0.0"), ("Primitive tests / ray (BVH)", f"=B{a + 4}/B{a}", "0.0"),
             ("Primitive tests / ray (brute force)", f"=B{sa}", FMT_INT), ("Fewer primitive tests (x)", f"=B{q + 2}/B{q + 1}", "0"),
             ("BVH = brute force on every ray?", f'=IF(B{a + 2}=B{a},"OK","CHECK")', None)]
        for k, (lab, fm, fmt) in enumerate(f):
            put(ws, q + k, 1, lab, font=BOLD, fill=TOTAL_FILL if k < 4 else None)
            put(ws, q + k, 2, fm, fmt, TOTAL_FILL if k < 4 else None, BOLD)
        _ok_rule(ws, f"B{q + 4}")
        put(ws, a, 3, f"camera {pr.get('camera_eye')} -> {pr.get('camera_target')}, fov {pr.get('fov_deg')} deg (as exported)")
        refs["bvh_tests_ray"] = f"'{S_RT}'!$B${q + 1}"
        r = q + 6
    return r


# ---- 25 Frame Explorer ----------------------------------------------------------------------------------------------------
def _stages_json():
    """Newest explorer_stages.json (layer names per stage; written by the explorer capture)."""
    cands = ["report/figures_v7/explorer_stages.json", "build/figdata_out/explorer_stages.json"] + \
            [os.path.relpath(p, path("")) for p in glob.glob(path("build/cap-runs/*/explorer_stages.json"))]
    cands = [c for c in cands if os.path.exists(path(c))]
    if not cands:
        return None, None
    best = max(cands, key=lambda c: os.path.getmtime(path(c)))
    return best, load_json(best)


def _shot_name(i, short):
    return "explorer_stage_%02d_%s.png" % (i, re.sub(r"[^a-z0-9]+", "_", short.lower()).strip("_"))


def build_explorer(wb, ctx):
    ws = wb[S_FX]
    refs = ctx["refs"]
    ex = (ctx["scene"].get("v7") or {}).get("explorer") or {}
    title(ws, "25  Frame Explorer",
          "v7 WP-I: the Frame Explorer (key `) freezes the frame and rebuilds it stage by stage. Below: the 16 stages with "
          "the caption, key formula and concept the program shows (fx::stageInfo), the layers of each stage and its capture; "
          "then worked examples recomputed by Excel: edge functions, barycentric weights and affine versus perspective-correct "
          "interpolation, the image-lab convolution kernels, the Halton (2, 3) TAA jitter and the ACES tone curve. "
          "Blue = exported by the program; white = Excel formulas.", span=10)
    stages = ex.get("stages") or []
    src, sj = _stages_json()
    layers = {s.get("index"): s.get("layers") or [] for s in (sj or {}).get("stages", [])}
    r = 4
    section(ws, r, 1, "The 16 stages (Left / Right in the explorer; Up / Down = layers)")
    r += 1
    if not stages:
        r = pending(ws, r, "v7.explorer.stages not exported - rebuild build/export_docs", span=8)
        first = last = None
    else:
        header(ws, r, ["#", "Short", "Stage", "What happens (caption)", "Key formula", "Concepts", "Layers", "Layer names (Up / Down)",
                       "Capture (report/figures_v7)"], height=30)
        first = r + 1
        for i, s in enumerate(stages):
            q = first + i
            for k, (v, fmt) in enumerate(((s.get("index"), "0"), (s.get("short"), None), (s.get("title"), None), (s.get("caption"), None),
                                          (s.get("formula"), None), (s.get("concepts"), None))):
                put(ws, q, 1 + k, v, fmt, INPUT_FILL, BOLD if k == 1 else None)
            ly = layers.get(s.get("index"))
            put(ws, q, 7, len(ly) if ly is not None else None, "0", INPUT_FILL if ly is not None else PENDING_FILL)
            put(ws, q, 8, "; ".join(ly) if ly else "pending: explorer_stages.json", fill=INPUT_FILL if ly else PENDING_FILL)
            shot = _shot_name(s.get("index", i + 1), s.get("short", ""))
            rel = "report/figures_v7/" + shot
            if os.path.exists(path(rel)):
                file_link(put(ws, q, 9, None), rel, shot)
            else:
                put(ws, q, 9, "not captured: " + shot, fill=PENDING_FILL)
        last = first + len(stages) - 1
        _fit(ws, range(first, last + 1), {3: 22, 4: 50, 5: 34, 6: 22, 8: 30})
        t = last + 1
        put(ws, t, 2, "Total", font=BOLD, fill=TOTAL_FILL)
        put(ws, t, 7, f"=SUM(G{first}:G{last})", "0", TOTAL_FILL, BOLD)
        put(ws, t + 1, 1, "Layer names: " + (src or "no explorer_stages.json") +
            (f" (open() took {sj.get('open_seconds'):.1f} s at {sj.get('size')})" if sj and sj.get("open_seconds") else ""))
        ws.merge_cells(start_row=t + 1, start_column=1, end_row=t + 1, end_column=8)
        r = t + 3
    r = _raster_example(ws, r, ex)
    r = _kernels(ws, r + 1, ex)
    r, h_ok = _halton(ws, r + 1, ex)
    r = _tone(ws, r + 1, ex)
    _kpi(ws, [("Stages", f"=COUNTA(B{first}:B{last})" if first else "pending", "0"),
              ("Layers", f"=SUM(G{first}:G{last})" if first else "pending", "0"),
              ("Captured stages", f'=COUNTIF(I{first}:I{last},"explorer_stage*")' if first else "pending", "0"),
              ("Checks OK", '=COUNTIF(A4:AH400,"OK")', "0"),
              ("Checks CHECK", '=COUNTIF(A4:AH400,"CHECK")', "0")])
    refs["fx_stages"] = f"'{S_FX}'!$B$3"
    refs["fx_ok"] = f"'{S_FX}'!$H$3"
    define_name(wb, "ExplorerStages", S_FX, "B3")
    widths(ws, [26, 12, 22, 50, 34, 22, 9, 30, 30] + [7] * 25)
    finish(ws, 5, freeze="A4")


def _raster_example(ws, r, ex):
    section(ws, r, 1, "Worked example (stages 4-5): edge functions, coverage, barycentric weights, affine vs perspective-correct")
    r += 1
    e = ex.get("raster_example")
    if not e:
        return pending(ws, r, "v7.explorer.raster_example not exported", span=8) + 1
    header(ws, r, ["Point", "x (px)", "y (px)", "clip w", "attribute a"], height=20)
    v = r + 1
    for k, key in enumerate(("v0", "v1", "v2")):
        q = v + k
        put(ws, q, 1, key, font=BOLD)
        put(ws, q, 2, e[key][0], "0.0", INPUT_FILL)
        put(ws, q, 3, e[key][1], "0.0", INPUT_FILL)
        put(ws, q, 4, e["w"][k], "0.00", INPUT_FILL)
        put(ws, q, 5, e["attr"][k], "0.00", INPUT_FILL)
    p = v + 3
    put(ws, p, 1, "p (pixel centre)", font=BOLD)
    put(ws, p, 2, e["p"][0], "0.0", INPUT_FILL)
    put(ws, p, 3, e["p"][1], "0.0", INPUT_FILL)
    X = lambda q: f"B{q}"  # noqa: E731
    Y = lambda q: f"C{q}"  # noqa: E731
    E = lambda a, b, pt: f"({X(b)}-{X(a)})*({Y(pt)}-{Y(a)})-({Y(b)}-{Y(a)})*({X(pt)}-{X(a)})"  # noqa: E731
    r = p + 2
    header(ws, r, ["Quantity", "Excel", "Program", "Check", "Formula"], height=20)
    a = r + 1
    v0, v1, v2 = v, v + 1, v + 2
    rows = [("E12(p)", "=" + E(v1, v2, p), e["e"][0], "(x2 - x1)(py - y1) - (y2 - y1)(px - x1)"),
            ("E20(p)", "=" + E(v2, v0, p), e["e"][1], "edge v2 -> v0"),
            ("E01(p)", "=" + E(v0, v1, p), e["e"][2], "edge v0 -> v1"),
            ("2 x signed area", "=" + E(v0, v1, v2), e["area2"], "E01(v2) > 0: counter-clockwise = front face"),
            ("b0 = E12 / 2A", f"=B{a}/B{a + 3}", e["bary"][0], "barycentric weight of v0"),
            ("b1 = E20 / 2A", f"=B{a + 1}/B{a + 3}", e["bary"][1], ""),
            ("b2 = E01 / 2A", f"=B{a + 2}/B{a + 3}", e["bary"][2], ""),
            ("Affine  sum b_i a_i", f"=B{a + 4}*E{v0}+B{a + 5}*E{v1}+B{a + 6}*E{v2}", e["affine"], "screen-space (wrong under perspective)"),
            ("Perspective  sum(b a / w) / sum(b / w)", f"=(B{a + 4}*E{v0}/D{v0}+B{a + 5}*E{v1}/D{v1}+B{a + 6}*E{v2}/D{v2})/"
             f"(B{a + 4}/D{v0}+B{a + 5}/D{v1}+B{a + 6}/D{v2})", e["perspective"], "what the GPU does (divide by w, interpolate, divide back)")]
    for k, (lab, f, prog, note) in enumerate(rows):
        q = a + k
        put(ws, q, 1, lab, font=BOLD)
        put(ws, q, 2, f, "0.000000")
        put(ws, q, 3, prog, "0.000000", INPUT_FILL)
        put(ws, q, 4, f'=IF(ABS(B{q}-C{q})<=0.00001*MAX(1,ABS(C{q})),"OK","CHECK")', font=BOLD)
        put(ws, q, 5, note)
    b = a + len(rows) - 1
    _ok_rule(ws, f"D{a}:D{b}")
    put(ws, b + 1, 1, "Covered (all E >= 0)", font=BOLD)
    put(ws, b + 1, 2, f'=IF(AND(B{a}>=0,B{a + 1}>=0,B{a + 2}>=0),"inside","outside")')
    put(ws, b + 1, 3, "inside" if e.get("inside") else "outside", fill=INPUT_FILL)
    put(ws, b + 2, 1, "Affine error", font=BOLD, fill=TOTAL_FILL)
    put(ws, b + 2, 2, f"=B{b - 1}-B{b}", "0.000000", TOTAL_FILL)
    put(ws, b + 2, 3, "the texture-swim the perspective-correct checker of stage 5 removes")
    ws.merge_cells(start_row=b + 2, start_column=3, end_row=b + 2, end_column=6)
    return b + 4


def _kernels(ws, r, ex):
    section(ws, r, 1, "Image lab (P): convolution kernels (fx::kernel), weights row-major; Excel sums them")
    r += 1
    ks = ex.get("kernels") or []
    if not ks:
        return pending(ws, r, "v7.explorer.kernels not exported", span=8) + 1
    header(ws, r, ["Kernel", "Size", "Bias", "Note", "Sum (program)", "Sum (Excel)", "Check", "", "", "Weights w[0..n-1] (row-major) ->"], height=30)
    ws.merge_cells(start_row=r, start_column=10, end_row=r, end_column=34)
    a = r + 1
    for i, k in enumerate(ks):
        q = a + i
        put(ws, q, 1, k.get("name"), fill=INPUT_FILL, font=BOLD)
        put(ws, q, 2, k.get("size"), "0", INPUT_FILL)
        put(ws, q, 3, k.get("bias"), "0.##", INPUT_FILL)
        put(ws, q, 4, (k.get("note") or "") + ("  (|G| from Gx, Gy)" if k.get("magnitude") else ""))
        put(ws, q, 5, k.get("sum"), "0.####", INPUT_FILL)
        w = k.get("w") or []
        put(ws, q, 6, f"=SUM(J{q}:{col(9 + len(w))}{q})", "0.####")
        put(ws, q, 7, f'=IF(ABS(F{q}-E{q})<0.00001,"OK","CHECK")', font=BOLD)
        for j, x in enumerate(w):
            put(ws, q, 10 + j, x, "0.###", INPUT_FILL)
    b = a + len(ks) - 1
    _ok_rule(ws, f"G{a}:G{b}")
    _fit(ws, range(a, b + 1), {4: 50})
    put(ws, b + 1, 1, "Normalised (sum 1)", font=BOLD, fill=TOTAL_FILL)
    put(ws, b + 1, 6, f'=COUNTIF(F{a}:F{b},1)', "0", TOTAL_FILL)
    put(ws, b + 1, 4, "smoothing kernels sum to 1 (brightness kept); edge kernels sum to 0")
    return b + 3


def _halton(ws, r, ex):
    section(ws, r, 1, "TAA jitter (stage 15): Halton (2, 3), k = 1..8; Excel radical inverse with MOD / INT")
    r += 1
    hs = ex.get("halton") or []
    if not hs:
        return pending(ws, r, "v7.explorer.halton not exported", span=6) + 1, None
    header(ws, r, ["k", "x = H2(k) (program)", "x (Excel)", "y = H3(k) (program)", "y (Excel)", "Check", "Pixel offset x", "Pixel offset y"], height=30)
    a = r + 1
    for i, (k, x, y) in enumerate(hs):
        q = a + i
        put(ws, q, 1, k, "0", INPUT_FILL)
        put(ws, q, 2, x, "0.0000", INPUT_FILL)
        put(ws, q, 3, f"=MOD(A{q},2)/2+MOD(INT(A{q}/2),2)/4+MOD(INT(A{q}/4),2)/8+MOD(INT(A{q}/8),2)/16", "0.0000")
        put(ws, q, 4, y, "0.0000", INPUT_FILL)
        put(ws, q, 5, f"=MOD(A{q},3)/3+MOD(INT(A{q}/3),3)/9+MOD(INT(A{q}/9),3)/27", "0.0000")
        put(ws, q, 6, f'=IF(AND(ABS(B{q}-C{q})<0.00001,ABS(D{q}-E{q})<0.00001),"OK","CHECK")', font=BOLD)
        put(ws, q, 7, f"=C{q}-0.5", "0.000")
        put(ws, q, 8, f"=E{q}-0.5", "0.000")
    b = a + len(hs) - 1
    _ok_rule(ws, f"F{a}:F{b}")
    put(ws, b + 1, 1, "Mean offset", font=BOLD, fill=TOTAL_FILL)
    put(ws, b + 1, 7, f"=AVERAGE(G{a}:G{b})", "0.000", TOTAL_FILL)
    put(ws, b + 1, 8, f"=AVERAGE(H{a}:H{b})", "0.000", TOTAL_FILL)
    return b + 3, f"F{a}:F{b}"


def _tone(ws, r, ex):
    section(ws, r, 1, "Tone mapping (stage 14): ACES fit and sRGB encode, display = sRGB(ACES(1.6 x exposure x))")
    r += 1
    tc = ex.get("tone_curve") or []
    if not tc:
        return pending(ws, r, "v7.explorer.tone_curve not exported", span=6) + 1
    header(ws, r, ["Linear radiance x", "log2 x", "ACES(x) (program)", "ACES(x) (Excel)", "Display, exposure 1 (program)",
                   "Display (Excel)", "Check"], height=40)
    h = r
    a = r + 1
    aces = lambda v: (f"MAX(0,MIN(1,({v}*({v}+0.0245786)-0.000090537)/({v}*(0.983729*{v}+0.432951)+0.238081)))")  # noqa: E731
    for i, (x, ac, disp) in enumerate(tc):
        q = a + i
        put(ws, q, 1, x, "0.00000", INPUT_FILL)
        put(ws, q, 2, f"=LOG(A{q},2)", "0.0")
        put(ws, q, 3, ac, "0.00000", INPUT_FILL)
        put(ws, q, 4, "=" + aces(f"A{q}"), "0.00000")
        put(ws, q, 5, disp, "0.00000", INPUT_FILL)
        g = aces(f"(1.6*A{q})")
        put(ws, q, 6, f"=IF({g}<=0.0031308,12.92*{g},1.055*{g}^(1/2.4)-0.055)", "0.00000")
        put(ws, q, 7, f'=IF(AND(ABS(C{q}-D{q})<0.00002,ABS(E{q}-F{q})<0.00002),"OK","CHECK")', font=BOLD)
    b = a + len(tc) - 1
    _ok_rule(ws, f"G{a}:G{b}")
    ch = ScatterChart()
    ch.title, ch.style = "Tone curve: display value vs log2 radiance", 13
    ch.x_axis.title, ch.y_axis.title = "log2 x", "0..1"
    xs = Reference(ws, min_col=2, min_row=a, max_row=b)
    for c in (3, 6):
        s = Series(Reference(ws, min_col=c, min_row=h, max_row=b), xs, title_from_data=True)
        s.marker.symbol, s.smooth = "none", True
        ch.series.append(s)
    ch.height, ch.width = 8, 15
    ws.add_chart(style_chart(ch), f"I{h}")
    return b + 2


# ---- 26 Game Camera & Controls --------------------------------------------------------------------------------------------
EXPLORER_KEYS = [  # WP-I design notes (FrameExplorer.h header): not part of the exported key tables
    ("` or Esc", "open / close the Frame Explorer (frame frozen)"),
    ("Left / Right, 1-9 / 0, Home / End", "previous / next stage, stage 1-10, first / last"),
    ("Up / Down", "layer of the stage"),
    ("W (drag)", "wipe against the previous stage"),
    ("M (wheel)", "magnifier (zoom)"),
    ("I", "pixel inspector (value in every buffer)"),
    ("P (K, [ ], S)", "image lab: kernel, gamma, sampling factor"),
    ("Space / R", "pause / restart the stage animation"),
    ("Left click", "probe pixel (every stage card follows it) or drag a split line"),
    ("Tab", "clean view (no cards)"),
]


def build_game(wb, ctx):
    ws = wb[S_GAME]
    refs = ctx["refs"]
    v7 = ctx["scene"].get("v7") or {}
    title(ws, "26  Game Camera & Controls",
          "v7 WP-G: the key table of the game camera (HudMath keyBindingsV7, the same rows the F1 help draws), every POV "
          "source the camera can jump into (listPovSources at t = 0: soldiers, UAVs, rover, vehicles; rounds in flight join "
          "while they fly) with its first-person eye, the picker order (nearest first) recomputed by Excel, and the camera "
          "rig's critically damped spring blend x(t) = (1 + w t) e^(-w t) recomputed against the program. "
          "Blue = exported; white = formulas; amber = design notes.", span=12)
    r = 4
    section(ws, r, 1, "v7 key bindings (keyBindingsV7)")
    r += 1
    keys = v7.get("keys") or []
    k0 = k1 = None
    if not keys:
        r = pending(ws, r, "v7.keys not exported", span=6)
    else:
        header(ws, r, ["#", "Keys", "Action", "Version", "Owner"], height=20)
        k0 = r + 1
        for i, k in enumerate(keys):
            q = k0 + i
            put(ws, q, 1, i + 1, "0")
            put(ws, q, 2, k.get("keys"), fill=INPUT_FILL, font=BOLD)
            put(ws, q, 3, k.get("action"), fill=INPUT_FILL)
            put(ws, q, 4, k.get("version"), fill=INPUT_FILL)
            put(ws, q, 5, f'=IF(OR(B{q}=";",B{q}="\'"),"WP-H ray tracing",IF(B{q}="`","WP-I frame explorer","WP-G game camera"))')
        k1 = k0 + len(keys) - 1
        _fit(ws, range(k0, k1 + 1), {2: 22, 3: 40})
        r = k1 + 2
    section(ws, r, 1, "Frame Explorer keys (inside the explorer; WP-I design notes)")
    r += 1
    header(ws, r, ["#", "Keys", "Action"], height=20)
    for i, (k, a) in enumerate(EXPLORER_KEYS):
        q = r + 1 + i
        put(ws, q, 1, i + 1, "0")
        put(ws, q, 2, k, fill=NOTE_FILL, font=BOLD)
        put(ws, q, 3, a, fill=NOTE_FILL)
    r += len(EXPLORER_KEYS) + 2
    # POV sources
    section(ws, r, 1, "POV sources: every moving object you can jump into (F4 picker, Insert / Delete, middle click)")
    r += 1
    srcs = v7.get("pov_sources") or []
    p0 = p1 = None
    if not srcs:
        r = pending(ws, r, "v7.pov_sources not exported", span=8)
    else:
        vw = v7.get("pov_viewer") or [None] * 3
        put(ws, r, 1, "Viewer (distance origin)", font=BOLD)
        for k in range(3):
            put(ws, r, 2 + k, vw[k], "0.0", INPUT_FILL)
        r += 1
        header(ws, r, ["Order (Ins / Del)", "Kind", "Name", "Tag", "Control", "Alt view", "Hostile", "x", "y", "z", "Distance (m)",
                       "Picker row (nearest first)", "Eye x", "Eye y", "Eye z", "Eye above origin (m)", "FOV (deg)"], height=40)
        p0 = r + 1
        p1 = p0 + len(srcs) - 1
        yn = lambda b: "yes" if b else "no"  # noqa: E731
        for i, s in enumerate(srcs):
            q = p0 + i
            pos, eye = s.get("position") or [None] * 3, s.get("first_eye") or [None] * 3
            vals = [(s.get("order"), "0"), (s.get("kind"), None), (s.get("name"), None), (s.get("tag"), None), (yn(s.get("can_control")), None),
                    (yn(s.get("has_alt")), None), (yn(s.get("hostile")), None), (pos[0], "0.00"), (pos[1], "0.00"), (pos[2], "0.00"),
                    (s.get("distance"), "0.00")]
            for k, (v, fmt) in enumerate(vals):
                put(ws, q, 1 + k, v, fmt, INPUT_FILL, BOLD if k == 2 else None)
            put(ws, q, 12, f'=COUNTIF($K${p0}:$K${p1},"<"&K{q})+1', "0")
            for k in range(3):
                put(ws, q, 13 + k, eye[k], "0.00", INPUT_FILL)
            put(ws, q, 16, f"=N{q}-I{q}", "0.00")
            put(ws, q, 17, s.get("first_fov"), "0", INPUT_FILL)
        r = p1 + 2
        section(ws, r, 1, "Sources by kind")
        r += 1
        header(ws, r, ["Kind", "Sources", "You can take control", "With alt view"], height=30)
        kinds = list(dict.fromkeys(s.get("kind") for s in srcs))
        a = r + 1
        for i, kd in enumerate(kinds):
            q = a + i
            put(ws, q, 1, kd, font=BOLD)
            put(ws, q, 2, f'=COUNTIF($B${p0}:$B${p1},A{q})', "0")
            put(ws, q, 3, f'=COUNTIFS($B${p0}:$B${p1},A{q},$E${p0}:$E${p1},"yes")', "0")
            put(ws, q, 4, f'=COUNTIFS($B${p0}:$B${p1},A{q},$F${p0}:$F${p1},"yes")', "0")
        b = a + len(kinds) - 1
        put(ws, b + 1, 1, "Total", font=BOLD, fill=TOTAL_FILL)
        for c in (2, 3, 4):
            put(ws, b + 1, c, f"=SUM({col(c)}{a}:{col(c)}{b})", "0", TOTAL_FILL, BOLD)
        put(ws, b + 2, 1, "Rows = list?", font=BOLD)
        put(ws, b + 2, 2, f'=IF(B{b + 1}=ROWS(B{p0}:B{p1}),"OK","CHECK")', font=BOLD)
        _ok_rule(ws, f"B{b + 2}")
        if "ctrl_possessable" in refs:
            put(ws, b + 3, 1, "Possessable controllables (sheet 14)", font=BOLD)
            put(ws, b + 3, 2, f"={refs['ctrl_possessable']}", "0")
            put(ws, b + 3, 3, "v4-v6 possession keys; POV sources add the wing UAV and the intruder rover and leave out the "
                              "non-moving controllables (doors, gate, flag, sky clock)")
            ws.merge_cells(start_row=b + 3, start_column=3, end_row=b + 3, end_column=10)
            ws.row_dimensions[b + 3].height = 28
        r = b + 5
    # camera rig
    rig = v7.get("camera_rig") or {}
    c = {}
    section(ws, r, 1, "Camera rig: jump blend (critically damped spring), letterbox bump, hit marker, free-fly camera")
    r += 1
    if not rig:
        r = pending(ws, r, "v7.camera_rig not exported", span=8)
    else:
        notes = {"blend_time_s": "kBlendTime: the offset is below 1 % (then snaps)", "omega": "kOmega: (1 + w T) e^(-w T) = 0.01",
                 "arc_per_metre": "long jumps lift the path by this per metre", "arc_max_m": "lift cap", "fly_speed": "free camera m/s (wheel x1.25)",
                 "fly_min_speed": "", "fly_max_speed": "", "fly_boost": "Shift multiplier", "fly_tau_s": "velocity time constant (glide)"}
        c = {}
        for k, v in rig.items():
            if isinstance(v, (int, float)):
                put(ws, r, 1, k, font=BOLD)
                put(ws, r, 2, v, "0.###", INPUT_FILL)
                put(ws, r, 3, notes.get(k, ""))
                c[k] = f"$B${r}"
                r += 1
        put(ws, r, 1, "Offset left at T (Excel)", font=BOLD, fill=TOTAL_FILL)
        put(ws, r, 2, f"=(1+{c['omega']}*{c['blend_time_s']})*EXP(-{c['omega']}*{c['blend_time_s']})", "0.0000", TOTAL_FILL, BOLD)
        put(ws, r, 3, f'=IF(ABS(B{r}-0.01)<0.0005,"OK","CHECK")', font=BOLD)
        _ok_rule(ws, f"C{r}")
        refs["rig_end"] = f"'{S_GAME}'!$B${r}"
        refs["rig_blend"] = f"'{S_GAME}'!{c['blend_time_s']}"
        r += 2
        cur = rig.get("curves") or []
        header(ws, r, ["t (s)", "Spring x(t) (program)", "x(t) = (1 + w t) e^(-w t)", "Check", "Blend weight (x - x(T)) / (1 - x(T))",
                       "Letterbox (program)", "0.11 sin(pi t / T)^0.6", "Hit marker alpha (program)", "Hit marker (Excel)", "Check"], height=48)
        h = r
        a = r + 1
        for i, cv in enumerate(cur):
            q = a + i
            put(ws, q, 1, cv.get("t"), "0.00", INPUT_FILL)
            put(ws, q, 2, cv.get("spring"), "0.0000", INPUT_FILL)
            put(ws, q, 3, f"=(1+{c['omega']}*A{q})*EXP(-{c['omega']}*A{q})", "0.0000")
            put(ws, q, 4, f'=IF(ABS(B{q}-C{q})<0.00001,"OK","CHECK")', font=BOLD)
            put(ws, q, 5, f"=MAX(0,MIN(1,(C{q}-{refs['rig_end']})/(1-{refs['rig_end']})))", "0.0000")
            put(ws, q, 6, cv.get("letterbox"), "0.0000", INPUT_FILL)
            p = f"A{q}/{c['blend_time_s']}"
            put(ws, q, 7, f"=IF(OR({p}<=0,{p}>=1),0,0.11*SIN(PI()*{p})^0.6)", "0.0000")
            put(ws, q, 8, cv.get("hit_marker"), "0.0000", INPUT_FILL)
            put(ws, q, 9, f"=IF(OR(A{q}<0,A{q}>0.4),0,IF(A{q}<=0.06,1,1-(A{q}-0.06)/0.34))", "0.0000")
            put(ws, q, 10, f'=IF(AND(ABS(F{q}-G{q})<0.00001,ABS(H{q}-I{q})<0.00001),"OK","CHECK")', font=BOLD)
        b = a + len(cur) - 1
        _ok_rule(ws, f"D{a}:D{b}")
        _ok_rule(ws, f"J{a}:J{b}")
        ch = ScatterChart()
        ch.title, ch.style = "Jump blend: spring offset, letterbox, hit marker vs time", 13
        ch.x_axis.title, ch.y_axis.title = "t (s)", "value"
        xs = Reference(ws, min_col=1, min_row=a, max_row=b)
        for cc in (3, 5, 7, 9):
            s = Series(Reference(ws, min_col=cc, min_row=h, max_row=b), xs, title_from_data=True)
            s.marker.symbol, s.smooth = "none", True
            ch.series.append(s)
        ch.height, ch.width = 8.5, 17
        ws.add_chart(style_chart(ch), f"L{h}")
        r = b + 2
    _kpi(ws, [("v7 keys", f"=COUNTA(B{k0}:B{k1})" if k0 else "pending", "0"),
              ("POV sources", f"=COUNTA(C{p0}:C{p1})" if p0 else "pending", "0"),
              ("You can control", f'=COUNTIF(E{p0}:E{p1},"yes")' if p0 else "pending", "0"),
              ("Blend time (s)", f"={c['blend_time_s']}" if rig else "pending", "0.00"),
              ("Checks OK", '=COUNTIF(A4:Q300,"OK")', "0")])
    refs["pov_sources"] = f"'{S_GAME}'!$D$3"
    refs["game_ok"] = f"'{S_GAME}'!$J$3"
    define_name(wb, "PovSources", S_GAME, "D3")
    widths(ws, [22, 22, 40, 12, 18, 12, 12, 10, 10, 10, 11, 12, 9, 9, 9, 12, 9])
    finish(ws, 5, freeze="A4")


# ---- 27 v7 Tests ----------------------------------------------------------------------------------------------------------
BLOCKS = [(240, 259, "WP-G game camera and POV", "docs/v7_notes/wp-g.md"), (260, 279, "WP-H ray tracing", "docs/v7_notes/wp-h.md"),
          (280, 299, "WP-I frame explorer", "docs/v7_notes/wp-i.md"), (300, 309, "WP-L game audio", "docs/v7_notes/wp-l.md"),
          (310, 319, "WP-CAP v7 director cut", "docs/v7_notes/wp-cap.md")]
OTHER_WP = [("WP-M", "hero model + material detail (+279 parts, soldier 75 leaves)", "docs/v7_notes/wp-m.md", "sheets 3, 22 (parts)"),
            ("WP-P", "visual polish: cloud lattice hash, HUD flush, 45-part rifle viewmodel", "docs/v7_notes/wp-p.md", "T198 unchanged"),
            ("WP-J", "playable builds: Windows exe (cross-compiled), WSLg GLFW", "docs/v7_notes/wp-j.md", "-"),
            ("WP-CAP", "v6 verification, v7 captures (game_*, raytrace_*, explorer_*); T310 director cut", "docs/v7_notes/wp-cap.md", "report/figures_v7"),
            ("WP-W", "this workbook: sheets 24-27, exporter v7 data", "docs/v7_notes/wp-w.md", "verify_workbook")]


def _test_ids():
    """(id, file) for every T-id >= 240 named in tests/*.cpp, in numeric order (T309b after T309)."""
    found = {}
    for f in sorted(glob.glob(path("tests/*.cpp"))):
        for m in re.finditer(r'"(T(\d+)[a-z]?)"', open(f, encoding="utf-8").read()):
            if int(m.group(2)) >= 240:
                found.setdefault(m.group(1), os.path.basename(f))
    key = lambda t: (int(re.sub(r"\D", "", t)), t)  # noqa: E731
    return [(t, found[t]) for t in sorted(found, key=key)]


def build_v7tests(wb, ctx):
    ws = wb[S_V7T]
    title(ws, "27  v7 Tests",
          "Every v7 test id found in tests/*.cpp (T240-T309), grouped by work package, with the description and observed "
          "value from build/test_results.json and the status looked up from sheet 10 (INDEX / MATCH). The block table counts "
          "them with COUNTIFS. Work packages without their own tests are listed with their notes.", span=8)
    by_id = {t["id"]: t for t in ctx["tests"]}
    t_first, t_last = ctx["test_rows"]
    ids, st = f"'{S_TEST}'!$A${t_first}:$A${t_last}", f"'{S_TEST}'!$F${t_first}:$F${t_last}"
    tl = _test_ids()
    r = 4
    section(ws, r, 1, "Blocks")
    r += 1
    header(ws, r, ["Block", "Ids", "Tests", "Passed", "Failed", "Pending", "Notes"], height=20)
    blk0 = r + 1
    rows_start = blk0 + len(BLOCKS) + 3 + len(OTHER_WP) + 3
    a = rows_start + 1
    b = a + len(tl) - 1
    for i, (lo, hi, name, notes) in enumerate(BLOCKS):
        q = blk0 + i
        put(ws, q, 1, name, font=BOLD)
        put(ws, q, 2, f"T{lo}-T{hi}")
        put(ws, q, 3, f'=COUNTIF($B${a}:$B${b},A{q})', "0")
        for c, s in ((4, "PASS"), (5, "FAIL"), (6, "pending")):
            put(ws, q, c, f'=COUNTIFS($B${a}:$B${b},$A{q},$F${a}:$F${b},"{s}")', "0")
        file_link(put(ws, q, 7, None), notes)
    q = blk0 + len(BLOCKS)
    put(ws, q, 1, "Total", font=BOLD, fill=TOTAL_FILL)
    for c in range(3, 7):
        put(ws, q, c, f"=SUM({col(c)}{blk0}:{col(c)}{q - 1})", "0", TOTAL_FILL, BOLD)
    ws.conditional_formatting.add(f"E{blk0}:E{q}", CellIsRule(operator="greaterThan", formula=["0"], fill=FAIL_FILL))
    r = q + 2
    section(ws, r, 1, "Other v7 work packages (no test block of their own)")
    r += 1
    header(ws, r, ["Work package", "Evidence", "Notes", "Scope"], height=20)
    for i, (wp, scope, notes, ev) in enumerate(OTHER_WP):
        q = r + 1 + i
        put(ws, q, 1, wp, font=BOLD)
        put(ws, q, 2, ev)
        file_link(put(ws, q, 3, None), notes) if os.path.exists(path(notes)) else put(ws, q, 3, notes)
        put(ws, q, 4, scope, fill=NOTE_FILL)
    r = rows_start - 1
    section(ws, r, 1, "Tests (status from sheet 10)")
    header(ws, rows_start, ["Test", "Block", "Source", "What is checked (build/test_results.json)", "Observed", "Status (sheet 10)"], height=24)
    if not tl:
        pending(ws, a, "no T-ids >= 240 found in tests/*.cpp", span=6)
        b = a
    for i, (tid, src) in enumerate(tl):
        q = a + i
        n = int(re.sub(r"\D", "", tid))
        blk = next((nm for lo, hi, nm, _ in BLOCKS if lo <= n <= hi), "other")
        t = by_id.get(tid)
        put(ws, q, 1, tid, font=BOLD)
        put(ws, q, 2, blk)
        put(ws, q, 3, src)
        put(ws, q, 4, t["description"] if t else "pending: test not in build/test_results.json yet", fill=INPUT_FILL if t else PENDING_FILL)
        put(ws, q, 5, t["observed"] if t else None, fill=INPUT_FILL if t else None)
        put(ws, q, 6, f'=IFERROR(INDEX({st},MATCH(A{q},{ids},0)),"pending")', font=BOLD)
    ws.conditional_formatting.add(f"F{a}:F{b}", CellIsRule(operator="equal", formula=['"PASS"'], fill=PASS_FILL))
    ws.conditional_formatting.add(f"F{a}:F{b}", CellIsRule(operator="equal", formula=['"FAIL"'], fill=FAIL_FILL))
    ws.conditional_formatting.add(f"F{a}:F{b}", CellIsRule(operator="equal", formula=['"pending"'], fill=PENDING_FILL))
    _fit(ws, range(a, b + 1), {4: 54, 5: 46}, line_h=12.5)
    _kpi(ws, [("v7 tests", f"=ROWS(F{a}:F{b})", "0"), ("Passed", f'=COUNTIF(F{a}:F{b},"PASS")', "0"),
              ("Failed", f'=COUNTIF(F{a}:F{b},"FAIL")', "0"), ("Pending", f'=COUNTIF(F{a}:F{b},"pending")', "0")])
    ctx["refs"].update(v7_tests=f"'{S_V7T}'!$B$3", v7_tests_passed=f"'{S_V7T}'!$D$3", v7_tests_failed=f"'{S_V7T}'!$F$3")
    define_name(wb, "V7TestsPassed", S_V7T, "D3")
    widths(ws, [28, 22, 24, 54, 46, 16, 26])
    finish(ws, rows_start, freeze="A4")


def build_v7(wb, ctx):
    build_raytracing(wb, ctx)
    build_explorer(wb, ctx)
    build_game(wb, ctx)
    build_v7tests(wb, ctx)
