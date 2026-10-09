# v9: plays and looks like a game (notes)

Plan: `~/.claude/plans/wiggly-herding-koala.md` (M0-M10). Each milestone: tests, before/after captures, `--bench-shots`,
a workbook sheet, a commit.

## M0: audit + measurement harness (done)
- Snapshot commit of the v5-v8 work (`138163d`).
- `--bench-shots FILE` (`src/app/BenchShots.cpp`): 5 fixed shots x day (10:30) / night (23:00), the interactive frame
  (stepSim + updateGameCamera, renderFrame + HUD, stepVision, present), CPU phases, GPU passes (GpuTimer), draws, triangles.
- `--input-script FILE --frames DIR` (`src/scene/InputScript.{h,cpp}`, played by `App::runScript`): timed keys, buttons,
  mouse looks, pointer, `shot`, `log`, `quit`. The loop's held-key / button / cursor queries go through `keyDown`,
  `mouseDown`, `cursorPos`, so a script drives exactly the paths a player does. Tests T330, T331.
- `--vsync 0|1`.
- Workbook: sheet 28 "v8 Daylight & Barracks" (v8 was missing) and 29 "v9 Audit & Budget".

### Findings (evidence in `audit/` and `m0/`)
| | |
|---|---|
| Live 20 fps | `stall_vsync1.log`: swap 87.6 ms of a 95 ms frame (sim 2.2, render 2.5, vision 2.6 ms) -> 10 fps; `stall_vsync0.log`: 84.8 fps. The blocking vsync swap under the compositor is the cause. |
| Fixed shots, exe, vsync 0 | 80-101 fps on all 10 shot x time combinations (`audit/bench_shots_m0_win.json.md`) |
| Fixed shots, WSL d3d12 | 54-73 fps (`audit/bench_shots_m0_wsl.json.md`); CPU sim + UAV cameras 6-10 ms on that path |
| GPU | scene pass ~6.5 ms of ~11 ms; 6.3-7.7 M triangles (grass blades, no LOD) |
| Controls | variable dt, linear accel (2 m/s^2), jump only on the press frame, exponential camera ease (see plan) |
| WASD in the exe | `wasd.log`: W 2 s moved the soldier 2.3 m (speed 0.77 m/s: the slow ramp) |

## M1: fixed tick, render interpolation, frame pacing, vision worker, grass LOD (done)
- `scene/TickInterp.{h,cpp}`: `FixedStepClock` (60 Hz, max 4 ticks / frame), `blendTickWorld`, `blendTickPose`.
  `Scene::captureTick / interpolate / restoreTick` blend the drawn leaves between the last two ticks; the camera pose
  is captured per tick (`App::captureTickPose`) and interpolated (`viewOverride_` in `composeView`).
- Frame pacing: swap interval 0 + deadline limiter at the display refresh (`app/FramePacer.cpp`, Windows
  high-resolution waitable timer); `--fps-limit N`, `--vsync 1` to opt in.
- UAV vision: `vision/VisionWorker.{h,cpp}` runs sensor model + CV + v4 tracker on a thread; images are read back
  behind a fence (`RenderTarget::readReady`) and the results join at the next tick start. `--trace` seed 7 60 s with
  `TS_VISION_THREAD=0/1`: byte-identical (`m1/trace_compare.txt`); T334.
- Day fog: half the march steps and no lamp-halo loop by day (1.65 -> 0.56 ms); night halos every other step.
- Grass: 5-triangle curved strip (was the 12-triangle cube), cell prefix submission + per-blade rank LOD.
- Window title FPS used the clamped dt (overstated slow frames): now real time.
- Exe, 1080p EPIC, vsync off: 86-121 fps on the 10 fixed shots (M0: 80-101), grass <= 1.82 M triangles.
- Live loop (orbit noon): 10.4 fps (vsync 1) -> 85.7 fps (100 Hz limiter).
- Open: the night forest is SSIM 0.969 vs v8 (the strip blades read slightly darker than the cube blades); M6 redoes
  grass with a night-parity check. uav_chase night 86 fps (< 90): M9 performance pass.

## M6: vegetation from CC0 assets (done, budgets not met; see open items)
- Assets (`assets/`, 171 MB, all CC0, table in `assets/LICENSES.md`): Poly Haven fir_tree_01, pine_tree_01,
  fir_sapling_medium, pine_sapling_small, tree_small_02, shrub_01..04, fern_02, grass_medium_01/02, boulder_01,
  rock_moss_set_01/02 (+ 5 ambientCG terrain sets vendored for M7, unused). Fetched by `tools/fetch_assets.py`, reduced
  offline by `tools/bake_assets.cpp` (`make bake-assets`): 28.6 M -> 3.1 M triangles, wind weights in `COLOR_0`.
- Code: cgltf / stb_image loaders (`src/assets/Model, Image`), coverage-preserving alpha mips, LOD chains
  (`assets/FoliageLod`: meshoptimizer for solid parts, card thinning for foliage), octahedral impostors baked at start-up
  (`assets/Impostor`, 8x8 views), `rendering/VegetationRenderer` (instancing per variant / LOD / material, 16 m culling
  cells, dithered LOD cross-fade, alpha-to-coverage on the MSAA target, two-sided foliage light with transmission,
  hierarchical wind trunk -> branch -> flutter, per-instance tint), shadow casters (near cascade LOD+1 with wind, cached
  world cascade LOD 2). Textured grass blades from the CC0 atlas with A2C. `objects/VegetationLayout` maps the v8
  primitive trees / bushes / boulders / treeline to asset instances; the scene graph keeps the primitives for gameplay
  (occluders, walker collision, `treelinePines()` unchanged), the renderer only skips drawing them at ULTRA / EPIC.
- Fallback: without `assets/` (looked up next to the exe and up to 3 parents, then the cwd) the v8 procedural trees and
  untextured grass are drawn (`m6/fallback_no_assets.jpg`, a copy of the binary run outside the worktree). LOW / HIGH
  always use the v8 trees.
- Fix in this pass: impostors looked transparent (the bake stored the soft alpha of the front-most needle only) and used
  a loose bounding sphere; now binary coverage at 2x super-sampling, a tight vertex sphere, and anisotropic instance scale.
- Night parity (`m6/night_compare.json`, mean 8-bit luma at 1280x720 vs the v8 renders): walk_forest whole 35.78 vs
  35.67, grass 45.75 vs 45.04 (not darker); walk_meadow grass 34.59 vs 34.74 (99.6 %), whole frame 39.35 vs 41.45
  (the asset trees against the night sky are darker silhouettes than the v8 moonlit cones). Grass night lift 1.0.
- GPU (`m6/bench_shots_m6_wsl.json`, WSL D3D12 on the shared RTX 4070, 1080p EPIC, 3 s per shot): vegetation colour
  pass 1.7-2.7 ms in the gameplay views (uav_chase, soldier 1p / 3p), 6.6 ms on the high overview (4.3 M vegetation
  triangles: 5 trees at LOD 0); vegetation depth (pre-pass + shadows) 0.7-1.8 ms; grass 0.4-1.5 ms.
- Tests T370-T375 (glTF loader round trip, card thinning, LOD chain error bound, coverage-preserving mips + grass atlas,
  octahedral impostor taps, vegetation layout). 309 tests pass.
- Open: (1) vegetation budget <= 3 ms is not met on the overview: LOD 0 of the tall conifers is 470 k triangles and the
  thinned LOD 1 / 2 grow twig cards about their trunk anchor (stems poke out); planned fix = per-archetype
  triangle-density LOD thresholds + earlier impostors + outer-card-first thinning. (2) Shimmer metric 2.6-6.6 / 255 on
  vegetation pixels (target 1.5); the non-vegetation pixels already flicker ~2.4 / 255 under the TAA jitter, so the
  TAA resolve (M8 territory) has to be part of the fix. (3) boulder_01 does not simplify (55 k triangles at every LOD).
  (4) The real-exe run was not repeated for this pass (exe rebuilt). (5) M7 (heightfield hills, splatting) not built: a
  draft exists only in `build/m7_snap/m7.tar` (uncommitted, unverified).
