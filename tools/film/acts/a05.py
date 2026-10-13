"""Part 4 (machine vision) and Part 5 (the agents: drones, intruders, the game)."""
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

# ------------------------------------------------------------------------------------------------------- part 4
act("Vision")
chapter("sensor", "The thermal camera")
card("Now to the algorithms. The drones have to find an intruder in the dark without any help from me. So what does "
     "a machine actually see, and how does it decide that a warm spot is a target?",
     "Part 4 | Machine vision", ["from a picture of heat", "to a target that is tracked"])
plate("A thermal camera does not see light. It sees heat. So every part in the camp is given a temperature, where an "
      "engine is hot, a soldier is warm, and the ground is cool and slowly warmed by the sun. A rover even leaves warm "
      "tyre tracks behind it, which fade as the ground cools. A real sensor is far from perfect, and I model that as "
      "well, so the lens blurs the picture, every pixel has its own small error, whole columns show up as stripes, "
      "there is noise, and from time to time a shutter closes so that the camera can calibrate itself again.",
      "sensor", title="A camera that sees heat, with its faults")
take("thermal", 22.6, "-14 9 26 0 1 6 50")
live("The F key switches the whole view to thermal, where hot is bright, and the T key changes the palette.",
     [(0, "set fpv 0"), (0, "orbit 0 1 6 26 10 4 110 50 cut"), (1.0, "set view thermal", "F"), ("@T key-0.2", "set palette 1", "T"), ("@T key+1.6", "set palette 2", "T"),
      ("100%-0.5", "set palette 0", "T")], title="F: thermal view   T: palette", min=9.0)
live("And this is what a drone's own camera sends down, three hundred and twenty by two hundred and forty pixels, "
     "with all of its noise. The Q key makes the sensor calibrate itself, and for a moment its shutter closes. The "
     "mouse wheel zooms the lens in three steps. This is the wide view of forty degrees. This is twenty degrees. And "
     "this is eight degrees, for a close look at a target.",
     [(0.2, "set hud 1"), (0.3, "set fpv 1"), ("@its shutter closes-0.3", "nuc", "Q"), ("@This is twenty-0.1", "set zoom 20", "Wheel"),
      ("@And this is eight-0.1", "set zoom 8", "Wheel"), ("100%-0.2", "set zoom 40")],
     title="The drone's camera: 320 x 240, recalibration, three zoom steps", min=16.0)

chapter("cv", "From a heat picture to a target")
plate("Every frame of that picture goes through the same five steps. First comes a threshold, which keeps only the "
      "pixels above eighty per cent heat, or above the level that best separates the dark part of the picture from "
      "the bright part, whichever is higher. Second, single stray pixels are removed, because noise is never a "
      "vehicle. Third, neighbouring hot pixels are joined into regions. Fourth, each region is measured. And fifth, "
      "the program decides what it is.", "cvsteps", title="Five steps, every frame")
plate("That automatic level deserves a closer look. If I count how many pixels there are of each brightness, a night "
      "picture shows two hills, a large one for the cold background and a small one for the warm things. Otsu's "
      "method tries every possible cut between them and keeps the one that leaves each side as tightly packed as "
      "possible. So the threshold adapts by itself when the night grows warmer or colder.", "otsu",
      title="Otsu's method: let the picture choose its own threshold")
plate("Measuring a region gives its area, its centre, its hottest pixel, and how stretched it is. Then a simple rule "
      "decides. A small region with a very hot core that is not too stretched is a vehicle, and therefore a target. A "
      "warm region without a hot core, upright or compact, is a person, and a person is never fired at. Anything "
      "else, like a wall warmed by the sun, is clutter.", "classify", title="Target, person or clutter")
still("By day I can check this rule. Through the drone's camera the soldiers are boxed as persons, and not one of "
      "them ever becomes a target.", [IMG + "/scene/day_thermal_fpv_persons.jpg"], title="Soldiers are marked as persons", min=7.0)

chapter("track", "Tracking")
plate("One detection is not enough, because the camera shakes, pixels flicker, and a rover drives behind a tree. So "
      "each target becomes a track, which is a guess of its position and speed, together with how unsure that guess "
      "is. Between frames the track is moved ahead by its speed, and the uncertainty grows, like walking with your "
      "eyes closed. When a new detection arrives, it pulls the guess back, strongly if the track was unsure and only "
      "gently if it was confident, and the uncertainty shrinks again. This cycle of predict and correct is the Kalman "
      "filter.", "kalman", title="Predict, then correct")
plate("With several targets there is also a matching problem, namely which detection belongs to which track? Only a "
      "detection inside a track's gate may match it, and among those the Hungarian method picks the pairing with the "
      "smallest total distance. And when both drones see the same target, their two lines of sight are crossed, which "
      "fixes its position far better than a single camera can.", "assoc", title="Gating, matching, two views")
clip("In the drone's view you can watch this happen. The boxes are tracks. When the rover goes behind cover, its track "
     "coasts on the prediction alone, and it is picked up again when the rover comes out.", "n_flir", at=T["hide"] - 3.0,
     title="Tracks coast behind cover", min=10.0, level=-27.0)
plate("Here is that same night as numbers, the uncertainty of one track over time. It jumps while the rover is "
      "hidden, and falls back each time a camera sees it again.", "tracklog", title="One track's uncertainty, measured", min=8.0)

# ------------------------------------------------------------------------------------------------------- part 5
act("Agents")
chapter("uav", "The drones")
card("Seeing is only half of it. Three kinds of things in the camp act on their own, the drones, the intruders and "
     "the soldiers. So let us look at the rules that make them behave as if they had a plan.",
     "Part 5 | The agents", ["how the drones, the intruders", "and the soldiers decide"])
plate("A drone has a duty for each part of the day. By day it sits docked on its pad. At dusk its rotors spool up "
      "and it takes off. At night the two of them patrol on opposite sides of a circle, so that together they always "
      "see the whole camp. And at dawn they return, hover over their pads, and land.", "duty", title="A drone's day")
clip("Here is dusk. The lamps come on, and both drones lift off with their searchlights.", "f2:uav_takeoff", at=0.6, title="Take-off at dusk", min=7.0)
clip("And here they are on patrol, with their searchlights sweeping the camp.", "n_wide", at=0.8, title="Night patrol", min=5.0)

chapter("strike", "One drone, or both?")
plate("When a track is confirmed, the nearer drone becomes the lead and the other becomes its wing, and they take up "
      "positions about ninety degrees apart, so that one of them almost always has a clear view. Before firing, the "
      "lead must hold its lock for eight tenths of a second and the wing for half a second, each with nothing in its "
      "line of fire. If there is only one intruder, the lead waits up to eight seconds for the wing, so that both can "
      "fire.", "decide1", title="Lead and wing")
plate("They do not fire at the same moment. The round from the farther drone needs longer to arrive, so that drone "
      "fires first, by exactly the difference in flight time. Both rounds then arrive together, which is called time "
      "on target, and it leaves a rover no chance to escape between two hits. Each round also steers itself on the "
      "way, turning towards its target at a limited rate, and goes off when it comes within half a metre.", "tot",
      title="Time on target")
clip("Here it is. One intruder is left, both drones hold their locks, and both rounds arrive together.", "n_follow",
     at=T["combined"] - 4.2, title="Combined strike", min=8.5, level=-20.0)
plate("Now a second intruder appears. If it is more than fourteen metres from the first, and it is close to "
      "something it could damage, then waiting for a combined strike would cost too much. So the pair splits. The "
      "nearer drone takes the second target and strikes alone. When that target is gone, or the two targets come "
      "within nine metres of each other, the pair forms again.", "decide2", title="Two intruders: split, or stay together?")
clip("This is the same night, a little earlier. Two intruders are far apart, the pair splits, and one drone fires "
     "alone.", "n_follow", at=T["single"] - 4.0, title="Single strike", min=8.0, level=-20.0)
clip("The guards join in as well. A soldier at his post opens fire on any tracked intruder within twenty-six metres.",
     "n_uav1", at=T["single"] + 5.5, title="The guards fire too", min=6.5, level=-22.0)

chapter("rover1", "The intruder: choosing a route")
plate("Now let us change sides. An intruder is a small rover that wants to reach something in the camp without being "
      "seen, and to plan its way it lays a grid over the ground, eighty by eighty cells of half a metre each. Cells "
      "inside obstacles are blocked.", "rgrid", title="The intruder's map")
plate("Every free cell then gets a price. It costs one just to step on it. It costs more if a drone can see it, and "
      "to find that out, the rover draws a line from each drone to the cell and checks whether anything is in the "
      "way. And each rover adds a little random cost of its own, so that no two rovers choose the same way.", "rcost",
      title="What a cell costs")
plate("Then comes a trick from the MicroMouse maze-solving robots. I start at the goal with zero and let the numbers "
      "flood outwards like water, each cell taking its cheapest neighbour plus its own price. When the flood reaches "
      "the rover, every cell knows how expensive the rest of the journey is. So the route needs no search at all, "
      "because the rover simply steps to the neighbour with the smaller number, again and again.", "rflood",
      title="Flood fill from the goal")
take("rovplan", 23.0, "-26 16 30 0 1 0 55", algo=True)
live("The M key shows this inside the program, with the flood field as coloured cells and the route as a line, and "
     "each rover has its own.", [(0, "set plan 1", "M"), (0, "cam 2 26 6 2 0 4 55 2.5"), ("100%-0.3", "set plan 0")], title="M: the flood field and the route", min=9.0)

chapter("rover2", "The intruder: following the line")
plate("A route is not yet a movement. The rover follows it the way a line-follower robot follows a black line on the "
      "floor. Five sensors across its nose each report whether they are over the line. Their weighted average is the "
      "error, which tells the rover how far the line lies to one side. A controller then turns that error, together "
      "with how fast it is changing, into a turn rate, much as a driver watches both where the lane is and how "
      "quickly the car is drifting, and the two wheels are driven at different speeds.", "lfr", title="Five sensors, two wheels")
take("rovclose", 23.0, "-26 16 30 0 1 0 55")
live("Here is one of them from above, shown in thermal so that it stands out against the cold ground. It weaves a "
     "little, changes its speed, and sometimes stops, so that a tracker cannot simply extend a straight line.",
     [(0, "set plan 1"), (0, "set fpv 0"), (0.1, "track rover - 3.0 4.5 8 40 42 cut"), (0.1, "set view thermal")], title="A rover on its line", min=10.0)
live("And this is the view from the camera on its own mast.", [(0.2, "set view visible"), (0.2, "pov rover - alt"), ("100%-0.2", "set plan 0")],
     title="The rover's camera", min=6.0)

chapter("rover3", "The intruder: hearing a drone and hiding")
plate("And the rover listens. When a drone comes within five metres, it drops what it was doing. Its goal is no "
      "longer the camp. Its goal becomes every cell that no drone can see, under a tree, behind a wall, or under a "
      "net. The same flood fill runs again, this time from all of those cells at once, and the rover drives to the "
      "nearest one. There it waits four seconds and then plans again, because by then the drones have moved.",
      "revade", title="Hiding from a drone")
split("Here are three views of the same few seconds. On the left is the drone's thermal camera. In the middle is "
      "the rover's own map, red where a drone can see, dark where it is hidden, with its route in yellow. On the right "
      "is the wide view. Watch the route bend into the shadow as the drone comes near.",
      [("n_flir", T["hide"] - 5.0), ("anim:rovermap", T["hide"] - 5.0), ("n_follow", T["hide"] - 5.0)],
      labels=["the drone's thermal camera", "the rover's map", "the camp"], title="One moment, three views", min=18.0)

chapter("game", "The game, and the blast")
plate("Put together, all of this becomes a game called Night Watch. Rovers come in waves, two more each time, up to "
      "six. Each has one hundred points of armour, and a hit takes about thirty. A kill scores one hundred points, "
      "and a combined kill one hundred and fifty. A rover that reaches its goal costs the base ten per cent.", "game",
      title="Night Watch: the rules")
take("hud", 23.0, "-26 16 30 0 1 0 55", pre=5.0)
live("The game's display shows the wave and the score at the top, the health of the base as a green bar, the clock, "
     "and a small radar on which every tracked intruder appears. And with the U key, the picture from a drone's "
     "thermal camera is added in the corner.",
     [(-4.5, "set hud 1"), (-4.5, "set game 1"), ("@And with the U key-0.1", "set game 0"), ("@And with the U key-0.1", "set feed 1", "U"),   # the feed belongs to the standard display
      ("100%-0.3", "set feed 0"), ("100%-0.2", "set hud 0")],
     title="F11: the game display   U: the drone's feed", min=12.0)
take("dayops", 10.5, "-24 17 34 0 0.5 2 55", pre=9.0)
live("With Shift and F2 the same game runs in daylight. The intruders come by day, and the drones scramble from "
     "their pads to meet them.",
     [(-8.5, "dayops 1"), (-8.6, "orbit 0 0.5 2 44 19 3 150 55 cut")], title="Shift+F2: day operations", min=10.0)
plate("The blast itself is physics too. Hundreds of particles are thrown out with random speeds. Gravity pulls them "
      "down, drag slows them, and they bounce once on the ground. Each one cools as it flies, and its colour follows "
      "its temperature, from white through yellow, orange and red to dark smoke.", "particles", title="An explosion is particles")
clip("Afterwards the wreck burns, and the fire slowly cools.", "n_wide", at=T["combined"] + 0.8, title="Blast, fire, smoke", min=6.0, level=-20.0)
clip("At dawn the rovers that are left withdraw, and the drones come home.", "f2:uav_landing", at=25.0, title="Dawn", min=7.0)
