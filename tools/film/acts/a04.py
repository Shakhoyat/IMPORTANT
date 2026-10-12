"""Act 6 (vision) and Act 7 (agents: the drones, the intruder, the game)."""
from dsl import *  # noqa: F401,F403

NIGHT = "-26 16 30 0 1 0  -20 13 34 0 1 0  55"
# One deterministic night (23:00, seed 1), filmed four ways; the wide take also writes what the algorithms hold.
free("n_wide", 46, 23.0, NIGHT, algo=True, events=True)
free("n_follow", 46, 23.0, NIGHT, TS_FILM_VIEW="follow")
free("n_uav1", 46, 23.0, NIGHT, TS_FILM_VIEW="uav1")
free("n_flir", 46, 23.0, NIGHT, TS_FILM_VIEW="flir")
# event times of that night (s into the takes), set from n_wide_events.txt by `build.py events`
T = {"single": 7.75, "combined": 18.0, "hide": 12.0}
try:
    import json
    T.update(json.load(open(OUT + "/takes/night_times.json")))
except Exception:
    pass

# ------------------------------------------------------------------------------------------------------- act 6
act("Vision")
chapter("sensor", "The thermal camera")
card("Question six. The drones must find an intruder in the dark, by themselves. What does a machine see, and how "
     "does it decide that a warm spot is a target?", "6  Vision", ["How does a machine find", "an intruder in the dark?"], kind="act")
plate("A thermal camera does not see light. It sees heat. Every part in the camp has a temperature: an engine is "
      "hot, a soldier is warm, the ground is cool. A real sensor is not perfect, so the program models that too: the "
      "lens blurs the picture, every pixel has its own small error, columns show as stripes, and there is noise.",
      "sensor", title="A camera that sees heat, with its faults")
take("thermal", 22.6, "-14 9 26 0 1 6 50")
live("F switches the whole view to thermal. Hot is bright. T changes the palette.",
     [(0, "set fpv 0"), (0, "orbit 0 1 6 26 10 4 110 50 cut"), (1.0, "set view thermal", "F"), ("55%", "set palette 1", "T"), ("72%", "set palette 2", "T"),
      ("88%", "set palette 0", "T")], title="F: thermal view   T: palette", min=10.0)
live("And this is what a drone's own camera sends down: three hundred and twenty by two hundred and forty pixels, "
     "with its noise.", [(0.2, "set hud 1"), (0.3, "set fpv 1")], title="The drone's camera: 320 x 240", min=7.5)

chapter("cv", "From a heat picture to a target")
plate("Every frame of that picture goes through the same five steps. One: a threshold. Keep only pixels above "
      "eighty per cent heat, or above the level that best splits the picture into dark and bright, whichever is "
      "higher. Two: remove single stray pixels. Three: join neighbouring hot pixels into regions. Four: measure each "
      "region. Five: decide what it is.", "cvsteps", title="Five steps, every frame")
plate("Measuring a region gives its area, its centre, its hottest pixel and how stretched it is. Then the rule. "
      "Small, with a very hot core, not too stretched: a vehicle, a target. Warm, without a hot core, upright or "
      "compact: a person, and a person is never fired at. Anything else is clutter.", "classify",
      title="Target, person or clutter")
still("By day you can check it. Through the drone's camera, the soldiers are boxed as persons. None of them is ever "
      "a target.", [IMG + "/scene/day_thermal_fpv_persons.jpg"], title="Soldiers are marked as persons", min=8.0)

chapter("track", "Tracking")
plate("One detection is not enough: the camera shakes, pixels flicker, and a rover drives behind a tree. So each "
      "target becomes a track with a position, a speed and an uncertainty. Between frames the track is moved ahead "
      "by its speed, and the uncertainty grows. A new detection pulls it back, and the uncertainty shrinks. This is "
      "a Kalman filter.", "kalman", title="Predict, then correct")
plate("Which detection belongs to which track? Only detections inside a track's gate may match, and the pairs are "
      "chosen so that the total distance is smallest. When both drones see the same target, their two lines of "
      "sight are crossed, which fixes the position much better than one camera can.", "assoc",
      title="Gating, matching, two views")
clip("In the drone's view you can watch it. The boxes are tracks. When the rover goes behind cover, the track coasts "
     "on its prediction, and is picked up again when the rover comes out.", "n_flir", at=T["hide"] - 3.0,
     title="Tracks coast behind cover", min=10.0, level=-27.0)
plate("Here is that same night as numbers: the uncertainty of one track over time. It jumps while the rover is "
      "hidden and falls back each time a camera sees it again.", "tracklog", title="One track's uncertainty, measured", min=8.0)

# ------------------------------------------------------------------------------------------------------- act 7
act("Agents")
chapter("uav", "The drones")
card("Question seven. Three kinds of things in the camp think for themselves: the drones, the intruders and the "
     "soldiers. What rules make them act as if they had a plan?", "7  Agents", ["What rules make them", "behave?"], kind="act")
plate("A drone has a duty for each part of the day. Docked on its pad by day. At dusk the rotors spool up and it "
      "takes off. At night the two patrol on opposite sides of a circle. At dawn they return and land.", "duty",
      title="A drone's day")
clip("Dusk. The lamps come on, and both drones lift off with their searchlights.", "f2:uav_takeoff", at=0.6, title="Take-off at dusk", min=8.0)
clip("On patrol, searchlights sweeping the camp.", "n_wide", at=0.8, title="Night patrol", min=5.0)

chapter("strike", "One drone, or both?")
plate("When a track is confirmed, the nearer drone becomes the lead and the other its wing, ninety degrees apart. "
      "The lead must hold its lock for eight tenths of a second, the wing for half a second, each with a clear line "
      "of fire. With one intruder, the lead waits up to eight seconds for the wing, so that both fire.", "decide1",
      title="Lead and wing")
plate("They do not fire at the same moment. The round from the farther drone needs longer, so that drone fires "
      "first. Both rounds arrive together. That is a combined strike.", "tot", title="Time on target")
clip("Here it is. One intruder left. Both drones hold their locks, and both rounds arrive together.", "n_follow",
     at=T["combined"] - 4.2, title="Combined strike", min=8.5, level=-20.0)
plate("Now a second intruder appears. If it is more than fourteen metres from the first, and it is close to "
      "something it could damage, waiting would cost too much. The pair splits. The nearer drone takes the second "
      "target and strikes alone. When that target is gone, or the two come within nine metres, the pair forms again.",
      "decide2", title="Two intruders: split, or stay together?")
clip("The same night, earlier. Two intruders far apart. The pair splits, and one drone fires alone.", "n_follow",
     at=T["single"] - 4.0, title="Single strike", min=8.0, level=-20.0)
clip("And the guards join in: a soldier at his post opens fire on any tracked intruder within twenty-six metres.",
     "n_uav1", at=T["single"] + 5.5, title="The guards fire too", min=6.5, level=-22.0)
card("Compared: always one drone per target, or always both. Chosen: a rule that weighs it. Both rounds for a lone "
     "intruder, one each when a second one is far away and urgent.", "Decision",
     ["compared   always alone  /  always together", "chosen     together, unless a second target is far and urgent",
      "why        two hits kill faster; two targets cannot wait"])

chapter("rover1", "The intruder: choosing a route")
plate("Now change sides. An intruder is a small rover that wants to reach something in the camp without being "
      "seen. It lays a grid over the ground: eighty by eighty cells, half a metre each. Cells inside obstacles are "
      "blocked.", "rgrid", title="The intruder's map")
plate("Every free cell gets a cost. One for the step. More if a drone can see that cell: the rover draws a line "
      "from each drone to the cell, and if nothing is in the way, the cell is exposed. And a little random cost "
      "of its own, so that no two rovers choose the same way.", "rcost", title="What a cell costs")
plate("Then the trick from the MicroMouse maze robots. Start at the goal with zero and let the numbers flood "
      "outwards, each cell taking the cheapest neighbour plus its own cost. When the flood reaches the rover, the "
      "route is simply: always step to the neighbour with the smaller number.", "rflood", title="Flood fill from the goal")
take("rovplan", 23.0, "-26 16 30 0 1 0 55", algo=True)
live("M shows this inside the program: the flood field as coloured cells, and the route as a line. Each rover has "
     "its own.", [(0, "set plan 1", "M"), (0, "cam 2 26 6 2 0 4 55 2.5"), ("100%-0.3", "set plan 0")], title="M: the flood field and the route", min=9.0)

chapter("rover2", "The intruder: following the line")
plate("A route is not a movement. The rover follows it the way a line-follower robot does. Five sensors across "
      "its nose each say whether they are over the line. Their weighted average is the error: how far the line is "
      "to one side. A controller turns that error, and how fast it changes, into a turn rate, and the two wheels "
      "are driven at different speeds.", "lfr", title="Five sensors, two wheels")
take("rovclose", 23.0, "-26 16 30 0 1 0 55")
live("Close to one. It weaves a little, changes speed, and sometimes stops: so that a tracker cannot simply "
     "extend a straight line.", [(0, "set plan 1"), (0.1, "track rover - 3.6 1.7 12 40 46 cut")], title="A rover on its line", min=10.0)
live("And this is the view from its own mast camera.", [(0.2, "pov rover - alt"), ("100%-0.2", "set plan 0")], title="The rover's camera", min=6.0)

chapter("rover3", "The intruder: hearing a drone and hiding")
plate("And it listens. When a drone comes within five metres, the rover stops what it is doing. Its goal is no "
      "longer the camp. Its goal becomes every cell that the drone cannot see: under a tree, behind a wall, under a "
      "net. The same flood fill runs again from those cells, and the rover drives to the nearest one. It waits four "
      "seconds, and plans again, because by then the drones have moved.", "revade", title="Hiding from a drone")
split("Three views of the same seconds. On the left, the drone's thermal camera. In the middle, the rover's own "
      "map: red where a drone can see, dark where it is hidden, the route in yellow. On the right, the wide view. "
      "Watch the route bend into the shadow as the drone comes near.",
      [("n_flir", T["hide"] - 5.0), ("anim:rovermap", T["hide"] - 5.0), ("n_follow", T["hide"] - 5.0)],
      labels=["the drone's thermal camera", "the rover's map", "the camp"], title="One moment, three views", min=18.0)
card("Compared: a fixed path, a shortest path, and a path that prices being seen. Chosen: flood fill on a cost "
     "map that changes as the drones move, followed like a line.", "Decision",
     ["compared   fixed path  /  shortest path  /  priced exposure", "chosen     flood fill + line follower, re-planned as drones move",
      "why        the rover reacts to what the drones do"])

chapter("game", "The game, and the blast")
plate("Put together, it is a game called Night Watch. Rovers come in waves, two more each time, up to six. Each has "
      "one hundred points of armour, and a hit takes about thirty. A kill scores one hundred, a combined kill one "
      "hundred and fifty. A rover that reaches its goal costs the base ten per cent.", "game", title="Night Watch: the rules")
plate("The blast itself is physics too. Hundreds of particles are thrown out with random speeds. Gravity pulls "
      "them down, drag slows them, they bounce once on the ground. Each one cools, and its colour follows its "
      "temperature: white, yellow, orange, red, then dark smoke.", "particles", title="An explosion is particles")
clip("The wreck burns, and the fire cools.", "n_wide", at=T["combined"] + 0.8, title="Blast, fire, smoke", min=6.0, level=-20.0)
clip("At dawn the rovers that are left withdraw, and the drones come home.", "f2:uav_landing", at=25.0, title="Dawn", min=8.0)
