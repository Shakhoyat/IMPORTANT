"""Hand-typed numbers of the report against the code and the export (the generated macros are covered by gen_numbers.py,
gen_game.py and check_counts.py). Exit code 1 on any mismatch. Run: python3 tools/report/check_numbers.py"""
import collections
import glob
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SEC = os.path.join(ROOT, "report", "sections")
bad = []


def src(path):
    return open(os.path.join(ROOT, path), errors="replace").read()


def tex(name):
    return open(os.path.join(SEC, name)).read()


def const(path, name):
    m = re.search(r"\b%s\s*=\s*([\d.]+)f?\b" % re.escape(name), src(path))
    if not m:
        bad.append("constant %s not found in %s" % (name, path))
        return None
    return float(m.group(1))


def expect(what, value, text, pattern, optional=False):
    """The report text must state 'value' where 'pattern' (with one group for the number) matches."""
    hits = re.findall(pattern, text)
    if not hits and optional:
        print("--  %-58s not stated in the report" % what)
        return
    if not hits:
        bad.append("%s: the report sentence was not found (%s)" % (what, pattern))
        return
    for h in hits:
        if abs(float(h.replace("\\,", "").replace(",", "")) - value) > 1e-6:
            bad.append("%s: report says %s, code says %g" % (what, h, value))
    print("ok  %-58s %g" % (what, value))


# ---- squad rule, guards (Sections 27 and 29, Algorithm 6)
game, paths, alg = tex("B08_game.tex"), tex("B09_paths.tex"), tex("algorithms.tex")
split = const("src/ai/Squad.h", "kSquadSplitDist")
threat = const("src/ai/Squad.h", "kSquadSplitThreat")
wait = const("src/ai/Squad.h", "kSquadWingWaitFree")
rng = const("src/scene/Scene.h", "kRifleRange")
for name, t in (("Section 27", game), ("Algorithm 6", alg), ("Section 29", paths)):
    expect("split distance, " + name, split, t, r"more than (\d+)\\,m from the")
expect("split urgency, Section 27", threat, game, r"within (\d+)\\,m of a protected point")
expect("split urgency, Algorithm 6", threat, alg, r"within (\d+)\\,m of a protected point")
expect("wait for a free wing, Section 27", wait, game, r"waits up to (\d+)\\,s for the wing")
expect("wait for a free wing, Algorithm 6", wait, alg, r"no lock after (\d+)\\,s")
expect("guards' range", rng, game, r"closer than (\d+)\\,m")
sc = src("src/scene/Scene.cpp")
m = re.search(r"std::fmod\(clock \+ [^,]+, ([\d.]+)f\);\s*// ([\d.]+) s burst every ([\d.]+) s", sc)
if not m or m.group(1) != m.group(3):
    bad.append("guards' burst timing not found in Scene.cpp")
else:
    expect("guards' burst length", float(m.group(2)), game, r"bursts of ([\d.]+)\\,s")
    expect("guards' burst period", float(m.group(3)), game, r"every ([\d.]+)\\,s")

# ---- hand-written vector functions (Section 16.1): calls per function, files
calls, files = collections.Counter(), set()
for p in glob.glob(os.path.join(ROOT, "src", "rendering", "*.cpp")):
    if p.endswith("ShaderMath.cpp"):
        continue
    found = re.findall(r"\bts(Dot|Length|Normalize|Cross|Reflect|Refract|Mix|Clamp|Step|Smoothstep|Transpose|Distance)\s*\(", open(p).read())
    calls.update(found)
    if found:
        files.add(p)
ill = tex("A10_illumination.tex")
for fn, n in calls.items():
    expect("calls of ts%s" % fn, n, ill, r"\\code\{ts%s\} & \\code\{\w+\} & (\d+) &" % fn)
expect("calls of the hand-written functions, total", sum(calls.values()), ill, r"\((\d+) calls in \d+ shader source files\)")
expect("shader source files", len(files), ill, r"\(\d+ calls in (\d+) shader source files\)")
for banned in ("dot", "normalize", "mix", "clamp", "smoothstep", "reflect", "refract", "cross", "length", "step"):
    for p in files:
        for raw in re.findall(r'R"\((.*?)\)"', open(p).read(), re.S):
            code = re.sub(r"//[^\n]*", "", raw)
            if re.search(r"(?<![A-Za-z0-9_.])%s\s*\(" % banned, code):
                bad.append("%s still calls the built-in %s()" % (os.path.basename(p), banned))
print("ok  no built-in vector call in the shader strings")

# ---- parts per shading model (Section 17), scene totals
exp = json.load(open(os.path.join(ROOT, "report", "data", "scene_export_v10.json")))
shade = collections.Counter(c["shading"] for c in exp["components"] if c["primitive"] != "Group")
sh = tex("A11_shading.tex")
expect("flat parts", shade["Flat"], sh, r"(\d[\d\\,]*) parts are flat", optional=True)
expect("Gouraud parts", shade["Gouraud"], sh, r"(\d[\d\\,]*) Gouraud \(nets", optional=True)
expect("Phong parts", shade["Phong"], sh, r"(\d[\d\\,]*) Phong \(people", optional=True)
nums = tex("gen_numbers.tex")
m = re.search(r"\\newcommand\{\\nLeaves\}\{(.*)\}\s*$", nums, re.M)
leaves = int(re.sub(r"[^\d]", "", m.group(1))) if m else -1
if leaves != exp["leaf_count"]:
    bad.append("nLeaves macro %d, export %d" % (leaves, exp["leaf_count"]))
else:
    print("ok  %-58s %d" % ("scene parts (macro = export)", leaves))

# ---- coordinate sheets: one row per component of the first instance of every design
des = tex("gen_designs.tex")
want = 0
for d in exp["designs"]:
    rid = d["instance_list"][0]["root_id"]
    want += sum(1 for c in exp["components"] if c["design_root"] == rid and c["design"] == d["name"] and c["primitive"] != "Group")
stated = sum(int(x) for x in re.findall(r"extent E--G; (\d+) components", des))
if stated != want:
    bad.append("coordinate sheets list %d components, the export has %d" % (stated, want))
else:
    print("ok  %-58s %d" % ("coordinate sheet rows", want))

# ---- tests
res = os.path.join(ROOT, "build", "test_results.json")
if os.path.exists(res):
    r = json.load(open(res))
    total = len(r) if isinstance(r, list) else r.get("total", len(r.get("tests", [])))
    m = re.search(r"\\newcommand\{\\nTests\}\{(\d+)\}", nums)
    if m and int(m.group(1)) != total:
        bad.append("nTests macro %s, test results %d" % (m.group(1), total))
    else:
        print("ok  %-58s %d" % ("tests (macro = build/test_results.json)", total))

if bad:
    print("\nMISMATCH:")
    for b in bad:
        print("  " + b)
    sys.exit(1)
print("\nall hand-typed numbers agree with the code")
