"""Prose lint for the report body: fails if template or development-history phrases come back.
Checked: every report/sections/*.tex that main.tex inputs (generated files included), except Section 3 (03_setup.tex,
where the provenance of figures and numbers is stated once) and the appendices (gen_wb_appendix.tex, C04_appendix.tex).
Forbidden: "Notice", "Builds on", "Where in the project", a version tag (v1..v12 as a word), "the brief", "honest",
"--export-docs". Also counted (reported, not failing): the old section-skeleton markers \\concept / \\critique /
\\where, "the program", "Critique".
Usage: python3 tools/report/lint_prose.py [--rev GIT_REV]   (counts at a git revision, e.g. the start of the pass)"""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXCLUDE = {"03_setup.tex", "gen_wb_appendix.tex", "C04_appendix.tex"}
FAIL = {
    "Notice": r"\bNotice(?= that| in| how| the|,)",   # the caption opener, not the part "Notice board"
    "Builds on": r"Builds on",
    "Where in the project": r"Where in the project|\\where\{",
    "version tag": r"(?<![\w/{])v(?:[1-9]|1[0-2])(?![\w./])",   # image paths (\\raw{v5/...}) are not prose
    "the brief": r"\b[Tt]he brief\b",
    "honest": r"\bhonest",
    "--export-docs": r"-{1,2}export-docs",
}
INFO = {
    "\\concept": r"\\concept\{",
    "\\critique / Critique.": r"\\critique\{|Critique\.",
    "the program": r"\bthe program\b",
    "Every claim / painted over / honestly": r"Every claim|painted over|honestly",
}


def files(rev):
    def read(path):
        if rev:
            r = subprocess.run(["git", "show", "%s:%s" % (rev, path)], cwd=ROOT, capture_output=True, text=True)
            return r.stdout if r.returncode == 0 else ""
        p = os.path.join(ROOT, path)
        return open(p).read() if os.path.exists(p) else ""
    main = read("report/main.tex")
    names = re.findall(r"\\input\{sections/([^}]+)\}", main)
    seen, out = set(), []
    for n in names:
        txt = read("report/sections/%s.tex" % n)
        out.append((n + ".tex", txt))
        seen.add(n)
        for m in re.findall(r"\\input\{sections/([^}]+)\}", txt):   # nested inputs (B01 -> gen_designs)
            if m not in seen:
                seen.add(m)
                out.append((m + ".tex", read("report/sections/%s.tex" % m)))
    return [(n, t) for n, t in out if n not in EXCLUDE]


def strip_comments(t):
    return "\n".join(re.sub(r"(?<!\\)%.*", "", line) for line in t.splitlines())


def main():
    rev = sys.argv[sys.argv.index("--rev") + 1] if "--rev" in sys.argv else None
    fs = [(n, strip_comments(t)) for n, t in files(rev)]
    bad = 0
    print("revision:", rev or "working tree", "|", len(fs), "files")
    for label, pat in list(FAIL.items()) + list(INFO.items()):
        hits = [(n, m.group(0), t[max(0, m.start() - 40):m.end() + 40].replace("\n", " ")) for n, t in fs for m in re.finditer(pat, t)]
        print("%-40s %4d" % (label, len(hits)))
        if label in FAIL and hits:
            bad += len(hits)
            if not rev:
                for n, g, ctx in hits[:30]:
                    print("    %s: ...%s..." % (n, ctx))
    sys.exit(1 if bad and not rev else 0)


if __name__ == "__main__":
    main()
