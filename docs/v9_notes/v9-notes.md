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

## M5: UAV game view, rotor blur, flight HUD, duty cycle, day ops (done)
- **Duty FSM** (`ai/SquadDuty.cpp`, the v7.1 WP-D declarations in `Squad.h` now implemented; the `Scene::uavAirborne` /
  `uavDutyState` stubs replaced): DOCKED -> TAKE-OFF (stagger 2.5 s, spool-up 2 s, vertical climb <= 1.6 m/s to 4 m)
  -> PATROL (the squad) -> RETURN (flyTo the hover point 5 m over the pad) -> LANDING (descent clamp(0.5 h, 0.25, 1.4) m/s)
  -> DOCKED; MANUAL while the player flies (released: PATROL / TAKE-OFF if wanted, else RETURN). Duty wanted = searchlights
  allowed (sun < -2 deg) or rovers on the field or day ops. A UAV off duty is "away" for the squad like a hand-flown one;
  its camera renders nothing (`Scene::uavVisionActive`, `App::stepSim`), its searchlight is off, the feed reads DOCKED.
  Night-start scenes are unchanged (all 303 earlier tests pass unmodified).
- **Pads** (`objects/UavPad.{h,cpp}`): two flush pads on the bunker roof (probe: > 2 m from every standing part), slab +
  painted ring + two landing rails under the skids (the gimbal hangs clear) + 4 green lights (steady docked, blinking
  during take-off / landing). Flush: no obstacle, no occluder.
- **Rotor blur**: rotor angle = full-speed angle minus the lag accumulated while spooled down (identical at full spool);
  blur = smoothstep(8..22 rev/s): blade opacity 1 - blur (plain alpha via the new `uBlurDisc` uniform), disc opacity
  0.32 x blur with sweeping crescents + radial streaks; the translucent pass also runs in the thermal view.
- **Chase camera** (`scene/UavView.{h,cpp}`, `app/AppUav.cpp`): critically damped springs (exact step) on the follow point
  (w = 10) and the heading (w = 5), lower-third framing (drone at NDC y = -0.40), bank 0.3 deg per deg/s of turn, pitch tilt
  1.8 deg per m/s^2, sphere-cast collision (probe 0.25 m + thin 0.08 m) against the static solids / vehicle parts / walk
  space. FPV nose camera (92 deg, 14 deg up-tilt, rolls with the airframe) and the gimbal camera; `V` cycles
  (`App::cycleUavView`, also for UAV-2 ridden along). The UAV game HUD replaces the possession panel in those views.
- **Controls fix**: `D` / Right / mouse-right yawed the UAV and its gimbal LEFT (yaw grows towards +x = the airframe's left);
  now right, as `ControlInput::turn` documents.
- **Day ops**: `--day-ops`, `Shift+F2` (`App::setDayOps` / `dayOps` for M2's menu); waves allowed by day, the UAVs
  scramble; off by day = the wave ends like a dawn. Trace: `FILE.duty.csv` (duty timeline), spawns checked against the
  scene's rules (`dayRules().wavesAllowed`).
- Tests T360-T369 (`tests/test_v9_m5.cpp`): full-day FSM, stagger / climb / touch-down, rotor blur, day ops -> confirmed
  track by day, determinism, lower third + no overshoot, 500-pose collision, bank / tilt, FPV / gimbal / HUD math, manual
  flight off the pad. 313 tests, 0 failed.
- Evidence: `m5/captures/` (play_uav_*; capture log with NDC of the drone, arm length), `m5/before/` (96782ee),
  `m5/exe/` (ThermalSense.exe, uav_night.txt / uav_day.txt: chase, FPV, gimbal, HUD, docked by day, spin-up, day ops),
  `m5/duty.csv` (full cycle: RETURN 08:01 once the field is clear, docked 09:19, TAKE-OFF 18:01) and `m5/duty_dayops.csv`.
- Workbook: `tools/wb_v9_m5.py`, sheet 34 "v9 UAV View & Duty" (needs the hook in build_workbook.py).
- Open: frame times above were measured on the shared GPU; the chase HUD sits on the v5 diagnostic HUD until M2's declutter.
- Merge with M2 (main): `V` goes through `cyclePersonView()` (UAV: chase -> FPV -> gimbal first, then soldier / vehicle /
  ride-along); the pause menu's TIME page has a **DAY OPS** button (key `O` in the menu) next to AUTO CYCLE / SPEED, so
  daytime enemies are a flag (`--day-ops`), a key (`Shift+F2`) and a menu toggle; `Shift+F2` and the menu `O` are in the
  key map (still 0 conflicts). The flight HUD replaces the debug corner blocks (F3 brings them back) and the `[E]` prompt
  sits under the heading tape in chase / FPV, clear of the reticle and lock ring. `captures/` were re-shot after the merge.
