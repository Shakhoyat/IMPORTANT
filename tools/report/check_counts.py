"""Cross-checks every count the report prints against the scene export, after the generators ran:
  1. per design: the shape-view legend = the parts table = the export's design row (report/data/designs_check.json,
     written by tools/report/gen_designs.py) and Table 19.1's row (parsed from gen_designs.tex);
  2. Section 6: each primitive's instance count in Table 6.1 (gen_tables.tex) = the sum of that primitive's column of
     Table 19.1 over all designs (cube and cuboid separately);
  3. the scene total is the same everywhere: export leaf_count, Table 6.1's total, Table 19.1's totals row, the sum of
     the designs, the macros \\nLeaves and \\sceneParts.
Exit code 1 on any mismatch. Run: python3 tools/report/check_counts.py"""
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
import primkey  # noqa: E402

EXP = json.load(open(os.path.join(ROOT, "report", "data", "scene_export_v10.json")))
CHK = json.load(open(os.path.join(ROOT, "report", "data", "designs_check.json")))
SEC = os.path.join(ROOT, "report", "sections")
bad = []


def n(s):
    return int(re.sub(r"[^\d]", "", s)) if re.search(r"\d", s) else 0


# 1. per design
for d in EXP["designs"]:
    c = CHK.get(d["name"])
    if not c:
        bad.append("design %s has no card" % d["name"])
        continue
    if not (c["legend"] == c["table"] == c["export"] == d["primitives_first"]):
        bad.append("design %s: legend %s table %s export %s" % (d["name"], c["legend"], c["table"], c["export"]))
    if sum(c["legend"].values()) != d["parts_first"]:
        bad.append("design %s: legend total %d != parts %d" % (d["name"], sum(c["legend"].values()), d["parts_first"]))

# Table 19.1 rows: "\hyperref[fig:d-x]{Name} & n & parts & total & cols..."
t19 = open(os.path.join(SEC, "gen_designs.tex")).read()
cols = [k for k in primkey.ORDER if k != "Grass blade"]
rows = {}
for m in re.finditer(r"\\hyperref\[fig:d-[^\]]+\]\{([^}]*)\} & (.*?)\\\\", t19):
    f = [x.strip() for x in m.group(2).split("&")]
    rows[m.group(1).replace("\\&", "&")] = f
col_sum = {k: 0 for k in cols}
for d in EXP["designs"]:
    f = rows.get(d["name"].replace("_", "\\_"))
    if not f:
        bad.append("Table 19.1 has no row for %s" % d["name"])
        continue
    if n(f[0]) != d["instances"] or n(f[2]) != d["parts_total"]:
        bad.append("Table 19.1 %s: n %s total %s vs export %d / %d" % (d["name"], f[0], f[2], d["instances"], d["parts_total"]))
    for k, v in zip(cols, f[3:]):
        col_sum[k] += n(v)
        if n(v) != d["primitives_total"].get(k, 0):
            bad.append("Table 19.1 %s column %s: %s vs %d" % (d["name"], k, v, d["primitives_total"].get(k, 0)))
tot = re.search(r"\\textbf\{Scene\} & (\d+) & & \\textbf\{([^}]*)\} & (.*?)\\\\", t19)
scene_t19 = n(tot.group(2))
tot_cols = [n(x) for x in tot.group(3).split("&")]

# 2. Table 6.1 instance counts vs the column sums of Table 19.1
t6 = open(os.path.join(SEC, "gen_tables.tex")).read()
lib_inst = {}
for p in EXP["primitives"]:
    key = "Cuboid" if p["name"] == "cuboid" else p["type"]
    lib_inst[key] = p["instances"]
for k in cols:
    if lib_inst.get(k, 0) != col_sum[k]:
        bad.append("primitive %s: Table 6.1 %d vs Table 19.1 column sum %d" % (k, lib_inst.get(k, 0), col_sum[k]))
for k, v in zip(cols, tot_cols):
    if v != col_sum[k]:
        bad.append("Table 19.1 totals row %s: %d vs column sum %d" % (k, v, col_sum[k]))
m6 = re.search(r"Primitive instances in the scene \(parts\)\} & ([\d{},]+)", t6)
scene_t6 = n(m6.group(1)) if m6 else -1

# 3. scene total
macros = open(os.path.join(SEC, "gen_numbers.tex")).read()
nleaves = n(re.search(r"\\newcommand\{\\nLeaves\}\{(.*)\}\s*$", macros, re.M).group(1))
sp = n(re.search(r"\\newcommand\{\\sceneParts\}\{([^}]*)\}", t19).group(1))
totals = dict(export=EXP["leaf_count"], table61=scene_t6, table191=scene_t19, designs=sum(d["parts_total"] for d in EXP["designs"]),
              nLeaves=nleaves, sceneParts=sp)
if len(set(totals.values())) != 1:
    bad.append("scene totals differ: %s" % totals)

print("designs checked:", len(EXP["designs"]), "| primitive columns:", len(cols), "| scene totals:", totals)
for b in bad:
    print("MISMATCH:", b)
print("OK" if not bad else "%d mismatches" % len(bad))
sys.exit(1 if bad else 0)
