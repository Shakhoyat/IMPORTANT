#include "objects/Soldier.h"

#include <string>

#include "objects/Builder.h"
#include "objects/PropKit.h"
#include "scene/Gait.h"

namespace ts {

SoldierRig buildSoldier(int index, const SoldierLook& look) {
    using P = PrimitiveType;
    namespace S = soldier;
    const std::string object = "Soldier " + std::to_string(index + 1);
    Builder b(object, "SLD" + std::to_string(index + 1), ShadingModel::Phong);
    SoldierRig r;
    r.owner = b.root(object, Vec3{0, 0, 0}, "On-duty soldier of the camp garrison (friendly, possessable)");
    r.owner->animation = "Follows its duty route (Catmull-Rom spline), heading = path tangent";
    r.root = r.owner.get();

    const GaitParams g;
    const float L1 = g.thigh, L2 = g.shin, wb = look.build;
    // Heat: exposed skin 0.82 (warm, below the 0.90 target core), clothed torso 0.70, limbs a little cooler.
    const Material skin = makeMaterial(look.skin, S::kHeatSkin, Surface::Plain, 0.55f, 0.0f);
    // v7: woodland camouflage cloth (roughness > 0.94 = the shader's fine cloth print, no sheen), shirt a shade lighter than the trousers, knee / seat wear.
    const Material torso = makeMaterial(look.uniform, S::kHeatTorso, Surface::Camo, 0.95f, 0.0f);
    const Material sleeve = makeMaterial(look.uniform * 1.06f, 0.68f, Surface::Camo, 0.95f, 0.0f);
    const Material trousers = makeMaterial(look.uniform * 0.90f, 0.66f, Surface::Camo, 0.96f, 0.0f);
    // Leather combat boots: dark brown, a little polish on the toe caps.
    const Material boots = makeMaterial({0.075f, 0.055f, 0.04f}, 0.50f, Surface::Rubber, 0.55f, 0.0f);
    // Helmet with a cloth cover (camouflage, matte); the rim is the bare painted shell.
    const Material helmet = makeMaterial(look.helmet, 0.58f, Surface::Camo, 0.95f, 0.0f);
    const Material helmetRim = makeMaterial(look.helmet * 0.7f, 0.58f, Surface::Paint, 0.6f, 0.1f);
    // Canvas pack and bedroll, coyote-tan nylon webbing with a slight sheen.
    const Material gear = makeMaterial(look.uniform * 0.8f + Vec3{0.05f, 0.04f, 0.01f}, 0.52f, Surface::Plain, 0.9f, 0.0f);
    const Material webbing = makeMaterial({0.27f, 0.235f, 0.155f}, 0.60f, Surface::Plain, 0.72f, 0.0f);
    // Polymer furniture + phosphate steel: dark, slightly worn (higher roughness than bare metal).
    const Material gun = makeMaterial({0.07f, 0.07f, 0.075f}, 0.45f, Surface::Metal, 0.5f, 0.7f);
    const Material wrenchMat = makeMaterial({0.62f, 0.63f, 0.66f}, 0.40f, Surface::Metal, 0.3f, 0.9f);
    // Fingerless tactical gloves: dark fabric on the palm (the fingertips keep the warm skin signature, heat 0.82).
    const Material glove = makeMaterial({0.11f, 0.10f, 0.085f}, S::kHeatSkin, Surface::Plain, 0.8f, 0.0f);

    // Capsule along the parent's -Y axis from 0 to -len: cylinder + the end-joint sphere of the same radius.
    auto capsule = [&](SceneNode* parent, const char* cylName, const char* jointName, float radius, float len,
                       const Material& m, const char* purpose) {
        b.part(parent, jointName, P::Sphere, {2 * radius, 2 * radius, 2 * radius}, {0, 0, 0}, {0, 0, 0}, m,
               "Joint sphere (same radius as the limb cylinder: a capsule)");
        b.part(parent, cylName, P::Cylinder, {2 * radius, 2 * radius, len}, {90, 0, 0}, {0, -0.5f * len, 0}, m, purpose);
    };

    // ---- pelvis and legs
    r.pelvis = b.group(r.root, "Pelvis", {0, g.hipHeight, 0}, {0, 0, 0}, "Hip height, gait bob and roll");
    r.pelvis->animation = "Height = hip height + gait bob; roll with the weight shift";
    b.part(r.pelvis, "Hips", P::Sphere, {0.36f * wb, 0.22f, 0.24f}, {0, 0, 0}, {0, 0.04f, 0}, trousers, "Ellipsoid hips");
    b.part(r.pelvis, "Belt", P::Cylinder, {0.37f * wb, 0.25f, 0.07f}, {90, 0, 0}, {0, 0.12f, 0}, webbing, "Belt with pouches");
    for (int i = 0; i < 2; ++i) {
        const float sx = i == 0 ? 1.0f : -1.0f;   // facing +Z the figure's left side is +X
        r.hip[i] = b.group(r.pelvis, i == 0 ? "Hip L" : "Hip R", {sx * S::kHipHalfWidth, 0, 0}, {0, 0, 0}, "Hip joint");
        r.hip[i]->animation = "Pitch from the two-link leg IK (stance foot world-locked)";
        capsule(r.hip[i], "Thigh", "Hip joint", 0.075f, L1, trousers, "Thigh capsule");
        r.knee[i] = b.group(r.hip[i], i == 0 ? "Knee L" : "Knee R", {0, -L1, 0}, {0, 0, 0}, "Knee joint");
        r.knee[i]->animation = "Bend from the leg IK";
        b.part(r.knee[i], "Knee joint", P::Sphere, {0.15f, 0.15f, 0.15f}, {0, 0, 0}, {0, 0, 0}, trousers,
               "Knee sphere (thigh capsule end)");
        b.part(r.knee[i], "Shin", P::Cylinder, {0.12f, 0.12f, L2}, {90, 0, 0}, {0, -0.5f * L2, 0}, trousers, "Shin capsule");
        r.ankle[i] = b.group(r.knee[i], i == 0 ? "Ankle L" : "Ankle R", {0, -L2, 0}, {0, 0, 0}, "Ankle joint");
        r.ankle[i]->animation = "Keeps the sole level; toe pivot at push-off";
        b.part(r.ankle[i], "Ankle joint", P::Sphere, {0.12f, 0.12f, 0.12f}, {0, 0, 0}, {0, 0, 0}, boots, "Ankle sphere (shin capsule end)");
        b.part(r.ankle[i], "Boot", P::Sphere, {0.13f, 0.11f, 0.30f}, {0, 0, 0}, {0, -0.025f, 0.06f}, boots, "Rounded combat boot");
    }

    // ---- spine, chest, gear
    r.spine = b.group(r.pelvis, "Spine", {0, S::kPelvisToSpine, 0}, {0, 0, 0}, "Lumbar joint");
    r.spine->animation = "Forward lean (gait, work, duck)";
    b.part(r.spine, "Abdomen", P::Sphere, {0.34f * wb, 0.30f, 0.24f}, {0, 0, 0}, {0, 0.12f, 0}, torso, "Ellipsoid abdomen");
    r.chest = b.group(r.spine, "Chest", {0, S::kSpineToChest, 0}, {0, 0, 0}, "Thoracic joint");
    r.chest->animation = "Twist against the pelvis (counter-rotation), head-scan follow";
    b.part(r.chest, "Torso", P::Sphere, {0.42f * wb, 0.35f, 0.26f}, {0, 0, 0}, {0, 0.10f, 0}, torso, "Ellipsoid chest with plate carrier");
    r.backpack = b.part(r.chest, "Backpack", P::Cube, {0.32f, 0.38f, 0.16f}, {0, 0, 0}, {0, 0.07f, -0.20f}, gear, "Field pack");
    SceneNode* bedroll = b.part(r.chest, "Bedroll", P::Cylinder, {0.11f, 0.11f, 0.34f}, {0, 90, 0}, {0, 0.31f, -0.21f}, gear, "Rolled mat on the pack");
    r.backpack->visible = look.backpack;
    bedroll->visible = look.backpack;

    // Held rifle (low ready, barrel forward and down) and the slung one (diagonal across the back).
    r.rifle = b.group(r.chest, "Rifle (held)", {0.05f, 0.02f, 0.30f}, {18, -12, 0}, "Rifle at the low ready");
    r.rifleBack = b.group(r.chest, "Rifle (slung)", {0, 0.08f, look.backpack ? -0.32f : -0.17f}, {-59, 101, 0}, "Rifle slung on the back");
    for (SceneNode* rf : {r.rifle, r.rifleBack}) {
        b.part(rf, "Rifle receiver", P::Cube, {0.06f, 0.11f, 0.52f}, {0, 0, 0}, {0, 0, 0}, gun, "Receiver and stock");
        b.part(rf, "Rifle barrel", P::Cylinder, {0.035f, 0.035f, 0.40f}, {0, 0, 0}, {0, 0.02f, 0.45f}, gun, "Barrel");
        b.part(rf, "Rifle magazine", P::Cube, {0.04f, 0.16f, 0.07f}, {15, 0, 0}, {0, -0.11f, 0.10f}, gun, "Curved magazine");
    }
    // The renderer draws leaves: hide a whole group by hiding its parts (the group flag records the choice).
    auto show = [](SceneNode* g, bool on) {
        g->visible = on;
        for (auto& c : g->children) c->visible = on;
    };
    show(r.rifle, !look.rifleSlung);
    show(r.rifleBack, look.rifleSlung);
    r.crate = b.part(r.chest, "Carried crate", P::Cube, {0.46f, 0.32f, 0.36f}, {0, 0, 0}, {0, -0.10f, 0.40f}, mats::crateWood(),
                     "Ammunition crate carried in both arms");
    r.crate->visible = false;

    // ---- neck and head
    SceneNode* neck = b.group(r.chest, "Neck", {0, S::kChestToNeck, 0.01f}, {8, 0, 0}, "Neck joint (8 deg forward carriage)");
    b.part(neck, "Neck", P::Cylinder, {0.125f, 0.115f, 0.11f}, {90, 0, 0}, {0, 0.035f, 0}, skin, "Short, thick neck");
    r.head = b.group(neck, "Head", {0, S::kNeckToHead, 0}, {0, 0, 0}, "Head joint");
    r.head->animation = "Yaw scan (guard), pitch (work, duck)";
    b.part(r.head, "Head", P::Sphere, {0.18f, 0.225f, 0.205f}, {0, 0, 0}, {0, 0.08f, 0.0f}, skin, "Ellipsoid head (face: warm in the thermal image)");
    b.part(r.head, "Helmet", P::Sphere, {0.245f, 0.19f, 0.265f}, {-6, 0, 0}, {0, 0.145f, -0.01f}, helmet, "Rounded combat helmet");
    b.part(r.head, "Helmet brim", P::Cylinder, {0.255f, 0.28f, 0.022f}, {84, 0, 0}, {0, 0.138f, -0.005f}, helmetRim, "Helmet rim (low over the brow)");

    // ---- arms
    for (int i = 0; i < 2; ++i) {
        const float sx = i == 0 ? 1.0f : -1.0f;
        r.shoulder[i] = b.group(r.chest, i == 0 ? "Shoulder L" : "Shoulder R", {sx * S::kShoulderX * wb, S::kShoulderY, 0},
                                {0, 0, sx * 6.0f}, "Shoulder joint");
        r.shoulder[i]->animation = "Arm swing against the legs / work / rifle pose";
        capsule(r.shoulder[i], "Upper arm", "Shoulder joint", 0.065f, S::kUpperArm, sleeve, "Upper-arm capsule");
        r.elbow[i] = b.group(r.shoulder[i], i == 0 ? "Elbow L" : "Elbow R", {0, -S::kUpperArm, 0}, {0, 0, 0}, "Elbow joint");
        r.elbow[i]->animation = "Bend";
        b.part(r.elbow[i], "Elbow joint", P::Sphere, {0.13f, 0.13f, 0.13f}, {0, 0, 0}, {0, 0, 0}, sleeve, "Elbow sphere (upper-arm capsule end)");
        b.part(r.elbow[i], "Forearm", P::Cylinder, {0.11f, 0.11f, S::kForearm}, {90, 0, 0}, {0, -0.5f * S::kForearm, 0}, sleeve, "Forearm capsule");
        r.wrist[i] = b.group(r.elbow[i], i == 0 ? "Wrist L" : "Wrist R", {0, -S::kForearm, 0}, {0, 0, 0}, "Wrist");
        b.part(r.wrist[i], "Hand", P::Sphere, {0.09f, 0.12f, 0.08f}, {0, 0, 0}, {0, -0.05f, 0}, glove, "Gloved hand (warm fingertips)");
    }
    r.wrench = b.part(r.wrist[1], "Wrench", P::Cylinder, {0.03f, 0.03f, 0.26f}, {0, 0, 0}, {0, -0.06f, 0.10f}, wrenchMat, "Spanner");
    r.wrench->visible = false;

    // ---- v7 detail (appended: the ids above stay stable; T201 allows 42 leaves)
    // Plate carrier: a box vest over the ellipsoid chest gives the tactical silhouette (front / back plates, webbing).
    detail(b, r.chest, "Plate carrier", P::Cube, {0.33f * wb, 0.27f, 0.295f}, {0, 0, 0}, {0, 0.07f, 0.005f}, webbing,
           "Plate carrier with magazine pouches (slightly cooler than the shirt)")->material.heat = 0.62f;
    // Jaw / chin: a second ellipsoid at the front of the head shapes the face profile (nose-chin line).
    detail(b, r.head, "Jaw", P::Sphere, {0.13f, 0.085f, 0.115f}, {0, 0, 0}, {0, 0.018f, 0.037f}, skin,
           "Jaw and chin (face shape, warm skin)");

    // ---- v7 hero pass (appended; T201 cap 75): face, neck / shoulder masses, tapered limbs, pads, kit, rifle furniture
    const Material eye = makeMaterial({0.035f, 0.03f, 0.025f}, 0.80f, Surface::Glass, 0.2f, 0.0f);
    const Material pad = makeMaterial({0.10f, 0.10f, 0.085f}, 0.58f, Surface::Rubber, 0.8f, 0.0f);
    const Material strap = makeMaterial({0.09f, 0.085f, 0.06f}, 0.58f, Surface::Plain, 0.85f, 0.0f);
    const Material lens = makeMaterial({0.04f, 0.05f, 0.06f}, 0.50f, Surface::Glass, 0.05f, 0.1f);
    const Material radio = makeMaterial({0.10f, 0.11f, 0.08f}, 0.50f, Surface::Plain, 0.7f, 0.1f);
    // Face: nose, eyes, ears, chin strap; goggles and NVG mount on the helmet.
    detail(b, r.head, "Nose", P::Sphere, {0.032f, 0.05f, 0.045f}, {-15, 0, 0}, {0, 0.062f, 0.098f}, skin, "Nose (warm skin)");
    for (int i = 0; i < 2; ++i) {
        const float sx = i == 0 ? 1.0f : -1.0f;
        detail(b, r.head, "Eye", P::Sphere, {0.024f, 0.016f, 0.012f}, {0, 0, 0}, {sx * 0.033f, 0.093f, 0.094f}, eye, "Eye (dark)");
        detail(b, r.head, "Ear", P::Sphere, {0.022f, 0.058f, 0.042f}, {0, 0, 0}, {sx * 0.089f, 0.075f, -0.005f}, skin, "Ear (warm skin)");
        strut(b, r.head, "Chin strap", P::Cylinder, {sx * 0.088f, 0.125f, -0.01f}, {sx * 0.035f, -0.02f, 0.06f}, 0.012f, strap,
              "Helmet chin strap")->obstacle = false;
    }
    detail(b, r.head, "Goggles", P::Cube, {0.15f, 0.032f, 0.03f}, {-35, 0, 0}, {0, 0.215f, 0.092f}, lens, "Ballistic goggles on the helmet");
    detail(b, r.head, "NVG mount", P::Cube, {0.04f, 0.035f, 0.025f}, {-10, 0, 0}, {0, 0.163f, 0.128f}, gun, "Night-vision shroud");
    // Neck / shoulder masses: trapezius (in the neck group: hidden with the head in first person) and deltoids.
    for (int i = 0; i < 2; ++i) {
        const float sx = i == 0 ? 1.0f : -1.0f;
        detail(b, neck, "Trapezius", P::Sphere, {0.20f, 0.085f, 0.15f}, {0, 0, sx * -20.0f}, {sx * 0.085f, -0.02f, -0.02f}, torso,
               "Trapezius slope from the neck to the shoulder");
        detail(b, r.shoulder[i], "Deltoid", P::Sphere, {0.155f, 0.17f, 0.15f}, {0, 0, 0}, {sx * 0.01f, -0.035f, 0}, sleeve,
               "Shoulder cap (deltoid)");
    }
    // Tapered limbs: muscle ellipsoids over the capsule tops (thigh, calf, forearm) and knee / elbow pads.
    for (int i = 0; i < 2; ++i) {
        detail(b, r.hip[i], "Thigh muscle", P::Sphere, {0.175f, 0.31f, 0.18f}, {0, 0, 0}, {0, -0.15f, 0.01f}, trousers,
               "Upper thigh mass (tapers to the knee)");
        detail(b, r.knee[i], "Calf", P::Sphere, {0.13f, 0.22f, 0.14f}, {0, 0, 0}, {0, -0.13f, -0.015f}, trousers, "Calf muscle");
        detail(b, r.knee[i], "Knee pad", P::Sphere, {0.115f, 0.13f, 0.06f}, {0, 0, 0}, {0, -0.02f, 0.06f}, pad, "Hard-shell knee pad");
        detail(b, r.elbow[i], "Forearm muscle", P::Sphere, {0.12f, 0.18f, 0.12f}, {0, 0, 0}, {0, -0.07f, 0.0f}, sleeve,
               "Forearm mass (tapers to the wrist)");
        detail(b, r.elbow[i], "Elbow pad", P::Sphere, {0.10f, 0.11f, 0.055f}, {0, 0, 0}, {0, -0.01f, -0.055f}, pad, "Elbow pad");
    }
    // Plate-carrier pouches and the radio with its whip antenna (left side, clear of the backpack).
    detail(b, r.chest, "Magazine pouches", P::Cube, {0.25f, 0.11f, 0.06f}, {0, 0, 0}, {0, 0.0f, 0.175f}, webbing,
           "Triple rifle-magazine pouch")->material.heat = 0.6f;
    detail(b, r.chest, "Admin pouch", P::Cube, {0.13f, 0.07f, 0.035f}, {0, 0, 0}, {0, 0.115f, 0.166f}, shade(webbing, 0.85f),
           "Admin pouch")->material.heat = 0.6f;
    detail(b, r.chest, "Radio", P::Cube, {0.05f, 0.15f, 0.08f}, {0, 0, 0}, {0.19f, 0.04f, -0.05f}, radio, "Personal role radio");
    strut(b, r.chest, "Radio antenna", P::Cylinder, {0.19f, 0.115f, -0.07f}, {0.215f, 0.48f, -0.12f}, 0.008f, gun,
          "Whip antenna of the radio")->obstacle = false;
    // Rifle furniture on both rifles (held / slung): pistol grip, handguard, optic.
    for (SceneNode* rf : {r.rifle, r.rifleBack}) {
        for (SceneNode* n : {detail(b, rf, "Pistol grip", P::Cube, {0.035f, 0.10f, 0.045f}, {-20, 0, 0}, {0, -0.09f, -0.13f}, gun, "Pistol grip"),
                             detail(b, rf, "Handguard", P::Cube, {0.058f, 0.068f, 0.24f}, {0, 0, 0}, {0, 0.0f, 0.34f}, gun, "Handguard"),
                             detail(b, rf, "Optic", P::Cylinder, {0.042f, 0.042f, 0.13f}, {0, 0, 0}, {0, 0.085f, 0.02f}, gun, "Red-dot optic")})
            n->visible = rf->visible;
    }
    return r;
}

}  // namespace ts
