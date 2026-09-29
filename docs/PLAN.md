# ThermalSense — Requirements, Mapping and Documentation Plan

This file records the decisions taken *before* implementation. It is derived from
the two reference documents in the project root:

* `graphics-lab-04.pdf` — handwritten Graphics Lab 04 sheet (3 pages)
* `ThermalSense Project Proposal (1).pdf` — 1-page proposal

## 1. What the handwritten assignment actually says

| Page | Note on the sheet | Consequence for the implementation |
|------|-------------------|------------------------------------|
| 1 | Canonical view volume, x,y,z in [-1,1]; unit square a,b,c,d at (±0.5, ±0.5) | Every primitive is a **unit primitive centred at the origin** (extent ±0.5) |
| 1 | "+ve → anticlockwise grouping → front facing, −ve → clockwise → back facing" | All triangles are wound **counter-clockwise seen from outside**; back-face culling is enabled (`GL_CCW`, `GL_BACK`) |
| 1 | Technique 1: `abc`,`cdb` = 18 floats. Technique 2 (optimisation): 4 unique points × 3 = 12f + 6i | **Indexed rendering** with unique vertices plus an index buffer, and the saving is measured and documented |
| 2 | `(x,y,z,r,g,b)` = 6f per point; VBO→vertex, EBO→index, VAO→configuration (location, offset, stride) | Interleaved VBO, `glVertexAttribPointer(location, offset, stride)`, with one VAO+VBO+EBO per primitive |
| 2 | Gouraud → `x,y,z,r,g,b`; Phong → `x,y,z,nx,ny,nz` | Vertex layout `position + normal` (stride 6 floats). Gouraud lights per vertex and Phong lights per fragment |
| 2 | "polygon (VAO,VBO,EBO) → best; object → fewer but may drop quality" | One shared GPU mesh per primitive, **reused by every component** |
| 3 | HW: cube, sphere, cylinder, **centre at origin** | `createCube()`, `createSphere()`, `createCylinder()` |
| 3 | Circle from triangles, d = 36, 360°/36 = 10° | Angular step Δφ = 2π/36 (10°) for sphere and cylinder |
| 3 | Sphere: build upper half, **reflect** to get lower half | The sphere generator builds the northern hemisphere and mirrors it (y→−y, winding swapped) |
| 3 | Cylinder: circle, "z axis borabor expand" | Cylinder axis is the **local z axis**, z ∈ [−0.5, 0.5] |
| 3 | Table M1, M2, M3 with sx, sy, sz, ty ("table maintain kora lagbe") | A per-component **S / R / T table** in Excel (Sheet 2), exported from code |
| 3 | `T × R × S` composition | Model matrix M = T · R · S; hierarchy World = Parent · Local |
| 3 | "Assignment: All → only object; Excel + explanation lagbe" | Objects are built only from primitives, with an Excel workbook and an explanation (LaTeX report) |

## 2. Requirements checklist (traceable)

| ID | Requirement | Source | Where satisfied |
|----|-------------|--------|-----------------|
| R01 | Cube, sphere, cylinder generators, unit size, centred at origin | Lab sheet p3 | `src/primitives/*` + unit tests |
| R02 | Unique vertices + index buffer (EBO) | Lab sheet p1–2 | `MeshData`, `Mesh` |
| R03 | Interleaved VBO with VAO attribute configuration | Lab sheet p2 | `Mesh::upload` |
| R04 | CCW front faces, back-face culling | Lab sheet p1 | primitives, `Renderer` |
| R05 | Normals for lighting | Lab sheet p2 | primitives |
| R06 | 36 angular slices (10°) | Lab sheet p3 | `kDefaultSlices = 36` |
| R07 | Sphere via hemisphere reflection | Lab sheet p3 | `createSphere` |
| R08 | Objects = transformed primitives; S/R/T table | Lab sheet p3 | `src/objects/*`, Excel Sheet 2 |
| R09 | Command bunker, Flat shading | Proposal §2 | `Bunker.cpp`, geometry-shader flat pipeline |
| R10 | Terrain, Gouraud shading | Proposal §2 | `Terrain.cpp`, per-vertex lighting |
| R11 | UAV, Phong shading | Proposal §2 | `Uav.cpp`, per-fragment lighting |
| R12 | Rover, moving ground object | Proposal §2–3 | `Rover.cpp`, path animation |
| R13 | Directional lunar light + point beacon + UAV spotlight simultaneously | Proposal §2 | `Lighting.*` |
| R14 | Translation / rotation / scaling | Proposal §3 | `Transform`, animation |
| R15 | UAV flight animation | Proposal §3 | `Scene::update` |
| R16 | Rover movement on terrain | Proposal §3 | `Scene::update` |
| R17 | Hierarchical searchlight (UAV → gimbal yaw → pitch head → lamp) | Proposal §3 | `Uav.cpp` rig |
| R18 | Z-buffer hidden-surface removal (rover behind bunker) | Proposal §3 | `GL_DEPTH_TEST`, toggle `Z` |
| R19 | Wireframe / edge rendering: hidden-line vs overlapping (x-ray) edges | Proposal §3 | Edge modes `X` |
| R20 | Thermal-camera heat colour map | Proposal §1 | Thermal palette in shaders |
| R21 | Interactive camera + diagnostic toggles | Proposal §3 | `Camera`, `App` |
| R22 | Primitive diagnostic mode (cube / sphere / cylinder colours, explode, legend) | Brief | View mode `V`, `E` |
| R23 | Excel workbook with 8 sheets, generated from code and measurements | Brief | `tools/build_workbook.py` |
| R24 | LaTeX report of about 4 pages, matching the implementation | Brief | `report/main.tex` |
| R25 | No external 3D models | Brief | Only procedural meshes |
| R26 | Real measurements only (FPS, draw calls) | Brief | `--benchmark` mode |

## 3. Primitive → component → object mapping

| Object | Shading | Primitives used (component examples) |
|--------|---------|--------------------------------------|
| Command Bunker | Flat | Cube: slab, main block, roof, vestibule, door, firing slits, vents · Cylinder: turret, sandbags, antenna mast, beacon housing, fan caps · Sphere: turret dome, beacon bulb |
| Terrain | Gouraud | Indexed height-field grid (a subdivided version of the cube's top face; documented as the one non-rigid surface) |
| UAV | Phong | Cube: fuselage, battery, arms, rotor blades, gimbal yoke · Sphere: canopy, nose sensor, rotor hubs, nav LEDs, lens · Cylinder: motors, landing struts, skids, lamp housing |
| Searchlight | Phong + additive beam | Cylinder (housing), Sphere (lens), Cylinder frustum (r_top ≠ r_bottom) for the light cone |
| Rover | Phong | Cube: chassis, hull, cabin, engine, radar panel · Cylinder: 6 wheels, exhausts, mast, antenna · Sphere: hubs, sensor head, headlights |
| Environment | Gouraud / emissive | Trees = cylinder trunk + 2 spheres, rocks = scaled spheres, moon = sphere |

## 4. Excel workbook schema (`ThermalSense_Graphics_Documentation.xlsx`)

The workbook is **generated** by `tools/build_workbook.py` from files that the program and tests write:

* `build/scene_export.json`: `ThermalSense.exe --export-docs` (primitive stats, every component's S/R/T, lights, shading)
* `build/test_results.json`: `tests.exe` (automated test outcomes)
* `build/benchmark.json`: `ThermalSense.exe --benchmark` (measured FPS, draw calls)
* `docs/visual_tests.json`: observations recorded by inspecting real captures

Sheets: 1 Primitive Inventory · 2 Object Construction · 3 Geometry Statistics · 4 Transformations ·
5 Lighting · 6 Shading · 7 Testing · 8 Performance (the columns follow the brief).

## 5. LaTeX report structure (`report/main.tex`)

1 Introduction · 2 Primitive Geometry Generation (2.1 Cube, 2.2 Sphere, 2.3 Cylinder) · 3 Indexed
Rendering and Optimisation · 4 Object Construction · 5 Transformations · 6 Shading and Lighting ·
7 Depth Testing and Rendering · 8 Results · 9 Conclusion.
Figures: F1 primitive library (real render), F2 pipeline (TikZ), F3 UAV construction (real exploded
render), F4 transformation pipeline (TikZ), F5 final scene, F6 diagnostic/wireframe.
