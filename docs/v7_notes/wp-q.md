# WP-Q: performance (agent Q)

Goal: smooth interactive play. 60+ fps at 1920x1080 on the native Windows exe at the best preset that holds it,
30+ fps in the WSLg window; output visually identical at the same preset.

Status: DONE (2026-10-08). 290/290 tests (build/tests-q2), -Wall -Wextra clean (g++), Windows exe cross-built (zig).

## Builds used (own dirs, nothing shared touched)
- WSL headless: `make -j32 HEADLESS=1 OBJ=build/obj-q2 TESTS=build/tests-q2 APP=build/ts-q2 build/tests-q2 build/ts-q2`
- WSLg window (GLFW sysroot): `make -j32 OBJ=build/obj-q2-gl APP=build/ts-q2-gl build/ts-q2-gl`
- Windows exe: the make line of tools/build_windows.sh with `OBJ=build/obj-win-q2 APP=build/ts-q2-win.exe`, run from
  the NTFS folder `C:\Users\Public\wpq` (copied there as ts-new.exe) through WSL interop.
- "Before" binaries: `build/ts-q2-base` (headless, built at the start of the session) and a scratch copy of the tree with
  the two runtime changes reverted (grass fix undone, instancing forced off) for the Windows exe and the WSLg build.
  So before / after differ only by WP-Q's changes; both include the benchmark instrumentation.

## Before / after (RTX 4070, `--benchmark`, benchmark camera, 2 rounds x 3 s per preset; WSLg after: 3 rounds)
Logs say `NVIDIA GeForce RTX 4070/PCIe/SSE2` (native, NVIDIA 591.86) and `D3D12 (NVIDIA GeForce RTX 4070)` (WSL).
JSON + logs: `build/wpq/final/` (`{native,wsl,wslg}_{before,after}_{1280x720,1920x1080}.json`).
Run with `TS_BENCH_ROUNDS=2 TS_BENCH_SECONDS=3 TS_BENCH_PHASES=1,2,4,12 ./ThermalSense --benchmark out.json --size WxH`
(for the Windows exe add `WSLENV=TS_BENCH_ROUNDS:TS_BENCH_SECONDS:TS_BENCH_PHASES`).
The benchmark has no vsync. Interactive play uses vsync (60 Hz cap).

### 1280x720

| platform | preset | before fps (ms) | after fps (ms) | speed-up |
|---|---|---|---|---|
| Native Windows exe (NVIDIA GL driver) | LOW | 49.6 (20.2) | **130.8** (7.6) | x2.6 |
| Native Windows exe (NVIDIA GL driver) | HIGH | 40.5 (24.7) | **86.9** (11.5) | x2.1 |
| Native Windows exe (NVIDIA GL driver) | ULTRA | 25.3 (39.5) | **87.0** (11.5) | x3.4 |
| Native Windows exe (NVIDIA GL driver) | EPIC | 8.8 (113.0) | **87.6** (11.4) | x9.9 |
| WSL2 headless (Mesa d3d12, EGL off-screen) | LOW | 9.6 (103.7) | **71.8** (13.9) | x7.4 |
| WSL2 headless (Mesa d3d12, EGL off-screen) | HIGH | 7.3 (137.3) | **70.4** (14.2) | x9.7 |
| WSL2 headless (Mesa d3d12, EGL off-screen) | ULTRA | 7.6 (132.0) | **42.8** (23.4) | x5.7 |
| WSL2 headless (Mesa d3d12, EGL off-screen) | EPIC | 4.0 (253.1) | **68.1** (14.7) | x17.2 |
| WSLg window (Mesa d3d12, X11 swap) | LOW | 8.4 (118.7) | **56.0** (17.9) | x6.6 |
| WSLg window (Mesa d3d12, X11 swap) | HIGH | 6.2 (162.5) | **53.6** (18.7) | x8.7 |
| WSLg window (Mesa d3d12, X11 swap) | ULTRA | 6.5 (154.2) | **48.6** (20.6) | x7.5 |
| WSLg window (Mesa d3d12, X11 swap) | EPIC | 3.7 (267.3) | **44.1** (22.7) | x11.8 |

- Native Windows exe (NVIDIA GL driver), EPIC after: renderer `NVIDIA GeForce RTX 4070/PCIe/SSE2`; draw calls main 852 (before 4579), shadow 41 (before 2399); CPU sim+UAV 5.0 ms, render 5.2 ms (UAV CV 2.9), present 0.3 ms
- WSL2 headless (Mesa d3d12, EGL off-screen), EPIC after: renderer `D3D12 (NVIDIA GeForce RTX 4070)`; draw calls main 851 (before 4574), shadow 39 (before 2560); CPU sim+UAV 6.1 ms, render 8.1 ms (UAV CV 5.4), present 0.0 ms
- WSLg window (Mesa d3d12, X11 swap), EPIC after: renderer `D3D12 (NVIDIA GeForce RTX 4070)`; draw calls main 851 (before 4572), shadow 39 (before 2548); CPU sim+UAV 8.9 ms, render 8.2 ms (UAV CV 5.7), present 8.5 ms

### 1920x1080

| platform | preset | before fps (ms) | after fps (ms) | speed-up |
|---|---|---|---|---|
| Native Windows exe (NVIDIA GL driver) | LOW | 58.7 (17.0) | **126.1** (7.9) | x2.1 |
| Native Windows exe (NVIDIA GL driver) | HIGH | 46.3 (21.6) | **106.5** (9.4) | x2.3 |
| Native Windows exe (NVIDIA GL driver) | ULTRA | 25.0 (40.0) | **77.0** (13.0) | x3.1 |
| Native Windows exe (NVIDIA GL driver) | EPIC | 9.2 (109.2) | **72.9** (13.7) | x8.0 |
| WSL2 headless (Mesa d3d12, EGL off-screen) | LOW | 9.2 (108.6) | **74.2** (13.5) | x8.1 |
| WSL2 headless (Mesa d3d12, EGL off-screen) | HIGH | 8.9 (113.0) | **70.5** (14.2) | x8.0 |
| WSL2 headless (Mesa d3d12, EGL off-screen) | ULTRA | 7.4 (135.0) | **44.6** (22.4) | x6.0 |
| WSL2 headless (Mesa d3d12, EGL off-screen) | EPIC | 4.6 (216.8) | **65.5** (15.3) | x14.2 |
| WSLg window (Mesa d3d12, X11 swap) | LOW | 9.2 (109.2) | **42.5** (23.5) | x4.6 |
| WSLg window (Mesa d3d12, X11 swap) | HIGH | 7.4 (135.5) | **40.8** (24.5) | x5.5 |
| WSLg window (Mesa d3d12, X11 swap) | ULTRA | 6.3 (158.3) | **34.9** (28.7) | x5.5 |
| WSLg window (Mesa d3d12, X11 swap) | EPIC | 4.2 (240.8) | **33.0** (30.3) | x7.9 |

- Native Windows exe (NVIDIA GL driver), EPIC after: renderer `NVIDIA GeForce RTX 4070/PCIe/SSE2`; draw calls main 852 (before 4582), shadow 41 (before 2460); CPU sim+UAV 6.0 ms, render 7.0 ms (UAV CV 4.2), present 0.4 ms
- WSL2 headless (Mesa d3d12, EGL off-screen), EPIC after: renderer `D3D12 (NVIDIA GeForce RTX 4070)`; draw calls main 852 (before 4574), shadow 39 (before 2549); CPU sim+UAV 6.5 ms, render 8.1 ms (UAV CV 5.5), present 0.0 ms
- WSLg window (Mesa d3d12, X11 swap), EPIC after: renderer `D3D12 (NVIDIA GeForce RTX 4070)`; draw calls main 850 (before 4573), shadow 39 (before 2561); CPU sim+UAV 5.3 ms, render 8.0 ms (UAV CV 5.6), present 17.1 ms


Interactive check with the auto preset (vsync on, the 16:30 start scene): native 1920x1080 kept EPIC at 85 fps, and
1600x900 kept EPIC at 98 fps ("Auto preset: EPIC holds 85 fps (target 60)"). WSLg kept EPIC at 38 fps (1600x900)
and 32 fps (1920x1080), with a target of 30.

Summary: **native EPIC 1080p 9.2 -> 72.9 fps** (60+ goal met at the best preset). **WSLg window EPIC 1080p 4.2 -> 33 fps,
720p 3.7 -> 44 fps** (30+ goal met). WSL headless EPIC (captures / video) 4.6 -> 65.5 fps at 1080p.

## Measured bottlenecks (in the order they were found)
Tools: the benchmark now prints a CPU breakdown per preset (simulation + UAV cameras, render submission, main-thread
busy time, UAV-image CV, present). `TS_GPU_SYNC_TIMING=1` replaces the GPU timer queries by glFinish-bracketed wall clock
(profiling only). On Mesa d3d12 the time-elapsed queries include idle gaps between command lists (a 320x240 UAV
camera pass "took" 57 ms), so the per-pass numbers in the old benchmark JSONs are not reliable on WSL. Temporary
skip switches (solids / grass / foliage / UAV cameras) were used for ablation and then removed.
1. **Grass rebuilt every frame (bug, CPU ~80 ms at EPIC).** `GrassField::setCount` returned early only when
   `blades == count_`, but `count_` was then set to the number of blades placed (always fewer than the budget). So
   every frame regenerated and re-uploaded all 450k EPIC blades (35k HIGH, 14k LOW). Main-thread CPU at EPIC went from
   95 ms to 12 ms when grass was skipped. The fix alone took EPIC from 205 to 89 ms on WSL.
2. **Per-part draw calls (~4500 scene + ~3700 pre-pass + ~2400 shadow + UAV cameras).** With the passes serialised
   (sync timing): solids ~80 ms (~20 us per draw), pre-pass 31 ms, shadow maps 21-36 ms, while the main thread was
   busy only ~24 ms. On Mesa d3d12 every draw with changed uniforms re-uploads the whole default uniform block (~3 KB
   for the lighting shaders: 16 practicals, indoor boxes, matrices), and the GPU pays per-draw state setup. Natively,
   the old EPIC frame spent 87 ms in "scene" (CPU-bound in the driver).
3. **UAV-camera readback = full CPU/GPU serialisation.** `captureUavCamera` does a blocking `glReadPixels` twice per
   step. It waits for everything queued, including the previous main frame, so frame = CPU + GPU, never max(CPU, GPU).
   Natively, after instancing, the step was 11-15 ms of mostly waiting.
4. **WSLg presentation.** Mesa d3d12 under WSLg presents by copying the back buffer GPU -> CPU -> X11 / Wayland
   (Wayland measured the same). Present costs ~4-8 ms at 720p and ~10-17 ms at 1080p, scales with window pixels,
   and does not depend on the preset. This is the WSLg ceiling: ~55 fps at 720p, ~42 fps at 1080p even on LOW.
5. Not bottlenecks (checked): sync latency of an idle glFinish 0.1 ms, 1-px readback 0.7 ms; no slow format
   fallback found (the RGBA16F targets, the 2-layer depth array, and the flat geometry shader all behave; only the
   uniform-block traffic was pathological). The static shadow cache works (the cached world cascade is not redrawn).
   Simulation CPU is ~1.5-2 ms. The full-screen post passes are ~0.1-0.5 ms each natively.

## Optimisations made
1. **Grass rebuild fix** (`Grass.{h,cpp}`): compare against the requested budget (`requested_`). Same blades, built once.
2. **Instanced scene parts** (`Instancing.{h,cpp}`, `ShadersInstanced.cpp`, `Renderer.cpp`, `RendererFrame.cpp`):
   - Each pass packs the per-part values into a buffer texture (RGBA32F; core GL 3.1, so the native GL 3.3 / 4.x
     context works too): model matrix, normal matrix and material (11 texels lit, 7 pre-pass, 4 depth). One
     `glDrawElementsInstanced` per (program, primitive mesh), with `uInstanceBase + gl_InstanceID`. It uses a ring of 16
     buffers. `GL_MAX_TEXTURE_BUFFER_SIZE` is checked, with a per-part fallback.
   - Shader maths unchanged: in `kLighting` / `kOutput` the per-object parameters are declared `TS_PER_OBJECT`
     (= `uniform`). The instanced programs define it empty, so they become globals that each stage loads from the
     buffer (vertex: by instance id; geometry / fragment: by a flat instance index). Flat, Gouraud, Phong, depth-only
     and pre-pass all have instanced variants. Grass / foliage / effects / rain / particles are untouched.
   - `Renderer::objectParams` is the single source of the per-part values (`setObjectUniforms` uses it too), so both
     paths get bit-identical inputs.
   - Used when `indexed && depthTest` (the painter's-order demo and the non-indexed comparison keep one draw per part).
     Parts are grouped by mesh (stable). `TS_NO_INSTANCING=1` forces the old path (A/B tests).
   - Draw calls at EPIC: main view 4570 -> 850 (the rest are grass / foliage cells and effects, already instanced),
     shadow maps 2550 -> 40, pre-pass 3700 -> 6.
3. **Asynchronous UAV-camera readback** (`RenderTarget::readAsync / collect` with PBOs,
   `Renderer::beginUavCamera / finishUavCamera`, `App::stepSim / stepVision`):
   - `step()` = `stepSim()` (simulation, render both UAV cameras, start the PBO copies, `glFlush`) + `stepVision()`
     (map, sensor model, CV, `submitVision`, tracker). Each sensor keeps its own RNG order (drop, jitter, noise), and
     `submitVision` only stores blobs, so the simulated run is unchanged. Captures / trace / record still call
     `step()`; their shots fire at the same simulation times.
   - The interactive loop and the benchmark call `stepSim`, then render the main frame, then `stepVision`, then swap. The
     CV runs on the CPU while the GPU draws. Cost: the HUD's PERS brackets / target box show the previous step's CV
     (16 ms), and the simulation does not change.
   - `glFlush` after the copy (Mesa d3d12 tracks buffer use per batch: without it the map waited for the whole frame)
     and at the start of `stepVision` (hands the main frame to the GPU before the CPU vision work). WSLg EPIC 1080p
     went 26.8 -> 32-33 fps from the second flush alone.
4. **Auto preset governor** (`App::governQuality`): interactive + auto-detected preset only. After a 2.5 s warm-up it
   measures 3 s; any frame over 250 ms (first-frame build, window drag) restarts the window. If fps < 0.9 x target, it
   steps down EPIC/ULTRA -> HIGH -> LOW and measures again. Target 60 fps, or 30 on the D3D12 (WSL) renderer, where
   the window copy caps the rate; `TS_TARGET_FPS` overrides. F5 (manual preset) ends it. It prints
   `Auto preset: ... holds N fps`. On this machine it keeps EPIC on both platforms.
5. **HUD debug line + frame-time graph** (`AppHud.cpp`, bottom left): `93 FPS  10.6 MS  EPIC AUTO  866 DRAW CALLS ...`
   plus a 120-frame bar graph (green <= 16.7 ms, amber <= 33.3 ms, red above; reference lines at 16.7 / 33.3 ms).
   Checked on a native Windows screenshot. The window title still shows FPS and draws.
6. **Benchmark instrumentation** (`Capture.cpp`): quick-run knobs `TS_BENCH_ROUNDS`, `TS_BENCH_SECONDS`, `TS_BENCH_PHASES`
   (comma list of phase indices: 0 v1, 1 LOW, 2 HIGH, 4 ULTRA, 12 EPIC, ...). Per phase it records CPU sim + UAV,
   render, main-thread busy (Linux CLOCK_THREAD_CPUTIME_ID / Windows GetThreadTimes), UAV CV and present, in the log and
   in the JSON (`cpu_step_ms`, `cpu_render_ms`, `cpu_uav_vision_ms`, `cpu_present_ms`). Defaults unchanged (5 x 4 s,
   all 17 phases). `GpuTimer`: `TS_GPU_SYNC_TIMING=1` sync timing mode.

## Visual equivalence (EPIC, 1280x720, headless d3d12 on the RTX)
`--capture --only day_overview,night_searchlights` with the baseline binary vs the final build:
| shot | PSNR | max abs diff | pixels > 8 levels |
|---|---|---|---|
| day_overview | 72.2 dB | 20 | 0.001 % |
| night_searchlights | 75.4 dB | 14 | 0.001 % |
The x8-amplified diff image is black except for sparse single-pixel speckle: float contraction differs slightly in
the new programs, and a few depth ties flip between coplanar parts after grouping by mesh. By eye the images are
identical. Capture logs: same shot times (the event-driven day_overview fires at t=91.6051 in both, so the simulation
and CV are deterministic), same triangle counts, stat_mean equal to 1e-5. Draws 5645 -> 215 (day_overview).
Capture time for the two shots: 153 s -> 115 s.
Files: `build/wpq/cap_base`, `build/wpq/cap_final`, `build/wpq/diff_final_*.png`.

Tests: `build/tests-q2` -> **290 tests, 0 failed** (after all changes). Builds -Wall -Wextra clean with g++ (headless,
GLFW) and clang/zig (Windows; the two clang warnings are in Combat.h / AppGame.cpp, not WP-Q files).

## Files changed (Q)
- new: `src/rendering/Instancing.{h,cpp}`, `src/rendering/ShadersInstanced.cpp`
- `src/rendering/Grass.{h,cpp}` (rebuild bug), `ShaderSources.{h,cpp}` (TS_PER_OBJECT, instanced declarations),
  `Renderer.h` (ObjectParams, instanced programs, InstanceStream, beginUavCamera / finishUavCamera; H's lines
  untouched), `Renderer.cpp` (objectParams, uploadInstances, drawInstanced, instanced drawSolids, UAV camera split),
  `RendererFrame.cpp` (instanced drawDepthOnly / renderPrepass), `RenderTarget.{h,cpp}` (PBO readAsync / collect),
  `GpuTimer.{h,cpp}` (sync timing mode)
- `src/app/App.{h,cpp}` (stepSim / stepVision, governor), `Interactive.cpp` (split step, frame-time history,
  F5 ends auto), `AppHud.cpp` (debug line + graph), `Capture.cpp` (benchmark knobs + CPU breakdown, split-step order)

## Open issues / notes for the supervisor
- WSLg is capped by the presentation copy (~10-17 ms at 1080p). The native Windows exe is the way to play: 73 fps
  EPIC 1080p in the benchmark, 85+ interactive. No GL-side fix exists (no dmabuf / zero-copy in WSLg). A smaller window
  (1600x900 default) gives 38 fps EPIC.
- ULTRA (8x MSAA, report preset) on WSL headless is slower than EPIC (43 vs 66 fps): 8x MSAA resolve on d3d12. Not
  an interactive preset; the governor steps EPIC -> HIGH, never to ULTRA.
- The UAV-camera CV costs ~2.5-5.5 ms of CPU per frame (2 x 320x240 sensor model + blob CV). It is now overlapped
  with the GPU; moving it to a worker thread would take it off the main thread entirely (not done: the simulation's
  order would need care).
- Not done (not needed for the targets, and risky for the "identical output" rule): half-res SSR / volumetrics with
  bilateral upsampling, grass LOD. Natively the GPU passes at EPIC 1080p are scene ~7 ms, fog ~2.4 ms, the rest < 1.5 ms
  each, so the frame is now balanced between CPU (~6 ms sim + UAV, ~5 ms render + CV) and GPU.
- The repo-root `ThermalSense.exe` and `./ThermalSense` were NOT rebuilt by Q (my exe: `build/ts-q2-win.exe`). Rerun
  `tools/build_windows.sh` and `tools/run_wsl.sh --setup` at integration to ship these changes.
- README "Quality presets" / "Measured on the test machine" still show the v2 integrated-GPU table. Suggested
  replacement: the 1080p table above, plus "auto preset: EPIC, stepped down at start-up if it does not hold 60 fps
  (30 in WSLg)", plus the env vars `TS_NO_INSTANCING`, `TS_TARGET_FPS`, `TS_GPU_SYNC_TIMING`, `TS_BENCH_*`. README
  is the supervisor's file, so not edited.

