#!/usr/bin/env python3
"""Makes compressed copies of every figure the report can reference into report/img/ (same relative path, .jpg).
Photographic renders -> JPEG quality 85, at most 1600 px wide. Diagrams (report/diagrams) stay PNG when under 300 KB, else JPEG.
main.tex's \\pic macro looks in img/ first and falls back to the original PNG. Run before tectonic:  uv run --with pillow python report/prepare_images.py
"""
import os, sys
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
DIRS = ["figures", "figures_v3", "figures_v4", "figures_v5", "figures_v6", "figures_v7", "figures_lab", "diagrams", "sheets"]
total_in = total_out = 0
for d in DIRS:
    src = os.path.join(HERE, d)
    if not os.path.isdir(src): continue
    for f in sorted(os.listdir(src)):
        if not f.lower().endswith(".png"): continue
        p = os.path.join(src, f)
        out_dir = os.path.join(HERE, "img", d); os.makedirs(out_dir, exist_ok=True)
        base = os.path.splitext(f)[0]
        dst_jpg = os.path.join(out_dir, base + ".jpg"); dst_png = os.path.join(out_dir, base + ".png")
        if os.path.exists(dst_jpg) and os.path.getmtime(dst_jpg) >= os.path.getmtime(p): 
            total_out += os.path.getsize(dst_jpg); total_in += os.path.getsize(p); continue
        if os.path.exists(dst_png) and os.path.getmtime(dst_png) >= os.path.getmtime(p):
            total_out += os.path.getsize(dst_png); total_in += os.path.getsize(p); continue
        total_in += os.path.getsize(p)
        if d in ("diagrams", "sheets"):
            # Flat-colour plots and spreadsheet crops: a 256-colour palette PNG keeps text sharp at ~1/3 the size.
            im = Image.open(p).convert("RGB")
            if im.width > 2400:
                im = im.resize((2400, round(im.height * 2400 / im.width)), Image.LANCZOS)
            im.quantize(256, method=Image.Quantize.MEDIANCUT).save(dst_png, optimize=True)
            total_out += os.path.getsize(dst_png); continue
        im = Image.open(p)
        if im.mode in ("RGBA", "LA", "P"):
            bg = Image.new("RGB", im.size, "white")
            im = im.convert("RGBA"); bg.paste(im, mask=im.split()[3]); im = bg
        else: im = im.convert("RGB")
        if im.width > 1600:
            im = im.resize((1600, round(im.height * 1600 / im.width)), Image.LANCZOS)
        im.save(dst_jpg, "JPEG", quality=85, optimize=True)
        total_out += os.path.getsize(dst_jpg)
print("images: %.1f MB -> %.1f MB" % (total_in / 1e6, total_out / 1e6))
