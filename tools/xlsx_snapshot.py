#!/usr/bin/env python3
"""Excel-window snapshot renderer for the ThermalSense documentation workbook.

Neither Excel nor LibreOffice is needed: the workbook is read with openpyxl (twice: cached values and formulas) and a
crop of one sheet is drawn with PIL as it would look in an Excel window (grey column / row headers, gridlines, fills,
fonts, borders, merged cells, alignment, number formats, column widths, row heights, conditional formats, optional
formula bar, simple bar / scatter charts).

  uv run --with openpyxl --with pillow python tools/xlsx_snapshot.py            # regenerate report/sheets/*.png
  uv run --with openpyxl --with pillow python tools/xlsx_snapshot.py --only s04  # names starting with s04

API:  Snapshot(xlsx).render(sheet, "A1:L20", out_png, formula_cell="H6", rows=[...], hide_cols=[...], title=None)
"""
import copy
import json
import math
import re
import sys
from pathlib import Path

import openpyxl
from openpyxl.utils import get_column_letter, column_index_from_string, range_boundaries
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
XLSX = ROOT / "ThermalSense_Graphics_Documentation.xlsx"
OUT = ROOT / "report" / "sheets"

SCALE = 2.0                 # 96 dpi * 2 = 192 dpi-equivalent
HDR_BG = (230, 230, 230)
HDR_FG = (70, 70, 70)
GRID = (208, 208, 208)
HDR_LINE = (171, 171, 171)
ROWHDR_W = 40               # px at 96 dpi
COLHDR_H = 20
FBAR_H = 26

_FD = "/usr/share/fonts/truetype/dejavu/"
_FONT_FILES = {
    (False, False): ["Carlito-Regular.ttf", "DejaVuSans.ttf"],
    (True, False): ["Carlito-Bold.ttf", "DejaVuSans-Bold.ttf"],
    (False, True): ["Carlito-Italic.ttf", "DejaVuSans-Oblique.ttf"],
    (True, True): ["Carlito-BoldItalic.ttf", "DejaVuSans-BoldOblique.ttf"],
}
_font_cache = {}


def _find_font(names):
    import glob
    for n in names:
        if n.startswith("DejaVu"):
            p = _FD + n
            if Path(p).exists():
                return p
        else:
            hits = glob.glob("/usr/share/fonts/**/" + n, recursive=True)
            if hits:
                return hits[0]
    return None


def get_font(size_px, bold=False, italic=False, mono=False):
    key = (round(size_px * 4), bold, italic, mono)
    if key not in _font_cache:
        if mono:
            p = _FD + ("DejaVuSansMono-Bold.ttf" if bold else "DejaVuSansMono.ttf")
        else:
            p = _find_font(_FONT_FILES[(bold, italic)])
        try:
            f = ImageFont.truetype(p, max(6, int(round(size_px)))) if p else ImageFont.load_default()
        except Exception:
            f = ImageFont.load_default()
        _font_cache[key] = f
    return _font_cache[key]


# ---------------------------------------------------------------- number formats
def _fmt_general(v, width_chars=11):
    if isinstance(v, bool):
        return "TRUE" if v else "FALSE"
    if isinstance(v, int):
        return str(v)
    if v == int(v) and abs(v) < 1e11:
        return str(int(v))
    a = abs(v)
    if a >= 1e11 or a < 1e-4:
        s = "%.5E" % v
        m, e = s.split("E")
        m = m.rstrip("0").rstrip(".")
        return "%sE%s%02d" % (m, e[0], abs(int(e)))
    s = ("%." + str(max(0, 10 - len(str(int(a))) if a >= 1 else 9)) + "f") % v
    s = s.rstrip("0").rstrip(".")
    if width_chars < len(s):          # Excel shrinks decimals to the column width
        dec = max(0, width_chars - len(str(int(a))) - 2)
        s = ("%." + str(dec) + "f") % v
        s = s.rstrip("0").rstrip(".") if "." in s else s
    return s


def format_value(v, nf, width_chars=11):
    if v is None:
        return ""
    if isinstance(v, str):
        return v
    if isinstance(v, bool):
        return "TRUE" if v else "FALSE"
    if not isinstance(v, (int, float)):
        return str(v)
    if nf in ("General", "@", None):
        return _fmt_general(v, width_chars)
    if nf.startswith("0.") and "E" in nf:
        dec = nf.split("E")[0].count("0") - 1
        s = ("%." + str(dec) + "E") % v
        m, e = s.split("E")
        return "%sE%s%02d" % (m, e[0], abs(int(e)))
    if nf.endswith("%"):
        dec = nf.count("0") - 1 if "." in nf else 0
        return ("%." + str(dec) + "f%%") % (v * 100)
    if nf == "#,##0":
        return "{:,.0f}".format(v)
    if re.fullmatch(r"0(\.0+)?", nf):
        dec = len(nf.split(".")[1]) if "." in nf else 0
        out = ("%." + str(dec) + "f") % v
        return out.lstrip("-") if v == 0 else out
    m = re.fullmatch(r"0\.(#+)", nf)
    if m:
        s = ("%." + str(len(m.group(1))) + "f") % v
        return s.rstrip("0").rstrip(".") if "." in s else s
    return _fmt_general(v, width_chars)


# ---------------------------------------------------------------- colours
def _rgb(color, default=None):
    if color is None:
        return default
    if color.type == "rgb" and isinstance(color.rgb, str) and len(color.rgb) in (6, 8):
        h = color.rgb[-6:]
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    if color.type == "indexed":
        return {8: (0, 0, 0), 9: (255, 255, 255), 64: default}.get(color.indexed, default)
    return default


def _col_px(ws, idx):
    cd = ws.column_dimensions.get(get_column_letter(idx))
    if cd is not None and cd.hidden:
        return 0
    # openpyxl groups columns (min..max) under the first letter
    for key, d in ws.column_dimensions.items():
        if d.min and d.max and d.min <= idx <= d.max and d.width:
            return 0 if d.hidden else int(d.width * 7 + 5)
    return 64


def _row_px(ws, r):
    rd = ws.row_dimensions.get(r)
    if rd is not None:
        if rd.hidden:
            return 0
        if rd.height:
            return int(round(rd.height * 96 / 72))
    return 20


def _wrap(draw, text, font, maxw):
    lines = []
    for para in str(text).split("\n"):
        words = para.split(" ")
        cur = ""
        for w in words:
            t = (cur + " " + w) if cur else w
            if draw.textlength(t, font=font) <= maxw or not cur:
                cur = t
                # break an over-long single word
                while draw.textlength(cur, font=font) > maxw and len(cur) > 1:
                    k = len(cur)
                    while k > 1 and draw.textlength(cur[:k], font=font) > maxw:
                        k -= 1
                    lines.append(cur[:k])
                    cur = cur[k:]
            else:
                lines.append(cur)
                cur = w
        lines.append(cur)
    return lines


# ---------------------------------------------------------------- mini Excel formula evaluator
# The workbook is written by openpyxl and carries NO cached values, so the values Excel would show are computed here
# (lazy + memoised; only the cells that a snapshot needs are evaluated). Functions: see FUNCS.
class XlErr(Exception):
    pass


class Arr:
    def __init__(self, rows):
        self.rows = rows

    def flat(self):
        return [v for r in self.rows for v in r]

    def map(self, fn):
        return Arr([[fn(v) for v in r] for r in self.rows])


_TOK = re.compile(r"""\s*(?:(?P<num>\d+\.?\d*(?:[eE][+-]?\d+)?|\.\d+)|(?P<str>"(?:[^"]|"")*")|
    (?P<ref>(?:(?:'(?:[^']|'')+'|[A-Za-z_][\w.]*)!)?\$?[A-Z]{1,3}\$?\d+(?::\$?[A-Z]{1,3}\$?\d+)?)(?![\w(])|
    (?P<fn>[A-Za-z_][\w.]*)\(|(?P<bool>TRUE|FALSE)|(?P<op><>|<=|>=|[-+*/^&=<>%(),]))""", re.X)


def _tokenize(s):
    pos, out = 0, []
    while pos < len(s):
        m = _TOK.match(s, pos)
        if not m or m.end() == pos:
            raise ValueError("cannot tokenize %r at %d" % (s, pos))
        pos = m.end()
        k = m.lastgroup
        out.append((k, m.group(k)))
    return out


class XlEval:
    def __init__(self, wb):
        self.wb = wb
        self.memo = {}
        self.ast = {}
        sys.setrecursionlimit(20000)

    # -- values ----------------------------------------------------------
    def cell(self, sh, r, c):
        key = (sh, r, c)
        if key in self.memo:
            return self.memo[key]
        v = self.wb[sh].cell(r, c).value
        if isinstance(v, str) and v.startswith("="):
            self.memo[key] = None          # cycle guard
            try:
                v = self.run(sh, v)
                if isinstance(v, Arr):
                    v = v.rows[0][0]
            except XlErr as e:
                v = "#ERR"
            except ZeroDivisionError:
                v = "#DIV/0!"
        self.memo[key] = v
        return v

    def rng(self, sh, a):
        a = a.replace("$", "")
        if ":" not in a:
            a = a + ":" + a
        x0, y0, x1, y1 = range_boundaries(a)
        return Arr([[self.cell(sh, r, c) for c in range(x0, x1 + 1)] for r in range(y0, y1 + 1)])

    # -- parser (AST = nested tuples) -------------------------------------
    def run(self, sh, f):
        key = (sh, f)
        if key not in self.ast:
            self.toks = _tokenize(f[1:])
            self.i = 0
            node = self.p_cmp()
            self.ast[key] = node
        return self.ev(sh, self.ast[key])

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else (None, None)

    def take(self):
        t = self.toks[self.i]
        self.i += 1
        return t

    def p_cmp(self):
        n = self.p_cat()
        while self.peek()[0] == "op" and self.peek()[1] in ("=", "<>", "<", ">", "<=", ">="):
            o = self.take()[1]
            n = ("bin", o, n, self.p_cat())
        return n

    def p_cat(self):
        n = self.p_add()
        while self.peek() == ("op", "&"):
            self.take()
            n = ("bin", "&", n, self.p_add())
        return n

    def p_add(self):
        n = self.p_mul()
        while self.peek()[0] == "op" and self.peek()[1] in "+-" and self.peek()[1] != "":
            o = self.take()[1]
            n = ("bin", o, n, self.p_mul())
        return n

    def p_mul(self):
        n = self.p_pow()
        while self.peek()[0] == "op" and self.peek()[1] in ("*", "/"):
            o = self.take()[1]
            n = ("bin", o, n, self.p_pow())
        return n

    def p_pow(self):
        n = self.p_un()
        while self.peek() == ("op", "^"):
            self.take()
            n = ("bin", "^", n, self.p_un())
        return n

    def p_un(self):
        if self.peek() == ("op", "-"):
            self.take()
            return ("neg", self.p_un())
        if self.peek() == ("op", "+"):
            self.take()
            return self.p_un()
        n = self.p_atom()
        while self.peek() == ("op", "%"):
            self.take()
            n = ("bin", "/", n, ("num", 100.0))
        return n

    def p_atom(self):
        k, v = self.take()
        if k == "num":
            return ("num", float(v))
        if k == "str":
            return ("str", v[1:-1].replace('""', '"'))
        if k == "bool":
            return ("num", v == "TRUE")
        if k == "ref":
            if "!" in v:
                sh, a = v.rsplit("!", 1)
                sh = sh[1:-1].replace("''", "'") if sh.startswith("'") else sh
            else:
                sh, a = None, v
            return ("ref", sh, a)
        if k == "fn":
            args = []
            if self.peek() == ("op", ")"):
                self.take()
            else:
                while True:
                    args.append(self.p_cmp())
                    t = self.take()
                    if t == ("op", ")"):
                        break
            return ("fn", v.upper(), args)
        if (k, v) == ("op", "("):
            n = self.p_cmp()
            self.take()
            return n
        raise ValueError("bad token %r" % ((k, v),))

    # -- evaluation ---------------------------------------------------------
    def ev(self, sh, n):
        t = n[0]
        if t == "num":
            return n[1]
        if t == "str":
            return n[1]
        if t == "ref":
            s = n[1] or sh
            a = n[2].replace("$", "")
            if ":" in a:
                return self.rng(s, a)
            c = openpyxl.utils.cell.coordinate_from_string(a)
            return self.cell(s, c[1], column_index_from_string(c[0]))
        if t == "neg":
            return self.elem(lambda a, b: -num(a), self.ev(sh, n[1]), 0)
        if t == "bin":
            a, b = self.ev(sh, n[2]), self.ev(sh, n[3])
            return self.elem(BINOPS[n[1]], a, b)
        if t == "fn":
            return self.call(sh, n[1], n[2])
        raise ValueError(t)

    @staticmethod
    def elem(fn, a, b):
        if isinstance(a, Arr) or isinstance(b, Arr):
            ra = a.rows if isinstance(a, Arr) else None
            rb = b.rows if isinstance(b, Arr) else None
            h = len(ra) if ra else len(rb)
            w = len(ra[0]) if ra else len(rb[0])
            g = lambda x, rr, i, j: (rr[i][j] if len(rr) > 1 or len(rr[0]) > 1 else rr[0][0]) if rr else x
            return Arr([[fn(g(a, ra, i, j), g(b, rb, i, j)) for j in range(w)] for i in range(h)])
        return fn(a, b)

    def call(self, sh, name, args):
        ev = lambda a: self.ev(sh, a)
        if name == "IF":
            c = ev(args[0])
            if isinstance(c, Arr):
                c = c.rows[0][0]
            if truthy(c):
                return ev(args[1])
            return ev(args[2]) if len(args) > 2 else False
        if name == "ISNUMBER":
            try:
                v = ev(args[0])
            except (XlErr, ZeroDivisionError):
                return False
            return self.elem(lambda a, b: isinstance(a, (int, float)) and not isinstance(a, bool), v, 0)
        if name == "IFERROR":
            try:
                v = ev(args[0])
                return v
            except (XlErr, ZeroDivisionError):
                return ev(args[1])
        vals = [ev(a) for a in args]
        fn = FUNCS.get(name)
        if fn is None:
            raise XlErr("unsupported function " + name)
        return fn(*vals)


def num(v):
    if v is None or v == "":
        return 0.0
    if isinstance(v, bool):
        return float(v)
    if isinstance(v, (int, float)):
        return v
    try:
        return float(v)
    except Exception:
        raise XlErr("#VALUE!")


def truthy(v):
    return bool(v) if not isinstance(v, str) else v.upper() == "TRUE"


def _txt(v):
    if v is None:
        return ""
    if isinstance(v, bool):
        return "TRUE" if v else "FALSE"
    if isinstance(v, float):
        if v == int(v) and abs(v) < 1e15:
            return str(int(v))
        return ("%.15g" % v)
    return str(v)


def _cmp(op):
    def f(a, b):
        if a is None and isinstance(b, str):
            a = ""
        if b is None and isinstance(a, str):
            b = ""
        if a is None:
            a = 0
        if b is None:
            b = 0
        if isinstance(a, str) != isinstance(b, str):
            ka, kb = (1 if isinstance(a, str) else 0), (1 if isinstance(b, str) else 0)
            return {"=": False, "<>": True, "<": ka < kb, ">": ka > kb, "<=": ka <= kb, ">=": ka >= kb}[op]
        if isinstance(a, str):
            a, b = a.lower(), b.lower()
        return {"=": a == b, "<>": a != b, "<": a < b, ">": a > b, "<=": a <= b, ">=": a >= b}[op]
    return f


BINOPS = {"+": lambda a, b: num(a) + num(b), "-": lambda a, b: num(a) - num(b), "*": lambda a, b: num(a) * num(b),
          "/": lambda a, b: num(a) / num(b), "^": lambda a, b: num(a) ** num(b), "&": lambda a, b: _txt(a) + _txt(b)}
for _o in ("=", "<>", "<", ">", "<=", ">="):
    BINOPS[_o] = _cmp(_o)


def _flat(*args):
    out = []
    for a in args:
        out += a.flat() if isinstance(a, Arr) else [a]
    return out


def _nums(*args):
    out = []
    for a in args:
        if isinstance(a, Arr):
            out += [v for v in a.flat() if isinstance(v, (int, float)) and not isinstance(v, bool)]
        elif a is not None:
            out.append(num(a))
    return out


def _crit(c):
    if isinstance(c, (int, float)) and not isinstance(c, bool):
        return lambda v: isinstance(v, (int, float)) and v == c
    c = _txt(c)
    m = re.match(r"(<>|<=|>=|=|<|>)(.*)", c)
    op, rhs = (m.group(1), m.group(2)) if m else ("=", c)
    try:
        x = float(rhs)
        isn = True
    except ValueError:
        isn = False
    if isn:
        def f(v):
            if not isinstance(v, (int, float)) or isinstance(v, bool):
                return op == "<>"
            return _cmp(op)(v, x)
        return f
    pat = re.compile("^" + re.escape(rhs.lower()).replace(r"\*", ".*").replace(r"\?", ".") + "$")

    def g(v):
        s = "" if v is None else _txt(v).lower()
        hit = bool(pat.match(s))
        return (not hit) if op == "<>" else hit
    return g


def _ifs(pairs):
    """pairs = [range, criteria, ...] -> list of row indices (flat) that satisfy all"""
    n = len(pairs[0].flat())
    ok = [True] * n
    for i in range(0, len(pairs), 2):
        fl = pairs[i].flat()
        f = _crit(pairs[i + 1])
        ok = [o and f(v) for o, v in zip(ok, fl)]
    return ok


def f_countif(r, c):
    if isinstance(c, Arr):
        return c.map(lambda x: f_countif(r, x))
    f = _crit(c)
    return float(sum(1 for v in r.flat() if f(v)))


def f_sumifs(s, *p):
    ok = _ifs(list(p))
    return float(sum(v for o, v in zip(ok, s.flat()) if o and isinstance(v, (int, float))))


def f_averageifs(s, *p):
    ok = _ifs(list(p))
    xs = [v for o, v in zip(ok, s.flat()) if o and isinstance(v, (int, float))]
    return sum(xs) / len(xs)


def f_sumproduct(*arrs):
    fl = [a.flat() if isinstance(a, Arr) else [a] for a in arrs]
    return float(sum(math.prod(num(v) for v in t) for t in zip(*fl)))


def f_match(x, r, t=1.0):
    fl = r.flat()
    if t == 0:
        f = _crit(x) if isinstance(x, str) else (lambda v: v == x)
        if isinstance(x, str) and re.match(r"(<>|<=|>=|=|<|>)", x):
            f = lambda v: isinstance(v, str) and v.lower() == x.lower()
        for i, v in enumerate(fl):
            if f(v):
                return float(i + 1)
        raise XlErr("#N/A")
    best = None
    for i, v in enumerate(fl):
        if isinstance(v, (int, float)) and v <= x:
            best = i + 1
    if best is None:
        raise XlErr("#N/A")
    return float(best)


def f_index(r, i, j=None):
    rows = r.rows
    i = int(num(i))
    if j is None:
        if len(rows) == 1:
            return rows[0][i - 1]
        return rows[i - 1][0]
    return rows[i - 1][int(num(j)) - 1]


def _quant(xs, q):
    xs = sorted(xs)
    k = (len(xs) - 1) * q
    lo = int(math.floor(k))
    hi = min(lo + 1, len(xs) - 1)
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def _roundup(x, d):
    x, d = num(x), int(num(d))
    m = 10 ** d
    return math.copysign(math.ceil(abs(x) * m - 1e-12) / m, x)


def _round(x, d):
    x, d = num(x), int(num(d))
    m = 10 ** d
    return math.copysign(math.floor(abs(x) * m + 0.5) / m, x)


FUNCS = {
    "SUM": lambda *a: float(sum(_nums(*a))), "MAX": lambda *a: max(_nums(*a) or [0]), "MIN": lambda *a: min(_nums(*a) or [0]),
    "AVERAGE": lambda *a: sum(_nums(*a)) / len(_nums(*a)), "MEDIAN": lambda *a: _quant(_nums(*a), 0.5),
    "COUNT": lambda *a: float(len(_nums(*a))), "COUNTA": lambda *a: float(sum(1 for v in _flat(*a) if v not in (None, ""))),
    "ABS": lambda x: abs(num(x)), "SQRT": lambda x: math.sqrt(num(x)), "COS": lambda x: math.cos(num(x)),
    "SIN": lambda x: math.sin(num(x)), "TAN": lambda x: math.tan(num(x)), "ATAN": lambda x: math.atan(num(x)),
    "RADIANS": lambda x: math.radians(num(x)), "DEGREES": lambda x: math.degrees(num(x)),
    "ASIN": lambda x: math.asin(max(-1.0, min(1.0, num(x)))), "ACOS": lambda x: math.acos(max(-1.0, min(1.0, num(x)))),
    "ATAN2": lambda x, y: math.atan2(num(y), num(x)), "PI": lambda: math.pi,   # sheet 21 sun elevation (Excel ATAN2(x, y))
    "AND": lambda *a: all(truthy(v) for v in _flat(*a)), "OR": lambda *a: any(truthy(v) for v in _flat(*a)),
    "NOT": lambda a: not truthy(a), "LEFT": lambda s, n=1: _txt(s)[:int(num(n))], "RIGHT": lambda s, n=1: _txt(s)[-int(num(n)):],
    "COUNTIF": f_countif, "COUNTIFS": lambda *p: float(sum(_ifs(list(p)))), "SUMIFS": f_sumifs, "AVERAGEIFS": f_averageifs,
    "SUMPRODUCT": f_sumproduct, "MATCH": f_match, "INDEX": f_index, "COLUMNS": lambda r: float(len(r.rows[0])), "ROWS": lambda r: float(len(r.rows)),
    "QUARTILE": lambda r, q: _quant(_nums(r), num(q) / 4), "PERCENTILE": lambda r, q: _quant(_nums(r), num(q)),
    "ROUNDUP": _roundup, "ROUND": _round,
    # v7 sheets 24-26 (spring blend, Halton jitter, a-trous kernel, tone curve)
    "EXP": lambda x: math.exp(num(x)), "LN": lambda x: math.log(num(x)),
    "LOG": lambda x, b=10: math.log(num(x)) / math.log(num(b)), "MOD": lambda a, b: num(a) - num(b) * math.floor(num(a) / num(b)),
    "INT": lambda x: float(math.floor(num(x))), "FACT": lambda x: float(math.factorial(int(num(x)))),
}


class Snapshot:
    def __init__(self, path=XLSX):
        # the workbook has no cached values (written by openpyxl): wbv is the formula workbook + the evaluator below
        self.wbf = openpyxl.load_workbook(path, data_only=False)
        self.wbv = self.wbf
        self.xe = XlEval(self.wbf)

    def val(self, sheet, r, c):
        return self.xe.cell(sheet, r, c)

    def _v(self, sheet, coord):
        c = openpyxl.utils.cell.coordinate_from_string(coord)
        return self.val(sheet, c[1], column_index_from_string(c[0]))

    # -- conditional formats ------------------------------------------------
    def _cf_fill(self, wsv, coord, value):
        ws = wsv
        for rng, rules in ws.conditional_formatting._cf_rules.items():
            c = openpyxl.utils.cell.coordinate_from_string(coord)
            ci = column_index_from_string(c[0])
            x0, y0, x1, y1 = range_boundaries(str(rng.sqref).split()[0])
            if not (x0 <= ci <= x1 and y0 <= c[1] <= y1):
                continue
            for r in rules:
                fill = r.dxf.fill.bgColor.rgb if r.dxf and r.dxf.fill else None
                if not fill:
                    continue
                ok = False
                f = r.formula[0] if r.formula else ""
                try:
                    if r.type == "cellIs":
                        if f.startswith('"'):
                            ref = f.strip('"')
                        else:
                            ref = f.replace("$", "")
                            if "!" in ref:
                                sh, cc = ref.split("!")
                                ref = self._v(sh.strip("'"), cc)
                            else:
                                ref = self._v(ws.title, ref) if not re.fullmatch(r"-?[\d.]+", ref) else float(ref)
                        op = r.operator
                        if value is None or (isinstance(ref, (int, float)) != isinstance(value, (int, float))):
                            ok = False
                        else:
                            ok = {"equal": value == ref, "lessThan": value < ref, "lessThanOrEqual": value <= ref,
                                  "greaterThan": value > ref}.get(op, False)
                    elif r.type == "expression":
                        s = str(value) if value is not None else ""
                        m = re.fullmatch(r'LEFT\(\w+,(\d+)\)="(.*)"', f)
                        m2 = re.fullmatch(r'EXACT\(RIGHT\(\w+,(\d+)\),"(.*)"\)', f)
                        m3 = re.fullmatch(r'\w+="(.*)"', f)
                        if m:
                            ok = s[:int(m.group(1))] == m.group(2)
                        elif m2:
                            ok = s[-int(m2.group(1)):] == m2.group(2)
                        elif m3:
                            ok = s == m3.group(1)
                except Exception:
                    ok = False
                if ok:
                    h = fill[-6:]
                    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
        return None

    # -- main ---------------------------------------------------------------
    def render(self, sheet, rng, out, formula_cell=None, rows=None, hide_cols=(), title=None,
               max_width_px=None, charts=True, hide_rows=(), cols=None, col_px=None, images=True, row_cap=None, row_px=None, nowrap=()):
        wsv, wsf = self.wbv[sheet], self.wbf[sheet]
        x0, y0, x1, y1 = range_boundaries(rng)
        if rows is None:
            rows = [r for r in range(y0, y1 + 1) if r not in set(hide_rows)]
        cols = list(cols) if cols else [c for c in range(x0, x1 + 1) if c not in set(hide_cols)]
        S = SCALE
        colw = {c: _col_px(wsv, c) for c in cols}
        colw.update({c: w for c, w in (col_px or {}).items() if c in colw})
        rowh = {r: _row_px(wsv, r) for r in rows}
        # rows without an explicit height are auto-fitted by Excel when a cell wraps: reproduce that
        _md = ImageDraw.Draw(Image.new("RGB", (4, 4)))
        _mc = {(m.min_row, m.min_col) for m in wsv.merged_cells.ranges}
        for r in rows:
            rd = wsv.row_dimensions.get(r)
            if rd is not None and rd.height:
                continue
            need = 0
            for c in cols:
                if (r, c) in _mc:
                    continue
                cell = wsv.cell(r, c)
                if not cell.alignment.wrap_text or c in nowrap:
                    continue
                v = self.val(sheet, r, c)
                if v in (None, "") or not colw[c]:
                    continue
                fnt = get_font(((cell.font.sz or 11) * 96 / 72) * 0.85, bool(cell.font.b), bool(cell.font.i))
                txt = format_value(v, cell.number_format, int(colw[c] / 7))
                n = len(_wrap(_md, txt, fnt, (colw[c] - 6) * 1.0))
                need = max(need, n * (cell.font.sz or 11) * 96 / 72 * 1.25 + 4)
            if need > rowh[r]:
                rowh[r] = int(need)
        rowh.update({r: h for r, h in (row_px or {}).items() if r in rowh})
        if row_cap:
            rowh = {r: min(h, row_cap) for r, h in rowh.items()}
        # merged map restricted to the visible set
        merged_at, merged_skip = {}, set()
        for mr in wsv.merged_cells.ranges:
            a, b, c2, d = mr.min_col, mr.min_row, mr.max_col, mr.max_row
            if a in colw and b in rowh:
                merged_at[(b, a)] = mr
                for rr in range(b, d + 1):
                    for cc in range(a, c2 + 1):
                        if (rr, cc) != (b, a):
                            merged_skip.add((rr, cc))
        gw = sum(colw.values())
        gh = sum(rowh.values())
        fbar = FBAR_H if formula_cell else 0
        W = int((ROWHDR_W + gw) * S)
        H = int((fbar + COLHDR_H + gh) * S)
        img = Image.new("RGB", (W, H), (255, 255, 255))
        d = ImageDraw.Draw(img)
        # x / y positions
        xs, x = {}, ROWHDR_W
        for c in cols:
            xs[c] = x
            x += colw[c]
        ys, y = {}, fbar + COLHDR_H
        for r in rows:
            ys[r] = y
            y += rowh[r]

        def X(v): return int(round(v * S))

        # headers
        hf = get_font(11 * S * 96 / 96 * 0.95, mono=False)
        d.rectangle([0, X(fbar), W, X(fbar + COLHDR_H)], fill=HDR_BG)
        d.rectangle([0, X(fbar), X(ROWHDR_W), H], fill=HDR_BG)
        for c in cols:
            if not colw[c]:
                continue
            x_a, x_b = X(xs[c]), X(xs[c] + colw[c])
            d.line([x_b, X(fbar), x_b, X(fbar + COLHDR_H)], fill=HDR_LINE, width=1)
            t = get_column_letter(c)
            tw = d.textlength(t, font=hf)
            d.text(((x_a + x_b - tw) / 2, X(fbar + 3)), t, font=hf, fill=HDR_FG)
        for r in rows:
            if not rowh[r]:
                continue
            y_a, y_b = X(ys[r]), X(ys[r] + rowh[r])
            d.line([0, y_b, X(ROWHDR_W), y_b], fill=HDR_LINE, width=1)
            t = str(r)
            tw = d.textlength(t, font=hf)
            d.text(((X(ROWHDR_W) - tw) / 2, (y_a + y_b) / 2 - hf.size * 0.6), t, font=hf, fill=HDR_FG)
        d.line([X(ROWHDR_W), X(fbar), X(ROWHDR_W), H], fill=HDR_LINE, width=1)
        d.line([0, X(fbar + COLHDR_H), W, X(fbar + COLHDR_H)], fill=HDR_LINE, width=1)

        # pass 1: fills + gridlines
        def span(r, c):
            mr = merged_at.get((r, c))
            if not mr:
                return xs[c], ys[r], xs[c] + colw[c], ys[r] + rowh[r]
            cs = [cc for cc in range(mr.min_col, mr.max_col + 1) if cc in xs]
            rs = [rr for rr in range(mr.min_row, mr.max_row + 1) if rr in ys]
            return (xs[cs[0]], ys[rs[0]], xs[cs[-1]] + colw[cs[-1]], ys[rs[-1]] + rowh[rs[-1]]) if cs and rs else None

        gl = [None] * 0
        for r in rows:
            for c in cols:
                if (r, c) in merged_skip or not colw[c] or not rowh[r]:
                    continue
                sp = span(r, c)
                if sp is None:
                    continue
                cell = wsv.cell(r, c)
                fill = None
                if cell.fill and cell.fill.fill_type == "solid":
                    fill = _rgb(cell.fill.fgColor)
                cv = self.val(sheet, r, c)
                cfv = self._cf_fill(wsv, cell.coordinate, cv)
                if cfv:
                    fill = cfv
                a, b, c2, d2 = [X(v) for v in sp]
                d.rectangle([a, b, c2, d2], fill=fill if fill else (255, 255, 255), outline=GRID, width=1)

        # pass 2: text
        for r in rows:
            for ci, c in enumerate(cols):
                if (r, c) in merged_skip or not colw[c] or not rowh[r]:
                    continue
                sp = span(r, c)
                if sp is None:
                    continue
                cell = wsv.cell(r, c)
                v = self.val(sheet, r, c)
                if v is None or v == "":
                    continue
                f = cell.font
                size = (f.sz or 11) * 96 / 72 * S * 0.85
                font = get_font(size, bool(f.b), bool(f.i))
                fg = _rgb(f.color, (0, 0, 0)) or (0, 0, 0)
                fx0, fy0, fx1, fy1 = sp
                widthch = max(4, int((fx1 - fx0 - 5) / 7))
                txt = format_value(v, cell.number_format, widthch)
                al = cell.alignment
                if c in nowrap and al.wrap_text:
                    al = copy.copy(al)
                    al.wrap_text = False
                h = al.horizontal
                if h in (None, "general"):
                    h = "right" if isinstance(v, (int, float)) and not isinstance(v, bool) else "left"
                pad = 3 * S
                a, b, c2, d2 = [X(q) for q in (fx0, fy0, fx1, fy1)]
                maxw = c2 - a - 2 * pad
                if al.wrap_text:
                    lines = _wrap(d, txt, font, maxw)
                    fs0 = font.size
                    while len(lines) * font.size * 1.22 > (d2 - b) - 2 * S and font.size > fs0 * 0.72:
                        font = get_font(font.size - 1, bool(f.b), bool(f.i))
                        lines = _wrap(d, txt, font, maxw)
                else:
                    lines = [txt]
                if not al.wrap_text and font.size * 1.22 > (d2 - b) - 2 * S:      # one line taller than its row: fit
                    font = get_font(max(7, ((d2 - b) - 2 * S) / 1.25), bool(f.b), bool(f.i))
                lh = font.size * 1.22
                total = lh * len(lines)
                va = al.vertical or "bottom"
                if va == "center":
                    ty = (b + d2) / 2 - total / 2
                elif va == "top":
                    ty = b + 2 * S
                else:
                    ty = d2 - total - 2 * S
                if ty < b:
                    ty = b + 1
                # overflow of left-aligned unwrapped text into empty cells to the right
                limit_x = c2
                if not al.wrap_text and h == "left" and not isinstance(v, (int, float)):
                    k = ci + 1
                    cur_r = r
                    while k < len(cols):
                        cc = cols[k]
                        if self.val(sheet, cur_r, cc) not in (None, "") or (cur_r, cc) in merged_skip:
                            break
                        limit_x = X(xs[cc] + colw[cc])
                        k += 1
                clipw = limit_x - a - 2 * pad
                for li, ln in enumerate(lines):
                    ly = ty + li * lh
                    if ly + lh < b or ly > d2:
                        continue
                    tw = d.textlength(ln, font=font)
                    if al.wrap_text:
                        if h == "center":
                            lx = a + (c2 - a - tw) / 2
                        elif h == "right":
                            lx = c2 - pad - tw
                        else:
                            lx = a + pad
                    elif h == "center":
                        lx = a + (c2 - a - tw) / 2
                    elif h == "right":
                        lx = c2 - pad - tw
                    else:
                        lx = a + pad
                    if (not al.wrap_text) and tw > clipw and isinstance(v, (int, float)):
                        # shrink-to-fit (Excel would show ####; the snapshot keeps the digits legible)
                        fs = font.size
                        while tw > clipw and fs > size * 0.6:
                            fs -= 1
                            font = get_font(fs, bool(f.b), bool(f.i))
                            tw = d.textlength(ln, font=font)
                        lx = c2 - pad - tw
                    # clip: draw into a crop and paste
                    box = (a + 1, max(b + 1, int(ly)), limit_x, min(d2, int(ly + lh + 2)))
                    if lx < a + 1:
                        lx = a + 1
                    cb = (a + 1, b + 1, max(a + 2, limit_x), d2)
                    reg = img.crop(cb)
                    ImageDraw.Draw(reg).text((lx - cb[0], ly - cb[1]), ln, font=font, fill=fg)
                    img.paste(reg, cb[:2])
        # clip overdraw: repaint cells to the right of a clipped text is skipped (text clipped by next column below)
        # pass 3: borders (drawn above text)
        for r in rows:
            for c in cols:
                if (r, c) in merged_skip or not colw[c] or not rowh[r]:
                    continue
                sp = span(r, c)
                if sp is None:
                    continue
                bd = wsv.cell(r, c).border
                a, b, c2, d2 = [X(q) for q in sp]
                for side, seg in (("left", (a, b, a, d2)), ("right", (c2, b, c2, d2)),
                                  ("top", (a, b, c2, b)), ("bottom", (a, d2, c2, d2))):
                    s = getattr(bd, side)
                    if s is not None and s.style:
                        col = _rgb(s.color, (60, 60, 60)) or (60, 60, 60)
                        wd = {"thin": 1, "hair": 1, "medium": 3, "thick": 4, "double": 3}.get(s.style, 1)
                        d.line(seg, fill=col, width=max(1, int(wd * S / 2)))

        if images:
            self._draw_images(img, wsf, xs, ys, X)
        # charts (simple)
        if charts:
            self._draw_charts(d, wsv, wsf, xs, ys, colw, rowh, rows, cols, X, img)

        # formula bar
        if formula_cell:
            fv = wsf[formula_cell].value
            d.rectangle([0, 0, W, X(fbar)], fill=(248, 248, 248))
            d.line([0, X(fbar), W, X(fbar)], fill=HDR_LINE, width=1)
            nb = get_font(11 * 96 / 72 * S * 0.8, bold=False)
            d.rectangle([X(4), X(3), X(64), X(fbar - 3)], fill=(255, 255, 255), outline=HDR_LINE)
            tw = d.textlength(formula_cell, font=nb)
            d.text((X(34) - tw / 2, X(5.5)), formula_cell, font=nb, fill=(0, 0, 0))
            d.text((X(74), X(4)), "fx", font=get_font(13 * S, italic=True, bold=True), fill=(80, 80, 80))
            ftxt = str(fv)
            mf = get_font(11 * S * 0.95, mono=True)
            maxw = W - X(104)
            while d.textlength(ftxt, font=mf) > maxw and len(ftxt) > 12:
                ftxt = ftxt[:-2]
            if ftxt != str(fv):
                ftxt = ftxt[:-1] + "..."
            d.text((X(100), X(5)), ftxt, font=mf, fill=(0, 0, 0))
            # selected-cell outline
            cc = openpyxl.utils.cell.coordinate_from_string(formula_cell)
            ci, ri = column_index_from_string(cc[0]), cc[1]
            if ci in xs and ri in ys:
                sp = span(ri, ci)
                d.rectangle([X(sp[0]), X(sp[1]), X(sp[2]), X(sp[3])], outline=(33, 115, 70), width=max(2, int(S * 1.5)))
        d.rectangle([0, 0, W - 1, H - 1], outline=(150, 150, 150), width=1)
        img.save(out, dpi=(192, 192), optimize=True)
        return img.size

    def _draw_images(self, img, wsf, xs, ys, X):
        import io
        for im in wsf._images:
            try:
                fr = im.anchor._from
                c0, r0 = fr.col + 1, fr.row + 1
                if c0 not in xs or r0 not in ys:
                    continue
                pw = im.anchor.ext.width / 9525.0
                ph = im.anchor.ext.height / 9525.0
                pic = Image.open(io.BytesIO(im._data())).convert("RGB").resize((X(pw), X(ph)), Image.LANCZOS)
                img.paste(pic, (X(xs[c0]), X(ys[r0])))
            except Exception as e:  # pragma: no cover
                print("image skipped:", e, file=sys.stderr)

    # -- simple charts --------------------------------------------------------
    def _draw_charts(self, d, wsv, wsf, xs, ys, colw, rowh, rows, cols, X, img):
        for ch in wsf._charts:
            try:
                fr = ch.anchor._from
                c0, r0 = fr.col + 1, fr.row + 1
                if c0 not in xs or r0 not in ys:
                    continue
                w = int((ch.width or 15) / 2.54 * 96)
                h = int((ch.height or 7.5) / 2.54 * 96)
                x_a, y_a = xs[c0], ys[r0]
                self._one_chart(d, ch, x_a, y_a, w, h, X)
            except Exception as e:  # pragma: no cover
                print("chart skipped:", e, file=sys.stderr)

    def _series_vals(self, ref):
        f = ref.f
        m = re.match(r"'?(.*?)'?!\$?([A-Z]+)\$?(\d+)(?::\$?([A-Z]+)\$?(\d+))?", f)
        sh, c1, r1, c2, r2 = m.groups()
        c2, r2 = c2 or c1, r2 or r1
        out = []
        for row in self.wbv[sh][f"{c1}{r1}:{c2}{r2}"]:
            for c in row:
                out.append(self.val(sh, c.row, c.column))
        return out

    def _one_chart(self, d, ch, x, y, w, h, X):
        d.rectangle([X(x), X(y), X(x + w), X(y + h)], fill=(255, 255, 255), outline=(190, 190, 190))
        tt = ""
        try:
            tt = "".join(r.t for p in ch.title.tx.rich.p for r in (p.r or []))
        except Exception:
            pass
        tf = get_font(13 * SCALE, bold=True)
        d.text((X(x + 10), X(y + 6)), tt, font=tf, fill=(40, 40, 40))
        px0, py0, px1, py1 = x + 55, y + 34, x + w - 15, y + h - 40
        pal = [(68, 114, 196), (237, 125, 49), (112, 173, 71), (255, 192, 0), (91, 155, 213)]
        ser = ch.series
        sf = get_font(9.5 * SCALE)
        d.rectangle([X(px0), X(py0), X(px1), X(py1)], outline=(170, 170, 170))
        if type(ch).__name__ == "BarChart":
            cats = self._series_vals(ser[0].cat.strRef or ser[0].cat.numRef) if ser[0].cat else []
            vals = [[(v or 0) for v in self._series_vals(s.val.numRef)] for s in ser]
            islog = bool(getattr(ch.y_axis.scaling, "logBase", None))
            vmax = max(max(v) for v in vals) or 1
            n = len(cats)
            gw = (px1 - px0) / max(1, n)
            if islog:
                lo, hi = 0, math.ceil(math.log10(vmax))
                top = 10 ** hi
                ticks = [(10 ** k, (k - lo) / (hi - lo)) for k in range(lo, hi + 1)]
                frac = lambda v: (math.log10(max(v, 1)) - lo) / (hi - lo)
            else:
                exp = 10 ** math.floor(math.log10(vmax))
                top = math.ceil(vmax / exp * 1.0 / 0.5) * 0.5 * exp if vmax / exp < 5 else math.ceil(vmax / exp) * exp
                ticks = [(top * t / 5, t / 5) for t in range(6)]
                frac = lambda v: v / top
            for tv, tf_ in ticks:
                yy = py1 - (py1 - py0) * tf_
                d.line([X(px0), X(yy), X(px1), X(yy)], fill=(225, 225, 225))
                lab = str(int(tv)) if tv >= 1 else format_value(tv, "General")
                d.text((X(px0 - 6) - d.textlength(lab, font=sf), X(yy) - sf.size / 2), lab, font=sf, fill=(90, 90, 90))
            ns = len(vals)
            for i in range(n):
                for k in range(ns):
                    bw = gw * 0.7 / ns
                    bx = px0 + gw * i + gw * 0.15 + bw * k
                    bh = (py1 - py0) * frac(vals[k][i])
                    d.rectangle([X(bx), X(py1 - bh), X(bx + bw), X(py1)], fill=pal[k % 5])
                lab = str(cats[i])[:12]
                d.text((X(px0 + gw * i + gw / 2) - d.textlength(lab, font=sf) / 2, X(py1 + 4)), lab, font=sf, fill=(70, 70, 70))
        else:   # scatter
            allx, ally = [], []
            data = []
            for s in ser:
                xv = [v for v in self._series_vals(s.xVal.numRef)]
                yv = [v for v in self._series_vals(s.yVal.numRef)]
                pts = [(a, b) for a, b in zip(xv, yv) if isinstance(a, (int, float)) and isinstance(b, (int, float))]
                data.append(pts)
                allx += [p[0] for p in pts]
                ally += [p[1] for p in pts]
            if not allx:
                return
            xa, xb, ya, yb = min(allx), max(allx), min(ally), max(ally)
            if xb == xa: xb = xa + 1
            if yb == ya: yb = ya + 1
            for k, pts in enumerate(data):
                P = [(px0 + (px1 - px0) * (a - xa) / (xb - xa), py1 - (py1 - py0) * (b - ya) / (yb - ya)) for a, b in pts]
                d.line([(X(a), X(b)) for a, b in P], fill=pal[k % 5], width=int(2 * SCALE))
            for t in range(5):
                lab = format_value(ya + (yb - ya) * t / 4, "0.00")
                yy = py1 - (py1 - py0) * t / 4
                d.text((X(px0 - 6) - d.textlength(lab, font=sf), X(yy) - sf.size / 2), lab, font=sf, fill=(90, 90, 90))
                lab = format_value(xa + (xb - xa) * t / 4, "0.0")
                xx = px0 + (px1 - px0) * t / 4
                d.text((X(xx) - d.textlength(lab, font=sf) / 2, X(py1 + 4)), lab, font=sf, fill=(90, 90, 90))
        # legend
        lx = px0
        for k, s in enumerate(ser):
            try:
                name = self._series_vals(s.tx.strRef)[0]
            except Exception:
                name = "series %d" % (k + 1)
            d.rectangle([X(lx), X(y + h - 20), X(lx + 10), X(y + h - 10)], fill=pal[k % 5])
            d.text((X(lx + 14), X(y + h - 22)), str(name), font=sf, fill=(60, 60, 60))
            lx += 30 + d.textlength(str(name), font=sf) / SCALE


# ====================================================================== snapshot catalogue
def _id_rows(snap, sheet, col=2, first=5):
    ws = snap.wbf[sheet]
    return {snap.val(sheet, r, col): r for r in range(first, ws.max_row + 1) if snap.val(sheet, r, col) not in (None, "")}


def build_all(only=None):
    OUT.mkdir(parents=True, exist_ok=True)
    snap = Snapshot()
    caps = {}
    names = [w.title for w in snap.wbf.worksheets]
    N = lambda i: names[i]
    p = OUT / "captions.json"
    if only and p.exists():
        caps = json.loads(p.read_text())

    def add(name, sheet_idx, rng, caption, concept, **kw):
        if only and not name.startswith(only):
            return
        sheet = N(sheet_idx)
        size = snap.render(sheet, rng, OUT / (name + ".png"), **kw)
        filt = []
        if kw.get("rows") is not None:
            filt = ["%s" % r for r in kw["rows"]]
        caps[name] = {"file": name + ".png", "sheet": sheet, "range": rng, "caption": caption, "concept": concept,
                      "formula_bar": kw.get("formula_cell"), "rows_shown": (",".join(filt) if filt else "all"),
                      "columns_shown": ([get_column_letter(c) for c in kw["cols"]] if kw.get("cols") else "all"),
                      "px": list(size)}
        print("wrote", name, size)

    # ---- sheet 0 ---------------------------------------------------------------------------------------------
    add("s00_cover_kpis", 0, "A1:I19", "Cover sheet: author block and the two KPI tile rows, every number computed by an Excel formula or exported from the program.",
        "Documentation generated from the program's own output", rows=list(range(1, 19)), cols=list(range(1, 8)))
    # ---- sheet 2 : vertex data -------------------------------------------------------------------------------
    add("sheet_vertex_cube", 2, "A4:H29", "Cube vertex buffer: 24 unique vertices (4 per face) as interleaved x, y, z, nx, ny, nz; the face is derived from the normal by an Excel formula. "
        "(The lab primitives carry no texture coordinates.)", "Vertex attributes, per-face normals, interleaved layout (Lab 04)", formula_cell="H6")
    add("s02_cube_indices", 2, "J4:O17", "Cube index buffer: 12 triangles = 36 indices; column O is an Excel formula testing the winding with n0 . ((p1-p0) x (p2-p0)) (green = CCW = front face).",
        "Indexed drawing and counter-clockwise winding / back-face culling", formula_cell="O6")
    add("sheet_memory", 2, "J20:Q39", "Technique 1 (separate triangles) versus Technique 2 (unique vertices + indices) for one face and for the whole cube; every figure is an Excel formula "
        "and the last row checks it against the exporter's byte count.", "Vertex buffer memory: non-indexed vs indexed (VBO + EBO)", formula_cell="O37", row_px={21: 40})
    add("s02_sphere_cylinder", 2, "A42:Q55", "Sphere and cylinder vertex data (first 12 vertices each); the check columns |p| = 0.5 and sqrt(x^2+y^2) = 0.5 confirm the parametrisation.",
        "Procedural primitives: spherical / cylindrical parametrisation and analytic normals", formula_cell="H44", cols=[1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17])
    # ---- sheet 3 : per-object construction ---------------------------------------------------------------------
    ids3 = _id_rows(snap, N(3), 1, 5)
    ws3 = snap.wbf[N(3)]
    objs = [("bunker", "Command Bunker"), ("uav", "Autonomous UAV"), ("rover", "Target Rover"), ("tank", "Main Battle Tank"),
            ("helicopter", "Scout Helicopter"), ("aagun", "AA Gatling Gun"), ("radar", "Air-defence radar"),
            ("sam", "SAM launcher"), ("mlrs", "MLRS rocket truck"), ("gate", "Boom barrier"), ("flag", "Flag")]
    by_obj = {}
    for r in range(5, ws3.max_row + 1):
        by_obj.setdefault(snap.val(N(3), r, 2), []).append(r)
    cx = column_index_from_string
    # summary table
    add("s03_primitive_mix", 3, "AA3:AI25", "Primitive mix per final object: how many cylinders, spheres, cones, cubes and frusta (and how many group nodes) build each object; "
        "Excel COUNTIFS over the 4 549-row construction table.", "Everything is built from five unit primitives", formula_cell="AH8", col_px={cx("AA"): 150})
    cols3 = [1, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
    w3 = {1: 62, 3: 92, 4: 170, 5: 62, 7: 46, 8: 46, 9: 46, 10: 60, 11: 60, 12: 60, 13: 56, 14: 56, 15: 56, 16: 70}

    def pick(rows, cap=24):
        grp = [r for r in rows if snap.val(N(3), r, 3) == "Group" and snap.val(N(3), r, 6) <= 3][:10]
        lead = rows[:6]
        leaves = [r for r in rows if snap.val(N(3), r, 3) != "Group"]
        step = max(1, len(leaves) // 8)
        sam = leaves[::step][:8]
        sel = sorted(set(lead + grp + sam))
        return sel[:cap]

    story = {
        "bunker": "Command bunker (underground, 400 parts): walls, ramp, blast doors and rooms as cubes and cylinders.",
        "uav": "Autonomous UAV: fuselage, four motor mounts with rotor groups, gimbal yaw / pitch chain, searchlight.",
        "rover": "Target rover: chassis, wheels, thermal engine block (heat column drives the thermal camera).",
        "tank": "Main battle tank: hull, tracks, turret yaw, gun pitch and recoil group.",
        "helicopter": "Scout helicopter: fuselage, main rotor and tail rotor groups.",
        "aagun": "AA Gatling gun: pedestal, yaw / pitch mount, rotating barrel cluster.",
        "radar": "Air-defence radar: tower and rotating antenna of tiles.",
        "sam": "SAM launcher: chassis, elevating launcher frame and missile tubes.",
        "mlrs": "MLRS rocket truck: cab, elevating pod frame and rocket tubes.",
        "gate": "Gate boom barrier: post, counterweight and pivoting boom with stripes.",
        "flag": "Flag: pole and a chain of cloth strips each yawed by a travelling wave.",
    }
    concept = {"bunker": "Primitive composition with S, R, T rows", "uav": "Parent-child groups (rotors, gimbal)", "rover": "Instancing of a primitive per part",
               "tank": "Hierarchical articulation: hull > turret > gun", "helicopter": "Rotating sub-hierarchies", "aagun": "Joint chains", "radar": "Animated group transform",
               "sam": "Elevation joint", "mlrs": "Elevation joint and repeated children", "gate": "Single-axis hinge", "flag": "Deep chain (travelling wave)"}
    for key, nm in objs:
        rows = by_obj[nm]
        roots = [r for r in rows if snap.val(N(3), r, 6) == 0]
        if len(roots) > 1:                       # e.g. two UAVs / three rovers in one object: show the first
            rows = [r for r in rows if r < roots[1]]
        if key == "tank":
            ids = ["TNK-00", "TNK-01", "TNK-02", "TNK-03", "TNK-04", "TNK-41", "TNK-201", "TNK-202", "TNK-203", "TNK-246", "TNK-247", "TNK-249", "TNK-250", "TNK-251"]
            sel = [ids3[i] for i in ids if i in ids3]
        else:
            sel = pick(rows)
        name = "sheet_construction_tank" if key == "tank" else "s03_" + key
        add(name, 3, "A4:Q%d" % rows[-1], "Construction rows of the %s (%d rows of %d shown; row labels are the real sheet rows). Every part is one primitive with S, R (degrees) and T relative to its parent; "
            "shading model in the last column. %s" % (nm.lower(), len(sel), len(rows), story[key]), concept[key], rows=[4] + sel, cols=cols3, col_px=w3)
    tk = [ids3[i] for i in ["TNK-00", "TNK-01", "TNK-201", "TNK-202", "TNK-246", "TNK-247", "TNK-249", "TNK-250"] if i in ids3]
    add("s03_tank_material", 3, "A4:X5000", "Tank rows, material and thermal columns: shading, procedural surface, roughness, metalness, heat (thermal-camera value), emissive, animation text.",
        "Per-part material (PBR) and heat for the thermal camera", rows=[4] + tk, cols=[1, 4, 16, 17, 18, 19, 20, 21, 24], col_px={24: 330, 4: 150, 17: 70})
    # ---- sheet 4 : transforms ---------------------------------------------------------------------------------
    W4 = N(4)
    cx = column_index_from_string
    add("sheet_mmult", 4, "BD11:BQ29", "Worked example for one component (CAMP-55): rotation blocks Ry, Rx, Rz, then R = Ry x Rx x Rz, T x R and finally M = T x R x S, every cell an Excel sum of products; "
        "the last block is |M - M_code|.", "Model matrix M = T . R . S built by matrix products", formula_cell="BN19")
    add("s04_inputs_blocks", 4, "AY4:BL22", "Worked example inputs (S, R in degrees, T looked up by component ID with INDEX / MATCH) and the translation, yaw, pitch blocks, plus Ry x Rx.",
        "Homogeneous 4x4 matrices: translation and axis rotations", formula_cell="AZ7")
    ids4 = _id_rows(snap, W4, 2, 6)
    gim = [ids4[i] for i in ["UAV-00", "UAV-43", "UAV-44", "UAV-45", "UAV-46", "UAV-47", "UAV-48"]]
    add("s04_rows_error", 4, "A3:AW40", "Per-component rows of sheet 4 for the UAV gimbal chain: S, R, T inputs, the 3x3 rotation entries and the max-error column (Excel M versus the code's matrix).",
        "Excel recomputes the matrices the program uses (verification, tolerance 1e-4)", rows=[5] + gim,
        cols=[2, 4, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 49], formula_cell="AW%d" % gim[3])
    add("s04_rows_m", 4, "A3:AW40", "The 12 M entries computed by Excel for the gimbal chain next to the code's matrix, and the max |difference| column.",
        "Model matrix entries M11..M34 (Excel) vs code", rows=[5] + gim, cols=[2, 4] + list(range(25, 37)) + [49], formula_cell="Y%d" % gim[3], col_px={4: 110})
    # ---- sheet 5 : hierarchy ----------------------------------------------------------------------------------
    W5 = N(5)
    ids5 = _id_rows(snap, W5, 2, 5)
    uav = [ids5[i] for i in ["UAV-00", "UAV-43", "UAV-44", "UAV-45", "UAV-46", "UAV-47", "UAV-48", "UAV-52"] if i in ids5]
    tnk = [ids5[i] for i in ["TNK-00", "TNK-01", "TNK-201", "TNK-246", "TNK-247", "TNK-249", "TNK-250"] if i in ids5]
    add("sheet_hierarchy", 5, "A3:AI1850", "Hierarchy sheet, UAV gimbal chain: UAV root > gimbal yaw > gimbal pitch > lamp / lens / camera; world position and the Excel-vs-code check, with the animation text of each joint.",
        "Scene graph: world = parent_world x M down a chain", rows=[4] + uav, cols=[1, 2, 3, 4, 5, 6, 7, 8, 33, 35], col_px={1: 150, 35: 400}, formula_cell="U%d" % uav[3])
    add("s05_tank_chain", 5, "A3:AI1850", "Hierarchy sheet, tank chain: tank root > hull > turret yaw > gun pitch > mantlet / barrel recoil. Depth increases by one per joint.",
        "Articulated chain: turret yaw then gun pitch then recoil", rows=[4] + tnk, cols=[1, 2, 3, 4, 5, 6, 7, 8, 33, 35], col_px={1: 150, 35: 400}, formula_cell="U%d" % tnk[4])
    add("s05_world_matrix", 5, "A4:AG1850", "World matrices of the UAV gimbal chain computed by Excel: W = W_parent x M (each W cell is a sum of products referencing the parent's row and sheet 4), next to the code's values.",
        "World matrix = parent world matrix x local matrix", rows=[4] + uav, cols=[2] + list(range(9, 21)) + [33], formula_cell="U%d" % uav[3])
    # ---- sheet 6 ---------------------------------------------------------------------------------------------
    add("s06_geometry_stats", 6, "A4:P11", "Memory of every GPU mesh: indexed versus non-indexed, per-mesh saving and vertex reuse, and the formula vs exporter check.",
        "Indexed drawing saves memory (shared vertices)", cols=[2, 4, 5, 6, 7, 9, 10, 11, 12, 16], formula_cell="J5")
    add("s06_charts", 6, "A13:S34", "Memory charts: bytes per mesh indexed versus non-indexed (drawn from the sheet's chart definitions).", "Memory saving with indices", charts=True)
    # ---- sheets 7, 8, 9 -----------------------------------------------------------------------------------------
    add("sheet_lighting", 7, "A4:R8", "The three scene lights plus the ambient term: type, position, direction, colour, intensity, attenuation coefficients (k linear, k quadratic), spot cone angles, role.",
        "Light types (directional / point / spot), attenuation and cone parameters", cols=[1, 2, 3, 4, 5, 9, 10, 11, 12, 13, 14, 15, 16, 17], col_px={2: 150})
    add("s07_attenuation", 7, "A14:K32", "Distance attenuation 1/(1 + k1 d + k2 d^2) for the beacon and the searchlight, and the spotlight cone factor versus angle; all Excel formulas.",
        "Attenuation law and spot cone falloff", formula_cell="B16", cols=[1, 2, 3, 4, 5, 8, 9, 10, 11])
    add("s07_charts", 7, "M14:Z50", "Attenuation and cone curves drawn from the sheet's scatter charts.", "Attenuation curve shapes", charts=True)
    add("s08_shading", 8, "A4:H13", "Shading models used in the scene: parts using each (COUNTIF), which normal is used, where lighting is evaluated, cost and visual effect.",
        "Flat vs Gouraud vs Phong vs PBR", cols=[1, 2, 4, 5, 6, 7], col_px={4: 150, 5: 200, 7: 230}, formula_cell="B5")
    add("s08_comparison", 8, "A12:J31", "Same bunker, same camera: flat, Gouraud and Phong override side by side (renders from the program).", "Visual difference of shading models")
    add("s09_pipeline_passes", 9, "A20:D30", "GPU pass list of one frame (measured): time per pass, share of the total and cumulative time, Excel formulas.",
        "Render pipeline: shadow maps, pre-pass, SSAO, volumetric fog, scene, bloom, composite", formula_cell="C23")
    add("s09_features", 9, "A5:H17", "Per-feature frame cost: configuration, frame time, baseline, feature cost and share (differences computed by Excel).", "Cost of each rendering feature", cols=[1, 2, 3, 4, 5, 6, 7, 8], formula_cell="G11")
    # ---- sheet 10 ---------------------------------------------------------------------------------------------
    add("s10_testing", 10, "A1:G30", "Test sheet: KPI row (tests, passed, failed, pass rate - Excel formulas over the status column) and the first 25 tests with expected and observed results.",
        "Automated verification of the geometry, transforms and lighting", rows=list(range(1, 4)) + list(range(5, 31)), cols=[1, 2, 3, 4, 5, 6, 7, 8], col_px={2: 70, 3: 270, 4: 250, 5: 270, 7: 70, 8: 70}, nowrap=(2, 7), formula_cell="H3")
    # ---- sheet 14 ---------------------------------------------------------------------------------------------
    add("sheet_controllables", 14, "A3:L21", "All controllable objects: root, part counts per primitive, group nodes and animated nodes (Excel SUMIFS over the construction sheet).",
        "Objects with independent articulated motion", rows=[3] + list(range(5, 22)), formula_cell="E6", row_cap=36)
    W14 = N(14)
    keys = [("uav", "UAV-1 DRONE"), ("tank", "MBT-1 TANK"), ("helicopter", "MH-6 HELICOPTER"), ("aagun", "AA-1 GATLING"), ("radar", "AIR-DEFENCE RADAR"),
            ("doors", "BLAST DOORS"), ("sam", "SAM-1 LAUNCHER"), ("mlrs", "MLRS-1 ROCKET TRUCK"), ("gate", "GATE BARRIER")]
    ws14 = snap.wbf[W14]
    for key, nm in keys:
        rows = [r for r in range(174, 651) if snap.val(W14, r, 2) == nm][:15]
        add("s14_" + key, 14, "A173:L650", "Controllable %s: the moving parts and the animation rule of each articulated node, as exported from the code." % nm,
            "Joint hierarchy and procedural animation per object", rows=[173] + rows, cols=[3, 4, 5], col_px={3: 80, 4: 130, 5: 620})
    add("s14_specs_tank", 14, "A24:F60", "Specification rows of the tank read from the code constants, with the Excel-derived figures in sheet 14 below them.",
        "Kinematic limits (slew rates, elevation range, reload)", rows=[25] + [r for r in range(26, 154) if snap.val(W14, r, 2) == "MBT-1 TANK"], cols=[2, 3, 4, 5, 6], col_px={6: 260, 3: 170})
    add("s14_derived", 14, "A156:D169", "Derived figures computed by Excel from the specification table (e.g. turret 360 degrees = 360 / slew rate).", "Kinematics from rates", formula_cell="D157")
    # ---- sheets 16, 18, 19, 20 ---------------------------------------------------------------------------------
    add("s16_weapons", 16, "A35:I40", "Weapons: speeds, blast power, damage radius (= power x kDamageRadiusPerPower), flight time, range, reload and guidance; formulas over the constants table.",
        "Projectile motion and blast model", formula_cell="D37", cols=[1, 2, 3, 4, 5, 6, 7, 8])
    add("s16_effects", 16, "A43:G54", "Particle effects measured by running the real Combat code: particle count, peak, lifetime and dynamic light for each effect.",
        "Particle systems and dynamic lights for explosions", cols=[1, 2, 3, 4, 5, 6, 7], col_px={1: 230})
    add("sheet_cv", 18, "A4:E16", "Computer-vision pipeline: the 11 stages from the adaptive threshold to the Kalman track life cycle, with the code location of each.",
        "Image processing on the thermal view: threshold, labelling, tracking", col_px={1: 40, 2: 170, 3: 330, 4: 330, 5: 200}, cols=[1, 2, 3, 4, 5], rows=list(range(4, 17)))
    add("s18_params_footprint", 18, "A40:E59", "Derived camera figures (pixels per degree, horizontal FOV, gate radius) and the ground footprint of one pixel versus UAV altitude, all Excel formulas.",
        "Camera model: field of view, pixel-to-ground scale", formula_cell="B44")
    add("s18_tests", 18, "A63:E73", "Vision tests T130-T137 with their PASS status looked up from the testing sheet (INDEX / MATCH).", "Verification of detection and tracking", col_px={2: 300, 3: 230, 4: 300})
    add("sheet_mission", 19, "A40:F62", "Time-on-target model of the rail rounds: ramp distance, flight time t(d) and its closed-form check against roundFlightTime(d); the last row confirms formula = code.",
        "Kinematics of a homing round with acceleration ramp", formula_cell="C50")
    add("s19_combined", 19, "A65:G71", "Combined strike: launch delay of the nearer UAV so both rounds arrive together (arrival spread = 0).", "Synchronised time-on-target", formula_cell="F66")
    add("s19_roles", 19, "A4:C16", "Squad roles and strike phases.", "Multi-agent coordination (lead / wing)", col_px={3: 520, 2: 280})
    add("s19_waves", 19, "A74:D100", "Mission constants: waves, base integrity and score parameters from the code.", "Game rules (waves, base integrity)")
    add("s19_armour", 19, "A102:D117", "Armour and damage falloff: damage per round, rounds to kill with one or two UAVs and the reload of one UAV, Excel formulas.", "Blast falloff, rounds to kill", formula_cell="C110")
    add("s20_sounds", 20, "A4:D21", "Synthesised sounds: trigger, technique and key parameters of all 15 sounds (no sample files).", "Procedural audio synthesis", col_px={2: 200, 3: 260, 4: 230})
    add("s20_mixer", 20, "A24:C35", "Mixer: sample rate, voices, panning, distance gain, air absorption, Doppler.", "Spatial audio (panning, attenuation, Doppler)", col_px={2: 260, 3: 330})
    add("s20_hud_tests", 20, "A56:E71", "Audio tests with PASS status looked up from sheet 10.", "Verification of the audio chain", col_px={2: 260, 3: 230, 4: 250})
    # ---- sheets 21-23 (v6); skipped quietly while the workbook has no such sheet -----------------------------------
    if len(names) > 23:
        add("sheet_daycycle", 21, "A1:W40", "Day Cycle & Lighting: the exported 24 h grid (sun elevation, key intensity, ambient, fog, exposure, solar, beams, waves, cloud-shadow mean) "
            "with the Excel recomputation of the sun elevation from latitude, declination and hour, the difference and the OK check.",
            "Solar position model and the day-cycle lighting curves", formula_cell="T%d" % 20, row_cap=40)
        add("sheet_soldiers", 22, "A1:Q40", "Soldiers & Routes: roster with day / night duty, primitive counts per soldier (Excel totals), route lengths, joint tree and gait constants.",
            "Soldier hierarchy, gait and spline routes", row_cap=40)
        add("sheet_cvnoise", 23, "A1:H60", "CV Noise & Seeds: sensor noise configuration, P_d / false-alarm sweep against NETD, derived stream seeds and the v6 tests with the status looked up from the testing sheet.",
            "Sensor noise model and detector statistics", row_cap=40)
    # ---- sheets 24-27 (v7, WP-W); rows located by their section titles so the catalogue survives layout changes -------
    if len(names) > 27:
        def sec(i, prefix):
            ws_ = snap.wbf[N(i)]
            return next(r for r in range(1, ws_.max_row + 1) if str(ws_.cell(r, 1).value or "").startswith(prefix))
        run = sec(24, "Measured GPU timings") - 2
        add("sheet_raytracing", 24, "A3:N%d" % run, "Ray Tracing: the five render modes cycled with ';' (names and captions from the code), their internal resolution, "
            "the measured GPU frame and ray-tracing pass times on the RTX 4070 for the selected run, FPS, speed-up versus raster and ns per traced pixel "
            "(Excel formulas over the raytrace_perf.json rows below), and the KPI strip.", "Ray tracing modes and their cost: raster, hybrid, Whitted, path, light tracing",
            rows=list(range(3, run + 1)), formula_cell="H6", col_px={3: 300})
        b0, b1 = sec(24, "Worked SAH example"), sec(24, "Probe:")
        add("sheet_rt_bvh", 24, "A%d:L%d" % (b0, b1 + 10), "Ray Tracing, BVH: the surface-area-heuristic cost of the root split recomputed by Excel from the exported child boxes, "
            "and a 64 x 36 probe of primary rays: BVH node visits and primitive tests per ray versus brute force, with the hit-for-hit agreement check.",
            "Bounding volume hierarchy and the SAH cost model", formula_cell="B%d" % (b0 + 5), row_cap=40)
        add("sheet_explorer", 25, "A4:I21", "Frame Explorer: the 16 stages of one frame as the program shows them (fx::stageInfo: title, caption, key formula, concepts), "
            "the layers of each stage and its capture in report/figures_v7.", "Frame generation step by step: the stages of the graphics pipeline",
            col_px={1: 40, 2: 80}, row_cap=90)
        add("sheet_controls", 26, "A4:E%d" % (sec(26, "POV sources") - 2), "Game Camera & Controls: the v7 key bindings exported from keyBindingsV7 (the rows the F1 help draws) with the owning work "
            "package, and the keys inside the Frame Explorer.", "Game camera controls: POV picker, jumps, free camera, render modes, ray debugger, explorer",
            formula_cell="E6", row_cap=60, col_px={1: 40})
        p0 = sec(26, "POV sources") + 1
        add("sheet_pov_sources", 26, "A%d:Q%d" % (p0 + 1, sec(26, "Sources by kind") - 2), "Game Camera & Controls, POV sources: every moving object the camera can jump into "
            "(listPovSources at t = 0), whether the player can take control, the alternative view, position, distance, the picker row recomputed by Excel "
            "(nearest first) and the first-person eye.", "Possessable objects and first-person cameras", formula_cell="L%d" % (p0 + 2), row_cap=40)
        add("sheet_v7tests", 27, "A3:G11", "v7 Tests: the v7 test blocks (T240-T310) with PASS / FAIL / pending counted by COUNTIFS over the test list, whose status is looked up from the testing sheet.",
            "Verification of the v7 features", formula_cell="D6")
    if not only or True:
        p.write_text(json.dumps(caps, indent=1, ensure_ascii=False))


if __name__ == "__main__":
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    build_all(only)
