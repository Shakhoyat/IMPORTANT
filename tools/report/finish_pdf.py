"""Last step before printing: report/main.pdf without a creation or modification date in its document information
(the report carries no date anywhere), and a check of what a print shop needs: A4 pages, every font embedded.
Run: ~/.local/bin/uv run -q --with pymupdf python tools/report/finish_pdf.py"""
import os
import sys

import pymupdf

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PDF = os.path.join(ROOT, "report", "main.pdf")

doc = pymupdf.open(PDF)
meta = dict(doc.metadata)
meta["creationDate"] = meta["modDate"] = ""
doc.set_metadata(meta)
doc.del_xml_metadata()
bad = []
for i, page in enumerate(doc):
    w, h = page.rect.width, page.rect.height
    if abs(w - 595.28) > 1 or abs(h - 841.89) > 1:
        bad.append("page %d is %.0f x %.0f pt, not A4" % (i + 1, w, h))
    for f in page.get_fonts(full=True):
        if f[1] == "n/a" and f[2] != "Type3":   # no embedded font file (Type 3 fonts carry their glyphs in the page)
            bad.append("page %d: font %s is not embedded" % (i + 1, f[3]))
tmp = PDF + ".tmp"
doc.save(tmp, garbage=3, deflate=True)
doc.close()
os.replace(tmp, PDF)
d = pymupdf.open(PDF)
print("%d pages, A4; title: %s; author: %s; creation date: %r" % (len(d), d.metadata["title"], d.metadata["author"], d.metadata["creationDate"]))
for b in sorted(set(bad))[:20]:
    print("  " + b)
sys.exit(1 if bad else 0)
