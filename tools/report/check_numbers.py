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
expect("calls of the hand-written functions, total", sum(calls.values()), ill, r"\((\d+) calls (?:in|across) \d+ shader source files\)")
expect("shader source files", len(files), ill, r"\(\d+ calls (?:in|across) (\d+) shader source files\)")
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
stated = sum(int(x) for x in re.findall(r"extent E--G; (\d+) components?\.", des))
if stated != want:
    bad.append("coordinate sheets list %d components, the export has %d" % (stated, want))
else:
    print("ok  %-58s %d" % ("coordinate sheet rows", want))

# ---- constants of the equations and hand-written tables
import math  # noqa: E402


def grab(path, rx, group=1):
    m = re.search(rx, src(path), re.S)
    if not m:
        bad.append("pattern %s not found in %s" % (rx, path))
        return float("nan")
    return float(m.group(group))


def near(what, value, text, pattern, digits):
    """The report prints 'value' rounded to 'digits' decimals where 'pattern' matches."""
    hits = re.findall(pattern, text)
    if not hits:
        bad.append("%s: the report sentence was not found (%s)" % (what, pattern))
        return
    for h in hits:
        if abs(float(h) - value) > 0.5 * 10 ** (-digits) + 1e-12:
            bad.append("%s: report says %s, the value is %.6g" % (what, h, value))
    print("ok  %-58s %s" % (what, hits[0]))


day, post, texm, proj, trans, hard = (tex(n) for n in ("B03_daynight.tex", "A12b_post.tex", "A12_texture.tex", "A05_projection.tex",
                                                       "A02_transforms.tex", "B10_hard.tex"))
ky = r"\+ ([\d.]+)f \* std::pow\(e \+ ([\d.]+)f, -([\d.]+)f\)"
m = re.search(r"\\sin e\+([\d.]+)\\,\(e\+([\d.]+)\^\\circ\)\^\{-([\d.]+)\}", day)
if not m or [float(x) for x in m.groups()] != [grab("src/scene/DayCycle.cpp", ky, i) for i in (1, 2, 3)]:
    bad.append("air mass formula of Section 22 differs from DayCycle.cpp")
else:
    print("ok  %-58s %s" % ("air mass constants (Kasten-Young)", ", ".join(m.groups())))
tm = r"x \* \(x \+ ([\d.]+)f\) - ([\d.]+)f;\s*const float b = x \* \(([\d.]+)f \* x \+ ([\d.]+)f\) \+ ([\d.]+)f"
m = re.search(r"\\vect v\+([\d.]+)\)-([\d.]+)\}\{\\vect v\(([\d.]+)\\,\\vect v\+([\d.]+)\)\+([\d.]+)\}", post)
if not m or [float(x) for x in m.groups()] != [grab("src/math/ToneMap.cpp", tm, i) for i in (1, 2, 3, 4, 5)]:
    bad.append("tone curve of Section 19 differs from ToneMap.cpp")
else:
    print("ok  %-58s %s" % ("tone curve constants", ", ".join(m.groups())))
near("tone curve: lowest exposure", grab("src/math/ToneMap.h", r"kAgxMinEv = -([\d.]+)f"), post, r"\\log_2\\vect v,-([\d.]+),", 2)
near("tone curve: highest exposure", grab("src/math/ToneMap.h", r"kAgxMaxEv = ([\d.]+)f"), post, r"\\log_2\\vect v,-[\d.]+,([\d.]+)\)", 2)
expect("fog density at night", grab("src/rendering/Lighting.h", r"float fogDensity = ([\d.]+)f"), post, r"\\rho=([\d.]+)\\ \\text\{\(night\)\}")
expect("fog density by day", grab("src/scene/DayCycle.cpp", r"kDayFogDensity = ([\d.]+)f"), post, r"\\ldots\\ ([\d.]+)\\,\\text\{m\}")
expect("value noise frequency ratio", grab("src/rendering/ShaderSources.cpp", r"valueNoise\(p\); p = p \* ([\d.]+) \+"), texm, r"noise\}\\big\(([\d.]+)\^\{\\,o\}")
expect("shadow depth bias, cascades", grab("src/rendering/ShaderSources.cpp", r"float zr = p\.z - ([\d.]+);"), ill, r"\$b=([\d.]+)\$ \(cascades\)")
expect("shadow depth bias, searchlight", grab("src/rendering/ShaderSources.cpp", r"uSpotShadow, vec3\([^;]*p\.z - ([\d.]+)\)"), ill, r"\$([\d.]+)\$ \(searchlight\)")
nz, fz = grab("src/scene/Camera.h", r"float nearZ = ([\d.]+)f"), grab("src/scene/Camera.h", r"farZ = ([\d.]+)f")
expect("far / near ratio", fz / nz, hard, r"F/N=([\d\\,]+)\$")
expect("near plane", nz, hard, r"the ([\d.]+)\\,m near plane")
near("isometric elevation", math.degrees(math.atan(1 / math.sqrt(2))), proj, r"\\arctan\(1/\\sqrt2\)=([\d.]+)", 2)
near("isometric foreshortening", math.sqrt(2 / 3), proj, r"\\sqrt\{2/3\}=([\d.]+)", 3)
near("focal factor at 50 degrees", 1 / math.tan(math.radians(25)), proj, r"50\^\\circ\$ gives \$f=([\d.]+)", 2)
near("focal factor at 60 degrees", 1 / math.tan(math.radians(30)), proj, r"60\^\\circ\$ gives \$f=([\d.]+)", 2)
near("wrong normal: angle to the true normal", math.degrees(math.acos(2 * 0.25 / (0.25 ** 2 + 1))), trans, r"\(wrong\) & [^&]* & \$([\d.]+)\^\\circ", 1)

# shadow settings per preset (the table of Section 16 against RendererFrame.cpp: qualityProfile)
hdr = src("src/rendering/Renderer.h")
body = re.search(r"struct QualityProfile \{(.*?)\n\};", hdr, re.S).group(1)
base = {k: v.strip().rstrip("f") for k, v in re.findall(r"(\w+) = ([^;,/]+)", re.sub(r"//[^\n]*", "", body))}
fn = src("src/rendering/RendererFrame.cpp")
fn = fn[fn.index("QualityProfile qualityProfile(Quality q)"):]
fn = fn[:fn.index("\n}\n")]
prof = {}
for name, blk in re.findall(r"q == Quality::(\w+)\) \{(.*?)\n    \}", fn, re.S) + [("High", re.search(r"\} else \{\s*// HIGH(.*?)\n    \}", fn, re.S).group(1))]:
    prof[name] = dict(base, **{k: v.strip().rstrip("f") for k, v in re.findall(r"p\.(\w+) = ([^;]+);", blk)})


def cell(q, field, gate=None, unit=""):
    if gate and prof[q][gate] != "true":
        return "--"
    v = float(prof[q][field])
    return "off" if field == "spotShadowRes" and v == 0 else ("%g" % v) + unit


for label, field, gate, unit in (("Key-light map (2 cascades)", "moonShadowRes", None, ""), ("Searchlight map", "spotShadowRes", None, ""),
                                 ("Near cascade covers", "cascadeSplit", None, "\\,m"), ("PCF taps $n$", "pcfTaps", None, ""),
                                 ("PCSS blocker taps", "blockerTaps", "pcss", ""), ("Contact-shadow steps", "contactSteps", "contactShadows", "")):
    want = " & ".join(cell(q, field, gate, unit) for q in ("Low", "High", "Ultra", "Epic"))
    m = re.search(re.escape(label) + r" & (.*?)\\\\", ill)
    if not m or m.group(1).strip() != want:
        bad.append("shadow table, %s: report '%s', code '%s'" % (label, m.group(1) if m else "missing", want))
    else:
        print("ok  %-58s %s" % ("shadow table: " + label, want.replace("\\,", " ")))

# ---- the film (Section 33)
import subprocess  # noqa: E402

film = os.path.join(ROOT, "report", "video", "thermalsense_final_720p.mp4")
probe = os.path.expanduser("~/.local/bin/ffprobe")
if os.path.exists(film) and os.path.exists(probe):
    r = subprocess.run([probe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=r_frame_rate:format=duration",
                        "-of", "default=nw=1:nk=1", film], capture_output=True, text=True).stdout.split()
    secs = int(round(float(r[1])))
    fps = float(r[0].split("/")[0]) / float(r[0].split("/")[1])
    con = re.sub(r"\\ab[pd]?\{\d+\}\{([^{}]*)\}", r"\1", tex("C03_conclusion.tex"))
    expect("film length, minutes", secs // 60, con, r"film of (\d+)\\,min")
    expect("film length, seconds", secs % 60, con, r"min\\,(\d+)\\,s")
    expect("film frames per second", fps, con, r"(\d+)\\,fps")

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
