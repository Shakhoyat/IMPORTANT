// v6 soldier figure (WP-B): an articulated human built only from primitives - ellipsoid torso and head (scaled
// spheres), capsule limbs (a cylinder with joint spheres of the same radius at both ends), a rounded helmet,
// rifle and backpack. The joint groups are exposed so Gait poses can drive them (forward kinematics).
//
//   root (ground point under the pelvis, heading)
//     Pelvis (height + bob, roll)        hips, belt
//       Hip L/R (pitch)                  hip joint, thigh
//         Knee (pitch)                   knee joint, shin
//           Ankle (pitch)                ankle joint, boot
//       Spine (lean)                     abdomen
//         Chest (twist)                  torso, backpack, bedroll, held rifle, slung rifle, carried crate
//           Neck -> Head (yaw, pitch)    neck, head (face), helmet, brim
//           Shoulder L/R (pitch) -> Elbow (pitch) -> Wrist -> hand (+ wrench in the right hand)
#pragma once

#include <memory>

#include "scene/SceneNode.h"

namespace ts {

struct SoldierLook {
    Vec3 uniform{0.29f, 0.31f, 0.22f};   // camouflage base colour
    Vec3 helmet{0.26f, 0.28f, 0.20f};
    Vec3 skin{0.55f, 0.40f, 0.30f};
    bool backpack = true;
    bool rifleSlung = false;             // workers carry the rifle on the back
    float build = 1.0f;                  // torso width factor (0.92 .. 1.08)
};

struct SoldierRig {
    std::unique_ptr<SceneNode> owner;          // the figure's root (moved into the scene graph by the owner)
    SceneNode* root = nullptr;                 // = owner.get(): ground position + heading
    SceneNode* pelvis = nullptr;               // bob / sway
    SceneNode* spine = nullptr;                // lean
    SceneNode* chest = nullptr;                // twist
    SceneNode* head = nullptr;                 // scan
    SceneNode* shoulder[2] = {nullptr, nullptr};
    SceneNode* elbow[2] = {nullptr, nullptr};
    SceneNode* wrist[2] = {nullptr, nullptr};
    SceneNode* hip[2] = {nullptr, nullptr};
    SceneNode* knee[2] = {nullptr, nullptr};
    SceneNode* ankle[2] = {nullptr, nullptr};
    SceneNode* rifle = nullptr;                // held at the low ready (guards)
    SceneNode* rifleBack = nullptr;            // slung across the back (workers)
    SceneNode* backpack = nullptr;             // backpack leaf (hidden when the look has none)
    SceneNode* wrench = nullptr;               // tool in the right hand (hidden unless working on a vehicle)
    SceneNode* crate = nullptr;                // carried crate (hidden unless carrying)
};

// Figure dimensions shared with the gait (scene/Gait.h GaitParams defaults) and the garrison.
namespace soldier {
constexpr float kHipHalfWidth = 0.10f;       // hip joints at +-x
// v7 (WP-M) proportions: head ~1/7.5 of the 1.80 m figure, short neck, shoulders at 1.51 m (were 0.34 / 0.10 / 0.27:
// a long neck and the head 0.18 m too high).
constexpr float kPelvisToSpine = 0.12f, kSpineToChest = 0.22f, kChestToNeck = 0.23f, kNeckToHead = 0.06f;
constexpr float kShoulderX = 0.215f, kShoulderY = 0.19f;   // in the chest frame
constexpr float kUpperArm = 0.30f, kForearm = 0.26f;   // v7: 0.28 / 0.25 (too short to reach the handguard)
constexpr float kBodyRadius = 0.25f;         // collision cylinder
constexpr float kEyeHeight = 1.62f;          // standing
constexpr float kHeatSkin = 0.82f, kHeatTorso = 0.70f;     // face / hands, torso (never a 0.90 target core)
constexpr float kHandOffset = 0.05f;         // hand sphere centre below the wrist joint (along the forearm)
// Rifle frame (the "Rifle (held)" group): +Z = muzzle, butt plate at z = kRifleButtZ; hand points for the arm IK
// (right hand on the pistol grip, left hand under the handguard; same points as the first-person viewmodel).
constexpr float kRifleButtZ = -0.26f;
const Vec3 kRifleGrip{0.0f, -0.085f, -0.12f};
const Vec3 kRifleHandguard{0.0f, -0.06f, 0.16f};   // third person: in front of the magazine well
}  // namespace soldier

// index 0..7: names "Soldier 1".. and node ids "SLD1-.." (unique per soldier).
SoldierRig buildSoldier(int index, const SoldierLook& look);

}  // namespace ts
