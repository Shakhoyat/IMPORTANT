"""The film's score, written in code: every note below is synthesised here (no samples, no library music).

One tempo for the whole film (80 beats a minute), one key (D minor and its relatives), and a mood per act made of four
layers: a pad of detuned saw waves, a plucked arpeggio, a bass and light percussion. Act changes get a riser and a low
hit. render() writes a 48 kHz stereo WAV as long as the film; the mix ducks it under the narration."""
import math

import numpy as np
from scipy.signal import butter, fftconvolve, lfilter

SR = 48000
BPM = 80.0
BEAT = 60.0 / BPM
BAR = 4 * BEAT
RNG = np.random.default_rng(7)

N = {"C": 0, "C#": 1, "D": 2, "Eb": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "Ab": 8, "A": 9, "Bb": 10, "B": 11}
CH = {"Dm": ("D3", "F3", "A3", "E4"), "Bb": ("Bb2", "D3", "F3", "A3"), "F": ("F2", "A3", "C4", "E4"), "C": ("C3", "E3", "G3", "D4"),
      "Gm": ("G2", "Bb3", "D4", "F4"), "Am": ("A2", "C3", "E3", "G3"), "A": ("A2", "C#3", "E3", "G3")}
# act -> (progression, pad level, arpeggio every n-th of a beat (0 = none), bass, percussion)
MOOD = {"": (("Dm", "Bb", "F", "C"), 1.0, 0, 0.5, 0.0),
        "Orientation": (("Dm", "Bb", "F", "C"), 1.0, 1, 0.0, 0.0),
        "Geometry": (("Dm", "F", "C", "Gm"), 0.9, 2, 0.35, 0.0),
        "Viewing": (("F", "C", "Dm", "Bb"), 0.9, 2, 0.4, 0.0),
        "Light": (("Bb", "F", "Gm", "Dm"), 1.0, 2, 0.35, 0.0),
        "Rendering": (("Bb", "Gm", "F", "C"), 1.0, 2, 0.4, 0.0),
        "World": (("F", "Bb", "C", "Dm"), 1.0, 1, 0.4, 0.0),
        "Control": (("Dm", "Bb", "C", "Am"), 0.8, 2, 0.8, 0.7),
        "Vision": (("Dm", "Dm", "Bb", "A"), 0.9, 4, 0.6, 0.25),
        "Agents": (("Dm", "Gm", "Bb", "A"), 0.8, 4, 0.9, 0.8),
        "Engineering": (("F", "C", "Dm", "Bb"), 1.0, 2, 0.45, 0.3)}


def hz(name):
    return 440.0 * 2.0 ** ((N[name[:-1]] + 12 * (int(name[-1]) + 1) - 69) / 12.0)


def lowpass(x, f, order=2):
    b, a = butter(order, f / (SR / 2), "low")
    return lfilter(b, a, x, axis=0)


def highpass(x, f, order=2):
    b, a = butter(order, f / (SR / 2), "high")
    return lfilter(b, a, x, axis=0)


def add(buf, at, x, pan=0.0, gain=1.0):
    i = int(at * SR)
    if i >= len(buf) or i + len(x) <= 0:
        return
    x = x[: len(buf) - i]
    buf[i:i + len(x), 0] += x * gain * math.cos((pan + 1) * math.pi / 4)
    buf[i:i + len(x), 1] += x * gain * math.sin((pan + 1) * math.pi / 4)


def pad(buf, at, chord, dur, level):
    n = int((dur + 2.4) * SR)
    t = np.arange(n) / SR
    env = np.clip(t / 2.0, 0, 1) * np.clip((dur + 2.4 - t) / 2.4, 0, 1)
    for k, name in enumerate(CH[chord]):
        f = hz(name)
        for cents in (-9, -4, 0, 5, 10):
            ph = RNG.random()
            saw = 2.0 * ((f * 2 ** (cents / 1200.0) * t + ph) % 1.0) - 1.0
            x = lowpass(saw, 520 + 260 * k) * env * (1.0 + 0.12 * np.sin(2 * math.pi * 0.11 * t + 6.28 * ph))
            add(buf, at, x, pan=(cents / 10.0) * 0.8, gain=0.018 * level)


def pluck(f, dur=1.6):
    t = np.arange(int(dur * SR)) / SR
    return (np.sin(2 * math.pi * f * t) * np.exp(-t * 4.5) + 0.35 * np.sin(4 * math.pi * f * t) * np.exp(-t * 7.0)
            + 0.12 * np.sin(6 * math.pi * f * t) * np.exp(-t * 10.0)) * np.clip(t / 0.004, 0, 1)


def bass_note(f, dur):
    t = np.arange(int(dur * SR)) / SR
    saw = 2.0 * ((f * t) % 1.0) - 1.0
    return (np.sin(2 * math.pi * f * t) * 0.8 + lowpass(saw, 380) * 0.35) * np.exp(-t * 3.2) * np.clip(t / 0.006, 0, 1)


def kick():
    t = np.arange(int(0.32 * SR)) / SR
    return np.sin(2 * math.pi * (46 * t + (95 / 22.0) * (1 - np.exp(-t * 22)))) * np.exp(-t * 13)


def hat():
    t = np.arange(int(0.07 * SR)) / SR
    return highpass(RNG.standard_normal(len(t)), 7000) * np.exp(-t * 70)


def section(act, dur):
    """The music of one act, `dur` seconds (plus a tail that the caller cross-fades)."""
    prog, pl, arp, bs, pc = MOOD.get(act, MOOD[""])
    buf = np.zeros((int((dur + 6.0) * SR), 2))
    bars = int(math.ceil(dur / BAR)) + 1
    k_, h_ = kick(), hat()
    for b in range(bars):
        chord = prog[(b // 2) % len(prog)]
        t0 = b * BAR
        if b % 2 == 0:
            pad(buf, t0, chord, 2 * BAR, pl)
        notes = [hz(n) * 2 for n in CH[chord]]
        if arp:
            steps = 4 * arp if arp <= 2 else 8
            order = (0, 2, 1, 3, 2, 1, 3, 2) if arp >= 2 else (0, 2, 3, 1)
            for s in range(steps):
                if arp == 1 and s % 2:
                    continue
                f = notes[order[s % len(order)]] * (2 if (s + b) % 7 == 6 else 1)
                add(buf, t0 + s * BAR / steps, pluck(f), pan=0.5 * math.sin(s * 1.7 + b), gain=0.05 if arp < 4 else 0.035)
        if bs:
            root = hz(CH[chord][0]) / (2 if hz(CH[chord][0]) > 80 else 1)
            pat = (0, 1.5, 2, 3.5) if pc < 0.5 else (0, 0.5, 1.5, 2, 2.5, 3.5)
            for p in pat:
                add(buf, t0 + p * BEAT, bass_note(root, BEAT * 0.9), gain=0.16 * bs)
        if pc:
            for q in range(4):
                if q in (0, 2) or pc > 0.6:
                    add(buf, t0 + q * BEAT, k_, gain=0.22 * pc * (1.0 if q in (0, 2) else 0.55))
                for e in (0.5,) if pc < 0.6 else (0.25, 0.5, 0.75):
                    add(buf, t0 + (q + e) * BEAT, h_, pan=0.3, gain=0.045 * pc)
    return buf


def riser(dur=2.6):
    t = np.arange(int(dur * SR)) / SR
    x = RNG.standard_normal(len(t))
    out = np.zeros(len(t))
    for k in range(8):                       # a noise sweep made of eight bands that open one after another
        seg = slice(len(t) * k // 8, len(t))
        out[seg] += lowpass(highpass(x[seg], 300 + 500 * k), 900 + 900 * k)
    return out * (t / dur) ** 2.2 * 0.5


def boom():
    t = np.arange(int(2.4 * SR)) / SR
    return np.sin(2 * math.pi * (34 * t + (60 / 9.0) * (1 - np.exp(-t * 9)))) * np.exp(-t * 2.2)


def reverb(x):
    n = int(2.6 * SR)
    t = np.arange(n) / SR
    ir = lowpass(RNG.standard_normal((n, 2)), 5200) * np.exp(-t * 2.4)[:, None]
    ir[: int(0.012 * SR)] = 0
    wet = np.stack([fftconvolve(x[:, c], ir[:, c])[: len(x)] for c in (0, 1)], axis=1)
    return x + 0.055 * wet


def render(dsl, total, out, B=None):
    import soundfile as sf
    spans = []                                # (start, end, act): consecutive chapters of one act
    for ch in dsl.CHAPTERS:
        if not ch["beats"]:
            continue
        a, e = ch["beats"][0]["start"], ch["beats"][-1]["start"] + ch["beats"][-1]["dur"]
        if spans and spans[-1][2] == ch["act"]:
            spans[-1][1] = e
        else:
            spans.append([a, e, ch["act"]])
    music = np.zeros((int(total * SR) + SR, 2))
    xf = 2.0
    for a, e, act in spans:
        m = reverb(section(act, e - a + xf))
        n = int((e - a + xf) * SR)
        m = m[:n]
        f = int(xf * SR)
        m[:f] *= np.linspace(0, 1, f)[:, None]
        m[-f:] *= np.linspace(1, 0, f)[:, None]
        i = max(0, int((a - xf / 2) * SR))
        m = m[: len(music) - i]
        music[i:i + len(m)] += m
        if a > 1.0:
            r = riser()
            add(music, a - len(r) / SR, r, gain=0.10)
            add(music, a, boom(), gain=0.30)
    music = music[: int(total * SR)]
    f = int(3.0 * SR)
    music[-f:] *= np.linspace(1, 0, f)[:, None]
    music = highpass(music, 32)
    rms = math.sqrt(float(np.mean(music ** 2))) or 1.0
    music *= 10 ** (-23.0 / 20.0) / rms       # stem at about -23 dBFS RMS; the mix sets its place
    sf.write(out, np.clip(music, -1, 1), SR, subtype="PCM_24")
    return out
