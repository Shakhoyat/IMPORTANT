"""Act 4 (the world that moves) and Act 5 (control)."""
from dsl import *  # noqa: F401,F403

SC = IMG + "/scene/"
AUD = ROOT + "/build/audio_v7/"

# ------------------------------------------------------------------------------------------------------- act 4
act("World")
chapter("day", "Day, night and weather")
card("Question four. A lit picture of a place is still only a picture. What makes it a world? Time, weather, "
     "sound, and things that go on without you.", "4  World", ["What turns a picture", "into a world?"], kind="act")
plate("Time first. The outpost stands at twenty-two point eight degrees north. From the hour, the program computes "
      "where the sun is. Low sun means a long path through the air, so the light turns orange and weaker. That one "
      "number, the height of the sun, drives the colour of the light, the sky, the lamps and the exposure.", "sun",
      title="One clock drives the light")
take("daylapse", 5.0, "-24 15 34 0 0.5 2 55")
live("Here is one whole day in the program, in twenty-five seconds. Dawn at about six. The day shift comes out. "
     "Noon. The long light of the afternoon. At dusk the lamps come on, the drones lift off, and night falls.",
     [(0, "orbit 0 0.5 2 40 15 3 125 55 cut"), (0.2, "scrub 5.0 23.2 22")], title="A day in 25 seconds", min=25.0)
take("weather", 17.3, "8 2.2 26 0 0.8 12 50")
live("The R key brings rain: drops, darker and shinier ground. And at night, J switches the fireflies and the "
     "dust in the air.",
     [(0, "orbit 0 0.8 12 15 3 4 60 50 cut"), (0.8, "set rain 1", "R"), ("55%", "set rain 0"), ("58%", "hour 21.5"), ("70%", "set particles 0", "J"),
      ("86%", "set particles 1", "J")], title="R: rain   J: fireflies", min=14.0)

chapter("veg", "Vegetation, scale and speed")
plate("The ground is covered by four hundred and fifty thousand blades of grass. That would be far too many separate "
      "objects. One blade, seven corners and five triangles, is sent to the graphics card once, with a list of "
      "places to draw it. The trees at the two highest presets are the one thing that is not generated: they are "
      "public-domain meshes. Far away they are drawn simpler, and the farthest as flat pictures.", "instancing",
      title="One blade, drawn 450,000 times")
take("veg", 16.5, "-5.4 1.6 17.6 -1 1.0 9 50")
live("Down in the grass. Every blade bends in the wind. F5 steps through the quality presets, from the lowest, "
     "which runs on a laptop, up to the highest.",
     [(0, "cam -4.2 1.25 15.4 -1 0.9 8 50 12"), ("30%", "set quality low", "F5"), ("48%", "set quality high", "F5"), ("66%", "set quality ultra", "F5"),
      ("84%", "set quality epic", "F5")], title="F5: quality LOW, HIGH, ULTRA, EPIC", min=14.0)
plate("Measured on the test machine, the same scene costs this much time per frame at each preset.", "bench",
      title="Frame time per preset", min=7.0)

chapter("sound", "Sound made by code")
plate("There are no sound files either. A cannon shot is a burst of noise through a filter, with a low thump. A "
      "rotor is a stack of tones at the rate the blades pass. Far sounds arrive late, because sound is slow, and "
      "dull, because air swallows the high tones. Loud shots echo off the wall and the treeline. Inside the bunker "
      "the same shot rings in a concrete room.", "sound", title="Every sound is computed",
      sounds=[("s1", AUD + "v7_cannon_echoes.wav", 0.5), ("s2", AUD + "v7_helicopter_flyby.wav", 0.4), ("s5", AUD + "v7_bunker_vs_open_reverb.wav", 0.5)])

# ------------------------------------------------------------------------------------------------------- act 5
act("Control")
chapter("doors", "Buildings you can walk into")
card("Question five. Can you go in? A world you can only look at is a model. This one can be walked, driven and "
     "flown.", "5  Control", ["Can you walk in,", "drive it, fly it?"], kind="act")
plate("Under the soldiers there is a second, invisible map: the floors they may stand on, the walls that stop them, "
      "and the doorways that join rooms. A door is a gate in that map. It opens when a soldier is close and facing it.",
      "walkspace", title="The walk space")
clip("Here a soldier under the player's control walks down the ramp. The blast gate opens for him.", "f3:f_bunker", at=3.6, title="A soldier under your control", min=7.0)
clip("Through the tunnel, into the command room under the ground.", "f3:f_bunker", at=12.4, title="Tunnel and command room", min=7.0)
clip("Other doors are opened by hand, with the E key: the generator room.", "f3:f_gen", at=2.0, title="E: open a door", key="E", min=6.0)
clip("And the barracks hall, where the soldiers who are off duty rest and train.", "f3:f_hall", at=2.6, title="The barracks hall", min=10.5)

chapter("control", "Take control of anything")
plate("Twenty things in the camp can be operated. The rule is always the same. Select it, take control, and its "
      "own keys work: drive, aim, fire, a second action. V changes the camera, from outside to the gunner's sight. "
      "Escape gives it back.", "control", title="Select, control, sight, release")
clip("The tank. He walks up and gets in with E.", "f3:f_tank2", at=5.0, title="The tank", key="E", min=5.5)
clip("Out of the bay, a turn on the spot, and the turret swings to the target.", "f3:f_tank2", at=10.8, title="Drive: W A S D   aim: arrows", min=10.0)
clip("From the gunner's sight: two rounds.", "f3:f_tank2", at=21.4, title="V: gunner's sight   Enter: fire", level=-19.0, min=8.2)
clip("The anti-aircraft gun: six barrels, bursts of tracer.", "f3:f_aa", at=11.4, title="Anti-aircraft gun", level=-19.0, min=5.0)
clip("The surface-to-air missile launcher.", "f3:f_sam", at=17.6, title="Missile launcher", level=-19.0, min=4.4)
take("heli", 16.6, "27 3 22 20.3 1.2 17 55")
live("The helicopter. Its second action starts the engine. The rotor spools up, and it lifts off, turns and flies "
     "out over the fence.",
     [(0, "orbit 20.3 1.2 17 10 2.6 6 30 55 cut"), (1.2, "control helicopter -"), (1.6, "action", "\\"), (7.0, "hold lift 1 5"),
      (10.5, "hold forward 1 9"), (12.0, "hold turn 0.6 3"), (16.0, "hold lift 0.5 3")], title="The helicopter", min=20.0)
take("uavfly", 16.6, "4 9 20 0 7 12.5 50")
live("A drone, taken off its pad by hand. V steps through its three cameras: behind it, from its nose, and "
     "through its thermal camera.",
     [(0.3, "control uav -"), (0.6, "set hud 1"), (0.6, "set game 1"), (3.2, "hold lift 1 4"), (6.0, "hold forward 1 9"), (8.0, "hold turn -0.5 3"),
      ("52%", "view", "V"), ("76%", "view", "V"), ("100%-0.3", "release"), ("100%-0.2", "set hud 0")], title="A drone: chase, nose camera, thermal camera", min=17.0)

chapter("pov", "Jump into anything that moves")
plate("Every moving thing has a point of view: each soldier, each vehicle, both drones, every intruder, even a "
      "round in flight. A click on the screen is turned into a ray from the camera. The first object the ray hits "
      "is the one you get.", "picking", title="A click becomes a ray")
take("povjump", 16.4, "-2 4 30 -0.8 1 21 50")
live("A soldier is walking his patrol. Move the pointer onto him and click.",
     [(0, "set hud 1"), (0, "track soldier PATROL 6.5 2.6 7 20 48 cut"), (0.8, "cursor 0.2 0.25 0.1"), (1.6, "cursorto soldier PATROL 1.6"), ("100%-0.6", "click middle")],
     title="Click a soldier", min=7.0)
live("Now you see what he sees. He keeps walking his patrol, and you ride along, in first person.", [], title="Through his eyes", min=7.5)
live("V steps back behind him, into third person.", [(0.6, "view", "V")], title="V: third person", min=6.0)
live("F4 lists everything that moves, nearest first. Click a row.",
     [(0.5, "picker open", "F4"), (1.6, "cursorrow 3 0.8"), (3.4, "click left")], title="F4: the list of points of view", min=8.0)
live("The same jump works for anything that moves. A tank. The helicopter. A drone on its pad.",
     [(0.4, "cursor hide"), (0.6, "pov tank MBT-1 chase"), ("38%", "pov helicopter - chase"), ("70%", "pov uav UAV-1 chase")],
     title="Insert / Delete, or the Travel menu: any point of view", min=10.0)
free("v7a", 58, 23.0, "", TS_DIRECTOR_CUT="v7", TS_FILM_CLEAN=1)
clip("At night it becomes a game. The list again, and into a drone's camera.", "v7a", at=17.2, title="F4 at night", key="F4", min=8.6, level=-24.0)
clip("A round is launched, and you ride it down to the target.", "v7a", at=26.1, title="Riding a round", min=6.8, level=-21.0)
clip("Or take the other side: into an intruder, while the drones hunt it.", "v7a", at=33.1, title="Into an intruder", min=5.8, level=-24.0)
clip("Or a sentry at the gate, with his rifle.", "v7a", at=39.2, title="A sentry, first person", min=6.4, level=-21.0)

chapter("garrison", "The garrison's day")
plate("The soldiers are not controlled by anyone. Each has a duty for the day shift and one for the night. To walk "
      "there, he searches a grid for the shortest way round the obstacles, straightens the result, and rounds the "
      "corners into a curve. He slows down where the curve is tight.", "astar", title="A soldier finds his way")
plate("His legs are solved backwards. The planted foot stays where it is on the ground. From the hip and the foot, "
      "the knee angle follows from the lengths of thigh and shin. The other foot swings forward in an arc.", "gait",
      title="Walking: the foot decides, the knee follows")
take("garrison", 10.2, "12 3 -20 9.5 1 -23.8 50")
live("By day: a mechanic works at a parked tank. Another carries crates from the depot. Sentries stand at the gate.",
     [(0, "track soldier MECHANIC 5 1.8 2.5 205 46 cut"), ("36%", "track soldier LOGISTICS 10 6.5 6 200 46 cut"), ("70%", "track soldier SENTRY 6 2.0 8 20 46 cut")],
     title="The day shift", min=13.0)
clip("When the camp is on alert, the weapons are manned and ready. Missile canisters raised through the net.", "f3:f_al_sam", at=0.2, title="On alert", min=4.2, speed=0.75)
clip("The rocket pod at firing elevation. The gun searching its sector.", "f3:f_al_mlrs", at=0.2, title="On alert", min=4.0)
clip("Tanks in their bays with their guns over the wall.", "f3:f_al_tank", at=0.8, title="On alert", min=3.4)
card("Compared: a scripted film of the camp, or a simulation. Chosen: a simulation. Nothing you have seen is "
     "an animation played back. Soldiers, doors, vehicles and time all run by rules, and you can step into any of them.",
     "Decision", ["compared   scripted scenes  /  a running simulation", "chosen     a simulation with 20 things you can operate",
                  "why        you can check every claim by trying it"])
