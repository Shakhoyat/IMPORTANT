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

### Fix after the M5 merge: thermal gimbal washed out right after FPV -> gimbal (exe)
In the real exe the gimbal view taken 1 s after FPV was a flat, low-contrast orange (`docs/v9_notes/fixes/before_exe_uav_gimbal_mode_switch.jpg`).
Cause: the renderer keeps one adapted-statistics texture and one TAA history; FPV fills them with the visible HDR image
(log luminance), and the thermal AGC then blended from those values at its 0.5 s rate, so the span stayed far too wide.
The headless capture waited long enough for the AGC to settle, which hid it. Fix (`RendererFrame.cpp` `beginFrame`): a change
of post mode (HDR / thermal sensor / plain) is treated as a camera cut, so the history and the adaptation restart from the new
image (`after_exe_uav_gimbal_mode_switch.jpg`). The same exe re-run (`exe_merge_rerun_log.txt`, NVIDIA 591.86, EPIC, 1080p)
shows UAV FPV 90.2 fps and gimbal 95.6 fps with no other jobs running; the 29-47 fps seen earlier was CPU contention from
parallel builds.

## M9: session recording and deterministic replay (done; the performance pass is folded into the final merge benchmark)
- `scene/SessionLog.{h,cpp}` (pure, tested) + `app/Replay.cpp` + `app/Record.cpp`: every interactive run logs its frames
  (frame time as a hex float, window / framebuffer size, inputs at the point of the frame where they arrived: script at the
  top, window callbacks at the poll; pointer reads by index, only when changed; a world hash every 60 ticks).
  `F9` starts / ends a clip and saves `recordings/session_*.tsrec` next to the exe (REC timer and "CLIP SAVED" toast on the
  HUD); auto-exposure moved to `Shift+F9`. `--session-out FILE` saves the whole run (scripted sessions).
- `--replay FILE` re-runs it in the window (same seed, world arguments, settings, window size; the window's own input is
  ignored, Esc stops) and compares every checkpoint (`replay check: N / N checkpoints identical`).
  `--replay FILE --record OUT.mp4 [--size WxH] [--ss 1|2]` renders the clip offline: one frame per simulation tick
  (exactly 60 fps), a second renderer with its own TAA / exposure history at 2x2 supersampling, NVENC / x264, 30 ticks of
  pre-roll, the procedural sound track muxed in.
- Tests T400-T405 (`tests/test_v9_m9.cpp`): exact text round trip, frame splitting, pointer-read replay, parse errors, and a
  60 s night session (variable frame times with hitches, 217 random key events on a possessed unit) written to text,
  parsed and re-run: 60 / 60 checkpoints bit-identical; one tampered key mid-session diverges only after its tick.
  339 tests, 0 failed.

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

## M4 -- enterable buildings: hinged doors, indoor camera, eye adaptation

- Hinged doors (`scene/Door.{h,cpp}` kinematics, `objects/HingedDoor.{h,cpp}` scene object): a leaf (panel, stiles and
  rails on both faces, kick plate, lever handles with roses and key cylinders, three butt hinges) on one group node
  that turns about the hinge axis. The hinge angle follows a critically damped spring (zeta = 1, `critDampStep`):
  no overshoot, 95 % of the swing after 4.74 / omega. State machine CLOSED -> OPENING -> OPEN (latched on the holder)
  -> CLOSING -> CLOSED, reversible mid-swing (the spring carries the angular velocity). The leaf is a set of dynamic
  collision boxes (`dynamicObstacles`, 4 segments: the possessed soldier's motor, the spring-arm camera uses the exact
  oriented leaf parts), and it stops against any soldier's body cylinder instead of swinging through it.
  - Barracks hall door: east jamb, opens outwards through 180 deg onto the wall where the v8 leaf stood;
    omega 4.2 1/s -> 95 % open 1.13 s after E, latched open 1.90 s after E (capture log), auto radius 4.5 m, close delay 2.5 s.
  - Bunker: the entrance keeps its sliding blast doors (a 0.35 m armoured leaf in wall pockets has no room for a
    hinged one); the generator room doorway off the tunnel got a hinged steel door (90 deg into the room, omega 5).
    The fuel line moved from x 2.3 to 3.9 out of its swing.
  - E: "[E] Open door" / "[E] Close door" at the handle on the player's side; Enter / Exit Barracks through a door that
    is not open yet opens it first and goes through once it is 60 % open (`App::hallMove_`); both views, a soldier or
    the v4 walker.
  - AI walkers (`Garrison.cpp stepRoute`): a route that crosses the doorway within the trigger radius requests the door
    (trigger volume); while a leaf swings, walkers outside its sweep wait; a walker never steps into a leaf (exact slab
    test, 0.2 m clearance); one that holds up a leaf steps back if his way leads into it. An auto-opened door does not
    start while a passer-by stands in its sweep, closes closeDelay after the last request once the sweep is clear, and
    does a safety reverse after 1 s held up by a body (stays shut until that body has left the sweep). The garrison
    steps the doors itself (idempotent per tick), so garrison-only test worlds move them too.
- Indoor camera (`IndoorArmBlend`, `indoorArmParams`): the arm (3.1 -> 1.9 m), shoulder and pitch limit blend on a
  critically damped spring (tau 0.3 s) across the doorway instead of switching in one frame; the probe still pulls the
  arm in at once on contact, so the eye never enters a wall or leaf box (T394).
- Eye adaptation (`scene/EyeAdapt.{h,cpp}`, `RendererFrame.cpp renderStatistics`, `ShadersPostFx.cpp kAdaptFrag` +
  composite): the adapted mean log2 luminance follows the metered one asymmetrically, tau 0.35 s when the view gets
  brighter (light adaptation: pupil + cone bleaching, fast) and 1.1 s when it gets darker (dark adaptation: slow);
  ratio 3.1 (Unreal's defaults: 3 vs 1 EV/s). EV = clamp(key - Lbar, -1.5, 1.5 + B): B is the local exposure bias of
  the indoor light volume around the camera (`IndoorLight::exposureBiasEv`, hall +1.25 EV, feathered over 0.8 m), stepped
  per simulation tick with the same asymmetric constants (deterministic under --seed, T398). Outdoors and in the bunker
  B = 0, so the existing shots keep their exposure (play_hall_outside mean luma 113.72 -> 113.72, play_hall_night 58.61 ->
  58.56). The thermal AGC stays symmetric (2 1/s). `Renderer::resetTaaHistory()` keeps the adaptation across the
  play_exposure_* shots. Merge note: main's later fix (a post-mode change resets TAA history + adaptation in
  `beginFrame`) is compatible: it clears `adaptValid_`, which sets uRateUp = uRateDown = 1 (reset) in the asymmetric adapt pass.
  - Walking into the hall at 10:30 (capture trace): just inside EV +0.70 (bias 0.02), after 1 s +1.45, 2 s +1.75,
    3 s +1.85 (bias 1.16 of 1.25).
- Tests T390-T399 (`tests/test_v9_m4.cpp`): state machine, spring vs the exact response (95 % at 1.133 s vs 1.129 s),
  leaves never in a wall through the swing, bodies stop the leaf, player in/out in first and third person without
  clipping, 6 residents through the initially closed door for 300 s (11 crossings, 9 opens / 9 closes, no overlap,
  longest wait for the door 1.9 s), adaptation constants and asymmetry, local bias, determinism, prompts. 333 tests
  pass (323 before).
- Evidence (`docs/v9_notes/m4/`): `shots_after/play_door_closed|opening|open`, `play_exposure_outside|enter|adapted`
  (1920x1080 EPIC seed 1, RTX 4070 d3d12, after every earlier play shot), `capture_after.log` (door timing, exposure
  trace); `shots_before/` (43b4839) vs `shots_after/` for the v8 hall shots, `capture_before.log`,
  `capture_after_v8shots.log`. play_prompt_door now shows "[E] Open door" on the shut door; play_hall_entered presses E
  twice (open, then enter), so the later v8 shots run 1.4 s later (people moved; exposure unchanged).
- Not done / open: the real-exe input-script run and the workbook sheet `tools/wb_v9_m4.py` were skipped (wrap-up
  request); a door the player opened stops against a walker whose route starts inside its sweep until the player closes
  it (no safety reverse for manual doors); residents can still queue at the PT bar for > 20 s (pre-existing v8
  behaviour, also at 43b4839).
