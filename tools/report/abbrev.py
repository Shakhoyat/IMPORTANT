"""Abbreviations: the list after the contents (report/sections/gen_abbrev.tex) and the full form at first use.

Every abbreviation the report uses is defined once below. Running the script
  1. writes \\abbrevlist (a two-column list, sorted, only the abbreviations that occur in the report), and
  2. expands the first occurrence in the hand-written prose (sections in \\input order; generated tables, code, keys,
     maths, labels, headings and captions are skipped) to "full form (ABBR)", unless that sentence already gives it.
Idempotent: an abbreviation already written as "... (ABBR)" somewhere before its first bare use is left alone.
Run: python3 tools/report/abbrev.py"""
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REP = os.path.join(ROOT, "report")

# abbreviation: (full form used in the text, optional note for the list)
ABBR = {
    "AA": ("anti-aircraft", ""),
    "AI": ("artificial intelligence", "here: the units' decision logic"),
    "API": ("application programming interface", ""),
    "BFS": ("breadth-first search", ""),
    "BRDF": ("bidirectional reflectance distribution function", ""),
    "BVH": ("bounding volume hierarchy", ""),
    "CAD": ("computer-aided design", ""),
    "CC0": ("Creative Commons Zero", "public-domain dedication of the imported assets"),
    "CDF": ("cumulative distribution function", ""),
    "CIC": ("command information centre", "the bunker's command room"),
    "CPU": ("central processing unit", ""),
    "CSE": ("Computer Science and Engineering", ""),
    "CSV": ("comma-separated values", ""),
    "CV": ("computer vision", ""),
    "DFT": ("discrete Fourier transform", ""),
    "DN": ("digital number", "the sensor's raw output value"),
    "DOF": ("depth of field", ""),
    "EO": ("electro-optical", ""),
    "EV": ("exposure value", "one EV is a factor of two in light"),
    "FLIR": ("forward-looking infrared", ""),
    "FOV": ("field of view", ""),
    "FPV": ("first-person view", ""),
    "fps": ("frames per second", ""),
    "GGX": ("Trowbridge--Reitz microfacet distribution", "name used in the rendering literature"),
    "GLAD": ("OpenGL loader generator", "loads the OpenGL function pointers"),
    "GLFW": ("Graphics Library Framework", "window and input library"),
    "GLM": ("OpenGL Mathematics", "header-only maths library"),
    "GLSL": ("OpenGL Shading Language", ""),
    "glTF": ("GL Transmission Format", "3D model file format"),
    "GPS": ("Global Positioning System", ""),
    "GPU": ("graphics processing unit", ""),
    "GTAO": ("ground-truth ambient occlusion", ""),
    "HDR": ("high dynamic range", ""),
    "HESCO": ("HESCO Bastion barrier", "wire-mesh and fabric gabion wall, named after its maker"),
    "HSR": ("hidden surface removal", ""),
    "HUD": ("head-up display", ""),
    "IFF": ("identification friend or foe", ""),
    "IK": ("inverse kinematics", ""),
    "IR": ("infrared", ""),
    "ISO": ("International Organization for Standardization", "ISO container: the standard shipping container"),
    "JSON": ("JavaScript Object Notation", ""),
    "KUET": ("Khulna University of Engineering \\& Technology", ""),
    "LED": ("light-emitting diode", ""),
    "LOD": ("level of detail", ""),
    "MG": ("machine gun", ""),
    "MLRS": ("multiple launch rocket system", ""),
    "MSAA": ("multisample anti-aliasing", ""),
    "NDC": ("normalised device coordinates", ""),
    "NUC": ("non-uniformity correction", ""),
    "NVG": ("night-vision goggles", ""),
    "OpenGL": ("Open Graphics Library", ""),
    "PBR": ("physically based rendering", ""),
    "PCSS": ("percentage-closer soft shadows", ""),
    "PD": ("proportional--derivative", "controller"),
    "PSF": ("point spread function", ""),
    "PT": ("physical training", ""),
    "RGB": ("red, green, blue", ""),
    "RT": ("ray tracing", ""),
    "SAM": ("surface-to-air missile", ""),
    "SH": ("spherical harmonics", ""),
    "sRGB": ("standard RGB colour space", ""),
    "SSAO": ("screen-space ambient occlusion", ""),
    "SSR": ("screen-space reflections", ""),
    "TAA": ("temporal anti-aliasing", ""),
    "TRS": ("translate, rotate, scale", "the order of a model matrix"),
    "UAV": ("uncrewed aerial vehicle", ""),
    "UV": ("texture coordinates $(u,v)$", ""),
    "VHF": ("very high frequency", ""),
    "WSL2": ("Windows Subsystem for Linux 2", ""),
}
PLURAL = {"UAV": "uncrewed aerial vehicles", "GPU": "graphics processing units", "LED": "light-emitting diodes",
          "SAM": "surface-to-air missiles", "CPU": "central processing units"}
NO_EXPAND = {"fps", "OpenGL", "RGB", "sRGB", "UV", "ISO", "HESCO", "GGX", "GLAD", "KUET", "CSE"}  # list only

order = [f for f in re.findall(r"\\input\{sections/([^}]+)\}", open(os.path.join(REP, "main.tex")).read()) if "#" not in f]
body = [f for f in order if not f.startswith("gen_") and f != "00_cover"]
all_text = "".join(open(os.path.join(REP, "sections", (f if f.endswith(".tex") else f + ".tex"))).read() for f in order)
all_text += open(os.path.join(REP, "main.tex")).read()


def occurs(a, text):
    return re.search(r"(?<![A-Za-z\\])%s(s)?(?![A-Za-z0-9])" % re.escape(a), text) is not None


# 1. the list
used = sorted([a for a in ABBR if occurs(a, all_text)], key=lambda a: a.lower())
rows = []
for a in used:
    full, note = ABBR[a]
    rows.append("\\textbf{%s} & %s%s\\\\" % (a, full, (" (" + note + ")") if note else ""))
half = (len(rows) + 1) // 2
tex = ["% generated by tools/report/abbrev.py: do not edit",
       "\\newcommand{\\abbrevlist}{\\section*{Abbreviations}\\addcontentsline{toc}{section}{Abbreviations}",
       "{\\small\\setlength{\\tabcolsep}{4pt}\\renewcommand{\\arraystretch}{1.08}",
       "\\noindent\\begin{tabular}[t]{@{}>{\\raggedright\\arraybackslash}p{1.45cm}>{\\raggedright\\arraybackslash}p{6.9cm}@{}}",
       "\n".join(rows[:half]),
       "\\end{tabular}\\hfill\\begin{tabular}[t]{@{}>{\\raggedright\\arraybackslash}p{1.45cm}>{\\raggedright\\arraybackslash}p{6.9cm}@{}}",
       "\n".join(rows[half:]),
       "\\end{tabular}}}"]
open(os.path.join(REP, "sections", "gen_abbrev.tex"), "w").write("\n".join(tex) + "\n")
print("list:", len(used), "abbreviations")

# 2. first use in the prose
SKIP = re.compile(r"\\(code|key|label|ref|eqref|cite|input|raw|img|plot|wbblock|fullsheet|includegraphics|hyperref|primchipn?|primsq|"
                  r"section\*?|subsection\*?|caption|here|texttt|url)(\[[^\]]*\])?\{")


def masked(text):
    """Spans where an abbreviation must not be expanded: commands with arguments, maths, comments."""
    spans = []
    for m in SKIP.finditer(text):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        spans.append((m.start(), i))
    for m in re.finditer(r"\$[^$]*\$|%[^\n]*|\\begin\{(equation|gather|align)\*?\}.*?\\end\{\1\*?\}", text, re.S):
        spans.append((m.start(), m.end()))
    return spans


done = set()
for a in ABBR:
    if a in NO_EXPAND:
        done.add(a)
for f in body:
    p = os.path.join(REP, "sections", f if f.endswith(".tex") else f + ".tex")
    text = open(p).read()
    changed = False
    for a, (full, _) in ABBR.items():
        if a in done:
            continue
        if re.search(r"\(%ss?\)" % re.escape(a), text):     # already expanded here
            done.add(a)
            continue
        spans = masked(text)
        for m in re.finditer(r"(?<![A-Za-z\\{])%s(s)?(?![A-Za-z0-9}])" % re.escape(a), text):
            if any(s <= m.start() < e for s, e in spans):
                continue
            plural = m.group(1) == "s"
            long = PLURAL.get(a, full + "s") if plural else full
            text = text[:m.start()] + "%s (%s)" % (long, m.group(0)) + text[m.end():]
            done.add(a)
            changed = True
            print("expanded %-6s in %s" % (a, f))
            break
    if changed:
        open(p, "w").write(text)
missing = [a for a in used if a not in done]
print("only in tables / headings / captions (list only):", missing)
