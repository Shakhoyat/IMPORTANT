"""README.md, written from the project's data so that its numbers are the workbook's and the report's.

Sources: report/data/scene_export_v10.json (parts, primitives, designs, things that can be operated), report/data/bench.csv,
report/data/film.json and build/film5/chapters.txt (the film), the source files (constants), report/main.pdf (page count).
Pictures: docs/img (tools/film/figures.py) and report/figures/img.
Run: ~/.local/bin/uv run -q --with pymupdf python tools/report/gen_readme.py [--youtube URL]"""
import csv
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
E = json.load(open(os.path.join(ROOT, "report", "data", "scene_export_v10.json")))
YT_FILE = os.path.join(ROOT, "docs", "youtube_link.txt")


def const(path, name):
    m = re.search(r"\b%s\s*=\s*(-?[\d.]+)" % name, open(os.path.join(ROOT, path), errors="replace").read())
    v = float(m.group(1))
    return int(v) if v == int(v) else v


def pages():
    try:
        import pymupdf
        return len(pymupdf.open(os.path.join(ROOT, "report", "main.pdf")))
    except Exception:
        return None


def clock(t):
    return "%d:%02d" % (int(t) // 60, int(t) % 60)


def img(path, alt, width=None):
    return '<img src="%s" alt="%s"%s>' % (path, alt, ' width="%s"' % width if width else "")


def row(*cells):
    return "| " + " | ".join(str(c) for c in cells) + " |"


def pair(a, b, ca, cb):
    return "\n".join(["<table><tr>", '<td width="50%%">%s<br><sub>%s</sub></td>' % (img(a, ca), ca), '<td width="50%%">%s<br><sub>%s</sub></td>' % (img(b, cb), cb),
                      "</tr></table>", ""])


def main():
    if "--youtube" in sys.argv:
        open(YT_FILE, "w").write(sys.argv[sys.argv.index("--youtube") + 1].strip() + "\n")
    yt = open(YT_FILE).read().strip() if os.path.exists(YT_FILE) else ""
    film = json.load(open(os.path.join(ROOT, "report", "data", "film.json")))
    chapters, seen = [], set()
    for b in film["beats"]:
        if b["chapter"] not in seen:
            seen.add(b["chapter"])
            chapters.append((b["start"], b["act"], b["chapter"]))
    n_pages = pages()
    operable = [c for c in E["controllables"] if c["possessable"]]
    tests = json.load(open(os.path.join(ROOT, "build", "test_results.json")))
    n_tests = tests.get("total", tests.get("tests")) if isinstance(tests, dict) else len(tests)
    if not isinstance(n_tests, int):
        n_tests = len(tests.get("results", [])) if isinstance(tests, dict) else 0
    bench = list(csv.DictReader(open(os.path.join(ROOT, "report", "data", "bench.csv"))))
    fam = {}
    for d in E["designs"]:
        fam.setdefault(d["family"], []).append(d)
    L = []
    A = L.append
    A("# ThermalSense")
    A("")
    A("**From one triangle to an autonomous outpost.** Final project of the Computer Graphics Laboratory (CSE 4102), Department of Computer")
    A("Science and Engineering, KUET. Md. Shakhoyat Rahman Shujon, Roll 2107104.")
    A("")
    A("A playable military outpost in C++17 and OpenGL 3.3 (4.3 compute for the ray tracers). The camp, the vehicles and the soldiers are")
    A("built from %d shape generators written for the project: %s parts in %d designs, no model files (the trees at the two highest" % (
        len(E["primitives"]), format(E["leaf_count"], ","), len(E["designs"])))
    A("presets are public-domain meshes). There is no mathematics library, no shader calls a built-in vector function, and the sound is")
    A("synthesised. On top of the graphics pipeline run a thermal camera model, an image-processing pipeline, a multi-target tracker, two")
    A("drones that decide together how to strike, intruders that plan routes and hide, and soldiers with duties.")
    A("")
    link = yt or "docs/FILM_SCRIPT.md"
    A('<a href="%s">%s</a>' % (link, img("docs/img/film_three_views.jpg", "The film: one moment in three views")))
    A("")
    A("## Start here")
    A("")
    A(row("To see", "Open"))
    A(row("---", "---"))
    A(row("**The film** (%s, narrated, with chapters)" % clock(film["length"]),
          ("[YouTube, full HD](%s)" % yt if yt else "YouTube link: *to be added after the upload*") +
          "; copies in this repository by part: " + ", ".join("[part %d](report/video/thermalsense_full_part%d.mp4)" % (k, k) for k in (1, 2, 3, 4))))
    A(row("The film's narration and shot list", "[`docs/FILM_SCRIPT.md`](docs/FILM_SCRIPT.md); subtitles [`docs/FILM_SUBTITLES.srt`](docs/FILM_SUBTITLES.srt); "
          "chapter list [`docs/FILM_CHAPTERS.txt`](docs/FILM_CHAPTERS.txt)"))
    A(row("**The report**%s" % (" (%d pages)" % n_pages if n_pages else ""), "[`report/main.pdf`](report/main.pdf)"))
    A(row("**The workbook** (%d sheets; the register of every number and coordinate)" % 23, "[`report/workbooks/cg_workbook.xlsx`](report/workbooks/cg_workbook.xlsx)"))
    A(row("A tour of the code, line by line, in pipeline order", "[`docs/CODE_TOUR.md`](docs/CODE_TOUR.md)"))
    A(row("Every report section mapped to its code", "[`docs/REPORT_CODE_MAP.md`](docs/REPORT_CODE_MAP.md)"))
    A(row("The slides", "[10 slides](report/ThermalSense_Presentation_10.pptx), [25 slides](report/ThermalSense_Presentation.pptx)"))
    A(row("The short film (3 min 38 s)", "[1080p](report/video/thermalsense_final_1080p_web.mp4), [720p](report/video/thermalsense_final_720p.mp4)"))
    A("")
    A("## The film, chapter by chapter")
    A("")
    A("Every chapter first draws the idea one step at a time, then shows the same thing in the running program, and ends with what was")
    A("compared and what was chosen.")
    A("")
    A(row("Time", "Part", "Chapter"))
    A(row("---", "---", "---"))
    for t, act, ch in chapters:
        A(row(clock(t), act or "", ch))
    A("")
    A(pair("docs/img/film_open_world.jpg", "docs/img/film_frame_explorer.jpg", "An open world: the camera is free", "Frame Explorer: one frame replayed in 16 stages"))
    A(pair("docs/img/film_click_soldier.jpg", "docs/img/film_first_person.jpg", "Click a walking soldier ...", "... and see through his eyes"))
    A(pair("docs/img/film_drone_hud.jpg", "docs/img/film_ride_round.jpg", "A drone flown by hand, with its flight display", "Riding a round to its target"))
    A("")
    A("## 1. Geometry: %d generators, %s parts" % (len(E["primitives"]), format(E["leaf_count"], ",")))
    A("")
    A(pair("docs/img/plate_primgrid.jpg", "docs/img/film_primitive_key.jpg", "The sixteen primitives", "Key F: every part coloured by the generator that made it"))
    A(row("#", "Primitive", "Corners V", "Triangles T", "V", "T", "Parts in the scene"))
    A(row("---", "---", "---:", "---:", "---", "---", "---:"))
    for p in E["primitives"]:
        A(row(p["number"], p["name"], format(p["vertices"], ","), format(p["triangles"], ","), "`%s`" % p["formula_V"].replace("V = ", ""),
              "`%s`" % p["formula_T"].replace("T = ", "").split(" (D")[0], format(p["instances"], ",") if p["instances"] else "instanced blades"))
    A("")
    A("An object is a list of parts; a part is one generator call and one matrix `M = T R S`. The %d designs:" % len(E["designs"]))
    A("")
    A(row("Family", "Designs", "Parts"))
    A(row("---", "---", "---:"))
    for f, ds in fam.items():
        A(row(f, ", ".join("%s%s" % (d["name"].lower(), " x%d" % d["instances"] if d["instances"] > 1 else "") for d in ds), format(sum(d["parts_total"] for d in ds), ",")))
    A("")
    A(pair("docs/img/plate_trs.jpg", "docs/img/film_axes.jpg", "One matrix places a part; the order matters", "Key H: the local axes of the nodes of the scene graph"))
    A("")
    A("## 2. The pipeline: what was compared, and what was chosen")
    A("")
    A(row("Stage", "Idea", "Compared", "Chosen", "Keys in the program"))
    A(row("---", "---", "---", "---", "---"))
    for r in [("Viewing", "look-at basis, perspective divide", "perspective / orthographic / isometric", "perspective to play, the parallel views to teach", "`Ctrl+P`"),
              ("Clipping, visibility", "Cohen-Sutherland, Liang-Barsky, depth buffer", "painter's sorting / depth buffer", "24-bit depth, near 0.05 m, far 600 m", "`Z`, `F`, `X`"),
              ("Rasterisation", "edge functions, barycentric weights", "none / multisampling / temporal", "4x multisampling + temporal anti-aliasing", "`` ` `` (Frame Explorer), `F6`"),
              ("Illumination", "ambient + diffuse + specular, written out (`ts*` functions)", "one term at a time", "all lights summed per pixel", "`Ctrl+L`, `1`-`4`"),
              ("Shading", "where the light is computed", "flat / Gouraud / Phong; Blinn-Phong / microfacet", "per-pixel shading, microfacet highlights", "`O`, `B`"),
              ("Shadows", "shadow maps, soft edge by blocker distance", "hard / filtered / contact-hardening", "cascades with soft edges, contact shadows", "-"),
              ("Tone", "exposure, tone curve, gamma", "clipped / tone-mapped", "eye adaptation + tone curve", "`=`, `-`, `N`"),
              ("Reflection, ray tracing", "reflect, refract, tree of boxes", "screen-space / hybrid / Whitted / path", "raster + screen-space to play, ray tracing on a key", "`;`, `'`, `F7`")]:
        A(row(*r))
    A("")
    A(pair("docs/img/plate_project.jpg", "docs/img/plate_raster.jpg", "One real vertex from the generator to its pixel (workbook: Transform_Trace)", "Edge functions fill a triangle"))
    A(pair("docs/img/plate_phong.jpg", "docs/img/film_shading.jpg", "Ambient + diffuse + specular", "Key O: flat, Gouraud, Phong on the helicopter"))
    A(pair("docs/img/film_ray_tracing.jpg", "docs/img/film_ray_tree.jpg", "Key ; switches between five renderers", "The ray debugger: the tree of rays behind one pixel"))
    A("")
    A("Frame time per preset (1920 x 1080, RTX 4070; `report/data/bench.csv`):")
    A("")
    A(row("Configuration", "ms per frame", "frames per second", "triangles"))
    A(row("---", "---:", "---:", "---:"))
    for b in bench:
        A(row(b["config"], "%.1f" % float(b["avg_ms"]), "%.0f" % float(b["avg_fps"]), format(int(float(b["triangles"])), ",")))
    A("")
    A("## 3. A world you can operate")
    A("")
    A("%d things can be operated: select one (click, or `Tab`), take control (`Enter`, or `E` beside it), and its own keys work. `V` changes" % len(operable))
    A("the camera, `Esc` gives it back. Any moving object can also be entered as a point of view: click it, pick it from the list (`F4`), or")
    A("step with `Insert` / `Delete`.")
    A("")
    A(row("Thing", "Where (x, y, z in m)", "Controls"))
    A(row("---", "---", "---"))
    shown = set()
    for c in operable:
        name = "Soldiers (8)" if c["name"].startswith("SOLDIER") else c["name"].title()
        if name in shown:
            continue
        shown.add(name)
        A(row(name, "(%.1f, %.1f, %.1f)" % tuple(c["position"]), re.sub(r"\s+", " ", c.get("controls", ""))[:110]))
    A("")
    A(pair("docs/img/film_tank_sight.jpg", "docs/img/film_helicopter.jpg", "The tank from the gunner's sight", "The helicopter, started and flown"))
    A(pair("docs/img/film_pov_list.jpg", "docs/img/film_day_cycle.jpg", "F4: every moving object, nearest first", "One clock drives light, sky, lamps and duties"))
    A("")
    A("## 4. Vision: from a heat picture to a track")
    A("")
    A(pair("docs/img/plate_cvsteps.jpg", "docs/img/plate_classify.jpg", "Five steps on every %d x %d frame" % (const("src/ai/DroneController.h", "kCvWidth"), const("src/ai/DroneController.h", "kCvHeight")),
           "Target, person or clutter"))
    A(pair("docs/img/plate_kalman.jpg", "docs/img/plate_tracklog.jpg", "A track: predict, then correct", "Uncertainty of the squad's target during one night (recorded from the program)"))
    A("")
    A("## 5. Agents: the drones, the intruders, the soldiers")
    A("")
    A(row("Agent", "Algorithm", "Constants (from the source)", "Code"))
    A(row("---", "---", "---", "---"))
    S, H = "src/ai/Squad.h", "src/ai/HideController.h"
    A(row("Drone pair", "lead and wing; combined strike with both rounds arriving together; split for a second, urgent intruder",
          "lock %s s (lead), %s s (wing); waits %s s for a free wing; split beyond %s m when within %s m of an asset; rejoin within %s m" % (
              const(S, "kSquadLeadLock"), const(S, "kSquadWingLock"), const(S, "kSquadWingWaitFree"), const(S, "kSquadSplitDist"),
              const(S, "kSquadSplitThreat"), const(S, "kSquadRejoinDist")), "`src/ai/Squad.cpp`"))
    A(row("Intruder: route", "flood fill (MicroMouse) on a cost map: 1 + exposure to the drones + its own noise field",
          "%d x %d cells of %s m" % (const(H, "kGridCells"), const(H, "kGridCells"), const(H, "kCellSize")), "`src/ai/HideController.cpp`"))
    A(row("Intruder: driving", "line follower: %d sensors, PD controller, differential drive" % const(H, "kIrSensorCount"),
          "Kp %s, Kd %s" % (const("src/ai/HideController.cpp", "kKp"), const("src/ai/HideController.cpp", "kKd")), "`src/ai/HideController.cpp`"))
    A(row("Intruder: hiding", "a drone within range: flood fill from every cell no drone can see, drive to the nearest",
          "%s m, %s s in cover" % (const(H, "kEvadeRadius"), const(H, "kEvadeTime")), "`src/ai/HideController.cpp`, `src/ai/Occlusion.cpp`"))
    A(row("Soldier", "A* on a grid, string pulling, Catmull-Rom curve; two-link leg inverse kinematics", "day shift %s to %s h" % (
        const("src/scene/Garrison.h", "kDayShiftStart"), const("src/scene/Garrison.h", "kDayShiftEnd")), "`src/scene/Garrison.cpp`, `src/scene/Gait.cpp`"))
    A(row("Tracker", "Kalman filter per target, gating, Hungarian assignment, two-view triangulation", "state x, z, vx, vz", "`src/vision/MultiTracker.cpp`"))
    A("")
    A(pair("docs/img/plate_decide2.jpg", "docs/img/film_combined_strike.jpg", "Two intruders: split, or stay together?", "A combined strike"))
    A(pair("docs/img/plate_rcost.jpg", "docs/img/plate_rflood.jpg", "The intruder's map: red is seen by a drone, green is cover (recorded from the program)", "Flood fill from the goal; the route follows the smaller numbers"))
    A(pair("docs/img/plate_revade.jpg", "docs/img/film_flood_field.jpg", "It hears a drone and hides", "Key M: the flood field and the routes in the scene"))
    A(pair("docs/img/plate_lfr.jpg", "docs/img/plate_astar.jpg", "Following the route like a line-follower robot", "How a soldier gets to his post"))
    A("")
    A("## The workbook: one home for every number")
    A("")
    A("`report/workbooks/cg_workbook.xlsx` has 23 sheets. Yellow cells are inputs read from the code or from the program's scene export; white")
    A("cells are formulas. It holds the worked examples of every pipeline stage, every number printed in the report (`Report_Numbers`), every")
    A("component of the scene in world coordinates (`World_Coordinates`, %s rows), the layout, lights and things that can be operated" % format(len(E["components"]), ","))
    A("(`Layout`), every constant of the algorithms with its source line (`Algorithms`) and the film's shot list (`Film`). The report's number")
    A("macros are written from the workbook, and the checkers fail if the program, the workbook and the report disagree:")
    A("")
    A("```")
    A("~/.local/bin/uv run -q --with openpyxl --with numpy python tools/report/check_workbook.py      # workbook = export = code = report")
    A("~/.local/bin/uv run -q --with openpyxl python tools/report/numbers_from_workbook.py --check   # report macros = workbook")
    A("python3 tools/report/check_counts.py && python3 tools/report/check_numbers.py                 # counts and hand-typed numbers")
    A("```")
    A("")
    A("## Build and run")
    A("")
    A(row("Platform", "Command"))
    A(row("---", "---"))
    A(row("Windows", "`run.bat` (starts `ThermalSense.exe`; builds it first if missing)"))
    A(row("Linux", "`./build.sh` then `./run.sh`"))
    A(row("Linux without a display", "`make -j HEADLESS=1`, then `./ThermalSense --headless ...`"))
    A(row("WSL2 window, no sudo", "`tools/run_wsl.sh`"))
    A(row("Tests (%d)" % n_tests, "`make test`"))
    A("")
    A("Useful options: `--quality low|high|ultra|epic`, `--fullscreen`, `--time 22:00`, `--seed N`, `--record out.mp4 SECONDS`,")
    A("`--capture DIR`, `--export-docs FILE`, `--help`. The full key table (more than 120 bindings) is in `src/scene/KeyMap.cpp`, in Section 5")
    A("of the report, and on screen with `F1`.")
    A("")
    A("## Repository")
    A("")
    A(row("Path", "Holds"))
    A(row("---", "---"))
    for a, b in [("src/primitives", "the %d mesh generators" % len(E["primitives"])), ("src/objects", "every object as a list of parts (size, rotation, position)"),
                 ("src/math", "vectors, matrices, frustum, splines, tone curves, CPU rasteriser"), ("src/scene", "scene graph, soldiers, combat and particles, day cycle, doors, cameras"),
                 ("src/rendering", "shaders (vector mathematics written out in `ShaderMath.cpp`), renderer, post-processing, Frame Explorer"),
                 ("src/raytrace", "ray tracers, intersections, bounding volume hierarchy"), ("src/vision", "thermal sensor model, image pipeline, multi-target tracker"),
                 ("src/ai", "drone squad and flight, intruders (flood fill, line follower, hiding)"), ("src/app", "play, capture, record, export, replay"),
                 ("src/audio", "synthesised sound"), ("tests", "%d automated tests" % n_tests),
                 ("tools/film", "the film as data (`acts/*.py`), its plates, score, sound mix and assembler"),
                 ("tools/report, tools/v10", "generators of the report's tables and figures, the workbook, the checkers, this README"),
                 ("report", "LaTeX sources, figures, data, PDF, workbook, slides, films"), ("docs", "film script, code tour, code map, pictures of this page, history"),
                 ("assets", "public-domain (CC0) vegetation meshes and ground textures of the two highest presets (`assets/LICENSES.md`)")]:
        A(row("`%s`" % a, b))
    A("")
    A("## Regenerating")
    A("")
    A("```")
    A("# report and workbook")
    A("./ThermalSense --export-docs build/routes_export.json && cp build/routes_export.json report/data/scene_export_v10.json")
    A("python3 tools/v10/gen_numbers.py report/data/scene_export_v10.json")
    A("python3 tools/v10/build_cg_workbook.py --export report/data/scene_export_v10.json    # with uv: --with openpyxl --with matplotlib --with numpy")
    A("python3 tools/report/gen_designs.py && python3 tools/report/abbrev_pages.py          # Section 7; short forms per page; builds the PDF")
    A("# film (narration -> takes on the GPU -> plates -> picture -> sound -> master)")
    A("U=\"uv run -q --python 3.12 --with kokoro-onnx --with soundfile --with numpy --with scipy --with pillow --with matplotlib --with openpyxl python tools/film/build.py\"")
    A("$U voice && $U takes && $U events && $U plates && $U plan && $U picture && $U sound && $U final")
    A("python3 tools/film/figures.py && python3 tools/report/gen_readme.py                  # pictures and text of this page")
    A("```")
    A("")
    A("`tools/film/build.py takes` records the 26 takes made for this film. Thirteen more recordings are reused from the short film and are")
    A("not re-recorded by it: six soldier tours (tank, bunker, generator room, barracks hall, anti-aircraft gun, missile launcher; their")
    A("scripts are in `tools/film/tours`, played with `TS_TOUR`), three views of the camp on alert, two views of the night, and the drones'")
    A("take-off and landing (camera glides with `TS_FILM_CAM`). A rebuild from a clean clone needs those files in `build/film3` and `build/film2`.")
    A("")
    A("The narration is a synthetic voice (Kokoro, run locally); `tools/film/build.py sound --voice-track FILE` uses a recorded voice instead.")
    A("The score is written in code (`tools/film/score.py`). Every sound of the program is synthesised (`src/audio`).")
    A("")
    open(os.path.join(ROOT, "README.md"), "w").write("\n".join(L))
    print("wrote README.md: %d lines, %d pictures" % (len(L), sum(ln.count("<img") for ln in L)))


if __name__ == "__main__":
    main()
