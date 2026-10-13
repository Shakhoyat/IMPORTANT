"""Engineering (hard problems, how it is checked) and the close."""
from dsl import *  # noqa: F401,F403

WB = ROOT + "/report/figures/workbook/"
try:
    import json
    T = json.load(open(OUT + "/takes/night_times.json"))
except Exception:
    T = {"single": 7.75, "combined": 18.0, "hide": 12.0}

act("Engineering")
chapter("hard", "Hard problems")
plate("It did not all work the first time, and four problems took me the longest. Far surfaces flickered, because "
      "depth is stored most precisely close to the camera. Shadows had stripes, because a surface was shadowing "
      "itself. Thin wires shimmered from one frame to the next. And soldiers walked through walls, until the doors "
      "became part of the walk map. Each of these is in the report with its cause, its fix, and a picture from before "
      "and after.", "hard", title="Four hard problems")

chapter("proof", "How it is checked")
plate("I also wanted every claim in this video to be checkable. Three hundred and ninety-six automatic tests run on "
      "every build. The same seed always gives exactly the same night, so any result can be repeated. And a whole "
      "play session can be recorded and replayed.", "proof", title="How it is checked")
still("And this brings me back to the workbook. Every coordinate of every part, every constant of every algorithm, "
      "and every number in the report lives there, on twenty-three sheets. A checker stops the build if the program, "
      "the workbook and the report ever disagree.",
      [WB + "Transform_Trace.png", WB + "Illumination.png", WB + "Tracking.png"], title="One workbook for every number")

act("")
chapter("close", "One night at the outpost")
clip("To finish, here is one night at the outpost, with no more words from me.", "n_wide", at=1.0, title="", min=5.5, level=-24.0)
clip("", "n_follow", at=T["single"] - 3.0, min=7.0, level=-17.0)
clip("", "n_follow", at=T["combined"] - 3.0, min=7.0, level=-16.0)
clip("", "n_wide", at=T["combined"] + 5.0, min=4.5, level=-18.0)
plate("That was ThermalSense, from a single triangle to sixteen primitives, five thousand four hundred and "
      "seventy-four parts, a complete pipeline, a thermal camera, a tracker, and three kinds of agents.", "numbers",
      title="")
card("One last word, to my juniors. Be brave with your projects. Pick a problem that scares you a little, and try "
     "to solve it elegantly. Because I believe that if student A and student B both use AI for a project, what "
     "matters is how creative you can be, and how much depth you can cover. Thank you for watching.",
     "“If student A and student B both use AI for a project,\nshow me how creative you can be,\nand how much depth you can cover.”",
     ["Be brave with your projects. Solve things elegantly.", "Shakhoyat   ·   Roll 2107104   ·   CSE 2K21, KUET"], kind="closing", min=20.0)
