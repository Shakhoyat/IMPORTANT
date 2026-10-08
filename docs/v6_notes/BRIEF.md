# v6 shared brief for all work-package agents (supervisor)

Read `docs/PLAN_V6.md` first: your WP section is the spec. Phase-0 contracts are already in place and compile:
`src/scene/DayCycle.h`, `src/math/Spline.h`, `src/scene/Gait.h`, `src/objects/Soldier.h`, `src/scene/Garrison.h`,
`src/vision/SensorModel.h` (stub .cpp files), `SceneOptions`/`deriveSeed` in `src/scene/Scene.h`, `Atmosphere` in
`src/rendering/Lighting.h`, new events in `src/scene/GameEvents.h`, `WorldContext` fields + `friendlyRadius()` in
`src/scene/Controllable.h`, `captureUavCamera(..., cameraOverride)` in `src/rendering/Renderer.h`.
You may extend a contract header you own; changing a header you do not own needs the supervisor (message "main").

## Rules
- Edit ONLY the files your WP owns (list in PLAN_V6.md). Other agents work in parallel on the other files.
- Build in your own directories (never the shared build/obj-headless or build/tests):
  `make -j16 HEADLESS=1 CXX=zcxx CC=zcc OBJ=build/obj-<wp> TESTS=build/tests-<wp> build/tests-<wp>`
  and run `./build/tests-<wp>` (all tests must pass: the 169 existing + yours). No GPU here: rendering code only compiles;
  headless EGL/llvmpipe renders are possible with `APP=build/ts-<wp>` + `--headless` if you need a visual check.
- Library defaults must keep v5 behaviour (SceneOptions{} = night, fixed seeds, legacy routes): never change what an
  existing test asserts; add new tests with your ID block (tests/TestKit.h header lists the v6 blocks).
- Code style: match the surrounding code (C++17, comment density, naming, -Wall -Wextra clean).
- Only primitives (cube, sphere, cylinder, cone/frustum via the cylinder generator) for any geometry.
- Soldiers are friendly personnel of the camp: they are never targets, have no damage model (blasts only make them
  duck). The UAVs engage only the unmanned intruder rovers, as in v5.
- Keep the workbook in mind: anything numeric the report/workbook should show must be reachable from the exporter
  (DocExport, WP-E) or a pure function; note such items in your notes file.
- Write progress notes to `docs/v6_notes/wp-<x>.md` (what is done, what is open) as you go (power-outage safety).
- Do not commit. Final message: files changed, tests added (ids, pass), open issues, anything the supervisor must wire.
