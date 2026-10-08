# WP-I Frame Explorer (agent I) - progress notes

Spec: docs/PLAN_V7.md "WP-I". Key: `` ` `` opens / closes the modal Frame Explorer.

## Files (owned / new)
- src/math/ExplorerMath.{h,cpp} (NEW, pure, linked into the tests): 16-stage table (title, caption, formula, concept),
  edge functions / top-left coverage / barycentrics, affine vs perspective-correct interpolation, vertex pipeline trace,
  kernels + CPU convolution, histogram, Halton, ACES grey curve + sRGB + auto-exposure, CPU replay of the SSR march
  (same steps as kCombineFrag), FrameSnapshot planes + pixel inspector. (In src/math because the test binary links
  only non-app / non-rendering sources.)
- src/rendering/FrameExplorer.{h,cpp} + FrameExplorerUi.inc (UI half, included by the .cpp), ShadersExplorer.cpp.
- src/rendering/RendererLab.cpp: + Renderer::labSnapshot(fx::FrameSnapshot&) (float copies of resolved HDR, lit,
  TAA output, surface, depth, SSAO, fog, bloom; matrices; switches; moon shadow tex + light matrices).
- src/rendering/Renderer.h (H's file, minimal additive): forward decl `namespace fx { struct FrameSnapshot; }` and the
  one labSnapshot declaration next to labSphereResolution.
- src/app/CaptureLab.{h,cpp}: explorer captures; tests/test_explorer.cpp (T280-T299).
- src/app/Interactive.cpp (G's file): one `// v7 WP-I` block (key `, routing while open).

## Design
open(): renders the frame 10x through the real Renderer (TAA converges), labSnapshot, then the explorer's own passes
over the same leaves/camera: 8-target MRT (albedo, material, IDs, light terms w/ shadow from the renderer's moon map,
triangle-ID raster colours, affine checker, perspective checker, barycentrics; GS gives barycentrics smooth +
noperspective), Gouraud/Phong MRT (+ coarse 12x8 sphere close-up), overdraw (additive, depth off), assembly
(front faces + edges / back faces x-ray), bird's-eye with the frustum. Then real-renderer variants: SSR off, SSAO off,
fog off, PBR<->Blinn-Phong, TAA off (1 frame), thermal, raw heat (-> cv::detectAll). Each stage = list of layers
(Up/Down), split layers a|b; overlay per stage drawn with Hud + a display shader (wipe, magnifier, scanline reveal,
sampling demo).

## Status (2026-10-08): done
- ExplorerMath + tests T280-T299: all pass. Full suite in build/tests-wpi: 289 tests, 1 failure = T201 (soldier leaf
  count 75 vs 34..42: someone's Soldier.cpp change, not WP-I).
- -Wall -Wextra clean (headless build and a full GLFW build build/ts-wpi-gl link fine).
- Visual checks on the RTX (log: "D3D12 (NVIDIA GeForce RTX 4070)", EPIC, 1280x720), night 23:00 and day 10:30, through a
  scratch driver that calls lab::explorer. Review JPEGs in docs/v7_notes/wpi_shots/.
- open() cost on the WSL d3d12 path with a warm renderer: ~5.7 s (10 frames for TAA convergence ~2.8 s, 7 single-frame
  variants ~1.4 s, explorer passes ~0.7 s, CPU stage images ~0.9 s). The very first frames of a cold renderer cost ~20 s
  more (terrain maps / caches), which only matters for captures. TS_EXPLORER_TRACE=1 prints the timing breakdown.

## The 16 stages (layers: Up / Down)
1 GRAPH objects / parts (leaf) / final; card: root -> leaf chain with local T, the leaf's world matrix, projected unit box.
2 VERTEX bird's-eye with the frustum (in-frustum objects lit, culled ones dimmed) / camera view with NDC grid; card: one
  vertex model -> world -> view -> clip -> NDC -> pixel with the numbers + camera inset.
3 ASSEMBLY front faces + edges / back faces x-ray / both; card: triangle counts front / back (signed area), picked tri.
4 RASTER triangle IDs with a progressive scanline reveal (slow while it crosses the pixel grid) / triangles / barycentrics;
  card: 22x14 pixel grid of the probe triangle filled in scan order with barycentric colours, edge functions E12 E20 E01,
  coverage, weights, perspective-correct depth.
5 INTERP affine | perspective-correct checker (split through the probe triangle, draggable) / Gouraud | Phong / barycentric
  RGB / single sides; card: numbers at the probe, the probe triangle rasterised on the CPU both ways, coarse sphere G vs P.
6 DEPTH linear (log) / stored d / overdraw heat map; legend; card: depth precision at 1 / 10 / 100 m.
7 G-BUF 2x2 mosaic (albedo, normal, rough/metal, depth) + singles; card: bytes per pixel.
8 SHADOW visibility (PCF 4x4 against the renderer's own moon map) / light's depth map / final; card: cascade, light uv/z,
  PCF taps, light-view inset with the probe.
9 LIGHT ambient / diffuse / specular / sum mosaic (same scale) + singles + PBR | Blinn-Phong (real renderer); card: N L V H
  at the probe + vector diagram.
10 REFLECT final + animated CPU replay of the SSR march (same steps as kCombineFrag) / reflectivity / what SSR added / on|off;
  card: D, N, R, equal angles, Schlick, steps, result + mirror diagram. (WP-H RayTracerGL did not exist when this was
  written: no RT fallback wired; src/raytrace/RayTracer.h could later supply a mirror-ray comparison.)
11 SSAO buffer / with | without; card: hemisphere diagram.   12 FOG in-scatter / transmittance / with | without; card: T(d).
13 BLOOM bright pass / 13-tap mip pyramid (CPU replay) / renderer's bloom / final; card: kernel weights.
14 TONEMAP linear clip | ACES / log2 L false colour / final; card: curves + log-luminance histogram + exposure.
15 TAA off | on / single jittered frame / TAA / difference; card: Halton (2,3) pattern.
16 FINAL final / thermal / CV (Otsu split + detection threshold, blob boxes from cv::detectAll) / visible | thermal.
Image lab (P): kernels (identity, box, Gaussian 5x5, Sobel x / y / |G|, Laplacian, sharpen, emboss) + gamma on the real
frame (CPU, fx::convolve), histogram (luminance + RGB), sampling demo nearest / bilinear / trilinear at 1/4..1/12.

## Keys inside the explorer (FrameExplorer.h)
Left/Right stage, Up/Down layer, 1-9/0 stage 1-10, Home/End, W wipe vs previous stage (drag), M magnifier (wheel zoom),
I pixel inspector, P image lab (K kernel, [ ] gamma, S sampling factor), Space pause/play, R restart animation,
left click = probe pixel (all stage cards follow it) or drag a split line, Tab clean view, ` or Esc close.

## Interactive.cpp (G's file): `// v7 WP-I` blocks
include; wpiExplorer() singleton (heap, never freed: no GL calls after context teardown); loop: while open, skip the
simulation and draw the explorer; onKey: routes every key while open, ` opens (camera = uavCameraView in thermal FPV,
else composeView(aspect); hideUav as renderFrame); onMouseButton / onCursor (framebuffer pixels) / onScroll routed.

## For CAP (Capture.cpp is CAP's): wiring the shots
In shotList()'s lab list add {"explorer", "explorer"}; in the lab dispatch add
`else if (kind == "explorer") ok = lab::explorer(ctx);` (optional lab::ExplorerShot: cam, width/height, wetness (default
0.7 so the reflection stage has puddles; restored afterwards), prefix). Outputs: explorer_stage_01_graph.png ..
explorer_stage_16_final.png, explorer_montage.png (clean 4x4), explorer_raster_scan, explorer_interp_gouraud_phong,
explorer_interp_affine_perspective, explorer_inspector, explorer_wipe, explorer_reflection_mask, explorer_final_cv,
explorer_imagelab_sobel, explorer_imagelab_gauss_gamma (.png) and explorer_stages.json (titles, captions, formulas,
concepts, layer names; for WP-F / workbook). Default camera: eye (-15, 5.2, 21) -> (1, 1.2, 2), fov 52.

## Open issues
- Variants (SSR/SSAO/fog off, Blinn-Phong, TAA off, thermal) are single un-jittered frames: splits compare TAA-resolved
  final vs one frame (slight AA difference at edges). Difference images use two un-jittered frames.
- The explorer's G-buffer albedo is the material base colour (procedural surface patterns are not reproduced).
- Opening takes seconds on the WSL d3d12 path (see above); no progress UI during open().

## Polish round (2026-10-08, supervisor review)
- Raster probe = the chosen triangle's centroid (weights ~1/3, must show that triangle in the ID buffer); card shows
  the probe pixel (covered, w) plus the scan cell; captures time the scan to sit on the probe cell
  (FrameExplorer::rasterProbeTime). A clicked probe stays where it is clicked.
- Montages: 4x4 tiles with a number + title band (26 / 19 px at 1596 px width). Default capture is now DAY (10:30,
  ExplorerShot::hour, clock restored afterwards) -> explorer_montage.png; explorer_montage_night.png (23:00) as well.
- Progress overlay while open() works (FrameExplorer::setProgress / drawProgress; Interactive presents it after each
  step: "rendering the frame (TAA convergence) k/n", "capturing 7 variants 3/7: SSAO off", ...). Captured as
  explorer_progress.png.
- Speed-up: interactive open continues the live view's converged TAA history (3 frames instead of 8; only in the
  visible, solid view, not thermal FPV); captures use 8 (one Halton cycle, was 10). Light-view image drawn on the GPU at
  1024^2 instead of reading the 4096^2 x 2 shadow array back (-0.55 s); thermal variant 1 frame. Capture open ~4.9 s
  (8 frames); interactive estimate ~3.9 s on the WSL d3d12 path.
- Tests: 289 / 289 pass in build/tests-wpi (T280-T299 all pass).

## WP-H ray-traced reflections in stage 10 (2026-10-08)
- Extra variant when Renderer::rayTracer() exists: the frozen frame with `rtMode = Hybrid` (rtOverlay off, TAA on,
  max(4, convergence) frames: its soft shadow rays are stochastic). Layers: "ray-traced reflections (BVH)",
  "SSR | ray traced" (final vs hybrid), "RT - SSR on reflective pixels (x5)" (difference weighted by the reflectivity
  buffer so the RT soft-shadow changes do not dominate).
- Card: SSR hit distance (march hit reprojected from the depth buffer) next to rt::traceReflection at the same probe
  (`RT hit (BVH): <object> at d m`, exact normal, R, Fresnel); the traced ray is drawn dashed magenta on the image and
  in the mirror diagram (at its exact-normal angle). Re-traced on every probe click.
- T293 extended: rt::traceReflection on a floor + wall RtScene obeys fx::reflectDir and lands where the SSR march
  does (y 2.80 vs 2.79). 289 / 289 pass.
- Captures: + explorer_reflection_ssr_vs_rt.png, explorer_reflection_rt_difference.png. Opening cost +~1.5 s on the
  first open (RT init / BVH build), +~0.7 s afterwards.
- To re-check after H's hybrid mirror-ball fix (no mirror ball is in the default explorer view; H's showcase props are
  only added inside H's own figures).
