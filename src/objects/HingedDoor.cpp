#include "objects/HingedDoor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "objects/PropKit.h"

namespace ts {

namespace {
using P = PrimitiveType;
SceneNode* moving(SceneNode* n) {
    n->obstacle = false;   // the leaf collides through dynamicObstacles(), not as a static solid / planner part
    n->occluder = false;
    return n;
}
}  // namespace

HingedDoor::HingedDoor(Builder& b, SceneNode* parent, std::string name, const HingeParams& p, const HingedDoorLook& look,
                       const Vec3& viewEye, const Vec3& viewTarget)
    : name_(std::move(name)), kin_(p), eye_(viewEye), target_(viewTarget) {
    const HingeParams& q = kin_.params();
    hinge_ = b.group(parent, name_ + " hinge", q.hinge, {0.0f, q.closedYawDeg, 0.0f},
                     "Hinge axis: the leaf turns about the vertical through the hinge pins (critically damped hinge spring)");
    hinge_->animation = "Turns about its vertical axis: the door angle (0 closed .. open stop)";
    // Leaf frame: x = across the leaf (n), y = up from the floor, z = along the leaf from the hinge (u).
    const float t = q.n1 - q.n0, nm = 0.5f * (q.n0 + q.n1), W = q.width, H = q.height, y0 = q.bottom;

    const float uc = q.u0 + 0.5f * W;
    auto part = [&](const char* n, P prim, const Vec3& s, const Vec3& r, const Vec3& c, const Material& m, const char* why) {
        return moving(b.part(hinge_, n, prim, s, r, c, m, why));
    };
    part("Door leaf", P::Cube, {t, H, W}, {0, 0, 0}, {nm, y0 + 0.5f * H, uc}, look.leaf,
         "Door leaf: 45 mm painted panel on a timber core");
    // Stiles and rails proud of both faces (a framed-and-panelled leaf), a steel kick plate at the bottom.
    for (int face = -1; face <= 1; face += 2) {
        const float x = face > 0 ? q.n1 + 0.006f : q.n0 - 0.006f;
        part("Hinge stile", P::Cube, {0.012f, H, 0.11f}, {0, 0, 0}, {x, y0 + 0.5f * H, q.u0 + 0.055f}, look.trim, "Hinge-side stile");
        part("Lock stile", P::Cube, {0.012f, H, 0.11f}, {0, 0, 0}, {x, y0 + 0.5f * H, q.u0 + W - 0.055f}, look.trim, "Lock-side stile");
        part("Top rail", P::Cube, {0.012f, 0.12f, W - 0.22f}, {0, 0, 0}, {x, y0 + H - 0.06f, uc}, look.trim, "Top rail");
        part("Lock rail", P::Cube, {0.012f, 0.14f, W - 0.22f}, {0, 0, 0}, {x, y0 + 1.0f, uc}, look.trim, "Lock rail at handle height");
        part("Bottom rail", P::Cube, {0.012f, 0.22f, W - 0.22f}, {0, 0, 0}, {x, y0 + 0.11f, uc}, look.trim, "Bottom rail");
        part("Kick plate", P::Cube, {0.004f, 0.20f, W - 0.06f}, {0, 0, 0}, {x + face * 0.008f, y0 + 0.11f, uc}, look.plate,
             "Steel kick plate");
        // Lever handle: rose, spindle, lever towards the hinge side; key cylinder below.
        const float hu = q.u0 + W - 0.075f, hx = face > 0 ? q.n1 : q.n0;
        part("Handle rose", P::Cylinder, {0.065f, 0.065f, 0.012f}, {0, 90, 0}, {hx + face * 0.018f, y0 + 1.0f, hu}, look.metal, "Handle rose");
        part("Handle spindle", P::Cylinder, {0.02f, 0.02f, 0.05f}, {0, 90, 0}, {hx + face * 0.045f, y0 + 1.0f, hu}, look.metal,
             "Handle neck");
        part("Handle lever", P::Cube, {0.022f, 0.024f, 0.13f}, {0, 0, 0}, {hx + face * 0.068f, y0 + 1.0f, hu - 0.055f}, look.metal,
             "Lever handle (points to the hinge side)");
        part("Key cylinder", P::Cylinder, {0.03f, 0.03f, 0.016f}, {0, 90, 0}, {hx + face * 0.02f, y0 + 0.87f, hu}, look.metal,
             "Lock cylinder");
    }
    // Three butt hinges: knuckle on the axis, a plate from the knuckle onto the leaf face nearest to the axis.
    const float face = q.n1 < 0.0f ? q.n1 : (q.n0 > 0.0f ? q.n0 : q.n1);
    const float plateT = std::max(std::fabs(face), 0.004f);
    for (float hy : {y0 + 0.22f, y0 + 0.5f * H + 0.1f, y0 + H - 0.22f}) {
        part("Hinge knuckle", P::Cylinder, {0.026f, 0.026f, 0.11f}, {90, 0, 0}, {0.0f, hy, 0.0f}, look.metal, "Hinge knuckle (pin axis)");
        part("Hinge leaf", P::Cube, {plateT, 0.10f, q.u0 + 0.05f}, {0, 0, 0}, {0.5f * face, hy, 0.5f * (q.u0 + 0.05f)}, look.metal,
             "Hinge leaf screwed to the door face");
    }
    apply();
}

void HingedDoor::apply() { hinge_->local.rotationDeg.y = kin_.yawDeg(); }

void HingedDoor::update(const WorldContext& w, const ControlInput* in) {
    if (in && (in->actionPressed || in->firePressed)) kin_.toggle();
    kin_.step(w.time, std::max(w.dt, 0.0f));
    apply();
}

CameraHint HingedDoor::camera(bool) const {
    CameraHint h;
    h.eye = eye_;
    h.target = target_;
    h.fovDeg = 60.0f;
    return h;
}

std::string HingedDoor::status() const {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s %s %d DEG%s", name_.c_str(), kin_.stateName(), static_cast<int>(std::lround(kin_.angleDeg())),
                  kin_.blocked() ? " BLOCKED" : "");
    return buf;
}

}  // namespace ts
