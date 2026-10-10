"""Abbreviations: the full form at the first use of every short form (no separate list).

Every abbreviation the report uses is defined once below (ABBR). Running the script expands the first occurrence in
the hand-written prose (sections in \\input order; generated files, code, keys, maths, labels and headings are
skipped) to "full form (ABBR)", unless that file already gives it. Section 7 (B01_decomposition.tex and its generated
cards) is skipped here: tools/report/gen_designs.py imports ABBR and writes Section 7's own first-use expansions.
Idempotent: an abbreviation already written as "... (ABBR)" before its first bare use is left alone.
Run: python3 tools/report/abbrev.py"""
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REP = os.path.join(ROOT, "report")

# abbreviation: (full form used in the text, optional note for the list)
ABBR = {
    "AA": ("anti-aircraft", ""),
    "AC": ("air-conditioning", ""),
    "ACES": ("Academy Color Encoding System", "its filmic tone curve"),
    "AI": ("artificial intelligence", "here: the units' decision logic"),
    "AO": ("ambient occlusion", ""),
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
    "DDA": ("digital differential analyser", ""),
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
    "HESCO": ("wire-mesh gabion", "named after its maker; written HESCO (wire-mesh gabion)"),
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
    "PCF": ("percentage-closer filtering", ""),
    "PD": ("proportional--derivative", "controller"),
    "PSF": ("point spread function", ""),
    "PT": ("physical training", ""),
    "RGB": ("red, green, blue", ""),
    "RT": ("ray tracing", ""),
    "SAH": ("surface area heuristic", ""),
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
REVERSED = {"HESCO"}   # written "HESCO (wire-mesh gabion)": the maker's name is the usual word, the gabion its meaning
NO_EXPAND = {"KUET", "CSE"}   # spelled out on the cover; only the running header uses the short forms

SKIP = re.compile(r"\\(code|key|label|ref|eqref|cite|input|raw|img|plot|wbblock|fullsheet|includegraphics|hyperref|primchipn?|primsq|"
                  r"section\*?|subsection\*?|paragraph|here|texttt|url|lstinline|gv|alg[A-Z]\w*|flow[A-Z]\w*)(\[[^\]]*\])?\{")


def masked(text):
    """Spans where an abbreviation must not be expanded: commands with arguments, maths, comments, code, pictures."""
    spans = []
    for m in SKIP.finditer(text):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        spans.append((m.start(), i))
    for m in re.finditer(r"\$\$.*?\$\$|\$[^$]*\$|(?<!\\)\\\[.*?\\\]|(?<!\\)%[^\n]*|\\begin\{(equation|gather|align|lstlisting|tikzpicture|algorithmic)\*?\}.*?"
                         r"\\end\{\1\*?\}", text, re.S):
        spans.append((m.start(), m.end()))
    return spans


def long_form(a, plural):
    full = ABBR[a][0]
    if a in REVERSED:
        return "%s%s (%s)" % (a, "s" if plural else "", full)
    return "%s (%s%s)" % (PLURAL.get(a, full + "s") if plural else full, a, "s" if plural else "")


def expand_first(text, seen):
    """Expand the first bare use of every abbreviation not in 'seen' (updated in place)."""
    for a in ABBR:
        if a in seen or a in NO_EXPAND:
            continue
        if re.search(r"\(%ss?\)|%s \(%s\)" % (re.escape(a), re.escape(a), re.escape(ABBR[a][0])), text):
            seen.add(a)
            continue
        spans = masked(text)
        for m in re.finditer(r"(?<![A-Za-z\\{/_-])%s(s)?(?![A-Za-z0-9}_])" % re.escape(a), text):
            if any(s <= m.start() < e for s, e in spans):
                continue
            text = text[:m.start()] + long_form(a, m.group(1) == "s") + text[m.end():]
            seen.add(a)
            break
    return text


def main():
    order = [f for f in re.findall(r"\\input\{sections/([^}]+)\}", open(os.path.join(REP, "main.tex")).read()) if "#" not in f]
    skip = {"algorithms", "flows", "B01_decomposition"}
    body = [f for f in order if not f.startswith("gen_") and f not in skip]
    seen = set()
    for f in body:
        p = os.path.join(REP, "sections", f if f.endswith(".tex") else f + ".tex")
        text = open(p).read()
        before = set(seen)
        new = expand_first(text, seen)
        if new != text:
            open(p, "w").write(new)
            print("expanded %-30s in %s" % (" ".join(sorted(seen - before)), f))
    rest = [a for a in ABBR if a not in seen and a not in NO_EXPAND]
    print("not used in the prose (generated text or not at all):", rest)


if __name__ == "__main__":
    main()
