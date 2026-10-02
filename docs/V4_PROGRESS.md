# ThermalSense v4 — progress and to-do (recovery file)

Saved in the repo so work can resume after a power cut or a lost session. Updated by the supervisor at every
milestone. Engineer notes: `docs/v4_notes/v4-<id>.md`; shared rules: `docs/v4_notes/v4-brief.md`.

## Request (user, 2026-10-02)
Audit the project and make it visually better as a **military base**; the **bunker goes underground** and is
walkable (tunnel / rooms); the **UAV shoots the intruder rovers** and they **blow up properly**; **at least 5 new
controllable objects**; keep the **Excel workbook and the report** updated, with an audit of every detail.
Course checklist (phone note): 10-page slide presentation, outline on page 2, 2-minute video at the end of the
slides, dynamic + controllable objects, complex motion, proper presentation + report + slides.

## Architecture decided (shared headers, written by the supervisor)
| File | Purpose |
|------|---------|
| `src/objects/BaseLayout.h` | site plan: base radius 36 m, perimeter 34 m, ramp / bunker footprint, helipad, radar, AA gun, motor pool, barracks, flagpole, depot, 3 rovers |
| `src/scene/Controllable.h` | ControlInput / WorldContext / CameraHint / Controllable interface |
| `src/scene/EffectsData.h` | Projectile, EffectParticle, DynamicLight, ScorchMark |
| `src/scene/Combat.h` | combat API (weapons, explode, targets, blasts, effects state) |
| `src/scene/WalkSpace.h` | floors / walls / indoor volumes for walking in the bunker |
| `tests/TestKit.h` | per-area test files with `TS_TEST_GROUP` (IDs T60-T129 by area) |
| `src/rendering/SceneAccess.h` | renderer detects `scene.combat()` / `scene.walkSpace()` at compile time |

## Status
| Id | Work | State |
|----|------|-------|
| sup | Surfaces 12-18 (corrugated, HESCO, hazard, screen, asphalt, camo, steel plate) | done |
| sup | 16 practical lights + combat dynamic lights, indoor mask (no sky / moon / rain / little fog underground), node `visible` / `dynamic` flags, shadow cascade for r = 36 m, effects pass hook | done (builds) |
| sup | Test split into tests/*.cpp + TestKit | done, 55/55 passed before the agents started |
| C2 | Effects renderer (fire, smoke, sparks, flash, shockwave, tracers, missiles, scorch, thermal) | done (harness verified; not yet seen in the real scene) |
| B1 | Tank (216 parts, 80 animated track links) + AA Gatling gun (57 parts), T80-T89 | done (tests passed against a stub Combat) |
| B2 | Helicopter (68 parts, cyclic rotor) + radar (102 parts), T90-T99 | done (10/10 in a private link) |
| A1 | Underground bunker, WalkSpace, terrain hole / mound, blast doors, T60-T69 | interrupted (rate limit) — files in progress: Bunker.cpp, Terrain.cpp, WalkSpace.cpp, test_bunker.cpp |
| A2 | Military base apron (HESCO, gate barrier, helipad, barracks, motor pool, flag), backdrop, ground map, T70-T79 | interrupted (rate limit) — Base.cpp written, others in progress |
| D | Possession keys, chase/sight cameras, walk camera in the bunker, HUD, director (2 min), captures, T120-T129 | interrupted (rate limit) — Control.cpp/h, SceneApi.h, test_app.cpp added |
| C1 | Combat.cpp, 3 rovers, destruction + debris + wreck + respawn, UAV missiles, manual drone, Scene wiring, T100-T119 | first attempt stopped (auto-mode check refused the Combat.cpp write); relaunched 2026-10-02 |

## Blockers
- The full program and the test binary do not link until `src/scene/Combat.cpp` exists (tank, AA gun and
  helicopter call Combat).

## To-do (in order)
1. C1: Combat.cpp + Scene wiring (every factory under `__has_include`), rovers, UAV weapon, manual drone.
2. A1 / A2 / D: finish (resumed).
3. Supervisor: integrate, fix cross-module issues, all tests green, captures of every new object and of
   explosions, review by eye, EPIC showcase set in `report/figures_v4/`.
4. Video: 2-minute director sequence (`--record report/demo_v4.mp4 120`).
5. Docs (agents): report rewrite with a full audit appendix, workbook update (new sheets: controllable
   objects, combat, bunker, audit), 10-slide deck (outline on slide 2, video on the last slide), README.
6. Windows exe rebuild (`tools/cross_build_windows.sh`), final test run, commit when the user asks.
