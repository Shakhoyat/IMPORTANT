"""Pictures of the README: the last state of chosen plates and chosen frames of the film -> docs/img/*.jpg.

Run after the film is built:
  ~/.local/bin/uv run -q --python 3.12 --with numpy --with pillow python tools/film/figures.py"""
import glob
import os
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.argv = ["build.py", "plan"]
import build  # noqa: E402
import dsl  # noqa: E402

OUT = os.path.join(dsl.ROOT, "docs", "img")
PLATES = ["map", "primgrid", "sphere", "trs", "project", "clipping", "raster", "phong", "shadowmap", "rays", "sun", "instancing", "bench", "sensor",
          "cvsteps", "classify", "kalman", "tracklog", "decide1", "decide2", "tot", "rgrid", "rcost", "rflood", "lfr", "revade", "astar", "gait", "particles"]
# (file name, start of the beat's title, which beat with that title, seconds into the beat)
FRAMES = [("film_open_world", "An open world", 0, 9.0), ("film_menu", "Menu and help", 0, 3.5), ("film_primitive_key", "F: which primitive", 0, 9.0),
          ("film_explode", "G: explode", 0, 4.0), ("film_axes", "H: the axes", 0, 7.0), ("film_projection_top", "Ctrl+P: the projection", 1, 3.0),
          ("film_depth_buffer", "F: the depth buffer", 0, 3.5), ("film_frame_explorer", "Frame Explorer: rasterisation", 0, 5.0),
          ("film_light_terms", "Ctrl+L", 1, 2.5), ("film_shading", "O: the shading model", 0, 5.0), ("film_ray_tracing", "; the render mode", 1, 6.5),
          ("film_ray_tree", "' the ray debugger", 0, 6.0), ("film_day_cycle", "A day in", 0, 10.0), ("film_tank_sight", "V: gunner's sight", 0, 4.0),
          ("film_helicopter", "The helicopter", 0, 13.0), ("film_drone_hud", "A drone: chase", 0, 6.0), ("film_click_soldier", "Click a soldier", 0, 5.5),
          ("film_first_person", "Through his eyes", 0, 4.0), ("film_pov_list", "F4: the list", 0, 3.0), ("film_ride_round", "Riding a round", 0, 3.0),
          ("film_thermal", "F: thermal view", 0, 3.0), ("film_tracks", "Tracks coast", 0, 5.0), ("film_combined_strike", "Combined strike", 0, 3.2),
          ("film_flood_field", "M: the flood field", 0, 6.0), ("film_rover", "A rover on its line", 0, 5.0), ("film_three_views", "One moment, three views", 0, 9.0)]


def main():
    build.load()
    build.timing()
    os.makedirs(OUT, exist_ok=True)
    for b in dsl.beats():
        if b["kind"] == "plate" and b["name"] in PLATES:
            stamp = os.path.getmtime(os.path.join(dsl.OUT, "plates", b["id"] + ".sig"))    # states of this build only
            last = sorted(f for f in glob.glob(os.path.join(dsl.OUT, "plates", b["id"] + "_[0-9][0-9].png")) if os.path.getmtime(f) > stamp - 900)[-1]
            Image.open(last).convert("RGB").resize((1600, 900), Image.LANCZOS).save(os.path.join(OUT, "plate_%s.jpg" % b["name"]), quality=86, optimize=True)
    pic = os.path.join(dsl.OUT, "picture.mp4")
    for name, title, k, at in FRAMES:
        found = [b for b in dsl.beats() if b.get("title", "").startswith(title)]
        if len(found) <= k:
            print("no beat titled", title)
            continue
        tmp = os.path.join(OUT, name + ".png")
        subprocess.run([build.FF, "-y", "-loglevel", "error", "-ss", "%.2f" % (found[k]["start"] + at), "-i", pic, "-frames:v", "1", tmp], check=True)
        Image.open(tmp).convert("RGB").resize((1280, 720), Image.LANCZOS).save(os.path.join(OUT, name + ".jpg"), quality=84, optimize=True)
        os.remove(tmp)
    print("wrote", len(os.listdir(OUT)), "pictures to docs/img,", sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT)) // 1024, "KiB")


if __name__ == "__main__":
    main()
