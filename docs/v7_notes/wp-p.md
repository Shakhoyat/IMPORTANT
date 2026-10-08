# WP-P visual polish (agent P) - progress notes

## 1. Cloud block artifacts (day sky)
- Cause: valueNoise used the float hash21 on lattice points. For the cloud layer the lattice index reaches a few
  hundred (4000 m sky ray * 1/55 m * 2.03^3 octave + wind drift), where fract(p * 456.21) keeps only ~5 bits.
  Mesa d3d12 (DXIL) re-associates `(i + vec2(1,0)) * k` differently at the four call sites, so the same lattice
  point gets different values in neighbouring cells -> value noise is discontinuous on cell borders -> hard-edged
  square / perspective-stretched blocks in the cumulus.
- Fix: integer lattice hash (lowbias32 `mixBits`, `latticeHash`) in kPalette valueNoise (ShaderSources.cpp) and
  in the CPU mirror (DayCycle.cpp): bit-exact on GPU and CPU for any cell index. Also fixes the same latent
  issue in every surface texture using valueNoise/fbm with large coordinates (brushed metal x90 etc.).
- Sky shading (ShadersPost.cpp daySky): cloudDensity base field (still the one that casts the ground shadows) +
  two zero-mean detail octaves faded by the pixel footprint (fwidth) so the horizon does not alias; edge softness
  widens with the footprint; 3-tap optical depth towards the sun (Beer-Lambert), powder term, Henyey-Greenstein
  (g 0.62) forward lobe for silver linings at thin edges, blue sky ambient from above, grey bases, aerial
  perspective with distance, translucent wispy rims. T198 unchanged and passing (mean shadow 0.725, drift 6e-7).

## 2. HUD overlap / clutter (AppHud.cpp, rendering/Hud.{h,cpp})
- Cause of "markers over the picker": Hud batches tris / lines / glyphs and draws them per kind at end(), so any
  line / text queued earlier paints over any later panel. Added Hud::flush() (draw what is queued, keep going).
- drawPicker / drawHelp flush first (panel now 0.88 opaque); drawGameHud flushes after the world markers, so the
  top bar, mini-map, clock, banners and the top-left / bottom-left blocks paint over them.
- Top-left: help line shortened ("F1 HELP  5-0 CONTROL  F2 TIME  F3 SOLDIER  F4 POV", ends at 356 s < 450 s where
  the centred wave panel starts on 16:9); "MILITARY BASE" moved next to the title. Soft 0.30 backings behind the
  top-left and bottom-left text blocks (dim text was unreadable over the day sky).
- Clock panel: "NIGHT  MANUAL" ran past the panel's right edge -> phase / mode stacked.
- Enemy markers skip a track within 2 m of the camera (bumper cam / driving the tracked rover drew "ID 1 0 M"
  over the POV panel).

## 3. First-person rifle (AppGame.cpp viewmodel section; App.h Hidden struct)
- Scene::leaves() is collected once at load, so new nodes would not render. The viewmodel borrows the soldier's
  leaves the first-person view hides anyway (head/helmet/neck, both arms + hands, held + slung rifle, wrench):
  each frame they are re-shaped (primitive, material, world) into 45 parts and restored in restoreViewmodel.
- Carbine: upper/lower receiver, top rail, round handguard + flat underside, M-LOK slots, end cap, barrel, muzzle
  brake + port + crown, folded front sight, curved magazine + floor plate, pistol grip, trigger guard, buffer
  tube, stock + cheek rest + butt pad, charging handle, bolt catch, red-dot optic (tube, mount, coated lenses,
  turrets); worn-edge strips (bright metal, low roughness) on the visible edges. Gloved hands (right on grip,
  index finger along the receiver; left under the handguard), cuffs, olive sleeves, left elbow + upper arm.
- PBR: anodised alloy (metal 0.6, r 0.42), FDE polymer, blued steel, worn edges, rubber, coated glass, gloves,
  sleeves. Surface::Plain on purpose (world-space procedural patterns would swim on a part glued to the eye).
- Motion: idle breathing sway (x/y + yaw), gait bob + roll scaled by speed, recoil kick (back + pitch).
- soldierWeapon muzzle now uses the same kGunEye / kMuzzleZ constants (tracer starts at the brake).
- Caveat: in the RT modes (WP-H), if its BVH caches the primitive type per dynamic node, the borrowed leaves may
  show their original shape in RT-only passes; raster path verified.
- Also: one line in raytrace/SceneBridge.cpp sync() re-reads the shape of each traced leaf per frame (WP-H file,
  additive) so the RT modes see the borrowed viewmodel leaves with their per-frame shape.
- Agent M is reworking the soldier's arm pose (Soldier/Gait/Garrison): untouched here. The viewmodel only needs the
  hidden leaves under head/shoulders/rifles; if M changes their count, the part list degrades gracefully (extras
  at the end of viewmodelParts() are dropped first: small rifle details, not the hands).

## Status: DONE
- Build -Wall -Wextra clean for these files (the only warning is CaptureRT.cpp:49 unused crop(), not WP-P).
- Tests: 289/289 pass (T198 mean shadow 0.725, drift err 6e-7).
- Captures (scratchpad wpp/): before/ (WP-G state), after/ (1280x720), after1080/ (1920x1080 HUD pass).
