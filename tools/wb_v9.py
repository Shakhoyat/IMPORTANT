"""Sheets 28+ of the workbook: 28 v8 Daylight & Barracks, 29 v9 Audit & Budget, then one sheet per v9 milestone.

Sources (all written by the program or its tests; nothing typed by hand except wording):
  build/test_results.json                    test status / observed values (via sheet 10)
  docs/v9_notes/audit/bench_shots_m0_*.json  --bench-shots: fixed shots x day / night (FPS, CPU phases, GPU passes)
  docs/v9_notes/audit/bench_wsl_*.json       --benchmark (v7 preset benchmark) on the WSL d3d12 path
  docs/v9_notes/m0/*.log                     --input-script runs of ThermalSense.exe ('script log' lines)
  docs/v8_notes/shots, docs/v9_notes/*/      captures (before / after)
Blue = imported, white = Excel formula, amber = design-note text. Missing inputs degrade to "pending: <what>" rows.
"""
import json
import os
import re

from openpyxl.formatting.rule import CellIsRule

from wb_common import (S_TEST, BOLD, FAIL_FILL, INPUT_FILL, NOTE_FILL, PASS_FILL, PENDING_FILL, TOTAL_FILL, col, define_name,
                       file_link, finish, header, image, path, put, section, title, widths)
from wb_v6 import pending

S_V8 = "28 v8 Daylight & Barracks"
S_V9A = "29 v9 Audit & Budget"
V9_SHEETS = [S_V8, S_V9A]
V9_INFO = {
    S_V8: "v8: daylight sky / ambient / grade, click-to-control, first / third person, barracks hall + residents, natural rover motion; tests T320-T327",
    S_V9A: "v9 audit: fixed-shot benchmark (5 shots x day / night, CPU phases, GPU passes), the vsync stall in the real exe, frame budget",
}

FRAME_BUDGET_MS = 1000.0 / 60.0   # 60 fps target (labelled setting)
LOG_RE = re.compile(r"script log (\S+) \| (.*?) \| ([\d.]+) fps, ms/frame: sim ([\d.]+) render ([\d.]+) vision ([\d.]+) swap ([\d.]+) \((\d+) frames\)")
POS_RE = re.compile(r"pos (-?[\d.]+) (-?[\d.]+) (-?[\d.]+) yaw (-?[\d.]+) speed (-?[\d.]+)")


def _load(rel):
    p = path(rel)
    if not os.path.exists(p):
        return None
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def _script_logs(rel):
    p = path(rel)
    if not os.path.exists(p):
        return []
    out = []
    with open(p, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LOG_RE.search(line)
            if m:
                pos = POS_RE.search(m.group(2))
                out.append({"label": m.group(1), "state": m.group(2), "fps": float(m.group(3)), "sim": float(m.group(4)),
                            "render": float(m.group(5)), "vision": float(m.group(6)), "swap": float(m.group(7)),
                            "frames": int(m.group(8)), "pos": [float(pos.group(i)) for i in (1, 2, 3)] if pos else None,
                            "speed": float(pos.group(5)) if pos else None})
    return out


def _kpi(ws, items, row=3):
    for k, (lab, f, fmt) in enumerate(items):
        put(ws, row, 1 + 2 * k, lab, font=BOLD, fill=TOTAL_FILL)
        put(ws, row, 2 + 2 * k, f, fmt, font=BOLD)


def _status_rules(ws, rng):
    ws.conditional_formatting.add(rng, CellIsRule(operator="equal", formula=['"PASS"'], fill=PASS_FILL))
    ws.conditional_formatting.add(rng, CellIsRule(operator="equal", formula=['"FAIL"'], fill=FAIL_FILL))
    ws.conditional_formatting.add(rng, CellIsRule(operator="equal", formula=['"pending"'], fill=PENDING_FILL))


def tests_block(ws, r, ctx, lo, hi, label):
    """Every test with lo <= id <= hi from build/test_results.json, status looked up from sheet 10. Returns (next row, a, b)."""
    section(ws, r, 1, label)
    r += 1
    header(ws, r, ["Test", "What is checked", "Expected", "Observed", "Status (sheet 10)"], height=22)
    rows = [t for t in ctx["tests"] if lo <= int(re.sub(r"\D", "", t["id"]) or 0) <= hi]
    a = r + 1
    if not rows:
        return pending(ws, a, f"no tests T{lo}-T{hi} in build/test_results.json", span=5) + 1, a, a
    t_first, t_last = ctx["test_rows"]
    ids, st = f"'{S_TEST}'!$A${t_first}:$A${t_last}", f"'{S_TEST}'!$F${t_first}:$F${t_last}"
    for i, t in enumerate(rows):
        q = a + i
        put(ws, q, 1, t["id"], font=BOLD)
        put(ws, q, 2, t.get("description"), fill=INPUT_FILL)
        put(ws, q, 3, t.get("expected"), fill=INPUT_FILL)
        put(ws, q, 4, t.get("observed"), fill=INPUT_FILL)
        put(ws, q, 5, f'=IFERROR(INDEX({st},MATCH(A{q},{ids},0)),"pending")', font=BOLD)
        ws.row_dimensions[q].height = 54
    b = a + len(rows) - 1
    _status_rules(ws, f"E{a}:E{b}")
    return b + 2, a, b


def _images(ws, r, shots, width=520, per_row=2, col_step=7):
    """Captions + embedded captures, per_row across; returns the next free row."""
    for i in range(0, len(shots), per_row):
        tallest = 0
        for k, (caption, rel) in enumerate(shots[i:i + per_row]):
            c = 1 + k * col_step
            put(ws, r, c, caption, font=BOLD, border=False)
            h = image(ws, f"{col(c)}{r + 1}", path(rel), width=width)
            if not h:
                put(ws, r + 1, c, "pending: " + rel, fill=PENDING_FILL)
            tallest = max(tallest, h)
        r += 2 + max(1, round(tallest / 20))
    return r


# ---- 28 v8 Daylight & Barracks --------------------------------------------------------------------------------------
V8_CHANGES = [
    ("Daylight sky ambient", "dayAmbient(): dome seen by the face + horizon band + ground bounce + circumsolar term", "hemisphere / sky-dome irradiance", "ShaderSources.cpp"),
    ("Sky reflections", "skyEnvironment(R, roughness) for specular ambient and wet ground", "environment reflection (roughness-blurred)", "ShaderSources.cpp"),
    ("Clear-day haze", "fog 0.0075 -> 0.0042, volumetric 0.0032 -> 0.0013 (DayCycle)", "exponential-squared fog, participating media", "DayCycle.cpp"),
    ("Daylight grade", "log-space contrast 1.10 around mid grey, +10 % saturation by day", "colour grading after tone mapping", "ShadersPostFx.cpp"),
    ("Specular anti-aliasing", "geometric specular AA from dFdx / dFdy of the normal + per-part specular LOD", "normal-variance roughness widening (Kaplanyan)", "ShaderSources.cpp, Renderer.cpp"),
    ("Click to control", "hover / click select (PovSource ray pick), Enter / double click", "ray picking against oriented bounds", "AppPlay.cpp"),
    ("Third person", "spring arm + oriented-box probe (NearBox) + sphere cast; camera-relative input", "camera collision, spring follow", "AppPlay.cpp"),
    ("Barracks hall", "walls with real door / window openings, ceiling, bunks, gym, mess table", "hierarchical modelling from unit primitives", "BarracksHall.cpp"),
    ("Residents", "6 off-duty soldiers: sleep, push-ups, sit-ups, pull-ups, talk, sit; A* -> string pulling -> Catmull-Rom", "path planning + spline routes + pose blending", "Garrison.cpp, Gait.cpp"),
    ("Natural rover motion", "Ornstein-Uhlenbeck speed wander, weave, hesitation, detours (seeded)", "stochastic processes, bounded randomness", "HideController.cpp"),
]


def build_v8(wb, ctx):
    ws = wb[S_V8]
    title(ws, "28  v8 Daylight & Barracks",
          "What v8 changed, the graphics concept behind each change, the tests that guard it (status from sheet 10) and the "
          "before / after captures (docs/v8_notes/shots). Amber = design notes; blue = imported from build/test_results.json.", span=12)
    r = 5
    section(ws, r, 1, "Changes")
    r += 1
    header(ws, r, ["Change", "What", "Graphics concept", "Code"], height=20)
    for i, row in enumerate(V8_CHANGES):
        for k, v in enumerate(row):
            put(ws, r + 1 + i, 1 + k, v, fill=NOTE_FILL, font=BOLD if k == 0 else None)
        ws.row_dimensions[r + 1 + i].height = 30
    r += len(V8_CHANGES) + 2
    r, a, b = tests_block(ws, r, ctx, 320, 329, "v8 tests (T320-T327)")
    _kpi(ws, [("v8 tests", f"=ROWS(E{a}:E{b})", "0"), ("Passed", f'=COUNTIF(E{a}:E{b},"PASS")', "0"),
              ("Failed", f'=COUNTIF(E{a}:E{b},"FAIL")', "0")])
    section(ws, r, 1, "Captures (headless EPIC on the RTX 4070)")
    r = _images(ws, r + 1, [("Before v8: day overview", "docs/v8_notes/shots/before_day_overview.jpg"),
                            ("After v8: day overview", "docs/v8_notes/shots/after_day_overview.jpg"),
                            ("Third person (spring arm)", "docs/v8_notes/shots/play_third_person.jpg"),
                            ("Inside the barracks hall", "docs/v8_notes/shots/play_hall_inside.jpg"),
                            ("Resident: push-ups", "docs/v8_notes/shots/play_hall_pushups.jpg"),
                            ("The hall at night", "docs/v8_notes/shots/play_hall_night.jpg")])
    widths(ws, [24, 52, 40, 26, 14, 4, 4, 24, 20, 20, 20, 20, 20])
    finish(ws, 6, freeze="A4")


# ---- 29 v9 Audit & Budget -------------------------------------------------------------------------------------------
def _shot_table(ws, r, bench, label):
    """Fixed-shot table from a --bench-shots JSON; returns (next row, first row, last row)."""
    section(ws, r, 1, label)
    r += 1
    if not bench:
        return pending(ws, r, "bench-shots JSON missing", span=12) + 1, r, r
    put(ws, r, 1, "Renderer", font=BOLD)
    put(ws, r, 2, bench.get("renderer"), fill=INPUT_FILL)
    put(ws, r, 4, "Quality / size", font=BOLD)
    put(ws, r, 5, f'{bench.get("quality")} {bench.get("resolution")} vsync {bench.get("vsync")}', fill=INPUT_FILL)
    r += 1
    header(ws, r, ["Shot", "Time", "Frame ms", "FPS", "CPU sim + UAV cams", "CPU render submit", "CPU UAV vision", "Present",
                   "GPU pass sum ms", "Draws", "Shadow draws", "Triangles (M)", "Margin vs 60 fps (ms)", "Holds 60?"], height=40)
    a = r + 1
    for i, s in enumerate(bench.get("shots", [])):
        q = a + i
        put(ws, q, 1, s["shot"], font=BOLD)
        put(ws, q, 2, s["time"])
        for k, key in enumerate(["frame_ms"]):
            put(ws, q, 3 + k, s[key], "0.00", INPUT_FILL)
        put(ws, q, 4, f"=IF(C{q}>0,1000/C{q},0)", "0.0")
        for k, key in enumerate(["cpu_sim_ms", "cpu_render_ms", "cpu_vision_ms", "present_ms", "gpu_sum_ms", "draws", "shadow_draws"]):
            put(ws, q, 5 + k, s[key], "0" if key in ("draws", "shadow_draws") else "0.00", INPUT_FILL)
        put(ws, q, 12, s["triangles"] / 1e6, "0.00", INPUT_FILL)
        put(ws, q, 13, f"={FRAME_BUDGET_MS:.6f}-C{q}", "0.00")
        put(ws, q, 14, f'=IF(D{q}>=60,"MEETS","BELOW")', font=BOLD)   # a measurement, not a test verdict
    b = a + len(bench.get("shots", [])) - 1
    ws.conditional_formatting.add(f"N{a}:N{b}", CellIsRule(operator="equal", formula=['"MEETS"'], fill=PASS_FILL))
    ws.conditional_formatting.add(f"N{a}:N{b}", CellIsRule(operator="equal", formula=['"BELOW"'], fill=PENDING_FILL))
    q = b + 1
    put(ws, q, 1, "Mean", font=BOLD, fill=TOTAL_FILL)
    for c in range(3, 14):
        put(ws, q, c, f"=AVERAGE({col(c)}{a}:{col(c)}{b})", "0.00", TOTAL_FILL, BOLD)
    return q + 2, a, b


def build_v9_audit(wb, ctx):
    ws = wb[S_V9A]
    title(ws, "29  v9 Audit & Budget",
          "Frame budget: at 60 fps a frame has 1000 / 60 = 16.67 ms for everything. The CPU (simulation, UAV camera passes, "
          "computer vision, draw submission) and the GPU (shadow maps, pre-pass, scene, post) work as a pipeline; the slower of "
          "the two sets the frame rate unless one waits for the other (a sync point: read-back, vsync swap). The fixed-shot "
          "benchmark (--bench-shots) measures 5 gameplay views x day / night with the same frame as the interactive loop, so "
          "every v9 milestone is compared against this baseline. Blue = imported from the program's JSON / logs; white = formulas.",
          span=14)
    win = _load("docs/v9_notes/audit/bench_shots_m0_win.json")
    wsl = _load("docs/v9_notes/audit/bench_shots_m0_wsl.json")
    r = 5
    r, wa, wb_ = _shot_table(ws, r, win, "Baseline (M0): ThermalSense.exe, native NVIDIA driver, 1920x1080 EPIC, vsync off")
    r, la, lb = _shot_table(ws, r, wsl, "Baseline (M0): headless WSL (Mesa d3d12 -> RTX 4070), 1920x1080 EPIC")
    _kpi(ws, [("Budget ms (60 fps)", f"=1000/60", "0.00"), ("Shots holding 60 (exe)", f'=COUNTIF(N{wa}:N{wb_},"MEETS")', "0"),
              ("Mean exe FPS", f"=AVERAGE(D{wa}:D{wb_})", "0.0"), ("Mean WSL FPS", f"=AVERAGE(D{la}:D{lb})", "0.0")])

    # GPU passes of the day overview (exe)
    section(ws, r, 1, "GPU passes: day overview (exe) - where the GPU time goes")
    r += 1
    header(ws, r, ["Pass", "GPU ms", "Share of pass sum"], height=20)
    p0 = r + 1
    passes = {}
    if win:
        passes = next((s["passes"] for s in win.get("shots", []) if s["shot"] == "overview" and s["time"] == "day"), {})
    if not passes:
        r = pending(ws, p0, "overview / day passes", span=3)
    else:
        for i, (name, ms) in enumerate(passes.items()):
            put(ws, p0 + i, 1, name, font=BOLD)
            put(ws, p0 + i, 2, ms, "0.00", INPUT_FILL)
        p1 = p0 + len(passes) - 1
        for i in range(len(passes)):
            put(ws, p0 + i, 3, f"=B{p0 + i}/SUM($B${p0}:$B${p1})", "0.0%")
        r = p1 + 2

    # The stall: the same orbit view at noon in the real exe, vsync on vs off
    section(ws, r, 1, "Why the live game showed 20 fps: vsync swap in the real exe (--input-script docs/v9_notes/m0/stall.txt)")
    r += 1
    header(ws, r, ["Run", "FPS", "CPU sim", "CPU render", "CPU vision", "Swap (present) ms", "Frames", "Swap share of frame"], height=30)
    s0 = r + 1
    runs = [("vsync 1 (old interactive default)", "docs/v9_notes/m0/stall_vsync1.log"), ("vsync 0", "docs/v9_notes/m0/stall_vsync0.log")]
    for i, (lab, rel) in enumerate(runs):
        q = s0 + i
        logs = _script_logs(rel)
        put(ws, q, 1, lab, font=BOLD)
        if not logs:
            put(ws, q, 2, "pending: " + rel, fill=PENDING_FILL)
            continue
        L = logs[-1]
        for k, key in enumerate(["fps", "sim", "render", "vision", "swap", "frames"]):
            put(ws, q, 2 + k, L[key], "0" if key == "frames" else "0.00", INPUT_FILL)
        put(ws, q, 8, f"=F{q}/(C{q}+D{q}+E{q}+F{q})", "0.0%")
    r = s0 + len(runs)
    put(ws, r, 1, "Finding", font=BOLD)
    put(ws, r, 2, "With swap interval 1 the swap blocks for most of the frame while the CPU and GPU are mostly idle: the "
                  "frame rate is set by the compositor wait, not by rendering. With swap interval 0 the same frame runs at "
                  "the GPU's rate. M1 replaces the blocking vsync by a frame limiter + fixed simulation tick.", fill=NOTE_FILL)
    ws.merge_cells(start_row=r, start_column=2, end_row=r, end_column=12)
    ws.row_dimensions[r].height = 45
    r += 2

    # WASD through the input script in the real exe
    section(ws, r, 1, "Control check in the real exe (--input-script docs/v9_notes/m0/wasd.txt): position after each step")
    r += 1
    header(ws, r, ["Step", "x", "y", "z", "Speed (m/s)", "Moved since previous (m)", "FPS"], height=30)
    w0 = r + 1
    logs = _script_logs("docs/v9_notes/m0/wasd.log")
    if not logs:
        r = pending(ws, w0, "docs/v9_notes/m0/wasd.log", span=7)
    else:
        for i, L in enumerate(logs):
            q = w0 + i
            put(ws, q, 1, L["label"], font=BOLD)
            for k in range(3):
                put(ws, q, 2 + k, L["pos"][k] if L["pos"] else None, "0.00", INPUT_FILL)
            put(ws, q, 5, L["speed"], "0.00", INPUT_FILL)
            put(ws, q, 6, f"=SQRT((B{q}-B{q - 1})^2+(D{q}-D{q - 1})^2)" if i else "-", "0.00")
            put(ws, q, 7, L["fps"], "0.0", INPUT_FILL)
        r = w0 + len(logs) + 1
    file_link(put(ws, r, 1, None), "docs/v9_notes/m0/frames", "frames from the exe")
    r += 2

    section(ws, r, 1, "Audit captures (headless EPIC 1080p, before v9)")
    r = _images(ws, r + 1, [("Day overview: dome hills, cone pines", "docs/v9_notes/audit/shots/day_overview.jpg"),
                            ("Third person: noisy broadleaf, flat lighting", "docs/v9_notes/audit/shots/play_third_person.jpg"),
                            ("Hall inside: flat blue window", "docs/v9_notes/audit/shots/play_hall_inside.jpg"),
                            ("Exe frame (WASD script, 123 fps vsync off)", "docs/v9_notes/m0/frames/wasd_1_after_w.jpg")])
    define_name(wb, "V9MeanExeFps", S_V9A, "F3")
    widths(ws, [26, 14, 12, 12, 12, 12, 12, 12, 12, 10, 10, 12, 12, 10])
    finish(ws, 7, freeze="A4")


def build_v9(wb, ctx):
    build_v8(wb, ctx)
    build_v9_audit(wb, ctx)
