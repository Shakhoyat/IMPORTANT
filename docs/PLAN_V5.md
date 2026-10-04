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

## 6. Status
| Item | State |
|------|-------|
| Plan + critique | done (this file) |
| P2 CV v2 | in progress |
