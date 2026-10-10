"""The 2-minute presentation film: minute 1 = the graphics pipeline, the soldiers and the interiors; minute 2 = the
intruder rovers, real-time tracking, the squad's strike, the explosion and the soldiers' free fire.

Every picture is the program's own: the v7 showcase recording (report/video/thermalsense_showcase.mp4, ts --record), the
v6 cut and the TS_FILM_CAM glide shots (build/film/*.mp4), report captures and plots, and the animated route map
(tools/report/gen_rover_anim.py). Sound is the program's offline-rendered audio; stills carry its ambience recording.
Output: 1280x720, 30 fps, cinema letterbox with a section title (top bar) and a caption (bottom bar), H.264 2-pass sized
under 19 MB (the slide's video limit is 20 MB) -> report/video/thermalsense_film_2min.mp4 and a poster frame.
Run: ~/.local/bin/uv run -q python tools/report/gen_film.py"""
import os
import shutil
import subprocess
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FF = os.path.expanduser("~/.local/bin/ffmpeg")
SC = os.path.join(ROOT, "report", "video", "thermalsense_showcase.mp4")      # v7 cut, 1920x1080 60 fps, letterboxed
V6 = os.path.join(ROOT, "build", "film", "v6_cut.mp4")                          # v6 cut, 1280x720 30 fps
FILM = os.path.join(ROOT, "build", "film")
IMG = os.path.join(ROOT, "report", "figures", "img")
AMB = os.path.join(ROOT, "build", "audio_v7", "v7_ambience_dawn_noon_night_bunker.wav")
OUT = os.path.join(ROOT, "report", "video", "thermalsense_film_2min.mp4")
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONTB = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
W, H, FPS, BAR = 1280, 720, 30, 88
STYLE = "bars"   # "bars" (letterbox titles) or "lower" (lower third)
TMP = tempfile.mkdtemp(prefix="film_")

# (kind, source, start, duration, section title, caption, extra)
#   kind: clip (video file, own audio) | still (image, Ken Burns) | flip (images in turn) | title (card)
SEG = [
    ("title", os.path.join(IMG, "showcase", "sc_overview_dusk.jpg"), 0, 4.6, "", "ThermalSense", "A graphics pipeline, end to end, in a military outpost"),
    ("still", os.path.join(IMG, "primcards", "camp_prims.jpg"), 0, 2.6, "1  PRIMITIVES", "16 primitive generators build all 5,476 parts", {}),
    ("flip", [os.path.join(IMG, "shapes", "autonomous_uav_stage%d.jpg" % p) for p in (12, 35, 65, 100)], 0, 2.6, "2  MODELLING",
     "Scene graph: every part placed by M = T R S, a UAV built up in hierarchy order", {}),
    ("flip", [os.path.join(IMG, "scene", "v10_proj_persp.jpg"), os.path.join(IMG, "scene", "v10_proj_iso.jpg")], 0, 3.6, "3  VIEWING AND PROJECTION",
     "Camera basis u, v, n; perspective, then isometric projection", {}),
    ("clip", SC, 107.6, 5.2, "4  CLIPPING AND RASTERISATION", "Frame Explorer: one triangle clipped, divided and scanned into pixels", {}),
    ("flip", [os.path.join(IMG, "v5", "scene_hiddenline.jpg"), os.path.join(IMG, "annot", "shade_scene.jpg")], 0, 3.6, "5  HIDDEN SURFACES AND SHADING",
     "Z-buffer and back-face culling; flat, Gouraud and Phong shading by part", {}),
    ("clip", SC, 113.3, 4.6, "6  ILLUMINATION AND COLOUR", "Ambient, diffuse, specular; colour interpolated across each triangle", {}),
    ("clip", SC, 47.0, 5.6, "7  TEXTURE AND A MOVING SUN", "Procedural textures; a solar model drives sky, light and fog", {}),
    ("clip", os.path.join(FILM, "gate_sentry.mp4"), 0.8, 4.2, "8  SOLDIERS ON DUTY", "Sentries hold the gate; the garrison follows its duty roster", {}),
    ("clip", os.path.join(FILM, "tower_post.mp4"), 1.5, 2.6, "8  SOLDIERS ON DUTY", "A guard tower post, 5.4 m up", {}),
    ("clip", os.path.join(FILM, "hall_day.mp4"), 1.0, 4.4, "9  INSIDE THE BUILDINGS", "Barracks hall: off-duty soldiers rest and train", {}),
    ("clip", os.path.join(FILM, "tunnel_cic.mp4"), 1.0, 5.2, "9  INSIDE THE BUILDINGS", "Bunker tunnel into the command room", {}),
    ("clip", os.path.join(FILM, "hall_night.mp4"), 1.5, 3.2, "9  INSIDE THE BUILDINGS", "The hall at night: lit by its own tube lights", {}),
    ("clip", SC, 78.6, 1.6, "10  RAY TRACING", "Raster", {}),
    ("clip", SC, 83.1, 1.6, "10  RAY TRACING", "Hybrid: ray-traced reflections through a BVH", {}),
    ("clip", SC, 87.6, 1.6, "10  RAY TRACING", "Whitted: recursive reflection and refraction", {}),
    ("clip", SC, 92.1, 1.6, "10  RAY TRACING", "Path tracing", {}),
    ("clip", SC, 96.6, 1.6, "10  RAY TRACING", "Light tracing", {}),
    # ---- minute 2
    ("clip", os.path.join(FILM, "night_camp.mp4"), 1.0, 4.0, "NIGHT", "Intruder rovers attack; two UAVs patrol", {}),
    ("clip", os.path.join(FILM, "rover_map.mp4"), 0, 8.0, "LINE FOLLOWERS",
     "Each rover plans its own line: flood fill to cover, Dijkstra to its objective", {"audio": "amb", "amb_at": 30}),
    ("clip", SC, 33.4, 5.0, "LINE FOLLOWERS", "The rover's view: five sensors follow the planned line", {}),
    ("clip", SC, 21.2, 4.2, "REAL-TIME TRACKING", "Each UAV sees the rovers only in its own thermal pixels", {}),
    ("still", os.path.join(ROOT, "report", "figures", "plots", "p_cv_story.png"), 0, 5.0, "REAL-TIME TRACKING",
     "Threshold, despeckle, connected components: the rover becomes a target", {"amb_at": 33}),
    ("clip", V6, 26.2, 6.2, "SHARED TRACK PICTURE", "Both UAVs feed one tracker: Kalman tracks, triangulated", {}),
    ("clip", SC, 8.6, 5.0, "COMBINED STRIKE", "LEAD and WING time their launches: both rounds arrive together", {}),
    ("clip", SC, 27.0, 4.2, "COMBINED STRIKE", "Riding the round: proportional navigation", {}),
    ("clip", SC, 13.6, 3.2, "IMPACT", "", {"speed": 0.55, "zoom": True}),
    ("still", os.path.join(IMG, "game", "explosion_early.jpg"), 0, 1.8, "IMPACT", "Particles, HDR fire and bloom", {"amb_at": 36}),
    ("still", os.path.join(IMG, "game", "wreck_burning.jpg"), 0, 2.0, "IMPACT", "Fire cools along a blackbody ramp", {"amb_at": 38}),
    ("clip", V6, 75.9, 2.2, "SOLDIERS", "Soldiers move freely: a logistics run across the camp", {}),
    ("clip", SC, 39.4, 6.6, "SOLDIERS", "and fire when needed: first person, recoil, tracers", {}),
    ("title", os.path.join(IMG, "scene", "night_searchlights.jpg"), 0, 2.6, "", "ThermalSense", "Computer Graphics Laboratory, CSE, KUET"),
]


def card(i, section, caption, big=False, sub=""):
    """Letterbox bars, the section title (top bar) and the caption (bottom bar) as a transparent PNG overlay
    (this ffmpeg has no drawtext)."""
    from PIL import Image, ImageDraw, ImageFont
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    u = H / 720.0   # type scale
    if STYLE == "bars":
        d.rectangle([0, 0, W, BAR], fill=(0, 0, 0, 255))
        d.rectangle([0, H - BAR, W, H], fill=(0, 0, 0, 255))
    def centred(text, font, y, fill):
        w = d.textlength(text, font=font)
        d.text(((W - w) / 2, y), text, font=font, fill=fill)
    if big:
        d.rectangle([0, 0, W, H], fill=(10, 14, 20, 110))
        centred(caption, ImageFont.truetype(FONTB, int(72 * u)), H / 2 - 64 * u, (245, 241, 234, 255))
        centred(sub, ImageFont.truetype(FONT, int(28 * u)), H / 2 + 26 * u, (224, 160, 58, 255))
    elif STYLE == "lower":   # lower third over a soft gradient, the full frame stays visible
        if caption or section:
            for y in range(int(H * 0.72), H):
                a = int(170 * max(0.0, (y - H * 0.72) / (H * 0.28)) ** 1.2)
                d.line([(0, y), (W, y)], fill=(6, 9, 13, a))
        if section:
            d.text((70 * u, H - 92 * u), section, font=ImageFont.truetype(FONT, int(19 * u)), fill=(224, 160, 58, 255))
        if caption:
            d.text((70 * u, H - 64 * u), caption, font=ImageFont.truetype(FONT, int(27 * u)), fill=(240, 240, 240, 255))
    else:
        if section:
            d.text((40, BAR / 2 - 13), section, font=ImageFont.truetype(FONT, 22), fill=(224, 160, 58, 255))
        if caption:
            centred(caption, ImageFont.truetype(FONT, 26), H - BAR / 2 - 16, (240, 240, 240, 255))
    path = os.path.join(TMP, "card%02d.png" % i)
    im.save(path)
    return path


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(" ".join(cmd)[-1500:] + "\n" + r.stderr[-2000:])


def segment(i, s):
    kind, src, ss, dur, section, caption, extra = s
    ex = extra if isinstance(extra, dict) else {}
    out = os.path.join(TMP, "s%02d.mp4" % i)
    n = int(round(dur * FPS))
    fade = "fade=t=in:st=0:d=0.25,fade=t=out:st=%.2f:d=0.25" % (dur - 0.25)
    afade = "afade=t=in:st=0:d=0.25,afade=t=out:st=%.2f:d=0.3" % (dur - 0.3)
    fit = "scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d,fps=%d,setsar=1" % (W, H, W, H, FPS)
    big = kind == "title" or bool(ex.get("big"))
    ov = card(i, section, caption, big=big, sub=extra if isinstance(extra, str) else ex.get("sub", ""))
    inputs, chain, audio = [], "", None
    if kind == "clip":
        speed = ex.get("speed", 1.0)
        inputs += ["-ss", str(ss), "-t", "%.3f" % (dur / speed), "-i", src]
        v = ("setpts=PTS/%g," % speed if speed != 1.0 else "") + ("crop=%s," % ex["crop"] if ex.get("crop") else "") + fit
        if ex.get("zoom"):   # slow push-in for the impact
            v += (",scale=%d:%d,zoompan=z='1+0.18*on/%d':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)+20':d=1:s=%dx%d:fps=%d"
                  % (W * 2, H * 2, n, W, H, FPS))
        chain = "[0:v]%s[bg]" % v
        if ex.get("audio") != "amb":
            audio = "[0:a]%s%s,aresample=48000[a]" % ("atempo=%g," % speed if speed != 1.0 else "", afade)
    elif kind == "flip":
        per = dur / len(src)
        parts = []
        for k, img in enumerate(src):
            inputs += ["-loop", "1", "-t", "%.3f" % per, "-i", img]
            parts.append("[%d:v]scale=%d:%d:force_original_aspect_ratio=decrease,pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=0x0e141b,"
                         "fps=%d,setsar=1[f%d]" % (k, W, H - 2 * BAR, W, H, FPS, k))
        chain = ";".join(parts) + ";" + "".join("[f%d]" % k for k in range(len(src))) + "concat=n=%d:v=1:a=0[bg]" % len(src)
    else:   # still or title: a slow Ken Burns push
        inputs += ["-loop", "1", "-t", "%.3f" % dur, "-i", src]
        if src.endswith(".png") and "plots" in src:   # a plot: fit on a white card, clear of the titles
            inner = H - 2 * BAR if STYLE == "bars" else int(H * 0.76)
            top = BAR if STYLE == "bars" else int(H * 0.04)
            v = ("scale=%d:%d:force_original_aspect_ratio=decrease,pad=%d:%d:(ow-iw)/2:%d+(%d-ih)/2:color=white,"
                 "zoompan=z='1+0.04*on/%d':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':d=%d:s=%dx%d:fps=%d,setsar=1"
                 % (W * 2, inner * 2, W * 2, H * 2, top * 2, inner * 2, n, n, W, H, FPS))
        else:
            v = ("scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d,zoompan=z='1+0.10*on/%d':x='iw/2-(iw/zoom/2)':"
                 "y='ih/2-(ih/zoom/2)':d=%d:s=%dx%d:fps=%d,setsar=1" % (W * 2, H * 2, W * 2, H * 2, n, n, W, H, FPS))
        chain = "[0:v]%s[bg]" % v
    oi = len([x for x in inputs if x == "-i"])
    inputs += ["-loop", "1", "-t", "%.3f" % dur, "-i", ov]
    if audio is None:
        ai = oi + 1
        inputs += ["-ss", str(ex.get("amb_at", 0)), "-t", "%.3f" % dur, "-i", AMB]
        audio = "[%d:a]volume=0.8,%s[a]" % (ai, afade)
    graph = chain + ";[bg][%d:v]overlay=0:0:shortest=0,%s,trim=duration=%.3f[v];" % (oi, fade, dur) + audio
    run([FF, "-y", "-loglevel", "error"] + inputs + ["-filter_complex", graph, "-map", "[v]", "-map", "[a]", "-t", "%.3f" % dur,
         "-r", str(FPS), "-c:v", "libx264", "-crf", "14", "-preset", "medium", "-pix_fmt", "yuv420p",
         "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2", out])
    return out


def main():
    parts = [segment(i, s) for i, s in enumerate(SEG)]
    total = sum(s[3] for s in SEG)
    lst = os.path.join(TMP, "list.txt")
    open(lst, "w").write("".join("file '%s'\n" % p for p in parts))
    joined = os.path.join(TMP, "joined.mp4")
    run([FF, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", joined])
    # 2-pass to fit the slide's limit: audio 96 kb/s, video what remains of 18.5 MB
    vbit = int(17.0 * 8 * 1000 * 1000 / total - 96000)
    log = os.path.join(TMP, "pass")
    run([FF, "-y", "-loglevel", "error", "-i", joined, "-c:v", "libx264", "-preset", "slow", "-b:v", str(vbit), "-pass", "1",
         "-passlogfile", log, "-an", "-f", "mp4", os.devnull])
    run([FF, "-y", "-loglevel", "error", "-i", joined, "-c:v", "libx264", "-preset", "slow", "-b:v", str(vbit), "-pass", "2",
         "-passlogfile", log, "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "96k", "-af", "loudnorm=I=-16:TP=-1.5:LRA=11",
         "-movflags", "+faststart", OUT])
    run([FF, "-y", "-loglevel", "error", "-ss", "1.5", "-i", OUT, "-frames:v", "1", OUT.replace(".mp4", "_poster.jpg")])
    print("wrote", OUT, "%.1f s" % total, "%.1f MB" % (os.path.getsize(OUT) / 1e6), "video %d kb/s" % (vbit // 1000))
    shutil.rmtree(TMP, ignore_errors=True)


if __name__ == "__main__":
    main()
