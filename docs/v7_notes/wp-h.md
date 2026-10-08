# WP-H Ray tracing (forward + reverse): progress notes (agent H)

## Status (2026-10-08)
- [x] CPU core `src/raytrace/` (GL-free, linked into the tests): exact unit-shape intersectors, SAH BVH + refit,
      terrain height field (2-level DDA), scene bridge, Whitted / path / light tracer, ray tree, a-trous denoiser,
      photon map, std430 GPU packing.
- [x] `tests/test_raytrace.cpp` T260-T279: 20/20 PASS. Full suite (build/tests-wph, 2026-10-08): 289 tests, 0 failed.
- [x] GPU `src/rendering/RayTracerGL.{h,cpp}` + `ShadersRT.cpp` (GLSL 4.30 compute): SSBO scene, hybrid / Whitted /
      path / light tracing, denoiser, upscale; CPU fallback without compute (forced with `TS_RT_CPU=1`).
- [x] Renderer hooks: `RenderSettings::rtMode` (+ rtScale, rtWhittedScale, rtBounces, rtDenoise, rtLightPathsK,
      rtOverlay), `src/rendering/RendererRT.cpp` (rtBegin / rtEnd / overlay), 4 marked lines in RendererFrame.cpp,
      2 in Renderer.cpp (init, sky-only draw), ctor/dtor + API in Renderer.h.
- [x] Interactive.cpp `// v7 WP-H` blocks: `;` cycles the mode, `'` toggles the ray debugger, left click picks.
- [x] `src/app/CaptureRT.{h,cpp}`: raytrace_* figures, checked on the RTX (shots in `docs/v7_notes/wph_shots/`).
- [x] performance measured (below); CPU fallback checked (TS_RT_CPU=1: modes render and accumulate).

## Follow-up (2026-10-08, supervisor items)
1. Hybrid mirror ball "blotches": not a ray-origin, normal or roughness bug. Whitted and the path tracer show the same
   reflection from the same view: a chrome ball reflecting a bright sky plus dark, unfogged objects behind the camera.
   Fixes so it reads like the raster world:
   - reflected hits are fogged by their distance, like the raster fogs what it shows;
   - escaped mirror rays use `skyDome()`, the raster's sky-in-water gradient (fog colour at the horizon, 1.2 x zenith
     overhead), instead of the brighter diffuse-sky model; this applies to hybrid metal and to Whitted reflection /
     refraction misses.

   raytrace_ssr was re-checked: clean mirror image of the camp.
2. Grass / foliage stand-ins (GPU; foliage also on the CPU):
   - Foliage (`Surface::Foliage` = crowns, pine cones, bushes, camouflage nets; prim flag kFoliage): alpha-tested by
     smooth 3D value noise in local space (`leafCovered`, 11 cells/unit, ~62 % covered). A ray through a gap
     continues to the far side of the same primitive. The result is organic gaps, dappled shadows and ragged
     silhouettes. Leaves get a noise-perturbed shading normal and wrapped Lambert (light through thin leaves).
   - Grass: where the ground map's meadow channel is dense, the terrain albedo becomes olive / straw blade tufts
     (value noise), darkened 20 % for blade self-occlusion, with a scattered shading normal.

   Cost is within noise: Whitted 25 ms, path 23 ms, light 38 ms (day); 35 / 29 / 41 ms (night).
3. Help: `;` and `'` lines in `helpLines()` and two rows in `keyBindingsV7()` (src/scene/HudMath.cpp). T257 uses
   `>= 8 rows`, so it is unchanged (11 rows now). T278 now also checks both keys in the help and in the key table.

Tests: 289 / 289 pass. Shots were re-taken on the RTX (wph_shots/day: modes, ssr, tree, perf; night: modes, perf).

## Design
- Scene -> `rt::SceneBridge::sync`: every solid leaf = one analytic primitive with its exact world matrix (sphere,
  box, capped cylinder, cone); rays go to local space with the inverse matrix (direction not renormalised, so t is in
  world metres), normal back with the inverse transpose. Quadratics are solved from the closest-approach point
  (1 cm fence wires 60 m away were missed with the naive form: catastrophic cancellation). Skipped: searchlight
  cone (volumetric), rotor blur discs (translucent), terrain disc (-> height field). Sky bodies cast no shadows;
  lamp bulbs (practical light proxies) are NEE lights, their emission only counts when seen directly / in mirrors.
- BVH: binned SAH (16 bins, leaves <= 16 prims when splitting does not pay), children adjacent -> refit is one reverse
  sweep. Camp: 5028-5141 prims, ~8.5k nodes, depth 21, build 61 ms once, sync (matrices + materials + refit)
  0.7-0.9 ms / frame; rebuild when the leaf list changes or the refit SAH cost exceeds 1.6x the build cost.
- Terrain: 289^2 samples of terrainHeight() over the 72 m disc (0.25 m cells, 2 triangles each, smooth normals),
  trench + outside-disc cells are holes like the mesh; 8x8-cell blocks with min/max height skip empty space; per-cell
  height-range test before the triangle tests. Same grid on CPU and GPU (RGBA32F texture + RG32F block texture).
- Materials: albedo = baseColor^2.2, roughness = pbrRoughness, metalness, F0 0.04; Surface::Glass parts that do not
  glow are clear dielectrics (n = 1.5, tint from the base colour). Lights in the raster's units (albedo * colour * N.L),
  converted to irradiance pi * colour, so Lambert = albedo / pi and the RT images match the raster exposure; sky for
  secondary rays L(d) = max(B + (Z - B)(0.5 + 0.75 d.y), 0), which integrates to the raster ambient mix(B, Z, 0.5 + 0.5 N.y).
- Modes (`;`): RASTER -> HYBRID RT -> WHITTED RT -> PATH TRACING -> LIGHT TRACING -> RASTER. Only in the graded visible
  view (cinematic, solid edges, no isolate); thermal / diagnostic views stay raster.
  - Hybrid: raster without shadow maps, then a compute pass replaces combine: per pixel the G-buffer position,
    a primary ray for the material, soft shadow rays (key light, searchlight) whose blocked direct term is removed
    (albedo capped by c / irradiance so puddles / grass are not over-darkened), and a reflection ray where the
    surface buffer says reflective: on-screen hits reuse the raster colour, others are shaded by the tracer;
    polished metal replaces the raster's sky-specular by the traced mirror image. Then the raster fog. TAA as usual.
  - Whitted: explicit ray stack (6 tasks): glass -> Fresnel (exact dielectric) reflection + Snell refraction (TIR),
    smooth surfaces -> Schlick-weighted mirror ray, every hit -> shadow ray per light (glass = tinted shadow),
    ambient sky with one AO ray; running average + Halton jitter while still (soft shadows, AO, AA converge).
  - Path: NEE to every light, cosine + GGX one-sample mixture, RR from bounce 2, firefly clamp 64, running average
    (reset on camera change, capped at 12 frames while the scene animates), 5/3/1 a-trous passes on the
    albedo-demodulated image (normal, depth, luminance edge stopping), bilinear upscale + remodulation.
  - Light ("reverse"): 256K light paths / frame, emitter picked by power (key light, beacon, searchlight, 16 lamps,
    sky); sun / moon / sky photons start on a plane across the beam with a 70/30 mixture of a 14 m disc around the
    view focus and the whole camp (mixture pdf, unbiased); every non-specular vertex is connected to the pinhole
    (importance W = Npix / (A cos^4)) and splatted with uint atomics; directly seen emitters + sky from one primary
    ray. Caustics through the glass props are visible; mirrors are black (correct for pure light tracing).
- Sky: a fully ray-traced frame still runs the raster sky pass (Renderer::drawScene returns after drawSky); primary
  misses read it, so stars / moon glow / clouds match the raster exactly.
- Ray debugger (`'` + left click): CPU Whitted tree of the clicked pixel (`rt::debugTrace`): primary, shadow (lit /
  blocked), reflection, refraction rays with depth, t, radiance contribution, object; plus one informational
  diffuse bounce. Drawn as 3D lines (white primary, yellow lit / red blocked shadow, cyan reflection, magenta
  refraction, green bounce) with de-cluttered labels and the indented tree list on the right. Works in every mode
  (the tree stays in 3D when the camera moves).

## Performance (RTX 4070 via Mesa d3d12, 1280x720 output, EPIC, wall clock with glFinish, interactive defaults)
Final runs (docs/v7_notes/wph_shots/{day,night}/raytrace_perf.json); the GPU was shared with other agents' captures.
| mode (internal resolution) | day 16:30 | night 23:00 | RT pass alone |
|------|-----------|-------------|---------------|
| raster (v6, EPIC) | 159 ms | 154 ms | - (scene pass ~80-90 ms on this d3d12 path; not WP-H) |
| hybrid (1280x720) | 171 ms | 154 ms | 14-21 ms on top of the raster frame |
| Whitted (960x540, rtWhittedScale 0.75) | 29 ms | 25 ms | 19-24 ms |
| Whitted (1280x720, figures) | 34-37 ms | 61-72 ms (busy GPU) | 28-50 ms |
| path (640x360 -> 720p, 1 spp + denoise) | 27 ms | 25 ms | 21-23 ms |
| light (640x360, 256K paths) | 45 ms | 34 ms | 29-38 ms |
| CPU fallback (TS_RT_CPU=1, 160x90) | Whitted 60, path 144, light 238 ms | | |
GPU timings vary +-20 % with the other agents' captures on the same GPU. Optimisations that mattered: entry-distance
BVH stack (no re-test of popped boxes), smaller stacks (register pressure: Whitted 49 -> 34 ms), per-cell height test.
A stochastic one-lamp NEE was tried and reverted (slower on this driver).

## Public API (for agent I, CAP, G)
- `src/raytrace/RayTracer.h` (CPU, GL-free): `rt::Tracer` (whitted / path / lightPath), `renderWhitted / renderPath /
  renderLight`, `denoiseAtrous`, `debugTrace` -> `rt::RayTree`, `tracePhotons`; `RtScene` (prims, BVH, lights, env),
  `SceneBridge::sync(scene, LightOptions, RtScene&)`.
- `src/rendering/Renderer.h`: `RenderSettings::rtMode` (and rt* fields; defaults = v6 raster bit for bit),
  `Renderer::rayTracer()` -> `RayTracerGL*` (`scene()`, `pick()`, `status()`, `samples()`),
  `setRayDebug(bool)`, `rayDebug()`, `rayDebugPick(scene, u, v)` (u, v in [0,1], v = 0 top, last frame's camera),
  `rayTree()`, `rtStatusLine()`.
- **Agent I (Frame Explorer reflection stage): "trace reflection rays for this frame"**
  1. Whole frame, GPU (ray-traced mirror vs SSR side by side): render the frozen frame twice through the normal path,
     once with `RenderSettings s` as is (raster, SSR) and once with `s.rtMode = rt::RtMode::Hybrid` (same G-buffer;
     the combine pass's SSR is replaced by BVH reflection rays: on-screen hits reuse the raster colour, off-screen /
     hidden ones are shaded by the tracer, polished metal shows the traced mirror image). Nothing else changes, so
     the two images differ only in the reflections (and the RT soft shadows). `raytrace_ssr.png` is exactly this.
  2. One pixel, CPU (for the stage's mirror-law diagram): after any ray-traced frame or `renderer.rayDebugPick(...)`
     (both sync the scene), call
     `rt::ReflectionSample r = rt::traceReflection(renderer.rayTracer()->scene(), rt::RtCamera::fromView(cam.view, cam.proj), u, v);`
     (u, v in [0,1], v = 0 top): primary hit `point`, `normal`, view `view`, mirror direction `reflectDir`
     (R = D - 2 (D.N) N), `reflectedPoint` / `reflectedObject`, Whitted `radiance` brought back, Schlick `fresnel`.
     (Tested in T270.)
  3. Full ray tree of a pixel: `renderer.setRayDebug(true); renderer.rayDebugPick(scene, u, v); renderer.rayTree()`;
     the renderer then draws it in 3D on every frame until `setRayDebug(false)`.

## What the supervisor / CAP / G must wire
- CAP (Capture.cpp): shots `raytrace_modes`, `raytrace_tree`, `raytrace_ssr`, `raytrace_photons`, `raytrace_perf`
  (list: `rtcap::shotNames()`), run like the lab shots: build `rtcap::Ctx{scene, renderer, dir, width 1280, height 720,
  quality Epic, hour 16.5 (or 23 for the night set), log}` and call `rtcap::figure(ctx, name)`. Outputs:
  raytrace_modes.png (labelled 3x2 montage) + raytrace_mode_{raster,hybrid,whitted,path,light}.png,
  raytrace_tree.png + raytrace_tree_pixel.png + raytrace_tree.json, raytrace_ssr.png (+ _raster, _hybrid),
  raytrace_photons.png, raytrace_perf.json. Showcase props (glass ball, mirror ball, glass block, chrome post, red
  block) are added only while the figure renders. Each figure takes 10-100 s.
- G (HUD / help): keys `;` (render mode) and `'` (ray debugger, left click = pick). The renderer already draws its
  status line at the bottom centre (`RenderSettings::rtOverlay`); G may show `Renderer::rtStatusLine()` instead and
  set rtOverlay = false. Help text: "; render mode: raster / hybrid RT / Whitted / path tracing / light tracing",
  "' ray debugger (click a pixel)".
- README controls: same two keys; env var `TS_RT_CPU=1` forces the CPU fallback.

## Open issues
- Grass blades and foliage cards are not in the BVH (alpha-tested instanced cards): in the full RT modes the
  meadow is the ground-map colour, and in hybrid the tree crowns lose the foliage self-shadowing of the shadow maps.
- Hybrid glass stays raster (no refraction in hybrid); the CPU tracer's terrain uses the plain terrain colour (the
  GPU samples the ground map), so debugger radiance values on the ground differ slightly from the GPU image.
- Light tracing needs ~100+ frames to converge near the camera (few photons per pixel there) - inherent.
- Raster EPIC itself is ~150 ms / frame on this d3d12 path in headless captures, which dominates hybrid.
