#include "scene/Door.h"

#include <algorithm>
#include <cmath>

#include "scene/CharacterMotor.h"

namespace ts {

namespace {

Vec3 axisU(float phiDeg) {
    const float r = radians(phiDeg);
    return {std::sin(r), 0.0f, std::cos(r)};
}
Vec3 axisN(float phiDeg) {
    const float r = radians(phiDeg);
    return {std::cos(r), 0.0f, -std::sin(r)};
}
float flatDot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.z * b.z; }

}  // namespace

DoorKinematics::DoorKinematics(const HingeParams& p) : p_(p) {
    p_.openDeg = std::max(p_.openDeg, 1.0f);
    p_.segments = std::max(p_.segments, 1);
    if (p_.n1 < p_.n0) std::swap(p_.n0, p_.n1);
}

const char* DoorKinematics::stateName() const {
    switch (state_) {
        case State::Closed: return "CLOSED";
        case State::Opening: return "OPENING";
        case State::Open: return "OPEN";
        case State::Closing: return "CLOSING";
    }
    return "?";
}

void DoorKinematics::snap(float angleDeg) {
    theta_ = clampf(angleDeg, 0.0f, p_.openDeg);
    vel_ = 0.0f;
    targetOpen_ = theta_ > 0.5f * p_.openDeg;
    autoOpened_ = autoMotion_ = false;
    blockedT_ = 0.0f;
    reversedFor_ = -1;
    state_ = theta_ <= 0.0f ? State::Closed : theta_ >= p_.openDeg ? State::Open : targetOpen_ ? State::Opening : State::Closing;
}

void DoorKinematics::toggle() { setTarget(!targetOpen_, true); }

void DoorKinematics::setTarget(bool open, bool manual) {
    targetOpen_ = open;
    autoOpened_ = open && !manual;
    autoMotion_ = !manual;
    if (open && state_ != State::Open) state_ = State::Opening;
    if (!open && state_ != State::Closed) state_ = State::Closing;
}

void DoorKinematics::request(float time, int id) {
    lastRequest_ = time;
    if (id >= 0 && std::find(requesters_.begin(), requesters_.end(), id) == requesters_.end()) requesters_.push_back(id);
    // A manually opened door stays manual (no auto close); after a safety reverse the door stays shut until the body that
    // held it up has left the sweep.
    if (!targetOpen_ && reversedFor_ < 0) setTarget(true, false);
}

void DoorKinematics::step(float time, float dt) {
    if (dt > 0.0f && time == lastStep_) return;   // already stepped this tick
    if (time < lastStep_ && lastRequest_ > time) lastRequest_ = -1e30f;
    lastStep_ = time;
    blocked_ = false;
    blockedBy_ = -1;
    // An auto-opened door that is still shut does not start into somebody passing in front of it: it waits until
    // nobody but the walkers who asked for it stands in its sweep (they wait outside the sweep while it moves).
    held_ = false;
    if (autoOpened_ && targetOpen_ && theta_ <= 0.0f)
        for (const DoorBody& b : bodies_)
            if (std::find(requesters_.begin(), requesters_.end(), b.id) == requesters_.end() && inSweep(b.feet, b.radius)) {
                held_ = true;
                break;
            }
    requesters_.clear();
    if (reversedFor_ >= 0) {
        bool still = false;
        for (const DoorBody& b : bodies_) still = still || (b.id == reversedFor_ && inSweep(b.feet, b.radius));
        if (!still) reversedFor_ = -1;
    }
    // Auto close: the last walker asked closeDelay ago and nobody stands where the leaf sweeps.
    if (autoOpened_ && targetOpen_ && time - lastRequest_ > p_.closeDelay && !sweepOccupied()) {
        targetOpen_ = false;
        autoOpened_ = false;
        autoMotion_ = true;
    }
    if (dt > 0.0f && !held_) {
        float th = theta_, v = vel_;
        critDampStep(th, v, targetOpen_ ? p_.openDeg : 0.0f, p_.omega, dt);
        // The frame stop (closed) and the holder (open): the leaf cannot pass them.
        if (th <= 0.0f) th = 0.0f, v = 0.0f;
        if (th >= p_.openDeg) th = p_.openDeg, v = 0.0f;
        // A body the leaf would run into (or cut deeper into) stops it where it is; it starts again once the way is clear.
        // Moving away from a body that is already in contact is allowed, so nothing can lock the leaf for good.
        for (const DoorBody& b : bodies_)
            if (const float pen = penetration(b, th); pen > 0.0f && pen >= penetration(b, theta_) - 1e-5f) {
                blocked_ = true;
                blockedBy_ = b.id;
                th = theta_;
                v = 0.0f;
                break;
            }
        theta_ = th;
        vel_ = v;
        // Safety reverse of an automatic door: a leaf an AI walker has held up for kReverseAfter turns back (it was
        // swinging towards him, so going back frees him); an auto open then waits, shut, for the sweep to clear. A door
        // the player moved just stops against the body (E turns it back).
        blockedT_ = blocked_ ? blockedT_ + dt : 0.0f;
        if (blockedT_ > kReverseAfter && autoMotion_) {
            targetOpen_ = !targetOpen_;
            autoOpened_ = targetOpen_;   // turned back open: it closes by itself again later
            blockedT_ = 0.0f;
            if (targetOpen_) lastRequest_ = time;
            else reversedFor_ = blockedBy_;
        }
    }
    // Latch on a stop once the leaf has (practically) arrived.
    if (targetOpen_ && p_.openDeg - theta_ <= kSettleDeg && std::fabs(vel_) < kSettleSpeed) {
        theta_ = p_.openDeg;
        vel_ = 0.0f;
        state_ = State::Open;
    } else if (!targetOpen_ && theta_ <= kSettleDeg && std::fabs(vel_) < kSettleSpeed) {
        theta_ = 0.0f;
        vel_ = 0.0f;
        state_ = State::Closed;
    } else {
        state_ = targetOpen_ ? State::Opening : State::Closing;
    }
}

void DoorKinematics::leafCorners(float angleDeg, Vec3 out[4]) const {
    const float phi = yawDeg(angleDeg);
    const Vec3 u = axisU(phi), n = axisN(phi), h{p_.hinge.x, 0.0f, p_.hinge.z};
    const float ua = p_.u0, ub = p_.u0 + p_.width;
    out[0] = h + u * ua + n * p_.n0;
    out[1] = h + u * ub + n * p_.n0;
    out[2] = h + u * ub + n * p_.n1;
    out[3] = h + u * ua + n * p_.n1;
}

std::vector<Box> DoorKinematics::panelBoxes(float angleDeg) const {
    const float phi = yawDeg(angleDeg);
    const Vec3 u = axisU(phi), n = axisN(phi), h{p_.hinge.x, 0.0f, p_.hinge.z};
    const float y0 = p_.hinge.y + p_.bottom, y1 = y0 + p_.height;
    std::vector<Box> out;
    for (int k = 0; k < p_.segments; ++k) {
        const float ua = p_.u0 + p_.width * static_cast<float>(k) / static_cast<float>(p_.segments);
        const float ub = p_.u0 + p_.width * static_cast<float>(k + 1) / static_cast<float>(p_.segments);
        Box b{{1e9f, y0, 1e9f}, {-1e9f, y1, -1e9f}};
        for (float su : {ua, ub})
            for (float sn : {p_.n0, p_.n1}) {
                const Vec3 c = h + u * su + n * sn;
                b.min.x = std::min(b.min.x, c.x);
                b.max.x = std::max(b.max.x, c.x);
                b.min.z = std::min(b.min.z, c.z);
                b.max.z = std::max(b.max.z, c.z);
            }
        out.push_back(b);
    }
    return out;
}

bool DoorKinematics::hits(const DoorBody& b, float angleDeg) const { return penetration(b, angleDeg) > 0.0f; }

float DoorKinematics::penetration(const DoorBody& b, float angleDeg) const {
    const float y0 = p_.hinge.y + p_.bottom, y1 = y0 + p_.height;
    if (b.feet.y > y1 || b.feet.y + b.height < y0) return -1.0f;
    const float phi = yawDeg(angleDeg);
    const Vec3 d{b.feet.x - p_.hinge.x, 0.0f, b.feet.z - p_.hinge.z};
    const float su = flatDot(d, axisU(phi)), sn = flatDot(d, axisN(phi));
    const float cu = clampf(su, p_.u0, p_.u0 + p_.width), cn = clampf(sn, p_.n0, p_.n1);
    const float du = su - cu, dn = sn - cn;
    float dist = std::sqrt(du * du + dn * dn);
    if (dist == 0.0f)   // centre inside the leaf slab: count the way out through the nearest face as extra depth
        dist = -std::min(std::min(su - p_.u0, p_.u0 + p_.width - su), std::min(sn - p_.n0, p_.n1 - sn));
    return b.radius - dist;
}

bool DoorKinematics::inSweep(const Vec3& p, float radius) const {
    const DoorBody b{p, radius, 1.8f, -1};
    const int n = std::max(2, static_cast<int>(std::ceil(p_.openDeg / 5.0f)));
    for (int k = 0; k <= n; ++k)
        if (hits(b, p_.openDeg * static_cast<float>(k) / static_cast<float>(n))) return true;
    return false;
}

bool DoorKinematics::sweepOccupied() const {
    for (const DoorBody& b : bodies_)
        if (inSweep(b.feet, b.radius)) return true;
    return false;
}

Vec3 DoorKinematics::doorwayCentre() const {
    const float phi = yawDeg(0.0f);
    const Vec3 c = p_.hinge + axisU(phi) * (p_.u0 + 0.5f * p_.width) + axisN(phi) * (0.5f * (p_.n0 + p_.n1));
    return {c.x, p_.hinge.y + 1.0f, c.z};
}

float DoorKinematics::sideOf(const Vec3& p) const {
    const Vec3 d{p.x - p_.hinge.x, 0.0f, p.z - p_.hinge.z};
    return static_cast<float>(p_.swing) * (flatDot(d, axisN(yawDeg(0.0f))) - 0.5f * (p_.n0 + p_.n1));
}

bool DoorKinematics::crossesDoorway(const Vec3& a, const Vec3& b) const {
    if (std::fabs(a.y - p_.hinge.y) > 1.0f && std::fabs(b.y - p_.hinge.y) > 1.0f) return false;
    const float sa = sideOf(a), sb = sideOf(b);
    if ((sa > 0.0f) == (sb > 0.0f)) return false;
    const float t = sa / (sa - sb);
    const Vec3 q = a + (b - a) * t;
    const Vec3 d{q.x - p_.hinge.x, 0.0f, q.z - p_.hinge.z};
    const float su = flatDot(d, axisU(yawDeg(0.0f)));
    return su > p_.u0 - 0.1f && su < p_.u0 + p_.width + 0.1f;
}

bool DoorKinematics::inDoorway(const Vec3& p, float margin) const {
    if (std::fabs(p.y - p_.hinge.y) > 1.2f) return false;
    const Vec3 d{p.x - p_.hinge.x, 0.0f, p.z - p_.hinge.z};
    const float su = flatDot(d, axisU(yawDeg(0.0f)));
    return std::fabs(sideOf(p)) < margin && su > p_.u0 + 0.1f && su < p_.u0 + p_.width - 0.1f;   // within the opening
}

Vec3 DoorKinematics::handlePoint(const Vec3& from) const {
    const float phi = yawDeg(theta_);
    const Vec3 u = axisU(phi), n = axisN(phi);
    const Vec3 d{from.x - p_.hinge.x, 0.0f, from.z - p_.hinge.z};
    const float side = flatDot(d, n) - 0.5f * (p_.n0 + p_.n1);
    const float sn = side >= 0.0f ? p_.n1 + 0.05f : p_.n0 - 0.05f;
    const Vec3 c = p_.hinge + u * (p_.u0 + p_.width - 0.075f) + n * sn;
    return {c.x, p_.hinge.y + 1.0f, c.z};
}

}  // namespace ts
