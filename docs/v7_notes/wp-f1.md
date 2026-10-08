# WP-F1 notes: report v6 sections (s18-s22) + edits s01/s07/s11/s13/sA/sB

Status: started 2026-10-08 (power-outage log; newest at the bottom).

## Progress
- Read PLAN_V6 (WP-F, working rules), PLAN_V7, v6_notes/*, v7_notes/wp-cap.md, main.tex, s14/s17, gen scripts.
- s18 daycycle written + compiled; s19 soldiers written
- Infra: report/gen_v6.py (macros + tables in sections/gen_v6.tex, run summaries cached in report/data/v6_runs.json,
  night regression MAE/PSNR with --regress), gen_tables.py calls it + v6/v7 test areas + gen_tree_soldier;
  listings.py +15 listings; tools/make_figures_v6.py -> report/diagrams/H1..H7; prepare_images.py + figures_v6/v7;
  main.tex \picsix + \input gen_v6. Build: tectonic -X compile main.tex (~25 s once the bundle is cached).
- s18 daycycle, s19 soldiers, s20 randompaths, s21 realcv, s22 v6results written; each compiled clean (0 errors,
  only forward refs until s22). PDF 22.2 MB, 110 pages after s22.
- Next: edits s01/s07/s11/s13/sA/sB + title/abstract.
- Edits done: s01 (intro sentence, checklist rows, how-to-read), s07 (controllables count + soldier / SKY rows),
  s11 (v6 verification text), s13 (v6 critique table tab:critsix), sA (section I, F7 / G1 updated), sB (v6 key table
  from v6.keys, K director row, possessable count), main.tex title / subtitle / date / abstract -> v6.
- Final build (2026-10-08 16:2x): 113 pages, 23.2 MB (22.2 MiB), 0 errors, 0 undefined refs, 0 pending boxes,
  43 overfull hbox warnings (none > 5 pt in the new/edited sections), 54 'h -> ht' float notes.
- Open: s12 Limitations still says every capture was made on llvmpipe (not in my edit list); soldier explode list
  (Scene.cpp); v6 video; FPS; PDF headroom to 25 MB is ~2 MB for v7 (consider JPEG width 1400 / q80 for v7 shots).
