"""docs/CODE_TOUR.md: a guided tour of the project for a demonstration, in the order of the graphics pipeline and then
through the advanced features. Every stop is a link to the exact line of the code (file#Lline: opens at that line in
VS Code's Markdown preview and on GitHub).

Each stop below is (file, pattern, what to say). The line number is found by searching the file for the pattern, so the
tour stays exact when the code moves; a pattern that no longer matches stops the script.
Run: python3 tools/report/gen_code_tour.py"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "docs", "CODE_TOUR.md")

TOUR = [
    ("0. Start here: one frame of the program", "The program is a fixed 60 Hz simulation plus a renderer. These four lines are the skeleton.", [
        ("src/main.cpp", r"^int main\(", "Entry point: reads the command line and picks a mode (play, capture, record, export)."),
        ("src/app/App.cpp", r"^int App::run\(", "Creates the window or the headless context, the scene and the renderer."),
        ("src/app/App.cpp", r"^void App::step\(", "One simulation tick: input, scene update, UAV cameras, vision, tracker."),
        ("src/app/App.cpp", r"^void App::renderFrame\(", "One frame: main view, overlays, HUD."),
    ]),
    ("1. Primitives: every vertex is computed by the program", "No model files for the camp: 16 generators produce all meshes (report Section 6).", [
        ("src/primitives/Cube.cpp", r"const Vec3 p = f\.n \* 0\.5f", "Cube: the corner positions of each face, written out from the face normal and its two edge axes."),
        ("src/primitives/Sphere.cpp", r"std::sin\(theta\) \* std::cos\(phi\)", "Sphere: the parametric point (sinθ cosφ, cosθ, sinθ sinφ); the normal is the same vector."),
        ("src/primitives/Cylinder.cpp", r"radius \* std::cos\(phi\)", "Cylinder: ring vertices (r cosφ, r sinφ, z)."),
        ("src/primitives/Torus.cpp", r"std::cos\(b\) \* std::cos\(a\)", "Torus: the two-angle parametrisation."),
        ("src/primitives/Revolve.cpp", r"^MeshData revolve\(", "Surfaces of revolution (hemisphere, capsule, cone): rings swept round an axis."),
        ("src/primitives/Library.cpp", r"PrimitiveType::Cube", "The table of the 16 primitives with their vertex and triangle counts."),
        ("src/rendering/Mesh.cpp", r"glBufferData", "Upload of a generated mesh to the GPU (vertex and index buffers)."),
    ]),
    ("2. Objects: hard-coded sizes, rotations and positions", "Every object is a list of parts: primitive, size S, rotation R (degrees), position T (metres). "
     "These are the numbers of the report's Section 7 cards and coordinate sheets.", [
        ("src/objects/Builder.cpp", r"^SceneNode\* Builder::part\(", "The one function every part goes through: (parent, name, primitive, S, R, T, material)."),
        ("src/objects/Tank.cpp", r'"Hull core", P::Cube', "Tank hull: a cube scaled to 1.7 x 0.9 x 5.6 m, 0.8 m above the ground. Change a number here and the tank changes."),
        ("src/objects/Tank.cpp", r'turret_ = b\.group\(h, "Turret yaw"', "Tank turret: a group node; everything on the turret hangs under it."),
        ("src/objects/Tank.cpp", r'"Gun barrel", P::Cylinder', "Gun barrel: a cylinder 4.2 m long under the recoil group."),
        ("src/objects/Soldier.cpp", r'"Torso", P::TaperedBox', "Soldier torso (the soldier is 75 parts on a 19-joint skeleton)."),
        ("src/objects/Uav.cpp", r'"Fuselage", P::Cube', "UAV frame plate; rotors and the camera gimbal are groups below it."),
        ("src/objects/Helicopter.cpp", r'"Cabin", P::Sphere', "Helicopter cabin: a stretched sphere."),
        ("src/objects/AaGun.cpp", r'"Radome", P::Hemisphere', "Anti-aircraft gun radome: a hemisphere."),
        ("src/objects/Launchers.cpp", r'Builder b\("SAM launcher"', "SAM launcher: turntable, cradle and four canisters."),
        ("src/objects/Bunker.cpp", r'"Portal head wall"', "Command bunker entrance: head wall, gate, tunnel and rooms are placed here."),
        ("src/objects/BaseLayout.h", r"constexpr Vec3 kHelipad", "Site plan: where each installation stands (helipad, radar, AA gun, motor pool, gates)."),
        ("src/app/DocExport.cpp", r"world_matrix", "The export that writes every part and matrix to JSON; the report's tables are generated from it."),
    ]),
    ("3. Modelling transformation: M = T R S, written by hand", "No mathematics library is linked (report Section 8).", [
        ("src/math/Math3D.h", r"^struct Mat4", "The 4x4 matrix type (column-major)."),
        ("src/math/Math3D.cpp", r"^Mat4 translate\(", "Translation matrix."),
        ("src/math/Math3D.cpp", r"^Mat4 rotateY\(", "Rotation about Y (X and Z are next to it)."),
        ("src/math/Math3D.cpp", r"^Mat4 scale\(", "Scale matrix."),
        ("src/math/Math3D.cpp", r"^Mat4 operator\*\(const Mat4& a, const Mat4& b\)", "Matrix product."),
        ("src/scene/Transform.h", r"Mat4 matrix\(\) const", "The order: translate x rotate (Y, X, Z) x scale. This line is M = T R S."),
        ("src/math/Math3D.cpp", r"^Mat3 normalMatrix\(", "Normals use the inverse transpose of the upper 3x3."),
    ]),
    ("4. Hierarchy: the scene graph", "A child's world matrix is its parent's world matrix times its own local matrix (report Section 9).", [
        ("src/scene/SceneNode.h", r"^struct SceneNode", "A node: local transform, world matrix, children, primitive, material."),
        ("src/scene/Scene.cpp", r"node\.world = parentWorld \* t\.matrix\(\)", "The recursion that walks the tree every frame."),
        ("src/objects/Tank.cpp", r"turret_->local\.rotationDeg = ", "Animation is one angle on one group: the turret yaw moves gun, sight and hatches together."),
        ("src/objects/AaGun.cpp", r"cradle_->local\.rotationDeg = ", "The gun cradle: pitch under the yawing mount."),
    ]),
    ("5. Viewing and projection", "Report Sections 10 to 12.", [
        ("src/math/Math3D.cpp", r"^Mat4 lookAt\(", "View matrix from eye, target and up (the u, v, n basis)."),
        ("src/scene/Camera.cpp", r"^CameraView Camera::compute\(", "The cameras: orbit, free, walk, chase, UAV gimbal."),
        ("src/math/Math3D.cpp", r"^Mat4 perspective\(", "Perspective projection matrix."),
        ("src/math/Math3D.cpp", r"^Mat4 ortho\(", "Orthographic projection (top, front, side and isometric teaching views)."),
        ("src/math/ExplorerMath.cpp", r"^VertexTrace traceVertex\(", "One vertex followed by hand: model, world, eye, clip, divide by w, viewport."),
    ]),
    ("6. Clipping, hidden surfaces, rasterisation", "The GPU does these in fixed function; the project also computes each one itself (Frame Explorer, report Sections 13 to 15).", [
        ("src/math/Frustum.cpp", r"^Frustum::Frustum\(const Mat4& m\)", "The six frustum planes from the rows of the view-projection matrix."),
        ("src/math/Frustum.cpp", r"^bool Frustum::intersects\(", "Sphere against the planes: whole parts are culled before drawing."),
        ("src/rendering/Renderer.cpp", r"glEnable\(GL_CULL_FACE\)", "Back-face culling switched on."),
        ("src/rendering/Renderer.cpp", r"glDepthFunc\(GL_LESS\)", "The Z-buffer test."),
        ("src/math/ExplorerMath.cpp", r"^Barycentric rasterize\(", "Edge functions and barycentric weights of a pixel, on the CPU."),
        ("src/math/ExplorerMath.cpp", r"^float interpolatePerspective\(", "Perspective-correct interpolation of an attribute."),
        ("src/rendering/FrameExplorer.cpp", r"bool FrameExplorer::open\(", "The Frame Explorer: freezes one frame and shows the 16 pipeline stages."),
    ]),
    ("7. Illumination and shading: every equation written by hand", "No built-in vector function is called in any shader (report Sections 16 and 17).", [
        ("src/rendering/ShaderMath.cpp", r"^float tsDot\(vec3 a, vec3 b\)", "The dot product, written out."),
        ("src/rendering/ShaderMath.cpp", r"^vec3 tsNormalize\(vec3 a\)", "Normalisation."),
        ("src/rendering/ShaderMath.cpp", r"^vec3 tsReflect\(vec3 d, vec3 n\)", "Reflection r = d - 2 (n.d) n."),
        ("src/rendering/ShaderMath.cpp", r"^vec3 tsRefract\(", "Snell refraction with total internal reflection."),
        ("src/rendering/Shader.cpp", r"builtinVectorCall\(source\)", "The compile step refuses a shader that still calls a built-in vector function."),
        ("src/rendering/ShaderSources.cpp", r"^void brdf\(", "The per-light reflectance function."),
        ("src/rendering/ShaderSources.cpp", r"float nl = max\(tsDot\(N, L\), 0\.0\)", "Lambert diffuse: max(0, N.L)."),
        ("src/rendering/ShaderSources.cpp", r"vec3 H = tsNormalize\(L \+ V\)", "The half vector H."),
        ("src/rendering/ShaderSources.cpp", r"spec = radiance \* uSpecular \* pow\(max\(tsDot\(N, H\)", "THE Blinn-Phong specular term: k_s (N.H)^n."),
        ("src/rendering/ShaderSources.cpp", r"uBeaconColor / \(1\.0 \+ uBeaconAtt\.x \* dist", "Point-light attenuation 1 / (1 + k_l d + k_q d^2)."),
        ("src/rendering/ShaderSources.cpp", r"float cone = tsClamp\(\(tsDot\(-L, uSpotDir\)", "Spot-light cone factor."),
        ("src/rendering/ShaderSources.cpp", r"float D = a2 / \(PI \* dd \* dd\)", "The physically based alternative: GGX distribution (key B switches)."),
        ("src/rendering/ShaderSources.cpp", r"Terms t = computeLight\(\(a \+ b \+ c\) / 3\.0, N\)", "FLAT shading: lit once per triangle."),
        ("src/rendering/ShaderSources.cpp", r"lighting per VERTEX", "GOURAUD shading: lit per vertex, colours interpolated."),
        ("src/rendering/ShaderSources.cpp", r"lighting per FRAGMENT", "PHONG shading: normal interpolated, lit per pixel."),
        ("src/rendering/ShaderSources.cpp", r"^float cascadeShadow\(", "Shadow map lookup with percentage-closer filtering."),
    ]),
    ("8. Texture, fog and tone mapping", "Report Sections 18 and 19.", [
        ("src/rendering/ShaderSources.cpp", r"^float hash21\(vec2 p\)", "Procedural textures start here: hash, value noise, fractal sum."),
        ("src/rendering/ShaderSources.cpp", r"^float fbm\(vec2 p\)", "Fractal noise used for concrete, soil, camouflage."),
        ("src/rendering/ShadersPost.cpp", r"horizon haze meets the fog colour", "Fog blended with the sky."),
        ("src/math/ToneMap.cpp", r"^Vec3 \w*(agx|aces|Agx|Aces|ACES)\w*\(", "Tone curves (AgX, ACES) on the CPU side, used for the plots and tests."),
    ]),
    ("9. Ray tracing", "The same scene traced with rays: Whitted, path and light tracing (report Section 20).", [
        ("src/raytrace/Intersect.cpp", r"^bool intersectUnitSphere\(", "Ray against the unit sphere (rays are taken into object space)."),
        ("src/raytrace/Intersect.cpp", r"^bool intersectUnitBox\(", "Ray against the unit box (slabs)."),
        ("src/raytrace/Optics.h", r"inline Vec3 reflectDir\(", "Mirror direction on the CPU."),
        ("src/raytrace/Optics.h", r"inline bool refractDir\(", "Refraction direction on the CPU."),
        ("src/raytrace/RayTracer.cpp", r"return whittedRec\(r, 0, rng, tree", "Whitted recursion starts here: shadow ray, reflection, refraction."),
        ("src/raytrace/Bvh.cpp", r"Binned SAH", "Bounding volume hierarchy built with the surface area heuristic."),
        ("src/rendering/ShadersRT.cpp", r"tsRefract\(-wo, n, eta\)", "The GPU path tracer's glass, using the hand-written refraction."),
    ]),
    ("10. Day and night", "A solar model drives sun direction, sky, fog, exposure and lamps (report Section 22).", [
        ("src/scene/DayCycle.cpp", r"const float phi = radians\(cfg\.latitudeDeg\)", "Sun elevation from latitude, declination and hour angle."),
    ]),
    ("11. Thermal camera and computer vision", "Each UAV renders a heat image; everything the defence knows comes from these pixels (report Sections 26 and 27).", [
        ("src/vision/SensorModel.cpp", r"^void SensorModel::apply\(", "The simulated sensor: blur, noise, fixed pattern, quantisation."),
        ("src/vision/CvPipeline.cpp", r"^uint8_t otsuThreshold\(", "Otsu threshold from the histogram."),
        ("src/vision/CvPipeline.cpp", r"^int despeckle\(", "Morphological clean-up of single hot pixels."),
        ("src/vision/CvPipeline.cpp", r"^int labelComponents\(", "Connected components with union-find."),
        ("src/vision/CvPipeline.cpp", r"^BlobClass classifyBlob\(", "Blob classification: rover, person, tracer."),
        ("src/ai/DroneController.cpp", r"imagePointToGround\(", "Back-projection: pixel to a ray to a point on the ground."),
        ("src/vision/MultiTracker.cpp", r"^void MultiTracker::predict\(", "Kalman prediction of every track."),
        ("src/vision/MultiTracker.cpp", r"^float MultiTracker::mahalanobis2\(", "Gating by Mahalanobis distance."),
        ("src/vision/MultiTracker.cpp", r"MultiTracker::hungarian\(", "Hungarian assignment of measurements to tracks."),
        ("src/vision/MultiTracker.cpp", r"^void MultiTracker::kalmanUpdate\(", "Kalman update."),
        ("src/vision/MultiTracker.cpp", r"^bool MultiTracker::triangulate\(", "Two cameras, two rays: triangulation of the target."),
    ]),
    ("12. The UAV squad: single and combined strike", "Report Section 27, Algorithm 6.", [
        ("src/ai/Squad.cpp", r"p\.baseRadius \+ p\.amplitude \* std::sin", "The patrol: a breathing orbit, the two UAVs half a turn apart."),
        ("src/ai/Squad.cpp", r"3a\. A second intruder far from the target", "The split rule: a second, urgent intruder takes the nearer UAV (single strikes)."),
        ("src/ai/Squad.cpp", r"^void Squad::planStrike\(", "The combined strike: flight times, the farther UAV launches first."),
        ("src/ai/Squad.cpp", r"strike_\.launchAt\[who\[k\]\] = time \+ \(tFar - T\[k\]\)", "Time on target in one line."),
        ("src/scene/Combat.cpp", r"^void Combat::fireMissile\(", "A round is launched; it homes on its UAV's laser spot."),
        ("src/scene/Scene.cpp", r"Soldiers on duty co-operate with the UAVs", "Guards at their posts fire on the tracked intruder."),
        ("src/scene/Scene.cpp", r"w\.alert = 0\.6f", "The alert posture: weapons ready, laid on the squad's target."),
    ]),
    ("13. Intruder rovers: MicroMouse planning and line following", "Report Section 29.", [
        ("src/ai/HideController.cpp", r"^void HideController::floodFill\(\)", "Flood fill (breadth-first) from the hidden cells: the MicroMouse layer."),
        ("src/ai/HideController.cpp", r"^void HideController::floodFillToObjective\(", "Infiltration: Dijkstra towards the objective."),
        ("src/ai/HideController.cpp", r"const int step = 16 \+ \(exposed\[ni\] \? exposure : 0\)", "The step cost: cells a UAV can see are expensive, so routes bend into cover."),
        ("src/ai/HideController.cpp", r"^void HideController::replan\(", "Re-planning: the route is recomputed with the UAV positions of the moment."),
        ("src/ai/HideController.cpp", r"const float omega = clampf\(kKp \* error \+ kKd", "The line follower: PD steering from the sensor error."),
        ("src/ai/HideController.cpp", r"constexpr float kKp = ", "Its gains."),
    ]),
    ("14. Soldiers: route planning, gait, guards", "Report Sections 24 and 29.", [
        ("src/scene/Garrison.cpp", r"^bool SitePlanner::gridPath\(", "A* over the site grid."),
        ("src/scene/Garrison.cpp", r"SitePlanner::stringPull\(", "String pulling: needless corners removed."),
        ("src/math/Spline.cpp", r"CatmullRom", "Centripetal Catmull-Rom spline through the corners."),
        ("src/scene/Gait.cpp", r"^bool legIK\(", "Two-link inverse kinematics of a leg."),
        ("src/scene/Door.cpp", r"DoorKinematics::DoorKinematics\(", "Hinged doors: a critically damped spring that waits for bodies in its path."),
    ]),
    ("15. Explosions: particles, physics, blackbody colour", "Report Section 28.", [
        ("src/scene/Combat.cpp", r"^void Combat::explodeImpl\(", "One blast: what is spawned (flash, fire, smoke, sparks, embers, debris)."),
        ("src/scene/Combat.cpp", r"^void Combat::updateParticles\(", "The integrators: buoyancy, gravity, drag, bounce."),
        ("src/scene/Combat.cpp", r"case EffectType::Fire:\s*p\.velocity\.y \+=", "Fire rises: buoyancy term."),
        ("src/rendering/Effects.cpp", r"^vec3 bb\(float T\)", "The colour ramp: temperature to red, then green, then blue."),
        ("src/rendering/Effects.cpp", r"emit = bb\(T\) \* \(0\.5 \+ 5\.0 \* T \* T\)", "HDR emission of a fire particle."),
        ("src/scene/Combat.cpp", r"kMissileTurnRate", "Proportional navigation of a round."),
    ]),
    ("16. Control: getting into vehicles", "Report Sections 5 and 27.4.", [
        ("src/app/AppInteract.cpp", r"^bool App::boardVehicle\(", "E on foot at a vehicle: the soldier gets in."),
        ("src/app/AppInteract.cpp", r"^bool App::exitVehicle\(", "E inside: he is placed at the first free spot beside it."),
        ("src/objects/Tank.cpp", r"yawRate_ = \(vr_ - vl_\) / kTrackSeparation", "Tank skid steering from the two track speeds."),
        ("src/objects/Tank.cpp", r"w\.combat->fireShell\(", "The tank gun fires."),
        ("src/objects/Launchers.cpp", r"On alert with nobody at the controls", "Launchers on alert: raised and sweeping."),
        ("src/objects/Helicopter.cpp", r"kAlertIdle : 0\.0f", "Helicopter at flight idle while unmanned."),
        ("src/scene/KeyMap.cpp", r"KeyBinding|\{\"", "Every key and its context."),
    ]),
    ("17. Evidence: tests and the report's generators", "", [
        ("tests/test_main.cpp", r"int main\(", "396 automated tests (run with `make test`)."),
        ("tools/report/check_numbers.py", r"^def expect\(", "The report's hand-typed numbers checked against the code."),
        ("tools/report/gen_designs.py", r"^def chain_check\(", "Every printed transformation chain checked against the exported world matrix."),
        ("src/app/Record.cpp", r"^int App::recordMode\(", "The film recorder (scripted takes, camera views, event log)."),
    ]),
]


def find(path, pattern):
    full = os.path.join(ROOT, path)
    if not os.path.exists(full):
        sys.exit("tour: %s does not exist" % path)
    rx = re.compile(pattern)
    for i, line in enumerate(open(full, errors="replace")):
        if rx.search(line):
            return i + 1
    text = open(full, errors="replace").read()
    m = re.compile(pattern, re.S).search(text)   # a pattern across a line break
    if m:
        return text.count("\n", 0, m.start()) + 1
    sys.exit("tour: pattern %r not found in %s" % (pattern, path))


def main():
    out = ["# ThermalSense: code tour", "",
           "A walk through the project in the order of the graphics pipeline, then through the advanced features. Each stop is a link "
           "to the exact line: in VS Code open this file's preview (Ctrl+Shift+V) and click, on GitHub click. "
           "The companion file [`REPORT_CODE_MAP.md`](REPORT_CODE_MAP.md) maps every section of the report to its code.", "",
           "Generated by `tools/report/gen_code_tour.py`; the line numbers are searched in the code of this commit.", ""]
    n = 0
    for title, intro, stops in TOUR:
        out += ["## " + title, ""]
        if intro:
            out += [intro, ""]
        out += ["| # | Open | What it shows |", "|---|---|---|"]
        for path, pattern, say in stops:
            n += 1
            ln = find(path, pattern)
            out.append("| %d | [`%s:%d`](../%s#L%d) | %s |" % (n, path, ln, path, ln, say))
        out.append("")
    open(OUT, "w").write("\n".join(out) + "\n")
    print("wrote", OUT, n, "stops")


if __name__ == "__main__":
    main()
