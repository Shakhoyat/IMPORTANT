"""Full form at the first use of every short form on every page.

Where a page breaks is decided by LaTeX, so this is done in a loop with the build:
  1. tag     every short form in the running text becomes \\ab{id}{UAV}; \\ab prints the short form and writes its page to
             main.aux (\\abpage{id}{UAV}{page}); the ids listed in sections/gen_ab_first.tex print "UAV (uncrewed aerial
             vehicle)" instead. Floats (figures, tables, algorithms), captions and headings can land on any page, so each of
             them gets the full form at its own first use, written into the source.
  2. build   tectonic
  3. decide  the first \\abpage line of every (page, short form) in main.aux joins the list (ids never leave it, so the loop
             ends); if the list grew, build again.
  4. verify  the PDF text of every page: a short form on a page without its full form is reported.
Text inside drawings and plots (TikZ nodes, plot labels, screenshots) is not rewritten; their captions are.
Run: python3 tools/report/abbrev_pages.py            (tag, then build / decide until stable, then verify)
     python3 tools/report/abbrev_pages.py verify     (only check report/main.pdf; needs pymupdf)"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import abbrev  # noqa: E402

ROOT = abbrev.ROOT
REP = abbrev.REP
SEC = os.path.join(REP, "sections")
ABBR = {a: v[0] for a, v in abbrev.ABBR.items() if a not in abbrev.NO_EXPAND}
PLURAL = abbrev.PLURAL
ORDER = sorted(ABBR, key=len, reverse=True)   # longer first (sRGB before RGB, MSAA / TAA / SSAO before AA / AO)
FIRST = os.path.join(SEC, "gen_ab_first.tex")
LONG = os.path.join(SEC, "gen_ab_long.tex")

SKIP_CMD = re.compile(
    r"\\(code|key|label|ref|eqref|cite|nocite|input|raw|img|imgbox|plot|wbblock|fullsheet|includegraphics|hyperref|primchipn?|primsq|"
    r"texttt|url|lstinline|gv|begin|end|newcommand|renewcommand|csname|cellcolor|rowcolor|columncolor|color|definecolor|fontsize|vspace|hspace|"
    r"setlength|addvspace|needspace|rotatebox|resizebox|parbox|multicolumn|multirow|cline|cmidrule|pageref|bibliographystyle|bibliography|"
    r"usetikzlibrary|tikzset|Call|abp?)\*?(\[[^\]]*\])?\{")
VERB_ENV = r"lstlisting|tikzpicture|equation|gather|align|multline|verbatim|displaymath"
FLOAT_ENV = r"figure|table|algorithm"
HEAD = re.compile(r"\\(section|subsection|subsubsection|paragraph|caption|captionof\{\w+\})\*?(\[[^\]]*\])?\{")


def brace_end(text, i):
    """Index just after the group whose opening brace is at i - 1."""
    depth = 1
    while i < len(text) and depth:
        c = text[i]
        if c == "\\":
            i += 2
            continue
        depth += {"{": 1, "}": -1}.get(c, 0)
        i += 1
    return i


def masked(text, tikz=True):
    spans = []
    for m in SKIP_CMD.finditer(text):
        e = brace_end(text, m.end())
        name = m.group(1)
        # further mandatory arguments that are not prose: column specs, dimensions, the first two of \multicolumn
        extra = {"begin": 0, "multicolumn": 1, "multirow": 1, "parbox": 0, "resizebox": 1, "rotatebox": 0, "definecolor": 2,
                 "newcommand": 0, "renewcommand": 0, "fontsize": 1, "setlength": 1}.get(name, 0)
        if name == "begin" and re.match(r"\{(tabularx?|longtable|array|minipage|subfigure|tabular\*)\}", text[m.end() - 1:]):
            extra = 2 if re.match(r"\{(tabularx|tabular\*)\}", text[m.end() - 1:]) else 1
        k = 0
        while k < extra:
            mm = re.match(r"\s*(\[[^\]]*\])?\s*\{", text[e:])
            if not mm:
                break
            e = brace_end(text, e + mm.end())
            k += 1
        spans.append((m.start(), e))
    for m in re.finditer(r"\$\$.*?\$\$|\$[^$]*\$|(?<!\\)\\\[.*?\\\]|\\\(.*?\\\)|(?<!\\)%[^\n]*|\\begin\{("
                         + (VERB_ENV if tikz else VERB_ENV.replace("tikzpicture|", "")) + r")\*?\}.*?\\end\{\1\*?\}", text, re.S):
        spans.append((m.start(), m.end()))
    for m in re.finditer(r"\\[A-Za-z@]+", text):   # command names themselves
        spans.append((m.start(), m.end()))
    return spans


def static_regions(text):
    """Floats, captions and headings: [(start, end)] of the text that gets its own first-use expansion."""
    out = []
    for m in re.finditer(r"\\begin\{(" + FLOAT_ENV + r")\*?\}.*?\\end\{\1\*?\}", text, re.S):
        out.append((m.start(), m.end()))
    for m in HEAD.finditer(text):
        e = brace_end(text, m.end())
        if not any(s <= m.start() < t for s, t in out):
            out.append((m.end(), e - 1))
    return sorted(out)


def inside(pos, spans):
    return any(s <= pos < e for s, e in spans)


def occ(a):
    return re.compile(r"(?<![A-Za-z0-9\\/_-])%s(s)?(?![A-Za-z0-9_]|-\d)" % re.escape(a))


def long_form(a, plural):
    return "%s%s (%s)" % (a, "s" if plural else "", PLURAL.get(a, ABBR[a] + "s") if plural else ABBR[a])


def strip_tags(text):
    text = re.sub(r"\\abp\{\d+\}\{([^{}]*)\}", r"\1's", text)
    return re.sub(r"\\ab\{\d+\}\{([^{}]*)\}", r"\1", text)


def undo_prose_expansions(text):
    """'uncrewed aerial vehicle (UAV)' / 'UAV (uncrewed aerial vehicle)' in the running text -> 'UAV' (the page pass decides)."""
    for a in ORDER:
        full, plural = ABBR[a], PLURAL.get(a, ABBR[a] + "s")
        forms = [(r"%s \(%ss\)" % (re.escape(plural), re.escape(a)), a + "s"), (r"%s \(%s\)" % (re.escape(full), re.escape(a)), a),
                 (r"(?<![A-Za-z])%ss \(%s\)" % (re.escape(a), re.escape(plural)), a + "s"), (r"(?<![A-Za-z])%s \(%s\)" % (re.escape(a), re.escape(full)), a)]
        for pat, short in forms:
            pos = 0
            while True:
                m = re.compile(pat, re.I if pat.startswith(re.escape(full)[:3]) else 0).search(text, pos)
                if not m:
                    break
                st = static_regions(text)
                if inside(m.start(), st) or inside(m.start(), masked(text)):
                    pos = m.end()
                    continue
                text = text[:m.start()] + short + text[m.end():]
                pos = m.start() + len(short)
    return text


# Short forms that appear only inside a plot image (not in the LaTeX source): figure label -> short forms for its caption note
EXTRA_NOTES = {"fig:d-trs": ["TRS"], "fig:tonecurve": ["ACES", "EV"], "fig:rovermap": ["UAV"], "fig:paths": ["UAV"], "fig:motionseries": ["UAV"],
               "fig:triang": ["UAV"], "fig:floodfill": ["BFS"], "fig:wb-trace": ["NDC"], "fig:partb-glance": ["UAV", "LOD"]}


def used_in_drawing(body, a):
    """The short form is written in the float outside its caption, TikZ node text included."""
    mk = masked(body, tikz=False)
    return any(not inside(m.start(), mk) for m in occ(a).finditer(body))


def expand_in(seg, a):
    """seg with the first bare use of 'a' given its full form (None if there is no bare use)."""
    mk = masked(seg)
    for m in occ(a).finditer(seg):
        if inside(m.start(), mk):
            continue
        end = m.end() + (2 if seg[m.end():m.end() + 2] == "'s" else 0)
        form = long_form(a, m.group(1) == "s")
        if end != m.end():
            form = form.replace(" (", "'s (", 1)
        return seg[:m.start()] + form + seg[end:]
    return None


def has_full(seg, a):
    low = seg.lower()
    return ABBR[a].lower() in low or PLURAL.get(a, "\0").lower() in low


def expand_static(text):
    """Each float, caption and heading: the full form of every short form it uses. In a caption or heading the full form
    follows the first use; short forms that a float uses only in its body (table cells, algorithm lines) are spelled out in
    a 'Short forms' note at the end of its caption, so the cells keep their width."""
    k = 0
    while True:
        regions = static_regions(text)
        if k >= len(regions):
            return text
        s, e = regions[k]
        seg = text[s:e]
        caps = [m for m in re.finditer(r"\\caption(\[[^\]]*\])?\{", seg)] if seg.startswith("\\begin{") else []
        if caps:
            c0 = caps[-1].end()
            c1 = brace_end(seg, c0) - 1
            cap, notes = seg[c0:c1], []
            body = seg[:c0] + seg[c1:]
            for a in ORDER:
                if has_full(seg, a):
                    continue
                new = expand_in(cap, a)
                if new is not None:
                    cap = new
                elif used_in_drawing(body, a) or any(a in v and ("\\label{%s}" % k) in seg for k, v in EXTRA_NOTES.items()):
                    notes.append("%s: %s" % (a, ABBR[a]))
            if notes:
                cap = cap.rstrip() + (" " if cap.rstrip().endswith((".", ")")) else ". ") + "Short forms: " + "; ".join(notes) + "."
            seg = seg[:c0] + cap + seg[c1:]
        else:
            for a in ORDER:
                if has_full(seg, a):
                    continue
                new = expand_in(seg, a)
                if new is not None:
                    seg = new
        text = text[:s] + seg + text[e:]
        k += 1


def tag(text, counter):
    st, mk = static_regions(text), masked(text)
    hits = []
    for a in ORDER:
        for m in occ(a).finditer(text):
            if inside(m.start(), st) or inside(m.start(), mk) or any(h[0] <= m.start() < h[1] for h in hits):
                continue
            hits.append((m.start(), m.end(), m.group(0)))
    out, pos = [], 0
    for s, e, word in sorted(hits):
        counter[0] += 1
        if text[e:e + 2] == "'s":   # possessive: "GPU's (graphics processing unit) back-face test"
            out.append(text[pos:s] + "\\abp{%d}{%s}" % (counter[0], word))
            e += 2
        else:
            out.append(text[pos:s] + "\\ab{%d}{%s}" % (counter[0], word))
        pos = e
    return "".join(out) + text[pos:]


def files():
    m = open(os.path.join(REP, "main.tex")).read()
    pre = re.findall(r"\\input\{sections/([^}]+)\}", m)
    return [f for f in dict.fromkeys(pre) if "#" not in f and os.path.exists(os.path.join(SEC, f + ".tex"))
            and not f.startswith(("gen_ab_", "gen_numbers", "gen_game", "gen_fecrops"))] + ["gen_bench_text"]


def do_tag():
    counter = [0]
    for f in files() + ["../main"]:
        p = os.path.normpath(os.path.join(SEC, f + ".tex"))
        if f == "00_cover":
            continue
        head, text = "", open(p).read()
        if f == "../main":   # only the document body (the part introductions and their figures)
            k = text.index("\\begin{document}")
            head, text = text[:k], text[k:]
        text = strip_tags(text)
        text = undo_prose_expansions(text)
        text = expand_static(text)
        text = tag(text, counter)
        open(p, "w").write(head + text)
    with open(LONG, "w") as o:
        o.write("% generated by tools/report/abbrev_pages.py: the full form printed by \\ab at the first use on a page\n\\makeatletter\n")
        for a in ORDER:
            o.write("\\@namedef{abl@%s}{%s}\n" % (a, long_form(a, False)))
            o.write("\\@namedef{abl@%ss}{%s}\n" % (a, long_form(a, True)))
            o.write("\\@namedef{abf@%s}{%s}\n" % (a, ABBR[a]))
        o.write("\\makeatother\n")
    open(FIRST, "w").write("% generated by tools/report/abbrev_pages.py: ids of the short forms that are first on their page\n")
    print("tagged %d short forms in the running text" % counter[0])


def build():
    r = subprocess.run([os.path.expanduser("~/.local/bin/tectonic"), "-X", "compile", "main.tex", "--keep-logs", "--keep-intermediates"],
                       cwd=REP, capture_output=True, text=True)
    if r.returncode:
        sys.exit("tectonic failed:\n" + (r.stdout + r.stderr)[-3000:])


def decide():
    have = set(re.findall(r"abx@(\d+)\}", open(FIRST).read()))
    first = {}
    for m in re.finditer(r"\\abpage\{(\d+)\}\{([^{}]*)\}\{([^{}]*)\}", open(os.path.join(REP, "main.aux")).read()):
        i, word, page = m.groups()
        base = word[:-1] if word.endswith("s") and word[:-1] in ABBR else word
        first.setdefault((page, base), i)
    new = set(first.values()) - have
    if new:
        with open(FIRST, "a") as o:
            o.write("\\makeatletter\n" + "".join("\\@namedef{abx@%s}{}\n" % i for i in sorted(new, key=int)) + "\\makeatother\n")
    return len(new), len(have) + len(new)


def verify():
    import pymupdf
    d = pymupdf.open(os.path.join(REP, "main.pdf"))
    bad = []
    for i, p in enumerate(d):
        raw = p.get_text()
        t = re.sub(r"\s+", " ", re.sub(r"-\n", "", raw))
        low = t.lower() + " " + re.sub(r"\s+", " ", raw.replace("-\n", "-")).lower()   # words broken at a line end, both readings
        bare = t
        for b in ORDER:
            bare = re.sub(re.escape(ABBR[b]), " ", bare, flags=re.I)
        for a in ORDER:
            if not occ(a).search(bare):
                continue
            full = re.sub(r"\\&", "&", re.sub(r"\$[^$]*\$", "", ABBR[a])).replace("--", "\u2013").strip().lower()
            plural = PLURAL.get(a, "\0").lower()
            if full[:14] in low or plural[:14] in low:
                continue
            bad.append((i + 1, a))
    print("pages: %d; short forms on a page without their full form: %d" % (len(d), len(bad)))
    by = {}
    for pg, a in bad:
        by.setdefault(a, []).append(pg)
    for a, pgs in sorted(by.items(), key=lambda x: -len(x[1])):
        print("  %-7s pdf pages %s" % (a, " ".join(map(str, pgs[:30])) + (" ..." if len(pgs) > 30 else "")))
    return bad


def main():
    if sys.argv[1:] == ["verify"]:
        verify()
        return
    do_tag()
    for it in range(8):
        build()
        new, total = decide()
        print("pass %d: %d new first uses, %d in all" % (it + 1, new, total), flush=True)
        if not new:
            break
    try:
        verify()
    except ImportError:
        print("pymupdf not available: run the verify step with uv")


if __name__ == "__main__":
    main()
