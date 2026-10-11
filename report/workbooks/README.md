# cg_workbook.xlsx (the workbook of the report)

18 sheets. Every derived number is a live Excel formula. Yellow cells are inputs taken from the code or the scene
export. White cells are formulas. Worked textbook examples are labelled as such on their sheet.

## Rebuild

```
~/.local/bin/uv run -q --with openpyxl --with matplotlib python tools/v10/build_cg_workbook.py --export report/data/scene_export_v10.json
~/.local/bin/uv run -q --with openpyxl --with numpy python tools/report/check_workbook.py   # workbook = export = code = report
~/.local/bin/uv run -q --with openpyxl python tools/report/numbers_from_workbook.py         # writes the report's number macros from the workbook
```

The report takes its numbers from this workbook: the worked tables of Part II are the sheets (`tools/v10/sheet_to_tex.py`),
the number macros of the text are the cells of `Report_Numbers` (`numbers_from_workbook.py`), and the coordinate sheets
of Section 7 are the rows of `Coordinates` (`tools/report/coord_sheet.py`). `check_workbook.py` fails if any of them differs.

The script:

1. Reads the scene export given with `--export` (written by `ThermalSense --export-docs`, GPU-free).
2. Writes `report/workbooks/cg_workbook.xlsx` with openpyxl. Every sheet is set to A4 landscape, 1 page wide.
3. Recalculates the workbook with headless LibreOffice and exports a PDF. It then reads the recalculated copy back and
   lists any formula cell that holds an error or no value. The exit code is 1 if it finds one.
4. Renders the PDF pages at 200 dpi, crops the white margins and writes `report/figures/workbook/<Sheet>.png`.
   A sheet that spans several pages also gets `<Sheet>_p2.png`, `_p3.png` and so on. The two registers
   (`Report_Numbers`, `Coordinates`) are long lists and are not rendered as pictures.

Options: `--no-snap` writes the xlsx only. `--snap-only` re-renders an existing xlsx.

LibreOffice is the extracted AppImage at `~/.local/opt/squashfs-root/opt/libreoffice25.8/program/soffice`. It needs
`libnss3` / `libssl3`. These were installed without sudo (`apt-get download libnss3 libnspr4` + `dpkg-deb -x`) into
`~/.local/opt/nss/root`. The script adds that directory to `LD_LIBRARY_PATH`. pymupdf and Pillow are pulled in by `uv`.

## Data sources per sheet

| Sheet | Source |
|---|---|
| Primitives | `src/primitives/{Cube,Sphere,Cylinder}.cpp`, `objects/Terrain.cpp` (createTerrainDisc), `rendering/Grass.cpp` (7-vertex, 5-triangle blade). Closed-form counts are checked against the exported `primitives` counts. The terrain's dT is the number of triangles dropped for the trench hole. |
| Decomposition | Export `objects` (per-object primitive totals) and `components`: every drawn part of UAV-1 (root UAV-00) and of Soldier 1. COUNTIFS totals are checked against the export. |
| Transform_Trace | Export component UAV-14 "Arm Front-right" (S, R_deg, T, local and world matrix) and its parent UAV-00's world matrix. The traced vertex is cube corner (0.5, 0.5, 0.5). M = T Ry Rx Rz S is built by explicit products, then V and P come from View_Projection, followed by the divide, NDC and the 1920x1080 viewport. |
| View_Projection | `scene/Camera.h` / `Camera.cpp` (default orbit camera), `math/Math3D.cpp` lookAt / perspective, near 0.05 m, far 600 m, 24-bit depth (`rendering/FrameBuffers.cpp`). |
| Clipping | Worked textbook example. The code clips on the GPU and culls with `math/Frustum`. |
| Illumination | `scene/DayCycle.{h,cpp}` (solar model, transmittance, sun intensity, ambient mixLog). Export `lights` supplies the moon and the UAV spot. `ShaderSources.cpp` supplies brdf() and the spot cone. The material is BNK-171 "Roof slab A". |
| Color_Interp | `math/ExplorerMath` edge function, affine and perspective interpolation, srgbEncode. Sections A to E use worked-example values. The sky table uses export `v6.day_cycle.grid`. |
| Ray_Tracing | Worked textbook example. Formulas follow `raytrace/Optics.h` (reflectDir, refractDir, fresnelDielectric). Depth limit is `RayTracer.h` whittedDepth = 5. |
| Thermal_Kernels | `math/ExplorerMath.cpp` kernel(): the 5x5 binomial Gaussian and Sobel; convolve() is a correlation. The 5x5 patch is a worked example. |
| Duty_Cycle | `scene/Garrison.h` (kDayShiftStart 6.4, kDayShiftEnd 17.6) and `Garrison.cpp` (programs, stagger delay 1 + 2.5 rank s). Rosters come from export `v6.garrison.soldiers`. There are 2 shifts, not 6-hour shifts. |
| Performance | `report/data/bench.csv` (config, avg_ms, p95_ms, avg_fps, draw_calls, triangles) when it exists. Otherwise the sheet holds a TODO row. Re-run the script after the benchmark to import it and draw the bar chart. |
| Report_Numbers | The register of the report's numbers. A: every number macro of the report (file, name, value, text printed, source, sections that use it). B: numbers stated in words (squad rule, guards, hand-written vector function calls, parts per shading model, tests). C: constants of the equations and hand-written tables (air mass, tone curve, fog, shadow bias, shadow settings per preset, near and far plane), each read from the source file named in its row. D: values derived in the text, as formulas. |
| Coordinates | Every component of the first instance of every design (2,804 rows): size, rotation, and its centre and extent in the design frame after the transformation chain, from the export's world matrices. The coordinate sheets of the report's Section 7 print columns K to P to 0.1 cm. |
