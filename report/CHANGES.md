# Report changes: final-submission pass (M0-M5)

Build note: this machine has no latexmk / pdftoppm, so the PDF is built with `tectonic` and pages are rasterised at
110 dpi with PyMuPDF (same output as `pdftoppm -r 110`).

Page count before this pass: 106 (body 1-78, workbook appendix 79-106).

## M0: one primitive colour key

- New `src/report/PrimitiveColors.h`: the only definition of the 16 primitive colours (sRGB + CIE L*), grouped by
  family, each family in its own L* band so families stay apart in greyscale:
  box (cube, cuboid, wedge, tapered box, pyramid) blues / cyan L* 30-45; ring (torus, annulus, helix) pinks / purple
  L* 50-57; special (grass blade, terrain disc) grey / brown L* 60-61; cylinder (cylinder, frustum, cone) greens
  L* 64-69; round (sphere, hemisphere, capsule) oranges / yellow L* 74-88.
- New `tools/report/gen_prim_colors.py` writes `report/prim_colors.tex` (`\definecolor{prim-<key>}`, `\primsq`,
  `\primchip`, `\primchipn`, `\primkeyline`) and fails if two families overlap in L*; `tools/report/primkey.py` gives
  the same table to the other report scripts.
- The program's diagnostic view (F) and its HUD legend now use the key (cube parts with a non-uniform S drawn as
  cuboids), replacing the cyan / magenta / amber colours.

## M1: capture / export modes (program side)

Scene geometry is unchanged: a fresh `--export-docs` differs from the previous export only by the new fields (checked
field by field: 6 071 components identical, the 5 476 parts unchanged).

- **Designs** (`src/report/Designs.{h,cpp}`, CPU only): a rule table maps builder output to 62 report designs in 8
  families. The aggregates Base (993 parts), Environment (652), Camp camouflage (658), Backdrop (120) and UAV pads are
  split into the designs they place (HESCO perimeter, concertina wire, guard tower x4, outer gate, helipad, motor pool,
  fuel depot, Quonset hut x3, perimeter fence, watchtower, sandbag wall x3, camouflaged tent, supply cache x7, radio
  mast, lamp post x2, fire barrel, track sign, HESCO blocks x2, jersey barrier x6, tank trap x2, pine x26, broadleaf
  tree x4, bush x14, boulder x13, earth revetment x5, camouflage net x7, cut branches x8, distant hills, treeline pine
  x43, far radio tower, far ground, road, tyre tracks, tank spot marking, moon ...). A design built inside another is a
  nested design (the camouflage net over the bunker ramp, the cargo trucks and containers in the motor pool, the rounds
  on the UAV rails, the SAM missile, the six MLRS rockets, the 20 AA shell casings, the supply cache in the ammunition
  bunker); every leaf belongs to exactly one design instance.
- **Export schema 9**: every component has `design` and `design_root`; new `designs` list (family, source builder,
  instances, parts per instance, parts total, primitives of the first instance and over all instances, nested designs,
  per-instance root id / position / heading / scale / hidden) and `designs_unassigned` (0).
- **`ts --export-prims-lowres FILE`** (GPU-free): the real generators at small parameters (sphere 8x4, hemisphere 8x2,
  cylinder / frustum / cone 8, torus 8x6, capsule 8x2, annulus 8, helix 2 turns 24x6 (tube r 0.07, thicker than the scene's 0.025 so the vertices can be numbered), terrain disc 3x8, flat ones and
  the grass blade as they are) with positions, normals and indices -> `report/data/prims_lowres.json`. The generators
  produce no texture coordinates (MeshData is position + normal), so none are exported.
- **`--capture DIR --only shapeview:<design>[|az|el|ksub|kpart|inst|cut|w|h|fill|stage]`** (`src/app/CaptureShapes.cpp`,
  CPU half in `src/report/ShapeView.{h,cpp}`): the design alone in the export's t = 0 pose (a freshly built Scene, so
  identical to the export), camera from a 3/4 view relative to the design's front (az 35, el 25), fitted so the
  projection fills 80 % of the frame; `<slug>_model.png` through the real renderer (no HUD, sky, fog, bloom, grain,
  flare, vignette or aerial perspective; anti-aliasing kept; per-object fog off), `<slug>_prims.png` rasterised on
  the CPU: every part Lambert-shaded in its key colour, triangle edges (faded when shorter than a few pixels), a dark
  outline per part, pulled apart (sub-assemblies by k_sub (centre - parent centre), then parts by k_part inside their
  group; groups with one child do not count as a level), nested designs pale; JSON with the legend (own parts by
  primitive), drawn parts with dagger / parked / cut flags and projected centres, groups with projected centres (for
  leader lines) and crop boxes. Parts hidden at t = 0 are drawn in the primitives panel at their rest scale and flagged;
  a hidden part of a nested design parked below the ground (the AA casings, y = -50 m) is listed but not drawn; a design
  whose own parts are all hidden (SAM missile, MLRS rocket, AA casing) is drawn in both panels.
- **`--cutaway`** with `cut=` names: the command bunker (roof slabs A/B, surface installations, the camouflage net),
  the ammunition bunker (earth mound, mound lobes, turf clumps) and the barracks hall (roof sheets, ridge cap,
  ceiling, gables) are captured with those parts left out.
- **`shapeview:@camp`**: the whole camp from the air in the colour key, each design instance moved as a block
  (far backdrop, sky bodies and the terrain disc left out) - replaces the exploded HUD capture of Fig. 6.1b.
- **`lineup:<a>+<b>+...`**: designs side by side at one scale, orthographic, each in its root frame, px per metre in
  the JSON (munitions: SAM missile, MLRS rocket, UAV round, AA shell casing; the exhaust flame is left out).
- **`tile:<object>|<part>`**: one part tile as in `parts:` (the "used for" thumbnails of the primitive cards).
- **`stage=<pct>`**: a build stage in the colour key (first ceil(pct N / 100) parts in hierarchy order).
- `tools/report/capture_shapes.py` runs all of them (per-design view overrides and the cutaway list are a small table
  in the script) into `report/figures/raw/shapes/`.
- Renderer: `RenderSettings::vignetteOn` (default on; the game is unchanged) and the composite grain follows
  `RenderSettings::grain`, so the shape views can switch both off.
- Tests T440-T447 (396 tests, 0 failed): designs add up to the scene (sum over designs of instances x parts = 5 476,
  every leaf once, 0 unassigned), aggregates split / nested designs found, per-primitive totals over designs = the
  scene's primitive counts, shape-view request syntax + cutaway, pull-apart offsets, the software rasteriser (key
  colour, background, 80 % fit), the colour key's L* bands, the low-resolution generator counts.

### Vegetation: primitive-built vs CC0 meshes
Built from primitives (scene graph, always): Pine (cylinder trunk + 5-7 cone whorls), Broadleaf tree (cylinder trunk,
3 cylinder limbs, 14 sphere clumps), Bush (5-7 spheres), Boulder (3-4 spheres), Treeline pine (2 cones), Cut branches
(cylinder stems, sphere foliage, cone tips, sphere brush). At ULTRA / EPIC the renderer draws CC0 Poly Haven meshes in
place of Pine, Broadleaf tree, Bush, Boulder and Treeline pine (the primitive parts stay the gameplay model and are
drawn at LOW / HIGH); cut branches are never replaced; ferns, low shrubs and grass tufts are extra ground cover with no
primitive design. LOD 0 triangles (`report/data/vegetation_assets.json`, written by the renderer): fir_sapling_medium
101 424-166 538 (camp pines), tree_small_02 319 454 (broadleaf), shrub_01 5 936-23 007 and shrub_02 3 111-5 537 (bushes),
boulder_01 56 558 and rock_moss_set 2 754-6 618 (boulders), fir_tree_01 76 080-473 624 and pine_tree_01 308 150-472 573
(treeline), ground cover 290-6 494. The shape views use the primitive trees (the isolated render path never swaps them).

### Not captured as designs (and why)
- Tank shell and AA tracers: `Combat::fireShell` / `fireTracer` projectiles drawn by the effects pass (a glowing slug),
  not scene parts, so there is nothing to decompose.
- "Light tower" (`Base.cpp`): the builder exists but is not placed in the scene.
- Grass blades: the GPU-instanced grass field (not scene parts); the blade is primitive 16 in Section 6.

## M2: Section 6 - one card per primitive

- New `tools/report/gen_prim_cards.py` -> `report/sections/gen_primcards.tex`: 14 card figures (Figs. 6.2-6.16 new;
  cube and cuboid share one, grass blade and terrain disc share one float with two captions), two per page. Each card:
  (a) the scene mesh rasterised in the exact key colour with its triangle edges (new `prim_NN_<name>_key.png` from
  `ts --capture prims`; the GPU tiles shifted the key's hue through lighting and tone mapping); (b) a TikZ drawing of
  the real generator's low-resolution mesh from `report/data/prims_lowres.json` (axes, numbered vertices, the
  highlighted cell, normals, the seam in red, cap fans darker, parameter arcs; the flat primitives keep their corner
  drawing and corner / face table from `gen_m3.py`); (c) that cell split into the two triangles in emission order with
  the generator's index formula (every triangle named is looked up in the low-res index array; the script stops if one
  is missing); (d) surface and normal formulas, V and T with a one-line derivation, the drawn and the scene parameters
  with their V / T and instance count; (e) three part tiles of different designs (`tile:` captures; parts more than
  12 times longer than their second side are skipped as unreadable thumbnails) with count, design and size.
- Section text rewritten: one paragraph on the three choices every generator makes (shared vs duplicated vertices,
  the seam, winding).
- Fig. 6.1: (a) the 16 primitives in the key colours with chips; (b) the exploded HUD capture replaced by the camp
  from the air made of primitives (`shapeview:@camp`) with a key legend and part counts.
- Tables 6.1 and 6.2 kept, with a key chip before every primitive name.
- Removed: Fig. 6.2 (workbook block repeating Table 6.1), Table 6.3 (parametric list; its content is in the cards),
  Fig. 6.3 (corner drawings; now in the flat cards). The reference in Fig. 7.x (worked group A) points to the cube card.
- Stated once: the "no LOD, 808 spheres x 1224 triangles" limitation is only in Section 22; Fig. 22.1 keeps only the
  tessellation plot and refers to Fig. 16.1 for the flat / Gouraud / Phong spheres.
- The ray tracer's approximations of the new primitives stay as Section 6's one "Limitation".

## M3: Section 19 - every design broken into primitives

New `tools/report/gen_designs.py` -> `report/sections/gen_designs.tex` (+ `report/data/designs_check.json`), from the
export's `designs` list and the shape-view captures. The script stops unless, for every design, the capture's legend,
the parts table and the export's row agree primitive by primitive; the designs sum to the scene's 5 476 parts.

Added / redone:
- Fig. 19.1 (new): object atlas, a thumbnail of each of the 62 designs by family, labelled "parts x instances".
- Table 19.1 (redone): one row per design (was one row per scene object, with the aggregates Base 993, Environment
  652, Camp camouflage 658, Backdrop 120), grouped by family, columns x n, parts per instance, total, one column per
  primitive headed with its key chip (totals over all instances, blank instead of 0), totals row = 5 476 parts and the
  per-primitive scene counts of Table 6.1.
- 62 design cards (new, Figs. 19.2-19.6x): framed "Full model | Made of primitives (pulled apart)" pair with a legend
  strip generated from the capture (own parts by primitive, total; pale nested designs with their part counts;
  dagger count; parts parked below ground), one sentence (purpose of the root node from the export, instance count,
  distance and direction from the bunker, placement T(x,y,z) Ry(heading) S(s) of the first instance) and a parts table:
  Part (x n) | Shape (chip + name) | Size (cm) | Position T (cm, design frame) | Rotation R (deg) | Parent, rows grouped
  under sub-assembly headers, repeats collapsed with patterns (+-a, a+di, {a,b}, a..b), every part kind listed (no
  "+N more"), nested designs as one row pointing to their card. Large designs (soldier, tank, MLRS, helicopter, UAV, SAM,
  AA gun, command bunker, barracks hall, radar) get a larger figure with sub-assemblies named by leader lines (projected
  group centres from the capture). Generation rules as formulas for the HESCO perimeter, concertina wire, perimeter
  fence, tyre tracks, distant hills, treeline pine, pine, broadleaf tree, bush, boulder, camouflage net, earth
  revetment, sandbag wall, cut branches, AA gun and radar surrounds (counts and radii computed from the export).
- Cutaways: command bunker (roof slabs, surface installations, camouflage net: 78 parts), barracks hall (roof sheets,
  ridge cap, ceiling, gables: 14), ammunition bunker (earth mound and lobes, turf clumps: 8); named in the captions.
- Vegetation: the five primitive tree / shrub designs and the boulder each say which CC0 mesh replaces them at ULTRA /
  EPIC and its LOD 0 triangle count; those meshes appear in no table.
- Fig. 19.x munitions (new): SAM missile, MLRS rocket, UAV round and AA shell casing at one scale with a 10 cm bar,
  plus the two small ones enlarged with their own bar.
- Kept and restyled with the key: UAV and soldier part-tile catalogues (recaptured without vignette; key chip and size
  in cm under every tile), soldier scene graph (key chips per node; animated joints by a thick border instead of the
  orange fill, which is the sphere's key colour), gait figure, soldier and tank variant tables, UAV and tank build
  stages (now drawn in the key colours, `stage=` captures). Added build stages for the helicopter and the command bunker.
- Sky bodies (sun, moon) show only the primitives panel (the renderer draws them in the sky pass, not as lit parts).

Removed: Fig. 19.17 (workbook block repeating the parts tables), the assembled-only figures of the tank, helicopter,
AA gun, SAM launcher and MLRS (now cards), Fig. 19.16 (six parts tables without pictures), the "largest parts" tables
19.2-19.5 (every part kind is now in the cards), the per-(primitive, material) tables with k_s / n columns, the old
exploded F/G captures of the UAV, rover, bunker and soldier, and the critique sentence about static scenery being listed
only in Table 19.1. `tools/v10/gen_decomp.py` / `gen_decomp.tex` are no longer used.

Layout: cards are non-floating blocks (`\needspace` before each, `\captionof`), parts tables are 7 pt longtables that
break across pages; family subsections end with `\FloatBarrier`. Section 19 is pages 45-91 (was 40-53).

# Earlier round (v10 follow-up: M1 legibility, M2 consistency, M3 adoptions)

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

## M3: 16 primitives adopted in the program and documented

Program (merged from the M3 worktree branch): 9 new generators (hemisphere, torus, wedge, tapered box, pyramid, capsule,
annulus, helix, grass blade moved out of Grass.cpp) with tests T410-T435 (388 tests, 0 failed); parts moved onto them
(helix concertina wire, torus tyres / treads / muzzle rings / cupola and motor rings, hemisphere radome / helmets /
earth berm, wedge ramp floor / glacis / wing-wall slopes, tapered-box turret bodies / torsos, capsule sandbags,
pyramid gate-post caps, annulus pad rings); export schema 8 (16 primitives with parameters, formulas, corner and face
lists; per part S_rest, hidden_at_export, dfs order, part kind; per object variants, instances, build stages); capture
modes `prims`, `parts:<object>`, `build:<object>` (capture group `m3`).

| Where | Old | New |
|---|---|---|
| Scene parts (`\nLeaves`, Table 19.1 total, Table 6.1 total) | 5 736 | 5 476 (312 "Wire coil turn" cylinders -> 52 helices) |
| Table 6.1 | 6 rows (Cube 2 478, Sphere 826, Cylinder 2 133, Frustum 3, Cone 295, Terrain 1) | 16 rows P01-P16 with V / T formulas: cube 24, cuboid 2 443, sphere 808, hemisphere 10, cylinder 1 358, frustum 3, cone 295, torus 97, wedge 6, tapered box 11, pyramid 2, capsule 364, annulus 2, helix 52, grass blade 0 parts (450 000 GPU instances at EPIC), terrain disc 1 |
| Table 6.1 caption | "...counted over the whole scene." | + "V and T are given as formulas of the generator parameters ... and as the counts of the meshes the scene uses." |
| Fig. 6.1a | old library render (7 primitives) | all 16 primitives with triangle edges (`ts --capture prims`); subcaption keeps its wording + "(all 16, with their triangle edges; ts --capture prims)" |
| Section 6 text | "Cuboids are scaled unit cubes." | + "The library has sixteen generators (Table 6.1); every one fits the unit cube, so a part's scale S is its size in metres." |
| Section 6 critique | "This code has seven ... There is no torus, wedge, hemisphere or helix generator; ... flattened sphere ... all 826 sphere instances use 1224 triangles each (about 1.0 million triangles)" | "the library has sixteen, and each is used in the scene ... ray tracer stand-ins ... all 808 sphere instances use 1224 triangles each (about 0.99 million triangles)" |
| Section 6 where | Cube, Sphere, Cylinder, Terrain | all generator files + Library.h |
| new Table 6.2 | -- | where each primitive is used (parts and most frequent part kinds) |
| new 6.1 "The vertices of each primitive" | -- | corner drawings + tables of cube / wedge / tapered box / pyramid (Fig. 6.3, from the export's corner and face lists), why the cube has 24 vertices, Table 6.3 parametric generation of all 16 |
| Workbook sheet Primitives (Fig. 6.2, App. A.1) | 7 rows + "not primitives" list + 5 parametric rows | 16 rows with live V / T formulas (dV = dT = 0 for all), 16 parametric rows from the export; prints at 77 % (7.0 pt) |
| new 7.1 "Following vertices into the world" | -- | Groups A (bunker retaining wall: 8 corners), B (tank road wheel: torus turned by R_y onto the axle), C (soldier hip / knee standing vs walking; blast-door leaf closed vs open, slide 1.5 m), D (UAV camera under gimbal yaw / pitch); Tables 7.1-7.4, matrices computed from the export (composition matches the exported world matrices to 2e-5) |
| Section 19 intro | "part kinds grouped by name and primitive, their count, the local scale S of the first part of each kind, its base colour, and its material" | parts grouped by primitive and material covering every part; main-parts tables; part-kind grids; build stages |
| Section 19 per-object tables | top 9 (name, primitive) kinds + "N more part kinds" | every part, grouped by (primitive, material), with the part kinds of each group |
| new Tables 19.x | -- | largest parts with S, T, R, parent: UAV, soldier, tank, command bunker |
| new Figures 19.x | -- | UAV and soldier part-kind grids; UAV and tank build stages 12 / 35 / 65 / 100 %; soldier scene graph with T / R per joint; walk cycle plot (export v6.garrison.gait.cycle) and gait equations (Eq. 19.1, Gait.cpp) |
| new variant tables | -- | 8 soldiers (root T, heading, girth, recoloured parts, posed joints, duty); 3 tanks (only the root transform differs) |
| Table 19.1 | columns Cube, Sphere, Cylinder, Frustum, Cone | Cube, Cuboid, Sphere, Hemisphere, Cylinder, Frustum, Cone, Torus, Wedge, Tapered box, Pyramid, Capsule, Annulus, Helix (rotated headers); Base 1 253 -> 993 parts |
| Section 19 critique | "1 253 + 652 + 658 parts" | "993 + 652 + 658 parts" (macros) |
| Tank caption | "118 of its nodes are animated" (typed) | `\tankAnim` (118, from the export) |
| Section 22 (curved) | "tyres ... helmets and heads (spheres and cylinders); the bunker's earth berm (a flattened sphere)"; critique "There is no torus or helix generator, and the primitives have no level of detail" | tyres and muzzle rings (tori); helmets, radome, berm (hemispheres); sandbags (capsules); concertina wire (helices); critique "The primitives have no level of detail" |
| Limitations | "no torus, wedge, hemisphere or helix generator" | "no level of detail; the ray tracer stands in a sphere, cylinder or box for seven of the sixteen and leaves the helix out" |
| Conclusion Table | "7 primitives" | "16 primitives" |

### Regenerated after the M3 merge

- Workbook: new sheet `Vertex_Groups` (groups A, C, D as live formulas; leg forward kinematics reproduces the exported ankle path) shown in Section 7.1 and in the appendix; 15 sheets, 2 355 formula cells, 0 errors. Primitives sheet now prints on two pages (page break before the parametric block).
- All captures re-rendered (scene, v5, play, lab, rt, game, m3 groups); Frame Explorer insets, plots and workbook recomputed.
- Benchmark re-run (`TS_BENCH_V10=1`, 3 rounds x 10 s, RTX 4070): the benchmark table (Section 30) and the analysis paragraph are generated from the new CSV. Fewer triangles after the helix replaced 312 wire cylinders.

| Configuration | mean ms old -> new | fps old -> new | triangles old -> new |
|---|---|---|---|
| EPIC day 10:30 | 14.87 -> 11.90 | 67.3 -> 84.1 | 5.73 M -> 4.11 M |
| EPIC night 23:00 (game: waves, UAVs) | 16.38 -> 15.33 | 61.0 -> 65.2 | 5.80 M -> 4.12 M |
| EPIC day, thermal view | 10.72 -> 12.93 | 93.3 -> 77.3 | 2.73 M -> 1.48 M |
| EPIC night, thermal view | 12.62 -> 13.32 | 79.2 -> 75.1 | 2.50 M -> 1.55 M |
| EPIC day, shadows off | 16.42 -> 15.43 | 60.9 -> 64.8 | 5.86 M -> 4.14 M |
| EPIC night, shadows off | 17.18 -> 16.78 | 58.2 -> 59.6 | 5.87 M -> 4.17 M |
| EPIC day, non-indexed | 54.47 -> 52.97 | 18.4 -> 18.9 | 5.86 M -> 4.13 M |
| ULTRA day | 16.76 -> 16.69 | 59.7 -> 59.9 | 4.31 M -> 2.61 M |
| HIGH day | 15.11 -> 14.91 | 66.2 -> 67.1 | 1.41 M -> 1.52 M |
| LOW day | 14.16 -> 12.62 | 70.6 -> 79.3 | 1.32 M -> 1.39 M |
| v1 look (no post) day | 13.01 -> 11.70 | 76.9 -> 85.5 | 1.34 M -> 1.44 M |

The analysis sentence "Every configuration except non-indexed drawing averages at least 60 fps" is no longer generated: EPIC night with shadows off (59.6 fps) and ULTRA (59.9 fps) fall just below 60 in this run.

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
