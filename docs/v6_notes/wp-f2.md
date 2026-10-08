# WP-F2 notes (workbook sheets 21-23)
Done: tools/wb_v6.py (sheets 21 Day Cycle & Lighting, 22 Soldiers & Routes, 23 CV Noise & Seeds), wired in wb_common
(SHEET_ORDER), build_workbook (+ `--scene FILE` for dry runs, computed possessable count in the index), wb_v4 (DESIGN_NOTES
"SOLDIER-" and "SKY / TIME"), wb_audit (U6-1..8, strict: Pending until all listed tests exist and pass), wb_trace (V6-01..07),
verify_workbook (sheet 21 formula-vs-export check, sheet 23 no failing v6 tests), xlsx_snapshot (sheet_daycycle/soldiers/cvnoise).
Sheet 3 is driven by the exported components (no hard-coded object list): soldiers appear automatically.
Every block writes "pending: <what>" when its data is missing. Sheet 23 reads build/figdata_out/cv_noise.json (WP-D layout).
Pending until WP-E export (v6 key) lands: sheet 21 grid/formulas/chart, sheet 22 all tables, sheet 23 sensor/seeds. v6 tests
T190-T239 pending until test_results.json is regenerated. Audit U6-8 (video) stays Pending until a v6 demo exists.
Dry-run with fake v6 data passed the formulas engine (97/97 sun-elevation rows OK).
Rebuild: uv run --with openpyxl --with pillow --with pypdf python tools/build_workbook.py
Verify:  uv run --with openpyxl --with formulas --with pillow --with pypdf python tools/verify_workbook.py
Snapshots (after export lands): uv run --with openpyxl --with pillow python tools/xlsx_snapshot.py --only sheet_
Assumptions: clouds tests = T198-T199; roster "off duty" strings start with "off" (day/night duty counts).
