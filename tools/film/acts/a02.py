"""Act 2 (viewing) and Act 3 (light)."""
from dsl import *  # noqa: F401,F403

LAB = IMG + "/lab/"
RT = IMG + "/rt/"

# ------------------------------------------------------------------------------------------------------- act 2
act("Viewing")
chapter("camera", "Camera and projection")
card("Question two. The camp now exists as numbers in three dimensions. A screen is flat. How does a point in the "
     "world become a pixel?", "2  Viewing", ["How does a point in the world", "become a pixel on the screen?"], kind="act")
plate("First, the camera. It is given by three things: where the eye is, what it looks at, and which way is up. From "
      "these the program builds three axes, right, up and forward, and one matrix that moves the whole world so that "
      "the eye sits at the origin, looking down one axis.", "lookat", title="The view matrix")
plate("Then projection. A point twice as far away should look half as big. So x and y are divided by the distance. "
      "The pyramid that the camera sees is squeezed into a cube, from minus one to plus one. This table follows one "
      "real corner of a drone arm through every step: world, view, clip, the divide, and the pixel.", "project",
      title="Perspective: divide by the distance")
take("proj", 16.5, "-20 13 30 0 0.5 2 50")
live("In the program, Control P changes the projection while everything keeps running. This is perspective.",
     [(0, "orbit 0 0.5 2 36 14 4 120 50 cut")], title="Ctrl+P: the projection", min=6.0)
live("Orthographic, from the top: a map. No division by distance, so nothing gets smaller. From the front. From "
     "the side. And isometric, where the three axes are drawn at the same scale.",
     [(0.2, "set projection top", "Ctrl+P"), ("30%", "set projection front", "Ctrl+P"), ("52%", "set projection side", "Ctrl+P"),
      ("72%", "set projection isometric", "Ctrl+P"), ("100%-0.3", "set projection perspective")], title="Ctrl+P: the projection", min=15.0)
card("Compared: perspective and three parallel projections. Chosen: perspective for playing, because it is how an "
     "eye and a camera see. The parallel views stay in the program as teaching views.", "Decision",
     ["compared   perspective  /  orthographic  /  isometric", "chosen     perspective to play, the others to teach",
      "why        size falls with distance, as in a real camera"])

chapter("clip", "Clipping and hidden surfaces")
plate("Not everything is in front of the camera. A line that leaves the view must be cut. Cohen and Sutherland give "
      "each end a four-bit code: above, below, left, right. Both codes zero: keep the line. A shared bit: throw it "
      "away. Otherwise, Liang and Barsky find the part of the line that stays inside.", "clipping", title="Clipping a line to the window")
plate("And when two surfaces cover the same pixel, which one do you see? The depth buffer keeps, for every pixel, the "
      "distance of the nearest surface drawn so far. A new fragment is kept only if it is nearer.", "zbuffer",
      title="The depth buffer")
take("depth", 16.5, "-8 8 -16 0 0.5 6 50")
live("With the Z key the depth test is switched off. Now whatever is drawn last wins, and the bunker shows through "
     "the ground. Switched on again, the scene is solid.",
     [(0, "orbit 0 0.5 6 24 8 5 250 50 cut"), ("22%", "set depth 0", "Z"), ("70%", "set depth 1", "Z")], title="Z: the depth test", min=11.0)
live("This is the depth buffer itself, drawn as grey: white is near, black is far.",
     [(0.3, "set view zbuf", "F"), ("100%-0.3", "set view visible")], title="F: the depth buffer", min=6.5)
card("Compared: sorting the parts back to front, or a depth buffer. Chosen: a twenty-four bit depth buffer, with the "
     "near plane at five centimetres and the far plane at six hundred metres.", "Decision",
     ["compared   painter's sorting  /  depth buffer", "chosen     24-bit depth, near 0.05 m, far 600 m",
      "why        correct for every pixel, no sorting of 5,474 parts"])

chapter("raster", "Rasterisation and anti-aliasing")
plate("A triangle now has three corners on the screen. Which pixels does it cover? For each edge there is a function "
      "that is positive on one side. A pixel is inside when all three are positive. The same three numbers also say "
      "how to mix the colours of the corners.", "raster", title="Edge functions fill a triangle")
take("explorer", 16.5, "0.6 2.0 14.8 5.8 0.2 7.8 50")
live("The program can show this on its own frame. The back-quote key freezes one frame and replays how it was made, "
     "in sixteen stages. Here the corners are transformed.",
     [(0, "set props 1"), (0.6, "explorer 1 6", "`")], title="Frame Explorer: one frame in 16 stages", min=9.0)
live("Here the triangles are filled, scan line by scan line.", [(0, "explorer 3 7")], title="Frame Explorer: rasterisation", min=7.5)
live("Depth. Shadow. Fog. Then the tone curve, and the final frame.",
     [(0, "explorer 5 4"), ("22%", "explorer 7 4"), ("44%", "explorer 11 4"), ("66%", "explorer 13 4"), ("86%", "explorer 15 4"),
      ("100%-0.1", "explorer off")], title="Frame Explorer: depth, shadow, fog, tone curve", min=13.0)
still("It also has a pixel inspector and a small image laboratory: blur and edge kernels, a histogram, gamma.",
      [LAB + "explorer_imagelab_gauss_gamma.jpg", LAB + "explorer_imagelab_sobel.jpg"], title="Frame Explorer: image laboratory")
card("Compared: no anti-aliasing, more samples per pixel, and reusing the previous frames. Chosen: both together, "
     "four samples per pixel and temporal anti-aliasing.", "Decision",
     ["compared   none  /  multisampling  /  temporal", "chosen     4x multisampling + temporal anti-aliasing",
      "why        smooth edges and no shimmer on thin wires"])

# ------------------------------------------------------------------------------------------------------- act 3
act("Light")
chapter("illum", "Illumination")
card("Question three. Every pixel now knows which surface it shows. What colour is it? That is decided by light.",
     "3  Light", ["What colour", "is each pixel?"], kind="act")
plate("Take one point on a surface. It has a normal. There is a direction to the light, and a direction to the eye. "
      "Ambient light is a constant. Diffuse light depends on the angle between the normal and the light. The "
      "highlight depends on the angle between the normal and the half-way vector. Add the three.", "phong",
      title="Ambient + diffuse + specular")
plate("None of this uses the shading language's own functions. Dot product, normalise, reflect, refract, mix, clamp: "
      "each one is written out in the project and placed in front of every shader. The compile step refuses a shader "
      "that calls a built-in one.", "tsmath", title="The vector mathematics is written out")
take("terms", 16.6, "38 6 -10 29 3 -3 50")
live("Control L shows one term at a time, in the running scene. Ambient only.",
     [(0, "set flare 0"), (0, "orbit 29 3 -3 12.5 4.5 5 200 50 cut"), (0.8, "set lightterms ambient", "Ctrl+L")], title="Ctrl+L: one term at a time", min=6.0)
live("Diffuse only: the shape appears. Specular only: just the highlights. And all three together.",
     [(0.2, "set lightterms diffuse", "Ctrl+L"), ("40%", "set lightterms specular", "Ctrl+L"), ("75%", "set lightterms all", "Ctrl+L")],
     title="Ctrl+L: one term at a time", min=10.0)
take("lights", 22.5, "-12 6 26 0 1 6 52")
live("The camp has four kinds of light, and keys one to four switch them. The moon. The red beacon on the mast. The "
     "searchlights. And forty-three small lamps, each one a point light with its own range.",
     [(0, "orbit 0 1 6 26 8 4 110 52 cut"), ("14%", "set moon 0", "1"), ("26%", "set moon 1"), ("34%", "set beacon 0", "2"), ("44%", "set beacon 1"),
      ("54%", "set spot 0", "3"), ("66%", "set spot 1"), ("76%", "set practicals 0", "4"), ("92%", "set practicals 1")],
     title="Keys 1 to 4: the lights", min=15.0)

chapter("shade", "Shading: flat, Gouraud, Phong")
still("Where is the light computed? Once per triangle: flat shading, and you see every facet. At the corners, then "
      "blended across: Gouraud shading, smooth but with a dull highlight. Or at every pixel, with a blended normal: "
      "Phong shading.", [LAB + "shading_sphere_lowpoly_flat.jpg", LAB + "shading_sphere_lowpoly_gouraud.jpg", LAB + "shading_sphere_lowpoly_phong.jpg"],
      title="Flat, Gouraud, Phong")
take("shade", 16.5, "27 3 22 20.3 1.2 17 45")
live("On the helicopter, the O key cycles them. Flat. Gouraud. Phong.",
     [(0, "set flare 0"), (0, "orbit 20.3 1.3 17 7.2 2.2 8 20 45 cut"), ("18%", "set shading flat", "O"), ("45%", "set shading gouraud", "O"), ("72%", "set shading phong", "O")],
     title="O: the shading model", min=10.0)
live("And B switches the highlight model itself, from the classic Blinn-Phong to a microfacet model.",
     [(0.3, "set shading object"), ("30%", "set pbr 0", "B"), ("75%", "set pbr 1", "B")], title="B: Blinn-Phong or microfacet", min=8.0)
card("Compared: flat, Gouraud and Phong shading, and two highlight models. Chosen: Phong shading with the "
     "microfacet model. Flat shading is kept only for parts that are meant to look faceted.", "Decision",
     ["compared   flat  /  Gouraud  /  Phong;  Blinn-Phong  /  microfacet", "chosen     per-pixel shading, microfacet highlights",
      "why        round parts look round, metal looks like metal"])

chapter("shadow", "Texture, shadows, and the camera as an eye")
still("Surfaces are not smooth. Instead of more triangles, the normal is bent by a small pattern computed in the "
      "shader. The same wall, without and with relief.", [LAB + "bump_on_off.jpg"], title="Relief without more triangles")
plate("A shadow is a question: can this point see the light? The scene is first drawn from the light, keeping only "
      "depth. Then each pixel compares its own distance to the light with the stored one. The edge is softened by "
      "averaging nearby tests, wider where the shadow falls further from its caster.", "shadowmap", title="Shadow maps")
take("shadows", 15.6, "14 4 -4 23 1 -11 50")
live("Shadows off, and on. Then the soft darkening in corners, off and on.",
     [(0, "orbit 23 1 -11.5 13 4.5 4 215 50 cut"), ("20%", "set shadows 0"), ("45%", "set shadows 1"), ("62%", "set ssao 0"), ("62%", "set gtao 0"),
      ("85%", "set ssao 1"), ("85%", "set gtao 1")], title="Shadows and ambient occlusion", min=10.0)
plate("Light in the scene has a huge range: the sun is thousands of times brighter than a lamp. The program keeps "
      "these real values, adapts the exposure like an eye, and only at the very end bends them through a tone curve "
      "and gamma into the range a screen can show.", "tonemap", title="From light to a pixel value")
live("Fog off, and on. Glow round bright lights, off and on. And N switches to the first version of the renderer, "
     "without any of this.",
     [("8%", "set fog 0"), ("8%", "set aerial 0"), ("28%", "set fog 1"), ("28%", "set aerial 1"), ("42%", "set bloom 0"), ("58%", "set bloom 1"),
      ("72%", "set cinematic 0", "N"), ("100%-0.4", "set cinematic 1", "N")], title="Fog, bloom, and the first renderer", min=12.0)

chapter("rt", "Reflection and ray tracing")
plate("A mirror needs something the rasteriser does not have: what is behind the camera. A ray does. Send a ray "
      "through a pixel. Where it hits, send one more ray to the light, one reflected ray, and through glass one "
      "refracted ray. Every part of the camp is a sphere, a box, a cylinder or a cone, so each hit is solved exactly, "
      "and a tree of boxes skips what the ray cannot reach.", "rays", title="Tracing a ray")
take("rt", 16.5, "0.6 2.0 14.8 5.8 0.2 7.8 50")
live("The semicolon key changes how the frame is made. First the rasteriser.",
     [(0, "set props 1"), (0, "set rt raster")], title="; the render mode", min=5.0)
live("Hybrid: rasterised, with ray-traced reflections. Whitted ray tracing, with refraction in the glass. Path "
     "tracing, where light bounces between surfaces. And light tracing, from the lamps outwards.",
     [(0.2, "set rt hybrid", ";"), ("25%", "set rt whitted", ";"), ("50%", "set rt path", ";"), ("76%", "set rt light", ";")],
     title="; the render mode", min=17.0)
live("With the ray debugger, one click shows the whole tree of rays behind a single pixel.",
     [(0.1, "set rt whitted"), (0.3, "cursor 0.52 0.56 0.9"), (1.4, "click left"), (1.45, "raypick 0.52 0.56"), ("100%-0.3", "set raydebug 0"),
      ("100%-0.3", "cursor hide"), ("100%-0.2", "set rt raster")], title="' the ray debugger", min=9.0)
card("Compared: screen-space reflections, a hybrid, and full ray tracing. Chosen: the rasteriser with screen-space "
     "reflections for playing, and the ray tracers as modes you can switch to.", "Decision",
     ["compared   screen-space  /  hybrid  /  Whitted  /  path", "chosen     raster + screen-space to play, ray tracing on a key",
      "why        the game must hold its frame rate"])
