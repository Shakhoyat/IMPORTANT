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

## M2: unified game controls, interaction prompts, pause menu, settings (done)
- **Interaction (E)** `scene/Interact.{h,cpp}` (pure) + `app/AppInteract.cpp`: each frame the App lists what can be used
  (doors / gate barrier 2.5 / 3 m, the barracks hall doorway in / out 2.5 m, other soldiers 2.5 m, vehicles and UAVs 2 m +
  hull radius on foot; from an orbit / free camera the selection, else the unit within 9 deg of the view ray up to 90 m;
  "release" while a vehicle / UAV / rover is controlled; "take control" while riding along). `pickInteractable`: nearest
  within reach, inside the view cone (on foot 50 deg, headings only), with a clear line of sight (`segmentClear`: WalkSpace
  walls + static solids, the target's own box ignored, window gaps do not block). Prompt drawn bottom centre / under the
  crosshair in the v8 prompt style: `[E] Open blast doors`, `[E] Enter Barracks`, `[E] Take control of UAV-1 DRONE`,
  `[E] Ride UAV-2 DRONE (WING)`, `[E] Release`. Enter / Exit Barracks moves the soldier (or the v4 walker) through the
  doorway with a camera blend (M4 replaces this with a hinged door).
- **Keys** (table = `scene/KeyMap.cpp keyMapV9()`, 123 bindings, 0 conflicts, `--export-keys FILE` -> `m2/keys.json`):
  E interact (explode -> G), Tab / Shift+Tab next / previous unit (soldiers, UAVs, vehicles; HUD toggle -> F3), V the only
  view key (soldier 1st/3rd, vehicle chase/sight/alt, ride-along view; C keeps only the overview camera cycle), Esc release ->
  clear selection -> pause menu (no more quit; Quit is a menu tab), P pause (Phong -> the O shading cycle), Space jump only,
  F view mode (was V; flat shading -> O cycle), F3 HUD level game / debug / off (was "next soldier": Tab reaches every
  soldier). README Controls + v4 / v6 / v7 / v8 key rows and F1 help (`HudMath helpLines`, `keyBindingsV6/V7`) rewritten.
- **Pause menu** (`scene/GameMenu.{h,cpp}` layout + `AppInteract.cpp` drawing / input): tab bar RESUME / TRAVEL / TIME /
  VIEW / SETTINGS / QUIT above the v7.1 field guide panel (`guideLayout`, `guideActionAt`, `guideSliderHour/X`,
  `guideKeyAction`, `guidePresetHour`, `guideSpeedFactor`), all wired: ride UAV-1 / UAV-2, random soldier 1st / 3rd, POV
  list, free camera, tank, helicopter, intruder rover (disabled with a note by day), dawn / noon / dusk / midnight, slider
  drag, auto cycle, speed x1/x2/x4/x8/x0.5, person view, render mode, Frame Explorer, thermal, HUD level, UAV feed. The
  world holds while it is open, the cursor is released, mouse hover / click / drag work; time changes re-light at once.
- **Settings**: sensitivity (applied in `onCursor` to every look path), invert Y, FOV on foot (first person; third person
  3 deg narrower; default 65 = unchanged captures), vsync + frame limit (`applyDisplaySettings`: swap interval + FramePacer
  target, re-applied at runtime), head bob (stored; `App::headBobEnabled()` for M3), quality. Saved to `ThermalSense.ini`
  next to the exe (`--ini FILE|none`); interactive mode only; `--vsync` / `--fps-limit` / `--quality` win for that run.
- **HUD declutter**: the corner blocks (title / switch rows, UAV / rover status, FPS / draws / triangles + frame graph)
  only at the F3 DEBUG level (interactive starts at GAME; offline report captures keep the v8 HUD except play_*).
  Soldier: crosshair (1st and 3rd person), prompt, status chip (stance, view, speed, rifle); vehicle / UAV: its own HUD;
  overview: top bar, mini-map, clock, UAV feed, one hint line.
- **Tests** `tests/test_v9_m2.cpp` T340-T349 (pick in cone / reach, wall blocks line of sight, key map conflicts 0 and the
  v8 overloads detected, required bindings, ini round trip, setting steps + look scaling, menu layout hit tests at 4 sizes,
  Tab order, prompt texts, the real hall doorway vs its walls / windows): 313 / 313 pass.
- **Evidence**: exe run `m2/m2_controls.txt` -> `m2/m2_controls.log`, frames `m2/exe_frames/` (native NVIDIA 591.86,
  1920x1080 EPIC, seed 1, 10:30): Tab -> soldier 1, 200 px look = 30 deg at 1.00x; Tab x3 -> mechanic with
  `[E] Take control of MH-6 HELICOPTER`; E -> helicopter chase view with `[E] Release`; V -> cockpit sight; Esc -> released;
  Esc -> selection cleared; Esc -> pause menu (TRAVEL); click SETTINGS; click + twice -> 1.20x (written to
  `m2/ThermalSense_after_script.ini`); click RESUME; Tab -> soldier 1, the same look = 36 deg. Headless captures
  `m2/shots/` (before_* = v8 HUD): play_select, play_first_person, play_third_person, play_prompt_uav, play_prompt_door,
  play_hall_entered, play_pause_menu, play_pause_time, play_settings. Workbook: `tools/wb_v9_m2.py` (sheet 31).
- Not done / limits: E on a resident in the probe run depended on where the residents stood (they wander), so the exe
  script uses the mechanic at the helicopter (fixed day work spot); the hall entry is shown in the headless capture
  (play_prompt_door -> play_hall_entered). The rifle has no ammunition model, so the soldier chip says AMMO UNLIMITED.
  No frame-time numbers here (shared GPU).
