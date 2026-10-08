# WP-A notes (time of day, sky, lighting)

## Status
- [x] DayCycle model (solar position, Kasten-Young key, presets, phases + hysteresis, auto rates, scrub/jump,
      quantised key dir, apparentHeat / cloud CPU mirrors, clockText, setConfig for WP-E) + SkyClock controllable
- [x] Lighting.h Atmosphere: + keyIsSun, twilight
- [x] Renderer: atmo uniforms (sky ambient, bounce, fog colour/density, vol fog, exposure key, purkinje, day grade),
      practicals x practicalScale (not fire embers / strobes / muzzle flashes / indoor lamps), spot/beacon enabled
      gating, invisible beam skipped, "Sun" like "Moon" (no shadow/prepass/wire), Moon emissive x night, Sun x day,
      moon ball hidden while the sun is up, static shadow cache keyed on key direction, cameraOverride, bloom
      threshold follows the exposure key, fireflies only at night
- [x] Shaders: daySky (gradient, Mie aureole, disc, twilight band, cumulus layer), stars/Milky Way x night,
      cloudDensity shared GLSL (constants generated from DayCycle.h), cloud shadow inside moonShadow(),
      solarLoad for passive surfaces, day sky in puddle reflections, v1 fog colour by day, composite purkinje + day grade
- [x] tests T190-T199 pass (build/tests-wpa: 204 tests, 0 failed on the shared tree of 2026-10-06 00:50)
- [x] headless noon / dusk / night capture via build/wpa_tool/sky_harness (not committed)

## Numbers for the report / workbook (all reachable from DayCycle pure functions)
- sunrise 06:08, sunset 17:52, noon elevation 62.6 deg (DayCycle::hourAtElevation / sunElevationDeg)
- auto cycle 290.6 s (DayCycle::cycleSeconds)
- cloud layer: 120 m, fBm 1/55 m, wind (3.2, 1.4) m/s, threshold 0.395..0.545 (coverage ~0.45), opacity 0.6;
  noon mean cloud shadow ~0.70
- day exposure key kDayExposureKeyLog2 = -2.8 (noon overview frame mean log2 L measured -2.9 / -2.85 at 13:30 on
  llvmpipe, HIGH); the key blends night -> day with smoothstep(-8, 10 deg) of the sun elevation
- headless UAV CV frames at 13:30 and 17:36: max heat byte 133 / 109 (< 204 floor): no daylight false targets
- solar loading: kSolarGain 0.12, passive band 0.55..0.75 -> passive 0.50 peaks at 0.62, max < 0.75 + 0.0

## Scene integration (WP-C, done by WP-C)
dayCycle.update(dt, events, time) then applyTo(lights) each frame and once in the constructor; makeSkyClock(dayCycle)
added to controllables before collectSceneLists (its "Sun" leaf must be in leaves()).
