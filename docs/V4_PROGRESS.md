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

## Status (2026-10-02, afternoon)
| Id | Work | State |
|----|------|-------|
| sup | Surfaces 12-18, 16 lights + combat lights, indoor mask, node flags, r = 36 m cascade, effects hook, test split | done |
| sup | Scene integration (all controllables in key order, 3 intruders with destroy -> debris -> burning wreck -> respawn, UAV launcher with 4 rail rounds + 10 s reload, lock >= 1.5 s, wreck / friendly (IFF) discrimination, ramp keep-out, manual drone control) | done, builds, 106/106 tests |
| sup | Old tests T13/T26/T38/T41/T43/T52/T53 rewritten for v4 | done |
| sup | Calibration: effect brightness per type (Combat::spawn), screen glow, flare off indoors, tank close-up camera | done |
| C2 | Effects renderer | done; look tuning in the real scene next |
| B1 | Tank (216 parts) + AA gun (57) | done; visuals in scene next |
| B2 | Helicopter (68) + radar (102) | done; visuals in scene next |
| A1 | Underground bunker (357 parts), WalkSpace (12 floors, 36 walls, 5 indoor volumes), terrain 36 m with hole + mound | done; interior lighting polish next |
| A2 | Base apron (1175 parts: HESCO wall, 4 towers, gate barrier, helipad, motor pool, barracks, flag, windsock), backdrop, ground map | done; night look polish next |
| D | Keys 5-0 / [ ] / Backspace possession, chase / sight cameras, walk in bunker, HUD sights, director 120 s / 12 shots, 30+ capture shots, T120-T129 | done; verification of captures next |
| C1 | Combat.cpp written by the relaunched agent before the model safety filter stopped it; the rest done by the supervisor | done |

First integrated captures (HIGH, llvmpipe): base overview reads as a military base; tunnel OK; CIC washed out by
screens (fixing); explosion mid-phase good but posterised; early flash white-out (fixing); tank too dark.

## Blockers
- none (everything links; 106/106 tests).

## To-do (in order)
1. Polish round (agents resumed): bunker interior light (A1), base night look (A2), tank/AA (B1), heli/radar (B2),
   explosions (C2), capture/director verification + HUD shots (D).
2. Combat tests T100-T113 (supervisor) — done: 120/120 tests pass.
3. Docs started in parallel: E1 report rewrite with audit appendix (report/main.tex, tectonic), E2 workbook
   (exporter + 4 new sheets: controllables, bunker & base, combat & effects, audit).
3. Supervisor: integrate, fix cross-module issues, all tests green, captures of every new object and of
   explosions, review by eye, EPIC showcase set in `report/figures_v4/`.
4. Video: 2-minute director sequence (`--record report/demo_v4.mp4 120`).
5. Docs (agents): report rewrite with a full audit appendix, workbook update (new sheets: controllable
   objects, combat, bunker, audit), 10-slide deck (outline on slide 2, video on the last slide), README.
6. Windows exe rebuild (`tools/cross_build_windows.sh`), final test run, commit when the user asks.
- helipad: parked helicopter reads as a silhouette (camera faces away from the moon) -> pad floodlight or camera change in the final pass

## Update (polish round results)
- A1 bunker interior: fixtures 3.2 / 9 m, 3 CIC lights, amber task lamps, red emergency, landing floodlight;
  root cause of the green wash-out was BlastDoors::apply() overriding screen emissive (fixed). Done.
- A2 base: lit huts (windows + door lamps), wire legible, dark far ground, apron puddles, tyre tracks,
  motor-pool floodlight (-20.8, 5.4, -9.3). Done.
- B1 tank / AA: camo + steel plate visible, real headlight lenses + spot lights, extra details. Done.
- B2 heli / radar: brighter materials; parked helicopter still a silhouette (camera faces away from the moon).
- E2 workbook: 18 sheets (new 14 Controllable Objects, 15 Bunker & Base, 16 Combat & Effects, 17 Audit),
  exporter schema 3, verify_workbook PASS (reduced-workbook recalculation + full numeric matrix check). Done.
- Tests: 120/120.
- Waiting: C2 explosions polish, D capture verification + HUD shots + door script fix + motor_pool shot, E1 report.
- Then: final EPIC captures into report/figures_v4, 2-minute video, 10-slide deck, README, Windows exe.

## Update (docs)
- E1 report: 29 pages, sections 1-12 + audit appendix (~85 rows), generated tables (report/gen_tables.py);
  v4 figures pending (\IfFileExists boxes). Re-run: `python3 report/gen_tables.py && cd report && tectonic -X compile main.tex`.
- E2 workbook: 18 sheets, verify PASS. Re-run: export_docs + tests + `make docs` (uv).
- E3 slides: tools/build_slides.py -> report/ThermalSense_Presentation.pptx (10 slides, outline slide 2, video
  slide 10, speaker notes). Re-run after figures_v4 + demo_v4.mp4: `uv run --with python-pptx --with pillow python tools/build_slides.py`.
- Supervisor: README v4 (controls, game rules, deliverables), helipad floodlight, soft particles on, T33 + exporter fixes.
- Still running: C2 explosion polish, D capture verification / HUD shots / preview video.
- Final steps: EPIC captures -> report/figures_v4 (D's shot list), 2-min video report/demo_v4.mp4 (1080p HIGH,
  ~2.5 h on llvmpipe), regenerate export / tests / workbook / report / slides, Windows exe, final test run.
- Windows exe rebuilt from v4 source (cross-compiled, static GLFW, imports only system DLLs; not run natively)
