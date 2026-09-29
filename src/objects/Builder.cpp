#include "objects/Builder.h"

#include <cstdio>

namespace ts {

std::string Builder::nextId() {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s-%02d", prefix_.c_str(), counter_++);
    return buf;
}

std::unique_ptr<SceneNode> Builder::root(const std::string& name, const Vec3& t, const std::string& purpose) {
    auto node = std::make_unique<SceneNode>();
    node->id = nextId();
    node->name = name;
    node->object = object_;
    node->purpose = purpose;
    node->shading = shading_;
    node->local.translation = t;
    node->explodable = false;
    return node;
}

SceneNode* Builder::group(SceneNode* parent, const std::string& name, const Vec3& t, const Vec3& rDeg,
                          const std::string& purpose) {
    auto node = std::make_unique<SceneNode>();
    node->id = nextId();
    node->name = name;
    node->object = object_;
    node->purpose = purpose;
    node->shading = shading_;
    node->local.translation = t;
    node->local.rotationDeg = rDeg;
    return parent->addChild(std::move(node));
}

SceneNode* Builder::part(SceneNode* parent, const std::string& name, PrimitiveType prim, const Vec3& s,
                         const Vec3& rDeg, const Vec3& t, const Material& mat, const std::string& purpose) {
    auto node = std::make_unique<SceneNode>();
    node->id = nextId();
    node->name = name;
    node->object = object_;
    node->purpose = purpose;
    node->primitive = prim;
    node->shading = shading_;
    node->material = mat;
    node->local.translation = t;
    node->local.rotationDeg = rDeg;
    node->local.scale = s;
    return parent->addChild(std::move(node));
}

}  // namespace ts
