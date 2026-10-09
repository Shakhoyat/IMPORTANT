#include "scene/CharacterMotor.h"

#include <algorithm>
#include <cmath>

namespace ts {

namespace {

float smooth01(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float rampTowards(float v, float target, float step) {
    return v < target ? std::min(target, v + step) : std::max(target, v - step);
}

}  // namespace

void critDampStep(float& x, float& v, float goal, float omega, float dt) {
    if (dt <= 0.0f) return;
    if (omega <= 0.0f) {   // no spring: free motion
        x += v * dt;
        return;
    }
    const float e0 = x - goal, k = v + omega * e0, decay = std::exp(-omega * dt);
    x = goal + (e0 + k * dt) * decay;
    v = (v - omega * k * dt) * decay;
}

float critRiseTime(float omega, float fraction) {
    if (omega <= 0.0f || fraction <= 0.0f) return 0.0f;
    fraction = std::min(fraction, 0.999999f);
    // f(u) = 1 - (1 + u) e^-u - fraction, f'(u) = u e^-u (u = w t > 0)
    float u = 2.0f;
    for (int i = 0; i < 40; ++i) {
        const float e = std::exp(-u), f = 1.0f - (1.0f + u) * e - fraction, d = u * e;
        if (d < 1e-12f) break;
        const float next = std::max(1e-4f, u - f / d);
        if (std::fabs(next - u) < 1e-7f) {
            u = next;
            break;
        }
        u = next;
    }
    return u / omega;
}

void CharacterMotor::reset(float x, float z, float feetY) {
    body_ = FootBody{};
    body_.x = x;
    body_.z = z;
    body_.feetY = feetY;
    vx_ = vz_ = ax_ = az_ = 0.0f;
    sprintRamp_ = crouchRamp_ = 0.0f;
    stepOff_ = stepVel_ = 0.0f;
    mx_ = mz_ = speed_ = 0.0f;
    bufferAge_ = -1.0f;
    jumpedInAir_ = jumped_ = landed_ = slopeBlocked_ = false;
    landSpeed_ = 0.0f;
}

float CharacterMotor::sprintBlend() const { return smooth01(sprintRamp_); }
float CharacterMotor::crouchBlend() const { return smooth01(crouchRamp_); }
float CharacterMotor::height() const { return lerpf(params.standHeight, params.crouchHeight, crouchBlend()); }

void CharacterMotor::step(const MotorInput& in, const WalkSpace* ws, const std::vector<Box>& solids, float dt) {
    jumped_ = landed_ = slopeBlocked_ = false;
    landSpeed_ = 0.0f;
    if (dt <= 0.0f) return;
    const MotorParams& P = params;

    // 1) Stance blends. A crouched body under a low ceiling stays down until there is room to stand.
    const bool canStand = crouchRamp_ <= 0.0f ||
                          headroom(ws, solids, body_.x, body_.z, body_.feetY, foot.radius * 0.9f, P.standHeight);
    const bool crouchWant = in.crouch || !canStand;
    const float rate = dt / std::max(P.blendTime, 1e-3f);
    crouchRamp_ = rampTowards(crouchRamp_, crouchWant ? 1.0f : 0.0f, rate);
    const bool sprintWant = in.sprint && in.forward > 0.1f && !crouchWant;
    sprintRamp_ = rampTowards(sprintRamp_, sprintWant ? 1.0f : 0.0f, rate);
    const float sb = sprintBlend(), cb = crouchBlend();

    // 2) Wish velocity in the world (body heading frame), diagonal input not faster than the larger top speed.
    float fwdTop = in.forward >= 0.0f ? lerpf(P.walkSpeed, P.runSpeed, sb) : P.backSpeed;
    fwdTop = lerpf(fwdTop, std::min(fwdTop, P.crouchSpeed), cb);
    const float sideTop = lerpf(lerpf(P.strafeWalk, P.strafeRun, sb), P.strafeCrouch, cb);
    const float yr = radians(in.yawDeg);
    const float fx = std::sin(yr), fz = std::cos(yr), rx = -std::cos(yr), rz = std::sin(yr);
    const float wf = clampf(in.forward, -1.0f, 1.0f) * fwdTop, wsd = clampf(in.strafe, -1.0f, 1.0f) * sideTop;
    float wx = fx * wf + rx * wsd, wz = fz * wf + rz * wsd;
    const float wlen = std::sqrt(wx * wx + wz * wz), wmax = std::max(std::fabs(wf) > 0.0f ? fwdTop : 0.0f, std::fabs(wsd) > 0.0f ? sideTop : 0.0f);
    if (wlen > wmax && wlen > 1e-6f) {
        wx *= wmax / wlen;
        wz *= wmax / wlen;
    }

    // 3) Velocity response: critically damped towards the wish. On the ground: braking (the change opposes the
    // velocity) uses the faster rate. In the air: a fifth of the rate, and without input the momentum is kept.
    const bool input = std::fabs(in.forward) > 0.05f || std::fabs(in.strafe) > 0.05f;
    if (body_.grounded) {
        const bool braking = (wx - vx_) * vx_ + (wz - vz_) * vz_ < 0.0f;
        const float w = braking ? P.omegaStop : P.omegaAccel;
        critDampStep(vx_, ax_, wx, w, dt);
        critDampStep(vz_, az_, wz, w, dt);
    } else if (input) {
        const float w = P.omegaAccel * P.airControl;
        critDampStep(vx_, ax_, wx, w, dt);
        critDampStep(vz_, az_, wz, w, dt);
    } else {
        ax_ = az_ = 0.0f;
    }
    float dx = vx_ * dt, dz = vz_ * dt;

    // 4) Slope limit: on the ground, no uphill component over ground steeper than the limit (sideways and downhill
    // movement still pass). Measured half a step ahead.
    if (body_.grounded && (dx != 0.0f || dz != 0.0f)) {
        const Vec3 g = groundSlope(ws, body_.x + 0.5f * dx, body_.z + 0.5f * dz, body_.feetY);
        const float gl = std::sqrt(g.x * g.x + g.z * g.z);
        if (gl > std::tan(radians(P.slopeLimitDeg))) {
            const float ux = g.x / gl, uz = g.z / gl, up = dx * ux + dz * uz;
            if (up > 0.0f) {
                dx -= ux * up;
                dz -= uz * up;
                const float vu = vx_ * ux + vz_ * uz, au = ax_ * ux + az_ * uz;
                if (vu > 0.0f) {
                    vx_ -= ux * vu;
                    vz_ -= uz * vu;
                }
                if (au > 0.0f) {
                    ax_ -= ux * au;
                    az_ -= uz * au;
                }
                slopeBlocked_ = true;
            }
        }
    }

    // 5) Jump: buffered press, fired on the ground or within the coyote time after walking off a ledge.
    if (in.jumpPressed) bufferAge_ = 0.0f;
    bool jump = false;
    const bool canJump = body_.grounded || (!jumpedInAir_ && body_.airTime <= P.coyoteTime);
    if (bufferAge_ >= 0.0f && bufferAge_ <= P.jumpBuffer && canJump) {
        jump = true;
        bufferAge_ = -1.0f;
        if (!body_.grounded) {   // coyote: take off from the fall (stepFoot jumps only from the ground)
            body_.grounded = true;
            body_.vy = 0.0f;
        }
    }

    // 6) Collision, ground follow, gravity (scene/WalkSpace stepFoot) with the blended capsule height.
    foot.height = height();
    const float ox = body_.x, oz = body_.z, oy = body_.feetY;
    const bool wasGrounded = body_.grounded && !jump;
    stepFoot(body_, ws, solids, dx, dz, jump, dt, foot);
    if (body_.jumped) jumpedInAir_ = true;
    mx_ = body_.x - ox;
    mz_ = body_.z - oz;
    const float movedLen = std::sqrt(mx_ * mx_ + mz_ * mz_);
    speed_ = movedLen / dt;
    distance_ += movedLen;
    // A wall / solid stopped part of the step: drop the velocity into it (slide along the wall, no build-up).
    if (body_.blocked) {
        const float bx = dx - mx_, bz = dz - mz_, bl = std::sqrt(bx * bx + bz * bz);
        if (bl > 1e-6f) {
            const float nx = bx / bl, nz = bz / bl, vn = vx_ * nx + vz_ * nz, an = ax_ * nx + az_ * nz;
            if (vn > 0.0f) {
                vx_ -= nx * vn;
                vz_ -= nz * vn;
            }
            if (an > 0.0f) {
                ax_ -= nx * an;
                az_ -= nz * an;
            }
        }
    }

    // 7) Step smoothing: a height change on the ground beyond a 45 deg slope is a step; the drawn feet keep their
    // height and follow on the spring.
    if (wasGrounded && body_.grounded) {
        const float dy = body_.feetY - oy;
        if (std::fabs(dy) > movedLen + P.stepDetect) stepOff_ -= dy;
    }
    critDampStep(stepOff_, stepVel_, 0.0f, P.stepOmega, dt);
    stepOff_ = clampf(stepOff_, -0.6f, 0.6f);

    jumped_ = body_.jumped;
    landed_ = body_.landed;
    landSpeed_ = body_.landSpeed;
    if (body_.grounded) jumpedInAir_ = false;
    if (bufferAge_ >= 0.0f) {
        bufferAge_ += dt;
        if (bufferAge_ > P.jumpBuffer + 1e-5f) bufferAge_ = -1.0f;
    }
}

Vec3 groundSlope(const WalkSpace* ws, float x, float z, float feetY) {
    const float scales[3] = {0.1f, 0.25f, 0.5f};
    // WalkSpace floors are picked within a step above the given level: look from 1 m higher, so a steep floor region
    // 0.5 m ahead still counts as ground (not the floor under it).
    const float probeY = feetY + 1.0f;
    auto axis = [&](float ax, float az) {
        float best = 0.0f;
        int sign = 0;
        for (int i = 0; i < 3; ++i) {
            const float e = scales[i];
            const float s = (groundHeight(ws, x + ax * e, z + az * e, probeY) - groundHeight(ws, x - ax * e, z - az * e, probeY)) / (2.0f * e);
            const int sg = s > 0.0f ? 1 : s < 0.0f ? -1 : 0;
            if (sg == 0) return 0.0f;
            if (i == 0) {
                sign = sg;
                best = std::fabs(s);
            } else {
                if (sg != sign) return 0.0f;
                best = std::min(best, std::fabs(s));
            }
        }
        return static_cast<float>(sign) * best;
    };
    return {axis(1.0f, 0.0f), 0.0f, axis(0.0f, 1.0f)};
}

bool headroom(const WalkSpace* ws, const std::vector<Box>& solids, float x, float z, float feetY, float radius, float height) {
    const float top = feetY + height;
    if (ws && ws->ceilingHeight(x, z, feetY) < top) return false;
    for (const Box& b : solids) {
        // an overhang: its underside above the knees and below the head, over the body column
        if (b.min.y <= feetY + 0.5f || b.min.y >= top) continue;
        if (x + radius < b.min.x || x - radius > b.max.x || z + radius < b.min.z || z - radius > b.max.z) continue;
        return false;
    }
    return true;
}

Vec3 headBobOffset(float phase, float groundSpeed) {
    const float v = std::fabs(groundSpeed);
    if (v < 1e-3f) return {0.0f, 0.0f, 0.0f};
    const float k = clampf(v / 0.6f, 0.0f, 1.0f);                         // fades in over the first steps
    const float amp = std::min(0.030f, 0.012f + 0.0086f * std::max(0.0f, v - 1.3f)) * k;
    const float tau = 2.0f * PI;
    // highest at mid-stance (phase 0.3 / 0.8, as the gait's pelvis), lowest at each foot contact
    const float y = amp * 0.5f * (std::cos(2.0f * tau * (phase - 0.3f)) - 1.0f);
    const float x = 0.45f * amp * std::sin(tau * phase);
    return {x, y, 0.0f};
}

}  // namespace ts
