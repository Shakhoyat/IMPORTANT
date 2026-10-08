// A "lab stage": a handful of unit primitives (spheres, cubes) with hand-set materials that REPLACE the scene's leaf
// list while a lab figure is rendered, so the real renderer (shadows, SSAO, PBR, SSR, bloom, tone mapping) draws a
// controlled test object instead of the camp. The nodes are tagged object = "LabStage"; render them with
// RenderSettings::isolate = "LabStage" (which also switches grass, foliage, rain and the combat effects off).
// install() / uninstall() swap the scene's leaf and practical-light lists and save / restore its light rig.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "scene/Scene.h"

namespace ts {

class LabStage {
public:
    static constexpr const char* kObject = "LabStage";

    SceneNode* add(PrimitiveType prim, const Material& m, const Vec3& pos, const Vec3& size, const Vec3& rotDeg = {0, 0, 0},
                   ShadingModel shading = ShadingModel::Phong, const std::string& name = "lab") {
        auto n = std::make_unique<SceneNode>();
        n->id = "LAB-" + std::to_string(nodes_.size());
        n->name = name;
        n->object = kObject;
        n->purpose = "lab figure test object";
        n->primitive = prim;
        n->shading = shading;
        n->material = m;
        n->world = translate(pos) * rotateEulerYXZ(rotDeg) * scale(size);
        n->obstacle = false;
        SceneNode* raw = n.get();
        nodes_.push_back(std::move(n));
        leaves_.push_back(raw);
        return raw;
    }
    void clear() {
        leaves_.clear();
        nodes_.clear();
    }

    void install(Scene& s) {
        savedLights_ = s.lights;
        auto& lv = const_cast<std::vector<SceneNode*>&>(s.leaves());
        auto& pr = const_cast<std::vector<SceneNode*>&>(s.practicalLights());
        savedLeaves_ = lv;
        savedPracticals_ = pr;
        lv = leaves_;
        pr.clear();
        installed_ = true;
    }
    void uninstall(Scene& s) {
        if (!installed_) return;
        const_cast<std::vector<SceneNode*>&>(s.leaves()) = savedLeaves_;
        const_cast<std::vector<SceneNode*>&>(s.practicalLights()) = savedPracticals_;
        s.lights = savedLights_;
        installed_ = false;
    }

private:
    std::vector<std::unique_ptr<SceneNode>> nodes_;
    std::vector<SceneNode*> leaves_, savedLeaves_, savedPracticals_;
    LightRig savedLights_;
    bool installed_ = false;
};

}  // namespace ts
