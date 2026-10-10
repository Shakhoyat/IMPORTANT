"""v10: the report's figures as compact JPEGs (Overleaf / git friendly). Finds every \\raw{group/name} used in
report/sections/*.tex and converts report/figures/raw/group/name.png -> report/figures/img/group/name.jpg
(max 1920 px wide, ffmpeg q 4; FORCE=1 re-encodes all). The full-size PNGs stay in figures/raw (untracked; tools/v10/capture_all.sh)."""
import glob
import os
import re
import subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FFMPEG = os.path.expanduser("~/.local/bin/ffmpeg")
names = set()
for f in glob.glob(os.path.join(ROOT, "report", "sections", "*.tex")):
    names |= set(re.findall(r"\\raw\{([^}]+)\}", open(f).read()))
missing = []
for n in sorted(names):
    src = os.path.join(ROOT, "report", "figures", "raw", n + ".png")
    dst = os.path.join(ROOT, "report", "figures", "img", n + ".jpg")
    if not os.path.exists(src):
        missing.append(n)
        continue
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if not os.environ.get("FORCE") and os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src):
        continue
    subprocess.run([FFMPEG, "-loglevel", "error", "-y", "-i", src, "-vf", "scale='min(1920,iw)':-2", "-q:v", "4", dst], check=True)
print(len(names), "figures;", "missing:", missing)
