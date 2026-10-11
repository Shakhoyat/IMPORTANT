# ThermalSense

A graphics pipeline, end to end, in a military outpost. Computer Graphics Laboratory (CSE 4102), KUET.
Md. Shakhoyat Rahman Shujon, Roll 2107104.

A playable outpost simulation built entirely from procedural geometric primitives (16 procedural generators, 5,474 rendered parts, 62 modular designs), lit by
first-principles illumination formulations, with compute ray tracing, a dynamic solar day cycle, a physically-modeled thermal sensor, computer vision tracking, and an
autonomous multi-agent defense system. Developed in C++17 and OpenGL 3.3 Core (4.3 Compute for ray tracing), with zero external mathematical dependencies and
custom vector mathematics across all shaders.

## Start here

| To see | Open |
|---|---|
| The report (192 pages) | [`report/main.pdf`](report/main.pdf) |
| A guided tour of the code, line by line, in pipeline order | [`docs/CODE_TOUR.md`](docs/CODE_TOUR.md) |
| Every report section mapped to its code | [`docs/REPORT_CODE_MAP.md`](docs/REPORT_CODE_MAP.md) |
| The film (3 min 38 s, with stereo sound) | [`report/video/thermalsense_final_1080p_web.mp4`](report/video/thermalsense_final_1080p_web.mp4) (1080p Full HD, 93 MB with audio), [`thermalsense_final_720p.mp4`](report/video/thermalsense_final_720p.mp4) (47 MB), and the standalone soundtrack [`report/video/thermalsense_final_soundtrack.mp3`](report/video/thermalsense_final_soundtrack.mp3) (320 kbps). |
| The workbook (18 sheets, live formulas; the register of every number in the report) | [`report/workbooks/cg_workbook.xlsx`](report/workbooks/cg_workbook.xlsx) |
| The presentation (10 slides, film on slide 2) | [`report/ThermalSense_Presentation_10.pptx`](report/ThermalSense_Presentation_10.pptx) |
| The long deck (25 slides) | [`report/ThermalSense_Presentation.pptx`](report/ThermalSense_Presentation.pptx) |
| What changed in each round | [`report/CHANGES.md`](report/CHANGES.md) |

## Build and run

| Platform | Command |
|---|---|
| Windows | `run.bat` (starts `ThermalSense.exe`; builds it first if missing) |
| Linux | `./build.sh` then `./run.sh` |
| Linux without a display (EGL) | `make -j HEADLESS=1`, then `./ThermalSense --headless ...` |
| WSL2 window, no sudo | `tools/run_wsl.sh` |
| Windows exe from Linux | `tools/build_windows.sh` |
| Tests (396) | `make test` |

Useful options: `--quality low|high|ultra|epic`, `--fullscreen`, `--time 22:00`, `--seed N`,
`--record out.mp4 SECONDS`, `--capture DIR`, `--export-docs FILE`, `--help`.

## Controls (short)

| Key | Action |
|---|---|
| `W A S D`, mouse | move, look |
| `Tab`, click + `Enter` | choose a unit and take control of it |
| `E` | open a door; get into / out of a tank, gun, launcher or helicopter; take control |
| `C`, `V` | chase camera / gunner's sight; first / third person |
| `LMB`, `Enter` | fire |
| `F` | visible, primitive diagnostic, Z-buffer, thermal view |
| `O`, `B` | flat / Gouraud / Phong; Blinn-Phong / physically based |
| `;` | raster, hybrid, Whitted, path, light tracing |
| `` ` `` | Frame Explorer: one frame in 16 pipeline stages |
| `F2`, `,` `.` | clock on / off, scrub the time of day |
| `Esc` | release, menu |

The full table (more than 120 bindings) is in `src/scene/KeyMap.cpp` and in Section 5 of the report.

## Repository

| Path | Holds |
|---|---|
| `src/primitives` | the 16 mesh generators |
| `src/objects` | every object as a list of parts (size, rotation, position) |
| `src/math` | vectors, matrices, frustum, splines, tone curves, CPU rasteriser |
| `src/scene` | scene graph, soldiers, combat and particles, day cycle, doors, cameras |
| `src/rendering` | shaders (hand-written maths in `ShaderMath.cpp`), renderer, post-processing, Frame Explorer |
| `src/raytrace` | CPU ray tracer, intersections, bounding volume hierarchy |
| `src/vision` | thermal sensor model, image processing, multi-target tracker |
| `src/ai` | UAV squad, drone flight, intruder rovers (flood fill, Dijkstra, line follower) |
| `src/app` | application modes: play, capture, record, export, replay |
| `src/audio` | synthesised sound |
| `tests` | 396 automated tests |
| `tools/report`, `tools/v10` | generators of the report's tables, figures, checks, film and these documents |
| `report` | LaTeX sources, figures, data, PDF, slides |
| `docs` | code tour, code map, the version history (`docs/history`), course documents (`docs/reference`) |
| `assets` | public-domain (CC0) vegetation meshes and ground textures used at the two highest presets |

## Regenerating the report

```
./ThermalSense --export-docs build/routes_export.json && cp build/routes_export.json report/data/scene_export_v10.json
python3 tools/report/capture_shapes.py --app ./ThermalSense          # design pictures
python3 tools/v10/gen_numbers.py report/data/scene_export_v10.json    # numbers from the export
python3 tools/report/gen_designs.py                                    # Section 7 cards, chains, coordinate sheets
python3 tools/report/gen_flows.py && python3 tools/v10/gen_game.py
python3 tools/report/abbrev_pages.py                                   # short forms per page; builds the PDF (tectonic)
python3 tools/v10/build_cg_workbook.py --export report/data/scene_export_v10.json   # the workbook
python3 tools/report/numbers_from_workbook.py && python3 tools/report/check_workbook.py   # report numbers = workbook = code
python3 tools/report/check_counts.py && python3 tools/report/check_numbers.py
python3 tools/report/gen_code_map.py && python3 tools/report/gen_code_tour.py
```

The Python tools that draw plots need `numpy`, `matplotlib` and `pillow` (`uv run --with ...`).
