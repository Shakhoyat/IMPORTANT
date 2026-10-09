# Report changes (v10 follow-up: M1 legibility, M2 consistency, M3 adoptions)

Figure and table numbers refer to the report before this round unless marked "new".

## M1: illegible / overlapping figures

### Workbook snapshots -> typeset blocks + appendix
New tool `tools/v10/sheet_to_tex.py` typesets the key blocks of each sheet from the LibreOffice-recalculated copy
(`report/workbooks/cg_workbook_values.xlsx`, written by `build_cg_workbook.py`); input cells keep the yellow shade.
New appendix A "Workbook sheets" (`tools/v10/gen_wb_appendix.py`): every page of LibreOffice's own PDF of the
workbook (vector) on a landscape page; the script measures the smallest printed text from the PDF (>= 7.0 pt;
Decomposition now prints at 75 %, Illumination at 78 % instead of fit-to-page, which had shrunk them to 6.1-6.3 pt).

| Figure | Before | After (caption wording kept, plus "(full sheet: Appendix A.n)") |
|---|---|---|
| 6.2 Primitives | full-sheet PNG | blocks: counts, parametric equations |
| 7.2 Transform_Trace | full-sheet PNG | blocks: inputs; S, Rx, Ry, Rz; T, M; W_parent, W (matrices); the vertex through the pipeline; checks |
| 9.2 View_Projection | full-sheet PNG | blocks: inputs + u, v, n basis; V and P matrices; depth table |
| 12.2 Clipping | full-sheet PNG | blocks: Cohen-Sutherland, Liang-Barsky, Sutherland-Hodgman stages 1 and 2 |
| 15.2 | plot + sheet side by side | specular plot alone (full width); new Fig. "Workbook sheet Illumination" with the geometry and term blocks. Caption: "Left:" / "Right:" removed, wording otherwise kept |
| 18.2 | ray tree + sheet side by side | ray tree alone (full width); new Fig. "Workbook sheet Ray_Tracing" (ray-sphere, ray-plane, Snell, TIR, Whitted). Caption: "Left:" / "Right:" removed |
| 19.11 Decomposition | full-sheet PNG | block: first 26 UAV parts with T/R/S and parent; caption adds "The body shows the first 26 UAV parts with their transforms." |
| 20.1 Color_Interp | full-sheet PNG | blocks A-F (triangle, barycentric, Gouraud vs Phong, texture u, sRGB vs linear, sky over the day) |
| 25.3 Thermal_Kernels | full-sheet PNG | Gaussian raw / normalised, patch + Sobel kernels, outputs (matrices) |
| 26.3 Duty_Cycle | full-sheet PNG | roster block |
| 29.2 Performance | full-sheet PNG | derived columns (1000/avg, fps check, triangles per draw) |

### Other figures
- 19.10: six tables at 1/3 width (scaled down) -> two per row at natural size (8 pt).
- 21.1 (`plots.py` p_day_cycle): titles shortened to "(a) sun elevation", "(b) key light per channel" (details are in the
  caption); legends moved below the axes; constrained layout for every plot.
- 11.1 (p_depth_precision): titles "(a) window depth z_w", "(b) smallest depth step"; y label "depth step (m)"; legend of
  (b) moved off the "1 cm" mark.
- 16.2-left (p_gouraud_vs_phong): legend moved below the axes (it sat on the curves); title kept.
- 25.2 (p_thermal_histogram): legends placed clear of the CDF curves; histogram now full width (it printed at 4.3 pt
  at 0.55 width); p_separable_cost title "Cost of a k x k blur".
- All half-width plots now placed at >= 0.5 text width (were 0.42: 6.7 pt tick labels); annotation / legend text 8 pt.
- 26.1 state diagrams: redrawn as three sub-diagrams (UAV, intruder, soldier) with explicit coordinates; return edges
  routed with corners in their own lanes; labels on white boxes beside the lines. Panels named (a), (b), (c).
- 4.1 architecture: redrawn as layers; no edge crosses a node.
- 4.2 site plan: the "+x (east)" / "+z (north)" arrows sit on a white box with dark text.
- Pipeline banner (every Part A section): text 7-7.5 pt (was 6 pt), inactive stages darker (text black 80 %).
- Frame Explorer captures (11.2b, 12.1b, 13.2, 14.1a/b, 14.2, 16.2-right, 25.2a/b): zoomed insets of the panel the
  caption refers to (clipped viewports of the same JPEG, pixels unchanged; `tools/v10/fe_crops.py` sizes each inset so
  the HUD text prints at >= 7.3 pt).
- 16.1: the two composite strips with burnt-in labels replaced by the six individual lab tiles with LaTeX subcaptions.
- 25.2 split in two figures (it ran into the page footer): Frame Explorer frames + kernel insets; histogram + cost.
- Equations 7.1 and 21.1 split over two lines (they ran into the margin).

## M2: factual consistency (Section 19) and no test content

All values from `ts --export-docs` (`report/data/scene_export_v10.json`) via `tools/v10/gen_numbers.py` and
`tools/v10/gen_decomp.py` (macros `\bunkerRank`, `\bunkerParts`, `\bunkerAbove`, `\partsBase`, `\partsEnv`,
`\partsCamp`, `\roverN/Parts/Anim`, ...).

| Where | Old | New |
|---|---|---|
| Fig. 19.4 caption | "the second-largest object (400 parts)" | "the fourth-largest object (427 parts, after Base, Camp camouflage, Environment)" |
| Section 19 critique | "1 286 + 741 + 679 parts" | "1 253 + 652 + 658 parts" (Base, Environment, Camp camouflage drawn parts; the old figures counted group nodes too) |
| Table 19.1 | one row per export object; UAV row 158 (both UAVs), Target Rover 156 (all rovers), 3 tank rows, 8 soldier rows | one row per design, per unit, with a "x n" column: Autonomous UAV 79 parts x 2; Target Rover 26 x 6; Main Battle Tank 255 x 3; Soldier 75 x 8 |
| Table 19.1 | no frustum column | Frustum column (UAV 1, Scout Helicopter 1) |
| Table 19.1 | Command Bunker groups 17, animated 17 | 18, 18 (current export: the hinged doors of v9 M4) |
| Table 19.1 caption | "Every composite object: parts, ..." | "Every composite object, per unit: parts, ..., with the number of units n of each design" |
| Fig. 19.3 caption (rover) | "48 of its nodes are animated ... the rover is the most articulated object per part" | "8 of its nodes are animated (wheels, sensor mast); the scene has 6 rovers of this design, 26 parts each" (48 summed six rovers; per part the tank, 118 of 255, is more articulated) |
| Rover parts table | counts summed over all six rovers (e.g. IR sensor 30) | one rover (IR sensor 5) |
| AA gun parts table | Shell casing S = 0.0 x 0.0 x 0.0 | "hidden (dagger)", footnote "scale 1e-04 at export: invisible until fired" |

Test content removed (per request): cover "Verification" row; Table 3.1 "Verification" row; Section 3 title
"Tools, hardware and test setup" -> "Tools, hardware and setup" and its caption; Fig. 4.1 caption ("so the 365 tests
link them without a GPU" -> "that runs without a GPU (the --export-docs path uses it)"); Section 5 ("a test checks it
has no conflicts" -> "it has no conflicts"); Table 23.1 "Test" column and caption clause; test ids in Sections 3, 23,
28 and Table 31.1.

## M4: pipeline flow, explanatory diagrams, Game mode, explosions, intruder algorithm

### New explanatory diagrams (`tools/v10/diagrams.py` -> `figures/diagrams/*.pdf`)
Teaching drawings of each section's maths; every constant drawn from the code is the code's value. All checked at
150 dpi for overlaps; smallest text 7.5 pt at natural size (>= 7.0 pt where a figure is scaled to the text width).

| New figure | Section | Shows |
|---|---|---|
| "The order matters" | 7 Modelling transformations | unit square through S, RS, TRS; SRT (translate first) lands elsewhere |
| "Why world = parent x local" | 8 Hierarchy | two-link arm, shoulder 20 -> 55 deg, elbow local angle unchanged |
| camera basis | 9 Viewing | e, a, n, u, v; camera looks along -n |
| perspective vs parallel | 10 Projection | two equal posts, projectors meeting at the eye vs parallel |
| frustum -> NDC | 11 Projective division | equal depth slices crowd towards z_ndc = 1 |
| clipping by hand | 12 Clipping | Cohen-Sutherland outcodes, Liang-Barsky interval, Sutherland-Hodgman (6 vertices) |
| back faces / Z-buffer | 13 HSR | culled faces, per-pixel nearest depth |
| rasterisation and colour interpolation | 14 Rasterisation | covered pixel centres (edge functions), barycentric RGB mix with the weights of a sample point |
| lighting vectors | 15 Illumination | N, L, V, H, R and how diffuse / specular fall off with angle |
| texture mapping and mip pyramid | 17 Texture | uv triangle, mapped on screen, 2x2-averaged mip levels |
| ray-sphere, reflection / refraction | 18 Ray tracing | two roots, nearest hit; Snell, critical angle 41.8 deg for n = 1.5 |
| two-view triangulation, Mahalanobis gate | 26 Game mode | skew pixel rays, midpoint; gate chi2 = 9.21 |
| particle integrators, debris bounce | 27 Explosions | one particle of each kind with the code's laws; debris restitution 0.3, friction 0.7 |
| blackbody ramp | 27 Explosions | bb(T) = (T^0.45, T^1.9, 0.85 T^4.5), flame and spark temperature over life |
| flood fill + PD line follower | 28 Paths | BFS distances, descent, Chaikin x3 guide line; IR array at the code's scale |

### Report flow
- New Figure "Part B on the pipeline" at the start of Part B: each Part B section placed under the pipeline stage it
  extends (same stage boxes as the Part A banner).

### Section 26: "Game-mode AI" -> "Game mode" (rewritten)
- Title "Game-mode AI" -> "Game mode".
- Intro: "hold a lock for 1.5 s" -> "hold a lock (lead 0.8 s, wing 0.5 s)": 1.5 s is `DroneController::kLockTime`
  (single-UAV controller); the squad that plays the game uses `kSquadLeadLock` 0.8 s / `kSquadWingLock` 0.5 s
  (`kSquadSoloLock` 1.3 s after `kSquadWingWait` 2.5 s), from `--export-docs` v5.squad.
- State diagram (a): node "LOCK 1.5 s" -> "LOCK lead 0.8 s / wing 0.5 s"; "reload 6 s" now from the export
  (kUavReloadTime).
- New: 26.1 "From pixels to a target" (detection-chain figure; table of the CV concepts used: Otsu threshold with
  floor 204, despeckle, union-find connected components, image moments, classification, back-projection with
  sigma = 0.12 + 0.015 d); "How the two UAVs work together" (shared track picture: Kalman predict, Mahalanobis gate
  9.21, Hungarian assignment, two-view triangulation, life cycle 3 hits / 0.25 s / 3 s; LEAD / WING roles, 6.5 m
  stand-off, 5 m height; combined strike, time on target, Eq. 26.1; flight-time table from v5.flight_time;
  proportional navigation clamped to 6 rad/s with Rodrigues rotation); sequence diagram of the cooperation;
  workbook sheet Tracking (new); figure of one engagement (launch, combined strike, thermal gimbal, kill banner).
  All values via `tools/v10/gen_game.py` (`\gv{...}` macros from the export or read out of the source).

### New Section 27: "Explosions: particles, physics and colour"
- Spawn table per blast power (0.15 / 1 / 1.6) from the export's effect census (UAV round: 122 particles, empty after
  11.7 s), motion law and colour law per kind; exact exponential drag; buoyancy; bounce; debris from the destroyed
  object's own primitives; colour from temperature bb(T); HDR emission; premultiplied blending with two sorted
  groups; thermal heat decay; workbook sheet Explosion (new); four frames of one blast.

### Section 28 (paths)
- New figure: flood fill + line follower diagram; new Algorithm 1 "Intruder rover, one tick" (pseudocode of
  `HideController::update / replan / followLine`); new paragraph: infiltration uses a weighted flood fill
  (Dijkstra, exposure and noise costs), hiding uses the breadth-first one; workbook sheet Line_Follower (new).

### Workbook
- 11 -> 14 sheets: Tracking, Line_Follower, Explosion (2 195 formula cells, 0 errors after LibreOffice recalculation);
  appendix A.20-A.22 added (smallest printed text 7.0 pt; Explosion prints at 80 %).

### Capture harness
- New capture group `game` in `tools/v10/capture_all.sh`; the explosion shots now wait up to 180 s for a real kill
  (was 60 s, after which they were skipped).
