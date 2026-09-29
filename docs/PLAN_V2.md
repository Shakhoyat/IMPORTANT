# ThermalSense — Plan v2 (visual quality + documentation overhaul)

Revised 2026-09-29 after a full audit of the existing build. `PLAN.md` (v1) is still the
requirements baseline; this file adds the work needed to go from "complete" to "best in class".

## Decisions (user, 2026-09-29)

| Topic | Decision |
|-------|----------|
| Terrain | radius **10 m → 20 m** (playable area), plus a fog-faded backdrop (hills, treeline, far tower) outside it |
| Lighting | GGX/Cook-Torrance PBR; Flat / Gouraud / Phong kept as the per-face / per-vertex / per-fragment evaluation choice; `B` toggles back to classic Blinn-Phong |
| Scope | a week or more: full plan + TAA + extra polish passes |
| Video | yes: 1080p, about 60 s, cinematic camera (ffmpeg) |

Consequences of the 20 m disc: MicroMouse grid, UAV patrol radius, search bounds, test T30 bounds, fence
perimeter, shadow cascade splits and every "10 m" statement in the README, report and workbook are updated.

## 0. Audit — where the project stands today

| Area | State | Verdict |
|------|-------|---------|
| Build (`mingw32-make`) | builds cleanly, static-linked | OK |
| Automated tests | 32 / 32 pass | OK |
| Lab-sheet rules (unit primitives, CCW, 36 slices, hemisphere reflection, z-axis cylinder, EBO, T·R·S) | all implemented and tested | OK |
| Proposal (Flat bunker, Gouraud terrain, Phong UAV, 3 lights, Z-buffer, wireframe, thermal) | all implemented | OK |
| Mid-task changes (10 m disc, MicroMouse/LFR rover, CV drone) | implemented, measured | OK |
| **Visual quality** | dark and muddy, no shadows, no glow, flat black sky, blob trees, sparse compound, props float visually (no contact shadows), thermal view is a single purple wash | **weak — main gap** |
| **Excel workbook** | 8 sheets of plain values; 0 formulas, 0 charts, 0 images, no cover/index, no per-vertex tables, no matrix (M1/M2/M3) sheet the lab sheet explicitly asks for | **functional, not "proper"** |
| Report (5 pages) | correct, but figures inherit the weak visuals | will be regenerated |
| Housekeeping | no git repo; stale starter `main.exe` in root | fix |

## 1. Visual target: AAA open-world night scene

Reference look: the night scenes in modern open-world games (RDR2, Ghost of Tsushima, Horizon).
What makes those images read as "AAA", and our version of each:

| AAA ingredient | ThermalSense implementation |
|----------------|-----------------------------|
| Physically based materials | **GGX / Cook-Torrance BRDF** with roughness + metalness per component. Flat / Gouraud / Phong stay as the *where lighting is evaluated* choice (per face / vertex / fragment), so the proposal's shading models are kept |
| Image-based ambient | procedural sky → hemisphere + 9-coefficient SH ambient, specular ambient from the sky colour |
| Cascaded shadows | **3-cascade moon shadow map** (PCF + soft penumbra) + searchlight and watchtower spot shadows |
| Volumetric atmosphere | ray-marched **height fog lit by the moon and the searchlight, shadowed** → god rays through trees, light shaft of the drone |
| Living vegetation | **~40 000 instanced grass blades** (thin scaled cubes) with wind sway and rover/rotor-wash push, wind-animated trees, falling leaves |
| Weather | light drizzle toggle: rain streaks, **wet ground with screen-space reflections** of the searchlight and beacon, puddles from the noise field |
| Sky | physically inspired night sky: gradient + Milky Way band + twinkling stars + moon with halo + moving clouds lit from behind |
| Distant world | the playable terrain is the **20 m disc**; beyond it, fog-faded hills, a treeline and a far radio tower as a backdrop (primitives), so the compound sits in a world rather than floating in black |
| Camera / lens | HDR + ACES filmic, auto-exposure, bloom, lens flare/glare on bright lights, depth of field + motion blur in cinematic mode, film grain, vignette, procedural colour grading (cool moonlight / warm practicals) |
| Anti-aliasing | 4× MSAA + FXAA; TAA only if time permits |

## 1a. Implementation phases (OpenGL 3.3 core, no external models/textures)

Every object stays built only from cube / sphere / cylinder. All the upgrades below are
*rendering* techniques, which makes them extra report material instead of breaking the brief.

### Phase A — Rendering pipeline (biggest visual gain)
| # | Feature | What it gives |
|---|---------|---------------|
| A1 | **HDR framebuffer** (RGBA16F) + 4× MSAA resolve | lights can exceed 1.0, which enables glow |
| A2 | **Bloom** (bright pass + 5-level dual-filter blur) | searchlight, beacon, LEDs and moon glow for real |
| A3 | **ACES tone mapping + exposure**, gamma-correct lighting (sRGB) | fixes the muddy, crushed-black look |
| A4 | **Shadow maps**: 3-cascade moon (2048², PCF + soft penumbra) + searchlight and watchtower spots (1024²) | objects sit on the ground; the rover *visibly* hides in the bunker's shadow |
| A5 | **Volumetric fog + light shafts**: half-res ray march of height fog lit by moon and spots, using their shadow maps (god rays, drone beam) | light shafts cut off by the bunker/trees (signature shot) |
| A6 | **SSAO** (16 samples, half resolution, blurred) | contact darkening in corners, under props and wheels |
| A7 | Post chain: vignette, chromatic aberration (thermal only), film grain, FXAA fallback | cinematic finish |
| A8 | Quality presets `F5` (Low/High/Ultra) + a pass timing overlay | stays interactive on the Radeon iGPU; timings go to the report |

### Phase B — Materials and sky
| # | Feature |
|---|---------|
| B1 | Procedural shader "textures" (triplanar value noise, no image files): concrete with stains on the bunker, grass/dirt/gravel blend and tyre tracks on the terrain, brushed metal on the UAV, painted camo on the rover |
| B2 | **GGX/Cook-Torrance PBR** (roughness, metalness per component), bump from the noise, SH ambient from the sky |
| B3 | Sky dome: moonlit gradient, twinkling stars, moon disc + halo, thin drifting clouds, and a horizon silhouette ring of distant hills (scaled spheres) |
| B4 | Height fog with light scattering toward the moon |

### Phase C — Scene richness (still primitives only)
| # | Addition | Primitives |
|---|----------|------------|
| C1 | Perimeter fence: posts + 3 wire strands + gate | cylinders, cubes |
| C2 | Watchtower with its own small floodlight | cubes, cylinders, sphere |
| C3 | Sandbag walls (curved rows), camo-net tent, fuel drums, crates, generator | cylinders, cubes, spheres |
| C4 | Pine trees (stacked cylinder frusta, r_top = 0) + round trees, grass tufts via instancing | cylinder, sphere |
| C5 | Radio mast with blinking red aviation lights | cylinders, spheres |
| C7 | **Instanced grass field** (~40k blades from scaled cubes) with wind + push from rover and rotor wash |
| C8 | Distant backdrop outside the 20 m disc: fog-faded hills, treeline, far tower |
| C6 | Instanced rendering for repeated props (`glDrawElementsInstanced`), measured vs. per-object draws | — |

### Phase D — Life and motion
| # | Feature |
|---|---------|
| D1 | UAV: banks and pitches with acceleration, spinning rotors with a translucent motion-blur disc, blinking nav LEDs |
| D2 | Rover: wheels spin with speed, suspension bob, headlight spot cones, dust particles |
| D3 | **Thermal footprints**: the rover leaves a heat trail that cools over time (ping-pong texture on the terrain); the drone's CV sees it too |
| D4 | Rotor-wash dust particles under the UAV when it is low |
| D5 | Thermal view: palette switch `T` (ironbow / white-hot / rainbow), sensor noise, heat shimmer, scanline, reticle |
| D7 | Weather toggle `R`: drizzle, wet ground + screen-space reflections, puddles |
| D6 | **Cinematic director camera** `K`: spline fly-through with automatic cuts (orbit → chase → UAV feed → hide shot) for the demo and the video |

### Phase E — Capture
| # | Deliverable |
|---|-------------|
| E1 | Regenerate all report figures at 1920×1080 on Ultra, plus **before/after** comparisons (v1 vs v2, and each post pass on/off) |
| E2 | `--record` mode writes frames; if `ffmpeg` is present, a 60 s 1080p demo video (`report/demo.mp4`) |

## 2. Excel workbook — "proper" v2 (`tools/build_workbook.py`)

Still generated from program output; no hand-typed numbers. Target: 14 styled sheets with live formulas.

| # | Sheet | New in v2 |
|---|-------|-----------|
| 0 | **Cover & Index** | title, author, roll, date, hyperlinked sheet index, hero screenshot, KPI tiles (parts, triangles, tests passed, FPS) |
| 1 | Primitive Inventory | thumbnail image of each primitive |
| 2 | **Primitive Vertex Data** | full cube table (24 × x,y,z,nx,ny,nz) + its 36 indices; first rows of the sphere/cylinder; Technique 1 vs Technique 2 float counts *as formulas* (lab sheet p1) |
| 3 | Object Construction | Excel Table with banded rows and filters, grouped by object, primitive colour coding, per-object subtotals via `COUNTIFS` |
| 4 | **Transformation Matrices (M1, M2, M3 …)** | for every component: S, R, T entered as values → **T, R, S and M = T·R·S computed by `MMULT` formulas in Excel**, plus the code-exported matrix and a max-error check column (≈ 0), which proves the table and the code agree |
| 5 | Hierarchy | parent → child tree (indented) with world matrices, UAV → gimbal yaw → pitch → lamp highlighted |
| 6 | Geometry Statistics | memory columns as formulas (verts × 24 B, indices × 4 B); **bar chart** indexed vs non-indexed |
| 7 | Lighting | light parameters + attenuation curve table computed by formula + **line chart** |
| 8 | Shading | Flat/Gouraud/Phong comparison with embedded crop screenshots |
| 9 | **Render Pipeline** | every pass (shadow, SSAO, geometry, volumetric, bloom, tonemap) with its measured GPU ms + stacked bar |
| 10 | Testing | pass-rate KPI by formula, conditional formatting, evidence links to screenshots |
| 11 | Performance | FPS per preset / indexed vs non-indexed / instanced vs not, with charts |
| 12 | **Behaviour** | trace-derived detection %, tracking error, hide events + time-series chart |
| 13 | Traceability | requirement ID → implementation file → sheet → test ID → report section |

Workbook-wide: one colour theme matching the primitive colours, frozen headers, auto-fit widths,
print setup (landscape, fit to width, repeat header row), named ranges, and a validation step that
recalculates it headlessly (LibreOffice, if installed) and checks for formula errors.

## 3. Report (`report/main.tex`)

- New section "Advanced Rendering": HDR, bloom, shadow maps (with bias/PCF math), SSAO, volumetric
  beam, tone mapping — each with an on/off figure and its measured cost.
- Replace all figures with v2 captures; add a full-page title render and a before/after strip.
- Transformation section cites the Excel M1/M2/M3 sheet with one worked matrix.
- All numbers re-measured (FPS per preset, pass timings, behaviour stats). Target 7–8 pages.

## 4. Order of work and verification

| Step | Work | Verification gate |
|------|------|-------------------|
| 0 | `git init` + commit current state (safety net for big changes) | clean tree |
| 1 | Phase A (A1–A4 first, then A5–A8) | tests pass; before/after captures look better; FPS ≥ 60 on High |
| 2 | Phase B | captures reviewed |
| 3 | Phase C + D | tests extended (new objects: primitive-only, CCW, part counts); 180 s trace still healthy |
| 4 | New tests: shadow-map matrices, instancing counts, M = T·R·S equals exported matrix | all pass |
| 5 | Workbook v2 | opens in Excel without repair; 0 formula errors; formula matrices match the code |
| 6 | Captures + video (Phase E) | every figure reviewed by eye |
| 7 | Report v2, recompile | numbers match workbook; page count |
| 8 | Code review pass (cpp-reviewer) + final README | no HIGH issues |

## 5. Risks

| Risk | Mitigation |
|------|------------|
| Radeon iGPU FPS drops with shadows + SSAO + volumetrics | quality presets; half-res SSAO/volumetrics; measured and reported honestly |
| Visual additions confuse the drone's CV (new hot objects) | CV uses the raw-heat pass, not the post-processed image; trace re-run after every phase |
| "Only primitives" rule questioned for cones | a frustum is the lab cylinder with r_top ≠ r_bottom (already used for the beam); documented in Excel |
| Excel formulas render as text in some viewers | cached values written alongside formulas, validated by headless recalculation |

## Status (2026-09-29, end of the first v2 session)

| Item | Status |
|------|--------|
| A1-A8 HDR, bloom, ACES, cascaded + cached moon shadows, spot shadows, SSAO, volumetric fog (analytic cone shaft), presets, GPU timers, frustum culling | done, measured (ablation in Excel sheet 9, report Table 2) |
| B1-B4 procedural surfaces, GGX PBR, night sky (stars, Milky Way, clouds, halo), fog | done; realistic terrain (ground data map, ruts, pebble creek, turf, wet specular) added on user request |
| C1-C8 fence, watchtower, sandbags, tent, caches, radio mast, spruces (whorls), broadleaf trees (clumps), boulders, bushes, instanced grass (tufts), backdrop | done (805 parts, 20 m disc) |
| Practical lights (door lamp, lamp posts, fire barrel, lantern, floodlight, aviation lights, UAV strobe) | done |
| D1 rotor blur discs, strobe, banking | done |
| D6 cinematic director camera (`K`), E1 captures, E2 `--record` video | done |
| Chase re-balanced for 20 m (320x240 sensor, search pattern, track coasting, exact rover collision) | done: TRACK 57 %, detection 51 %, hidden 31 % |
| Excel v2 (14 sheets, formula matrices verified = code) and report v2 (7 pages) | done |
| Not done yet | D3 thermal footprints, D5 thermal palette switch, D7 rain / wet SSR, TAA, lens flare, DOF / motion blur, auto-exposure |
