// v10 M3: part bookkeeping shared by the export (S_rest, hidden_at_export, hierarchy order, build stages, object
// instances) and the capture modes (--only parts:<object> / build:<object>), so both use the same definitions.
#pragma once

#include <string>
#include <vector>

#include "scene/SceneNode.h"

namespace ts {

// Root of the rig a node belongs to: its topmost ancestor with the same object tag (the two UAVs and the six rovers
// share an object name but are separate rigs).
const SceneNode* rigRoot(const SceneNode& n);

// Leaves of one rig in depth-first (pre-order, children in build order) order: the "hierarchy order" of its parts.
std::vector<const SceneNode*> rigLeavesDfs(const SceneNode& root);

// Build stages: the first ceil(pct * N / 100) parts in hierarchy order (at least 1) for pct = 12, 35, 65, 100.
constexpr int kBuildStagePct[4] = {12, 35, 65, 100};
int buildStageCount(int parts, int pct);

// Modelled scale of a part (restScale if its animation shrinks it to zero while idle, else local.scale).
Vec3 restScaleOf(const SceneNode& n);

// Not drawn in the t = 0 export state: invisible, scaled to (almost) zero, or parked below the ground (y < -6 m).
bool hiddenAtRest(const SceneNode& n);

// Part kind of a leaf name: trailing side / index tokens dropped ("Track link L07" -> "Track link", "Exhaust pipe R" ->
// "Exhaust pipe", "Bedroll A" -> "Bedroll"); a name that is only such a token stays as it is.
std::string partKindName(const std::string& name);

struct PartKind {
    std::string name;          // partKindName of the leaves
    PrimitiveType primitive = PrimitiveType::None;
    const SceneNode* first = nullptr;   // first leaf of the kind in hierarchy order (its R and S are drawn)
    int count = 0;             // leaves of this kind in the rig
};
// Distinct (kind name, primitive) pairs of one rig, in the hierarchy order of their first leaf.
std::vector<PartKind> partKinds(const SceneNode& root);

// Family of numbered object variants: "Soldier 3" -> "Soldier", "Main Battle Tank 2" -> "Main Battle Tank"; names
// without a trailing number are their own family.
std::string objectFamily(const std::string& object);

// File-name slug: lower case, runs of other characters -> '_' ("Main Battle Tank" -> "main_battle_tank").
std::string slug(const std::string& s);

}  // namespace ts
