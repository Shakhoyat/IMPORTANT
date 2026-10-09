// v9 M4: a hinged door as a scene object. Built from the unit primitives like everything else: the leaf (a slab with
// stiles and rails proud of both faces, a steel kick plate, a lever handle with its rose and a key cylinder on each
// face) hangs on three butt hinges (knuckle on the axis + hinge leaf). The whole leaf is one group node that turns
// about the hinge axis; the angle comes from scene/Door.h (critically damped hinge spring, blocking bodies, the
// auto-open trigger for AI walkers). As a Controllable it toggles on the action input (the E interaction of the App),
// reports its leaf as dynamic collision boxes (walkers, the possessed soldier, the v4 walker, the spring-arm camera)
// and is found by the garrison through DoorPortal.
#pragma once

#include <memory>
#include <string>

#include "objects/Builder.h"
#include "rendering/Material.h"
#include "scene/Controllable.h"
#include "scene/Door.h"

namespace ts {

struct HingedDoorLook {
    Material leaf;      // painted leaf and its stiles / rails
    Material trim;      // stiles and rails (a shade darker or lighter than the leaf)
    Material metal;     // handle, rose, hinges
    Material plate;     // kick plate
};

class HingedDoor : public Controllable, public DoorPortal {
public:
    // Builds the leaf under 'parent' (world-aligned parent: the hinge position is in the parent's frame = world).
    HingedDoor(Builder& b, SceneNode* parent, std::string name, const HingeParams& p, const HingedDoorLook& look,
               const Vec3& viewEye, const Vec3& viewTarget);

    const char* name() const override { return name_.c_str(); }
    const char* controls() const override { return "ENTER or \\  open / close the door (E on foot)"; }
    SceneNode* root() override { return hinge_; }
    void update(const WorldContext& w, const ControlInput* in) override;
    CameraHint camera(bool firstPerson) const override;
    std::string status() const override;
    Vec3 position() const override { return kin_.doorwayCentre(); }
    std::vector<Box> dynamicObstacles() const override { return kin_.panelBoxes(); }
    float friendlyRadius() const override { return 0.0f; }   // not a mark for the UAVs' identification

    DoorKinematics& door() override { return kin_; }
    const DoorKinematics& door() const override { return kin_; }
    SceneNode* hingeNode() const { return hinge_; }

private:
    void apply();
    std::string name_;
    DoorKinematics kin_;
    SceneNode* hinge_ = nullptr;
    Vec3 eye_, target_;
};

}  // namespace ts
