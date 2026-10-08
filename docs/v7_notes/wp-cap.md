# WP-CAP notes: finish v6 integration (verification, captures, docs data)

Status: started 2026-10-07 (power-outage log; newest at the bottom).

## Progress
- Read PLAN_V6 / PLAN_V7 / wp-e / wp-f2 / BRIEF.
- 21:46 snapshot of the tree (src include tests Makefile tools) in build/snap-cap (nobody had edited since PLAN_V7);
  binaries build/snap-cap/{ts-cap,tests-cap,export-cap}; long runs use a copy build/cap-runs/ts-run1 (launch.sh).
  Run every binary from the repo root (T167 writes build/test_audio_tmp.wav: fails when run from build/snap-cap).
- GPU check: "D3D12 (NVIDIA GeForce RTX 4070)", "Quality preset: EPIC". Cost: ~3.5-4 s wall per simulated second
  (trace, CPU bound in the d3d12 readback) -> a 600 s trace is ~40 min.
- Capture.cpp traceMode: new FILE.events.csv (every event + synthetic Spawn rows with hour / phase / waves_allowed,
  RoundLaunched with the track's hits/state, EnemyDestroyed with the step's strongest blast) and a 'rules:' summary
  line (spawns by day, withdrawals outside dawn, kills without a UAV round blast, rounds without a confirmed track).
- DocExport.cpp: char id[8] -> [16] (-Wformat-truncation warning). Build is warning-free except Scene.cpp:719
  (-Wsequence-point on hash01(seed++) inside a braced Vec3{...}: well defined in C++17 list-init, but GCC warns;
  Scene.cpp is G's -> supervisor).
- Docs data: export-cap -> build/scene_export.json (schema 5, v6: 97 grid rows, 8 soldiers, 11 sensor rows);
  tests-cap -> build/test_results.json: 219/219 PASS. build_workbook OK; verify_workbook RESULT: PASS (sheet 21 97/97,
  sheet 22 8 soldiers, sheet 23 50/50 v6 tests, sheets 21-23 0 'pending:'). Whole workbook: 12 literal 'pending:'
  cells, all in 17 Audit col I (evidence figures): 7 v4 shots looked up in figures_v4 (moved to figures_v5 in v5),
  5 v6 figures (day_overview, sky_noon, soldier_closeup, plan_routes, lab_cv_noise) not captured yet.
  Fix: wb_audit.py resolves report/figures_v4/X to report/figures_v5/X when only the v5 copy exists (as \picfour).
- 21:52 launched: trace42a / trace42b (600 s, --time cycle --seed 42), trace43 (--seed 43), night_v5 (v5 shot list,
  --time night --seed 0 --no-garrison, EPIC) -> build/cap-runs/.

## Verification results (2026-10-07 22:2x, RTX 4070 / EPIC, snapshot binary)
### PLAN_V6 step 4: traces (600 s, --time cycle, start 23:00 auto, 280 s cycle)
- seed 42 run A == run B byte for byte (trace CSV md5 955da30e..., events md5 b11f5dc8...). seed 43 differs in all
  2400 sample rows (md5 eb3282d9...). -> deterministic per seed, seed changes the run.
- Phase events (both seeds identical clock): Dawn 89.40 s (05:43), DayBreak 102.35 (06:26), Dusk 215.38 (17:36),
  NightFalls 228.38 (18:19), Dawn 379.98, DayBreak 392.93, Dusk 505.97, NightFalls 518.97.
- seed 42: 22 spawns, all NIGHT (first 18:19:30 after NightFalls, 1 step later); 57 rounds, 39 hits, 9 kills (7
  combined), 8 breaches (integrity 20 %), wave 5; 2 withdrawals (379.98 s = the Dawn step; 179.38 s = 14:04, exactly
  90.0 s after Dawn = kRetreatTimeout: retreat started at dawn, rover could not reach an exit cell); 754/72000
  frames dropped (1.05 %).
- seed 43: 11 spawns (NIGHT), 20 rounds, 13 hits, 4 kills (3 combined), 5 breaches, wave 3, withdrawals at 89.40 s
  (Dawn step) and 469.97 s (= Dawn + 90 s timeout); 706/72000 dropped.
- Rules check (traceMode 'rules:' line + events.csv): 0 spawns by day, 0 withdrawals outside dawn/day, every kill in
  a step with a UAV round blast (>= 0.9 power), every RoundLaunched aimed at a confirmed shared-tracker track (min
  51 / 77 vision hits at launch; 36/57 and 9/20 triangulated), 0 cannon / gatling fire. All combat events NIGHT.
  Kills only from vision: rounds come only from Squad LaunchOrders (aim = tracker estimate), the tracker is fed only
  by App::step submitVision (pixels -> SensorModel -> detectAll Target blobs); Scene's v4 'det' fallback only fires
  when BOTH UAV frames drop in a step, and it is also pixel-derived (UAV 0's last noisy image).
- Note for the supervisor (balance, not a rule break): breaches dominate (seed 42: 8 breaches vs 9 kills).
### PLAN_V6 step 2: night regression (--time night --seed 0 --no-garrison, v5 shot list, 39/39 written)
- Compared with report/figures_v5 (scratchpad cmp.py: MAE 0-255 / PSNR). Pre-script static shots match:
  bunker_door 0.39 / 45.5 dB, camp_mlrs 0.71, tank_closeup 1.16, motor_pool 1.78, aa_gun_burst 1.76, heli_takeoff
  1.88, bunker_ramp 2.29, radar 2.92, base_gate 3.07, base_overview 3.80 (smoke / fire positions), cic 5.84,
  under_nets 5.50, helipad 8.57, camp_sam 12.4 (v5 frame had a burning wreck at the right: v5's scripted t=1 s strike).
  bunker_tunnel 32.0: same geometry, brighter (adapted log-lum -5.88 vs -4.82, auto-exposure on a darker tunnel).
- Event-driven shots differ as expected (different game: real sensor noise changes when / where the squad strikes):
  median MAE over all 39 = 8.1, median PSNR 22.2 dB. explosion_early (MAE 73) / hit_marker (83): v5's frame was the
  removed capture-only scripted single blast; v6's first kill is a real combined strike (2 rounds, value 150), so the
  0.15 s frame is inside a double flash. HUD differs by design (v6 clock panel, NETD/drop line, scene clock 23:00).
- Visual check (montages): terrain, camp, lighting, sky, searchlights, fog identical in look. Verdict: night preset
  reproduces v5; no renderer regression found.

## v6 captures
- Pass A (22:00, build/cap-runs/v6a, seed 1, EPIC, default sizes): 18/18 written. Review (Read on every image):
  OK: plan_routes, lab_cv_noise, dawn_withdraw (DAWN + INTRUDER WITHDRAWS banners, 05:47), day_overview,
  day_patrol_spline, soldier_possess_tp/fp, day_thermal_fpv_persons (PERS bracket present but small: CV boxes the warm
  head only), dusk_shift_change (DUSK / SHIFT CHANGE banners). Problems -> Capture.cpp fixes:
  * night_searchlights: overview camera, beams invisible (UAVs tiny) -> UAV-1 chase (dist 11, pitch 8) after 6 s.
  * day_mechanic: mechanic WALKING between tank and helicopter (~45 s walks) -> new Trig::Status (Shot::waitStatus /
    waitCount): waits for "WORKING AT MBT-2". day_logistics: waits for "CARRYING CRATE".
  * soldier_closeup / soldier_axes / soldier_exploded showed the back -> yaw 340 / 310 / 330 (front).
  * sky_noon: lamp pole in the middle -> eye (-9, 1.8, 30), aim (-1, 14, -10).
  * sky_sunset: no sun in frame, radar cabinet filled the right third -> Shot::aimSun: eye (30, 10, 6) looking along
    DayCycle::sunDirection(17.75) (aim offset (0,-6,0)).
  * bunker_offduty: corridor only, soldiers still walking at 21:00 -> waits until 3 soldiers are "OFF DUTY" (seated),
    free camera on the bunk room's east wall (-1.95, floor+1.7, 0.7) -> (-4.6, floor+0.5, 1.3), fov 74.
  * captureMode now stops after the last shot an --only list names (the night run simulated all v6 shots for nothing).
- NOT fixable in my files (supervisor):
  * soldier_exploded: soldiers are not exploded - Scene::updateWorld (Scene.cpp:386) applies 'explode' only to the bunker,
    "Autonomous UAV" and "Target Rover" roots; add soldier roots (object names "Soldier N").
  * Day sky clouds: hard straight-edged seams / sheared blocks in the cloud layer (sky_noon full res, also day_overview);
    daySky() in ShadersPost.cpp samples cloudDensity per fragment, so suspect the value-noise hash precision at large
    plane coordinates (t up to 4000 m near the horizon) or the TAA / SSR passes on the sky. Owner: renderer (H / supervisor).
- --record smoke test (10 s, seed 1, EPIC): D3D12 RTX 4070; h264 1920x1080 30 fps + AAC 48 kHz stereo, both 10.0 s;
  3.3 frames/s with another capture running -> 120 s video ~ 10-12 min. Frame at 6 s: night wave shot, HUD ok.
- Pass B (v6b) + test runs: night_searchlights tried dist 7 / pitch -6 (UAV visible, beam not) -> final dist 10,
  yaw 200, pitch 32, settle 10 s (cone + lit ground pool with a soldier in it). soldier_closeup yaw 340 had a lamp pole
  over the left quarter -> yaw 20, dist 2.4.
- Pass C (v6c, 23:2x, final code) -> copied to report/figures_v6 (18 PNG + capture_log, captions.json, cv_noise.json).
  Deterministic: 14 shots bit-identical to pass B, night_searchlights bit-identical to the test run.
  Final status (every image read): ALL OK -
  night_searchlights OK (cone + pool) | plan_routes OK | lab_cv_noise OK | dawn_withdraw OK (05:47, banners) |
  day_overview OK (cloud seams, renderer) | sky_noon OK (cloud seams, renderer) | day_patrol_spline OK (routes faint) |
  day_mechanic OK (wrench at MBT-2) | day_logistics OK (crate carried) | soldier_closeup OK (front) | soldier_axes OK
  (front, joint frames) | soldier_exploded PARTIAL (front, primitive colours, NOT exploded: Scene.cpp) |
  soldier_possess_tp / fp OK | day_thermal_fpv_persons OK (PERS bracket, head only) | sky_sunset OK (sun on the
  horizon) | dusk_shift_change OK | bunker_offduty OK (3 seated soldiers).
- Shared tree at 23:18: my files compile warning-free; tests 268/268 pass (v7 tests included); app link briefly
  broken by H's RayTracerGL (Renderer.cpp:105 incomplete type), builds again at 23:3x.

## v7 additions (supervisor, 2026-10-08)
- DocExport: "v7": {"keys": HudMath keyBindingsV7()} after "v6"; schema_version 5 -> 6. Shared-tree exporter run:
  schema 6, 9 v7 keys, 7 v6 keys, 97 grid rows (build/cap-runs/export_v7.json).
- game_* (AppGame.cpp captureGame, now mine): pass g7a (shared tree build, RTX/EPIC, 1280x720 default) 12/12 written.
  Review: rover_chase, uav2_gimbal, missile_impact, picker, soldier_fp, soldier_fire, tank_commander, free_fly OK;
  jump_blend OK (letterbox mid-blend; the rifle viewmodel is already drawn while the eye is still 8 m up: G's
  viewmodel should fade in at blend end); rover_bumper = wall of grass (hood cam 0.42 m in the meadow) -> mast cam
  (PovView::Alt); missile_follow blown out by the motor glow (chase cam looks through it) -> nose cam
  (PovView::First); heli_cockpit: cabin shell over the left half (eye (-0.3, 0.5, 0.55) is inside the Cabin ellipsoid,
  0.23 m from its wall).
  Heli fix, tested in build/snap-cap2 only (Helicopter.cpp was being edited by someone at 23:17, not mine):
    Helicopter::camera(true): eye {0.0f, 0.55f, 1.15f}, target {0.0f, 0.43f, 6.0f} (inside the canopy glass, outside
    the cabin shell) -> clear forward view. Published game_heli_cockpit.png comes from that build -> SUPERVISOR: apply.
  Pass g7b (snap-cap2 = shared tree 00:0x + heli patch) -> report/figures_v7/game_*.png (12), log capture_log_game.txt.
- Hero close-ups (Capture.cpp): iso_tank, iso_heli, iso_aa, iso_sam, iso_mlrs, iso_uav, soldier_front (last), 10:30,
  isolate (no nets), Shot::hero: bounds from the live leaves (one instance: topmost ancestor with the same object
  name; parts parked below y -6 m ignored), front three-quarter on the sunny side (+-35 deg, pitch 16; soldier 20 / 8),
  distance by bisection so the projected parts span 70 % of the frame (larger axis), box centred, exposure x2.
  An --only list of hero shots runs only those (45 s). 1920x1080. hero1 too small (bounding sphere fit), hero2 dark,
  hero3 OK -> report/figures_v7 (7 PNG, capture_log_hero.txt).
- Frame Explorer wired: lab shot {"explorer","explorer"} -> lab::explorer(ctx). To capture near the end.
- Frame Explorer captured (shared tree, RTX/EPIC): 26 PNG (explorer_stage_01..16, montage 1600x905, wipe, inspector,
  raster_scan, interp x2, reflection_mask, imagelab x2, final_cv) + explorer_stages.json -> report/figures_v7 (also
  build/figdata_out/explorer_stages.json for the workbook; captions_explorer.json, capture_log_explorer.txt). All read:
  every panel has content, no black frames.
- Final docs data (shared tree): export-cap -> build/scene_export.json (schema 6, 5414 parts), tests-cap -> build/
  test_results.json 289/289 PASS; build_workbook OK; verify_workbook RESULT: PASS (sheet 21 97/97, sheet 22 8 soldiers,
  sheet 23 50/50, cover tests 314/314 incl. figdata rows); 0 literal 'pending:' cells (figures_v6 present, v4 evidence
  resolved to figures_v5). Audit 'pending' count 1 = U6-8 (v6 video, not recorded yet).
- xlsx_snapshot: sheet 21 showed #ERR (no ASIN in the snapshot evaluator) -> added ASIN, ACOS, ATAN2, PI, ROWS;
  full run 63 images; evaluator over all 105873 formula cells: 0 errors.
- PLAN_V6 status row + integration log updated.

## Open / for the supervisor
1. Helicopter.cpp (not mine): first-person eye {0.0f, 0.55f, 1.15f}, target {0.0f, 0.43f, 6.0f} (tested; the published
   game_heli_cockpit.png used it). Without it the cabin shell covers the left half.
2. Scene.cpp updateWorld: add soldier roots to the 'composite' explode list (soldier_exploded is not exploded).
3. Renderer / sky (H): cloud layer shows straight-edged seams (day sky_noon / day_overview, also at night).
4. G viewmodel: rifle drawn during the jump blend while the eye is still high (game_jump_blend).
5. Scene.cpp:719 -Wsequence-point warning (hash01(seed++) x3 in a braced Vec3: defined in C++17, GCC warns).
   CaptureRT.cpp:49 unused function 'crop' warning (H).
6. Game balance: 600 s seed 42 = 9 kills vs 8 breaches (integrity 20 %).
7. Video not recorded (by request). --record works: h264 1080p30 + AAC 48 kHz, ~3.3 frames/s -> 120 s ~ 10-12 min.
8. build/cap-runs (843 MB staging, git-ignored) and build/snap-cap{,2} can be deleted after review.

## Round 3 (supervisor): RT figures, re-captures after heli / explode fixes, v7 director cut (~150 s), 20 s RT+explorer smoke
- RT figures wired: Capture.cpp lab kind "rt" -> rtcap::figure(Ctx{scene, renderer, dir, 1280, 720, Epic, hour, log});
  shots raytrace_{modes,tree,ssr,photons,perf} (16:30) and *_night (23:00, written to DIR/rt_night/). Day set 2 min,
  night set ~2 min. All images read: OK (5 modes, tree, SSR vs RT reflections, photons). -> report/figures_v7 (+ rt_night/,
  captions_raytrace.json, capture_log_raytrace.txt). Perf (1280x720, shared GPU): raster 157.7 ms, hybrid 169.4, Whitted
  25.8 (960x540 internal), path 21.9 (640x360), light 38.8 -> raster slower than the RT modes: flag for Q.
- Re-captures after the supervisor's heli eye + soldier explode fixes: hero4 (7, OK), game_soldier_fp / _fire (P's
  scoped rifle, OK), game_heli_cockpit (clear canopy view, OK), game_jump_blend: was a finished soldier view because
  nothing had been rendered before the jump (shownPose_ stale when earlier shots are not requested) -> captureGame
  renders the orbit frame before jumpToPov -> mid-blend frame OK (rifle viewmodel during the blend + a track marker in
  the letterbox bar remain: G).
- v7 director cut (Director.{h,cpp}): DirectorCut::V7, 23 segments / 150 s (kSegmentsV7: name, length, V7Act, param,
  subtitle), hourV7 (23:00 action, dawn 5.2->7.4, 10:30 heroes, 16:30 RT + Explorer, dusk 17.2->18.05, 19.5->20),
  shotV7 (v6 night / strike / dawn / dusk / finale cameras reused, hero orbits of the tank / heli / AA at their live
  positions, the RT showcase camera and the ray-debugger swing to the side view), directorRtCamera().
  V5 / V6 cuts unchanged (default argument still V6 for the K camera).
- Record.cpp: TS_DIRECTOR_CUT (v7 default | v6 | v5), TS_DIRECTOR_START, TS_DIRECTOR_SPEED; v7 actions: picker (free
  camera, togglePicker, pickerMove every 0.55 s), UAV-2 gimbal jump, round nose-cam jump as soon as a round flies,
  rover mast cam (rover nearest the squad's target track), sentry first person (takeControl, bursts, slow scan), RT
  modes 0..4 on the showcase props (mirror of CaptureRT Props), ray debugger pick of the glass ball + swing to the side,
  Frame Explorer (frozen once, stages 3 / 4 / 9 / 15, transitions, raster scan synced to the segment); titles per
  segment (top left, 3.5 s); audio listener from the POV pose in POV / possession modes. Script clock off in V7.
- tests/test_app.cpp: T310 v7 director cut (150 s, 23 segments, order, modes, stages, clock, cameras): PASS; 290/290.
  (TestKit.h block list: add "T310 v7 director cut (WP-CAP)" - supervisor.)
- Smoke record (TS_DIRECTOR_START=78 TS_DIRECTOR_SPEED=2.55, 20 s): h264 1080p30 + AAC 48 kHz, 20.0 s each; 600 frames
  in ~95 s (3-6 frames/s). Frames read: all 5 RT modes, ray tree, 4 Explorer stages OK. Fixed after it: doubled RT
  status line (renderer's overlay already prints it), my title over the Explorer's header (skipped there).
- 16:2x: rebuilt from the current tree (H + I follow-ups) -> RT day / night and explorer re-shoots (rt3, rt4, expl2).
- Re-shoots after H / I follow-ups (current tree): rt3 (day) / rt4 (night) / expl2 (31 incl. explorer_reflection_ssr_vs_rt,
  explorer_reflection_rt_difference, explorer_progress) -> report/figures_v7 (+ rt_night/); all read, OK. New perf
  (1280x720): raster 16.1 ms, hybrid 32.6, Whitted 77.0, path 30.0, light 51.4. explorer_stages.json -> build/figdata_out.
- v6 re-captures: soldier_closeup (M's model, OK), soldier_exploded (now exploded; hero-fitted after the explode
  offsets, OK) -> report/figures_v6.
- Cut previews (960x540, not the video): 0-78 s at x3, 21-39 s and 57-78 s at x1. Fixes: round ride armed in the last
  1.5 s of the UAV segment + re-armed whenever the camera is not on a round (two rounds ridden at x1; at x3 too little
  sim time for a launch); tank orbit raised to 2.5 m / 7.5 m over the low wall; heli orbit 7.5 m / 2 m. Rover mast cam
  sits in tall grass (sneaking rover; LASER DESIGNATED warning reads).
- tools/v7_jpegs.py: report/img/figures_v7/**.jpg at <= 1400 px, q80 (73 files, 9.9 MB total; the report pays only
  for what it includes; newer than the PNGs so prepare_images keeps them).
- Final (19:4x): tests 290/290 (T310 incl.), export schema 6, verify_workbook PASS (sheets 24-27 by WP-W ok; one
  transient FAIL while W was writing sheets 24-27 concurrently, PASS after a rebuild), 0 'pending:' cells.

## Round 3 (final video, perf re-shoot, workbook, slides)
- 21:17 fresh build (ts-final). Tests 290/290.
- Video (now a PREVIEW, supervisor change of plan: new features coming): --record 150, v7 cut, seed 1, EPIC, D3D12
  RTX 4070, ~10 frames/s (7.5 min). ffprobe: h264 1920x1080 30/1 150.000 s + aac 48000 Hz 2 ch 150.000 s, 270 MB
  (x264 crf 17). Moved out of report/ to build/cap-runs/demo_v7_preview.mp4 so build_slides does not embed it.
  Frames read (25): 4 night wave, 12 combined strike +150, 19 picker (18 objects), 25.5-32 two rounds ridden to impact
  (UAV-2), 33.5-38.5 rover mast cam (rover hides under a pine: branches, LASER DESIGNATED, then destroyed +150), 43
  sentry firing, 52 dawn 06:26, 60 tank hero, 85 hybrid, 94 path, 103 ray tree, 110 Explorer raster, 121 reflection,
  133 dusk 17:37, 145 finale 19:48 (wave 2). Tried a chase view for the rover segment (preview): also inside foliage
  (rovers hide in cover) -> kept the mast cam, code reverted (binary identical to the recording build).
- Audit U6-8: checks pass for the preview (h264 + AAC, day / night, soldiers, real CV: no scripted strikes, kills from
  vision tracks) but the row stays Pending (the final cut comes after the new features); C3 stays on demo_v5.mp4.
- raytrace_perf re-shoot (GPU not shared, ts-final) -> report/figures_v7/raytrace_perf.json + rt_night/:
  day: raster 11.68, hybrid 20.30 (RT 9.98), Whitted 25.68 (19.89, 960x540), path 21.88 (18.08, 640x360),
  light 38.96 (32.63); night: 12.69, 20.46 (9.47), 27.65 (21.72), 28.63 (23.98), 30.66 (26.05) ms/frame.
  v7_jpegs: 73 JPEGs, 9.9 MB.
- Workbook: export (schema 6) + tests JSON 290/290 + build_workbook; verify first FAILED (2 #DIV/0! on sheet 24: the
  new perf JSON reports raster internal [0, 0]) -> wb_v7.py ns / pixel formulas guarded (IF(pixels > 0, ..., "-"));
  "Run shown" now defaults to "CAP figure run, day" (one line). verify_workbook RESULT: PASS (315/315, sheets 21-27 ok,
  0 formula errors, 0 'pending:' cells, audit pending 1 = U6-8). xlsx_snapshot: 69 images.
- Slides (step 4) skipped per the change of plan; RT_TIMES in build_slides.py not updated yet (numbers above).
