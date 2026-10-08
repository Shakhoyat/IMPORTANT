# Report text reduction brief (supervisor, 2026-10-05)

User request: "the report contains old redundant scenes. remove them & I want you to reduce text contents as well".
Old scenes are already removed by the supervisor. Your job is the **text**.

## Target
Cut each file you own by about **40 % of its prose words**. The report must read as a lean textbook about the
**current (v5) program**: the graphics pipeline story, then vision, tracking, squad, game.

## Keep (never delete)
- Every equation (`$...$`, `\[...\]`, `equation`, `align`), with its one-line plain-English reading.
- Every listing macro (`\lstXxx`), figure (`\pic*`, `\sheetfig`, `\autofig`, `\picdia`, `\piclab`, `\picfive`,
  `\picfour`), table, `\label`, `\pipestrip{k}`, section/subsection headings (you may merge two tiny subsections).
- Numbers that are evidence (test ids like T105, measured values), but state each once.
- Generated inputs (`\input{sections/gen_*}`) and macros like `\nTests{}`.

## Cut
- Version history narration: "in v2 ...", "v3 introduced ...", "v4 adds ...", "the old surface bunker", engineer
  letters ("engineer D", "the HUD engineer", "as passed on to me"), process notes, plans already done.
  Describe the program as it is now, in present tense.
- Repetition: the same fact explained in two chapters -> keep it in the chapter that owns the concept, the other
  gets a short `Section~\ref{...}` pointer.
- Hedging, filler, long parenthetical asides, lists of things that are not in the program.
- Single-UAV wording that describes current behaviour: the game now has two UAVs in a squad (Section~\ref{sec:squad}).
  If a v4 single-UAV mechanism is superseded by the squad, cut it down to what still exists in the code.

## Rules
- Do not invent facts. If unsure whether something is still true, check the source in `src/` or cut the claim.
- Do not touch files you do not own, `main.tex`, any `gen_*.tex`, or anything outside `report/sections/`.
- Check your files compile: `cd report && tectonic -X compile main.tex --outdir <your scratch dir>`
  (never write main.pdf in report/; other agents compile at the same time). Fix any error in your files.
- When done, report: words before/after per file (`wc -w`), and anything you were unsure about.
