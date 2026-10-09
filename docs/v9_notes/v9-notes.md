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
