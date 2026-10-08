# v8: daylight, open world, barracks life (notes)

Request (2026-10-09): rework daytime visual quality (colour interpolation, shading, lighting, reflections, shadows,
dynamic objects that do not look detached); controllable soldiers / UAVs; less predictable enemy movement for a
future CV tracker; W A S D free movement; first / third person; click a soldier to select / possess; open-world camp;
enterable buildings with living NPCs; modular architecture.

Status: done. Tests 298/298 (`build/tests-*`, run from the repo root: T167 needs `./build`). -Wall -Wextra clean
(headless and the GLFW window build). Checked on the RTX 4070 (Mesa d3d12, EPIC). Benchmark at 1280x720 within noise of
the pre-v8 build in the same session (EPIC 55.3 -> 61.1 fps).

## What existed (survey)
- Renderer: forward HDR, GGX in `ShaderSources.cpp` (`kLighting` / `kOutput`); 2-colour hemisphere ambient
  (`uSkyZenith` / `uGroundBounce`); day presets in `DayCycle::applyTo`; post in `ShadersPost*.cpp`.
- v7.1 WP-T had left pure-CPU parts (stepFoot, sphereCast, ThirdPersonCam, thirdPersonSoldierInput, Soldier jump /
  strafe) but no App wiring; `App.h` declared ~15 functions that had no definitions. WP-U's guide layout (HudMath) is
  still unwired; WP-D's `uavAirborne` is a stub; both left as they were.
- The UAV computer vision (sensor model -> blobs -> Kalman / Hungarian tracker) already existed and is unchanged.

## Daylight (before / after: shots/before_*.jpg, shots/after_*.jpg)
| Cause | Fix |
|---|---|
| grey, milky scene: squared-exp fog 0.0075 + volumetric haze 0.0032 with a pale ambient fog | 0.0042 / 0.0013 |
| flat ambient (zenith / ground only), blue-grey shade | `dayAmbient()`: dome as seen by the face + horizon band + ground bounce + circumsolar |
| dull reflections | `skyEnvironment(R, rough)` for `t.sa` and wet ground |
| low daylight contrast | log-space contrast 1.10 around mid grey, saturation +10 % by day |
| teal pines, straw-coloured grass, glowing blades | green spruce cards, greener tufts, day translucency 2.0 -> 0.55 |
| white blobs + rainbow flare ghosts | sun glints on thin galvanized cylinders (GGX ~100x mid grey): geometric specular AA, specular LOD per thin curved part, roughness 0.52 |
| UAV lens / heli beam cone lit by day | follow the night `beamsOn` rule |

## Player control (app/AppPlay.cpp)
Hover / click select (PovSource ray pick), Enter or double click -> `controlSource()`; soldiers in first person, V / C
-> spring-arm third person (`thirdPersonTarget`, oriented-box probe `NearBox`, indoor arm), camera-relative input
(`shapePlayerInput`), RMB aim / LMB fire (third person fires from the rifle muzzle to `aimPoint`), Space jump, Esc
release. Captures: `--only play_*` (`App::capturePlay`).

## Barracks hall + residents
`objects/BarracksHall.cpp` (layout in `BaseLayout.h::hall`), `WalkSpace::pickFloor` / `IndoorLight`,
`Garrison::build(seed, residents)`, `Duty::Resident`, activities Sleep / PushUps / PullUps / SitUps / Talk
(`Gait.cpp`, `Pose::bodyPitch`), `Stop::settle` / `shared`, unsettle before leaving a timed settle stop (the planner
would otherwise snap the bunk to a cell outside the wall). `SceneOptions::residents` defaults to 0 (tests keep the
8-soldier world); the app uses 6.

## Natural rover motion
`RoverPersonality::{speedWander, weave, weaveWavelength, hesitate, detour}` hashed from the route seed in
`Mission::draw`; `HideController::naturalMotion / weaveLine` + stuck guard. Defaults 0 = the v6 rover.

## Open / not done
- Shadows: filtering unchanged (PCF 8/16 taps); no contact-hardening penumbra.
- No daytime enemy activity: rover waves still only at night (v6 rule). Scrub to night (`,` `.` `/`) to see the
  rovers' natural motion, or add a day mission if the CV work needs daylight targets.
- Residents' talk partners are positional (two seats / spots facing each other), not a conversation system.
- Report / slides / workbook not updated for v8.
