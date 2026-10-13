"""Opening, orientation, and Part 1 (the graphics lab): primitives, transformations, the scene graph."""
from dsl import *  # noqa: F401,F403

M3 = IMG + "/m3/"
SH = IMG + "/shapes/"

# ------------------------------------------------------------------------------------------------------- opening
act("")
chapter("open", "ThermalSense")
card("Hi, this is Shakhoyat, roll 2107104, from CSE 2K21. I am presenting the full functionality of my Computer "
     "Graphics project, with an AI voice. Sorry for that! I hope you enjoy the depth of the project.",
     "Hi, this is Shakhoyat", ["Md. Shakhoyat Rahman Shujon   ·   Roll 2107104   ·   CSE 2K21",
                               "Computer Graphics Laboratory (CSE 4102), KUET",
                               "Submitted to  Md Tajmilur Rahman  and  Md. Mubtashim Abrar Nihal",
                               "narrated with an AI voice (sorry for that :3)"], kind="intro", min=17.0)
clip("This is ThermalSense: a military outpost at night, watched by two drones that carry thermal cameras.",
     "f3:f_night_wide", at=1.0, min=5.5)
clip("When an intruder comes, the drones find it, track it, and fire so that both of their rounds arrive together.",
     "f3:f_night_follow", at=15.6, min=6.0)
clip("And none of this is a recording. I can climb into this tank myself, drive it, and fire.", "f3:f_tank2", at=22.6, min=5.0)
card("In this video I will first go through every topic of our graphics lab, one by one, and show each of them inside "
     "this scene. After that I will show the more advanced techniques, and the algorithms that let the outpost think "
     "for itself.", "ThermalSense", ["from one triangle to an autonomous outpost", "", "first: every topic of the graphics lab",
                                     "then: advanced rendering, a living world, and its algorithms"], kind="title")
plate("One note before we start. A scene of this size cannot be managed from memory. So while I was building it, I "
      "recorded every point of every object in an Excel workbook. It has six thousand and sixty-nine components, each "
      "with its position in the world, and every number you will hear in this video comes from that workbook.",
      "workbook", title="The workbook behind the project")

# ------------------------------------------------------------------------------------------------------- orientation
act("Orientation")
chapter("what", "The outpost")
plate("Let me first show you the place, a military outpost about seventy metres across. A command bunker sits "
      "under the ground in the middle. Around it there are a helipad, a radar, an anti-aircraft gun, missile and rocket "
      "launchers, three tanks, a barracks hall, guard towers, and a fence with a single gate.", "map", title="The outpost, from above")
take("tour0", 16.5, "0 30 62 0 0 4 55")
live("Here it is, running. The camera is free, so I can fly wherever I like. Soldiers live and work here, two drones "
     "wait on their pads, and every lamp, door and vehicle really works.",
     [(0, "orbit 0 0 4 52 24 4 95 55 cut")], title="An open world: fly anywhere")
live("Let me come down to the road: in through the gate, past the flag, and towards the ramp that leads under the ground.",
     [(0, "cam 3 4 44 0 1.5 26 55 3.5"), (3.6, "cam 1.5 2.4 24 0 0.2 8 55 6")], title="An open world: fly anywhere")
live("The Escape key opens a menu. From here I can travel to any unit, set the time of day, change the view, and "
     "adjust the settings. The help screen lists more than a hundred and twenty keys.",
     [(0.3, "set hud 1", "Esc"), (0.4, "menu open"), (2.6, "menu tab 1"), (4.6, "menu tab 2"), (6.6, "menu tab 3"),
      (8.8, "menu close"), (9.0, "set help 1", "F1"), ("100%-0.4", "set help 0"), ("100%-0.3", "set hud 0")],
     title="Menu and help", min=12.5)
plate("Here is the plan of the video. Part one is the graphics lab, from primitives and transformations, through "
      "viewing, clipping and rasterisation, to lighting, shading, texture and ray tracing. Part two goes beyond the "
      "lab, to the techniques that make the picture look real and the world feel alive. In part three you take control "
      "of the outpost. And parts four and five are about its algorithms, how a drone sees and tracks, and how the "
      "drones, the intruders and the soldiers decide what to do.", "roadmap", title="The plan of the video")

# ------------------------------------------------------------------------------------------------------- part 1
act("Geometry")
chapter("prims", "Primitives: from a triangle to sixteen shapes")
card("Let us begin with the lab. Everything starts from the only shape a graphics card can really draw: a triangle.",
     "Part 1 | The graphics lab", ["every topic of the course,", "shown inside the outpost"])
plate("A triangle has three corners, and each corner stores a position and a normal. Two triangles make a square face. "
      "Six faces make a cube. The two triangles of a face share their corners, so I store each corner once and refer "
      "to it by an index: twenty-four corners and thirty-six indices for the whole cube. That is not a small saving. "
      "With indices the full scene takes about twelve milliseconds per frame, and without them fifty-three.",
      "triangle", title="Vertices, indices, triangles")
plate("A curved shape is the same idea with more pieces, so I cut a sphere into slices, like an orange, and into "
      "stacks from pole to pole. With eight slices it is a rough ball. With sixteen it is better. With thirty-six slices and "
      "eighteen stacks it has one thousand two hundred and twenty-four triangles, and it finally looks round. Two short "
      "formulas give the number of corners and triangles for any choice.",
      "sphere", title="Tessellation: how round is round enough")
plate("The whole outpost is built from only sixteen primitives like these. First the boxes, which are the cube, the "
      "cuboid, the wedge, the tapered box and the pyramid. Then the round shapes, the sphere, the hemisphere and the "
      "capsule. Then the shapes turned about an axis, the cylinder, the frustum and the cone. Then the rings, a torus, "
      "an annulus and a helix. And finally two special ones, a single blade of grass and the ground itself.", "primgrid", title="The sixteen primitives")
take("prims", 16.5, "-22 15 34 0 0.5 2 55")
live("Inside the program, the F key recolours every part by the primitive it is made of. Blue is a box, green is a "
     "cylinder, orange is a sphere. The camp has five thousand four hundred and seventy-four parts, and every one of "
     "them is one of the sixteen.",
     [(0, "orbit 0 0.5 2 40 17 5 125 55 cut"), (1.6, "set view prims", "F")], title="F: which primitive made each part")
live("The G key pulls every object apart into its primitives, and then puts it back together.",
     [(0.6, "set explode 1", "G"), ("100%-1.6", "set explode 0", "G")], title="G: explode into parts", min=7.0)
live("And the X key shows the triangles themselves, first with the hidden lines removed, and then as an x-ray.",
     [(0.3, "set view visible"), (0.6, "set edges hidden", "X"), ("55%", "set edges xray", "X"), ("100%-0.5", "set edges solid")],
     title="X: wireframe", min=8.0)
still("So an object is simply a list of parts. This drone has one hundred and fifty-eight of them: first the body, "
      "then the arms, the motors and rotors, and finally the camera, the searchlight and the rounds on their rails.",
      [M3 + "build_autonomous_uav_%d.jpg" % p for p in (12, 35, 65, 100)], title="A drone, part by part")

chapter("trs", "Transformations and the scene graph")
plate("Every primitive starts one unit big, sitting at the origin. One matrix turns it into a real part. I scale it, "
      "then rotate it, then move it into place. The order matters, just as putting on socks and shoes does. Scale, "
      "rotate, translate puts a drone arm exactly where it belongs, but if I translate first and rotate afterwards, the "
      "arm swings away in a circle round the origin.", "trs", title="M = T R S, and why the order matters")
plate("Parts also hang on each other, like the bones of an arm. A rotor blade is placed relative to its hub. The hub "
      "is placed relative to the arm, and the arm relative to the body. The world matrix of the blade is the product "
      "of all of them, parent first. So when I turn the hub, the blade follows without a single extra line of code.",
      "graph", title="The scene graph: parent times child")
take("graph", 16.5, "3 8.6 16 0 7.5 12.5 40")
live("The H key draws the local axes of these nodes. On a drone you can see the frames nested inside each other: the "
     "body, and inside it the two frames of the camera mount.",
     [(0, "set flare 0"), (0, "track uav UAV-1 1.9 0.75 9 30 40 cut"), (1.0, "set axes 1", "H")], title="H: the axes of the nodes")
live("The radar is a hierarchy in motion: the mount turns, and the dish tilts inside the frame that is turning.",
     [(0, "orbit 29 4.5 -3 11 5 8 200 50 cut")], title="A hierarchy in motion", min=6.5)
live("A soldier is the deepest hierarchy in the project: seventy-five parts on seven levels, from the hip to the "
     "thigh, the shin and the foot. When he walks, only the joint angles change.",
     [(0, "track soldier PATROL 3.4 1.5 14 30 45 cut"), (0.1, "axes soldier PATROL"), ("100%-0.4", "axes off")], title="A soldier: 75 parts, 7 levels", min=9.0)
