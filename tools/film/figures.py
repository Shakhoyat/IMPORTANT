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
# (file name, beat id or chapter id, seconds into the beat)
FRAMES = [("film_open_world", "what_01", 9.0), ("film_menu", "what_03", 3.5), ("film_primitive_key", "prims_04", 9.0), ("film_explode", "prims_05", 4.0),
          ("film_axes", "trs_02", 7.0), ("film_projection_top", "camera_04", 3.0), ("film_depth_buffer", "clip_03", 3.5),
          ("film_frame_explorer", "raster_02", 4.0), ("film_light_terms", "illum_04", 2.5), ("film_shading", "shade_01", 3.5),
          ("film_ray_tracing", "rt_02", 6.5), ("film_ray_tree", "rt_03", 6.0), ("film_day_cycle", "day_02", 12.0), ("film_tank_sight", "control_03", 4.0),
          ("film_helicopter", "control_06", 14.0), ("film_drone_hud", "control_07", 6.0), ("film_click_soldier", "pov_01", 5.5),
          ("film_first_person", "pov_02", 4.0), ("film_pov_list", "pov_04", 3.0), ("film_ride_round", "pov_07", 3.0), ("film_thermal", "sensor_02", 3.0),
          ("film_tracks", "track_02", 5.0), ("film_combined_strike", "strike_02", 3.2), ("film_flood_field", "rover1_03", 6.0),
          ("film_rover", "rover2_01", 5.0), ("film_three_views", "rover3_01", 9.0)]


def main():
    build.load()
    build.timing()
    os.makedirs(OUT, exist_ok=True)
    beats = {b["id"]: b for b in dsl.beats()}
    for b in beats.values():
        if b["kind"] == "plate" and b["name"] in PLATES:
            last = sorted(glob.glob(os.path.join(dsl.OUT, "plates", b["id"] + "_[0-9][0-9].png")))[-1]
            Image.open(last).convert("RGB").resize((1600, 900), Image.LANCZOS).save(os.path.join(OUT, "plate_%s.jpg" % b["name"]), quality=86, optimize=True)
    pic = os.path.join(dsl.OUT, "picture.mp4")
    for name, bid, at in FRAMES:
        if bid not in beats:
            print("no beat", bid)
            continue
        tmp = os.path.join(OUT, name + ".png")
        subprocess.run([build.FF, "-y", "-loglevel", "error", "-ss", "%.2f" % (beats[bid]["start"] + at), "-i", pic, "-frames:v", "1", tmp], check=True)
        Image.open(tmp).convert("RGB").resize((1280, 720), Image.LANCZOS).save(os.path.join(OUT, name + ".jpg"), quality=84, optimize=True)
        os.remove(tmp)
    print("wrote", len(os.listdir(OUT)), "pictures to docs/img,", sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT)) // 1024, "KiB")


if __name__ == "__main__":
    main()
