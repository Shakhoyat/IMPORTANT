"""Sheet "31 v9 Controls & Interaction" (v9 M2): unified game controls, context interaction, pause menu, settings.

Sources (written by the program or its tests; only wording is authored here):
  docs/v9_notes/m2/keys.json              ThermalSense --export-keys: every binding, its context, the old binding, conflicts
  docs/v9_notes/m2/m2_controls.log        --input-script run of ThermalSense.exe ('script log' lines: prompt, menu, sensitivity)
  docs/v9_notes/m2/ThermalSense_after_script.ini   the settings file the menu wrote during that run
  build/test_results.json                 T340-T349 (status via sheet 10)
  docs/v9_notes/m2/shots, exe_frames      headless captures and the exe frames
The coordinator registers the sheet (S_V9M2, INFO, build_v9_m2) in tools/build_workbook.py.
"""
import json
import os
import re

from openpyxl.formatting.rule import CellIsRule

from wb_common import BOLD, INPUT_FILL, NOTE_FILL, PASS_FILL, PENDING_FILL, TOTAL_FILL, file_link, finish, header, path, put, section, title, widths
from wb_v6 import pending
import wb_v9

S_V9M2 = "31 v9 Controls & Interaction"
INFO = "v9 M2: input mapping as data (no conflicts), context interaction E with view-cone + line-of-sight pick, Tab units, pause menu (field guide), settings in ThermalSense.ini; tests T340-T349"

KEYS_JSON = "docs/v9_notes/m2/keys.json"
SCRIPT_LOG = "docs/v9_notes/m2/m2_controls.log"
INI = "docs/v9_notes/m2/ThermalSense_after_script.ini"

LOG_RE = re.compile(r"script log (\S+) \| (.*?) \| ([\d.]+) fps")
FIELD_RE = {
    "ctl": re.compile(r"ctl (.+?) (?:pos|sel) "),
    "yaw": re.compile(r"yaw (-?\d+)"),
    "prompt": re.compile(r'prompt "([^"]*)"'),
    "menu": re.compile(r"menu (\S+)"),
    "sens": re.compile(r"sens ([\d.]+)"),
}

CONCEPTS = [
    ("Input mapping as data", "Every binding is a row (key, modifiers, context, action, old binding) in keyMapV9(); help, README and "
     "this sheet come from it, and a test proves no key does two things in contexts that can be active together.",
     "src/scene/KeyMap.cpp, T342 / T343"),
    ("Input contexts", "ANY overlaps every gameplay context (OVERVIEW, WALK, SOLDIER, VEHICLE, RIDE); the pause menu and the POV list "
     "are modal: while open they take their keys first, so E can be 'interact' in play and 'dusk' in the menu.",
     "keyContextsOverlap()"),
    ("Context-sensitive interaction", "One 'use' key: the App lists what can be used from here (doors, the barrier, the hall "
     "doorway, soldiers, vehicles, UAVs, release) and one pick decides; the prompt names the action before it happens.",
     "scene/Interact.h, app/AppInteract.cpp"),
    ("View-cone picking", "Candidate accepted if horizontal distance <= reach and the angle between the view direction and the "
     "direction to it <= the cone half-angle (on foot 50 deg, headings only; from an orbit / free camera 9 deg in 3D, 90 m); the "
     "nearest wins, ties go to the smaller angle.", "pickInteractable(), T340"),
    ("Line of sight", "Segment eye -> target against the WalkSpace walls and the static solid boxes (slab test); boxes that contain "
     "the eye or the target are ignored, so a door frame or a hull never hides its own object. Real gaps (windows) do not block.",
     "segmentClear(), T341 / T349"),
    ("Look sensitivity", "Every mouse-look path (soldier, third-person arm, vehicle aim, ride-along, orbit / free) scales the "
     "pointer delta by the sensitivity; invert Y flips the vertical axis only. Verified in the exe: 200 px = 30 deg at 1.00x, "
     "36 deg at 1.20x.", "applyLookSettings(), T345"),
    ("Persistent settings", "key = value file next to the exe, parsed with clamping and per-line warnings; the command line "
     "overrides a value for one run until it is changed in the menu.", "scene/GameMenu.cpp, T344"),
]


def _log_rows(rel):
    p = path(rel)
    if not os.path.exists(p):
        return []
    rows = []
    with open(p, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LOG_RE.search(line)
            if not m:
                continue
            st = m.group(2)
            row = {"label": m.group(1), "fps": float(m.group(3))}
            for k, rx in FIELD_RE.items():
                mm = rx.search(st)
                row[k] = mm.group(1) if mm else ""
            rows.append(row)
    return rows


def build_v9_m2(wb, ctx):
    ws = wb[S_V9M2] if S_V9M2 in wb.sheetnames else wb.create_sheet(S_V9M2)
    title(ws, "31  v9 M2: Controls & Interaction",
          "Human-computer interaction for a real-time 3D scene. Input mapping: keys are data with a context, checked for "
          "conflicts. Context-sensitive interaction: one 'use' key whose meaning the on-screen prompt states, chosen by a "
          "view-cone + reach + line-of-sight pick (the same ray / slab tests as picking and collision). A pause menu (field "
          "guide pages: travel, time of day, views) and persistent settings (mouse sensitivity, invert Y, FOV, vsync, frame "
          "limit, head bob, quality). Blue = imported from the program's JSON / logs; white = formulas; amber = notes.", span=12)
    keys = None
    if os.path.exists(path(KEYS_JSON)):
        with open(path(KEYS_JSON), encoding="utf-8") as f:
            keys = json.load(f)
    r = 5
    section(ws, r, 1, "Concepts")
    r += 1
    header(ws, r, ["Concept", "How it works here", "Code / test"], height=20)
    for i, (a, b, c) in enumerate(CONCEPTS):
        put(ws, r + 1 + i, 1, a, font=BOLD, fill=NOTE_FILL)
        put(ws, r + 1 + i, 2, b, fill=NOTE_FILL)
        put(ws, r + 1 + i, 3, c, fill=NOTE_FILL)
        ws.row_dimensions[r + 1 + i].height = 48
    r += len(CONCEPTS) + 2

    # Key changes and the whole table.
    section(ws, r, 1, f"Key bindings (ThermalSense --export-keys -> {KEYS_JSON})")
    r += 1
    if not keys:
        r = pending(ws, r, KEYS_JSON, span=6) + 2
        ka = kb = r
    else:
        header(ws, r, ["Key", "Context", "Action", "Before v9 M2", "Changed?", "GLFW code"], height=22)
        ka = r + 1
        for i, k in enumerate(keys["keys"]):
            q = ka + i
            put(ws, q, 1, k["key"], font=BOLD)
            put(ws, q, 2, k["context"], fill=INPUT_FILL)
            put(ws, q, 3, k["action"], fill=INPUT_FILL)
            put(ws, q, 4, k["was"], fill=INPUT_FILL)
            put(ws, q, 5, f'=IF(LEN(D{q})>0,"CHANGED","")', font=BOLD)
            put(ws, q, 6, k["code"], "0", INPUT_FILL)
        kb = ka + len(keys["keys"]) - 1
        ws.conditional_formatting.add(f"E{ka}:E{kb}", CellIsRule(operator="equal", formula=['"CHANGED"'], fill=PENDING_FILL))
        r = kb + 1
        put(ws, r, 1, "Conflicts (export)", font=BOLD, fill=TOTAL_FILL)
        put(ws, r, 2, len(keys.get("conflicts", [])), "0", INPUT_FILL, BOLD)
        put(ws, r, 3, "; ".join(keys.get("conflicts", [])) or "none: no key has two actions in overlapping contexts", fill=INPUT_FILL)
        r += 2
    wb_v9._kpi(ws, [("Bindings", f"=COUNTA(A{ka}:A{kb})" if keys else 0, "0"),
                    ("Changed in M2", f'=COUNTIF(E{ka}:E{kb},"CHANGED")' if keys else 0, "0"),
                    ("Conflicts", len(keys.get("conflicts", [])) if keys else "pending", "0")])

    # The exe run.
    section(ws, r, 1, "ThermalSense.exe input script (docs/v9_notes/m2/m2_controls.txt): Tab -> E -> V -> Esc -> menu -> Settings -> "
                      "sensitivity -> Resume")
    r += 1
    header(ws, r, ["Step (log label)", "Controlled", "Prompt on screen", "Menu page", "Sensitivity", "Yaw (deg)", "Yaw change", "FPS"], height=30)
    rows = _log_rows(SCRIPT_LOG)
    la = r + 1
    if not rows:
        r = pending(ws, la, SCRIPT_LOG, span=8) + 1
    else:
        for i, L in enumerate(rows):
            q = la + i
            put(ws, q, 1, L["label"], font=BOLD)
            put(ws, q, 2, L["ctl"], fill=INPUT_FILL)
            put(ws, q, 3, L["prompt"], fill=INPUT_FILL)
            put(ws, q, 4, L["menu"], fill=INPUT_FILL)
            put(ws, q, 5, float(L["sens"]) if L["sens"] else None, "0.00", INPUT_FILL)
            put(ws, q, 6, float(L["yaw"]) if L["yaw"] else None, "0", INPUT_FILL)
            put(ws, q, 7, f'=IF(AND(ISNUMBER(F{q}),ISNUMBER(F{q - 1}),LEFT(A{q},10)="look_after"),F{q - 1}-F{q},"")' if i else "", "0")
            put(ws, q, 8, L["fps"], "0.0", INPUT_FILL)
        r = la + len(rows)
        put(ws, r, 1, "Look check", font=BOLD, fill=TOTAL_FILL)
        put(ws, r, 2, "the same 200 px mouse move: 200 x 0.15 deg x sensitivity", fill=NOTE_FILL)
        put(ws, r, 3, "expected at 1.00x / 1.20x", font=BOLD)
        put(ws, r, 4, "=200*0.15*1.0", "0.0")
        put(ws, r, 5, "=200*0.15*1.2", "0.0")
        r += 1
        file_link(put(ws, r, 1, None), SCRIPT_LOG, "exe log")
        file_link(put(ws, r, 2, None), INI, "ThermalSense.ini written by the menu")
        r += 2

    r, ta, tb = wb_v9.tests_block(ws, r, ctx, 340, 349, "M2 tests (T340-T349)")
    section(ws, r, 1, "Captures: before (v8 HUD) / after (decluttered game HUD, prompts, pause menu) and the exe frames")
    r = wb_v9._images(ws, r + 1, [
        ("Before: play_select (v8 HUD, debug blocks)", "docs/v9_notes/m2/shots/before_play_select.jpg"),
        ("After: play_select (game HUD, [E] prompt)", "docs/v9_notes/m2/shots/play_select.jpg"),
        ("Before: third person (v8 HUD)", "docs/v9_notes/m2/shots/before_play_third_person.jpg"),
        ("After: third person (soldier HUD)", "docs/v9_notes/m2/shots/play_third_person.jpg"),
        ("[E] Take control of UAV-1 DRONE (orbit view)", "docs/v9_notes/m2/shots/play_prompt_uav.jpg"),
        ("[E] Enter Barracks (third person)", "docs/v9_notes/m2/shots/play_prompt_door.jpg"),
        ("After E: inside the hall", "docs/v9_notes/m2/shots/play_hall_entered.jpg"),
        ("Pause menu: TRAVEL (field guide)", "docs/v9_notes/m2/shots/play_pause_menu.jpg"),
        ("Pause menu: TIME", "docs/v9_notes/m2/shots/play_pause_time.jpg"),
        ("Pause menu: SETTINGS", "docs/v9_notes/m2/shots/play_settings.jpg"),
        ("Exe: Tab x4 -> [E] Take control of MH-6 HELICOPTER", "docs/v9_notes/m2/exe_frames/02_tab_mechanic_prompt.jpg"),
        ("Exe: after E (helicopter, [E] Release)", "docs/v9_notes/m2/exe_frames/03_after_E.jpg"),
        ("Exe: after V (cockpit sight)", "docs/v9_notes/m2/exe_frames/04_after_V.jpg"),
        ("Exe: Esc x3 -> pause menu", "docs/v9_notes/m2/exe_frames/06_menu.jpg"),
        ("Exe: SETTINGS, sensitivity 1.20x", "docs/v9_notes/m2/exe_frames/08_sens_changed.jpg"),
        ("Exe: RESUME", "docs/v9_notes/m2/exe_frames/09_resumed.jpg"),
    ])
    widths(ws, [30, 30, 60, 30, 14, 12, 12, 10, 12, 12, 12, 12])
    finish(ws, 6, freeze="A4")
