# Report changes: final-submission pass (M0-M5)

Build note: this machine has no latexmk / pdftoppm, so the PDF is built with `tectonic` and pages are rasterised at
110 dpi with PyMuPDF (same output as `pdftoppm -r 110`).

Page count before this pass: 106 (body 1-78, workbook appendix 79-106); after: 127 (body 1-109, appendices 110-127). See M5.

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

## M4: repetitive, template-like writing removed (whole report)

`tools/report/lint_prose.py` (new) checks every section that `main.tex` inputs (generated files included) except
Section 3 and the appendices, and fails on the forbidden patterns. Counts at the start of the pass (`--rev 9a4e54a`)
and now:

| Pattern | Before | After |
|---|---:|---:|
| "Notice ..." (caption opener) | 42 | 0 |
| "Builds on:" | 10 | 0 |
| "Where in the project" | 23 | 0 |
| version tags (v1 ... v12) | 36 | 0 |
| "the brief" | 9 | 0 |
| "honest(ly)" | 2 | 0 |
| `--export-docs` | 10 | 0 |
| `\concept` paragraphs ("Concept.") | 13 | 0 |
| `\critique` ("Critique.") | 23 | 0 (21 kept as "Limitation", the rest folded) |
| "the program" | 36 | 4 |
| "Every claim / painted over / honestly" | 4 | 0 |

What changed:
- Captions: every "Notice ..." caption is now one sentence saying what the figure shows, with at most one direct
  observation; the longer explanations moved into the body next to the figure.
- "Concept." labels removed (the paragraph stays); "Builds on:" lines deleted (the pipeline banner shows it); all 23
  "Where in the project" paragraphs replaced by one appendix table, "Where each section lives in the code".
- Critiques: specific, non-obvious ones kept and renamed "Limitation" (21); the brief-related ones (clip window, N/D
  keys, live thermal panel, 6-hour shifts, backup fire, moon rise, enemy toggle key) moved to the new appendix table
  "Requirements coverage" (requirement -> section -> done / partial / not done, from `docs/audit.md`); the world-update
  cost is also listed in Section 31.
- Development history removed: version tags, "what each key did before v9", `docs/v9_notes` and `tools/v10` paths in
  captions and text, and code identifiers used as prose (mixLog -> mix_log notation, terrainShade, surfaceAlbedo,
  RigPose.rot, DayCycleConfig, weave / speedWander / hesitate, CameraRig::kBlendTime, kMaxPracticals,
  Renderer::drawTranslucent, Combat::explodeImpl / updateParticles, Scene::updateDebris, HideController::update,
  MultiTracker::triangulate, Squad::update, roundFlightTime, v6.garrison.gait.cycle, kTaaFrag). The benchmark
  configuration "v1 look" is shown as "classic look". The regeneration commands are in one new appendix,
  "Reproducing the figures" (the benchmark command moved there from Section 29).
- Reassurances: one statement in Section 3 that every image is a program capture (or a plot / workbook block) and
  every number comes from the export through a script; "Every claim is backed...", "nothing is painted over",
  "listed honestly", "(--export-docs)" and "rendered / captured by the program" removed elsewhere; the cover's
  provenance line removed.
- Section 20 title "Colour interpolation, with critique" -> "Colour interpolation".
- Workbook blocks (`tools/v10/sheet_to_tex.py`): meaningful precision (trailing zeros dropped: 5.5 not 5.5000; 3
  decimals in tables and matrices; values below 1e-6 printed as "< 10^-6"). Workbook figures deleted because the same
  section already has the content as a table: Fig. 7.4 (`Vertex_Groups`, repeats Tables 7.1-7.4), Fig. 29.2
  (`Performance`, repeats Table 29.1); Figs. 6.2 and 19.17 went in M2 / M3. The remaining blocks (Transform_Trace,
  View_Projection, Clipping, Illumination, Ray_Tracing, Color_Interp, Thermal_Kernels, Tracking, Duty_Cycle,
  Line_Follower, Explosion) hold worked values that are not tabulated elsewhere and stay.
- Over-precise numbers: the day length prints as 291 s (was 290.6), the benchmark analysis to 0.1 ms / 1 fps;
  "k_s / n" no longer appears (the material columns of Section 19 were dropped in M3).
- Cover: "parts in designs" instead of the old object count; Conclusion's O1 row likewise.

## M5: consistency and layout

- New `tools/report/check_counts.py` (passes): for all 62 designs the shape-view legend = parts table = export row =
  Table 19.1 row; each primitive's instance count in Table 6.1 = the sum of its Table 19.1 column (15 primitive
  columns, cube and cuboid separately); the scene total is 5 476 in the export, Table 6.1, Table 19.1's totals row, the
  sum of the designs and the macros `\nLeaves` / `\sceneParts`. The one hand-typed "5 476" (grass-blade card) now comes
  from the export.
- Cross-references: no undefined or duplicate labels; the ToC lists the Section 19 families as subsections. The report
  has no list of figures (it had none before this pass either).
- Floats: body figures and tables moved from [H] to [htbp] with `placeins` barriers at every section (`[section,below]`),
  so no float leaves its section (checked: 0 figures or tables after the start of the next section). Section 19
  cards are in-place blocks; the soldier scene graph and gait share a float page; the munitions figure closes its family.
- Pages scanned at 40 dpi for the largest blank band: no body page is more than 40 % blank (the largest is 38 %;
  only the last bibliography page and the appendix openers are emptier). Every page was rasterised and looked at.
- Smallest text: text spans below 7 pt are only sub- and superscripts (indices, exponents, degree signs, daggers).
  Fixed on the way: workbook matrices now `\textstyle` (were `\scriptstyle` inside `\scriptsize`, 6.2 pt), the
  particle-diagram legend and the flood-fill cell numbers 7.5 pt (were 7 / 6.5).
- Overfull boxes: none wider than 3 pt (were up to 64 pt in the new cards: break points after commas and x in the
  parts tables' patterns and in the placement formula, narrower table columns and panel pairs, tile grids).
- Workbook appendix trimmed to the 12 sheets the body cites (`gen_wb_appendix.py` reads the `\fullsheet` / `app:wb-`
  references): 14 instead of 27 landscape pages. Not printed any more: Primitives, Decomposition, Performance (their
  content is in Tables 6.1, 19.1 and 29.1).

Page count: 106 before this pass (body 1-78, appendix 79-106); 127 after (body 1-109, appendices 110-127:
requirements coverage, source files, reproduction commands 110-112, workbook sheets 113-127). The body grew by the
primitive cards (Section 6: 4 -> 9 pages) and above all by the design cards (Section 19: 14 -> 47 pages), which list
every part kind of all 62 designs.

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

## Round 2: structure, visuals, workbook

- **Section order.** The decomposition catalogue now follows the primitives directly (Section 7). Part B no longer
  starts with it, and its map node is gone. The report ends with the bibliography: the appendices (C04,
  `gen_wb_appendix`) were removed. The requirements-coverage table moved to the end of Limitations (Table `tab:req`), and
  workbook pointers name the sheet in `report/workbooks/cg_workbook.xlsx`.
- **Curved primitive cards (Figs 6.6-6.16)** now carry a numbered vertex table like Figs 6.1-6.5 (`i, x, y, z`, up to 12
  vertices, plus the triangles of the highlighted cell), built by `gen_prim_cards.py` from the low-res mesh it draws.
- **Object atlas (Fig 7.1)** rebuilt: two pages, 8 designs per row, each thumbnail cropped to its content and padded to
  one aspect (`atlas_<slug>.jpg`). Family headers carry the count of designs and parts. Concertina wire and perimeter
  fence use the outlined primitive render (the shaded wire is invisible at this size). Table 7.1 (one row per design)
  was removed; the same numbers are in the workbook's Designs sheet and are still cross-checked by `check_counts.py`.
- **Build stages (Fig 7.13 and the other strips):** the UAV's searchlight beam (a `BeamCone` light volume) is no longer
  drawn in the stage views, where it was a solid that reached the ground and covered the model. All four stages of a
  strip share one crop around the full design.
- **Section 2** is a 10-panel showcase (sunset, grass, foliage, night base, searchlights, a night wave from the
  watchtower, a strike, third person, thermal gimbal view, SSR vs ray-traced reflections).
- **Visuals removed where weak:** A05 (kept perspective / isometric), A07 (assembly stage only), A08 depth panel, A12
  relief comparison, B05 door-opening / exposure pairs, B06 (ray-trace comparison only), B07 (scene thermal only),
  B08 events figure, B09 (plan routes only).
- **Plots upgraded (`tools/v10/plots.py`):** benchmark grouped and colour-coded by kind (presets / views / ablations)
  with fps labels, a sub-16.7 ms band and a draw-call panel. The day cycle gains a strip of the zenith, horizon and
  key-light colours read from the export's `v6.day_cycle.grid`. Tessellation shows the 8 / 16 / 36-slice meshes that
  `createSphere` builds, beside the error curve.
- **Text:** B10 is now one table (problem / why / solution / measured); B01, B08 and B08b intros were shortened. Table
  captions moved above their tables. Part B sections have pipeline banners. Hero sub-figure references were corrected.
- **Layout:** float pages fill from the top (`\@fptop = 0pt`) instead of being centred.
- **Workbook:** new Designs sheet (one row per design: export inputs, `n x parts` formulas, row checks, scene total
  against `leaf_count`, colour key with L*). The Decomposition sheet's columns follow `PRIM_ORDER` (14 primitive types)
  instead of fixed column letters, which had produced 45 DIFF cells. 16 sheets, 0 errors; Decomposition 75 OK / 0 DIFF,
  Designs 63 OK / 0 DIFF.
- Checks: 396 tests pass; `check_counts.py` OK (5 476 parts everywhere); 0 overfull boxes, 0 undefined references.
  Page count: 111 (was 127; appendices removed).

## Round 3: visual storytelling, interiors, abbreviations

- **Removed:** the requirements-coverage subsection and its table (old Table 32.1) and the reference to it.
- **Section 2:** two new full pages follow a soldier at eye height into the command bunker (gate, trench, tunnel,
  command room, generator door shut / open) and the barracks hall (door shut / opening / open, inside by day and night,
  looking out). New capture shots `sc_*` (HUD-free 1920x1080 EPIC) with a `doors` option that opens the blast doors,
  the hall door or the generator-room door before the settle; `TS_CLEAN=1` renders the gameplay shots without HUD.
- **Section 7:**
  - Vehicles are now followed by the munition each one fires. The new family "vehicles and their weapons" orders the
    UAV, then UAV round, SAM launcher, SAM missile, MLRS truck, MLRS rocket, AA gun and shell casing. The atlas labels
    each munition "fired by ...".
  - Every card shows the design in the running scene. `tools/report/insitu_cams.py` places one camera per design from
    the export's part bounds (`TS_INSITU` shot file). Live-following shots cover the UAV and the helicopter. Other
    captures are used where nets, the backdrop or night units need them.
  - New grass card (`tools/report/gen_grass.py`): the grass-blade primitive, one tuft and a patch, built with the
    rules of `rendering/Grass.cpp` in the key colour, plus the rendered meadow and a parameter table (budgets parsed
    from `GrassBlade.h`). Grass also appears in the atlas.
  - Build stages are laid out 2x2 at half page width (1400 px).
- **Sections 8-29, one project figure per concept** (`tools/report/annotate.py` call-outs on the captures):
  - TRS placements of huts, towers, barriers and the hinged door;
  - the tank's and UAV's group nodes;
  - four cameras, 2x2;
  - the light types in the scene;
  - flat, Gouraud and Phong parts (from the export's shading field);
  - procedural versus image textures;
  - thermal palettes as colour interpolation, with ramps plotted from the shader stops;
  - the same camera at three times of day;
  - curved primitives at the AA position;
  - the door and blast-door dynamics;
  - larger path, reflection and explosion figures (explosions 2x2).
- **Fig 25.1** replaced by `tools/report/gen_cv_story.py`: the scene, the UAV's heat image with a zoom box, the
  histogram with Otsu's threshold against the floor actually used, then the window through threshold, despeckle,
  components and target, all from the program's stage outputs and `cv_frame.json`.
- **Abbreviations:** a list after the contents (69 entries) and the full form at first use in the prose, both from
  `tools/report/abbrev.py`.
- **Image quality:** captures are converted at the full 1920 px (ffmpeg q 4; `FORCE=1` re-encodes everything).
- Checks: 396 tests pass; `check_counts.py` OK; no figure drifts out of its section; 2 small overfull vboxes on two
  card pages. Page count: 127. PDF: 55 MB (about 500 images).

## Round 4: interiors, route maps, Part B overview

- **Interiors:** a third "walking in" page in Section 2 with the manned posts and the other bunker rooms: both guard-tower
  views, the watchtower, the bunk room at night, the generator room and the command-room corner by the exit shaft
  (`sc_pov_*` shots). The text says which buildings are walkable (bunker, hall) and which are closed shells.
- **Route maps (Section 28):**
  - The export now carries the garrison's duty plan, through `Garrison::planJson`: per soldier and shift, the stops
    (label, point, activity) and the walked routes sampled every 0.5 m.
  - The export's 130 s simulation now also records the rover and UAV tracks.
  - `tools/report/gen_maps.py` copies these to `report/data/routes.json` and draws a guard map (day and night shifts,
    every post and route, coloured per soldier) and a rover route map (every life of every rover, spawns, the five
    objectives, the UAV orbits) over a site plan built from every part's footprint.
  - The main export used by the report numbers is unchanged.
- **Part B:**
  - A new "at a glance" figure: each advanced section with the class method it starts from, what ThermalSense does and
    the measured result.
  - A before/after chart in Section 29 (`tools/report/gen_fix_chart.py`).
  - Prose that repeated the figures was cut in Sections 21, 24, 27 and 28.
- **Removed:** the objectives table in the conclusion (old Table 32.1).
- Checks: 396 tests pass; `check_counts.py` OK; no figure drift. 132 pages.

## Round 5: algorithms, cards, film tooling

- **Algorithms:** every algorithm of the program now has pseudocode with its time (and, where relevant, space)
  complexity, written from the source (`report/sections/algorithms.tex`, each placed in its section):
  - frustum culling and Sutherland–Hodgman clipping;
  - edge-function rasterisation with the Z-buffer;
  - Whitted ray tracing over a binned-SAH BVH;
  - the thermal CV pipeline (Otsu, despeckle, two-pass union-find, moments);
  - the multi-target tracker (Kalman, χ² gate, Hungarian, triangulation);
  - the UAV squad (roles, time-on-target, proportional navigation);
  - the explosion particle system;
  - the soldiers' route planner (A* with octile heuristic, string pulling, Catmull–Rom repair);
  - the gait and two-link IK;
  - the intruder rover, which now also states its complexity.
- **Section 7 cards:**
  - Parts hidden at the start are no longer drawn in the pulled-apart panel. The soldier's carried crate sat on the
    held rifle in Fig. 7.2; the legend and parts table still list hidden parts.
  - Weak in-scene panels were replaced: helicopter and tank (cinematic day frames from the showcase recording), radar,
    UAV and UAV round (close follow on the pad), HESCO perimeter, concertina wire and road (close views).
- **Recorder:** `TS_FILM_CAM` (a free camera gliding between two poses at a fixed hour, HUD off), for the presentation film.
- 129 pages, no overfull boxes, no float drift.
- **Presentation film** (`report/video/thermalsense_film_2min.mp4`, 120 s, 1280×720, 17.2 MB, with the program's audio;
  gitignored like the other videos): `tools/report/gen_film.py` assembles it from the showcase recording, a v6-cut
  recording and `TS_FILM_CAM` glide shots, with an animated route map (`tools/report/gen_rover_anim.py`).
  - 0:00–1:00: the pipeline stages, soldiers on duty, and the barracks hall and bunker interiors.
  - 1:00–2:00: rover route planning, thermal tracking, the shared track, the combined strike with a slow-motion
    impact, and soldiers moving and firing.

## Round 6: pipeline rigor

- **Source check:** the renderer uses no maths library and no fixed-function lighting, shading or matrix stack (OpenGL 3.3 core;
  `src/math/Math3D.cpp` holds every matrix, `ShaderSources.cpp` every lighting equation). Library or fixed-function parts
  are only: clipping, perspective divide and viewport, back-face culling (`glCullFace`, Renderer.cpp:647), depth test
  (`glDepthFunc`), MSAA resolve (`glBlitFramebuffer`, FrameBuffers.cpp:191), mip-map build (`glGenerateMipmap`: GroundMap.cpp:156,
  Hud.cpp:60, VegetationRenderer.cpp:83), the shadow-compare sampler (FrameBuffers.cpp:24) and `glPolygonOffset`
  (RendererFrame.cpp:194, Renderer.cpp:654/760, Effects.cpp:444). The new Table "Fixed-function GPU stages" (Section 12) lists
  each with the project's own CPU equivalent (`math/ExplorerMath.cpp`).
- **Per stage (equation, code excerpt, table, curve):**
  - 6 primitives: sphere generator equations, vertex/triangle/Euler counts, `createSphere` excerpt.
  - 7 (new subsection 7.1): basic matrices T, Rx, Ry, Rz, S, M = T Ry Rx Rz S, world chain, order diagram.
  - 8 transforms: homogeneous points/directions, normal matrix with a numeric example table, code table, `rotateY`/`normalMatrix` excerpt.
  - 9 scene graph: `updateNode` excerpt. 10 viewing: V = R_uvn T(-e), `lookAt` excerpt.
  - 11 projection: fov/aspect, oblique shear, `perspective`/`ortho` excerpt, table of every projection built.
  - 12 projective: viewport matrix, depth inversion, fixed-function table. 13 clipping: outcodes, Liang-Barsky, S-H, `Frustum.cpp` excerpt.
  - 14 HSR: screen-area test, depth affinity, method table. 15 raster: edge functions, top-left rule, `rasterize` excerpt,
    DDA/Bresenham, MSAA/TAA/Halton equations, plot (jitter, history weights, lines), preset table.
  - 16 illumination: Phong vs Blinn-Phong, as-implemented colour equation, lamp window falloff, GGX/Smith/Schlick with the
    `brdf()` excerpt, attenuation and lobe plots, equation-to-code table; new subsection 16.1 Shadows (shadow map, PCF, PCSS,
    bias, plots, preset table, excerpt).
  - 17 shading: flat/Gouraud/Phong shader excerpts and table. 18 texture: box projection, value noise, fbm, bump, filtering,
    noise plot, bump figure, table.
  - 19 (new section) fog, exposure, AgX/ACES tone mapping, sRGB gamma, bloom, with curves and table.
  - 20 ray tracing: object-space rays, box/cylinder/triangle, exact Fresnel vs Schlick (plot), SAH, table.
  - 21 colour: lerp and palette equations. 23 curves: centripetal Catmull-Rom, Chaikin (plot). 26 image processing: convolution,
    separability, equalisation, Otsu.
  - New plots: `tools/report/gen_pipeline_plots.py` (p_attenuation, p_phong_blinn, p_fresnel, p_shadows, p_tonemap, p_fog, p_noise,
    p_curves, p_antialias).
- **Flow diagrams:** `report/sections/flows.tex` (TikZ), one per algorithm, placed before it: frustum culling and clipping,
  rasterisation, Whitted/BVH, thermal CV, tracker, squad, particles, soldier route planning, gait IK, intruder rover.
- **Abbreviations:** the list page and `gen_abbrev.tex` are removed; `tools/report/abbrev.py` now only expands each short form at
  its first use in the prose (cover included) and exports `ABBR`/`expand_first`. `gen_designs.py` expands the first use inside
  Section 7 itself (family list, atlas, cards: UAV, SAM, MLRS, AA, MG, LED, EO, IR, NVG, VHF, FLIR, HESCO (wire-mesh gabion), ...).
- **Section 7 transformation chains:** every card has unboxed numbered equations (root placement W_root with its numeric matrix,
  one part's local matrix M, the chain W_part = W_root M_g1 ... M, numeric world matrix), generated by `gen_designs.py`. The
  generator stops if a chain product differs from the exported world matrix or T Ry Rx Rz S from the exported local matrix by more
  than 1e-3 (124 chains checked, largest relative error 1.3e-7).
- **Fig 7.57(a)** (grass card): the three titles of panel (a) overlapped; the panel is wider and the title smaller
  (`tools/report/gen_grass.py`).
- Large cards start after a float barrier; build strips placed inline (no float page overflow).
- New captures in the text, converted by `tools/v10/prepare_figures.py` from existing lab captures (no new program run):
  `lab/materials_blinnphong_row`, `lab/materials_pbr_grid` (Section 16), `lab/explorer_stage_08_shadow` (16.1),
  `lab/bump_on_off` (18), `lab/explorer_stage_12_fog`, `lab/explorer_stage_14_tonemap` (19).
- Checks: `check_counts.py` OK; `lint_prose.py` 0 failures (it now skips code listings); 0 overfull boxes; 0 undefined
  references. 178 pages. The Command bunker card must be regenerated from a fresh export if the bunker redesign changes its parts.

### Round 6 addendum: bunker entrance and second searchlight (program at d99baa9)

- **Export replaced** (`report/data/scene_export_v10.json`, from a binary built at d99baa9). Only the Command bunker changed:
  390 to 403 own parts (cuboid 281 to 290, cylinder 80 to 82, sphere 18 to 20; 427 to 440 with its nested net), scene total
  5 476 to 5 489 parts; 62 designs and 244 instances as before. The 13 new parts are flat-shaded (783 to 796 flat parts).
- **Regenerated:** bunker shape view, cutaway and four build stages, the camp-of-primitives view (`capture_shapes.py` tokens with
  the clean binary); `gen_numbers.py`, `gen_m3.py`, `gen_prim_cards.py`, `gen_designs.py`, `gen_maps.py` (site plan; routes
  identical), the workbook and its typeset blocks; trench captures `sc_bunker_ramp_closed` / `sc_bunker_ramp_open`.
- **Generators:** `gen_m3.py` works the west gate leaf as a rotation about its jamb hinge (was a slide); `build_cg_workbook.py`
  finds the roof slab by name (it used a part id, which shifted); `gen_game.py` reads the C++ sources from `TS_SRC_ROOT` when set.
- **Text:** Section 8.1 group C (leaf rotation R_y(-92 deg) about the hinge), Section 2 walk-in caption, Section 24 table row and
  figure (gate shut, gate open), Section 16 light table (two searchlights: UAV 1 shadowed, UAV 2 unshadowed), Section 19 (both
  searchlights have a shaft in the volumetric pass).
- Checks: `check_counts.py` OK (5 489 everywhere); lint 0; 0 overfull boxes; 0 undefined references; 179 pages.

## Final night

- **Program.** Every shader uses hand-written vector functions (`src/rendering/ShaderMath.cpp`: `tsDot`, `tsLength`, `tsNormalize`,
  `tsCross`, `tsReflect`, `tsRefract`, `tsMix`, `tsClamp`, `tsStep`, `tsSmoothstep`, `tsTranspose`; 905 calls in 13 files); the compile
  step refuses a shader that calls a built-in vector function. Weapons on alert (launchers raised through the net apertures and sweeping,
  helicopter at flight idle, tank and AA gun laid on the tracked intruder). A soldier gets into and out of the tank, AA gun, launchers
  and helicopter with `E`. Guards at their posts fire on a tracked intruder within 26 m. Squad: a second intruder more than 14 m from
  the target and within 10 m of a protected point takes the nearer UAV (two single strikes); a lone intruder gets the combined strike
  (the lead waits up to 8 s for the wing). The motor-pool bay is open at the front so the tank can drive out. 396 tests pass.
- **Film.** `report/video/thermalsense_final_1080p.mp4` (3:38, 1920×1080, 336 MB; 720p copy 49.5 MB; 960×540 slide copy 18 MB) from
  `tools/report/gen_film3.py`; footage by `build/film3/final.sh` with the recorder's scripted takes (`TS_TOUR` steps use, face, drive,
  turn, aim, fire, sight, cam, camfix, exit) and film views (`TS_FILM_VIEW` follow / uav1 / flir, `TS_FILM_EVENTS`).
- **Report** (192 pages, export of the final build: 5,474 parts).
  - Cover in the department's layout; "Submitted by / Submitted to" in a table with year, semester and lab group; no date.
  - New Section 16.1 "Vector maths written by hand" (equations, source excerpt, table of calls); shader excerpts and Table 16.1 use
    the `ts*` names.
  - Section 7: a coordinate sheet per design (every component's centre and extent in the design frame, 2,804 rows, checked against
    the chain of local matrices); family headings start a page; the space test before cards is removed (it left pages half empty).
  - Every heading and caption that names an object by a short form carries the full form in brackets (`abbrev.py` title pass).
  - Flow diagrams regenerated as one-row chains of at most six boxes and one decision (`tools/report/gen_flows.py`).
  - Section 29.1 "The motion over the whole base": six moments of one night over the whole camp and one rover's time series
    (`tools/report/gen_motion.py`) replace the two debug captures of the old Figure 29.4.
  - Section 27: single strike, combined strike, guards, alert posture, getting into the vehicles; Algorithm 6 and its flow updated.
- **Critique pass.** Found and fixed: two pages holding only a heading and twelve half-empty pages in Section 7 (now four short pages,
  all float pages or section ends); a code table that still showed built-in calls; site labels covering the tracks in the motion
  panels; the motion subsection split by floats. Checks: 0 overfull boxes, 0 undefined references, `check_counts.py` OK.
- **Slides.** `report/ThermalSense_Presentation.pptx`: 25 slides (new: vehicles, single / combined strike and guards, hand-written
  vector maths), the final film embedded.
- **Later the same night.**
  - Contents page in the academic layout: roman-numbered front matter, dotted leaders on every level, Parts I to III
    ("Back matter" removed), References listed; running header without short forms.
  - All text in the Times face of the body (TeX Gyre Termes), cover headings and the labels inside every plot included
    (`tools/report/fonts`); code stays monospaced.
  - Full form at the first use of every short form on every page: `tools/report/abbrev_pages.py` tags the 607 short forms of
    the running text, reads from `main.aux` which one is first on each page and rebuilds until stable (191 expanded); floats,
    captions and headings carry their own full forms (a "Short forms" note when a table or drawing uses one). Left as
    they are: one code comment and one cited title.
  - Captions cut to one-sentence titles (`tools/report/shorten_captions.py`: caption text 4,870 to 3,463 words); the chain and
    sheet sentences of the 62 cards shortened and explained once in Section 7.1; one spacing rule for floats and captions.
  - `tools/report/check_numbers.py`: the hand-typed numbers (strike rule, guards, calls of the hand-written functions,
    sheet rows, tests) against the code; all agree.
  - `docs/CODE_TOUR.md` (120 clickable stops in pipeline order) and `docs/REPORT_CODE_MAP.md` (33 sections, 143 code
    references, 62 designs), generated from the code so the line numbers are exact.
  - Repository: new `README.md`; stray files in `archive/`; course documents in `docs/reference`; the old README in
    `docs/history`; LaTeX artefacts untracked; `ThermalSense.exe` rebuilt from the final sources.
  - Film sound: every shot levelled to one loudness over a continuous ambience bed (day, then night), compressed and
    limited: integrated loudness -14.0 LUFS, loudness range 3 LU, peak -0.7 dB.
  - Workbook rebuilt on the final export (2,641 formula cells, no errors); `tools/report/check_workbook.py` compares it with
    the export, the code and the report (149 checks); the family names match Section 7.
  - Repository: tool caches, development notes and the earlier workbook, report and figure generations removed; the
    film (720p and slide copies) is part of the repository.

## After the showcase: full-length film, workbook registers, public documentation

  - Program, one defect fixed: the helicopter's rotor head stood above the fuselage with a gap under it. The engine
    cowling now reaches forward under the rotor (`src/objects/Helicopter.cpp`: size 0.95 x 0.75 x 1.9 m at
    (0, 0.95, -1.15) -> 0.95 x 0.9 x 2.5 m at (0, 0.95, -0.9)) and the mast stands in it (0.18 x 0.18 x 0.55 m at
    y = 1.55 -> 0.34 x 0.34 x 0.87 m at y = 1.385). Two components of the export changed (HEL-07, HEL-09); the scene
    still has 5,474 parts; 396 tests pass.
  - Recorder only (`src/app/Record.cpp`, no new program feature): `TS_FILM_SCRIPT`, timed steps that do what the keys
    and the mouse do (switches of the key map, camera poses and orbits, jumps into an object's view, the pointer and
    clicks, held controls, the menu, the Frame Explorer, the clock), and `TS_FILM_ALGO`, a record of what the tracker,
    the squad and the rovers' planners hold ten times a second.
  - Film: `tools/film` (the film as data in `acts/*.py`, plates, score, sound mix, assembler); 32 min 44 s, 149 beats
    in 30 chapters; narration and shot list in `docs/FILM_SCRIPT.md`, subtitles and chapter list beside it; timeline
    in `report/data/film.json`. Each chapter draws the idea step by step, then shows it in the running program.
  - Film sound: narration (synthetic voice), the program's own sound, and a score written in code, mixed with the
    narration leading. Measured on the master: integrated loudness -14.2 LUFS, loudness range 2.9 LU, true peak -1.1 dBFS; the 30
    chapters lie between -13.8 and -15.5 LUFS. Mean level per band (dB): below 150 Hz -23.7, 150-600 Hz -21.2,
    600 Hz-2.5 kHz -24.8, 2.5-8 kHz -26.9, above 8 kHz -34.3. The four copies in the repository (1280 x 720, 61 to
    83 MB) carry the master's audio unchanged; decoded from their own start they peak between -0.4 and -1.3 dBFS.
  - Workbook: 23 sheets (2,659 formula cells, no errors). New sheets `Index`, `World_Coordinates` (all 6,069
    components), `Layout`, `Algorithms` (every constant with its source line) and `Film`
    (`tools/v10/wb_registers.py`); `tools/report/check_workbook.py` now runs 1,156 checks.
  - Report: rebuilt on the new export, 186 pages. The helicopter's card, coordinate sheet and pictures regenerated;
    four short forms given their full form (GTAO twice, GPU, PCF); `tools/report/check_numbers.py` accepts the reworded
    sentence on the hand-written functions; British spelling restored in one caption.
  - `README.md` is generated from the project's data (`tools/report/gen_readme.py`), with pictures in `docs/img`
    (`tools/film/figures.py`).
