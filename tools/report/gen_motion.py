"""Motion of the UAVs and the intruder rovers over the whole base, from the simulation tracks of the export
(report/data/routes.json; the site plan and its drawing come from gen_maps.py).

p_motion_steps: six moments of one night over the whole camp: the two UAV orbits (the last 12 s as a trail, the camera
    footprint as a circle), every rover's driven path so far and where it stands, the objectives, the kills.
p_motion_series: one rover against time: its distance to the nearest UAV, its speed (pauses in cover) and how fast its
    heading turns (a re-plan shows as a turn), with the seconds a UAV was overhead shaded.
Run: ~/.local/bin/uv run -q --with matplotlib --with numpy python tools/report/gen_motion.py"""
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

sys.path.insert(0, os.path.dirname(__file__))
import gen_maps as gm  # noqa: E402

OUT = gm.OUT
R = gm.routes
ROVER = ["#c0504d", "#d08c1a", "#7a4f9a", "#1f4e79", "#3a8a8a", "#4f8a3a"]
UAV = ["#111111", "#555555"]
FOOT = 5.0   # m: radius of the ground a UAV camera covers from its patrol height (drawn as a circle)


def lives(track):
    """Split a rover track where it respawns (a jump of more than 3 m between samples)."""
    a = np.array(track)
    if len(a) < 2:
        return [a] if len(a) else []
    cut = np.nonzero(np.hypot(np.diff(a[:, 1]), np.diff(a[:, 2])) > 3.0)[0] + 1
    return [s for s in np.split(a, cut) if len(s) > 1]


def kills():
    out = []
    for e in R["events"]:
        if "Alive -> Wreck" in e["event"]:
            tr = np.array(R["rover_tracks"][e["intruder"]])
            k = np.argmin(np.abs(tr[:, 0] - e["t"]))
            out.append((e["t"], tr[k, 1], tr[k, 2], e["intruder"]))
    return out


def steps():
    rov = [np.array(t) for t in R["rover_tracks"] if t]
    uav = [np.array(t) for t in R["uav_tracks"]]
    tmax = max(u[-1, 0] for u in uav)
    K = kills()
    times = [k[0] + 0.6 for k in K[:2]]   # two of the panels sit just after a kill
    grid = list(np.linspace(8.0, tmax - 4.0, 6))
    for t in times:   # move the nearest grid time onto the kill
        j = int(np.argmin([abs(g - t) for g in grid]))
        grid[j] = t
    grid = sorted(grid)
    fig, axs = plt.subplots(2, 3, figsize=(7.0, 5.3))
    for ax, t in zip(axs.ravel(), grid):
        gm.base(ax, "t = %.0f s" % t, r=36)
        for txt in list(ax.texts):   # site names only in the first panel; the bunker label would cover the tracks
            if txt.get_text() != "10 m" and (ax is not axs.ravel()[0] or txt.get_text().startswith("command bunker")):
                txt.remove()
        for o in R["objectives"]:
            ax.plot(o[0], o[2], marker="*", ms=7, mfc="#f2c230", mec="black", mew=0.5, zorder=7)
        for i, tr in enumerate(R["rover_tracks"]):
            for seg in lives(tr):
                s = seg[seg[:, 0] <= t]
                if len(s) < 2:
                    continue
                alive_now = seg[-1, 0] >= t
                ax.plot(s[:, 1], s[:, 2], color=ROVER[i % 6], lw=1.1 if alive_now else 0.7, alpha=0.95 if alive_now else 0.4, zorder=4)
                if alive_now:
                    ax.plot(s[-1, 1], s[-1, 2], marker="s", ms=4.2, mfc=ROVER[i % 6], mec="white", mew=0.6, zorder=8)
        for k, u in enumerate(uav):
            s = u[(u[:, 0] <= t) & (u[:, 0] >= t - 12.0)]
            if len(s) < 2:
                continue
            ax.plot(s[:, 1], s[:, 2], color=UAV[k], lw=0.9, ls=(0, (1, 1.2)), zorder=5)
            ax.add_patch(plt.Circle((s[-1, 1], s[-1, 2]), FOOT, fc="#f7e08a", ec="#b8860b", lw=0.6, alpha=0.45, zorder=3))
            ax.plot(s[-1, 1], s[-1, 2], marker="^", ms=5, mfc=UAV[k], mec="white", mew=0.6, zorder=9)
            ax.text(s[-1, 1] + 1.8, s[-1, 2] - 1.8, "UAV-%d" % (k + 1), fontsize=6.5, color="black", zorder=11,   # above the crosses
                    bbox=dict(fc="white", ec="none", alpha=0.85, pad=0.4))
        for kt, kx, kz, ki in K:
            if kt <= t:
                ax.plot(kx, kz, marker="x", ms=6, mew=1.6, color="black", zorder=9)
    from matplotlib.lines import Line2D
    from matplotlib.patches import Patch
    h = [Line2D([], [], color=ROVER[i], lw=1.6, label="rover %d" % (i + 1)) for i in range(min(4, len(rov)))]
    h += [Line2D([], [], color="black", lw=1.0, ls=(0, (1, 1.2)), marker="^", ms=4, label="UAV, last 12 s"),
          Patch(fc="#f7e08a", ec="#b8860b", alpha=0.6, label="camera footprint"),
          Line2D([], [], color="none", marker="*", ms=7, mfc="#f2c230", mec="black", label="objective"),
          Line2D([], [], color="none", marker="x", ms=6, mew=1.6, mec="black", label="rover destroyed")]
    fig.legend(handles=h, loc="lower center", ncol=8, frameon=False, fontsize=6.6, handlelength=1.5, columnspacing=0.9)
    fig.subplots_adjust(left=0.005, right=0.995, top=0.95, bottom=0.07, wspace=0.02, hspace=0.12)
    fig.savefig(os.path.join(OUT, "p_motion_steps.pdf"))
    fig.savefig(os.path.join(OUT, "p_motion_steps.png"), dpi=200)
    plt.close(fig)
    print("wrote p_motion_steps", ["%.0f" % g for g in grid])


def series():
    uav = [np.array(t) for t in R["uav_tracks"]]
    # the rover with the longest single life
    best = max(((seg, i) for i, tr in enumerate(R["rover_tracks"]) for seg in lives(tr)), key=lambda x: len(x[0]))
    seg, idx = best
    t = seg[:, 0]
    d = np.full(len(t), 1e9)
    for u in uav:
        ux, uz = np.interp(t, u[:, 0], u[:, 1]), np.interp(t, u[:, 0], u[:, 2])
        d = np.minimum(d, np.hypot(seg[:, 1] - ux, seg[:, 2] - uz))
    vx, vz = np.gradient(seg[:, 1], t), np.gradient(seg[:, 2], t)
    speed = np.hypot(vx, vz)
    k = np.ones(5) / 5.0
    speed = np.convolve(speed, k, mode="same")
    head = np.unwrap(np.arctan2(vx, vz))
    turn = np.abs(np.degrees(np.gradient(np.convolve(head, k, mode="same"), t)))
    turn[speed < 0.05] = 0.0
    over = d < FOOT
    fig, axs = plt.subplots(3, 1, figsize=(7.0, 3.5), sharex=True)
    col = ROVER[idx % 6]
    rows = [(d, "distance to the\nnearest UAV (m)", None), (speed, "speed (m/s)", None), (np.minimum(turn, 200), "turn rate (deg/s)", None)]
    for ax, (y, lab, _) in zip(axs, rows):
        ax.plot(t, y, color=col, lw=1.0)
        ax.fill_between(t, 0, 1, where=over, transform=ax.get_xaxis_transform(), color="#f7e08a", alpha=0.6, lw=0)
        ax.set_ylabel(lab, fontsize=7)
        ax.tick_params(labelsize=7)
        ax.grid(True, lw=0.3, color="0.85")
        for s in ("top", "right"):
            ax.spines[s].set_visible(False)
    axs[0].axhline(FOOT, color="#b8860b", lw=0.7, ls="--")
    # the notes stand above each panel, clear of the data
    pauses = int(np.sum(np.diff((speed < 0.05).astype(int)) == 1))
    for ax, note in ((axs[0], "dashed: camera footprint, 5 m"), (axs[1], "%d pause%s in cover" % (pauses, "" if pauses == 1 else "s")),
                     (axs[2], "peaks: the route was re-planned")):
        ax.text(1.0, 1.02, note, transform=ax.transAxes, ha="right", va="bottom", fontsize=6.5, color="#7a5a00" if ax is axs[0] else "0.3")
    axs[2].set_xlabel("time (s); shaded: a UAV is overhead (the rover is inside a camera footprint)", fontsize=7.5)
    fig.subplots_adjust(left=0.105, right=0.99, top=0.95, bottom=0.125, hspace=0.30)
    fig.savefig(os.path.join(OUT, "p_motion_series.pdf"))
    fig.savefig(os.path.join(OUT, "p_motion_series.png"), dpi=200)
    plt.close(fig)
    print("wrote p_motion_series: rover %d, %.0f s, overhead %.0f s, %d pauses" % (idx + 1, t[-1] - t[0], over.sum() * np.median(np.diff(t)), pauses))


if __name__ == "__main__":
    steps()
    series()
