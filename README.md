# ThermalSense: Autonomous UAV Patrol System

Md. Shakhoyat Rahman Shujon, Roll 2107104. Computer Graphics Lab project (OpenGL 3.3 core, GLFW, GLAD;
OpenGL 4.3 compute shaders for the ray tracer when the driver offers them).

**v8** makes the daytime camp look right and turns it into a small open world you can walk around in (details:
[v8 section](#v8-daylight-open-world-barracks-life)). Daylight uses a sky-dome ambient with sunlit ground bounce, a
sky environment for reflections, clear-day haze and a daylight grade, plus specular anti-aliasing, so the sun no
longer flares off wires and posts. **Click a soldier or UAV** to select it; **Enter** or a double-click takes control
(soldiers start in first person, **V** switches to a third-person camera on a spring arm that does not clip). There
is an **enterable barracks hall** where six off-duty residents sleep, do PT, talk at the mess table and use the gym.
Intruder rovers now drive with **bounded random speed, weave, hesitations and detours**.

**v7** makes it play like a game and shows how the pictures are made: **first person from any moving object**
(soldiers with a rifle, intruder rovers, both UAVs, the helicopter, the tank, rounds in flight) with a POV picker,
spring-blended camera jumps and a free fly camera; **GPU ray tracing** of the primitive-only scene (raster, hybrid,
Whitted, path tracing and light tracing = reverse ray tracing) with a **ray-tree debugger**; and the **Frame Explorer**,
which freezes a frame and replays it in 16 stages, from the scene graph through colour interpolation and reflection
(SSR vs ray traced) to the final thermal / CV image (details: `docs/PLAN_V7.md`, [v7 section](#v7-play-anywhere-ray-tracing-frame-explorer)).

**v6** makes the base live: an automatic **day / night cycle** (solar position, sun / moon key light, cloud shadows),
**8 soldiers** on Catmull-Rom routes with an IK gait who work by day and go off duty at night, **random rover
routes** per seed, and **real computer vision under a sensor-noise model** (details: `docs/PLAN_V6.md`,
[v6 section](#v6-day-and-night-soldiers-random-routes-real-cv)).

**v5 "Night Watch"** turns it into a playable base-defence game: **two UAVs** patrol and defend a **hidden army
camp** (camo nets, light discipline, SAM and MLRS launchers, tanks in revetments) against **endless waves of
intruder rovers**. Both UAVs' thermal cameras feed a **computer-vision pipeline** and one shared **Kalman /
Hungarian multi-target tracker with two-view triangulation**; the squad makes **coordinated time-on-target
strikes** (an armoured rover takes about 4-5 hits), with a game HUD, score, waves and **procedural sound**
(details: `docs/PLAN_V5.md`).

A night-time **military base** (v4): a 36 m radius base with an inner 20 m patrol compound. A quadcopter
finds intruder ground robots **from its own thermal camera images** (blob detection on a 320x240 heat image),
holds them in its camera and releases rail-mounted rounds that home on the camera's aim point; a hit throws the
rover's parts as rigid-body debris and leaves a burning wreck that is cleared and replaced by a new intruder.
The rovers use **MicroMouse flood-fill planning + line-follower control** to stay out of the UAV's sight.

v4 adds an **underground command bunker you can walk through** (entrance ramp, steel blast doors, tunnel,
command room, generator and bunk rooms, exit shaft), the **base apron** (HESCO perimeter with concertina wire,
4 guard towers, gate with boom barrier, helipad, motor pool, Quonset barracks, fuel depot, waving flag,
windsock) and **7 objects you can control** (keys 5-0): tank with animated tracks, helicopter, AA Gatling gun,
air-defence radar, blast doors, gate barrier and the UAV itself. Every object (about 3,000 parts) is built
only from the lab's unit primitives (cube, sphere, cylinder; the cone and the light frustum are the cylinder
generator with a different top radius). No model files and no image textures are used.

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
| Windows exe cross-built on Linux / WSL2 (no sudo, no MinGW) | `tools/build_windows.sh` (zig c++, incremental in `build/obj-win`; `--tests` also builds `build/tests-win.exe`) |
| WSL2 window without sudo (WSLg, RTX via Mesa d3d12) | `tools/run_wsl.sh` (fetches GLFW into `~/.local/opt/sysroot`, builds `./ThermalSense`, starts it) |

The Windows executable links GLFW and the C++ runtime statically, so it runs on any Windows PC with an
OpenGL 3.3 driver. On Linux the Makefile uses `pkg-config glfw3` (`sudo apt install build-essential pkg-config libglfw3-dev`);
without pkg-config it falls back to `~/.local/opt/sysroot` (see "WSL2 without sudo" below).

### Windows exe from Linux / WSL2 (cross-compile)

```
uv tool install ziglang          # once; user-local, gives ~/.local/bin/python-zig (the script does this itself if needed)
tools/build_windows.sh           # -> ThermalSense.exe (PE32+, x86-64), objects in build/obj-win
```
It runs `make OS=Windows_NT` with `zig c++ -target x86_64-windows-gnu` and links `lib/libglfw3.a` (GLFW 3.5.1),
libc++ and the MinGW-w64 runtime statically: the exe only imports KERNEL32 / USER32 / GDI32 / SHELL32 and the
Windows 10+ UCRT (`api-ms-win-crt-*`); `opengl32.dll` is loaded by GLFW, so the NVIDIA driver is used. No `glfw3.dll`
is needed. On WSL2 the exe can be started straight from the Linux shell (Windows interop), e.g.
`./ThermalSense.exe --trace build/t.csv 2` prints `NVIDIA GeForce RTX 4070/PCIe/SSE2` and exits; plain
`./ThermalSense.exe` opens the window on the Windows desktop. Any zig >= 0.13 works (`ZIG=/path/to/zig tools/build_windows.sh`).

### WSL2 without sudo (WSLg window on the RTX 4070)

```
tools/run_wsl.sh                         # X11 through WSLg (normal title bar); TS_WAYLAND=1 for native Wayland
tools/run_wsl.sh --fullscreen --quality epic
tools/run_wsl.sh --setup                 # only fetch GLFW + build ./ThermalSense
```
GLFW comes from Ubuntu's `libglfw3` / `libglfw3-dev` (`apt-get download` + `dpkg -x` into `~/.local/opt/sysroot`,
no root needed); the Makefile links it with an rpath, the X11 / Wayland / GL runtime libraries are the system ones.
The script sets `DISPLAY=:0` when the shell (ssh / Tailscale) did not export it. The program itself picks the NVIDIA
adapter (`GALLIUM_DRIVER=d3d12`), so the log shows `D3D12 (NVIDIA GeForce RTX 4070)` and EPIC. The window appears on
the Windows desktop of the WSL machine (view it with Parsec / Sunshine + Moonlight / RDP when remote); for the best
quality and input latency run `ThermalSense.exe` on Windows instead. With sudo the normal route is
`sudo apt install pkg-config libglfw3-dev` and `./build.sh`.

## Running on the RTX 4070 remote machine

The quality preset is chosen automatically: **EPIC** on NVIDIA / discrete GPUs (override with `--quality`).

### WSL2 on the RTX 4070 machine (no sudo)

WSL2 exposes the GPU as `/dev/dxg` (no `/dev/dri`), so plain Mesa / EGL would only find llvmpipe. The program
handles this itself: `preferWslGpu()` (`src/app/HeadlessGL.cpp`, called for the window and for `--headless`) sets
`GALLIUM_DRIVER=d3d12` and `MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA` when `/dev/dxg` exists and `/dev/dri` does not,
and headless runs use Mesa's surfaceless EGL platform. Nothing has to be exported by hand.
```
tools/run_wsl.sh                                   # interactive window through WSLg (builds ./ThermalSense first)
./build.sh --headless && ./ThermalSense --headless --capture captures --only raytrace_modes   # off-screen
```
Check the log: `D3D12 (NVIDIA GeForce RTX 4070)` and `Quality preset: EPIC`. `TS_GPU_ADAPTER=<name>` picks another
adapter, `TS_NO_WSL_GPU=1` turns the automatic choice off. Mesa d3d12 gives OpenGL 4.6, so the ray tracer runs in
compute shaders. The three `libEGL warning: failed to get driver name for fd -1` lines are harmless.

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
| `--bench-shots FILE [--only a,b]` | v9: 5 fixed gameplay shots (overview, uav_chase, soldier_1p, soldier_3p, hall_inside) x day / night at 1080p: FPS, CPU phases, GPU passes, draws, triangles (JSON + `FILE.md`) |
| `--input-script FILE [--frames DIR]` | v9: play timed key / mouse events into the interactive game (`shot` / `log` lines save frames and state; format in `src/scene/InputScript.h`) |
| `--vsync 0\|1` | swap interval (default 0: the frame limiter paces the interactive loop; the Settings menu / `ThermalSense.ini` otherwise) |
| `--fps-limit N` | interactive frame limiter (default: display refresh; 0 = uncapped) |
| `--ini FILE\|none` | v9 M2: the settings file (default `ThermalSense.ini` next to the exe) |
| `--export-keys FILE` | v9 M2: the key / mouse binding table as JSON (contexts, old binding, conflict check) |
| `--trace FILE [SECONDS]` | log UAV / rover behaviour (also writes `FILE.events.csv` and a `rules:` check line) |
| `--export-docs FILE` | GPU-free export of every part, matrix and mesh (JSON for the workbook) |
| `--seed N` | v6 run seed: rover routes, patrols, sensor noise (default: random and printed when interactive, 1 for offline modes; 0 = the fixed v5 run with an ideal sensor) |
| `--time day\|night\|dusk\|dawn\|cycle\|HH:MM` | v6 time of day (default: automatic cycle from 16:30 interactive, night 23:00 for offline modes) |
| `--cycle-seconds N` | length of one automatic day + night cycle |
| `--no-garrison` | no soldiers |
| `--help` | list all options |

Environment variables: `TS_RT_CPU=1` forces the CPU ray tracer (otherwise used only without GL 4.3 compute);
`TS_DIRECTOR_CUT=v7|v6|v5`, `TS_DIRECTOR_START=s`, `TS_DIRECTOR_SPEED=x` for `--record` (v7 cut by default);
`TS_EXPLORER_TRACE=1` prints the Frame Explorer's timing; `TS_GPU_ADAPTER` / `TS_NO_WSL_GPU` (WSL2, above);
`TS_EGL_DEVICE=N` (headless device); `TS_NO_AUDIO=1` (record without sound).

### Troubleshooting

- The log prints `GL_RENDERER`. If it says `llvmpipe`, `Mesa` or `Microsoft Basic Render Driver`, the NVIDIA
  driver is not in use: install / repair the driver (Windows: NVIDIA driver, not RDP's software path; Linux: check `nvidia-smi`).
- Headless EGL picks the wrong device (several GPUs or a software device): try `--gpu 1` / `TS_EGL_DEVICE=1`.
- Hybrid Linux laptop (window opens on the iGPU): `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./run.sh`.
- `--encoder h264_nvenc` fails: your ffmpeg lacks NVENC or the driver is too old; use `--encoder libx264`.
- `run.sh` says there is no display: use one of the `--headless` commands above (on WSL2: `tools/run_wsl.sh`).
- WSL2 log shows `llvmpipe`: `/dev/dxg` is missing (old WSL / no GPU passthrough) or `TS_NO_WSL_GPU` is set; see "WSL2 on the RTX 4070 machine".
- `Ray tracing: no compute shaders (OpenGL 3.3)`: the context has no GL 4.3 compute, so `;` modes use the slow CPU
  tracer. Mesa d3d12 on WSL2 gives GL 4.6; the native Windows window currently asks for a 3.3 core context.

### Command-line modes

```
ThermalSense.exe                                  interactive simulation (F1 = controls)
ThermalSense.exe --capture report/figures [--only a,b]   render the report figures (Ultra, off-screen)
ThermalSense.exe --benchmark build/benchmark.json   FPS per preset + per-feature ablation + GPU pass times
ThermalSense.exe --trace build/trace.csv 180        log UAV / rover behaviour (4 Hz) for 180 s
ThermalSense.exe --record report/demo_v7.mp4 150 [--ffmpeg PATH]   1080p director cut with sound (v7 cut ~150 s; needs ffmpeg)
build/export_docs.exe build/scene_export.json       GPU-free export of every part, matrix and mesh
build/tests.exe build/test_results.json             289 automated tests (no GPU needed; build/tests on Linux)
python tools/build_workbook.py                      rebuild ThermalSense_Graphics_Documentation.xlsx
python tools/verify_workbook.py                     recalculate every formula and check it
```

`mingw32-make docs` runs the export, the tests and the workbook script in one go.

## Controls (F1 in the window shows the list; the table is `keyMapV9()` in `src/scene/KeyMap.cpp`, `--export-keys FILE`)

v9 M2 unified the scheme the way action games do it: **E** uses whatever the on-screen prompt names, **Tab** switches
unit, **V** switches the view of what you control, **Esc** lets go and then opens the pause menu, **P** pauses.

| Key | Action |
|-----|--------|
| `E` | interact: the prompt on screen says what (`[E] Open blast doors`, `[E] Enter Barracks`, `[E] Take control of UAV-1 DRONE`, `[E] Ride UAV-2`, `[E] Release`); nearest thing in reach (doors 2.5 m, the barrier 3 m, soldiers 2.5 m, vehicles a step from the hull) inside the view cone and not behind a wall; from an overview camera: the selection, else the unit the view centres on |
| `Tab` / `Shift+Tab` | take control of the next / previous unit: soldiers, then UAVs, then vehicles and installations |
| `V` | soldier: first ↔ third person; vehicle / UAV: chase → sight → alt camera; riding along: first → chase → alt |
| `Esc` | release control / leave the ride → clear the selection → **pause menu** (Quit is a menu entry); closes F1 help |
| `P` or `Pause` | pause / resume |
| `F3` | HUD: game (decluttered) → game + debug blocks (switches, UAV / rover status, frame time, triangles) → off |
| left click / double-click / `Enter` | select the object under the pointer / take control / take control of the selection |
| `C` | camera (nothing controlled): orbit, free, chase, UAV camera, **walk (first person, 1.7 m eye height)** |
| mouse drag / wheel | orbit and zoom (look around in free / walk camera: left or right button) |
| `W A S D` (+`Shift`), `PgUp PgDn` | walk (Shift = run) or move the free camera |
| `F` | view: visible, primitive diagnostic, Z-buffer, thermal |
| `O` | shading: per-object → all flat → all Gouraud → all Phong |
| `G` | explode composite objects into their primitives |
| `Y` / `T` / wheel / `Q` | thermal: first-person or external view / palette (ironbow, white-hot, rainbow, black-hot) / zoom 40-20-8 deg / force a NUC |
| `X` | edges: solid, hidden-line wireframe, x-ray wireframe |
| `Z` | depth test on/off (shows why the Z-buffer is needed) |
| `1` `2` `3` `4` | toggle moonlight, beacon, searchlight, practical lights |
| `F5` | quality preset LOW / HIGH / ULTRA / EPIC |
| `F6` `F7` `F8` `F9` `F10` | TAA, screen-space reflections, lens flare, auto-exposure, depth of field |
| `L` / `J` / `R` | foliage cards / fireflies / rain |
| `B` / `N` | PBR vs classic Blinn-Phong / v1 renderer (no post-processing) |
| `+` `-` | exposure |
| `H` | local axes of the hierarchy (UAV, gimbal yaw, gimbal pitch, rover) |
| `M` | MicroMouse flood field and the line-follower guide line |
| `I` | indexed / non-indexed drawing |
| `U`, `F12` | UAV feed, screenshot |
| `F1` | help overlay (all keys) |

**Pause menu** (`Esc` with nothing under control; the world holds, the mouse is free): tabs RESUME / TRAVEL / TIME /
VIEW / SETTINGS / QUIT (`Tab` / `Shift+Tab` switch pages, `Esc` or `G` resumes). TRAVEL / TIME / VIEW are the v7.1
field guide, drawn and wired: ride UAV-1 / UAV-2, a random soldier in first / third person, the POV list, the free
camera, the tank, the helicopter, an intruder rover (keys `1`-`9`); dawn / noon / dusk / midnight (`Q W E R`), a
time-of-day slider (drag, or `←` `→` 15 min), automatic cycle on / off (`T`), cycle speed x1 / x2 / x4 / x8 / x0.5
(`Y`); person view, render mode, Frame Explorer, thermal, HUD level, UAV feed (`A S D F H U`). SETTINGS (`↑` `↓`
select, `←` `→` or click `-` / `+`): mouse sensitivity (0.1-3.0, every look path), invert Y, field of view on foot
(50-95°), vsync, frame limit (display / 30-240 / uncapped), head bob, quality preset. They apply at once and are
saved to `ThermalSense.ini` next to the exe (`key = value`; `--ini FILE` picks another file, `--ini none` none;
`--vsync` / `--fps-limit` / `--quality` on the command line win for that run until changed in the menu).

| v9 M2 change | Before | Now |
|-----|--------|-----|
| `E` | explode | interact (prompt) |
| `G` | Gouraud shading | explode |
| `F` | flat shading | view mode (was `V`) |
| `O` | per-object shading | cycles per-object / flat / Gouraud / Phong |
| `P` | Phong shading | pause |
| `V` | view mode; soldier 1st / 3rd | the only view key: soldier 1st / 3rd, vehicle chase / sight / alt, ride-along view |
| `C` | camera cycle, and soldier 1st / 3rd, POV view, vehicle sight | camera cycle only (nothing controlled) |
| `Tab` | HUD on / off | next unit (`Shift+Tab` previous) |
| `F3` | possess the next soldier | HUD level game / debug / off (`Tab` reaches every soldier) |
| `Space` | jump (soldier), else pause | jump only |
| `Esc` | release → clear selection → **quit** | release → clear selection → **pause menu** |

### Controlling objects (v4)

| Key | Action |
|-----|--------|
| `5` `6` `7` `8` `9` `0` | take control of: tank, helicopter, AA gun, radar, blast doors, UAV (`F3`: next soldier, v6) |
| `[` / `]` | cycle through every controllable object (incl. SAM launcher, MLRS truck, gate barrier, soldiers, SKY clock): 18 possessable |
| `Backspace` | release control (`Esc` and `E` too) |
| `W` `S` / `A` `D` / `PgUp` `PgDn` | drive / steer / climb (tank, helicopter, UAV) |
| arrows, mouse drag | aim (turret and gun, AA mount, searchlight, radar sector, UAV gimbal) |
| `Enter` or `Left Ctrl` | fire / transmit (tank gun, AA burst, UAV round); in walk mode: open/close the nearest door or barrier |
| `\` | action: helicopter start-up / land, radar mode, door toggle, UAV gimbal assist |
| `Shift` | boost |
| `V` (while controlling; `C` before v9 M2) | chase camera / gun sight / cockpit |

### Game rules (v5 "Night Watch")

| Rule | Value |
|------|-------|
| Defenders | 2 UAVs: opposite-phase patrol; on contact LEAD + WING (~90 deg apart) |
| Vision | each UAV's 320x240 heat image -> threshold (max(0.80, Otsu)) -> despeckle -> union-find components -> moments -> shared Kalman multi-target tracker (chi-square gating, Hungarian assignment, two-view triangulation, track merging) |
| Strike | lead locked >= 0.8 s and wing >= 0.5 s with a clear line of fire -> combined strike: the farther UAV launches first so both rounds arrive together (time on target), each aimed at the predicted position; solo strike after 2.5 s if the wing can't lock |
| Rounds | 4 per UAV on rails, 6 s reload, homing on a laser spot at the predicted intercept, proximity fuse 0.5 m |
| Intruders | waves of min(2 + n, 6) rovers from random edge points, each heading for a random camp asset (bunker ramp, bunker mast, watchtower, radio mast, supply tent) on its own random route (weighted MicroMouse flood fill + line follower); hide for 4 s when a UAV comes within 5 m |
| Armour | 100 HP; a blast does 30 x power, half at the edge of its 1.6 m radius: about 4-5 hits per kill; damaged rovers limp and smoke |
| Score | kill +100, combined-strike kill +150; a rover reaching its asset = breach, base integrity -10 %; 0 % = BASE LOST, restart at wave 1 |
| Friend or foe | blobs near any camp vehicle, known wrecks (30 s) and the bunker ramp are never targets |
| Sound | procedural (no files): rotors, rounds, explosions, siren, lock beeps, ambient night; live playback and in `--record` videos |

## v6: day and night, soldiers, random routes, real CV

| Feature | What it does |
|---------|--------------|
| Day / night cycle | solar position at latitude 22.8° (declination −4.6°): sunrise ≈ 06:08, sunset ≈ 17:52, noon elevation 62.6°; the key light is the sun (air-mass reddening near the horizon) or the v5 moon, and the night preset is the v5 look bit for bit; practical lamps fade out by day, searchlights run only below −2°; a drifting procedural cloud layer at 120 m also casts moving cloud shadows; automatic cycle ≈ 290 s (twilight slowed ×1.5); intruder waves only at night, survivors withdraw at dawn |
| Soldiers | 8 on duty (SENTRY, WATCH, GUNNER, MECHANIC, LOGISTICS, TECHNICIAN, PATROL, RUNNER), 75 primitives each in a hierarchy 7 levels deep; centripetal Catmull-Rom routes (α = 0.5, arc-length table, speed v = vmax / (1 + 1.5 κ)) planned on a 1 m grid around the camp; forward kinematics + two-link leg IK (planted stance foot, swing arc, pelvis bob, counter arm swing); work poses by day (wrench at the tank, crates), shift change at dusk / daybreak, some off duty in the bunker at night; all possessable |
| Random rover routes | ring spawns (never within ±18° of the gate road), 0-3 via points in a ±70° sector, objective switch with p = 0.3, a personality per rover (speed 0.8-1.25, noise / exposure weights of the MicroMouse cost map, pauses in cover); seeded patrols for the UAVs; everything derives from the run seed (`--seed`, seed 0 = the v5 run) |
| Real CV under sensor noise | each UAV image goes through y = Q(g·(PSF * x) + o + c + n): temporal noise NETD 2 DN, per-UAV fixed-pattern gain / offset, column stripes, NUC residual, PSF blur, stuck pixels, gimbal jitter 0.08°, 1 % dropped frames; `detectAll` classifies target / person / hot clutter / clutter; soldiers are persons, never targets; rounds are only aimed at vision tracks (a blind squad never fires) |

Verified with 600 s traces: the same seed gives byte-identical runs, another seed a different one; 0 spawns by day,
withdrawals only at dawn, every kill from a vision-designated round (`docs/v7_notes/wp-cap.md`).

| Key (v6) | Action |
|----------|--------|
| `F2` | day / night cycle: automatic <-> manual clock |
| `,` `.` (held) | manual clock: scrub the time of day back / forward (2 h/s; a press stops the automatic cycle) |
| `/` | jump to the next phase (dawn, day, dusk, night) |
| `Tab` | take control of the next unit, soldiers first (v6 used `F3` = next soldier; since v9 M2 `F3` is the HUD level) |
| `W A S D`, `Shift` | possessed soldier: walk / turn, run |
| `\` | possessed soldier: stand / crouch / wave; possessed SKY clock: auto / manual |
| `W` `S`, `Enter` | possessed SKY clock (via `[` `]`): scrub the time, jump to the next phase |

## v7: play anywhere, ray tracing, Frame Explorer

- **Game camera** (WP-G/J): every moving object has a point of view: soldiers (eye in the head node, head bob from
  the gait, rifle and hands in view), intruder rovers (mast / hood camera), both UAVs (gimbal and belly camera),
  helicopter cockpit, tank commander hatch, AA gun, SAM / MLRS, rounds in flight (followed until impact, then the
  camera holds on the blast for 2.5 s). Jumps blend position and orientation with a critically damped spring
  (~0.6 s). Riding along, any drive key takes control; taking an intruder rover's wheel pauses its planner while the
  UAVs keep hunting it, so you can try to sneak in. `F11` game mode adds crosshair, hit marker, compass strip, alert
  vignette when your rover is laser designated, FOV kick and letterboxed jumps.
- **Ray tracing** (WP-H): the scene is only spheres, boxes, cylinders and cones, so every part is intersected
  exactly (ray to local space with the inverse matrix) in a binned-SAH BVH (~5.1k primitives, ~8.5k nodes, build
  61 ms once, refit < 1 ms per frame) plus a 289² terrain height field, all in GLSL 4.30 compute shaders. Five render
  modes (`;`) and a ray-tree debugger (`'`).
- **Frame Explorer** (WP-I): `` ` `` freezes the current frame and replays how it was made in 16 stages, each a real
  buffer of that frame with a caption, the key formula and a probe pixel, plus a pixel inspector and an image-processing
  lab (convolution kernels, histogram, gamma, sampling / aliasing).

### Keys (v7, complete)

Checked against `keyBindingsV7()` / `helpLines()` in `src/scene/HudMath.cpp` and the handlers in `src/app/Interactive.cpp`.

| Key | Action |
|-----|--------|
| `F4` | POV picker: every moving object (soldiers, intruder rovers, both UAVs, helicopter, tank, AA gun, SAM / MLRS, radar, rounds in flight) sorted by distance; `↑` `↓` (`PgUp` `PgDn` = 8 rows) and `Enter` or a click jumps, `Esc` / `F4` closes |
| `Insert` / `Delete` | jump to the next / previous moving object's POV (spring-blended camera, ~0.6 s) |
| middle mouse | jump into the object under the cursor (ray pick; the crosshair in game mode) |
| `Home` | free fly camera from the current view: `W A S D`, `PgUp` `PgDn`, drag (left or right button) = look, wheel = speed, `Shift` ×3 |
| `End` | back to the previous view (`End` again swaps back) |
| `V` (riding along; `C` before v9 M2) | first person -> chase -> alt camera (tank commander hatch, UAV belly camera, rover mast camera) |
| mouse drag (riding along) | free look (left or right button; no button needed in game mode) |
| `E`, `W A S D` / arrows / `PgUp` `PgDn` (riding along) | take control: possess the object, or take an intruder rover's wheel (its planner pauses, the UAVs keep hunting it) |
| `Backspace` | driving a rover: give it back to its planner and keep riding along; otherwise release control |
| `Left Ctrl` (or `Enter`), soldier | possessed soldier (first or third person): fire the rifle (hold = automatic; tracers, damages intruder rovers) |
| `Left Alt`, soldier | crouch <-> stand (`Left Ctrl` stays fire, so crouch is on `Left Alt`) |
| `F11` | game mode: game HUD only, crosshair, hit marker, compass strip, alert vignette, FOV kick on sprint / boost, stronger blast shake, letterbox on jumps, mouse look without a button |
| `;` | render mode: raster -> hybrid RT -> Whitted RT -> path tracing -> light tracing (reverse) -> raster |
| `'` | ray debugger on / off; while on, a left click traces that pixel and draws its ray tree in 3D |
| `` ` `` | Frame Explorer: freeze this frame and step through its 16 stages (`` ` `` or `Esc` closes) |

Inside the Frame Explorer (it takes every key and the mouse while open):

| Key | Action |
|-----|--------|
| `←` `→` / `1`-`9`, `0` / `Home` `End` | previous / next stage; stage 1-9, 10; first / last stage |
| `↑` `↓` | layer of the stage (e.g. affine \| perspective-correct, Gouraud \| Phong, SSR \| ray traced) |
| left click / drag | move the probe pixel (every stage card follows it) / drag a split line |
| `W` | wipe against the previous stage (drag the line) |
| `M`, wheel | magnifier, its zoom |
| `I` | pixel inspector: the probe's value in every buffer |
| `P` (then `K`, `[` `]`, `S`) | image lab (next kernel, gamma down / up, sampling factor) |
| `Space` / `R` / `Tab` | pause / restart the animation (raster scan, SSR march) / captions on-off (clean view) |

### Render modes (`;`) and the ray debugger (`'`)

Times: RTX 4070 via Mesa d3d12 (WSL2), 1280x720 output, EPIC, day 16:30, wall clock (`docs/v7_notes/wp-h.md`).

| Mode | What is traced | Internal resolution | Frame |
|------|----------------|---------------------|------:|
| Raster | the v6 EPIC rasteriser (shadow maps, SSR, SSAO, fog, TAA, bloom) | 1280x720 | 11.7 ms |
| Hybrid RT | raster G-buffer + BVH soft shadow rays (key light, searchlight) + reflection rays that replace SSR (on-screen hits reuse the raster colour, off-screen ones are shaded by the tracer) | 1280x720 | 20.3 ms (RT pass 10 ms) |
| Whitted RT | recursive: shadow ray per light, mirror reflection, glass with exact Fresnel + Snell refraction (total internal reflection), one AO ray; converges (soft shadows, AA) while the camera is still | 960x540 | 25.7 ms |
| Path tracing | Monte Carlo: next event estimation to every light, cosine + GGX sampling, Russian roulette from bounce 2, progressive accumulation, à-trous denoise | 640x360 -> 720p | 21.9 ms |
| Light tracing (reverse) | 256K light paths per frame from sun / moon / sky, searchlight, beacon and lamps; every vertex is splatted to the camera: caustics through glass, mirrors stay black (correct for pure light tracing) | 640x360 | 39.0 ms |

- Day 16:30 on the RTX 4070 (WSL d3d12, `report/figures_v7/raytrace_perf.json`). After WP-Q the raster frame is 11.7 ms; the ray-traced passes themselves are 10-33 ms.
- Ray-traced modes apply to the graded visible view; thermal and diagnostic views stay raster.
- They need OpenGL 4.3 compute shaders; without them (or with `TS_RT_CPU=1`) a CPU tracer renders them at low resolution.
- Ray debugger: `'`, then left click a pixel. The CPU Whitted tracer re-traces it and the tree is drawn in 3D (white
  primary, yellow lit / red blocked shadow rays, cyan reflection, magenta refraction, green diffuse bounce) with
  depth, distance, contribution and object, plus an indented list; it stays in the world while the camera moves.

### Frame Explorer (`` ` ``)

Opening renders the frame through the real renderer (TAA converged) plus variants (SSR / SSAO / fog off,
Blinn-Phong, TAA off, thermal, raw heat, ray-traced reflections) and the explorer's own passes; a progress panel shows
while it works (~4-5 s on the WSL d3d12 path).

| # | Stage | # | Stage |
|--:|-------|--:|-------|
| 1 | scene graph -> world matrices | 9 | lighting: ambient + diffuse + specular, PBR \| Blinn-Phong |
| 2 | vertex transform: model -> world -> view -> clip -> NDC -> pixels | 10 | reflection: mirror law, SSR march replay, **SSR \| ray traced**, RT - SSR difference |
| 3 | primitive assembly + back-face culling | 11 | screen-space ambient occlusion |
| 4 | rasterisation: coverage, edge functions, barycentrics (scanline reveal) | 12 | fog and volumetric light |
| 5 | **colour interpolation**: affine \| perspective-correct, Gouraud \| Phong | 13 | bloom: threshold, mip chain, blur |
| 6 | depth test: Z-buffer and overdraw | 14 | tone mapping + exposure (curve, histogram) |
| 7 | G-buffer channels | 15 | temporal anti-aliasing (Halton jitter, history) |
| 8 | shadow map: the scene seen from the light, PCF | 16 | final frame + thermal / computer vision |

Captures: `--capture DIR --only explorer` writes `explorer_stage_01_graph.png` ... `explorer_stage_16_final.png`, the
montage and the detail shots; `--only raytrace_modes,raytrace_tree,raytrace_ssr,raytrace_photons,raytrace_perf` the
ray-tracing figures; any `game_*` name (e.g. `--only game_soldier_fp,game_picker`) the game-camera shots.

## v8: daylight, open world, barracks life

| Area | What changed (files) |
|------|----------------------|
| Daylight lighting | `ShaderSources.cpp`: `dayAmbient()` is the sky dome a face sees: zenith overhead, a brighter and whiter horizon band (`uSkyHorizon`) for walls, sunlit ground bounce from below, and the brighter circumsolar sky on the sun's side. `skyEnvironment()` provides roughness-blurred sky/ground reflections with a soft sun glow. Both are weighted by `uDayWeight`, so the v5 night is untouched. Foliage and grass use the same ambient. |
| Haze and grade | `DayCycle.cpp`: clear-day fog 0.0075 → 0.0042, volumetric haze 0.0032 → 0.0013 (it was a grey veil past ~40 m). `ShadersPostFx.cpp`: daylight contrast around mid-grey plus a little more saturation (`uDayGrade`). Lamp halos in the mist fade by day (`uHaloScale`). |
| Sun glints | The "white blobs" by day came from the sun mirrored in thin galvanized wires, posts and rails: a GGX peak of about 100× mid-grey, then blown up by bloom and lens-flare ghosts. Fixed with geometric specular AA in the Phong path (`gSpecAA`, normal variance from `dFdx`/`dFdy`), a specular LOD per curved part thinner than ~4 px (`Renderer::specularLod`), and weathered zinc (roughness 0.40 → 0.52). The UAV lens and the helicopter's beam cone are off by day. |
| Colours | Spruce cards deep green instead of teal; grass mostly green/olive with dry tufts; sunlit translucency is a rim, not a glow. |
| Click to control | `app/AppPlay.cpp`. Hovering shows a name tag and a ring. A **left click** (no drag) selects any moving object; **Enter** or a **double-click** takes control. Soldiers start in **first person** (A/D strafe, the mouse turns); **V** (v8: or **C**) toggles **third person**. **Esc** releases control, then clears the selection, then quit (v9 M2: `V` only, and `Esc` then opens the pause menu). Everything works on `PovSource`, so any new `Controllable` becomes selectable automatically. |
| Third person | v7.1 `ThirdPersonCam` + `thirdPersonSoldierInput` are now wired in. Camera-relative W A S D (the body turns into the walk), right mouse aims (shoulder zoom, the body faces the camera), the left mouse fires from the rifle muzzle toward the crosshair hit. The spring arm is probed against the oriented boxes of nearby parts, WalkSpace walls, floors and ceilings: it pulls in at once and eases out; a shorter arm indoors; when squeezed under head size the character is hidden for that frame. Mouse is captured while you control a soldier. Space jumps, Shift sprints, Alt crouches; landing shake. |
| Barracks hall | `objects/BarracksHall.cpp`, `BaseLayout.h::hall`: an axis-aligned prefab on the east apron (x 18.6–27.6, z −15…−8.6). It has real door and window openings, a gable roof over a 2.7 m ceiling, 3 bunk beds, lockers, a rifle rack, a notice board, a mess table with benches, a gym corner (pull-up bar, mats, dumbbell rack, punching bag), fluorescent lights and a PT area outside. WalkSpace floors now count on the apron too; indoor volumes carry `IndoorLight` (the bunker: no sky or sun; the hall: 24 % sky light, and the sun through the windows decided by the shadow map). No grass grows through the floor. |
| Barracks life | `Garrison` takes any number of soldiers (`SceneOptions::residents`, 6 in the app). Residents are `Soldier`s with duty BARRACKS and new activities: **Sleep** (in their own bunk, also by day for the night watch), **PushUps**, **SitUps**, **PullUps**, **Talk** (seated at the table or standing outside), plus sitting on a bunk and the lockers. Poses are new `Gait` functions with a whole-body `bodyPitch`. They get the duty soldiers' A* routes, door handling, separation, shift change (all asleep at night) and possession (first/third person) for free. |
| Natural enemy motion | `HideController` + `RoverPersonality` (hashed from each route seed; the draw stream is unchanged). Speed wanders (Ornstein-Uhlenbeck, ±15–35 %), the guide line weaves 0.15–0.45 m with a 3.5–7 m wavelength (phased by the odometer, kept off obstacles), short hesitations in the open, and up to two random side detours. A stuck guard turns the weave off for 10 s. The same seed repeats exactly; `--seed 0` stays the plain v5 rover. The UAV CV / Kalman tracker is unchanged and now has to follow less predictable targets. |
| Tests | `tests/test_v8.cpp` T320–T327 (daylight presets, hall walkability and lighting, residents' variety / containment / night sleep, route clearance, poses, natural rover motion, third-person helpers). 298/298 pass. |

Scripted figures of the new paths (off-screen, like the other captures):
`ThermalSense --headless --capture DIR --only play_select,play_first_person,play_third_person,play_third_person_turn,play_third_person_aim,play_third_person_indoor,play_hall_outside,play_hall_inside,play_hall_resident,play_hall_pushups,play_hall_situps,play_hall_pullups,play_hall_talk,play_hall_sleep,play_hall_night`

| Key (v8) | Action |
|-----|--------|
| left click / hover | select the object under the pointer (soldiers, UAVs, vehicles, rovers) / show its name |
| `Enter` or double-click | take control of the selection (soldier: first person) |
| `V` (soldier; `C` too before v9 M2) | first person ↔ third person (smooth blend) |
| `W A S D`, `Shift`, `Space`, `Left Alt` (soldier) | move (third person: relative to the camera), sprint, jump, crouch |
| mouse, left / right button (soldier) | look / fire / aim (first person: iron-sight zoom; third person: over-the-shoulder zoom) |
| `Esc` | release control → clear selection → pause menu (v8: quit) |
| `P`, `Pause` | pause (v8: `Space` paused when you were not a soldier; since v9 M2 `Space` only jumps) |

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

## Measured on the test machine (RTX 4070, v7 renderer)

`--benchmark` FPS, before and after the v7 performance pass (WP-Q: instanced scene parts, grass built once,
asynchronous UAV-camera readback). WSLg is capped by the window's GPU-to-CPU presentation copy; play the native
exe for the full frame rate. Details in `docs/v7_notes/wp-q.md`.

| Preset | Native 1080p | Native 720p | WSLg 1080p | WSLg 720p |
|--------|--------------|-------------|------------|-----------|
| LOW | 58.7 -> 126.1 | 49.6 -> 130.8 | 9.2 -> 42.5 | 8.4 -> 56.0 |
| HIGH | 46.3 -> 106.5 | 40.5 -> 86.9 | 7.4 -> 40.8 | 6.2 -> 53.6 |
| ULTRA | 25.0 -> 77.0 | 25.3 -> 87.0 | 6.3 -> 34.9 | 6.5 -> 48.6 |
| EPIC | 9.2 -> 72.9 | 8.8 -> 87.6 | 4.2 -> 33.0 | 3.7 -> 44.1 |

Auto preset: EPIC, stepped down at start-up (EPIC -> HIGH -> LOW) if it does not hold 60 fps (30 in WSLg);
`TS_TARGET_FPS` overrides the target and `F5` ends the governor. EPIC draw calls fell from about 5600 to about 215
per frame. The HUD debug line shows FPS, frame time, preset and draw calls, plus a 120-frame frame-time graph.
Environment variables: `TS_NO_INSTANCING=1` (old one-draw-per-part path), `TS_GPU_SYNC_TIMING=1` (glFinish-based
pass timing, since Mesa d3d12 timer queries include idle gaps), `TS_BENCH_ROUNDS`, `TS_BENCH_SECONDS`,
`TS_BENCH_PHASES`.

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
| `src/` | source code (primitives, objects, rendering, raytrace, scene, ai, vision, audio, app, math) |
| `tests/test_*.cpp` | 289 automated tests (CPU only; `tests/TestKit.h`, IDs by area; v7: T240-T259 game camera, T260-T279 ray tracing, T280-T299 Frame Explorer, T300-T309 audio) |
| `ThermalSense_Graphics_Documentation.xlsx` | 24-sheet workbook: cover/KPIs, primitives, vertex data, object construction, transformation matrices (formulas), hierarchy, geometry, lighting, shading, render pipeline, testing, performance, behaviour, traceability, controllables, bunker and base, combat, audit, computer vision, squad, audio and HUD, day cycle, soldiers and routes, CV noise and seeds |
| `report/main.tex`, `report/main.pdf` | report (v6 sections s18-s22; v7 sections to follow) |
| `report/figures_v6/`, `report/figures_v7/` | v6 captures (day / night, soldiers, routes, CV noise); v7 captures (`game_*`, `iso_*`, `raytrace_*`, `explorer_*`) |
| `report/ThermalSense_Presentation.pptx` | 22-slide deck built by `tools/build_slides.py` (v1-v5 foundations, 4 v6 slides, 8 v7 slides, results, demo video slide) |
| `report/demo_v7.mp4` | v7 director cut (~150 s, procedural sound): **not recorded yet**; `report/demo_v5.mp4` is the v5 cut |
| `docs/PLAN*.md`, `docs/V4_PROGRESS.md` | requirement checklist and the plans of every version (v6 / v7 with live status tables) |
| `docs/v4_notes/` ... `docs/v7_notes/` | every work package's design notes, measurements and open items |
| `report/figures_v3/`, `report/figures_v4/`, `report/figures_v5/` | earlier captures (EPIC): first-person thermal, base, bunker, vehicles, game HUD |
| `docs/visual_tests.json` | observations of the captured screenshots (used by the workbook) |
