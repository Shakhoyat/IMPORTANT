# v10 Phase 0 audit (2026-10-09, main at v9 M9)

Scope: what the code does today, mapped to the v10 spec (`docs/v10/spec.md`). Status: **YES** = in the code and
capturable, **PARTIAL** = exists in another form (stated), **NO** = not in the code. The report describes only YES /
PARTIAL items, in the form stated here.

## 1. Stack
| | |
|---|---|
| Language | C++17 (g++ on Linux / WSL; zig clang cross-build for `ThermalSense.exe`) |
| Graphics API | OpenGL 3.3 core (4.x paths where available), GLAD loader, GLFW window; headless EGL (WSL: Mesa d3d12 on the RTX 4070) |
| Shaders | GLSL 330 core, sources embedded in `src/rendering/Shader*.cpp` |
| Libraries | GLFW, GLAD (vendored); no GLM: own `math/Math3D` (column-major `Mat4`); ffmpeg for video |
| Build | `Makefile` (APP = `src/*/*.cpp`, tests = CPU-only core), `CMakeLists.txt`, `tools/build_windows.sh` |
| Entry point | `src/main.cpp` -> `ts::App` (`src/app/App.cpp`): interactive, `--capture`, `--record`, `--benchmark`, `--bench-shots`, `--export-docs`, `--replay` |
| Tests | 339 CPU tests (`tests/*.cpp`, TestKit), all pass |

## 2. Objects and the primitives they are built from
Every object is a scene-graph hierarchy of instanced primitives (`objects/Builder.cpp`); `M = T * Ry * Rx * Rz * S`,
`world = parent_world * M`. Numbers from `ts --export-docs` (`tools/v10/audit_objects.py`).

| Object | Parts | Groups | Animated nodes | Primitives used |
|---|---:|---:|---:|---|
| Terrain | 1 | 1 | 0 | Terrain disc 1 |
| Backdrop | 120 | 47 | 3 | Cone 86, Cylinder 10, Sphere 24 |
| Environment | 652 | 89 | 2 | Cone 168, Cube 46, Cylinder 237, Sphere 201 |
| Command Bunker | 400 | 17 | 17 | Cube 291, Cylinder 90, Sphere 19 |
| Autonomous UAV | 158 | 34 | 26 | Cone 8, Cube 68, Cylinder 54, Cylinder (frustum) 2, Sphere 26 |
| Base | 1253 | 33 | 0 | Cube 558, Cylinder 654, Sphere 41 |
| Barracks hall | 252 | 4 | 0 | Cube 209, Cylinder 41, Sphere 2 |
| UAV pads | 18 | 3 | 0 | Cube 4, Cylinder 6, Sphere 8 |
| Target Rover | 156 | 12 | 48 | Cube 36, Cylinder 48, Sphere 72 |
| Main Battle Tank (x3) | 255 | 29 | 118 | Cube 170, Cylinder 80, Sphere 5 |
| Scout Helicopter | 91 | 19 | 17 | Cone 1, Cube 40, Cylinder 28, Cylinder (frustum) 1, Sphere 21 |
| AA Gatling Gun | 78 | 5 | 26 | Cube 19, Cylinder 54, Sphere 5 |
| Air-defence radar | 108 | 4 | 4 | Cone 1, Cube 62, Cylinder 42, Sphere 3 |
| SAM launcher | 112 | 9 | 10 | Cone 1, Cube 74, Cylinder 32, Sphere 5 |
| MLRS rocket truck | 136 | 22 | 28 | Cone 6, Cube 45, Cylinder 76, Sphere 9 |
| Ammunition bunker | 43 | 2 | 0 | Cube 19, Cylinder 14, Sphere 10 |
| Camp camouflage | 658 | 21 | 0 | Cone 24, Cube 277, Cylinder 309, Sphere 48 |
| Boom barrier | 20 | 2 | 2 | Cube 17, Cylinder 1, Sphere 2 |
| Windsock | 15 | 7 | 6 | Cube 1, Cylinder 14 |
| Flag | 47 | 14 | 13 | Cube 40, Cylinder 5, Sphere 2 |
| Soldier (x8) | 75 | 20 | 15 | Cube 16, Cylinder 20, Sphere 39 |
| Sky Clock | 1 | 1 | 0 | Sphere 1 |

| Primitive | Generator | Vertices | Triangles | Instances in scene |
|---|---|---:|---:|---:|
| Cube | `createCube()` | 24 | 12 | 2444 |
| Sphere | `createSphere(36, 18)` | 614 | 1224 | 826 |
| Cylinder | `createCylinder(36)` | 146 | 144 | 2115 |
| Cylinder (frustum) | `createCylinder(36, 0.03, 0.5, false)` | 72 | 72 | 3 |
| Terrain disc | `createTerrainDisc(130, 384, 36)` | 49921 | 97440 | 1 |
| Cone | `createCylinder(36, 0.5, 0, true)` | 109 | 72 | 295 |

Scene leaves (drawn parts): 5684. Matrix convention: local_matrix and world_matrix are 16 floats ROW-MAJOR (row 0 first); M = T * Ry * Rx * Rz * S (R_deg = pitch X, yaw Y, roll Z); world = parent_world * M; the code stores Mat4 column-major (m[col*4+row]); world matrices are at t = 0

Not built from the primitive library: grass blades (instanced 5-triangle curved strips, `rendering/Grass.cpp`),
foliage spray cards (`rendering/Foliage.cpp`), particles, sky dome, HUD. The v9 M6 branch (pending merge) adds CC0
glTF vegetation assets as the EPIC look; the primitive pines remain the LOW/HIGH fallback.

**Spec primitive list vs code.** The code has 4 generators (`createCube`, `createSphere`, `createCylinder` with
top/bottom radii, `createTerrainDisc`). Cube, cuboid (scaled cube), cylinder, sphere, cone and frustum (cylinder with
two radii) are real. **Not present as primitives:** hemisphere (only as scaled / clipped spheres in places), torus,
wedge/prism, helix. Grass blade exists as its own instanced mesh. The report lists 7 real primitives and states that
torus / wedge / helix are absent rather than inventing them.

## 3. Implemented features mapped to lecture topics
| Topic | Feature in the code | Where | Live key / capture |
|---|---|---|---|
| Primitives, meshes | indexed vs non-indexed meshes, vertex / index counts | `primitives/*`, `MeshData` | `I` indexed toggle, `G` explode to primitives, `F` primitive diagnostic view |
| Modelling transforms | TRS per part, homogeneous 4x4, pivot rotations (turrets, rotors, gimbal, doors) | `objects/Builder`, `math/Math3D` | `G` explode, exported matrices |
| Hierarchical modelling | scene graph (parent world x local), articulated UAV / tank / soldier rig | `scene/SceneGraph`, `objects/*` | `H` local axes of the hierarchy |
| Viewing transform | `lookAt` (u, v, n), orbit / free / chase / UAV / first- and third-person cameras | `math/Math3D lookAt`, `scene/CameraRig` | `C` camera cycle, `V` view, `Home` free fly |
| Projection | perspective (all views); orthographic only for shadow maps | `perspective`, `ortho`, `ShadowMath` | **no orthographic / isometric viewing camera** |
| Clipping | GPU frustum clipping; CPU frustum culling of parts / grass cells | `math/Frustum`, `Renderer` | Frame Explorer bird's-eye frustum stage |
| Hidden surface removal | Z-buffer, back-face culling, Z-buffer visualisation | `RendererFrame`, `F` Z-buffer view | `Z` depth test on/off, `F` Z-buffer view, Frame Explorer back-face stage |
| Rasterisation / AA | MSAA, TAA, barycentric + affine vs perspective-correct demo | `RendererFrame`, Frame Explorer | `F6` TAA, Frame Explorer stages |
| Illumination | Blinn-Phong and PBR; directional sun + moon, point (beacon, practicals), spot (UAV searchlights) with cone + attenuation; ambient hemisphere | `ShaderSources kLighting`, `scene/DayCycle` | `B` PBR/Blinn-Phong, `1`-`4` lights |
| Shading | per-object, flat, Gouraud, Phong on the same scene | shader switch | `O` shading cycle; lab figure `lab_shading` |
| Texture mapping | procedural textures (camo, relief / bump), thermal LUT textures; glTF textures on the M6 branch | `ShaderSources`, `CaptureLab relief` | lab `relief_compare` |
| Ray tracing | ray picking, shadow rays, Whitted reflection / refraction, path tracing, light tracing, BVH | `raytrace/*` | `;` render mode, `'` ray debugger |
| Shadows | cascaded shadow maps, PCF (PCSS + contact shadows on the M8 branch) | `ShadowMath`, `RendererFrame` | lab buffers |
| Post / image processing | SSAO, fog, SSR, bloom, tone mapping, eye adaptation | `ShadersPost*` | `F5`-`F10` |
| Thermal sensing | simulated: per-part heat value -> sensor model (blur, noise, NUC, AGC) -> palette; **not real sensing** | `vision/SensorModel`, `ThermalPalette` | `F` thermal view, `T` palette (4 LUTs), `Y` gimbal view, `U` UAV feed |
| Computer vision | threshold, despeckle, connected components, tracking, triangulation on the UAV image | `vision/CvPipeline`, `ai/Squad` | lab `cv_*` figures |
| Image processing lab | gamma, convolution kernels (Gaussian, Sobel, Laplacian...), sampling / aliasing, histogram | `math/ExplorerMath`, Frame Explorer `P` | Frame Explorer lab panel |
| Day / night | solar-position model (latitude 22.8 deg), sun arc east to west, moon at night, light / sky / fog derived from elevation, auto or scrubbed clock | `scene/DayCycle` | `F2` auto/manual, `,` `.` scrub, `/` next phase, menu dawn/noon/dusk/midnight |
| Game AI | rover intruder waves (night; day with day ops), UAV squad: detect, track, lock, engage; soldiers' duty | `ai/*`, `scene/Garrison`, `scene/Mission` | `F11` game HUD |
| Path planning | A* on a 72x72 grid with clearance (soldiers), MicroMouse flood field (rover guide line) | `Garrison SitePlanner`, `scene/MicroMouse` | `M` flood field + guide line |
| Instancing | instanced primitives, grass, foliage | `rendering/Instancing` | `L` foliage, quality presets |
| Performance | FPS / frame time / draws / triangles HUD, `--benchmark` (presets + ablation), `--bench-shots` (JSON + MD) | `BenchShots`, `Capture` | `F3` debug HUD |
| Capture | `--capture DIR --only ...` (named shots incl. lab_*, play_*), `--record` 120 s director cut, F9 session + `--replay --record` 60 fps video | `Capture`, `Record`, `Replay` | |

## 4. Spec features: status
| Spec item | Status | Note |
|---|---|---|
| 1.1 Observation mode default, `G` game mode | PARTIAL | Enemies follow the day rules (night waves; `Shift+F2` day ops). `F11` is the game HUD. `G` is taken (explode). No separate "no enemies" mode switch. |
| 1.2 free-fly, third-person follow + Tab target, UAV FPV | YES | `Home`, `Tab`/`V`, UAV chase / FPV / gimbal |
| 1.2 orthographic top / front / side, isometric | YES (v10) | `Ctrl+P` cycles perspective, top, front, side, isometric; shots `v10_proj_*` |
| 1.2 cinematic orbit | YES | orbit camera, `K` director |
| 1.3 wireframe overlay | YES | `X` hidden-line / x-ray wireframe |
| 1.3 vertex / face normals as lines | PARTIAL | Frame Explorer / lab normals buffer; no line overlay |
| 1.3 depth visualisation (linearised) | YES | `F` Z-buffer view |
| 1.3 second-camera frustum in world space | PARTIAL | Frame Explorer bird's-eye stage draws the view frustum |
| 1.3 clipping window demo | **NO** | the report uses worked examples (workbook), stated as such |
| 1.3 back-face culling toggle | PARTIAL | Frame Explorer back-face stage; no live toggle |
| 1.3 flat / Gouraud / Phong | YES | `O` |
| 1.3 ambient / diffuse / specular split | YES (v10) | `Ctrl+L` (scene objects and terrain; grass / foliage always show all terms); shots `v10_light_*` |
| 1.3 local axes gizmo | YES | `H` |
| 1.4 `N`/`D` animated day/night | PARTIAL | animated cycle and scrub exist (`F2`, `,` `.`, `/`, menu presets); `N` and `D` are taken (`N` v1 renderer, `D` movement) |
| 1.4 sun arc + moon, derived lighting | YES (moon PARTIAL) | sun from the solar model; the moon is a fixed-direction night key light, not an orbiting body |
| 1.4 HUD clock, time scale | YES | auto: ~120 s day + ~160 s night (`DayCycleConfig`) |
| 1.5 intruders spawn, UAVs patrol | YES | rovers from the perimeter, UAV squad patrol / track |
| 1.5 UAV spotlight torch | YES | spot light, inner 9 / outer 14 deg, attenuation |
| 1.5 UAV picture-in-picture normal/thermal | PARTIAL | `U` thermal feed (thermal only; the gimbal FPV shows normal or thermal) |
| 1.5 UAV detect -> lock -> fire, soldier backup | PARTIAL | detect / track / lock 1.5 s / launch per UAV, combined strike; AI soldiers do not fire (ground fire only under player control) |
| 1.5 soldier shifts every 6 h | PARTIAL | day / night shifts with staggered shift change (2 shifts, not 4) |
| 1.5 hit detection | YES | projectile vs target spheres / boxes + blast radius (`scene/Combat`) |
| 1.6 live thermal stage panel with sliders | PARTIAL | Frame Explorer image lab (gamma, kernels, histogram) + CV lab figures; no histogram equalisation, no single tile strip with sliders |
| 1.6 noise restoration / DFT | **NO** | optional in the spec; not done |
| 1.7 LFR / micromouse | YES | rovers: MicroMouse flood fill (80x80 cells of 0.5 m) + 5-sensor line follower with a PD controller (Kp 3.2, Kd 0.25), `M` view; soldiers: A* grid planner |
| 1.8 FPS / ms / draws / triangles | YES | `F3` |
| 1.8 F9 benchmark CSV | PARTIAL | `TS_BENCH_V10=1 --benchmark FILE` writes FILE.csv (config, avg_ms, p95_ms, avg_fps, draw_calls, triangles), orbit camera not a flown path; F9 is the session clip key |
| 1.9 capture harness | YES | `--capture` named shots |
| Report: 11 primitives | PARTIAL | 7 exist (section 2) |

## 5. Key bindings in use
`docs/v9_notes/m2/keys.json` (exported by `ts --export-keys`, `scene/KeyMap.cpp`, 0 conflicts) lists all 123
bindings. Free single keys for v10 additions: none of the letters (all used); free: `F9` modifiers other than Shift,
`Ctrl+<letter>` combinations, keypad keys. Conflicts with the spec: `G` (explode), `N` (v1 renderer), `D` (movement),
`H` (local axes), `V` (view), `F9` (session clip). Decision with the user: keep the v9 keys; v10 additions go on free
keys and are listed in the report's controls table.

## 6. Feasibility
Nothing requested is infeasible in this stack. With the user's instruction to wrap up and focus on the report, v10
adds only the cheap views that close pipeline gaps (orthographic / isometric views, the lighting-term split) and
otherwise reports the PARTIAL items in the form they exist.
