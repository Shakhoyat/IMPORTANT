"""The film as data: chapters made of beats. A beat is one stretch of narration with the picture that goes with it.

    chapter(id, title)                      a chapter (lower-third heading, YouTube chapter)
    take(id, hour, cam, **env)              a scripted take: the following live() beats are filmed in one run of the
                                            recorder (TS_FILM_CAM + TS_FILM_SCRIPT), their lengths set by the narration
    live(text, steps, ...)                  a beat of the open take; steps = [(seconds from the beat's start, "step")]
    free(id, seconds, hour, cam, **env)     a free-running take (the simulation plays; cut by clip())
    clip(text, src, at, ...)                an excerpt of a free take or of footage on disk
    plate(text, name, ...)                  an explanatory plate drawn by plates.py (function p_<name>)
    still(text, image, ...)                 a picture with a slow push
    card(text, heading, lines, ...)         a text card (act opening, decision)
    split(text, panes, ...)                 two to four excerpts side by side, in step

Every beat lasts as long as its narration (plus a breath), or `min` seconds if that is longer. tools/film/build.py turns
the list into voice, takes, plates, picture and sound."""
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "build", "film5")
F3 = os.path.join(ROOT, "build", "film3")
F2 = os.path.join(ROOT, "build", "film2")
IMG = os.path.join(ROOT, "report", "figures", "img")

CHAPTERS = []   # [{"id", "title", "act", "beats": [...]}]
TAKES = {}      # id -> {"kind": "script" | "free", "hour", "cam", "env", "seconds", "pre"}
_state = {"act": "", "take": None}


def act(name):
    _state["act"] = name


def chapter(cid, title):
    CHAPTERS.append({"id": cid, "title": title, "act": _state["act"], "beats": []})
    _state["take"] = None


def _add(kind, text, **kw):
    ch = CHAPTERS[-1]
    b = {"kind": kind, "text": " ".join(text.split()), "id": "%s_%02d" % (ch["id"], len(ch["beats"])), "chapter": ch["id"]}
    b.update(kw)
    ch["beats"].append(b)
    return b


def take(tid, hour, cam, pre=1.5, **env):
    """cam: "ex ey ez ax ay az fov" (the opening pose; the script moves it)."""
    assert tid not in TAKES, tid
    TAKES[tid] = {"kind": "script", "hour": hour, "cam": cam, "env": env, "pre": pre, "beats": []}
    _state["take"] = tid


def live(text, steps=(), title="", key="", min=0.0, **kw):
    tid = _state["take"]
    assert tid, "live() outside a take"
    b = _add("live", text, take=tid, steps=list(steps), title=title, key=key, min=min, **kw)
    TAKES[tid]["beats"].append(b)
    return b


def free(tid, seconds, hour, cam, **env):
    """cam: "ex ey ez ax ay az  ex ey ez ax ay az  fov" (glide from the first pose to the second)."""
    assert tid not in TAKES, tid
    TAKES[tid] = {"kind": "free", "hour": hour, "cam": cam, "env": env, "seconds": seconds}


def clip(text, src, at=0.0, title="", key="", min=0.0, speed=1.0, **kw):
    b = _add("clip", text, src=src, at=at, title=title, key=key, min=min, speed=speed, **kw)
    if key:
        b["keys"] = [(0.6, key)]
    return b


def plate(text, name, title="", min=0.0, **kw):
    return _add("plate", text, name=name, title=title, min=min, **kw)


def still(text, image, title="", min=0.0, **kw):
    return _add("still", text, image=image, title=title, min=min, **kw)


def card(text, heading, lines=(), kind="decision", min=0.0, **kw):
    return _add("card", text, heading=heading, lines=list(lines), card=kind, min=min, **kw)


def split(text, panes, labels=(), title="", min=0.0, **kw):
    """panes: [(src, at)]"""
    return _add("split", text, panes=list(panes), labels=list(labels), title=title, min=min, **kw)


def beats():
    for ch in CHAPTERS:
        for b in ch["beats"]:
            yield b


def footage(src):
    """Path of a clip source: a take of this film, or a file of the earlier recordings (f3:name, f2:name)."""
    if src.startswith("f3:"):
        return os.path.join(F3, src[3:] + ".mp4")
    if src.startswith("f2:"):
        return os.path.join(F2, src[3:] + ".mp4")
    if src.startswith("anim:"):
        return os.path.join(OUT, "anim", src[5:] + ".mp4")
    return os.path.join(OUT, "takes", src + ".mp4")
