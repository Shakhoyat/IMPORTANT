# WP-W workbook v7 (agent W) - notes

Status (2026-10-08): DONE. The workbook has 28 sheets (24-27 are new), and verify_workbook gives RESULT: PASS against the current
tree (290/290 tests, including CAP's T310). Nothing is committed.

## What was added
### Exporter (src/app/DocExport.cpp only; no runtime behaviour changed)
- `schema_version` 6 -> 7. `writeV7(o, scene)` keeps `v7.keys` (keyBindingsV7, now 12 rows with the ` key) and adds:
  - `v7.raytrace`
    - modes: rtModeName / rtModeCaption.
    - trace_settings: rt::TraceSettings defaults.
    - bvh_constants: Bvh::kTraversalCost / kIntersectCost / kBins / kMaxLeaf, SceneBridge::kRebuildRatio, kHeightfieldSamples.
    - bvh: built by SceneBridge::sync on the t = 0 scene.
      - prims, nodes, leaves, depth, SAH cost, plus a median-split build for comparison.
      - shape counts, dynamic / foliage counts, the root split (boxes + primitive counts).
      - a 64 x 36 probe of primary rays (TraceStats: node visits, prim tests, hits, agreement with intersectBrute).
    - lights (RtLight list), env, and Tracer::emitterPower.
  - `v7.explorer`
    - fx::stageInfo for the 16 stages.
    - a rasterisation worked example: fx::rasterize, interpolateAffine / interpolatePerspective.
    - the 9 fx::kernel weights + kernelSum, Halton (2, 3) k = 1..8, and acesFitted / toneCurve samples.
  - `v7.pov_sources`
    - listPovSources at t = 0: 17 sources (kind, tag, control, alt view, hostile, position, distance, pick radius).
    - first-person eye + FOV from povPose(First), and the chase eye.
  - `v7.camera_rig`
    - CameraRig::kBlendTime / kOmega / arc constants and FlyCam constants.
    - springDecay, jumpLetterbox and hitMarkerAlpha sampled over 0..0.6 s.
- Clean under -Wall -Wextra (compiled into build/obj-w). The exporter takes about 15 s, most of it the brute-force probe.

### Workbook (tools/wb_v7.py, new; pattern of wb_v6.py: every block degrades to "pending: ...")
- **24 Ray Tracing**
  - Modes table with a labelled "Run shown" selector (default: WP-H final, day 16:30 = the wp-h.md timings). Formulas use
    AVERAGEIFS / INDEX-MATCH for internal resolution, scale, pixels, ms, FPS, speed-up vs raster, ns / pixel, and the mean of all runs.
  - All 4 measured runs of raytrace_perf.json: WP-H day / night in docs/v7_notes/wph_shots/, and the CAP figure runs in
    report/figures_v7/ and rt_night/.
  - Tracer settings.
  - Ray tree of one pixel (report/figures_v7/raytrace_tree.json). Formulas give the sum of children / local term per ray,
    counts per kind, max depth, and a check that the primary contribution equals the pixel luminance.
  - Russian roulette table: q = max(0.05, 1 - max beta), survivor weight beta / (1 - q), and E[weight] = a^(k+1) checked for
    every bounce.
  - NEE lights: luminance, E at d, emitter power recomputed by type and checked against Tracer::emitterPower, pick probability.
  - A-trous B3 kernel h_j = 4! / (j! (4-j)!) / 16, the 5 x 5 outer product (sum = 1), hole spacing and footprint per pass.
  - BVH: SAH vs median, a full-tree check (2L - 1), shape shares.
  - Worked SAH cost of the root split from the exported boxes.
  - Probe: BVH vs brute force (21x fewer primitive tests, 2304 / 2304 rays agree).
- **25 Frame Explorer**
  - The 16 stages: caption, formula, concepts, layer count + names, and a link to each capture. Layer names come from the
    newest explorer_stages.json (currently build/figdata_out/, with the RT reflection layers).
  - Worked edge functions / barycentrics / affine vs perspective-correct interpolation (Excel = program).
  - Kernel sums.
  - Halton radical inverse with MOD / INT.
  - ACES + sRGB tone curve with a chart.
- **26 Game Camera & Controls**
  - keyBindingsV7 rows with an owner formula (G / H / I).
  - Frame Explorer internal keys (amber, from the WP-I notes).
  - 17 POV sources with an Excel picker row (nearest first), eye height and FOV; per-kind COUNTIFS.
  - Rig constants, the spring x(t) = (1 + w t) e^(-w t) recomputed for every sample (x(T) = 0.01 check), letterbox and
    hit-marker curves, chart.
- **27 v7 Tests**
  - Every T-id >= 240 found in tests/*.cpp (71 ids, T240-T310 incl. T309b).
  - Blocks WP-G / H / I / L / CAP (T310 director cut) counted with COUNTIFS; status via INDEX / MATCH on sheet 10.
  - The work packages without tests (M, P, J, CAP, W), linked to their notes.
- Other tool changes:
  - wb_common: 4 new sheet names.
  - build_workbook: build_v7, SHEET_INFO, a 4th cover KPI row (RT modes, BVH prims, SAH gain, explorer stages, POV sources,
    v7 tests, blend time); the index moved down 3 rows.
  - verify_workbook: v7 checks (OK counts and no CHECK on 24-26, 0 failed / 0 pending on 27).
  - xlsx_snapshot: EXP, LN, LOG, MOD, INT, FACT in the evaluator, and catalogue entries for sheets 24-27; rows are found by
    section title.

## Commands
```
export PATH=$HOME/.local/bin:$PATH
make -j32 HEADLESS=1 OBJ=build/obj-w TESTS=build/tests-w APP=build/ts-w build/tests-w build/ts-w
make docs EXPORTER=build/export_docs-w TESTS=build/tests-w        # exporter + 290 tests -> build/test_results.json + build_workbook
uv run --with openpyxl --with formulas --with pillow --with pypdf python tools/verify_workbook.py
uv run --with openpyxl --with pillow python tools/xlsx_snapshot.py --only sheet_
```

## verify_workbook output (v7 lines; formulas engine, reduced workbook)
```
  formula errors: 0
  sheet 21: 97 / 97 grid rows match the Excel sun elevation, max |diff| = 5.56e-05 deg  [ok]
  sheet 22: soldiers = 8
  sheet 23: v6 tests 50 pass / 0 fail / 0 pending of 50  [ok]
  sheet 24 Ray Tracing: 35 OK checks, 0 CHECK  [ok]
  sheet 25 Frame Explorer: 51 OK checks, 0 CHECK  [ok]
  sheet 26 Game Camera & Controls: 44 OK checks, 0 CHECK  [ok]
  sheet 27 v7 Tests: v7 tests 71 pass / 0 fail / 0 pending of 71  [ok]
  sheets 24-27: 0 'pending' cells
RESULT: PASS
```

## Snapshots (report/sheets/, captions in report/sheets/captions.json)
- The three requested: sheet_raytracing.png (modes + timings), sheet_explorer.png (16 stages),
  sheet_controls.png (v7 keys + explorer keys).
- Extras: sheet_rt_bvh.png (SAH root split + probe), sheet_pov_sources.png, sheet_v7tests.png.
- All were reviewed by eye. Rows of the subtitle (row 2) are left out: the snapshot renderer draws merged subtitles tiny on
  every sheet (pre-existing, also on sheets 21-23).

## Open
- report/sheet_figs.py was NOT run, because it rewrites report/sections/gen_sheets.tex and the report .tex files were out of
  scope. Run it when the v7 report sections land: `python3 report/sheet_figs.py`. It will pick up the six new captions.
- report/gen_tables.py: the test-area table stops at T309, so the new T310 (CAP, test_app.cpp) is not covered and the script
  will warn "area table covers 289 of 290".
- The ray-tracing mean timings mix runs from before and after WP-Q's raster speed-ups (CAP night raster 13 ms vs WP-H
  141 ms). The selector shows one run at a time; re-run the CAP raytrace_perf shots after Q lands and rebuild.
- BVH stats are for the t = 0 night scene. Timings are imported from JSON; the exporter measures no time, so it stays deterministic.
