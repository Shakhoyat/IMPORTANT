#!/usr/bin/env python3
"""v9 M6/M7: downloads the CC0 source assets (Poly Haven models, ambientCG materials) into a local cache.

    python3 tools/fetch_assets.py [--cache build/asset_cache] [--only name,name]

The cache holds the full-resolution originals (fir_tree_01 alone is 480 MB, pine_tree_01 950 MB): it is NOT committed.
tools/bake_assets (C++, `make bake-assets`) reduces them to the game-ready files under assets/ (see assets/LICENSES.md).
Every asset is downloaded into its own directory and only ever opened as data (JSON / glTF / images).
"""
import argparse
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.request
import zipfile

# Poly Haven models (https://polyhaven.com/license: CC0 1.0). Name -> what the game uses it for.
POLYHAVEN_MODELS = {
    "fir_tree_01": "treeline + hill firs (3 variants)",
    "pine_tree_01": "treeline + hill pines (3 variants)",
    "fir_sapling_medium": "camp firs (3 variants)",
    "pine_sapling_small": "young pines at forest edges (3 variants)",
    "tree_small_02": "camp broadleaf trees",
    "shrub_01": "bushes", "shrub_02": "bushes", "shrub_03": "bushes", "shrub_04": "bushes",
    "fern_02": "ground cover under trees",
    "grass_medium_01": "grass blade atlas", "grass_medium_02": "grass blade atlas",
    "boulder_01": "boulders", "rock_moss_set_01": "mossy rocks", "rock_moss_set_02": "mossy rocks",
}
# ambientCG materials (https://docs.ambientcg.com/license/: CC0 1.0) at 1K-JPG. Name -> terrain layer.
AMBIENTCG_MATERIALS = {
    "Grass004": "grass", "Ground048": "dirt", "Rock051": "rock", "Gravel040": "gravel", "Ground037": "forest floor",
}

UA = {"User-Agent": "ThermalSense-asset-fetch/1.0 (university lab project)"}


def get(url, tries=5):
    for k in range(tries):
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except urllib.error.HTTPError as e:   # the APIs answer 503 now and then: retry with a back-off
            if k + 1 == tries or e.code < 500:
                raise
            time.sleep(2.0 * (k + 1))


def download(url, path, md5=None, size=None):
    if os.path.exists(path) and (size is None or os.path.getsize(path) == size):
        return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".part"
    req = urllib.request.Request(url, headers=UA)
    h = hashlib.md5()
    with urllib.request.urlopen(req, timeout=600) as r, open(tmp, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            h.update(chunk)
            f.write(chunk)
    if md5 and h.hexdigest() != md5:
        os.remove(tmp)
        raise RuntimeError(f"md5 mismatch for {url}")
    os.replace(tmp, path)
    return True


def fetch_polyhaven(name, cache):
    info = json.loads(get(f"https://api.polyhaven.com/info/{name}"))
    files = json.loads(get(f"https://api.polyhaven.com/files/{name}"))
    g = files["gltf"]["1k"]["gltf"]
    root = os.path.join(cache, "polyhaven", name)
    download(g["url"], os.path.join(root, f"{name}.gltf"), g.get("md5"), g.get("size"))
    for rel, f in g.get("include", {}).items():
        download(f["url"], os.path.join(root, rel), f.get("md5"), f.get("size"))
    # The glTF's baseColor JPGs carry no alpha: the cut-out masks of leaves / twigs / grass come as separate maps.
    for key, res in files.items():
        if key.lower().endswith("alpha") and "1k" in res and "jpg" in res["1k"]:
            f = res["1k"]["jpg"]
            download(f["url"], os.path.join(root, "textures", os.path.basename(f["url"])), f.get("md5"), f.get("size"))
    meta = {"name": name, "source": f"https://polyhaven.com/a/{name}", "gltf_url": g["url"],
            "authors": info.get("authors", {}), "license": "CC0 1.0", "polycount": info.get("polycount")}
    with open(os.path.join(root, "meta.json"), "w") as f:
        json.dump(meta, f, indent=1)
    return meta


def fetch_ambientcg(name, cache):
    url = f"https://ambientcg.com/api/v2/full_json?id={name}&include=downloadData"
    d = json.loads(get(url))
    asset = d["foundAssets"][0]
    dl = None
    for cat in asset["downloadFolders"]["default"]["downloadFiletypeCategories"].values():
        for item in cat["downloads"]:
            if item.get("attribute") == "1K-JPG":
                dl = item
    if dl is None:
        raise RuntimeError(f"no 1K-JPG download for {name}")
    root = os.path.join(cache, "ambientcg", name)
    zpath = os.path.join(root, f"{name}_1K-JPG.zip")
    download(dl["fullDownloadPath"], zpath, None, dl.get("size"))
    # Only the maps the terrain shader uses are extracted (by exact name: the archive is data, nothing is executed).
    with zipfile.ZipFile(zpath) as z:
        for suffix in ("Color", "NormalGL", "Displacement", "Roughness", "AmbientOcclusion"):
            member = f"{name}_1K-JPG_{suffix}.jpg"
            if member in z.namelist():
                with z.open(member) as src, open(os.path.join(root, member), "wb") as dst:
                    dst.write(src.read())
    meta = {"name": name, "source": f"https://ambientcg.com/view?id={name}", "zip_url": dl["fullDownloadPath"],
            "authors": {"ambientCG (Lennart Demes)": "All"}, "license": "CC0 1.0"}
    with open(os.path.join(root, "meta.json"), "w") as f:
        json.dump(meta, f, indent=1)
    return meta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", default="build/asset_cache")
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = set(filter(None, a.only.split(",")))
    for name in POLYHAVEN_MODELS:
        if only and name not in only:
            continue
        m = fetch_polyhaven(name, a.cache)
        print("polyhaven", name, m["authors"], flush=True)
    for name in AMBIENTCG_MATERIALS:
        if only and name not in only:
            continue
        fetch_ambientcg(name, a.cache)
        print("ambientcg", name, flush=True)


if __name__ == "__main__":
    sys.exit(main())
