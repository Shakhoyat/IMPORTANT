"""Flow diagrams of the report (report/sections/flows.tex): one simple left-to-right chain per algorithm.

Each diagram has at most six boxes in one row and one decision with a single side branch, in plain words; the equations
stay in the text and in the algorithm that follows the figure. Green = data in / out, blue = step, orange = decision.
The macro names, captions and labels are the ones the sections already use.
Run: python3 tools/report/gen_flows.py"""
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "report", "sections", "flows.tex")

# macro, label, caption, boxes [(kind, text)], branch (index of the decision, label on the branch, branch box text,
# label on the way on, optional index the branch box loops back to)
FLOWS = [
    ("Clip", "fig:flow-clip",
     r"Flow of Algorithm~\ref{alg:clip}: whole parts are culled on the CPU (\code{math/Frustum.cpp}); the GPU clips the triangles of the parts that remain.",
     [("d", "scene parts"), ("s", "bounding sphere of each part"), ("q", "outside the view volume?"),
      ("s", "vertex shader: to clip space"), ("s", "clip triangles at the six planes"), ("d", "triangles inside the view")],
     (2, "yes", "skip the part", "no", None)),
    ("Raster", "fig:flow-raster",
     r"Flow of Algorithm~\ref{alg:raster}: one triangle becomes fragments (\code{math/ExplorerMath.cpp: rasterize}).",
     [("d", "screen triangle"), ("q", "facing the camera?"), ("s", "visit the pixels of its bounding box"),
      ("s", "keep pixels inside all three edges"), ("s", "keep the nearest: depth test"), ("d", "fragment colour")],
     (1, "no", "culled", "yes", None)),
    ("Whitted", "fig:flow-whitted",
     r"Flow of Algorithm~\ref{alg:whitted}: one ray of the recursive ray tracer (\code{raytrace/RayTracer.cpp}).",
     [("d", "ray through a pixel"), ("s", "nearest hit (bounding volume hierarchy)"), ("s", "direct light and shadow ray"),
      ("q", "mirror or glass, depth left?"), ("d", "colour of the pixel")],
     (3, "yes", "new ray: reflected or refracted", "no", 1)),
    ("Cv", "fig:flow-cv",
     r"Flow of Algorithm~\ref{alg:cv}: one thermal frame becomes target blobs (\code{vision/CvPipeline.cpp}).",
     [("d", "thermal image of the camera"), ("s", "smooth (Gaussian)"), ("s", "threshold (Otsu)"),
      ("s", "remove speckle"), ("s", "label connected regions"), ("d", "blobs: centre and size")],
     None),
    ("Tracker", "fig:flow-tracker",
     r"Flow of Algorithm~\ref{alg:tracker}: one frame of the tracker shared by both UAVs (uncrewed aerial vehicles).",
     [("d", "blobs of both cameras"), ("s", "project to ground points"), ("s", "predict every track (Kalman)"),
      ("q", "point inside a track's gate?"), ("s", "update the track"), ("d", "confirmed tracks")],
     (3, "no", "start a new track", "yes", None)),
    ("Squad", "fig:flow-squad",
     r"Flow of Algorithm~\ref{alg:squad}: the two UAVs' (uncrewed aerial vehicles') roles, the single strike and the combined strike, every tick.",
     [("d", "confirmed tracks"), ("s", "choose the most dangerous target"), ("q", "second intruder far away and urgent?"),
      ("s", "lead and wing lock the target"), ("s", "time the launches: both rounds arrive together"), ("d", "combined strike")],
     (2, "yes", "nearer UAV leaves the pair: single strike", "no", None)),
    ("Particles", "fig:flow-particles",
     r"Flow of Algorithm~\ref{alg:particles}: one explosion, simulated every tick and drawn every frame.",
     [("d", "blast at a point"), ("s", "spawn fire, smoke, sparks, debris"), ("s", "move: gravity and drag"),
      ("s", "cool: colour from temperature"), ("q", "life over?"), ("d", "sorted, blended sprites")],
     (4, "yes", "remove the particle", "no", None)),
    ("Soldier", "fig:flow-soldier",
     r"Flow of Algorithm~\ref{alg:soldier}: one route of the guard roster, from two posts to a smooth path.",
     [("d", "two posts of the roster"), ("s", "A* over the site grid"), ("s", "string pulling: drop needless corners"),
      ("s", "spline through the corners"), ("q", "too close to an obstacle?"), ("d", "smooth walking route")],
     (4, "yes", "add a corner there", "no", 3)),
    ("Gait", "fig:flow-gait",
     r"Flow of Algorithm~\ref{alg:gait}: one tick of a soldier's walk (\code{scene/Gait.cpp}).",
     [("d", "walking speed"), ("s", "advance the stride phase"), ("s", "place the swing foot ahead"),
      ("s", "leg: two-link inverse kinematics"), ("s", "arms swing against the legs"), ("d", "joint angles")],
     None),
    ("Rover", "fig:flow-rover",
     r"Flow of Algorithm~\ref{alg:intruder}: one tick of an intruder rover, planner (MicroMouse) first, line follower after it.",
     [("d", "rover and UAV positions"), ("s", "flood fill: distance to cover"), ("s", "Dijkstra: cheapest hidden route"),
      ("q", "seen by a UAV?"), ("s", "follow the line (PD steering)"), ("d", "wheel speeds")],
     (3, "yes", "re-plan at once", "no", 1)),
]

HEAD = r"""% Flow diagrams of the ten algorithms, generated by tools/report/gen_flows.py: one row, at most six boxes and one decision
% each, plain words (the equations are in the text and in the algorithm after each figure).
% Green = data in / out, blue = step, orange = decision. Each \flow... macro is a figure placed right before its algorithm.

\tikzset{
  flow/.style={font=\fontsize{8}{9.4}\selectfont},
  fd/.style={draw=green!40!black,fill=green!9,rounded corners=5pt,text width=2.05cm,align=center,inner sep=2.5pt,minimum height=11mm},
  fs/.style={draw=blue!45!black,fill=blue!6,rounded corners=1.5pt,text width=2.05cm,align=center,inner sep=2.5pt,minimum height=11mm},
  fq/.style={draw=orange!75!black,fill=orange!13,rounded corners=1.5pt,text width=2.05cm,align=center,inner sep=2.5pt,minimum height=11mm,
             double,double distance=0.6pt},
  fa/.style={-{Latex[length=1.6mm]},semithick,black!65},
  yes/.style={font=\fontsize{7.5}{8.5}\selectfont\itshape,text=black!70,inner sep=1.5pt},
}
"""


def figure(macro, label, caption, boxes, branch):
    n = len(boxes)
    pitch = 16.4 / n if n > 5 else 3.1   # cm between box centres
    o = ["% " + "-" * 96 + " " + macro, r"\newcommand{\flow%s}{%%" % macro, r"\begin{figure}[htbp]", r"\centering",
         r"\begin{tikzpicture}[flow]"]
    for k, (kind, text) in enumerate(boxes):
        o.append(r"\node[f%s] (n%d) at (%.2f,0) {%s};" % (kind, k, k * pitch, text))
    for k in range(n - 1):
        lab = ""
        if branch and branch[0] == k:
            lab = r"node[yes,above]{%s}" % branch[3]
        o.append(r"\draw[fa] (n%d)--%s(n%d);" % (k, lab, k + 1))
    if branch:
        q, blabel, btext, _, back = branch
        o.append(r"\node[fs] (b) at (%.2f,-1.75) {%s};" % (q * pitch, btext))
        o.append(r"\draw[fa] (n%d)--node[yes,right]{%s}(b);" % (q, blabel))
        if back is not None:
            o.append(r"\draw[fa] (b.west) -| (n%d.south);" % back)
    o += [r"\end{tikzpicture}", r"\caption{%s}" % caption, r"\label{%s}" % label, r"\end{figure}}", ""]
    return "\n".join(o)


def main():
    open(OUT, "w").write(HEAD + "\n" + "\n".join(figure(*f) for f in FLOWS))
    print("wrote", OUT, len(FLOWS), "flow diagrams")


if __name__ == "__main__":
    main()
