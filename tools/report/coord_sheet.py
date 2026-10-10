"""Section 7 coordinate sheets: a spreadsheet-style table of every component of a design after its transformation chain,
in the design's own frame (used by gen_designs.py).

For a component with world matrix W_part under a design root with world matrix W_root, M = W_root^-1 W_part maps the
unit primitive into the design frame. The centre is p = M (0,0,0,1); the extent is the box of the unit primitive
([-1/2, 1/2]^3) under M, extent_i = sum_j |M_ij|, so the corners run from p - extent/2 to p + extent/2.
The centre is checked against the product of the local matrices from the root down to the part."""
import sys

import numpy as np

SHEET_ROWS = 18   # rows per column of a sheet block (two columns side by side); short blocks let pages break between them

HEAD = (r"\rowcolor{sheethead} & \textbf{A} & \textbf{B} & \textbf{C} & \textbf{D} & \textbf{E} & \textbf{F} & \textbf{G}\\\hline" "\n"
        r"\cellcolor{sheethead}\textbf{1} & \textbf{Component} & $x$ & $y$ & $z$ & $\Delta x$ & $\Delta y$ & $\Delta z$\\\hline")
COLS = r"\begin{tabular}[t]{|>{\columncolor{sheethead}}r|>{\raggedright\arraybackslash}p{2.55cm}|r|r|r|r|r|r|}\hline"


def cnum(v):
    """A coordinate in cm: one decimal below 10 m, none above."""
    v = 0.0 if abs(v) < 0.05 else v
    return ("%.0f" % v if abs(v) >= 1000 else "%.1f" % v).replace("-", "$-$")


def sheet_rows(design, own, root_id, by_id, mat):
    """[(part kind, centre (cm), extent (cm))] for every component of the first instance."""
    inv = np.linalg.inv(mat(by_id[root_id]["world_matrix"]))
    rows = []
    for c in own:
        M = inv @ mat(c["world_matrix"])
        L = np.eye(4)
        node = c
        while node and node["id"] != root_id:
            L = mat(node["local_matrix"]) @ L
            node = by_id.get(node["parent_id"])
        if node is None or np.abs(L[:3, 3] - M[:3, 3]).max() > 1e-3 * max(1.0, np.abs(M[:3, 3]).max()):
            sys.exit("%s / %s: centre from the chain differs from the exported world matrix" % (design["name"], c["name"]))
        rows.append((c["part_kind"], M[:3, 3] * 100.0, np.abs(M[:3, :3]).sum(axis=1) * 100.0))
    return rows


def column(chunk, first, tex):
    o = [COLS, HEAD]
    for k, (kind, p, e) in enumerate(chunk):
        nm = tex(kind if len(kind) <= 24 else kind[:23] + ".")
        o.append(r"%d & %s & %s & %s & %s & %s & %s & %s\\\hline"
                 % (first + k, nm, cnum(p[0]), cnum(p[1]), cnum(p[2]), cnum(e[0]), cnum(e[1]), cnum(e[2])))
    o.append(r"\end{tabular}")
    return "\n".join(o)


def coord_sheet(design, own, root_id, by_id, mat, tex, shown_name):
    rows = sheet_rows(design, own, root_id, by_id, mat)
    o = [r"\par\smallskip\noindent{\small\textbf{Coordinate sheet} (cm, design frame): centre B--D, extent E--G; %d components.}"
         r"\par\nopagebreak\vspace{0.6mm}" % len(rows)]
    for b in range(0, len(rows), 2 * SHEET_ROWS):
        left = rows[b:b + SHEET_ROWS]
        right = rows[b + SHEET_ROWS:b + 2 * SHEET_ROWS]
        if len(rows) - b <= 2 * SHEET_ROWS:   # last block: balance the two columns
            rest = rows[b:]
            h = (len(rest) + 1) // 2 if len(rest) > 8 else len(rest)
            left, right = rest[:h], rest[h:]
        blk = (r"\noindent{\fontsize{6.6}{7.6}\selectfont\setlength{\tabcolsep}{2.2pt}\renewcommand{\arraystretch}{0.98}"
               + column(left, b + 2, tex))
        if right:
            blk += r"\hfill " + column(right, b + 2 + len(left), tex)
        blk += r"}\par\vspace{1.5mm}"
        if b == 0:   # the title is set in one box with the first (short) block, so it cannot be left behind on a page
            blk = r"\noindent\begin{minipage}{\linewidth}" + o.pop() + "\n" + blk + r"\end{minipage}\par\vspace{1.5mm}"
        o.append(blk)
    return "\n".join(o), len(rows)
