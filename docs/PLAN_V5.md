# ThermalSense v5 — "Night Watch": a playable base-defence night game

Started 2026-10-02. Builds on v4 (`docs/V4_PROGRESS.md`). This file is the plan, the critique of the current
project, the improved plan that came out of the critique, and the live status.

## 1. Request (user, 2026-10-02)
- Enemy rovers **keep spawning** (waves), take **random paths** and use **MicroMouse + line-follower** logic.
- **Two UAVs** patrol constantly to keep the army camp / bunker safe.
- When an enemy is sighted, **both UAVs coordinate**, track it in real time with **computer vision**, and make a
  **combined attack** (two rounds) that blasts it; mark hits **like a game** (markers, score), **with sound**.
- Pro-level reflection / colour / shading, a realistic game feel.
- The base must look like a real **hidden army camp** and bunker with **missile launchers, tanks** and more.

## 2. First plan (straight from the request)
1. Second UAV (copy of the first) + a patrol route for each.
2. Waves of rovers from random edge points.
3. Both UAVs fire when either sees a rover.
4. HUD markers + score, sound effects.
5. Add launchers and more tanks to the base; better shading.

## 3. Critique of the current project (v4) — honest audit

### Gameplay / AI
| # | Problem | Why it matters |
|---|---------|----------------|
| G1 | Rovers only **hide**; they have no objective | no tension: nothing is ever "kept safe" from anything |
| G2 | Fixed 3 rovers, 3 fixed spawn points, respawn only after a kill | not a wave game; predictable |
| G3 | Rover paths are deterministic flood-fill descents to the nearest hidden cell | the same routes every run, not "random paths" |
| G4 | **One** UAV, no coordination; it launches on its own lock | the request is a two-ship team |
| G5 | Tracker = one thresholded blob per frame, **no data association, no filter, no track IDs** | weak "computer vision": it can't track several targets, jumps between blobs, no velocity |
| G6 | Target position from a single ground-ray intersection | no use of the second view (triangulation), noisy |
| G7 | Missiles home on the camera's aim point, no lead / time-on-target | "combined attack" can't be synchronised |
| G8 | No score, no base integrity, no win/lose, no game markers | doesn't feel like a game |
| G9 | **No sound at all** | the request asks for it; a night game without audio feels dead |
| G10 | `Scene` is turning into a god object (agents + combat + game rules) | hard to extend / test |

### Visuals
| # | Problem |
|---|---------|
| V1 | The "base" is lit like a car park (sodium floods, lit windows): a **hidden** camp keeps light discipline (dim red / blue-green, camo nets, nothing silhouetted) |
| V2 | No **missile launchers**; one tank; vehicles stand in the open instead of in revetments / under nets |
| V3 | Procedural surfaces only change **albedo**: no surface relief, so every box face reads flat (the biggest "primitive" giveaway) |
| V4 | Reflections only from SSR in puddles: metal / glass / wet vehicles don't pick up the environment (no reflection probe) |
| V5 | Colour: one global grade; no per-light colour contrast story; bloom/flare tuned for v3 |
| V6 | Explosions good now, but no **game feedback** (hit marker, marker on the enemy, kill banner) |

### Engineering / delivery
| # | Problem |
|---|---------|
| E1 | v4 final captures / 2-min video unfinished (7 / 25 shots) — the run was cut by a session end |
| E2 | Nothing measured on the RTX 4070 yet; llvmpipe renders take hours |
| E3 | Subagents were stopped by safety filters on weapon wording; core targeting code must be written carefully as game code |
| E4 | No audio path in `--record`; the video would be silent |

## 4. Improved plan (v5)
Principles from the critique: **game first** (objective, waves, score, sound), **real CV** (multi-target tracking
with a filter, association and two-view triangulation), **coordination** that is visible on screen, and a
**hidden camp** look instead of more lamps. One final production pass (captures, video with sound, docs) at the
end instead of re-rendering after every change.

### P1 Mission / waves (supervisor) — `scene/Mission.h/.cpp`
- Waves: wave n spawns `2 + n` rovers (max 6 alive) at **random entry points** on the compound edge
  (seeded RNG, different every wave), short "WAVE n" alarm; next wave when the previous is cleared or after 60 s.
- Rover **objective**: infiltrate to a target in the camp (bunker ramp, fuel depot side, ammo bunker); planner =
  MicroMouse flood fill on a **cost map** = distance to the objective + exposure to the UAVs (cells seen by either
  UAV cost more) + random per-rover noise field (=> random paths); driving = the existing 5-sensor LFR + PD.
- A rover reaching its objective = **base hit** (integrity −10 %); integrity 0 => "BASE LOST", mission restarts.
- Score: +100 kill, +50 coordinated kill bonus, −10 % integrity per breach; HUD + report data.

### P2 Computer vision v2 (supervisor) — `vision/`
Per UAV, per frame, on its own 320×240 heat image:
1. Threshold (Otsu on the hot tail, clamped) → binary mask.
2. Morphological **opening** (3×3 erode + dilate) removes speckle (grass hot spots, sparks).
3. **Connected components** (two-pass union-find, 8-connected) → blobs with area, centroid, bbox, mean / peak heat,
   second moments (orientation, elongation).
4. Blob filter (area, hot core, aspect) → measurements; each centroid → ground-plane point by ray casting.
5. **Multi-target tracker** (shared by the squad): constant-velocity **Kalman filter** per track on (x, z, vx, vz);
   **gating** (Mahalanobis χ², 2 dof, 99 %) + **Hungarian assignment** (cost = Mahalanobis distance);
   track life cycle tentative → confirmed (3 hits in 5 frames) → coasting → deleted; track IDs.
6. **Two-view triangulation**: when both UAVs see the same track, the two pixel rays are intersected (midpoint of
   the shortest segment between them) → 3D position with lower error; the filter uses it as a tighter measurement.
7. Discrimination as before (wrecks, friendly vehicles, keep-out).
Tests: synthetic images (known blobs) → exact component counts / centroids; Kalman converges on a moving target;
Hungarian vs brute force; triangulation error < single-ray error on noisy rays.

### P3 Two-UAV squad (supervisor) — `ai/Squad.h/.cpp`
- **Patrol**: two UAVs on opposite halves of a figure-of-eight / perimeter route, phase-locked 180° apart so the
  whole compound is covered; cameras sweep outward.
- **On contact**: shared track picture; the UAV with the best view becomes **lead (designator)**, the other
  **wingman** flies to a position ≈ 90° around the target (good triangulation geometry, different approach angle).
- **Combined attack**: when the track is confirmed + both have line of sight + locks ≥ 1 s, each computes its
  round's flight time; the one with the longer time launches first so both rounds **arrive within 0.2 s**
  (time-on-target). Rounds use **lead prediction** from the Kalman velocity.
- Several enemies: confirmed tracks are ranked by threat (distance to its objective); both engage the top threat
  together (combined attack) unless two threats are both close → split (one each).
- Resume patrol after the kill; reload 10 s.

### P4 Game HUD + sound (agents)
- HUD: enemy markers on screen (red brackets + track ID + distance, "LOCK" ring, predicted lead dot), UAV tags,
  kill banner "+100 / COMBINED STRIKE +150", hit marker, wave / score / base-integrity bar, radar mini-map
  (top-down: UAV icons, track dots, objectives), alarm flash on breach.
- Sound: `miniaudio` (single header, MIT-0) + **procedural synthesis** (no sound files, like no image files):
  rotor hum per UAV (pitch with speed, distance attenuation, stereo pan), launch whoosh, explosion boom
  (filtered noise + sub sine + tail), cannon, Gatling buzz, siren on a new wave, radio beeps on lock, ambient
  night (wind + crickets). Live playback when a device exists; **offline mixing for `--record`** (WAV muxed into
  the MP4 with ffmpeg), so the demo video has sound.

### P5 Hidden army camp + shading (agents + supervisor)
- Light discipline: replace bright floods with dim red / blue-green tactical lights; lit windows blacked out;
  the camp reads by moonlight, explosions, UAV searchlights.
- Camo nets (thin cubes + Camo surface + holes pattern) over vehicles, launchers, the bunker entrance, tents.
- **Missile launchers**: a SAM launcher (4 canisters on a turntable, controllable elevate / rotate, launch
  effect) and a truck-mounted MLRS (6×2 tube pod); 2 extra parked tanks in earth revetments; ammo bunker.
- Shading (supervisor): **procedural bump** for every surface (normal perturbation from the same noise:
  corrugation ribs, steel seams / rivets, HESCO mesh, concrete pores, camo paint chips) → light catches edges;
  **reflection probe**: a low-res cubemap of the sky + horizon for metal / glass / wet vehicles; per-material
  clear-coat for wet paint; re-tuned night grade (teal moonlight, orange fire, red tactical).

### P6 Production + docs
Tests for every new module; final EPIC captures; 2-minute video **with sound**; report / workbook / slides
regenerated; Windows exe; progress in `docs/V4_PROGRESS.md` → `docs/V5_PROGRESS.md`.

## 5. Order of work
1. Supervisor: P2 CV v2 (tracker module + tests) → P3 squad (second UAV, coordination, time-on-target) →
   P1 mission / waves. These are the core game code (written by the supervisor).
2. Agents in parallel: P4 audio engine (procedural synth + offline mix), P4 game HUD, P5 hidden-camp dressing +
   launchers + revetments, P5 surface bump / reflection probe review.
3. Integration, tuning by captures, then P6.

## 6. Status (2026-10-05)
| Item | State |
|------|-------|
| Plan + critique | done |
| P2 CV v2: CvPipeline (adaptive threshold, despeckle, union-find components, moments), MultiTracker (Kalman, chi2 gating, Hungarian, two-view triangulation, track life cycle) | done, T130-T137 |
| P3 Squad: 2 UAVs, opposite-phase patrol, lead / wing roles, 90 deg wing geometry, line-of-fire check + detour, time-on-target combined strike with lead prediction, solo fallback, reacquire 12 s round the last known position | done, T138-T142 |
| P1 Mission: waves min(2+n, 6), random edge spawns, random objectives (5 camp assets), breach -10 %, base lost -> restart, score (+100, +50 combined) | done, T143, T180, T181 |
| Rovers: infiltrate (weighted MicroMouse flood fill: distance + exposure + per-rover noise; LFR driving), evade < 5 m for 4 s, route commitment 1.6 s | done, T144, T145 |
| User rule "4-5 shots = 1 kill": armoured rovers (100 HP, 30 x power x falloff per blast), hit events, damaged rovers limp + smoke | done, T105 (5 hits), T140 (13 rounds -> 3 kills) |
| App: both UAV cameras -> CV -> squad each frame; live audio (A5 engine) | done |
| A5 audio engine (procedural synths, spatial mix, offline mix + mux in --record) | done, T160-T169 |
| C5 hidden camp (light discipline done; launchers / parked tanks in progress) | resumed |
| H5 game HUD | launching |
| Shading upgrade (procedural bump, reflection probe) | todo (supervisor) |
| Final production (captures, 2-min video with sound, docs) | todo |

## 7. Documentation requirements (user, 2026-10-05, late)
1. The report must be **self-contained and visual**: object creation, colour interpolation, shading, shininess,
   reflection, LFR / MicroMouse logic **with code**, and how the UAVs detect the rovers in real time.
2. It must **tell the story of the whole graphics pipeline** (primitive -> T R S -> hierarchy -> view / projection ->
   rasterisation / interpolation -> shading -> depth -> shadows -> post -> image -> heat image -> CV -> decisions) with
   **mathematical notation** and **Excel sheet screenshots per object** where they help.

| Work | Owner | Output |
|------|-------|--------|
| Explanatory diagrams from the program's data and shader formulas | F1 | report/diagrams/ |
| In-program renders (CV stages on the real thermal image, material grid, shading models, bump / SSR on-off, pipeline buffers) | F2 | report/figures_lab/ |
| Workbook snapshots (rendered from the real .xlsx: per-object construction rows, MMULT worked example, hierarchy, vertex data...) | F3 | report/sheets/ |
| Report restructure: textbook chapters along the pipeline, real code listings, compressed images (< 25 MB) | E1 | report/main.tex, report/sections/ |
| 2-minute video with sound (1280x720 HIGH 30 fps, llvmpipe) | supervisor | report/demo_v5.mp4 |
| Final: slides with video, workbook rebuild (audit), Windows exe (done 13:24), tests | supervisor | |

### 7.1 Status (2026-10-05, 14:05)
- F1, F2, F3, E1 all done. Report: 96 pages, 19.9 MiB, 0 pending figures, 169/169 tests.
  - Rebuild chain: `python3 report/listings.py && python3 report/gen_tables.py && python3 report/sheet_figs.py &&
    uv run --with pillow python report/prepare_images.py && (cd report && tectonic -X compile main.tex)`.
  - `report/sheet_figs.py` turns sheets/, diagrams/ and figures_lab/ `captions.json` into `sections/gen_sheets.tex`
    (`\sheetfig{name}`, `\autofig{dir}{name}`: captions name the real sheet + cell range / source function).
  - `prepare_images.py`: renders -> JPEG q85 <= 1600 px; diagrams + sheet crops -> 256-colour palette PNG.
  - `\picfour` falls back to the v5 capture of the same shot.
- Fixes from F1's audit: tracker merge no longer resets a confirmed track's velocity (a tentative newcomer is fused
  as a measurement); blob rays stop 0.2 m above the terrain (`kSquadBlobHeight`, removes the 0.27 m single-view bias);
  stale comments (HideController grid, CvPipeline threshold); `EnemyHit` now plays a hit tick.
- Remaining: video (recording with the pre-fix binary), slides with video, workbook rebuild + snapshots, Windows exe.

### 7.2 Status (2026-10-05, 14:30) -- user: "remove old redundant scenes, reduce text"
- Old scenes removed: no v2/v3 capture is placed any more; v4 shot names resolve to the v5 capture of the same
  shot (`\picfour` prefers figures_v5); duplicate images removed (checked by name and by file hash); the capture-shot
  inventory table is gone from the appendix. Slides: `find_img` only searches figures_v5, h5_shots, figures_lab.
- Text: hand-written section words 22.3k -> 17.5k (editors T1-T3 + supervisor for T4's files after T4 hit a usage
  limit). Report 96 -> 86 pages, 16.95 MiB, 0 pending figures, no undefined references.
- Audit + intro checklist statuses updated (video, slides, workbook, trace, soft particles now done); T112 text now
  uses kUavReloadTime (6 s); cv_strip caption corrected (0.15 s spacing).
- Deliverables: report/main.pdf, report/ThermalSense_Presentation.pptx (10 slides, video on 10),
  report/demo_v5.mp4 (120 s 1280x720 30 fps, AAC), ThermalSense_Graphics_Documentation.xlsx (verify PASS),
  ThermalSense.exe (14:28). Tests 169/169.
- Still open: FPS on the RTX 4070 (not measured), reflection probe (not implemented).
