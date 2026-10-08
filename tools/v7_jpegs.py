#!/usr/bin/env python3
"""v7 report images: report/figures_v7/**.png -> report/img/figures_v7/**.jpg (<= 1400 px wide, JPEG quality 80: the report
has ~2 MB of headroom under 25 MB). Newer than the PNGs, so report/prepare_images.py keeps them (it also skips subfolders).
  uv run --with pillow python tools/v7_jpegs.py
"""
import os, sys
from PIL import Image
root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = os.path.join(root, "report", "figures_v7"); dst = os.path.join(root, "report", "img", "figures_v7")
tot = 0; n = 0
for dp, _, files in os.walk(src):
    for f in sorted(files):
        if not f.lower().endswith(".png"): continue
        rel = os.path.relpath(os.path.join(dp, f), src)
        out = os.path.join(dst, os.path.splitext(rel)[0] + ".jpg")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        im = Image.open(os.path.join(dp, f)).convert("RGB")
        if im.width > 1400: im = im.resize((1400, round(im.height * 1400 / im.width)), Image.LANCZOS)
        im.save(out, "JPEG", quality=80, optimize=True)
        tot += os.path.getsize(out); n += 1
print(n, "jpgs", round(tot / 1e6, 1), "MB")
