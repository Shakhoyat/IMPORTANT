# ThermalSense v4 — shared brief for every engineer (read fully before starting)

Repo: /home/vscode/IMPORTANT (C++17, OpenGL 3.3 core, no external models/textures). Read README.md,
docs/PLAN_V3.md, and the headers listed below. A supervisor integrates everything and owns the renderer core.

## Goal (user request)
Turn the night patrol compound into a realistic **military base** (Ghost-of-Tsushima-level night look, RTX 4070
target): the command bunker goes **underground** and is walkable (entrance ramp, blast doors, tunnel, rooms);
the UAV **shoots the intruder rovers** with missiles and they **blow up properly** (fireball, smoke, sparks,
debris, scorch, burning wreck); **at least 5 new user-controllable objects** with complex hierarchical motion;
the Excel workbook and the report get updated later from the program's own output.

## Hard project rules
- Geometry ONLY from the lab unit primitives via `Builder` (objects/Builder.h): Cube, Sphere, Cylinder,
  Cone (cylinder with top radius 0), BeamCone (frustum). Every part = one row "primitive, S, R(deg), T".
  No model or image files. Procedural shader patterns are allowed (Surface enum in rendering/Material.h,
  new ones: Corrugated 12, Hesco 13, Hazard 14, Screen 15 (glowing display, use emissive>0), Asphalt 16,
  Camo 17, SteelPlate 18 — already implemented in the shader).
- Every node: meaningful name, object, purpose; `animation` text if it moves (the workbook lists it).
- Practical lights = emissive parts with `asLight(material, intensity, range, coneDeg)`; the renderer uses the
  16 most relevant ones per view, so use them where they matter (don't add 80 lamps).
- Realistic scale in metres. Night scene: dark albedos (0.02-0.25 linear), roughness set explicitly.
- Deterministic (fixed seeds), no wall-clock time in simulation.

## Shared interfaces (written by the supervisor — do NOT change their existing members; you may ask for
additions in your progress note)
- src/objects/BaseLayout.h — the site plan: every coordinate (ramp, bunker footprint, helipad, radar, AA gun,
  motor pool, barracks, flagpole, depot, gates, perimeter radius, kBaseRadius = 36 terrain mesh).
- src/scene/Controllable.h — interface for controllable / animated objects (ControlInput, WorldContext,
  CameraHint, Controllable). Each object owns its node tree, sets local transforms in update().
- src/scene/EffectsData.h — Projectile, EffectParticle, DynamicLight, ScorchMark (combat -> renderer).
- src/scene/WalkSpace.h — floors / walls / indoor volumes for walking inside the bunker.

## Ownership (edit ONLY your files; everything else is read-only for you)
| Engineer | Files |
|---|---|
| A1 bunker | src/objects/Bunker.h/.cpp, src/scene/WalkSpace.cpp, src/objects/Terrain.h/.cpp, tests/test_bunker.cpp |
| A2 base | src/objects/Base.h/.cpp (new), src/objects/Environment.h/.cpp, src/objects/Structures.h/.cpp, src/objects/PropKit.h/.cpp, src/objects/Backdrop.h/.cpp, src/objects/Vegetation.h/.cpp, src/rendering/GroundMap.h/.cpp, src/rendering/Grass.cpp (extents only), tests/test_base.cpp |
| B1 ground vehicles | src/objects/Tank.h/.cpp, src/objects/AaGun.h/.cpp (new), tests/test_vehicles.cpp |
| B2 air + sensors | src/objects/Helicopter.h/.cpp, src/objects/Radar.h/.cpp (new), tests/test_air.cpp |
| C1 combat + scene | src/scene/Combat.h/.cpp (new), src/scene/Scene.h/.cpp, src/scene/HeatTrail.*, src/ai/*, src/objects/Uav.*, src/objects/Rover.*, src/vision/*, tests/test_combat.cpp, tests/test_main.cpp (only to keep old tests compiling) |
| C2 effects rendering | src/rendering/Effects.h/.cpp (new) |
| D app / camera / HUD | src/app/App.h, App.cpp, AppHud.cpp, Interactive.cpp, Capture.cpp, Record.cpp, src/main.cpp, src/scene/Camera.*, src/scene/Director.*, tests/test_app.cpp |
| supervisor | renderer core (Renderer*, Shader*, FrameBuffers, ShadersPost*), shared headers, Makefile, CMake, docs |

## Tests
New CPU-only tests go in your own tests/test_<area>.cpp using tests/TestKit.h:
```cpp
#include "TestKit.h"
using ts::testkit::record; using ts::testkit::fmt;
TS_TEST_GROUP(Bunker) { record("T60", "what is checked", "expected", fmt("observed %.2f", v), pass); }
```
ID ranges: T60-69 A1, T70-79 A2, T80-89 B1, T90-99 B2, T100-119 C1, T120-129 D. The test binary links only
CORE sources (no src/app, no src/rendering, no GL) — keep your simulation code out of rendering/.

## Build (never use the default dirs; never `make clean`; never git commit/stash/checkout)
No system g++: `zcxx`/`zcc` (clang via zig) and `make` are in ~/.local/bin. Use your own dirs, e.g. for A1:
`cd /home/vscode/IMPORTANT && make -j16 HEADLESS=1 CXX=zcxx CC=zcc OBJ=build/obj-a1 APP=build/ts-a1 TESTS=build/tests-a1 2>&1 | grep -E "error|warning:" | grep -v nullability`
then `./build/tests-a1 /tmp/claude-1000/a1-tests.json | tail -3`.
Other engineers edit the same tree at the same time. Keep YOUR files compiling at every save (write in small
compilable steps). If the build fails in a file you don't own, compile yours alone
(`zcxx -std=c++17 -O1 -Isrc -Iinclude -Wall -Wextra -c src/objects/X.cpp -o /tmp/claude-1000/x.o`) and retry
the full build ~10 min later; mention persistent breakage in your note.

## Seeing it (headless EGL, Mesa llvmpipe software GPU — slow, ~1-2 min per shot)
`./build/ts-a1 --headless --quality high --capture /tmp/claude-1000/a1-cap --only <shot>` and view PNGs with
the Read tool. Existing shots: grep `add(` in src/app/Capture.cpp (cine_establishing, cine_low_angle,
walk_bunker, walk_forest, ...). Your objects only appear once Scene builds them: the C1 engineer wires every
factory into Scene with `#if __has_include(...)` guards, so they show up automatically as soon as your
header exists and compiles. Until then (or to frame your object), you may write a throw-away harness in
/tmp/claude-1000/<you>/ — never add temporary code to files you don't own.
Judge from ground level / close up, not only overviews. GLSL errors print at start-up.

## Progress notes and report
After every milestone append to /home/vscode/IMPORTANT/docs/v4_notes/v4-<you>.md (in the repo, survives power cuts):
done / in progress / next / requests for the supervisor / API you expose.
Final report (short): files + public API, part counts, controls, tests (IDs, results), captures you looked at,
open issues. The report and workbook are written later from your notes — record exact numbers
(part counts, speeds, limits, timings) and the technique behind each motion.
