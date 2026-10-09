"""v10 M4: numbers and tables of the Game mode and Explosions sections.
Values come from the program's export (report/data/scene_export_v10.json: v5 squad / tracker / cv / mission / armour,
combat constants, the measured effect census) or are read out of the source by pattern (particle integrators in
scene/Combat.cpp, the colour ramp in rendering/Effects.cpp), so no number in those sections is typed by hand.
Writes report/sections/gen_game.tex:  \\gv{key} -> value, \\flighttimetable, \\explosiontable, \\bb... macros.
Run: python3 tools/v10/gen_game.py"""
import json
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXP = json.load(open(os.path.join(ROOT, "report", "data", "scene_export_v10.json")))
SEC = os.path.join(ROOT, "report", "sections")


def fmt(v):
    if isinstance(v, float) and v.is_integer():
        v = int(v)
    return f"{v:,}".replace(",", "\\,") if isinstance(v, int) else f"{v:g}"


vals = {}
for grp in ("squad", "tracker", "cv", "mission", "armour"):
    for e in EXP["v5"][grp]:
        vals[e["key"].replace("Squad::", "Squad")] = e["value"]
for e in EXP["combat"]["constants"]:
    vals[e["key"]] = e["value"]
fm = EXP["v5"]["flight_model"]
for k in ("launch_speed", "accel", "ramp_time", "cruise_speed", "fuse_m"):
    vals["flight_" + k] = fm[k]
vals["reloadMeasured"] = round(EXP["simulation"]["reload_seconds_measured"], 2)

# ---- source constants read by pattern (fail loudly if the code changes shape) ----
combat = open(os.path.join(ROOT, "src", "scene", "Combat.cpp")).read()
eff = open(os.path.join(ROOT, "src", "rendering", "Effects.cpp")).read()


def grab(pat, src, n=1):
    m = re.search(pat, src, re.S)
    if not m:
        raise SystemExit("pattern not found: " + pat)
    return [float(x) for x in m.groups()][:n] if n > 1 else float(m.group(1))


upd = combat[combat.index("void Combat::updateParticles"):]
vals["fireRise"] = grab(r"case EffectType::Fire:\s*p\.velocity\.y \+= ([\d.]+)f \* dt", upd)
vals["fireDrag"] = grab(r"case EffectType::Fire:.*?std::exp\(-([\d.]+)f \* dt\)", upd)
vals["smokeRise"] = grab(r"EffectType::Smoke \? ([\d.]+)f", upd)
vals["smokeMaxUp"] = grab(r"if \(p\.velocity\.y > ([\d.]+)f\)", upd)
vals["smokeWind"] = grab(r"1\.0f - std::exp\(-([\d.]+)f \* dt\);\s*// drift", upd)
vals["sparkDrag"] = grab(r"case EffectType::Spark:.*?std::exp\(-([\d.]+)f \* dt\)", upd)
vals["emberG"] = grab(r"case EffectType::Ember:\s*p\.velocity\.y -= ([\d.]+)f \* kGravity", upd)
vals["emberDrag"] = grab(r"case EffectType::Ember:.*?std::exp\(-([\d.]+)f \* dt\)", upd)
vals["dustDrag"] = grab(r"case EffectType::Dust:\s*p\.velocity = p\.velocity \* std::exp\(-([\d.]+)f \* dt\)", upd)
vals["smokeGrowK"] = grab(r"\? std::exp\(-([\d.]+)f \* p\.age\)", upd)
vals["bounce"] = grab(r"p\.velocity\.y = -p\.velocity\.y \* ([\d.]+)f;", upd)
vals["bounceH"] = grab(r"p\.velocity\.x \*= ([\d.]+)f;", upd)
vals["gravity"] = grab(r"constexpr float kGravity = ([\d.]+)f;", combat)
bbm = re.search(r"return vec3\(pow\(T, ([\d.]+)\), pow\(T, ([\d.]+)\), ([\d.]+) \* pow\(T, ([\d.]+)\)\)", eff).groups()
vals["bbR"], vals["bbG"], vals["bbBk"], vals["bbB"] = (float(x) for x in bbm)
vals["fireLife"] = grab(r"T \*= pow\(life, ([\d.]+)\)", eff)
vals["fireEmitA"], vals["fireEmitB"] = grab(r"emit = bb\(T\) \* \(([\d.]+) \+ ([\d.]+) \* T \* T\)", eff, 2)
vals["sparkCool"] = grab(r"heat \* \(1\.0 - t \* \(kind == 5 \? ([\d.]+)", eff)
vals["smokeGlowR"], vals["smokeGlowG"], vals["smokeGlowB"] = grab(r"light \+= vec3\(([\d.]+), ([\d.]+), ([\d.]+)\) \* warm", eff, 3)
vals["smokeGlowDecay"] = grab(r"float warm = heat \* exp\(-age \* \(kind == 1 \? ([\d.]+)", eff)
vals["smokeHeatFloor"], vals["smokeHeatK"] = grab(r"h = ([\d.]+) \+ \(heat - [\d.]+\) \* exp\(-age \* \(kind == 2 \? [\d.]+ : ([\d.]+)\)\)", eff, 2)
fl = re.search(r"emit = fogLit\(mix\(vec3\(([\d., ]+)\), vec3\(([\d., ]+)\), g\) \* g \* f", eff).groups()
vals["flashOuter"], vals["flashInner"] = fl[0].replace(" ", ""), fl[1].replace(" ", "")
deb = open(os.path.join(ROOT, "src", "scene", "Scene.cpp")).read() if os.path.exists(os.path.join(ROOT, "src", "scene", "Scene.cpp")) else ""
for f in os.listdir(os.path.join(ROOT, "src", "scene")):
    if f.startswith("Scene") and f.endswith(".cpp"):
        t = open(os.path.join(ROOT, "src", "scene", f)).read()
        if "updateDebris" in t and "restitution" in t:
            deb = t
vals["debrisSub"] = grab(r"std::ceil\(dt \* ([\d.]+)f\)", deb)
vals["debrisRest"] = grab(r"p\.velocity\.y = -([\d.]+)f \* p\.velocity\.y;", deb)
vals["debrisFric"] = grab(r"p\.velocity\.x \*= ([\d.]+)f;\s*p\.velocity\.z \*= [\d.]+f;\s*p\.spinRate", deb)
vals["debrisSpin"] = grab(r"p\.spinRate \*= ([\d.]+)f;", deb)
vals["debrisRestSpeed"] = grab(r"if \(length\(p\.velocity\) < ([\d.]+)f\)", deb)
hc = open(os.path.join(ROOT, "src", "ai", "HideController.cpp")).read()
hh = open(os.path.join(ROOT, "src", "ai", "HideController.h")).read()
for k in ("kMaxSpeed", "kReplanPeriod", "kInfiltrateReplan", "kSensorForward", "kSensorSpacing", "kLineHalfWidth"):
    vals[k] = grab(k + r" = ([\d.]+)f;", hc)
vals["kKp"], vals["kKd"] = grab(r"kKp = ([\d.]+)f, kKd = ([\d.]+)f", hc, 2)
for k in ("kCellSize", "kEvadeRadius", "kEvadeTime"):
    vals[k] = grab(k + r" = ([\d.]+)f;", hh)
vals["kRoverTrackWidth"] = grab(r"kRoverTrackWidth = ([\d.]+)f;", open(os.path.join(ROOT, "src", "objects", "Rover.h")).read())
vals["kGridCells"] = int(grab(r"kGridCells = (\d+);", hh))
vals["kIrSensorCount"] = int(grab(r"kIrSensorCount = (\d+);", hh))
vals["omegaMax"] = grab(r"\(error - lastError_\) / std::max\(dt, 1e-3f\), -([\d.]+)f", hc)
vals["slowDown"] = grab(r"\(1\.0f - ([\d.]+)f \* std::fabs\(error\)\)", hc)
vals["errScale"] = grab(r"error = \(sum / static_cast<float>\(active\)\) / ([\d.]+)f", hc)

out = ["% generated by tools/v10/gen_game.py -- do not edit",
       "\\makeatletter\\newcommand{\\gv}[1]{\\@ifundefined{gv@#1}{\\textbf{??#1}}{\\csname gv@#1\\endcsname}}\\makeatother"]
for k, v in sorted(vals.items()):
    out.append(f"\\expandafter\\def\\csname gv@{k}\\endcsname{{{v if isinstance(v, str) else fmt(v)}}}")

ft = EXP["v5"]["flight_time"]
out.append("\\newcommand{\\flighttimetable}{\\begin{tabular}{@{}l" + "r" * len(ft) + "@{}}\\toprule")
out.append("distance (m) & " + " & ".join(fmt(e["d"]) for e in ft) + "\\\\")
out.append("flight time (s) & " + " & ".join(f"{e['t']:.2f}" for e in ft) + "\\\\\\bottomrule\\end{tabular}}")

ex = {e["name"]: e for e in EXP["effects"]}
m1 = next(e for n, e in ex.items() if n.startswith("UAV missile"))
m2 = next(e for n, e in ex.items() if n.startswith("Tank shell"))
m3 = next(e for n, e in ex.items() if n.startswith("Tracer impact"))
law = {
    "Flash": "fixed; sphere $e^{-6r^2}$, fades $(1-t)^{1.5}$",
    "Fire": f"$v_y{{+}}{fmt(vals['fireRise'])}\\Delta t$, $\\mathbf{{v}}\\,e^{{-{fmt(vals['fireDrag'])}\\Delta t}}$",
    "Smoke": f"$v_y{{+}}{fmt(vals['smokeRise'])}\\Delta t\\le{fmt(vals['smokeMaxUp'])}$, wind $1-e^{{-{fmt(vals['smokeWind'])}\\Delta t}}$",
    "Spark": f"$-g\\Delta t$, $e^{{-{fmt(vals['sparkDrag'])}\\Delta t}}$, bounce $\\times{fmt(vals['bounce'])}$",
    "Ember": f"$-{fmt(vals['emberG'])}g\\Delta t$, $e^{{-{fmt(vals['emberDrag'])}\\Delta t}}$, bounce",
    "Dust": f"$e^{{-{fmt(vals['dustDrag'])}\\Delta t}}$, slight lift",
    "Shockwave": "ring grows to its radius",
}
col = {
    "Flash": f"mix orange$\\to$white by $g$",
    "Fire": f"$\\mathrm{{bb}}(T)({fmt(vals['fireEmitA'])}+{fmt(vals['fireEmitB'])}T^2)$",
    "Smoke": f"albedo $\\times$ light + glow $e^{{-{fmt(vals['smokeGlowDecay'])}\\,\\mathrm{{age}}}}$",
    "Spark": f"$\\mathrm{{bb}}(0.1+0.9T_n)$, $T_n$ cools",
    "Ember": "$\\mathrm{bb}$, flickers",
    "Dust": "brown albedo, lit",
    "Shockwave": "warm white ring",
}
order = ["Flash", "Fire", "Smoke", "Spark", "Ember", "Shockwave", "Dust"]


def cnt(e, t):
    for x in e["types"]:
        if x["type"] == t:
            return x
    return None


out.append("\\newcommand{\\explosiontable}{\\begin{tabular}{@{}lrrrlll@{}}\\toprule")
out.append(f"Kind & \\multicolumn{{3}}{{c}}{{count at power}} & life (s) & motion per tick & colour\\\\"
           f"\\cmidrule(lr){{2-4}} & {fmt(m3['power'])} & {fmt(m1['power'])} & {fmt(m2['power'])} & & & \\\\\\midrule")
for t in order:
    a, b, c = cnt(m3, t), cnt(m1, t), cnt(m2, t)
    life = f"{b['life_min']:.1f}--{b['life_max']:.1f}" if b and b["life_max"] - b["life_min"] > 0.05 else (f"{b['life_min']:.2f}" if b else "")
    out.append(f"{t} & {a['count'] if a else '--'} & {b['count'] if b else '--'} & {c['count'] if c else '--'} & {life} & "
               f"{law[t]} & {col[t]}\\\\")
out.append(f"\\midrule total & {m3['particles_at_spawn']} & {m1['particles_at_spawn']} & {m2['particles_at_spawn']} & "
           f"\\multicolumn{{3}}{{l}}{{empty after {m1['seconds_until_empty']:.1f}\\,s (power {fmt(m1['power'])})}}\\\\\\bottomrule\\end{{tabular}}}}")
open(os.path.join(SEC, "gen_game.tex"), "w").write("\n".join(out) + "\n")
print("wrote gen_game.tex:", len(vals), "values")
