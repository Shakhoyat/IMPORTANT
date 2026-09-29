#include "objects/Rover.h"

#include "objects/Builder.h"
#include "objects/PropKit.h"

namespace ts {

RoverRig buildRover() {
    using P = PrimitiveType;
    Builder b("Target Rover", "ROV", ShadingModel::Phong);
    RoverRig rig;
    rig.root = b.root("Target Rover", {0, 0, 0}, "Ground robot hiding from the UAV");
    rig.root->animation = "Path from flood-fill planner, driven by line-follower controller";
    SceneNode* r = rig.root.get();

    const Material paint = withPbr({{0.34f, 0.37f, 0.26f}, 0.46f, 0.0f, 0.35f, 32.0f}, Surface::Paint, 0.45f, 0.5f);
    const Material darkPaint = withPbr({{0.20f, 0.22f, 0.17f}, 0.50f, 0.0f, 0.30f, 32.0f}, Surface::Paint, 0.5f, 0.5f);
    const Material motorBox = withPbr({{0.16f, 0.16f, 0.17f}, 0.97f, 0.0f, 0.40f, 48.0f}, Surface::Metal, 0.4f, 0.8f);
    const Material rubber = withPbr({{0.06f, 0.06f, 0.06f}, 0.74f, 0.0f, 0.05f, 8.0f}, Surface::Rubber, 0.9f, 0.0f);
    const Material metal = withPbr({{0.55f, 0.56f, 0.58f}, 0.52f, 0.0f, 0.70f, 64.0f}, Surface::Metal, 0.3f, 0.9f);
    const Material sensor = withPbr({{0.10f, 0.12f, 0.16f}, 0.60f, 0.0f, 0.90f, 96.0f}, Surface::Glass, 0.08f, 0.0f);
    const Material irLed = withPbr({{0.90f, 0.15f, 0.12f}, 0.62f, 0.6f, 0.5f, 64.0f}, Surface::Glass, 0.1f, 0.0f);

    b.part(r, "Chassis", P::Cube, {0.42f, 0.10f, 0.62f}, {0, 0, 0}, {0, 0.14f, 0}, paint, "Lower frame");
    b.part(r, "Upper hull", P::Cube, {0.34f, 0.10f, 0.40f}, {0, 0, 0}, {0, 0.24f, -0.04f}, darkPaint, "Electronics bay");
    b.part(r, "Motor + battery box", P::Cube, {0.22f, 0.07f, 0.16f}, {0, 0, 0}, {0, 0.325f, -0.14f}, motorBox,
           "Hottest part: strongest thermal signature");
    b.part(r, "Front bumper", P::Cube, {0.44f, 0.05f, 0.04f}, {0, 0, 0}, {0, 0.12f, 0.33f}, darkPaint, "Bumper");

    // Facing +Z with Y up, the robot's left side is +X.
    // Six wheels: cylinder axis (local Z) turned onto the X axle by yaw 90; spin = roll about the axle.
    const float wheelZ[3] = {-0.22f, 0.0f, 0.22f};
    for (int i = 0; i < 3; ++i) {
        rig.wheelsLeft[i] = b.part(r, "Wheel L", P::Cylinder, {0.16f, 0.16f, 0.07f}, {0, 90, 0},
                                   {kRoverTrackWidth * 0.5f, kRoverWheelRadius, wheelZ[i]}, rubber, "Drive wheel (left side)");
        rig.wheelsRight[i] = b.part(r, "Wheel R", P::Cylinder, {0.16f, 0.16f, 0.07f}, {0, 90, 0},
                                    {-kRoverTrackWidth * 0.5f, kRoverWheelRadius, wheelZ[i]}, rubber, "Drive wheel (right side)");
        rig.wheelsLeft[i]->animation = "Spins with left track speed";
        rig.wheelsRight[i]->animation = "Spins with right track speed";
        b.part(r, "Hub L", P::Sphere, {0.07f, 0.07f, 0.03f}, {0, 90, 0}, {0.29f, kRoverWheelRadius, wheelZ[i]}, metal, "Wheel hub");
        b.part(r, "Hub R", P::Sphere, {0.07f, 0.07f, 0.03f}, {0, 90, 0}, {-0.29f, kRoverWheelRadius, wheelZ[i]}, metal, "Wheel hub");
    }

    // Line-follower IR sensor array (5 sensors) under the front bumper.
    b.part(r, "IR sensor bar", P::Cube, {0.30f, 0.02f, 0.04f}, {0, 0, 0}, {0, 0.07f, 0.30f}, darkPaint, "LFR sensor board");
    for (int i = 0; i < 5; ++i) {
        const float x = -0.12f + 0.06f * static_cast<float>(i);
        rig.irSensors[i] = b.part(r, "IR sensor", P::Sphere, {0.025f, 0.025f, 0.025f}, {0, 0, 0}, {x, 0.055f, 0.31f},
                                  irLed, "Reflectance sensor (lights when on the line)");
    }

    // Sensor mast with a rotating scanner head (small hierarchy: mast -> pivot -> head).
    b.part(r, "Sensor mast", P::Cylinder, {0.025f, 0.025f, 0.16f}, {90, 0, 0}, {0.08f, 0.37f, 0.1f}, metal, "Mast");
    rig.scannerPivot = b.group(r, "Scanner pivot", {0.08f, 0.46f, 0.1f}, {0, 0, 0}, "Rotating scanner mount");
    rig.scannerPivot->animation = "Yaw rotates continuously";
    b.part(rig.scannerPivot, "Scanner head", P::Sphere, {0.07f, 0.07f, 0.07f}, {0, 0, 0}, {0, 0, 0}, sensor, "Acoustic/RF sensor that hears the UAV");
    b.part(rig.scannerPivot, "Scanner window", P::Cube, {0.05f, 0.03f, 0.02f}, {0, 0, 0}, {0, 0, 0.035f}, sensor, "Sensor aperture");
    b.part(r, "Whip antenna", P::Cylinder, {0.012f, 0.012f, 0.30f}, {90, 0, 0}, {-0.13f, 0.44f, -0.2f}, metal, "Radio antenna");
    return rig;
}

}  // namespace ts
