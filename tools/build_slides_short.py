#!/usr/bin/env python3
"""Build report/ThermalSense_Presentation_10.pptx: the short deck (10 slides, 16:9, dark theme).

Run from the repository root:
    ~/.local/bin/uv run -q --with python-pptx --with pillow --with matplotlib python3 tools/build_slides_short.py

Slides: 1 title, 2 film (embedded movie with a poster frame), 3 contents, 4 pipeline, 5 numbers (table),
6 lighting by hand (typeset equation), 7 rover path planning (complexity table), 8 computer vision (cost table),
9 defence and vehicles, 10 thank you.  Every slide carries the date and "n / 10" in the footer.

Every number on a slide is listed with its source next to the place where it is used (search for "source:").
Pictures are cropped to their tiles here (cover fit, never stretched), at most 1920 px wide, JPEG quality 88.
"""
import datetime, io, os, sys, tempfile, zipfile

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from lxml import etree
from PIL import Image
from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Inches, Pt

# ---------------------------------------------------------------- things to change
DATE = "11 October 2026"                      # footer date on every slide
TOTAL = 10                                    # footer "n / TOTAL"; the build stops if the count differs
AUTHOR = "Md. Shakhoyat Rahman Shujon"
ROLL = "2107104"
UAV_FULL = "uncrewed aerial vehicle"          # the full form the report uses for UAV

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REP = os.path.join(ROOT, "report")
OUT = os.path.join(REP, "ThermalSense_Presentation_10.pptx")
VIDEO = os.path.join(REP, "video", "thermalsense_final_slide.mp4")           # 960x540, 3 min 38 s
POSTER = os.path.join(REP, "video", "thermalsense_final_1080p_poster.jpg")
TMP = tempfile.mkdtemp(prefix="ts_slides10_")

# ---------------------------------------------------------------- theme (same palette as tools/build_slides.py)
BG, PANEL, PANEL2 = RGBColor(0x08, 0x0A, 0x0D), RGBColor(0x14, 0x18, 0x1E), RGBColor(0x0F, 0x12, 0x17)
AMBER, WHITE, GREY, DIM = RGBColor(0xFF, 0x9E, 0x1F), RGBColor(0xFF, 0xFF, 0xFF), RGBColor(0xA8, 0xB0, 0xBA), RGBColor(0x2A, 0x31, 0x3A)
W, H = 13.333, 7.5
MX = 0.5                      # left and right margin
CW = W - 2 * MX               # content width
Y0, Y1 = 1.55, 6.88           # content top and bottom on slides 3 to 9
FONT, MONO = "Segoe UI", "Consolas"
L, C, R = PP_ALIGN.LEFT, PP_ALIGN.CENTER, PP_ALIGN.RIGHT
TOP, MID = MSO_ANCHOR.TOP, MSO_ANCHOR.MIDDLE


def fig(*parts):
    p = os.path.join(REP, *parts)
    if not os.path.exists(p):
        sys.exit("missing picture: " + p)
    return p


# ---------------------------------------------------------------- pictures
_n = [0]
def _save(im):
    _n[0] += 1
    out = os.path.join(TMP, f"img{_n[0]:03d}.jpg")
    im.convert("RGB").save(out, "JPEG", quality=88)
    return out


def cover(path, w, h, crop=None, focus=(0.5, 0.5), nearest=False):
    """JPEG of 'path' cropped to the aspect w:h (inches). crop = (left, top, right, bottom) in source pixels is applied
    first; focus = where the kept window sits when the picture is wider or taller than the tile (0 .. 1)."""
    im = Image.open(path).convert("RGB")
    if crop:
        im = im.crop(crop)
    ar, br = im.width / im.height, w / h
    if ar > br:
        nw = round(im.height * br); x = round((im.width - nw) * focus[0]); im = im.crop((x, 0, x + nw, im.height))
    elif ar < br:
        nh = round(im.width / br); y = round((im.height - nh) * focus[1]); im = im.crop((0, y, im.width, y + nh))
    want = min(1920, round(w * 220))
    if im.width > want:
        im = im.resize((want, round(want / br)), Image.LANCZOS)
    elif im.width < w * 110:                       # a small detail blown up (mask crops): keep the pixels square
        k = max(2, round(w * 150 / im.width))
        im = im.resize((im.width * k, im.height * k), Image.NEAREST if nearest else Image.LANCZOS)
    return _save(im)


def hero(path, top=0.25, bottom=0.92, start=0.30, flat=0.0):
    """Full-slide picture with the theme colour faded in towards the bottom (so text on it stays readable)."""
    im = Image.open(cover(path, W, H)).convert("RGB")
    w, h = im.size
    mask = Image.new("L", (1, h))
    for y in range(h):
        t = y / (h - 1)
        a = flat + (top if t < start else top + (bottom - top) * ((t - start) / (1 - start)) ** 1.2) * (1 - flat)
        mask.putpixel((0, y), round(255 * min(a, 1.0)))
    im = Image.composite(Image.new("RGB", (w, h), (8, 10, 13)), im, mask.resize((w, h)))
    return _save(im)


def mosaic(paths, w, h):
    """2 x 2 mosaic of four pictures, aspect w:h."""
    px = 1100; py = round(px * h / w)
    out = Image.new("RGB", (px, py), (8, 10, 13)); g = 6
    cw, ch = (px - g) // 2, (py - g) // 2
    for i, p in enumerate(paths):
        t = Image.open(cover(p, cw / 100, ch / 100)).resize((cw, ch), Image.LANCZOS)
        out.paste(t, ((i % 2) * (cw + g), (i // 2) * (ch + g)))
    return _save(out)


plt.rcParams["mathtext.fontset"] = "stix"        # a Times-like face, as in the report
plt.rcParams["font.family"] = "STIXGeneral"

def eq_png(tex, name, size=30, color="white"):
    """One line of mathtext as a transparent PNG."""
    f = plt.figure(figsize=(1, 1))
    f.text(0, 0, tex, fontsize=size, color=color)
    out = os.path.join(TMP, name + ".png")
    f.savefig(out, dpi=220, transparent=True, bbox_inches="tight", pad_inches=0.03)
    plt.close(f)
    return out


def eq_labelled(pieces, name, size=34, color="white", accent="#FF9E1F"):
    """A row of mathtext pieces on one baseline; a piece with a label gets a rule and the label under it."""
    f = plt.figure(figsize=(16, 2.2), dpi=220)
    r = f.canvas.get_renderer()
    x, y = 0.01, 0.55
    spans = []
    for tex, label in pieces:
        t = f.text(x, y, tex, fontsize=size, color=color, va="baseline", ha="left")
        bb = t.get_window_extent(r).transformed(f.transFigure.inverted())
        if label:
            spans.append((bb.x0, bb.x1, label))
        x = bb.x1 + 0.002
    for x0, x1, label in spans:
        f.add_artist(plt.Line2D([x0, x1], [0.30, 0.30], color=accent, lw=2.2))
        f.text((x0 + x1) / 2, 0.08, label, fontsize=size * 0.55, color=accent, ha="center", va="baseline", style="italic")
    out = os.path.join(TMP, name + ".png")
    f.savefig(out, dpi=220, transparent=True, bbox_inches="tight", pad_inches=0.04)
    plt.close(f)
    return out


# ---------------------------------------------------------------- slide helpers
class Deck:
    def __init__(self):
        self.prs = Presentation()
        self.prs.slide_width, self.prs.slide_height = Inches(W), Inches(H)
        self.slides = []
        self.words = {}          # slide number -> body words (tables, equation and tile labels are not counted)

    def new(self, notes):
        s = Slide(self, len(self.slides) + 1, notes)
        self.slides.append(s)
        return s


class Slide:
    def __init__(self, deck, n, notes):
        self.deck, self.n = deck, n
        self.s = deck.prs.slides.add_slide(deck.prs.slide_layouts[6])
        bg = self.s.background.fill; bg.solid(); bg.fore_color.rgb = BG
        self.s.notes_slide.notes_text_frame.text = notes
        deck.words[n] = 0

    def rect(self, x, y, w, h, color=PANEL, line=None, shape=MSO_SHAPE.RECTANGLE):
        sh = self.s.shapes.add_shape(shape, Inches(x), Inches(y), Inches(w), Inches(h))
        sh.fill.solid(); sh.fill.fore_color.rgb = color
        if line: sh.line.color.rgb = line; sh.line.width = Pt(1)
        else: sh.line.fill.background()
        sh.shadow.inherit = False
        return sh

    def text(self, x, y, w, h, text, size=18, color=WHITE, bold=False, align=L, anchor=TOP, font=FONT, body=False, gap=0.3):
        """text: one string, or a list of paragraphs; a paragraph is a string or a list of (string, colour, bold) runs."""
        tb = self.s.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
        tf = tb.text_frame; tf.word_wrap = True; tf.vertical_anchor = anchor
        tf.margin_left = tf.margin_right = Inches(0.05); tf.margin_top = tf.margin_bottom = Inches(0.03)
        paras = text if isinstance(text, list) else [text]
        for i, para in enumerate(paras):
            p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
            p.alignment = align; p.space_after = Pt(size * gap)
            runs = para if isinstance(para, list) else [(para, color, bold)]
            for t, col, b in runs:
                r = p.add_run(); r.text = t
                r.font.size = Pt(size); r.font.bold = b; r.font.name = font; r.font.color.rgb = col
                if body: self.deck.words[self.n] += len(t.split())
        return tb

    def pic(self, path, x, y, w, h, **kw):
        return self.s.shapes.add_picture(cover(path, w, h, **kw), Inches(x), Inches(y), Inches(w), Inches(h))

    def png(self, path, x, y, w, h, align="c"):
        """A transparent PNG kept whole inside the box (x, y, w, h)."""
        im = Image.open(path); ar = im.width / im.height
        if ar > w / h: nw, nh = w, w / ar
        else: nw, nh = h * ar, h
        px = x if align == "l" else x + (w - nw) / 2
        return self.s.shapes.add_picture(path, Inches(px), Inches(y + (h - nh) / 2), Inches(nw), Inches(nh))

    def arrow(self, x, y, w=0.26, h=0.3):
        sh = self.s.shapes.add_shape(MSO_SHAPE.RIGHT_ARROW, Inches(x), Inches(y), Inches(w), Inches(h))
        sh.fill.solid(); sh.fill.fore_color.rgb = AMBER; sh.line.fill.background(); sh.shadow.inherit = False

    def chip(self, x, y, label, s=0.42):
        self.rect(x, y, s, s, AMBER)
        self.text(x, y, s, s, label, 16, BG, True, C, MID)

    def caption(self, x, y, w, h, label, size=13):
        """Caption bar along the bottom edge of the picture in (x, y, w, h)."""
        self.rect(x - 0.01, y + h - 0.36, w + 0.02, 0.38, BG)
        self.text(x + 0.05, y + h - 0.36, w - 0.1, 0.36, label, size, AMBER, False, L, MID)

    def title(self, title, kicker):
        self.rect(0, 0, 0.12, H, AMBER)
        self.text(MX, 0.28, 9.0, 0.32, kicker.upper(), 13, AMBER, True)
        self.text(MX, 0.52, CW, 0.8, title, 32, WHITE, True)

    def footer(self):
        self.text(MX, 7.04, 4.0, 0.3, DATE, 11, GREY, anchor=MID)
        self.text(W - MX - 2.0, 7.04, 2.0, 0.3, f"{self.n} / {TOTAL}", 11, GREY, False, R, MID)

    def table(self, x, y, widths, rows, row_h=0.5, size=15, head_size=13, bold_cols=(), accent_cols=(), aligns=None):
        """Native table. rows[0] is the header. Cells are strings."""
        nr, nc = len(rows), len(widths)
        gf = self.s.shapes.add_table(nr, nc, Inches(x), Inches(y), Inches(sum(widths)), Inches(row_h * nr))
        tbl = gf.table
        style = tbl._tbl.tblPr.find(qn("a:tableStyleId"))
        if style is None:
            style = etree.SubElement(tbl._tbl.tblPr, qn("a:tableStyleId"))
        style.text = "{2D5ABB26-0587-4C30-8999-92F81FD0307C}"       # "No Style, No Grid": every colour is set below
        tbl.first_row = True; tbl.horz_banding = False
        for j, cw in enumerate(widths): tbl.columns[j].width = Inches(cw)
        for i in range(nr): tbl.rows[i].height = Inches(row_h)
        aligns = aligns or [L] * nc
        for i, row in enumerate(rows):
            for j, val in enumerate(row):
                c = tbl.cell(i, j)
                c.fill.solid(); c.fill.fore_color.rgb = PANEL if i == 0 else (PANEL2 if i % 2 else BG)
                c.margin_left = c.margin_right = Inches(0.12); c.margin_top = c.margin_bottom = Inches(0.03)
                c.vertical_anchor = MID
                tf = c.text_frame; tf.word_wrap = True
                p = tf.paragraphs[0]; p.alignment = aligns[j]
                r = p.add_run(); r.text = val
                head = i == 0
                r.font.name = FONT; r.font.size = Pt(head_size if head else size)
                r.font.bold = head or j in bold_cols
                r.font.color.rgb = AMBER if (head or j in accent_cols) else WHITE
                _borders(c, bottom=AMBER if head else DIM, bottom_w=1.5 if head else 0.75)
        return gf


def _borders(cell, bottom, bottom_w):
    """Only a bottom rule on a table cell (the line elements must come before the fill in the cell properties)."""
    tcPr = cell._tc.get_or_add_tcPr()
    for tag in ("a:lnL", "a:lnR", "a:lnT", "a:lnB"):
        for old in tcPr.findall(qn(tag)): tcPr.remove(old)
    made = []
    for tag in ("a:lnL", "a:lnR", "a:lnT"):
        ln = etree.Element(qn(tag)); ln.set("w", "0"); etree.SubElement(ln, qn("a:noFill")); made.append(ln)
    ln = etree.Element(qn("a:lnB")); ln.set("w", str(int(bottom_w * 12700))); ln.set("cap", "flat"); ln.set("cmpd", "sng")
    sf = etree.SubElement(ln, qn("a:solidFill")); clr = etree.SubElement(sf, qn("a:srgbClr")); clr.set("val", str(bottom))
    made.append(ln)
    for k, ln in enumerate(made): tcPr.insert(k, ln)


# ---------------------------------------------------------------- the ten slides
def build():
    d = Deck()

    # ---- 1 Title
    s = d.new("ThermalSense: a military outpost renderer and game for Computer Graphics Laboratory, CSE 4102, "
              "written in C++17 on OpenGL 3.3 core.")
    title_picture = hero(fig("figures", "img", "showcase", "sc_overview_dusk.jpg"))
    s.s.shapes.add_picture(title_picture, 0, 0, Inches(W), Inches(H))
    s.rect(0.6, 3.72, 1.4, 0.07, AMBER)
    s.text(0.6, 3.82, 12.0, 1.05, "ThermalSense", 60, WHITE, True)
    s.text(0.6, 4.85, 12.0, 0.6, "A graphics pipeline, end to end, in a military outpost", 28, AMBER)
    s.text(0.6, 5.65, 12.0, 0.45, [[(AUTHOR, WHITE, True), (f"   |   Roll {ROLL}", WHITE, False)]], 20)
    s.text(0.6, 6.12, 12.0, 0.4, "Computer Graphics Laboratory, CSE 4102", 17, GREY)
    s.text(0.6, 6.48, 12.0, 0.4, "Khulna University of Engineering & Technology (KUET)", 17, GREY)
    s.footer()

    # ---- 2 Film (embedded movie with a poster frame)
    s = d.new("The project film, 3 minutes 38 seconds. Click the picture to play it.")
    s.rect(0, 0, 0.12, H, AMBER)
    vw = 11.2; vh = vw * 9 / 16; vx = (W - vw) / 2; vy = 0.32
    poster = cover(POSTER, vw, vh)
    if not os.path.exists(VIDEO):
        sys.exit("missing film: " + VIDEO)
    s.s.shapes.add_movie(VIDEO, Inches(vx), Inches(vy), Inches(vw), Inches(vh), poster_frame_image=poster, mime_type="video/mp4")
    s.text(vx, vy + vh + 0.04, vw, 0.4, [[("Project film", WHITE, True), ("   |   3 min 38 s   (1080p, audio enabled)", GREY, False)]], 16, align=C, anchor=MID, body=True)
    s.footer()

    # ---- 3 Contents (numbered tiles for slides 4 to 9)
    s = d.new("Six parts: the pipeline, the numbers, the lighting written by hand, the rovers' path planning, "
              "the computer vision, and the defence with the vehicles the player can control.")
    s.title("Contents", "3  |  Overview")
    tiles = [("04", "Graphics pipeline", fig("figures", "scene_primitives_exploded.png"), dict(focus=(0.5, 0.45))),
             ("05", "Project in numbers", fig("figures", "img", "scene", "day_overview.jpg"), dict(focus=(0.5, 0.6))),
             ("06", "Lighting by hand", fig("figures", "img", "lab", "materials_pbr_grid.jpg"), dict(crop=(296, 165, 1025, 498))),
             ("07", "Rover path planning", fig("figures_v6", "plan_routes.png"), dict(crop=(420, 250, 1500, 1000))),
             ("08", "Real-time vision", fig("figures_final", "final_flir.jpg"), dict(crop=(250, 130, 1650, 760))),
             ("09", "Defence and vehicles", fig("figures_final", "final_single.jpg"), dict(focus=(0.5, 0.35)))]
    gx, gy = 0.20, 0.16
    tw = (CW - 2 * gx) / 3; ih, bh = 2.05, 0.54
    for i, (num, name, path, kw) in enumerate(tiles):
        x = MX + (i % 3) * (tw + gx); y = Y0 + (i // 3) * (ih + bh + gy)
        s.pic(path, x, y, tw, ih, **kw)
        s.rect(x, y + ih, tw, bh, PANEL)
        s.rect(x, y + ih, tw, 0.04, AMBER)
        s.text(x + 0.10, y + ih + 0.02, 0.65, bh - 0.04, num, 24, AMBER, True, L, MID)
        s.text(x + 0.78, y + ih + 0.02, tw - 0.88, bh - 0.04, name, 17, WHITE, True, L, MID)
    s.footer()

    # ---- 4 Pipeline as a flow
    # source: 16 primitive generators, C++17, OpenGL 3.3 core / 4.3 compute, no built-in vector function in any shader,
    # no mathematics library (project facts); M = T R S: report/sections/A02_transforms.tex, equation eq:trs;
    # fog, bloom, tone mapping: Frame Explorer stages 12 to 14 (tools/build_slides.py st_titles, report/figures_v7/explorer_stages.json)
    s = d.new("The flow of one frame: primitives are generated in code, placed by hierarchical transformations, "
              "lit by hand-written vector maths, drawn by the rasteriser or the ray tracer, then post-processed.")
    s.title("The graphics pipeline, end to end", "4  |  Pipeline")
    n5 = 5; tw = 2.25; gap = (CW - n5 * tw) / (n5 - 1); ih = 3.52; lh = 0.92
    prim = [fig("figures", "img", "m3", "prims", f) for f in ("prim_03_sphere_wire.jpg", "prim_08_torus_wire.jpg",
                                                              "prim_14_helix_wire.jpg", "prim_07_cone_wire.jpg")]
    stages = [("Primitives", "16 generators in code"),
              ("Hierarchy", "M = T · R · S, graph"),
              ("Lighting", "vector maths by hand"),
              ("Rendering", "raster / ray tracer"),
              ("Post-process", "fog, bloom, tonemap")]
    for i, (name, sub) in enumerate(stages):
        x = MX + i * (tw + gap)
        if i == 0:
            s.s.shapes.add_picture(mosaic(prim, tw, ih), Inches(x), Inches(Y0), Inches(tw), Inches(ih))
        elif i == 1:
            s.pic(fig("figures", "img", "scene", "soldier_axes.jpg"), x, Y0, tw, ih, crop=(676, 120, 1315, 1020))
        elif i == 2:
            s.pic(fig("figures", "img", "lab", "materials_pbr_grid.jpg"), x, Y0, tw, ih, crop=(316, 163, 826, 881))
        elif i == 3:
            hh = (ih - 0.05) / 2
            s.pic(fig("figures_v7", "explorer_stage_04_raster.png"), x, Y0, tw, hh, crop=(20, 110, 540, 471))
            s.pic(fig("figures_v7", "raytrace_mode_whitted.png"), x, Y0 + hh + 0.05, tw, hh, crop=(380, 300, 900, 650))
        else:
            s.pic(fig("figures_final", "final_single.jpg"), x, Y0, tw, ih, crop=(604, 60, 1296, 1035))
        s.chip(x, Y0, str(i + 1))
        s.rect(x, Y0 + ih, tw, lh, PANEL)
        s.rect(x, Y0 + ih, tw, 0.04, AMBER)
        s.text(x + 0.08, Y0 + ih + 0.08, tw - 0.16, lh - 0.12, [[(name, WHITE, True)], [(sub, GREY, False)]], 15, gap=0.18)
        if i < n5 - 1:
            s.arrow(x + tw + (gap - 0.26) / 2, Y0 + ih / 2 - 0.15)
    s.text(MX, Y0 + ih + lh + 0.18, CW, 0.45,
           [[("No built-in vector function in any shader", AMBER, True), ("     |     ", DIM, False),
             ("No mathematics library", AMBER, True), ("     |     ", DIM, False),
             ("C++17, OpenGL 3.3 core", WHITE, False)]], 18, align=C, anchor=MID, body=True)
    s.footer()

    # ---- 5 Numbers (native table)
    # source: project facts checked against the code and the report (16 generators, 62 designs, 5,474 parts, 905 calls in
    # 13 shader source files, 396 tests, 192 pages, 3 min 38 s, C++17, OpenGL 3.3 core, 4.3 compute, no mathematics library)
    s = d.new("The key figures: 16 primitive generators build 62 designs and 5,474 parts; 905 calls of hand-written vector "
              "functions in 13 shader source files; 396 automated tests; a 192-page report and a film of 3 minutes 38 seconds.")
    s.title("The project in numbers", "5  |  Scale")
    rows = [["Measure", "Value"],
            ["Primitive generators", "16"],
            ["Designs built from them", "62"],
            ["Parts in the scene", "5,474"],
            ["Calls of hand-written vector functions", "905, in 13 shader files"],
            ["Automated tests", "396"],
            ["Report", "192 pages"],
            ["Film", "3 min 38 s"],
            ["Language and graphics interface", "C++17, OpenGL 3.3 core"],
            ["Ray tracer", "OpenGL 4.3 compute"],
            ["Mathematics library", "none"]]
    rh = (Y1 - Y0) / len(rows)
    s.table(MX, Y0, [3.15, 2.05], rows, row_h=rh, size=15, head_size=13, bold_cols=(1,), accent_cols=(1,))
    ix = MX + 5.20 + 0.30
    iw = W - MX - ix; ihh = Y1 - Y0 - 0.40
    s.pic(fig("figures", "scene_primitives_exploded.png"), ix, Y0, iw, ihh, crop=(620, 10, 1600, 900), focus=(1.0, 0.5))
    s.rect(ix, Y0 + ihh, iw, 0.40, PANEL)
    s.text(ix + 0.08, Y0 + ihh, iw - 0.16, 0.40, "The camp drawn by primitive type: cube, sphere, cylinder, cone", 13, AMBER, anchor=MID)
    s.footer()

    # ---- 6 Lighting and shading by hand
    # source: equation: report/sections/A10_illumination.tex, equations eq:phong and eq:phongblinn (Blinn-Phong is the one used);
    # functions: src/rendering/ShaderMath.cpp lines 11 to 76 (tsDot ... tsSmoothstep), refusal of built-in calls:
    # src/rendering/ShaderMath.h lines 16 to 18; 905 calls in 13 shader source files: project fact
    s = d.new("The lighting equation is evaluated with the half vector (Blinn-Phong). Flat shading uses one normal per face, "
              "Gouraud lights the vertices, Phong lights every pixel. Dot, normalise, cross, reflect, refract, mix, clamp, step "
              "and smooth step are written out in one file and placed in front of every shader.")
    s.title("Lighting and shading, written by hand", "6  |  Illumination")
    eh = 1.65
    s.rect(MX, Y0, CW, eh, PANEL); s.rect(MX, Y0, 0.06, eh, AMBER)
    main = eq_labelled([(r"$I \;=\;$", None), (r"$k_a I_a$", "ambient"),
                        (r"$\;+\;\sum_{l}\, f_{\mathrm{att},l}\; c_l\, I_l\; \mathrm{[}\;$", None),
                        (r"$k_d\,\max(0,\ \mathbf{N}\cdot\mathbf{L}_l)$", "diffuse"), (r"$\;+\;$", None),
                        (r"$k_s\,\max(0,\ \mathbf{N}\cdot\mathbf{H}_l)^{n}$", "specular (Blinn-Phong)"), (r"$\;\mathrm{]}$", None)], "eq_main", size=32)
    s.png(main, MX + 0.3, Y0 + 0.10, 8.6, eh - 0.20, align="l")
    half = eq_png(r"$\mathbf{H}=\frac{\mathbf{L}+\mathbf{V}}{\Vert\mathbf{L}+\mathbf{V}\Vert}$", "eq_half", 28)
    phong = eq_png(r"Phong:  $k_s\,\max(0,\ \mathbf{R}\cdot\mathbf{V})^{n}$", "eq_phong", 21, "#A8B0BA")
    s.png(half, MX + 9.15, Y0 + 0.18, 2.9, 0.72)
    s.png(phong, MX + 9.15, Y0 + 1.05, 2.9, 0.40)
    y2 = Y0 + eh + 0.18; sw = 2.95; sg = 0.16; sih = Y1 - y2 - 0.55
    shade = [("Flat", "one normal per face", "shading_sphere_lowpoly_flat.png"),
             ("Gouraud", "lit at vertices", "shading_sphere_lowpoly_gouraud.png"),
             ("Phong", "lit at every pixel", "shading_sphere_lowpoly_phong.png")]
    for i, (name, sub, f) in enumerate(shade):
        x = MX + i * (sw + sg)
        s.pic(fig("figures_lab", f), x, y2, sw, sih, crop=(40, 60, 600, 600))
        s.rect(x, y2 + sih, sw, 0.55, PANEL)
        s.text(x + 0.08, y2 + sih, sw - 0.16, 0.55, [[(name, WHITE, True), ("   " + sub, GREY, False)]], 14, anchor=MID)
    fx = MX + 3 * sw + 2 * sg + 0.22; fw = W - MX - fx; fh = Y1 - y2
    s.rect(fx, y2, fw, fh, PANEL); s.rect(fx, y2, fw, 0.04, AMBER)
    s.text(fx + 0.14, y2 + 0.12, fw - 0.28, 0.28, "VECTOR MATHS IN SHADERS", 12, GREY, True)
    s.text(fx + 0.14, y2 + 0.44, fw - 0.28, 1.4,
           ["tsDot         tsLength", "tsNormalize   tsCross", "tsReflect     tsRefract", "tsMix         tsClamp",
            "tsStep        tsSmoothstep"], 13, WHITE, font=MONO, gap=0.20)
    s.text(fx + 0.14, y2 + fh - 1.15, fw - 0.28, 0.50, [[("905 calls", AMBER, True), ("  in 13 shaders", WHITE, False)]], 20, body=True)
    s.text(fx + 0.14, y2 + fh - 0.58, fw - 0.28, 0.48, "Built-in GLSL calls refused", 13, GREY, body=True)
    s.footer()

    # ---- 7 Rover path planning
    # source: grid 80 x 80 cells of 0.5 m: src/ai/HideController.h lines 21, 22;
    # flood fill = breadth-first, std::queue, 4 neighbours: src/ai/HideController.cpp lines 132 to 169 (each cell enters the queue once);
    # Dijkstra with std::priority_queue (binary heap), step cost 16 + exposure + noise: HideController.cpp lines 216 to 251;
    # steepest descent to a cell path: lines 171 to 198; line follower with 5 sensors (HideController.h line 23), each tested
    # against every guide-line segment: HideController.cpp lines 441 to 451 and 481 to 489; PD law: line 502;
    # gains Kp 3.2, Kd 0.25: line 22; re-plan periods 0.4 s and 1.6 s: lines 23, 24; re-plan when blocked: line 532;
    # route map: report/figures/plots/p_rover_map.png (4 rovers in its legend), 130 s: report/sections/B09_paths.tex, caption of fig:rovermap
    s = d.new("Each intruder rover plans like a micromouse and drives like a line-follower robot. Hiding, a breadth-first "
              "flood fill spreads distances from the cells out of sight; infiltrating, Dijkstra adds a cost for cells a camera "
              "can see. The path becomes a guide line that five sensors and a proportional-derivative law follow, and the plan "
              "is redone every 0.4 or 1.6 seconds.")
    s.title("Intruder rovers: path planning", "7  |  Flood fill, Dijkstra, line follower")
    mw = 6.95; mh = mw * 855 / 1170
    s.rect(MX, Y0, mw, mh, WHITE)
    s.pic(fig("figures", "plots", "p_rover_map.png"), MX + 0.05, Y0 + 0.05, mw - 0.1, mh - 0.1, crop=(75, 60, 1245, 915))
    s.text(MX, Y0 + mh + 0.06, mw, 0.30, f"Route map: 4 rovers, two UAVs ({UAV_FULL}s), 130 s mission", 13, GREY, body=True)
    rx = MX + mw + 0.28; rw = W - MX - rx
    steps = [("Flood fill", "BFS cover"), ("Dijkstra", "cost + exposure"),
             ("Line follow", "5-sensor PD")]
    cg = 0.20; cw_ = (rw - 2 * cg) / 3; chh = 1.05
    for i, (name, sub) in enumerate(steps):
        x = rx + i * (cw_ + cg)
        s.rect(x, Y0, cw_, chh, PANEL); s.rect(x, Y0, cw_, 0.04, AMBER)
        s.text(x + 0.06, Y0 + 0.10, cw_ - 0.12, chh - 0.12, [[(f"{i + 1}  ", AMBER, True), (name, WHITE, True)], [(sub, GREY, False)]], 13, gap=0.20)
        if i < 2: s.arrow(x + cw_ + (cg - 0.26) / 2, Y0 + chh / 2 - 0.15)
    ty = Y0 + chh + 0.18
    rows = [["Algorithm", "In the code", "Time", "Space"],
            ["Flood fill", "BFS (breadth-first)", "O(N)", "O(N)"],
            ["Dijkstra", "binary heap (priority queue)", "O(N log N)", "O(N)"],
            ["Path", "steepest descent, L cells", "O(L)", "O(L)"],
            ["Line follower", "5 sensors × M line points", "O(M)", "O(M)"]]
    s.table(rx, ty, [1.35, rw - 1.35 - 1.15 - 0.75, 1.15, 0.75], rows, row_h=0.46, size=13, head_size=12, bold_cols=(0,), accent_cols=(2, 3))
    ny = ty + 0.46 * len(rows) + 0.06
    s.text(rx, ny, rw, 0.28, "N = 80 × 80 = 6,400 cells (0.5 m);  line follower: per frame", 12, GREY)
    s.rect(rx + 0.05, ny + 0.42, 0.10, 0.10, AMBER)
    s.text(rx + 0.22, ny + 0.30, rw - 0.22, 0.38, "Re-plans every 0.4 s hiding, 1.6 s infiltrating", 15, WHITE, body=True)
    s.rect(rx + 0.05, ny + 0.82, 0.10, 0.10, AMBER)
    s.text(rx + 0.22, ny + 0.70, rw - 0.22, 0.38, "PD steering: Kp 3.2, Kd 0.25", 15, WHITE, body=True)
    s.footer()

    # ---- 8 Computer vision
    # source: 320 x 240 heat image per UAV: src/ai/DroneController.h lines 15, 16; two UAVs: src/ai/Squad.h line 111;
    # threshold = max(floor 204, Otsu + 1), despeckle, two-pass union-find, moments: src/vision/CvPipeline.h lines 1 to 6, 19
    # and CvPipeline.cpp lines 9 to 50, 66 to 108, 113 to 160 (each stage is a pass over the pixels);
    # Kalman state (x, z, vx, vz), gate chi-square 9.21, Hungarian: src/vision/MultiTracker.h lines 2 to 4, 30, 46;
    # Hungarian O(n^3): src/vision/MultiTracker.cpp line 81; worker thread: src/vision/VisionWorker.h lines 1 to 8;
    # 2.9 ms: report/data/bench.json, "EPIC night 23:00 (game: waves, UAVs)", cpu_uav_vision_ms = 2.90974
    # (quoted in report/sections/gen_bench_text.tex); pictures: report/figures_lab/cv_pipeline_uav1.png, cv_tracks.png,
    # report/figures_final/final_flir.jpg
    s = d.new("Each camera renders a 320 by 240 heat image. An Otsu threshold with a fixed floor, a despeckle step and two-pass "
              "union-find labelling give the blobs; a Kalman filter per track with gated Hungarian assignment keeps the "
              "identities. The image stage is linear in the pixels, and both cameras together cost about 2.9 milliseconds a frame "
              "in the benchmark, on a worker thread in the game.")
    s.title("Computer vision that tracks in real time", "8  |  Thermal image to tracks")
    tw = 2.25; gap = (CW - 5 * tw) / 4; ih = 1.62; lh = 0.78
    cvp = fig("figures_lab", "cv_pipeline_uav1.png")
    flir = fig("figures_final", "final_flir.jpg")
    strip = [("Thermal image", "320 × 240 per camera", cvp, dict(crop=(233, 114, 497, 294))),
             ("Threshold", "Otsu, floor 204/255", cvp, dict(crop=(296, 553, 428, 643), nearest=True)),
             ("Blobs", "union-find, moments", cvp, dict(crop=(250, 915, 514, 1095))),
             ("Tracker", "Kalman + association", fig("figures_lab", "cv_tracks.png"), dict(crop=(590, 300, 1110, 655))),
             ("Track boxes", "identity & range", flir, dict(crop=(318, 214, 610, 413)))]
    for i, (name, sub, path, kw) in enumerate(strip):
        x = MX + i * (tw + gap)
        s.pic(path, x, Y0, tw, ih, **kw)
        s.chip(x, Y0, str(i + 1), 0.36)
        s.rect(x, Y0 + ih, tw, lh, PANEL); s.rect(x, Y0 + ih, tw, 0.04, AMBER)
        s.text(x + 0.08, Y0 + ih + 0.06, tw - 0.16, lh - 0.08, [[(name, WHITE, True)], [(sub, GREY, False)]], 14, gap=0.12)
        if i < 4: s.arrow(x + tw + (gap - 0.26) / 2, Y0 + ih / 2 - 0.15)
    by = Y0 + ih + lh + 0.18; bh = Y1 - by; bw = bh * 16 / 9
    s.pic(flir, MX, by, bw, bh)
    s.caption(MX, by, bw, bh, f"Through the UAV ({UAV_FULL}) camera")
    tx = MX + bw + 0.28; tw2 = W - MX - tx
    rows = [["Stage, every frame", "Work", "Cost"],
            ["Threshold, despeckle", "histogram, 3 × 3 neighbours", "O(P)"],
            ["Blobs", "two passes, union-find", "≈ O(P)"],
            ["Kalman filter", "state (x, z, vx, vz) per track", "O(T)"],
            ["Data association", "Hungarian, gate χ² = 9.21", "O(n³)"],
            ["Measured (benchmark)", "both cameras, sensor + vision", "2.9 ms"]]
    rh = 0.38
    s.table(tx, by, [2.10, tw2 - 2.10 - 0.95, 0.95], rows, row_h=rh, size=13, head_size=12, bold_cols=(0,), accent_cols=(2,))
    s.text(tx, by + rh * len(rows) + 0.06, tw2, 0.28, "P = 320 × 240 = 76,800 pixels;  T tracks;  n = tracks / blobs", 12, GREY)
    s.footer()

    # ---- 9 Defence and vehicles
    # source: two UAVs: src/ai/Squad.h line 111; single and combined strike, both rounds arrive together:
    # report/sections/algorithms.tex, algorithm alg:squad; boarding with E, gunner's sight, tank,
    # anti-aircraft gun, launcher: report/sections/B08_game.tex, first paragraph and subsection sec:board; pictures: frames of the final film
    s = d.new("The defence is autonomous: two UAVs strike from their thermal tracks, alone when two intruders are far apart, "
              "together when one intruder gets both rounds. The player can also walk a soldier up to the tank, the "
              "anti-aircraft gun or a launcher, press E to get in, and fire in first person.")
    s.title("Autonomous defence and controllable vehicles", "9  |  Strikes and vehicles")
    gx = 0.16; pw = (CW - 2 * gx) / 3; ph = pw * 9 / 16; hh = 0.34
    rowsp = [(f"Autonomous: two UAVs ({UAV_FULL}s) strike from thermal tracks",
              [("final_flir.jpg", "Thermal tracks"), ("final_single.jpg", "Single strike"), ("final_combined.jpg", "Combined strike: both rounds together")]),
             ("Playable: a soldier boards the tank, anti-aircraft gun or launcher (key E)",
              [("final_tank_walk.jpg", "Walk up, get in"), ("final_tank_sight.jpg", "Tank: gunner's sight"), ("final_aa_fire.jpg", "Anti-aircraft gun: first person")])]
    y = Y0 - 0.08
    for head, pics in rowsp:
        s.rect(MX, y + 0.11, 0.11, 0.11, AMBER)
        s.text(MX + 0.2, y, CW - 0.2, hh, head, 15, WHITE, True, anchor=MID, body=True)
        for i, (f, cap) in enumerate(pics):
            x = MX + i * (pw + gx)
            s.pic(fig("figures_final", f), x, y + hh, pw, ph)
            s.caption(x, y + hh, pw, ph, cap)
        y += hh + ph + 0.08
    s.footer()

    # ---- 10 Thank you
    s = d.new("Thank you. Questions are welcome.")
    s.s.shapes.add_picture(hero(fig("figures", "img", "scene", "sky_sunset.jpg"), top=0.35, bottom=0.9, start=0.45), 0, 0, Inches(W), Inches(H))
    s.rect((W - 1.4) / 2, 2.55, 1.4, 0.07, AMBER)
    s.text(0.5, 2.7, CW, 1.3, "Thank you", 72, WHITE, True, C, MID)
    s.text(0.5, 4.0, CW, 0.7, "Questions?", 32, AMBER, False, C, MID)
    s.text(0.5, 6.15, CW, 0.4, f"{AUTHOR}   |   Roll {ROLL}   |   Computer Graphics Laboratory, CSE 4102", 15, GREY, False, C, MID)
    s.footer()

    assert len(d.slides) == TOTAL, (len(d.slides), TOTAL)
    cp = d.prs.core_properties
    cp.author = AUTHOR; cp.last_modified_by = AUTHOR
    cp.title = "ThermalSense: a graphics pipeline, end to end, in a military outpost"
    cp.subject = "Computer Graphics Laboratory, CSE 4102"
    cp.keywords = "ThermalSense, OpenGL, C++17"; cp.comments = ""; cp.category = ""
    cp.created = cp.modified = datetime.datetime.now().replace(microsecond=0)
    for part in d.prs.part.package.iter_parts():          # file thumbnail: the title picture, not the blank template's
        if str(part.partname).endswith("thumbnail.jpeg"):
            buf = io.BytesIO()
            Image.open(title_picture).resize((256, 144), Image.LANCZOS).save(buf, "JPEG", quality=85)
            part._blob = buf.getvalue()
    d.prs.save(OUT)
    return d


def check(d):
    """Re-open the file: slide count, the movie part inside the package, body words per slide."""
    prs = Presentation(OUT)
    assert len(prs.slides) == TOTAL, len(prs.slides)
    with zipfile.ZipFile(OUT) as z:
        media = [(i.filename, i.file_size) for i in z.infolist() if i.filename.startswith("ppt/media/")]
        movies = [m for m in media if m[0].lower().endswith(".mp4")]
        rels = z.read("ppt/slides/_rels/slide2.xml.rels").decode()
    assert movies and movies[0][1] == os.path.getsize(VIDEO), movies
    assert "relationships/video" in rels and "relationships/media" in rels, "slide 2 has no movie relationship"
    print(f"slides: {len(prs.slides)} | movie part: {movies[0][0]} ({movies[0][1] / 1e6:.1f} MB) | pictures: {len(media) - len(movies)}")
    print("body words per slide:", ", ".join(f"{n}: {w}" for n, w in sorted(d.words.items())))
    print("size: %.1f MB | %s" % (os.path.getsize(OUT) / 1e6, OUT))


if __name__ == "__main__":
    check(build())
