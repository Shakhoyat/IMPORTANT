# WP-G Game feel (agent G) - progress notes

Spec: docs/PLAN_V7.md "WP-G". Newest entries at the bottom (power-outage log).

## Baseline
- 2026-10-07: build/obj-wpg green, 219/219 tests.

## Design (decided)
- Pure CPU parts live in `src/scene/` (the test binary links every `src/*/*.cpp` except `src/app` and
  `src/rendering`, so a `src/app/CameraRig.cpp` could not be unit-tested):
  - `src/scene/CameraRig.{h,cpp}`: quaternion helpers, `RigPose` (eye + orientation + fov), `CameraRig` =
    critically damped spring blend of the *offset* between the old and the new view (moving targets stay smooth),
    free-fly camera (`FlyCam`), game-feel curves (letterbox, FOV kick, hit marker, compass strip).
  - `src/scene/Pov.{h,cpp}`: list of every moving object (`PovSource`), first-person / chase pose of each, picker
    ordering, Insert/Delete cycling order, middle-click ray pick, projectile follow-cam tracking.
- `CameraMode::Pov` (appended to the enum) = riding along with a source that is not possessed (missile in
  flight, UAV-2, an intruder rover before the player touches the controls).
- Rover possession: `Scene::setPlayerIntruder(slot, input)`; that slot skips its HideController and drives from
  the player's input (differential drive, same kinematics); the UAVs keep hunting it (it stays a target).
- Blends only start from the new jump keys; `possess()` itself never blends, so every v6 capture is unchanged.

## Open / requests
- (none yet)

## Progress (2026-10-07, after the session reset)
- Done: src/scene/CameraRig.{h,cpp}, src/scene/Pov.{h,cpp}, Scene player rover (setPlayerIntruder / driveRover),
  CameraMode::Pov, src/app/AppGame.cpp (jumps, ride-along, take control, free fly, picker, game feel, soldier rifle,
  viewmodel, captureGame), AppHud.cpp (picker, POV panel, F11 game HUD, alert vignette), Interactive.cpp keys,
  HudMath helpLines + keyBindingsV7(), tests/test_game.cpp T240-T258: all 19 pass.
- Shared tree: tests/test_audio.cpp (L, WIP) and FrameExplorer.cpp (I, WIP) did not compile at times; I linked a
  test binary without test_audio.o to check (246 tests, only H's WIP T264/T268/T272/T276 failed).
- Fixed the old -Wsequence-point warning in Scene::destroyIntruder (same left-to-right order, explicit).

## Visual checks (RTX 4070, EPIC, 1280x720, `--capture DIR --only game_*`)
- Log: `D3D12 (NVIDIA GeForce RTX 4070)`, `Quality preset: EPIC (auto-detected)`.
- Checked every game_* image: soldier first person (rifle + hands, crosshair, compass, game HUD), rifle fire (tracers,
  muzzle flash, impacts), rover hood cam (alert vignette + "LASER DESIGNATED" while the UAV lasers it; the rover
  drives in tall grass, so its hood view is mostly grass at night: realistic sneak view), rover chase (lasers converge
  on it), UAV-2 gimbal, missile follow (exhaust, target lock ahead) and impact hold (letterbox from the hold blend),
  picker (19 objects, selection bracket in the world), mid-jump frame (letterbox + blend), heli cockpit (v4 hint),
  tank commander hatch, free fly camera panel.
- Tuned after the first pass: viewmodel rifle further out (was filling the right third), hands moved onto the grip,
  rover cams raised (bumper was in the grass), rover chase higher, alert text moved below the lock bracket, missile
  shot waits for a fresh launch, the impact hold starts from the live pose (not the last rendered one).

## Audio (WP-L API) wired
- renderFrame: `audio::ListenerState` (camera basis; velocity of the followed / controlled object when no blend runs).
- UI cues: PickerOpen / PickerClose (F4, Esc), PickerMove, PovJump (interactive jumps: Enter, click, Insert / Delete,
  middle click, End), FreeCam (Home), Possess (take control, possession keys), Release (Backspace), HitMarker (the
  player's own hit), Alert (designation vignette rising past 0.3).

## For CAP / supervisor
- `--capture DIR --only game_a,...` with any `game_` name runs `App::captureGame()` (AppGame.cpp) instead of the v6
  shot list: game_rover_bumper, game_rover_chase, game_uav2_gimbal, game_missile_follow, game_missile_impact,
  game_picker, game_jump_blend, game_soldier_fp, game_soldier_fire, game_heli_cockpit, game_tank_commander,
  game_free_fly (one run, night then 10:30). Default 1280x720, --size overrides. CAP may move these into Capture.cpp.
- DocExport could export `keyBindingsV7()` next to keyBindingsV6() (key table for the report appendix).
- TestKit.h header comment: add "v7: T240-T259 game camera (test_game.cpp)" (I left the shared header alone).
- Plan said src/app/CameraRig.*: it is src/scene/CameraRig.* + src/scene/Pov.* so the tests can link it.

## Open issues
- Soldiers have no jump (Garrison.cpp is not mine); crouch uses the existing stance cycle (Left Alt steps it to the
  wanted stance, passing "wave" for one frame when standing up).
- The helicopter first person is its v4 CameraHint (canopy frame fills the left of the view).
- Rifle tracers damage rovers through Combat (power 0.15 blasts): no rifle sound cue yet (L could add one).
- In captures the jump "from" pose is the last rendered frame (fine interactively, where every frame renders).
