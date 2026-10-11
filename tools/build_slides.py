#!/usr/bin/env python3
"""Build report/ThermalSense_Presentation.pptx (22 slides, 16:9, dark night-ops theme).

Run:  PATH=$HOME/.local/bin:$PATH uv run --with python-pptx --with pillow python tools/build_slides.py
Re-runnable: every picture is looked up by name in SOURCES (report/figures_v7 first), so re-shot figures are
picked up automatically; the picture chosen for each slot is listed in FIG below (swap names there).
The demo video slide embeds report/demo_v7.mp4 when it exists, otherwise it shows a TODO placeholder.
Also writes previews from the same layout records (no LibreOffice needed): build/slides_preview.png (contact
sheet) and build/slides_preview/slide_NN.png, and warns when a text box overflows (measured with DejaVu Sans,
which is wider than Calibri, so the check is conservative).
"""
import os, re, sys, tempfile, json
from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REP = os.path.join(ROOT, "report")
OUT = os.path.join(REP, "ThermalSense_Presentation.pptx")
VIDEO = os.path.join(REP, "video", "thermalsense_final_slide.mp4")   # the final film at 960x540, 18 MB (tools/report/gen_film3.py)
PREVIEW = os.path.join(ROOT, "build", "slides_preview.png")
PREVIEW_DIR = os.path.join(ROOT, "build", "slides_preview")
TMP = tempfile.mkdtemp(prefix="ts_slides_")

BG, PANEL = RGBColor(0x08, 0x0A, 0x0D), RGBColor(0x14, 0x18, 0x1E)
AMBER, WHITE, GREY = RGBColor(0xFF, 0x9E, 0x1F), RGBColor(0xFF, 0xFF, 0xFF), RGBColor(0xA8, 0xB0, 0xBA)
CYAN, DIM = RGBColor(0x6F, 0xD6, 0xE8), RGBColor(0x2A, 0x31, 0x3A)
W, H = 13.333, 7.5
FONT, MONO = "Times New Roman", "Consolas"

# Picture search order (first match wins; .png before .jpg): frames of the final film, then the report captures.
SOURCES = [("report/figures_final", "final film frame"), ("report/figures_v7", "v7"), ("report/figures_v6", "v6"), ("report/figures_v5", "v5"),
           ("report/figures_lab", "lab render")]

# One entry per picture slot: preferred name first, substitutes after. Swap names here after new captures.
FIG = {
    # v1-v5 slides
    "title": ["game_overview", "base_overview", "cine_establishing", "scene_visible"],
    "history_a": ["plan_topdown"], "history_b": ["day_overview", "wave_banner"],
    "prim_a": ["shading_sphere_compare"], "prim_b": ["shading_uav_wire"], "prim_c": ["materials_pbr_grid"],
    "camp_a": ["camp_under_nets", "base_overview"], "camp_b": ["camp_sam", "base_gate"], "camp_c": ["camp_mlrs", "motor_pool"],
    "camp_d": ["bunker_cic", "iso_bunker_exploded"], "camp_e": ["bunker_tunnel", "bunker_door", "walk_bunker"],
    "ctl_a": ["tank_closeup"], "ctl_b": ["heli_takeoff", "helipad"], "ctl_c": ["possess_sam", "camp_sam"],
    "ctl_d": ["possess_mlrs", "camp_mlrs"], "ctl_e": ["radar", "aa_gun_burst"],
    "pipe_a": ["base_overview"], "pipe_b": ["thermal_fpv"],
    "loop_a": ["thermal_fpv_tracks", "thermal_fpv_strike", "thermal_fpv"], "loop_b": ["combined_strike", "explosion_mid"],
    "loop_c": ["tactical_map", "kill_banner"],
    # v6
    "day_main": ["sky_sunset"], "day_1": ["day_overview"], "day_2": ["dusk_shift_change"], "day_3": ["night_searchlights"],
    "day_4": ["dawn_withdraw"],
    "sol_axes": ["soldier_axes", "soldier_front"], "sol_route": ["day_patrol_spline"], "sol_fp": ["soldier_possess_fp", "day_mechanic"],
    "routes": ["plan_routes"],
    "cv_lab": ["lab_cv_noise"], "cv_day": ["day_thermal_fpv_persons"],
    # v7 game camera
    "game_main": ["game_soldier_fire", "game_soldier_fp"], "game_picker": ["game_picker"], "game_heli": ["game_heli_cockpit"],
    # v7 ray tracing
    "rt_whitted": ["raytrace_mode_whitted"], "rt_path": ["raytrace_mode_path"], "rt_light": ["raytrace_photons", "raytrace_mode_light"],
    "rt_ssr": ["raytrace_ssr"], "rt_tree": ["raytrace_tree_pixel", "raytrace_tree"],
    # v7 Frame Explorer
    "fx_montage": ["explorer_montage"], "fx_interp": ["explorer_interp_affine_perspective", "explorer_stage_05_interp"],
    "fx_scan": ["explorer_raster_scan", "explorer_stage_04_raster"],
    "fx_reflect": ["explorer_reflection_ssr_vs_rt", "explorer_stage_10_reflect"],
    "fx_diff": ["explorer_reflection_rt_difference", "explorer_reflection_mask"],
    # results, video
    # final night: vehicles, strikes, guards (frames of the final film)
    "veh_a": ["final_tank_walk"], "veh_b": ["final_tank_drive"], "veh_c": ["final_tank_sight"], "veh_d": ["final_tank_exit"],
    "str_a": ["final_single"], "str_b": ["final_combined"], "str_c": ["final_guards"], "str_d": ["final_flir"],
    "mm_a": ["final_alert_sam"],
    "results": ["game_free_fly", "game_missile_impact", "kill_banner"],
    "video": ["final_combined", "game_tank_commander", "game_soldier_fire", "raytrace_mode_path", "combined_strike", "game_overview"],
}

# GPU timings from docs/v7_notes/wp-h.md "Performance" (RTX 4070 via Mesa d3d12, 1280x720 output, EPIC, glFinish).
RT_TIMES = [  # mode (internal resolution), day 16:30, night 23:00, RT pass alone, bar value (day ms), is RT
    ("Raster (v7, EPIC)", "11.7 ms", "12.7 ms", "-", 11.7, False),
    ("Hybrid RT (1280x720)", "20.3 ms", "20.5 ms", "9.5-10 ms", 20.3, True),
    ("Whitted (960x540)", "25.7 ms", "27.7 ms", "20-22 ms", 25.7, True),
    ("Path (640x360, 1 spp)", "21.9 ms", "28.6 ms", "18-24 ms", 21.9, True),
    ("Light (640x360, 256K paths)", "39.0 ms", "30.7 ms", "26-33 ms", 39.0, True),
]

def stats():
    d = {}
    for f in ("gen_stats.tex", "gen_v6.tex"):
        p = os.path.join(REP, "sections", f)
        if os.path.exists(p):
            for m in re.finditer(r"\\newcommand\{\\(\w+)\}\{([^{}%]*)\}", open(p).read()):
                d[m.group(1)] = m.group(2).replace("\\,", ",")
    return d
S = stats()
def n(k, default="?"): return S.get(k, default)

used = []  # (slide, slot, path, source)
def find_img(slide, slot):
    names = FIG[slot]
    for nm in names:
        for d, tag in SOURCES:
            for ext in (".png", ".jpg"):
                p = os.path.join(ROOT, d, nm + ext)
                if os.path.exists(p):
                    if nm != names[0]: tag += f" (substitute for {names[0]})"
                    used.append((slide, slot, f"{d}/{nm}{ext}", tag))
                    return p
    used.append((slide, slot, None, "MISSING " + names[0]))
    return None

_cache = {}
def jpeg(path, crop=None):
    key = (path, crop)
    if key in _cache: return _cache[key]
    im = Image.open(path).convert("RGB")
    if crop:
        l, t, r, b = crop
        im = im.crop((round(l * im.width), round(t * im.height), round(r * im.width), round(b * im.height)))
    if im.width > 1600: im = im.resize((1600, round(im.height * 1600 / im.width)), Image.LANCZOS)
    out = os.path.join(TMP, f"img{len(_cache)}.jpg")
    im.save(out, "JPEG", quality=90)
    _cache[key] = out
    return out

class Slide:
    """Records every shape so the preview uses identical coordinates."""
    def __init__(self, prs, notes):
        self.s = prs.slides.add_slide(prs.slide_layouts[6])
        self.rec = []
        self.name = ""
        bg = self.s.background.fill; bg.solid(); bg.fore_color.rgb = BG
        self.s.notes_slide.notes_text_frame.text = notes
        wc = len(notes.split())
        assert 60 <= wc <= 90, f"notes {wc} words: {notes[:50]}"
    def rect(self, x, y, w, h, color=PANEL, line=None, shape=MSO_SHAPE.RECTANGLE):
        sh = self.s.shapes.add_shape(shape, Inches(x), Inches(y), Inches(w), Inches(h))
        sh.fill.solid(); sh.fill.fore_color.rgb = color
        if line: sh.line.color.rgb = line; sh.line.width = Pt(1)
        else: sh.line.fill.background()
        kind = {MSO_SHAPE.OVAL: "oval", MSO_SHAPE.ISOSCELES_TRIANGLE: "tri"}.get(shape, "rect")
        self.rec.append((kind, x, y, w, h, color, line)); return sh
    def text(self, x, y, w, h, text, size=20, color=WHITE, bold=False, align=PP_ALIGN.LEFT, anchor=MSO_ANCHOR.TOP, font=FONT):
        tb = self.s.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
        tf = tb.text_frame; tf.word_wrap = True; tf.vertical_anchor = anchor
        tf.margin_left = tf.margin_right = Inches(0.05)
        paras = text if isinstance(text, list) else [text]
        for i, t in enumerate(paras):
            p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
            p.alignment = align; p.space_after = Pt(size * 0.35)
            r = p.add_run(); r.text = t
            r.font.size = Pt(size); r.font.bold = bold; r.font.name = font; r.font.color.rgb = color
        self.rec.append(("text", x, y, w, h, paras, size, color, bold, align, anchor, font)); return tb
    def arrow(self, x, y, w, h, down=False):
        sh = self.s.shapes.add_shape(MSO_SHAPE.DOWN_ARROW if down else MSO_SHAPE.RIGHT_ARROW, Inches(x), Inches(y), Inches(w), Inches(h))
        sh.fill.solid(); sh.fill.fore_color.rgb = AMBER; sh.line.fill.background()
        self.rec.append(("rect", x, y, w, h, AMBER, None))
    def title(self, t, kicker=None):
        self.name = t
        self.rect(0, 0, 0.12, H, AMBER)
        if kicker: self.text(0.5, 0.28, 8, 0.35, kicker.upper(), 13, AMBER, True)
        self.text(0.5, 0.58, 12.3, 0.85, t, 34, WHITE, True)
    def image(self, path, x, y, w, h, caption=None, fit="cover", crop=None):
        """Place a picture in the box (x, y, w, h): 'cover' crops it to fill, 'contain' keeps it whole (centred)."""
        if path is None:
            self.rect(x, y, w, h, PANEL, GREY)
            self.text(x, y + h / 2 - 0.3, w, 0.6, "figure pending", 16, GREY, align=PP_ALIGN.CENTER)
            self.rec.append(("img", x, y, w, h, None)); return
        j = jpeg(path, crop); im = Image.open(j)
        ar, br = im.width / im.height, w / h
        if fit == "contain":
            if ar > br: nh = w / ar; y += (h - nh) / 2; h = nh
            else: nw = h * ar; x += (w - nw) / 2; w = nw
            br = ar
        pic = self.s.shapes.add_picture(j, Inches(x), Inches(y), Inches(w), Inches(h))
        if abs(ar - br) > 1e-3:
            if ar > br:
                c = (1 - br / ar) / 2; pic.crop_left = pic.crop_right = c
            else:
                c = (1 - ar / br) / 2; pic.crop_top = pic.crop_bottom = c
        self.rec.append(("img", x, y, w, h, j))
        if caption:
            self.rect(x, y + h - 0.38, w, 0.38, BG)
            self.text(x, y + h - 0.38, w, 0.38, caption, 12, AMBER, anchor=MSO_ANCHOR.MIDDLE)
        return (x, y, w, h)

def bullets(sl, x, y, w, items, size=20, gap=0.62):
    for i, it in enumerate(items):
        sl.rect(x, y + i * gap + size / 72 * 0.42, 0.11, 0.11, AMBER)
        sl.text(x + 0.25, y + i * gap, w - 0.25, gap, it, size)

def stat(sl, x, y, w, big, small, h=1.15, bigsize=30):
    sl.rect(x, y, w, h, PANEL)
    sl.text(x, y + 0.02, w, h * 0.57, big, bigsize, AMBER, True, PP_ALIGN.CENTER)
    sl.text(x, y + h * 0.59, w, h * 0.38, small, 13, GREY, False, PP_ALIGN.CENTER)

def formula(sl, x, y, w, h, lines, size=15, head=None):
    """Key-formula panel (cyan monospace on a panel with a cyan rule), like the Frame Explorer's formula box."""
    sl.rect(x, y, w, h, PANEL)
    sl.rect(x, y, 0.06, h, CYAN)
    yy = y + 0.08
    if head:
        sl.text(x + 0.15, yy, w - 0.2, 0.3, head.upper(), 11, GREY, True); yy += 0.3
    sl.text(x + 0.15, yy, w - 0.2, h - (yy - y) - 0.05, lines, size, CYAN, font=MONO)

def keychip(sl, x, y, w, h, key, desc):
    sl.rect(x, y, w, h, PANEL, DIM)
    sl.text(x, y + 0.06, w, 0.42, key, 17, AMBER, True, PP_ALIGN.CENTER)
    sl.text(x + 0.05, y + 0.5, w - 0.1, h - 0.52, desc, 12, GREY, False, PP_ALIGN.CENTER)

def hline(sl, x, y, w, color=GREY, t=0.025): sl.rect(x, y - t / 2, w, t, color)
def vline(sl, x, y, h, color=GREY, t=0.025): sl.rect(x - t / 2, y, t, h, color)

def build():
    prs = Presentation(); prs.slide_width = Inches(W); prs.slide_height = Inches(H)
    slides = []
    def k(label): return f"{len(slides) + 1}  |  {label}"

    # 1 Title
    sl = Slide(prs, "Good morning. This is ThermalSense, my computer graphics lab project: autonomous drones defending a hidden army base, now a playable game. "
        "Everything you will see, the camp, the soldiers, the vehicles and the rover intruders, is assembled from sixteen primitive shapes that the program generates itself. "
        "Every lighting equation is written by hand. Version seven adds a day and night cycle, first person play, real ray tracing and a frame explorer. I end with the demonstration video.")
    sl.image(find_img(1, "title"), 0, 0, W, H)
    sl.rect(0, 3.7, W, 3.8, BG)
    sl.rect(0.6, 3.95, 1.4, 0.07, AMBER)
    sl.text(0.6, 4.1, 12, 1.0, "ThermalSense", 54, WHITE, True)
    sl.text(0.6, 5.0, 12, 0.7, "Autonomous UAV Patrol of a Military Base", 28, AMBER)
    sl.text(0.6, 5.95, 12, 0.5, "Md. Shakhoyat Rahman Shujon  |  Roll 2107104", 20, WHITE)
    sl.text(0.6, 6.4, 12, 0.5, "Computer Graphics Lab  |  C++17, OpenGL 3.3 (+ 4.3 compute for ray tracing)  |  v7", 18, GREY)
    slides.append(sl)

    # 2 Outline (numbers checked against the slide titles at the end of build)
    OUTLINE = [("Foundations (v1-v5)", [(3, "Goal and history"), (4, "Building with primitives"), (5, "Camp and bunker"),
                                        (6, "Controllable objects"), (7, "Rendering pipeline"), (8, "Autonomy, game loop")]),
               ("v6: a living base", [(9, "Day and night cycle"), (10, "Soldiers and IK gait"), (11, "Random rover routes"),
                                      (12, "Vision under noise")]),
               ("v7: play, rays, frames", [(13, "Game camera, possession"), (14, "Whitted, path, light tracing"), (15, "BVH and SAH"),
                                           (16, "Ray-tree debugger"), (17, "Ray tracing timings"), (18, "Frame Explorer: 16 stages"),
                                           (19, "Colour interpolation"), (20, "Reflection: SSR vs RT"), (21, "Vehicles: in, fire, out"),
                                           (22, "Single and combined strike"), (23, "Hand-written vector maths"), (24, "Results"), (25, "Demo video")])]
    sl = Slide(prs, "Here is the outline in three parts. First the foundations from versions one to five: the goal, how every object is assembled from primitives, "
        "the camp and bunker, the controllable vehicles, the rendering pipeline and the autonomous game loop. "
        "Second, version six makes the base live: a day and night cycle, eight soldiers, random rover routes and vision under sensor noise. "
        "Third, version seven: playing from any point of view, ray tracing and the Frame Explorer.")
    sl.title("Outline", "ThermalSense")
    colx = [0.5, 4.75, 9.0]
    for c, (head, items) in enumerate(OUTLINE):
        x = colx[c]
        sl.rect(x, 1.6, 3.95, 0.06, AMBER)
        sl.text(x, 1.72, 3.95, 0.45, head, 20, AMBER, True)
        gap = min(0.49, 4.9 / len(items))
        fs = 17 if gap > 0.45 else 14
        for i, (num, it) in enumerate(items):
            y = 2.3 + i * gap
            sl.text(x, y, 0.6, gap - 0.04, f"{num}", fs, AMBER if c else GREY, True)
            sl.text(x + 0.6, y, 3.35, gap - 0.04, it, fs, WHITE)
    slides.append(sl)

    # 3 Goal and history
    sl = Slide(prs, "The goal: drones that find and track intruders using only their own thermal cameras, in a believable base built entirely from primitives. "
        "The requirements grew in stages: the lab sheet gave canonical primitives and M equals T R S, the proposal added shading and the Z-buffer, "
        "version two PBR and tracking, three the RTX pipeline, four the base and bunker, five the game. Six adds day and night, seven play, ray tracing and the explorer.")
    sl.title("Goal and requirements history", k("Goal"))
    stages = [("Lab sheet", "Unit cube, sphere, cylinder; M = T·R·S; VAO/VBO/EBO"),
              ("Proposal", "Flat / Gouraud / Phong, 3 lights, Z-buffer, thermal map"),
              ("v2", "PBR, shadows, SSAO, fog; thermal tracker; MicroMouse rover"),
              ("v3", "RTX 4070 EPIC preset, first-person FLIR, TAA, SSR"),
              ("v4", "Military base, walkable bunker, controllable vehicles"),
              ("v5", "Night Watch: 2 UAVs, CV tracker, waves, score, sound"),
              ("v6", "Day/night cycle, 8 soldiers, random routes, sensor noise"),
              ("v7", "First person anywhere, ray tracing, Frame Explorer")]
    cw, gap = 1.44, 0.1
    for i, (a, b) in enumerate(stages):
        x = 0.5 + i * (cw + gap)
        new = i >= 6
        sl.rect(x, 1.75, cw, 0.08, AMBER if new else GREY)
        sl.text(x, 1.9, cw, 0.5, a, 18, AMBER if new else WHITE, True)
        sl.text(x, 2.45, cw, 1.9, b, 14, WHITE if new else GREY)
    sl.image(find_img(3, "history_a"), 0.5, 4.45, 6.0, 2.8, "v5: top-down plan, night")
    sl.image(find_img(3, "history_b"), 6.83, 4.45, 6.0, 2.8, "v6: the same camp by day")
    slides.append(sl)

    # 4 Primitives
    sl = Slide(prs, "Every visible solid is one part: a unit cube, sphere or cylinder drawn with its own matrix M equals T times R times S. "
        f"Parts are grouped in a hierarchy, so a track link moves with its tank. The scene has {n('nLeaves')} primitive leaves in total. "
        f"For example the bunker has {n('partsBunker')} parts, the tank {n('partsTank')}, the radar {n('partsRadar')}, the UAV {n('partsUav')} and the helicopter {n('partsHeli')}. Cones are just cylinders with a different top radius.")
    sl.title("Building with primitives", k("Construction"))
    sl.image(find_img(4, "prim_a"), 0.5, 1.7, 6.1, 2.3)
    sl.image(find_img(4, "prim_b"), 0.5, 4.15, 3.0, 3.0)
    sl.image(find_img(4, "prim_c"), 3.6, 4.15, 3.0, 3.0)
    sl.text(7.0, 1.75, 5.8, 0.6, "cube + sphere + cylinder → objects", 20, AMBER, True)
    sl.text(7.0, 2.35, 5.8, 0.6, "M = T · R · S per part, parent × child", 20)
    for i, (big, small) in enumerate([(n("nLeaves"), "primitive leaves"), (n("partsBunker"), "bunker parts"), (n("partsTank"), "tank parts"),
                                      (n("soldierParts"), "parts per soldier"), (n("partsUav"), "UAV parts"), (n("partsBase"), "base apron parts")]):
        stat(sl, 7.0 + (i % 2) * 2.95, 3.2 + (i // 2) * 1.3, 2.8, big, small)
    slides.append(sl)

    # 5 Camp and bunker
    sl = Slide(prs, "The base is a hidden army camp. It keeps light discipline: dim red and blue-green tactical lamps, blacked-out windows, so at night it reads only by moonlight, searchlights and explosions. "
        "Camo nets cover the vehicles, SAM and MLRS launchers and the bunker entrance, and tanks sit in earth revetments. "
        "Below ground is the walkable bunker with blast doors, a tunnel, command room, bunk room and generator room, all with collision volumes.")
    sl.title("Hidden army camp and underground bunker", k("Environment"))
    sl.image(find_img(5, "camp_a"), 0.5, 1.6, 6.1, 3.4, "Camp under camo nets, light discipline")
    sl.image(find_img(5, "camp_b"), 6.73, 1.6, 3.0, 3.4, "SAM launcher")
    sl.image(find_img(5, "camp_c"), 9.83, 1.6, 3.0, 3.4, "MLRS truck")
    sl.image(find_img(5, "camp_d"), 0.5, 5.15, 4.0, 2.1, "Command room (walkable)")
    sl.image(find_img(5, "camp_e"), 4.65, 5.15, 4.0, 2.1, "Tunnel and blast door")
    stat(sl, 8.83, 5.15, 1.95, n("partsBase"), "base parts")
    stat(sl, 10.88, 5.15, 1.95, n("partsBunker"), "bunker parts")
    slides.append(sl)

    # 6 Controllable
    sl = Slide(prs, "Nine vehicles and devices can be possessed with the number keys or the bracket keys: the UAV, tank, helicopter, AA gun, radar, blast doors, SAM launcher, MLRS truck and gate barrier. "
        "Each has real hierarchical motion: tank track links, rotor flapping with a spring-damper attitude, Gatling spin-up, a turntable with elevating canisters, eased sliding doors. "
        f"With the eight soldiers and the sky clock from version six that makes {n('nPossessable')}. Backspace releases control.")
    sl.title("Controllable objects and complex motion", k("Control"))
    sl.image(find_img(6, "ctl_a"), 0.5, 1.6, 4.0, 2.55, "Tank: 80 track links, turret")
    sl.image(find_img(6, "ctl_b"), 4.65, 1.6, 4.0, 2.55, "Helicopter: rotor flapping")
    sl.image(find_img(6, "ctl_c"), 8.8, 1.6, 4.0, 2.55, "SAM: turntable + canisters")
    sl.image(find_img(6, "ctl_d"), 0.5, 4.3, 4.0, 2.0, "MLRS: 6x2 tube pod")
    sl.image(find_img(6, "ctl_e"), 4.65, 4.3, 4.0, 2.0, "Radar, AA gun, doors, barrier")
    sl.rect(8.8, 4.3, 4.0, 2.0, PANEL)
    sl.text(8.9, 4.35, 3.8, 1.9, ["Key map", "5 tank  6 heli  7 AA gun  8 radar", "9 doors  0 UAV   [ ] SAM / MLRS / gate",
                                  "F3 soldier   Backspace release", "WASD move  arrows aim  Enter fire"], 15, WHITE)
    sl.text(0.5, 6.5, 12.3, 0.6, f"{n('nPossessable')} possessable: 9 vehicles and devices + 8 soldiers + the SKY clock (v6)", 18, AMBER)
    slides.append(sl)

    # 7 Pipeline
    sl = Slide(prs, "The frame is rendered in linear light into an HDR target. GGX Cook-Torrance PBR is the base, with cascaded shadows, screen-space ambient occlusion, volumetric fog with light shafts, "
        "temporal anti-aliasing, screen-space reflections in puddles and bloom, finished by an ACES tone curve. The thermal view is a sensor model: automatic gain, noise, shimmer and a FLIR style HUD. "
        "The preset is chosen automatically, EPIC on the RTX 4070.")
    sl.title("Rendering pipeline", k("Rendering"))
    sl.image(find_img(7, "pipe_a"), 0.5, 1.6, 6.1, 3.3, "Visible: PBR, shadows, fog")
    sl.image(find_img(7, "pipe_b"), 6.73, 1.6, 6.1, 3.3, "Thermal sensor view")
    chips = ["PBR GGX", "Shadows", "SSAO", "Fog", "TAA", "SSR", "Bloom", "Thermal sensor"]
    for i, c in enumerate(chips):
        x, y = 0.5 + (i % 4) * 3.1, 5.2 + (i // 4) * 1.0
        sl.rect(x, y, 2.95, 0.8, PANEL, AMBER)
        sl.text(x, y, 2.95, 0.8, c, 22, WHITE, True, PP_ALIGN.CENTER, MSO_ANCHOR.MIDDLE)
    slides.append(sl)

    # 8 Game loop
    sl = Slide(prs, "Here is the game loop. Each of the two UAVs runs a computer vision pipeline on its own thermal image: threshold, despeckle, union-find connected components and moments. "
        "A shared Kalman and Hungarian tracker, with two-view triangulation, keeps IDs and velocities. The best-placed drone becomes lead, the other the wingman, and a time-on-target strike lands both rounds together. "
        "Four to five hits kill an armoured rover. Kills feed waves, score and base integrity.")
    sl.title("Autonomy and the game loop", k("Simulation"))
    sl.image(find_img(8, "loop_a"), 0.5, 1.5, 4.0, 2.1, "CV tracks, IDs, LOCK")
    sl.image(find_img(8, "loop_b"), 4.65, 1.5, 4.0, 2.1, "Combined strike")
    sl.image(find_img(8, "loop_c"), 8.8, 1.5, 4.0, 2.1, "Tactical map / kill banner")
    rows = [[("2 UAVs", "thermal cameras"), ("CV pipeline", "threshold, despeckle, union-find, moments"),
             ("Shared tracker", "Kalman + Hungarian"), ("Two-view triangulation", "ray midpoint")],
            [("Lead / wing", "roles, 90 deg geometry"), ("Time-on-target", "combined strike"),
             ("4-5 hits = 1 kill", "armoured rovers"), ("Waves, score, base", "integrity: the game loop")]]
    bw, gp = 2.8, 0.37
    for r, row in enumerate(rows):
        y = 3.85 + r * 1.65
        for i, (t, sub) in enumerate(row):
            x = 0.5 + i * (bw + gp)
            sl.rect(x, y, bw, 1.2, PANEL, AMBER if (r, i) in ((0, 1), (1, 1)) else GREY)
            sl.text(x, y + 0.1, bw, 0.5, t, 16, WHITE, True, PP_ALIGN.CENTER)
            sl.text(x, y + 0.62, bw, 0.55, sub, 12, GREY, False, PP_ALIGN.CENTER)
            if i < 3: sl.arrow(x + bw + 0.04, y + 0.45, gp - 0.08, 0.3)
    sl.arrow(11.2, 5.08, 0.4, 0.38, down=True)
    sl.text(0.5, 7.0, 12.3, 0.4, "Rovers: MicroMouse flood fill on a cost map (distance + exposure + noise) so paths are random each wave", 13, GREY)
    slides.append(sl)

    # 9 Day / night (v6)
    sl = Slide(prs, f"Version six adds time. The sun position comes from the solar elevation formula at latitude {n('dcLat')} degrees: sunrise about {n('dcSunrise')}, sunset {n('dcSunset')}. "
        "The key light switches between sun and moon, reddened through the air mass near the horizon; lamps fade out by day and searchlights only run at night. "
        "A drifting cloud layer also casts moving shadows. Intruders attack only at night and withdraw at dawn. F2, comma, period and slash control the clock.")
    sl.title("Day and night cycle", k("v6  |  Time of day"))
    sl.image(find_img(9, "day_main"), 0.5, 1.6, 6.4, 3.65, "Sunset: air-mass reddened sun, twilight sky")
    formula(sl, 7.1, 1.6, 5.73, 0.95, ["sin e = sin φ sin δ + cos φ cos δ cos H", f"φ = {n('dcLat')}°   δ = {n('dcDecl')}°   H = 15° × (t − 12 h)"], 15)
    bullets(sl, 7.1, 2.7, 5.73, ["Key light: sun (e > −1°) or the v5 moon; night preset = v5 bit for bit",
                                 "Lamps fade by day; searchlights below −2°; cloud shadows sweep the camp",
                                 "F2 auto / manual   , . scrub   / next phase"], 15, 0.5)
    for i, (big, small) in enumerate([(n("dcSunrise"), "sunrise"), (n("dcSunset"), "sunset"), (n("dcNoonElev") + "°", "noon elevation"),
                                      (n("dcCycle").split(".")[0] + " s", "auto cycle")]):
        stat(sl, 7.1 + i * 1.46, 4.2, 1.35, big, small, 1.05, 24)
    cap = ["Day: UAVs patrol, no waves", "Dusk: shift change", "Night: searchlights, waves", "Dawn: intruders withdraw"]
    for i, slot in enumerate(["day_1", "day_2", "day_3", "day_4"]):
        sl.image(find_img(9, slot), 0.5 + i * 3.11, 5.42, 2.98, 1.83, cap[i])
    slides.append(sl)

    # 10 Soldiers (v6)
    sl = Slide(prs, f"Eight soldiers live on the base, each built from {n('soldierParts')} primitives in a hierarchy seven levels deep. "
        "They walk centripetal Catmull-Rom splines, which never overshoot or loop, and slow down in tight bends. "
        "The gait is forward kinematics with two-link inverse kinematics for the legs, so the feet stay planted on the ground. "
        "By day they work on the tank, carry crates and patrol; at night some go off duty into the bunker. F3 possesses one.")
    sl.title("Soldiers: Catmull-Rom paths and IK gait", k("v6  |  Garrison"))
    sl.image(find_img(10, "sol_axes"), 0.5, 1.6, 3.6, 5.65, "Joint frames of the hierarchy")
    sl.image(find_img(10, "sol_route"), 4.25, 1.6, 4.6, 2.6, "Patrol splines through the camp")
    formula(sl, 4.25, 4.35, 4.6, 1.75, ["tᵢ₊₁ = tᵢ + |Pᵢ₊₁ − Pᵢ|^α,  α = 0.5", "v = v_max / (1 + 1.5 κ)",
                                         "cos θ_knee = (a² + b² − d²) / 2ab", f"a = {n('gaitThigh')} m (thigh), b = {n('gaitShin')} m (shin)"], 14, "key formulas")
    bullets(sl, 4.25, 6.25, 4.6, ["Planted stance foot, swing arc, arm swing", "Shift change at dusk and daybreak"], 14, 0.45)
    for i, (big, small) in enumerate([(n("nSoldiers"), "soldiers"), (n("soldierParts"), "parts each"),
                                      (n("soldierDepth"), "hierarchy levels"), (n("nRoutes"), "spline routes")]):
        stat(sl, 9.0 + (i % 2) * 1.96, 1.6 + (i // 2) * 1.2, 1.87, big, small, 1.08, 26)
    sl.image(find_img(10, "sol_fp"), 9.0, 4.1, 3.83, 3.15, "F3: possessed soldier, first person")
    slides.append(sl)

    # 11 Random routes (v6)
    sl = Slide(prs, "Rover routes are no longer predictable. Each rover spawns on a jittered ring, picks up to three via points inside a seventy degree sector, "
        "and may switch objective mid-route. Each one has a personality: speed, noise and exposure weights for its MicroMouse cost map, and pauses in cover. "
        "Everything is seeded: the same seed gives a byte-identical six hundred second run, another seed a different one. No rover spawns by day.")
    sl.title("Random rover routes", k("v6  |  Intruders"))
    sl.image(find_img(11, "routes"), 0.5, 1.6, 6.6, 5.65, "Planned routes on the thermal plan view")
    bullets(sl, 7.3, 1.62, 5.53, [f"Spawn on a ring (±{n('rrSpawnJitter')} m), not within ±{n('rrNorthGapDeg')}° of the gate road",
                                  f"0-{n('rrMaxVia')} via points in a ±{n('rrViaSectorDeg')}° sector, radius {n('rrViaMinR')}-{n('rrViaMaxR')} m",
                                  f"Objective switch with p = {n('rrSwitchProbability')} after {n('rrSwitchMinS')}-{n('rrSwitchMaxS')} s",
                                  "Personality: speed 0.8-1.25, noise and exposure weights, pauses in cover",
                                  "Seeded patrols for the UAVs too (seed 0 = the exact v5 run)"], 16, 0.55)
    for i, (big, small) in enumerate([("0", "spawns by day"), ("42 = 42", "seed 42 twice: identical"), ("≠", "seed 43: other run")]):
        stat(sl, 7.3 + i * 1.86, 4.5, 1.77, big, small, 1.1, 26)
    formula(sl, 7.3, 5.8, 5.53, 1.45, ["cost = dist + w_e·exposure + w_n·noise", "flood fill on cost, line follower on path",
                                         "seed = deriveSeed(run seed, stream)"], 14, "per-rover planner")
    slides.append(sl)

    # 12 CV under noise (v6)
    sl = Slide(prs, "The vision is now real: each drone sees its rendered heat image through a sensor model with temporal noise, fixed-pattern gain and offset, column stripes, "
        f"a blur kernel, stuck pixels, gimbal jitter and one percent dropped frames. Detection still works: probability {n('cvPdFarNom')} for a distant rover at the default noise. "
        "Warm soldiers are classified as persons and never targeted, and a squad that sees nothing never fires: no hidden true positions.")
    sl.title("Real computer vision under sensor noise", k("v6  |  Vision"))
    sl.image(find_img(12, "cv_lab"), 0.5, 1.6, 5.9, 4.43, fit="contain")
    sl.text(0.5, 6.12, 5.9, 1.1, ["Ideal heat image | sensor model | difference ×5 | detectAll:",
                                  "threshold max(0.80, Otsu) → components → target / person / clutter"], 14, GREY)
    formula(sl, 6.6, 1.6, 6.23, 1.35, ["y = Q( g · (PSF * x) + o + c + n )",
                                        f"g, o fixed pattern  c stripes  n NETD {n('snNetdDn')} DN"], 15, "sensor model")
    for i, (big, small) in enumerate([(n("cvPdFarNom"), "P_d, distant rover"), (n("cvPdFarWorst"), f"P_d at NETD {n('cvNetdWorst')} DN"),
                                      ("1 %", "frames dropped")]):
        stat(sl, 6.6 + i * 2.1, 3.1, 2.0, big, small, 1.1, 26)
    sl.image(find_img(12, "cv_day"), 6.6, 4.4, 3.65, 2.85, "Day thermal: PERS bracket, not a target", crop=(0.0, 0.15, 0.62, 0.85))
    bullets(sl, 10.4, 4.4, 2.43, ["Persons never become targets", "Blind squad never fires", "Seeded: same seed, same noise"], 14, 0.85)
    slides.append(sl)

    # 13 Game camera (v7)
    sl = Slide(prs, "Version seven makes it feel like a game. F4 opens a list of every moving object, sorted by distance, and the camera jumps into any of them with a critically damped spring blend. "
        "Insert and Delete cycle, a middle click jumps into whatever is under the cursor, and Home starts a free fly camera. "
        "Riding along, any drive key takes control: a soldier with a rifle, an intruder rover the drones still hunt, a drone or the helicopter.")
    sl.title("Game camera: jump into any point of view", k("v7  |  Play"))
    sl.image(find_img(13, "game_main"), 0.5, 1.6, 7.6, 4.27, "Soldier first person: rifle, tracers, game HUD (F11)")
    sl.image(find_img(13, "game_picker"), 8.25, 1.6, 4.58, 2.06, "F4: POV picker, every moving object")
    sl.image(find_img(13, "game_heli"), 8.25, 3.81, 4.58, 2.06, "Helicopter cockpit")
    keys = [("F4", "POV picker"), ("Ins / Del", "next / previous POV"), ("Middle click", "jump into it"),
            ("Home / End", "free fly / back"), ("F11", "game mode HUD"), ("L-Ctrl / L-Alt", "soldier: fire / crouch")]
    for i, (kk, dd) in enumerate(keys):
        keychip(sl, 0.5 + i * 2.075, 6.02, 1.975, 1.23, kk, dd)
    slides.append(sl)

    # 14 Whitted vs path vs light tracing (v7)
    sl = Slide(prs, "The semicolon key switches the renderer between raster, hybrid and three full ray tracers, all on the GPU in compute shaders. "
        "Whitted tracing is deterministic: shadow rays, mirror reflection and refraction with exact Fresnel. "
        "The path tracer solves the rendering equation by Monte Carlo sampling with next event estimation and a denoiser. "
        "Light tracing runs the other way: paths start at the lights and are splatted to the camera, which shows caustics.")
    sl.title("Three ways to trace light", k("v7  |  Ray tracing"))
    cols = [("rt_whitted", "Whitted (recursive)", ["Shadow rays, mirror reflection, Snell refraction with exact Fresnel",
                                                    "Converges softly while the camera is still"], "η₁ sin θ₁ = η₂ sin θ₂"),
            ("rt_path", "Path tracing", ["Next event estimation, cosine + GGX sampling, Russian roulette",
                                                       "1 sample / frame, accumulated, à-trous denoise"], "L = Le + ∫ f · L · cos θ dω"),
            ("rt_light", "Light tracing (reverse)", ["256K paths / frame from sun, moon, searchlight, lamps",
                                                     "Each vertex splatted to the camera: caustics (cyan)"], "W = Npix / (A cos⁴ θ)")]
    cw = 3.98
    for i, (slot, head, lines, eq) in enumerate(cols):
        x = 0.5 + i * (cw + 0.195)
        sl.image(find_img(14, slot), x, 1.6, cw, 2.55)
        sl.text(x, 4.25, cw, 0.45, head, 20, AMBER, True)
        bullets(sl, x, 4.75, cw, lines, 14, 0.7)
        sl.rect(x, 6.2, cw, 0.5, PANEL)
        sl.text(x + 0.1, 6.2, cw - 0.2, 0.5, eq, 15, CYAN, anchor=MSO_ANCHOR.MIDDLE, font=MONO)
    sl.text(0.5, 6.82, 12.33, 0.45, "One scene, one BVH, exact intersectors: only spheres, boxes, cylinders and cones, no triangles.", 15, GREY)
    slides.append(sl)

    # 15 BVH / SAH (v7)
    sl = Slide(prs, "Because the scene is built only from primitives, every part is traced exactly: the ray goes into the part's local space with the inverse matrix and meets a unit sphere, box, cylinder or cone. "
        "About five thousand primitives sit in a bounding volume hierarchy built with the surface area heuristic. "
        "Moving objects only refit the boxes each frame, under one millisecond. In hybrid mode these rays replace screen-space reflections, so off-screen objects appear in mirrors.")
    sl.title("BVH with the surface area heuristic", k("v7  |  Ray tracing"))
    formula(sl, 0.5, 1.6, 5.9, 1.0, ["C = C_trav + (A_L/A)·N_L·C_isect", "          + (A_R/A)·N_R·C_isect"], 14, "SAH split cost")
    bullets(sl, 0.5, 2.75, 5.9, ["Exact unit shapes: ray to local space with M⁻¹, normal with M⁻ᵀ",
                                 "Binned SAH, 16 bins; leaves of up to 16 primitives",
                                 "Refit per frame (one reverse sweep); rebuild when the cost exceeds 1.6×",
                                 "Terrain: 289² height field, 8×8-cell min/max blocks skip empty space",
                                 "Flattened to SSBOs for the GLSL 4.30 compute tracer"], 15, 0.58)
    for i, (big, small) in enumerate([("~5.1k", "primitives"), ("~8.5k", "BVH nodes"), ("61 ms", "build, once"), ("< 1 ms", "refit / frame")]):
        stat(sl, 0.5 + i * 1.5, 5.85, 1.4, big, small, 1.1, 24)
    # BVH schematic: root -> 2 -> 4 boxes -> primitive leaves
    px, py, pw, ph = 6.7, 1.6, 6.13, 3.1
    sl.rect(px, py, pw, ph, PANEL)
    sl.text(px + 0.15, py + 0.06, 3, 0.3, "BVH (SCHEMATIC)", 11, GREY, True)
    py += (ph - 2.6) / 2
    cx = px + pw / 2
    sl.rect(cx - 0.6, py + 0.35, 1.2, 0.42, BG, AMBER); sl.text(cx - 0.6, py + 0.35, 1.2, 0.42, "root", 13, AMBER, True, PP_ALIGN.CENTER, MSO_ANCHOR.MIDDLE)
    l1 = [px + pw * 0.27, px + pw * 0.73]
    l2 = [px + pw * f for f in (0.14, 0.38, 0.62, 0.86)]
    vline(sl, cx, py + 0.77, 0.18); hline(sl, l1[0], py + 0.95, l1[1] - l1[0])
    for x in l1:
        vline(sl, x, py + 0.95, 0.15)
        sl.rect(x - 0.55, py + 1.1, 1.1, 0.38, BG, GREY); sl.text(x - 0.55, py + 1.1, 1.1, 0.38, "node", 12, WHITE, False, PP_ALIGN.CENTER, MSO_ANCHOR.MIDDLE)
    for j, x0 in enumerate(l1):
        vline(sl, x0, py + 1.48, 0.15); hline(sl, l2[2 * j], py + 1.63, l2[2 * j + 1] - l2[2 * j])
    shapes = [("oval", "sphere"), ("rect", "box"), ("cyl", "cylinder"), ("cone", "cone")]
    for i, x in enumerate(l2):
        vline(sl, x, py + 1.63, 0.15)
        sl.rect(x - 0.6, py + 1.78, 1.2, 0.72, BG, DIM)
        kind, lab = shapes[i]
        if kind == "oval": sl.rect(x - 0.17, py + 1.84, 0.34, 0.34, AMBER, shape=MSO_SHAPE.OVAL)
        elif kind == "rect": sl.rect(x - 0.17, py + 1.84, 0.34, 0.34, AMBER)
        elif kind == "cyl": sl.rect(x - 0.13, py + 1.84, 0.26, 0.34, AMBER, shape=MSO_SHAPE.CAN)
        else: sl.rect(x - 0.18, py + 1.84, 0.36, 0.34, AMBER, shape=MSO_SHAPE.ISOSCELES_TRIANGLE)
        sl.text(x - 0.6, py + 2.19, 1.2, 0.3, lab, 11, GREY, False, PP_ALIGN.CENTER)
    sl.text(6.7, 4.78, 6.13, 0.4, "Hybrid mode: (a) screen-space reflection  vs  (b) BVH reflection rays", 13, AMBER)
    sl.image(find_img(15, "rt_ssr"), 6.7, 5.23, 6.13, 1.72, fit="contain")
    slides.append(sl)

    # 16 Ray-tree debugger (v7)
    tree = None
    tp = os.path.join(REP, "figures_v7", "raytrace_tree.json")
    if os.path.exists(tp): tree = json.load(open(tp))
    nr = len(tree["rays"]) if tree else 16
    depth = max(r["depth"] for r in tree["rays"]) if tree else 6
    sl = Slide(prs, f"To see what a ray tracer actually does, the apostrophe key turns on the ray debugger. Clicking a pixel re-traces it on the CPU with the same intersectors and draws its ray tree in 3D. "
        f"Here a click on the glass ball gives {nr} rays, {depth} levels deep: the primary ray, a Fresnel reflection, refraction through the ball, internal reflections, and shadow rays that are lit or blocked. "
        "Each label shows depth, distance and contribution.")
    sl.title("Ray-tree debugger", k("v7  |  Ray tracing"))
    sl.image(find_img(16, "rt_tree"), 0.5, 1.6, 8.6, 4.84, fit="contain")
    sl.text(0.5, 6.55, 8.6, 0.7, f"' on, left click a pixel: rt::debugTrace re-traces it (Whitted, CPU) → {nr} rays, depth {depth}; the tree stays in 3D while the camera moves", 15, GREY)
    sl.text(9.3, 1.6, 3.53, 0.4, "RAY COLOURS", 13, GREY, True)
    legend = [(WHITE, "primary"), (RGBColor(0xFF, 0xE0, 0x40), "shadow, lit"), (RGBColor(0xFF, 0x40, 0x40), "shadow, blocked"),
              (CYAN, "reflection"), (RGBColor(0xE0, 0x60, 0xE8), "refraction"), (RGBColor(0x60, 0xE0, 0x70), "diffuse bounce")]
    for i, (c, lab) in enumerate(legend):
        y = 2.05 + i * 0.45
        sl.rect(9.3, y + 0.14, 0.55, 0.07, c)
        sl.text(10.0, y, 2.8, 0.42, lab, 16, WHITE)
    formula(sl, 9.3, 4.85, 3.53, 2.4, ["#0 primary  → glass ball", "#1 reflection → camo net", "#2 refraction → glass ball",
                                       "  #3-6 internal reflections", "  #8 shadow: lit by key light"], 13, "this pixel")
    slides.append(sl)

    # 17 Timings (v7)
    sl = Slide(prs, "These are the measured frame times on the RTX 4070 through the WSL Direct3D 12 path at 720p output. "
        "The full ray tracers run at reduced internal resolution and are fast: Whitted about 29 milliseconds, path tracing 27, light tracing 45 by day. "
        "Surprisingly the raster EPIC frame is the slow one on this driver path, about 155 milliseconds, and hybrid inherits it; the ray-traced pass itself adds only 14 to 21.")
    sl.title("Ray tracing timings on the RTX 4070", k("v7  |  Ray tracing"))
    colw = [3.0, 1.25, 1.25, 1.3]
    hdr = ["Mode (internal resolution)", "Day", "Night", "RT pass"]
    x0, y0, rh = 0.5, 1.6, 0.52
    sl.rect(x0, y0, sum(colw), rh, PANEL)
    xx = x0
    for j, hcell in enumerate(hdr):
        sl.text(xx + 0.05, y0, colw[j] - 0.1, rh, hcell, 14, AMBER, True, PP_ALIGN.LEFT if j == 0 else PP_ALIGN.RIGHT, MSO_ANCHOR.MIDDLE)
        xx += colw[j]
    for i, row in enumerate(RT_TIMES):
        y = y0 + (i + 1) * rh
        if i % 2: sl.rect(x0, y, sum(colw), rh, RGBColor(0x0F, 0x12, 0x17))
        xx = x0
        for j in range(4):
            sl.text(xx + 0.05, y, colw[j] - 0.1, rh, row[j], 14, WHITE if j else GREY, False, PP_ALIGN.LEFT if j == 0 else PP_ALIGN.RIGHT, MSO_ANCHOR.MIDDLE)
            xx += colw[j]
    hline(sl, x0, y0 + rh, sum(colw), AMBER, 0.02)
    sl.text(x0, y0 + (len(RT_TIMES) + 1) * rh + 0.05, sum(colw), 0.4, "Day 16:30, night 23:00. CPU fallback (TS_RT_CPU=1, 160x90): Whitted 60, path 144, light 238 ms", 12, GREY)
    # bar chart: day frame time per mode (single series; raster in grey as the reference)
    bx, by, bw = 7.75, 1.6, 5.08
    sl.text(bx, by, bw, 0.4, "Frame time by day, ms (lower is faster)", 15, WHITE, True)
    bars = [(r[0].split(" (")[0], r[4], r[5]) for r in RT_TIMES if r[4] is not None]
    vmax, lw, top = 45.0, 1.35, by + 0.55
    plot_w = bw - lw - 0.75
    for i, (lab, v, isrt) in enumerate(bars):
        y = top + i * 0.56
        sl.text(bx, y, lw - 0.1, 0.45, lab, 14, GREY, False, PP_ALIGN.RIGHT, MSO_ANCHOR.MIDDLE)
        sl.rect(bx + lw, y + 0.08, plot_w * v / vmax, 0.3, AMBER if isrt else GREY)
        sl.text(bx + lw + plot_w * v / vmax + 0.06, y, 0.75, 0.45, f"{v}", 14, WHITE, True, anchor=MSO_ANCHOR.MIDDLE)
    vline(sl, bx + lw, top, len(bars) * 0.56, GREY, 0.02)
    bullets(sl, 7.75, 5.15, 5.08, ["WP-Q: raster EPIC 157 → 11.7 ms (instancing, grass fix, async readback)",
                                   "Entry-distance BVH stack, smaller stacks: Whitted 49 → 34 ms",
                                   "Hybrid adds ~10 ms of rays on top of the raster frame"], 14, 0.56)
    sl.text(0.5, 6.92, 12.3, 0.35, "Source: report/figures_v7/raytrace_perf.json (CAP), RTX 4070 via Mesa d3d12, 1280x720 output, EPIC, wall clock with glFinish", 12, GREY)
    slides.append(sl)

    # 18 Frame Explorer pipeline (v7)
    st_titles = ["Scene graph", "Vertex transform", "Assembly + culling", "Rasterisation", "Colour interpolation", "Depth test",
                 "G-buffer", "Shadow map", "Lighting", "Reflection", "SSAO", "Fog", "Bloom", "Tone mapping", "TAA", "Final + CV"]
    sp = os.path.join(REP, "figures_v7", "explorer_stages.json")
    if os.path.exists(sp):
        stj = json.load(open(sp)); stj = stj["stages"] if isinstance(stj, dict) else stj
        assert len(stj) == 16, len(stj)
    sl = Slide(prs, "The backtick key freezes the current frame and replays how it was made, in sixteen stages, from the scene graph to the final thermal and computer vision view. "
        "Every stage is a real buffer of that frame, with a caption, the key formula and a probe pixel that every card follows. "
        "Arrows step through stages and layers, W wipes against the previous stage, I inspects a pixel in every buffer, and P opens an image processing lab.")
    sl.title("Frame Explorer: one frame, 16 stages", k("v7  |  Frame Explorer"))
    sl.image(find_img(18, "fx_montage"), 0.5, 1.6, 8.3, 4.69, fit="contain")
    sl.text(0.5, 6.4, 8.3, 0.85, ["` freezes the frame: 8 TAA frames + 7 variants (SSR / SSAO / fog off, Blinn-Phong, TAA off, thermal, raw heat) through the real renderer"], 14, GREY)
    for i, t in enumerate(st_titles):
        c, r = divmod(i, 8)
        x, y = 9.05 + c * 1.92, 1.6 + r * 0.46
        sl.text(x, y, 0.5, 0.42, f"{i + 1:02d}", 15, AMBER, True)
        sl.text(x + 0.45, y, 1.47, 0.42, t, 13, WHITE)
    sl.text(9.05, 5.4, 3.78, 0.35, "INSIDE THE EXPLORER", 12, GREY, True)
    sl.text(9.05, 5.72, 3.78, 1.55, ["← → stage   ↑ ↓ layer   1-0 jump", "W wipe   M magnifier   I inspector", "P image lab   Space play   ` / Esc close"], 13, WHITE, font=MONO)
    slides.append(sl)

    # 19 Colour interpolation (v7)
    fi = find_img(19, "fx_interp")
    sl = Slide(prs, "Stage five shows colour interpolation, the heart of rasterisation. A checkerboard on every surface makes the difference visible: "
        "blending vertex attributes linearly in screen space bends the squares, while dividing by w before blending and multiplying back keeps them straight. "
        "The cards zoom into one probe triangle, and a coarse sphere compares Gouraud, which lights the vertices, with Phong, which lights every pixel.")
    sl.title("Colour interpolation: affine vs perspective", k("v7  |  Frame Explorer"))
    sl.image(fi, 0.5, 1.6, 7.9, 4.44, fit="contain")
    if fi and "explorer_interp_affine_perspective" in fi:
        sl.image(fi, 8.6, 1.6, 4.23, 2.05, "Probe triangle: affine bends, perspective is straight", crop=(0.703, 0.335, 0.992, 0.535))
        sl.image(fi, 8.6, 3.78, 4.23, 2.26, "Coarse sphere: Gouraud vs Phong", crop=(0.703, 0.585, 0.992, 0.765))
    else:
        sl.image(find_img(19, "fx_scan"), 8.6, 1.6, 4.23, 4.44, "Rasterisation: coverage and barycentrics")
    formula(sl, 0.5, 6.18, 12.33, 1.07, ["λᵢ = Eᵢ(p) / (E₀ + E₁ + E₂)        affine:  a = Σ λᵢ aᵢ",
                                         "perspective-correct:  a = Σ (λᵢ aᵢ / wᵢ) / Σ (λᵢ / wᵢ)"], 15)
    slides.append(sl)

    # 20 Reflection SSR vs RT (v7)
    sl = Slide(prs, "Stage ten explains reflection. The reflected ray leaves at the angle it arrived. Screen-space reflection marches that ray through the depth buffer and reuses colour already on screen, "
        "so it is cheap but cannot see anything off-screen or hidden. The same frame with ray-traced reflections follows the ray through the BVH instead. "
        "The split view and the difference image show exactly where the two disagree.")
    sl.title("Reflection: screen-space vs ray traced", k("v7  |  Frame Explorer"))
    sl.image(find_img(20, "fx_reflect"), 0.5, 1.6, 8.3, 4.67, fit="contain")
    sl.image(find_img(20, "fx_diff"), 9.0, 1.6, 3.83, 2.15, "RT − SSR on reflective pixels (×5)")
    sl.text(9.0, 3.9, 3.83, 0.4, "SSR (screen space)", 17, AMBER, True)
    sl.text(9.0, 4.3, 3.83, 0.8, "Marches the depth buffer; reuses on-screen colour; blind off-screen", 14, GREY)
    sl.text(9.0, 5.1, 3.83, 0.4, "Ray traced (BVH)", 17, CYAN, True)
    sl.text(9.0, 5.5, 3.83, 0.8, "Hybrid G-buffer + BVH rays; finds off-screen and hidden objects", 14, GREY)
    formula(sl, 0.5, 6.4, 12.33, 0.85, ["R = D − 2 (D·N) N          F = F₀ + (1 − F₀)(1 − cos θ)⁵   (Schlick)"], 15)
    slides.append(sl)

    # 21 Vehicles: get in, fire, get out (final night)
    sl = Slide(prs, "The vehicles are part of the same world as the soldier. Here the player walks a soldier up to the tank and presses E. "
        "The soldier is no longer drawn, the tank takes the input, drives out of its bay and pivots on its tracks, and the turret turns on its own. "
        "C switches to the gunner's sight for two rounds, then E puts the soldier back on his feet beside the hull. "
        "The anti-aircraft gun, both launchers and the helicopter work the same way.")
    sl.title("Get in, drive, fire, get out", k("Final  |  controllable vehicles"))
    for i, (slot, cap) in enumerate([("veh_a", "1  Walk up, [E] to get in"), ("veh_b", "2  Drive out, pivot, traverse"),
                                     ("veh_c", "3  Gunner's sight: fire"), ("veh_d", "4  [E] again: back on foot")]):
        sl.image(find_img(21, slot), 0.5 + (i % 2) * 4.27, 1.6 + (i // 2) * 2.5, 4.12, 2.32, cap)
    bullets(sl, 9.25, 1.7, 3.6, ["One key for doors, vehicles and units", "Soldier hidden while aboard, duty paused",
                                 "Exit spot: 8 directions on 3 rings tested against the solid boxes",
                                 "Unmanned weapons on alert: launchers raised, rotor at idle, guns on the intruder"], 15, 0.92)
    slides.append(sl)

    # 22 Single strike, combined strike, guards (final night)
    sl = Slide(prs, "The defence decides by rule how to strike. One intruder gets both drones: the lead waits for the wing's lock and both rounds are timed to arrive together. "
        "A second intruder that is far from the first and close to a protected point cannot wait, so the nearer drone leaves the pair and each strikes its own target alone. "
        "Guards at their posts open fire on any tracked intruder within range and a clear line. All of it starts from thermal pixels only.")
    sl.title("Single strike, combined strike, guards", k("Final  |  autonomous defence"))
    for i, (slot, cap) in enumerate([("str_d", "Thermal view: tracks on every hot target"), ("str_a", "Single strike: the other UAV is busy"),
                                     ("str_c", "Guards fire from their posts (tracers)"), ("str_b", "Combined strike: both rounds together")]):
        sl.image(find_img(22, slot), 0.5 + (i % 2) * 4.27, 1.6 + (i // 2) * 2.5, 4.12, 2.32, cap)
    bullets(sl, 9.25, 1.7, 3.6, ["Split: second track > 14 m away and < 10 m from an asset", "Lead waits up to 8 s for a free wing",
                                 "Time on target: the farther UAV launches first", "Guards: range 26 m, 0.6 s bursts, same clear-line test as the rounds"], 15, 0.92)
    slides.append(sl)

    # 23 Hand-written vector maths (final night)
    sl = Slide(prs, "No shader of the program calls a built-in vector function. The dot product, normalise, cross, reflect, refract, clamp, mix, step and smooth step "
        "are written out from their definitions in one file and placed in front of every shader, nine hundred and five calls in thirteen files. "
        "The compile step refuses a shader that still calls a built-in one. Blinn-Phong and the microfacet model are built from these functions, "
        "and the C++ side has its own vectors and matrices as well.")
    sl.title("Lighting maths written by hand", k("Final  |  no built-in vector functions"))
    formula(sl, 0.5, 1.6, 7.6, 3.1, ["a·b = Σ aᵢ bᵢ          |a| = √(a·a)          â = a / |a|",
                                     "a×b = (a_y b_z − a_z b_y,  a_z b_x − a_x b_z,  a_x b_y − a_y b_x)",
                                     "reflect:  r = d − 2 (n·d) n",
                                     "refract:  k = 1 − η² (1 − (n·d)²),   t = η d − (η (n·d) + √k) n",
                                     "mix(a, b, t) = (1 − t) a + t b        clamp(x, a, b) = min(max(x, a), b)",
                                     "smoothstep:  t = clamp((x − e₀)/(e₁ − e₀), 0, 1),   t² (3 − 2t)"], 12, "src/rendering/ShaderMath.cpp  (tsDot, tsNormalize, tsReflect, ...)")
    formula(sl, 0.5, 4.9, 7.6, 2.3, ["I = k_a I_a + Σ f_att I_l [ k_d max(0, N·L) + k_s max(0, N·H)ⁿ ]",
                                     "float nl = max(tsDot(N, L), 0.0);",
                                     "vec3 H = tsNormalize(L + V);",
                                     "spec = radiance * uSpecular * pow(max(tsDot(N, H), 0.0), uShininess);"], 12, "Blinn-Phong, as compiled")
    for i, (big, small) in enumerate([("905", "calls of the hand-written functions"), ("0", "built-in vector calls (compile refuses them)"), ("13", "shader source files")]):
        stat(sl, 8.4, 1.6 + i * 1.45, 4.43, big, small)
    sl.image(find_img(23, "mm_a"), 8.4, 5.85, 4.43, 1.35, "Lit by these equations only")
    slides.append(sl)

    # 24 Results
    sl = Slide(prs, f"Results. All {n('nTests')} automated tests pass, covering primitives, vision, tracking, soldiers, the day cycle, the game camera, ray tracing and the explorer. "
        "Six hundred second traces are deterministic per seed, no rover spawns by day, and every kill comes from a round aimed at a vision track. "
        "The night preset still matches the version five figures. The Windows executable and the WSL window both build without administrator rights.")
    sl.title("Testing and results", k("Results"))
    for i, (big, small) in enumerate([(f"{n('nPass')} / {n('nTests')}", "automated tests pass"), (n("nLeaves"), "primitive leaves"),
                                      (n("nPossessable"), "possessable objects"), ("5 + 16", "render modes + explorer stages")]):
        stat(sl, 0.5 + i * 3.1, 1.6, 2.95, big, small)
    bullets(sl, 0.6, 3.05, 5.9, ["600 s traces: seed 42 twice byte-identical, seed 43 differs",
                                 "0 spawns by day; every kill from a vision-designated round",
                                 "Night regression matches v5 (static shots MAE 0.4-5.8 / 255)",
                                 "Raster 11.7 ms; ray traced: Whitted 25.7 ms, path 21.9 ms on the RTX 4070",
                                 "Workbook verify PASS; Windows exe + WSLg build, no sudo"], 16, 0.68)
    sl.image(find_img(24, "results"), 6.73, 3.05, 6.1, 4.2, "Free camera over a combined strike (+150)")
    slides.append(sl)

    # 25 Video
    sl = Slide(prs, "And now the demonstration, recorded in the program itself with its own sound, three minutes and thirty-eight seconds. Part one shows the graphics: the base, the render modes, "
        "a soldier opening the doors, the weapons on alert, and the same soldier getting into the tank, the anti-aircraft gun and the launcher. "
        "Part two shows the defence: the dusk launch, the thermal tracks, a single strike, the guards, the combined strike and dawn. Thank you; questions are welcome.")
    sl.title("Demo video", k("Demonstration, 3 min 38 s"))
    poster = find_img(25, "video")
    vx, vy, vw, vh = 1.67, 1.5, 10.0, 5.625
    pj = jpeg(poster) if poster else None
    if os.path.exists(VIDEO):
        sl.rec.append(("img", vx, vy, vw, vh, pj))
        sl.s.shapes.add_movie(VIDEO, Inches(vx), Inches(vy), Inches(vw), Inches(vh), poster_frame_image=pj, mime_type="video/mp4")
        video = "embedded " + os.path.relpath(VIDEO, ROOT)
    else:
        sl.image(poster, vx, vy, vw, vh)
        by = vy + vh - 1.75
        sl.rect(vx, by, vw, 1.75, BG)
        sl.rect(vx, by, vw, 0.05, AMBER)
        sl.text(vx, by + 0.12, vw, 0.7, "TODO: record " + os.path.relpath(VIDEO, ROOT), 32, AMBER, True, PP_ALIGN.CENTER, MSO_ANCHOR.MIDDLE)
        sl.text(vx, by + 0.85, vw, 0.4, "./ThermalSense --headless --quality epic --record report/demo_v7.mp4 150", 15, WHITE, False, PP_ALIGN.CENTER, MSO_ANCHOR.MIDDLE, MONO)
        sl.text(vx, by + 1.25, vw, 0.4, "v7 director cut: 23 segments, ~150 s, procedural sound; this slide embeds the file once it exists", 13, GREY, False, PP_ALIGN.CENTER, MSO_ANCHOR.MIDDLE)
        video = "placeholder (TODO: " + os.path.relpath(VIDEO, ROOT) + " not recorded yet)"
    slides.append(sl)

    # the outline numbers must match the slides
    want = {num: lab for _, items in OUTLINE for num, lab in items}
    assert len(slides) == max(want), (len(slides), max(want))
    prs.save(OUT)
    return slides, video

# ---------------- previews (schematic, same coordinates as the pptx) ----------------
_fonts = {}
def fnt(sz, bold=False, mono=False):
    key = (sz, bold, mono)
    if key not in _fonts:
        name = "DejaVuSansMono" if mono else ("DejaVuSans-Bold" if bold else "DejaVuSans")
        p = f"/usr/share/fonts/truetype/dejavu/{name}.ttf"
        _fonts[key] = ImageFont.truetype(p, sz) if os.path.exists(p) else ImageFont.load_default()
    return _fonts[key]

def render(i, sl, sc):
    tw, th = int(W * sc), int(H * sc)
    im = Image.new("RGB", (tw, th), (8, 10, 13)); d = ImageDraw.Draw(im)
    warn = []
    for r in sl.rec:
        if r[0] in ("rect", "oval", "tri"):
            _, x, y, w, h, c, ln = r
            box = [x * sc, y * sc, (x + w) * sc, (y + h) * sc]
            if r[0] == "tri": d.polygon([((x + w / 2) * sc, y * sc), (box[2], box[3]), (box[0], box[3])], fill=tuple(c))
            else: (d.ellipse if r[0] == "oval" else d.rectangle)(box, fill=tuple(c), outline=tuple(ln) if ln else None)
        elif r[0] == "img":
            _, x, y, w, h, j = r
            if j:
                pi = Image.open(j); ar, br = pi.width / pi.height, w / h
                if ar > br: nw = int(pi.height * br); pi = pi.crop(((pi.width - nw) // 2, 0, (pi.width + nw) // 2, pi.height))
                else: nh = int(pi.width / br); pi = pi.crop((0, (pi.height - nh) // 2, pi.width, (pi.height + nh) // 2))
                im.paste(pi.resize((max(1, int(w * sc)), max(1, int(h * sc))), Image.LANCZOS), (int(x * sc), int(y * sc)))
        else:
            _, x, y, w, h, paras, size, color, bold, align, anchor, font = r
            f = fnt(max(6, int(size * sc / 72 * 0.95)), bold, font == MONO)
            avail = w * sc - 0.1 * sc
            blocks = []
            for t in paras:
                words, line, lines = t.split(" "), "", []
                for wd in words:
                    cand = (line + " " + wd) if line else wd
                    if d.textlength(cand, font=f) <= avail or not line: line = cand
                    else: lines.append(line); line = wd
                lines.append(line)
                for ln in lines:
                    if d.textlength(ln, font=f) > avail + 2: warn.append(f"slide {i+1}: word wider than box {ln[:30]!r}")
                blocks.append(lines)
            lh = f.size * 1.2; ps = size * sc / 72 * 0.35
            total = sum(len(b) * lh for b in blocks) + ps * (len(blocks) - 1)
            yy = y * sc + 0.05 * sc
            if anchor == MSO_ANCHOR.MIDDLE: yy = y * sc + (h * sc - total) / 2
            for lines in blocks:
                for ln in lines:
                    tx = x * sc + 0.05 * sc
                    if align == PP_ALIGN.CENTER: tx = x * sc + (w * sc - d.textlength(ln, font=f)) / 2
                    elif align == PP_ALIGN.RIGHT: tx = x * sc + w * sc - 0.05 * sc - d.textlength(ln, font=f)
                    d.text((tx, yy), ln, font=f, fill=tuple(color)); yy += lh
                yy += ps
            if total + 0.05 * sc > h * sc + 4: warn.append(f"slide {i+1}: overflow {paras[0][:40]!r} ({total/sc + 0.05:.2f} > {h:.2f} in)")
    return im, warn

def preview(slides):
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    warns = []
    for i, sl in enumerate(slides):
        im, w = render(i, sl, 120)
        im.save(os.path.join(PREVIEW_DIR, f"slide_{i+1:02d}.png")); warns += w
    sc = 60; tw, th = int(W * sc), int(H * sc)
    cols = 3; rows = (len(slides) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (tw + 10) + 10, rows * (th + 10) + 10), (60, 60, 60))
    for i, sl in enumerate(slides):
        im = Image.open(os.path.join(PREVIEW_DIR, f"slide_{i+1:02d}.png")).resize((tw, th), Image.LANCZOS)
        sheet.paste(im, (10 + (i % cols) * (tw + 10), 10 + (i // cols) * (th + 10)))
    sheet.save(PREVIEW)
    for w in warns: print("  WARN", w)
    return warns

if __name__ == "__main__":
    slides, video = build()
    warns = preview(slides)
    for u in used: print("image", u)
    print(f"slides: {len(slides)} | video: {video} | overflow warnings: {len(warns)} | size: %.1f MB | {OUT}" % (os.path.getsize(OUT) / 1e6))
