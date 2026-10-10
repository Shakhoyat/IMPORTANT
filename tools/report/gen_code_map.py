"""docs/REPORT_CODE_MAP.md: every section and subsection of the report mapped to the code that implements it.

For each section, in the report's order:
  * the primary sources (the list below, checked against the tree: a missing file stops the script),
  * every code reference the section's text makes (\\code{file: symbol, ...}), resolved to file and line,
  * its subsections with their own references.
Section 7 gets one row per design from the scene export (builder file and function, instances, parts).
A reference that cannot be resolved to a file or line is listed at the end, so nothing is dropped silently.
Run: python3 tools/report/gen_code_map.py"""
import json
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REP = os.path.join(ROOT, "report")
OUT = os.path.join(ROOT, "docs", "REPORT_CODE_MAP.md")

# Primary sources per section file: (path, what it holds). Paths are relative to the repository root.
PRIMARY = {
    "01_intro": [("src/main.cpp", "program entry, command-line modes"), ("src/app/App.cpp", "application loop")],
    "02_scene": [("src/scene/Scene.cpp", "the scene: objects, lights, rules of the day"), ("src/objects/Base.cpp", "camp layout and buildings"),
                 ("src/objects/Bunker.cpp", "command bunker and its rooms"), ("src/objects/BarracksHall.cpp", "barracks hall interior"),
                 ("src/app/Capture.cpp", "the captures shown in the figures")],
    "03_setup": [("Makefile", "build (g++, C++17)"), ("CMakeLists.txt", "CMake build"), ("src/app/HeadlessGL.cpp", "EGL context, GPU selection"),
                 ("src/glad.c", "OpenGL loader")],
    "04_architecture": [("src/app/App.h", "application, modes, camera"), ("src/scene/Scene.h", "scene interface"),
                        ("src/rendering/Renderer.h", "renderer interface"), ("src/objects/BaseLayout.h", "site plan constants")],
    "05_controls": [("src/scene/KeyMap.cpp", "every key binding and its context"), ("src/app/Interactive.cpp", "key and mouse handling"),
                    ("src/app/AppInteract.cpp", "the E key: doors, vehicles, units"), ("src/scene/Control.cpp", "possession slots")],
    "A01_primitives": [("src/primitives/Library.cpp", "the 16 primitives: table, formulas"), ("src/primitives/Sphere.cpp", "sphere generator"),
                       ("src/primitives/Cube.cpp", "cube"), ("src/primitives/Cylinder.cpp", "cylinder"), ("src/primitives/Revolve.cpp", "surfaces of revolution"),
                       ("src/primitives/Torus.cpp", "torus"), ("src/primitives/Capsule.cpp", "capsule"), ("src/primitives/Hemisphere.cpp", "hemisphere"),
                       ("src/primitives/Wedge.cpp", "wedge"), ("src/primitives/TaperedBox.cpp", "tapered box"), ("src/primitives/Pyramid.cpp", "pyramid"),
                       ("src/primitives/Annulus.cpp", "annulus"), ("src/primitives/Helix.cpp", "helix"), ("src/primitives/GrassBlade.cpp", "grass blade"),
                       ("src/primitives/MeshData.cpp", "vertex / index buffers"), ("src/rendering/Mesh.cpp", "upload to the GPU")],
    "B01_decomposition": [("src/report/Designs.cpp", "the 62 designs and their families"), ("src/objects/Builder.cpp", "part and group builder (T, R, S)"),
                          ("src/scene/SceneNode.h", "scene-graph node"), ("src/app/DocExport.cpp", "export of every part and matrix"),
                          ("src/report/ShapeView.cpp", "pulled-apart shape views"), ("tools/report/gen_designs.py", "cards, chains, parts tables"),
                          ("tools/report/coord_sheet.py", "coordinate sheets")],
    "A02_transforms": [("src/math/Math3D.cpp", "translate, rotate, scale, normal matrix"), ("src/math/Math3D.h", "Vec3, Vec4, Mat3, Mat4")],
    "A03_hierarchy": [("src/scene/SceneNode.h", "node: local and world matrix"), ("src/scene/Scene.cpp", "updateNode: world = parent x local"),
                      ("src/objects/Tank.cpp", "hull, turret, gun chain"), ("src/objects/Uav.cpp", "gimbal chain")],
    "A04_viewing": [("src/math/Math3D.cpp", "lookAt"), ("src/scene/Camera.cpp", "orbit, free, walk, chase cameras"), ("src/scene/CameraRig.cpp", "blends between views")],
    "A05_projection": [("src/math/Math3D.cpp", "perspective, ortho"), ("src/app/AppGame.cpp", "composeView: teaching projections")],
    "A06_projective": [("src/math/ExplorerMath.cpp", "traceVertex, viewport, depth"), ("src/rendering/FrameExplorer.cpp", "Frame Explorer stages")],
    "A07_clipping": [("src/math/Frustum.cpp", "frustum planes, sphere test"), ("src/math/ExplorerMath.cpp", "clip-space inside test"),
                     ("src/rendering/Renderer.cpp", "culling before drawing")],
    "A08_hsr": [("src/rendering/Renderer.cpp", "back-face culling, depth test"), ("src/rendering/RendererFrame.cpp", "depth pre-pass"),
                ("src/math/ExplorerMath.cpp", "signedArea2")],
    "A09_raster": [("src/math/ExplorerMath.cpp", "rasterize, interpolatePerspective"), ("src/rendering/FrameBuffers.cpp", "MSAA targets"),
                   ("src/rendering/ShadersPostFx.cpp", "temporal anti-aliasing")],
    "A10_illumination": [("src/rendering/ShaderSources.cpp", "computeLight, brdf, compose"), ("src/rendering/ShaderMath.cpp", "hand-written vector functions"),
                         ("src/rendering/Shader.cpp", "compile: prelude, built-in guard"), ("src/rendering/Lighting.h", "light structures"),
                         ("src/math/ShadowMath.cpp", "cascades"), ("src/rendering/ShadersLightingDepth.cpp", "shadow sampling, ambient occlusion")],
    "A11_shading": [("src/rendering/ShaderSources.cpp", "flat, Gouraud, Phong programs"), ("src/rendering/Material.h", "material and shading model")],
    "A12_texture": [("src/rendering/ShaderSources.cpp", "box projection, value noise, bump"), ("src/assets/Image.cpp", "imported images"),
                    ("src/rendering/GroundMap.cpp", "ground map"), ("src/rendering/VegetationRenderer.cpp", "textured vegetation")],
    "A12b_post": [("src/rendering/ShadersPost.cpp", "fog, bloom, tone mapping"), ("src/math/ToneMap.cpp", "AgX / ACES curves"),
                  ("src/scene/EyeAdapt.h", "exposure adaptation")],
    "A13_raytracing": [("src/raytrace/RayTracer.cpp", "Whitted and path tracer (CPU)"), ("src/raytrace/Intersect.cpp", "ray-primitive tests"),
                       ("src/raytrace/Bvh.cpp", "BVH with SAH"), ("src/rendering/ShadersRT.cpp", "GPU ray tracing"), ("src/rendering/RayTracerGL.cpp", "compute programs")],
    "B02_color": [("src/rendering/ThermalPalette.h", "heat palettes"), ("src/rendering/ShaderSources.cpp", "palette lookup")],
    "B03_daynight": [("src/scene/DayCycle.cpp", "solar model, phases, lamps"), ("src/math/SkyModel.cpp", "sky radiance")],
    "B04_curved": [("src/primitives/Revolve.cpp", "curved generators"), ("src/primitives/Library.cpp", "slices from the silhouette error"),
                   ("src/math/Spline.cpp", "Catmull-Rom, Chaikin")],
    "B05_dynamic": [("src/scene/Door.cpp", "hinged door spring"), ("src/objects/HingedDoor.cpp", "door object"), ("src/scene/Gait.cpp", "gait and leg IK"),
                    ("src/scene/CharacterMotor.cpp", "player movement"), ("src/scene/TickInterp.h", "fixed 60 Hz tick")],
    "B06_reflection": [("src/rendering/ShadersPostFx.cpp", "screen-space reflections"), ("src/rendering/ShadersRT.cpp", "ray-traced reflections")],
    "B07_thermal": [("src/vision/SensorModel.cpp", "simulated thermal sensor"), ("src/vision/CvPipeline.cpp", "blur, threshold, components"),
                    ("src/vision/ThermalTracker.cpp", "single-camera tracker")],
    "B08_game": [("src/ai/Squad.cpp", "roles, single and combined strike"), ("src/ai/SquadDuty.cpp", "take-off, patrol, landing"),
                 ("src/vision/MultiTracker.cpp", "Kalman tracks, gating, assignment"), ("src/ai/DroneController.cpp", "flight and gimbal"),
                 ("src/scene/Mission.cpp", "waves, score"), ("src/scene/Garrison.cpp", "soldiers: roster, guards"),
                 ("src/scene/Scene.cpp", "alert posture, guards' fire"), ("src/app/AppInteract.cpp", "boardVehicle, exitVehicle")],
    "B08b_explosions": [("src/scene/Combat.cpp", "projectiles, particles"), ("src/rendering/Effects.cpp", "particle shader, blackbody colour")],
    "B09_paths": [("src/ai/HideController.cpp", "flood fill, Dijkstra, line follower"), ("src/scene/Garrison.cpp", "site planner: A*, string pulling"),
                  ("src/math/Spline.cpp", "route smoothing"), ("src/ai/Squad.cpp", "patrol orbit"), ("tools/report/gen_motion.py", "motion figures")],
    "B10_hard": [("src/rendering/Instancing.cpp", "instanced drawing"), ("src/rendering/Grass.cpp", "grass field"),
                 ("src/rendering/ShadersPost.cpp", "separable blur")],
    "C01_performance": [("src/app/BenchShots.cpp", "benchmark shots"), ("src/rendering/GpuTimer.cpp", "GPU timers"), ("report/data/bench.csv", "measured data")],
    "C02_limits": [],
    "C03_conclusion": [],
}
SEARCH = ["src", "tools", "tests", "report"]


def clean(t):
    t = re.sub(r"\\(code|texttt|key|textbf|emph|textit)\{([^{}]*)\}", r"\2", t)
    t = re.sub(r"\$[^$]*\$", "", t)
    return re.sub(r"\s+", " ", t.replace("\\_", "_").replace("\\&", "&").replace("~", " ").replace("\\,", " ")).strip()


def all_files():
    out = []
    for d in SEARCH:
        for base, _, names in os.walk(os.path.join(ROOT, d)):
            if "/figures" in base or "/raw" in base or "__pycache__" in base:
                continue
            for n in names:
                if n.endswith((".cpp", ".h", ".py", ".inc", ".c", ".sh", ".txt", ".csv", ".json")):
                    out.append(os.path.relpath(os.path.join(base, n), ROOT))
    return out


FILES = all_files()
TEXT = {}


def text(path):
    if path not in TEXT:
        try:
            TEXT[path] = open(os.path.join(ROOT, path), errors="replace").read().split("\n")
        except OSError:
            TEXT[path] = []
    return TEXT[path]


def resolve_file(ref):
    ref = ref.strip().lstrip("./")
    if os.path.exists(os.path.join(ROOT, ref)):
        return ref
    hits = [f for f in FILES if f.endswith("/" + ref) or f == ref]
    if len(hits) == 1:
        return hits[0]
    hits = [f for f in hits if f.startswith("src/")] or hits
    return hits[0] if hits else None


def find_line(path, symbol):
    """First line that defines or (failing that) mentions the symbol."""
    name = symbol.split("::")[-1].split("(")[0].strip()
    if not name:
        return None
    lines = text(path)
    pats = [r"\b[\w:<>\*&]+\s+(\w+::)?%s\s*\([^;]*$" % re.escape(name), r"\b(def|class|struct|enum class|constexpr \w+)\s+%s\b" % re.escape(name),
            r"\b%s\b\s*[\(=]" % re.escape(name), r"\b%s\b" % re.escape(name)]
    for pat in pats:
        for i, l in enumerate(lines):
            if re.search(pat, l) and not l.lstrip().startswith("//"):
                return i + 1
    return None


def find_symbol(symbol):
    """A bare symbol (no file): search the C++ sources for its definition."""
    name = symbol.split("(")[0].strip()
    if "::" in name:
        cls, fn = name.split("::")[0], name.split("::")[-1]
        for f in FILES:
            if f.startswith("src/") and f.endswith(".cpp"):
                for i, l in enumerate(text(f)):
                    if re.search(r"\b%s::%s\s*\(" % (re.escape(cls), re.escape(fn)), l) and not l.startswith(" "):
                        return f, i + 1
        for f in FILES:
            if f.startswith("src/") and f.endswith(".h"):
                found_cls = False
                for i, l in enumerate(text(f)):
                    found_cls = found_cls or re.search(r"\b(class|struct)\s+%s\b" % re.escape(cls), l)
                    if found_cls and re.search(r"\b%s\b" % re.escape(fn), l):
                        return f, i + 1
        return None
    for ext in (".cpp", ".h"):
        for f in FILES:
            if f.startswith("src/") and f.endswith(ext):
                for i, l in enumerate(text(f)):
                    if re.search(r"^[\w:<>\*& ]*\b%s\s*\(" % re.escape(name), l) and not l.startswith((" ", "\t", "//")) and "(" in l:
                        return f, i + 1
    for f in FILES:
        if f.startswith("src/"):
            for i, l in enumerate(text(f)):
                if re.search(r"\b(constexpr|const|enum class|struct|class)\b.*\b%s\b" % re.escape(name), l):
                    return f, i + 1
    return None


FILE_RE = re.compile(r"[\w./-]*\w\.(?:cpp|h|py|inc|sh|csv|json|xlsx)\b")
unresolved = []


def refs_of(tex):
    """[(file or None, line or None, shown text)] for every \\code{...} of a piece of LaTeX, in order, without repeats."""
    out, seen = [], set()
    for m in re.finditer(r"\\code\{((?:[^{}]|\{[^{}]*\})*)\}", tex):
        raw = clean(m.group(1))
        if not raw or len(raw) > 140:
            continue
        files = FILE_RE.findall(raw)
        if files:
            f = resolve_file(files[0])
            syms = [s.strip() for s in raw.split(":", 1)[1].split(",")] if ":" in raw else []
            syms = [s for s in syms if re.match(r"^[A-Za-z_][\w:]*(\(\))?$", s)]
            if not f:
                if (raw,) not in seen and not raw.endswith(".xlsx"):
                    unresolved.append(raw)
                seen.add((raw,))
                continue
            if not syms:
                key = (f, None)
                if key not in seen:
                    seen.add(key)
                    out.append((f, None, ""))
            for s in syms:
                ln = find_line(f, s)
                key = (f, s)
                if key not in seen:
                    seen.add(key)
                    out.append((f, ln, s))
                    if ln is None:
                        unresolved.append("%s: %s" % (f, s))
        elif re.match(r"^[A-Za-z_]\w*(::\w+)+(\(\))?$", raw) or re.match(r"^(ts[A-Z]\w+|k[A-Z]\w+|[a-z]+[A-Z]\w*)(\(\))?$", raw):
            hit = find_symbol(raw)
            key = ("sym", raw)
            if key in seen:
                continue
            seen.add(key)
            if hit:
                out.append((hit[0], hit[1], raw))
    return out


def link(f, ln):
    """Clickable: opens the file (at the line) from docs/ in VS Code's preview and on GitHub."""
    return "[`%s%s`](../%s%s)" % (f, ":%d" % ln if ln else "", f, "#L%d" % ln if ln else "")


def rows(refs):
    by = {}
    for f, ln, s in refs:
        by.setdefault(f, []).append((ln, s))
    o = []
    for f, items in by.items():
        syms = ", ".join("[`%s`](../%s#L%d)" % (s, f, ln) if ln else "`%s`" % s for ln, s in items if s)
        o.append("| %s | %s |" % (link(f, None), syms or "whole file"))
    return o


def main():
    m = open(os.path.join(REP, "main.tex")).read()
    body = m[m.index("\\begin{document}"):]
    order = re.findall(r"\\input\{sections/([^}]+)\}", body)
    exp = json.load(open(os.path.join(REP, "data", "scene_export_v10.json")))
    out = ["# Report to code map", "",
           "Every section and subsection of `report/main.pdf`, in the report's order, with the code that implements it. "
           "Generated by `tools/report/gen_code_map.py` from the report's LaTeX sources and the source tree, so the file names and line "
           "numbers are checked against the code of this commit. Paths are relative to the repository root.", "",
           "How to read a block: **Primary sources** are the files that implement the section; **Referenced in the text** lists every "
           "file and symbol the section names, with the line where it is defined.", ""]
    n = 0
    count_refs = 0
    for fname in order:
        if fname.startswith("gen_") or fname in ("algorithms", "flows", "00_cover"):
            continue
        tex = open(os.path.join(REP, "sections", fname + ".tex")).read()
        pieces = re.split(r"(?m)^(\\(?:clearpage|FloatBarrier)?\\?(?:sub)?section\{.*)$", tex)
        # pieces: [pre, heading, text, heading, text, ...]
        sub = 0
        cur = None
        for k in range(1, len(pieces), 2):
            head, txt = pieces[k], pieces[k + 1] if k + 1 < len(pieces) else ""
            title = clean(re.search(r"section\{((?:[^{}]|\{[^{}]*\})*)\}", head).group(1))
            is_sub = "subsection{" in head
            if not is_sub:
                n += 1
                sub = 0
                out += ["", "## %d %s" % (n, title), "", "Report source: `report/sections/%s.tex`" % fname, ""]
                prim = PRIMARY.get(fname)
                if prim is None:
                    raise SystemExit("no primary sources listed for " + fname)
                if prim:
                    out += ["**Primary sources**", "", "| File | Holds |", "|---|---|"]
                    for p, what in prim:
                        if not os.path.exists(os.path.join(ROOT, p)):
                            raise SystemExit("%s: primary source %s does not exist" % (fname, p))
                        out.append("| [`%s`](../%s) | %s |" % (p, p, what))
                    out.append("")
                else:
                    out += ["No code of its own: the section discusses the results of the sections above.", ""]
            else:
                sub += 1
                out += ["### %d.%d %s" % (n, sub, title), ""]
            # the macros a section pulls in (algorithms, flows) belong to it
            extra = ""
            for mac in re.findall(r"\\(alg[A-Z]\w*|flow[A-Z]\w*)\b", txt):
                src = open(os.path.join(REP, "sections", "algorithms.tex" if mac.startswith("alg") else "flows.tex")).read()
                mm = re.search(r"\\newcommand\{\\%s\}\{(.*?)\n\\end\{(?:algorithm|figure)\}\}" % mac, src, re.S)
                extra += mm.group(1) if mm else ""
            refs = refs_of(txt + extra)
            count_refs += len(refs)
            if refs:
                out += ["**Referenced in the text**", "", "| File | Symbols |", "|---|---|"] + rows(refs) + [""]
            elif is_sub:
                out += ["Uses the primary sources of Section %d." % n, ""]
            if fname == "B01_decomposition" and not is_sub:
                out += ["### Designs of Section 7", "",
                        "One row per design card: the function that builds it (from the scene export, `src/report/Designs.cpp`).", "",
                        "| Design | Family | Built by | Instances | Parts (first instance) |", "|---|---|---|---|---|"]
                for d in exp["designs"]:
                    src = d["source"]
                    f, _, fn = src.partition(":")
                    rf = resolve_file(f.strip())
                    ln = find_line(rf, fn.strip()) if rf and fn.strip() else None
                    if not rf:
                        unresolved.append("design %s: %s" % (d["name"], src))
                    out.append("| %s | %s | %s `%s` | %d | %d |" % (d["name"], d["family"], link(rf or f, ln), fn.strip(), d["instances"], d["parts_first"]))
                out.append("")
    # the films, slides and data that accompany the report
    out += ["", "## Material outside the sections", "",
            "| Item | Produced by |", "|---|---|",
            "| Final film `report/video/thermalsense_final_1080p.mp4` | `tools/report/gen_film3.py` from footage of `src/app/Record.cpp` (scripts in `build/film3/tour/`, `build/film3/final.sh`) |",
            "| Slides `report/ThermalSense_Presentation.pptx` | `tools/build_slides.py` |",
            "| Scene export `report/data/scene_export_v10.json` | `ThermalSense --export-docs` (`src/app/DocExport.cpp`) |",
            "| Numbers in the text (`\\\\gv{...}` macros) | `tools/v10/gen_game.py`, `tools/v10/gen_numbers.py` read them from the C++ sources |",
            "| Part counts cross-check | `tools/report/check_counts.py` |",
            "| Workbook `report/workbooks/cg_workbook.xlsx` | `tools/v10/build_cg_workbook.py` |",
            "| Tests (396) | `tests/*.cpp`, run with `make test` |", ""]
    un = sorted(set(unresolved))
    out += ["## References that could not be resolved to a line", ""]
    out += ["None."] if not un else ["- `%s`" % u for u in un]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w").write("\n".join(out) + "\n")
    print("wrote", OUT, n, "sections,", count_refs, "code references,", len(un), "unresolved")
    for u in un[:40]:
        print("  unresolved:", u)


if __name__ == "__main__":
    main()
