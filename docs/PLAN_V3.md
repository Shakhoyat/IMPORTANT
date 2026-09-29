# ThermalSense — Plan v3 (RTX 4070 target, first-person thermal, finishing v2)

Started 2026-09-29. Builds on `PLAN_V2.md` (its "Not done yet" row is the backlog here).

## Decisions (user, 2026-09-29)

| Topic | Decision |
|-------|----------|
| Target hardware | the project runs on a **remote RTX 4070 (12 GB)** machine: add an EPIC preset for it, auto-selected on discrete GPUs |
| Thermal view | a **first-person, full-screen** view through the UAV's gimbal camera (like a real drone FLIR feed), not a third-person palette view |
| Visual target | "Ghost of Tsushima"-class night scene: as realistic as a modern game |
| Scope | finish everything left in PLAN_V2: TAA, auto-exposure, lens flare, DOF, motion blur, SSR |

## 1. Platform (remote GPU)

| # | Work |
|---|------|
| P1 | Cross-platform build: Windows (MinGW, as before) + Linux (`make`, CMake with system or fetched GLFW) |
| P2 | `--headless`: EGL context without a window (Linux GPU server over SSH) for captures, video and benchmarks |
| P3 | HUD font baked into the program (no Windows GDI) |
| P4 | `--quality`, `--size`, `--fullscreen`, `--gpu`, `--fps`, `--encoder h264_nvenc` |
| P5 | Windows: `NvOptimusEnablement` export (hybrid laptops use the discrete GPU) |

## 2. Rendering pipeline v3

```
shadows -> pre-pass (depth+normal) -> SSAO -> volumetric fog -> HDR scene (MSAA, jittered)
  -> resolve colour + depth -> lighting composite (fog + screen-space reflections)
  -> TAA (reprojection + YCoCg variance clip) -> luminance / heat reduction (auto-exposure, thermal AGC)
  -> bloom -> lens flare -> depth of field -> final composite (motion blur, grading, filmic curve, grain)
```

| # | Feature | Presets |
|---|---------|---------|
| R1 | Full-resolution depth resolve (needed by TAA, SSR, DOF, motion blur) | all |
| R2 | TAA: Halton jitter, depth reprojection, neighbourhood variance clipping, cut detection; captures accumulate 8 frames | HIGH+ |
| R3 | Auto-exposure: log-average luminance, temporal adaptation, clamped EV range (a night must stay dark) | all |
| R4 | Screen-space reflections in puddles and on wet ground | ULTRA+ |
| R5 | Lens flare (ghosts, halo) + anamorphic streaks from the bloom chain | HIGH+ |
| R6 | Depth of field (golden-angle bokeh gather) in cinematic shots | ULTRA+ |
| R7 | Camera motion blur from depth reprojection | ULTRA+ (cinematic / video) |
| R8 | Filmic tone curve (fitted ACES RRT+ODT), sRGB OETF, scotopic (Purkinje) night shift, split toning | all |
| R9 | EPIC preset: 4096² shadows, full-res SSAO pre-pass, 64-step fog at half res, ~450k grass blades, dense foliage | EPIC |

## 3. First-person thermal (FLIR) view

- `V` -> THERMAL shows the UAV gimbal camera full screen (the image the tracker sees, at display resolution).
- Heat is rendered as a scalar into the HDR buffer; a sensor pass does AGC (automatic gain from the frame's
  heat range, temporally smoothed), digital detail enhancement, palette, fixed-pattern + temporal noise,
  optical vignetting, heat shimmer, periodic NUC freeze.
- Palettes (`T`): white-hot, black-hot, ironbow, rainbow.
- Military-style HUD: reticle, compass tape, gimbal azimuth/elevation scales, altitude, speed, slant range,
  target box, track state, zoom (wheel: WFOV/MFOV/NFOV), grid position, clock, REC.
- `Y` switches back to the old external (third-person) thermal view.

## 4. World realism (all still built from cube / sphere / cylinder)

| # | Addition |
|---|----------|
| W1 | Foliage cards: instanced thin boxes on every pine whorl / broadleaf clump / bush with procedural needle and leaf cut-outs (alpha-to-coverage), translucency and wind |
| W2 | Grass v3: more, taller meadow patches, travelling wind gusts, sheen and translucency under the moon |
| W3 | Fireflies and drifting motes (instanced, emissive, bloom) |
| W4 | Sky: detailed moon disc, brighter halo, two moonlit cloud layers |
| W5 | First-person walk camera (`C`): eye height on the terrain, mouse look, Shift to run |

## 5. Status (2026-09-29)

| Item | Status | Verified by |
|------|--------|-------------|
| P1 cross-platform build (Makefile, CMake, build.sh / build.bat) | done | Linux make (headless + GLFW), CMake (both), Windows cross-link of the full exe |
| P2 headless EGL | done | all captures and the test video in this session were rendered headless (Mesa llvmpipe) |
| P3 baked HUD font, P4 options, P5 Optimus export | done | captures show the baked font |
| R1-R8 v3 post chain (TAA, auto-exposure, SSR, flare, DOF, motion blur, filmic curve, Purkinje) | done | captures + 6 s test video; exposure calibrated from measured frame statistics (key log2 L = -5.2, +-1.5 EV) |
| R9 EPIC preset | done | runs here on llvmpipe; FPS on the RTX 4070 still to be measured with `--benchmark` |
| 3 first-person thermal view, sensor model, FLIR HUD, 4 palettes, zoom, NUC | done | `thermal_fpv*` captures |
| W1 foliage cards (~50k cards at EPIC), W2 grass v3 (tall patches, gust waves) | done | `report/figures_v3/walk_*`, `cine_*` (EPIC) |
| W3 fireflies / motes, W4 sky | done | `cine_*`, `walk_*` captures |
| W5 walk camera | done | tests T50-T55, `walk_*` captures |
| Not measured here | FPS / GPU times on the RTX 4070 (no NVIDIA GPU in this environment), NVENC encoding, a native Windows run |
