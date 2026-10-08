# WP-S: README v7 + slide deck for v6 / v7 (agent S)

Status 2026-10-08: DONE (open items below). No C++ or .tex edits, nothing committed.

## Build
```
export PATH=$HOME/.local/bin:$PATH
uv run --with python-pptx --with pillow python tools/build_slides.py
```
-> report/ThermalSense_Presentation.pptx (22 slides, ~12 MB without video), build/slides_preview.png (contact sheet),
build/slides_preview/slide_NN.png (one per slide, 120 px/in). It prints every picture it used (and from where) and an
overflow warning for any text box whose wrapped text is taller than the box (measured with DejaVu Sans, which is wider
than Calibri, so the check is conservative). Final run: 0 overflow warnings, 0 missing figures, every shape inside the
slide, speaker notes 63-78 words per slide (the script asserts 60-90).
No LibreOffice on the box (no sudo), so the previews are drawn by the script from the same layout records as the pptx.

## What changed
### tools/build_slides.py
- Deck 10 -> 22 slides. Slides 1-8 (v1-v5) kept; title, outline (3 columns, numbers asserted against the deck length),
  history (+ v6 / v7 columns), primitives (soldier part count), control (18 possessable) and results updated.
- v6: 9 Day and night cycle (sky_sunset + day / dusk / night / dawn strip, solar formula, sunrise / sunset / noon /
  cycle tiles), 10 Soldiers: Catmull-Rom paths and IK gait (soldier_axes, day_patrol_spline, soldier_possess_fp,
  spline / speed / IK formulas), 11 Random rover routes (plan_routes, spawn / via / switch rules, determinism tiles),
  12 Real CV under sensor noise (lab_cv_noise, sensor model, P_d tiles, day_thermal_fpv_persons).
- v7: 13 Game camera (game_soldier_fire, game_picker, game_heli_cockpit, six key chips), 14 Three ways to trace light
  (Whitted / path / light tracing, one column each with its equation), 15 BVH with SAH (SAH cost, intersector / refit /
  terrain bullets, stats, a drawn BVH schematic, raytrace_ssr SSR vs BVH reflections), 16 Ray-tree debugger
  (raytrace_tree_pixel, colour legend, the clicked pixel's tree; ray count / depth read from raytrace_tree.json),
  17 Ray tracing timings (wp-h.md table + single-series bar chart of day frame times), 18 Frame Explorer 16 stages
  (montage + stage list + explorer keys), 19 Colour interpolation (explorer_interp_affine_perspective + two crops of
  its cards: probe triangle checker, Gouraud vs Phong sphere; barycentric / affine / perspective-correct formulas),
  20 Reflection SSR vs RT (explorer_reflection_ssr_vs_rt + rt_difference, mirror law + Schlick).
- 21 Results: tests / leaves / possessable from report/sections/gen_stats.tex + gen_v6.tex macros, v6 trace and
  regression facts, RT timings.
- 22 Demo video: embeds report/demo_v7.mp4 when it exists (earlier decks embedded report/demo_v5.mp4); until then a
  poster with "TODO: record report/demo_v7.mp4" and the record command. The old v5 video is no longer embedded
  (that is why the pptx shrank from 117 MB to ~12 MB).
- Figures: one FIG table at the top (slot -> preferred name, substitutes); every name is searched in SOURCES order
  (report/figures_v7, figures_v6, figures_v5, docs/v7_notes/wpi_shots, wph_shots/day, ...; .png then .jpg), so CAP's
  re-shots replace the pictures on the next build without code changes. Helpers added: image fit="contain" and crop,
  formula panel, key chips, lines; the preview now draws ovals / triangles, centred / right / middle text and
  writes one PNG per slide.
### README.md
- Top: v7 and v6 summaries with links to the new sections.
- New "WSL2 on the RTX 4070 machine": /dev/dxg without /dev/dri -> preferWslGpu() sets GALLIUM_DRIVER=d3d12 and
  MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA itself (window and headless), tools/run_wsl.sh, log lines to check,
  TS_GPU_ADAPTER / TS_NO_WSL_GPU.
- Command-line table: --seed, --time, --cycle-seconds, --no-garrison, --export-docs; env vars (TS_RT_CPU,
  TS_DIRECTOR_*, TS_EXPLORER_TRACE, TS_EGL_DEVICE, TS_NO_AUDIO). Modes block: 289 tests, v7 record command.
- New "## v6" section (features table + v6 key table from keyBindingsV6()) and "## v7" section: features, complete
  v7 key table, keys inside the Frame Explorer, render-mode table with timings, ray debugger, 16-stage table, capture
  names. The old "Game camera (v7)" subsection is merged into it.
- Stale parts fixed: test count (120 -> 289), workbook (14 -> 24 sheets), deliverables (deck, figures_v6 / v7,
  demo_v7 pending), F1 / K / F3 rows, troubleshooting (WSL llvmpipe, RT CPU fallback).

## Key verification (against src/scene/HudMath.cpp + src/app/Interactive.cpp + FrameExplorerUi.inc)
All keys in keyBindingsV7() / helpLines() exist in onKey / onMouseButton: F4 (picker: Up/Down, PgUp/PgDn = 8, Enter,
Esc/F4), Insert / Delete, middle mouse (crosshair when the cursor is captured), Home / End (End swaps), C (cycleView
while riding along), W A S D / arrows / PgUp PgDn take control in Pov mode, Backspace (releaseRover or possess(-1)),
Left Ctrl / Enter fire (rifle only in the possessed soldier's first person, App::soldierWeapon), Left Alt crouch,
F11, `;` (nextRtMode), `'` (ray debugger; left click picks), `` ` `` (Frame Explorer; it then takes every key:
Left/Right, Up/Down, 1-9/0, Home/End, W, M + wheel, I, P, K, [ ], S, Space, R, Tab, ` / Esc).

## Open items
1. **`` ` `` (Frame Explorer) is missing from helpLines() and keyBindingsV7()** (src/scene/HudMath.cpp, G's file): the F1
   overlay and the exported key table (scene_export.json v7.keys, workbook, report sB) do not list it. Suggested rows:
   help `"` + "` Frame Explorer: freeze the frame, step through 16 stages"`; table `{"`", "Frame Explorer: freeze
   this frame and step through its 16 stages (` or Esc closes)", "v7"}` (T257 checks >= 8 rows, so it stays green).
2. **Native Windows exe gets an OpenGL 3.3 context** (App.cpp asks for 3.3 core; ThermalSense.exe on this box logs
   `OpenGL 3.3.0 NVIDIA 591.86`), so after the next Windows rebuild the `;` ray-traced modes would run on the slow
   CPU fallback (RayTracerGL needs GL >= 4.3). Suggested: try a 4.6 / 4.3 core context first, fall back to 3.3.
   (WSLg and headless on WSL get GL 4.6 from Mesa d3d12 and are fine.)
3. Demo video: record report/demo_v7.mp4 (v7 director cut, ~150 s), then rerun build_slides.py; slide 22 embeds it
   automatically (pptx will grow by the video size).
4. Figures CAP is still re-shooting: just rerun the build (the last build already picked up CAP's new banded
   explorer_montage and explorer_reflection_ssr_vs_rt / _rt_difference in report/figures_v7).
5. RT timings slide uses the wp-h.md table (raster 159 ms on the WSL d3d12 path); if WP-Q speeds the raster up,
   update RT_TIMES at the top of build_slides.py and the README render-mode table.
6. Report v7 sections (s23-s25) are not written yet (not WP-S); the README deliverables row says so.
