# ThermalSense: the film script

Narration and shot list of the full-length film, written out from `tools/film/acts/*.py` by `tools/film/build.py plan`.
Length 39:35. Times are minutes:seconds from the start.

### 0:00  ThermalSense

| Time | Picture | Narration |
|---|---|---|
| 0:00 | card: Hi, this is Shakhoyat | Hi, this is Shakhoyat, roll 2107104, from CSE 2K21. I am presenting the full functionality of my Computer Graphics project, with an AI voice. Sorry for that! I hope you enjoy the depth of the project. |
| 0:18 | program: `f3:f_night_wide` | This is ThermalSense: a military outpost at night, watched by two drones that carry thermal cameras. |
| 0:26 | program: `f3:f_night_follow` | When an intruder comes, the drones find it, track it, and fire so that both of their rounds arrive together. |
| 0:33 | program: `f3:f_tank2` | And none of this is a recording. I can climb into this tank myself, drive it, and fire. |
| 0:39 | card: ThermalSense | In this video I will first go through every topic of our graphics lab, one by one, and show each of them inside this scene. After that I will show the more advanced techniques, and the algorithms that let the outpost think for itself. |
| 0:53 | plate `workbook`<br>*The workbook behind the project* | One note before we start. A scene of this size cannot be managed from memory. So while I was building it, I recorded every point of every object in an Excel workbook. It has six thousand and sixty-nine components, each with its position in the world, and every number you will hear in this video comes from that workbook. |

## Orientation

### 1:14  The outpost

| Time | Picture | Narration |
|---|---|---|
| 1:14 | plate `map`<br>*The outpost, from above* | Let me first show you the place, a military outpost about seventy metres across. A command bunker sits under the ground in the middle. Around it there are a helipad, a radar, an anti-aircraft gun, missile and rocket launchers, three tanks, a barracks hall, guard towers, and a fence with a single gate. |
| 1:33 | program: take `tour0`<br>*An open world: fly anywhere* | Here it is, running. The camera is free, so I can fly wherever I like. Soldiers live and work here, two drones wait on their pads, and every lamp, door and vehicle really works. |
| 1:47 | program: take `tour0`<br>*An open world: fly anywhere* | Let me come down to the road: in through the gate, past the flag, and towards the ramp that leads under the ground. |
| 1:54 | program: take `tour0`<br>*Menu and help*<br>keys: `Esc` `F1` | The Escape key opens a menu. From here I can travel to any unit, set the time of day, change the view, and adjust the settings. The help screen lists more than a hundred and twenty keys. |
| 2:06 | plate `roadmap`<br>*The plan of the video* | Here is the plan of the video. Part one is the graphics lab, from primitives and transformations, through viewing, clipping and rasterisation, to lighting, shading, texture and ray tracing. Part two goes beyond the lab, to the techniques that make the picture look real and the world feel alive. In part three you take control of the outpost. And parts four and five are about its algorithms, how a drone sees and tracks, and how the drones, the intruders and the soldiers decide what to do. |

## Geometry

### 2:39  Primitives: from a triangle to sixteen shapes

| Time | Picture | Narration |
|---|---|---|
| 2:39 | card: Part 1 | The graphics lab | Let us begin with the lab. Everything starts from the only shape a graphics card can really draw: a triangle. |
| 2:46 | plate `triangle`<br>*Vertices, indices, triangles* | A triangle has three corners, and each corner stores a position and a normal. Two triangles make a square face. Six faces make a cube. The two triangles of a face share their corners, so I store each corner once and refer to it by an index: twenty-four corners and thirty-six indices for the whole cube. That is not a small saving. With indices the full scene takes about twelve milliseconds per frame, and without them fifty-three. |
| 3:14 | plate `sphere`<br>*Tessellation: how round is round enough* | A curved shape is the same idea with more pieces, so I cut a sphere into slices, like an orange, and into stacks from pole to pole. With eight slices it is a rough ball. With sixteen it is better. With thirty-six slices and eighteen stacks it has one thousand two hundred and twenty-four triangles, and it finally looks round. Two short formulas give the number of corners and triangles for any choice. |
| 3:39 | plate `primgrid`<br>*The sixteen primitives* | The whole outpost is built from only sixteen primitives like these. First the boxes, which are the cube, the cuboid, the wedge, the tapered box and the pyramid. Then the round shapes, the sphere, the hemisphere and the capsule. Then the shapes turned about an axis, the cylinder, the frustum and the cone. Then the rings, a torus, an annulus and a helix. And finally two special ones, a single blade of grass and the ground itself. |
| 4:10 | program: take `prims`<br>*F: which primitive made each part*<br>keys: `F` | Inside the program, the F key recolours every part by the primitive it is made of. Blue is a box, green is a cylinder, orange is a sphere. The camp has five thousand four hundred and seventy-four parts, and every one of them is one of the sixteen. |
| 4:27 | program: take `prims`<br>*G: explode into parts*<br>keys: `G` `G` | The G key pulls every object apart into its primitives, and then puts it back together. |
| 4:34 | program: take `prims`<br>*X: wireframe*<br>keys: `X` `X` | And the X key shows the triangles themselves, first with the hidden lines removed, and then as an x-ray. |
| 4:42 | picture<br>*A drone, part by part* | So an object is simply a list of parts. This drone has one hundred and fifty-eight of them: first the body, then the arms, the motors and rotors, and finally the camera, the searchlight and the rounds on their rails. |

### 4:55  Transformations and the scene graph

| Time | Picture | Narration |
|---|---|---|
| 4:55 | plate `trs`<br>*M = T R S, and why the order matters* | Every primitive starts one unit big, sitting at the origin. One matrix turns it into a real part. I scale it, then rotate it, then move it into place. The order matters, just as putting on socks and shoes does. Scale, rotate, translate puts a drone arm exactly where it belongs, but if I translate first and rotate afterwards, the arm swings away in a circle round the origin. |
| 5:23 | plate `graph`<br>*The scene graph: parent times child* | Parts also hang on each other, like the bones of an arm. A rotor blade is placed relative to its hub. The hub is placed relative to the arm, and the arm relative to the body. The world matrix of the blade is the product of all of them, parent first. So when I turn the hub, the blade follows without a single extra line of code. |
| 5:44 | program: take `graph`<br>*H: the axes of the nodes*<br>keys: `H` | The H key draws the local axes of these nodes. On a drone you can see the frames nested inside each other: the body, and inside it the two frames of the camera mount. |
| 5:56 | program: take `graph`<br>*A hierarchy in motion* | The radar is a hierarchy in motion: the mount turns, and the dish tilts inside the frame that is turning. |
| 6:04 | program: take `graph`<br>*A soldier: 75 parts, 7 levels* | A soldier is the deepest hierarchy in the project: seventy-five parts on seven levels, from the hip to the thigh, the shin and the foot. When he walks, only the joint angles change. |

## Viewing

### 6:17  Viewing and projection

| Time | Picture | Narration |
|---|---|---|
| 6:17 | plate `lookat`<br>*The view matrix* | The camp now exists as numbers in three dimensions, but a screen is flat, so the next question is how a point in the world becomes a pixel. I describe the camera with three things, where the eye is, what it looks at, and which way is up. From these the program builds three axes, right, up and forward, and one matrix that moves the whole world so that the eye sits at the origin, looking straight down one axis. |
| 6:43 | plate `project`<br>*Perspective: divide by the distance* | Then comes projection. A point twice as far away should look half as big. So x and y are divided by the distance. The pyramid of space that the camera sees is squeezed into a cube that runs from minus one to plus one. This table follows one real corner of a drone arm through every step, from the world to the view, to clip space, through the divide, and finally to its pixel. |
| 7:08 | program: take `proj`<br>*Ctrl+P: the projection* | In the program, Control P changes the projection while everything keeps running. This is perspective, the way an eye or a camera sees. |
| 7:18 | program: take `proj`<br>*Ctrl+P: the projection*<br>keys: `Ctrl+P` `Ctrl+P` `Ctrl+P` `Ctrl+P` | Orthographic from the top gives a map, because without the division by distance nothing gets smaller as it moves away. Here is the same camp from the front, like an engineer's elevation drawing. And here it is from the side, at exactly the same scale. Finally, this is the isometric view, in which all three axes are drawn equally. |

### 7:40  Clipping and hidden surfaces

| Time | Picture | Narration |
|---|---|---|
| 7:40 | plate `clipping`<br>*Clipping a line to the window* | Not everything is in front of the camera. A line that leaves the window has to be cut. Cohen and Sutherland give each end of the line a four-bit code that says whether it is above, below, left or right of the window. If both codes are zero, the whole line is inside and I keep it. If the two codes share a bit, both ends are off the same side, and I throw the line away. For everything else, Liang and Barsky treat the line as a parameter running from zero to one, and each edge of the window narrows the part that survives. |
| 8:13 | plate `polyclip`<br>*Clipping a polygon, culling an object* | Lines are the easy case. A triangle that crosses the border is clipped by the method of Sutherland and Hodgman, one border at a time, keeping the corners that are inside and adding a new corner wherever a side crosses the border. And before any of that, I test a sphere around each whole object against the six planes of the view, so that an object behind the camera is never sent to the graphics card at all. |
| 8:37 | plate `zbuffer`<br>*The depth buffer* | And when two surfaces cover the same pixel, which one do you see? The depth buffer keeps, for every pixel, the distance of the nearest surface drawn so far. A new fragment is kept only if it is nearer, so the order of drawing no longer matters. |
| 8:54 | program: take `depth`<br>*Z: the depth test*<br>keys: `Z` `Z` | With the Z key I switch the depth test off. Now whatever is drawn last simply wins, and the bunker shows through the ground. When I switch it on again, the scene is solid. |
| 9:06 | program: take `depth`<br>*F: the depth buffer*<br>keys: `F` | And this is the depth buffer itself, drawn in grey, where white is near and black is far. |

### 9:13  Rasterisation and colour interpolation

| Time | Picture | Narration |
|---|---|---|
| 9:13 | plate `raster`<br>*Edge functions fill a triangle* | A triangle now has three corners on the screen, so which pixels does it cover? For each edge I use a function that is positive on one side of the edge and negative on the other. A pixel is inside the triangle when all three functions are positive. And the same three numbers, scaled so that they add up to one, tell me how much of each corner's colour that pixel receives. |
| 9:37 | picture<br>*Colour interpolation, with the correction for perspective* | That blending is colour interpolation, and it has one trap. If I blend along the screen, a texture on a receding surface bends. Dividing by depth first, and undoing it afterwards, keeps it straight. The workbook works the same triangle out by hand, pixel by pixel. |
| 9:57 | program: take `explorer`<br>*Frame Explorer: one frame in 16 stages*<br>keys: ``` | The program can show all of this on its own frame. The back-quote key freezes one frame and replays how it was built, in sixteen stages. Here the corners of every triangle are being transformed. |
| 10:10 | program: take `explorer`<br>*Frame Explorer: rasterisation* | Here the triangles are assembled, and then filled, one scan line after another. |
| 10:18 | program: take `explorer`<br>*Frame Explorer: depth, shadow, fog, tone curve* | After that comes the depth test, which decides what is in front. Then the shadows are added from the light's own view. Then comes the fog, thicker with distance. Then the tone curve bends the brightness into the range of the screen. And at last, this is the finished frame. |
| 10:35 | picture<br>*Frame Explorer: image laboratory* | The explorer also has a pixel inspector and a small image laboratory, with blur and edge-detection kernels, a histogram, and a gamma control. |

## Light

### 10:46  Illumination

| Time | Picture | Narration |
|---|---|---|
| 10:46 | plate `phong`<br>*Ambient + diffuse + specular* | Every pixel now knows which surface it shows, so what colour should it be? The point has a normal, the direction the surface is facing. There is a direction towards the light, and a direction towards the eye. Ambient light is a constant that stands in for all the light bouncing around the scene. Diffuse light depends on the angle between the normal and the light, which is why a face turned to the lamp is bright and a face turned away is dark. The highlight depends on how closely the normal lines up with the half-way vector between the light and the eye. Add the three, for every light, and you have the Phong illumination model. |
| 11:24 | plate `tsmath`<br>*The vector mathematics, written by hand* | One rule I set for myself was that the lighting mathematics had to be my own. So the dot product, normalise, reflect, refract, mix and clamp are each written by hand and placed in front of every shader. The build even refuses a shader that calls the built-in version. |
| 11:42 | program: take `terms`<br>*Ctrl+L: one term at a time*<br>keys: `Ctrl+L` | Control L shows one term at a time in the running scene. This is ambient light alone. |
| 11:49 | program: take `terms`<br>*Ctrl+L: one term at a time*<br>keys: `Ctrl+L` `Ctrl+L` `Ctrl+L` | With diffuse light alone, the shapes appear. With specular light alone, only the highlights remain. And here are all three together. |
| 11:59 | program: take `lights`<br>*Keys 1 to 4: the lights*<br>keys: `1` `2` `3` `4` | The camp has four kinds of light, and the keys one to four switch them. Key one switches the moon. Key two switches the red beacon on top of the mast. Key three switches the searchlights of the two drones. And key four switches the forty-three small lamps around the camp, each one a point light with its own range. |

### 12:19  Shading: flat, Gouraud, Phong

| Time | Picture | Narration |
|---|---|---|
| 12:19 | picture<br>*Flat, Gouraud, Phong* | The next question is where that light is computed. If I compute it once per triangle, I get flat shading, and you can see every facet. If I compute it at the corners and blend across, I get Gouraud shading, which is smooth but dulls the highlight. And if I blend the normal instead and compute the light at every pixel, I get Phong shading. |
| 12:41 | program: take `shade`<br>*O: the shading model*<br>keys: `O` `O` `O` | On the helicopter, the O key steps through them. This is flat shading, where every facet shows. This is Gouraud shading, smooth, but with a dull highlight. And this is Phong shading, the one I use, because round parts should look round. |

### 12:58  Texture mapping

| Time | Picture | Narration |
|---|---|---|
| 12:58 | plate `texture`<br>*Procedural texture: layers of noise* | A surface also needs detail, and I did not want to paint every wall by hand. So the patterns are procedural. I start with smooth random noise, add a second layer at twice the detail and half the strength, then a third and a fourth. The sum looks like concrete, sand or rust, and it is projected onto each part from the side its surface faces, so that it never stretches. |
| 13:22 | picture<br>*Relief without more triangles* | The same pattern can also bend the normal instead of the colour. This is the same wall, without and with that relief, and not one triangle has been added. |

### 13:32  Ray tracing

| Time | Picture | Narration |
|---|---|---|
| 13:32 | plate `rays`<br>*Tracing a ray* | A mirror needs something that the rasteriser does not have, namely what lies behind the camera. Ray tracing gets it by following the light backwards. I send a ray from the eye through a pixel. Where it hits a surface, I send one ray to the light to test for shadow, one reflected ray, and through glass one refracted ray. Each of those can spawn more, up to five bounces, and because every part of the camp is a sphere, a box, a cylinder or a cone, each hit is solved exactly, while a tree of bounding boxes lets a ray skip everything it cannot reach. |
| 14:08 | program: take `rt`<br>*; the render mode* | The semicolon key changes how the frame is made. This is the ordinary rasteriser. |
| 14:14 | program: take `rt`<br>*; the render mode*<br>keys: `;` `;` `;` `;` | The hybrid mode keeps the rasteriser but traces the reflections. Whitted ray tracing adds refraction in the glass. Path tracing lets light bounce between surfaces, so shadows turn soft. And light tracing starts from the lamps instead of the eye. |
| 14:32 | program: take `rt`<br>*' the ray debugger* | And with the ray debugger, a single click draws the whole tree of rays behind one pixel. |

## Rendering

### 14:41  Materials that behave like the real thing

| Time | Picture | Narration |
|---|---|---|
| 14:41 | card: Part 2 | Beyond the lab | That completes the lab. Everything from here on goes beyond it. I begin with the techniques that make the picture more believable, and then turn to the algorithms that make the world move. |
| 14:52 | picture<br>*From Blinn-Phong to a microfacet model* | The highlight of the lab is a simple formula. Real surfaces are rough at a scale too small to see, so I added a microfacet model. It treats a surface as millions of tiny mirrors and asks how many of them happen to face the right way. After that, a material needs only two numbers, how rough it is and how metallic it is. |
| 15:16 | program: take `pbr`<br>*B: Blinn-Phong or microfacet*<br>keys: `B` `B` | The B key switches between the two on the helicopter. This is the classic model, and this is the microfacet model, where painted metal finally looks like painted metal. |

### 15:28  Shadows and ambient occlusion

| Time | Picture | Narration |
|---|---|---|
| 15:28 | plate `shadowmap`<br>*Shadow maps with soft edges* | A shadow is really a question, can this point see the light? I first draw the scene from the position of the light and keep only the depth. Then each pixel compares its own distance to the light with the stored one, and if it is farther, something is in the way. To soften the edge I average many nearby tests, over a wider area where the shadow falls farther from the object that casts it, just as a real shadow blurs with distance. |
| 15:55 | program: take `shadows`<br>*Shadows and ambient occlusion* | Here I switch the shadows off, and back on. And this is ambient occlusion, the soft darkening in corners where light struggles to get in, first off, and then on again. |

### 16:07  The camera as an eye

| Time | Picture | Narration |
|---|---|---|
| 16:07 | plate `tonemap`<br>*From light to a pixel value* | Light in a real scene has an enormous range. The sun is thousands of times brighter than a lamp. So I keep the true values while lighting, choose an exposure the way an eye adapts to a dark room, and only at the very end bend the result through a tone curve and gamma into the range that a screen can show. |
| 16:26 | program: take `air`<br>*Fog, bloom, and the first renderer*<br>keys: `N` `N` | Now the air itself. Here I switch the fog off for a moment, and then bring it back. This is the glow around bright lights, which I also switch off for a moment, and bring back. And the N key returns to my very first renderer, which had none of these. |
| 16:41 | plate `taa`<br>*Temporal anti-aliasing* | Edges need one more idea. A pixel that tests only its centre turns a slanted edge into a staircase, and a thin wire flickers as it slips between two pixels. So in every frame the whole picture is shifted by a different fraction of a pixel, in a fixed pattern of eight positions, and each new frame is blended with the ones before it. After eight frames every pixel has looked at eight places inside itself, and the edge comes out smooth. |
| 17:11 | picture<br>*Screen-space reflections against traced ones* | Reflections in the rasteriser use a trick of their own. For a shiny pixel I march the reflected ray through the depth buffer and copy the colour where it lands. That is fast, but it can only reflect what is already on the screen, which is why the ray-traced modes are kept one key away. On the left is the screen-space result, and on the right the traced one. |

### 17:34  Scale and speed

| Time | Picture | Narration |
|---|---|---|
| 17:34 | plate `instancing`<br>*One blade, drawn 450,000 times* | The ground is covered by four hundred and fifty thousand blades of grass. Sent one by one, they would bring any computer to its knees. So a single blade, with seven corners and five triangles, is sent to the graphics card once, together with a list of the places where it should be drawn, and that is called instancing. The trees use a second trick called level of detail, where a tree near the camera is a full mesh, a farther one is simpler, and the farthest are only flat pictures. The trees, by the way, are the only imported models in the project, and they are free public-domain meshes. |
| 18:11 | program: take `veg`<br>*F5: quality LOW, HIGH, ULTRA, EPIC*<br>keys: `F5` `F5` `F5` `F5` | Down in the grass, every blade bends in the wind. The F5 key steps through the quality presets. This is low, which runs on a laptop. This is high. This is ultra. And this is epic, the preset used throughout this video. |
| 18:27 | plate `bench`<br>*Frame time per preset* | I measured each preset on the same scene and the same machine, and this is the time that one frame costs. |

## World

### 18:34  Day, night and weather

| Time | Picture | Narration |
|---|---|---|
| 18:34 | plate `sun`<br>*One clock drives the light* | A picture becomes a world when time passes. The outpost stands at twenty-two point eight degrees north, the latitude of Khulna. From the hour, the program computes where the sun stands in the sky. A low sun shines through a longer path of air, so its light turns orange and weaker, as it does at every sunset. That one number, the height of the sun, then drives the colour of the light, the sky, the lamps and the exposure. |
| 19:03 | program: take `daylapse`<br>*A day in 20 seconds* | Here is one whole day in twenty seconds. Dawn comes at about six, and the day shift walks out. Then noon, and the long light of the afternoon. At dusk the lamps come on, the drones lift off, and night falls. |
| 19:24 | program: take `weather`<br>*R: rain   J: fireflies*<br>keys: `R` `J` `J` | The R key brings rain, with falling drops and darker, shinier ground. And at night, the J key switches the fireflies and the dust in the air. |

### 19:37  Sound synthesis

| Time | Picture | Narration |
|---|---|---|
| 19:37 | plate `sound`<br>*Sound synthesis* | Sound follows physics too, and every sound is synthesised from a simple recipe. A cannon shot is a burst of noise pushed through a filter, with a low thump underneath. A rotor is a stack of tones at the rate at which the blades pass. A distant sound arrives late, because sound is slow, and dull, because air swallows the high tones first. A loud shot echoes off the wall and the treeline. And inside the bunker, the same shot rings in a concrete room. |

## Control

### 20:07  Buildings you can walk into

| Time | Picture | Narration |
|---|---|---|
| 20:07 | card: Part 3 | Take control | So far I have only been looking at the outpost. But a world you can only look at is still a model. This one can be walked through, driven and flown. |
| 20:16 | plate `walkspace`<br>*The walk space* | Under the soldiers there is a second, invisible map, made of the floors they may stand on, the walls that stop them, and the doorways that join one room to the next. A door is a gate in that map, and it opens when a soldier comes close and faces it. |
| 20:32 | program: `f3:f_bunker`<br>*A soldier under my control* | Here I am controlling a soldier as he walks down the ramp, and the blast gate opens for him. |
| 20:39 | program: `f3:f_bunker`<br>*Tunnel and command room* | We go through the tunnel and into the command room under the ground. |
| 20:46 | program: `f3:f_gen`<br>*E: open a door* | Other doors are opened by hand with the E key, like this one to the generator room. |
| 20:52 | program: `f3:f_hall`<br>*The barracks hall* | And this is the barracks hall, where the soldiers who are off duty rest and train. |
| 20:59 | program: `walk`<br>*W A S D: walk   Shift: sprint   V: first or third person* | Outdoors, a soldier walks with W, A, S and D, and sprints with Shift. The V key puts me behind his eyes in first person, and takes me back out again. He can also jump, crouch, aim and fire his rifle. |

### 21:15  Take control of anything

| Time | Picture | Narration |
|---|---|---|
| 21:15 | plate `control`<br>*Select, control, sight, release* | Twenty things in the camp can be operated. Here they are, and the rule is the same for every one of them. I select it, take control of it, and then its own keys work, for driving, aiming, firing and one special action. The V key changes the camera, from outside to the gunner's sight, and Escape hands the thing back to itself. |
| 21:37 | program: `f3:f_tank2`<br>*The tank* | Let us start with the tank. The soldier walks up to it and climbs in with E. |
| 21:43 | program: `f3:f_tank2`<br>*Drive: W A S D   aim: arrows* | He drives it out of the bay, turns on the spot, and swings the turret onto the target. |
| 21:51 | program: `f3:f_tank2`<br>*V: gunner's sight   Enter: fire* | And from the gunner's sight, he fires two rounds. |
| 21:59 | program: `f3:f_aa`<br>*Anti-aircraft gun* | The anti-aircraft gun has six barrels and fires bursts of tracer. |
| 22:04 | program: `f3:f_sam`<br>*Missile launcher* | And this is the surface-to-air missile launcher. |
| 22:08 | program: take `operate`<br>*8: the radar*<br>keys: `8` | The number keys jump straight to a unit. Eight takes the radar, and the arrow keys swing its dish across the sector it scans. |
| 22:17 | program: take `operate`<br>*The gate barrier*<br>keys: `Enter` | Even the small things can be operated. This is the barrier at the gate, raised with Enter. |
| 22:24 | program: take `operate`<br>*9: the blast doors*<br>keys: `9` | And nine takes the blast doors of the bunker, which close in about four seconds. |
| 22:32 | program: take `heli`<br>*The helicopter*<br>keys: `\` | Now the helicopter. Its special action starts the engine, the rotor spools up, and it lifts off, turns, and flies out over the fence. |
| 22:49 | program: take `uavfly`<br>*A drone: chase, nose camera, thermal camera*<br>keys: `V` `V` | A drone can be flown off its pad by hand as well, and the V key steps through its three cameras. This is the view from behind it. This is the view from its nose. And this is the view through its thermal camera. |
| 23:06 | program: take `rovdrive`<br>*Driving an intruder*<br>keys: `Backspace` | I can even change sides and drive one of the intruders myself. When I press Backspace, it goes back to its own planner and carries on with its mission. |

### 23:16  Jump into anything that moves

| Time | Picture | Narration |
|---|---|---|
| 23:16 | plate `picking`<br>*A click becomes a ray* | Every moving thing also has a point of view, each soldier, each vehicle, both drones, every intruder, and even a round in flight. To choose one I click on it, and the program has to work out what a flat click means in a world with depth. It runs the camera's mathematics backwards and turns the pixel into a ray, and the first object that the ray hits is the one I get. |
| 23:41 | program: take `povjump`<br>*Click a soldier* | A soldier is walking his patrol. I move the pointer onto him, and click. |
| 23:48 | program: take `povjump`<br>*Through his eyes* | Now I see what he sees. He keeps walking his patrol, and I simply ride along in first person. |
| 23:55 | program: take `povjump`<br>*V: third person*<br>keys: `V` | The V key steps back behind him, into third person. |
| 24:01 | program: take `povjump`<br>*F4: the list of points of view*<br>keys: `F4` | F4 lists everything that moves, with the nearest first, and I can click any row. |
| 24:09 | program: take `povjump`<br>*Insert / Delete, or the Travel menu: any point of view* | The same jump works for anything that moves. This is one of the tanks, seen from behind. This is the helicopter on its pad. And this is a drone, waiting for the night. |
| 24:20 | program: `v7a`<br>*K: the cinematic director* | At night it becomes a game. The K key hands the camera to a director that cuts between these views by itself. Here it opens the list and goes into a drone's camera. |
| 24:31 | program: `v7a`<br>*Riding a round* | A round is launched, and we ride it all the way down to the target. |
| 24:38 | program: `v7a`<br>*Into an intruder* | Or we take the other side, inside an intruder while the drones hunt it. |
| 24:44 | program: `v7a`<br>*A sentry, first person* | Or we stand with a sentry at the gate, behind his rifle. |

### 24:50  The garrison's day

| Time | Picture | Narration |
|---|---|---|
| 24:50 | plate `astar`<br>*A soldier finds his way* | Nobody controls the soldiers. Each has a duty for the day shift and another for the night, and has to walk there by himself. He searches a grid with the A* algorithm, which always explores the most promising cell first, then pulls the zigzag path straight like a string, and finally rounds its corners into a smooth curve. On that curve he slows down wherever the bend is tight, as a person would. |
| 25:15 | plate `gait`<br>*Walking: the foot decides, the knee follows* | His legs are solved backwards, which is called inverse kinematics. The planted foot stays exactly where it is on the ground, so it never slides. The hip, the knee and the foot form a triangle whose two sides, the thigh and the shin, have fixed lengths, so the knee angle follows from the law of cosines. Meanwhile the other foot swings forward in an arc. |
| 25:38 | program: take `garrison`<br>*The day shift* | By day each soldier has work of his own. This one walks through the bunker to his console in the command room. And outside, the sentries stand guard at the gate. |
| 25:51 | program: `f3:f_al_sam`<br>*On alert* | When the camp goes on alert, every weapon is manned. The missile canisters rise through the net. |
| 25:58 | program: `f3:f_al_mlrs`<br>*On alert* | The rocket truck raises its pod to firing elevation, and the gun searches its sector. |
| 26:04 | program: `f3:f_al_tank`<br>*On alert* | And the tanks wait in their bays with their guns over the wall. |
| 26:09 | plate `tick`<br>*A fixed tick, and a session you can replay* | One more piece of engineering sits under all of this. The world does not advance by whatever time a frame happened to take. It advances in fixed steps of one sixtieth of a second, and the picture is blended between the last two steps. So the motion stays smooth at any frame rate, and the same seed always gives exactly the same night, which is what lets me record a whole play session with F9 and replay it later, event for event. |

## Vision

### 26:38  The thermal camera

| Time | Picture | Narration |
|---|---|---|
| 26:38 | card: Part 4 | Machine vision | Now to the algorithms. The drones have to find an intruder in the dark without any help from me. So what does a machine actually see, and how does it decide that a warm spot is a target? |
| 26:50 | plate `sensor`<br>*A camera that sees heat, with its faults* | A thermal camera does not see light. It sees heat. So every part in the camp is given a temperature, where an engine is hot, a soldier is warm, and the ground is cool and slowly warmed by the sun. A rover even leaves warm tyre tracks behind it, which fade as the ground cools. A real sensor is far from perfect, and I model that as well, so the lens blurs the picture, every pixel has its own small error, whole columns show up as stripes, there is noise, and from time to time a shutter closes so that the camera can calibrate itself again. |
| 27:25 | program: take `thermal`<br>*F: thermal view   T: palette*<br>keys: `F` `T` `T` `T` | The F key switches the whole view to thermal, where hot is bright, and the T key changes the palette. |
| 27:34 | program: take `thermal`<br>*The drone's camera: 320 x 240, recalibration, three zoom steps*<br>keys: `Q` `Wheel` `Wheel` | And this is what a drone's own camera sends down, three hundred and twenty by two hundred and forty pixels, with all of its noise. The Q key makes the sensor calibrate itself, and for a moment its shutter closes. The mouse wheel zooms the lens in three steps. This is the wide view of forty degrees. This is twenty degrees. And this is eight degrees, for a close look at a target. |

### 27:59  From a heat picture to a target

| Time | Picture | Narration |
|---|---|---|
| 27:59 | plate `cvsteps`<br>*Five steps, every frame* | Every frame of that picture goes through the same five steps. First comes a threshold, which keeps only the pixels above eighty per cent heat, or above the level that best separates the dark part of the picture from the bright part, whichever is higher. Second, single stray pixels are removed, because noise is never a vehicle. Third, neighbouring hot pixels are joined into regions. Fourth, each region is measured. And fifth, the program decides what it is. |
| 28:30 | plate `otsu`<br>*Otsu's method: let the picture choose its own threshold* | That automatic level deserves a closer look. If I count how many pixels there are of each brightness, a night picture shows two hills, a large one for the cold background and a small one for the warm things. Otsu's method tries every possible cut between them and keeps the one that leaves each side as tightly packed as possible. So the threshold adapts by itself when the night grows warmer or colder. |
| 28:56 | plate `classify`<br>*Target, person or clutter* | Measuring a region gives its area, its centre, its hottest pixel, and how stretched it is. Then a simple rule decides. A small region with a very hot core that is not too stretched is a vehicle, and therefore a target. A warm region without a hot core, upright or compact, is a person, and a person is never fired at. Anything else, like a wall warmed by the sun, is clutter. |
| 29:23 | picture<br>*Soldiers are marked as persons* | By day I can check this rule. Through the drone's camera the soldiers are boxed as persons, and not one of them ever becomes a target. |

### 29:32  Tracking

| Time | Picture | Narration |
|---|---|---|
| 29:32 | plate `kalman`<br>*Predict, then correct* | One detection is not enough, because the camera shakes, pixels flicker, and a rover drives behind a tree. So each target becomes a track, which is a guess of its position and speed, together with how unsure that guess is. Between frames the track is moved ahead by its speed, and the uncertainty grows, like walking with your eyes closed. When a new detection arrives, it pulls the guess back, strongly if the track was unsure and only gently if it was confident, and the uncertainty shrinks again. This cycle of predict and correct is the Kalman filter. |
| 30:09 | plate `assoc`<br>*Gating, matching, two views* | With several targets there is also a matching problem, namely which detection belongs to which track? Only a detection inside a track's gate may match it, and among those the Hungarian method picks the pairing with the smallest total distance. And when both drones see the same target, their two lines of sight are crossed, which fixes its position far better than a single camera can. |
| 30:32 | program: `n_flir`<br>*Tracks coast behind cover* | In the drone's view you can watch this happen. The boxes are tracks. When the rover goes behind cover, its track coasts on the prediction alone, and it is picked up again when the rover comes out. |
| 30:45 | plate `tracklog`<br>*One track's uncertainty, measured* | Here is that same night as numbers, the uncertainty of one track over time. It jumps while the rover is hidden, and falls back each time a camera sees it again. |

## Agents

### 30:57  The drones

| Time | Picture | Narration |
|---|---|---|
| 30:57 | card: Part 5 | The agents | Seeing is only half of it. Three kinds of things in the camp act on their own, the drones, the intruders and the soldiers. So let us look at the rules that make them behave as if they had a plan. |
| 31:09 | plate `duty`<br>*A drone's day* | A drone has a duty for each part of the day. By day it sits docked on its pad. At dusk its rotors spool up and it takes off. At night the two of them patrol on opposite sides of a circle, so that together they always see the whole camp. And at dawn they return, hover over their pads, and land. |
| 31:29 | program: `f2:uav_takeoff`<br>*Take-off at dusk* | Here is dusk. The lamps come on, and both drones lift off with their searchlights. |
| 31:36 | program: `n_wide`<br>*Night patrol* | And here they are on patrol, with their searchlights sweeping the camp. |

### 31:41  One drone, or both?

| Time | Picture | Narration |
|---|---|---|
| 31:41 | plate `decide1`<br>*Lead and wing* | When a track is confirmed, the nearer drone becomes the lead and the other becomes its wing, and they take up positions about ninety degrees apart, so that one of them almost always has a clear view. Before firing, the lead must hold its lock for eight tenths of a second and the wing for half a second, each with nothing in its line of fire. If there is only one intruder, the lead waits up to eight seconds for the wing, so that both can fire. |
| 32:08 | plate `tot`<br>*Time on target* | They do not fire at the same moment. The round from the farther drone needs longer to arrive, so that drone fires first, by exactly the difference in flight time. Both rounds then arrive together, which is called time on target, and it leaves a rover no chance to escape between two hits. Each round also steers itself on the way, turning towards its target at a limited rate, and goes off when it comes within half a metre. |
| 32:34 | program: `n_follow`<br>*Combined strike* | Here it is. One intruder is left, both drones hold their locks, and both rounds arrive together. |
| 32:43 | plate `decide2`<br>*Two intruders: split, or stay together?* | Now a second intruder appears. If it is more than fourteen metres from the first, and it is close to something it could damage, then waiting for a combined strike would cost too much. So the pair splits. The nearer drone takes the second target and strikes alone. When that target is gone, or the two targets come within nine metres of each other, the pair forms again. |
| 33:05 | program: `n_follow`<br>*Single strike* | This is the same night, a little earlier. Two intruders are far apart, the pair splits, and one drone fires alone. |
| 33:14 | program: `n_uav1`<br>*The guards fire too* | The guards join in as well. A soldier at his post opens fire on any tracked intruder within twenty-six metres. |

### 33:23  The intruder: choosing a route

| Time | Picture | Narration |
|---|---|---|
| 33:23 | plate `rgrid`<br>*The intruder's map* | Now let us change sides. An intruder is a small rover that wants to reach something in the camp without being seen, and to plan its way it lays a grid over the ground, eighty by eighty cells of half a metre each. Cells inside obstacles are blocked. |
| 33:38 | plate `rcost`<br>*What a cell costs* | Every free cell then gets a price. It costs one just to step on it. It costs more if a drone can see it, and to find that out, the rover draws a line from each drone to the cell and checks whether anything is in the way. And each rover adds a little random cost of its own, so that no two rovers choose the same way. |
| 33:59 | plate `rflood`<br>*Flood fill from the goal* | Then comes a trick from the MicroMouse maze-solving robots. I start at the goal with zero and let the numbers flood outwards like water, each cell taking its cheapest neighbour plus its own price. When the flood reaches the rover, every cell knows how expensive the rest of the journey is. So the route needs no search at all, because the rover simply steps to the neighbour with the smaller number, again and again. |
| 34:24 | program: take `rovplan`<br>*M: the flood field and the route*<br>keys: `M` | The M key shows this inside the program, with the flood field as coloured cells and the route as a line, and each rover has its own. |

### 34:33  The intruder: following the line

| Time | Picture | Narration |
|---|---|---|
| 34:33 | plate `lfr`<br>*Five sensors, two wheels* | A route is not yet a movement. The rover follows it the way a line-follower robot follows a black line on the floor. Five sensors across its nose each report whether they are over the line. Their weighted average is the error, which tells the rover how far the line lies to one side. A controller then turns that error, together with how fast it is changing, into a turn rate, much as a driver watches both where the lane is and how quickly the car is drifting, and the two wheels are driven at different speeds. |
| 35:06 | program: take `rovclose`<br>*A rover on its line* | Here is one of them from above, shown in thermal so that it stands out against the cold ground. It weaves a little, changes its speed, and sometimes stops, so that a tracker cannot simply extend a straight line. |
| 35:21 | program: take `rovclose`<br>*The rover's camera* | And this is the view from the camera on its own mast. |

### 35:27  The intruder: hearing a drone and hiding

| Time | Picture | Narration |
|---|---|---|
| 35:27 | plate `revade`<br>*Hiding from a drone* | And the rover listens. When a drone comes within five metres, it drops what it was doing. Its goal is no longer the camp. Its goal becomes every cell that no drone can see, under a tree, behind a wall, or under a net. The same flood fill runs again, this time from all of those cells at once, and the rover drives to the nearest one. There it waits four seconds and then plans again, because by then the drones have moved. |
| 35:54 | split screen<br>*One moment, three views* | Here are three views of the same few seconds. On the left is the drone's thermal camera. In the middle is the rover's own map, red where a drone can see, dark where it is hidden, with its route in yellow. On the right is the wide view. Watch the route bend into the shadow as the drone comes near. |

### 36:14  The game, and the blast

| Time | Picture | Narration |
|---|---|---|
| 36:14 | plate `game`<br>*Night Watch: the rules* | Put together, all of this becomes a game called Night Watch. Rovers come in waves, two more each time, up to six. Each has one hundred points of armour, and a hit takes about thirty. A kill scores one hundred points, and a combined kill one hundred and fifty. A rover that reaches its goal costs the base ten per cent. |
| 36:34 | program: take `hud`<br>*F11: the game display   U: the drone's feed*<br>keys: `U` | The game's display shows the wave and the score at the top, the health of the base as a green bar, the clock, and a small radar on which every tracked intruder appears. And with the U key, the picture from a drone's thermal camera is added in the corner. |
| 36:50 | program: take `dayops`<br>*Shift+F2: day operations* | With Shift and F2 the same game runs in daylight. The intruders come by day, and the drones scramble from their pads to meet them. |
| 37:00 | plate `particles`<br>*An explosion is particles* | The blast itself is physics too. Hundreds of particles are thrown out with random speeds. Gravity pulls them down, drag slows them, and they bounce once on the ground. Each one cools as it flies, and its colour follows its temperature, from white through yellow, orange and red to dark smoke. |
| 37:21 | program: `n_wide`<br>*Blast, fire, smoke* | Afterwards the wreck burns, and the fire slowly cools. |
| 37:27 | program: `f2:uav_landing`<br>*Dawn* | At dawn the rovers that are left withdraw, and the drones come home. |

## Engineering

### 37:34  Hard problems

| Time | Picture | Narration |
|---|---|---|
| 37:34 | plate `hard`<br>*Four hard problems* | It did not all work the first time, and four problems took me the longest. Far surfaces flickered, because depth is stored most precisely close to the camera. Shadows had stripes, because a surface was shadowing itself. Thin wires shimmered from one frame to the next. And soldiers walked through walls, until the doors became part of the walk map. Each of these is in the report with its cause, its fix, and a picture from before and after. |

### 38:03  How it is checked

| Time | Picture | Narration |
|---|---|---|
| 38:03 | plate `proof`<br>*How it is checked* | I also wanted every claim in this video to be checkable. Three hundred and ninety-six automatic tests run on every build. The same seed always gives exactly the same night, so any result can be repeated. And a whole play session can be recorded and replayed. |
| 38:20 | picture<br>*One workbook for every number* | And this brings me back to the workbook. Every coordinate of every part, every constant of every algorithm, and every number in the report lives there, on twenty-three sheets. A checker stops the build if the program, the workbook and the report ever disagree. |

### 38:38  One night at the outpost

| Time | Picture | Narration |
|---|---|---|
| 38:38 | program: `n_wide` | To finish, here is one night at the outpost, with no more words from me. |
| 38:43 | program: `n_follow` |  |
| 38:50 | program: `n_follow` |  |
| 38:57 | program: `n_wide` |  |
| 39:02 | plate `numbers` | That was ThermalSense, from a single triangle to sixteen primitives, five thousand four hundred and seventy-four parts, a complete pipeline, a thermal camera, a tracker, and three kinds of agents. |
| 39:15 | card: “If student A and student B both use AI for a project,
show me how creative you can be,
and how much depth you can cover.” | One last word, to my juniors. Be brave with your projects. Pick a problem that scares you a little, and try to solve it elegantly. Because I believe that if student A and student B both use AI for a project, what matters is how creative you can be, and how much depth you can cover. Thank you for watching. |
