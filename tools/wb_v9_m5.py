"""Sheet 34 of the workbook: v9 M5, the UAV as a game (chase / FPV / gimbal views, rotor blur, flight HUD, duty FSM).

Sources (all written by the program or its tests):
  build/test_results.json            T360-T369 (status looked up from sheet 10)
  docs/v9_notes/m5/duty.csv          ThermalSense --trace FILE 330 --time cycle --cycle-seconds 300 (FILE.duty.csv):
                                     the UAV duty timeline over a full day (a row per duty change and one per second)
  docs/v9_notes/m5/duty_dayops.csv   the same with --day-ops --time 10:30 (90 s): the daytime scramble
  docs/v9_notes/m5/captures/*.jpg    --capture --only play_uav_* (headless EPIC 1080p); before/*.jpg from 96782ee
  docs/v9_notes/m5/exe/*             ThermalSense.exe --input-script uav_night.txt / uav_day.txt (frames + logs)
Blue = imported, white = Excel formula, amber = design-note text. Missing inputs degrade to "pending: <what>" rows.
Hooked in by build_workbook.py like wb_v9 (S_V9M5 / V9M5_INFO / build_v9_m5).
"""
import csv
import os

from wb_common import (BOLD, INPUT_FILL, NOTE_FILL, PENDING_FILL, TOTAL_FILL, define_name, file_link, finish, header, path,
                       put, section, title, widths)
from wb_v6 import pending
from wb_v9 import _images, _kpi, tests_block

S_V9M5 = "34 v9 UAV View & Duty"
V9M5_INFO = ("v9 M5: UAV spring-arm chase camera (lower third, bank / tilt, collision), FPV / gimbal views (V), rotor blur discs, "
             "game flight HUD, duty state machine (docked by day, patrol by night), --day-ops; tests T360-T369")
INFO = V9M5_INFO   # the INFO line of this sheet (cover / index)

CONCEPTS = [
    ("Spring-arm chase camera",
     "Follow point and heading trail the drone through critically damped springs x'' = w^2 (target - x) - 2 w x' "
     "(exact step: (1 + w t) e^(-w t), no overshoot); the eye sits 'distance' behind and 'height' above the lagged follow "
     "point, never closer than 0.85 x distance", "UavChaseCam::update, springStep", "scene/UavView.cpp"),
    ("Lower-third framing",
     "The view axis is raised theta = atan(framing * tan(fov / 2)) above the camera->drone ray, so the drone projects to "
     "NDC y = -framing (0.40: inside the lower third, below the 1/3 line)", "framedLook", "scene/UavView.cpp"),
    ("Bank / pitch tilt",
     "Camera roll = -0.30 deg per deg/s of turn rate (into the turn, max 15), pitch tilt = 1.8 deg per m/s^2 of forward "
     "acceleration (max 8), both through a spring (w = 6)", "UavChaseCam (bank_, tilt_)", "scene/UavView.cpp"),
    ("Camera collision",
     "Sphere cast (probe 0.25 m, thin probe 0.08 m) from the drone to the wanted eye against the static solids, nearby vehicle "
     "parts and the walk space (ground, floors, walls): pull in at once, ease back out (tau 0.35 s)",
     "App::computeUavPose -> armCast / NearBox", "app/AppUav.cpp, app/AppPlay.cpp"),
    ("Rotor motion blur (alpha discs)",
     "blur = smoothstep over 8..22 rev/s of the rotor speed (40 rev/s x spool); blade opacity 1 - blur (plain alpha), "
     "disc opacity 0.32 x blur with two sweeping crescents and fine radial streaks; drawn in the translucent pass "
     "(depth test, no depth write) in the visible and the thermal view", "Scene::applyRotorBlur, partAlpha",
     "scene/Scene.cpp, rendering/ShaderSources.cpp"),
    ("HUD projection",
     "Pitch ladder rung at elevation e: offset = (H / 2) tan(e - pitch) / tan(fov / 2), rotated by -roll of the view; "
     "heading tape = compassTicks over 90 deg; tapes: offset = (v - value) * px per unit; targets / reticle via the view-"
     "projection matrix (projectMarker)", "pitchLadder, tapeMarks, drawUavGameHud", "scene/UavView.cpp, app/AppUav.cpp"),
    ("Finite-state machine (duty)",
     "DOCKED -> TAKE-OFF -> PATROL -> RETURN -> LANDING -> DOCKED, plus MANUAL while the player flies; duty wanted = "
     "searchlights allowed (sun < -2 deg) or rovers on the field or day ops; a pure function of state, dt and the flag",
     "Squad::dutyTransitions / dutyFlight", "ai/SquadDuty.cpp"),
]

FSM = [
    ("DOCKED", "duty wanted", "TAKE-OFF", "stagger 2.5 s per UAV index, rotors spool up 2 s"),
    ("TAKE-OFF", "height >= 4 m above the pad", "PATROL", "vertical climb <= 1.6 m/s"),
    ("TAKE-OFF", "duty no longer wanted", "LANDING (DOCKED on the pad)", ""),
    ("PATROL", "duty no longer wanted", "RETURN", "the squad (patrol / track / strike) flies it while PATROL"),
    ("RETURN", "< 0.35 m over the hover point (5 m above the pad), < 0.6 m/s", "LANDING", "flyTo with obstacle avoidance"),
    ("RETURN", "duty wanted again", "PATROL", ""),
    ("LANDING", "on the rails (height 0)", "DOCKED", "descent clamp(0.5 h, 0.25, 1.4) m/s: touch-down 0.25 m/s"),
    ("LANDING", "duty wanted again", "TAKE-OFF", "climbs from where it is"),
    ("any", "player takes control (key 0)", "MANUAL", "released: PATROL / TAKE-OFF if wanted, else RETURN"),
]


def _read_csv(rel):
    p = path(rel)
    if not os.path.exists(p):
        return None
    with open(p, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def _duty_timeline(ws, r, rows, label, src):
    """Every duty change of a FILE.duty.csv (imported) and per-UAV airborne share (formulas over the per-second rows)."""
    section(ws, r, 1, label)
    r += 1
    file_link(put(ws, r, 1, None), src, src)
    r += 1
    if not rows:
        return pending(ws, r, src, span=8) + 2
    header(ws, r, ["t (s)", "Hour", "Phase", "Day ops", "Waves", "Rovers alive", "UAV-1", "UAV-1 height (m)", "UAV-2",
                   "UAV-2 height (m)", "Change"], height=30)
    a = r + 1
    changes = [x for x in rows if x.get("change")]
    for i, x in enumerate(changes):
        q = a + i
        put(ws, q, 1, float(x["t"]), "0.00", INPUT_FILL)
        put(ws, q, 2, float(x["hour"]), "0.00", INPUT_FILL)
        put(ws, q, 3, x["phase"], fill=INPUT_FILL)
        put(ws, q, 4, int(x["day_ops"]), "0", INPUT_FILL)
        put(ws, q, 5, int(x["waves_allowed"]), "0", INPUT_FILL)
        put(ws, q, 6, int(x["alive"]), "0", INPUT_FILL)
        put(ws, q, 7, x["uav1_duty"], fill=INPUT_FILL, font=BOLD)
        put(ws, q, 8, float(x["uav1_height"]), "0.00", INPUT_FILL)
        put(ws, q, 9, x["uav2_duty"], fill=INPUT_FILL, font=BOLD)
        put(ws, q, 10, float(x["uav2_height"]), "0.00", INPUT_FILL)
        put(ws, q, 11, x["change"], fill=INPUT_FILL)
    b = a + len(changes) - 1
    r = b + 2
    # Per-second samples: airborne share by formula.
    section(ws, r, 1, "Per-second samples (imported) and the share of the run each UAV spent off its pad")
    r += 1
    header(ws, r, ["t (s)", "Hour", "UAV-1", "UAV-1 height", "UAV-2", "UAV-2 height", "UAV-1 vision", "UAV-2 vision"], height=24)
    s0 = r + 1
    samples = [x for x in rows if not x.get("change")]
    for i, x in enumerate(samples):
        q = s0 + i
        put(ws, q, 1, float(x["t"]), "0.0", INPUT_FILL)
        put(ws, q, 2, float(x["hour"]), "0.00", INPUT_FILL)
        put(ws, q, 3, x["uav1_duty"], fill=INPUT_FILL)
        put(ws, q, 4, float(x["uav1_height"]), "0.00", INPUT_FILL)
        put(ws, q, 5, x["uav2_duty"], fill=INPUT_FILL)
        put(ws, q, 6, float(x["uav2_height"]), "0.00", INPUT_FILL)
        put(ws, q, 7, int(x["uav1_vision"]), "0", INPUT_FILL)
        put(ws, q, 8, int(x["uav2_vision"]), "0", INPUT_FILL)
    s1 = s0 + max(len(samples), 1) - 1
    q = s1 + 1
    put(ws, q, 1, "Share", font=BOLD, fill=TOTAL_FILL)
    put(ws, q, 2, "off the pad / camera on", fill=TOTAL_FILL)
    put(ws, q, 4, f'=COUNTIF(D{s0}:D{s1},">0.02")/MAX(1,COUNT(D{s0}:D{s1}))', "0.0%", TOTAL_FILL, BOLD)
    put(ws, q, 6, f'=COUNTIF(F{s0}:F{s1},">0.02")/MAX(1,COUNT(F{s0}:F{s1}))', "0.0%", TOTAL_FILL, BOLD)
    put(ws, q, 7, f"=AVERAGE(G{s0}:G{s1})", "0.0%", TOTAL_FILL, BOLD)
    put(ws, q, 8, f"=AVERAGE(H{s0}:H{s1})", "0.0%", TOTAL_FILL, BOLD)
    return q + 2, (a, b), q


def build_v9_m5(wb, ctx):
    ws = wb[S_V9M5] if S_V9M5 in wb.sheetnames else wb.create_sheet(S_V9M5)
    title(ws, "34  v9 M5: UAV View & Duty",
          "The UAV plays like a drone game: a low spring-arm chase camera that lags without overshoot and keeps the drone on "
          "the lower third, banks into turns and pitches with acceleration and never enters a solid; an FPV nose camera and "
          "the gimbal (tracker) camera on V; spinning-propeller blur discs; a flight HUD (heading tape, pitch ladder, altitude "
          "and speed tapes, reticle on the gimbal aim, lock ring from the squad's lock time, rounds). The UAVs now have a duty "
          "cycle: docked on their pads by day, launch at dusk, land at dawn once the field is clear; --day-ops (Shift+F2) "
          "brings rover waves by day and scrambles them. Blue = imported, white = formulas, amber = design notes.", span=12)
    r = 5
    section(ws, r, 1, "Concepts")
    r += 1
    header(ws, r, ["Concept", "How it works here", "Code", "File"], height=20)
    for i, row in enumerate(CONCEPTS):
        for k, v in enumerate(row):
            put(ws, r + 1 + i, 1 + k, v, fill=NOTE_FILL, font=BOLD if k == 0 else None)
        ws.row_dimensions[r + 1 + i].height = 62
    r += len(CONCEPTS) + 2

    # Spring and framing calculator (formulas; the blue inputs are the UavChaseParams defaults).
    section(ws, r, 1, "Chase camera: critically damped step response and the lower-third angle (inputs = UavChaseParams)")
    r += 1
    inputs = [("omega follow (rad/s)", 10.0), ("vertical FOV (deg)", 64.0), ("framing (NDC below centre)", 0.40),
              ("speed (m/s)", 6.0)]
    i0 = r
    for k, (lab, v) in enumerate(inputs):
        put(ws, r + k, 1, lab, font=BOLD)
        put(ws, r + k, 2, v, "0.00", INPUT_FILL)
    r += len(inputs)
    put(ws, r, 1, "raise angle theta (deg)", font=BOLD)
    put(ws, r, 2, f"=DEGREES(ATAN(B{i0 + 2}*TAN(RADIANS(B{i0 + 1}/2))))", "0.00")
    put(ws, r + 1, 1, "steady trail 2 v / w (m)", font=BOLD)
    put(ws, r + 1, 2, f"=2*B{i0 + 3}/B{i0}", "0.00")
    put(ws, r + 2, 1, "drone screen row from the top (%)", font=BOLD)
    put(ws, r + 2, 2, f"=(1+B{i0 + 2})/2", "0.0%")
    define_name(wb, "M5ChaseOmega", S_V9M5, f"B{i0}")
    r0 = i0
    header(ws, r0 - 1, ["t (s)", "offset left (1 + w t) e^(-w t)", "per cent"], start_col=4, height=20)
    for k in range(13):
        q = r0 + k
        put(ws, q, 4, round(0.05 * k, 2), "0.00")
        put(ws, q, 5, f"=(1+$B${i0}*D{q})*EXP(-$B${i0}*D{q})", "0.0000")
        put(ws, q, 6, f"=E{q}", "0.0%")
    r = max(r + 4, r0 + 14)

    # Rotor blur curve.
    section(ws, r, 1, "Rotor blur: blade and disc opacity against the rotor speed (smoothstep over 8..22 rev/s)")
    r += 1
    header(ws, r, ["Spool", "rev/s", "t", "blur", "blade opacity", "disc opacity"], height=20)
    b0 = r + 1
    for k in range(11):
        q = b0 + k
        put(ws, q, 1, k / 10, "0.0")
        put(ws, q, 2, f"=40*A{q}", "0.0")
        put(ws, q, 3, f"=MAX(0,MIN(1,(B{q}-8)/(22-8)))", "0.00")
        put(ws, q, 4, f"=C{q}^2*(3-2*C{q})", "0.00")
        put(ws, q, 5, f"=IF(D{q}>0.001,1-D{q},1)", "0.00")
        put(ws, q, 6, f"=0.32*D{q}", "0.000")
    r = b0 + 12

    # HUD projection: pitch ladder.
    section(ws, r, 1, "HUD pitch ladder: rung offset (px above the centre) for a camera pitched down 20 deg, FOV 64, H 1080")
    r += 1
    header(ws, r, ["Rung (deg)", "Camera pitch", "FOV", "H (px)", "Offset (px)"], height=20)
    l0 = r + 1
    for k, deg in enumerate([-20, -10, 0, 10, 20]):
        q = l0 + k
        put(ws, q, 1, deg, "0")
        put(ws, q, 2, -20, "0", INPUT_FILL)
        put(ws, q, 3, 64, "0", INPUT_FILL)
        put(ws, q, 4, 1080, "0", INPUT_FILL)
        put(ws, q, 5, f"=D{q}/2*TAN(RADIANS(A{q}-B{q}))/TAN(RADIANS(C{q}/2))", "0.0")
    r = l0 + 6

    section(ws, r, 1, "Duty state machine (ai/SquadDuty.cpp)")
    r += 1
    header(ws, r, ["State", "Condition", "Next", "Motion / note"], height=20)
    for i, row in enumerate(FSM):
        for k, v in enumerate(row):
            put(ws, r + 1 + i, 1 + k, v, fill=NOTE_FILL, font=BOLD if k in (0, 2) else None)
    r += len(FSM) + 2

    r, (ca, cb), share = _duty_timeline(ws, r, _read_csv("docs/v9_notes/m5/duty.csv"),
                                        "Duty timeline over a full day: --trace 330 s, --time cycle, --cycle-seconds 300, seed 1",
                                        "docs/v9_notes/m5/duty.csv")
    r, _, _ = _duty_timeline(ws, r, _read_csv("docs/v9_notes/m5/duty_dayops.csv"),
                             "Day ops at 10:30 (--day-ops, 90 s): rover waves by day, both UAVs scramble",
                             "docs/v9_notes/m5/duty_dayops.csv")

    r, ta, tb = tests_block(ws, r, ctx, 360, 369, "M5 tests (T360-T369)")
    _kpi(ws, [("M5 tests", f"=ROWS(E{ta}:E{tb})", "0"), ("Passed", f'=COUNTIF(E{ta}:E{tb},"PASS")', "0"),
              ("Duty changes (day)", f"=COUNTA(K{ca}:K{cb})", "0"), ("UAV-1 off the pad", f"=D{share}", "0.0%")])

    section(ws, r, 1, "Captures (headless EPIC 1080p on the RTX 4070): before (96782ee) / after")
    r = _images(ws, r + 1, [("Before: possessed UAV (thermal sight)", "docs/v9_notes/m5/before/possess_uav_manual.jpg"),
                            ("After: same shot (gimbal view kept)", "docs/v9_notes/m5/captures/possess_uav_manual.jpg"),
                            ("Before: UAV close-up (blades + disc always)", "docs/v9_notes/m5/before/uav_closeup_visible.jpg"),
                            ("After: UAV close-up (blur only, pads below)", "docs/v9_notes/m5/captures/uav_closeup_visible.jpg"),
                            ("play_uav_docked_day: both UAVs on their pads", "docs/v9_notes/m5/captures/play_uav_docked_day.jpg"),
                            ("play_uav_rotor_spinup: blade fading, disc appearing", "docs/v9_notes/m5/captures/play_uav_rotor_spinup.jpg"),
                            ("play_uav_rotor_blur: full speed", "docs/v9_notes/m5/captures/play_uav_rotor_blur.jpg"),
                            ("play_uav_chase: right turn at night (bank)", "docs/v9_notes/m5/captures/play_uav_chase.jpg"),
                            ("play_uav_fpv: nose camera", "docs/v9_notes/m5/captures/play_uav_fpv.jpg"),
                            ("play_uav_gimbal: thermal sight", "docs/v9_notes/m5/captures/play_uav_gimbal.jpg"),
                            ("play_uav_hud: lock ring on a rover", "docs/v9_notes/m5/captures/play_uav_hud.jpg"),
                            ("play_uav_dayops_chase: day ops", "docs/v9_notes/m5/captures/play_uav_dayops_chase.jpg")])
    section(ws, r, 1, "ThermalSense.exe (native NVIDIA, --input-script docs/v9_notes/m5/exe/uav_night.txt / uav_day.txt)")
    r = _images(ws, r + 1, [("Exe night: chase in a right turn", "docs/v9_notes/m5/exe/n1_chase_right_turn.jpg"),
                            ("Exe night: chase + HUD, hovering", "docs/v9_notes/m5/exe/n6_chase_hud.jpg"),
                            ("Exe day: UAV-1 docked, chase", "docs/v9_notes/m5/exe/d0_docked_chase.jpg"),
                            ("Exe day ops: scramble + HUD", "docs/v9_notes/m5/exe/d5_dayops_hud.jpg")])
    put(ws, r, 1, "Exe logs", font=BOLD)
    file_link(put(ws, r, 2, None), "docs/v9_notes/m5/exe/uav_night.log", "uav_night.log")
    file_link(put(ws, r, 3, None), "docs/v9_notes/m5/exe/uav_day.log", "uav_day.log")
    if not os.path.exists(path("docs/v9_notes/m5/exe/uav_night.log")):
        put(ws, r, 4, "pending: exe logs", fill=PENDING_FILL)
    widths(ws, [30, 46, 30, 26, 22, 14, 14, 14, 14, 14, 36, 12, 12, 12])
    finish(ws, 6, freeze="A4")
