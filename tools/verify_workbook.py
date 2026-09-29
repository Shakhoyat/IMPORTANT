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
from wb_common import OUT, S_BEHAV, S_COVER, S_HIER, S_MAT, S_TEST, S_TRACE, load_json  # noqa: E402
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
    comps = load_json("build/scene_export.json")["components"]
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
    col_err = values.get((mat, "D3"))
    print(f"  formula errors: {len(errs)}")
    print(f"  sheet 4: max |M_excel - M_code|   = {worst_code:.3e} (sheet cell D3 says {col_err})")
    print(f"  sheet 4: max |M_excel - M_python| = {worst_py:.3e}")
    print(f"  sheet 5: max |W_excel - W_code|   = {worst_world:.3e}")
    print(f"  non-numeric matrix cells: {missing}")
    kpis = [("cover: parts", S_COVER, "A11"), ("cover: meshes", S_COVER, "B11"), ("cover: triangles", S_COVER, "C11"),
            ("cover: tests", S_COVER, "D11"), ("cover: pass rate", S_COVER, "E11"), ("cover: best FPS", S_COVER, "F11"),
            ("cover: matrix error", S_COVER, "G11"), ("sheet 4 verdict", S_MAT, "F3"), ("sheet 5 verdict", S_HIER, "C3"),
            ("sheet 10 pass rate", S_TEST, "H3"), ("sheet 12 detection", S_BEHAV, "B8"),
            ("sheet 12 median error", S_BEHAV, "B10"), ("sheet 13 verified", S_TRACE, "D3")]
    for label, sheet, cell in kpis:
        print(f"  {label:24s} = {values.get((sheet.upper(), cell))}")
    ok = not errs and not flags and missing == 0 and max(worst_code, worst_world) <= TOLERANCE and worst_py <= 1e-9
    if not ok:
        print("  failed:", ", ".join(n for n, bad in (("formula errors", errs), ("flagged cells", flags),
                                                      ("non-numeric matrix cells", missing),
                                                      ("matrix tolerance", max(worst_code, worst_world) > TOLERANCE),
                                                      ("python cross-check", worst_py > 1e-9)) if bad))
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
