"""Sheet 17 (Audit): requirement-by-requirement checklist.

Sources (the PDFs are read with pypdf at build time; the lab sheet is a hand-written CamScanner scan without a
text layer, so its transcription in docs/PLAN.md section 1 is used for the lab rows):
  - graphics-lab-04.pdf and docs/PLAN.md section 1            -> L rows
  - ThermalSense Project Proposal (1).pdf                     -> P rows (text extracted live and checked)
  - docs/PLAN_V2.md, docs/PLAN_V3.md, docs/V4_PROGRESS.md      -> U2 / U3 / U4 rows (the user's requests)
  - the course checklist note in docs/V4_PROGRESS.md           -> C rows
Authored here: the requirement wording, the evidence / test / figure mapping. Computed: evidence-file presence at
generation time, the test status (Excel formula over sheet 10) and the status itself.
"""
import os
import re

from openpyxl.formatting.rule import FormulaRule

from wb_common import (BOLD, FAIL_FILL, INPUT_FILL, NOTE_FILL, PASS_FILL, PENDING_FILL, S_AUDIT, S_TEST, ROOT, file_link,
                       finish, header, path, put, title, widths)

FIG = "report/figures_v4/"


def _expand(spec, known):
    """'T60-T69, T73' -> ['T60', ...] limited to IDs that exist in the test results."""
    out = []
    for part in re.split(r"[,\s]+", spec.strip()):
        if not part:
            continue
        m = re.fullmatch(r"([TVB])(\d+)-[TVB]?(\d+)", part)
        if m:
            for n in range(int(m.group(2)), int(m.group(3)) + 1):
                tid = f"{m.group(1)}{n:02d}" if n < 100 else f"{m.group(1)}{n}"
                if tid in known:
                    out.append(tid)
        elif part in known:
            out.append(part)
    return out


# (area, id, requirement, source, evidence files, tests, figure, manual status (when no test decides), note)
ROWS = [
    # ---- lab sheet (graphics-lab-04.pdf; transcription in docs/PLAN.md section 1) ----------------------------
    ("Lab sheet 04", "L01", "Unit primitives centred at the origin (cube, sphere, cylinder), canonical volume [-1, 1]",
     "Lab p1, p3", "src/primitives/Cube.cpp, Sphere.cpp, Cylinder.cpp", "T01-T02 T04-T05 T07-T08", "report/figures/primitive_library.png", None, ""),
    ("Lab sheet 04", "L02", "Counter-clockwise = front facing; back-face culling", "Lab p1", "src/primitives/*.cpp, src/rendering/Renderer.cpp",
     "T03 T09 T10 T36", "report/figures/scene_depth.png", None, ""),
    ("Lab sheet 04", "L03", "Technique 2: unique vertices + index buffer (12 f + 6 i for a square)", "Lab p1", "src/primitives/MeshData.cpp",
     "T11 T12", "report/figures/scene_visible.png", None, "Sheet 2 repeats the 18 f vs 12 f + 6 i computation by formula"),
    ("Lab sheet 04", "L04", "Interleaved VBO / EBO / VAO with position + normal, 6 floats per vertex", "Lab p2", "src/rendering/Mesh.cpp",
     "T03 T05 T09", "report/figures/scene_visible.png", None, ""),
    ("Lab sheet 04", "L05", "36 slices (10 deg) for sphere and cylinder; sphere upper half reflected; cylinder axis = z", "Lab p3",
     "src/primitives/Sphere.cpp, Cylinder.cpp", "T04 T06 T07 T08", "report/figures/primitive_library.png", None, ""),
    ("Lab sheet 04", "L06", "Model matrix M = T x R x S and world = parent x local", "Lab p3", "src/scene/Transform.h, src/math/Math3D.cpp",
     "T14 T15 T16 T21", "report/figures/hierarchy_axes.png", None, "Sheets 4 and 5 rebuild M and the world matrices by formula"),
    ("Lab sheet 04", "L07", "Table M1, M2, M3 with sx sy sz tx ty tz for every component", "Lab p3", "tools/wb_transforms.py", "", "", "Done",
     "Sheet 4; the subset limit is explained on the sheet"),
    ("Lab sheet 04", "L08", "Everything built only from the primitives; Excel + explanation", "Lab p3", "tools/build_workbook.py, report/main.tex",
     "T18 T38 T67 T70 T96", "report/figures/iso_bunker_exploded.png", None, ""),
    # ---- proposal ------------------------------------------------------------------------------------------
    ("Proposal", "P01", "Interactive 3D night simulation of an autonomous UAV, thermal-camera view", "Proposal 1",
     "src/scene/Scene.cpp, src/ai/DroneController.cpp", "T22 T28 T30 T45 T46", "report/figures/scene_thermal.png", None, ""),
    ("Proposal", "P02", "Command Bunker with Flat shading", "Proposal 2", "src/objects/Bunker.cpp", "T19 T67", "report/figures/bunker_flat_visible.png", None,
     "v4: the bunker is now underground; Flat shading kept"),
    ("Proposal", "P03", "Terrain with Gouraud shading", "Proposal 2", "src/objects/Terrain.cpp", "T13 T19 T42", "report/figures/scene_visible.png", None, ""),
    ("Proposal", "P04", "UAV with Phong shading, moving specular highlights", "Proposal 2", "src/objects/Uav.cpp", "T19", "report/figures/iso_uav_assembled.png", None, ""),
    ("Proposal", "P05", "Target rover moving on the terrain; hides behind objects", "Proposal 2-3", "src/objects/Rover.cpp, src/ai/HideController.cpp",
     "T28 T44", "report/figures/hide_thermal.png", None, ""),
    ("Proposal", "P06", "Three lights at once: directional moon, point beacon, spot UAV searchlight", "Proposal 2", "src/rendering/Lighting.h", "T35",
     "report/figures/scene_visible.png", None, "Sheet 7"),
    ("Proposal", "P07", "Translation, rotation, scaling; UAV flies; rover drives", "Proposal 3", "src/scene/Scene.cpp, src/scene/Transform.h",
     "T14 T20 T30 T49", "report/figures/cine_chase.png", None, ""),
    ("Proposal", "P08", "Hierarchical searchlight casts a cone over the landscape", "Proposal 3", "src/objects/Uav.cpp", "T21", "report/figures/hierarchy_axes.png", None, ""),
    ("Proposal", "P09", "Z-buffer hidden-surface removal (rover behind the bunker)", "Proposal 3", "src/rendering/Renderer.cpp", "T26", "report/figures/hide_ztest_off.png", None, ""),
    ("Proposal", "P10", "Wireframe mode: hidden-line vs overlapping (x-ray) edges", "Proposal 3", "src/rendering/Renderer.cpp", "", "report/figures/scene_xray.png", "Done", "Visual checks V07, V08"),
    ("Proposal", "P11", "Camera controls and diagnostic toggles", "Proposal 3", "src/scene/Camera.cpp, src/app/Interactive.cpp", "T17 T50 T55",
     "report/figures/cine_establishing.png", None, ""),
    # ---- user requests v2 / v3 --------------------------------------------------------------------------------
    ("User v2", "U2-1", "Realistic night look: PBR, shadows, bloom, SSAO, volumetric fog", "docs/PLAN_V2.md phases A-B", "src/rendering/Renderer*.cpp, ShaderSources.cpp",
     "T33 T34 T35 T39", "report/figures/compare_v2.png", None, "Ablation figures V17 / sheet 9"),
    ("User v2", "U2-2", "Richer compound: fence, watchtower, sandbags, trees, grass, backdrop", "docs/PLAN_V2.md phase C", "src/objects/Environment.cpp, Vegetation.cpp, Backdrop.cpp",
     "T38 T40", "report/figures/cine_establishing.png", None, ""),
    ("User v2", "U2-3", "Thermal footprints, rain, rotor blur, director camera", "docs/PLAN_V2.md phase D", "src/scene/HeatTrail.cpp, src/rendering/Rain.cpp, src/scene/Director.cpp",
     "T47 T48 T49", "report/figures/thermal_footprints.png", None, ""),
    ("User v2", "U2-4", "Excel workbook with formulas, charts, images; matrices match the code", "docs/PLAN_V2.md section 2", "tools/build_workbook.py, tools/verify_workbook.py", "",
     "", "Done", "Cover KPI 'max |Excel - code|'"),
    ("User v3", "U3-1", "RTX 4070 target: EPIC preset, headless EGL, cross-platform build", "docs/PLAN_V3.md section 1", "Makefile, CMakeLists.txt, src/app/HeadlessGL.cpp", "",
     "", "Done", "Frame rate on the remote RTX 4070 is not measured here (sheet 11 pending)"),
    ("User v3", "U3-2", "First-person full-screen thermal view with FLIR HUD and palettes", "docs/PLAN_V3.md section 3", "src/rendering/Hud.cpp, ShadersPost*.cpp", "",
     "report/figures_v3/thermal_fpv.png", "Done", ""),
    ("User v3", "U3-3", "TAA, auto-exposure, SSR, lens flare, DOF, motion blur, filmic curve", "docs/PLAN_V3.md section 2", "src/rendering/ShadersPost.cpp", "",
     "report/figures_v3/cine_establishing.png", "Done", ""),
    ("User v3", "U3-4", "First-person walk camera", "docs/PLAN_V3.md W5", "src/scene/Camera.cpp", "T50-T55", "report/figures_v3/walk_forest.png", None, ""),
    # ---- user request v4 -----------------------------------------------------------------------------------
    ("User v4", "U4-1", "Military base look (night): HESCO wall, guard towers, helipad, motor pool, barracks, flag", "docs/V4_PROGRESS.md",
     "src/objects/Base.cpp, Environment.cpp, Structures.cpp, BaseLayout.h", "T70-T79", FIG + "base_overview.png", None, "Sheet 15"),
    ("User v4", "U4-2", "Command bunker underground and walkable: ramp, blast doors, tunnel, rooms", "docs/V4_PROGRESS.md",
     "src/objects/Bunker.cpp, src/scene/WalkSpace.cpp, src/objects/Terrain.cpp", "T60-T69 T120-T123 T126", FIG + "bunker_cic.png", None, "Sheet 15 (walk space)"),
    ("User v4", "U4-3", "UAV shoots the intruder rovers (4 rail rounds, lock time, reload)", "docs/V4_PROGRESS.md",
     "src/scene/Scene.cpp, src/scene/Combat.cpp, src/ai/DroneController.cpp", "T100 T108 T112", FIG + "uav_missile_launch.png", None, "Sheet 16"),
    ("User v4", "U4-4", "Rovers blow up properly: fireball, smoke, sparks, debris, scorch, burning wreck, respawn", "docs/V4_PROGRESS.md",
     "src/scene/Combat.cpp, src/rendering/Effects.cpp, src/scene/Scene.cpp", "T103-T107", FIG + "explosion_mid.png", None, "Sheet 16 particle census"),
    ("User v4", "U4-5", "At least 5 new user-controllable objects with complex hierarchical motion", "docs/V4_PROGRESS.md",
     "src/objects/Tank.cpp, Helicopter.cpp, AaGun.cpp, Radar.cpp, Base.cpp, Bunker.cpp", "T64 T65 T73 T80-T99", FIG + "tank_closeup.png", None,
     "Tank, helicopter, AA gun, radar, blast doors, gate barrier (6) + UAV; sheet 14"),
    ("User v4", "U4-6", "Possession keys 5-9 / 0, [ ] cycle, chase and sight cameras, HUD sights", "docs/V4_PROGRESS.md",
     "src/scene/Control.cpp, src/app/Interactive.cpp, src/app/AppHud.cpp", "T127 T128", FIG + "possess_tank_sight.png", None, ""),
    ("User v4", "U4-7", "Friend-or-foe, wreck discrimination and ramp keep-out for the UAV", "docs/V4_PROGRESS.md",
     "src/ai/DroneController.cpp, src/scene/Scene.cpp", "T109-T111", "", None, "Sheet 16 section E"),
    ("User v4", "U4-8", "Camera shake, walk camera inside the bunker, 120 s director sequence", "docs/V4_PROGRESS.md",
     "src/scene/Camera.cpp, src/scene/Director.cpp", "T120-T126 T129", FIG + "bunker_ramp.png", None, ""),
    ("User v4", "U4-9", "Realistic scale, deterministic simulation, geometry only from primitives", "docs/v4_notes/v4-brief.md",
     "src/objects/*.cpp", "T66 T67 T70 T76 T96 T113", "", None, ""),
    ("User v4", "U4-10", "Excel workbook regenerated from the program's output and extended", "docs/V4_PROGRESS.md",
     "tools/build_workbook.py, tools/wb_v4.py, tools/wb_audit.py, src/app/DocExport.cpp", "", "", "Done", "This workbook (18 sheets); verify_workbook.py"),
    ("User v4", "U4-11", "Report rewritten with a full audit appendix", "docs/V4_PROGRESS.md", "report/main.tex", "", "", "Pending", "Owned by another engineer"),
    ("User v4", "U4-12", "Windows executable rebuilt", "docs/V4_PROGRESS.md", "tools/cross_build_windows.sh", "", "", "Pending", "Supervisor to-do item 6"),
    # ---- course checklist ----------------------------------------------------------------------------------
    ("Course checklist", "C1", "10-slide presentation", "Course note (V4_PROGRESS.md)", "report/*.pptx", "", "", "Pending", "Slides owned by another engineer"),
    ("Course checklist", "C2", "Outline on slide 2", "Course note", "report/*.pptx", "", "", "Pending", "Checked when the deck exists"),
    ("Course checklist", "C3", "2-minute video at the end of the slides", "Course note", "report/*.mp4", "T125 T129", "", None,
     "Director sequence is 120 s / 12 shots (T125); the recorded file report/*.mp4 is pending"),
    ("Course checklist", "C4", "Dynamic objects (scene animation, intruders, effects)", "Course note", "src/scene/Scene.cpp, src/scene/Combat.cpp", "T20 T49 T107 T113", "", None, ""),
    ("Course checklist", "C5", "Controllable objects", "Course note", "src/scene/Control.cpp", "T127 T128", FIG + "possess_tank_sight.png", None, "Sheet 14"),
    ("Course checklist", "C6", "Complex motion (hierarchical rigs, skid steer, rotor head, tracks)", "Course note", "src/objects/Tank.cpp, Helicopter.cpp, Radar.cpp",
     "T80-T84 T90-T91 T97-T98", FIG + "tank_closeup.png", None, "Sheet 5 joint chains"),
    ("Course checklist", "C7", "Proper report", "Course note", "report/main.pdf", "", "", "Pending", "v3 report exists; v4 rewrite pending"),
    ("Course checklist", "C8", "Proper slides", "Course note", "report/*.pptx", "", "", "Pending", ""),
]


def proposal_check():
    """Reads the proposal PDF with pypdf and confirms each key phrase of the P rows is in its text."""
    try:
        from pypdf import PdfReader
    except ImportError:
        return None
    pdf = path("ThermalSense Project Proposal (1).pdf")
    if not os.path.exists(pdf):
        return None
    text = " ".join((p.extract_text() or "") for p in PdfReader(pdf).pages).lower()
    return text


PHRASES = {"P02": "flat shading", "P03": "gouraud", "P04": "phong", "P05": "rover", "P06": "spotlight", "P08": "hierarchical",
           "P09": "z-buffer", "P10": "wireframe", "P11": "camera controls", "P01": "thermal"}


def build_audit(wb, ctx):
    ws = wb[S_AUDIT]
    known = {t["id"] for t in ctx["tests"]}
    t_first, t_last = ctx["test_rows"]
    ids, st = f"'{S_TEST}'!$A${t_first}:$A${t_last}", f"'{S_TEST}'!$F${t_first}:$F${t_last}"
    text = proposal_check()
    title(ws, "17  Audit: requirement-by-requirement checklist",
          "Lab sheet (graphics-lab-04.pdf), proposal, the user's v2 / v3 / v4 requests and the course checklist. "
          "Status = Verified (all linked tests pass: formula over sheet 10), Done (deliverable exists, no test), "
          "Pending (file or capture not there yet) or CHECK. 'Evidence present' is checked against the file system when "
          "the workbook is generated. The proposal phrase check reads the proposal PDF text (pypdf).", span=11)
    h, first = 5, 6
    header(ws, h, ["ID", "Area", "Requirement", "Source", "Evidence file(s)", "Evidence present", "Test IDs",
                   "Tests passing (formula)", "Figure", "Status", "Note"], height=34)
    r = first
    for area, rid, req, src, files, tests, fig, manual, note in ROWS:
        tl = _expand(tests, known) if tests else []
        flist, prev_dir = [], ""
        for f in (x.strip() for x in files.split(",")):
            if not f:
                continue
            if "/" not in f and prev_dir:
                f = prev_dir + f
            prev_dir = f.rsplit("/", 1)[0] + "/" if "/" in f else prev_dir
            flist.append(f)
        present = _present(flist)
        put(ws, r, 1, rid, font=BOLD)
        put(ws, r, 2, area)
        put(ws, r, 3, req)
        put(ws, r, 4, src)
        c = put(ws, r, 5, "\n".join(flist))
        first_real = next((f for f in flist if "*" not in f and os.path.exists(path(f))), None)
        if first_real:
            file_link(c, first_real, "\n".join(flist))
        put(ws, r, 6, present[0], fill=PASS_FILL if present[1] == "all" else (PENDING_FILL if present[1] == "none" else None))
        put(ws, r, 7, " ".join(tl) or "-")
        if tl:
            passed = "+".join(f'SUMPRODUCT(({ids}="{t}")*({st}="PASS"))' for t in tl)
            put(ws, r, 8, f'=({passed})&" / {len(tl)}"')
            put(ws, r, 10, f'=IF({passed}={len(tl)},"Verified","CHECK")', font=BOLD)
        else:
            put(ws, r, 8, "-")
            status = manual or "Done"
            if status == "Done" and present[1] == "none":
                status = "Pending"
            put(ws, r, 10, status, font=BOLD)
        figtext = fig if (not fig or os.path.exists(path(fig))) else f"pending: {fig}"
        c = put(ws, r, 9, figtext or "-", fill=PENDING_FILL if fig and not os.path.exists(path(fig)) else None)
        if fig and os.path.exists(path(fig)):
            file_link(c, fig)
        n = note
        if rid in PHRASES and text is not None:
            n = (n + "; " if n else "") + ("proposal text confirms '%s'" % PHRASES[rid] if PHRASES[rid] in text
                                           else "proposal text does NOT contain '%s'" % PHRASES[rid])
        put(ws, r, 11, n, fill=NOTE_FILL if n else None)
        r += 1
    last = r - 1
    rng = f"J{first}:J{last}"
    for word, fill in (("Verified", PASS_FILL), ("Done", PASS_FILL), ("Pending", PENDING_FILL), ("CHECK", FAIL_FILL)):
        ws.conditional_formatting.add(rng, FormulaRule(formula=[f'J{first}="{word}"'], fill=fill))
    labels = [("Items", f"=COUNTA(A{first}:A{last})"), ("Verified by tests", f'=COUNTIF({rng},"Verified")'),
              ("Done", f'=COUNTIF({rng},"Done")'), ("Pending", f'=COUNTIF({rng},"Pending")'),
              ("Failed (CHECK)", f'=COUNTIF({rng},"CHECK")')]
    for k, (lab, f) in enumerate(labels):
        put(ws, 3, 1 + 2 * k, lab, font=BOLD, fill=NOTE_FILL if False else None)
        put(ws, 3, 2 + 2 * k, f, "0", font=BOLD)
    ctx["refs"].update(audit_items=f"'{S_AUDIT}'!$B$3", audit_verified=f"'{S_AUDIT}'!$D$3",
                       audit_done=f"'{S_AUDIT}'!$F$3", audit_pending=f"'{S_AUDIT}'!$H$3", audit_check=f"'{S_AUDIT}'!$J$3")
    widths(ws, [7, 14, 52, 22, 40, 11, 22, 12, 34, 11, 44])
    finish(ws, h)


def _present(files):
    ok = []
    for f in files:
        if "*" in f:
            import glob
            ok.append(bool(glob.glob(os.path.join(ROOT, f))))
        else:
            ok.append(os.path.exists(path(f)))
    if not ok:
        return "-", "all"
    if all(ok):
        return "yes", "all"
    if any(ok):
        return "partly (%d / %d)" % (sum(ok), len(ok)), "some"
    return "no", "none"
