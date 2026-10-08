# WP-C notes (scene integration, game rules, random rover routes)

Build: `make -j16 HEADLESS=1 CXX=zcxx CC=zcc OBJ=build/obj-wpc TESTS=build/tests-wpc build/tests-wpc`
The shared tree was temporarily broken by other WPs mid-edit (Spline.cpp, Director/test_app), so the suite was run
on a snapshot (index + my files, HEAD Director/test_app): 182 tests, 180 pass; the 2 failures are WP-D's T229/T230
(in progress). All 169 v5 tests + T215-T224 pass.

## Done
- Combat::setSeed(seed) (0 = legacy 0x2468ace1).
- HideController: RoverPersonality {noiseWeight, exposureWeight, speedScale, pauseInCover, noiseCells};
  setRoute(via, objective, seed, personality), retarget(objective), setRetreat(exit), retreating(), reachedExit(),
  currentGoal(), via()/viaReached(), pausing(), freeAt(p). Defaults == v5 rover bit for bit.
- Mission: MissionConfig {randomRoutes, legacyFirst, freeCell}, configure(), nextSpawn() (legacy draw order unchanged),
  random ring spawn (radius 16.8 +- 1.2, north gap 18 deg), 0-3 vias in +-70 deg (r 4-15 m, free cells, outermost
  first), personality, objective switch p 0.3 after 8-22 s; endNight(), onWithdraw() (Withdraw event, no score /
  integrity); MissionState.withdrawn / nightsEnded. Constants kSpawnJitter, kNorthGapDeg, kViaSectorDeg, kViaMinR/MaxR,
  kMaxVia, kSwitchProbability, kSwitchMinS/MaxS (exporter-ready).
- Squad: FriendlyMark {p, r}; setFriendly(marks) (+ v5 Vec3 overload), friendlyMarks(), friendlyAt(p);
  setPatrolSeed(seed) (0 = v5 orbit exactly), PatrolParams per UAV (patrolParams(i)), investigate detours
  (investigating(i), detours()).
- Scene: seeds via deriveSeed (1 mission, 2 combat, 3 squad patrol, 4 garrison); DayCycle owned (update + applyTo each
  frame); Garrison + SkyClock appended after the v5 controllables (indices unchanged), garrison.init after solids;
  WorldContext walk/hour/daylight/beamsOn; friendly marks from friendlyRadius() > 0; waves only when allowed; dawn ->
  endNight + withdrawIntruders() (exit on bearing at r 17.5, 90 s timeout) -> Withdraw; day: spot off, beam cones
  hidden, no UAV 1 light, beacon off below practicalScale 0.5, beacon intensity x practicalScale; legacy first rover
  only when waves are allowed at start; soldier footfalls warm the heat trail.
  API: dayCycle(), garrison(), dayRules(), pinDayRules(DayRules), unpinDayRules(), withdrawIntruders(),
  spawnIntruder(const SpawnRequest&); Intruder.age / switchAt / altObjective / retreatTime.
- tests/test_paths.cpp T215-T224.

## For others
- WP-A: makeSkyClock's controllable should return friendlyRadius() 0 (it is not a ground object). DayCycle::applyTo
  at night must leave the v5 lights unchanged (Scene calls it every frame).
- WP-E: build Scene(SceneOptions::v6(seed, hour, auto)) (+ garrison flag); keys -> scene.dayCycle().scrub/toggleAuto/
  jumpToNextPhase; HUD -> dayCycle().clockText(), events Dusk/NightFalls/Dawn/DayBreak/Withdraw; captures that need a
  fixed look can pinDayRules(); export: Mission constants above, mission().state().withdrawn, squad().patrolParams(i),
  intruders()[i]->hide.via()/personality() for the plan_routes figure.
