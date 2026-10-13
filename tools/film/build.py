"""Builds the full-length ThermalSense film from tools/film/acts/*.py.

    voice     narration of every beat (neural voice; cached per text) with the time of every word, and the beat lengths
    plan      print the timeline (chapters, beats, lengths) and write docs/FILM_SCRIPT.md, the subtitles and the chapter list
    takes     write the recorder scripts of every take and record the ones that changed (EPIC 1080p, RTX 4070)
    plates    draw the explanatory plates
    picture   cut every beat to its length with titles and key badges, join them
    sound     program sound + narration + score, mixed and measured
    final     mux, encode the copies
    qc        scan the master (or --takes) for shake, frozen stretches and hard jumps
    all       everything in order

Run: ~/.local/bin/uv run -q --python 3.12 --with edge-tts --with kokoro-onnx --with soundfile --with numpy --with scipy
     --with pillow --with matplotlib --with openpyxl python tools/film/build.py <step> [--only chapter,chapter]
     [--engine edge|kokoro] [--voice NAME]"""
import glob
import hashlib
import importlib
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dsl  # noqa: E402

ROOT, OUT = dsl.ROOT, dsl.OUT
FF = os.path.expanduser("~/.local/bin/ffmpeg")
FP = os.path.expanduser("~/.local/bin/ffprobe")
APP = os.path.join(ROOT, "build", "ts-film5")
W, H, FPS = 1920, 1080, 30
LEAD, TAIL = 0.35, 0.55          # silence before and after the narration of a beat (s)
ENGINE = "edge"                  # edge: Microsoft's online read-aloud voices (edge-tts); kokoro: the local model in build/tts
VOICES = {"edge": "en-US-AndrewMultilingualNeural", "kokoro": "am_michael"}
VOICE = None
RATE = "+2%"                     # a presenter's pace
SAY = {"KUET": "Kuet", "2K21": "two K twenty-one", "2107104": "two one zero seven, one zero four", "CSE": "C S E",
       "GGX": "G G X", "BVH": "B V H", "NDC": "N D C", "TAA": "T A A", "SSR": "S S R", "HUD": "hud", "A\\*": "A star",
       "PCSS": "P C S S", "GTAO": "G T A O", "HDR": "H D R", "CV": "C V", "IR": "I R", "POV": "P O V", "PD": "P D",
       "LFR": "L F R", "SAM": "sam", "MLRS": "M L R S", "AA": "A A", "FPV": "F P V", "NUC": "nuck", "AGC": "A G C"}
BANNED = ("generated", "by code", "compared:", "chosen:", "decision", "no model files", "question one", "gibberish")


def sh(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        raise RuntimeError("FAILED: %s\n... %s" % (r.stderr[-900:], " ".join(map(str, cmd))[:400]))   # not SystemExit: a pool worker must report it
    return r


def load(only=None):
    for f in sorted(glob.glob(os.path.join(HERE, "acts", "a*.py"))):
        importlib.import_module("acts." + os.path.basename(f)[:-3])
    if only:
        keep = set(only.split(","))
        dsl.CHAPTERS[:] = [c for c in dsl.CHAPTERS if c["id"] in keep]
    return dsl.CHAPTERS


def duration(path):
    return float(sh([FP, "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path]).stdout.strip())


# ------------------------------------------------------------------------------------------------------------ voice
def spoken(text):
    for k, v in SAY.items():
        text = re.sub(r"\b%s(?!\w)" % k, v, text)
    return text


def voice_name():
    return VOICE or VOICES[ENGINE]


def voice_path(b):
    key = voice_name() + "|" + spoken(b["text"]) if ENGINE == "kokoro" else "|".join((ENGINE, voice_name(), RATE, spoken(b["text"])))
    return os.path.join(OUT, "voice", "%s.wav" % hashlib.sha1(key.encode()).hexdigest()[:12])


def _edge(todo):
    """Microsoft's neural voice: one request per beat, four at a time; the service also reports when each word starts."""
    import asyncio
    import edge_tts

    async def one(b, sem, done):
        text, wav = spoken(b["text"]), voice_path(b)
        async with sem:
            for attempt in range(5):
                try:
                    words, mp3 = [], wav[:-4] + ".mp3"
                    with open(mp3, "wb") as f:
                        # word times; if the service keeps refusing them for a line, sentence times will do
                        kind = "WordBoundary" if attempt < 3 else "SentenceBoundary"
                        async for ch in edge_tts.Communicate(text, voice_name(), rate=RATE, boundary=kind).stream():
                            if ch["type"] == "audio":
                                f.write(ch["data"])
                            elif ch["type"] == kind:
                                words.append((ch["text"], ch["offset"] / 1e7))
                    if os.path.getsize(mp3) < 2000 or not words:
                        raise RuntimeError("empty audio")
                    break
                except Exception as e:  # noqa: BLE001  (the service drops a connection now and then)
                    if attempt == 4:
                        raise RuntimeError("voice failed for %s: %s" % (b["id"], e))
                    await asyncio.sleep(2.0 + 3.0 * attempt)
        sh([FF, "-y", "-loglevel", "error", "-i", mp3, "-ar", "48000", "-ac", "1", wav + ".tmp.wav"])
        os.replace(wav + ".tmp.wav", wav)
        os.remove(mp3)
        pos, marks = 0, []                    # where each spoken word starts in the text
        for w, t in words:
            i = text.find(w, pos)
            if i >= 0:
                marks.append((i, round(t, 3)))
                pos = i + len(w)
        json.dump({"text": text, "marks": marks}, open(wav[:-4] + ".json", "w"))
        done.append(b["id"])
        print("voice %d/%d %s %.1f s" % (len(done), len(todo), b["id"], duration(wav)), flush=True)

    async def run():
        sem, done = asyncio.Semaphore(4), []
        await asyncio.gather(*(one(b, sem, done) for b in todo))
    asyncio.run(run())


def step_voice():
    os.makedirs(os.path.join(OUT, "voice"), exist_ok=True)
    todo, seen = [], set()
    for b in dsl.beats():
        if b["text"] and not os.path.exists(voice_path(b)) and voice_path(b) not in seen:
            seen.add(voice_path(b))
            todo.append(b)
    if todo and ENGINE == "edge":
        _edge(todo)
    elif todo:
        import soundfile as sf
        from kokoro_onnx import Kokoro
        tts = os.path.join(ROOT, "build", "tts")
        k = Kokoro(os.path.join(tts, "kokoro-v1.0.onnx"), os.path.join(tts, "voices-v1.0.bin"))
        for i, b in enumerate(todo):
            s, sr = k.create(spoken(b["text"]), voice=voice_name(), speed=0.97, lang="en-gb" if voice_name()[0] == "b" else "en-us")
            sf.write(voice_path(b), s, sr)
            print("voice %d/%d %s %.1f s" % (i + 1, len(todo), b["id"], len(s) / sr), flush=True)
    timing()


def timing():
    """Length and start of every beat (frames), from the narration."""
    t = 0
    for b in dsl.beats():
        v = duration(voice_path(b)) if b["text"] and os.path.exists(voice_path(b)) else 0.0
        b["voice"] = v
        need = max(b.get("min", 0.0), v + LEAD + TAIL if v else 0.0, 3.0 if b["kind"] == "card" else 1.5)
        b["frames"] = int(round(need * FPS))
        b["dur"] = b["frames"] / FPS
        b["start"] = t / FPS
        t += b["frames"]
    return t / FPS


# ------------------------------------------------------------------------------------------------------------- plan
def clock(t):
    return "%d:%02d" % (int(t) // 60, int(t) % 60)


def sentences(text):
    return [s.strip() for s in re.split(r"(?<=[.?!:])\s+", text) if s.strip()]


def sentence_times(b):
    """[(start, end, sentence)] inside the beat: from the times the voice reports for its words, else by share of characters."""
    ss = sentences(b["text"])
    side = voice_path(b)[:-4] + ".json" if b["text"] else ""
    if side and os.path.exists(side):
        q = json.load(open(side))
        sp = sentences(q["text"])
        if len(sp) == len(ss) and q["marks"]:
            starts, pos = [], 0
            for s in sp:
                i = q["text"].find(s, pos)
                later = [t for c, t in q["marks"] if c >= i]
                starts.append(LEAD + (later[0] if later else b["voice"]))
                pos = i + len(s)
            starts[0] = LEAD
            return [(a, (starts[k + 1] if k + 1 < len(ss) else LEAD + b["voice"]), s) for k, (a, s) in enumerate(zip(starts, ss))]
    total = float(sum(len(s) for s in ss)) or 1.0
    out, t = [], LEAD
    for s in ss:
        d = b["voice"] * len(s) / total
        out.append((t, t + d, s))
        t += d
    return out


def srt_time(t):
    ms = int(round(t * 1000))
    return "%02d:%02d:%02d,%03d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000)


def step_plan():
    total = timing()
    lines = ["# ThermalSense: the film script", "",
             "Narration and shot list of the full-length film, written out from `tools/film/acts/*.py` by `tools/film/build.py plan`.",
             "Length %s. Times are minutes:seconds from the start." % clock(total), ""]
    srt, chapters, n, act = [], [], 1, None
    for ch in dsl.CHAPTERS:
        if not ch["beats"]:
            continue
        if ch["act"] != act and ch["act"]:
            act = ch["act"]
            lines += ["## %s" % act, ""]
        t0 = ch["beats"][0]["start"]
        chapters.append("%s %s" % (clock(t0), ch["title"]))
        lines += ["### %s  %s" % (clock(t0), ch["title"]), "", "| Time | Picture | Narration |", "|---|---|---|"]
        for b in ch["beats"]:
            pic = {"live": "program: take `%s`" % b.get("take"), "clip": "program: `%s`" % b.get("src"), "plate": "plate `%s`" % b.get("name"),
                   "still": "picture", "card": "card: %s" % b.get("heading"), "split": "split screen"}[b["kind"]]
            if b.get("title"):
                pic += "<br>*%s*" % b["title"]
            keys = [s[2] for s in b.get("steps", ()) if len(s) > 2]
            if keys:
                pic += "<br>keys: " + " ".join("`%s`" % k for k in keys)
            lines.append("| %s | %s | %s |" % (clock(b["start"]), pic, b["text"]))
            for a, e, s in sentence_times(b):
                srt.append("%d\n%s --> %s\n%s\n" % (n, srt_time(b["start"] + a), srt_time(b["start"] + e), s))
                n += 1
        lines.append("")
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(ROOT, "docs", "FILM_SCRIPT.md"), "w").write("\n".join(lines))
    # for the upload: subtitles, and the chapter list to paste into the video's description
    open(os.path.join(ROOT, "docs", "FILM_SUBTITLES.srt"), "w").write("\n".join(srt))
    open(os.path.join(ROOT, "docs", "FILM_CHAPTERS.txt"), "w").write("\n".join(chapters) + "\n")
    title = {c["id"]: (c["act"], c["title"]) for c in dsl.CHAPTERS}
    film = {"length": round(total, 2), "takes": len(dsl.TAKES), "beats": [
        {"start": round(b["start"], 2), "length": round(b["dur"], 2), "act": title[b["chapter"]][0], "chapter": title[b["chapter"]][1], "kind": b["kind"],
         "source": str(b.get("take") or b.get("src") or b.get("name") or b.get("heading") or ("pictures" if b["kind"] == "still" else "split screen")),
         "title": b.get("title", "")} for b in dsl.beats()]}
    json.dump(film, open(os.path.join(ROOT, "report", "data", "film.json"), "w"), indent=0)
    words = sum(len(b["text"].split()) for b in dsl.beats())
    print("\n".join(chapters))
    for b in dsl.beats():                      # wording the presenter does not want, and fragments that sound like a list
        low = b["text"].lower()
        for w in BANNED:
            if w in low:
                print("LINT %s: \"%s\" in the narration" % (b["id"], w))
        short = [s for s in sentences(b["text"]) if len(s.split()) < 3]
        if len(short) > 1:
            print("LINT %s: fragments %s" % (b["id"], short))
        if b["voice"] > 5 and not 120 <= len(b["text"].split()) / b["voice"] * 60 <= 225:
            print("LINT %s: %.0f words a minute" % (b["id"], len(b["text"].split()) / b["voice"] * 60))
    print("total %s, %d beats, %d words, %d takes" % (clock(total), sum(1 for _ in dsl.beats()), words, len(dsl.TAKES)))


# ------------------------------------------------------------------------------------------------------------ takes
def word_time(b, word):
    """When the narration of a beat reaches `word` (s from the beat's start); None without the voice's word times."""
    side = voice_path(b)[:-4] + ".json" if b["text"] else ""
    if not side or not os.path.exists(side):
        return None
    q = json.load(open(side))
    i = q["text"].lower().find(spoken(word).lower())
    later = [t for c, t in q["marks"] if c >= i]
    return LEAD + later[0] if i >= 0 and later else None


def offset(v, dur, b=None):
    """Step time inside a beat: seconds, "40%", "100%-0.5", "s2" (start of sentence 2) or "@word" (when that word is
    spoken), the last two with an optional "+0.4" / "-0.4"."""
    if isinstance(v, (int, float)):
        return float(v)
    m = re.match(r"^s(\d+)([+-][\d.]+)?$", v)
    if m:
        st = sentence_times(b)
        return max(0.0, st[min(int(m.group(1)), len(st) - 1)][0] + float(m.group(2) or 0))
    m = re.match(r"^@(.+?)([+-][\d.]+)?$", v)
    if m:
        t = word_time(b, m.group(1))
        if t is None:
            raise RuntimeError("%s: \"%s\" is not in the narration (or the voice has not been made)" % (b["id"], m.group(1)))
        return max(0.0, t + float(m.group(2) or 0))
    m = re.match(r"^([\d.]+)%([+-][\d.]+)?$", v)
    return float(m.group(1)) / 100.0 * dur + float(m.group(2) or 0)


def take_script(tid):
    """(script text, seconds, [(time, key)]) of a scripted take; sets every beat's `at`."""
    tk = dsl.TAKES[tid]
    t, lines, keys = tk["pre"], [], []
    for b in tk["beats"]:
        b["at"] = t
        for st in b["steps"]:
            ts = t + offset(st[0], b["dur"], b)
            lines.append("%.3f %s" % (ts, st[1]))
            if len(st) > 2:
                keys.append((ts, st[2]))
                b.setdefault("keys", []).append((ts - t, st[2]))
        t += b["dur"]
    return "\n".join(lines) + "\n", t + 0.6, keys


def take_cmd(tid):
    tk = dsl.TAKES[tid]
    d = os.path.join(OUT, "takes")
    env = {"TS_DIRECTOR_CUT": "v6"}
    cam = tk["cam"].split()
    if tk["kind"] == "script":
        text, seconds, _ = take_script(tid)
        open(os.path.join(d, tid + ".txt"), "w").write(text)
        env["TS_FILM_SCRIPT"] = os.path.join(d, tid + ".txt")
        cam = cam[:6] + cam[:6] + cam[6:7]
    else:
        seconds = tk["seconds"]
    if cam:
        env["TS_FILM_CAM"] = "%s %s" % (tk["hour"], " ".join(cam))
    for k, v in tk["env"].items():
        if k == "algo":
            env["TS_FILM_ALGO"] = os.path.join(d, tid + "_algo.jsonl")
            if v is not True:
                env["TS_FILM_ALGO_ROVER"] = str(v)
        elif k == "events":
            env["TS_FILM_EVENTS"] = os.path.join(d, tid + "_events.txt")
        elif k == "tour":
            env["TS_TOUR"] = os.path.join(HERE, "tours", v + ".txt")
        else:
            env[k] = str(v)
    args = [APP, "--headless", "--quality", "epic", "--seed", str(tk["env"].get("seed", 1)), "--size", "%dx%d" % (W, H),
            "--record", os.path.join(d, tid + ".mp4"), "%.2f" % seconds]
    env.pop("seed", None)
    return env, args, seconds


def step_takes(lanes=2):
    timing()
    d = os.path.join(OUT, "takes")
    os.makedirs(d, exist_ok=True)
    jobs = []
    for tid in dsl.TAKES:
        env, args, seconds = take_cmd(tid)
        sig = hashlib.sha1(json.dumps([env, args[4:], open(env["TS_FILM_SCRIPT"]).read() if "TS_FILM_SCRIPT" in env else "",
                                       open(env["TS_TOUR"]).read() if "TS_TOUR" in env else ""], sort_keys=True).encode()).hexdigest()
        stamp = os.path.join(d, tid + ".sig")
        if os.path.exists(os.path.join(d, tid + ".mp4")) and os.path.exists(stamp) and open(stamp).read() == sig:
            continue
        jobs.append((tid, env, args, sig, stamp, seconds))
    print("%d takes to record (%.0f s of footage), %d already there" % (len(jobs), sum(j[5] for j in jobs), len(dsl.TAKES) - len(jobs)), flush=True)
    running = []

    def reap(block):
        for item in list(running):
            p, tid, sig, stamp, log = item
            if block:
                p.wait()
            if p.poll() is None:
                continue
            running.remove(item)
            log.close()
            text = open(os.path.join(d, tid + ".log")).read()
            ok = p.returncode == 0 and "RTX 4070" in text and "EPIC" in text
            failed = [ln for ln in open(os.path.join(d, tid + "_steps.txt")).read().splitlines() if " FAILED " in ln] if os.path.exists(os.path.join(d, tid + "_steps.txt")) else []
            print("take %-14s %s%s" % (tid, "ok" if ok else "FAILED (see %s.log)" % tid, "".join("\n    step failed: " + f for f in failed)), flush=True)
            if ok:
                open(stamp, "w").write(sig)
            block = False

    for tid, env, args, sig, stamp, seconds in jobs:
        while len(running) >= lanes:
            reap(True)
        log = open(os.path.join(d, tid + ".log"), "w")
        running.append((subprocess.Popen(args, cwd=ROOT, env=dict(os.environ, **env), stdout=log, stderr=subprocess.STDOUT), tid, sig, stamp, log))
    while running:
        reap(True)


def step_events():
    """Times of the deterministic night (single strike, combined strike, a rover hiding) -> takes/night_times.json."""
    d = os.path.join(OUT, "takes")
    single = combined = None
    for ln in open(os.path.join(d, "n_wide_events.txt")):
        p = ln.split()
        if len(p) > 1 and p[1] == "RoundLaunched" and single is None:
            single = float(p[0])
        if len(p) > 1 and p[1] == "CombinedStrike" and combined is None:
            combined = float(p[0])
    hide, run, mine = None, {}, None
    for ln in open(os.path.join(d, "n_wide_algo.jsonl")):
        q = json.loads(ln)
        if mine is None and "grid" in q:
            mine = q["grid"]["i"]            # the rover whose planner is recorded
        for r in q["rovers"]:
            if r["i"] != mine:
                continue
            if r["ev"]:
                run.setdefault(r["i"], q["t"])
                if hide is None and q["t"] - run[r["i"]] >= 1.5 and q["t"] > 6.0:
                    hide = run[r["i"]]
            else:
                run.pop(r["i"], None)
    out = {"single": single or 7.75, "combined": combined or 18.0, "hide": hide or 12.0}
    json.dump(out, open(os.path.join(d, "night_times.json"), "w"))
    print("night times:", out)


if __name__ == "__main__":
    step = sys.argv[1] if len(sys.argv) > 1 else "plan"
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    if "--engine" in sys.argv:
        ENGINE = sys.argv[sys.argv.index("--engine") + 1]
    if "--voice" in sys.argv:
        VOICE = sys.argv[sys.argv.index("--voice") + 1]
    load(only)
    if step in ("voice", "all"):
        step_voice()
    if step in ("plan", "all"):
        if step == "plan":
            timing()
        step_plan()
    if step == "events":
        step_events()
    if step in ("takes", "all"):
        step_takes(int(sys.argv[sys.argv.index("--lanes") + 1]) if "--lanes" in sys.argv else 2)
    if step == "qc":
        import qc
        qc.run(sys.modules[__name__])
    if step in ("plates", "picture", "sound", "final", "all"):
        import assemble
        assemble.run(step, sys.modules[__name__])
