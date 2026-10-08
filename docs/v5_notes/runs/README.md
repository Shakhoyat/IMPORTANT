# v5 headless real-render runs (supervisor)

`trace_v5_low_90s.csv`: `ThermalSense --headless --quality low --trace <file> 90` on the Mesa llvmpipe software GPU,
real thermal render of both UAV cameras -> CV pipeline -> squad (build of 2026-10-05, after track merging and the
flicker-tolerant lock). The program printed at the end:

```
Game: wave 2, kills 3 (combined 2), rounds launched 21, breaches 1, integrity 90 %, score 400, fused track updates 689
      round blasts 21, hits 12, combined strikes 8, tracks confirmed 47
```

Earlier runs of the same day (for the record of the tuning):
| Build state | rounds | hits | kills | notes |
|---|---|---|---|---|
| before track merging | 5 | 5 | 0 | 48 confirmed tracks (blob flicker -> new ids reset the locks) |
| + track merging, target continuity | 6 | 5 | 0 | 31 tracks, locks still decayed too fast |
| + flicker-tolerant lock (0.4 dt bleed, 0.8 / 0.5 s thresholds) | 21 | 12 | 3 | the run above |

Re-run: `./ThermalSense --headless --quality low --trace build/trace.csv 90` (about 3-4 min on llvmpipe).
