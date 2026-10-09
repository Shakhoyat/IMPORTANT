"""v10 Phase 0: object / primitive table for docs/audit.md from `ts --export-docs FILE` (no GPU)."""
import json, sys
d = json.load(open(sys.argv[1]))
print("| Object | Parts | Groups | Animated nodes | Primitives used |")
print("|---|---:|---:|---:|---|")
seen = set()
for o in d["objects"]:
    name = o["object"]
    base = name.rstrip(" 0123456789")
    if base in seen and base in ("Soldier", "Main Battle Tank"):
        continue
    seen.add(base)
    n = sum(1 for x in d["objects"] if x["object"].rstrip(" 0123456789") == base)
    label = f"{base} (x{n})" if n > 1 else name
    prims = ", ".join(f"{k} {v}" for k, v in sorted(o["primitives"].items()))
    print(f"| {label} | {o['parts']} | {o['groups']} | {o['animated_nodes']} | {prims} |")
print()
print("| Primitive | Generator | Vertices | Triangles | Instances in scene |")
print("|---|---|---:|---:|---:|")
for p in d["primitives"]:
    print(f"| {p['type']} | `{p['function']}` | {p['vertices']} | {p['triangles']} | {p['instances']} |")
print(f"\nScene leaves (drawn parts): {d['leaf_count']}. Matrix convention: {d['matrix_convention']}")
