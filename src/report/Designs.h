// The report's object designs: every distinct thing the scene builds with one builder function (or one prefab), with
// its instances in the scene graph. The aggregate scene objects (Base, Environment, Camp camouflage, Backdrop) are split
// into the designs they place (guard tower, HESCO perimeter, pine, bush, camouflage net, ...); a design built inside
// another (the camouflage net over the bunker ramp, the rounds on the UAV rails) is a nested design of its own.
// Every leaf of the scene belongs to exactly one design instance (the innermost), so the designs add up to the scene.
// Used by the export ("designs" list, per-component design fields) and the shape-view capture. Pure CPU.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "scene/SceneNode.h"

namespace ts::designs {

// Families in report order (object atlas, Table 19.1).
inline constexpr const char* kFamilies[] = {"people", "vehicles and aircraft", "weapons and munitions", "buildings",
                                            "fortifications", "vegetation", "props", "terrain and backdrop"};

struct Instance {
    int design = -1;                       // index into Catalogue::designs
    const SceneNode* root = nullptr;       // group (or leaf, for one-part designs) the builder created
    int parent = -1;                       // instance this one is nested in (-1: top level)
    std::vector<const SceneNode*> own;     // leaves owned by this instance (not by a nested one), depth-first order
    std::vector<int> nested;               // instances built inside this one
};

struct Design {
    std::string name;      // report name ("Guard tower", "UAV round", ...)
    std::string family;    // one of kFamilies
    std::string source;    // builder (file: function)
    std::vector<int> instances;   // in depth-first scene order
};

struct Catalogue {
    std::vector<Design> designs;      // in rule (report) order; designs without instances are left out
    std::vector<Instance> instances;
    std::map<const SceneNode*, int> instanceOf;    // leaf -> owning instance
    std::vector<const SceneNode*> unassigned;      // leaves no design claimed (must stay empty)

    const Design* find(const std::string& name) const;
    int partsOf(int instance) const { return static_cast<int>(instances[static_cast<size_t>(instance)].own.size()); }
    // Instance root that owns a node (walks up to the nearest instance root); -1 when none.
    int instanceAt(const SceneNode& n) const;
};

// Walks the scene roots and assigns every leaf to a design instance.
Catalogue collect(const std::vector<SceneNode*>& roots);

}  // namespace ts::designs
