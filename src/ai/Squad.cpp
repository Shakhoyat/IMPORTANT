#include "ai/Squad.h"

#include <cstdlib>

#include <algorithm>
#include <cmath>

#include "objects/Terrain.h"

namespace ts {

namespace {

Vec3 flat(const Vec3& v) { return {v.x, 0.0f, v.z}; }

float wrapPi(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

}  // namespace

const char* squadRoleName(SquadRole r) {
    switch (r) {
        case SquadRole::Patrol: return "PATROL";
        case SquadRole::Lead: return "LEAD";
        case SquadRole::Wing: return "WING";
        case SquadRole::Manual: return "MANUAL";
        case SquadRole::Search: return "SEARCH";
        case SquadRole::Solo: return "SOLO";
    }
    return "?";
}

const char* strikePhaseName(StrikePhase p) {
    switch (p) {
        case StrikePhase::None: return "NONE";
        case StrikePhase::Tracking: return "TRACKING";
        case StrikePhase::Planned: return "STRIKE PLANNED";
        case StrikePhase::Inbound: return "ROUNDS INBOUND";
    }
    return "?";
}

float roundFlightTime(float dIn) {
    const float rampDist = 0.5f * (8.0f + 60.0f) * 0.4f;  // 13.6 m while accelerating
    const float d = std::max(0.0f, dIn - 0.5f);             // the proximity fuse fires 0.5 m short
    float t;
    if (d <= rampDist) t = (-8.0f + std::sqrt(64.0f + 260.0f * d)) / 130.0f;  // d = 8 t + 65 t^2
    else t = 0.4f + (d - rampDist) / 60.0f;
    return t;
}

Squad::Squad() {
    drones[0].position = {0.0f, kSquadPatrolAlt, 12.5f};
    drones[1].position = {0.0f, kSquadPatrolAlt, -12.5f};
    drones[1].yawDeg = 0.0f;
}

void Squad::setObstacles(const std::vector<Box>& b) {
    obstacles_ = b;
    for (DroneController& c : ctl_) c.setObstacles(b);
}

bool Squad::clearShot(const Vec3& from, const Vec3& to) const {
    // Slab test of the segment against every box, ignoring the last 0.8 m (the target's own surroundings).
    const Vec3 d = to - from;
    const float len = length(d);
    if (len < 1e-3f) return true;
    const float tEnd = std::max(0.0f, 1.0f - 0.8f / len);
    for (const Box& b : obstacles_) {
        float t0 = 0.0f, t1 = tEnd;
        const float o[3] = {from.x, from.y, from.z}, v[3] = {d.x, d.y, d.z};
        const float lo[3] = {b.min.x, b.min.y, b.min.z}, hi[3] = {b.max.x, b.max.y, b.max.z};
        bool hit = true;
        for (int k = 0; k < 3 && hit; ++k) {
            if (std::fabs(v[k]) < 1e-6f) {
                if (o[k] < lo[k] || o[k] > hi[k]) hit = false;
                continue;
            }
            float a = (lo[k] - o[k]) / v[k], c = (hi[k] - o[k]) / v[k];
            if (a > c) std::swap(a, c);
            t0 = std::max(t0, a);
            t1 = std::min(t1, c);
            if (t0 > t1) hit = false;
        }
        if (hit) return false;
    }
    return true;
}

bool Squad::friendlyAt(const Vec3& p) const {
    for (const FriendlyMark& f : friendly_)
        if (length(flat(p - f.p)) < f.r) return true;
    return false;
}

bool Squad::ignored(const Vec3& p) const {
    for (const Ignore& z : ignore_)
        if (length(flat(p - z.p)) < kWreckIgnoreRadius) return true;
    return friendlyAt(p);
}

float Squad::prnd() {
    prng_ = prng_ * 1664525u + 1013904223u;
    return static_cast<float>(prng_ >> 8) / 16777216.0f;
}

void Squad::setPatrolSeed(uint32_t seed) {
    patrolSeed_ = seed;
    prng_ = seed ? seed : 1u;
    for (int i = 0; i < kCount; ++i) {
        patrol_[i] = PatrolParams{};
        nextDetour_[i] = detourUntil_[i] = -1.0f;
    }
    if (!seed) return;
    // One shared sweep rate keeps the two UAVs on opposite sides; radius and breathing differ per UAV
    // (r_max <= 17.5 m stays inside the fence ring).
    const float rate = (2.6f / 12.5f) * (0.85f + 0.3f * prnd());
    for (int i = 0; i < kCount; ++i) {
        PatrolParams& p = patrol_[i];
        p.baseRadius = 10.0f + 2.5f * prnd();
        p.amplitude = 3.0f + 2.0f * prnd();
        p.breathing = 0.07f + 0.08f * prnd();
        p.breathPhase = 2.0f * PI * prnd();
        p.angularSpeed = rate;
        p.aimSwing = 4.5f + 3.0f * prnd();
        p.aimRate = 0.45f + 0.35f * prnd();
    }
}

float Squad::threat(const Track& t) const {
    // Lower = more dangerous: distance to the nearest protected asset, minus a bit for speed towards it.
    float best = 1e9f;
    for (const Vec3& a : assets_) {
        const Vec3 d = flat(a - t.position());
        const float dist = length(d);
        const float closing = dist > 1e-3f ? dot(t.velocity(), d / dist) : 0.0f;
        best = std::min(best, dist - 2.0f * closing);
    }
    return assets_.empty() ? length(flat(t.position())) : best;
}

Vec3 Squad::cameraPosition(int uav) const { return camPos_[uav]; }

// kSquadSplitThreat (TS_SPLIT_THREAT overrides it for tuning runs).
static float splitThreat() {
    static const float v = [] {
        const char* e = std::getenv("TS_SPLIT_THREAT");
        return e && *e ? static_cast<float>(std::atof(e)) : kSquadSplitThreat;
    }();
    return v;
}

bool Squad::designatorValid(int uav) const {
    if (uav == detached_ && side_.phase == StrikePhase::Inbound && side_.launched[uav]) return true;
    return strike_.track >= 0 && (strike_.phase == StrikePhase::Planned || strike_.phase == StrikePhase::Inbound) &&
           strike_.launched[uav];
}

void Squad::patrol(float dt, float time, int i) {
    // Breathing orbit, the two UAVs 180 degrees apart and in opposite radius phase: one covers the inner ring
    // while the other covers the outer ring, so together they sweep the whole compound.
    DroneState& s = drones[i];
    const float phase = i == 0 ? 0.0f : PI;
    if (patrolSeed_) {
        // Seeded patrol: every so often a UAV leaves the orbit to look at a random spot of the compound.
        if (nextDetour_[i] < 0.0f) nextDetour_[i] = time + kDetourMinGap * (0.3f + 0.7f * prnd()) + 8.0f * i;
        if (detourUntil_[i] < time && time >= nextDetour_[i]) {
            const float a = 2.0f * PI * prnd(), r = 3.0f + 12.0f * prnd();
            detourPoint_[i] = {r * std::cos(a), 0.0f, r * std::sin(a)};
            detourPoint_[i].y = terrainHeight(detourPoint_[i].x, detourPoint_[i].z);
            detourUntil_[i] = time + kDetourMinTime + (kDetourMaxTime - kDetourMinTime) * prnd();
            nextDetour_[i] = detourUntil_[i] + kDetourMinGap + (kDetourMaxGap - kDetourMinGap) * prnd();
            ++detours_;
        }
        if (detourUntil_[i] >= time) {
            // Hover off-set from the spot (camera looking down at it at ~50 deg), a little higher than the orbit.
            const Vec3 c = detourPoint_[i];
            const Vec3 away = length(flat(c)) > 1.0f ? normalize(flat(c)) : Vec3{1, 0, 0};
            const Vec3 g = c - away * 5.0f;
            ctl_[i].flyTo(dt, {g.x, terrainHeight(g.x, g.z) + kSquadPatrolAlt + 0.5f * i, g.z}, 3.2f, s);
            ctl_[i].faceTowards(dt, c, 45.0f, s);
            ctl_[i].aimGimbalAt(dt, c, s);
            s.mode = DroneMode::Search;
            return;
        }
        const PatrolParams& p = patrol_[i];
        const float radius = p.baseRadius + p.amplitude * std::sin(p.breathing * time + p.breathPhase + phase);
        const float angle = p.angularSpeed * time + phase;
        const Vec3 goal{radius * std::cos(angle), 0.0f, radius * std::sin(angle)};
        ctl_[i].flyTo(dt, {goal.x, terrainHeight(goal.x, goal.z) + kSquadPatrolAlt, goal.z}, 3.2f, s);
        const Vec3 tangent{-std::sin(angle), 0.0f, std::cos(angle)};
        ctl_[i].faceTowards(dt, s.position + tangent, 45.0f, s);
        const float aimR = clampf(radius + p.aimSwing * std::sin(p.aimRate * time + 1.7f * phase), 2.0f, 18.5f);
        const float aimA = angle + 0.35f;
        const Vec3 aim{aimR * std::cos(aimA), 0.0f, aimR * std::sin(aimA)};
        ctl_[i].aimGimbalAt(dt, {aim.x, terrainHeight(aim.x, aim.z), aim.z}, s);
        s.mode = DroneMode::Search;
        return;
    }
    const float radius = 11.0f + 4.0f * std::sin(0.11f * time + phase);
    const float angle = (2.6f / 12.5f) * time + phase;
    const Vec3 goal{radius * std::cos(angle), 0.0f, radius * std::sin(angle)};
    ctl_[i].flyTo(dt, {goal.x, terrainHeight(goal.x, goal.z) + kSquadPatrolAlt, goal.z}, 3.2f, s);
    const Vec3 tangent{-std::sin(angle), 0.0f, std::cos(angle)};
    ctl_[i].faceTowards(dt, s.position + tangent, 45.0f, s);
    // Camera sweeps a ground point ahead that swings across the orbit (inside to the fence).
    const float aimR = clampf(radius + 6.0f * std::sin(0.6f * time + 1.7f * phase), 2.0f, 18.5f);
    const float aimA = angle + 0.35f;
    const Vec3 aim{aimR * std::cos(aimA), 0.0f, aimR * std::sin(aimA)};
    ctl_[i].aimGimbalAt(dt, {aim.x, terrainHeight(aim.x, aim.z), aim.z}, s);
    s.mode = DroneMode::Search;
}

void Squad::reacquire(float dt, float time, int i) {
    // The target went behind cover: both UAVs circle its last known position on opposite sides and at
    // different radii / heights, cameras on the spot, to look around the occluder from new angles.
    DroneState& s = drones[i];
    const float a = 0.45f * time + (i == 0 ? 0.0f : PI);
    const float r = i == 0 ? 5.0f : 8.0f;
    const Vec3 g = lastKnown_ + Vec3{std::cos(a), 0.0f, std::sin(a)} * r;
    ctl_[i].flyTo(dt, {g.x, terrainHeight(g.x, g.z) + (i == 0 ? 6.0f : 8.5f), g.z}, 3.4f, s);
    ctl_[i].faceTowards(dt, lastKnown_, 60.0f, s);
    ctl_[i].aimGimbalAt(dt, lastKnown_, s);
    s.mode = DroneMode::Reacquire;
}

void Squad::engage(float dt, const Track& t, int lead, int wing, const bool manual[kCount]) {
    const Vec3 target = t.position() + t.velocity() * 0.3f;
    // Lead keeps its bearing; wing goes ~90 degrees around (the side it is already closer to).
    const Vec3 dl = flat(drones[lead].position - target);
    const float aLead = std::atan2(dl.z, dl.x);
    float aWing = aLead + 0.5f * PI;
    if (wing >= 0) {
        const Vec3 dw = flat(drones[wing].position - target);
        const float cur = std::atan2(dw.z, dw.x);
        if (std::fabs(wrapPi(cur - (aLead - 0.5f * PI))) < std::fabs(wrapPi(cur - aWing))) aWing = aLead - 0.5f * PI;
    }
    for (int i = 0; i < kCount; ++i) {
        if (manual[i] || (i != lead && i != wing)) continue;
        // No clear line of fire (trees, the watchtower...): keep circling round the target and climb for a
        // steeper shot until one opens; the detour resets once the shot is clear.
        const bool clear = clearShot(drones[i].position, target);
        detour_[i] = clear ? std::max(0.0f, detour_[i] - dt) : std::min(detour_[i] + dt, 8.0f);
        const float a = (i == lead ? aLead : aWing) + 0.35f * detour_[i] * (i == lead ? 1.0f : -1.0f);
        const Vec3 g = target + Vec3{std::cos(a), 0.0f, std::sin(a)} * kSquadStandOff;
        const float alt = kSquadEngageAlt + 0.6f * detour_[i];
        ctl_[i].flyTo(dt, {g.x, terrainHeight(g.x, g.z) + alt, g.z}, 3.6f, drones[i]);
        ctl_[i].faceTowards(dt, target, 70.0f, drones[i]);
        ctl_[i].aimGimbalAt(dt, target, drones[i]);
        drones[i].mode = DroneMode::Track;
        drones[i].targetEstimate = target;
        drones[i].hasEstimate = true;
    }
}

void Squad::planStrike(float time, const Track& t, int lead, int wing, const bool manual[kCount], const int ready[kCount],
                       std::vector<GameEvent>& events) {
    const bool wingOk = wing >= 0 && !manual[wing] && lock[wing] >= kSquadWingLock && ready[wing] > 0 &&
                        clearShot(drones[wing].position, t.position());
    const bool leadOk = lock[lead] >= kSquadLeadLock && ready[lead] > 0 && clearShot(drones[lead].position, t.position());
    const float wait = wing >= 0 && !manual[wing] && ready[wing] > 0 ? kSquadWingWaitFree : kSquadWingWait;
    const bool solo = !wingOk && lock[lead] >= kSquadSoloLock && (time - strike_.started) > wait;
    if (!leadOk || (!wingOk && !solo)) return;
    const Vec3 now = t.position();
    if (hasKeepOut_ && now.x > keepOut_.min.x && now.x < keepOut_.max.x && now.z > keepOut_.min.z && now.z < keepOut_.max.z)
        return;  // never strike next to the camp's own bunker entrance
    // Time on target: predict each round's flight time to the target's position at arrival (two iterations).
    const int who[2] = {lead, wingOk ? wing : -1};
    float T[2] = {0.0f, 0.0f};
    Vec3 aim = now;
    for (int it = 0; it < 2; ++it) {
        float tmax = 0.0f;
        for (int k = 0; k < 2; ++k) {
            if (who[k] < 0) continue;
            T[k] = roundFlightTime(length(aim - drones[who[k]].position));
            tmax = std::max(tmax, T[k]);
        }
        aim = now + t.velocity() * tmax;
    }
    strike_.phase = StrikePhase::Planned;
    strike_.combined = wingOk;
    for (int k = 0; k < 2; ++k) {
        strike_.launched[k] = false;
        strike_.launchAt[k] = -1.0f;
    }
    const float tFar = std::max(T[0], wingOk ? T[1] : 0.0f);
    for (int k = 0; k < 2; ++k) {
        if (who[k] < 0) continue;
        strike_.flightTime[who[k]] = T[k];
        strike_.launchAt[who[k]] = time + (tFar - T[k]);  // the farther one launches now, the nearer one waits
    }
    strike_.plannedSpread = 0.0f;
    GameEvent ev;
    ev.type = wingOk ? GameEventType::CombinedStrike : GameEventType::LockOn;
    ev.position = aim;
    ev.track = t.id;
    ev.uav = lead;
    ev.value = static_cast<int>(1000.0f * std::fabs((wingOk ? T[1] : T[0]) - T[0]));
    events.push_back(ev);
}

void Squad::update(float dt, float time, const SquadFrame& frame, const bool manual[kCount], const int ready[kCount],
                   std::vector<LaunchOrder>& orders, std::vector<GameEvent>& events) {
    lastTime_ = time;
    for (Ignore& z : ignore_) z.t -= dt;
    ignore_.erase(std::remove_if(ignore_.begin(), ignore_.end(), [](const Ignore& z) { return z.t <= 0.0f; }), ignore_.end());

    // ---- 1. Vision: blobs -> ground measurements (ray cast on the terrain) -> shared tracker ----
    std::vector<GroundMeasurement> meas;
    for (int i = 0; i < kCount; ++i) {
        if (!frame.valid[i]) continue;
        if (hasPads_ && !manual[i] && !visionActive(i)) continue;   // v9 M5: docked / taking off / landing: no picture
        camPos_[i] = transformPoint(frame.camera[i], {0, 0, 0});
        for (const Measurement2D& b : frame.blobs[i]) {
            Vec3 hit;
            if (!DroneController::imagePointToGround(b.u, b.v, frame.camera[i], hit, kSquadBlobHeight)) continue;
            if (ignored(hit)) continue;  // known wreck or friendly vehicle
            GroundMeasurement g;
            g.point = hit;
            g.rayOrigin = camPos_[i];
            g.rayDir = DroneController::pixelRay(b.u, b.v, frame.camera[i]);
            g.sigma = 0.12f + 0.015f * length(hit - camPos_[i]);
            g.sensor = i;
            g.area = b.area;
            meas.push_back(g);
        }
    }
    tracker_.predict(dt);
    tracker_.update(meas, time);
    for (const Track& t : tracker_.tracks()) {
        if (t.state != TrackState::Tentative && t.id > confirmedSeen_) {
            confirmedSeen_ = t.id;
            GameEvent ev;
            ev.type = GameEventType::EnemySpotted;
            ev.position = t.position();
            ev.track = t.id;
            events.push_back(ev);
        }
    }

    // ---- 2. Target selection (with hysteresis) ----
    const Track* target = tracker_.find(strike_.track);
    if (target && ignored(target->position())) target = nullptr;
    const Track* best = nullptr;
    for (const Track& t : tracker_.tracks()) {
        if (t.state == TrackState::Tentative || ignored(t.position())) continue;
        if (!best || threat(t) < threat(*best)) best = &t;
    }
    if (best && (!target || (best->id != target->id && threat(*best) < threat(*target) - 3.0f &&
                             strike_.phase != StrikePhase::Inbound))) {
        target = best;
    }
    if (!target) {
        if (strike_.phase != StrikePhase::Inbound) strike_ = StrikeState{-1, StrikePhase::None, false, time};
    } else if (target->id != strike_.track) {
        // Same rover under a new track id (re-acquired after cover): keep the locks and the strike state.
        const bool sameRover = time - lastKnownTime_ < 3.0f && length(flat(target->position() - lastKnown_)) < 2.0f;
        if (sameRover) {
            strike_.track = target->id;
            if (strike_.phase == StrikePhase::None) strike_.phase = StrikePhase::Tracking;
        } else {
            strike_ = StrikeState{};
            strike_.track = target->id;
            strike_.phase = StrikePhase::Tracking;
            strike_.started = time;
            lock[0] = lock[1] = 0.0f;
        }
    }
    strike_.cooldown = std::max(0.0f, strike_.cooldown - dt);

    // ---- 3. Roles, locks and flight ----
    // v9 M5: a UAV off duty (docked, taking off, returning, landing) is out of the squad like a hand-flown one.
    dutyTransitions(time, manual);
    bool away[kCount];
    for (int i = 0; i < kCount; ++i) away[i] = manual[i] || duty_[i] != UavDuty::Patrol;

    // ---- 3a. A second intruder far from the target: one UAV leaves the pair and deals with it alone ----
    const Track* side = nullptr;
    if (target && !away[0] && !away[1]) {
        side = tracker_.find(side_.track);
        if (side && (side->id == target->id || side->state == TrackState::Tentative || ignored(side->position()) ||
                     length(flat(side->position() - target->position())) < kSquadRejoinDist))
            side = nullptr;
        // a new second target only while the pair has not committed both rounds to the first
        const bool pairFree = strike_.phase == StrikePhase::None || strike_.phase == StrikePhase::Tracking;
        if (!side && side_.phase != StrikePhase::Inbound && pairFree)
            for (const Track& t : tracker_.tracks()) {
                if (t.state == TrackState::Tentative || t.id == target->id || ignored(t.position())) continue;
                if (length(flat(t.position() - target->position())) < kSquadSplitDist) continue;
                if (threat(t) > splitThreat()) continue;   // not urgent yet
                if (!side || threat(t) < threat(*side)) side = &t;
            }
    }
    if (!side) {
        if (side_.phase != StrikePhase::Inbound) {   // (a round in the air is followed to its arrival)
            side_ = StrikeState{};
            detached_ = -1;
            sideLock_ = 0.0f;
        }
    } else if (side->id != side_.track) {
        side_ = StrikeState{};
        side_.track = side->id;
        side_.phase = StrikePhase::Tracking;
        side_.started = time;
        sideLock_ = 0.0f;
        // the UAV nearer to the second intruder goes
        detached_ = length(flat(drones[0].position - side->position())) <= length(flat(drones[1].position - side->position())) ? 0 : 1;
    }
    if (detached_ >= 0 && away[detached_]) {   // recalled or taken over by the user
        side_ = StrikeState{};
        detached_ = -1;
        side = nullptr;
    }
    if (detached_ >= 0) away[detached_] = true;   // out of the pair: the other UAV leads alone
    int lead = -1, wing = -1;
    if (target) {
        for (int i = 0; i < kCount; ++i) {
            const bool seen = (target->seenNow >> i) & 1u;
            // Flicker-tolerant lock: time seen accumulates, a short dropout (foliage, partial occlusion) only
            // bleeds it slowly; a long one clears it.
            lock[i] = seen ? lock[i] + dt : std::max(0.0f, lock[i] - 0.4f * dt);
        }
        // Lead = the available UAV with the longer lock (ties: the closer one).
        for (int i = 0; i < kCount; ++i) {
            if (away[i]) continue;
            if (lead < 0) { lead = i; continue; }
            const float di = length(drones[i].position - target->position()), dl = length(drones[lead].position - target->position());
            if (lock[i] > lock[lead] + 0.2f || (std::fabs(lock[i] - lock[lead]) <= 0.2f && di < dl)) lead = i;
        }
        for (int i = 0; i < kCount; ++i)
            if (!away[i] && i != lead) wing = i;
        if (lead < 0) {  // both flown by the user: still track, no autonomous strike
            for (int i = 0; i < kCount; ++i) roles[i] = manual[i] ? SquadRole::Manual : SquadRole::Patrol;
        } else {
            engage(dt, *target, lead, wing, away);
            for (int i = 0; i < kCount; ++i)
                roles[i] = away[i] ? (manual[i] ? SquadRole::Manual : SquadRole::Patrol) : (i == lead ? SquadRole::Lead : SquadRole::Wing);
        }
    } else {
        lock[0] = lock[1] = 0.0f;
        const bool search = time - lastKnownTime_ < kSquadReacquireTime;
        for (int i = 0; i < kCount; ++i) {
            roles[i] = away[i] ? (manual[i] ? SquadRole::Manual : SquadRole::Patrol) : (search ? SquadRole::Search : SquadRole::Patrol);
            if (away[i]) continue;
            if (search) reacquire(dt, time, i);
            else patrol(dt, time, i);
        }
    }
    if (target) {
        lastKnown_ = target->position();
        lastKnownTime_ = time;
    }

    // ---- 4. Strike: plan (time on target), release, keep the laser spots on the predicted target ----
    if (target && lead >= 0 && strike_.phase == StrikePhase::Tracking && strike_.cooldown <= 0.0f)
        planStrike(time, *target, lead, wing, away, ready, events);
    if (strike_.phase == StrikePhase::Planned || strike_.phase == StrikePhase::Inbound) {
        bool allOut = true;
        for (int i = 0; i < kCount; ++i) {
            if (strike_.launchAt[i] < 0.0f) continue;
            if (!strike_.launched[i] && time >= strike_.launchAt[i] && ready[i] > 0) {
                const Vec3 aim = target ? target->position() + target->velocity() * strike_.flightTime[i] : designator_[i];
                designator_[i] = aim;
                orders.push_back({i, aim, 1 + i});
                strike_.launched[i] = true;
                strike_.launchedAt[i] = time;
                GameEvent ev;
                ev.type = GameEventType::RoundLaunched;
                ev.uav = i;
                ev.track = strike_.track;
                ev.position = drones[i].position;
                events.push_back(ev);
            }
            allOut = allOut && strike_.launched[i];
        }
        if (allOut && strike_.phase == StrikePhase::Planned) strike_.phase = StrikePhase::Inbound;
        // Laser spots: where the target will be when each round arrives.
        float lastArrival = 0.0f;
        for (int i = 0; i < kCount; ++i) {
            if (!strike_.launched[i]) continue;
            const float remaining = std::max(0.0f, strike_.flightTime[i] - (time - strike_.launchedAt[i]));
            if (target) designator_[i] = target->position() + target->velocity() * remaining;
            lastArrival = std::max(lastArrival, strike_.launchedAt[i] + strike_.flightTime[i]);
        }
        if (strike_.phase == StrikePhase::Inbound && time > lastArrival + 1.2f) {  // missed / target survived
            strike_.phase = StrikePhase::Tracking;
            strike_.cooldown = 2.0f;
            strike_.started = time;
        }
    }

    // ---- 4a. The detached UAV: its own lock and a strike of one round ----
    if (detached_ >= 0) {
        const int u = detached_;
        roles[u] = SquadRole::Solo;
        side_.cooldown = std::max(0.0f, side_.cooldown - dt);
        if (side) {
            bool others[kCount];
            for (int i = 0; i < kCount; ++i) others[i] = i != u;
            engage(dt, *side, u, -1, others);
            const bool seen = (side->seenNow >> u) & 1u;
            sideLock_ = seen ? sideLock_ + dt : std::max(0.0f, sideLock_ - 0.4f * dt);
            const Vec3 now = side->position();
            const bool keepOut = hasKeepOut_ && now.x > keepOut_.min.x && now.x < keepOut_.max.x && now.z > keepOut_.min.z && now.z < keepOut_.max.z;
            if (side_.phase == StrikePhase::Tracking && side_.cooldown <= 0.0f && sideLock_ >= kSquadSoloLock && ready[u] > 0 && !keepOut &&
                clearShot(drones[u].position, now)) {
                float T = 0.0f;
                Vec3 aim = now;
                for (int it = 0; it < 2; ++it) {
                    T = roundFlightTime(length(aim - drones[u].position));
                    aim = now + side->velocity() * T;
                }
                side_.phase = StrikePhase::Inbound;
                side_.combined = false;
                side_.launchAt[u] = side_.launchedAt[u] = time;
                side_.flightTime[u] = T;
                side_.launched[u] = true;
                designator_[u] = aim;
                orders.push_back({u, aim, 1 + u});
                GameEvent ev;
                ev.type = GameEventType::LockOn;
                ev.position = aim;
                ev.track = side->id;
                ev.uav = u;
                events.push_back(ev);
                ev.type = GameEventType::RoundLaunched;
                ev.position = drones[u].position;
                events.push_back(ev);
            }
        }
        if (side_.phase == StrikePhase::Inbound) {
            const float remaining = std::max(0.0f, side_.flightTime[u] - (time - side_.launchedAt[u]));
            if (side) designator_[u] = side->position() + side->velocity() * remaining;
            if (time > side_.launchedAt[u] + side_.flightTime[u] + 1.2f) {   // missed / target survived (or it is gone)
                side_.phase = StrikePhase::Tracking;
                side_.launched[u] = false;
                side_.cooldown = 2.0f;
            }
        }
    }

    // ---- 5. v9 M5 duty: rotor spool, take-off, return, landing, docked on the pad ----
    for (int i = 0; i < kCount; ++i) {
        if (manual[i] || duty_[i] == UavDuty::Patrol) spool_[i] = std::min(1.0f, spool_[i] + dt / kRotorSpoolUp);
        else dutyFlight(dt, i);
    }
}

void Squad::onKill(const Vec3& where) {
    ignoreNear(where, kWreckIgnoreTime);
    if (length(flat(lastKnown_ - where)) < 3.0f) lastKnownTime_ = -1e9f;  // target dead: back to patrol
    const Track* t = tracker_.find(strike_.track);
    if (t && length(flat(t->position() - where)) < 3.0f) strike_ = StrikeState{};
    const Track* s = tracker_.find(side_.track);
    if (s && length(flat(s->position() - where)) < 3.0f) {   // the second intruder is destroyed: the pair re-forms
        side_ = StrikeState{};
        detached_ = -1;
        sideLock_ = 0.0f;
    }
    std::vector<int> drop;
    for (const Track& tr : tracker_.tracks())
        if (length(flat(tr.position() - where)) < 2.5f) drop.push_back(tr.id);
    for (int id : drop) tracker_.remove(id);
}

}  // namespace ts
