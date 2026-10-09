"""Builds ThermalSense_Graphics_Documentation.xlsx (14 from v3, 14-17 from v4, 18 Computer Vision, 19 Squad & Mission, 20 Audio & HUD, 21-23 v6, 24-27 v7, 28 v8, 29+ v9) from files produced by the program itself.

Inputs (regenerate them first with `mingw32-make docs`):
  build/scene_export.json   build/export_docs.exe (or ThermalSense.exe --export-docs): primitives, components,
                            S/R/T, local + world matrices, materials, lights, vertex samples
  build/test_results.json   build/tests.exe                  (automated tests)
  build/benchmark.json      ThermalSense.exe --benchmark     (measured FPS, optional per-pass GPU ms "passes")
  build/trace.csv           ThermalSense.exe --trace         (UAV / rover behaviour)
  docs/visual_tests.json    observations of the captured screenshots

No number is typed by hand: every value is imported from those files or computed by an Excel formula over them
(explanatory text, the 0/1 entries of the matrix templates, and a few labelled settings such as tolerances and
chart resolution are the only authored content). Verify with `python tools/verify_workbook.py`.
"""
import os
import sys
from datetime import date, datetime

from openpyxl import Workbook
from openpyxl.styles import Alignment, Font, PatternFill

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from summarize_trace import summarize  # noqa: E402
from wb_common import (S_CVN, S_DAY, S_SOLD, S_RT, S_FX, S_GAME, S_V7T, AUTHOR, BOLD, DARK, S_AUDIO, S_AUDIT, S_BASE, S_COMBAT, S_CTRL, S_CV, S_SQUAD, FORMULA_FILL, INPUT_FILL, OUT, PRIM_COLOURS, ROLL, SHEET_ORDER,  # noqa: E402
                       SUBTITLE_FONT, S_BEHAV, S_COVER, S_GEOM, S_HIER, S_INV, S_LIGHT, S_MAT, S_OBJ, S_PERF, S_PIPE,
                       S_SHADE, S_TEST, S_TRACE, S_VERT, image, link, load_json, path, prim_fill, put, section,
                       widths)
from wb_geometry import build_geometry, build_inventory, build_object_construction, build_vertex_data  # noqa: E402
from wb_lighting import build_lighting, build_shading  # noqa: E402
from wb_pipeline import build_pipeline  # noqa: E402
from wb_results import build_behaviour, build_performance, build_testing  # noqa: E402
from wb_trace import build_traceability  # noqa: E402
from wb_v4 import build_bunker_base, build_combat, build_controllables  # noqa: E402
from wb_audit import build_audit  # noqa: E402
from wb_v6 import build_v6  # noqa: E402
from wb_v7 import build_v7  # noqa: E402
from wb_v9 import V9_INFO, V9_SHEETS, build_v9  # noqa: E402
import wb_v9_m2  # noqa: E402
import wb_v9_m3  # noqa: E402
import wb_v9_m5  # noqa: E402

# v9 milestone sheets (31+), one module each: (sheet name, cover-page line, builder)
V9_MILESTONES = [
    (wb_v9_m2.S_V9M2, wb_v9_m2.INFO, wb_v9_m2.build_v9_m2),
    (wb_v9_m3.S_V9M3, wb_v9_m3.V9M3_INFO, wb_v9_m3.build_v9_m3),
    (wb_v9_m5.S_V9M5, wb_v9_m5.V9M5_INFO, wb_v9_m5.build_v9_m5),
]
from wb_v5 import build_audio_hud, build_cv, build_squad  # noqa: E402
from wb_transforms import build_hierarchy, build_matrices  # noqa: E402

INPUT_FILES = ["build/scene_export.json", "build/test_results.json", "build/benchmark.json", "build/trace.csv",
               "docs/visual_tests.json"]
SHEET_INFO = {
    S_COVER: "This page: author, KPIs, index",
    S_INV: "The GPU meshes (cube, sphere, cylinder, frustum, terrain...) with counts, instances and thumbnails",
    S_VERT: "Cube's 24 vertices + 36 indices, sphere/cylinder samples, CCW test, Technique 1 vs 2 (formulas)",
    S_OBJ: "Every component: primitive, parent, S / R / T, shading, material; COUNTIFS per object",
    S_MAT: "M1, M2, ... for every component: T, R = Ry Rx Rz, S -> M = T R S by formulas; equals the code's matrix",
    S_HIER: "Parent -> child tree; world = parent_world x M rebuilt in Excel; searchlight chain highlighted",
    S_GEOM: "Memory indexed vs non-indexed per mesh (formulas) + charts",
    S_LIGHT: "Moon, beacon, searchlight parameters; attenuation and cone fall-off curves + charts",
    S_SHADE: "Flat / Gouraud / Phong / PBR: where used, how, why + screenshots",
    S_PIPE: "Per-feature frame cost by ablation + approximate GPU pass times (pending until measured)",
    S_TEST: "All tests with pass-rate KPI, conditional formatting and evidence links",
    S_PERF: "Measured FPS / frame time / submitted geometry + charts",
    S_BEHAV: "UAV tracking vs hiding rover: detection %, tracking error, hide events + time series",
    S_TRACE: "Requirement -> file -> sheet -> test -> report section (R01-R26 + V4-01..14)",
    S_CTRL: "Possessable objects (computed count) + animated scenery: DOF, joints, technique, limits, parts, cameras (exported + design notes)",
    S_BASE: "Underground bunker and base apron: sections, site plan, walk-space floors / walls / indoor volumes, practical lights",
    S_COMBAT: "Weapons, guidance, fuse, damage radius, measured particle census, wreck / respawn timings, IFF + keep-out rules",
    S_AUDIT: "Requirement-by-requirement checklist: lab sheet, proposal, user v2-v5 requests, course checklist",
    S_CV: "v5 vision: CV pipeline stages, parameters, tracker (Kalman / Hungarian / triangulation), tests T130-T137",
    S_SQUAD: "Two-UAV squad roles and strike rules, time-on-target formula, mission waves / scoring, armour, measured runs",
    S_AUDIO: "Procedural synths, mixer, game events, HUD elements, tests T160-T178",
    S_DAY: "v6 day cycle: sun, key light, ambient, fog, exposure, solar, beams, waves, cloud shadow; sun elevation recomputed by formula + chart",
    S_SOLD: "v6 garrison: soldier roster (day / night duty), primitives, joints, gait constants, route lengths",
    S_CVN: "v6 sensor noise config, noise sweep, derived seeds, tests T190-T239 (status from sheet 10)",
    S_RT: "v7 ray tracing: 5 modes, measured GPU timings, ray tree, Russian roulette, NEE lights, a-trous kernel, SAH BVH",
    S_FX: "v7 Frame Explorer: the 16 stages + Excel-recomputed raster, kernel, Halton and tone-curve examples",
    S_GAME: "v7 game camera: key bindings, every POV source (possessable / ride-along), spring blend recomputed",
    S_V7T: "v7 tests T240-T309 by work package (status from sheet 10)",
}
SHEET_INFO.update(V9_INFO)
SHEET_INFO.update({name: info for name, info, _ in V9_MILESTONES})
SHEET_ORDER = SHEET_ORDER + [s for s in V9_SHEETS + [m[0] for m in V9_MILESTONES] if s not in SHEET_ORDER]


def build_cover(wb, ctx):
    ws = wb[S_COVER]
    refs = ctx["refs"]
    ws.sheet_view.showGridLines = False
    ws["A1"] = "ThermalSense - Computer Graphics Lab Documentation"
    ws["A1"].font = Font(bold=True, size=22, color=DARK)
    ws["A2"] = ("Graphics Lab 04: procedural primitives (cube, sphere, cylinder), indexed rendering, hierarchical "
                "modelling with M = T x R x S, lighting and shading. Generated from the program's own exports.")
    ws["A2"].font = SUBTITLE_FONT
    info = [("Author", AUTHOR), ("Roll", ROLL), ("Generated", date.today().isoformat()),
            ("Regenerate", "mingw32-make docs   (build/export_docs.exe -> tools/build_workbook.py)"),
            ("Verify", "python tools/verify_workbook.py   (recalculates every formula, checks for errors)")]
    for i, (k, v) in enumerate(info):
        put(ws, 4 + i, 1, k, font=BOLD, border=False)
        put(ws, 4 + i, 2, v, border=False, align=Alignment(vertical="top"))
        ws.merge_cells(start_row=4 + i, start_column=2, end_row=4 + i, end_column=7)
    tiles = [("Scene parts", f"={refs['parts']}", "0", "leaf nodes (sheet 3)"),
             ("GPU meshes", f"={refs['mesh_count']}", "0", "shared by all parts (sheet 1)"),
             ("Triangles / frame", f"={refs['tris_frame']}", "#,##0", "scene geometry (sheet 6)"),
             ("Tests passed", f"={refs['tests_passed']}&\" / \"&{refs['tests_total']}", None, "sheet 10"),
             ("Pass rate", f"={refs['pass_rate']}", "0.0%", "sheet 10"),
             ("Best FPS", f"={refs['fps']}" if "fps" in refs else "pending", "0.0", "measured (sheet 11)"),
             ("max |Excel - code|", f"=MAX({refs['mat_err']},{refs['world_err']})", "0.0E+00",
              "matrices, sheets 4 + 5")]
    tile_fill = PatternFill("solid", fgColor=DARK)
    for k, (label, f, fmt, note) in enumerate(tiles):
        c = 1 + k
        a = ws.cell(row=10, column=c, value=label)
        a.font, a.fill, a.alignment = Font(bold=True, color="D1D5DB", size=9), tile_fill, Alignment(horizontal="center")
        v = ws.cell(row=11, column=c, value=f)
        v.font, v.fill = Font(bold=True, color="FFFFFF", size=18), tile_fill
        v.alignment = Alignment(horizontal="center", vertical="center")
        if fmt:
            v.number_format = fmt
        n = ws.cell(row=12, column=c, value=note)
        n.font, n.fill, n.alignment = Font(italic=True, color="9CA3AF", size=8), tile_fill, Alignment(
            horizontal="center", wrap_text=True)
    ws.row_dimensions[11].height = 36
    ws.row_dimensions[12].height = 24
    tiles2 = [("Possessable objects", f"={refs['ctrl_possessable']}" if "ctrl_possessable" in refs else "n/a", "0", "sheet 14"),
              ("Practical lights", f"={refs['practicals']}" if "practicals" in refs else "n/a", "0", "sheet 15"),
              ("Walk floors / walls", (f"={refs['walk_floors']}&\" / \"&{refs['walk_walls']}" if "walk_floors" in refs else "n/a"),
               None, "walk space, sheet 15"),
              ("Particle types", f"={refs['particle_types']}" if "particle_types" in refs else "n/a", "0", "measured, sheet 16"),
              ("Audit verified", f"={refs['audit_verified']}&\" / \"&{refs['audit_items']}" if "audit_items" in refs else "n/a", None,
               "sheet 17"),
              ("Audit pending", f"={refs['audit_pending']}" if "audit_pending" in refs else "n/a", "0", "sheet 17"),
              ("Sheets", len(SHEET_ORDER), "0", "this workbook")]
    for k, (label, f, fmt, note) in enumerate(tiles2):
        c = 1 + k
        a = ws.cell(row=13, column=c, value=label)
        a.font, a.fill, a.alignment = Font(bold=True, color="D1D5DB", size=9), tile_fill, Alignment(horizontal="center")
        v = ws.cell(row=14, column=c, value=f)
        v.font, v.fill = Font(bold=True, color="FFFFFF", size=18), tile_fill
        v.alignment = Alignment(horizontal="center", vertical="center")
        if fmt:
            v.number_format = fmt
        n = ws.cell(row=15, column=c, value=note)
        n.font, n.fill, n.alignment = Font(italic=True, color="9CA3AF", size=8), tile_fill, Alignment(
            horizontal="center", wrap_text=True)
    ws.row_dimensions[14].height = 36
    ws.row_dimensions[15].height = 24
    tiles3 = [("UAVs in the squad", f"={refs['uavs']}" if "uavs" in refs else "n/a", "0", "sheet 19"),
              ("Vision tests", f"={refs['cv_tests']}" if "cv_tests" in refs else "n/a", None, "T130-T137, sheet 18"),
              ("Rounds to kill", f"={refs['rounds_to_kill']}" if "rounds_to_kill" in refs else "n/a", "0", "at centre, sheet 19"),
              ("Breaches = base lost", f"={refs['breaches_to_lose']}" if "breaches_to_lose" in refs else "n/a", "0", "sheet 19"),
              ("Synthesised sounds", f"={refs['synths']}" if "synths" in refs else "n/a", "0", "sheet 20"),
              ("HUD elements", f"={refs['hud_elements']}" if "hud_elements" in refs else "n/a", "0", "sheet 20"),
              ("Game event types", f"={refs['event_types']}" if "event_types" in refs else "n/a", "0", "sheet 20")]
    for k, (label, f, fmt, note) in enumerate(tiles3):
        c = 1 + k
        a = ws.cell(row=16, column=c, value=label)
        a.font, a.fill, a.alignment = Font(bold=True, color="D1D5DB", size=9), tile_fill, Alignment(horizontal="center")
        v = ws.cell(row=17, column=c, value=f)
        v.font, v.fill = Font(bold=True, color="FFFFFF", size=18), tile_fill
        v.alignment = Alignment(horizontal="center", vertical="center")
        if fmt:
            v.number_format = fmt
        n = ws.cell(row=18, column=c, value=note)
        n.font, n.fill, n.alignment = Font(italic=True, color="9CA3AF", size=8), tile_fill, Alignment(
            horizontal="center", wrap_text=True)
    ws.row_dimensions[17].height = 36
    ws.row_dimensions[18].height = 24
    tiles4 = [("Ray-tracing modes", f"={refs['rt_modes']}" if "rt_modes" in refs else "n/a", "0", "sheet 24"),
              ("BVH primitives", f"={refs['bvh_prims']}" if "bvh_prims" in refs else "n/a", "#,##0", "SAH BVH, sheet 24"),
              ("SAH vs median cost", f"={refs['bvh_sah_gain']}" if "bvh_sah_gain" in refs else "n/a", "0.00", "x cheaper, sheet 24"),
              ("Explorer stages", f"={refs['fx_stages']}" if "fx_stages" in refs else "n/a", "0", "sheet 25"),
              ("POV sources", f"={refs['pov_sources']}" if "pov_sources" in refs else "n/a", "0", "jump-in cameras, sheet 26"),
              ("v7 tests passed", f"={refs['v7_tests_passed']}&\" / \"&{refs['v7_tests']}" if "v7_tests" in refs else "n/a", None,
               "T240-T309, sheet 27"),
              ("Camera jump blend (s)", f"={refs['rig_blend']}" if "rig_blend" in refs else "n/a", "0.00", "spring, sheet 26")]
    for k, (label, f, fmt, note) in enumerate(tiles4):
        c = 1 + k
        a = ws.cell(row=19, column=c, value=label)
        a.font, a.fill, a.alignment = Font(bold=True, color="D1D5DB", size=9), tile_fill, Alignment(horizontal="center")
        v = ws.cell(row=20, column=c, value=f)
        v.font, v.fill = Font(bold=True, color="FFFFFF", size=18), tile_fill
        v.alignment = Alignment(horizontal="center", vertical="center")
        if fmt:
            v.number_format = fmt
        n = ws.cell(row=21, column=c, value=note)
        n.font, n.fill, n.alignment = Font(italic=True, color="9CA3AF", size=8), tile_fill, Alignment(
            horizontal="center", wrap_text=True)
    ws.row_dimensions[20].height = 36
    ws.row_dimensions[21].height = 24
    section(ws, 23, 1, "Index (click a sheet name)")
    for i, name in enumerate(SHEET_ORDER):
        r = 24 + i
        put(ws, r, 1, i)
        c = put(ws, r, 2, name)
        link(c, name, "A1", name)
        ws.merge_cells(start_row=r, start_column=2, end_row=r, end_column=3)
        info = SHEET_INFO.get(name, "")
        if name == S_CTRL and "ctrl_possessable" in refs:
            info = f'={refs["ctrl_possessable"]}&" possessable objects + animated scenery: DOF, joints, technique, limits, parts, cameras (exported + design notes)"'
        put(ws, r, 4, info)
        ws.merge_cells(start_row=r, start_column=4, end_row=r, end_column=7)
    _legend(ws, 24 + len(SHEET_ORDER) + 1)
    image(ws, "I4", path("report/figures/scene_visible.png"), width=560)
    ws["I3"] = "Hero shot: report/figures/scene_visible.png (captured by ThermalSense.exe --capture)"
    ws["I3"].font = SUBTITLE_FONT
    widths(ws, [16, 16, 16, 22, 22, 22, 22, 3])
    ws.page_setup.orientation = "landscape"
    ws.page_setup.fitToWidth = 1
    ws.page_setup.fitToHeight = 1
    ws.sheet_properties.pageSetUpPr.fitToPage = True


def _legend(ws, top):
    section(ws, top, 1, "Colour key")
    plain = Alignment(vertical="top")
    put(ws, top + 1, 1, "Imported value", fill=INPUT_FILL)
    put(ws, top + 1, 2, "copied from the program's export (JSON / CSV)", border=False, align=plain)
    put(ws, top + 2, 1, "Formula", fill=FORMULA_FILL)
    put(ws, top + 2, 2, "computed by Excel from imported values", border=False, align=plain)
    for i, name in enumerate(n for n in PRIM_COLOURS if n != "Group"):
        put(ws, top + 3 + i, 1, name, fill=prim_fill(name))
    put(ws, top + 3, 2, "primitive colour code (same as the renderer's primitive diagnostic view)", border=False,
        align=plain)
    stamp = [f"{f}: {datetime.fromtimestamp(os.path.getmtime(path(f))).strftime('%Y-%m-%d %H:%M')}"
             for f in INPUT_FILES if os.path.exists(path(f))]
    r = top + 4 + len(PRIM_COLOURS)
    section(ws, r, 1, "Input files (last modified)")
    for i, s in enumerate(stamp):
        ws.cell(row=r + 1 + i, column=1, value=s)


def _arg(flag, default):
    """Optional overrides (for dry runs): --bench FILE, --out FILE."""
    argv = sys.argv[1:]
    return argv[argv.index(flag) + 1] if flag in argv and argv.index(flag) + 1 < len(argv) else default


def main():
    if _arg("--leaves", None):
        import wb_common
        wb_common.set_leaves(_arg("--leaves", None), _arg("--groups", None))
    scene = load_json(_arg("--scene", "build/scene_export.json"))
    if scene is None or "mesh_samples" not in scene:
        sys.exit("build/scene_export.json missing or old - run `mingw32-make docs` (build/export_docs.exe) first")
    trace_csv = path("build/trace.csv")
    ctx = {
        "scene": scene,
        "tests": load_json("build/test_results.json", []),
        "bench": load_json(_arg("--bench", "build/benchmark.json"), {}),
        "visual": load_json("docs/visual_tests.json", []),
        "trace_summary": summarize(trace_csv) if os.path.exists(trace_csv) else None,
        "refs": {},
    }
    wb = Workbook()
    wb.remove(wb.active)
    for name in SHEET_ORDER:
        wb.create_sheet(name)
    build_inventory(wb, ctx)
    build_vertex_data(wb, ctx)
    build_object_construction(wb, ctx)
    build_matrices(wb, ctx)
    build_hierarchy(wb, ctx)
    build_geometry(wb, ctx)
    build_lighting(wb, ctx)
    build_shading(wb, ctx)
    build_pipeline(wb, ctx)
    if ctx["trace_summary"]:
        build_behaviour(wb, ctx)
    build_testing(wb, ctx)
    build_performance(wb, ctx)
    build_controllables(wb, ctx)
    build_combat(wb, ctx)   # after sheet 14 (weapon rows read its spec table)
    build_bunker_base(wb, ctx)
    build_cv(wb, ctx)
    build_squad(wb, ctx)
    build_audio_hud(wb, ctx)
    build_v6(wb, ctx)
    build_v7(wb, ctx)
    build_v9(wb, ctx)
    for _, _, build in V9_MILESTONES:
        build(wb, ctx)
    build_audit(wb, ctx)
    build_traceability(wb, ctx)
    build_cover(wb, ctx)
    wb.active = 0
    wb.calculation.fullCalcOnLoad = True
    wb.properties.creator = AUTHOR
    wb.properties.title = "ThermalSense Graphics Documentation"
    out = _arg("--out", OUT)
    wb.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main()
