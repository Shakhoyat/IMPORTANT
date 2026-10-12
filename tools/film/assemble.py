"""Plates, picture, sound and final encode of the film (called by build.py)."""
import hashlib
import math
import os
import subprocess

import numpy as np

import dsl

OUT, ROOT = dsl.OUT, dsl.ROOT
B = None          # the build module (ffmpeg paths, sizes, timing helpers)
XF = 0.35         # dissolve between the states of a plate (s)
ACTS = ["Orientation", "Geometry", "Viewing", "Light", "World", "Control", "Vision", "Agents", "Engineering"]


def _init():
    global B
    import build
    B = build


def pool(n):
    """Worker processes started fresh (spawn): a forked matplotlib / ffmpeg parent can leave a worker hanging."""
    import multiprocessing
    return multiprocessing.get_context("spawn").Pool(n, initializer=_init)


def ff(args):
    return B.sh([B.FF, "-y", "-loglevel", "error", "-threads", "6", "-filter_complex_threads", "2"] + args)


def enc(frames):
    return ["-frames:v", str(frames), "-r", str(B.FPS), "-c:v", "libx264", "-preset", "fast", "-crf", "15", "-pix_fmt", "yuv420p", "-an"]


def states_video(states, dur, frames, out):
    """[(time, png)] -> a video of `frames` frames: each state from its time on, dissolving in."""
    st = [list(s) for s in sorted(states)]
    st[0][0] = 0.0
    for k in range(1, len(st)):                      # room for every dissolve
        st[k][0] = min(max(st[k][0], st[k - 1][0] + XF + 0.25), dur - (len(st) - k) * (XF + 0.25))
    # one input (an image list with durations) instead of a chain of looped inputs: a chain of ten 1080p inputs with
    # dissolves needs gigabytes, and several at once were killed for memory
    from PIL import Image
    nx = int(round(XF * B.FPS))
    lines = []
    for k, (t, png) in enumerate(st):
        if k:
            a, b = Image.open(st[k - 1][1]).convert("RGB"), Image.open(png).convert("RGB")
            for j in range(1, nx):
                mid = png[:-4] + "_x%02d.png" % j
                Image.blend(a, b, j / float(nx)).save(mid, compress_level=1)
                lines += ["file '%s'" % mid, "duration %.5f" % (1.0 / B.FPS)]
        end = st[k + 1][0] if k + 1 < len(st) else dur + 0.3
        hold = end - t - (XF - 1.0 / B.FPS if k else 0.0)
        lines += ["file '%s'" % png, "duration %.5f" % max(hold, 1.0 / B.FPS)]
    lines.append("file '%s'" % st[-1][1])
    lst = out[:-4] + "_list.txt"
    open(lst, "w").write("\n".join(lines) + "\n")
    ff(["-f", "concat", "-safe", "0", "-i", lst, "-vf", "fps=%d,tpad=stop_mode=clone:stop_duration=40,format=yuv420p" % B.FPS] + enc(frames) + [out])
    for ln in lines:
        if "_x" in ln and ln.startswith("file"):
            try:
                os.remove(ln[6:-1])
            except OSError:
                pass


# ----------------------------------------------------------------------------------------------------------- plates
def _plate(job):
    b, sent, out_dir = job
    import importlib
    import plates
    c = plates.Ctx(b, sent, out_dir)
    fn = None
    for mod in ("plates", "plates_b", "plates_c", "plates_d"):   # plates by act
        try:
            fn = fn or getattr(importlib.import_module(mod), "p_" + b["name"], None)
        except ModuleNotFoundError:
            pass
    st = fn(c)
    states_video(st, b["dur"], b["frames"], os.path.join(out_dir, b["id"] + ".mp4"))
    return b["id"], len(st)


def step_plates():
    d = os.path.join(OUT, "plates")
    os.makedirs(d, exist_ok=True)
    import glob
    src = hashlib.sha1(b"".join(open(f, "rb").read() for f in sorted(glob.glob(os.path.join(os.path.dirname(__file__), "plates*.py"))))).hexdigest()[:10]
    jobs = []
    for b in dsl.beats():
        if b["kind"] != "plate":
            continue
        sig = "%s|%s|%d" % (src, b["text"], b["frames"])
        stamp = os.path.join(d, b["id"] + ".sig")
        if os.path.exists(os.path.join(d, b["id"] + ".mp4")) and os.path.exists(stamp) and open(stamp).read() == sig:
            continue
        jobs.append(((b, B.sentence_times(b), d), stamp, sig))
    print("%d plates to draw" % len(jobs), flush=True)
    with pool(min(8, max(1, len(jobs)))) as pl:
        for (bid, n), (_, stamp, sig) in zip(pl.imap(_plate, [j[0] for j in jobs]), jobs):
            open(stamp, "w").write(sig)
            print("plate %s: %d states" % (bid, n), flush=True)


# ---------------------------------------------------------------------------------------------------------- overlays
def _overlay(b, chapter, t_frac, path):
    """Transparent 1920x1080 PNG: chapter tag, lower-third title, progress line."""
    import plates as P
    fig, ax = P.canvas()
    fig.patch.set_alpha(0.0)
    if b["kind"] != "card":
        tag = (chapter["act"] + "   ·   " if chapter["act"] else "") + chapter["title"]
        ax.text(1872, 44, tag, fontsize=15.5, color="#ffffff", alpha=0.92, ha="right", va="center", family=P.FONT,
                bbox=dict(boxstyle="round,pad=0.5", fc="#0c121c", ec="none", alpha=0.62))
    if b["kind"] == "split" and b.get("labels"):
        k = len(b["panes"])
        cols = 2 if k in (2, 4) else k
        pw = (1920 - 12 * (cols + 1)) // cols
        for i, lab in enumerate(b["labels"][:cols]):
            ax.text(12 + i * (pw + 12) + pw / 2, 112, lab, fontsize=20, color="#ffffff", ha="center", va="center", family=P.FONT, fontweight="bold")
    if b.get("title") and b["kind"] in ("live", "clip", "still", "split"):
        from matplotlib.patches import Rectangle
        ax.add_patch(Rectangle((0, 938), 1920, 142, fc="#0c121c", ec="none", alpha=0.0))
        for i in range(24):   # soft gradient under the title
            ax.add_patch(Rectangle((0, 930 + i * 6.25), 1920, 6.4, fc="#060a10", ec="none", alpha=0.035 * i))
        ax.add_patch(Rectangle((72, 968), 7, 58, fc=P.AMB, ec="none"))
        ax.text(98, 997, b["title"], fontsize=27, color="#ffffff", ha="left", va="center", family=P.FONT, fontweight="bold")
    from matplotlib.patches import Rectangle
    ax.add_patch(Rectangle((0, 1074), 1920, 6, fc="#ffffff", ec="none", alpha=0.16))
    ax.add_patch(Rectangle((0, 1074), 1920 * t_frac, 6, fc=P.AMB, ec="none", alpha=0.95))
    fig.savefig(path, transparent=True)
    P.plt.close(fig)


def _keycap(key, path):
    import plates as P
    fig, ax = P.canvas()
    fig.patch.set_alpha(0.0)
    w = 96 + 30 * max(0, len(key) - 1)
    x, y = 1920 - 96 - w, 846
    P.box(ax, x + 5, y + 7, w, 96, fc="#05080d", ec="none", alpha=0.6)
    P.box(ax, x, y, w, 96, fc="#f3f5f8", ec="#ffffff", lw=2)
    ax.text(x + w / 2, y + 49, key, fontsize=31, color="#10161f", ha="center", va="center", family=P.FONT, fontweight="bold")
    ax.text(x - 18, y + 49, "key", fontsize=17, color="#ffffff", ha="right", va="center", family=P.FONT, alpha=0.85)
    fig.savefig(path, transparent=True)
    P.plt.close(fig)


def _card(b, path):
    import plates as P
    fig, ax = P.canvas()
    kind = b.get("card", "decision")
    if kind == "title":
        P.txt(ax, 960, 330, b["heading"], size=120, weight="bold", ha="center")
        P.line(ax, [760, 1160], [420, 420], color=P.AMB, lw=5)
        for i, ln in enumerate(b["lines"]):
            P.txt(ax, 960, 480 + i * 62, ln, size=40 if i == 0 else 32, ha="center", color=P.FG if i == 0 else P.DIM)
    elif kind == "act":
        P.txt(ax, 150, 330, b["heading"], size=92, weight="bold", color=P.AMB)
        for i, ln in enumerate(b["lines"]):
            P.txt(ax, 150, 500 + i * 84, ln, size=50)
    else:
        P.box(ax, 190, 240, 1540, 560, ec=P.AMB, lw=3)
        P.txt(ax, 260, 330, b["heading"], size=58, weight="bold", color=P.AMB)
        for i, ln in enumerate(b["lines"]):
            head, _, rest = ln.partition("   ")
            P.txt(ax, 260, 450 + i * 100, head.strip(), size=34, color=P.DIM)
            P.txt(ax, 520, 450 + i * 100, rest.strip(), size=40, weight="bold" if i == 1 else "normal")
    fig.savefig(path, facecolor=P.BG)
    P.plt.close(fig)


def _fit_image(src, path):
    """A picture on the film's background, fitted into the frame above the lower third."""
    import plates as P
    from PIL import Image
    im = Image.open(src).convert("RGB")
    fig, ax = P.canvas()
    w, h = im.size
    s = min(1760 / w, 830 / h)
    W2, H2 = w * s, h * s
    ax.imshow(np.asarray(im), extent=(960 - W2 / 2, 960 + W2 / 2, 480 + H2 / 2, 480 - H2 / 2), zorder=2, interpolation="lanczos")
    ax.set_xlim(0, 1920)
    ax.set_ylim(1080, 0)
    fig.savefig(path, facecolor=P.BG)
    P.plt.close(fig)


# ------------------------------------------------------------------------------------------------------------ picture
def _segment(job):
    b, chapter, t_frac = job
    seg = os.path.join(OUT, "seg")
    out = os.path.join(seg, b["id"] + ".mp4")
    ov = os.path.join(seg, b["id"] + "_ov.png")
    # unchanged beat (same text, timing, sources and source files): keep its segment
    srcs = [os.path.join(OUT, "takes", b["take"] + ".mp4")] if b["kind"] == "live" else [dsl.footage(b["src"])] if b["kind"] == "clip" else \
        [os.path.join(OUT, "plates", b["id"] + ".mp4")] if b["kind"] == "plate" else [dsl.footage(p[0]) for p in b.get("panes", []) if not p[0].startswith("anim:")]
    sig = hashlib.sha1(repr((sorted((k, repr(v)) for k, v in b.items() if k != "start"), chapter["title"], chapter["act"], round(t_frac, 4),
                             [(p, os.path.getmtime(p)) for p in srcs if os.path.exists(p)], "v2")).encode()).hexdigest()   # bump "v2" when the look changes
    if os.path.exists(out) and os.path.exists(out + ".sig") and open(out + ".sig").read() == sig:
        return b["id"]
    _overlay(b, chapter, t_frac, ov)
    n, dur = b["frames"], b["dur"]
    fit = "scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d,fps=%d,setsar=1" % (B.W, B.H, B.W, B.H, B.FPS)
    base = os.path.join(seg, b["id"] + "_base.mp4")
    if b["kind"] in ("live", "clip"):
        src = os.path.join(OUT, "takes", b["take"] + ".mp4") if b["kind"] == "live" else dsl.footage(b["src"])
        sp = b.get("speed", 1.0)
        # the last frame is held if the footage ends early, so a segment is never shorter than its narration
        ff(["-ss", "%.3f" % b["at"], "-t", "%.3f" % (dur * sp + 0.2), "-i", src, "-vf", ("setpts=PTS/%g," % sp if sp != 1.0 else "") + fit +
            ",tpad=stop_mode=clone:stop_duration=20"] + enc(n) + [base])
    elif b["kind"] == "plate":
        base = os.path.join(OUT, "plates", b["id"] + ".mp4")
    elif b["kind"] == "card":
        png = os.path.join(seg, b["id"] + "_card.png")
        _card(b, png)
        ff(["-loop", "1", "-framerate", str(B.FPS), "-t", "%.3f" % (dur + 0.2), "-i", png, "-vf", "fade=t=in:st=0:d=0.3,format=yuv420p"] + enc(n) + [base])
    elif b["kind"] == "still":
        imgs = b["image"] if isinstance(b["image"], list) else [b["image"]]
        st = []
        for k, im in enumerate(imgs):
            png = os.path.join(seg, "%s_im%d.png" % (b["id"], k))
            _fit_image(im, png)
            st.append((dur * k / len(imgs), png))
        states_video(st, dur, n, base)
    elif b["kind"] == "split":
        k = len(b["panes"])
        args, graph = [], ""
        cols = 2 if k in (2, 4) else k
        gap = 12
        pw = (B.W - gap * (cols + 1)) // cols
        ph = (B.H // 2 - 2 * gap) if k == 4 else min(int(pw * 9 / 16) if k == 2 else 600, 760)
        y0 = 150 if k != 4 else gap
        for i, (src, at) in enumerate(b["panes"]):
            if src.startswith("anim:"):          # an animated plate made for this beat (plates_d.a_<name>)
                import plates_d
                os.makedirs(os.path.join(OUT, "anim"), exist_ok=True)
                path = os.path.join(OUT, "anim", "%s_%s.mp4" % (b["id"], src[5:]))
                getattr(plates_d, "a_" + src[5:])(at, dur + 0.3, path)
                args += ["-i", path]
                graph += "[%d:v]scale=%d:%d:force_original_aspect_ratio=decrease,pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=0x0c121c,fps=%d,setsar=1[p%d];" % (i, pw, ph, pw, ph, B.FPS, i)
            else:
                args += ["-ss", "%.3f" % at, "-t", "%.3f" % (dur + 0.3), "-i", dsl.footage(src)]
                graph += "[%d:v]scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d,fps=%d,setsar=1[p%d];" % (i, pw, ph, pw, ph, B.FPS, i)
        lay = "|".join("%d_%d" % (gap + (i % cols) * (pw + gap), y0 + (i // cols) * (ph + gap)) for i in range(k))
        graph += "color=c=0x0c121c:s=%dx%d:r=%d[bg];" % (B.W, B.H, B.FPS)
        last = "[bg]"
        for i in range(k):
            x, y = lay.split("|")[i].split("_")
            graph += "%s[p%d]overlay=%s:%s:shortest=0[q%d];" % (last, i, x, y, i)
            last = "[q%d]" % i
        graph += "%sformat=yuv420p[v]" % last
        ff(args + ["-filter_complex", graph, "-map", "[v]"] + enc(n) + [base])
    # titles, key badges, progress line
    args = ["-i", base, "-i", ov]
    graph, last = "[0:v][1:v]overlay=0:0[o0];", "[o0]"
    for i, (t, key) in enumerate(b.get("keys", [])):
        kp = os.path.join(seg, "key_%s.png" % hashlib.sha1(key.encode()).hexdigest()[:8])
        if not os.path.exists(kp):
            _keycap(key, kp)
        args += ["-i", kp]
        graph += "%s[%d:v]overlay=0:0:enable='between(t,%.2f,%.2f)'[o%d];" % (last, i + 2, max(0.0, t - 0.15), t + 1.7, i + 1)
        last = "[o%d]" % (i + 1)
    ff(args + ["-filter_complex", graph + "%sformat=yuv420p[v]" % last, "-map", "[v]"] + enc(n) + [out])
    open(out + ".sig", "w").write(sig)
    return b["id"]


def step_picture():
    total = B.timing()
    for tid, tk in dsl.TAKES.items():
        if tk["kind"] == "script":
            B.take_script(tid)
    os.makedirs(os.path.join(OUT, "seg"), exist_ok=True)
    jobs = []
    for ch in dsl.CHAPTERS:
        for b in ch["beats"]:
            jobs.append((b, ch, (b["start"] + b["dur"] / 2) / total))
    with pool(10) as pl:
        for i, bid in enumerate(pl.imap(_segment, jobs)):
            print("segment %d/%d %s" % (i + 1, len(jobs), bid), flush=True)
    lst = os.path.join(OUT, "seg", "list.txt")
    open(lst, "w").write("".join("file '%s'\n" % os.path.join(OUT, "seg", j[0]["id"] + ".mp4") for j in jobs))
    ff(["-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", os.path.join(OUT, "picture.mp4")])
    print("picture %.1f s" % B.duration(os.path.join(OUT, "picture.mp4")))


# -------------------------------------------------------------------------------------------------------------- sound
SR = 48000


def _read(path, start=0.0, dur=None):
    """Audio of a file as float32 stereo at 48 kHz (ffmpeg does the decoding and resampling)."""
    cmd = [B.FF, "-v", "error"] + (["-ss", "%.3f" % start] if start else []) + (["-t", "%.3f" % dur] if dur else []) + \
          ["-i", path, "-vn", "-f", "f32le", "-ac", "2", "-ar", str(SR), "-"]
    raw = subprocess.run(cmd, capture_output=True).stdout
    return np.frombuffer(raw, np.float32).reshape(-1, 2).copy()


def _write(path, x):
    import soundfile as sf
    sf.write(path, np.clip(x, -1.0, 1.0), SR, subtype="PCM_24")


def _db(x):
    r = float(np.sqrt(np.mean(np.square(x)))) if len(x) else 0.0
    return 20.0 * math.log10(max(r, 1e-7))


def step_sound():
    total = B.timing()
    for tid, tk in dsl.TAKES.items():
        if tk["kind"] == "script":
            B.take_script(tid)
    N = int(round(total * SR))
    prog, voice = np.zeros((N, 2), np.float32), np.zeros((N, 2), np.float32)
    edge = int(0.12 * SR)
    ramp = np.linspace(0.0, 1.0, edge, dtype=np.float32)[:, None]
    gains = {}
    # --voice-track FILE: a narration recorded by a person against the film's timing (docs/FILM_SCRIPT.md, docs/FILM_SUBTITLES.srt)
    # replaces the synthetic voice
    import sys
    own_voice = sys.argv[sys.argv.index("--voice-track") + 1] if "--voice-track" in sys.argv else None
    if own_voice:
        v = _read(own_voice)[:N]
        voice[: len(v)] += v
    for b in dsl.beats():
        i0 = int(round(b["start"] * SR))
        if b["kind"] in ("live", "clip"):
            src = os.path.join(OUT, "takes", b["take"] + ".mp4") if b["kind"] == "live" else dsl.footage(b["src"])
            x = _read(src, b["at"], b["dur"] * b.get("speed", 1.0) + 0.05)[: int(b["dur"] * SR)]
            if len(x) > 2 * edge:
                # one level per source: quiet scenes stay in the background, action is not flattened
                key = src
                if key not in gains:
                    target = b.get("level", -31.0 if b["kind"] == "live" else -25.0)
                    gains[key] = float(np.clip(target - _db(x), -14.0, 12.0))
                x = x * (10.0 ** (gains[key] / 20.0))
                x[:edge] *= ramp
                x[-edge:] *= ramp[::-1]
                prog[i0:i0 + len(x)] += x[: max(0, N - i0)]
        for when, wav, g in b.get("sounds", ()):       # sounds a plate talks about ("s2" = at the start of sentence 2)
            st = B.sentence_times(b)
            t0 = st[min(int(when[1:]), len(st) - 1)][0] if isinstance(when, str) else float(when)
            y = _read(wav)
            y = y[: int(min(len(y) / SR, 4.5) * SR)]
            y[-edge:] *= ramp[::-1]
            y *= g * 10.0 ** (-22.0 / 20.0) / max(10.0 ** (_db(y) / 20.0), 1e-4)
            j = i0 + int(t0 * SR)
            prog[j:j + len(y)] += y[: max(0, N - j)]
        if b["text"] and not own_voice:
            v = _read(B.voice_path(b))
            j = i0 + int(B.LEAD * SR)
            voice[j:j + len(v)] += v[: max(0, N - j)]
    snd = os.path.join(OUT, "sound")
    os.makedirs(snd, exist_ok=True)
    _write(os.path.join(snd, "program_raw.wav"), prog)
    _write(os.path.join(snd, "voice_raw.wav"), voice)
    # stems: the program sound loses its rumble and gains presence; the voice is cleaned, levelled and brought to -17 LUFS
    ff(["-i", os.path.join(snd, "program_raw.wav"), "-af",
        "highpass=f=38,lowshelf=f=130:g=-5,equalizer=f=320:t=q:w=1.0:g=-2,highshelf=f=3500:g=5,acompressor=threshold=-24dB:ratio=2.5:attack=15:release=250",
        "-ar", str(SR), os.path.join(snd, "program.wav")])
    ff(["-i", os.path.join(snd, "voice_raw.wav"), "-af",
        "highpass=f=75,equalizer=f=220:t=q:w=1.2:g=-2,equalizer=f=3200:t=q:w=1.0:g=2.5,acompressor=threshold=-22dB:ratio=3:attack=8:release=160:makeup=2,"
        "loudnorm=I=-17:TP=-2:LRA=7:linear=true", "-ar", str(SR), os.path.join(snd, "voice.wav")])
    score = os.path.join(snd, "score.wav")
    import score as score_mod
    score_mod.render(dsl, total, score, B)
    # mix: the voice leads; score and program sound duck under it; one fixed gain to -14 LUFS; limiter under -1.2 dBFS
    graph = ("[0:a]asplit=3[v][vk1][vk2];"
             "[1:a][vk1]sidechaincompress=threshold=0.02:ratio=5:attack=25:release=420:makeup=1[p];"
             "[2:a][vk2]sidechaincompress=threshold=0.015:ratio=8:attack=25:release=520:makeup=1[s];"
             "[v][p][s]amix=inputs=3:normalize=0:weights='1 0.9 0.8',alimiter=limit=0.84:level=false[a]")
    mix = os.path.join(snd, "mix_pre.wav")
    ff(["-i", os.path.join(snd, "voice.wav"), "-i", os.path.join(snd, "program.wav"), "-i", score, "-filter_complex", graph, "-map", "[a]", "-ar", str(SR), mix])
    I = loudness(mix)["I"]
    gain = float(np.clip(-14.0 - I, -10.0, 10.0))
    final = os.path.join(OUT, "mix.wav")
    ff(["-i", mix, "-af", "volume=%.2fdB,alimiter=limit=0.80:level=false" % gain, "-ar", str(SR), "-c:a", "pcm_s24le", final])
    rep = loudness(final)
    lines = ["mix: integrated %.1f LUFS, range %.1f LU, true peak %.1f dBFS (gain %+.1f dB)" % (rep["I"], rep["LRA"], rep["TP"], gain), "", "per chapter (LUFS):"]
    x = _read(final)
    for ch in dsl.CHAPTERS:
        if not ch["beats"]:
            continue
        a, e = ch["beats"][0]["start"], ch["beats"][-1]["start"] + ch["beats"][-1]["dur"]
        part = os.path.join(snd, "_part.wav")
        _write(part, x[int(a * SR):int(e * SR)])
        lines.append("  %-44s %6.1f" % (ch["title"][:44], loudness(part)["I"]))
    lines += ["", "bands of the mix (mean dB): " + "  ".join("%s %.1f" % (n, band(final, f)) for n, f in
              (("<150 Hz", "lowpass=f=150"), ("150-600", "highpass=f=150,lowpass=f=600"), ("600-2.5k", "highpass=f=600,lowpass=f=2500"),
               ("2.5-8k", "highpass=f=2500,lowpass=f=8000"), (">8k", "highpass=f=8000")))]
    open(os.path.join(OUT, "sound_report.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


def loudness(path):
    import re
    r = subprocess.run([B.FF, "-hide_banner", "-nostats", "-i", path, "-af", "ebur128=peak=true", "-f", "null", "-"], capture_output=True, text=True).stderr
    tail = r[r.rfind("Summary:"):]
    g = lambda pat: float(re.search(pat, tail).group(1))  # noqa: E731
    return {"I": g(r"I:\s+([-\d.]+) LUFS"), "LRA": g(r"LRA:\s+([-\d.]+) LU"), "TP": g(r"Peak:\s+([-\d.]+) dBFS")}


def band(path, flt):
    import re
    r = subprocess.run([B.FF, "-hide_banner", "-nostats", "-i", path, "-af", flt + ",volumedetect", "-f", "null", "-"], capture_output=True, text=True).stderr
    return float(re.search(r"mean_volume: ([-\d.]+) dB", r).group(1))


def step_final():
    pic, mix = os.path.join(OUT, "picture.mp4"), os.path.join(OUT, "mix.wav")
    vid = os.path.join(ROOT, "report", "video")
    master = os.path.join(vid, "thermalsense_full_1080p.mp4")
    import sys
    if "--parts-only" not in sys.argv or not os.path.exists(master):     # --parts-only: keep the master, cut the parts again
        ff(["-i", pic, "-i", mix, "-map", "0:v", "-map", "1:a", "-c:v", "libx264", "-preset", "slow", "-crf", "17", "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-b:a", "256k", "-shortest", "-movflags", "+faststart", master])
        ff(["-ss", "6", "-i", master, "-frames:v", "1", master.replace(".mp4", "_poster.jpg")])
    # copies for the repository: four parts at 1280 x 720, each under GitHub's 100 MB file limit
    total = B.timing()
    acts = {}
    for ch in dsl.CHAPTERS:
        if ch["beats"] and ch["act"] not in acts:
            acts[ch["act"]] = ch["beats"][0]["start"]
    cuts = [0.0, acts.get("Light", total / 4), acts.get("Control", total / 2), acts.get("Agents", 3 * total / 4), total]
    log = os.path.join(OUT, "pass")
    for k in range(4):
        a, e = cuts[k], cuts[k + 1]
        dst = os.path.join(vid, "thermalsense_full_part%d.mp4" % (k + 1))
        # the parts carry the master's audio unchanged: a second encode at 128 kb/s overshot the limiter by 2 dB on a low hit
        vb = str(int(92.0 * 8e6 / (e - a) - 256000))
        for n in ("1", "2"):
            ff(["-ss", "%.3f" % a, "-t", "%.3f" % (e - a), "-i", master, "-vf", "scale=1280:720", "-c:v", "libx264", "-preset", "medium", "-b:v", vb,
                "-maxrate", str(int(int(vb) * 1.6)), "-bufsize", str(int(vb) * 3), "-pass", n, "-passlogfile", log] +
               (["-an", "-f", "mp4", os.devnull] if n == "1" else ["-pix_fmt", "yuv420p", "-c:a", "copy", "-movflags", "+faststart", dst]))
        print("part %d  %s - %s  %.0f MB" % (k + 1, B.clock(a), B.clock(e), os.path.getsize(dst) / 1e6), flush=True)
    print("wrote %s  %.1f s  %.0f MB" % (master, B.duration(master), os.path.getsize(master) / 1e6))


def run(step, build):
    global B
    B = build
    B.timing()
    if step in ("plates", "all"):
        step_plates()
    if step in ("picture", "all"):
        step_picture()
    if step in ("sound", "all"):
        step_sound()
    if step in ("final", "all"):
        step_final()
