# v10 spec (as given by the user, 2026-10-09)

Decisions taken with the user:
- Finish v9 (M4, M6/M7, M8 merges, M9, the v9 parts of M10) first; this spec is v10. Its audit, features, workbook and report replace the plain M10 report update.
- Keep the v9 key bindings; every spec feature gets a free key; the final table goes in the report's controls section and the KeyMap conflict test.
- The spec's 35-45 page report (report/sections/*.tex) replaces the old report; keep ThermalSense_Graphics_Documentation.xlsx and add report/workbooks/cg_workbook.xlsx beside it.

---

## ROLE AND GOAL

You are working on my Computer Graphics course project at KUET CSE (a 3D military outpost scene: bunkers, soldiers, UAVs, terrain, vegetation, day/night). You have two jobs, in this order:

1. **Finish and instrument the project** so every graphics concept can be shown live and captured as evidence.
2. **Produce a compact, visually dense academic report** that walks the professor through the full graphics pipeline, class concepts first, then advanced work.

The report must impress through **evidence, not word count**. Every claim is backed by a screenshot, a debug view, an equation, a plot, or a workbook snapshot. Prefer one strong figure plus three lines of explanation over a paragraph.

---

## PHASE 0: AUDIT FIRST (do not skip)

Before writing any code or report text:

1. Read the codebase. Identify the language, graphics API, shader version, libraries, build system, and entry point.
2. Produce `docs/audit.md` with:
   - Every object in the scene and which primitives it is built from.
   - Every implemented feature, mapped to the lecture topic it demonstrates.
   - Every feature requested below that is **missing or partial**.
   - Key bindings currently in use (to detect conflicts).
3. **Never describe a feature in the report that is not in the code.** If something is approximated (e.g. "thermal" is simulated from a per-object temperature value, not real sensing), say so plainly.

Show me the audit before Phase 1 if any requested feature looks infeasible in the current stack.

---

## PHASE 1: FEATURES TO IMPLEMENT OR COMPLETE

### 1.1 Two modes

| Key | Mode | Behaviour |
|---|---|---|
| (default) | **Observation mode** | Free roaming for inspecting the scene and its graphics concepts. No enemies. |
| `G` | **Game mode** toggle | Intruders spawn, UAVs patrol and engage, soldiers respond. |

### 1.2 Observation mode camera system

Cycle cameras with number keys and show the active camera name on the HUD:

1. Free-fly first-person camera
2. Third-person follow camera (attach to a soldier or UAV, `Tab` cycles target)
3. UAV-mounted first-person camera
4. Orthographic top view (scene layout)
5. Orthographic front and side views (multiview projection, L5)
6. Isometric view (axonometric projection, L5)
7. Fixed cinematic orbit camera (for hero screenshots)

### 1.3 Debug / teaching views (critical for the report)

Toggle each with a key and list them in an on-screen help panel (`H`):

- Wireframe overlay
- Vertex and face normals as lines
- Depth buffer visualisation (linearised)
- View frustum of a second camera drawn in world space, showing near/far planes
- Clipping demonstration: geometry outside a user-defined window highlighted or cut
- Back-face culling on/off
- Flat vs Gouraud vs Phong shading switch on the same object
- Ambient-only / diffuse-only / specular-only / combined lighting switch
- Local axes gizmo per object (shows local vs world coordinates)

### 1.4 Day and night

- `N` transitions to night, `D` transitions to day. The transition is animated, not instant.
- The **sun travels a real arc** (rises in the east, sets in the west) and the **moon rises as the sun sets**, on the opposite side of the sky.
- Light direction, light colour, ambient level, sky colour, and fog colour all derive from the sun/moon elevation angle.
- **Key conflict:** if `D` is already strafe-right in WASD movement, move free-camera translation to arrow keys plus `Q`/`E` (down/up) and keep `N`/`D` for time of day as I requested. Record the final mapping in the report's controls table.
- Show an in-game clock on the HUD. Define a time scale (e.g. one in-game day = 8 real minutes) and state it in the report.

### 1.5 Game mode

- Intruders spawn at the perimeter and move toward the base.
- UAVs patrol defined routes. Each UAV has:
  - its own **spotlight torch** (spot light with cone angle and attenuation, L8)
  - its own **picture-in-picture screen** that switches between normal view and **thermal view** (see 1.6).
- **Single-intruder encounter per UAV:** when any UAV detects one intruder in its torch cone or thermal view, it locks on and fires. The nearest soldiers automatically provide backup fire at the same target. Make each UAV able to trigger this case independently.
- **Soldier duty cycle:** soldiers rotate shifts every 6 in-game hours (walk out, swap posts, walk back). Shift changes must be visible and logged on the HUD.
- Include hit detection (state the method: ray vs bounding sphere/box) and simple projectile or tracer visuals.

### 1.6 Live thermal / vision pipeline (advanced marks)

Build an interactive panel (key `V`) that shows the thermal image being built **stage by stage, frame by frame**, with sliders I can change live:

1. Rendered frame or per-object temperature buffer
2. Grayscale conversion
3. Gamma transform (slider: gamma)
4. Gaussian convolution (sliders: kernel size, sigma; show the kernel values)
5. Sobel X, Sobel Y, gradient magnitude (toggle) or Laplacian
6. Threshold (slider)
7. False-colour LUT (ironbow / white-hot / black-hot)
8. Histogram and CDF of the current stage, plus a histogram-equalisation toggle

Each stage appears as a small tile so the whole chain is visible at once. Implement it as GPU shader passes if possible; if done on CPU, run it at reduced resolution and say so.

Optional if time allows: a "recon image restoration" tool that adds salt-and-pepper or periodic noise to a captured frame and restores it with median filtering or a notch filter in the DFT domain, showing original, corrupted, spectrum, and restored images side by side.

### 1.7 Line-follower / micromouse logic

If UAV patrol or soldier movement uses path following, implement it in a way that maps to LFR / micromouse ideas (waypoint following with a PID-style correction, or grid flood-fill pathfinding). Expose a debug view that draws the path, the sensor/look-ahead points, and the chosen direction.

### 1.8 Performance instrumentation

- Add an FPS and frame-time counter (ms), plus draw-call and triangle counts.
- Add a benchmark mode (`F9`) that flies a fixed camera path for 30 s per configuration and writes CSV: `config, avg_ms, p95_ms, avg_fps, draw_calls, triangles`.
- Configurations: day vs night, observation vs game mode, thermal panel on/off, shadows on/off, instancing on/off (if applicable), culling on/off.

### 1.9 Capture harness

Add a scripted capture mode that saves screenshots for every camera, every debug view, day/sunset/night, and each game-mode event (detection, lock-on, backup fire, shift change) into `report/figures/raw/` with consistent resolution and naming. The report must use these real captures.

---

## PHASE 2: EVIDENCE GENERATION

### 2.1 Excel workbooks (academic, formula-driven, screenshot-ready)

Create `report/workbooks/cg_workbook.xlsx` using openpyxl with **live formulas** (not pasted values), clear headers, units, consistent number formats (4 decimals), bordered tables, highlighted input cells, and one sheet per topic:

| Sheet | Contents |
|---|---|
| `Primitives` | Vertex/face/triangle counts and generation parameters for each primitive (cube, cuboid, cylinder, sphere, hemisphere, cone, frustum, torus, wedge/prism, grass blade, helix). Include parametric equations. |
| `Decomposition` | For each object: part name, primitive, dimensions, local T/R/S, parent, colour (RGB), material (ka, kd, ks, shininess). Object-level primitive totals. |
| `Transform_Trace` | One real vertex from one real object traced through S, R, T, parent matrix, model, view, projection, perspective divide, NDC, viewport. Every matrix shown as a 4x4 grid. |
| `View_Projection` | Eye, look-at, up; u, v, n basis via cross products; view matrix; perspective matrix from fov, aspect, near, far; depth mapping table showing nonlinear z precision. |
| `Clipping` | Cohen-Sutherland outcodes for sample lines; Liang-Barsky t-values; Sutherland-Hodgman polygon steps. |
| `Illumination` | Ambient, diffuse, specular terms for one surface point at noon, sunset, night (moon) and under a UAV torch. |
| `Color_Interp` | Gouraud vs Phong at sample points across one triangle; barycentric weights; affine vs perspective-correct interpolation; sRGB vs linear-space interpolation; sky colour interpolation over the day cycle. |
| `Ray_Tracing` | Ray-sphere and ray-plane intersection worked examples, reflection vector, Snell refraction, recursion depth contributions. |
| `Thermal_Kernels` | Gaussian kernel for chosen sigma, Sobel kernels, a 5x5 image patch convolved step by step. |
| `Duty_Cycle` | 24-hour shift schedule per soldier. |
| `Performance` | Imported benchmark CSV with charts. |

Render each sheet to a clean PNG (e.g. LibreOffice headless to PDF, then crop) into `report/figures/workbook/`. These are the "workbook snaps".

### 2.2 Plots (matplotlib, consistent style, vector PDF output)

- Sun and moon elevation vs in-game time; light intensity and colour temperature vs time
- Perspective depth nonlinearity (z_ndc vs z_eye) for two near-plane values (explains z-fighting)
- Specular falloff for several shininess values
- Gouraud vs Phong intensity profile across a triangle (shows missed highlights)
- Gaussian kernel surface plot; histogram before/after equalisation; CDF
- Performance bar chart across configurations
- Duty-cycle Gantt chart

### 2.3 Diagrams

- Module architecture diagram
- Top-down scene layout with labelled zones and coordinate axes
- Scene graph (hierarchy tree) for at least one articulated object (soldier or UAV)
- Full graphics pipeline diagram used as a "you are here" marker at the start of each pipeline section
- State diagrams: intruder, UAV (patrol, detect, lock-on, engage), soldier (on duty, backup, shift change)

---

## PHASE 3: THE REPORT

### 3.1 Format

- LaTeX, Times font (`\usepackage{newtxtext,newtxmath}`), A4, 11pt, compiles cleanly in Overleaf.
- Numbered figures, tables, and equations; every figure cited in text; captions say **what to notice**, not just what it is.
- Target **35 to 45 pages** including figures. Text is short; visuals carry the report.
- Output: `report/main.tex`, `report/sections/*.tex`, `report/figures/`, `report/refs.bib`.

### 3.2 Self-contained topic template (use for every topic)

Each topic fits on roughly one page and can be read alone:

1. **Concept** (2 to 3 lines, what and why)
2. **Equation(s)** (numbered)
3. **In-project visual** (real screenshot, with debug view where relevant)
4. **Workbook snap or plot** (the numbers behind the visual)
5. **Where it lives in the project** (file/function name, one line)
6. **Critique** (limitation, artifact, or tradeoff, with evidence)

### 3.3 Structure

**Front matter**
1. Cover: university, department, course, title, my details, hero screenshot
2. Table of contents
3. Introduction and objectives (half a page)
4. The scene in pictures: hero shots, day/sunset/night, observation vs game mode
5. Tools, hardware, and test setup
6. Architecture and scene layout
7. Controls table

**Part A: Class concepts, following the graphics pipeline in order**

8. Primitives and geometry (all 11 primitives, parametric forms, triangle counts)
9. Modelling transformations at component level (T, R, S, homogeneous coordinates, composite order, pivot rotation, reflection/shear if used)
10. Hierarchical modelling and scene graph (articulated UAV rotors or soldier limbs)
11. 3D viewing transformation (eye, look-at, up, u/v/n basis)
12. Projection (perspective vs orthographic vs isometric, with the same scene in each)
13. Projective transformation (frustum, NDC, perspective divide, depth mapping, z-fighting)
14. Clipping (frustum clipping, Cohen-Sutherland, Liang-Barsky, Sutherland-Hodgman worked examples)
15. Hidden surface removal (back-face culling and Z-buffer with depth visualisation)
16. Rasterisation and anti-aliasing (with and without, if supported)
17. Illumination model (ambient/diffuse/specular split views; directional sun, moon, point and spot lights)
18. Shading (flat vs Gouraud vs Phong on the same object; Mach bands)
19. Texture mapping
20. Ray tracing concepts used (ray picking, shadow rays, reflection, refraction where present)

**Part B: Advanced work** (each section states which class concepts it builds on)

21. **Object decomposition catalogue**: for every major object, the exploded parts view, the assembled view, and the parts table (primitive, dimensions, transform, colour, material)
22. **Colour interpolation with critique**: Gouraud vs Phong, affine vs perspective-correct, sRGB vs linear blending, sky gradients over time. Show where naive interpolation fails, with screenshots and the `Color_Interp` workbook
23. **Day/night system**: sun and moon arcs, derived lighting parameters, plots of light vs time
24. **Curved objects**: torus, helix, sphere tessellation level vs quality vs triangle count
25. **Dynamic and controllable objects**: UAV flight, soldier movement, shift rotation, the camera system
26. **Reflection and advanced ray tracing**: what is real-time approximated vs truly traced, and the cost of each
27. **Live thermal pipeline**: the stage-by-stage tile strip at two or three slider settings, kernels, histograms, separable Gaussian cost argument
28. **Game-mode AI**: state diagrams, single-intruder encounter sequence (screenshot strip: patrol, detect, lock-on, backup fire, neutralised), duty-cycle Gantt chart
29. **Line-follower / micromouse logic**: path debug view, control law or flood-fill explanation
30. **Hard problems and tradeoffs** (the section that earns the most credit). For each: problem, why the class method alone falls short, my solution, measured result. Candidates:
    - z-fighting in a large scene (near-plane choice, depth precision plot)
    - many UAV torches vs light-count limits
    - thousands of grass blades (instancing vs naive draw calls, measured)
    - thermal convolution cost (separable 2 x 1D vs 2D kernel, O(k) vs O(k^2), measured)
    - shadow acne and peter-panning (bias tradeoff)
    - transparency sorting
    Only include problems actually present in the code.

**Back matter**

31. Performance evaluation: setup, table, bar chart, short analysis
32. Limitations and future work (honest)
33. Conclusion: each objective mapped to the section that proves it
34. References (OpenGL/API docs, Hearn & Baker or Foley et al., Gonzalez & Woods for image processing, any papers used)

### 3.4 Writing rules

- Short sentences, plain academic English, no filler or hype words.
- No em-dashes.
- Every number in the report comes from the code, the workbook, or the benchmark CSV. **Never invent measurements.** If something was not measured, leave a clearly marked TODO.
- Use "differences" tables wherever two methods are compared (method, visual result, cost, when to use).
- Show code only as short excerpts (under 15 lines) when the code itself teaches something.

---

## PHASE 4: QUALITY CHECK BEFORE HANDING BACK

Go through this list and report pass/fail for each:

- [ ] Every lecture topic L1 to L10 plus hidden surface removal has a self-contained section with a visual
- [ ] Every object has a decomposition figure and parts table
- [ ] At least one vertex is traced end to end through the pipeline with real numbers
- [ ] Every comparison has side-by-side screenshots from the same camera position
- [ ] All workbook snaps are readable at print size
- [ ] All performance numbers trace back to the CSV
- [ ] No feature is described that the audit did not confirm
- [ ] The report compiles in Overleaf without errors
- [ ] Page count is within 35 to 45

Work phase by phase. After each phase, give me a short summary and list anything you could not do.
