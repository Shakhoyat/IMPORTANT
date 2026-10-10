#!/usr/bin/env python3
"""Computes line ranges in the REAL source files and writes sections/gen_listings.tex.

Each entry: key, file, a regex for the first line, and how the block ends (brace matching from the first line that
contains '{', or an explicit end regex). The report then does \\lstinputlisting[firstline=..,lastline=..] on the file itself,
so a listing can never differ from the source; if the code moves, re-run this script.
Output macros: \\code<key> = the listing command; \\ref text: file:first-last in the caption.
"""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = "src/"
# (key, file, start regex, end: 'brace' | ('re', regex) | int lines, language, caption)
ITEMS = [
 ("Cube", S+"primitives/Cube.cpp", r"^MeshData createCube", "brace", "C++", "Cube generator: 24 vertices, 6 faces"),
 ("Sphere", S+"primitives/Sphere.cpp", r"^MeshData createSphere", "brace", "C++", "Sphere generator (northern hemisphere, then reflection)"),
 ("Cylinder", S+"primitives/Cylinder.cpp", r"^MeshData createCylinder", "brace", "C++", "Cylinder / cone / frustum generator"),
 ("Perspective", S+"math/Math3D.cpp", r"^Mat4 perspective", "brace", "C++", "Perspective projection matrix"),
 ("LookAt", S+"math/Math3D.cpp", r"^Mat4 lookAt", "brace", "C++", "View matrix lookAt"),
 ("Part", S+"objects/Builder.cpp", r"^SceneNode\* Builder::part", "brace", "C++", "Builder::part: one table row becomes one node"),
 ("Transform", S+"scene/Transform.h", r"Mat4 matrix\(\) const", 1, "C++", "Local matrix M = T R S"),
 ("UpdateNode", S+"scene/Scene.cpp", r"^void updateNode", "brace", "C++", "World matrix = parent world times local (and the explode view)"),
 ("Brdf", S+"rendering/ShaderSources.cpp", r"^void brdf\(", "brace", "GLSL", "Blinn-Phong and Cook-Torrance (GGX) in one function"),
 ("FlatGeom", S+"rendering/ShaderSources.cpp", r"^const char\* kFlatGeom", ("re", r"^\)\";"), "GLSL", "Flat shading: one normal per triangle in a geometry shader"),
 ("GouraudVert", S+"rendering/ShaderSources.cpp", r"^const char\* kGouraudVert", ("re", r"^\)\";"), "GLSL", "Gouraud shading: lighting per vertex"),
 ("Practical", S+"rendering/ShaderSources.cpp", r"^float practicalAttenuation", "brace", "GLSL", "Practical light attenuation (windowed inverse square)"),
 ("Indoor", S+"rendering/ShaderSources.cpp", r"^float indoorMask", "brace", "GLSL", "Indoor mask"),
 ("MoonShadow", S+"rendering/ShaderSources.cpp", r"^float moonShadow", "brace", "GLSL", "Cascaded moon shadow lookup with PCF"),
 ("Bump", S+"rendering/ShaderSources.cpp", r"^vec3 surfaceBump", "brace", "GLSL", "Procedural bump: height gradient tilts the normal"),
 ("Aces", S+"rendering/ShadersPostFx.cpp", r"^vec3 acesFitted", "brace", "GLSL", "Fitted ACES tone-mapping curve"),
 ("Ssr", S+"rendering/ShadersPostFx.cpp", r"^vec3 reflection\(", "brace", "GLSL", "Screen-space reflection: ray march, bisection, Fresnel, edge fade"),
 ("Flood", S+"ai/HideController.cpp", r"^void HideController::floodFill\(\)", "brace", "C++", "MicroMouse flood fill (breadth-first search) from the hiding cells"),
 ("Dijkstra", S+"ai/HideController.cpp", r"^void HideController::floodFillToObjective", "brace", "C++", "Weighted flood fill (Dijkstra) from the objective: distance + exposure + noise"),
 ("Descend", S+"ai/HideController.cpp", r"^std::vector<Vec3> HideController::descend", "brace", "C++", "Steepest descent through the distance field"),
 ("Chaikin", S+"ai/HideController.cpp", r"^std::vector<Vec3> chaikin", "brace", "C++", "Chaikin corner cutting"),
 ("FollowLine", S+"ai/HideController.cpp", r"^void HideController::followLine", "brace", "C++", "Line follower: IR sensors, PD steering, differential drive"),
 ("Otsu", S+"vision/CvPipeline.cpp", r"^uint8_t otsuThreshold", "brace", "C++", "Otsu's threshold"),
 ("Despeckle", S+"vision/CvPipeline.cpp", r"^int despeckle", "brace", "C++", "Despeckle"),
 ("Label", S+"vision/CvPipeline.cpp", r"^int labelComponents", "brace", "C++", "Two-pass union-find labelling"),
 ("Detect", S+"vision/CvPipeline.cpp", r"^std::vector<Measurement2D> detectTargets", "brace", "C++", "The image stage: threshold, despeckle, label, moments, filter"),
 ("RayGround", S+"ai/DroneController.cpp", r"^bool DroneController::imagePointToGround", "brace", "C++", "Pixel ray to ground point"),
 ("Predict", S+"vision/MultiTracker.cpp", r"^void MultiTracker::predict", "brace", "C++", "Kalman prediction with white-noise-acceleration Q"),
 ("Kalman", S+"vision/MultiTracker.cpp", r"^void MultiTracker::kalmanUpdate", "brace", "C++", "Kalman measurement update"),
 ("Mahal", S+"vision/MultiTracker.cpp", r"^float MultiTracker::mahalanobis2", "brace", "C++", "Mahalanobis distance for gating"),
 ("Hungarian", S+"vision/MultiTracker.cpp", r"^std::vector<int> MultiTracker::hungarian", "brace", "C++", "Hungarian algorithm"),
 ("Triangulate", S+"vision/MultiTracker.cpp", r"^bool MultiTracker::triangulate", "brace", "C++", "Two-view triangulation"),
 ("FlightTime", S+"ai/Squad.cpp", r"^float roundFlightTime", "brace", "C++", "Round flight-time model for time-on-target"),
 ("SunDir", S+"scene/DayCycle.cpp", r"^Vec3 DayCycle::sunDirection", "brace", "C++", "Solar position: hour angle, latitude and declination to the unit vector towards the sun"),
 ("AirMass", S+"scene/DayCycle.cpp", r"^float DayCycle::airMass", 9, "C++", "Kasten--Young air mass and the per-channel sun transmittance"),
 ("ApparentHeat", S+"scene/DayCycle.cpp", r"^float apparentHeat", 19, "C++", "Solar loading of passive surfaces, cloud density and cloud shadow (CPU mirror of the shader)"),
 ("SplineSeg", S+"math/Spline.cpp", r"^Eval evalSegment", "brace", "C++", "Centripetal Catmull--Rom segment as a cubic Hermite curve (position, first and second derivative)"),
 ("ArcLen", S+"math/Spline.cpp", r"^float CatmullRomPath::paramAtDistance", "brace", "C++", "Arc-length parameterisation: binary search in the cumulative chord table"),
 ("LegIK", S+"scene/Gait.cpp", r"^bool legIK", 15, "C++", "Two-link leg IK (law of cosines) and its forward kinematics"),
 ("FootPath", S+"scene/Gait.cpp", r"^void footInPelvis", "brace", "C++", "Ankle trajectory: world-locked stance, cycloid swing with a sine lift"),
 ("ArmIK", S+"scene/Gait.cpp", r"^ArmIk armIK", "brace", "C++", "Two-bone arm IK with a pole vector (rifle hold), result as Euler angles of the shoulder"),
 ("AStar", S+"scene/Garrison.cpp", r"^bool SitePlanner::gridPath", "brace", "C++", "Site planner: 8-connected A* with the octile heuristic and no corner cutting"),
 ("StringPull", S+"scene/Garrison.cpp", r"^std::vector<int> SitePlanner::stringPull", "brace", "C++", "Greedy string pulling with sampled clearance checks"),
 ("SpeedLaw", S+"scene/Garrison.cpp", r"^void Soldier::stepRoute", 42, "C++", "Route following: curvature speed law with look-ahead braking"),
 ("RandomRoute", S+"scene/Mission.cpp", r"^void Mission::randomRoute", "brace", "C++", "Random route of an intruder: ring spawn, via sector, personality, objective switch"),
 ("DeriveSeed", S+"scene/Scene.cpp", r"^uint32_t deriveSeed", "brace", "C++", "Independent random streams from one run seed (splitmix32 finaliser)"),
 ("SensorApply", S+"vision/SensorModel.cpp", r"^void SensorModel::apply", "brace", "C++", "SensorModel::apply: PSF, fixed pattern, NETD, quantisation, stuck pixels and bad-pixel replacement"),
 ("Classify", S+"vision/CvPipeline.cpp", r"^BlobClass classifyBlob", "brace", "C++", "Blob classes: target, hot clutter, person, clutter"),
]
def find(lines, rx, start=0):
    r = re.compile(rx)
    for i in range(start, len(lines)):
        if r.search(lines[i]): return i
    return -1
out = ["% generated by report/listings.py -- do not edit\n"]
bad = 0
for key, f, rx, end, lang, cap in ITEMS:
    lines = open(os.path.join(ROOT, f), encoding="utf-8").read().split("\n")
    a = find(lines, rx)
    if a < 0:
        print("NOT FOUND", key, rx); bad += 1; continue
    if isinstance(end, int): b = a + end - 1
    elif end == "brace":
        depth = 0; seen = False; b = a
        for i in range(a, len(lines)):
            depth += lines[i].count("{") - lines[i].count("}")
            if "{" in lines[i]: seen = True
            if seen and depth <= 0: b = i; break
    else:
        b = find(lines, end[1], a + 1)
        if b < 0: b = a + 40
    n = b - a + 1
    if n > 75: print("LONG", key, n)
    fl, ll = a + 1, b + 1
    out.append("\\newcommand{\\lst%s}{\\lstinputlisting[language=%s,firstline=%d,lastline=%d,caption={%s \\textnormal{(\\texttt{%s}, lines %d--%d)}},label={lst:%s}]{../%s}}\n"
               % (key, lang, fl, ll, cap, f.replace("_", "\\_"), fl, ll, key, f))
    print("%-12s %s:%d-%d (%d lines)" % (key, f, fl, ll, n))
open(os.path.join(ROOT, "report/sections/gen_listings.tex"), "w").write("".join(out))
sys.exit(1 if bad else 0)
