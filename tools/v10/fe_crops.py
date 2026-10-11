"""v10 M1: legible insets of the Frame Explorer captures.
Each capture is 1280x720 with fixed panels. Every inset is cut from the full-resolution lossless capture
(report/figures/raw/lab/<file>.png, not the JPEG the report shows) as a whole panel with an even margin, saved as
report/figures/img/fecrops/<macro>.png, and report/sections/gen_fecrops.tex gets one macro per inset:
  \\fe<Name><Panel>  ->  \\includegraphics[width=...mm]{figures/img/fecrops/fe<Name><Panel>.png}
Pixels are never resampled. Insets printed side by side (ROWS) get the same pixel height (the shorter one is padded
with its panel's own background colour) and one scale, so their printed heights are equal and the pair fits the line;
insets printed one above the other (STACKS) get the same printed width.
The scale keeps the HUD text >= 7 pt (an 11 px em at 0.2337 mm per pixel prints at 7.3 pt).
Run: ~/.local/bin/uv run -q --with pillow --with numpy python tools/v10/fe_crops.py"""
import os

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RAW = os.path.join(ROOT, "report", "figures", "raw", "lab")
IMG = os.path.join(ROOT, "report", "figures", "img", "fecrops")
OUT = os.path.join(ROOT, "report", "sections", "gen_fecrops.tex")
MM_PER_PX = 0.2337  # an 11 px em prints at 2.57 mm = 7.3 pt
LINE_MM = 174.0     # \linewidth of the report (A4, 18 mm margins)
GAP_MM = 3.0        # least gap between two insets on one line
MARGIN = 8          # pixels of the frame kept around a panel


def panel(x0, y0, x1, y1, pad_bottom=0):
    """A panel rectangle in pixels, inclusive, top-left origin: cut with MARGIN px around it. pad_bottom adds rows of
    the panel's background colour below (for a sub-panel that is cut where the next sub-panel starts)."""
    return {"kind": "panel", "rect": (x0, y0, x1, y1), "pad_bottom": pad_bottom}


def box(x0, y0, x1, y1, width_mm):
    """A fixed crop box (x1, y1 exclusive) printed at a fixed width."""
    return {"kind": "box", "box": (x0, y0, x1, y1), "width_mm": width_mm}


# the KEY FORMULA box (x 732..1255, y 602..699) lies in the bottom strip (y 592..709); the inset is the strip from just
# left of the box to just right of the formula text
FORMULA = {"kind": "formula", "strip": (726, 592, 1273, 709), "text": (740, 604, 1250, 697)}
WANT = {
    # ONE VERTEX table: the panel down to the row above the CAMERA IMAGE sub-panel (its top line is at y 322)
    "explorer_stage_02_vertex": {"Formula": FORMULA, "Info": panel(896, 94, 1267, 321, pad_bottom=9)},
    "explorer_stage_03_assembly": {"Formula": FORMULA, "Info": panel(896, 94, 1267, 247)},
    "explorer_stage_06_depth": {"Formula": FORMULA, "Info": panel(896, 94, 1267, 263)},
    "explorer_raster_scan": {"Formula": FORMULA, "Info": panel(868, 94, 1267, 494)},
    # THIS TRIANGLE sub-panel (y 222..383) / SPHERE sub-panel (y 396..545) of the interpolation panel
    "explorer_interp_affine_perspective": {"Formula": FORMULA, "Info": panel(896, 222, 1267, 383)},
    "explorer_interp_gouraud_phong": {"Formula": FORMULA, "Info": box(885, 392, 1279, 555, 92.1)},
    "explorer_stage_15_taa": {"Formula": FORMULA, "Info": panel(896, 94, 1267, 371)},
    "explorer_imagelab_gauss_gamma": {"Kernel": panel(721, 94, 1267, 293)},
    "explorer_imagelab_sobel": {"Kernel": panel(721, 94, 1267, 293)},
}
# insets the report prints on one line (\feA\hfill\feB): equal height, one scale, together <= LINE_MM - GAP_MM
ROWS = [
    (("explorer_stage_02_vertex", "Info"), ("explorer_stage_02_vertex", "Formula")),
    (("explorer_stage_03_assembly", "Info"), ("explorer_stage_03_assembly", "Formula")),
    (("explorer_stage_06_depth", "Info"), ("explorer_stage_06_depth", "Formula")),
    (("explorer_stage_15_taa", "Info"), ("explorer_stage_15_taa", "Formula")),
]
# insets the report prints one above the other: equal printed width; gap_mm of space is added above the lower one
# (the raster pair is one LaTeX line that is too wide and wraps, so the gap has to come from the macro)
STACKS = [
    ((("explorer_raster_scan", "Info"), ("explorer_interp_affine_perspective", "Info")), 2.0),
    ((("explorer_imagelab_gauss_gamma", "Kernel"), ("explorer_imagelab_sobel", "Kernel")), 0.0),
]


def median_colour(img, region):
    a = np.asarray(img.crop(region)).reshape(-1, 3)
    return tuple(int(v) for v in np.median(a, axis=0))


def text_right(img, region, thr=140):
    """x of the last bright (text) pixel column in region."""
    a = np.asarray(img.crop(region)).max(axis=2)
    xs = np.nonzero((a > thr).any(axis=0))[0]
    return region[0] + (int(xs.max()) if len(xs) else 0)


def pad_to(im, bg, top=0, bottom=0):
    if top == 0 and bottom == 0:
        return im
    out = Image.new("RGB", (im.width, im.height + top + bottom), bg)
    out.paste(im, (0, top))
    return out


def cut(img, spec, min_width=0):
    """-> (inset image, background colour to pad it with). min_width widens a formula inset (more of its box)."""
    if spec["kind"] == "box":
        return img.crop(spec["box"]), (0, 0, 0)
    if spec["kind"] == "panel":
        x0, y0, x1, y1 = spec["rect"]
        bg = median_colour(img, (x0, y0 + 2, x1 + 1, y1 + 1))
        m = MARGIN
        crop = (max(0, x0 - m), max(0, y0 - m), min(img.width, x1 + 1 + m), min(img.height, y1 + 1 + m))
        if spec["pad_bottom"]:  # cut at the last row of the sub-panel, continue it with its background colour
            crop = crop[:3] + (y1 + 1,)
        return pad_to(img.crop(crop), bg, bottom=spec["pad_bottom"]), bg
    sx0, sy0, sx1, sy1 = spec["strip"]
    right = max(text_right(img, spec["text"]) + 12, sx0 + min_width)
    bg = median_colour(img, (sx0, sy0 + 1, right, sy0 + 9))  # the strip above the formula box
    return img.crop((sx0, sy0, min(sx1, right), sy1 + 1)), bg


def macro_name(file, panel_name):
    base = "".join(w.title() for w in file.replace("explorer_", "").split("_") if not w.isdigit())
    return "fe" + "".join(ch for ch in base if ch.isalpha()) + panel_name


def main():
    os.makedirs(IMG, exist_ok=True)
    src = {file: Image.open(os.path.join(RAW, file + ".png")).convert("RGB") for file in WANT}
    insets, width_mm, raise_mm = {}, {}, {}
    for file, panels in WANT.items():
        for name, spec in panels.items():
            insets[(file, name)] = cut(src[file], spec)
            if spec["kind"] == "box":
                width_mm[(file, name)] = spec["width_mm"]
    budget = int((LINE_MM - GAP_MM) / MM_PER_PX)  # pixels of one line at the standard scale
    for a, b in ROWS:
        for k, other in ((a, b), (b, a)):  # a formula inset fills the rest of the line
            if WANT[k[0]][k[1]]["kind"] == "formula":
                insets[k] = cut(src[k[0]], WANT[k[0]][k[1]], min_width=budget - insets[other][0].width)
        h = max(insets[a][0].height, insets[b][0].height)
        for k in (a, b):
            im, bg = insets[k]
            extra = h - im.height
            insets[k] = (pad_to(im, bg, top=extra // 2, bottom=extra - extra // 2), bg)
        scale = min(MM_PER_PX, (LINE_MM - GAP_MM) / (insets[a][0].width + insets[b][0].width))
        for k in (a, b):
            width_mm[k] = int(insets[k][0].width * scale * 100) / 100.0
    for keys, gap in STACKS:
        w = max(insets[k][0].width for k in keys) * MM_PER_PX
        for i, k in enumerate(keys):
            width_mm[k] = round(min(LINE_MM, w), 1)
            if i and gap:
                raise_mm[k] = gap
    lines = ["% generated by tools/v10/fe_crops.py: insets of the Frame Explorer captures, cut from the lossless "
             "1280x720 captures (pixels not resampled)"]
    for file, panels in WANT.items():
        for name in panels:
            k = (file, name)
            im = insets[k][0]
            macro = macro_name(file, name)
            im.save(os.path.join(IMG, macro + ".png"), optimize=True)
            w = width_mm.get(k, round(min(LINE_MM, im.width * MM_PER_PX), 1))
            inc = f"\\includegraphics[width={w:g}mm]{{figures/img/fecrops/{macro}.png}}"
            if k in raise_mm:  # taller box, same picture: space above it when the line wraps
                inc = f"\\raisebox{{0pt}}[\\dimexpr\\height+{raise_mm[k]:g}mm\\relax][0pt]{{{inc}}}"
            lines.append(f"\\newcommand{{\\{macro}}}{{{inc}}}")
            pt = 11 * w / im.width / 25.4 * 72
            print(f"{macro:34s} {im.width:4d}x{im.height:<4d} -> {w:6.2f} x {w * im.height / im.width:5.1f} mm, 11 px em = {pt:.1f} pt")
    open(OUT, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
