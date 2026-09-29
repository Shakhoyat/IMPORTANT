"""Summarise a ThermalSense --trace CSV (mode shares, detection rate, tracking error)."""
import csv
import sys
from collections import Counter


def summarize(path):
    rows = list(csv.DictReader(open(path)))
    n = len(rows)
    modes = Counter(r["uav_mode"] for r in rows)
    errs = sorted(float(r["est_error"]) for r in rows if float(r["est_error"]) >= 0)
    out = {
        "samples": n,
        "duration_s": float(rows[-1]["t"]) if rows else 0.0,
        "mode_share": {m: c / n for m, c in modes.items()},
        "detected_share": sum(int(r["detected"]) for r in rows) / n,
        "rover_hidden_share": sum(int(r["rover_hidden"]) for r in rows) / n,
        "est_error_median": errs[len(errs) // 2] if errs else None,
        "est_error_p90": errs[int(len(errs) * 0.9)] if errs else None,
        "est_error_max": errs[-1] if errs else None,
        "transitions": [],
    }
    prev = None
    for r in rows:
        if r["uav_mode"] != prev:
            out["transitions"].append((float(r["t"]), r["uav_mode"]))
            prev = r["uav_mode"]
    return out


if __name__ == "__main__":
    s = summarize(sys.argv[1] if len(sys.argv) > 1 else "build/trace.csv")
    for k, v in s.items():
        print(k, ":", v)
