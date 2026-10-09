"""The primitive colour key for the Python report tools, read from src/report/PrimitiveColors.h."""
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
_ROW = re.compile(r'\{"([^"]+)", "([a-z]+)", "([^"]+)", "([a-z]+)", (\d+), (\d+), (\d+), ([\d.]+)\}')
ROWS = []
for _m in _ROW.finditer(open(os.path.join(ROOT, "src", "report", "PrimitiveColors.h")).read()):
    _k, _t, _l, _f, _r, _g, _b, _L = _m.groups()
    ROWS.append(dict(key=_k, tex=_t, label=_l, family=_f, rgb=(int(_r), int(_g), int(_b))))
BY_KEY = {r["key"]: r for r in ROWS}
ORDER = [r["key"] for r in ROWS]


def tex_key(export_name):
    return BY_KEY[export_name]["tex"]


def rgb01(export_name):
    return tuple(v / 255.0 for v in BY_KEY[export_name]["rgb"])


def chip(export_name, n=None):
    t = tex_key(export_name)
    return "\\primchipn{%s}{%s}" % (t, n) if n is not None else "\\primchip{%s}" % t
