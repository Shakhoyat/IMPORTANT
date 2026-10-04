#include "objects/AaGun.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "objects/Builder.h"
#include "objects/BaseLayout.h"
#include "objects/PropKit.h"
#include "objects/Terrain.h"
#include "scene/Combat.h"

namespace ts {

namespace {
constexpr float kCradleY = 1.25f, kCradleZ = 0.2f;     // trunnion in the mount frame
constexpr float kClusterZ = 1.15f, kBarrelLen = 2.0f;   // cluster origin in the cradle frame, barrel length
constexpr float kBarrelRadius = 0.14f;

float wrapPi(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}
Mat4 worldOf(const SceneNode* n) {
    Mat4 m = n->local.matrix();
    for (const SceneNode* p = n->parent; p; p = p->parent) m = p->local.matrix() * m;
    return m;
}
}  // namespace

AaGun::AaGun() { buildRig(); }

void AaGun::buildRig() {
    using P = PrimitiveType;
    Builder b("AA Gatling Gun", "AAG", ShadingModel::Phong);
    root_ = b.root("AA Gatling Gun", {layout::kAaGunSite.x, layout::kAaGunSite.y, layout::kAaGunSite.z},
                   "User-controllable anti-aircraft Gatling gun with radar dome");
    root_->animation = "Static pedestal; mount yaw / cradle pitch / barrel spin below";
    SceneNode* r = root_.get();

    const Material paint = makeMaterial({0.26f, 0.29f, 0.19f}, 0.25f, Surface::Camo, 0.55f, 0.3f);
    const Material steel = makeMaterial({0.2f, 0.205f, 0.2f}, 0.3f, Surface::SteelPlate, 0.5f, 0.7f);
    const Material gunMetal = makeMaterial({0.16f, 0.165f, 0.17f}, 0.35f, Surface::Metal, 0.42f, 0.85f);
    const Material concrete = makeMaterial({0.16f, 0.16f, 0.15f}, 0.2f, Surface::Concrete, 0.9f, 0.0f);
    const Material dome = makeMaterial({0.7f, 0.72f, 0.74f}, 0.22f, Surface::Paint, 0.35f, 0.0f);
    const Material glass = makeMaterial({0.1f, 0.2f, 0.24f}, 0.2f, Surface::Glass, 0.05f, 0.0f, 0.04f);
    const Material brass = makeMaterial({0.5f, 0.38f, 0.1f}, 0.5f, Surface::Metal, 0.4f, 0.9f);
    auto camoPart = [&](SceneNode* n) { camoParts_.push_back(n); camoBase_.push_back(n->material.baseColor); return n; };

    b.part(r, "Base plate", P::Cylinder, {2.2f, 2.2f, 0.15f}, {90, 0, 0}, {0, 0.075f, 0}, concrete, "Concrete / steel foundation plate");
    b.part(r, "Pedestal column", P::Cylinder, {0.9f, 0.9f, 0.85f}, {90, 0, 0}, {0, 0.575f, 0}, steel, "Pedestal column");
    b.part(r, "Pedestal collar", P::Cylinder, {1.2f, 1.2f, 0.12f}, {90, 0, 0}, {0, 0.95f, 0}, steel, "Bearing collar");
    for (int i = 0; i < 4; ++i) {
        const float a = radians(90.0f * i + 45.0f);
        b.part(r, "Pedestal brace", P::Cube, {0.12f, 0.6f, 0.12f}, {-14, degrees(a), 0}, {0.62f * std::sin(a), 0.4f, 0.62f * std::cos(a)}, steel, "Diagonal brace");
    }

    mount_ = b.group(r, "Mount yaw", {0, 1.05f, 0}, {0, 0, 0}, "Rotating mount: 360 deg azimuth");
    mount_->animation = "Yaw slews at 60 deg/s (arrows / mouse); idle: slow search sweep";
    SceneNode* m = mount_;
    camoPart(b.part(m, "Turntable", P::Cylinder, {1.5f, 1.5f, 0.2f}, {90, 0, 0}, {0, 0.1f, 0}, paint, "Turntable ring"));
    camoPart(b.part(m, "Mount body", P::Cube, {1.4f, 0.5f, 1.4f}, {0, 0, 0}, {0, 0.45f, 0}, paint, "Mount body"));
    camoPart(b.part(m, "Cheek L", P::Cube, {0.15f, 0.9f, 1.1f}, {0, 0, 0}, {0.55f, 0.95f, 0.1f}, paint, "Cradle support arm"));
    camoPart(b.part(m, "Cheek R", P::Cube, {0.15f, 0.9f, 1.1f}, {0, 0, 0}, {-0.55f, 0.95f, 0.1f}, paint, "Cradle support arm"));
    b.part(m, "Ammo drum", P::Cylinder, {0.9f, 0.9f, 0.7f}, {90, 0, 0}, {0, 0.95f, -0.85f}, steel, "1550-round ammunition drum");
    b.part(m, "Ammo drum lid", P::Cylinder, {0.95f, 0.95f, 0.08f}, {90, 0, 0}, {0, 1.3f, -0.85f}, gunMetal, "Drum lid");
    b.part(m, "Radome mast", P::Cylinder, {0.4f, 0.4f, 0.5f}, {90, 0, 0}, {0, 1.6f, -0.85f}, steel, "Radome support");
    b.part(m, "Radome", P::Sphere, {0.8f, 0.8f, 0.8f}, {0, 0, 0}, {0, 2.05f, -0.85f}, dome, "Search / track radar dome (white)");
    b.part(m, "Antenna mast", P::Cylinder, {0.05f, 0.05f, 0.3f}, {90, 0, 0}, {0, 2.55f, -0.85f}, steel, "Search antenna mast");
    search_ = b.group(m, "Search antenna", {0, 2.72f, -0.85f}, {0, 0, 0}, "Rotating search antenna");
    search_->animation = "Rotates 1 rev/s (3 rev/s while the gun is possessed)";
    b.part(search_, "Search antenna plate", P::Cube, {0.55f, 0.25f, 0.03f}, {0, 0, 0}, {0.1f, 0, 0}, steel, "Antenna reflector");

    cradle_ = b.group(m, "Cradle pitch", {0, kCradleY, kCradleZ}, {0, 0, 0}, "Gun cradle: elevation -5..+85 deg");
    cradle_->animation = "Pitch slews at 60 deg/s";
    SceneNode* c = cradle_;
    camoPart(b.part(c, "Gun housing", P::Cube, {0.6f, 0.55f, 1.3f}, {0, 0, 0}, {0, 0, 0.2f}, paint, "Gun housing / gearbox"));
    b.part(c, "Trunnion L", P::Cylinder, {0.25f, 0.25f, 0.2f}, {0, 90, 0}, {0.5f, 0, 0}, gunMetal, "Trunnion bearing");
    b.part(c, "Trunnion R", P::Cylinder, {0.25f, 0.25f, 0.2f}, {0, 90, 0}, {-0.5f, 0, 0}, gunMetal, "Trunnion bearing");
    b.part(c, "Front shroud", P::Cylinder, {0.5f, 0.5f, 0.4f}, {0, 0, 0}, {0, 0, 0.95f}, steel, "Fixed barrel shroud");
    b.part(c, "Feed chute", P::Cube, {0.25f, 0.12f, 0.9f}, {0, 0, 0}, {0, -0.3f, -0.5f}, gunMetal, "Ammunition feed chute");
    b.part(c, "Ejection port", P::Cube, {0.1f, 0.12f, 0.2f}, {0, 0, 0}, {0.32f, 0.12f, 0.1f}, gunMetal, "Casing ejection port");
    b.part(c, "EO sensor pod", P::Cylinder, {0.2f, 0.2f, 0.18f}, {0, 0, 0}, {-0.42f, 0.35f, 0.7f}, steel, "Electro-optical sensor pod");
    b.part(c, "EO sensor lens", P::Sphere, {0.14f, 0.14f, 0.08f}, {0, 0, 0}, {-0.42f, 0.35f, 0.8f}, glass, "Sensor lens");

    cluster_ = b.group(c, "Barrel cluster", {0, 0, kClusterZ}, {0, 0, 0}, "Six revolving barrels");
    cluster_->animation = "Spins about its axis; spool-up 0.5 s, spool-down 1.5 s, 5 rev/s = 30 rounds/s";
    for (int i = 0; i < kBarrels; ++i) {
        const float a = radians(60.0f * i);
        barrels_.push_back(b.part(cluster_, "Gatling barrel", P::Cylinder, {0.07f, 0.07f, kBarrelLen}, {0, 0, 0},
                                  {kBarrelRadius * std::sin(a), kBarrelRadius * std::cos(a), 0.5f * kBarrelLen}, gunMetal, "Rotating barrel"));
    }
    b.part(cluster_, "Cluster shaft", P::Cylinder, {0.1f, 0.1f, kBarrelLen}, {0, 0, 0}, {0, 0, 0.5f * kBarrelLen}, steel, "Central shaft");
    b.part(cluster_, "Barrel clamp rear", P::Cylinder, {0.4f, 0.4f, 0.06f}, {0, 0, 0}, {0, 0, 0.55f}, steel, "Barrel clamp ring");
    b.part(cluster_, "Barrel clamp mid", P::Cylinder, {0.4f, 0.4f, 0.06f}, {0, 0, 0}, {0, 0, 1.45f}, steel, "Barrel clamp ring");
    b.part(cluster_, "Barrel clamp muzzle", P::Cylinder, {0.42f, 0.42f, 0.06f}, {0, 0, 0}, {0, 0, kBarrelLen - 0.03f}, steel, "Muzzle clamp ring");
    b.part(cluster_, "Rotation marker", P::Cube, {0.05f, 0.05f, 0.25f}, {0, 0, 0}, {0.21f, 0, 0.55f}, brass, "Marker bolt that makes the spin visible");

    flash_ = b.part(c, "Muzzle flash", P::Sphere, {0.5f, 0.5f, 0.9f}, {0, 0, 0}, {0, 0, kClusterZ + kBarrelLen + 0.25f},
                    asLight(makeMaterial({1.0f, 0.7f, 0.3f}, 1.0f, Surface::Plain, 0.5f, 0.0f, 3.0f), 0.0f, 18.0f), "Muzzle flash");
    flash_->animation = "Flickers while firing";
    flash_->local.scale = {0.0001f, 0.0001f, 0.0001f};
    flash_->explodable = false;
    flash_->obstacle = false;

    for (int i = 0; i < kCasingPool; ++i) {
        SceneNode* n = b.part(r, "Shell casing", P::Cylinder, {0.03f, 0.03f, 0.1f}, {0, 0, 0}, {0, -50, 0}, brass, "Ejected brass casing");
        n->animation = "Ballistic arc out of the ejection port, 0.7 s life";
        n->explodable = false;
        n->obstacle = false;
        n->local.scale = {0.0001f, 0.0001f, 0.0001f};
        casingNodes_.push_back(n);
    }
    casings_.assign(kCasingPool, Casing{});

    outward_ = std::atan2(layout::kAaGunSite.x, layout::kAaGunSite.z);
    yaw_ = yawCmd_ = outward_;
    mount_->local.rotationDeg = {0, degrees(yaw_), 0};
    cradle_->local.rotationDeg = {-pitch_, 0, 0};
    mountWorld_ = worldOf(mount_);
    cradleWorld_ = worldOf(cradle_);
}

Vec3 AaGun::muzzlePosition() const { return transformPoint(cradleWorld_, {0.0f, kBarrelRadius, kClusterZ + kBarrelLen}); }
Vec3 AaGun::muzzleDirection() const { return normalize(transformDirection(cradleWorld_, {0.0f, 0.0f, 1.0f})); }

void AaGun::spawnCasing() {
    Casing& cs = casings_[casingNext_];
    SceneNode* node = casingNodes_[casingNext_];
    casingNext_ = (casingNext_ + 1) % kCasingPool;
    const Vec3 port = transformPoint(cradleWorld_, {0.4f, 0.12f, 0.1f});
    const Vec3 right = normalize(transformDirection(cradleWorld_, {1.0f, 0.0f, 0.0f}));
    const Vec3 up = normalize(transformDirection(cradleWorld_, {0.0f, 1.0f, 0.0f}));
    const Vec3 fwd = muzzleDirection();
    cs.pos = port;
    cs.vel = right * (2.0f + 1.5f * rnd()) + up * (1.5f + 1.5f * rnd()) - fwd * (0.5f + rnd());
    cs.age = 0.0f;
    cs.spin = 600.0f + 600.0f * rnd();
    node->local.scale = {0.03f, 0.03f, 0.1f};
}

void AaGun::update(const WorldContext& w, const ControlInput* in) {
    const float dt = clampf(w.dt, 0.0f, 0.05f);
    time_ = w.time;
    possessed_ = in != nullptr;

    // ---- aiming
    if (in) {
        yawCmd_ -= radians(in->aimYaw * kSlewDeg * dt + in->aimYawDelta);
        pitchCmd_ += in->aimPitch * kSlewDeg * dt + in->aimPitchDelta;
    } else {
        float rel = wrapPi(yawCmd_ - outward_);
        if (rel > radians(110.0f)) sweepDir_ = -1.0f;
        if (rel < -radians(110.0f)) sweepDir_ = 1.0f;
        yawCmd_ += sweepDir_ * radians(15.0f) * dt;
        pitchCmd_ = 25.0f + 10.0f * std::sin(0.3f * time_);
    }
    pitchCmd_ = clampf(pitchCmd_, kPitchMinDeg, kPitchMaxDeg);
    pitchCmd_ = clampf(pitchCmd_, pitch_ - 30.0f, pitch_ + 30.0f);
    float diff = clampf(wrapPi(yawCmd_ - yaw_), -radians(90.0f), radians(90.0f));
    yawCmd_ = yaw_ + diff;
    yaw_ = wrapPi(yaw_ + clampf(diff, -radians(kSlewDeg) * dt, radians(kSlewDeg) * dt));
    pitch_ = clampf(pitch_ + clampf(pitchCmd_ - pitch_, -kSlewDeg * dt, kSlewDeg * dt), kPitchMinDeg, kPitchMaxDeg);

    // ---- barrel spin and firing
    const bool trigger = in && in->fire && rounds_ > 0;
    if (trigger) spin_ = std::min(kMaxSpin, spin_ + kMaxSpin / kSpoolUp * dt);
    else spin_ = std::max(0.0f, spin_ - kMaxSpin / kSpoolDown * dt);
    spinAngle_ = std::fmod(spinAngle_ + spin_ * 360.0f * dt, 360.0f);
    firing_ = false;
    mountWorld_ = worldOf(mount_);   // aiming is applied below, so refresh after setting the angles
    mount_->local.rotationDeg = {0.0f, degrees(yaw_), 1.0f * shake_ * std::sin(47.0f * time_)};
    cradle_->local.rotationDeg = {-pitch_ + 0.8f * shake_ * std::sin(53.0f * time_), 0.0f, 0.0f};
    cradleWorld_ = worldOf(cradle_);
    mountWorld_ = worldOf(mount_);

    if (trigger && spin_ >= kMaxSpin * 0.999f) {
        firing_ = true;
        rofAcc_ += kRoundsPerSecond * dt;
        const int n = std::min(static_cast<int>(rofAcc_), rounds_);
        rofAcc_ -= std::floor(rofAcc_);
        const Vec3 m = muzzlePosition(), d0 = muzzleDirection();
        const Vec3 right = normalize(transformDirection(cradleWorld_, {1.0f, 0.0f, 0.0f}));
        const Vec3 up = normalize(transformDirection(cradleWorld_, {0.0f, 1.0f, 0.0f}));
        for (int i = 0; i < n; ++i) {
            const float ang = 2.0f * PI * rnd(), rad = kDispersion * std::sqrt(rnd());
            const Vec3 d = normalize(d0 + right * (rad * std::cos(ang)) + up * (rad * std::sin(ang)));
            if (w.combat) w.combat->fireTracer(m, d, kMuzzleSpeed, ownerId);
            ++fired_;
            --rounds_;
            spawnCasing();
        }
        if (n > 0) {
            flashT_ = 0.0f;
            heat_ = std::min(1.0f, heat_ + 0.02f * n);
            if (w.combat) w.combat->muzzleFlash(m, d0, 0.7f);
        }
    } else {
        rofAcc_ = 0.0f;
    }
    flashT_ += dt;
    heat_ = std::max(0.0f, heat_ - dt / 30.0f);

    // ---- node transforms
    cluster_->local.rotationDeg = {0.0f, 0.0f, spinAngle_};
    searchAngle_ = std::fmod(searchAngle_ + (possessed_ ? 1080.0f : 360.0f) * dt, 360.0f);
    search_->local.rotationDeg = {0.0f, searchAngle_, 0.0f};
    const bool flash = flashT_ < 0.05f;
    const float fs = flash ? (0.7f + 0.5f * rnd()) : 0.0001f;
    flash_->local.scale = {0.5f * fs, 0.5f * fs, 0.9f * fs};
    flash_->material.lightIntensity = flash ? 30.0f : 0.0f;
    for (SceneNode* bn : barrels_) bn->material.heat = 0.3f + 0.6f * heat_;
    shake_ = std::max(0.0f, shake_ - 2.0f * dt);
    for (size_t i = 0; i < camoParts_.size(); ++i) camoParts_[i]->material.baseColor = camoBase_[i] * (1.0f - 0.6f * scorch_);

    // ---- casings (ballistic, bounce once, vanish after 0.7 s)
    for (int i = 0; i < kCasingPool; ++i) {
        Casing& cs = casings_[i];
        SceneNode* node = casingNodes_[i];
        if (cs.age > 0.7f) { node->local.scale = {0.0001f, 0.0001f, 0.0001f}; continue; }
        cs.age += dt;
        cs.vel.y -= 9.81f * dt;
        cs.pos = cs.pos + cs.vel * dt;
        const float ground = terrainHeight(cs.pos.x, cs.pos.z);
        if (cs.pos.y < ground + 0.02f) { cs.pos.y = ground + 0.02f; cs.vel = {cs.vel.x * 0.4f, -cs.vel.y * 0.3f, cs.vel.z * 0.4f}; }
        node->local.translation = cs.pos - root_->local.translation;
        node->local.rotationDeg = {cs.spin * cs.age, 30.0f * i, 0.0f};
    }
    mountWorld_ = worldOf(mount_);
}

CameraHint AaGun::camera(bool firstPerson) const {
    CameraHint h;
    const Vec3 d = muzzleDirection();
    if (firstPerson) {
        h.eye = transformPoint(cradleWorld_, {0.0f, 0.38f, 0.5f});
        h.target = h.eye + d * 60.0f;
        h.fovDeg = 30.0f;
        h.sight = true;
    } else {
        const Vec3 dh{std::sin(yaw_), 0.0f, std::cos(yaw_)};
        const Vec3 p = transformPoint(cradleWorld_, {0, 0, 0});
        h.eye = root_->local.translation - dh * 5.0f + Vec3{0.0f, 3.0f, 0.0f};
        h.target = p + d * 20.0f;
        h.fovDeg = 55.0f;
    }
    return h;
}

std::string AaGun::status() const {
    char buf[160];
    const int az = (static_cast<int>(std::lround(degrees(yaw_))) % 360 + 360) % 360;
    const char* st = firing_ ? "FIRING" : (spin_ > 0.01f ? "SPOOLING" : "READY");
    std::snprintf(buf, sizeof(buf), "AZ %03d  EL %+03d  BARRELS %d RPM  %s  ROUNDS %d", az,
                  static_cast<int>(std::lround(pitch_)), static_cast<int>(std::lround(spin_ * 60.0f)), st, rounds_);
    return buf;
}

bool AaGun::onBlast(const Vec3& point, float power) {
    const float d = length(point - root_->local.translation);
    if (d > 10.0f + 6.0f * power) return false;
    const float k = power / (1.0f + 0.08f * d * d);
    shake_ = std::min(1.0f, shake_ + 1.5f * k);
    scorch_ = std::min(0.5f, scorch_ + 0.15f * k);
    return true;
}

std::unique_ptr<Controllable> makeAaGun() { return std::make_unique<AaGun>(); }

}  // namespace ts
