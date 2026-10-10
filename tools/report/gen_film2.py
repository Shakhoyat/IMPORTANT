"""The ThermalSense film, 1080p (YouTube): part 1 = a soldier explores the camp and its buildings by day (free
exploration, no game); part 2 = game mode (duty roster and shift change, UAVs launching at dusk, the intrusion, thermal
tracking, the coordinated strike and its explosion, soldiers firing, dawn and the UAVs landing).

Footage (all rendered by the program, its own offline audio): build/film2/*.mp4 from build/film2/record.sh (TS_TOUR
walks, TS_FILM_CAM glides, the v6 cut at 1080p), the v7 showcase (report/video/thermalsense_showcase.mp4), report
stills and the animated route map. Uses the segment builder of gen_film.py with lower-third titles.
Output: report/video/thermalsense_film_1080p.mp4 (1920x1080, 30 fps, H.264 CRF 17, AAC 192 kb/s) + poster.
Run: ~/.local/bin/uv run -q --with pillow python tools/report/gen_film2.py"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import gen_film as gf  # noqa: E402

ROOT = gf.ROOT
F2 = os.path.join(ROOT, "build", "film2")
SC, IMG = gf.SC, gf.IMG
PL = os.path.join(ROOT, "report", "figures", "plots")
gf.W, gf.H, gf.FPS, gf.BAR, gf.STYLE = 1920, 1080, 30, 0, "lower"
SCROP = {"crop": "1920:800:0:140"}
V6CROP = {"crop": "1920:790:0:145"}   # the v6 cut: letterbox and title strip cut away   # the showcase: its letterbox bars and title strip cut away
OUT = os.path.join(ROOT, "report", "video", "thermalsense_film_1080p.mp4")


def f2(n):
    return os.path.join(F2, n + ".mp4")


def f3(n):
    return os.path.join(ROOT, "build", "film3", n + ".mp4")


N7 = f3("v7_night")   # the first 47 s of the v7 cut (night wave, strike, UAV gimbal, round, rover, sentry)


def sc(extra=None):
    d = dict(SCROP)
    d.update(extra or {})
    return d


P1 = "PART 1  ·  EXPLORING THE CAMP"
P2 = "PART 2  ·  GAME MODE"
SEG = [
    # ---- part 1: free exploration by day
    ("clip", f2("overview_dusk"), 0.6, 8.0, "", "ThermalSense", {"big": True, "sub": "A graphics pipeline, end to end, in a military outpost", "audio": "amb"}),
    ("clip", f2("gate_sentry"), 0.6, 7.0, P1, "Sentries hold the gate; the garrison follows its duty roster", {}),
    # the follow camera drops onto the ramp at 10.6-11.4 s while the soldier ducks under the lintel: cut there
    ("clip", f3("tour_bunker"), 4.0, 6.6, P1, "A soldier, free to explore: down the trench to the blast gate", {}),
    ("clip", f3("tour_bunker"), 11.5, 12.0, P1, "Through the hinged blast gate, along the tunnel into the command room", {}),
    ("clip", f2("tour_rooms"), 0.6, 16.0, P1, "The bunk room, past the off-duty crew", {}),
    ("clip", f2("tour_hall"), 1.6, 23.0, P1, "The barracks hall: off-duty soldiers rest and train", {}),
    ("clip", f2("hall_night"), 0.6, 6.0, P1, "The same hall at night, lit only by its tube lights", {}),
    ("clip", f2("tower_post"), 0.6, 7.0, P1, "A guard tower post, 5.4 m above the camp", {}),
    ("clip", f2("tour_camp"), 0.6, 13.6, P1, "Past the scout helicopter to the radar and the AA gun", {}),
    ("clip", f2("tour_motor"), 7.0, 10.0, P1, "The motor pool under camouflage nets", {}),
    ("clip", SC, 60.6, 3.3, P1, "Every vehicle is built from the same 16 primitives", sc()),
    ("clip", SC, 67.6, 3.3, P1, "Hierarchical models: rotor, skids, searchlight on one scene graph", sc()),
    ("clip", SC, 78.8, 1.7, P1, "Ray tracing: raster", sc()),
    ("clip", SC, 83.3, 1.7, P1, "Hybrid: ray-traced reflections through a BVH", sc()),
    ("clip", SC, 87.8, 1.7, P1, "Whitted: recursive reflection and refraction", sc()),
    ("clip", SC, 92.3, 1.7, P1, "Path tracing", sc()),
    # ---- part 2: game mode (night footage recorded after the gate and UAV-2 searchlight fixes: build/film3)
    ("still", os.path.join(IMG, "scene", "night_searchlights.jpg"), 0, 4.0, "", "Part 2 · Game mode", "Duty, intrusion, tracking, strike"),
    ("still", os.path.join(PL, "p_guard_map.png"), 0, 5.5, P2, "An hourly duty roster: every soldier's posts and routes, day and night", {"amb_at": 3}),
    ("clip", f3("v6_cut"), 96.5, 8.0, P2, "Dusk: the shift changes and the lamps come on", dict(V6CROP)),
    ("clip", f3("v6_cut"), 105.6, 7.0, P2, "Off duty: the day shift goes down into the bunker", dict(V6CROP)),
    ("clip", f3("uav_takeoff"), 0.3, 11.5, P2, "After sunset the UAVs launch on their own, searchlights on", {}),
    ("clip", N7, 3.6, 4.3, P2, "Night: intruder rovers move in on the camp", sc()),
    ("clip", f2("rover_map"), 0, 9.0, P2, "Each rover plans its own covered route: flood fill, then Dijkstra", {"audio": "amb", "amb_at": 30}),
    ("clip", N7, 36.5, 2.4, P2, "A rover's view: following its planned line", sc()),
    ("clip", f3("v6_cut"), 8.6, 6.0, P2, "Two UAVs patrol, each with its searchlight", dict(V6CROP)),
    ("clip", f3("v6_cut"), 26.5, 3.2, P2, "Each UAV sees the rovers only in its own thermal pixels", dict(V6CROP)),
    ("still", os.path.join(PL, "p_cv_story.png"), 0, 5.0, P2, "Threshold, despeckle, connected components: the rover becomes a target", {"amb_at": 33}),
    ("clip", f3("v6_cut"), 29.7, 3.2, P2, "Both UAVs feed one tracker: Kalman tracks, triangulated", dict(V6CROP)),
    ("clip", f3("v6_cut"), 15.4, 5.2, P2, "LEAD and WING time their launches so both rounds arrive together", dict(V6CROP)),
    ("clip", N7, 29.5, 3.4, P2, "Riding the round: proportional navigation", sc()),
    ("clip", N7, 13.5, 5.2, P2, "Impact: particles, HDR fire and bloom", sc({"speed": 0.5, "zoom": True})),
    ("still", os.path.join(IMG, "game", "explosion_early.jpg"), 0, 2.0, P2, "", {"amb_at": 36}),
    ("still", os.path.join(IMG, "game", "wreck_burning.jpg"), 0, 2.4, P2, "The fire cools along a blackbody ramp", {"amb_at": 38}),
    ("clip", N7, 42.5, 4.3, P2, "Soldiers fire when needed: first person, recoil, tracers", sc()),
    ("clip", f3("v6_cut"), 42.6, 7.0, P2, "Dawn: the surviving intruders withdraw", dict(V6CROP)),
    ("clip", f2("uav_landing"), 4.0, 8.0, P2, "Dawn: the UAVs come home", {}),
    ("clip", f2("uav_landing"), 25.0, 15.0, P2, "and land on their pads until the next nightfall", {}),
    ("still", os.path.join(IMG, "showcase", "sc_overview_dusk.jpg"), 0, 4.5, "", "ThermalSense", "Computer Graphics Laboratory · CSE, KUET"),
]


def main():
    gf.TMP = os.path.join(F2, "seg")
    shutil.rmtree(gf.TMP, ignore_errors=True)
    os.makedirs(gf.TMP)
    parts = []
    for i, s in enumerate(SEG):
        parts.append(gf.segment(i, s))
        print("segment", i, s[1] if isinstance(s[1], str) else "flip", flush=True)
    lst = os.path.join(gf.TMP, "list.txt")
    open(lst, "w").write("".join("file '%s'\n" % p for p in parts))
    joined = os.path.join(gf.TMP, "joined.mp4")
    gf.run([gf.FF, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", joined])
    gf.run([gf.FF, "-y", "-loglevel", "error", "-i", joined, "-c:v", "libx264", "-preset", "slow", "-crf", "17",
            "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-af", "loudnorm=I=-15:TP=-1.0:LRA=11",
            "-movflags", "+faststart", OUT])
    gf.run([gf.FF, "-y", "-loglevel", "error", "-ss", "3", "-i", OUT, "-frames:v", "1", OUT.replace(".mp4", "_poster.jpg")])
    total = sum(s[3] for s in SEG)
    print("wrote", OUT, "%.1f s" % total, "%.1f MB" % (os.path.getsize(OUT) / 1e6))


if __name__ == "__main__":
    main()
