# D notes (app / camera / HUD / director / capture / tests)

## Done (compiles individually, full link waits on src/scene/Combat.cpp from C1)
- src/scene/SceneApi.h: SFINAE adapter `ts::sa::` (controllables, combat, walkSpace, update(dt,det,input,possessed), kills, roverPositions, primaryRover, roverHidden, roverSpeed). Uses Scene members when they exist, safe defaults otherwise.
- src/scene/Control.h/.cpp (new, D-owned): key table 5 Tank, 6 Helicopter, 7 AA gun, 8 Radar, 9 Blast doors, 0 UAV; resolveSlot by name (fallback documented order), cyclePossessable, nearestToggle (3.5 m).
- Camera: walk camera uses WalkSpace floors/ceiling/collide + dynamic obstacles, 52 m radius; CameraMode::Possess; applyShake().
- Director: 12 shots / 120 s, directorScript() synthetic inputs, directorWalkPose().
- App/Interactive: possession keys, [ ] cycle, Backspace, Backslash action, Enter/LCtrl fire or door interaction in walk (one frame of actionPressed possession of nearest door), C = chase/sight while possessed.
- HUD: possessed panel, tank sight, AA lead ring, heli tapes, ammo/reload bars, combat feed + kill counter, two-column F1 help.
- Record default 120 s; Capture: v4 shots (bunker_*, base_*, helipad, radar, tank_closeup, heli_takeoff, tank_fire, aa_gun_*, uav_missile_launch, thermal_fpv_strike, explosion_early/mid/smoke, wreck_burning).
- tests/test_app.cpp T120-T129.
## Open
- Full link blocked by missing Combat.cpp; rover list API from C1 (sa:: adapter handles rovers()/rover).

## Verification round (done)
- SceneApi: primaryRover = newest burning wreck (<15 s) else first alive intruder; scripted strike uses designator id 100 (scene rewrites id 0 each frame).
- Door scripting is state-aware: the director's "open" action is dropped when status() already says OPEN (doors start open). Walk-mode Enter stays a plain toggle (user intent).
- Control.cpp AA slot keywords now include GATLING / AA-1.
- Capture shots added: motor_pool (-12,2.2,-4)->(-21,1.2,-15), possess_doors/heli_cockpit/tank_sight/aa_sight/uav_manual (HUD on), cameras adjusted (base_overview, radar, bunker_cic, wreck_burning).
- Rendered and viewed at low quality: base_*, helipad, radar, tank_closeup, bunker_*, heli_takeoff, tank_fire, aa_gun_*, possess_*, uav_missile_launch, thermal_fpv_strike, explosion_early/mid/smoke, wreck_burning, motor_pool: all show their subject; explosions land on a rover (KILLS counter 2/3).
- Preview video: /tmp/claude-1000/d-preview.mp4 (640x360 low), rendering slowly (~1.4 fps on llvmpipe).
- Known: tank_fire frame is dark/cluttered (camera behind tank among huts); aa_gun_burst camera looks into muzzle flash.
