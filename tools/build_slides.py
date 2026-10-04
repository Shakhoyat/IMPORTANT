#!/usr/bin/env python3
"""Build report/ThermalSense_Presentation.pptx (10 slides, 16:9, dark night-ops theme).

Run:  uv run --with python-pptx --with pillow python tools/build_slides.py
Re-runnable: picks up report/figures_v4/*.png and report/demo_v4.mp4 when they exist.
Also writes a schematic contact sheet (build/slides_preview.png) from the same layout records.
"""
import os, re, sys, tempfile, json
from pptx import Presentation
from pptx.util import Inches, Pt, Emu
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REP = os.path.join(ROOT, "report")
OUT = os.path.join(REP, "ThermalSense_Presentation.pptx")
VIDEO = os.path.join(REP, "demo_v4.mp4")
PREVIEW = os.path.join(ROOT, "build", "slides_preview.png")
TMP = tempfile.mkdtemp(prefix="ts_slides_")
DIRS = [("figures_v4", "v4"), ("figures_v3", "v3 fallback"), ("figures", "v2 fallback")]

BG, PANEL = RGBColor(0x08, 0x0A, 0x0D), RGBColor(0x14, 0x18, 0x1E)
AMBER, WHITE, GREY = RGBColor(0xFF, 0x9E, 0x1F), RGBColor(0xFF, 0xFF, 0xFF), RGBColor(0xA8, 0xB0, 0xBA)
W, H = 13.333, 7.5
FONT = "Calibri"

def stats():
    d = {}
    p = os.path.join(REP, "sections", "gen_stats.tex")
    if os.path.exists(p):
        for m in re.finditer(r"\\newcommand\{\\(\w+)\}\{([^}]*)\}", open(p).read()):
            d[m.group(1)] = m.group(2).replace("\\,", ",")
    return d
S = stats()
def n(k, default="?"): return S.get(k, default)

used = []  # (slide, wanted, path, source)
def find_img(slide, names):
    """First existing image among names, searching v4, then v3, then v2 dirs per name list order.
    names may be ordered by preference; v4 files win over fallbacks regardless of order."""
    order = [("figures_v4", nm) for nm in names] + [(d, nm) for nm in names for d in ("figures_v3", "figures")]
    for d, nm in order:
        p = os.path.join(REP, d, nm + ".png")
        if os.path.exists(p):
            tag = "v4 real" if d == "figures_v4" else ("v3 fallback" if d == "figures_v3" else "v2 fallback")
            if nm != names[0]: tag += f" (substitute for {names[0]})"
            used.append((slide, names[0], f"{d}/{nm}.png", tag))
            return p
    used.append((slide, names[0], None, "MISSING"))
    return None

_cache = {}
def jpeg(path):
    if path in _cache: return _cache[path]
    im = Image.open(path).convert("RGB")
    if im.width > 1600: im = im.resize((1600, round(im.height * 1600 / im.width)), Image.LANCZOS)
    out = os.path.join(TMP, f"img{len(_cache)}.jpg")
    im.save(out, "JPEG", quality=88)
    _cache[path] = out
    return out

class Slide:
    """Records every shape so the preview uses identical coordinates."""
    def __init__(self, prs, notes):
        self.s = prs.slides.add_slide(prs.slide_layouts[6])
        self.rec = []
        bg = self.s.background.fill; bg.solid(); bg.fore_color.rgb = BG
        self.s.notes_slide.notes_text_frame.text = notes
        wc = len(notes.split())
        assert 60 <= wc <= 90, f"notes {wc} words"
    def rect(self, x, y, w, h, color=PANEL, line=None):
        sh = self.s.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(x), Inches(y), Inches(w), Inches(h))
        sh.fill.solid(); sh.fill.fore_color.rgb = color
        if line: sh.line.color.rgb = line; sh.line.width = Pt(1)
        else: sh.line.fill.background()
        self.rec.append(("rect", x, y, w, h, color, line)); return sh
    def text(self, x, y, w, h, text, size=20, color=WHITE, bold=False, align=PP_ALIGN.LEFT, anchor=MSO_ANCHOR.TOP, lines=None):
        tb = self.s.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
        tf = tb.text_frame; tf.word_wrap = True; tf.vertical_anchor = anchor
        tf.margin_left = tf.margin_right = Inches(0.05)
        paras = text if isinstance(text, list) else [text]
        for i, t in enumerate(paras):
            p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
            p.alignment = align; p.space_after = Pt(size * 0.35)
            r = p.add_run(); r.text = t
            r.font.size = Pt(size); r.font.bold = bold; r.font.name = FONT; r.font.color.rgb = color
        self.rec.append(("text", x, y, w, h, paras, size, color, bold, align)); return tb
    def title(self, t, kicker=None):
        self.rect(0, 0, 0.12, H, AMBER)
        if kicker: self.text(0.5, 0.28, 8, 0.35, kicker.upper(), 13, AMBER, True)
        self.text(0.5, 0.58, 12.3, 0.85, t, 34, WHITE, True)
    def image(self, path, x, y, w, h, caption=None):
        """Cover-crop the picture into the box (x, y, w, h)."""
        if path is None:
            self.rect(x, y, w, h, PANEL, GREY)
            self.text(x, y + h / 2 - 0.3, w, 0.6, "figure pending", 16, GREY, align=PP_ALIGN.CENTER)
            self.rec.append(("img", x, y, w, h, None)); return
        j = jpeg(path); im = Image.open(j)
        ar, br = im.width / im.height, w / h
        pic = self.s.shapes.add_picture(j, Inches(x), Inches(y), Inches(w), Inches(h))
        if ar > br:
            c = (1 - br / ar) / 2; pic.crop_left = pic.crop_right = c
        else:
            c = (1 - ar / br) / 2; pic.crop_top = pic.crop_bottom = c
        self.rec.append(("img", x, y, w, h, j))
        if caption:
            self.rect(x, y + h - 0.38, w, 0.38, BG)
            self.text(x, y + h - 0.38, w, 0.38, caption, 12, AMBER, anchor=MSO_ANCHOR.MIDDLE)

def bullets(sl, x, y, w, items, size=20, gap=0.62):
    for i, it in enumerate(items):
        sl.rect(x, y + i * gap + 0.13, 0.12, 0.12, AMBER)
        sl.text(x + 0.25, y + i * gap, w - 0.25, gap, it, size)

def stat(sl, x, y, w, big, small):
    sl.rect(x, y, w, 1.15, PANEL)
    sl.text(x, y + 0.02, w, 0.65, big, 30, AMBER, True, PP_ALIGN.CENTER)
    sl.text(x, y + 0.68, w, 0.4, small, 13, GREY, False, PP_ALIGN.CENTER)

def build():
    prs = Presentation(); prs.slide_width = Inches(W); prs.slide_height = Inches(H)
    slides = []

    # 1 Title
    sl = Slide(prs, "Good morning. This is ThermalSense, my computer graphics lab project: an autonomous drone patrolling a night-time military base. "
        "Everything you will see, the bunker, the tank, the helicopter and the rover targets, is built only from three primitives: cube, sphere and cylinder. "
        "No model files, no textures. In the next ten minutes I will show the goal, the construction, the rendering, the game logic, the tests and finally a two minute demonstration video.")
    sl.image(find_img(1, ["base_overview", "cine_establishing", "scene_visible"]), 0, 0, W, H)
    sl.rect(0, 3.7, W, 3.8, BG)
    sl.rect(0.6, 3.95, 1.4, 0.07, AMBER)
    sl.text(0.6, 4.1, 12, 1.0, "ThermalSense", 54, WHITE, True)
    sl.text(0.6, 5.0, 12, 0.7, "Autonomous UAV Patrol of a Military Base", 30, AMBER)
    sl.text(0.6, 5.95, 12, 0.5, "Md. Shakhoyat Rahman Shujon  |  Roll 2107104", 20, WHITE)
    sl.text(0.6, 6.4, 12, 0.5, "Computer Graphics Lab  |  C++17, OpenGL 3.3", 18, GREY)
    slides.append(sl)

    # 2 Outline
    sl = Slide(prs, "Here is the outline. First the goal and how the requirements grew from the lab sheet to version four. "
        "Second, how every object is assembled from primitives with translate, rotate, scale hierarchies. Third, the base and the walkable underground bunker. "
        "Fourth, the controllable objects and their complex motion. Then the rendering pipeline, the autonomy and game logic, and the testing results. "
        "The final slide plays the two minute demo video.")
    sl.title("Outline", "ThermalSense")
    items = ["Goal and requirements history", "Building with primitives", "The base and the underground bunker",
             "Controllable objects and complex motion", "Rendering pipeline", "Autonomy and game logic",
             "Testing and results", "Demo video (2 minutes)"]
    for i, it in enumerate(items):
        col, row = divmod(i, 4)
        x, y = 0.6 + col * 6.2, 1.9 + row * 1.2
        sl.text(x, y, 0.9, 0.8, f"{i + 3}", 40, AMBER, True)
        sl.text(x + 0.95, y + 0.1, 5.1, 0.8, it, 22)
    slides.append(sl)

    # 3 Goal and history
    sl = Slide(prs, "The goal: a UAV that finds and tracks intruders using only its own thermal camera, in a believable base, built entirely from primitives. "
        "The requirements grew in stages. The lab sheet gave canonical primitives and the matrix M equals T R S. The proposal added shading modes, lights and the Z-buffer. "
        "Version two added PBR and the thermal tracker, version three the RTX 4070 pipeline, and version four the military base, bunker, seven controllable objects and combat.")
    sl.title("Goal and requirements history", "3  |  Goal")
    stages = [("Lab sheet", "Unit cube, sphere, cylinder; M = T·R·S; indexed VAO/VBO/EBO"),
              ("Proposal", "Flat / Gouraud / Phong, 3 lights, Z-buffer hiding, thermal map"),
              ("v2", "PBR, shadows, SSAO, fog; thermal UAV tracker; MicroMouse rover"),
              ("v3", "RTX 4070 EPIC preset, first-person FLIR view, TAA, SSR"),
              ("v4", "Military base, walkable bunker, 7 controllable objects, combat")]
    cw = 2.35
    for i, (a, b) in enumerate(stages):
        x = 0.5 + i * (cw + 0.13)
        sl.rect(x, 2.0, cw, 0.08, AMBER if i == 4 else GREY)
        sl.text(x, 2.15, cw, 0.6, a, 26, AMBER if i == 4 else WHITE, True)
        sl.text(x, 2.85, cw, 1.8, b, 17, GREY)
    sl.image(find_img(3, ["compare_v2", "cine_establishing"]), 0.5, 4.5, 6.0, 2.8)
    sl.image(find_img(3, ["base_overview", "plan_topdown", "scene_visible"]), 6.83, 4.5, 6.0, 2.8)
    slides.append(sl)

    # 4 Primitives
    sl = Slide(prs, "Every visible solid is one part: a unit cube, sphere or cylinder drawn with its own matrix M equals T times R times S. "
        f"Parts are grouped in a hierarchy, so a track link moves with its tank. The scene has {n('nLeaves')} primitive leaves in total. "
        f"For example the bunker has {n('partsBunker')} parts, the tank {n('partsTank')}, the radar {n('partsRadar')}, the UAV {n('partsUav')} and the helicopter {n('partsHeli')}. Cones are just cylinders with a different top radius.")
    sl.title("Building with primitives", "4  |  Construction")
    sl.image(find_img(4, ["primitive_library"]), 0.5, 1.7, 6.1, 2.3)
    sl.image(find_img(4, ["iso_uav_exploded", "iso_uav_assembled"]), 0.5, 4.15, 3.0, 3.0)
    sl.image(find_img(4, ["hierarchy_axes", "scene_primitives_exploded"]), 3.6, 4.15, 3.0, 3.0)
    sl.text(7.0, 1.75, 5.8, 0.6, "cube + sphere + cylinder → objects", 20, AMBER, True)
    sl.text(7.0, 2.35, 5.8, 0.6, "M = T · R · S per part, parent × child", 20)
    for i, (big, small) in enumerate([(n("nLeaves"), "primitive leaves"), (n("partsBunker"), "bunker parts"), (n("partsTank"), "tank parts"),
                                      (n("partsRadar"), "radar parts"), (n("partsUav"), "UAV parts"), (n("partsBase"), "base apron parts")]):
        stat(sl, 7.0 + (i % 2) * 2.95, 3.2 + (i // 2) * 1.3, 2.8, big, small)
    slides.append(sl)

    # 5 Base and bunker
    sl = Slide(prs, "The scene is a night-time base of 36 metres radius with a HESCO perimeter, watch towers, a helipad, motor pool, barracks and a gate barrier. "
        "Under the ground sits the bunker: an entrance ramp, blast doors, a tunnel, a command room, a bunk room and a generator room. "
        "You can walk through it in first person; floors, walls and ceilings are collision volumes, so it behaves like a real interior with its own lighting.")
    sl.title("The base and the underground bunker", "5  |  Environment")
    sl.image(find_img(5, ["base_gate", "base_overview", "cine_gate"]), 0.5, 1.6, 6.1, 3.4, "Base: HESCO wall, towers, gate barrier")
    sl.image(find_img(5, ["bunker_tunnel", "walk_bunker"]), 6.73, 1.6, 6.1, 3.4, "Walkable bunker tunnel")
    sl.image(find_img(5, ["bunker_cic", "iso_bunker_exploded"]), 0.5, 5.15, 4.0, 2.1, "Command room")
    sl.image(find_img(5, ["bunker_door", "iso_bunker_assembled"]), 4.65, 5.15, 4.0, 2.1, "Blast door")
    stat(sl, 8.83, 5.15, 1.95, n("partsBase"), "base parts")
    stat(sl, 10.88, 5.15, 1.95, n("partsBunker"), "bunker parts")
    slides.append(sl)

    # 6 Controllable
    sl = Slide(prs, "Seven objects can be possessed with the keys five to zero or the bracket keys: tank, helicopter, AA gun, radar, blast doors, the UAV, and the gate barrier. "
        "Each has real hierarchical motion: tank track links following an arc-length path, a helicopter with rotor flapping and a spring-damper attitude, a Gatling gun with spin-up, a three-mode radar, and eased sliding doors. "
        "Backspace releases control. The camera switches between chase and sight views.")
    sl.title("Controllable objects and complex motion", "6  |  Control")
    sl.image(find_img(6, ["tank_closeup", "uav_closeup_visible"]), 0.5, 1.6, 4.0, 2.55, "Tank: 80 track links, turret")
    sl.image(find_img(6, ["heli_takeoff", "helipad", "cine_chase"]), 4.65, 1.6, 4.0, 2.55, "Helicopter: rotor flapping")
    sl.image(find_img(6, ["aa_gun_burst", "aa_gun_sight", "cine_watchtower"]), 8.8, 1.6, 4.0, 2.55, "AA gun: Gatling spin-up")
    sl.image(find_img(6, ["radar", "cine_watchtower"]), 0.5, 4.3, 4.0, 2.0, "Radar: 3 modes")
    sl.image(find_img(6, ["possess_doors", "bunker_door", "cine_gate"]), 4.65, 4.3, 4.0, 2.0, "Blast doors / barrier")
    sl.rect(8.8, 4.3, 4.0, 2.0, PANEL)
    sl.text(8.9, 4.35, 3.8, 1.9, ["Key map", "5 tank   6 heli   7 AA gun", "8 radar   9 doors   0 UAV", "[ ]  cycle     Backspace  release",
                                  "WASD move  Enter fire"], 15, WHITE)
    sl.text(0.5, 6.5, 12.3, 0.6, "7 possessable objects: tank, helicopter, AA gun, radar, blast doors, gate barrier, UAV", 18, AMBER)
    slides.append(sl)

    # 7 Pipeline
    sl = Slide(prs, "The frame is rendered in linear light into an HDR target. GGX Cook-Torrance PBR is the base, with cascaded moon shadows, screen-space ambient occlusion, volumetric fog with light shafts, "
        "temporal anti-aliasing, screen-space reflections in puddles and bloom, finished by an ACES tone curve. The thermal view is a sensor model: automatic gain, noise, shimmer and a FLIR style HUD. "
        "The preset is chosen automatically, EPIC on the RTX 4070.")
    sl.title("Rendering pipeline", "7  |  Rendering")
    sl.image(find_img(7, ["scene_visible", "base_overview"]), 0.5, 1.6, 6.1, 3.3, "Visible: PBR, shadows, fog")
    sl.image(find_img(7, ["thermal_fpv_strike", "thermal_fpv", "scene_thermal"]), 6.73, 1.6, 6.1, 3.3, "Thermal sensor view")
    chips = ["PBR GGX", "Shadows", "SSAO", "Fog", "TAA", "SSR", "Bloom", "Thermal sensor"]
    for i, c in enumerate(chips):
        x, y = 0.5 + (i % 4) * 3.1, 5.2 + (i // 4) * 1.0
        sl.rect(x, y, 2.95, 0.8, PANEL, AMBER)
        sl.text(x, y, 2.95, 0.8, c, 22, WHITE, True, PP_ALIGN.CENTER, MSO_ANCHOR.MIDDLE)
    slides.append(sl)

    # 8 Autonomy
    sl = Slide(prs, "The drone works from its own thermal image: blobs are found by flood fill, tracked, and after a lock of one and a half seconds it fires a rail round. "
        "The missile homes with proportional navigation, the rover explodes into rigid-body debris and leaves a burning wreck that the drone learns to ignore. "
        "Friendly vehicles are never targeted. After a short delay the rover respawns, and the intruders hide behind obstacles using a MicroMouse planner.")
    sl.title("Autonomy and game logic", "8  |  Simulation")
    sl.image(find_img(8, ["thermal_fpv_strike", "thermal_fpv"]), 0.5, 1.6, 4.0, 2.4, "Thermal tracking")
    sl.image(find_img(8, ["explosion_mid", "uav_missile_launch", "thermal_fpv_hidden"]), 4.65, 1.6, 4.0, 2.4, "Blast")
    sl.image(find_img(8, ["wreck_burning", "hide_thermal"]), 8.8, 1.6, 4.0, 2.4, "Burning wreck / hiding rover")
    steps = ["Thermal track", "Lock 1.5 s", "Rail round", "Blast", "Debris + wreck", "Respawn"]
    bw = 1.95
    for i, st in enumerate(steps):
        x = 0.5 + i * (bw + 0.12)
        sl.rect(x, 4.45, bw, 0.9, PANEL, AMBER)
        sl.text(x, 4.45, bw, 0.9, st, 17, WHITE, True, PP_ALIGN.CENTER, MSO_ANCHOR.MIDDLE)
    bullets(sl, 0.6, 5.6, 12, ["Wreck stays hot: ignored 30 s;  friendly vehicles never targeted (IFF 4.5 m)",
                               "Rovers hide: MicroMouse flood fill + line follower, 3 intruders"], 18, 0.6)
    slides.append(sl)

    # 9 Testing
    sl = Slide(prs, f"Quality is checked by {n('nTests')} automated tests, all passing: they cover the primitives, matrices, shading, the game logic, the controllable objects and the bunker walk space. "
        "The Excel workbook documents every transform and part from an export of the live scene, and a verification script recomputes it. "
        "The report has 29 pages with an audit appendix. The target is Windows with MinGW, Linux and headless EGL, tuned for the RTX 4070.")
    sl.title("Testing and results", "9  |  Results")
    stat(sl, 0.5, 1.7, 2.9, f"{n('nPass')} / {n('nTests')}", "automated tests pass")
    stat(sl, 3.55, 1.7, 2.9, "18", "workbook sheets")
    stat(sl, 6.6, 1.7, 2.9, "29", "report pages")
    stat(sl, 9.65, 1.7, 3.2, "3", "platforms")
    bullets(sl, 0.6, 3.2, 5.8, ["CPU-only tests: no GPU, no window", "Excel workbook, regenerated from the scene export", "Report with audit appendix",
                                "Windows (MinGW) · Linux · headless EGL"], 19, 0.7)
    sl.image(find_img(9, ["compare_v2", "scene_thermal", "base_overview"]), 6.7, 3.2, 6.1, 3.9, "Results gallery")
    slides.append(sl)

    # 10 Video
    sl = Slide(prs, "And now the demonstration: a two minute cinematic sequence recorded in the program itself. You will see the base at night, the walk through the bunker, the possessed tank, helicopter and AA gun, "
        "then the drone locking onto a rover, the rail round, the explosion and the burning wreck, in thermal and visible views. Thank you; questions are welcome afterwards.")
    sl.title("Demo video", "10  |  Demonstration, 2 minutes")
    poster = find_img(10, ["explosion_mid", "thermal_fpv_strike", "base_overview", "cine_establishing"])
    vx, vy, vw, vh = 1.67, 1.5, 10.0, 5.625
    pj = jpeg(poster) if poster else None
    sl.rec.append(("img", vx, vy, vw, vh, pj))
    if os.path.exists(VIDEO):
        sl.s.shapes.add_movie(VIDEO, Inches(vx), Inches(vy), Inches(vw), Inches(vh), poster_frame_image=pj, mime_type="video/mp4")
        video = "embedded"
    else:
        if pj: sl.s.shapes.add_picture(pj, Inches(vx), Inches(vy), Inches(vw), Inches(vh))
        sl.rect(vx, vy + vh / 2 - 0.5, vw, 1.0, BG)
        sl.text(vx, vy + vh / 2 - 0.5, vw, 1.0, "demo_v4.mp4 pending", 36, AMBER, True, PP_ALIGN.CENTER, MSO_ANCHOR.MIDDLE)
        video = "placeholder"
    slides.append(sl)
    prs.save(OUT)
    return slides, video

# ---------------- schematic preview ----------------
def fnt(sz):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"):
        if os.path.exists(p): return ImageFont.truetype(p, sz)
    return ImageFont.load_default()

def preview(slides):
    sc = 70  # px per inch
    tw, th = int(W * sc), int(H * sc)
    cols = 2; rows = (len(slides) + 1) // 2
    sheet = Image.new("RGB", (cols * (tw + 10) + 10, rows * (th + 10) + 10), (60, 60, 60))
    for i, sl in enumerate(slides):
        im = Image.new("RGB", (tw, th), (8, 10, 13)); d = ImageDraw.Draw(im)
        for r in sl.rec:
            if r[0] == "rect":
                _, x, y, w, h, c, ln = r
                d.rectangle([x * sc, y * sc, (x + w) * sc, (y + h) * sc], fill=tuple(c), outline=tuple(ln) if ln else None)
            elif r[0] == "img":
                _, x, y, w, h, j = r
                if j:
                    pi = Image.open(j); ar, br = pi.width / pi.height, w / h
                    if ar > br: nw = int(pi.height * br); pi = pi.crop(((pi.width - nw) // 2, 0, (pi.width + nw) // 2, pi.height))
                    else: nh = int(pi.width / br); pi = pi.crop((0, (pi.height - nh) // 2, pi.width, (pi.height + nh) // 2))
                    im.paste(pi.resize((max(1, int(w * sc)), max(1, int(h * sc)))), (int(x * sc), int(y * sc)))
            else:
                _, x, y, w, h, paras, size, color, bold, align = r
                f = fnt(max(6, int(size * sc / 72 * 0.95))); yy = y * sc
                for t in paras:
                    words, line, lines = t.split(), "", []
                    for wd in words:
                        if d.textlength((line + " " + wd).strip(), font=f) <= w * sc - 6: line = (line + " " + wd).strip()
                        else: lines.append(line); line = wd
                    lines.append(line)
                    for ln in lines:
                        tx = x * sc + 3
                        if align == PP_ALIGN.CENTER: tx = x * sc + (w * sc - d.textlength(ln, font=f)) / 2
                        d.text((tx, yy), ln, font=f, fill=tuple(color)); yy += f.size * 1.2
                    yy += size * sc / 72 * 0.35
                if yy > (y + h) * sc + 8: print(f"  WARN overflow slide {i+1}: {paras[0][:40]!r} ({yy/sc:.2f} > {y+h:.2f} in)")
        sheet.paste(im, (10 + (i % cols) * (tw + 10), 10 + (i // cols) * (th + 10)))
    os.makedirs(os.path.dirname(PREVIEW), exist_ok=True)
    sheet.save(PREVIEW)

if __name__ == "__main__":
    slides, video = build()
    preview(slides)
    for u in used: print("image", u)
    print("video:", video, "| size: %.1f MB" % (os.path.getsize(OUT) / 1e6), "|", OUT)
