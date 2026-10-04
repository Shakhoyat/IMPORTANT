"""v4 sheets: 14 Controllable Objects, 15 Bunker & Base, 16 Combat & Effects.

Everything numeric comes from build/scene_export.json (schema 3: controllables, walkspace, layout, combat, effects,
practical_lights, simulation, roots, objects). Cells with a pale-yellow fill (NOTE_FILL) are hand-maintained text
"from design notes" (docs/v4_notes/*.md and the Combat.h / Controllable.h comments), never numbers measured by the
program. Blue (INPUT_FILL) = imported from the export, white = Excel formula.
"""
import os
import re

from openpyxl.formatting.rule import CellIsRule
from openpyxl.styles import Alignment

from wb_common import (BOLD, FAIL_FILL, FMT_3, FMT_INT, INPUT_FILL, NOTE_FILL, PASS_FILL, S_BASE, S_COMBAT, S_CTRL,
                       S_OBJ, TOTAL_FILL, articulation_chains, col, define_name, finish, header, image, path, prim_fill,
                       put, section, title, widths)

PRIM_ORDER = ["Cube", "Sphere", "Cylinder", "Cylinder (frustum)", "Cone", "Terrain disc"]

# ---- hand-maintained (design notes) ----------------------------------------------------------------------
# key = substring of Controllable::name().  Source: docs/v4_notes/v4-b1/b2/a1/a2/d.md and the header comments.
DESIGN_NOTES = {
    "UAV": {
        "dof": "Body: x, y, z + yaw (autonomous or manual); attitude pitch / roll follow acceleration. Gimbal: 2 joints "
               "(yaw, pitch). 4 rotors spin. 4 rail rounds hidden at launch.",
        "technique": "Hierarchical forward kinematics UAV -> gimbal yaw -> gimbal pitch -> lamp + thermal camera; "
                     "autonomous search / track / reacquire state machine (DroneController) driven by the CV detection; "
                     "manual mode keeps only the vision bookkeeping.",
        "limits": "Keep inside the 19.5 m patrol disc, avoid obstacles; lock >= 1.5 s before a launch, 4 s between rounds.",
    },
    "TANK": {
        "dof": "Body 3 (x, z, yaw) + suspension heave / pitch springs; turret yaw 1; gun pitch 1; barrel recoil 1; "
               "2 track belts (40 links each) + 14 road wheels, sprockets, idlers, return rollers; commander MG, antennas.",
        "technique": "Skid steer: left / right track speeds = forward +- turn with acceleration limits, v = (vl + vr) / 2, "
                     "yaw rate = (vr - vl) / track separation. Track links placed every frame from an arc-length "
                     "parameter on a closed stadium path. Turret stabilised against the hull. Spring-damper suspension.",
        "limits": "Speed 8 m/s forward, 3 m/s reverse (x1.5 boost); turret 40 deg/s, gun 20 deg/s, elevation -8..+20 deg; "
                  "reload 3.5 s, 18 rounds.",
    },
    "HELI": {
        "dof": "Body 6 (x, y, z, yaw, pitch, roll); main rotor head: mast -> hub -> 5 blade pivots; tail rotor; "
               "searchlight pan / tilt 2; skids.",
        "technique": "Rigid-body state machine Parked -> Startup -> Hover / Fly -> Landing; rotor rpm integrates with 6 s "
                     "spool-up / 8 s spool-down; attitude follows acceleration (second order, omega 5 rad/s, zeta 0.8); "
                     "translucent blur disc fades in with rpm; rotor-wash dust below 4 m AGL.",
        "limits": "Cruise 15 m/s, boost 25, reverse 7; climb 4 m/s; AGL 0.5..40 m; radius <= 60 m; accel limit 8 (12 boost) "
                  "m/s2; yaw 55 deg/s; pitch <= 22 deg, roll <= 25 deg; searchlight pan +-150 deg, tilt -10..90 deg down.",
    },
    "AA-1": {
        "dof": "Mount yaw 1, cradle pitch 1, 6-barrel cluster spin 1, ejected shell casings (pool of 20).",
        "technique": "Slew-rate limited aiming; barrel spin spools up 0.5 s / down 1.5 s to 5 rev/s; 30 rounds/s tracers "
                     "with dispersion; recoil and muzzle flash through Combat; lead ring on the HUD.",
        "limits": "Elevation -5..+85 deg, slew 60 deg/s, 1550 rounds, muzzle 1000 m/s.",
    },
    "RADAR": {
        "dof": "Azimuth 1 (continuous), elevation / tilt 1; antenna of 21 tiles (7 x 3) on 14 ribs; feed horn on 3 struts; "
               "IFF bar with 6 dipoles; beacon and TX lamps.",
        "technique": "Three modes cycled by Backslash: ROTATE (12 rpm = 72 deg/s), SECTOR (+-45 deg sine sweep, period 6 s "
                     "around the aimed centre), STOP (manual aim). Idle returns to ROTATE.",
        "limits": "Tilt -5..+20 deg; azimuth = clockwise degrees from +Z.",
    },
    "BLAST": {
        "dof": "2 sliding door leaves, 1 DOF each (1.5 m travel).",
        "technique": "State machine OPEN / CLOSING / CLOSED / OPENING, smoothstep over 4 s, reversible mid-travel; closed "
                     "doors become a dynamic obstacle box for the walker. Starts OPEN. Status text 'BLAST DOORS OPEN 100%'.",
        "limits": "Travel 4 s; clear opening 2.6 m wide x 2.6 m high; walk-mode interaction within 3.5 m.",
    },
    "GATE": {
        "dof": "Boom arm 1 DOF (hinge 0..85 deg); blinking warning lamp.",
        "technique": "Smoothstep angle over 2.5 s; Enter / Backslash toggles; lamp blinks at 3 Hz; the arm box is a dynamic "
                     "obstacle only below 15 deg.",
        "limits": "0..85 deg in 2.5 s.",
    },
    "WINDSOCK": {
        "dof": "Swivel 1 + 5 chained cone segments (scenery, not possessable).",
        "technique": "Each segment is the child group of the previous one and droops more when the wind is weak "
                     "(BaseLayout wind direction / strength function of time).",
        "limits": "-",
    },
    "FLAG": {
        "dof": "12 chained strips (scenery, not possessable).",
        "technique": "Each strip is a child of the previous one, yawed by a travelling sine wave; strips never stretch.",
        "limits": "-",
    },
}


def _notes(name):
    up = name.upper()
    for k, v in DESIGN_NOTES.items():
        if k in up:
            return v
    return {"dof": "-", "technique": "-", "limits": "-"}


def _prim_cols(prims):
    return [p for p in PRIM_ORDER if any(p in d for d in prims)] or PRIM_ORDER[:3]


# ---- 14 Controllable Objects -----------------------------------------------------------------------------
def build_controllables(wb, ctx):
    ws = wb[S_CTRL]
    scene = ctx["scene"]
    ctl = scene.get("controllables", [])
    title(ws, "14  Controllable Objects",
          "Every object the user can possess (keys 5-9 and 0, [ ] to cycle, Backspace to release) plus the animated "
          "scenery. Blue cells are exported by the program (Scene::controllables(): names, control text, part counts, "
          "camera hints, constants read from the class headers, animated nodes). Pale-yellow cells are hand-maintained "
          "text FROM THE DESIGN NOTES (docs/v4_notes) - degrees of freedom, motion technique and limits - not measured.",
          span=14)
    if not ctl:
        put(ws, 4, 1, "PENDING - scene_export.json has no 'controllables' (re-run `make docs`)")
        return
    prims = [c["primitives"] for c in ctl]
    pcols = _prim_cols(prims)
    h = 5
    hdr = (["Key", "Name (exported)", "Possessable", "Root object", "Parts (leaves)"] + pcols
           + ["Group nodes", "Animated nodes", "Chase cam FOV", "Sight cam FOV", "Sight reticle", "Initial status text",
              "Controls (exported)", "Degrees of freedom / joints (design notes)", "Motion technique (design notes)",
              "Speeds and limits (design notes)", "Deepest joint chain (exported tree)", "Joint levels"])
    header(ws, h, hdr, height=48)
    first = h + 1
    chains = {c["root_id"]: c for c in articulation_chains(scene["components"], ctl)}
    c0 = 6 + len(pcols)
    for i, c in enumerate(ctl):
        r = first + i
        note = _notes(c["name"])
        vals = [c["key"], c["name"], "Yes" if c["possessable"] else "No (scenery)", c["root_object"], c["parts"]]
        for k, v in enumerate(vals):
            put(ws, r, 1 + k, v, FMT_INT if k == 4 else None, INPUT_FILL, BOLD if k == 1 else None)
        for k, p in enumerate(pcols):
            put(ws, r, 6 + k, c["primitives"].get(p, 0), FMT_INT, INPUT_FILL)
        ex = [c["groups"], c["animated_nodes"], c["camera_chase"]["fov_deg"], c["camera_sight"]["fov_deg"],
              "Yes" if c["camera_sight"]["sight"] else "No", c["status"], c["controls"]]
        for k, v in enumerate(ex):
            put(ws, r, c0 + k, v, "0" if k < 4 else None, INPUT_FILL)
        for k, key in enumerate(("dof", "technique", "limits")):
            put(ws, r, c0 + 7 + k, note[key], fill=NOTE_FILL)
        ch = chains.get(c["root_id"])
        put(ws, r, c0 + 10, " -> ".join(ch["path"]) if ch else "-", fill=INPUT_FILL)
        put(ws, r, c0 + 11, ch["levels"] if ch else 0, "0", INPUT_FILL)
    last = first + len(ctl) - 1
    t = last + 1
    put(ws, t, 2, "Total", fill=TOTAL_FILL, font=BOLD)
    for k in range(len(pcols) + 3):
        cc = 5 + k
        put(ws, t, cc, f"=SUM({col(cc)}{first}:{col(cc)}{last})", FMT_INT, TOTAL_FILL, BOLD)
    for k, p in enumerate(pcols):
        ws.cell(row=h, column=6 + k).fill = prim_fill(p, strong=True)
    pc = lambda k: f"{col(k)}{first}:{col(k)}{last}"  # noqa: E731
    put(ws, 3, 1, "Possessable", font=BOLD)
    put(ws, 3, 2, f'=COUNTIF({pc(3)},"Yes")', "0")
    put(ws, 3, 3, "Animated scenery", font=BOLD)
    put(ws, 3, 4, f'=COUNTIF({pc(3)},"No*")', "0")
    put(ws, 3, 5, "Total parts", font=BOLD)
    put(ws, 3, 6, f"=E{t}", FMT_INT)
    put(ws, 3, 7, "Animated nodes", font=BOLD)
    put(ws, 3, 8, f"={col(c0 + 1)}{t}", FMT_INT)
    ctx["refs"]["ctrl_possessable"] = f"'{S_CTRL}'!$B$3"
    ctx["refs"]["ctrl_total"] = f"COUNTA('{S_CTRL}'!$B${first}:$B${last})"
    define_name(wb, "PossessableObjects", S_CTRL, "B3")
    widths(ws, [5, 20, 12, 18, 9] + [8] * len(pcols) + [8, 9, 8, 8, 8, 30, 38, 46, 56, 44, 52, 7])
    for rr in range(first, last + 1):
        ws.row_dimensions[rr].height = 110

    # --- B. numeric specs read from the class headers --------------------------------------------------
    top = t + 3
    section(ws, top, 1, "B. Specifications read from the code (public constants of each class)")
    header(ws, top + 1, ["Key", "Object", "Parameter", "Value", "Unit", "Source"], start_col=1)
    r = top + 2
    spec_first = r
    for c in ctl:
        for s in c["specs"]:
            put(ws, r, 1, c["key"])
            put(ws, r, 2, c["name"])
            put(ws, r, 3, s["key"])
            put(ws, r, 4, s["value"], "0.###", INPUT_FILL)
            put(ws, r, 5, s["unit"])
            put(ws, r, 6, "exported: static constexpr / header constant")
            r += 1
    spec_last = r - 1
    put(ws, r, 3, "Rows", fill=TOTAL_FILL, font=BOLD)
    put(ws, r, 4, f"=COUNTA(C{spec_first}:C{spec_last})", FMT_INT, TOTAL_FILL, BOLD)
    ctx["ctrl_specs"] = (spec_first, spec_last)
    # derived figures (formulas over the spec rows)
    r += 2
    section(ws, r, 1, "Derived figures (Excel formulas over the table above)")
    spec_rng = (f"$B${spec_first}:$B${spec_last}", f"$C${spec_first}:$C${spec_last}", f"$D${spec_first}:$D${spec_last}")

    def spec_val(obj_kw, param):
        return f'SUMIFS({spec_rng[2]},{spec_rng[0]},"*{obj_kw}*",{spec_rng[1]},"{param}")'

    derived = [
        ("Tank: time to turn the turret 360 deg (s)", f"=360/{spec_val('TANK', 'turret slew rate')}"),
        ("Tank: gun elevation range (deg)", f"={spec_val('TANK', 'gun elevation max')}-{spec_val('TANK', 'gun elevation min')}"),
        ("Tank: tracks links in total", f"=2*{spec_val('TANK', 'track links per side')}"),
        ("Tank: spin-in-place yaw rate at full track speed (deg/s)",
         f"=DEGREES(2*{spec_val('TANK', 'max forward speed')}/{spec_val('TANK', 'track separation')}/2*2/2)"),
        ("AA gun: seconds to empty the magazine", f"={spec_val('AA-1', 'start rounds')}/{spec_val('AA-1', 'rate of fire')}"),
        ("AA gun: time to slew from horizon to zenith (s)",
         f"=({spec_val('AA-1', 'elevation max')}-{spec_val('AA-1', 'elevation min')})/{spec_val('AA-1', 'slew rate')}"),
        ("Helicopter: rotor rev per second", f"={spec_val('HELI', 'rotor rpm (nominal)')}/60"),
        ("Helicopter: tail rotor rpm", f"={spec_val('HELI', 'rotor rpm (nominal)')}*{spec_val('HELI', 'tail rotor ratio')}"),
        ("Helicopter: blade spacing (deg)", f"=360/{spec_val('HELI', 'main blades')}"),
        ("Radar: seconds per revolution", f"=60/{spec_val('RADAR', 'rotation rate')}"),
        ("Radar: antenna tiles", f"={spec_val('RADAR', 'antenna tile columns')}*{spec_val('RADAR', 'antenna tile rows')}"),
        ("Blast doors: full opening width of two leaves (m)", f"=2*{spec_val('BLAST', 'door leaf width')}"),
        ("Gate barrier: angular speed on average (deg/s)", f"={spec_val('GATE', 'max boom angle')}/{spec_val('GATE', 'travel time')}"),
    ]
    # the spin-in-place formula above is written explicitly to avoid a confusing expression
    derived[3] = ("Tank: spin-in-place yaw rate at full track speed, opposite tracks (deg/s)",
                  f"=DEGREES(2*{spec_val('TANK', 'max forward speed')}/{spec_val('TANK', 'track separation')})")
    for i, (lab, f) in enumerate(derived):
        put(ws, r + 1 + i, 2, lab)
        ws.merge_cells(start_row=r + 1 + i, start_column=2, end_row=r + 1 + i, end_column=3)
        put(ws, r + 1 + i, 4, f, "0.00")
    r += len(derived) + 3

    # --- C. moving parts -------------------------------------------------------------------------------
    section(ws, r, 1, "C. Moving parts and their animation text (exported node.animation of each controllable)")
    header(ws, r + 1, ["Key", "Object", "Node ID", "Node name", "Animation"], start_col=1)
    rr = r + 2
    for c in ctl:
        for m in c["moving_parts"]:
            put(ws, rr, 1, c["key"])
            put(ws, rr, 2, c["name"])
            put(ws, rr, 3, m["id"])
            put(ws, rr, 4, m["name"])
            put(ws, rr, 5, m["animation"], fill=INPUT_FILL)
            ws.merge_cells(start_row=rr, start_column=5, end_row=rr, end_column=10)
            rr += 1
    finish(ws, h, freeze="C6")
    ws.freeze_panes = "C6"


# ---- 15 Bunker & Base ------------------------------------------------------------------------------------
def _merge_sections(root):
    """Sections (depth-1 children) of a root, merged by name (e.g. several 'Tree' groups)."""
    out = {}
    for s in root["sections"]:
        e = out.setdefault(s["name"], {"parts": 0, "groups": 0, "primitives": {}})
        e["parts"] += s["parts"]
        e["groups"] += s["groups"]
        for k, v in s["primitives"].items():
            e["primitives"][k] = e["primitives"].get(k, 0) + v
    return out


def build_bunker_base(wb, ctx):
    ws = wb[S_BASE]
    sc = ctx["scene"]
    if "walkspace" not in sc:
        put(ws, 4, 1, "PENDING - scene_export.json has no 'walkspace' (re-run `make docs`)")
        return
    title(ws, "15  Bunker & Base",
          "The underground command bunker and the military base apron, taken from the program's export: part counts "
          "per section (scene-graph groups), the site plan (BaseLayout.h), the walk space (floors, wall boxes, indoor "
          "volumes of WalkSpace) and every practical light. Blue = exported, white = formula. Room names are the "
          "scene-graph section names; the walk-space tables are anonymous boxes in metres (+Z north, +X east).",
          span=14)
    r = 4
    roots = {x["object"]: x for x in sc["roots"] if x["parts"] > 1 or x["object"] in ("Command Bunker", "Base")}
    prims = _prim_cols([x["primitives"] for x in sc["roots"]])
    # ---- A. sections --------------------------------------------------------------------------------
    section(ws, r, 1, "A. Part counts by section (scene-graph groups directly under each root)")
    hdr = ["Area", "Section"] + prims + ["Parts (leaves)", "Group nodes", "Share of area"]
    header(ws, r + 1, hdr)
    for k, p in enumerate(prims):
        ws.cell(row=r + 1, column=3 + k).fill = prim_fill(p, strong=True)
    r += 2
    sec_rows = {}
    for area in ("Command Bunker", "Base"):
        root = roots.get(area)
        if not root:
            continue
        a0 = r
        for name, e in _merge_sections(root).items():
            put(ws, r, 1, area)
            put(ws, r, 2, name, font=BOLD)
            for k, p in enumerate(prims):
                put(ws, r, 3 + k, e["primitives"].get(p, 0), FMT_INT, INPUT_FILL)
            lastp = col(2 + len(prims))
            put(ws, r, 3 + len(prims), f"=SUM({col(3)}{r}:{lastp}{r})", FMT_INT, font=BOLD)
            put(ws, r, 4 + len(prims), e["groups"], FMT_INT, INPUT_FILL)
            r += 1
        a1 = r - 1
        pc = col(3 + len(prims))
        for rr in range(a0, a1 + 1):
            put(ws, rr, 5 + len(prims), f"={pc}{rr}/SUM(${pc}${a0}:${pc}${a1})", "0.0%")
        put(ws, r, 1, area, fill=TOTAL_FILL, font=BOLD)
        put(ws, r, 2, "Subtotal", fill=TOTAL_FILL, font=BOLD)
        for k in range(len(prims) + 2):
            put(ws, r, 3 + k, f"=SUM({col(3 + k)}{a0}:{col(3 + k)}{a1})", FMT_INT, TOTAL_FILL, BOLD)
        sec_rows[area] = (a0, a1, r)
        r += 2
    # whole-scene object table
    section(ws, r, 1, "Parts per final object (exported scene-graph tags; compare with sheet 3, COUNTIFS)")
    header(ws, r + 1, ["Object", "Parts (exported)", "Group nodes", "Parts (sheet 3 COUNTIFS)", "Check"])
    r += 2
    o_first = r
    o_rng = f"'{S_OBJ}'!{ctx['obj']['range']('B').split('!')[1]}" if "obj" in ctx else None
    p_rng = f"'{S_OBJ}'!{ctx['obj']['range']('C').split('!')[1]}" if "obj" in ctx else None
    for o in sc["objects"]:
        put(ws, r, 1, o["object"], font=BOLD)
        put(ws, r, 2, o["parts"], FMT_INT, INPUT_FILL)
        put(ws, r, 3, o["groups"], FMT_INT, INPUT_FILL)
        if o_rng:
            put(ws, r, 4, f'=COUNTIFS({o_rng},A{r},{p_rng},"<>Group")', FMT_INT)
            put(ws, r, 5, f'=IF(B{r}=D{r},"match","MISMATCH")')
        r += 1
    put(ws, r, 1, "All objects", fill=TOTAL_FILL, font=BOLD)
    for k in (2, 3, 4):
        put(ws, r, k, f"=SUM({col(k)}{o_first}:{col(k)}{r - 1})", FMT_INT, TOTAL_FILL, BOLD)
    put(ws, r, 5, f'=IF(COUNTIF(E{o_first}:E{r - 1},"MISMATCH")=0,"all match","MISMATCH")', fill=TOTAL_FILL, font=BOLD)
    ws.conditional_formatting.add(f"E{o_first}:E{r}", CellIsRule(operator="equal", formula=['"MISMATCH"'], fill=FAIL_FILL))
    ctx["refs"]["objects_check"] = f"'{S_BASE}'!$E${r}"
    r += 3

    # ---- B. site plan -------------------------------------------------------------------------------
    lay = sc["layout"]
    section(ws, r, 1, "B. Site plan (BaseLayout.h constants, metres; +Z north, +X east)")
    header(ws, r + 1, ["Constant", "x", "y", "z", "value", "Distance from origin (xz, m)"])
    r += 2
    for k, v in lay.items():
        put(ws, r, 1, k, font=BOLD)
        if isinstance(v, list):
            for a in range(3):
                put(ws, r, 2 + a, v[a], FMT_3, INPUT_FILL)
            put(ws, r, 6, f"=SQRT(B{r}^2+D{r}^2)", "0.0")
        else:
            put(ws, r, 5, v, FMT_3, INPUT_FILL)
        r += 1
    r += 2

    # ---- C. walk floors -----------------------------------------------------------------------------
    ww = sc["walkspace"]
    section(ws, r, 1, "C. Walk-space floors (WalkSpace::floors): footprints where the walker stands above the terrain")
    header(ws, r + 1, ["#", "x0", "z0", "x1", "z1", "y0 (low edge)", "y1 (high edge)", "slope axis (0 flat, 1 x, 2 z)",
                       "ceiling y", "Width (m)", "Depth (m)", "Area (m2)", "Type", "Rise (m)", "Clear height (m)"], height=46)
    r += 2
    f_first = r
    for i, f in enumerate(ww["floors"]):
        put(ws, r, 1, i + 1)
        for k, key in enumerate(("x0", "z0", "x1", "z1", "y0", "y1", "slope_axis", "ceiling")):
            put(ws, r, 2 + k, f[key], FMT_3, INPUT_FILL)
        put(ws, r, 10, f"=D{r}-B{r}", "0.00")
        put(ws, r, 11, f"=E{r}-C{r}", "0.00")
        put(ws, r, 12, f"=J{r}*K{r}", "0.00")
        put(ws, r, 13, f'=IF(H{r}=0,"flat floor","ramp / slope")')
        put(ws, r, 14, f"=ABS(G{r}-F{r})", "0.00")
        put(ws, r, 15, f'=IF(ISNUMBER(I{r}),I{r}-MAX(F{r},G{r}),"open sky")', "0.00")
        r += 1
    f_last = r - 1
    put(ws, r, 1, "Total", fill=TOTAL_FILL, font=BOLD)
    put(ws, r, 12, f"=SUM(L{f_first}:L{f_last})", "0.0", TOTAL_FILL, BOLD)
    put(ws, r, 13, f'=COUNTIF(M{f_first}:M{f_last},"ramp*")&" ramps, "&COUNTIF(M{f_first}:M{f_last},"flat*")&" flat"',
        fill=TOTAL_FILL, font=BOLD)
    ctx["refs"]["walk_floors"] = f"COUNT('{S_BASE}'!$B${f_first}:$B${f_last})"
    r += 3

    # ---- D. walls and indoor volumes ----------------------------------------------------------------
    for label, key, unit_note in (("D. Wall boxes (WalkSpace::walls): the walker collides with them", "walls", ""),
                                  ("E. Indoor volumes (WalkSpace::indoorVolumes): no sky light / rain / little fog; "
                                   "the renderer uses the first 8", "indoor", "")):
        section(ws, r, 1, label)
        header(ws, r + 1, ["#", "min x", "min y", "min z", "max x", "max y", "max z", "Size x (m)", "Size y (m)",
                           "Size z (m)", "Volume (m3)"])
        r += 2
        b_first = r
        for i, b in enumerate(ww[key]):
            put(ws, r, 1, i + 1)
            for a in range(3):
                put(ws, r, 2 + a, b["min"][a], FMT_3, INPUT_FILL)
                put(ws, r, 5 + a, b["max"][a], FMT_3, INPUT_FILL)
            for a in range(3):
                put(ws, r, 8 + a, f"={col(5 + a)}{r}-{col(2 + a)}{r}", "0.00")
            put(ws, r, 11, f"=H{r}*I{r}*J{r}", "0.0")
            r += 1
        b_last = r - 1
        put(ws, r, 1, "Total", fill=TOTAL_FILL, font=BOLD)
        put(ws, r, 2, f"=COUNT(B{b_first}:B{b_last})", FMT_INT, TOTAL_FILL, BOLD)
        put(ws, r, 11, f"=SUM(K{b_first}:K{b_last})", "0.0", TOTAL_FILL, BOLD)
        ctx["refs"]["walk_" + key] = f"'{S_BASE}'!$B${r}"
        r += 3

    # ---- F. practical lights -----------------------------------------------------------------------
    pl = sc.get("practical_lights", [])
    section(ws, r, 1, "F. Practical lights (emissive parts that also light the scene; the renderer uses the 16 most "
                      "relevant per view - TS_MAX_PRACTICALS, from the design notes)")
    header(ws, r + 1, ["Node ID", "Name", "Object", "x", "y", "z", "colour R", "colour G", "colour B", "intensity",
                       "range (m)", "cone half-angle (deg)", "Type", "Distance from origin (m)", "Animated"], height=46)
    r += 2
    l_first = r
    for n in pl:
        put(ws, r, 1, n["id"])
        put(ws, r, 2, n["name"])
        put(ws, r, 3, n["object"])
        for a in range(3):
            put(ws, r, 4 + a, n["position"][a], "0.00", INPUT_FILL)
            put(ws, r, 7 + a, n["color"][a], "0.00", INPUT_FILL)
        put(ws, r, 10, n["intensity"], "0.0", INPUT_FILL)
        put(ws, r, 11, n["range"], "0.0", INPUT_FILL)
        put(ws, r, 12, n["cone_deg"], "0", INPUT_FILL)
        put(ws, r, 13, f'=IF(L{r}>0,"spot","omni")')
        put(ws, r, 14, f"=SQRT(D{r}^2+F{r}^2)", "0.0")
        put(ws, r, 15, "Yes" if n["animated"] else "No")
        r += 1
    l_last = r - 1
    put(ws, r, 1, "Total", fill=TOTAL_FILL, font=BOLD)
    put(ws, r, 2, f"=COUNTA(A{l_first}:A{l_last})", FMT_INT, TOTAL_FILL, BOLD)
    put(ws, r, 10, f"=SUM(J{l_first}:J{l_last})", "0.0", TOTAL_FILL, BOLD)
    put(ws, r, 12, f'=COUNTIF(M{l_first}:M{l_last},"spot")&" spots"', fill=TOTAL_FILL, font=BOLD)
    ctx["refs"]["practicals"] = f"'{S_BASE}'!$B${r}"
    define_name(wb, "PracticalLights", S_BASE, f"B{r}")
    r += 2
    section(ws, r, 1, "Practical lights per object (COUNTIF over the list)")
    header(ws, r + 1, ["Object", "Lights", "Share"])
    objs = list(dict.fromkeys(n["object"] for n in pl))
    for i, o in enumerate(objs):
        rr = r + 2 + i
        put(ws, rr, 1, o, font=BOLD)
        put(ws, rr, 2, f"=COUNTIF($C${l_first}:$C${l_last},A{rr})", FMT_INT)
        put(ws, rr, 3, f"=B{rr}/$B${l_last + 1}", "0.0%")
    # figures
    img_r = r + len(objs) + 5
    for k, (fig, cap) in enumerate((("base_overview", "Base overview"), ("bunker_cic", "Command room (CIC)"))):
        p = path(f"report/figures_v4/{fig}.png")
        put(ws, img_r - 1, 1 + k * 8, cap + (" (figure)" if os.path.exists(p) else f" - pending capture report/figures_v4/{fig}.png"),
            border=False)
        image(ws, f"{col(1 + k * 8)}{img_r}", p, width=520)
    widths(ws, [24, 24, 14, 10, 10, 10, 10, 10, 10, 10, 10, 11, 12, 12, 11, 11])
    finish(ws, 3, freeze="A4")
    ws.print_title_rows = None


# ---- 16 Combat & Effects ---------------------------------------------------------------------------------
def _source_consts(rel, names):
    """Reads `constexpr <type> kName = value;` lines of a .cpp for constants that are private to it."""
    out = {}
    try:
        text = open(path(rel), encoding="utf-8").read()
    except OSError:
        return out
    for n in names:
        m = re.search(r"\b" + n + r"\s*=\s*([0-9.]+)f?(?:\s*/\s*([0-9.]+)f?)?", text)
        if m:
            out[n] = float(m.group(1)) / (float(m.group(2)) if m.group(2) else 1.0)
    return out


WEAPON_NOTES = {  # from the Combat.h header comments and docs/V4_PROGRESS.md (design notes)
    "missile": ("UAV rail round (4 on rails)", "Homing on the laser designator at the camera centre; speed ramps up over "
                "kMissileRampTime to kMissileMaxSpeed; turn rate limited; proximity fuse; burns out after kMissileLife."),
    "shell": ("Tank 120 mm-class shell", "Ballistic (gravity 9.81 m/s2, sub-stepped 1/120 s), no guidance; detonates on "
              "static solids or targets."),
    "tracer": ("AA gatling tracer", "Straight fast round with dispersion; segment-vs-sphere test per step so it cannot "
               "tunnel through a target."),
}


def build_combat(wb, ctx):
    ws = wb[S_COMBAT]
    sc = ctx["scene"]
    if "combat" not in sc:
        put(ws, 4, 1, "PENDING - scene_export.json has no 'combat' (re-run `make docs`)")
        return
    title(ws, "16  Combat & Effects",
          "UAV rail rounds, tank shells and AA tracers; guidance, fuse, damage radius; the particle types of an "
          "explosion with counts and lifetimes MEASURED by running the real Combat simulation inside the exporter; the "
          "intruder life cycle (destroy -> burning wreck -> removal -> respawn) measured in a headless Scene run; "
          "friend-or-foe and keep-out rules. Blue = exported / measured, white = formula, pale yellow = design-note text.",
          span=12)
    consts = {c["key"]: c for c in sc["combat"]["constants"]}
    r = 4
    section(ws, r, 1, "A. Constants (Combat.h, DroneController.h, Uav.h, Scene.h)")
    header(ws, r + 1, ["Constant", "Value", "Unit", "Source file"])
    r += 2
    crow = {}
    for c in sc["combat"]["constants"]:
        put(ws, r, 1, c["key"], font=BOLD)
        put(ws, r, 2, c["value"], "0.###", INPUT_FILL)
        put(ws, r, 3, c["unit"])
        put(ws, r, 4, c["source"])
        crow[c["key"]] = f"$B${r}"
        r += 1
    cpp = _source_consts("src/scene/Combat.cpp", ["kScorchLife", "kScorchCap", "kMaxSubstep", "kGravity"])
    cpp_unit = {"kScorchLife": "s", "kScorchCap": "marks", "kMaxSubstep": "s", "kGravity": "m/s2"}
    for k, v in cpp.items():
        put(ws, r, 1, k, font=BOLD)
        put(ws, r, 2, v, "0.####", INPUT_FILL)
        put(ws, r, 3, cpp_unit[k])
        put(ws, r, 4, "Combat.cpp (parsed from the source file)")
        crow[k] = f"$B${r}"
        r += 1
    ctx["refs"]["combat_rows"] = crow
    r += 2

    # ---- B. weapons ---------------------------------------------------------------------------------
    section(ws, r, 1, "B. Weapons (formulas over A; tank / AA numbers from the controllable specs on sheet 14)")
    header(ws, r + 1, ["Weapon", "Muzzle / max speed (m/s)", "Blast power (BlastEvent::power)", "Damage radius (m) = 1.6 x power",
                       "Max flight time (s)", "Max range (m)", "Reload / interval (s)", "Ammunition", "Guidance and fuse "
                       "(design notes)"], height=46)
    spec_first, spec_last = ctx.get("ctrl_specs", (1, 1))
    sp = lambda obj, par: (f"SUMIFS('{S_CTRL}'!$D${spec_first}:$D${spec_last},'{S_CTRL}'!$B${spec_first}:$B${spec_last},"  # noqa: E731
                           f"\"*{obj}*\",'{S_CTRL}'!$C${spec_first}:$C${spec_last},\"{par}\")")
    k = crow
    weapons = [
        ("missile", f"={k['kMissileMaxSpeed']}", 1.0, f"={k['kMissileLife']}", f"=B{{r}}*E{{r}}",
         f"={k['kLaunchInterval']}", f"={k['kUavMissiles']}&\" rounds, \"&{k['kUavReloadTime']}&\" s reload each\"",
         WEAPON_NOTES["missile"][1] + " Fuse radius is the constant kMissileFuse."),
        ("shell", f"={k['kShellSpeed']}", 1.6, '="ballistic"', '="level range v2 sin(2a)/g"', f"={sp('TANK', 'reload time')}",
         f"={sp('TANK', 'start rounds')}&\" rounds\"", WEAPON_NOTES["shell"][1]),
        ("tracer", f"={k['kTracerSpeed']}", 0.15, f"={k['kTracerLife']}", f"=B{{r}}*E{{r}}",
         f"=1/{sp('AA-1', 'rate of fire')}", f"={sp('AA-1', 'start rounds')}&\" rounds, \"&{sp('AA-1', 'rate of fire')}&\" per s\"",
         WEAPON_NOTES["tracer"][1]),
    ]
    r += 2
    for key, speed, power, life, rng, reload_, ammo, note in weapons:
        put(ws, r, 1, WEAPON_NOTES[key][0], font=BOLD)
        put(ws, r, 2, speed, "0")
        put(ws, r, 3, power, "0.00", NOTE_FILL)
        put(ws, r, 4, f"={k['kDamageRadiusPerPower']}*C{r}", "0.00")
        put(ws, r, 5, life, "0.0")
        put(ws, r, 6, rng.replace("{r}", str(r)), "#,##0")
        put(ws, r, 7, reload_, "0.00")
        put(ws, r, 8, ammo)
        put(ws, r, 9, note, fill=NOTE_FILL)
        ws.merge_cells(start_row=r, start_column=9, end_row=r, end_column=12)
        ws.row_dimensions[r].height = 60
        r += 1
    put(ws, r, 1, "Blast power values (1.0 / 1.6 / 0.15) are from the Combat.h comment on BlastEvent::power "
                  "(design notes); speeds, life and radius factor are exported.", border=False)
    r += 3

    # ---- C. effect census ---------------------------------------------------------------------------
    eff = sc.get("effects", [])
    section(ws, r, 1, "C. Particle effects measured by the program (Combat run at 60 Hz in the exporter)")
    header(ws, r + 1, ["Effect", "Blast power", "Particles at spawn", "Peak particles", "Seconds until all gone",
                       "Peak dynamic lights", "Share of particle cap at peak"], height=46)
    r += 2
    e_first = r
    for e in eff:
        put(ws, r, 1, e["name"], font=BOLD)
        put(ws, r, 2, e["power"], "0.00", INPUT_FILL)
        put(ws, r, 3, e["particles_at_spawn"], FMT_INT, INPUT_FILL)
        put(ws, r, 4, e["peak_particles"], FMT_INT, INPUT_FILL)
        put(ws, r, 5, e["seconds_until_empty"], "0.0", INPUT_FILL)
        put(ws, r, 6, e["flash_lights"], FMT_INT, INPUT_FILL)
        put(ws, r, 7, f"=D{r}/{k['kParticleCap']}", "0.0%")
        r += 1
    e_last = r - 1
    r += 2
    section(ws, r, 1, "Particle types per effect (counts and lifetimes of each type)")
    header(ws, r + 1, ["Effect", "Type", "Count", "Life min (s)", "Life max (s)", "Mean life (s)", "Radius min (m)",
                       "Radius max (m)", "Share of effect"])
    r += 2
    t_first = r
    for e in eff:
        a0 = r
        for t in e["types"]:
            put(ws, r, 1, e["name"])
            put(ws, r, 2, t["type"], font=BOLD)
            put(ws, r, 3, t["count"], FMT_INT, INPUT_FILL)
            put(ws, r, 4, t["life_min"], "0.00", INPUT_FILL)
            put(ws, r, 5, t["life_max"], "0.00", INPUT_FILL)
            put(ws, r, 6, f"=(D{r}+E{r})/2", "0.00")
            put(ws, r, 7, t["size_min"], "0.000", INPUT_FILL)
            put(ws, r, 8, t["size_max"], "0.000", INPUT_FILL)
            r += 1
        for rr in range(a0, r):
            put(ws, rr, 9, f"=C{rr}/SUM($C${a0}:$C${r - 1})", "0.0%")
    t_last = r - 1
    put(ws, r, 1, "Distinct particle types", fill=TOTAL_FILL, font=BOLD)
    put(ws, r, 3, f'=SUMPRODUCT(1/COUNTIF(B{t_first}:B{t_last},B{t_first}:B{t_last}))', "0", TOTAL_FILL, BOLD)
    ctx["refs"]["particle_types"] = f"'{S_COMBAT}'!$C${r}"
    r += 3

    # ---- D. life cycle ------------------------------------------------------------------------------
    sim = sc.get("simulation", {})
    section(ws, r, 1, "D. Intruder life cycle: destroy -> burning wreck -> removal -> respawn")
    header(ws, r + 1, ["Phase", "Constant (s)", "Cumulative (s)", "Meaning"])
    r += 2
    c0 = r
    phases = [("Destruction (t0)", "=0", "Rover hit: debris pieces thrown (rigid bodies), fireball, smoke column, scorch mark"),
              ("Burning wreck", f"={k['kWreckBurnTime']}", "Wreck burns for kWreckBurnTime: fire + smoke + embers, light flicker"),
              ("Wreck stays", f"={k['kWreckClearTime']}-{k['kWreckBurnTime']}", "Cold hulk until kWreckClearTime (drone ignores it, "
                                                                                 "kWreckIgnoreRadius / Time)"),
              ("Respawn delay", f"={k['kRespawnDelay']}", "Gone, then a new intruder enters at the next spawn point")]
    for i, (a, f, d) in enumerate(phases):
        put(ws, r, 1, a, font=BOLD)
        put(ws, r, 2, f, "0.0")
        put(ws, r, 3, f"=SUM($B${c0}:B{r})", "0.0")
        put(ws, r, 4, d, fill=NOTE_FILL)
        ws.merge_cells(start_row=r, start_column=4, end_row=r, end_column=9)
        r += 1
    life_total = f"$C${r - 1}"
    put(ws, r, 1, "Total cycle", fill=TOTAL_FILL, font=BOLD)
    put(ws, r, 3, f"={life_total}", "0.0", TOTAL_FILL, BOLD)
    cycle_cell = f"C{r}"
    r += 2
    ev = sim.get("events", [])
    section(ws, r, 1, f"Measured in a headless Scene run ({sim.get('duration', 0):.0f} s at {1 / sim.get('dt', 1):.0f} Hz; "
                      "intruder 0 destroyed at t = 40 s through Scene::destroyIntruder)")
    header(ws, r + 1, ["Time (s)", "Intruder", "Event", "Delta to previous event of that intruder (s)"])
    r += 2
    ev_first = r
    last_t = {}
    for e in ev:
        put(ws, r, 1, e["t"], "0.00", INPUT_FILL)
        put(ws, r, 2, e["intruder"], "0", INPUT_FILL)
        put(ws, r, 3, e["event"], fill=INPUT_FILL)
        prev = last_t.get(e["intruder"])
        put(ws, r, 4, f"=A{r}-A{prev}" if prev else "-", "0.00")
        last_t[e["intruder"]] = r
        r += 1
    ev_last = r - 1
    # measured vs constant check
    kill = next((i for i, e in enumerate(ev) if e["event"].startswith("destroy")), None)
    alive_again = next((i for i, e in enumerate(ev) if e["intruder"] == 0 and e["event"] == "Gone -> Alive"), None)
    r += 1
    put(ws, r, 1, "Measured cycle (destroy -> next entry)", font=BOLD)
    ws.merge_cells(start_row=r, start_column=1, end_row=r, end_column=2)
    if kill is not None and alive_again is not None:
        put(ws, r, 3, f"=A{ev_first + alive_again}-A{ev_first + kill}", "0.00")
        put(ws, r, 4, f'=IF(ABS(C{r}-{cycle_cell})<=0.1,"matches constants","CHECK")', font=BOLD)
        ws.conditional_formatting.add(f"D{r}", CellIsRule(operator="equal", formula=['"matches constants"'], fill=PASS_FILL))
        ctx["refs"]["cycle_check"] = f"'{S_COMBAT}'!$D${r}"
    r += 2
    entries = [e for e in ev if e["event"] == "Waiting -> Alive"]
    put(ws, r, 1, "Intruder entry times (s)", font=BOLD)
    ws.merge_cells(start_row=r, start_column=1, end_row=r, end_column=2)
    for i, e in enumerate(entries):
        put(ws, r, 3 + i, e["t"], "0.0", INPUT_FILL)
    r += 1
    put(ws, r, 1, "Rail rounds: ready before / after one launch / measured reload (s)", font=BOLD)
    ws.merge_cells(start_row=r, start_column=1, end_row=r, end_column=2)
    put(ws, r, 3, sim.get("rounds_ready_before"), "0", INPUT_FILL)
    put(ws, r, 4, sim.get("rounds_ready_after_launch"), "0", INPUT_FILL)
    put(ws, r, 5, sim.get("reload_seconds_measured"), "0.00", INPUT_FILL)
    put(ws, r, 6, f'=IF(ABS(E{r}-{k["kUavReloadTime"]})<=0.1,"matches kUavReloadTime","CHECK")', font=BOLD)
    ws.merge_cells(start_row=r, start_column=6, end_row=r, end_column=8)
    ctx["refs"]["reload_check"] = f"'{S_COMBAT}'!$F${r}"
    r += 3

    # ---- E. rules -----------------------------------------------------------------------------------
    section(ws, r, 1, "E. Targeting rules (values come from A; the rule text is from the design notes)")
    header(ws, r + 1, ["Rule", "Value", "Unit", "Behaviour (design notes)"])
    r += 2
    box = sc["combat"]["keep_out_box"]
    rules = [
        ("Lock time before a launch", f"={k['kLockTime']}", "s", "The target must stay in the thermal camera continuously."),
        ("Interval between rounds", f"={k['kLaunchInterval']}", "s", "Rate limit even when rounds are ready."),
        ("Friend-or-foe (IFF) radius", f"={k['kFriendlyRadius']}", "m", "Hot blobs this close to a friendly vehicle (tank, "
                                                                       "helicopter, AA gun ...) are never targets."),
        ("Wreck ignore radius", f"={k['kWreckIgnoreRadius']}", "m", "A cooling wreck keeps producing a hot blob; detections "
                                                                    "near a known wreck are ignored."),
        ("Wreck ignore time", f"={k['kWreckIgnoreTime']}", "s", "How long the ignore zone lasts."),
        ("Keep-out: bunker entrance ramp, x half width (m)", f"={(box['max'][0] - box['min'][0]) / 2:.4f}", "m",
         "No rounds at targets on the base's own doorway (box x: %.1f..%.1f)." % (box["min"][0], box["max"][0])),
        ("Keep-out: z range (m)", f"={box['max'][2] - box['min'][2]:.4f}", "m",
         "z from %.1f to %.1f (ramp bottom to top plus margin; computed from BaseLayout.h like Scene.cpp)." % (box["min"][2], box["max"][2])),
        ("Damage radius of a UAV missile", f"={k['kDamageRadiusPerPower']}*1", "m", "Destroyed at 0.5 r, untouched at 3 r (T105)."),
    ]
    for lab, f, u, text in rules:
        put(ws, r, 1, lab, font=BOLD)
        put(ws, r, 2, f, "0.00")
        put(ws, r, 3, u)
        put(ws, r, 4, text, fill=NOTE_FILL)
        ws.merge_cells(start_row=r, start_column=4, end_row=r, end_column=9)
        r += 1
    r += 2
    p = path("report/figures_v4/explosion_mid.png")
    put(ws, r, 1, "Explosion (mid phase)" + ("" if os.path.exists(p) else " - pending capture report/figures_v4/explosion_mid.png"),
        border=False)
    image(ws, f"A{r + 1}", p, width=560)
    widths(ws, [44, 14, 16, 18, 16, 16, 16, 24, 46, 10, 10, 10])
    finish(ws, 3, freeze="A4")
    ws.print_title_rows = None
