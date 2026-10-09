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

## M3: character motor, spring camera, locomotion (done)
- `scene/CharacterMotor.{h,cpp}` (pure CPU, used by `Soldier::stepPossessed`): per fixed tick, the horizontal velocity
  follows the wish on a critically damped spring (`critDampStep`, exact for any dt: w 17 1/s accelerating, 30 1/s
  braking, 20 % in the air and no braking there); sprint / crouch blends are 0.2 s smoothstep ramps and the capsule
  height follows the crouch blend (stays crouched under an overhang); coyote time 0.12 s and a 0.15 s jump buffer live in
  the motor (App still sends the edge, `jumpEdge_`); slope limit 40 deg from a multi-scale ground gradient (`groundSlope`:
  a step or a floor edge is not a slope); step-up through `stepFoot` (0.35 m passes, 0.5 m blocks); a step up / down
  moves the collision feet at once and the drawn feet follow on a spring (`Soldier::drawnFeetY`). The v7.1 linear
  ramps (`cmdSpeed_` / `strafe_`) are gone. Everything is a function of the tick inputs (T357: bit-identical replay).
- Camera: `ThirdPersonCam` pivot follow and arm ease-out are critically damped springs (w = 2 / tau: same lag behind a
  steady walk as the v8 exponential, no overshoot, T354); the arm still pulls in at once; the shoulder point now stops
  2 cm short of a contact (a shoulder exactly on the probe boundary could round to inside a box, and the arm cast from
  there ignored that box: found by T355, 500 random poses near walls). The pivot rides on the drawn feet and the crouch
  blend. First person: a stable eye (crouch-blended height, landing dip) + `headBobOffset` from the gait phase and
  ground speed (`App::headBob_`, default on, M2 adds the toggle); the possessed soldier's head no longer scans with the
  guard / idle pose (the first-person view used to wander +-55 deg when standing still), mouse look is direct.
  Sprint FOV kick: +6 deg x sprint blend x speed / 3.4 m/s, eased, in every mode (was game mode only, 8 deg).
- Locomotion (`Gait.cpp locomotionGait`, `Soldier::animate`): walk -> run -> crouch-walk parameters blended by the actual
  ground speed (shorter strides below walking speed; run elbows blend in instead of switching at 2.5 m/s); the phase
  advances by distance / stride for every soldier; the leg IK now runs on the final blended pose: stance feet
  world-locked, swing feet on the gait path in the direction of travel (hip roll for strafe / backwards), standing feet
  on the ground under them (slopes, steps), pelvis drop when a foot is out of reach, feet pitched with the ground.
  Rendered stance-foot slip <= 0.22 cm at 0.46-3.4 m/s incl. crouch, backwards, strafe, diagonal (T356).
- Tests T350-T359 (`tests/test_v9_m3.cpp`); 313 tests pass (303 before). No existing test changed.
- Evidence (`docs/v9_notes/m3/`):
  - `m3_video.txt` / `m3_video.log` / `m3_session.mp4`: ThermalSense.exe 960x540 EPIC session (shot every 0.1 s ->
    44 s MP4): walk (90 % of 1.3 m/s after 0.231 s, below 5 cm/s 0.167 s after release), sprint (90 % after 0.305 s,
    stop 0.211 s, FOV kick to 5.9 deg), running jump onto the 0.85 m road barrier and off it, crouch (eye 1.52 -> 0.95 m
    in 0.2 s, crouch walk 0.8 m/s), buffered double jump, third-person orbit beside the barrier (arm 3.10 -> 2.08 m at
    once, eased back 2.35 / 2.86 / 3.04 / 3.10), third-person walk with a camera swing and a sprint jump.
  - `m3_session.txt` / `m3_session_run3.log` / `exe_frames/`: the same session at 1920x1080 with 15 key shots (a stray
    real cursor event pitched the view down in this run; the motor numbers are unaffected).
  - `make_scripts.py`: generates both scripts. First-person turns use the held arrow keys (60 deg/s): scripted mouse moves
    in the real exe can be offset by a real cursor event (M0 harness: `onCursor` mixes script and GLFW positions).
  - `shots_before/` (M1 exe) and `shots_after/`: headless `play_*` 1920x1080 EPIC seed 1, new `play_sprint_fov`,
    `play_crouch`, `play_jump` (taken after the v8 shots, so those stay comparable).
- Workbook: `tools/wb_v9_m3.py` (sheet "32 v9 Character Motor & Camera", `build_v9_m3(wb, ctx)`; not yet wired into
  `build_workbook.py`): concepts, named design constants, exe speed curves vs the spring model with Excel formulas
  (interpolated 90 % / 5 cm/s crossings), crouch blend, buffered jump, orbit arm, T350-T359, captures.
- Not done / open: a standing jump into the 0.85 m barrier does not clear it (needs a run-up; 0.72 m apex by design);
  looking straight down in first person shows the soldier's own chest (pre-existing; the body is not hidden);
  `stepFoot`'s snap-down is 0.32 m, so a 0.35 m drop is a short fall (landing dip) rather than a smoothed step.
