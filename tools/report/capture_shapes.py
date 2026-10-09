"""Report captures of the final pass (program side: src/app/CaptureShapes.cpp, CaptureParts.cpp):
  shapes    every design of the export's "designs" list: shapeview:<design> (+ the overrides below)
  cutaway   the enclosed designs again with --cutaway (roof / earth cover left out; the list is in CUT)
  camp      the whole camp from the air, made of primitives (shapeview:@camp)
  lineup    the munitions at one scale (all four; the two small ones again on their own)
  stages    build stages in the colour key (first 12 / 35 / 65 / 100 % of the parts in hierarchy order) of the UAV,
            tank, helicopter and command bunker
  prims     the 16 primitives (gallery tiles in the colour key)
  tiles     "used for" part tiles of the primitive cards: per primitive the 3 most frequent part kinds of different
            designs (from the export)
Writes report/figures/raw/shapes/ (shapeview/, prims/, tiles/). Default: all groups.
Run: python3 tools/report/capture_shapes.py [--app build/ts-f] [group ...]"""
import collections
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXPORT = os.path.join(ROOT, "report", "data", "scene_export_v10.json")
OUT = os.path.join(ROOT, "report", "figures", "raw", "shapes")
COMMON = ["--headless", "--quality", "epic", "--size", "1920x1080", "--seed", "1"]

# Per-design view overrides: camera azimuth / elevation (deg, relative to the design's front), pull-apart factors.
VIEW = {
    "Bush": "kpart=0.7", "Boulder": "kpart=0.7", "Broadleaf tree": "kpart=0.5", "Pine": "kpart=0.6",
    "Treeline pine": "kpart=0.6", "Main battle tank": "ksub=0.6", "Soldier": "kpart=0.35",
    "Command bunker": "el=50", "Barracks hall": "el=40", "Ammunition bunker": "el=35",
    "HESCO perimeter": "el=35", "Concertina wire": "el=35", "Perimeter fence": "el=35", "Tyre tracks": "el=45",
    "Distant hills": "el=30", "Terrain disc": "el=35", "Far ground": "el=35", "Road": "el=40",
    "Tank spot marking": "el=45", "Helipad": "el=35", "UAV pad": "el=35",
}
# Cutaways (--cutaway): what is left out so the rooms show (names / name prefixes, see the cards' captions).
CUT = {
    "Command bunker": "el=50|cut=Roof slab*;Surface installations;Camouflage net",
    "Ammunition bunker": "el=35|cut=Earth mound*;Turf clump",
    "Barracks hall": "el=50|cut=Roof sheet;Ridge cap;Ceiling;Gable",
}
CAMP = "shapeview:@camp|el=55|az=20|ksub=0.12|kpart=0|w=2400|h=1500|fill=0.95"
STAGES = {"Autonomous UAV": "", "Main battle tank": "", "Scout helicopter": "", "Command bunker": "el=50"}
LINEUPS = ["lineup:SAM missile+MLRS rocket+UAV round+AA shell casing", "lineup:UAV round+AA shell casing"]


def tile_friendly(c):
    """A part that reads as a thumbnail: its longest side at most 12 times its second-longest (fence wires and ropes
    are not; flat plates and discs are)."""
    S = sorted(abs(v) for v in c["S_rest"])
    return S[1] > 0 and S[2] / S[1] <= 12


def tokens_tiles(d):
    """3 part kinds per primitive key, most frequent first, each from a different design."""
    lib_keys = [p["type"] if p["name"] != "cuboid" else "Cuboid" for p in d["primitives"]]
    by = collections.defaultdict(collections.Counter)
    first = {}
    for c in d["components"]:
        if c["primitive"] == "Group" or c["hidden_at_export"] or not tile_friendly(c):
            continue
        key = c["primitive"]
        if key == "Cube":
            s = c["S_rest"]
            if max(s) - min(s) > 1e-3 * max(max(s), 1e-6):
                key = "Cuboid"
        kind = c["part_kind"]
        by[key][(c["design"], kind)] += 1
        first.setdefault((c["design"], kind), c)
    out = []
    for key in lib_keys:
        seen = set()
        for (design, kind), n in by[key].most_common():
            if design in seen:
                continue
            seen.add(design)
            out.append("tile:%s|%s" % (first[(design, kind)]["object"], kind))
            if len(seen) == 3:
                break
    return out


def run(app, tokens, extra=()):
    os.makedirs(OUT, exist_ok=True)
    cmd = [app] + COMMON + list(extra) + ["--capture", OUT, "--only", ",".join(tokens)]
    print("==", len(tokens), "requests", " ".join(extra))
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    log = os.path.join(OUT, "capture_stdout_%s.txt" % ("cut" if extra else "main"))
    open(log, "a").write(r.stdout + r.stderr)
    bad = [l for l in (r.stdout + r.stderr).splitlines() if "FAIL" in l or "no design" in l or "no part" in l]
    print("   exit", r.returncode, "; problems:", bad[:5])
    return r.returncode == 0


def main():
    args = sys.argv[1:]
    app = os.path.join(ROOT, "build", "ts-f")
    if args[:1] == ["--app"]:
        app, args = os.path.abspath(args[1]), args[2:]
    groups = args or ["shapes", "cutaway", "camp", "lineup", "stages", "prims", "tiles"]
    d = json.load(open(EXPORT))
    ok = True
    if "shapes" in groups:
        ok &= run(app, ["shapeview:%s%s" % (x["name"], ("|" + VIEW[x["name"]]) if x["name"] in VIEW else "") for x in d["designs"]])
    if "cutaway" in groups:
        ok &= run(app, ["shapeview:%s|%s" % (k, v) for k, v in CUT.items()], ["--cutaway"])
    if "camp" in groups:
        ok &= run(app, [CAMP])
    if "lineup" in groups:
        ok &= run(app, LINEUPS)
    if "stages" in groups:
        ok &= run(app, ["shapeview:%s|ksub=0|kpart=0|stage=%d%s" % (k, pct, ("|" + v) if v else "")
                        for k, v in STAGES.items() for pct in (12, 35, 65, 100)])
    if "prims" in groups:
        ok &= run(app, ["prims"])
    if "tiles" in groups:
        ok &= run(app, tokens_tiles(d))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
