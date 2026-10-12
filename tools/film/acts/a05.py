"""Act 8 (engineering: hard problems, proof) and the close."""
from dsl import *  # noqa: F401,F403

WB = ROOT + "/report/figures/workbook/"
try:
    import json
    T = json.load(open(OUT + "/takes/night_times.json"))
except Exception:
    T = {"single": 7.75, "combined": 18.0, "hide": 12.0}

act("Engineering")
chapter("hard", "Hard problems")
card("Question eight. It runs. How do you know it is right, and what went wrong on the way?", "8  Engineering",
     ["What went wrong,", "and how do you know it is right?"], kind="act")
plate("Four problems took the longest. Far surfaces flickered, because depth is stored most precisely near the "
      "camera. Shadows had stripes, because a surface shadowed itself. Thin wires shimmered from frame to frame. And "
      "soldiers walked through walls, until doors became part of the walk map. Each one is in the report with its "
      "cause, the fix and a picture before and after.", "hard", title="Four hard problems")

chapter("proof", "Proof")
plate("Every claim in this film can be checked. Three hundred and ninety-six automatic tests run on every build. "
      "The same seed always gives exactly the same night, so a result can be repeated. A whole play session can be "
      "recorded and replayed.", "proof", title="How it is checked")
still("And every number has one home: a workbook. Each coordinate of each part, each constant of each algorithm, "
      "each number in the report. The report takes its numbers from the workbook, and a checker fails the build if "
      "the program, the workbook and the report ever disagree.",
      [WB + "Transform_Trace.png", WB + "Illumination.png", WB + "Tracking.png"], title="One workbook for every number")

act("")
chapter("close", "One night, uncut")
clip("To end: one night, without a cut and without a word.", "n_wide", at=1.0, title="", min=4.6, level=-24.0)
clip("", "n_follow", at=T["single"] - 3.0, min=9.0, level=-17.0)
clip("", "n_uav1", at=T["single"] + 6.0, min=4.0, level=-17.0)
clip("", "n_follow", at=T["combined"] - 3.0, min=8.0, level=-16.0)
clip("", "n_wide", at=T["combined"] + 5.0, min=6.0, level=-18.0)
plate("From one triangle to this: sixteen generators, five thousand four hundred and seventy-four parts, a full "
      "pipeline with its mathematics written out, a thermal camera, a tracker, and three kinds of agents.", "numbers",
      title="")
card("ThermalSense. Computer Graphics Laboratory, Department of Computer Science and Engineering, KUET. Everything "
     "is in the repository: the code, the report, the workbook, and this film's own script. Now it is your turn.",
     "ThermalSense", ["Computer Graphics Laboratory  ·  CSE 4102", "Department of Computer Science and Engineering, KUET", "",
                      "Md. Shakhoyat Rahman Shujon  ·  Roll 2107104",
                      "Submitted to  Md Tajmilur Rahman  and  Md. Mubtashim Abrar Nihal", "", "code, report, workbook and this film's script: in the repository",
                      "now it is your turn"], kind="title", min=14.0)
