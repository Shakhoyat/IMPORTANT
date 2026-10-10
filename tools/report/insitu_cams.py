"""Cameras for the in-scene view of each design (Section 7 cards, panel "in the scene").

From the export (report/data/scene_export_v10.json): the first instance's own parts (design_root = its root), their
world boxes (unit primitive corners through the world matrix), the bounding sphere. The camera looks at the centre from
35 degrees off the design's front and 22 degrees up (large flat designs: steeper), at the distance where the sphere fills
about 70 % of a 50-degree view, never below 1.6 m above the apron. Writes build/insitu_cams.txt for TS_INSITU.
Run: python3 tools/report/insitu_cams.py, then
  TS_INSITU=build/insitu_cams.txt build/ts-f --headless --quality epic --size 1920x1080 --seed 1 \\
      --capture report/figures/raw/insitu --only "$(cut -d' ' -f1 build/insitu_cams.txt | paste -sd,)" """
import ast
import json
import math
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXP = json.load(open(os.path.join(ROOT, "report", "data", "scene_export_v10.json")))
SKIP = {"Moon", "Sky clock (sun)"}
# per design: (azimuth off the front, elevation, fill, hour); defaults (35, 22, 0.7, 10.5)
VIEW = {"Terrain disc": (20, 55, 0.95, 10.5), "Far ground": (20, 35, 1.0, 10.5), "Distant hills": (20, 30, 1.0, 10.5),
        "HESCO perimeter": (10, 50, 0.95, 10.5), "Concertina wire": (10, 50, 0.95, 10.5), "Perimeter fence": (10, 50, 0.95, 10.5),
        "Road": (25, 40, 0.8, 10.5), "Tyre tracks": (20, 50, 0.8, 10.5), "Treeline pine": (0, 12, 0.6, 10.5),
        "Far radio tower": (0, 8, 0.5, 10.5), "Camouflage net": (35, 35, 0.75, 10.5), "Command bunker": (35, 32, 0.75, 10.5),
        "Helipad": (30, 40, 0.8, 10.5), "UAV pad": (30, 45, 0.8, 10.5), "Tank spot marking": (30, 50, 0.8, 10.5),
        "Radar surround": (30, 40, 0.8, 10.5), "AA gun surround": (30, 40, 0.8, 10.5), "Earth revetment": (35, 35, 0.75, 10.5),
        "Motor pool": (35, 38, 0.8, 10.5), "Fuel depot": (35, 30, 0.75, 10.5),
        # under the camouflage nets: a low camera from the open side; clear of trees
        "Main battle tank": (70, 5, 1.05, 10.5), "MLRS rocket truck": (60, 7, 1.0, 10.5), "SAM launcher": (-50, 9, 1.0, 10.5),
        "Tank spot marking": (30, 72, 0.9, 10.5), "Barracks hall": (35, 20, 1.0, 10.5), "Cargo truck": (35, 14, 1.05, 10.5),
        "Pine": (-70, 18, 0.8, 10.5), "Outer gate": (-35, 12, 0.95, 10.5), "Earth revetment": (-45, 30, 0.85, 10.5),
        "Bush": (-50, 30, 1.05, 10.5), "Windsock": (-70, 14, 0.9, 10.5), "Lamp post": (-40, 14, 0.8, 10.5)}
# backdrop and moving designs: their cards use other captures (overview, live-following shots)
SKIP |= {"Distant hills", "Far ground", "Terrain disc", "Autonomous UAV", "Scout helicopter", "Target rover"}


def slug(t):
    return re.sub(r"_+", "_", re.sub(r"[^a-z0-9]", "_", t.lower())).strip("_")


def val(x):
    return ast.literal_eval(x) if isinstance(x, str) else x


comps = EXP["components"]
corners = [(sx, sy, sz) for sx in (-0.5, 0.5) for sy in (-0.5, 0.5) for sz in (-0.5, 0.5)]
lines = []
for d in EXP["designs"]:
    name = d["name"]
    if name in SKIP:
        continue
    inst = val(d["instance_list"])[0]
    root, heading = inst["root_id"], float(inst.get("heading_deg", 0.0))
    pts = []
    for c in comps:
        if c.get("design_root") != root or c.get("design") != name:
            continue
        if str(c.get("hidden_at_export")) == "True":
            continue
        m = val(c["world_matrix"])
        for (x, y, z) in corners:
            pts.append((m[0] * x + m[1] * y + m[2] * z + m[3], m[4] * x + m[5] * y + m[6] * z + m[7], m[8] * x + m[9] * y + m[10] * z + m[11]))
    if not pts:
        print("no parts:", name)
        continue
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    c = [(lo[i] + hi[i]) / 2 for i in range(3)]
    r = max(0.6, 0.5 * math.dist(lo, hi))
    az, el, fill, hour = VIEW.get(name, (35, 22, 0.7, 10.5))
    fov = 50.0
    dist = r / math.tan(math.radians(fov / 2)) / fill
    a, e = math.radians(heading + az), math.radians(el)
    eye = [c[0] + dist * math.cos(e) * math.sin(a), c[1] + dist * math.sin(e), c[2] + dist * math.cos(e) * math.cos(a)]
    eye[1] = max(eye[1], -0.3 + 1.6)
    lines.append("insitu_%s %.3f %.3f %.3f %.3f %.3f %.3f %.1f %.2f" % (slug(name), *eye, *c, fov, hour))
out = os.path.join(ROOT, "build", "insitu_cams.txt")
open(out, "w").write("\n".join(lines) + "\n")
print("wrote", out, len(lines), "cameras")
