# ThermalSense: Autonomous UAV Patrol System

Md. Shakhoyat Rahman Shujon, Roll 2107104. Computer Graphics Lab project (OpenGL 3.3 core, GLFW, GLAD).

A night-time patrol of a 20 m radius compound. A quadcopter finds a small ground robot **from its own
thermal camera images** (blob detection on a 320x240 heat image) and follows it with a searchlight. The
robot uses **MicroMouse flood-fill planning + line-follower control** to keep an obstacle between itself
and the UAV. Every object (805 parts: bunker, UAV, rover, watchtower, fence, forest, boulders, grass...)
is built only from the lab's unit primitives (cube, sphere, cylinder; the cone and the light frustum are
the cylinder generator with a different top radius). No model files and no image textures are used.

The renderer aims at a modern, realistic look: GGX/Cook-Torrance lighting (flat / Gouraud / Phong still
decide *where* it is evaluated), cascaded moon shadows + searchlight shadows, SSAO, volumetric fog with
light shafts, practical lights (lamps, fire, floodlight), procedural ground materials, instanced grass,
HDR bloom and ACES tone mapping.

## Build and run (Windows, MinGW-w64 g++)

| Way | Command |
|-----|---------|
| One click | `run.bat` (builds `ThermalSense.exe` if missing, then starts it) |
| Build only | `build.bat` |
| Make (Git Bash / MSYS, incremental, also builds the tests) | `mingw32-make -j8` |
| CMake | `cmake -G "MinGW Makefiles" -B build-cmake && cmake --build build-cmake` |

The executable links GLFW and the C++ runtime statically, so it runs on any Windows PC with an
OpenGL 3.3 driver.

### Command-line modes

```
ThermalSense.exe                                  interactive simulation (F1 = controls)
ThermalSense.exe --capture report/figures [--only a,b]   render the report figures (Ultra, off-screen)
ThermalSense.exe --benchmark build/benchmark.json   FPS per preset + per-feature ablation + GPU pass times
ThermalSense.exe --trace build/trace.csv 180        log UAV / rover behaviour (4 Hz) for 180 s
ThermalSense.exe --record report/demo.mp4 60 [--ffmpeg PATH]   1080p cinematic video (needs ffmpeg)
build/export_docs.exe build/scene_export.json       GPU-free export of every part, matrix and mesh
build/tests.exe build/test_results.json             46 automated tests (no GPU needed)
python tools/build_workbook.py                      rebuild ThermalSense_Graphics_Documentation.xlsx
python tools/verify_workbook.py                     recalculate every formula and check it
```

`mingw32-make docs` runs the export, the tests and the workbook script in one go.

## Controls (F1 in the window shows the same list)

| Key | Action |
|-----|--------|
| Mouse drag / wheel | orbit and zoom (look around in free camera) |
| `C` / `K` | camera: orbit, free, chase, UAV camera / cinematic director |
| `W A S D`, `PgUp PgDn` | move the free camera |
| `V` | view: visible, primitive diagnostic, Z-buffer, thermal |
| `X` | edges: solid, hidden-line wireframe, x-ray wireframe |
| `Z` | depth test on/off (shows why the Z-buffer is needed) |
| `O` `F` `G` `P` | shading: per-object, all flat, all Gouraud, all Phong |
| `1` `2` `3` `4` | toggle moonlight, beacon, searchlight, practical lights |
| `F5` | quality preset LOW / HIGH / ULTRA |
| `B` / `N` | PBR vs classic Blinn-Phong / v1 renderer (no post-processing) |
| `+` `-` | exposure |
| `E` | explode composite objects into their primitives |
| `H` | local axes of the hierarchy (UAV, gimbal yaw, gimbal pitch, rover) |
| `M` | MicroMouse flood field and the line-follower guide line |
| `I` | indexed / non-indexed drawing |
| `U`, `Tab`, `Space`, `F12`, `Esc` | UAV feed, HUD, pause, screenshot, quit |

## Measured on the test machine (AMD Radeon integrated GPU, 1280x720)

| Preset | FPS | ms / frame |
|--------|-----|-----------|
| v1 renderer (no post) | 37.9 | 26.4 |
| LOW | 20.1 | 49.6 |
| HIGH (default) | 15.7 | 63.8 |
| ULTRA (captures, video) | 5.5 | 183.0 |

Per-feature cost on HIGH (frame time with the feature minus without it): shadow maps 15.3 ms, MSAA
7.4 ms, volumetric fog 3.9 ms, bloom 2.7 ms, SSAO 2.6 ms, searchlight shadow 2.2 ms. On this
integrated GPU LOW is the smoothest interactive setting; a discrete GPU runs HIGH comfortably.

Behaviour over 180 s: UAV in TRACK 57 %, REACQUIRE 28 %, SEARCH 14 %; rover detected in 51 % of the
samples and hidden in 31 %; median tracking error 0.45 m (p90 0.67 m).

## How objects are built

`createCube()`, `createSphere(36, 18)` and `createCylinder(36, rBottom, rTop, capped)` (in `src/primitives/`)
make unit meshes centred at the origin (extent +-0.5, CCW winding, 36 slices = 10 degrees). Each mesh is
uploaded once (one VAO + VBO + EBO) and reused by all parts. A part is a scene-graph leaf with its own
`M = T * Ry * Rx * Rz * S`; its world matrix is `parent_world * M`. The Excel sheet *Transformation
Matrices* rebuilds every M with formulas and checks it against the code (max difference 1.7e-5).

| Object | Parts | Shading | Built from |
|--------|------:|---------|------------|
| Command bunker | 29 | Flat | cubes (slab, block, roof, door, slits, vents), cylinders (turret, sandbags, mast), spheres (dome, beacon, door lamp) |
| UAV | 39 | Phong | cubes (fuselage, arms, blades), spheres (canopy, hubs, LEDs, strobe), cylinders (motors, legs, lamp, rotor-blur discs) |
| Rover | 26 | Phong | cubes (chassis, hull, motor box), cylinders (6 wheels, mast), spheres (hubs, IR sensors, scanner) |
| Environment | 621 | Gouraud | fence, watchtower, tent, caches, radio mast, lamp posts, fire barrel, spruces (cone whorls), broadleaf trees (sphere clumps), boulders, bushes |
| Backdrop | 89 | Gouraud | far ground, distant hills, tree line, far tower |
| Terrain | 1 | Gouraud | cylinder cap subdivided into 72 rings x 256 slices, displaced by h(x,z) |
| Grass | 35 000 (HIGH) | Gouraud | instanced unit cubes (`glDrawElementsInstanced`) |

## Deliverables

| File | Content |
|------|---------|
| `src/` | source code (primitives, objects, rendering, scene, ai, vision, app) |
| `tests/test_main.cpp` | 46 automated tests |
| `ThermalSense_Graphics_Documentation.xlsx` | 14-sheet workbook: cover/KPIs, primitives, vertex data, object construction, transformation matrices (formulas), hierarchy, geometry, lighting, shading, render pipeline, testing, performance, behaviour, traceability |
| `report/main.tex`, `report/main.pdf` | 7-page report |
| `report/figures/` | screenshots captured from the program |
| `report/demo.mp4` | 60 s cinematic demo (1080p) |
| `docs/PLAN.md`, `docs/PLAN_V2.md` | requirement checklist and the v2 plan |
| `docs/visual_tests.json` | observations of the captured screenshots (used by the workbook) |
