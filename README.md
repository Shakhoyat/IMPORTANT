# ThermalSense: Autonomous UAV Patrol System

Md. Shakhoyat Rahman Shujon, Roll 2107104. Computer Graphics Lab project (OpenGL 3.3 core, GLFW, GLAD).

A night-time patrol of a 20 m radius compound. A quadcopter finds a small ground robot **from its own
thermal camera images** (blob detection on a 320x240 heat image) and follows it with a searchlight. The
robot uses **MicroMouse flood-fill planning + line-follower control** to keep an obstacle between itself
and the UAV. Every object (805 parts: bunker, UAV, rover, watchtower, fence, forest, boulders, grass...)
is built only from the lab's unit primitives (cube, sphere, cylinder; the cone and the light frustum are
the cylinder generator with a different top radius). No model files and no image textures are used.

The renderer aims at a modern game-like night (v3, tuned for an RTX 4070 12 GB): GGX/Cook-Torrance lighting
(flat / Gouraud / Phong still decide *where* it is evaluated), cascaded moon shadows + searchlight shadows,
SSAO, volumetric fog with light shafts, practical lights (lamps, fire, floodlight), procedural ground
materials, instanced grass and foliage cards, fireflies, a moonlit cloud sky, screen-space reflections in
puddles, temporal anti-aliasing, auto-exposure, bloom, lens flare, depth of field, camera motion blur and a
fitted ACES filmic curve with a night-vision (Purkinje) shift.

The **THERMAL view is first person**: the full screen is the UAV's gimbal camera (the same pose the tracker
uses) through a sensor model (automatic gain control, detail enhancement, fixed-pattern and temporal noise,
heat shimmer, NUC shutter) under a military-style FLIR HUD (compass tape, gimbal scales, telemetry,
target box with slant range, zoom WFOV / MFOV / NFOV). `Y` switches back to the external thermal view.

## Build and run

| Way | Command |
|-----|---------|
| Windows one click (MinGW-w64) | `run.bat` (builds `ThermalSense.exe` if missing, then starts it; arguments are passed through) |
| Windows build only | `build.bat` |
| Windows make (Git Bash / MSYS, incremental, also builds the tests) | `mingw32-make -j8` |
| Windows CMake (MinGW or MSVC) | `cmake -G "MinGW Makefiles" -B build-cmake && cmake --build build-cmake` |
| Linux | `./build.sh` (or `make -j$(nproc)`), then `./run.sh` |
| Linux without GLFW / without a display (EGL only) | `./build.sh --headless` (or `make -j$(nproc) HEADLESS=1`) |
| Linux CMake | `cmake -B build-cmake && cmake --build build-cmake -j` (`-DTS_HEADLESS_ONLY=ON` for EGL only; GLFW 3.4 is fetched if not installed) |

The Windows executable links GLFW and the C++ runtime statically, so it runs on any Windows PC with an
OpenGL 3.3 driver. On Linux the Makefile uses `pkg-config glfw3` (`sudo apt install build-essential pkg-config libglfw3-dev`).

## Running on the RTX 4070 remote machine

The quality preset is chosen automatically: **EPIC** on NVIDIA / discrete GPUs (override with `--quality`).

### Windows

1. Install MinGW-w64 (g++ in `PATH`), then run `build.bat` (or use the committed `ThermalSense.exe`).
2. `run.bat` opens a 1600x900 window. `run.bat --fullscreen` fills the primary monitor;
   `run.bat --quality epic --size 2560x1440` picks size and preset.
3. Remote desktop: use **Parsec** or **Sunshine + Moonlight** (they capture the GPU output, so the NVIDIA OpenGL
   driver is used). Plain **RDP** often gives OpenGL 3.3 only through "Microsoft Basic Render Driver"
   or fails to create the context; if you must use RDP, start the program from a console session
   (or use the headless Linux route below).

### Linux desktop (a session with a display)

```
sudo apt install build-essential pkg-config libglfw3-dev
./build.sh && ./run.sh --fullscreen
```
Hybrid laptops: `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./run.sh`.

### Linux headless over SSH (no display, EGL)

Needs only the NVIDIA driver (it provides `libEGL_nvidia`) and, for video, ffmpeg. `tools/remote_setup_linux.sh`
installs everything on a fresh Ubuntu box, builds and runs a smoke test.
```
./build.sh --headless        # or plain ./build.sh if libglfw3-dev is installed
./ThermalSense --headless --quality epic --capture captures --only cine_low_angle,thermal_fpv
./ThermalSense --headless --quality epic --benchmark build/benchmark.json
./ThermalSense --headless --quality epic --size 3840x2160 --fps 60 --encoder h264_nvenc --record report/demo_4k.mp4 60
```

### Command-line options

| Option | Meaning |
|--------|---------|
| `--quality low\|high\|ultra\|epic` | render preset (default: auto, EPIC on NVIDIA / discrete GPUs) |
| `--size WxH` | window / capture / video size (default 1600x900 window, 1080p video, 720p benchmark) |
| `--fullscreen` | full-screen window on the primary monitor |
| `--headless` | no window: EGL context (Linux) for `--capture`, `--record`, `--benchmark`, `--trace` |
| `--gpu N` | EGL device index in headless mode (also env `TS_EGL_DEVICE=N`) |
| `--fps 30\|60` | video frame rate for `--record` |
| `--encoder libx264\|h264_nvenc\|hevc_nvenc` | ffmpeg video encoder for `--record` |
| `--ffmpeg PATH` | ffmpeg executable |
| `--capture DIR [--only a,b]` | render the report figures |
| `--record FILE [SECONDS]` | cinematic video |
| `--benchmark FILE` | FPS per preset + per-feature ablation |
| `--trace FILE [SECONDS]` | log UAV / rover behaviour |
| `--help` | list all options |

### Troubleshooting

- The log prints `GL_RENDERER`. If it says `llvmpipe`, `Mesa` or `Microsoft Basic Render Driver`, the NVIDIA
  driver is not in use: install / repair the driver (Windows: NVIDIA driver, not RDP's software path; Linux: check `nvidia-smi`).
- Headless EGL picks the wrong device (several GPUs or a software device): try `--gpu 1` / `TS_EGL_DEVICE=1`.
- Hybrid Linux laptop (window opens on the iGPU): `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./run.sh`.
- `--encoder h264_nvenc` fails: your ffmpeg lacks NVENC or the driver is too old; use `--encoder libx264`.
- `run.sh` says there is no display: use one of the `--headless` commands above.

### Command-line modes

```
ThermalSense.exe                                  interactive simulation (F1 = controls)
ThermalSense.exe --capture report/figures [--only a,b]   render the report figures (Ultra, off-screen)
ThermalSense.exe --benchmark build/benchmark.json   FPS per preset + per-feature ablation + GPU pass times
ThermalSense.exe --trace build/trace.csv 180        log UAV / rover behaviour (4 Hz) for 180 s
ThermalSense.exe --record report/demo.mp4 60 [--ffmpeg PATH]   1080p cinematic video (needs ffmpeg)
build/export_docs.exe build/scene_export.json       GPU-free export of every part, matrix and mesh
build/tests.exe build/test_results.json             55 automated tests (no GPU needed; build/tests on Linux)
python tools/build_workbook.py                      rebuild ThermalSense_Graphics_Documentation.xlsx
python tools/verify_workbook.py                     recalculate every formula and check it
```

`mingw32-make docs` runs the export, the tests and the workbook script in one go.

## Controls (F1 in the window shows the same list)

| Key | Action |
|-----|--------|
| Mouse drag / wheel | orbit and zoom (look around in free / walk camera: left or right button) |
| `C` / `K` | camera: orbit, free, chase, UAV camera, **walk (first person, 1.7 m eye height)** / cinematic director |
| `W A S D` (+`Shift`), `PgUp PgDn` | walk (Shift = run) or move the free camera |
| `V` | view: visible, primitive diagnostic, Z-buffer, thermal |
| `Y` / `T` / wheel / `Q` | thermal: first-person or external view / palette (ironbow, white-hot, rainbow, black-hot) / zoom 40-20-8 deg / force a NUC |
| `X` | edges: solid, hidden-line wireframe, x-ray wireframe |
| `Z` | depth test on/off (shows why the Z-buffer is needed) |
| `O` `F` `G` `P` | shading: per-object, all flat, all Gouraud, all Phong |
| `1` `2` `3` `4` | toggle moonlight, beacon, searchlight, practical lights |
| `F5` | quality preset LOW / HIGH / ULTRA / EPIC |
| `F6` `F7` `F8` `F9` `F10` | TAA, screen-space reflections, lens flare, auto-exposure, depth of field |
| `L` / `J` / `R` | foliage cards / fireflies / rain |
| `B` / `N` | PBR vs classic Blinn-Phong / v1 renderer (no post-processing) |
| `+` `-` | exposure |
| `E` | explode composite objects into their primitives |
| `H` | local axes of the hierarchy (UAV, gimbal yaw, gimbal pitch, rover) |
| `M` | MicroMouse flood field and the line-follower guide line |
| `I` | indexed / non-indexed drawing |
| `U`, `Tab`, `Space`, `F12`, `Esc` | UAV feed, HUD, pause, screenshot, quit |

## Quality presets

| Preset | For | Adds |
|--------|-----|------|
| LOW | integrated GPUs, software rendering | 2x MSAA, 1024 shadows, no SSAO / fog / TAA |
| HIGH | mid-range GPUs | 4x MSAA + TAA, SSAO, volumetric fog, 35k grass, lens flare |
| ULTRA | report figures | 8x MSAA, 4096 shadows, moon god rays, 140k grass, SSR, DOF, motion blur |
| EPIC | RTX 4070 class (auto-selected on NVIDIA / discrete GPUs) | 4x MSAA + TAA, 4096 / 2048 shadows, full-resolution SSAO, 64-step fog, 450k grass blades, full foliage density, 700 fireflies, 64-step SSR |

The v3 pipeline: shadows -> depth/normal pre-pass -> SSAO -> volumetric fog -> HDR scene (MSAA, jittered,
second render target with normal + reflectivity) -> resolve colour / surface / depth -> fog + screen-space
reflections -> TAA (depth reprojection, Catmull-Rom history, YCoCg variance clipping) -> luminance / heat
statistics (auto-exposure, thermal AGC, temporally adapted) -> bloom -> lens flare + anamorphic streak ->
depth of field -> composite (motion blur, grading, ACES fit, sRGB) or the thermal sensor model. Details in
`docs/PLAN_V3.md`.

## Measured on the test machine (AMD Radeon integrated GPU, 1280x720, v2 renderer)

These numbers predate v3 (they were measured before the RTX 4070 target). Run
`ThermalSense --benchmark build/benchmark.json` on the RTX machine: it now also measures EPIC and the cost of
TAA, SSR and the foliage cards.

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
| `tests/test_main.cpp` | 55 automated tests (CPU only) |
| `ThermalSense_Graphics_Documentation.xlsx` | 14-sheet workbook: cover/KPIs, primitives, vertex data, object construction, transformation matrices (formulas), hierarchy, geometry, lighting, shading, render pipeline, testing, performance, behaviour, traceability |
| `report/main.tex`, `report/main.pdf` | 7-page report |
| `report/figures/` | screenshots captured from the program |
| `report/demo.mp4` | 60 s cinematic demo (1080p) |
| `docs/PLAN.md`, `docs/PLAN_V2.md`, `docs/PLAN_V3.md` | requirement checklist, the v2 plan, the v3 plan (RTX target, first-person thermal) |
| `report/figures_v3/` | v3 captures (EPIC): first-person thermal, walk camera, cinematic shots |
| `docs/visual_tests.json` | observations of the captured screenshots (used by the workbook) |
