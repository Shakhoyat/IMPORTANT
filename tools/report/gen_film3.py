"""The final ThermalSense film (under 4 minutes, 1920x1080, 30 fps, the program's own sound).

Part 1 (about 2.5 minutes): the graphics depth and the controllable world: the base, the render modes, a soldier walking
through the buildings and opening their doors, the weapons on alert, then the same soldier getting into the tank, the
AA gun and the SAM launcher, firing from the gunner's sight and getting out again.
Part 2 (about 1.5 minutes): computer vision and the autonomous defence: the UAVs launching at dusk with their
searchlights, the rovers' route planning, the thermal view with its tracks, a single strike while the pair is split, the
guards firing, the combined strike, dawn.

All footage is rendered by the final build: build/film3/f_*.mp4 from build/film3/final.sh and final_C.sh (TS_TOUR scripted
takes in build/film3/tour/*.txt, TS_FILM_CAM glides, TS_FILM_VIEW follow / uav1 / flir of one deterministic night, the
clean v7 segments), plus the dusk take-off, the dawn landing and the animated route map recorded earlier.
Output: report/video/thermalsense_final_1080p.mp4 + poster, and a 720p copy under 50 MB for the slides.
Run: ~/.local/bin/uv run -q --with pillow python tools/report/gen_film3.py"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(__file__))
import gen_film as gf  # noqa: E402

ROOT = gf.ROOT
F3 = os.path.join(ROOT, "build", "film3")
F2 = os.path.join(ROOT, "build", "film2")
PL = os.path.join(ROOT, "report", "figures", "plots")
IMG = gf.IMG
gf.W, gf.H, gf.FPS, gf.BAR, gf.STYLE = 1920, 1080, 30, 0, "lower"
OUT = os.path.join(ROOT, "report", "video", "thermalsense_final_1080p.mp4")
SMALL = os.path.join(ROOT, "report", "video", "thermalsense_final_720p.mp4")


def f(n):
    return os.path.join(F3, "f_%s.mp4" % n)


P1 = "PART 1  ·  GRAPHICS AND A CONTROLLABLE WORLD"
P2 = "PART 2  ·  COMPUTER VISION AND AUTONOMOUS DEFENCE"
SEG = [
    # ---------------------------------------------------------------- part 1
    ("clip", f("overview"), 0.6, 7.6, "", "ThermalSense", {"big": True, "sub": "A graphics pipeline, end to end, in a military outpost"}),
    ("clip", f("v7"), 0.6, 4.4, P1, "Every object is built from 16 primitives, each part placed by M = T R S", {}),
    ("clip", f("v7"), 7.6, 3.6, P1, "Scene graph: rotor, skids and searchlight hang on one hierarchy", {}),
    ("clip", f("v7"), 21.9, 2.4, P1, "One scene, four renderers: rasterised", {}),
    ("clip", f("v7"), 26.4, 2.4, P1, "Hybrid: ray-traced reflections", {}),
    ("clip", f("v7"), 30.9, 2.4, P1, "Whitted: recursive reflection and refraction", {}),
    ("clip", f("v7"), 36.2, 2.4, P1, "Path tracing", {}),
    ("clip", f("v7"), 51.0, 4.2, P1, "Frame Explorer: a frame taken apart, rasterisation by edge functions", {}),
    ("clip", f("v7"), 57.2, 3.4, P1, "Colour interpolation: flat, Gouraud, Phong", {}),
    ("clip", f("bunker"), 3.6, 6.4, P1, "A soldier under the player's control: the blast gate opens as he comes down the ramp", {}),
    ("clip", f("bunker"), 12.4, 7.6, P1, "Through the tunnel into the command room", {}),
    ("clip", f("gen"), 2.0, 6.0, P1, "Doors are opened by hand: [E] at the generator room", {}),
    ("clip", f("hall"), 2.6, 6.6, P1, "The barracks hall: the door swings on its hinge, then he walks in", {}),
    ("clip", f("hall"), 10.2, 4.2, P1, "Off-duty soldiers rest and train inside", {}),
    ("clip", f("al_sam"), 0.8, 4.2, P1, "The camp on alert: SAM (surface-to-air missile) canisters raised through the net", {}),
    ("clip", f("al_mlrs"), 0.8, 4.0, P1, "MLRS (multiple launch rocket system) pod at firing elevation, sweeping", {}),
    ("clip", f("al_aa"), 0.8, 4.0, P1, "AA (anti-aircraft) gun searching its sector, barrels turning", {}),
    ("clip", f("al_heli"), 0.8, 4.0, P1, "Helicopter at flight idle, ready to lift off", {}),
    ("clip", f("al_tank"), 0.8, 3.4, P1, "Tanks hull-down in their bays, guns raised over the wall", {}),
    ("clip", f("tank2"), 5.0, 5.6, P1, "The tank: he walks up and gets in with [E]", {}),
    ("clip", f("tank2"), 10.8, 10.0, P1, "Driving: out of the bay, a pivot on the tracks, the turret traversing", {}),
    ("clip", f("tank2"), 21.4, 8.2, P1, "Gunner's sight, first person: two rounds", {}),
    ("clip", f("tank2"), 30.2, 5.4, P1, "and out again: the soldier is back on his feet", {}),
    ("clip", f("aa"), 2.0, 3.4, P1, "The AA (anti-aircraft) gun, manned the same way", {}),
    ("clip", f("aa"), 11.4, 5.0, P1, "Six barrels, tracer bursts", {}),
    ("clip", f("aa"), 19.0, 2.6, P1, "", {}),
    ("clip", f("sam"), 2.0, 3.4, P1, "The SAM (surface-to-air missile) launcher", {}),
    ("clip", f("sam"), 17.6, 4.4, P1, "Launch", {}),
    ("clip", f("sam"), 23.0, 2.6, P1, "and back on foot", {}),
    # ---------------------------------------------------------------- part 2
    ("still", os.path.join(IMG, "scene", "night_searchlights.jpg"), 0, 3.4, "", "Part 2", "Computer vision and autonomous defence"),
    ("clip", os.path.join(F3, "uav_takeoff.mp4"), 0.4, 9.0, P2, "Dusk: the lamps come on and both UAVs (uncrewed aerial vehicles) lift off, searchlights on", {}),
    ("clip", os.path.join(F2, "rover_map.mp4"), 0, 8.0, P2, "Intruder rovers plan a covered route: flood fill, then Dijkstra; re-planned as the UAVs move", {"audio": "amb", "amb_at": 30}),
    ("clip", f("night_wide"), 0.8, 5.2, P2, "Night: the patrol orbits, searchlights sweeping the camp", {}),
    ("clip", f("night_flir"), 1.6, 5.6, P2, "Thermal camera: threshold, connected regions, Kalman tracks on every hot target", {}),
    ("still", os.path.join(PL, "p_cv_story.png"), 0, 4.2, P2, "From a heat image to a tracked target", {"amb_at": 33}),
    ("clip", f("night_follow"), 2.0, 4.2, P2, "Every guard post is manned; the searchlights follow the patrol", {}),
    ("clip", f("night_uav1"), 4.8, 3.2, P2, "Two intruders far apart: the pair splits, each UAV takes one", {}),
    ("clip", f("night_follow"), 7.2, 4.4, P2, "Single strike: one UAV alone, the other is busy", {}),
    ("clip", f("night_uav1"), 8.4, 2.8, P2, "The hit seen from the UAV that fired", {}),
    ("clip", f("night_flir"), 9.2, 3.4, P2, "Tracks are kept while the target is behind cover", {}),
    ("clip", f("night_uav1"), 13.6, 3.8, P2, "Guards at their posts open fire on the tracked intruder", {}),
    ("clip", f("night_flir"), 15.4, 2.8, P2, "One target left: LEAD and WING hold their locks", {}),
    ("clip", f("night_follow"), 17.5, 3.6, P2, "Combined strike: both rounds arrive together", {"speed": 0.6}),
    ("clip", f("night_flir"), 18.3, 2.6, P2, "The blast in the thermal camera", {}),
    ("clip", f("night_wide"), 18.6, 5.6, P2, "The wreck burns; the fire cools along a blackbody ramp", {}),
    ("clip", os.path.join(F2, "uav_landing.mp4"), 25.0, 8.0, P2, "Dawn: the UAVs return to their pads", {}),
    ("still", os.path.join(IMG, "showcase", "sc_overview_dusk.jpg"), 0, 4.4, "", "ThermalSense", "Computer Graphics Laboratory · CSE, KUET"),
]


def main():
    missing = [s[1] for s in SEG if isinstance(s[1], str) and not os.path.exists(s[1])]
    if missing:
        raise SystemExit("missing footage: " + ", ".join(sorted(set(missing))))
    gf.TMP = os.path.join(F3, "seg")
    shutil.rmtree(gf.TMP, ignore_errors=True)
    os.makedirs(gf.TMP)
    parts = []
    for i, s in enumerate(SEG):
        parts.append(gf.segment(i, s))
        print("segment", i, os.path.basename(s[1]), s[2], s[3], flush=True)
    total = sum(s[3] for s in SEG)
    if total > 240.0:
        raise SystemExit("the film is %.1f s, over 4 minutes" % total)
    lst = os.path.join(gf.TMP, "list.txt")
    open(lst, "w").write("".join("file '%s'\n" % p for p in parts))
    joined = os.path.join(gf.TMP, "joined.mp4")
    gf.run([gf.FF, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", joined])
    gf.run([gf.FF, "-y", "-loglevel", "error", "-i", joined, "-c:v", "libx264", "-preset", "slow", "-crf", "18",
            "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-af", "loudnorm=I=-15:TP=-1.0:LRA=11",
            "-movflags", "+faststart", OUT])
    gf.run([gf.FF, "-y", "-loglevel", "error", "-ss", "3", "-i", OUT, "-frames:v", "1", OUT.replace(".mp4", "_poster.jpg")])
    gf.run([gf.FF, "-y", "-loglevel", "error", "-i", OUT, "-vf", "scale=1280:720", "-c:v", "libx264", "-preset", "slow", "-crf", "25",
            "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart", SMALL])
    print("wrote", OUT, "%.1f s" % total, "%.1f MB" % (os.path.getsize(OUT) / 1e6), "and", SMALL, "%.1f MB" % (os.path.getsize(SMALL) / 1e6))


if __name__ == "__main__":
    main()
