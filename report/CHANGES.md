# Report changes (v10 follow-up: M1 legibility, M2 consistency, M3 adoptions)

Figure and table numbers refer to the report before this round unless marked "new".

## M1: illegible / overlapping figures

### Workbook snapshots -> typeset blocks + appendix
New tool `tools/v10/sheet_to_tex.py` typesets the key blocks of each sheet from the LibreOffice-recalculated copy
(`report/workbooks/cg_workbook_values.xlsx`, written by `build_cg_workbook.py`); input cells keep the yellow shade.
New appendix A "Workbook sheets" (`tools/v10/gen_wb_appendix.py`): every page of LibreOffice's own PDF of the
workbook (vector) on a landscape page; the script measures the smallest printed text from the PDF (>= 7.0 pt;
Decomposition now prints at 75 %, Illumination at 78 % instead of fit-to-page, which had shrunk them to 6.1-6.3 pt).

| Figure | Before | After (caption wording kept, plus "(full sheet: Appendix A.n)") |
|---|---|---|
| 6.2 Primitives | full-sheet PNG | blocks: counts, parametric equations |
| 7.2 Transform_Trace | full-sheet PNG | blocks: inputs; S, Rx, Ry, Rz; T, M; W_parent, W (matrices); the vertex through the pipeline; checks |
| 9.2 View_Projection | full-sheet PNG | blocks: inputs + u, v, n basis; V and P matrices; depth table |
| 12.2 Clipping | full-sheet PNG | blocks: Cohen-Sutherland, Liang-Barsky, Sutherland-Hodgman stages 1 and 2 |
| 15.2 | plot + sheet side by side | specular plot alone (full width); new Fig. "Workbook sheet Illumination" with the geometry and term blocks. Caption: "Left:" / "Right:" removed, wording otherwise kept |
| 18.2 | ray tree + sheet side by side | ray tree alone (full width); new Fig. "Workbook sheet Ray_Tracing" (ray-sphere, ray-plane, Snell, TIR, Whitted). Caption: "Left:" / "Right:" removed |
| 19.11 Decomposition | full-sheet PNG | block: first 26 UAV parts with T/R/S and parent; caption adds "The body shows the first 26 UAV parts with their transforms." |
| 20.1 Color_Interp | full-sheet PNG | blocks A-F (triangle, barycentric, Gouraud vs Phong, texture u, sRGB vs linear, sky over the day) |
| 25.3 Thermal_Kernels | full-sheet PNG | Gaussian raw / normalised, patch + Sobel kernels, outputs (matrices) |
| 26.3 Duty_Cycle | full-sheet PNG | roster block |
| 29.2 Performance | full-sheet PNG | derived columns (1000/avg, fps check, triangles per draw) |

### Other figures
- 19.10: six tables at 1/3 width (scaled down) -> two per row at natural size (8 pt).
- 21.1 (`plots.py` p_day_cycle): titles shortened to "(a) sun elevation", "(b) key light per channel" (details are in the
  caption); legends moved below the axes; constrained layout for every plot.
- 11.1 (p_depth_precision): titles "(a) window depth z_w", "(b) smallest depth step"; y label "depth step (m)"; legend of
  (b) moved off the "1 cm" mark.
- 16.2-left (p_gouraud_vs_phong): legend moved below the axes (it sat on the curves); title kept.
- 25.2 (p_thermal_histogram): legends placed clear of the CDF curves; histogram now full width (it printed at 4.3 pt
  at 0.55 width); p_separable_cost title "Cost of a k x k blur".
- All half-width plots now placed at >= 0.5 text width (were 0.42: 6.7 pt tick labels); annotation / legend text 8 pt.
- 26.1 state diagrams: redrawn as three sub-diagrams (UAV, intruder, soldier) with explicit coordinates; return edges
  routed with corners in their own lanes; labels on white boxes beside the lines. Panels named (a), (b), (c).
- 4.1 architecture: redrawn as layers; no edge crosses a node.
- 4.2 site plan: the "+x (east)" / "+z (north)" arrows sit on a white box with dark text.
- Pipeline banner (every Part A section): text 7-7.5 pt (was 6 pt), inactive stages darker (text black 80 %).
- Frame Explorer captures (11.2b, 12.1b, 13.2, 14.1a/b, 14.2, 16.2-right, 25.2a/b): zoomed insets of the panel the
  caption refers to (clipped viewports of the same JPEG, pixels unchanged; `tools/v10/fe_crops.py` sizes each inset so
  the HUD text prints at >= 7.3 pt).
- 16.1: the two composite strips with burnt-in labels replaced by the six individual lab tiles with LaTeX subcaptions.
- 25.2 split in two figures (it ran into the page footer): Frame Explorer frames + kernel insets; histogram + cost.
- Equations 7.1 and 21.1 split over two lines (they ran into the margin).
