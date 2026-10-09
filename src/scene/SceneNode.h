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
enum class PrimitiveType { None, Cube, Sphere, Cylinder, BeamCone, TerrainDisc, Cone };

inline const char* primitiveName(PrimitiveType p) {
    switch (p) {
        case PrimitiveType::None: return "Group";
        case PrimitiveType::Cube: return "Cube";
        case PrimitiveType::Sphere: return "Sphere";
        case PrimitiveType::Cylinder: return "Cylinder";
        case PrimitiveType::BeamCone: return "Cylinder (frustum)";
        case PrimitiveType::TerrainDisc: return "Terrain disc";
        case PrimitiveType::Cone: return "Cone";
    }
    return "?";
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
