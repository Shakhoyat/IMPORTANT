// Small helper so every composite object is written as a readable table of
// "primitive, scale, rotation, translation" rows, exactly like the Lab-04 M1/M2/M3 table.
#pragma once

#include <memory>
#include <string>

#include "scene/SceneNode.h"

namespace ts {

class Builder {
public:
    Builder(std::string objectName, std::string idPrefix, ShadingModel shading)
        : object_(std::move(objectName)), prefix_(std::move(idPrefix)), shading_(shading) {}

    std::unique_ptr<SceneNode> root(const std::string& name, const Vec3& t, const std::string& purpose);

    // Group node (no geometry). Its transform is inherited by its children.
    SceneNode* group(SceneNode* parent, const std::string& name, const Vec3& t, const Vec3& rDeg,
                     const std::string& purpose);

    // Leaf node: one primitive with its own S, R, T.
    SceneNode* part(SceneNode* parent, const std::string& name, PrimitiveType prim, const Vec3& s,
                    const Vec3& rDeg, const Vec3& t, const Material& mat, const std::string& purpose);

    void setShading(ShadingModel s) { shading_ = s; }

private:
    std::string nextId();

    std::string object_;
    std::string prefix_;
    ShadingModel shading_;
    int counter_ = 0;
};

}  // namespace ts
