# WP-E notes: App / CLI / keys / HUD / director / capture / export

Status: in progress (power-outage log; newest at the bottom of "Progress").

## APIs used (confirmed by the supervisor)
- `Scene::dayCycle()` (DayCycle&), `Scene::garrison()` (const Garrison*, nullptr without garrison) -- detected through
  SFINAE in `src/scene/SceneApi.h` (`sa::dayCycle(scene)` returns nullptr until WP-C lands).
- `cv::detectAll(img, w, h, cfg, stats)` -> `std::vector<Measurement2D>` with `Measurement2D::cls` (`BlobClass`).
  Until WP-D lands, `sa::detectAll` falls back to `cv::detectTargets` (every blob is a target) via a template stand-in.

## CLI (main.cpp)
- `--seed N` (default: `mixRunSeed(random_device, steady_clock)`; capture / benchmark / trace / record default seed 1).
  Printed at start, shown in the HUD clock line and written as a `# seed=... time=...` header line of the trace CSV.
- `--time day|night|dusk|dawn|cycle|HH:MM` (day 12:00, night 23:00, dusk 17:45, dawn 06:00 manual; cycle = auto).
  Interactive default: cycle from 16:30. Capture / benchmark / trace default: night 23:00, manual. Record: the director
  forces the clock per shot (manual), warm-up at 23:00.
- `--cycle-seconds N`: length of one full auto day+night cycle (scales DayCycleConfig day/nightSecPerHour, default 280 s).
- `--no-garrison`: Scene without the 8 soldiers.

## Keys (Interactive.cpp, table data in HudMath `helpLines()` / `keyBindingsV6()`)
F2 auto/manual clock, `,` / `.` scrub (manual, held), `/` next phase, F3 next soldier (possess).

## DocExport JSON (build/scene_export.json) -- v6 keys (stable; the workbook reads them)
Top level: `"schema_version"` 4 -> 5 and the new object `"v6"` (after `"v5"`):
```
"v6": {
  "keys": [ {"keys", "action", "version"} ],        // v6 key table (report sB), HudMath keyBindingsV6()
  "help_lines": [ {"column", "text"} ],             // the F1 overlay, HudMath helpLines() (column 0 view, 1 control)
  "day_cycle": {
    "config": {"latitude_deg", "declination_deg", "day_sec_per_hour", "night_sec_per_hour", "twilight_slow",
               "scrub_hours_per_sec", "shadow_quant_deg", "cycle_seconds", "night_below_deg", "day_above_deg",
               "beams_below_deg", "sun_intensity", "day_exposure_key_log2", "solar_gain", "cloud_altitude_m",
               "cloud_coverage", "sunrise", "sunset"},
    "events": [{"hour", "event"}],                // phase crossings of one manual sweep 0..24 h (Dawn, DayBreak, Dusk, NightFalls)
    "grid": [ {                                    // 0..24 h every 0.25 h (97 rows)
      "hour", "clock", "phase", "elevation_deg", "azimuth_deg", "daylight", "key_is_sun",
      "key_intensity", "key_color": [r,g,b], "key_dir": [x,y,z],
      "ambient": [r,g,b], "ambient_luma", "sky_zenith": [r,g,b], "fog_color": [r,g,b],
      "fog_density", "vol_fog_density", "exposure_key_log2", "purkinje",
      "solar", "practical_scale", "beams_on", "waves_allowed", "cloud_shadow_mean" } ]
  },
  "garrison": {
    "count", "route_lengths_m": [...],
    "routes": [ {"index", "length_m", "closed", "control_points", "segments", "max_curvature", "min_clearance_m"} ],
    "soldiers": [ {"index", "name", "controllable_index", "roster_day", "roster_night", "position": [x,y,z],
                   "parts", "groups", "primitives": {...}, "friendly_radius", "status"} ],
    "joints": [ {"id", "name", "parent", "depth", "primitive", "animation"} ],   // soldier 1's node tree ("group" = joint)
    "gait": {"walk": {speed, stride, duty, lift, bob, arm_swing_deg, thigh, shin, hip_height, cadence_steps_per_min},
             "run": {...same keys},
             "cycle": [ {"phase", "hip_l", "knee_l", "ankle_l", "hip_r", "knee_r", "shoulder_l", "shoulder_r",
                         "elbow_l", "spine_yaw", "pelvis_y", "foot_x_l", "foot_y_l"} ]}   // walk, 0..1 step 1/32
  },
  "sensor": [ {"key", "value", "unit", "source"} ],   // SensorNoiseConfig defaults (netdDn, fpnGainSigma, fpnOffsetSigma,
                                                      // columnSigma, psf, jitterDeg, dropProb, stuckFrac, nucResidual) + cvFloor, cvCore
  "sensor_sweep": "build/figdata_out/cv_noise.json",  // P_d / false alarms vs NETD (WP-D figdata tool)
  "random_routes": [ {"key", "value", "unit", "source"} ], // kSpawnJitter, kNorthGapDeg, kViaSectorDeg, kViaMinR, kViaMaxR,
                                                           // kMaxVia, kSwitchProbability, kSwitchMinS, kSwitchMaxS
  "seeds": {"example_base": 42,
            "streams": [{"name", "stream", "seed"}],   // mission 1, combat 2, squad 3, garrison 4, sensor UAV-1 100, UAV-2 101
            "patrols": [{"uav", "base_radius", "amplitude", "breathing", "angular_speed"}]}   // Scene(v6(42)) squad patrols
}
```
`controllables` (v4 key) now lists the soldiers and the SKY clock automatically (whatever Scene builds).

## Progress
- SceneApi.h v6 adapters: sa::dayCycle, sa::garrison, sa::soldierIndex / soldierPosition, sa::setDayCycleConfig.
- HudMath: phase banners (Dusk / NightFalls / Dawn / DayBreak / Shift / Withdraw, one phase banner at a time, withdrawals
  merged with a count), skyGlyph, formatClock, parseTimeOfDay (--time), mixRunSeed, cvBoxToScreen (PERS boxes),
  helpLines() (F1 overlay data), keyBindingsV6() (report sB / workbook).
- Director: `DirectorCut::V5` kept for the report captures; new default `DirectorCut::V6` (15 shots, 120 s):
  night wave 8, patrol 7, combined strike 11, thermal FPV 7, kill 9, dawn time-lapse 10 (5.2 -> 7.4 h), day flyover 9,
  soldier patrol 7, mechanic 6, logistics 6, helicopter 9, day thermal through UAV-1's gimbal with PERS boxes 7,
  dusk 9 (17.2 -> 18.05 h, doors opened), off duty into the bunker 8, night searchlights 7 (19.5 h).
  `DirectorScript::hour` forces the clock (manual) every frame; `strike` removed. K and --record warm up 8 s at 23:00.
- App: Scene(SceneOptions::v6(seed, hour, auto)) incl. restartWorld; --seed 0 = legacy routes (sensor seeds 101/102).
  Per UAV each frame: dropFrame -> gimbalJitter -> jitterCamera -> captureUavCamera(true pose) -> SensorModel::apply ->
  cv::detectAll -> Target blobs to submitVision with the reported pose; Person blobs kept for the HUD.
  forceStrike / strikeTimer_ / scripted strike removed.
- Capture: Shot.hour / settle / autoClock / soldier / v6; triggers Withdraw, Shift, Persons; no true-rover explode and
  no synthetic HUD events (kill / hit shots are logged and skipped when no real kill happens in time). The v5 shots
  replay the v5 director cut at 23:00. v6 shots (see list in Capture.cpp shotList): night_searchlights, plan_routes,
  lab_cv_noise (CaptureLab::cvNoise -> lab_cv_noise.png + cv_noise.json), dawn_withdraw, day_overview, sky_noon,
  day_patrol_spline, day_mechanic, day_logistics, soldier_closeup, soldier_axes, soldier_exploded, soldier_possess_tp,
  soldier_possess_fp, day_thermal_fpv_persons, sky_sunset, dusk_shift_change, bunker_offduty.
  `--only` with only v6 names skips the v5 sequence.
- Overlays: M also draws the garrison spline routes and every live rover's via chain; H on a soldier shot draws all
  joint frames (axesControllable_).
- Trace CSV: appended columns hour, phase, seed, uav0_targets, uav0_persons, frames_dropped (old readers unaffected);
  summary line with phase events / withdrawals / dropped frames.
- HUD: clock panel above the mini-map (sun / low sun / moon glyph, HH:MM, phase, AUTO/MANUAL, sun elevation, seed,
  garrison on/off-duty count), FPV telemetry clock = scene clock, FPV sensor line (NETD, drops), PERS brackets,
  phase banners; window title shows clock + seed. AudioEngine handles all new events.
- DocExport: "v6" section (keys above), schema_version 5.
- Tests: T125 (both cuts), T129 (v5 script + v6 script), T177 (v6 HUD flags), new T237 (CLI helpers), T238 (director
  clock), T239 (phase banners, PERS mapping, key table). All WP-E tests pass on the shared tree (2026-10-06); the only
  failures there are WP-B's T200-T214 (in progress).
- Verified headless (llvmpipe, 960x540 LOW, seed 1): --trace 20 s --time cycle --seed 42 (CSV columns ok, 28 / 2398
  frames dropped ~ 1 %); --capture of night_searchlights, plan_routes, lab_cv_noise, dawn_withdraw (real Withdraw
  event at dawn), day_overview, sky_noon, sky_sunset, dusk_shift_change, bunker_offduty all written; --export-docs
  writes the "v6" section (97 grid rows, events Dawn 5.72 / DayBreak 6.45 / Dusk 17.6 / NightFalls 18.33).
- Also exported: "v6.keys" and "v6.help_lines" (same data as the F1 overlay).
- Leaving the K director restores the clock mode (auto unless --time fixed it).

## Open / for the supervisor
- --cycle-seconds uses DayCycle::setConfig + DayCycle::cycleSeconds (both present).
- The UAV feed inset (U) shows the renderer's clean heat texture, not the noisy CV image (Renderer is WP-A's).
- The person-box counter in the clock panel counts soldiers whose status() contains "OFF" as off duty.
