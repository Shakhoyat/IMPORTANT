"""Workbook sheet of v9 M6 (asset vegetation): 35 v9 Vegetation. (M7 terrain & hills was not built; sheet 36 is reserved.)

    import wb_v9_m6
    wb_v9_m6.build_v9_m6(wb, ctx)          # after the sheets in wb_v9_m6.SHEETS exist (build_workbook creates them)

Sources (written by the program, its tests or the baker; nothing typed by hand except wording):
  assets/bake_summary.json                      tools/bake_assets: source -> baked triangles, card keep / growth, bytes
  assets/LICENSES.md                            asset / library license table (parsed)
  docs/v9_notes/m6/vegetation_assets.json       renderer: per variant LOD 0/1/2 triangles, solid error, impostor atlas
  docs/v9_notes/m6/bench_shots_m6_*.json        --bench-shots: GPU passes (vegetation, grass, terrain), shimmer metric
  docs/v9_notes/m6/night_compare.json           night parity vs v8 (mean luma of the grass region, SSIM)
  build/test_results.json                       T370-T379 (via sheet 10)
Blue = imported, white = Excel formula, amber = design-note text. Missing inputs degrade to "pending: <what>" rows.
"""
import json
import os
import re

from openpyxl.formatting.rule import CellIsRule

from wb_common import BOLD, INPUT_FILL, NOTE_FILL, PASS_FILL, PENDING_FILL, TOTAL_FILL, finish, header, path, put, section, title, widths
from wb_v6 import pending
from wb_v9 import _images, _kpi, tests_block

S_V9M6 = "35 v9 Vegetation"
SHEETS = [S_V9M6]
INFO = {
    S_V9M6: "v9 M6: CC0 glTF vegetation (cgltf, stb_image), LOD chains (meshoptimizer, card thinning), octahedral impostors, "
            "alpha-to-coverage, hierarchical wind, textured grass, shimmer metric, night parity; tests T370-T375",
}
VEG_GPU_BUDGET_MS = 3.0      # acceptance: vegetation GPU time at 1080p EPIC (labelled setting)
SHIMMER_LIMIT = 1.5          # acceptance: mean |frame-to-frame luma difference| on vegetation, /255


def _load(rel):
    p = path(rel)
    if not os.path.exists(p):
        return None
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def _bench_files(prefix):
    d = path(prefix)
    if not os.path.isdir(d):
        return []
    return sorted(os.path.join(prefix, f) for f in os.listdir(d) if re.match(r"bench_shots_m[67].*\.json$", f))


def _licenses():
    p = path("assets/LICENSES.md")
    rows = []
    if not os.path.exists(p):
        return rows
    with open(p, encoding="utf-8") as f:
        for line in f:
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) == 5 and cells[0] not in ("Asset", "---") and not cells[0].startswith("---"):
                rows.append(cells)
    return rows


def _concepts(ws, r, items):
    section(ws, r, 1, "Concepts")
    r += 1
    header(ws, r, ["Technique", "What it does here", "Where"], height=22)
    for i, (name, text, where) in enumerate(items):
        q = r + 1 + i
        put(ws, q, 1, name, font=BOLD)
        put(ws, q, 2, text, fill=NOTE_FILL)
        put(ws, q, 3, where)
        ws.row_dimensions[q].height = 62
    return r + len(items) + 2


M6_CONCEPTS = [
    ("GPU instancing", "One glDrawElementsInstanced per (variant, LOD, material): the per-tree data (position, yaw, scale, tint "
     "seed, LOD dither) is a vertex attribute with divisor 1. Instances sit in 16 m culling cells: frustum test per cell, then "
     "per instance; ~700 trees, bushes, rocks and ferns cost ~60-90 draw calls.", "VegetationRenderer.cpp"),
    ("LOD chain: meshoptimizer", "Solid parts (trunks, branches, rocks): meshopt_simplifyWithAttributes collapses edges in order "
     "of the quadric error (position + UV + normal), stopping at a triangle target or an error bound relative to the mesh "
     "extent; the achieved error is reported in metres (table below, T372).", "assets/FoliageLod.cpp"),
    ("LOD chain: card thinning", "Foliage is thousands of alpha-tested cards (fir_tree_01: 812 k twig cards per tree). Edge "
     "collapse cannot remove a card, so a level keeps a deterministic random share of the cards (connected components) and "
     "grows each about its anchor by keep^-e; e = 0.5 keeps the total card area (T371), e > 0.5 fills the crown out.",
     "assets/FoliageLod.cpp, tools/bake_assets.cpp"),
    ("Octahedral impostors", "Beyond ~150 px a tree is one camera-facing quad. At start-up 8 x 8 pictures (albedo + coverage, "
     "normal + occlusion) are rendered from directions spread evenly over the upper hemisphere by a hemi-octahedral map; the "
     "view direction picks the 4 nearest pictures (bilinear weights) and each is re-projected onto its own image plane, so "
     "orbiting the tree blends smoothly (T374). Lit at run time with the stored normals.", "VegetationRenderer.cpp, assets/Impostor.cpp"),
    ("Alpha-to-coverage", "On the 4x MSAA scene target the card's alpha decides how many of the 4 samples it covers: thin "
     "needles and blade edges become partial coverage instead of the binary alpha test's stair-steps; mips are coverage "
     "preserving (Castano) so distant foliage keeps its density (T373). Shadow and depth passes alpha-test the same textures.",
     "ShadersVegetation.cpp, Grass.cpp, assets/Image.cpp"),
    ("Hierarchical wind", "Vertex shader, three levels with their own frequencies: trunk sway (cantilever bend ~ height^2, slow "
     "gusts travelling across the forest), branch swing (weight = distance from the trunk axis), leaf / needle flutter (weight = "
     "distance from the card anchor, random phase per card). Weights are baked into COLOR_0.", "ShadersVegetation.cpp"),
    ("Two-sided foliage + translucency", "The card normal is flipped to the viewer; wrapped diffuse on the lit side and "
     "transmission (light through thin leaves) when the sun / moon is behind the card; baked crown occlusion darkens the inside.",
     "ShadersVegetation.cpp"),
    ("Shimmer metric", "Frozen scene (no wind, no grain), the TAA jitter running: mean |luma(frame n) - luma(frame n-1)| over "
     "24 frames, on the pixels that change when vegetation is switched off. Measures temporal aliasing.", "app/Shimmer.cpp"),
]



def build_v9_m6_sheet(wb, ctx):
    ws = wb[S_V9M6]
    title(ws, "35  v9 M6: Vegetation at Game Quality (CC0 assets, LOD, impostors, alpha-to-coverage, wind)",
          "The camp's cone pines, sphere-cluster trees and bushes are replaced (ULTRA / EPIC) by CC0 Poly Haven glTF models, loaded "
          "with cgltf and stb_image. The scene graph keeps the v8 primitive trees as the gameplay model (occluders, walker "
          "collision) and as the LOW / HIGH fallback (the 'built from primitives' comparison). Grass blades use the CC0 blade "
          "atlas with alpha-to-coverage.", span=14)
    r = 4
    summary = _load("assets/bake_summary.json")
    section(ws, r, 1, "Offline bake (tools/bake_assets.cpp): source -> game-ready asset")
    r += 1
    header(ws, r, ["Asset", "Variants", "Source triangles", "Baked triangles", "Reduction", "Cards kept", "Card growth",
                   "Mesh MB", "Texture MB"], height=30)
    a = r + 1
    if not summary:
        r = pending(ws, a, "assets/bake_summary.json (make bake-assets)", span=9) + 1
        b = a
    else:
        for i, m in enumerate(summary["models"]):
            q = a + i
            put(ws, q, 1, m["name"], font=BOLD)
            put(ws, q, 2, m["variants"], "0", INPUT_FILL)
            put(ws, q, 3, m["source_triangles"], "#,##0", INPUT_FILL)
            put(ws, q, 4, m["baked_triangles"], "#,##0", INPUT_FILL)
            put(ws, q, 5, f"=C{q}/D{q}", "0.0x")
            put(ws, q, 6, m["card_keep"], "0.000", INPUT_FILL)
            put(ws, q, 7, m["card_growth"], "0.00", INPUT_FILL)
            put(ws, q, 8, m["bin_bytes"] / 1e6, "0.0", INPUT_FILL)
            put(ws, q, 9, m["texture_bytes"] / 1e6, "0.0", INPUT_FILL)
        b = a + len(summary["models"]) - 1
        q = b + 1
        put(ws, q, 1, "Total", font=BOLD, fill=TOTAL_FILL)
        for c, L in ((3, "C"), (4, "D"), (8, "H"), (9, "I")):
            put(ws, q, c, f"=SUM({L}{a}:{L}{b})", "#,##0" if c < 8 else "0.0", font=BOLD)
        r = q + 2

    veg = _load("docs/v9_notes/m6/vegetation_assets.json")
    section(ws, r, 1, "Run-time LOD chain per variant (meshoptimizer + card thinning) and impostor atlas")
    r += 1
    header(ws, r, ["Asset", "Variant", "Height m", "LOD0 tris", "LOD1 tris", "LOD2 tris", "LOD2 / LOD0", "Solid error LOD1 m",
                   "Solid error LOD2 m", "Card growth LOD2", "Impostor"], height=30)
    a2 = r + 1
    if not veg:
        r = pending(ws, a2, "docs/v9_notes/m6/vegetation_assets.json", span=11) + 1
    else:
        for i, v in enumerate(veg["variants"]):
            q = a2 + i
            put(ws, q, 1, v["asset"], font=BOLD)
            put(ws, q, 2, v["variant"])
            put(ws, q, 3, v["height_m"], "0.00", INPUT_FILL)
            for k in range(3):
                put(ws, q, 4 + k, v["triangles"][k], "#,##0", INPUT_FILL)
            put(ws, q, 7, f"=F{q}/D{q}", "0.0%")
            put(ws, q, 8, v["solid_error_m"][1], "0.0000", INPUT_FILL)
            put(ws, q, 9, v["solid_error_m"][2], "0.0000", INPUT_FILL)
            put(ws, q, 10, v["card_scale"][2], "0.00", INPUT_FILL)
            put(ws, q, 11, "yes" if v["impostor"] else "no", fill=INPUT_FILL)
        r = a2 + len(veg["variants"]) + 1
        imp = veg["impostor"]
        put(ws, r, 1, "Impostor atlas", font=BOLD, fill=TOTAL_FILL)
        put(ws, r, 2, f"{imp['grid']} x {imp['grid']} views x {imp['cell_px']} px = {imp['atlas_px']} px square, albedo + normal "
                      f"(RGBA8, mips), rendered at {imp['supersample']}x and down-sampled", fill=INPUT_FILL)
        put(ws, r + 1, 1, "LOD switch (projected px)", font=BOLD, fill=TOTAL_FILL)
        put(ws, r + 1, 2, " / ".join(str(int(x)) for x in veg["lod_pixels"]) + " px tall: LOD0 -> 1 -> 2 -> impostor (+-12 % "
                          "dithered cross-fade)", fill=INPUT_FILL)
        put(ws, r + 2, 1, "Start-up load + bake s", font=BOLD, fill=TOTAL_FILL)
        put(ws, r + 2, 2, veg["load_seconds"], "0.00", INPUT_FILL)
        r += 4

    section(ws, r, 1, "GPU time and shimmer (--bench-shots, 1920x1080 EPIC; the GPU is shared with other work: repeated runs)")
    r += 1
    header(ws, r, ["Run", "Shot", "Time", "vegetation ms", "vegetation depth ms", "grass ms", "terrain ms", "veg + grass ms",
                   "Shimmer veg /255", "Shimmer all /255", "Veg share of frame", "Budget"], height=30)
    a3 = r + 1
    q = a3
    for rel in _bench_files("docs/v9_notes/m6"):
        bench = _load(rel)
        for x in (bench or {}).get("shots", []):
            p = x.get("passes", {})
            put(ws, q, 1, os.path.basename(rel), font=BOLD)
            put(ws, q, 2, x["shot"])
            put(ws, q, 3, x["time"])
            put(ws, q, 4, p.get("vegetation", 0.0), "0.00", INPUT_FILL)
            put(ws, q, 5, p.get("vegetation depth", 0.0), "0.00", INPUT_FILL)
            put(ws, q, 6, p.get("grass", 0.0), "0.00", INPUT_FILL)
            put(ws, q, 7, p.get("terrain", 0.0), "0.00", INPUT_FILL)
            put(ws, q, 8, f"=D{q}+F{q}", "0.00")
            put(ws, q, 9, x.get("shimmer_vegetation"), "0.00", INPUT_FILL)
            put(ws, q, 10, x.get("shimmer_all"), "0.00", INPUT_FILL)
            put(ws, q, 11, x.get("vegetation_fraction"), "0.0%", INPUT_FILL)
            put(ws, q, 12, f'=IF(D{q}<={VEG_GPU_BUDGET_MS},"MEETS","OVER")', font=BOLD)
            q += 1
    if q == a3:
        r = pending(ws, a3, "docs/v9_notes/m6/bench_shots_m6_*.json", span=12) + 1
    else:
        b3 = q - 1
        ws.conditional_formatting.add(f"L{a3}:L{b3}", CellIsRule(operator="equal", formula=['"MEETS"'], fill=PASS_FILL))
        ws.conditional_formatting.add(f"L{a3}:L{b3}", CellIsRule(operator="equal", formula=['"OVER"'], fill=PENDING_FILL))
        r = b3 + 2
        _kpi(ws, [("Max vegetation ms", f"=MAX(D{a3}:D{b3})", "0.00"), ("Mean veg + grass ms", f"=AVERAGE(H{a3}:H{b3})", "0.00"),
                  ("Mean shimmer veg", f"=AVERAGE(I{a3}:I{b3})", "0.00"), ("Shimmer limit", SHIMMER_LIMIT, "0.0")], row=3)

    night = _load("docs/v9_notes/m6/night_compare.json")
    section(ws, r, 1, "Night parity: the grass must not read darker than v8 (mean 8-bit luma, 1280x720)")
    r += 1
    header(ws, r, ["Shot", "Region", "v8", "M1", "M6", "M6 / v8", "SSIM M6 vs v8", "Verdict"], height=22)
    if not night:
        r = pending(ws, r + 1, "docs/v9_notes/m6/night_compare.json", span=8) + 2
    else:
        for i, row in enumerate(night["rows"]):
            q = r + 1 + i
            put(ws, q, 1, row["shot"], font=BOLD)
            put(ws, q, 2, row["region"])
            put(ws, q, 3, row["v8"], "0.00", INPUT_FILL)
            put(ws, q, 4, row.get("m1"), "0.00", INPUT_FILL)
            put(ws, q, 5, row["m6"], "0.00", INPUT_FILL)
            put(ws, q, 6, f"=E{q}/C{q}", "0.0%")
            put(ws, q, 7, row.get("ssim"), "0.000", INPUT_FILL)
            put(ws, q, 8, f'=IF(E{q}>=0.98*C{q},"NOT DARKER","DARKER")', font=BOLD)
        r += len(night["rows"]) + 3

    r = _concepts(ws, r, M6_CONCEPTS)
    section(ws, r, 1, "Assets and libraries (assets/LICENSES.md)")
    r += 1
    header(ws, r, ["Asset / library", "Used for", "Author(s)", "Source", "License"], height=22)
    lic = _licenses()
    for i, row in enumerate(lic):
        for k, v in enumerate(row):
            put(ws, r + 1 + i, 1 + k, v, fill=INPUT_FILL if k else None, font=BOLD if k == 0 else None)
    r += len(lic) + 3
    r, _, _ = tests_block(ws, r, ctx, 370, 375, "M6 tests (T370-T375)")
    section(ws, r, 1, "Captures: before (v8 primitives) / after (CC0 assets)")
    r = _images(ws, r + 1, [
        ("Before: camp overview (cone pines, sphere trees)", "docs/v9_notes/audit/shots/day_overview.jpg"),
        ("After: camp overview (CC0 firs, impostors far)", "docs/v9_notes/m6/day_overview.jpg"),
        ("After: camp fir close-up (branch structure)", "docs/v9_notes/m6/m6_tree_close.jpg"),
        ("After: treeline firs / pines", "docs/v9_notes/m6/m6_treeline.jpg"),
        ("After: broadleaf tree", "docs/v9_notes/m6/m6_broadleaf_close.jpg"),
        ("After: bushes, ferns, rocks", "docs/v9_notes/m6/m6_bush_close.jpg"),
        ("After: textured grass blades", "docs/v9_notes/m6/m6_grass_close.jpg"),
        ("Before: third person", "docs/v9_notes/audit/shots/play_third_person.jpg"),
        ("After: third person", "docs/v9_notes/m6/play_third_person.jpg"),
        ("v8: night forest", "docs/v9_notes/audit/shots/walk_forest.jpg"),
        ("M6: night forest", "docs/v9_notes/m6/walk_forest.jpg"),
        ("v8: night meadow", "docs/v9_notes/m6/v8_walk_meadow.jpg"),
        ("M6: night meadow", "docs/v9_notes/m6/walk_meadow.jpg"),
    ])
    widths(ws, [28, 22, 14, 14, 14, 14, 14, 14, 14, 14, 12, 10])
    finish(ws, 5, freeze="A4")



def build_v9_m6(wb, ctx):
    """Sheet 35; created here if build_workbook has not created it from SHEETS / INFO."""
    for name in SHEETS:
        if name not in wb.sheetnames:
            wb.create_sheet(name)
    build_v9_m6_sheet(wb, ctx)
