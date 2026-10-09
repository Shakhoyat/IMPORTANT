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

## M8: lighting depth: PCSS, contact shadows, GTAO, analytic sky, aerial perspective, AgX (done)
- Shadows (`kLighting`, `math/ShadowMath`): PCSS on the sun cascades (HIGH+): a blocker search over a Poisson disc,
  then the penumbra `w = (dR - dB) * wL / dB` (sun at infinity: `gap * 2 tan(0.8 deg)` = 2.8 cm per metre of gap), a
  Poisson PCF kernel of radius `w / 2` (1.6 texels at contact, clamped to 40). Stored depth x `depthRange` = metres
  (T389). Near / world cascades cross-fade over the last 20 % of the split with a smoothstep (T381). The moon keeps
  the v8 filter.
- Contact shadows (`ShadersLightingDepth.cpp`): a 0.6 m screen-space march (16 steps at EPIC) towards the sun through the
  pre-pass depth, by day. Soldiers' feet, vehicle wheels and props now sit on the ground (`feet_contact_off_on.jpg`,
  `mechanic_off_on.jpg`, `feet_m1_m8.jpg`).
- GTAO replaces SSAO at EPIC by day (Jimenez 2016 horizon search, 3 slices x 8 steps at EPIC, per-frame rotated, bilateral blur + TAA accumulation). Nights
  and LOW / HIGH / ULTRA keep SSAO.
- Sky (`math/SkyModel`): a Preetham sky with a limb-darkened sun disc and lit procedural cumulus, driven by
  `DayCycle::sunDirection` (T385, T386). The day ambient is the order-2 SH projection of the same sky plus the sunlit
  ground (T387, T388: noon zenith ambient L 0.44, v8 0.49), so ambient matches the sky that is drawn. Aerial
  perspective in-scatters the sky plus a Henyey-Greenstein sun lobe by distance and height.
- Tone mapping (`math/ToneMap`): AgX (Sobotka, base contrast look) is the default by day; the night keeps the v8 ACES
  curve (its grade was authored for that toe), with a twilight cross-fade by the day weight (T382-T384; T383: AgX
  clips 0 / 8 saturated samples, ACES 1 / 8; hue drift 13 vs 28 deg). `TS_TONEMAP=aces|agx`,
  `RenderSettings::toneMap`. Ablation: `TS_LIGHTING_OFF=pcss,contact,gtao,aerial`.
- Night parity: SSIM against the M1 renders (96782ee, same `--only` lists): 1.000000 on all 7 night shots
  (walk_forest, cine_chase, base_overview, night_searchlights, cine_establishing, base_gate, helipad), re-measured
  after the final build. Against v8 (138163d) the values are those M1 already had (walk_forest 0.968, searchlights
  0.946, others >= 0.987): the night differences are from M1 (grass strips / fixed tick), not M8
  (`m8/night_ssim.txt`, `v8_vs_m1_*.jpg`).
- GPU (GpuTimer, `--bench-shots` 1080p EPIC, 2 rounds, shared GPU so +-1 ms noise; `m8/gpu_passes_summary.txt`):
  GTAO 0.53-1.71 ms (WSL) / 0.59-1.45 ms (exe) vs SSAO 0.39-0.59; contact shadows 0.10-0.17 ms; PCSS is inside the
  scene pass: +1.0-1.9 ms by day (WSL); composite +0.013 ms. Night passes unchanged. Exe day frames 9.1-11.7 ms.
- Exe run (native NVIDIA, 10:30, input script): `m8/exe_m8_*.jpg`. Workbook: `tools/wb_v9_m8.py`, sheet
  "37 v9 Lighting & Shadows". Tests T380-T389.
- Open: the v8 night-forest / searchlights deltas from M1 (above) belong to M6's grass night-parity check.
