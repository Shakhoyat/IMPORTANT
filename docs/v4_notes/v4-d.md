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
