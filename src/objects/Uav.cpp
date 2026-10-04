#include "objects/Uav.h"

#include "objects/Builder.h"
#include "objects/PropKit.h"

namespace ts {

UavRig buildUav() {
    using P = PrimitiveType;
    Builder b("Autonomous UAV", "UAV", ShadingModel::Phong);
    UavRig rig;
    rig.root = b.root("Autonomous UAV", {0, 5.5f, 11}, "Patrol quadcopter");
    rig.root->animation = "Position/attitude from the pursuit controller";
    SceneNode* r = rig.root.get();

    const Material shell = withPbr({{0.62f, 0.64f, 0.66f}, 0.42f, 0.0f, 0.85f, 96.0f}, Surface::Paint, 0.3f, 0.2f);
    const Material carbon = withPbr({{0.08f, 0.08f, 0.09f}, 0.40f, 0.0f, 0.60f, 64.0f}, Surface::Plain, 0.35f, 0.1f);
    const Material battery = withPbr({{0.14f, 0.15f, 0.20f}, 0.62f, 0.0f, 0.40f, 48.0f}, Surface::Plain, 0.5f, 0.0f);
    const Material motor = withPbr({{0.45f, 0.46f, 0.50f}, 0.86f, 0.0f, 0.90f, 128.0f}, Surface::Metal, 0.3f, 0.9f);
    const Material prop = withPbr({{0.10f, 0.10f, 0.11f}, 0.30f, 0.0f, 0.30f, 32.0f}, Surface::Plain, 0.4f, 0.0f);
    const Material ledRed = withPbr({{1.0f, 0.1f, 0.05f}, 0.55f, 1.0f, 0.4f, 64.0f}, Surface::Glass, 0.1f, 0.0f);
    const Material ledGreen = withPbr({{0.1f, 1.0f, 0.25f}, 0.55f, 1.0f, 0.4f, 64.0f}, Surface::Glass, 0.1f, 0.0f);
    const Material lamp = withPbr({{0.30f, 0.31f, 0.33f}, 0.70f, 0.0f, 0.80f, 96.0f}, Surface::Metal, 0.35f, 0.8f);
    const Material lens = withPbr({{1.0f, 0.97f, 0.85f}, 0.75f, 1.0f, 1.0f, 128.0f}, Surface::Glass, 0.05f, 0.0f);
    const Material beam = withPbr({{1.0f, 0.95f, 0.80f}, 0.0f, 1.0f, 0.0f, 1.0f}, Surface::Plain, 1.0f, 0.0f);
    Material rotorBlur{{0.10f, 0.10f, 0.11f}, 0.30f, 0.0f, 0.2f, 16.0f};
    rotorBlur.opacity = 0.32f;
    rotorBlur.roughness = 0.5f;
    Material strobe = asLight(Material{{1.0f, 1.0f, 1.0f}, 0.50f, 1.0f, 0.5f, 64.0f}, 2.0f, 3.5f);
    strobe.surface = Surface::Glass;
    strobe.roughness = 0.1f;

    // Central body
    b.part(r, "Fuselage", P::Cube, {0.16f, 0.07f, 0.26f}, {0, 0, 0}, {0, 0, 0}, carbon, "Frame centre plate");
    b.part(r, "Canopy", P::Sphere, {0.15f, 0.08f, 0.22f}, {0, 0, 0}, {0, 0.04f, 0.01f}, shell, "Aerodynamic cover");
    b.part(r, "Battery", P::Cube, {0.10f, 0.04f, 0.16f}, {0, 0, 0}, {0, -0.055f, -0.02f}, battery, "LiPo pack");
    b.part(r, "GPS mast", P::Cylinder, {0.008f, 0.008f, 0.05f}, {90, 0, 0}, {0, 0.07f, -0.07f}, carbon, "GPS support");
    b.part(r, "GPS puck", P::Cylinder, {0.05f, 0.05f, 0.012f}, {90, 0, 0}, {0, 0.098f, -0.07f}, shell, "GPS receiver");

    // Four arms at 45, 135, 225, 315 degrees; motor + spinning rotor at each tip.
    // Yaw measured from +Z (forward); with Y up, +X is the UAV's left side.
    const float armYaw[4] = {45.0f, -45.0f, 135.0f, -135.0f};
    const char* armName[4] = {"Front-left", "Front-right", "Rear-left", "Rear-right"};
    for (int i = 0; i < 4; ++i) {
        const float a = radians(armYaw[i]);
        const Vec3 dir{std::sin(a), 0.0f, std::cos(a)};
        b.part(r, std::string("Arm ") + armName[i], P::Cube, {0.025f, 0.018f, 0.22f}, {0, armYaw[i], 0}, dir * 0.15f,
               carbon, "Carbon arm");
        SceneNode* mount = b.group(r, std::string("Motor mount ") + armName[i], dir * 0.26f + Vec3{0, 0.012f, 0},
                                   {0, 0, 0}, "Arm tip");
        b.part(mount, "Motor", P::Cylinder, {0.045f, 0.045f, 0.035f}, {90, 0, 0}, {0, 0.02f, 0}, motor, "Brushless motor (hot)");
        rig.rotors[i] = b.group(mount, "Rotor", {0, 0.045f, 0}, {0, 0, 0}, "Spinning rotor");
        rig.rotors[i]->animation = "Yaw spins at ~40 rev/s (alternating direction)";
        b.part(rig.rotors[i], "Prop hub", P::Sphere, {0.025f, 0.025f, 0.025f}, {0, 0, 0}, {0, 0, 0}, motor, "Spinner");
        b.part(rig.rotors[i], "Prop blade", P::Cube, {0.20f, 0.004f, 0.018f}, {0, 0, 0}, {0, 0, 0}, prop, "Two-blade propeller");
        // Motion-blur disc swept by the spinning blades (translucent; what the eye sees at ~40 rev/s).
        SceneNode* disc = b.part(mount, "Rotor blur disc", P::Cylinder, {0.205f, 0.205f, 0.002f}, {90, 0, 0},
                                 {0, 0.045f, 0}, rotorBlur, "Translucent disc swept by the propeller");
        disc->explodable = false;
        // Navigation light under each front arm: red on the left, green on the right.
        if (i < 2) {
            b.part(mount, "Nav LED", P::Sphere, {0.018f, 0.018f, 0.018f}, {0, 0, 0}, {0, -0.012f, 0},
                   i == 0 ? ledRed : ledGreen, "Navigation light");
        }
    }

    // Anti-collision strobe on the tail: double white flash every second (practical light).
    SceneNode* s = b.part(r, "Strobe", P::Sphere, {0.022f, 0.016f, 0.022f}, {0, 0, 0}, {0, 0.02f, -0.135f}, strobe,
                          "Anti-collision strobe (practical light)");
    s->animation = "Double flash every second";

    // Landing gear: four vertical struts + two skids (cylinders).
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            b.part(r, "Landing strut", P::Cylinder, {0.012f, 0.012f, 0.09f}, {90, 0, 0},
                   {0.06f * sx, -0.08f, 0.07f * sz}, carbon, "Strut");
        }
        b.part(r, "Landing skid", P::Cylinder, {0.014f, 0.014f, 0.24f}, {0, 0, 0}, {0.06f * sx, -0.125f, 0}, carbon, "Skid tube");
    }

    // Searchlight / thermal camera gimbal: UAV -> yaw node -> pitch node -> lamp, lens, camera, beam.
    rig.gimbalYaw = b.group(r, "Gimbal yaw", {0, -0.06f, 0.1f}, {0, 0, 0}, "Pan axis");
    rig.gimbalYaw->animation = "Yaw from visual-servo controller";
    b.part(rig.gimbalYaw, "Gimbal yoke", P::Cube, {0.07f, 0.03f, 0.05f}, {0, 0, 0}, {0, -0.015f, 0}, carbon, "Yoke");
    rig.gimbalPitch = b.group(rig.gimbalYaw, "Gimbal pitch", {0, -0.05f, 0}, {45, 0, 0}, "Tilt axis");
    rig.gimbalPitch->animation = "Pitch from visual-servo controller";
    b.part(rig.gimbalPitch, "Lamp housing", P::Cylinder, {0.06f, 0.06f, 0.08f}, {0, 0, 0}, {0, 0, 0}, lamp, "Searchlight body");
    b.part(rig.gimbalPitch, "Lens", P::Sphere, {0.05f, 0.05f, 0.015f}, {0, 0, 0}, {0, 0, kLampFrontOffset}, lens,
           "Spotlight source L2");
    b.part(rig.gimbalPitch, "Thermal camera", P::Cube, {0.03f, 0.025f, 0.035f}, {0, 0, 0}, {0, 0.042f, 0.0f}, carbon,
           "Camera used by the tracker");

    // Launcher: a rail under each side of the body carrying two small rounds (body cylinder, cone nose,
    // four fins). A launched round's group is hidden and comes back after the reload time.
    const Material rail = withPbr({{0.20f, 0.21f, 0.20f}, 0.40f, 0.0f, 0.5f, 64.0f}, Surface::Metal, 0.45f, 0.7f);
    const Material round = withPbr({{0.32f, 0.34f, 0.28f}, 0.45f, 0.0f, 0.5f, 64.0f}, Surface::Paint, 0.4f, 0.1f);
    const Material seeker = withPbr({{0.05f, 0.05f, 0.06f}, 0.45f, 0.0f, 0.9f, 128.0f}, Surface::Glass, 0.08f, 0.0f);
    for (int side = 0; side < 2; ++side) {
        const float sx = side == 0 ? 1.0f : -1.0f;
        SceneNode* pylon = b.group(r, side == 0 ? "Launcher rail L" : "Launcher rail R", {0.105f * sx, -0.075f, 0.0f},
                                   {0, 0, 0}, "Under-body launcher rail");
        b.part(pylon, "Rail", P::Cube, {0.012f, 0.012f, 0.22f}, {0, 0, 0}, {0, 0.012f, 0}, rail, "Launch rail");
        b.part(pylon, "Rail strut", P::Cube, {0.008f, 0.02f, 0.008f}, {0, 0, 0}, {0, 0.025f, 0.05f}, rail, "Strut to the frame");
        for (int k = 0; k < 2; ++k) {
            const int idx = side * 2 + k;
            SceneNode* m = b.group(pylon, "Round", {0.022f * (k == 0 ? -1.0f : 1.0f), -0.008f, 0.0f}, {0, 0, 0},
                                   "One round on the rail");
            m->animation = "Hidden at launch, reappears after the reload";
            b.part(m, "Round body", P::Cylinder, {0.022f, 0.022f, 0.17f}, {0, 0, 0}, {0, 0, 0}, round, "Airframe tube");
            b.part(m, "Round nose", P::Cone, {0.022f, 0.022f, 0.04f}, {0, 0, 0}, {0, 0, 0.105f}, seeker, "Seeker dome");
            for (int f = 0; f < 4; ++f) {
                b.part(m, "Round fin", P::Cube, {0.002f, 0.03f, 0.03f}, {0, 0, 45.0f + 90.0f * f}, {0, 0, -0.07f}, rail,
                       "Tail fin");
            }
            rig.missiles[idx] = m;
        }
    }

    b.setShading(ShadingModel::Beam);
    rig.beam = b.part(rig.gimbalPitch, "Light cone", P::BeamCone, {1, 1, 1}, {0, 0, 0}, {0, 0, 1}, beam,
                      "Visible cone of the spotlight");
    rig.beam->explodable = false;
    rig.beam->animation = "Length follows distance to the ground";
    return rig;
}

}  // namespace ts
