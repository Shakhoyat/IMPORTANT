"""Part 3 (take control): buildings to walk into, everything that can be operated, points of view, the garrison."""
from dsl import *  # noqa: F401,F403

SC = IMG + "/scene/"

act("Control")
chapter("doors", "Buildings you can walk into")
card("So far I have only been looking at the outpost. But a world you can only look at is still a model. This one "
     "can be walked through, driven and flown.", "Part 3 | Take control", ["walk in, drive, fly,", "and see through any pair of eyes"])
plate("Under the soldiers there is a second, invisible map, made of the floors they may stand on, the walls that stop "
      "them, and the doorways that join one room to the next. A door is a gate in that map, and it opens when a "
      "soldier comes close and faces it.", "walkspace", title="The walk space")
clip("Here I am controlling a soldier as he walks down the ramp, and the blast gate opens for him.", "f3:f_bunker", at=3.6,
     title="A soldier under my control", min=7.0)
clip("We go through the tunnel and into the command room under the ground.", "f3:f_bunker", at=12.4, title="Tunnel and command room", min=6.5)
clip("Other doors are opened by hand with the E key, like this one to the generator room.", "f3:f_gen", at=2.0, title="E: open a door", key="E", min=6.0)
clip("And this is the barracks hall, where the soldiers who are off duty rest and train.", "f3:f_hall", at=2.6, title="The barracks hall", min=7.5)
free("walk", 20, 16.5, "1 4 36 1 1 20  1 4 36 1 1 20  50", tour="walk")
clip("Outdoors, a soldier walks with W, A, S and D, and sprints with Shift. The V key puts me behind his eyes in "
     "first person, and takes me back out again. He can also jump, crouch, aim and fire his rifle.", "walk", at=1.0,
     title="W A S D: walk   Shift: sprint   V: first or third person", min=13.0)

chapter("control", "Take control of anything")
plate("Twenty things in the camp can be operated. Here they are, and the rule is the same for every one of them. I "
      "select it, take control of it, and then its own keys work, for driving, aiming, firing and one special action. "
      "The V key changes the camera, from outside to the gunner's sight, and Escape hands the thing back to itself.",
      "control", title="Select, control, sight, release")
clip("Let us start with the tank. The soldier walks up to it and climbs in with E.", "f3:f_tank2", at=5.0, title="The tank", key="E", min=5.5)
clip("He drives it out of the bay, turns on the spot, and swings the turret onto the target.", "f3:f_tank2", at=10.8,
     title="Drive: W A S D   aim: arrows", min=8.5)
clip("And from the gunner's sight, he fires two rounds.", "f3:f_tank2", at=21.4, title="V: gunner's sight   Enter: fire", level=-19.0, min=7.4)
clip("The anti-aircraft gun has six barrels and fires bursts of tracer.", "f3:f_aa", at=11.4, title="Anti-aircraft gun", level=-19.0, min=5.0)
clip("And this is the surface-to-air missile launcher.", "f3:f_sam", at=17.6, title="Missile launcher", level=-19.0, min=4.4)
take("operate", 16.5, "38 6 -10 29 3 -3 50")
live("The number keys jump straight to a unit. Eight takes the radar, and the arrow keys swing its dish across the "
     "sector it scans.",
     [(0, "orbit 29 4.5 -3 11 5 8 200 50 cut"), ("@Eight-0.3", "possess radar", "8"), ("@arrow-0.2", "hold aimyaw 1 3.5"), ("100%-0.2", "release")],
     title="8: the radar", min=8.0)
live("Even the small things can be operated. This is the barrier at the gate, raised with Enter.",
     [(0, "orbit 2.9 1 31.6 8 3 6 0 50 cut"), (0.6, "possess barrier"), ("@raised-0.6", "action", "Enter"), ("100%-0.2", "release")],
     title="The gate barrier", min=7.0)
live("And nine takes the blast doors of the bunker, which close in about four seconds.",
     [(0.1, "possess blast", "9"), (1.6, "action"), ("100%-0.2", "release")], title="9: the blast doors", min=7.5)
take("heli", 16.6, "27 3 22 20.3 1.2 17 55")
live("Now the helicopter. Its special action starts the engine, the rotor spools up, and it lifts off, turns, and "
     "flies out over the fence.",
     [(0, "orbit 20.3 1.2 17 10 2.6 6 30 55 cut"), (1.2, "control helicopter -"), (1.6, "action", "\\"), (7.0, "hold lift 1 5"),
      (10.5, "hold forward 1 9"), (12.0, "hold turn 0.6 3"), (16.0, "hold lift 0.5 3")], title="The helicopter", min=17.5)
take("uavfly", 16.6, "4 9 20 0 7 12.5 50")
live("A drone can be flown off its pad by hand as well, and the V key steps through its three cameras. This is the "
     "view from behind it. This is the view from its nose. And this is the view through its thermal camera.",
     [(0.3, "control uav -"), (0.6, "set hud 1"), (0.6, "set game 1"), (3.2, "hold lift 1 4"), (6.0, "hold forward 1 9"), (8.0, "hold turn -0.5 3"),
      ("@This is the view from its nose-0.1", "view", "V"), ("@And this is the view through-0.1", "view", "V"), ("100%-0.3", "release"), ("100%-0.2", "set hud 0")],
     title="A drone: chase, nose camera, thermal camera", min=17.0)
take("rovdrive", 23.0, "-26 16 30 0 1 0 55", pre=5.2)
live("I can even change sides and drive one of the intruders myself. When I press Backspace, it goes back to its own "
     "planner and carries on with its mission.",
     [(-1.0, "set hud 1"), (-0.9, "control rover -"), (-0.3, "hold forward 1 7"), (2.4, "hold turn 0.6 2"), ("@Backspace+0.4", "release", "Backspace"),
      ("100%-0.2", "set hud 0")], title="Driving an intruder", min=10.0)

chapter("pov", "Jump into anything that moves")
plate("Every moving thing also has a point of view, each soldier, each vehicle, both drones, every intruder, and even "
      "a round in flight. To choose one I click on it, and the program has to work out what a flat click means in a "
      "world with depth. It runs the camera's mathematics backwards and turns the pixel into a ray, and the first "
      "object that the ray hits is the one I get.", "picking", title="A click becomes a ray")
take("povjump", 16.4, "-2 4 30 -0.8 1 21 50")
live("A soldier is walking his patrol. I move the pointer onto him, and click.",
     [(0, "set hud 1"), (0, "track soldier PATROL 6.5 2.6 7 20 48 cut"), (0.8, "cursor 0.2 0.25 0.1"), (1.6, "cursorto soldier PATROL 1.6"), ("100%-0.6", "click middle")],
     title="Click a soldier", min=7.0)
live("Now I see what he sees. He keeps walking his patrol, and I simply ride along in first person.", [], title="Through his eyes", min=7.5)
live("The V key steps back behind him, into third person.", [(0.6, "view", "V")], title="V: third person", min=6.0)
live("F4 lists everything that moves, with the nearest first, and I can click any row.",
     [(0.5, "picker open", "F4"), (1.6, "cursorrow 3 0.8"), (3.4, "click left")], title="F4: the list of points of view", min=8.0)
live("The same jump works for anything that moves. This is one of the tanks, seen from behind. This is the helicopter "
     "on its pad. And this is a drone, waiting for the night.",
     [(0.4, "cursor hide"), ("@This is one of the tanks-0.1", "pov tank MBT-1 chase"), ("@This is the helicopter-0.1", "pov helicopter - chase"), ("@And this is a drone-0.1", "pov uav UAV-1 chase")],
     title="Insert / Delete, or the Travel menu: any point of view", min=10.0)
free("v7a", 58, 23.0, "", TS_DIRECTOR_CUT="v7", TS_FILM_CLEAN=1)
clip("At night it becomes a game. The K key hands the camera to a director that cuts between these views by itself. "
     "Here it opens the list and goes into a drone's camera.", "v7a", at=17.2, title="K: the cinematic director", min=8.6, level=-24.0)
clip("A round is launched, and we ride it all the way down to the target.", "v7a", at=26.1, title="Riding a round", min=6.8, level=-21.0)
clip("Or we take the other side, inside an intruder while the drones hunt it.", "v7a", at=33.1, title="Into an intruder", min=5.8, level=-24.0)
clip("Or we stand with a sentry at the gate, behind his rifle.", "v7a", at=39.2, title="A sentry, first person", min=6.4, level=-21.0)

chapter("garrison", "The garrison's day")
plate("Nobody controls the soldiers. Each has a duty for the day shift and another for the night, and has to walk "
      "there by himself. He searches a grid with the A* algorithm, which always explores the most promising cell "
      "first, then pulls the zigzag path straight like a string, and finally rounds its corners into a smooth curve. "
      "On that curve he slows down wherever the bend is tight, as a person would.", "astar", title="A soldier finds his way")
plate("His legs are solved backwards, which is called inverse kinematics. The planted foot stays exactly where it is "
      "on the ground, so it never slides. The hip, the knee and the foot form a triangle whose two sides, the thigh "
      "and the shin, have fixed lengths, so the knee angle follows from the law of cosines. Meanwhile the other foot "
      "swings forward in an arc.", "gait", title="Walking: the foot decides, the knee follows")
take("garrison", 10.2, "12 3 -20 9.5 1 -23.8 50", pre=5.6)
live("By day each soldier has work of his own. This one walks through the bunker to his console in the command room. "
     "And outside, the sentries stand guard at the gate.",
     [(-4.6, "pov soldier MECHANIC chase"), (0.3, "pov soldier LOGISTICS chase"), ("@And outside-0.2", "track soldier SENTRY 6 2.0 8 20 46 cut")],
     title="The day shift", min=13.0)
clip("When the camp goes on alert, every weapon is manned. The missile canisters rise through the net.",
     "f3:f_al_sam", at=0.2, title="On alert", min=4.2, speed=0.75)
clip("The rocket truck raises its pod to firing elevation, and the gun searches its sector.", "f3:f_al_mlrs", at=0.2, title="On alert", min=4.0)
clip("And the tanks wait in their bays with their guns over the wall.", "f3:f_al_tank", at=0.8, title="On alert", min=3.4)
plate("One more piece of engineering sits under all of this. The world does not advance by whatever time a frame "
      "happened to take. It advances in fixed steps of one sixtieth of a second, and the picture is blended between "
      "the last two steps. So the motion stays smooth at any frame rate, and the same seed always gives exactly the "
      "same night, which is what lets me record a whole play session with F9 and replay it later, event for event.",
      "tick", title="A fixed tick, and a session you can replay")
