#include "ai/Squad.h"

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

float roundFlightTime(float d) {
    const float rampDist = 0.5f * (8.0f + 60.0f) * 0.4f;  // 13.6 m while accelerating
    float t;
    if (d <= rampDist) t = (-8.0f + std::sqrt(64.0f + 260.0f * d)) / 130.0f;  // d = 8 t + 65 t^2
    else t = 0.4f + (d - rampDist) / 60.0f;
    return t + 0.08f;
}

Squad::Squad() {
    drones[0].position = {0.0f, kSquadPatrolAlt, 12.5f};
    drones[1].position = {0.0f, kSquadPatrolAlt, -12.5f};
    drones[1].yawDeg = 0.0f;
}

void Squad::setObstacles(const std::vector<Box>& b) {
    for (DroneController& c : ctl_) c.setObstacles(b);
}

bool Squad::ignored(const Vec3& p) const {
    for (const Ignore& z : ignore_)
        if (length(flat(p - z.p)) < kWreckIgnoreRadius) return true;
    for (const Vec3& f : friendly_)
        if (length(flat(p - f)) < kFriendlyRadius) return true;
    return false;
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

bool Squad::designatorValid(int uav) const {
    return strike_.track >= 0 && (strike_.phase == StrikePhase::Planned || strike_.phase == StrikePhase::Inbound) &&
           strike_.launched[uav];
}

void Squad::patrol(float dt, float time, int i) {
    // Breathing orbit, the two UAVs 180 degrees apart and in opposite radius phase: one covers the inner ring
    // while the other covers the outer ring, so together they sweep the whole compound.
    DroneState& s = drones[i];
    const float phase = i == 0 ? 0.0f : PI;
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
        const float a = i == lead ? aLead : aWing;
        const Vec3 g = target + Vec3{std::cos(a), 0.0f, std::sin(a)} * kSquadStandOff;
        ctl_[i].flyTo(dt, {g.x, terrainHeight(g.x, g.z) + kSquadEngageAlt, g.z}, 3.6f, drones[i]);
        ctl_[i].faceTowards(dt, target, 70.0f, drones[i]);
        ctl_[i].aimGimbalAt(dt, target, drones[i]);
        drones[i].mode = DroneMode::Track;
        drones[i].targetEstimate = target;
        drones[i].hasEstimate = true;
    }
}

void Squad::planStrike(float time, const Track& t, int lead, int wing, const bool manual[kCount], const int ready[kCount],
                       std::vector<GameEvent>& events) {
    const bool wingOk = wing >= 0 && !manual[wing] && lock[wing] >= kSquadWingLock && ready[wing] > 0;
    const bool leadOk = lock[lead] >= kSquadLeadLock && ready[lead] > 0;
    const bool solo = !wingOk && lock[lead] >= kSquadSoloLock && (time - strike_.started) > kSquadWingWait;
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
    for (Ignore& z : ignore_) z.t -= dt;
    ignore_.erase(std::remove_if(ignore_.begin(), ignore_.end(), [](const Ignore& z) { return z.t <= 0.0f; }), ignore_.end());

    // ---- 1. Vision: blobs -> ground measurements (ray cast on the terrain) -> shared tracker ----
    std::vector<GroundMeasurement> meas;
    for (int i = 0; i < kCount; ++i) {
        if (!frame.valid[i]) continue;
        camPos_[i] = transformPoint(frame.camera[i], {0, 0, 0});
        for (const Measurement2D& b : frame.blobs[i]) {
            Vec3 hit;
            if (!DroneController::imagePointToGround(b.u, b.v, frame.camera[i], hit)) continue;
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
        strike_ = StrikeState{};
        strike_.track = target->id;
        strike_.phase = StrikePhase::Tracking;
        strike_.started = time;
        lock[0] = lock[1] = 0.0f;
    }
    strike_.cooldown = std::max(0.0f, strike_.cooldown - dt);

    // ---- 3. Roles, locks and flight ----
    int lead = -1, wing = -1;
    if (target) {
        for (int i = 0; i < kCount; ++i) {
            const bool seen = (target->seenNow >> i) & 1u;
            lock[i] = seen ? lock[i] + dt : std::max(0.0f, lock[i] - 1.5f * dt);
        }
        // Lead = the available UAV with the longer lock (ties: the closer one).
        for (int i = 0; i < kCount; ++i) {
            if (manual[i]) continue;
            if (lead < 0) { lead = i; continue; }
            const float di = length(drones[i].position - target->position()), dl = length(drones[lead].position - target->position());
            if (lock[i] > lock[lead] + 0.2f || (std::fabs(lock[i] - lock[lead]) <= 0.2f && di < dl)) lead = i;
        }
        for (int i = 0; i < kCount; ++i)
            if (!manual[i] && i != lead) wing = i;
        if (lead < 0) {  // both flown by the user: still track, no autonomous strike
            for (int i = 0; i < kCount; ++i) roles[i] = SquadRole::Manual;
        } else {
            engage(dt, *target, lead, wing, manual);
            for (int i = 0; i < kCount; ++i)
                roles[i] = manual[i] ? SquadRole::Manual : (i == lead ? SquadRole::Lead : SquadRole::Wing);
        }
    } else {
        lock[0] = lock[1] = 0.0f;
        for (int i = 0; i < kCount; ++i) {
            roles[i] = manual[i] ? SquadRole::Manual : SquadRole::Patrol;
            if (!manual[i]) patrol(dt, time, i);
        }
    }

    // ---- 4. Strike: plan (time on target), release, keep the laser spots on the predicted target ----
    if (target && lead >= 0 && strike_.phase == StrikePhase::Tracking && strike_.cooldown <= 0.0f)
        planStrike(time, *target, lead, wing, manual, ready, events);
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
}

void Squad::onKill(const Vec3& where) {
    ignoreNear(where, kWreckIgnoreTime);
    const Track* t = tracker_.find(strike_.track);
    if (t && length(flat(t->position() - where)) < 3.0f) strike_ = StrikeState{};
    std::vector<int> drop;
    for (const Track& tr : tracker_.tracks())
        if (length(flat(tr.position() - where)) < 2.5f) drop.push_back(tr.id);
    for (int id : drop) tracker_.remove(id);
}

}  // namespace ts
