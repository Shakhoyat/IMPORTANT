"""Sheet 13 (Traceability): requirement -> implementation file -> sheet -> test -> report section.

Requirements are parsed from docs/PLAN.md section 2; implementation files are resolved against the real source
tree; test IDs are kept only if they exist in the Testing sheet; report sections are parsed from report/main.tex.
"""
import glob
import os
import re

from openpyxl.formatting.rule import FormulaRule

from wb_common import (BOLD, FAIL_FILL, PASS_FILL, ROOT, S_BEHAV, S_GEOM, S_HIER, S_INV, S_LIGHT, S_MAT, S_OBJ,
                       S_PERF, S_SHADE, S_TEST, S_TRACE, S_VERT, S_COVER, file_link, finish, header, link, path,
                       put, title, widths)

# Requirement -> (workbook sheets, test IDs, report-section keyword). Authored cross-reference, validated below.
LINKS = {
    "R01": ([S_INV, S_VERT], ["T01", "T02", "T04", "T05", "T07", "T08", "V01"], "Primitive Geometry"),
    "R02": ([S_VERT, S_GEOM], ["T11", "T12"], "Indexed Rendering"),
    "R03": ([S_VERT], ["T11", "V01"], "Indexed Rendering"),
    "R04": ([S_VERT], ["T03", "T09"], "Primitive Geometry"),
    "R05": ([S_VERT], ["T03", "T05", "T09", "T16"], "Shading and Lighting"),
    "R06": ([S_INV], ["T04", "T07"], "Primitive Geometry"),
    "R07": ([S_VERT], ["T06"], "Primitive Geometry"),
    "R08": ([S_OBJ, S_MAT], ["T14", "T18"], "Object Construction"),
    "R09": ([S_SHADE], ["T19", "V10"], "Shading and Lighting"),
    "R10": ([S_SHADE], ["T13", "T19"], "Shading and Lighting"),
    "R11": ([S_SHADE], ["T19", "V11"], "Shading and Lighting"),
    "R12": ([S_BEHAV], ["T28"], "Autonomous Behaviour"),
    "R13": ([S_LIGHT], ["V12"], "Shading and Lighting"),
    "R14": ([S_MAT], ["T14", "T15"], "Transformations"),
    "R15": ([S_BEHAV], ["T30", "B01"], "Autonomous Behaviour"),
    "R16": ([S_BEHAV], ["T28", "B02"], "Autonomous Behaviour"),
    "R17": ([S_HIER], ["T21", "V13"], "Transformations"),
    "R18": ([S_TEST], ["V04", "V05", "T26"], "Depth Testing"),
    "R19": ([S_TEST], ["V07", "V08"], "Depth Testing"),
    "R20": ([S_TEST], ["V02"], "Shading and Lighting"),
    "R21": ([S_TEST], ["T17"], "Depth Testing"),
    "R22": ([S_OBJ], ["V09", "V01"], "Object Construction"),
    "R23": ([S_COVER], [], "Results"),
    "R24": ([S_TRACE], [], "Introduction"),
    "R25": ([S_OBJ], ["T18"], "Object Construction"),
    "R26": ([S_PERF], [], "Results"),
}


def parse_requirements(rel="docs/PLAN.md"):
    text = open(path(rel), encoding="utf-8").read()
    block = text.split("## 2.", 1)[1].split("\n## ", 1)[0]
    rows = []
    for line in block.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) >= 4 and re.fullmatch(r"R\d+", cells[0]):
            rows.append(cells[:4])
    return rows


def report_sections(rel="report/main.tex"):
    p = path(rel)
    if not os.path.exists(p):
        return []
    titles = re.findall(r"\\section\{([^}]*)\}", open(p, encoding="utf-8").read())
    return [(i + 1, t) for i, t in enumerate(titles)]


MAX_GREP_HITS = 3


def _prefer_sources(paths):
    """Keeps the .cpp of each stem and a header only when no .cpp exists (shorter, still complete)."""
    stems = {os.path.splitext(p)[0] for p in paths if p.endswith((".cpp", ".c"))}
    return [p for p in paths if p.endswith((".cpp", ".c")) or os.path.splitext(p)[0] not in stems]


def resolve_files(where):
    """Turns `Bunker.cpp`, `src/primitives/*`, `primitives`, `Mesh::upload`, `Lighting.*`, `kDefaultSlices = 36`
    into real paths under src/ (directory, file stem, or - as a last resort - files that mention the identifier)."""
    found = []
    all_src = sorted(os.path.relpath(p, ROOT).replace("\\", "/")
                     for p in glob.glob(os.path.join(ROOT, "src", "**", "*.*"), recursive=True))
    dirs = {p.split("/")[1] for p in all_src if p.count("/") >= 2}
    for token in re.findall(r"`([^`]+)`", where):
        token = token.split("::")[0].strip()
        ident = re.match(r"[A-Za-z_][A-Za-z0-9_]*", token.split("/")[-1] or token)
        if "/" in token:
            hits = [p for p in all_src if p.startswith(token.rstrip("*").rstrip("/"))]
        elif token in dirs:
            hits = [p for p in all_src if p.startswith(f"src/{token}/")]
        else:
            stem = token.split(".")[0]
            hits = [p for p in all_src if os.path.splitext(os.path.basename(p))[0] == stem]
        if not hits and ident:
            word = re.compile(r"\b" + re.escape(ident.group(0)) + r"\b")
            hits = [p for p in all_src if word.search(open(path(p), encoding="utf-8", errors="ignore").read())]
            hits = _prefer_sources(hits)[:MAX_GREP_HITS]
        for h in _prefer_sources(hits):
            if h not in found:
                found.append(h)
    return found


def build_traceability(wb, ctx):
    ws = wb[S_TRACE]
    title(ws, "13  Traceability matrix",
          "Requirement (docs/PLAN.md section 2) -> implementation files (resolved against src/) -> workbook sheet "
          "(hyperlink) -> tests -> report section (report/main.tex). 'Tests passing' is a formula that looks each "
          "listed test up in sheet 10.", span=10)
    h, first = 4, 5
    header(ws, h, ["Req ID", "Requirement", "Source", "Where satisfied (plan)", "Implementation files",
                   "Workbook sheet", "Test IDs", "Tests passing (formula)", "Report section", "Status"])
    known = {t["id"] for t in ctx["tests"]} | {v["id"] for v in ctx["visual"]} | {"B01", "B02"}
    sections = report_sections()
    t_first, t_last = ctx["test_rows"]
    ids, st = f"'{S_TEST}'!$A${t_first}:$A${t_last}", f"'{S_TEST}'!$F${t_first}:$F${t_last}"
    reqs = parse_requirements()
    for i, (rid, req, src, where) in enumerate(reqs):
        r = first + i
        sheets, tests, sec_kw = LINKS.get(rid, ([], [], ""))
        tests = [t for t in tests if t in known]
        files = resolve_files(where)
        put(ws, r, 1, rid, font=BOLD)
        put(ws, r, 2, req)
        put(ws, r, 3, src)
        put(ws, r, 4, where.replace("`", ""))
        c = put(ws, r, 5, "\n".join(files) if files else "-")
        if files:
            file_link(c, files[0], "\n".join(files))
        c = put(ws, r, 6, ", ".join(sheets) or "-")
        if sheets:
            link(c, sheets[0], "A1", ", ".join(sheets))
        put(ws, r, 7, ", ".join(tests) or "-")
        if tests:
            passed = "+".join(f'COUNTIFS({ids},"{t}",{st},"PASS")' for t in tests)
            put(ws, r, 8, f'=({passed})&" / {len(tests)}"')
            put(ws, r, 10, f'=IF({passed}={len(tests)},"Verified","CHECK")', font=BOLD)
        else:
            put(ws, r, 8, "-")
            put(ws, r, 10, "Deliverable", font=BOLD)
        sec = next((f"{n}. {t}" for n, t in sections if sec_kw and sec_kw.lower() in t.lower()), "-")
        put(ws, r, 9, sec)
    last = first + len(reqs) - 1
    rng = f"J{first}:J{last}"
    ws.conditional_formatting.add(rng, FormulaRule(formula=[f'J{first}="Verified"'], fill=PASS_FILL))
    ws.conditional_formatting.add(rng, FormulaRule(formula=[f'J{first}="CHECK"'], fill=FAIL_FILL))
    put(ws, 3, 1, "Requirements", font=BOLD)
    put(ws, 3, 2, f"=COUNTA(A{first}:A{last})", "0")
    put(ws, 3, 3, "Verified by passing tests", font=BOLD)
    put(ws, 3, 4, f'=COUNTIF({rng},"Verified")', "0")
    widths(ws, [7, 44, 14, 30, 34, 24, 20, 12, 30, 12])
    finish(ws, h)
