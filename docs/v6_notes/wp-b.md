# WP-B soldiers — progress notes

## Done (compiles, in the shared tree)
- Spline.cpp: centripetal Catmull-Rom (Hermite form), arc-length table, analytic curvature.
- Gait.{h,cpp}: walk/run cycles, stance/swing ankle trajectory, two-link IK + FK, idle/guard/work/sit/duck/crouch/wave poses.
- Soldier.{h,cpp}: 40-leaf figure (depth 7), capsule limbs, helmet, rifles (held / slung), backpack, wrench, crate.
  (v7 WP-M hero pass: 75 leaves, T201 cap raised 42 -> 75 with the supervisor's approval; shorter neck / lower head
  (kChestToNeck 0.23, kNeckToHead 0.06, kShoulderY 0.19), arms 0.30 / 0.26 m, face, pads, pouches, radio, rifle
  furniture; armed soldiers hold the rifle by two-bone arm IK (Gait armIK, Garrison holdRifle). See docs/v7_notes/wp-m.md.)
- Garrison.{h,cpp}: SitePlanner (72x72 1 m grid, oriented part footprints, fence chords, trench, HESCO),
  A* -> string pulling -> spline with clearance validation; roster (8 duties, day/night programs), bunker legs,
  shift change (hour >= 6.4 && < 17.6 = day), possession, camera hints, duck, footfalls, takeShiftChanges().
- GarrisonContext: controllables + leaves (wired by the supervisor in Scene).
- tests/test_soldiers.cpp T200-T214 written (debug env: TS_GARRISON_DUMP / PTS / PROBE / DEBUG).

## Open (iterating)
- Route clearance near thin parts (trench guard rails, poles), foot plant drift, bunker wall contacts,
  mechanic stuck, separation / long waits in T214.
