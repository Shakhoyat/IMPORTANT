"""Sheet 9 (Render Pipeline): per-feature cost by ablation (primary) and GPU pass timer queries (approximate).

build/benchmark.json schema used here (written by ThermalSense.exe --benchmark):
  phases[] = {name?, quality, without, fps, frame_ms, draw_calls_*...}   (without = feature switched off, or empty)
  passes[] = {pass, gpu_ms}                                              (timer queries; overlap on an iGPU)
Nothing is shown as a number until it has been measured.
"""
from openpyxl.chart import BarChart, Reference

from wb_common import (BOLD, FMT_MS, FMT_PCT, INPUT_FILL, PENDING_FILL, S_PIPE, TOTAL_FILL, finish, header, put,
                       section, style_chart, title, widths)

NO_FEATURE = "-"  # written in the 'Without' column of full-quality rows (SUMIFS criterion for the baseline)
PLANNED_PASSES = ["Shadow maps (moon cascades + searchlight)", "Depth / normal pre-pass", "SSAO",
                  "Geometry / lighting (HDR)", "Volumetric searchlight beam", "Bloom", "Tone mapping + UI"]


def phase_label(p, i=0):
    if p.get("name"):
        return str(p["name"])
    q, w = p.get("quality"), p.get("without")
    return f"{q or 'phase ' + str(i + 1)}" + (f" without {w}" if w else "")


def _passes(bench):
    raw = (bench or {}).get("passes")
    if raw is None:  # per-phase schema: use the reference phase (HIGH, indexed, nothing switched off)
        for ph in (bench or {}).get("phases", []):
            if ph.get("quality") == "HIGH" and not ph.get("without") and not ph.get("rain") and ph.get("indexed", True):
                raw = ph.get("passes")
                break
    if isinstance(raw, dict):
        return [{"pass": k, "gpu_ms": v} for k, v in raw.items()]
    return [p for p in raw if isinstance(p, dict)] if isinstance(raw, list) else []


def _pending(ws, row, text):
    put(ws, row, 1, text, fill=PENDING_FILL, font=BOLD)
    ws.merge_cells(start_row=row, start_column=1, end_row=row, end_column=8)
    ws.row_dimensions[row].height = 32
    return row + 1


def _group(p):
    """Phases are compared only with the unmodified phase of the same group (quality, indexed or not)."""
    return str(p.get("quality", "-")) + ("" if p.get("indexed", True) else " non-indexed")


def _change(p):
    if p.get("without"):
        return "without " + p["without"]
    if p.get("rain"):
        return "with rain"
    return NO_FEATURE


def _ablation(ws, phases, top):
    """Table A: every phase; the baseline is the unique unmodified phase of the same group, so
    cost(without X) = baseline - frame_ms and cost(with X) = frame_ms - baseline."""
    section(ws, top - 1, 1, "A. Per-feature cost by ablation: cost(X) = |frame_ms(group) - frame_ms(group with/without X)|")
    header(ws, top, ["Configuration", "Group", "Change", "frame_ms (median)", "fps", "Baseline frame_ms (same group)",
                     "Feature cost (ms)", "Share of baseline frame", "Noise: interquartile range of rounds (ms)",
                     "Resolved above noise?"] + [f"Round {k + 1} (ms)" for k in range(5)], height=40)
    first, last = top + 1, top + len(phases)
    q_rng, w_rng, f_rng = (f"${c}${first}:${c}${last}" for c in "BCD")
    for i, p in enumerate(phases):
        r = first + i
        change = _change(p)
        put(ws, r, 1, phase_label(p, i), font=BOLD)
        put(ws, r, 2, _group(p), fill=INPUT_FILL)
        put(ws, r, 3, change, fill=INPUT_FILL)
        put(ws, r, 4, p.get("frame_ms", "n/a"), FMT_MS, INPUT_FILL)
        put(ws, r, 5, p.get("fps", "n/a"), "0.0", INPUT_FILL)
        rounds = p.get("frame_ms_rounds")
        if isinstance(rounds, list) and rounds:
            for k, v in enumerate(rounds[:5]):
                put(ws, r, 11 + k, v, FMT_MS, INPUT_FILL)
            put(ws, r, 9, f"=QUARTILE(K{r}:O{r},3)-QUARTILE(K{r}:O{r},1)", FMT_MS)
        else:
            put(ws, r, 9, "n/a")
        if change == NO_FEATURE:
            for c in (6, 7, 8):
                put(ws, r, c, "baseline" if c == 6 else "-")
            continue
        n_base = f'COUNTIFS({q_rng},B{r},{w_rng},"{NO_FEATURE}")'
        base = f'AVERAGEIFS({f_rng},{q_rng},B{r},{w_rng},"{NO_FEATURE}")'
        put(ws, r, 6, f'=IF({n_base}=1,{base},"no unique baseline")', FMT_MS)
        put(ws, r, 7, f'=IF(ISNUMBER(F{r}),IF(LEFT(C{r},4)="with",D{r}-F{r},F{r}-D{r}),"n/a")', FMT_MS)
        put(ws, r, 8, f'=IF(ISNUMBER(F{r}),G{r}/F{r},"n/a")', FMT_PCT)
        # Noise: the larger interquartile range (this phase or its baseline); the cost must exceed it.
        base_spread = f'AVERAGEIFS($I${first}:$I${last},{q_rng},B{r},{w_rng},"{NO_FEATURE}")'
        put(ws, r, 10, f'=IF(AND(ISNUMBER(G{r}),ISNUMBER(I{r})),IF(ABS(G{r})>MAX(I{r},{base_spread}),'
                       f'"resolved","below noise"),"n/a")', font=BOLD)
    ablated = [first + i for i, p in enumerate(phases) if _change(p) != NO_FEATURE]
    if ablated:
        ch = BarChart()
        ch.type, ch.title = "bar", "Frame-time cost of each feature (ms, ablation)"
        ch.add_data(Reference(ws, min_col=7, min_row=top, max_row=last), titles_from_data=True)
        ch.set_categories(Reference(ws, min_col=3, min_row=first, max_row=last))
        ch.legend = None
        ch.height, ch.width = 8, 18
        ws.add_chart(style_chart(ch), f"J{top}")
    return last + 3


def _pass_table(ws, passes, top):
    section(ws, top - 1, 1, "B. GPU pass timings, HIGH preset (timer queries) - APPROXIMATE: passes overlap on "
                            "this integrated GPU, so they do not add up exactly to the frame time")
    header(ws, top, ["Pass", "GPU time (ms, approx.)", "Share of summed pass time", "Cumulative (ms)"])
    first, last = top + 1, top + len(passes)
    for i, p in enumerate(passes):
        r = first + i
        ms = p.get("gpu_ms", p.get("ms"))
        put(ws, r, 1, str(p.get("pass", p.get("name", f"pass {i + 1}"))), font=BOLD)
        put(ws, r, 2, ms if isinstance(ms, (int, float)) else "n/a", FMT_MS, INPUT_FILL)
        put(ws, r, 3, f"=IF(ISNUMBER(B{r}),B{r}/SUM($B${first}:$B${last}),\"n/a\")", FMT_PCT)
        put(ws, r, 4, f"=SUM($B${first}:B{r})", FMT_MS)
    t = last + 1
    put(ws, t, 1, "Sum of passes", fill=TOTAL_FILL, font=BOLD)
    put(ws, t, 2, f"=SUM(B{first}:B{last})", FMT_MS, TOTAL_FILL, BOLD)
    ch = BarChart()
    ch.type, ch.title = "bar", "GPU time per pass (ms, approximate)"
    ch.add_data(Reference(ws, min_col=2, min_row=top, max_row=last), titles_from_data=True)
    ch.set_categories(Reference(ws, min_col=1, min_row=first, max_row=last))
    ch.legend = None
    ch.height, ch.width = 8, 18
    ws.add_chart(style_chart(ch), f"J{top}")
    return t + 3


def build_pipeline(wb, ctx):
    ws = wb[S_PIPE]
    bench = ctx["bench"] or {}
    phases = [p for p in bench.get("phases", []) if isinstance(p, dict)]
    ablation = [p for p in phases if "quality" in p or "without" in p]
    passes = _passes(bench)
    title(ws, "9  Render Pipeline",
          "Primary: the cost of each rendering feature measured by ablation (ThermalSense.exe --benchmark renders "
          "the same flight with the feature switched off; cost = frame_ms(HIGH) - frame_ms(HIGH without X)). "
          "Secondary: GPU timer-query time per pass, labelled approximate. All values come from "
          "build/benchmark.json; baselines, costs and shares are formulas.", span=8)
    row = 5
    if any(p.get("without") for p in ablation):
        row = _ablation(ws, ablation, row)
    else:
        row = _pending(ws, row - 1, "A. Per-feature cost: PENDING MEASUREMENT - build/benchmark.json has no "
                                    "quality / without (ablation) phases yet; rerun ThermalSense.exe --benchmark.") + 2
    if passes:
        row = _pass_table(ws, passes, row + 1)
    else:
        row = _pending(ws, row, "B. GPU pass timings: PENDING MEASUREMENT - build/benchmark.json has no "
                                "\"passes\" key yet; rerun ThermalSense.exe --benchmark. No numbers are shown.")
        section(ws, row + 1, 1, "Passes expected once measured (docs/PLAN_V2.md, Phase A) - text only")
        for i, name in enumerate(PLANNED_PASSES):
            put(ws, row + 2 + i, 1, name)
            put(ws, row + 2 + i, 2, "pending", fill=PENDING_FILL)
    ctx["refs"]["pipeline_pending"] = not passes or not any(p.get("without") for p in ablation)
    widths(ws, [34, 12, 18, 11, 9, 16, 14, 14])
    finish(ws, 4)
