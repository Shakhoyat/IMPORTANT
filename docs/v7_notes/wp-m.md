# WP-M notes: hero model + material detail (objects/*, not Rover)

Status: started 2026-10-07 (power-outage log; newest at the bottom).

## Rules I follow
- Only append parts at the END of each builder (node ids = prefix + running counter, so existing ids stay stable);
  names of existing parts unchanged. Details are flagged obstacle=false / occluder=false (PropKit `detail()` / `bevel()`).
- New parts take the heat of the surface they sit on (or <= 0.35 for emissive indicators); no new practical lights (T158).
- UAV: restore Phong shading after the Beam cone (T19).

## Tests that pin counts / names / heat (must stay true)
| Test | Assertion | Baseline |
|------|-----------|----------|
| T38 | scene total leaves < 6000; Environment/Backdrop lab prims, cones | 4871 |
| T76 | Base static + 3 controllables 900..1500 | 1193 + 82 = 1275 |
| T66 | Bunker 250..450 | 400 |
| T201 | Soldier leaves 34..42, depth >= 6, capsule radii equal, one visible "Rifle receiver", ids unique | 40 |
| T209 | Soldier "Head"/"Hand" heat 0.82, "Torso" 0.70, max < 0.90 | ok |
| T96 | Helicopter leaves >= 60, named, blur disc | 68 |
| T99 | Radar 21 "Array tile*" | 102 leaves |
| T150 | SAM >= 80, MLRS >= 100 (text), camp parts lab prims with name/purpose/id | SAM 90, MLRS 116 |
| T80 | tank 80 links | 226 parts / tank |
| T18/T19 | Bunker/UAV/Rover cube+sphere+cylinder; Bunker Flat, UAV Phong | |
| T39 | every leaf roughness/metalness in [0,1] | |
| T40/T77 | env props r <= 20 m; base centres r <= 36 | |
| T41 | static ground-level heat < blob threshold and < rover motor | Fire embers 0.74 |
| T196 | passive (heat < 0.75) noon peak < 0.80 | |
| T158 | practical light count / power lower than v4 | 11 lights |
| T205 | soldier routes keep 0.28 m from part footprints | |

## Before counts (export-docs, leaves per object)
AA Gatling Gun 57, Air-defence radar 102, Ammunition bunker 43, Autonomous UAV 134 (2 x 67), Backdrop 120, Base 1193,
Boom barrier 20, Camp camouflage 658, Command Bunker 400, Environment 652, Flag 47, MLRS 116, Main Battle Tank 226 (x3),
SAM 90, Scout Helicopter 68, Soldier 40 (x8), Target Rover 156 (not mine), Windsock 15. TOTAL 4871.

## Progress
- Baseline build + 219/219 tests green (build/tests-wpm).
- Baseline captures (night v4 shots + v6 day shots) in the scratchpad `before/`.
- PropKit: detail kit (shade, reworked, indicator, decal, detail, bevel).
- Detail passes appended (ids verified stable: export-docs before/after, 0 renamed / re-parented / missing ids, 279 new
  parts, hottest new part = soldier "Jaw" 0.82 (skin, T209 max < 0.90); every other new part <= 0.62; indicators 0.30).
- Tests: 288/288 green with the other agents' current files (build/tests-wpm). Night thermal close-ups unchanged.

## What was improved (per object, leaves before -> after)
| Object | Before | After | Detail |
|--------|--------|-------|--------|
| Main Battle Tank (x3) | 226 | 255 | bolt-on side armour (4), worn bevel edges on turret roof / deck / glacis / bustle (6), turret numbers, tow cables, skirt rubber flaps, jerry cans, headlight guards, commander panoramic sight (post, head, window), crosswind sensor, hatch hinge, dark muzzle bore, muzzle reference sensor, green convoy marker (emissive) |
| Scout Helicopter | 68 | 91 | canopy centre + sill frames and a cabin seam (thin ellipsoid slices), door windows, engine intakes, nose FLIR ball + window, drive-shaft fairing, UHF / VHF blade antennas, tail serials, NVG formation strips (emissive), skid end caps, 5 blade erosion strips; paint lifted from near-black to dark olive, canopy glass slightly blue |
| Autonomous UAV (x2) | 67 | 79 | motor cooling rings, rear amber status LEDs, telemetry antennas, battery straps, camera window, blue arming LED; Phong restored after the beam (T19) |
| AA Gatling Gun | 57 | 78 | 6 anchor bolts, radome seam, fire-control console + live Screen display + ready / armed LEDs, DANGER stencils, 4 worn mount edges, 3 housing vents, grab handle |
| SAM launcher | 90 | 112 | canister ribs + stencils, status display + 2 LEDs, generator grille, worn cabin edges, 8 tyre tread bands; paint lifted |
| MLRS rocket truck | 116 | 136 | cab side windows, radiator grille, exhaust stack, worn cab edges, pod stencils, 12 tyre tread bands; paint lifted |
| Air-defence radar | 102 | 108 | shelter status display + LED, RF hazard placard, worn shelter roof edges, bearing seal |
| Soldier (x8) | 40 | 42 | plate carrier (box vest, 0.62 heat), jaw / chin ellipsoid (face shape); fingerless dark gloves (Hand heat still 0.82), leather boots, helmet cloth cover (camo) + painted rim, coyote webbing, rougher cloth, worn rifle finish |
| Base (static) | 1193 | 1253 | cargo trucks: 6 wheel hubs, side windows, mirrors, grille, exhaust; containers: 5 more corner castings + owner code; guard towers: 2 sandbag course lines, MG ammo can, field radio + LED; guard booth: door handle, rounded roof edges, placard |
| Scene total | 4871 | 5150 | T38 limit 6000; Base T76 1275 -> 1335 (limit 1500); Bunker untouched (400 / 450) |

Not changed: Bunker, Structures, Environment, Vegetation, Backdrop, Terrain (budget kept for other agents; T66 bunker
has only 50 parts of headroom).

## Visual checks (RTX 4070, D3D12, EPIC, 1280x720)
- Scratch harness (not in the repo): renders orbit close-ups of a named object with the real Renderer (optionally
  isolated), linked against build/obj-wpm; "before" relinked with the pre-edit object files from build/obj.
- Before / after pairs: scratchpad `pairs/*.png` (tank, heli, AA, SAM, MLRS, UAV, radar, soldier, night thermal).
- Capture-tool shots before (`before/`, `before_day/` with --time 11:00) and after (`after_day/`, `after_v6/`).

## Open
- A proper close-up capture for each hero object (iso_tank, iso_heli, iso_aa, iso_sam, iso_mlrs, soldier front) would
  help the report: Capture.cpp is CAP's file; the existing v4 tank / camp shots are mostly hidden by the camo nets.
- The Surface::Camo pattern barely reads on the soldier at 3 m (pattern scale is in the shader, not mine).
- T276 (WP-H, in progress at the time): the centre-ray failures were thin wires / UAV round noses, none of the new
  parts (checked with a scratch tool over all primitives).
- Final after captures with the capture tool (RTX 4070 D3D12, EPIC, 1280x720): scratchpad `after_day/` (tank_closeup,
  helipad, camp_sam, camp_mlrs, heli_takeoff, base_gate, uav_closeup_visible at --time 11:00) and `after_v6/`
  (soldier_closeup, day_mechanic); matching befores in `before_day/` and `before/`.
- Status 2026-10-08: done. Files changed: src/objects/{PropKit.h,PropKit.cpp,Soldier.cpp,Tank.cpp,Helicopter.cpp,
  Uav.cpp,AaGun.cpp,Launchers.cpp,Radar.cpp,Base.cpp}. No test edited, no commit.

## Soldier hero pass (2026-10-08, supervisor request)
- Leaves per soldier 42 -> 75 (8 soldiers: +264). **T201 cap raised 42 -> 75** (supervisor-approved, expected text
  updated in tests/test_soldiers.cpp; still primitives cube/sphere/cylinder only, depth >= 6, capsule radii equal, unique
  ids, one visible "Rifle receiver"). Existing ids unchanged (all new leaves appended).
- Proportions (Soldier.h): kChestToNeck 0.34 -> 0.23, kNeckToHead 0.10 -> 0.06, kShoulderY 0.27 -> 0.19, upper arm /
  forearm 0.28 / 0.25 -> 0.30 / 0.26 (the support hand must reach the handguard). Head centre ~1.69 m, helmet top ~1.81 m
  (was 1.86 / 1.98): head ~1/7.5 of the figure. Neck: short, thick, 8 deg forward carriage (slight hunch).
  Torso ellipsoid and plate carrier lowered; trapezius ellipsoids (neck group: hidden in first person with the head).
- Head: skull + jaw + nose + 2 eyes (dark) + 2 ears + 2 chin straps; helmet lower over the brow (rim at 0.138, pitched),
  goggles and NVG shroud on the helmet.
- Limbs: tapered by muscle ellipsoids over the capsules (thigh, calf, forearm, deltoid); knee and elbow pads.
  (No frustum primitive exists apart from the uncapped beam cone, and T201 allows only cube/sphere/cylinder.)
- Kit: triple magazine pouch + admin pouch on the carrier, radio with whip antenna; rifles: pistol grip, handguard,
  red-dot optic (both held and slung rifles).
- Rifle hold: Gait.h `armIK()` (two-bone IK, shoulder Euler YXZ + elbow bend; FK(IK) error 1.4e-6 m on 1708 random
  reachable targets) and `eulerYXZFromBasis()`; Garrison.cpp `holdRifle()`: butt in the right shoulder pocket, chest
  bladed 20 deg (head counter-turned), right hand on the pistol grip, left hand in front of the magazine well; pitch
  26 deg at the post, 34 deg walking (low ready), 42 deg crouched / ducking. Applies when the held rifle is visible,
  not waving and not carrying. First person: WP-G's viewmodel re-worlds the rifle and wrists afterwards (unchanged).
- Heat: skin parts 0.82 (Head, Jaw, Nose, Ears), eyes 0.80, kit <= 0.62: T209 unchanged (max 0.82).
- Camo scale: ShaderSources.cpp surface 17, one line: cloth (uRoughness > 0.94) uses a 4.5x finer print; the soldier
  cloth / helmet cover roughness is now 0.95-0.96 (vehicles 0.42-0.75, Structures 0.88-0.92 unchanged).
- Workbook export: DocExport writes the soldier parts / joints from the live graph (no hard-coded counts), so sheet 22
  picks up 75 parts and the new joints list automatically; nothing to change there.
- Scene total 5150 -> 5414 (< 6000).
- Verified on the RTX 4070 (D3D12, EPIC, 1280x720): harness close-ups `sold2/` (front, side, back, face, walking pairs)
  and the capture tool `hero_after/` (soldier_closeup, day_patrol_spline, game_soldier_fp; befores in `before/` and
  `hero_before/`). 289/289 tests green (T200-T214 incl.), scene total 5414.
