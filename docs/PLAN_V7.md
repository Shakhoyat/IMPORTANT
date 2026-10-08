# ThermalSense v7: game feel, ray tracing, frame-by-frame graphics explorer (+ finish v6)

## Context
User request (2026-10-07, while v6 integration was pending): "a damn game-like feel, I want to stand out":
1. First-person view from a soldier's eyes; soldiers, rovers, UAVs etc. all controllable.
2. A free-moving camera from which you can jump straight into any moving object's POV.
3. Real ray tracing, reverse (light / photon) ray tracing and related core techniques.
4. An intuitive, step-by-step look at how a frame is made: frame-by-frame generation, colour interpolation,
   reflection, and the other headline effects, explained with core computer-graphics and image-processing concepts.
5. Always render on the remote RTX 4070 (WSL2: Mesa d3d12, NVIDIA adapter; `preferWslGpu()` in HeadlessGL.cpp does this
   automatically; logs must say `D3D12 (NVIDIA GeForce RTX 4070)` and EPIC).

Starting state: v6 code complete, 219/219 tests pass (g++). Existing things to build on, not rebuild: walk camera (`C`),
possession of every v4 controllable (`5`-`0`, `[` `]`, Backspace), soldier possession (F3, third / first person),
SSR (F7), flat / Gouraud / Phong (`F` `G` `P`), Z-buffer + primitive views (`V`), x-ray wireframe (`X`), exploded view (`E`),
LabStage (primitive test stage rendered by the real renderer), CaptureLab lab_* figures.

## Environment (all agents)
- No sudo. Repo is vscode-owned. Compiler: plain g++ (no zcxx). Build in your own dirs:
  `make -j32 HEADLESS=1 OBJ=build/obj-<wp> TESTS=build/tests-<wp> APP=build/ts-<wp> build/tests-<wp> build/ts-<wp>`
  then `./build/tests-<wp>` (all tests must pass). Sources are globbed as `src/*/*.cpp` (ONE directory level).
- Headless GPU renders: `./build/ts-<wp> --headless --capture DIR --only <shots>` (RTX via d3d12, EPIC). The machine is
  shared by 4 agents: keep render sizes modest for checks (e.g. `--size 1280x720`), no long runs without need.
- uv, tectonic, ffmpeg, ffprobe: `~/.local/bin` (add to PATH).
- Geometry: primitives only (cube, sphere, cylinder, cone). C++17, -Wall -Wextra clean, match surrounding style.
- Library defaults must keep v6 behaviour: never change what an existing test asserts.
- Progress notes: `docs/v7_notes/wp-<x>.md` (power-outage safe). Do not commit.

## Key map for v7 (free keys only; everything else is taken, see README "Controls")
| Key | Owner | Action |
|-----|-------|--------|
| `F4` | G | open / close the **POV picker** (live list of every moving / controllable object, sorted by distance) |
| `Home` | G | free camera (fly) from the current view; `End` back to the previous POV |
| `Insert` / `Delete` | G | jump to next / previous moving object's POV (smooth camera blend) |
| middle mouse | G | jump into the object under the cursor (ray pick) |
| `;` | H | render mode: raster → hybrid RT (raster + ray-traced shadows / reflections) → Whitted RT → path tracing (progressive) → light tracing (reverse) |
| `'` | H | ray debugger: click a pixel → its ray tree (primary, shadow, reflection, refraction, bounce) drawn in 3D |
| `` ` `` | I | **Frame Explorer** (modal): build one frame stage by stage; its own keys inside the mode |
| `F11` | G | game mode: minimal game HUD, crosshair, cinematic transitions on/off |

## Work packages (parallel subagents, disjoint file ownership)

**WP-G Game feel: free camera, jump-to-any-POV, first person everything** — owns Interactive.cpp, App.{h,cpp},
AppHud.cpp, scene/Camera.{h,cpp}, scene/Control.{h,cpp}, scene/HudMath.{h,cpp}, new src/app/CameraRig.{h,cpp},
rover possession (objects/Rover.*, the intruder part of Scene.{h,cpp}), tests/test_game.cpp (T240-T259).
- Unified `CameraRig`: modes Free(fly), Orbit, Chase(3rd person), FirstPerson (eye socket node on each object), Gimbal
  (UAV camera), Cockpit; critically damped spring blend (position + quaternion slerp, ~0.6 s) between any two.
- Every moving object gets a POV: soldiers (head node, head bob from the gait, weapon in view), rovers (bumper cam),
  UAVs (gimbal + belly), helicopter cockpit, tank commander/gunner, AA gun, MLRS, missiles/rounds in flight (follow-cam
  until impact, then hold), gate barrier/doors excluded (not moving objects).
- Control in any POV: soldiers (WASD, mouse look, Shift run, Ctrl crouch, jump if cheap), rovers (drive; possessing an
  intruder pauses its AI and the UAVs still hunt it: the player can try to sneak!), UAVs and v4 vehicles as today.
- POV picker overlay (F4): list with icon/name/type/distance/speed, arrow keys + Enter, also mouse click; Insert/Delete
  cycle; middle-click ray pick. Free camera with mouse look, WASD, Q/E-free alternatives (PgUp/PgDn), speed on wheel.
- Game feel: crosshair + hit marker on firing objects, subtle camera shake on blasts near the viewer, FOV kick on boost,
  motion-blur-free sprint feel via FOV, footstep audio already exists (wire to possessed soldier), letterbox transition
  on jump, objective/compass strip in game mode (F11), damage/alert vignette when a rover is being designated.
- Keep v4/v5/v6 key behaviour; H and I add small key cases in Interactive.cpp inside clearly marked
  `// v7 WP-H` / `// v7 WP-I` blocks - keep those blocks intact.
- Tests: rig blend continuity, every POV source valid, picker ordering, possession round trip for soldier/rover/UAV,
  free-cam input, pick ray hits the right object.

**WP-H Ray tracing (forward + reverse)** — owns new src/raytrace/*.{h,cpp} (CPU reference), new
src/rendering/RayTracerGL.{h,cpp} + src/rendering/ShadersRT.cpp (GPU), Renderer.{h,cpp} + RendererFrame.cpp hooks,
tests/test_raytrace.cpp (T260-T279).
- Scene → analytic primitive list (sphere, oriented box, capped cylinder, cone; the scene is built only from these so
  intersections are exact, no triangles) + SAH BVH, flattened to SSBO / texture buffer (GL 4.3+ compute when available;
  RTX via d3d12 gives GL 4.6). Static BVH cached; dynamic objects refit per frame.
- Modes: (1) hybrid: raster G-buffer + ray-traced hard/soft shadows and mirror/glossy reflections (compare with SSR);
  (2) Whitted recursive RT (reflection, refraction with Fresnel/Snell for glass/water, shadow rays); (3) path tracer
  (progressive accumulation while the camera is still, cosine/GGX importance sampling, NEE, Russian roulette, simple
  temporal reset on motion, À-trous or bilateral denoise); (4) light tracing / photon mapping = "reverse ray tracing"
  (photons from sun/moon/searchlight, caustic photon map visualised, light-path splatting to the camera).
- Ray debugger (`'`): CPU tracer re-traces the clicked pixel and returns the ray tree; Renderer draws it as lines with
  labels (type, depth, t, contribution).
- CPU reference path (same intersectors) for tests and for report figures (`raytrace_*` captures via a small hook the
  CAP agent wires, or a capture function in your own file).
- Tests: each intersector vs analytic cases, BVH == brute force on random rays, Fresnel/Snell, energy conservation of
  the path tracer on a furnace test, light tracing ≈ path tracing on a small scene, determinism with a seed.

**WP-I Frame Explorer (frame by frame, colour interpolation, reflection, image processing)** — owns new
src/rendering/FrameExplorer.{h,cpp} + src/rendering/ShadersExplorer.cpp, RendererLab.cpp, LabStage.h,
CaptureLab.{h,cpp}, tests/test_explorer.cpp (T280-T299).
- `` ` `` freezes the current frame and replays it stage by stage, each stage shown full screen with a caption,
  the key formula and a magnifier: (1) scene graph → world matrices, (2) vertex transform model→world→view→clip
  (show frustum, clip, perspective divide, NDC, viewport), (3) primitive assembly + back-face culling,
  (4) rasterisation (pixel grid zoom: coverage, edge functions, barycentric weights), (5) **colour interpolation**
  (barycentric, perspective-correct vs affine side by side, Gouraud vs Phong), (6) depth test (Z-buffer view +
  overdraw heat map), (7) G-buffer channels (albedo, normal, roughness/metal, depth), (8) shadow map (light view +
  PCF), (9) lighting (diffuse / specular / ambient terms separately, Blinn-Phong vs PBR), (10) **reflection**
  (mirror law, SSR march visualised, fallback to WP-H ray-traced reflection when available), (11) SSAO, (12) fog /
  volumetrics, (13) bloom (threshold, mip chain, blur kernels), (14) tone mapping + exposure (curve plotted),
  (15) TAA (jitter pattern, history blend), (16) final + thermal/CV view (sensor noise, threshold, blobs).
- Stage stepping: left/right arrows, number of stage on screen, "progressive reveal" scanline animation of the
  raster stage, split-screen wipe between consecutive stages, pixel inspector (hover: value in every buffer).
- Image-processing panel: convolution kernels on the frame (box, Gaussian, Sobel, Laplacian, sharpen), histogram,
  gamma, sampling/aliasing (nearest vs bilinear vs mipmap), all on the real frame.
- Captures for the report: `explorer_stage_XX` montage + per-stage images (functions in CaptureLab; CAP wires names).
- Tests: stage list complete, barycentric/perspective-correct maths vs CPU reference, kernels sum/normalisation,
  tone curve monotonic, inspector reads the right buffer values.

**WP-CAP finish v6 (integration, verification, captures)** — owns Capture.cpp, scene/Director.{h,cpp}, Record.cpp,
DocExport.{h,cpp}, tools/* (except wb_v6 changes are fine), tests/test_app.cpp.
- Verification steps 2 and 4 of PLAN_V6 (night regression vs v5 figures, trace determinism / seed / day rules).
- All v6 captures into report/figures_v6 on the RTX at EPIC, look at every image, fix capture/director issues.
- `make docs`-equivalent with g++ (exporter + tests JSON + workbook), verify_workbook PASS.
- Later (after G/H/I land): add v7 shots + v7 director shots (POV jumps, RT modes, explorer) and record the video.

**WP-F report / slides / README** (supervisor, last): sections for v6 (s18-s22) and v7 (s23 game camera, s24 ray
tracing, s25 frame explorer), workbook sheets for v7, slides, README controls.

## Status (live)
| Step | Owner | State |
|------|-------|-------|
| v7 plan, RTX auto-select, build green 219/219 | supervisor | done 2026-10-07 |
| WP-G game feel | agent G | DONE (T240-T258; game_* shots via App::captureGame; audio wired; CAP captures them) |
| WP-H ray tracing | agent H | DONE (5 modes + ray debugger, T260-T279); follow-up DONE (mirror misses use skyDome + fog, foliage alpha-test kFoliage, GPU grass tint, ; and ' in help, T278 ext; 289/289) |
| WP-I frame explorer | agent I | DONE (16 stages, T280-T299); polish DONE (probe centroid, montage bands, progress panel, open 5.7->4.9 s); RT reflection compare DONE (stage 10 SSR|RT split, RT-SSR diff, probe RT hit, T293 ext; 289/289) |
| WP-CAP v6 verification + captures | agent CAP | DONE (traces deterministic, 0 rule violations; figures_v6 18/18; figures_v7 game/hero/explorer; workbook PASS, 0 pending) |
| WP-J playable builds (Windows exe via cross-compile, Linux/WSLg GLFW without sudo) | agent J | DONE (tools/build_windows.sh, tools/run_wsl.sh; rebuild both at the end; notes wp-j.md) |
| WP-M hero model + material detail (objects/*, not Rover) | agent M | pass 1 DONE (+279 parts, 289/289); pass 2 soldier DONE (75 leaves, armIK rifle hold, T201 cap 75, scene 5414) |
| WP-L game audio (src/audio/*, T300-T309) | agent L | DONE (268/268; WAVs build/audio_v7; wired; follow-up rifle/impact/cockpit engine T309b, 289/289) |
| WP-P visual polish (cloud block artifacts, HUD overlaps, rifle viewmodel) | agent P | DONE (integer lattice hash for valueNoise; volumetric-looking daySky; Hud::flush; 45-part viewmodel) |
| WP-Q performance (raster EPIC ~150 ms on WSL d3d12; target 60 fps native 1080p) | agent Q | DONE (EPIC native 1080p 72.9 fps, WSLg 33 fps; wp-q.md) |
| CAP round 2: raytrace_* shots, re-captures, v7 director cut (video after Q) | agent CAP | running |
| Integration, video | supervisor | todo |
| WP-F1a report v6 sections s18-s22 (+ s01/s07/s11/s13/sA/sB edits) | agent F1 | DONE (113 pp, 23.2 MB, 0 undefined refs; s12 limitations pointed to s22; open: soldier_exploded not exploded, arm IK has no test, about 2 MB headroom for v7) |
| WP-F report v7 sections, workbook v7, slides, README | supervisor / agent | todo (after H) |
| Final checks, Windows exe | supervisor | todo |

### Log
- 2026-10-07 ~22:00: all 7 agents stopped by the API session limit mid-work (partial files in tree); resumed with
  context after the reset. M had no notes yet (asked to write wp-m.md first).
- WP-J done: zig cross-compiled static ThermalSense.exe runs natively on the RTX (GL 3.3 NVIDIA, EPIC); WSLg window via
  sysroot GLFW. Final step: rerun tools/build_windows.sh + tools/run_wsl.sh --setup. Fix clang warning Combat.h:105 burnAcc_.
- Supervisor: Helicopter::camera(true) eye moved to the canopy front (CAP); soldiers added to the explode list. 289/289 (build/obj-s7; NB build/obj-sup was a stale zig dir from Oct 2).
- Remaining after H: I's RT reflection compare, re-capture soldier_front/soldier_exploded/game_heli_cockpit/iso_* , v7 director + video, report/slides/README, Windows exe + WSL rebuild.
- H landed; CAP told to re-shoot explorer_reflection_ssr_vs_rt / _rt_difference (the RT half shows speckled cyan pine foliage now).
- F1 landed (v6 report). Q and CAP resumed after the session limit. W (workbook v7 sheets) and S (README v7 + slides) launched. soldier_exploded: soldiers are already in Scene::updateWorld's composite list, so CAP's re-capture should fix the figure; update the s22 caption and audit I7 after that.
- S landed (README v7 and a 22-slide deck via tools/build_slides.py, 11.8 MB; demo slide waits for report/demo_v7.mp4). Main fixed S's open items: the windowed context now asks for GL 4.6, then 4.3, then 3.3 (the Windows exe had 3.3, so RT fell back to the CPU), and `` ` `` was added to helpLines/keyBindingsV7. 290/290.
- CAP round 2 landed: rt figures (day and night), re-captures (soldier_exploded now explodes and is fitted), explorer re-shoot, v7 director cut of 150 s in 23 segments (T310), smoke record OK, tools/v7_jpegs.py (1400 px, q80). Raster is 16.1 ms at 1280x720. Next: the final video after Q.
- W landed: workbook sheets 24-27 (Ray Tracing, Frame Explorer, Game Camera & Controls, v7 Tests), verify PASS, snapshots in report/sheets. gen_tables now covers T310. Open: re-shoot raytrace_perf after Q and rebuild the workbook; run sheet_figs.py when the v7 report sections are written.
- Q landed: EPIC at native 1080p went from 9.2 to 72.9 fps, and at WSLg 1080p from 4.2 to 33 fps (instancing, grass rebuild bug, async UAV readback, start-up governor); PSNR 72-75 dB; 290/290. README perf section rewritten with the RTX table.
- Main rebuilt ThermalSense.exe and ./ThermalSense. Windows tests-win.exe 290/290 (T167 needs a ./build dir). Native exe: OpenGL 4.6.0 NVIDIA, EPIC about 93 fps in a short benchmark. CAP round 3 running: final demo_v7.mp4, raytrace_perf re-shoot, workbook rebuild, slides with the video. Rebuild the exe again if CAP changes code.

## v7.1 user requests (2026-10-08)
User: "add 3rd person view with first person view & add an intuitive guideline to explore like click this to move to uav pov & some soldier's pov. I want free movement access along the entire project like open world games. the game currently only shows the uav screen I want both uavs view. The uavs shouldn't patrol in day time. The soldiers on patrol increase in daytime. In daytime I see some anomaly related to sudden mirror like reflections, fix them like a real game. I want everything controllable from day to night shift then getting into some random soldier's pov etc."

| WP | Scope | Owner files | Status |
|---|---|---|---|
| WP-R daytime reflections | find and fix the sudden mirror-like reflections by day (SSR / specular / material reflectivity / TAA), make them behave like a real game | src/rendering shaders + Renderer*, Material.h, object reflectivity values | running |
| WP-D day/night operations | UAVs docked by day (land at dawn, take off at dusk, visible on pads), more soldiers patrolling by day (day shift), Scene::uavAirborne(i) for UI, v6 rules/tests updated | src/ai, src/scene/{Garrison,Mission,Scene,Combat}, tests | running |
| WP-T third person and open world | proper third-person over-the-shoulder for soldiers and vehicles (V toggles 1st/3rd), free movement across the whole map (terrain-following, collisions, sprint, jump), seamless enter/leave | src/scene/{CameraRig,Pov,Control,WalkSpace,Camera}, src/app/AppGame.cpp, movement in Interactive.cpp | running |
| WP-U guide UI and dual UAV feeds | clickable guide / quick-travel panel (UAV-1, UAV-2, random soldier, free camera, time of day dawn/noon/dusk/night + slider, render mode, Frame Explorer), both UAV feeds side by side, docked-UAV state | src/app/AppHud.cpp, src/rendering/Hud*, src/scene/HudMath*, mouse/menu in Interactive.cpp | running |
Then: CAP re-captures and the final video, workbook rebuild, slides, report v7 sections (s23-s25 including v7.1), exe/WSL rebuild.
- CAP round 3: the preview video (150 s, not final) is in build/cap-runs/demo_v7_preview.mp4 and looked fine; new raytrace_perf (raster 11.7, hybrid 20.3, Whitted 25.7, path 21.9, light 39.0 ms by day); workbook verify PASS (wb_v7 div/0 guard). Main updated the slide RT_TIMES and the README render-mode table; deck rebuilt (22 slides, 0 overflow). Final video after v7.1 (R/D/T/U).
