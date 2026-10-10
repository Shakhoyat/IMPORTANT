"""Captions as titles: every figure and table caption of the hand-written sections is cut to its first sentence
(the explanation belongs to the text). A trailing \\fullsheet{...} pointer is kept. Idempotent.
Run: python3 tools/report/shorten_captions.py"""
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REP = os.path.join(ROOT, "report")
ABBREV_END = re.compile(r"(Eqs?|Figs?|Secs?|Algs?|e\.g|i\.e|vs|cf|No|approx|resp|Tab)$")


def brace_end(text, i):
    depth = 1
    while i < len(text) and depth:
        c = text[i]
        if c == "\\":
            i += 2
            continue
        depth += {"{": 1, "}": -1}.get(c, 0)
        i += 1
    return i


def first_sentence(cap):
    depth, math, i = 0, False, 0
    while i < len(cap):
        c = cap[i]
        if c == "\\":
            i += 2
            continue
        if c == "$":
            math = not math
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        elif c == "." and depth == 0 and not math and (i + 1 == len(cap) or cap[i + 1] in " \n~"):
            head = cap[:i]
            if not ABBREV_END.search(head) and not re.search(r"\d$", head[-1:]) and len(re.findall(r"[A-Za-z]{2,}", head)) >= 4:
                return cap[:i + 1]
        i += 1
    return cap


def shorten(text):
    out, pos, n, before, after = [], 0, 0, 0, 0
    for m in re.finditer(r"\\caption(of\{\w+\})?(\[[^\]]*\])?\{", text):
        if m.start() < pos:
            continue
        e = brace_end(text, m.end())
        cap = text[m.end():e - 1]
        keep = "".join(re.findall(r"\s*\\fullsheet\{[^}]*\}", cap)) + "".join(re.findall(r"\\label\{[^}]*\}", cap))
        core = re.sub(r"\s*\\fullsheet\{[^}]*\}|\\label\{[^}]*\}", "", cap)
        new = first_sentence(core.strip())
        # a long one-sentence caption: the part before its colon is the title (kept whole when the rest names the panels)
        if len(new.split()) > 28 and ":" in new:
            headc, tail = new.split(":", 1)
            if len(re.findall(r"[A-Za-z]{2,}", headc)) >= 4 and headc.count("$") % 2 == 0 and headc.count("{") == headc.count("}") \
                    and not re.search(r"\(a\)|\(b\)", tail):
                new = headc.rstrip() + "."
        before += len(core.split())
        after += len(new.split())
        if new != core.strip():
            n += 1
        out.append(text[pos:m.end()] + new + keep)
        pos = e - 1
    out.append(text[pos:])
    return "".join(out), n, before, after


def main():
    tot = [0, 0, 0]
    files = [os.path.join(REP, "sections", f) for f in sorted(os.listdir(os.path.join(REP, "sections")))
             if f.endswith(".tex") and not f.startswith("gen_")] + [os.path.join(REP, "main.tex")]
    for p in files:
        text = open(p).read()
        head, body = ("", text)
        if p.endswith("main.tex"):
            k = text.index("\\begin{document}")
            head, body = text[:k], text[k:]
        new, n, b, a = shorten(body)
        if new != body:
            open(p, "w").write(head + new)
        tot = [tot[0] + n, tot[1] + b, tot[2] + a]
    print("captions shortened: %d; caption words %d -> %d" % tuple(tot))


if __name__ == "__main__":
    main()
