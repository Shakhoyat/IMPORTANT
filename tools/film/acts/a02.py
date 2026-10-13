"""Part 1 (the graphics lab), continued: viewing, clipping, hidden surfaces, rasterisation, illumination, shading,
texture and ray tracing."""
from dsl import *  # noqa: F401,F403

LAB = IMG + "/lab/"
RT = IMG + "/rt/"

# ------------------------------------------------------------------------------------------------------- viewing
act("Viewing")
chapter("camera", "Viewing and projection")
plate("The camp now exists as numbers in three dimensions, but a screen is flat, so the next question is how a point "
      "in the world becomes a pixel. I describe the camera with three things, where the eye is, what it looks at, and "
      "which way is up. From these the program builds three axes, right, up and forward, and one matrix that moves the "
      "whole world so that the eye sits at the origin, looking straight down one axis.", "lookat", title="The view matrix")
plate("Then comes projection. A point twice as far away should look half as big. So x and y are divided by the "
      "distance. The pyramid of space that the camera sees is squeezed into a cube that runs from minus one to plus "
      "one. This table follows one real corner of a drone arm through every step, from the world to the view, to clip "
      "space, through the divide, and finally to its pixel.", "project", title="Perspective: divide by the distance")
take("proj", 16.5, "-20 13 30 0 0.5 2 50")
live("In the program, Control P changes the projection while everything keeps running. This is perspective, the way "
     "an eye or a camera sees.",
     [(0, "orbit 0 0.5 2 36 14 4 120 50 cut")], title="Ctrl+P: the projection", min=6.0)
live("Orthographic from the top gives a map, because without the division by distance nothing gets smaller as it "
     "moves away. Here is the same camp from the front, like an engineer's elevation drawing. And here it is from the "
     "side, at exactly the same scale. Finally, this is the isometric view, in which all three axes are drawn equally.",
     [(0.2, "set projection top", "Ctrl+P"), ("@Here is the same camp-0.1", "set projection front", "Ctrl+P"),
      # the elevations look at the horizon: a fixed exposure, or the bright sky turns the camp into a silhouette
      ("@Here is the same camp-0.1", "set autoexposure 0"), ("@Here is the same camp-0.1", "set exposure 4.5"),
      ("@And here it is from the side-0.1", "set projection side", "Ctrl+P"), ("@Finally-0.1", "set projection isometric", "Ctrl+P"),
      ("@Finally-0.1", "set exposure 1"), ("@Finally-0.1", "set autoexposure 1"), ("100%-0.3", "set projection perspective")], title="Ctrl+P: the projection", min=17.0)

chapter("clip", "Clipping and hidden surfaces")
plate("Not everything is in front of the camera. A line that leaves the window has to be cut. Cohen and Sutherland "
      "give each end of the line a four-bit code that says whether it is above, below, left or right of the window. "
      "If both codes are zero, the whole line is inside and I keep it. If the two codes share a bit, both ends are off "
      "the same side, and I throw the line away. For everything else, Liang and Barsky treat the line as a parameter "
      "running from zero to one, and each edge of the window narrows the part that survives.", "clipping",
      title="Clipping a line to the window")
plate("Lines are the easy case. A triangle that crosses the border is clipped by the method of Sutherland and "
      "Hodgman, one border at a time, keeping the corners that are inside and adding a new corner wherever a side "
      "crosses the border. And before any of that, I test a sphere around each whole object against the six planes of "
      "the view, so that an object behind the camera is never sent to the graphics card at all.", "polyclip",
      title="Clipping a polygon, culling an object")
plate("And when two surfaces cover the same pixel, which one do you see? The depth buffer keeps, for every pixel, the "
      "distance of the nearest surface drawn so far. A new fragment is kept only if it is nearer, so the order of "
      "drawing no longer matters.", "zbuffer", title="The depth buffer")
take("depth", 16.5, "-8 8 -16 0 0.5 6 50")
live("With the Z key I switch the depth test off. Now whatever is drawn last simply wins, and the bunker shows "
     "through the ground. When I switch it on again, the scene is solid.",
     [(0, "orbit 0 0.5 6 24 8 5 250 50 cut"), ("@off-0.4", "set depth 0", "Z"), ("@on again-0.5", "set depth 1", "Z")], title="Z: the depth test", min=11.0)
live("And this is the depth buffer itself, drawn in grey, where white is near and black is far.",
     [(0.3, "set view zbuf", "F"), ("100%-0.3", "set view visible")], title="F: the depth buffer", min=6.5)

chapter("raster", "Rasterisation and colour interpolation")
plate("A triangle now has three corners on the screen, so which pixels does it cover? For each edge I use a function "
      "that is positive on one side of the edge and negative on the other. A pixel is inside the triangle when all "
      "three functions are positive. And the same three numbers, scaled so that they add up to one, tell me how much "
      "of each corner's colour that pixel receives.", "raster", title="Edge functions fill a triangle")
still("That blending is colour interpolation, and it has one trap. If I blend along the screen, a texture on a "
      "receding surface bends. Dividing by depth first, and undoing it afterwards, keeps it straight. The workbook "
      "works the same triangle out by hand, pixel by pixel.",
      [LAB + "explorer_interp_affine_perspective.jpg", ROOT + "/report/figures/workbook/Color_Interp.png"],
      title="Colour interpolation, with the correction for perspective")
take("explorer", 16.5, "0.6 2.0 14.8 5.8 0.2 7.8 50")
live("The program can show all of this on its own frame. The back-quote key freezes one frame and replays how it was "
     "built, in sixteen stages. Here the corners of every triangle are being transformed.",
     [(0, "set props 1"), (0.6, "explorer 1 6", "`")], title="Frame Explorer: one frame in 16 stages", min=9.0)
live("Here the triangles are assembled, and then filled, one scan line after another.",
     [(0, "explorer 2 3"), ("@filled-0.3", "explorer 3 5")], title="Frame Explorer: rasterisation", min=8.0)
live("After that comes the depth test, which decides what is in front. Then the shadows are added from the light's "
     "own view. Then comes the fog, thicker with distance. Then the tone curve bends the brightness into the range of "
     "the screen. And at last, this is the finished frame.",
     [(0, "explorer 5 2"), ("@Then the shadows-0.2", "explorer 7 2"), ("@Then comes the fog-0.2", "explorer 11 2"), ("@Then the tone-0.2", "explorer 13 2.5"),
      ("@And at last-0.2", "explorer 15 3"), ("100%-0.1", "explorer off")], title="Frame Explorer: depth, shadow, fog, tone curve", min=12.0)
still("The explorer also has a pixel inspector and a small image laboratory, with blur and edge-detection kernels, a "
      "histogram, and a gamma control.",
      [LAB + "explorer_imagelab_gauss_gamma.jpg", LAB + "explorer_imagelab_sobel.jpg"], title="Frame Explorer: image laboratory")

# ------------------------------------------------------------------------------------------------------- light
act("Light")
chapter("illum", "Illumination")
plate("Every pixel now knows which surface it shows, so what colour should it be? The point has a normal, the "
      "direction the surface is facing. There is a direction towards the light, and a direction towards the eye. "
      "Ambient light is a constant that stands in for all the light bouncing around the scene. Diffuse light depends "
      "on the angle between the normal and the light, which is why a face turned to the lamp is bright and a face "
      "turned away is dark. The highlight depends on how closely the normal lines up with the half-way vector between "
      "the light and the eye. Add the three, for every light, and you have the Phong illumination model.", "phong",
      title="Ambient + diffuse + specular")
plate("One rule I set for myself was that the lighting mathematics had to be my own. So the dot product, normalise, "
      "reflect, refract, mix and clamp are each written by hand and placed in front of every shader. The build even "
      "refuses a shader that calls the built-in version.", "tsmath", title="The vector mathematics, written by hand")
take("terms", 16.6, "38 6 -10 29 3 -3 50")
live("Control L shows one term at a time in the running scene. This is ambient light alone.",
     [(0, "set flare 0"), (0, "orbit 29 3 -3 12.5 4.5 5 200 50 cut"), (0.8, "set lightterms ambient", "Ctrl+L")], title="Ctrl+L: one term at a time", min=6.0)
live("With diffuse light alone, the shapes appear. With specular light alone, only the highlights remain. And here "
     "are all three together.",
     [(0.2, "set lightterms diffuse", "Ctrl+L"), ("@specular-0.5", "set lightterms specular", "Ctrl+L"), ("@all three-0.6", "set lightterms all", "Ctrl+L")],
     title="Ctrl+L: one term at a time", min=10.0)
take("lights", 22.5, "-12 6 26 0 1 6 52")
live("The camp has four kinds of light, and the keys one to four switch them. Key one switches the moon. Key two "
     "switches the red beacon on top of the mast. Key three switches the searchlights of the two drones. And key four "
     "switches the forty-three small lamps around the camp, each one a point light with its own range.",
     [(0, "orbit 0 1 6 26 8 4 110 52 cut"), ("@Key one+0.3", "set moon 0", "1"), ("@Key two-0.1", "set moon 1"), ("@Key two+0.3", "set beacon 0", "2"),
      ("@Key three-0.1", "set beacon 1"), ("@Key three+0.3", "set spot 0", "3"), ("@And key four-0.1", "set spot 1"), ("@And key four+0.6", "set practicals 0", "4"),
      ("100%-0.9", "set practicals 1")],
     title="Keys 1 to 4: the lights", min=15.0)

chapter("shade", "Shading: flat, Gouraud, Phong")
still("The next question is where that light is computed. If I compute it once per triangle, I get flat shading, "
      "and you can see every facet. If I compute it at the corners and blend across, I get Gouraud shading, which is "
      "smooth but dulls the highlight. And if I blend the normal instead and compute the light at every pixel, I get "
      "Phong shading.", [LAB + "shading_sphere_lowpoly_flat.jpg", LAB + "shading_sphere_lowpoly_gouraud.jpg", LAB + "shading_sphere_lowpoly_phong.jpg"],
      title="Flat, Gouraud, Phong")
take("shade", 16.5, "27 3 22 20.3 1.2 17 45")
live("On the helicopter, the O key steps through them. This is flat shading, where every facet shows. This is "
     "Gouraud shading, smooth, but with a dull highlight. And this is Phong shading, the one I use, because round "
     "parts should look round.",
     [(0, "set flare 0"), (0, "orbit 20.3 1.3 17 7.2 2.2 8 20 45 cut"), ("@This is flat-0.1", "set shading flat", "O"), ("@This is Gouraud-0.1", "set shading gouraud", "O"),
      ("@And this is Phong-0.1", "set shading phong", "O"), ("100%-0.2", "set shading object")],
     title="O: the shading model", min=11.0)

chapter("texture", "Texture mapping")
plate("A surface also needs detail, and I did not want to paint every wall by hand. So the patterns are procedural. "
      "I start with smooth random noise, add a second layer at twice the detail and half the strength, then a third "
      "and a fourth. The sum looks like concrete, sand or rust, and it is projected onto each part from the side its "
      "surface faces, so that it never stretches.", "texture", title="Procedural texture: layers of noise")
still("The same pattern can also bend the normal instead of the colour. This is the same wall, without and with that "
      "relief, and not one triangle has been added.", [LAB + "bump_on_off.jpg"], title="Relief without more triangles")

chapter("rt", "Ray tracing")
plate("A mirror needs something that the rasteriser does not have, namely what lies behind the camera. Ray tracing "
      "gets it by following the light backwards. I send a ray from the eye through a pixel. Where it hits a surface, I "
      "send one ray to the light to test for shadow, one reflected ray, and through glass one refracted ray. Each of "
      "those can spawn more, up to five bounces, and because every part of the camp is a sphere, a box, a cylinder or "
      "a cone, each hit is solved exactly, while a tree of bounding boxes lets a ray skip everything it cannot reach.",
      "rays", title="Tracing a ray")
take("rt", 16.5, "0.6 2.0 14.8 5.8 0.2 7.8 50")
live("The semicolon key changes how the frame is made. This is the ordinary rasteriser.",
     [(0, "set props 1"), (0, "set rt raster")], title="; the render mode", min=5.0)
live("The hybrid mode keeps the rasteriser but traces the reflections. Whitted ray tracing adds refraction in the "
     "glass. Path tracing lets light bounce between surfaces, so shadows turn soft. And light tracing starts from the "
     "lamps instead of the eye.",
     [(0.2, "set rt hybrid", ";"), ("@Whitted-0.4", "set rt whitted", ";"), ("@Path tracing-0.4", "set rt path", ";"), ("@light tracing-0.5", "set rt light", ";")],
     title="; the render mode", min=17.0)
live("And with the ray debugger, a single click draws the whole tree of rays behind one pixel.",
     [(0.1, "set rt whitted"), (0.3, "cursor 0.52 0.56 0.9"), (1.4, "click left"), (1.45, "raypick 0.52 0.56"), ("100%-0.3", "set raydebug 0"),
      ("100%-0.3", "cursor hide"), ("100%-0.2", "set rt raster")], title="' the ray debugger", min=9.0)
