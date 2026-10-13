"""Part 2 (beyond the lab): better rendering techniques, scale and speed, and a world that changes with time."""
from dsl import *  # noqa: F401,F403

LAB = IMG + "/lab/"
RT = IMG + "/rt/"
SC = IMG + "/scene/"
AUD = ROOT + "/build/audio_v7/"

# ------------------------------------------------------------------------------------------------------- rendering
act("Rendering")
chapter("pbr", "Materials that behave like the real thing")
card("That completes the lab. Everything from here on goes beyond it. I begin with the techniques that make the "
     "picture more believable, and then turn to the algorithms that make the world move.",
     "Part 2 | Beyond the lab", ["better techniques for light, shadow and detail,", "and a world that changes with time"])
still("The highlight of the lab is a simple formula. Real surfaces are rough at a scale too small to see, so I added a "
      "microfacet model. It treats a surface as millions of tiny mirrors and asks how many of them happen to face the "
      "right way. After that, a material needs only two numbers, how rough it is and how metallic it is.",
      [LAB + "materials_blinnphong_row.jpg", LAB + "materials_pbr_grid.jpg"], title="From Blinn-Phong to a microfacet model")
take("pbr", 16.5, "27 3 22 20.3 1.2 17 45")
live("The B key switches between the two on the helicopter. This is the classic model, and this is the microfacet "
     "model, where painted metal finally looks like painted metal.",
     [(0, "set flare 0"), (0, "orbit 20.3 1.3 17 7.2 2.2 8 20 45 cut"), ("@classic-0.6", "set pbr 0", "B"), ("@and this is the microfacet-0.2", "set pbr 1", "B")],
     title="B: Blinn-Phong or microfacet", min=9.0)

chapter("shadow", "Shadows and ambient occlusion")
plate("A shadow is really a question, can this point see the light? I first draw the scene from the position of the "
      "light and keep only the depth. Then each pixel compares its own distance to the light with the stored one, and "
      "if it is farther, something is in the way. To soften the edge I average many nearby tests, over a wider area "
      "where the shadow falls farther from the object that casts it, just as a real shadow blurs with distance.",
      "shadowmap", title="Shadow maps with soft edges")
take("shadows", 15.6, "14 4 -4 23 1 -11 50")
live("Here I switch the shadows off, and back on. And this is ambient occlusion, the soft darkening in corners where "
     "light struggles to get in, first off, and then on again.",
     [(0, "orbit 23 1 -11.5 13 4.5 4 215 50 cut"), ("@off-0.4", "set shadows 0"), ("@back on-0.4", "set shadows 1"), ("@first off-0.3", "set ssao 0"),
      ("@first off-0.3", "set gtao 0"), ("@then on again-0.2", "set ssao 1"), ("@then on again-0.2", "set gtao 1")], title="Shadows and ambient occlusion", min=11.0)

chapter("tone", "The camera as an eye")
plate("Light in a real scene has an enormous range. The sun is thousands of times brighter than a lamp. So I keep the "
      "true values while lighting, choose an exposure the way an eye adapts to a dark room, and only at the very end "
      "bend the result through a tone curve and gamma into the range that a screen can show.", "tonemap",
      title="From light to a pixel value")
take("air", 22.5, "-12 6 26 0 1 6 52")
live("Now the air itself. Here I switch the fog off for a moment, and then bring it back. This is the glow around "
     "bright lights, which I also switch off for a moment, and bring back. And the N key returns to my very first "
     "renderer, which had none of these.",
     [(0, "orbit 0 1 6 26 8 4 110 52 cut"), ("@fog off-0.2", "set fog 0"), ("@fog off-0.2", "set aerial 0"), ("@then bring it back-0.1", "set fog 1"),
      ("@then bring it back-0.1", "set aerial 1"), ("@which I also-0.1", "set bloom 0"), ("@and bring back-0.1", "set bloom 1"), ("@N key-0.2", "set cinematic 0", "N"),
      ("100%-0.4", "set cinematic 1", "N")],
     title="Fog, bloom, and the first renderer", min=13.0)
plate("Edges need one more idea. A pixel that tests only its centre turns a slanted edge into a staircase, and a thin "
      "wire flickers as it slips between two pixels. So in every frame the whole picture is shifted by a different "
      "fraction of a pixel, in a fixed pattern of eight positions, and each new frame is blended with the ones before "
      "it. After eight frames every pixel has looked at eight places inside itself, and the edge comes out smooth.",
      "taa", title="Temporal anti-aliasing")
still("Reflections in the rasteriser use a trick of their own. For a shiny pixel I march the reflected ray through the "
      "depth buffer and copy the colour where it lands. That is fast, but it can only reflect what is already on the "
      "screen, which is why the ray-traced modes are kept one key away. On the left is the screen-space result, and on "
      "the right the traced one.", [RT + "raytrace_ssr.jpg"], title="Screen-space reflections against traced ones")

chapter("veg", "Scale and speed")
plate("The ground is covered by four hundred and fifty thousand blades of grass. Sent one by one, they would bring "
      "any computer to its knees. So a single blade, with seven corners and five triangles, is sent to the graphics "
      "card once, together with a list of the places where it should be drawn, and that is called instancing. The "
      "trees use a second trick called level of detail, where a tree near the camera is a full mesh, a farther one is "
      "simpler, and the farthest are only flat pictures. The trees, by the way, are the only imported models in the "
      "project, and they are free public-domain meshes.", "instancing", title="One blade, drawn 450,000 times")
take("veg", 16.5, "-5.05 1.5 16.95 -1 0.97 8.7 50")
live("Down in the grass, every blade bends in the wind. The F5 key steps through the quality presets. This is low, "
     "which runs on a laptop. This is high. This is ultra. And this is epic, the preset used throughout this video.",
     [(0, "cam -4.2 1.25 15.4 -1 0.9 8 50 11"), ("@This is low-0.1", "set quality low", "F5"), ("@This is high-0.1", "set quality high", "F5"),
      ("@This is ultra-0.1", "set quality ultra", "F5"), ("@And this is epic-0.1", "set quality epic", "F5")], title="F5: quality LOW, HIGH, ULTRA, EPIC", min=14.0)
plate("I measured each preset on the same scene and the same machine, and this is the time that one frame costs.",
      "bench", title="Frame time per preset", min=7.0)

# ------------------------------------------------------------------------------------------------------- world
act("World")
chapter("day", "Day, night and weather")
plate("A picture becomes a world when time passes. The outpost stands at twenty-two point eight degrees north, the "
      "latitude of Khulna. From the hour, the program computes where the sun stands in the sky. A low sun shines "
      "through a longer path of air, so its light turns orange and weaker, as it does at every sunset. That one "
      "number, the height of the sun, then drives the colour of the light, the sky, the lamps and the exposure.", "sun",
      title="One clock drives the light")
take("daylapse", 5.0, "-24 15 34 0 0.5 2 55")
live("Here is one whole day in twenty seconds. Dawn comes at about six, and the day shift walks out. Then noon, "
     "and the long light of the afternoon. At dusk the lamps come on, the drones lift off, and night falls.",
     [(0, "orbit 0 0.5 2 40 15 3 125 55 cut"), (0.2, "scrub 5.0 23.2 19")], title="A day in 20 seconds", min=21.0)
take("weather", 17.3, "8 2.2 26 0 0.8 12 50")
live("The R key brings rain, with falling drops and darker, shinier ground. And at night, the J key switches the "
     "fireflies and the dust in the air.",
     [(0, "orbit 0 0.8 12 15 3 4 60 50 cut"), (0.8, "set rain 1", "R"), ("@And at night-0.9", "set rain 0"), ("@And at night-0.6", "hour 21.5"),
      ("@J key+0.6", "set particles 0", "J"), ("100%-1.8", "set particles 1", "J")], title="R: rain   J: fireflies", min=13.0)

chapter("sound", "Sound synthesis")
plate("Sound follows physics too, and every sound is synthesised from a simple recipe. A cannon shot is a burst of "
      "noise pushed through a filter, with a low thump underneath. A rotor is a stack of tones at the rate at which "
      "the blades pass. A distant sound arrives late, because sound is slow, and dull, because air swallows the high "
      "tones first. A loud shot echoes off the wall and the treeline. And inside the bunker, the same shot rings in a "
      "concrete room.", "sound", title="Sound synthesis",
      sounds=[("s1", AUD + "v7_cannon_echoes.wav", 0.5), ("s2", AUD + "v7_helicopter_flyby.wav", 0.4), ("s5", AUD + "v7_bunker_vs_open_reverb.wav", 0.5)])
