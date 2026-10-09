// Scene graph node. Leaves draw one primitive; group nodes only carry a transform for their children.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rendering/Material.h"
#include "scene/Transform.h"

namespace ts {

// Cone = the lab cylinder generator with top radius 0: createCylinder(36, 0.5f, 0.0f, true), apex at z = +0.5.
// v10 M3: the 16-primitive library (primitives/Library.h). The new types are appended (the old values keep their
// numbers). Every one fits the unit cube [-0.5, 0.5]^3, so the bounding box / radius / occlusion paths that assume a
// unit box stay valid. GrassBlade is never a scene leaf (the grass field instances it on the GPU); it has a type so
// the library renders (--capture --only prims) and the export can name it.
enum class PrimitiveType {
    None, Cube, Sphere, Cylinder, BeamCone, TerrainDisc, Cone,
    Hemisphere, Torus, Wedge, TaperedBox, Pyramid, Capsule, Annulus, Helix, GrassBlade
};

inline const char* primitiveName(PrimitiveType p) {
    switch (p) {
        case PrimitiveType::None: return "Group";
        case PrimitiveType::Cube: return "Cube";
        case PrimitiveType::Sphere: return "Sphere";
        case PrimitiveType::Cylinder: return "Cylinder";
        case PrimitiveType::BeamCone: return "Cylinder (frustum)";
        case PrimitiveType::TerrainDisc: return "Terrain disc";
        case PrimitiveType::Cone: return "Cone";
        case PrimitiveType::Hemisphere: return "Hemisphere";
        case PrimitiveType::Torus: return "Torus";
        case PrimitiveType::Wedge: return "Wedge";
        case PrimitiveType::TaperedBox: return "Tapered box";
        case PrimitiveType::Pyramid: return "Pyramid";
        case PrimitiveType::Capsule: return "Capsule";
        case PrimitiveType::Annulus: return "Annulus";
        case PrimitiveType::Helix: return "Helix";
        case PrimitiveType::GrassBlade: return "Grass blade";
    }
    return "?";
}

// A unit-mesh primitive of the library that scene parts may use (everything but groups, the terrain disc and the
// GPU-instanced grass blade). The "lab primitives only" checks of the object tests accept exactly these.
inline bool isLibraryPartPrimitive(PrimitiveType p) {
    return p != PrimitiveType::None && p != PrimitiveType::TerrainDisc && p != PrimitiveType::GrassBlade;
}

struct SceneNode {
    std::string id;         // e.g. "UAV-07"
    std::string name;       // e.g. "Motor FL"
    std::string object;     // final composite object this node belongs to
    std::string purpose;
    std::string animation;  // empty if static
    PrimitiveType primitive = PrimitiveType::None;
    ShadingModel shading = ShadingModel::Phong;
    Material material;
    Transform local;
    bool explodable = true;  // pushed apart in the diagnostic "explode" view
    bool occluder = false;   // blocks line of sight / ground cells for the hide-and-seek logic
    bool obstacle = true;    // UAV collision avoidance keeps clear of it (false for thin wires, far ground)
    bool visible = true;     // false: not drawn, casts no shadow (fired missile, removed wreck)
    bool dynamic = false;    // moves (vehicles, doors, debris): never baked into the static shadow cache
    // v10 M3: the modelled scale of a part that its animation shrinks to (almost) zero while it is not in use (muzzle
    // flashes, ejected casings): S_rest in the export and the size of the part's tile in --only parts:<object>.
    // {0, 0, 0} = not set (the rest scale is local.scale).
    Vec3 restScale{0, 0, 0};

    SceneNode* parent = nullptr;
    std::vector<std::unique_ptr<SceneNode>> children;
    Mat4 world;  // recomputed every frame: world = parent.world * local.matrix()
    // v9 M1: world at the latest / previous simulation tick (Scene::captureTick), for render interpolation.
    Mat4 tickWorld, prevTickWorld;
    uint32_t tickStamp = 0, prevTickStamp = 0;

    bool isLeaf() const { return primitive != PrimitiveType::None; }

    SceneNode* addChild(std::unique_ptr<SceneNode> child) {
        child->parent = this;
        children.push_back(std::move(child));
        return children.back().get();
    }
};

}  // namespace ts
