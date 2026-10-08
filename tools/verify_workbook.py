"""Recalculates every formula of ThermalSense_Graphics_Documentation.xlsx and checks the result.

Engines: LibreOffice (soffice --headless) if installed, else Microsoft Excel via COM (pywin32) if installed,
else the pure-Python `formulas` engine (pip install formulas); force one with an argument. Checks:
  1. no cell evaluates to an Excel error (#VALUE!, #REF!, #NAME?, #DIV/0!, #N/A, #NUM!)
  2. sheet 4: every Excel-computed M element equals the code-exported matrix (max abs error <= tolerance)
  3. sheet 5: every world matrix rebuilt in Excel equals the exported world matrix
  4. the Excel M formulas also match an independent numpy recomputation from S / R / T
Exit code 0 = all checks passed.
"""
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wb_common import (S_CVN, S_DAY, S_SOLD, S_RT, S_FX, S_GAME, S_V7T, OUT, S_AUDIT, S_BEHAV, S_COMBAT, S_COVER, S_CTRL, S_HIER, S_MAT, S_TEST, S_TRACE, load_json,
                       matrix_components)  # noqa: E402
from wb_transforms import C_M, C_WERR, TOLERANCE  # noqa: E402

ERRORS = ("#VALUE!", "#REF!", "#NAME?", "#DIV/0!", "#N/A", "#NUM!", "#NULL!")


def _libreoffice():
    for exe in ("soffice", "libreoffice", r"C:\Program Files\LibreOffice\program\soffice.exe"):
        if shutil.which(exe) or os.path.exists(exe):
            return exe
    return None


def evaluate_libreoffice(exe):
    from openpyxl import load_workbook
    out_dir = tempfile.mkdtemp()
    subprocess.run([exe, "--headless", "--convert-to", "xlsx", "--outdir", out_dir, OUT], check=True,
                   capture_output=True, timeout=600)
    return _cached_values(load_workbook(os.path.join(out_dir, os.path.basename(OUT)), data_only=True))


def evaluate_excel(pdf=None):
    """Real Microsoft Excel through COM: full recalculation, then the cached values are read back."""
    import win32com.client
    from openpyxl import load_workbook
    out = os.path.join(tempfile.mkdtemp(), "recalculated.xlsx")
    xl = win32com.client.DispatchEx("Excel.Application")
    xl.Visible, xl.DisplayAlerts = False, False
    try:
        wb = xl.Workbooks.Open(OUT, ReadOnly=True)
        xl.CalculateFull()
        wb.SaveCopyAs(out)
        if pdf:
            wb.ExportAsFixedFormat(0, pdf)
        wb.Close(False)
    finally:
        xl.Quit()
    return _cached_values(load_workbook(out, data_only=True))


def _cached_values(wb):
    values = {}
    for ws in wb.worksheets:
        for row in ws.iter_rows():
            for c in row:
                if c.value is not None:
                    values[(ws.title.upper(), c.coordinate)] = c.value
    return values


def _has_excel():
    try:
        import win32com.client  # noqa: F401
        import winreg
        winreg.CloseKey(winreg.OpenKey(winreg.HKEY_CLASSES_ROOT, "Excel.Application"))
        return True
    except (ImportError, OSError):
        return False


def evaluate_formulas():
    import formulas
    model = formulas.ExcelModel().loads(OUT).finish()
    sol = model.calculate()
    values = {}
    pat = re.compile(r"^'?\[[^\]]+\](.+?)'?!([A-Z]+\d+)(?::([A-Z]+\d+))?$")
    for key, rng in sol.items():
        m = pat.match(str(key))
        if not m or m.group(3):
            continue
        v = rng.value[0][0] if hasattr(rng, "value") else rng
        values[(m.group(1).upper(), m.group(2))] = v
    return values


def _col(n):
    from openpyxl.utils import get_column_letter
    return get_column_letter(n)


def _rot(deg):
    x, y, z = (math.radians(a) for a in deg)
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    ry = [[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]
    rx = [[1, 0, 0], [0, cx, -sx], [0, sx, cx]]
    rz = [[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]
    mul = lambda a, b: [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]  # noqa
    return mul(mul(ry, rx), rz)


def main():
    """Usage: verify_workbook.py [auto|libreoffice|excel|formulas] [--pdf FILE]"""
    args = sys.argv[1:]
    pdf = args[args.index("--pdf") + 1] if "--pdf" in args else None
    engine = next((a for a in args if a in ("auto", "libreoffice", "excel", "formulas")), "auto")
    exe = _libreoffice()
    if engine == "auto":
        engine = "libreoffice" if exe else ("excel" if _has_excel() else "formulas")
    print("recalculating with", engine, "...")
    global OUT
    reduced = None
    if engine == "formulas":
        # The pure-Python engine cannot digest the 40 000 matrix formulas of the full workbook in reasonable time /
        # memory, so it recalculates a REDUCED workbook built by the same code (all sheets complete, but sheets 4 / 5
        # with only 8 group nodes + 2 leaves per root). The full workbook is still checked numerically below.
        reduced = os.path.join(os.path.dirname(OUT), "build", "wb_verify.xlsx")
        import wb_common
        wb_common.set_leaves(2, 8)
        subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "build_workbook.py"),
                        "--out", reduced, "--leaves", "2", "--groups", "8"], check=True, capture_output=True)
        OUT = reduced
        print("  reduced workbook:", reduced)
    if engine == "libreoffice":
        values = evaluate_libreoffice(exe)
    elif engine == "excel":
        values = evaluate_excel(pdf)
    else:
        values = evaluate_formulas()
    print(f"  {len(values)} evaluated cells")
    errs = [(k, v) for k, v in values.items() if str(v).strip().upper() in ERRORS]
    flags = [(k, v) for k, v in values.items() if str(v) in ("MISMATCH", "CHECK", "FAIL")]
    for k, v in flags[:20]:
        print("  FLAG", k, v)
    for k, v in errs[:20]:
        print("  ERROR", k, v)
    all_comps = load_json("build/scene_export.json")["components"]
    comps = matrix_components(all_comps)   # the rows that carry Excel formulas (sheets 4 + 5)
    mat, hier = S_MAT.upper(), S_HIER.upper()
    first = 6
    worst_code = worst_py = worst_world = 0.0
    missing = 0
    for i, c in enumerate(comps):
        r = first + i
        R = _rot(c["R_deg"])
        py = [R[a][b] * c["S"][b] if b < 3 else c["T"][a] for a in range(3) for b in range(4)]
        for k in range(12):
            xl = values.get((mat, f"{_col(C_M + k)}{r}"))
            if not isinstance(xl, (int, float)):
                missing += 1
                continue
            worst_code = max(worst_code, abs(xl - c["local_matrix"][k]))
            worst_py = max(worst_py, abs(xl - py[k]))
        e = values.get((hier, f"{_col(C_WERR)}{first - 1 + i}"))
        if isinstance(e, (int, float)):
            worst_world = max(worst_world, e)
        else:
            missing += 1
    # Full-scene check in Python (rows without Excel formulas too): M from S / R / T, then world = parent x M.
    worst_all_local = worst_all_world = 0.0
    wmap = {}
    for c in all_comps:
        R = _rot(c["R_deg"])
        m = [[R[a][b] * c["S"][b] if b < 3 else c["T"][a] for b in range(4)] + [] for a in range(3)]
        worst_all_local = max(worst_all_local, max(abs(m[a][b] - c["local_matrix"][a * 4 + b]) for a in range(3) for b in range(4)))
        par = wmap.get(c.get("parent_id"))
        if par is None:
            w = m
        else:
            w = [[sum(par[a][k] * m[k][b] for k in range(3)) + (par[a][3] if b == 3 else 0.0) for b in range(4)] for a in range(3)]
        wmap[c["id"]] = w
        worst_all_world = max(worst_all_world, max(abs(w[a][b] - c["world_matrix"][a * 4 + b]) for a in range(3) for b in range(4)))
    col_err = values.get((mat, "D3"))
    print(f"  formula errors: {len(errs)}")
    print(f"  sheet 4: max |M_excel - M_code|   = {worst_code:.3e} (sheet cell D3 says {col_err})")
    print(f"  sheet 4: max |M_excel - M_python| = {worst_py:.3e}")
    print(f"  sheet 5: max |W_excel - W_code|   = {worst_world:.3e}")
    print(f"  non-numeric matrix cells: {missing}")
    print(f"  all {len(all_comps)} components (python): max |M - M_code| = {worst_all_local:.3e}, "
          f"max |W - W_code| = {worst_all_world:.3e}   (Excel formulas cover {len(comps)})")
    kpis = [("cover: parts", S_COVER, "A11"), ("cover: meshes", S_COVER, "B11"), ("cover: triangles", S_COVER, "C11"),
            ("cover: tests", S_COVER, "D11"), ("cover: pass rate", S_COVER, "E11"), ("cover: best FPS", S_COVER, "F11"),
            ("cover: matrix error", S_COVER, "G11"), ("sheet 4 verdict", S_MAT, "F3"), ("sheet 5 verdict", S_HIER, "C3"),
            ("sheet 10 pass rate", S_TEST, "H3"), ("sheet 12 detection", S_BEHAV, "B8"),
            ("sheet 12 median error", S_BEHAV, "B10"), ("sheet 13 verified", S_TRACE, "D3"),
            ("cover: possessable", S_COVER, "A14"), ("cover: practical lights", S_COVER, "B14"),
            ("cover: walk floors/walls", S_COVER, "C14"), ("cover: particle types", S_COVER, "D14"),
            ("cover: audit verified", S_COVER, "E14"), ("cover: audit pending", S_COVER, "F14"),
            ("sheet 14 possessable", S_CTRL, "B3"), ("sheet 17 CHECK count", S_AUDIT, "J3"),
            ("sheet 16 cycle check", S_COMBAT, None)]
    for label, sheet, cell in kpis:
        if cell is None:
            continue
        print(f"  {label:24s} = {values.get((sheet.upper(), cell))}")
    # v6 sheets 21-23: every block is either data or an explicit "pending: ..." row (old export); when the data is
    # there the Excel sun-elevation recomputation must match the exported grid and no v6 test may fail.
    v6_ok = True
    day, sold, cvn = S_DAY.upper(), S_SOLD.upper(), S_CVN.upper()
    n_rows, n_ok, max_err = (values.get((day, c)) for c in ("B3", "F3", "D3"))
    if isinstance(n_rows, (int, float)) and n_rows > 0:
        good = n_ok == n_rows and isinstance(max_err, (int, float)) and max_err <= 0.05
        v6_ok &= good
        print(f"  sheet 21: {n_ok} / {n_rows} grid rows match the Excel sun elevation, max |diff| = {max_err} deg"
              f"  [{'ok' if good else 'FAIL'}]")
    else:
        print("  sheet 21: pending (no exported day-cycle grid yet)")
    n_sol = values.get((sold, "B3"))
    print("  sheet 22: soldiers =", n_sol if isinstance(n_sol, (int, float)) else "pending (no exported garrison yet)")
    t_all, t_pass, t_fail, t_pend = (values.get((cvn, c)) for c in ("B3", "D3", "F3", "H3"))
    if isinstance(t_fail, (int, float)):
        v6_ok &= t_fail == 0
        print(f"  sheet 23: v6 tests {t_pass} pass / {t_fail} fail / {t_pend} pending of {t_all}  "
              f"[{'ok' if t_fail == 0 else 'FAIL'}]")
    else:
        v6_ok = False
        print("  sheet 23: KPI cells missing  [FAIL]")
    pend = sum(1 for k, v in values.items() if k[0] in (day, sold, cvn) and str(v).startswith("pending"))
    print(f"  sheets 21-23: {pend} 'pending:' cells")
    # v7 sheets 24-27 (WP-W): every check cell evaluates to OK (a CHECK is already a flagged cell) and no v7 test fails.
    v7_ok = True
    rt, fx, gm, vt = S_RT.upper(), S_FX.upper(), S_GAME.upper(), S_V7T.upper()
    for name, sh in ((S_RT, rt), (S_FX, fx), (S_GAME, gm)):
        n_ok = sum(1 for k, v in values.items() if k[0] == sh and v == "OK")
        n_chk = sum(1 for k, v in values.items() if k[0] == sh and v == "CHECK")
        v7_ok &= n_ok > 0 and n_chk == 0
        print(f"  sheet {name}: {n_ok} OK checks, {n_chk} CHECK  [{'ok' if n_ok > 0 and n_chk == 0 else 'FAIL'}]")
    t_all, t_pass, t_fail, t_pend = (values.get((vt, c)) for c in ("B3", "D3", "F3", "H3"))
    if isinstance(t_fail, (int, float)):
        good = t_fail == 0 and t_pend == 0
        v7_ok &= good
        print(f"  sheet {S_V7T}: v7 tests {t_pass} pass / {t_fail} fail / {t_pend} pending of {t_all}  [{'ok' if good else 'FAIL'}]")
    else:
        v7_ok = False
        print(f"  sheet {S_V7T}: KPI cells missing  [FAIL]")
    pend7 = sum(1 for k, v in values.items() if k[0] in (rt, fx, gm, vt) and str(v).startswith("pending"))
    print(f"  sheets 24-27: {pend7} 'pending' cells")
    ok = (v6_ok and v7_ok and not errs and not flags and missing == 0 and max(worst_code, worst_world) <= TOLERANCE and worst_py <= 1e-9
          and max(worst_all_local, worst_all_world) <= TOLERANCE)
    if not ok:
        print("  failed:", ", ".join(n for n, bad in (("formula errors", errs), ("flagged cells", flags),
                                                      ("non-numeric matrix cells", missing),
                                                      ("matrix tolerance", max(worst_code, worst_world) > TOLERANCE),
                                                      ("python cross-check", worst_py > 1e-9),
                                                      ("full-scene python check", max(worst_all_local, worst_all_world) > TOLERANCE)) if bad))
    if not v6_ok:
        print("  failed: v6 sheets 21-23")
    if not v7_ok:
        print("  failed: v7 sheets 24-27")
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
