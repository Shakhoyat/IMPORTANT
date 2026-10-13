"""Section 19 (object decomposition): every design of the export's "designs" list broken into primitives.
Inputs: report/data/scene_export_v10.json (schema 9: designs, components with design / design_root), the shape-view
captures in report/figures/raw/shapes/shapeview (tools/report/capture_shapes.py), report/data/vegetation_assets.json.
Writes report/sections/gen_designs.tex:
  \\designatlasA/B         thumbnails of every design by family, "name - parts x instances" (two float pages)
  \\designtable            Table 19.1: one row per design, x n, parts, primitive columns (key chips), totals
  \\designcards<Family>    the cards of one family (shape-view figure + legend, one sentence, parts table)
  \\munitionlineup         the munitions at one scale with a 10 cm scale bar
  \\buildstripKey<Tag>     build stages in the colour key (UAV, tank, helicopter, command bunker)
and report/data/designs_check.json (legend = parts table = Table 19.1 row, per design) for tools/report/check_counts.py.
Cropped JPEGs go to report/figures/img/shapes/.
Run: <venv with numpy + pillow> python tools/report/gen_designs.py"""
import collections
import json
import math
import os
import re
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import primkey  # noqa: E402
import abbrev  # noqa: E402  (ABBR and the first-use expansion; Section 7 writes its own)
import coord_sheet as cs  # noqa: E402  (spreadsheet-style table of every component in the design frame)

SEEN = set()   # short forms already written out in Section 7, in document order

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXP = json.load(open(os.path.join(ROOT, "report", "data", "scene_export_v10.json")))
VEG = json.load(open(os.path.join(ROOT, "report", "data", "vegetation_assets.json")))
RAW = os.path.join(ROOT, "report", "figures", "raw", "shapes", "shapeview")
IMG = os.path.join(ROOT, "report", "figures", "img", "shapes")
OUT = os.path.join(ROOT, "report", "sections", "gen_designs.tex")
CHECK = os.path.join(ROOT, "report", "data", "designs_check.json")
os.makedirs(IMG, exist_ok=True)

C = EXP["components"]
BY_ID = {c["id"]: c for c in C}
KIDS = collections.defaultdict(list)
for c in C:
    KIDS[c["parent_id"]].append(c)
DESIGNS = EXP["designs"]
FAMILIES = ["people", "vehicles and their weapons", "buildings", "fortifications", "vegetation", "props",
            "terrain and backdrop"]
# The report groups each vehicle or launcher with the munition it fires (the export keeps two families).
VW_ORDER = ["Main battle tank", "Autonomous UAV", "UAV round", "SAM launcher", "SAM missile", "MLRS rocket truck",
            "MLRS rocket", "AA Gatling gun", "AA shell casing", "Scout helicopter", "Cargo truck", "Target rover"]
FIRED_BY = {"UAV round": "Autonomous UAV", "SAM missile": "SAM launcher", "MLRS rocket": "MLRS rocket truck",
            "AA shell casing": "AA Gatling gun"}
for _d in EXP["designs"]:
    if _d["family"] in ("vehicles and aircraft", "weapons and munitions"):
        assert _d["name"] in VW_ORDER, _d["name"]
        _d["family"] = "vehicles and their weapons"
PRIM_COLS = [k for k in primkey.ORDER if k != "Grass blade"]
CUT_DESIGNS = {"Command bunker", "Ammunition bunker", "Barracks hall"}
SKY = {"Moon", "Sky clock (sun)"}   # drawn by the sky pass, not as lit parts: only the primitives panel is shown


NAMES = {"GENERATOR ROOM DOOR": "Generator room door", "BARRACKS DOOR": "Barracks door", "Gatling Gun": "Gatling gun"}


def tex(s):
    """Text for LaTeX; internal node names in capitals are written in sentence case, 6x6 as a product."""
    s = str(s)
    for a, b in NAMES.items():
        s = s.replace(a, b)
    s = s.replace("&", "\\&").replace("_", "\\_").replace("#", "\\#").replace("%", "\\%")
    return re.sub(r"(?<=\d)x(?=\d)", "$\\\\times$", s)


def slug(t):
    o = ""
    for ch in t:
        if ch.isalnum():
            o += ch.lower()
        elif o and o[-1] != "_":
            o += "_"
    return o.rstrip("_")


def lower_name(name):
    """A design name inside a sentence: lower case except acronyms (UAV, SAM, MLRS, AA, HESCO)."""
    return " ".join(w if (w.isupper() and len(w) > 1) or w in ("Gatling", "Quonset") else w.lower() for w in name.split())


def macro(name):
    return re.sub(r"[^A-Za-z]", "", name.title())


def num(n):
    return "{:,}".format(n).replace(",", "\\,")


def prim_key(c):
    k = c["primitive"]
    if k == "Cube":
        s = c["S_rest"]
        if max(s) - min(s) > 1e-3 * max(max(s), 1e-6):
            return "Cuboid"
    return k


def mat(m16):
    return np.array(m16, float).reshape(4, 4)


def kind_norm(name):
    """Section / parent names without side or index suffix (as the program's partKindName)."""
    toks = name.split()
    while len(toks) > 1 and re.fullmatch(r"(L|R|W|E|N|S|FL|FR|RL|RR|[A-D]|\d+|L\d+|R\d+|[A-Z]\d+|\#\d+|(Front|Rear)-(left|right))", toks[-1]):
        toks.pop()
    return " ".join(toks)


# ------------------------------------------------------------------------------------------------ images
def crop_to(src, box, dst, maxw=1500):
    im = Image.open(src).convert("RGB").crop(tuple(box))
    if im.width > maxw:
        im = im.resize((maxw, max(1, int(im.height * maxw / im.width))), Image.LANCZOS)
    im.save(dst, quality=88)
    return im.width / im.height


def shape(design, cut=False):
    sl = slug(design) + ("_cut" if cut else "")
    jp = os.path.join(RAW, sl + ".json")
    if not os.path.exists(jp):
        sys.exit("missing capture " + jp)
    j = json.load(open(jp))
    am = crop_to(os.path.join(RAW, j["model"]), j["crop_model"], os.path.join(IMG, sl + "_model.jpg"))
    ap = crop_to(os.path.join(RAW, j["prims"]), j["crop_prims"], os.path.join(IMG, sl + "_prims.jpg"))
    j["_slug"], j["_am"], j["_ap"] = sl, am, ap
    return j


KEEP = "\x00"   # marks an equation row that must stay on the page of the next row

# ------------------------------------------------------------------------------------------------ number patterns
def fmtv(v):
    return "{-}%d" % -v if v < 0 else "%d" % v   # braces: a minus sign, never a spaced subtraction


def pattern(vals):
    """Integer values of one coordinate over a group of parts -> a short pattern."""
    v = [int(round(x)) for x in vals]
    u = sorted(set(v))
    if len(u) == 1:
        return fmtv(u[0])
    if len(u) == 2 and u[0] == -u[1]:
        return "\\pm %d" % u[1]
    pos = sorted(set(abs(x) for x in v))
    if len(u) >= 4 and set(u) == set(pos) | set(-x for x in pos) and len(pos) >= 2:   # symmetric progression
        d = [b - a for a, b in zip(pos, pos[1:])]
        if max(d) - min(d) <= 1:
            return "\\pm(%d{+}%di)" % (pos[0], d[0])
    if len(u) >= 3:
        d = [b - a for a, b in zip(u, u[1:])]
        if max(d) - min(d) <= 1 and d[0] > 0:
            return "%s{+}%di" % (fmtv(u[0]), d[0])
    if len(u) <= 3:
        return "\\{" + ",\\allowbreak ".join(fmtv(x) for x in u) + "\\}"
    return "%s..%s" % (fmtv(u[0]), fmtv(u[-1]))


def size_txt(S):
    """S (cm) of a group of parts: one size or per-axis ranges."""
    out = []
    for k in range(3):
        a = [s[k] for s in S]
        lo, hi = min(a), max(a)
        f = (lambda x: ("%.0f" % x if abs(x) >= 1000 else "%.3g" % x))   # (no exponent form for 10 m and above)
        out.append(f(lo) if hi - lo < 0.05 * max(hi, 1e-6) + 0.05 else "%s..%s" % (f(lo), f(hi)))
    return "$" + "{\\times}\\allowbreak ".join(out) + "$"


def rot_txt(R):
    if all(abs(r) < 0.5 for rr in R for r in rr):
        return "--"
    return "$(" + ",\\allowbreak\\,".join(pattern([r[k] for r in R]) for k in range(3)) + ")$"


def pos_txt(T):
    return "$(" + ",\\allowbreak\\,".join(pattern([t[k] for t in T]) for k in range(3)) + ")$"


# ------------------------------------------------------------------------------------------------ design data
def own_parts(design, root_id):
    return [c for c in C if c["design_root"] == root_id and c["design"] == design and c["primitive"] != "Group"]


def root_frame(root_id):
    return np.linalg.inv(mat(BY_ID[root_id]["world_matrix"]))


def path_groups(c, root_id):
    """Group names between the design root (exclusive) and the part (exclusive), top first."""
    p, out = BY_ID.get(c["parent_id"]), []
    while p and p["id"] != root_id:
        out.append(p)
        p = BY_ID.get(p["parent_id"])
    return list(reversed(out))


def strip_chain(root_id):
    """Groups directly under the root that hold everything (Hull, Pelvis, Body): not sections."""
    chain = []
    cur = root_id
    while True:
        kids = KIDS[cur]
        groups = [k for k in kids if k["primitive"] == "Group"]
        leaves = [k for k in kids if k["primitive"] != "Group"]
        if len(groups) == 1 and not leaves and len(chain) < 3:
            chain.append(groups[0]["id"])
            cur = groups[0]["id"]
        else:
            return chain


def parts_rows(d):
    """Rows of the parts table: [(section, [row, ...])], rows = dict(kind, n, key, size, T, R, parent, dagger)."""
    inst = d["instance_list"][0]
    root_id = inst["root_id"]
    own = own_parts(d["name"], root_id)
    inv = root_frame(root_id)
    skip = set(strip_chain(root_id))
    body = BY_ID[list(skip)[-1]]["name"] if skip else ""
    secs = collections.OrderedDict()
    for c in own:
        pg = [g for g in path_groups(c, root_id) if g["id"] not in skip]
        sec = kind_norm(pg[0]["name"]) if pg else body
        w = mat(c["world_matrix"])
        p = inv @ w[:, 3]
        parent = BY_ID.get(c["parent_id"])
        pname = kind_norm(parent["name"]) if parent and parent["id"] != root_id else "root"
        key = (c["part_kind"], prim_key(c))
        g = secs.setdefault(sec, collections.OrderedDict()).setdefault(key, dict(kind=c["part_kind"], key=prim_key(c), S=[], T=[], R=[],
                                                                                  parents=collections.OrderedDict(), dagger=0))
        g["S"].append([v * 100 for v in c["S_rest"]])
        g["T"].append([v * 100 for v in p[:3]])
        g["R"].append(c["R_deg"])
        g["parents"][pname] = 1
        g["dagger"] += 1 if c["hidden_at_export"] else 0
    # nested designs: one row in the section of their root's parent
    nested = collections.OrderedDict()
    for c in C:
        if c["primitive"] == "Group" and c["design_root"] == c["id"] and c["design"] != d["name"]:
            # an instance root inside this instance?
            p = BY_ID.get(c["parent_id"])
            while p and p["id"] != root_id and p["design_root"] == root_id:
                p = BY_ID.get(p["parent_id"])
            if p and p["id"] == root_id:
                pg = [g for g in path_groups(c, root_id) if g["id"] not in skip]
                sec = kind_norm(pg[0]["name"]) if pg else body
                nested.setdefault((sec, c["design"]), []).append(c)
    for c in C:   # one-part nested designs (AA shell casings): leaves owned by another design under this root
        if c["primitive"] != "Group" and c["design_root"] == c["id"] and c["design"] != d["name"]:
            p = BY_ID.get(c["parent_id"])
            while p and p["id"] != root_id and p["design_root"] == root_id:
                p = BY_ID.get(p["parent_id"])
            if p and p["id"] == root_id:
                pg = [g for g in path_groups(c, root_id) if g["id"] not in skip]
                sec = kind_norm(pg[0]["name"]) if pg else body
                nested.setdefault((sec, c["design"]), []).append(c)
    out = []
    for sec, rows in secs.items():
        out.append((sec, list(rows.values()), [(nd, len(v)) for (s2, nd), v in nested.items() if s2 == sec]))
    for (s2, nd), v in nested.items():
        if s2 not in secs:
            out.append((s2, [], [(nd, len(v))]))
    return out, own


def parts_table(d, rows):
    o = ["{\\fontsize{7}{8.2}\\selectfont\\setlength{\\tabcolsep}{2.6pt}\\renewcommand{\\arraystretch}{1.02}",
         "\\begin{longtable}{@{}>{\\raggedright\\arraybackslash}p{3.0cm}l>{\\raggedright\\arraybackslash}p{2.6cm}"
         ">{\\raggedright\\arraybackslash}p{4.0cm}>{\\raggedright\\arraybackslash}p{2.2cm}>{\\raggedright\\arraybackslash}p{2.3cm}@{}}",
         "\\toprule Part ($\\times n$) & Shape & Size $S$ (cm) & Position $T$ (cm, design frame) & Rotation $R$ ($^\\circ$) & Parent\\\\\\midrule\\endhead",
         "\\bottomrule\\endfoot"]
    multi = len(rows) > 1
    for sec, rs, nest in rows:
        if multi:
            o.append("\\multicolumn{6}{@{}l}{\\textit{%s}}\\\\*[0.2mm]" % tex(sec or d["name"]))
        for r in rs:
            n = len(r["S"])
            nm = tex(r["kind"]) + (" $\\times$%d" % n if n > 1 else "") + ("\\,$^\\dagger$" if r["dagger"] else "")
            par = ", ".join(tex(p) for p in r["parents"])
            o.append("%s & %s & %s & %s & %s & %s\\\\" % (nm, primkey.chip(r["key"]), size_txt(r["S"]), pos_txt(r["T"]), rot_txt(r["R"]), par))
        for nd, k in nest:
            o.append("\\textit{%s} $\\times$%d & \\multicolumn{5}{@{}l}{\\textit{nested design, drawn pale; its parts are counted in its own card "
                     "(Figure~\\ref{fig:d-%s})}}\\\\" % (tex(nd), k, slug(nd)))
    o.append("\\end{longtable}\\addtocounter{table}{-1}}")   # (no caption: the table counter is not stepped)
    return "\n".join(o)


# ------------------------------------------------------------------------------------------------ placement sentence
def compass(x, z):
    # scene axes: +x east, +z north (Section 4 site plan)
    a = (math.degrees(math.atan2(x, z)) + 360) % 360
    names = ["north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"]
    return names[int((a + 22.5) // 45) % 8]


def sentence(d):
    inst = d["instance_list"][0]
    root = BY_ID[inst["root_id"]]
    purpose = root["purpose"].rstrip(".") if root["purpose"] else ""
    x, y, z = inst["position"]
    dist = math.hypot(x, z)
    n = d["instances"]
    where = ("at the origin of the camp" if dist < 1.0 else
             "%.0f\\,m %s of the camp centre" % (dist, compass(x, z)))
    s = ""
    if purpose:
        s += tex(purpose[0].upper() + purpose[1:]) + ". "
    if n == 1:
        s += "One instance, %s" % where
    else:
        s += "%d instances; the first stands %s" % (n, where)
    if d["nested_in"]:
        s += ", as part of the %s" % tex(lower_name(d["nested_in"]))
    return s + "."   # (the root placement itself is the first equation of the card's chain)


# ------------------------------------------------------------------------------------------------ generation rules
def rel_positions(d):
    root_id = d["instance_list"][0]["root_id"]
    inv = root_frame(root_id)
    return [(c, (inv @ mat(c["world_matrix"])[:, 3])[:3]) for c in own_parts(d["name"], root_id)]


def radius_stats(ps):
    r = [math.hypot(p[0], p[2]) for p in ps]
    return min(r), max(r), sum(r) / len(r)


# Caption titles that would otherwise get a short form expanded twice ("SAM missile": the full form already ends in "missile")
CAPTION_NAME = {"SAM missile": "Surface-to-air missile"}


def rule(d):
    nm = d["name"]
    rp = rel_positions(d)
    by = collections.defaultdict(list)
    for c, p in rp:
        by[c["part_kind"]].append((c, p))
    if nm == "HESCO perimeter":
        cells = by["HESCO cell"] + by["HESCO cell (upper)"]
        lo, hi, r = radius_stats([p for _, p in cells])
        s = by["HESCO cell"][0][0]["S"]
        return ("Generation rule: $N=\\mathrm{round}(2\\pi r/1\\,\\mathrm{m})$ slots around a circle of radius $r=%.1f$\\,m; slot $i$ "
                "holds a %.2f$\\times$%.2f$\\times$%.2f\\,m cell at angle $2\\pi(i+\\frac12)/N$, turned along the tangent; slots in the gate "
                "opening are empty, slots beside the gate and the four towers get a second cell on top (%d lower + %d upper cells)."
                % (r, s[0], s[1], s[2], len(by["HESCO cell"]), len(by["HESCO cell (upper)"])))
    if nm == "Concertina wire":
        _, _, r = radius_stats([p for _, p in by["Wire coil"]])
        return ("Generation rule: one helix coil on every 4th wall slot (axis along the wall tangent, %.2f\\,m above the cell "
                "top), %d coils at radius %.1f\\,m; neighbouring coils are tied by a razor strand (a thin cylinder from coil top to coil "
                "top, %d strands; none across the gate)." % (by["Wire coil"][0][1][1] - 1.4, len(by["Wire coil"]), r, len(by["Razor strand"])))
    if nm == "Perimeter fence":
        posts = by["Fence post"] + by["Gate post"]
        _, _, r = radius_stats([p for _, p in posts])
        return ("Generation rule: %d posts every 18$^\\circ$ on a circle of radius %.1f\\,m (the two ends are steel gate posts), three wire "
                "strands (thin cylinders) across every bay but the gate bay (%d wires), two gate leaves of a frame and a cross brace."
                % (len(posts), r, len(by["Fence wire"])))
    if nm == "Tyre tracks":
        _, _, r = radius_stats([p for _, p in by["Tyre track"]])
        return ("Generation rule: two arcs of the service ring (radius %.1f\\,m), cut into steps of at most 6$^\\circ$; each step is a pair of thin "
                "boxes (ruts) at $r\\pm0.6$\\,m turned along the tangent (%d ruts)." % (r, len(by["Tyre track"])))
    if nm == "Distant hills":
        a = [p for _, p in by["Hill"]]
        b = [p for _, p in by["Far hill"]]
        return ("Generation rule: two rings of flattened spheres, half buried: %d hills at %.0f--%.0f\\,m and %d far hills at %.0f--%.0f\\,m from "
                "the centre, evenly spaced with a random jitter of $\\pm8^\\circ$, each turned along the tangent."
                % (len(a), radius_stats(a)[0], radius_stats(a)[1], len(b), radius_stats(b)[0], radius_stats(b)[1]))
    if nm == "Treeline pine":
        xs = [i["position"] for i in d["instance_list"]]
        r = [math.hypot(p[0], p[2]) for p in xs]
        return ("Generation rule: tree height $h$ per instance; lower cone $0.56h$ wide and $0.62h$ tall, upper cone $0.36h$ wide and "
                "$0.55h$ tall, turned 30$^\\circ$. The %d instances stand on a ring at %.0f--%.0f\\,m." % (len(xs), min(r), max(r)))
    if nm == "Pine":
        return ("Generation rule: tree height $h$; trunk cylinder $0.045h+0.05$ wide and $0.8h$ tall; 5--7 cone whorls (7 above 3.8\\,m, 6 above "
                "3\\,m) from the skirt at 0.6\\,m to the tip, radius $0.33h(1-0.78k)$ for $k=0..1$, each turned and tilted by a few degrees "
                "with a seeded random generator.")
    if nm == "Broadleaf tree":
        return ("Generation rule: size $s$; trunk cylinder $0.22s\\times1.9s$; three limbs from 1.45$s$ at 120$^\\circ$ steps; 12 sphere clumps "
                "on an ellipsoid shell around $(0,2.55s,0)$ on a golden-angle spiral, plus two inner fillers.")
    if nm == "Bush":
        return "Generation rule: 5--7 flattened spheres of radius $(0.28..0.48)s$ scattered within $0.38s$ of the centre (seeded)."
    if nm == "Boulder":
        return "Generation rule: 3--4 squashed, rotated spheres, the first full size, the others 45--75\\,\\% of it, partly buried (seeded)."
    if nm == "Camouflage net":
        return ("Generation rule: a $w\\times d$ net is cut into a grid of about 1.15\\,m panels (thin boxes) that sag as "
                "$h(u,v)=h_0-s\\sin\\pi u\\sin\\pi v$ and tilt with the slope; 12\\,\\%% of the panels are left out at random; poles at the corners "
                "(and mid-edge for sides over 5.5\\,m), four edge ropes. The %d instances differ in size: %s parts."
                % (d["instances"], ", ".join(str(x) for x in d["parts_per_instance"])))
    if nm == "Earth revetment":
        return ("Generation rule: a U of three earth walls (core box, outer slope tilted 28$^\\circ$, inner face tilted 10$^\\circ$) with a "
                "course of capsule sandbags every 0.55\\,m along each crest. Instances: %s parts." % ", ".join(str(x) for x in d["parts_per_instance"]))
    if nm == "Sandbag wall":
        return ("Generation rule: an arc of capsules; row $r$ has one bag fewer than the row below and is shifted by half a bag (running "
                "bond), each bag turned along the arc. Instances: %s parts." % ", ".join(str(x) for x in d["parts_per_instance"]))
    if nm == "Cut branches":
        return ("Generation rule: $n=\\max(3,\\lfloor L/0.9\\rfloor)$ branches along the vehicle side, each a stem cylinder leaning "
                "14--28$^\\circ$, a sphere of foliage and a cone tip; three sphere brush clumps at the foot.")
    if nm == "AA gun surround":
        return "Generation rule: a ring of capsule sandbags around the round pad in two courses, the upper one shifted half a bag (running bond), three ammunition crates outside."
    if nm == "Radar surround":
        return "Generation rule: a square fence of posts and three wire strands per bay (at 0.4, 1.0 and 1.7 m) around the concrete pad, with one warning sign."
    return ""


# ------------------------------------------------------------------------------------------------ transformation chain
def trs(c):
    """M = T Ry Rx Rz S from the export's T, R_deg (x pitch, y yaw, z roll) and S (math/Math3D.cpp, scene/Transform.h)."""
    tx, ty, tz = c["T"]
    ax, ay, az = [math.radians(v) for v in c["R_deg"]]
    T = np.eye(4)
    T[:3, 3] = [tx, ty, tz]
    cx, sx, cy, sy, cz, sz = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay), math.cos(az), math.sin(az)
    Rx = np.array([[1, 0, 0, 0], [0, cx, -sx, 0], [0, sx, cx, 0], [0, 0, 0, 1]])
    Ry = np.array([[cy, 0, sy, 0], [0, 1, 0, 0], [-sy, 0, cy, 0], [0, 0, 0, 1]])
    Rz = np.array([[cz, -sz, 0, 0], [sz, cz, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]])
    S = np.diag(list(c["S"]) + [1.0])
    return T @ Ry @ Rx @ Rz @ S


def close(a, b):
    return float(np.max(np.abs(a - b) / np.maximum(1.0, np.abs(b))))


CHAIN_ERR = []


def ancestors(c):
    """Nodes from the scene root down to c (inclusive)."""
    out = []
    while c:
        out.append(c)
        c = BY_ID.get(c["parent_id"])
    return list(reversed(out))


def chain_check(c):
    """Product of every local matrix from the scene root to c = the exported world matrix of c (and T Ry Rx Rz S = local)."""
    W = np.eye(4)
    for a in ancestors(c):
        L = mat(a["local_matrix"])
        e = close(trs(a), L)
        if e > 1e-3:
            sys.exit("%s: T Ry Rx Rz S != exported local matrix (error %.2g)" % (a["id"], e))
        W = W @ L
    e = close(W, mat(c["world_matrix"]))
    if e > 1e-3:
        sys.exit("%s: product of the chain != exported world matrix (error %.2g)" % (c["id"], e))
    CHAIN_ERR.append(e)
    return e


def fnum(v, nd=2):
    t = ("%%.%df" % nd) % v
    return t[1:] if t.startswith("-") and float(t) == 0 else t


def mnum(v, nd=3):
    if abs(v) < 5e-4:
        return "0"
    if abs(v - round(v)) < 5e-4:
        return "%d" % round(v)
    return fnum(v, nd)


def bmat(M, nd=3):
    rows = [" & ".join(mnum(v, nd) for v in r) for r in M]
    return "\\begin{bmatrix}" + "\\\\".join(rows) + "\\end{bmatrix}"


def factors(c):
    t, r, sc = c["T"], c["R_deg"], c["S"]
    parts = ["T(%s)" % ",".join(fnum(v, 3) for v in t)]
    for ax, nm in [(1, "y"), (0, "x"), (2, "z")]:
        if abs(r[ax]) > 0.05:
            parts.append("R_%s(%s^\\circ)" % (nm, fnum(r[ax], 1)))
    if any(abs(v - 1) > 1e-4 for v in sc):
        parts.append("S(%s)" % ",".join(fnum(v, 3) for v in sc))
    return "\\,".join(parts)


def mname(c):
    return "M_{\\text{%s}}" % tex(c["name"]).replace("$", "")


def chain_block(d):
    inst = d["instance_list"][0]
    root = BY_ID[inst["root_id"]]
    own = [c for c in own_parts(d["name"], root["id"]) if not c["hidden_at_export"] and min(abs(v) for v in c["S"]) > 1e-6]
    if root["primitive"] != "Group" or not own:
        part = root
    else:
        def score(c):
            return (len(ancestors(c)), any(abs(v) > 0.05 for v in c["R_deg"]), -c["dfs_index"])
        part = max(own, key=score)
    chain_check(root)
    chain_check(part)
    par = BY_ID.get(root["parent_id"])
    sl = slug(d["name"])
    rootf = ("W_{\\text{parent}}\\," if par else "") + factors(root)
    under = " (parent: %s)" % tex(par["name"]) if par else ""

    def line(lhs, rhs_sym, M, label):
        plain = re.sub(r"\\\\[a-zA-Z]+|[{}^_]", "", rhs_sym)
        if len(plain) <= 46:
            return ["%s&=%s=%s\\label{%s}" % (lhs, rhs_sym, bmat(M), label)]
        return ["%s&=%s\\nonumber" % (lhs, rhs_sym) + KEEP, "&=%s\\label{%s}" % (bmat(M), label)]

    eqs = line("W_{\\text{root}}", rootf, mat(root["world_matrix"]), "eq:ch-%s" % sl)
    if part is root:
        txt = "\\textbf{Chain.} One part%s: its world matrix is the root's, Eq.~\\eqref{eq:ch-%s}." % (under, sl)
        if np.abs(mat(root["world_matrix"])[:3, :3]).max() < 1e-3:
            txt += " The part waits in a pool with size 0 until it is used; the table and the sheet give its size in use."
    else:
        path = ancestors(part)
        groups = path[path.index(root) + 1:-1]
        names = ["W_{\\text{root}}"] + [mname(g) for g in groups] + ["M"]
        rows = [names[i:i + 5] for i in range(0, len(names), 5)]
        eqs += line("M", factors(part), mat(part["local_matrix"]), "eq:chm-%s" % sl)
        eqs.append("W_{\\text{part}}&=%s\\nonumber" % "\\,".join(rows[0]) + KEEP)
        for r in rows[1:]:
            eqs.append("&\\quad\\,%s\\nonumber" % "\\,".join(r) + KEEP)
        eqs.append("&=%s\\label{eq:chw-%s}" % (bmat(mat(part["world_matrix"])), sl))
        txt = ("\\textbf{Chain.} Root%s: Eq.~\\eqref{eq:ch-%s}. Part %s (%s): local $M$, Eq.~\\eqref{eq:chm-%s}; world through %s, "
               "Eq.~\\eqref{eq:chw-%s}."
               % (under, sl, tex(part["name"]), prim_key(part).lower(), sl,
                  "%d group%s" % (len(groups), "" if len(groups) == 1 else "s") if groups else "the root alone", sl))
    body = "".join(e.replace(KEEP, "") + ("" if i == len(eqs) - 1 else "\\\\*\n" if e.endswith(KEEP) else "\\\\\n") for i, e in enumerate(eqs))
    # the sentence stays with its first equation; a page may break between two equations, not inside one
    return ("\\begingroup\\allowdisplaybreaks\\interlinepenalty=10000\\clubpenalty=10000\\relax\n" + txt
            + "\n\\begin{align}\n" + body + "\n\\end{align}\\par\\endgroup")


# ------------------------------------------------------------------------------------------------ vegetation note
VEG_KIND = {"Pine": 0, "Broadleaf tree": 1, "Bush": 2, "Boulder": 3, "Treeline pine": 4}


def veg_note(nm):
    if nm not in VEG_KIND:
        return ""
    k = VEG_KIND[nm]
    by = collections.defaultdict(list)
    for v in VEG["variants"]:
        if v["kind"] == k:
            by[v["asset"]].append(v["triangles"][0])
    parts = ["\\texttt{%s} (%s triangles)" % (tex(a), num(min(t)) if min(t) == max(t) else num(min(t)) + "--" + num(max(t)))
             for a, t in by.items()]
    return (" At ULTRA and EPIC the renderer draws the CC0 Poly Haven mesh %s in its place; that mesh is imported, not built "
            "from primitives, and the primitive parts above stay the model the game logic uses." % " or ".join(parts))


# ------------------------------------------------------------------------------------------------ figures
def legend_strip(j, d):
    leg = j["legend"]
    items = ["\\primchipn{%s}{%s}" % (primkey.tex_key(k), num(leg[k])) for k in primkey.ORDER if k in leg]
    total = sum(leg.values())
    s = "\\quad ".join(items) + "\\quad \\textbf{total %s}" % num(total)
    extra = []
    if j["nested"]:
        extra.append("pale: " + ", ".join("%s $\\times$%d (%d parts)" % (tex(k), v, j["nested_parts"].get(k, 0)) for k, v in j["nested"].items()))
    nd = sum(1 for p in j["parts"] if p["dagger"] and not p["nested_design"])
    if nd:
        extra.append("$^\\dagger$ %d hidden at the start (listed in the table, not drawn)" % nd)
    parked = sum(1 for p in j["parts"] if p["parked"])
    if parked:
        extra.append("%d parked below ground, not drawn" % parked)
    if extra:
        s += "\\\\" + "; ".join(extra)
    return s, leg, total


# The design in the running scene (EPIC, 10:30 unless noted): tools/report/insitu_cams.py + the live-following shots, or a
# capture that shows it better (under the camouflage nets, backdrop seen from inside the world, sky objects, night units).
RAWROOT = os.path.join(ROOT, "report", "figures", "raw")
INSITU = {"Main battle tank": "showreel/tank", "Autonomous UAV": "insitu/insitu_uav_close",
          "Scout helicopter": "showreel/heli", "Target rover": "game/combined_strike",
          "UAV round": "insitu/insitu_uav_close", "Air-defence radar": "showcase/sc_radar",
          "HESCO perimeter": "insitu/insitu_hesco_close", "Concertina wire": "insitu/insitu_wire_close",
          "Road": "insitu/insitu_road_close", "SAM missile": "insitu/insitu_sam_launcher",
          "MLRS rocket": "insitu/insitu_mlrs_rocket_truck", "AA shell casing": "insitu/insitu_aa_gatling_gun",
          "Barracks hall": "showcase/sc_hall_door_closed", "Command bunker": "showcase/sc_pov_cic",
          "Tank spot marking": "insitu/insitu_motor_pool", "Terrain disc": "scene/v10_proj_iso",
          "Far ground": "scene/day_overview", "Distant hills": "showcase/sc_overview_dusk",
          "Sky clock (sun)": "scene/sky_sunset", "Moon": "scene/night_searchlights", "Soldier": "play_clean/play_third_person"}
INSITU_NOTE = {"Target rover": "night wave", "UAV round": "on the UAV's rail", "SAM missile": "in its launcher",
               "MLRS rocket": "in the launcher", "AA shell casing": "the gun that ejects it", "Command bunker": "the command room",
               "Tank spot marking": "under the motor-pool nets", "Moon": "night", "Sky clock (sun)": "sunset",
               "Distant hills": "dusk", "Barracks hall": "entrance"}


def insitu(name):
    """figures/img/insitu/<slug>.jpg (16:9, 1600 px) of the design in the scene, or None."""
    src = os.path.join(RAWROOT, INSITU.get(name, "insitu/insitu_%s" % slug(name)) + ".png")
    if not os.path.exists(src):
        return None
    dst = os.path.join(ROOT, "report", "figures", "img", "insitu", slug(name) + ".jpg")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src):
        im = Image.open(src).convert("RGB")
        im.thumbnail((1280, 1280), Image.LANCZOS)
        im.save(dst, quality=85)
    return "figures/img/insitu/%s.jpg" % slug(name)


def scene_caption(name):
    return "In the scene" + (" (%s)" % INSITU_NOTE[name] if name in INSITU_NOTE else "")


def pair(j, label_groups=False, hmax=4.2):
    """Two panels side by side, the same height, fitted to the text width; small cards add the in-scene view as a third."""
    am, ap = j["_am"], j["_ap"]
    lab_w = 2.4 if label_groups else 0.0
    sc = insitu(j["design"]) if not label_groups and j["design"] not in SKY else None
    if sc:
        h = min(hmax, (16.0 - 0.6) / (am + ap + 16 / 9))
        return ("\\begin{tabular}{@{}c@{\\hspace{3mm}}c@{\\hspace{3mm}}c@{}}\\img{figures/img/shapes/%s_model.jpg}{%.2fcm} & "
                "\\img{figures/img/shapes/%s_prims.jpg}{%.2fcm} & \\img{%s}{%.2fcm}\\\\[-0.3mm]"
                "{\\fontsize{7.5}{9}\\selectfont Full model} & {\\fontsize{7.5}{9}\\selectfont Made of primitives (pulled apart)} & "
                "{\\fontsize{7.5}{9}\\selectfont %s}\\end{tabular}"
                % (j["_slug"], am * h, j["_slug"], ap * h, sc, 16 / 9 * h, scene_caption(j["design"])))
    h = min(hmax, (16.0 - lab_w) / (am + ap))
    left = "\\img{figures/img/shapes/%s_model.jpg}{%.2fcm}" % (j["_slug"], am * h)
    if j["design"] in SKY:
        return ("\\begin{tabular}{@{}c@{}}\\img{figures/img/shapes/%s_prims.jpg}{%.2fcm}\\\\[-0.3mm]{\\fontsize{7.5}{9}\\selectfont "
                "Made of primitives (the renderer draws it in the sky pass, not as a lit part)}\\end{tabular}" % (j["_slug"], ap * h))
    right = "\\img{figures/img/shapes/%s_prims.jpg}{%.2fcm}" % (j["_slug"], ap * h)
    if label_groups:
        right = labelled(j, ap * h, h)
    return ("\\begin{tabular}{@{}c@{\\hspace{3mm}}c@{}}%s & %s\\\\[-0.3mm]{\\fontsize{7.5}{9}\\selectfont Full model} & "
            "{\\fontsize{7.5}{9}\\selectfont Made of primitives (pulled apart)}\\end{tabular}" % (left, right))


def labelled(j, w, h):
    """The primitives panel with its sub-assemblies named by leader lines (projected group centres from the capture)."""
    x0, y0, x1, y1 = j["crop_prims"]
    byid = {g["id"]: g for g in j["groups"]}
    kids = collections.defaultdict(list)
    for g in j["groups"]:
        kids[g["parent_id"]].append(g)
    root = next(g for g in j["groups"] if not g["parent_id"])
    frontier = [root]
    # expand the largest sub-assembly until about six groups are named (single-child chains do not count)
    for _ in range(40):
        exp = [g for g in frontier if kids[g["id"]]]
        if not exp or len(frontier) >= 6:
            break
        g = max(exp, key=lambda g: g["parts"])
        frontier.remove(g)
        frontier += kids[g["id"]]
    gs = [g for g in frontier if g["parts"] >= 2 and g["px_prims"][0] >= 0 and g is not root]
    gs = sorted(gs, key=lambda g: -g["parts"])[:10]
    # merge mirrored sub-assemblies (Track L / Track R) into one label with two lines
    lab = collections.OrderedDict()
    for g in gs:
        lab.setdefault(kind_norm(g["name"]), []).append(g)
    tk = ["\\begin{tikzpicture}[font=\\fontsize{7}{8}\\selectfont]",
          "\\node[inner sep=0,anchor=south west] at (0,0) {\\includegraphics[width=%.2fcm]{figures/img/shapes/%s_prims.jpg}};" % (w, j["_slug"])]
    items = sorted(lab.items(), key=lambda kv: min(g["px_prims"][1] for g in kv[1]))
    n = len(items)
    for i, (name, g) in enumerate(items):
        ly = h * (1 - (i + 0.5) / max(n, 1))
        tk.append("\\node[anchor=west,inner sep=1pt] (L%d) at (%.2f,%.2f) {%s};" % (i, w + 0.25, ly, tex(name)))
        for gg in g:
            u = (gg["px_prims"][0] - x0) / (x1 - x0) * w
            v = (1 - (gg["px_prims"][1] - y0) / (y1 - y0)) * h
            tk.append("\\draw[black!70,line width=0.4pt] (L%d.west) -- (%.2f,%.2f); \\fill[black] (%.2f,%.2f) circle (0.6pt);" % (i, u, v, u, v))
    tk.append("\\end{tikzpicture}")
    return "\n".join(tk)


# ------------------------------------------------------------------------------------------------ build all
out = ["% generated by tools/report/gen_designs.py: do not edit"]
check = {}
order = sorted(DESIGNS, key=lambda d: (FAMILIES.index(d["family"]), VW_ORDER.index(d["name"]) if d["name"] in VW_ORDER else 0))
shapes = {d["name"]: shape(d["name"]) for d in order}
for nm in CUT_DESIGNS:
    shapes[nm + "@cut"] = shape(nm, cut=True)

# atlas: two float pages (families split after "buildings"), 7 designs per row, each thumbnail cropped to its content
# and padded to one aspect so the tiles line up
ATLAS_W, ATLAS_H, ATLAS_N = 1.98, 1.5, 8
ATLAS_PRIMS = {"Concertina wire", "Perimeter fence"}  # thin wire: the outlined primitive render reads, the model does not


def atlas_thumb(src, dst, aspect=ATLAS_W / ATLAS_H):
    im = np.asarray(Image.open(src).convert("RGB"), dtype=np.int16)
    H, W = im.shape[:2]
    t = np.linspace(0, 1, H)[:, None, None]
    bg = (1 - t) * im[:1, :1] + t * im[-1:, :1]          # vertical background gradient from the corner pixels
    ys, xs = np.nonzero(np.abs(im - bg).max(axis=2) > 18)
    if len(xs) == 0:
        x0, x1, y0, y1 = 0, W - 1, 0, H - 1
    else:
        x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    cw, ch = (x1 - x0 + 1) * 1.12, (y1 - y0 + 1) * 1.12
    cw, ch = max(cw, ch * aspect), max(ch, cw / aspect)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    px, py = int(max(0, cw - W) / 2 + 2), int(max(0, ch - H) / 2 + 2)
    im = np.pad(im, ((py, py), (px, px), (0, 0)), mode="edge")   # extends the background gradient
    bx, by = int(round(cx + px - cw / 2)), int(round(cy + py - ch / 2))
    bx, by = min(max(bx, 0), im.shape[1] - int(cw)), min(max(by, 0), im.shape[0] - int(ch))
    out = Image.fromarray(im[by:by + int(ch), bx:bx + int(cw)].astype(np.uint8))
    out.thumbnail((520, 520), Image.LANCZOS)
    out.save(dst, quality=90)


def atlas_block(fams):
    blocks = []
    for fam in fams:
        ds = [d for d in order if d["family"] == fam]
        parts = sum(d["parts_first"] * d["instances"] if len(set(d["parts_per_instance"])) == 1 else sum(d["parts_per_instance"]) for d in ds)
        imgs, labs = [], []
        for d in ds:
            j = shapes[d["name"]]
            which = "prims" if d["name"] in SKY or d["name"] in ATLAS_PRIMS else "model"
            th = "atlas_%s.jpg" % j["_slug"]
            atlas_thumb(os.path.join(IMG, "%s_%s.jpg" % (j["_slug"], which)), os.path.join(IMG, th))
            imgs.append("\\includegraphics[width=%.2fcm,height=%.2fcm]{figures/img/shapes/%s}" % (ATLAS_W, ATLAS_H, th))
            ppi = d["parts_first"] if len(set(d["parts_per_instance"])) == 1 else "%d..%d" % (min(d["parts_per_instance"]), max(d["parts_per_instance"]))
            labs.append("\\textbf{%s}\\par %s$\\,\\times\\,$%d%s" % (tex(d["name"]), ppi, d["instances"],
                        "\\par\\textit{fired by the %s}" % FIRED_BY[d["name"]].replace("Autonomous ", "") if d["name"] in FIRED_BY else ""))
        if fam == "vegetation" and os.path.exists(os.path.join(IMG, "grass_field_model.jpg")):   # tools/report/gen_grass.py
            atlas_thumb(os.path.join(IMG, "grass_field_model.jpg"), os.path.join(IMG, "atlas_grass_field.jpg"))
            imgs.append("\\includegraphics[width=%.2fcm,height=%.2fcm]{figures/img/shapes/atlas_grass_field.jpg}" % (ATLAS_W, ATLAS_H))
            labs.append("\\textbf{Grass field}\\par\\textit{blades instanced on the GPU}")
        rows = []
        for k in range(0, len(imgs), ATLAS_N):
            pad = [""] * (ATLAS_N - len(imgs[k:k + ATLAS_N]))
            rows.append(" & ".join(imgs[k:k + ATLAS_N] + pad) + "\\\\[0.5mm]\n" + " & ".join(labs[k:k + ATLAS_N] + pad) + "\\\\[1.6mm]")
        blocks.append("\\noindent\\begin{tabular}{@{}*{%d}{>{\\centering\\arraybackslash\\fontsize{7}{8}\\selectfont}p{%.2fcm}}@{}}\n"
                      "\\multicolumn{%d}{@{}l@{}}{\\small\\bfseries %s\\quad\\normalfont\\footnotesize\\color{black!60}%d design%s, %s parts}\\\\[-0.6mm]\n"
                      "\\multicolumn{%d}{@{}l@{}}{\\color{black!35}\\rule{\\linewidth}{0.5pt}}\\\\[0.6mm]\n%s\n\\end{tabular}"
                      % (ATLAS_N, ATLAS_W, ATLAS_N, fam.capitalize(), len(ds), "" if len(ds) == 1 else "s",
                         "{:,}".format(parts).replace(",", "\\,"), ATLAS_N, "\n".join(rows)))
    return "\n\\par\\vspace{1.5mm}\n".join(blocks)


# Section 7 opens with the designs by family, every short form of their names written out at its first use
fam_items = []
for fam in FAMILIES:
    names = [lower_name(d["name"]) for d in order if d["family"] == fam]
    fam_items.append("\\item \\textbf{%s:} %s." % (fam.capitalize(), ", ".join(tex(n) for n in names)))
out.append("\\newcommand{\\designfamilies}{\\begin{itemize}\n%s\n\\end{itemize}}" % abbrev.expand_first("\n".join(fam_items), SEEN))
cut_at = FAMILIES.index("buildings") + 1
out.append(abbrev.expand_first("\\newcommand{\\designatlasA}{%s}" % atlas_block(FAMILIES[:cut_at]), SEEN))
out.append(abbrev.expand_first("\\newcommand{\\designatlasB}{%s}" % atlas_block(FAMILIES[cut_at:]), SEEN))


# Table 19.1
tot = collections.Counter()
tl = ["{\\fontsize{7}{8}\\selectfont\\setlength{\\tabcolsep}{2.1pt}\\renewcommand{\\arraystretch}{1.0}",
      "\\begin{longtable}{@{}l r r r " + "r" * len(PRIM_COLS) + "@{}}",
      "\\caption{Every design of the scene: instances $n$, parts per instance, parts over all instances and the parts of each "
      "primitive over all instances (blank = none). The totals row is the whole scene.}\\label{tab:objs}\\\\",
      "\\toprule Design & $\\times n$ & parts & total & " + " & ".join("\\rotatebox{90}{\\primchip{%s}}" % primkey.tex_key(k) for k in PRIM_COLS)
      + "\\\\\\midrule\\endfirsthead",
      "\\toprule Design & $\\times n$ & parts & total & " + " & ".join("\\rotatebox{90}{\\primsq{%s}}" % primkey.tex_key(k) for k in PRIM_COLS)
      + "\\\\\\midrule\\endhead", "\\bottomrule\\endlastfoot"]
for fam in FAMILIES:
    tl.append("\\multicolumn{%d}{@{}l}{\\textit{%s}}\\\\" % (4 + len(PRIM_COLS), fam.capitalize()))
    for d in [d for d in order if d["family"] == fam]:
        pt = d["primitives_total"]
        tot.update(pt)
        ppi = d["parts_per_instance"]
        per = str(ppi[0]) if len(set(ppi)) == 1 else "%d..%d" % (min(ppi), max(ppi))
        tl.append("\\hyperref[fig:d-%s]{%s} & %d & %s & %s & %s\\\\" % (slug(d["name"]), tex(d["name"]), d["instances"], per, num(d["parts_total"]),
                                                    " & ".join(num(pt[k]) if pt.get(k) else "" for k in PRIM_COLS)))
total = sum(d["parts_total"] for d in DESIGNS)
assert total == EXP["leaf_count"], (total, EXP["leaf_count"])
tl.append("\\midrule\\textbf{Scene} & %d & & \\textbf{%s} & %s\\\\" % (len(DESIGNS), num(total), " & ".join(num(tot[k]) if tot.get(k) else "" for k in PRIM_COLS)))
tl.append("\\end{longtable}}")
out.append("\\newcommand{\\designtable}{%s}" % "\n".join(tl))
out.append("\\newcommand{\\nDesigns}{%d}\\newcommand{\\nDesignInstances}{%d}\\newcommand{\\sceneParts}{%s}"
           % (len(DESIGNS), sum(d["instances"] for d in DESIGNS), num(total)))

# cards
big = {"Soldier", "Main battle tank", "MLRS rocket truck", "Scout helicopter", "Autonomous UAV", "SAM launcher", "AA Gatling gun",
       "Command bunker", "Barracks hall", "Air-defence radar"}
for fam in FAMILIES:
    cards = []
    for d in [d for d in order if d["family"] == fam]:
        nm = d["name"]
        j = shapes[nm + "@cut"] if nm in CUT_DESIGNS else shapes[nm]
        rows_, own = parts_rows(d)
        # consistency: legend (capture) = parts table = export row
        leg_capture = j["legend"]
        leg_table = collections.Counter()
        for sec, rs, nest in rows_:
            for r in rs:
                leg_table[r["key"]] += len(r["S"])
        if dict(leg_capture) != dict(leg_table) or dict(leg_table) != d["primitives_first"]:
            sys.exit("%s: legend %s / table %s / export %s differ" % (nm, leg_capture, dict(leg_table), d["primitives_first"]))
        check[nm] = dict(legend=leg_capture, table=dict(leg_table), export=d["primitives_first"], parts=d["parts_first"],
                         instances=d["instances"], total=d["parts_total"])
        strip, leg, total_ = legend_strip(j, d)
        large = nm in big
        cut_note = ""
        if j.get("cutaway"):
            names = [tex(c.rstrip("*").lower() + ("s" if c.endswith("*") else "")) for c in j["cut"]]
            cut_note = (" In the pictures (a cutaway), the %s are left out (%d parts) so that what they cover shows." %
                        (", ".join(names[:-1]) + " and " + names[-1] if len(names) > 1 else names[0], sum(j["removed"].values())))
        hfig = 5.1 if large else 3.6
        first = not cards   # the family's first card follows its heading on a fresh page: no space test (it would break again)
        # no space test before a card: its picture block is one box, so a card starts where the box fits (the test read a
        # stale page total after the coordinate sheets and sent cards to a new page with most of the old one empty)
        fig = ["" if first else ("\\FloatBarrier" if large else "") + "\\par\\addvspace{2mm}",
               "\\begin{center}\\begin{minipage}{\\linewidth}\\centering",   # picture box + caption: one unbreakable block
               "\\fcolorbox{black!30}{white}{\\begin{minipage}{0.975\\linewidth}\\centering\\vspace{0.6mm}",
               pair(j, label_groups=large, hmax=hfig),
               ("\\\\[1mm]\\begin{tabular}{@{}c@{}}\\img{%s}{%.2fcm}\\\\[-0.3mm]{\\fontsize{7.5}{9}\\selectfont %s}\\end{tabular}"
                % (insitu(nm), 0.46 * 16.0 if large else 9.0, scene_caption(nm)) if (large or nm in SKY) and insitu(nm) else ""),
               "\\\\[0.8mm]{\\fontsize{7}{8.5}\\selectfont %s}\\vspace{0.6mm}\\end{minipage}}\\par\\nobreak" % strip,
               "\\captionof{figure}{%s.}\\label{fig:d-%s}" % (CAPTION_NAME.get(nm, tex(nm)), slug(nm)),
               "\\end{minipage}\\end{center}\\vspace{-3mm}"]
        body = ["\\noindent\\textbf{%s.}\\label{des:%s} %s%s%s" % (tex(nm), slug(nm), sentence(d), veg_note(nm), cut_note)]
        r = rule(d)
        if r:
            body.append(r)
        body.append(chain_block(d))
        card = "\n".join(x for x in fig if x) + "\n" + "\n\n".join(body) + "\n" + parts_table(d, rows_) + "\n" + cs.coord_sheet(d, own, d["instance_list"][0]["root_id"], BY_ID, mat, tex, lower_name(nm), EXP["terrain"]["radius"])[0]
        if nm == "Soldier":
            card += "\n\\soldierextras"
        if nm == "Main battle tank":
            card += "\n\\tankextras"
        if nm == "Autonomous UAV":
            card += "\n\\uavextras"
        if nm == "Scout helicopter":
            card += "\n\\heliextras"
        if nm == "Command bunker":
            card += "\n\\bunkerextras"
        cards.append(abbrev.expand_first(card, SEEN))
    out.append("\\newcommand{\\designcards%s}{%%\n%s\n\\FloatBarrier}" % (macro(fam), "\n\n".join(cards)))

# munitions at one scale
lj = json.load(open(os.path.join(RAW, "lineup_sam_missile_mlrs_rocket_uav_round_aa_shell_casing.json")))
lj2 = json.load(open(os.path.join(RAW, "lineup_uav_round_aa_shell_casing.json")))


def lineup_panel(jj, which, width_cm):
    src = os.path.join(RAW, jj[which])
    dst = os.path.join(IMG, os.path.basename(src)[:-4] + ".jpg")
    im = Image.open(src).convert("RGB")
    im.save(dst, quality=90)
    W, H = jj["size"]
    hcm = width_cm * H / W
    s = width_cm / W   # cm per px
    bar = jj["px_per_m"] * 0.1 * s   # 10 cm in cm on the page
    tk = ["\\begin{tikzpicture}[font=\\fontsize{7}{8}\\selectfont]",
          "\\node[inner sep=0,anchor=south west] at (0,0) {\\includegraphics[width=%.2fcm]{figures/img/shapes/%s}};" % (width_cm, os.path.basename(dst))]
    for it in jj["items"]:
        x, y = it["label_px"]
        tk.append("\\node[anchor=south west,inner sep=1pt,fill=white,fill opacity=0.8,text opacity=1] at (%.2f,%.2f) {%s, %.2f\\,m};" % (x * s, hcm - y * s + 0.12, tex(it["design"]), it["length_m"]))
    tk.append("\\draw[line width=1.2pt] (%.2f,0.25) -- (%.2f,0.25) node[midway,above=-1pt] {10\\,cm};" % (width_cm - 0.4 - bar, width_cm - 0.4))
    tk.append("\\end{tikzpicture}")
    return "\n".join(tk)


out.append("\\newcommand{\\munitionlineupfig}{\\begin{figure}[tbp]\\centering"
           "\\begin{subfigure}[t]{\\linewidth}\\centering %s\\caption{All four at one scale.}\\end{subfigure}\\\\[1mm]"
           "\\begin{subfigure}[t]{0.62\\linewidth}\\centering %s\\caption{The two small rounds enlarged (their own 10\\,cm bar).}\\end{subfigure}"
           "\\caption{The munitions built from parts, side by side at one scale, made of primitives (orthographic, lengths along the "
           "axis are true; the exhaust flame of the missiles is left out).}\\label{fig:munitions}\\end{figure}}"
           % (lineup_panel(lj, "prims", 10.5), lineup_panel(lj2, "prims", 7.5)))

# build stages in the colour key
for nm, tag in (("Autonomous UAV", "Uav"), ("Main battle tank", "Tank"), ("Scout helicopter", "Heli"), ("Command bunker", "Bunker")):
    cells = []
    # one crop for the four stages (so they line up): the box of the full design at 100 %, padded to 4:3
    full = np.asarray(Image.open(os.path.join(RAW, "%s_stage100.png" % slug(nm))).convert("RGB"), dtype=np.int16)
    bg = full[:, :1]  # background: a vertical gradient, so compare each row with its first pixel
    ys, xs = np.nonzero(np.abs(full - bg).max(axis=2) > 12)
    H, W = full.shape[:2]
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    cw, ch = (x1 - x0) * 1.08, (y1 - y0) * 1.08
    cw, ch = max(cw, ch * 4 / 3), max(ch, cw * 3 / 4)
    cw, ch = min(cw, W), min(ch, H)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    bx0 = int(min(max(cx - cw / 2, 0), W - cw))
    by0 = int(min(max(cy - ch / 2, 0), H - ch))
    stage_box = (bx0, by0, bx0 + int(cw), by0 + int(ch))
    for pct in (12, 35, 65, 100):
        sj = json.load(open(os.path.join(RAW, "%s_stage%d.json" % (slug(nm), pct))))
        src = os.path.join(RAW, sj["prims"])
        dst = os.path.join(IMG, sj["prims"][:-4] + ".jpg")
        im = Image.open(src).convert("RGB").crop(stage_box)
        im.thumbnail((1400, 1400))
        im.save(dst, quality=88)
        cells.append("\\begin{subfigure}[t]{0.49\\linewidth}\\img{figures/img/shapes/%s}{\\linewidth}\\caption{%d\\,\\%%: %d of %d parts}\\end{subfigure}"
                     % (os.path.basename(dst), pct, sj["parts"], sj["of"]))
    # 2 x 2: each stage at half the text width
    out.append("\\newcommand{\\buildstripKey%s}{%s\\hfill\n%s\\\\[1.5mm]\n%s\\hfill\n%s}" % ((tag,) + tuple(cells)))

open(OUT, "w").write("\n".join(out) + "\n")
json.dump(check, open(CHECK, "w"), indent=1)
print("transformation chains checked: %d, max relative error %.2g" % (len(CHAIN_ERR), max(CHAIN_ERR)))
print("wrote", OUT, len(DESIGNS), "designs;", total, "parts")
