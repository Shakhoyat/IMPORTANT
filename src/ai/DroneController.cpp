#include "ai/DroneController.h"

#include <algorithm>

#include "objects/Terrain.h"

namespace ts {

namespace {

// Scaled for the 20 m terrain: the search orbit covers the ring between the bunker and the forest patches.
constexpr float kSearchMid = 10.5f, kSearchSwing = 5.0f;  // orbit radius 5.5 .. 15.5 m
constexpr float kSearchBreath = 0.10f;                    // rad/s: one in-out cycle every ~63 s
constexpr float kSearchGroundSpeed = 2.4f;                // m/s along the orbit
constexpr float kSearchAltitude = 7.0f, kSearchSpeed = 3.0f;
constexpr float kScanSwing = 7.0f, kScanRate = 0.55f;     // camera aim sweeps radius -+ 7 m every ~16 s
constexpr float kScanLead = 0.35f;                        // rad ahead of the UAV along the orbit
constexpr float kScanMaxRadius = 18.5f;
constexpr float kStandOff = 5.5f, kTrackAltitude = 4.6f, kTrackOrbitRate = 0.10f;
constexpr float kReacquireRate = 0.30f, kReacquireTimeout = 12.0f;
constexpr float kBoundaryRadius = 19.5f;  // never fly outside the fenced area
constexpr float kMinClearance = 2.0f;     // metres above the terrain
constexpr float kAvoidRadius = 0.9f;      // obstacle push-away distance
constexpr int kGroundRaySteps = 1000;     // 5 cm steps, max 50 m
constexpr float kServoGain = 5.0f;        // 1/s, proportional visual-servo gain
constexpr int kFramesToAcquire = 3, kFramesToLose = 45;  // coast 0.75 s through brief occlusions (branches)
constexpr float kMaxAccel = 3.0f;         // m/s^2
const Vec3 kGimbalOffset{0.0f, -0.11f, 0.1f};  // camera pivot in the UAV body frame

float wrapDeg(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

Vec3 horizontal(const Vec3& v) { return {v.x, 0.0f, v.z}; }

Mat4 bodyMatrix(const DroneState& s) {
    return translate(s.position) * rotateEulerYXZ({s.pitchDeg, s.yawDeg, s.rollDeg});
}

}  // namespace

const char* droneModeName(DroneMode m) {
    switch (m) {
        case DroneMode::Search: return "SEARCH";
        case DroneMode::Track: return "TRACK";
        case DroneMode::Reacquire: return "REACQUIRE";
    }
    return "?";
}

bool DroneController::imagePointToGround(float u, float v, const Mat4& cameraWorld, Vec3& hit) {
    const float aspect = static_cast<float>(kCvWidth) / static_cast<float>(kCvHeight);
    const float ty = std::tan(radians(kCvFovYDeg) * 0.5f), tx = ty * aspect;
    // The camera looks along local +Z with +Y up, so image-right is local -X.
    const Vec3 dir = normalize(transformDirection(cameraWorld, normalize(Vec3{-u * tx, v * ty, 1.0f})));
    Vec3 p = transformPoint(cameraWorld, {0, 0, 0});
    for (int i = 0; i < kGroundRaySteps; ++i) {  // ray march in 5 cm steps
        p = p + dir * 0.05f;
        if (p.y <= terrainHeight(p.x, p.z)) {
            hit = p;
            return true;
        }
    }
    return false;
}

Vec3 DroneController::pixelRay(float u, float v, const Mat4& cameraWorld) {
    const float aspect = static_cast<float>(kCvWidth) / static_cast<float>(kCvHeight);
    const float ty = std::tan(radians(kCvFovYDeg) * 0.5f), tx = ty * aspect;
    return normalize(transformDirection(cameraWorld, normalize(Vec3{-u * tx, v * ty, 1.0f})));
}

void DroneController::faceTowards(float dt, const Vec3& target, float rateDeg, DroneState& s) const {
    const Vec3 d = target - s.position;
    yawTowards(dt, degrees(std::atan2(d.x, d.z)), rateDeg, s);
}

void DroneController::yawTowards(float dt, float desiredDeg, float rateDeg, DroneState& s) const {
    const float diff = wrapDeg(desiredDeg - s.yawDeg);
    s.yawDeg = wrapDeg(s.yawDeg + clampf(diff, -rateDeg * dt, rateDeg * dt));
}

void DroneController::flyTowards(float dt, const Vec3& goal, float maxSpeed, DroneState& s) {
    Vec3 desiredVel = (goal - s.position) * 1.2f;

    // Keep clear of obstacles (masts, watchtower, trees): push away from any box closer than kAvoidRadius.
    for (const Box& b : obstacles_) {
        const Vec3 closest{clampf(s.position.x, b.min.x, b.max.x), clampf(s.position.y, b.min.y, b.max.y),
                           clampf(s.position.z, b.min.z, b.max.z)};
        const Vec3 away = s.position - closest;
        const float d = length(away);
        if (d < kAvoidRadius)
            desiredVel = desiredVel + (d > 1e-3f ? away / d : Vec3{0, 1, 0}) * (2.5f * (kAvoidRadius - d) / kAvoidRadius);
    }
    if (length(desiredVel) > maxSpeed) desiredVel = normalize(desiredVel) * maxSpeed;

    const Vec3 prevVel = s.velocity;
    Vec3 dv = desiredVel - s.velocity;
    if (length(dv) > kMaxAccel * dt) dv = normalize(dv) * (kMaxAccel * dt);
    s.velocity = s.velocity + dv;
    s.position = s.position + s.velocity * dt;

    // Stay inside the patrol area and above the ground.
    const Vec3 flat = horizontal(s.position);
    if (length(flat) > kBoundaryRadius) {
        const Vec3 clamped = normalize(flat) * kBoundaryRadius;
        s.position.x = clamped.x;
        s.position.z = clamped.z;
    }
    s.position.y = std::max(s.position.y, terrainHeight(s.position.x, s.position.z) + kMinClearance);

    // A multirotor tilts into its acceleration: nose down to speed up, bank into turns.
    const Vec3 accel = (s.velocity - prevVel) / std::max(dt, 1e-3f);
    const float yaw = radians(s.yawDeg);
    const Vec3 fwd{std::sin(yaw), 0, std::cos(yaw)}, left{std::cos(yaw), 0, -std::sin(yaw)};
    const float targetPitch = clampf(4.0f * dot(s.velocity, fwd) + 3.0f * dot(accel, fwd), -18.0f, 18.0f);
    const float targetRoll = clampf(-4.0f * dot(s.velocity, left) - 3.0f * dot(accel, left), -18.0f, 18.0f);
    s.pitchDeg = lerpf(s.pitchDeg, targetPitch, std::min(1.0f, 4.0f * dt));
    s.rollDeg = lerpf(s.rollDeg, targetRoll, std::min(1.0f, 4.0f * dt));
}

void DroneController::pointGimbalAt(float dt, const Vec3& target, DroneState& s) const {
    const Vec3 local = transformPoint(inverse(bodyMatrix(s)), target) - kGimbalOffset;
    const float yaw = degrees(std::atan2(local.x, local.z));
    const float pitch = degrees(std::atan2(-local.y, std::sqrt(local.x * local.x + local.z * local.z)));
    const float k = std::min(1.0f, 3.0f * dt);
    s.gimbalYawDeg = wrapDeg(s.gimbalYawDeg + wrapDeg(yaw - s.gimbalYawDeg) * k);
    s.gimbalPitchDeg = lerpf(s.gimbalPitchDeg, clampf(pitch, 5.0f, 88.0f), k);
}

void DroneController::update(float dt, float time, const Detection& detIn, const Mat4& cameraWorld, DroneState& s) {
    // ---- 0. Target discrimination: blobs on a known wreck are not new targets ----
    for (Ignore& z : ignore_) z.timeLeft -= dt;
    ignore_.erase(std::remove_if(ignore_.begin(), ignore_.end(), [](const Ignore& z) { return z.timeLeft <= 0.0f; }),
                  ignore_.end());
    Detection det = detIn;
    if (det.found && (!ignore_.empty() || !friendly_.empty())) {
        Vec3 hit;
        if (imagePointToGround(det.u, det.v, cameraWorld, hit)) {
            for (const Ignore& z : ignore_)
                if (length(horizontal(hit - z.point)) < kWreckIgnoreRadius) det.found = false;
            for (const Vec3& f : friendly_)
                if (length(horizontal(hit - f)) < kFriendlyRadius) det.found = false;
        }
    }

    // ---- 1. Vision bookkeeping and state transitions ----
    if (det.found) {
        ++s.framesSeen;
        s.framesLost = 0;
        Vec3 hit;
        if (imagePointToGround(det.u, det.v, cameraWorld, hit)) {
            s.targetEstimate = s.hasEstimate ? lerp(s.targetEstimate, hit, 0.35f) : hit;
            s.hasEstimate = true;
        }
        s.timeSinceSeen = 0.0f;
    } else {
        s.framesSeen = 0;
        ++s.framesLost;
        s.timeSinceSeen += dt;
    }
    if (s.mode != DroneMode::Track && det.found && s.framesSeen >= kFramesToAcquire) {
        s.mode = DroneMode::Track;
        const Vec3 away = horizontal(s.position - s.targetEstimate);
        orbitAngle_ = std::atan2(away.z, away.x);
    } else if (s.mode == DroneMode::Track && s.framesLost > kFramesToLose) {
        s.mode = DroneMode::Reacquire;
        const Vec3 away = horizontal(s.position - s.targetEstimate);
        orbitAngle_ = std::atan2(away.z, away.x);
    } else if (s.mode == DroneMode::Reacquire && s.timeSinceSeen > kReacquireTimeout) {
        s.mode = DroneMode::Search;
        orbitAngle_ = std::atan2(s.position.z, s.position.x);
    }

    // ---- 1b. Launcher: hold the target for kLockTime, then request a round (the scene releases it) ----
    s.launchCooldown = std::max(0.0f, s.launchCooldown - dt);
    s.lockTime = (s.mode == DroneMode::Track && det.found) ? s.lockTime + dt : 0.0f;
    if (!s.manual && s.lockTime >= kLockTime && s.launchCooldown <= 0.0f && s.hasEstimate) {
        const Vec3& e = s.targetEstimate;
        const bool keepOut = hasKeepOut_ && e.x > keepOut_.min.x && e.x < keepOut_.max.x && e.z > keepOut_.min.z &&
                             e.z < keepOut_.max.z;
        if (!keepOut) {
            s.launchRequest = true;
            s.launchCooldown = kLaunchInterval;
        }
    }
    if (s.manual) return;  // flown by the user: no autonomous behaviour

    // ---- 2. Behaviour of the current mode ----
    if (s.mode == DroneMode::Track) {
        // Visual servoing: turn the image error (u, v) into gimbal rate commands.
        if (det.found) {
            const float aspect = static_cast<float>(kCvWidth) / static_cast<float>(kCvHeight);
            const float ty = std::tan(radians(kCvFovYDeg) * 0.5f), tx = ty * aspect;
            const float errYaw = degrees(std::atan(det.u * tx));    // + = target right of centre
            const float errPitch = degrees(std::atan(det.v * ty));  // + = target above centre
            s.gimbalYawDeg = wrapDeg(s.gimbalYawDeg - kServoGain * errYaw * dt);
            s.gimbalPitchDeg = clampf(s.gimbalPitchDeg - kServoGain * errPitch * dt, 5.0f, 88.0f);
        } else {
            pointGimbalAt(dt, s.targetEstimate, s);  // coasting: hold the camera on the last estimate
        }
        orbitAngle_ += kTrackOrbitRate * dt;
        const Vec3 goal = s.targetEstimate + Vec3{std::cos(orbitAngle_), 0, std::sin(orbitAngle_)} * kStandOff;
        flyTowards(dt, {goal.x, terrainHeight(goal.x, goal.z) + kTrackAltitude, goal.z}, 2.4f, s);
        const Vec3 toTarget = s.targetEstimate - s.position;
        yawTowards(dt, degrees(std::atan2(toTarget.x, toTarget.z)), 60.0f, s);
    } else if (s.mode == DroneMode::Reacquire) {
        orbitAngle_ += kReacquireRate * dt;
        const Vec3 goal = s.targetEstimate + Vec3{std::cos(orbitAngle_), 0, std::sin(orbitAngle_)} * kStandOff;
        flyTowards(dt, {goal.x, terrainHeight(goal.x, goal.z) + kTrackAltitude, goal.z}, 2.2f, s);
        const Vec3 toTarget = s.targetEstimate - s.position;
        yawTowards(dt, degrees(std::atan2(toTarget.x, toTarget.z)), 60.0f, s);
        pointGimbalAt(dt, s.targetEstimate, s);
    } else {
        // Breathing orbit: the radius swings between kSearchMid -+ kSearchSwing, so over a few laps the orbit
        // itself passes over the bunker ring, the open ground and the forest edges near the fence.
        const float radius = kSearchMid + kSearchSwing * std::sin(kSearchBreath * time);
        orbitAngle_ += (kSearchGroundSpeed / radius) * dt;
        const Vec3 goal{radius * std::cos(orbitAngle_), 0, radius * std::sin(orbitAngle_)};
        flyTowards(dt, {goal.x, terrainHeight(goal.x, goal.z) + kSearchAltitude, goal.z}, kSearchSpeed, s);
        const Vec3 tangent{-std::sin(orbitAngle_), 0, std::cos(orbitAngle_)};
        yawTowards(dt, degrees(std::atan2(tangent.x, tangent.z)), 45.0f, s);
        // The camera scans a ground point slightly ahead of the UAV that sweeps across the orbit, from well
        // inside it to well outside it (the whole disc between the bunker and the fence is covered).
        const float aimRadius = clampf(radius + kScanSwing * std::sin(kScanRate * time), 1.5f, kScanMaxRadius);
        const float aimAngle = orbitAngle_ + kScanLead;
        pointGimbalAt(dt, {aimRadius * std::cos(aimAngle), terrainHeight(aimRadius * std::cos(aimAngle), aimRadius * std::sin(aimAngle)),
                           aimRadius * std::sin(aimAngle)}, s);
    }
}

}  // namespace ts
