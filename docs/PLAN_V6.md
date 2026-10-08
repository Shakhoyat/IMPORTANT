# ThermalSense v6: day/night cycle, soldiers, random rover routes, real CV under sensor noise

## Context
User request (2026-10-05): add a daytime scene (UAVs keep patrolling, no searchlights, no rover spawns by day),
8 human soldiers on duty (walk between camp objects by day; at night some off duty in the bunker, some on guard,
some working outdoors by day), more random rover paths, and non-deterministic real computer vision; then update the
report and workbook, re-record the 2-minute video and rebuild the slides. Answers: auto day/night cycle + keys,
with manual control; everything should be *controllable objects*, curvy shapes and dynamic motion that showcase
computer graphics.

Exploration facts that shape the design:
- Night is hard-coded in ~6 places (Lighting.h LightRig, Renderer.cpp:17-19 constants, RendererFrame fog/exposure
  literals, kSkyFrag nightSky, fixed moon dir Environment.cpp:13). Static shadow cache is keyed without light dir.
- Vision already runs on rendered pixels (App.cpp:245-284 -> cv::detectTargets -> Squad -> MultiTracker); missiles home on
  squad designators. "Deterministic" comes from fixed seeds (Mission 0x5eed1234, Combat, AaGun), zero sensor noise
  (grain forced 0), closed-form patrol orbits, fixed 1/60 step in record. Cheats left: App::forceStrike/strikeTimer_ (dead,
  true rover), Capture.cpp:501-524 capture-only fallbacks (true-rover explode, fake kill events).
- No humans exist. Rover planner grid covers only r < 18.3 m; camp sites lie on the apron up to r 34.
- CV thresholds: floor 204 (0.80 heat), core 230 (0.90); rover motor box 0.97 is the only target core.

Library defaults keep v5 behaviour (all 169 tests unchanged); the App switches v6 on via `SceneOptions`.

## Phase 0 (supervisor, first): contracts + stubs, then fan out
- Save this plan as `docs/PLAN_V6.md` with a live status table (power-outage safe), like PLAN_V5.md.
- `GameEvents.h`: append `Dusk, NightFalls, Dawn, DayBreak, ShiftChange, Withdraw`.
- `Controllable.h`: `WorldContext` += `const WalkSpace* walk; float hour, daylight; bool beamsOn`; `Controllable` +=
  `virtual float friendlyRadius() const { return 4.5f; }`.
- `Lighting.h`: `struct Atmosphere` (day/night weights, solar, practicalScale, beamsOn, sunDir/sunColor, skyZenith,
  groundBounce, fogColor, fogAmbient, densities, exposureKeyLog2, purkinje; defaults = v5 literals) in `LightRig`.
- Headers with stub .cpp: `scene/DayCycle.h`, `math/Spline.h`, `scene/Gait.h`, `scene/Garrison.h`, `objects/Soldier.h`,
  `vision/SensorModel.h`; `Scene.h` gets `SceneOptions {seed=0, startHour=23, autoCycle=false, garrison=true,
  randomRoutes=false}` + `deriveSeed(base, stream)`; `Renderer.h` `captureUavCamera(..., uav, const Mat4* override)`.
- Build must stay green (169/169) before agents start.

## Work packages (parallel subagents, disjoint file ownership)
If Sonnet is rate-limited (limit hit earlier today, resets 18:10 Dhaka), run agents on the default model.

**WP-A Time of day, sky, lighting** — owns DayCycle.{h,cpp}, Lighting.h, Renderer.{h,cpp}, RendererFrame.cpp,
ShaderSources.cpp, ShadersPost.cpp, ShadersPostFx.cpp, FrameBuffers.{h,cpp}, tests/test_daycycle.cpp.
- Solar position (lat 22.8°, decl −4.6°): sin e = sinφ sinδ + cosφ cosδ cosH; azimuth; sunrise ≈06:08, sunset ≈17:52.
  Key light = sun (e > −1°, intensity 3.2·smoothstep(−1°,10°,e), Kasten-Young air-mass reddening) else the v5 moon.
- Night preset = v5 literals bit-for-bit; day preset blended by smoothstep weights (sky, ambient, fog, exposure key
  calibrated by supervisor, Purkinje 0). Practicals/beacon scale → 0 by day; `beamsOn` = sun elev < −2°.
- Day sky shader `daySky()` (gradient, Mie halo, sun disc, twilight tint) mixed with nightSky by uDay; stars × night.
- Quantised key direction (1°) + static shadow cache keyed on direction.
- **Floating clouds + cloud shadows (user, mid-implementation)**: one procedural cloud layer (fBm value noise at
  altitude ~120 m, coverage ~0.45, drifting with a wind vector over time) drawn in the day sky (lit by the sun:
  silver lining + darker base, fades at night); the SAME field projected along the sun direction onto every lit
  surface gives a cloud-shadow factor that multiplies the sun term (soft, moving shadows sweeping across the camp).
  Shared GLSL `cloudDensity(xz, time)` in ShaderSources (included by sky + lighting); CPU mirror for tests/export.
  Tests: cloud shadow mean in [0.55, 0.9] at noon, field moves with wind, 0 effect at night.
- Solar loading in `surfaceHeat` only for passive surfaces (uHeat < 0.75): passive peak ≈0.62 < CV floor 0.80;
  CPU mirror `apparentHeat()` with shared constants.
- **SkyClock controllable "SKY / TIME OF DAY"** (possessable: W/S scrub, `\` auto/manual, Enter next phase; sun sphere node).
- Auto cycle ≈ 120 s day + 160 s night, twilight slowed ×1.5; phase events with hysteresis.
- Tests T190-T197 (sun path, gating, night preset == v5, day preset, period/scrub, quantisation, heat cap, SkyClock).

**WP-B Soldiers (8, possessable, curvy, dynamic)** — owns Spline.{h,cpp}, Soldier.{h,cpp}, Gait.{h,cpp},
Garrison.{h,cpp}, tests/test_soldiers.cpp (+ optional FuelBowser curvy truck on a spline road loop by day).
- Figure ≈36 leaves, depth ≥6: ellipsoid torso, capsule limbs (cylinder + joint spheres of equal radius), rounded
  helmet, rifle, backpack; per-soldier look variety. Heat: face/hands 0.82, torso 0.70, max < 0.90 (never a target core).
- Centripetal Catmull-Rom routes (α=0.5) with arc-length table, curvature speed law v = vmax/(1+1.5κ).
- Gait by FK + two-link leg IK: stance/swing foot trajectory (feet world-locked), pelvis bob, counter arm swing,
  spine twist; poses Idle, Guard (head scan), Work (wrench / carry crate / inspect), Sit, Duck (blast reaction).
- SitePlanner: 72×72 1 m grid from static solids + fence ring with inner gate → A* → string-pulling → spline;
  hand-authored bunker leg using WalkSpace floor/collision (waits at closed doors).
- Roster: SENTRY (gate), WATCH (watchtower→sandbag post), GUNNER (apron patrol loop→MG nest), MECHANIC (tank, off duty
  at night), LOGISTICS (crate loop depot↔ammo bunker, off duty), TECHNICIAN (radar→CIC), PATROL (inner loop→guard
  tower), RUNNER (running route, off duty). Staggered shift change at Dusk/DayBreak.
- Possession: WASD walk with collision, Shift run, `\` stand/crouch/wave; third/first-person cameras; friendlyRadius 1.2 m.
  Names avoid slot keywords (TANK, RADAR, DOOR, UAV…).
- Tests T200-T214 (spline C1/arc length, hierarchy/primitives, foot plant, IK, counter-phase, route clearance, roster,
  bunker leg, possession, heat, work loops, duck, cameras, 300 s no-overlap).

**WP-C Game rules + Scene integration + random rover paths** — owns Scene.{h,cpp}, Mission.{h,cpp},
HideController.{h,cpp}, Squad.{h,cpp}, Combat.h (seed setter), tests/test_paths.cpp, fixes in test_squad/test_combat.
- Scene(SceneOptions): seeds via deriveSeed per stream; DayCycle update/applyTo each frame; Garrison + SkyClock added to
  controllables; friendly marks from `friendlyRadius()`; mission runs only when waves allowed; at Dawn → `endNight`,
  alive rovers `setRetreat(exit)` → Withdraw event (not a breach); beams/extra light/beacon off by day.
- Random routes (when enabled): uniform ring spawns (radius jitter), 0-3 via points in a ±70° sector (rejection-sampled),
  objective switch (p 0.3), per-rover personality (noise/exposure weights, speed 0.8-1.25, pause-in-cover prob,
  noise lattice 3-7). Legacy `setObjective` path unchanged.
- Squad: per-mark friendly radius; seeded patrol (random radius/amplitude/breathing), random investigate detours;
  seed 0 = exact v5 orbit.
- Tests T215-T224 (via chains, route diversity, personalities, objective switch, spawn distribution, day gating,
  dawn withdrawal, day lights, patrol randomness, friendly marks).

**WP-D Real CV under sensor noise** — owns SensorModel.{h,cpp}, CvPipeline.{h,cpp}, MultiTracker (if needed),
tools/figdata_main.cpp, tests/test_sensor.cpp.
- Sensor model y = Q(g⊙(PSF*x) + o + c + n): temporal NETD 2 DN, per-UAV fixed-pattern gain/offset + column stripes,
  NUC residual, 3×3 binomial PSF, stuck pixels, gimbal jitter 0.08°, 1 % frame drops; PCG32, seeded per run.
- `detectAll` with `BlobClass {Target, Person, HotClutter, Clutter}` (Person: warm, no core, vertical/elongated);
  `detectTargets` unchanged. Shared `makeSyntheticFrame` for tests and figdata noise sweep (P_d, false alarms vs NETD).
- Tests T225-T235 (noise stats, FPN/NUC, PSF, P_d ≥ 0.97, ≤1 false target/1000 frames, persons never targets, drops +
  coasting, jitter, seed reproducibility, **blind squad never fires** (no true-position targeting), clutter).

**WP-E App/CLI/keys/HUD/director/capture/export** — owns main.cpp, App.{h,cpp}, Interactive.cpp, AppHud.cpp,
Capture.cpp, CaptureLab.*, Record.cpp, DocExport.*, Director.*, HudMath.*, SceneApi.h, Camera.* (if needed),
AudioEngine.cpp, tests/test_app.cpp.
- CLI: `--seed N` (default random_device ^ clock, printed + in HUD/trace), `--time day|night|dusk|dawn|cycle|HH:MM`
  (interactive default cycle from 16:30; capture/benchmark/trace default night, seed 1), `--cycle-seconds`, `--no-garrison`.
- App step per UAV: drop? → jittered true pose render → `SensorModel::apply` → `detectAll` → targets to squad with the
  reported (unjittered) pose. Remove forceStrike/strikeTimer_ and the capture cheats (wait for real kills).
- Keys (free ones): F2 auto/manual, `,`/`.` scrub, `/` next phase, F3 next soldier; help overlay updated.
- HUD: clock + sun/moon glyph, phase banners, "PERS" brackets on person blobs in thermal FPV, NETD/drop line.
- Director: new 120 s timeline (night strike shots first, dawn time-lapse, day flyover, soldiers patrol/mechanic/
  logistics, heli take-off, day thermal with persons, dusk shift change, soldier into bunker, night searchlights on).
- Captures `report/figures_v6/` (day_overview, sky_noon/sunset, day_patrol_spline, day_mechanic, day_logistics,
  day_thermal_fpv_persons, dusk_shift_change, dawn_withdraw, night_searchlights, soldier_closeup/axes/exploded/
  possess_tp/fp, bunker_offduty, plan_routes, lab_cv_noise). DocExport: day-cycle grid, garrison roster/joints/gait,
  sensor config. Tests: update T125/T126/T129/T177; new T237-T238.

**WP-F Docs** (after data exists) — report sections s18 daycycle, s19 soldiers, s20 random paths, s21 real CV, s22
v6 results (equations, diagrams, listings, workbook crops), edits s01/s07/s11/s13/sA/sB; gen_tables areas + parts
macros; listings (Spline, IK, SensorModel::apply, sun position); make_figures (spline, soldier tree, gait plots,
day-cycle curves, P_d vs NETD); workbook `tools/wb_v6.py` sheets 21 Day Cycle & Lighting (Excel formulas recompute sun
elevation), 22 Soldiers & Routes, 23 CV Noise & Seeds, sheet 14 notes, computed possessable count, audit/trace rows;
xlsx_snapshot + sheet_figs; slides +3 (day/night, soldiers, real CV) and video slide; README.

## Order
Phase 0 → A, B, C, D in parallel (C wires A/B when they land) + E early parts → supervisor integration, day exposure
calibration, traces, captures, video (background) → F (report, workbook, slides) → final checks.

## Risks
Daylight CV false positives (passive-gated solar term, T196/T229); soldier near rover blob merge (still a target);
day exposure calibration; shadow-cache churn in time-lapse (quantum 2° in record if slow); llvmpipe video ≈1 h;
route clearance in dense camp (A* + sample validation); slot-keyword name collisions; keep -Wall -Wextra clean; brief
agents that soldiers are friendly personnel with no harm model.

## Verification
1. `make HEADLESS=1 CXX=zcxx CC=zcc build/tests && ./build/tests build/test_results.json`: 169 old + ~47 new pass.
2. Night regression capture with `--time night --seed 0 --no-garrison` matches v5 figures.
3. Day calibration capture → set day exposure key.
4. `--trace … 600 --time cycle --seed 42` twice identical; `--seed 43` differs; spawns only at night, Withdraw at dawn,
   kills only from vision-designated rounds.
5. Captures into report/figures_v6; video `--record report/demo_v5.mp4 120 --seed <chosen>` (ffprobe: video + AAC).
6. `make docs`, verify_workbook PASS, xlsx_snapshot, sheet_figs, gen_tables, listings, prepare_images, tectonic
   (0 pending, no undefined refs, < 25 MB), build_slides; Windows exe rebuild.

## Working rules (user, 2026-10-05)
- Always keep the workbook updated: after every work package lands, the supervisor runs `make docs` (exporter +
  tests JSON + build_workbook), `verify_workbook.py` (must PASS); new data goes into the export / wb_v6.py as it appears.
- The report is updated only once everything else is complete, step by step (one section per step, compile after each),
  token-efficiently: generated tables / macros / captions instead of hand-written numbers, short prose.

## Status (live; update after every step)
| Step | Owner | State |
|------|-------|-------|
| Phase 0 contracts + stubs, build green | supervisor | done 15:46 (169/169) |
| WP-A time of day, sky, lighting, clouds | agent A | DONE (T190-T199; day key -2.8; notes wp-a.md) |
| WP-B soldiers | agent B | DONE (T200-T214 pass on 2026-10-07 g++ build) |
| WP-C scene integration, random routes | agent C | DONE (T215-T224 pass; notes wp-c.md) |
| WP-D sensor noise CV | agent D | DONE (T225-T236 pass; cv_noise.json; notes wp-d.md) |
| WP-E app/CLI/HUD/director/capture/export | agent E | DONE (T237-T239 pass on 2026-10-07 g++ build) |
| Integration, calibration, traces, captures, video | supervisor / agent CAP | traces + night regression + captures DONE 2026-10-08 (report/figures_v6 18/18; notes docs/v7_notes/wp-cap.md); video todo (after v7 features) |
| WP-F2 workbook (sheets 21-23, audit, snapshots) | agent F2 | DONE (verify PASS; data pending on WP-E export; rebuild after each WP) |
| WP-F1 report (after everything else) | agent F1 | todo |
| Final checks (tests, exe, pdf, xlsx, pptx, mp4) | supervisor | todo |

### Integration log
- 2026-10-06: shared tree builds; 204/204 tests pass (WP-B fixes + Scene gc.controllables wiring by supervisor).
  Workbook rebuilt from fresh export: verify PASS; sheet 21 97/97 sun-elevation rows match; sheet 22 8 soldiers;
  sheet 23 35 v6 tests pass / 15 pending; 30 'pending:' cells remain until WP-A/B/E finish.
- 2026-10-07: repo moved to a vscode-owned copy (original kept at ../IMPORTANT.orig-user, no sudo on the WSL box);
  toolchain is plain g++ (no zcxx/zcc): build with `make -j32 HEADLESS=1 OBJ=build/obj-<x> TESTS=build/tests-<x>`.
  Garrison.h needed `#include <cstdint>` for g++. 219/219 tests pass. Tools in ~/.local/bin (uv, tectonic, ffmpeg).
- 2026-10-08 (agent CAP, RTX 4070 / EPIC): verification step 2 night regression (--time night --seed 0 --no-garrison,
  39 v5 shots): static shots match v5 (MAE 0.4-5.8 / 255), event shots differ by game moment only (real kills, sensor
  noise); step 4 traces 600 s --time cycle: seed 42 x2 byte-identical, seed 43 differs; 0 spawns by day, withdrawals
  only from Dawn (2 per seed, one via the 90 s retreat timeout), every kill from a UAV round on a confirmed vision
  track (traceMode now writes FILE.events.csv + a 'rules:' check line). Captures report/figures_v6 18/18 reviewed.
  make docs equivalent (g++, shared tree): export schema 6 (+ v7.keys), tests 289/289, verify_workbook PASS,
  0 'pending:' cells, xlsx snapshots 63 (ASIN / ACOS / ATAN2 / PI / ROWS added to the snapshot evaluator).

