"""Before / after chart of the measured fixes (Section 29 and the presentation): grass triangles, 7x7 blur cost,
indexed vs non-indexed frame time. Run: ~/.local/bin/uv run -q --with matplotlib python tools/report/gen_fix_chart.py"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
plt.rcParams.update({"font.family": "serif", "font.serif": ["Times New Roman", "Nimbus Roman", "DejaVu Serif"], "mathtext.fontset": "stix",
                     "font.size": 15, "axes.spines.top": False, "axes.spines.right": False})
# numbers from the report: Section 29 (hard problems) and the benchmark (report/data/bench.csv: EPIC day / non-indexed)
panels = [("Grass: triangles (millions)", "one cube\nper blade", 7.7, "instanced\nblades + LOD", 1.82, "M"),
          ("7x7 blur: multiply-adds / pixel", "2D kernel", 49, "separable", 14, ""),
          ("Frame time, EPIC day (ms)", "non-indexed", 52.97, "indexed", 11.90, " ms"),
          ("Reflections: off-screen objects", "screen-space", 0, "hybrid RT", 1, "")]
fig, axs = plt.subplots(1, 3, figsize=(15, 4.2))
for ax, (title, a, va, b, vb, unit) in zip(axs, panels[:3]):
    ax.set_facecolor("white")
    bars = ax.bar([0, 1], [va, vb], color=["#B9B3A6", "#E0A03A"], width=0.62)
    ax.set_xticks([0, 1]); ax.set_xticklabels([a, b])
    ax.set_title(title, fontsize=16, pad=12)
    ax.set_yticks([])
    ax.spines["left"].set_visible(False)
    for x, v in zip([0, 1], [va, vb]):
        ax.text(x, v * 1.02, ("%g" % v) + unit, ha="center", va="bottom", fontsize=17, fontweight="bold")
    ax.set_ylim(0, max(va, vb) * 1.22)
    ax.text(0.5, -0.36, "%.1f$\\times$ less" % (va / vb), transform=ax.transAxes, ha="center", fontsize=16, color="#9A5B00")
fig.tight_layout()
import os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "report", "figures", "plots")
fig.savefig(os.path.join(OUT, "p_fixes.pdf"), facecolor="white")
fig.savefig(os.path.join(OUT, "p_fixes.png"), dpi=150, facecolor="white")
