#include "scene/DayCycle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "objects/Builder.h"
#include "objects/Environment.h"
#include "objects/PropKit.h"
#include "scene/Controllable.h"

namespace ts {

namespace {

float smoothstepf(float e0, float e1, float x) {
    const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float wrapHour(float h) {
    float w = std::fmod(h, 24.0f);
    if (w < 0.0f) w += 24.0f;
    return w >= 24.0f ? 0.0f : w;
}

// Radiance-like quantities blend in log space (a twilight sky is the geometric mean of night and day, not half
// the day). w = 0 returns 'a' unchanged (the night preset stays bit-exact).
Vec3 mixLog(const Vec3& a, const Vec3& b, float w) {
    if (w <= 0.0f) return a;
    if (w >= 1.0f) return b;
    auto f = [w](float x, float y) { return std::exp2(lerpf(std::log2(x), std::log2(y), w)); };
    return {f(a.x, b.x), f(a.y, b.y), f(a.z, b.z)};
}

// Day preset (linear HDR units: the sun's irradiance is 3.2, a white sunlit wall reflects 3.2).
const Vec3 kDaySkyZenith{0.40f, 0.50f, 0.68f};     // hemisphere irradiance from the blue sky
const Vec3 kDayGroundBounce{0.20f, 0.19f, 0.16f};  // sunlit ground seen from below
const Vec3 kDayFogColor{0.60f, 0.68f, 0.80f};      // distance haze = horizon sky
const Vec3 kDayFogAmbient{0.30f, 0.36f, 0.46f};    // sky light scattered by the volumetric haze
const Vec3 kDayAmbient{0.34f, 0.36f, 0.40f};       // display-referred (v1) views
const Vec3 kDaySkyHorizon{0.60f, 0.66f, 0.74f};    // v8: whiter, brighter sky band near the horizon (side ambient)
const Vec3 kTwilightWarm{1.0f, 0.62f, 0.40f};      // haze tint at sunrise / sunset
// v8: clear-day aerial perspective (was 0.0075 / 0.0032: a grey veil over everything beyond ~40 m)
constexpr float kDayFogDensity = 0.0042f, kDayVolFogDensity = 0.0013f;

GameEventType eventFor(DayPhase p) {
    switch (p) {
        case DayPhase::Night: return GameEventType::NightFalls;
        case DayPhase::Dawn: return GameEventType::Dawn;
        case DayPhase::Day: return GameEventType::DayBreak;
        case DayPhase::Dusk: return GameEventType::Dusk;
    }
    return GameEventType::NightFalls;
}

DayPhase nextPhase(DayPhase p) {
    switch (p) {
        case DayPhase::Night: return DayPhase::Dawn;
        case DayPhase::Dawn: return DayPhase::Day;
        case DayPhase::Day: return DayPhase::Dusk;
        case DayPhase::Dusk: return DayPhase::Night;
    }
    return DayPhase::Night;
}

// Unit vector towards a body at elevation e, azimuth az (from north towards east), degrees.
Vec3 fromAngles(float elevDeg, float azDeg) {
    const float e = radians(elevDeg), a = radians(azDeg);
    return {std::sin(a) * std::cos(e), std::sin(e), std::cos(a) * std::cos(e)};
}

// ---- CPU mirror of the GLSL noise (ShaderSources.cpp kPalette: latticeHash, valueNoise, fbm) ----
// Integer lattice hash (GLSL latticeHash / mixBits): bit-exact with the shader for any cell index.
uint32_t mixBits(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float latticeHash(float ix, float iy) {
    const uint32_t ux = static_cast<uint32_t>(static_cast<int32_t>(ix)), uy = static_cast<uint32_t>(static_cast<int32_t>(iy));
    return static_cast<float>(mixBits(ux + mixBits(uy + 0x9e3779b9u)) >> 8) * (1.0f / 16777216.0f);
}

float valueNoise(float px, float py) {
    const float ix = std::floor(px), iy = std::floor(py);
    float fx = px - ix, fy = py - iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = latticeHash(ix, iy), b = latticeHash(ix + 1.0f, iy), c = latticeHash(ix, iy + 1.0f),
                d = latticeHash(ix + 1.0f, iy + 1.0f);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}

float fbm(float px, float py) {
    float s = 0.0f, a = 0.5f;
    for (int i = 0; i < 4; ++i) {
        s += a * valueNoise(px, py);
        px = px * 2.03f + 17.1f;
        py = py * 2.03f + 9.7f;
        a *= 0.5f;
    }
    return s;
}

}  // namespace

// ------------------------------------------------------------------------------------------- solar model
Vec3 DayCycle::sunDirection(float hour, const DayCycleConfig& cfg) {
    const float H = radians(15.0f * (hour - 12.0f));
    const float phi = radians(cfg.latitudeDeg), del = radians(cfg.declinationDeg);
    const float up = std::sin(phi) * std::sin(del) + std::cos(phi) * std::cos(del) * std::cos(H);
    const float east = -std::cos(del) * std::sin(H);
    const float north = std::cos(phi) * std::sin(del) - std::sin(phi) * std::cos(del) * std::cos(H);
    return normalize(Vec3{east, up, north});
}

float DayCycle::sunElevationDeg(float hour, const DayCycleConfig& cfg) {
    return degrees(std::asin(clampf(sunDirection(hour, cfg).y, -1.0f, 1.0f)));
}

float DayCycle::hourAtElevation(float elevDeg, bool morning, const DayCycleConfig& cfg) {
    const float phi = radians(cfg.latitudeDeg), del = radians(cfg.declinationDeg);
    const float c = (std::sin(radians(elevDeg)) - std::sin(phi) * std::sin(del)) / (std::cos(phi) * std::cos(del));
    if (c < -1.0f || c > 1.0f) return -1.0f;
    const float h = degrees(std::acos(c)) / 15.0f;
    return morning ? 12.0f - h : 12.0f + h;
}

float DayCycle::airMass(float elevDeg) {
    const float e = std::max(elevDeg, -1.0f);
    return 1.0f / (std::sin(radians(e)) + 0.50572f * std::pow(e + 6.07995f, -1.6364f));
}

Vec3 DayCycle::sunTransmittance(float elevDeg) {
    const float m = airMass(elevDeg) - 1.0f;   // relative to the zenith path
    return {std::exp(-kSunOpticalDepth[0] * m), std::exp(-kSunOpticalDepth[1] * m), std::exp(-kSunOpticalDepth[2] * m)};
}

float DayCycle::cycleSeconds(const DayCycleConfig& cfg) {
    DayCycle c(0.0f, true, cfg);
    double seconds = 0.0;
    const int steps = 24000;
    const float dh = 24.0f / steps;
    for (int i = 0; i < steps; ++i) {
        c.hour_ = (i + 0.5f) * dh;
        seconds += dh / c.hoursPerSecond();
    }
    return static_cast<float>(seconds);
}

LightRig DayCycle::rigAt(float hour, const DayCycleConfig& cfg) {
    LightRig rig;
    rig.moon.direction = moonLightDirection();
    DayCycle(hour, false, cfg).applyTo(rig);
    return rig;
}

// ------------------------------------------------------------------------------------------- clock
DayCycle::DayCycle(float startHour, bool autoRun, DayCycleConfig cfg) : cfg_(cfg), hour_(wrapHour(startHour)), auto_(autoRun) {
    const float e = sunElevationDeg();
    phase_ = e < kNightBelowDeg ? DayPhase::Night
           : e > kDayAboveDeg  ? DayPhase::Day
                               : (hour_ < 12.0f ? DayPhase::Dawn : DayPhase::Dusk);
}

DayPhase DayCycle::bandFor(float hour) const {
    const float e = sunElevationDeg(hour, cfg_);
    const DayPhase twilight = hour < 12.0f ? DayPhase::Dawn : DayPhase::Dusk;
    const float h = kPhaseHysteresisDeg;
    switch (phase_) {
        case DayPhase::Night:
            if (e <= kNightBelowDeg + h) return DayPhase::Night;
            return e > kDayAboveDeg + h ? DayPhase::Day : twilight;
        case DayPhase::Day:
            if (e >= kDayAboveDeg - h) return DayPhase::Day;
            return e < kNightBelowDeg - h ? DayPhase::Night : twilight;
        default:
            if (e < kNightBelowDeg - h) return DayPhase::Night;
            if (e > kDayAboveDeg + h) return DayPhase::Day;
            return twilight;
    }
}

void DayCycle::moveTo(float hour, bool backwards) {
    hour_ = wrapHour(hour);
    const DayPhase p = bandFor(hour_);
    if (p == phase_) return;
    if (backwards) {
        pending_.push_back(eventFor(p));
    } else {   // forward through every phase in between (a fast scrub must not skip the dawn withdrawal)
        DayPhase q = phase_;
        for (int i = 0; i < 4 && q != p; ++i) {
            q = nextPhase(q);
            pending_.push_back(eventFor(q));
        }
    }
    phase_ = p;
}

void DayCycle::update(float dt, std::vector<GameEvent>& events, float time) {
    if (auto_ && dt > 0.0f) {
        // Sub-steps of at most 0.05 h so a long frame does not jump over a twilight band.
        float left = dt;
        for (int i = 0; i < 64 && left > 0.0f; ++i) {
            const float rate = hoursPerSecond();
            const float step = std::min(left, 0.05f / rate);
            moveTo(hour_ + rate * step, false);
            left -= step;
        }
    }
    for (GameEventType t : pending_) {
        GameEvent e;
        e.type = t;
        e.value = static_cast<int>(std::lround(hour_ * 100.0f));
        e.time = time;
        events.push_back(e);
    }
    pending_.clear();
}

void DayCycle::setAuto(bool on) { auto_ = on; }
void DayCycle::toggleAuto() { auto_ = !auto_; }
bool DayCycle::autoRun() const { return auto_; }

void DayCycle::setHour(float h) {
    float d = std::fmod(wrapHour(h) - hour_ + 36.0f, 24.0f) - 12.0f;   // signed shortest change, (-12, 12]
    moveTo(h, d < 0.0f);
}

void DayCycle::scrub(float d) { moveTo(hour_ + d, d < 0.0f); }

void DayCycle::jumpToNextPhase() {
    const float m = 2.0f * kPhaseHysteresisDeg;
    float target = hour_;
    switch (nextPhase(phase_)) {
        case DayPhase::Dawn: target = hourAtElevation(kNightBelowDeg + m, true, cfg_); break;
        case DayPhase::Day: target = hourAtElevation(kDayAboveDeg + m, true, cfg_); break;
        case DayPhase::Dusk: target = hourAtElevation(kDayAboveDeg - m, false, cfg_); break;
        case DayPhase::Night: target = hourAtElevation(kNightBelowDeg - m, false, cfg_); break;
    }
    if (target >= 0.0f) moveTo(target, false);
}

float DayCycle::hour() const { return hour_; }
DayPhase DayCycle::phase() const { return phase_; }

const char* DayCycle::phaseName() const {
    switch (phase_) {
        case DayPhase::Night: return "NIGHT";
        case DayPhase::Dawn: return "DAWN";
        case DayPhase::Day: return "DAY";
        case DayPhase::Dusk: return "DUSK";
    }
    return "?";
}

float DayCycle::sunElevationDeg() const { return sunElevationDeg(hour_, cfg_); }

float DayCycle::sunAzimuthDeg() const {
    const Vec3 s = towardSun();
    float a = degrees(std::atan2(s.x, s.z));
    return a < 0.0f ? a + 360.0f : a;
}

Vec3 DayCycle::towardSun() const { return sunDirection(hour_, cfg_); }

bool DayCycle::keyIsSun() const { return sunElevationDeg() > kSunKeyAboveDeg; }

Vec3 DayCycle::keyDirection() const {
    if (!keyIsSun()) return moonLightDirection();
    // Quantised (elevation, azimuth): the cached static shadow map is re-rendered once per quantum.
    const float q = std::max(cfg_.shadowQuantDeg, 0.01f);
    const float e = std::round(sunElevationDeg() / q) * q, a = std::round(sunAzimuthDeg() / q) * q;
    return fromAngles(std::max(e, q), a) * -1.0f;   // never exactly horizontal
}

float DayCycle::daylight() const { return smoothstepf(-8.0f, 6.0f, sunElevationDeg()); }
bool DayCycle::wavesAllowed() const { return phase_ == DayPhase::Night; }
bool DayCycle::beamsOn() const { return sunElevationDeg() < kBeamsBelowDeg; }

float DayCycle::hoursPerSecond() const {
    const float e = sunElevationDeg();
    float rate = 1.0f / (e > 0.0f ? cfg_.daySecPerHour : cfg_.nightSecPerHour);
    if (std::fabs(e) < 6.0f) rate /= std::max(cfg_.twilightSlow, 0.1f);
    return rate;
}

void DayCycle::applyTo(LightRig& L) const {
    const float e = sunElevationDeg();
    const Atmosphere night{};
    Atmosphere a;
    a.day = daylight();
    a.night = 1.0f - smoothstepf(-10.0f, 0.0f, e);
    a.solar = solarWeight(e);
    a.practicalScale = 1.0f - smoothstepf(-4.0f, 2.0f, e);
    a.beamsOn = beamsOn();
    a.keyIsSun = keyIsSun();
    if (a.night < 1.0f) a.sunDir = towardSun() * -1.0f;   // deep night: every field keeps its v5 default
    const Vec3 tr = sunTransmittance(e);
    a.sunColor = tr * (kSunIntensity * smoothstepf(-3.0f, 2.0f, e));
    a.twilight = smoothstepf(-8.0f, -2.0f, e) * (1.0f - smoothstepf(3.0f, 14.0f, e));
    if (a.day > 0.0f) {
        a.skyZenith = mixLog(night.skyZenith, kDaySkyZenith, a.day);
        a.groundBounce = mixLog(night.groundBounce, kDayGroundBounce, a.day);
        a.skyHorizon = mixLog(night.skyHorizon, kDaySkyHorizon, a.day);
        a.fogColor = mixLog(night.fogColor, kDayFogColor, a.day);
        a.fogAmbient = mixLog(night.fogAmbient, kDayFogAmbient, a.day);
        a.fogDensity = lerpf(night.fogDensity, kDayFogDensity, a.day);
        a.volFogDensity = lerpf(night.volFogDensity, kDayVolFogDensity, a.day);
        // Exposure key: reaches the day value once the sun is 10 deg up (twilight frames are shown a little brighter
        // than their mean luminance, dusk stays darker than day).
        a.exposureKeyLog2 = lerpf(night.exposureKeyLog2, kDayExposureKeyLog2, smoothstepf(-8.0f, 10.0f, e));
    }
    if (a.twilight > 0.0f) {
        const Vec3 tint = lerp(Vec3{1, 1, 1}, kTwilightWarm, a.twilight);
        a.fogColor = mul(a.fogColor, tint);
        a.fogAmbient = mul(a.fogAmbient, lerp(Vec3{1, 1, 1}, tint, 0.6f));
        a.skyHorizon = mul(a.skyHorizon, lerp(Vec3{1, 1, 1}, tint, 0.7f));
    }
    a.purkinje = a.night;
    L.atmo = a;

    // Key light L0: the sun above -1 deg, otherwise the v5 moon (fading out through the bright twilight).
    DirectionalLight key;
    key.enabled = L.moon.enabled;
    if (a.keyIsSun) {
        key.direction = keyDirection();
        key.color = tr;
        key.intensity = kSunIntensity * smoothstepf(kSunKeyAboveDeg, 10.0f, e);
    } else {
        key.direction = moonLightDirection();
        key.intensity = DirectionalLight{}.intensity * (1.0f - smoothstepf(-6.0f, kSunKeyAboveDeg, e));
    }
    L.moon = key;
    L.ambient = mixLog(LightRig{}.ambient, kDayAmbient, a.day);
    L.spot.enabled = a.beamsOn;
    L.beacon.enabled = a.practicalScale > 0.0f;
}

std::string DayCycle::clockText() const {
    const int minutes = static_cast<int>(std::floor(hour_ * 60.0f)) % 1440;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%02d:%02d %s %s", minutes / 60, minutes % 60, phaseName(), auto_ ? "AUTO" : "MANUAL");
    return buf;
}

// ------------------------------------------------------------------------------------------- thermal + clouds
float solarWeight(float elevDeg) { return clampf(std::sin(radians(elevDeg)) / std::sin(radians(60.0f)), 0.0f, 1.0f); }

float apparentHeat(float uHeat, float irr, float sunNdotL, float shadow, float solar) {
    float h = uHeat + 0.04f * std::fmin(irr, 1.5f);
    if (solar > 0.0f) {
        const float passive = 1.0f - smoothstepf(kPassiveLo, kPassiveHi, uHeat);
        h += kSolarGain * solar * passive * (0.3f + 0.7f * clampf(sunNdotL, 0.0f, 1.0f) * shadow);
    }
    return h;
}

float cloudDensity(float x, float z, float time) {
    const float n = fbm((x + kCloudWindX * time) * kCloudScale, (z + kCloudWindZ * time) * kCloudScale);
    return smoothstepf(kCloudLo, kCloudHi, n);
}

float cloudShadow(const Vec3& p, const Vec3& towardSun, float time) {
    if (towardSun.y <= 0.0f) return 1.0f;   // sun below the horizon: no cloud shadows
    const Vec3 q = p + towardSun * ((kCloudAltitude - p.y) / std::max(towardSun.y, 0.05f));
    return 1.0f - kCloudOpacity * cloudDensity(q.x, q.z, time);
}

// ------------------------------------------------------------------------------------------- sky clock
namespace {

constexpr float kSunDistance = 300.0f;   // same distance as the moon sphere

class SkyClock : public Controllable {
public:
    explicit SkyClock(DayCycle& cycle) : cycle_(cycle) {
        Builder b("Sky Clock", "SKY", ShadingModel::Gouraud);
        root_ = b.root("Sky clock", {0, 0, 0}, "Time of day: carries the sun along its daily path");
        Material sun = makeMaterial({1.0f, 0.95f, 0.85f}, 0.25f, Surface::Plain, 0.9f, 0.0f, 5.0f);
        sun.fogged = false;
        sun_ = b.part(root_.get(), "Sun", PrimitiveType::Sphere, {9, 9, 9}, {0, 0, 0}, cycle.towardSun() * kSunDistance,
                      sun, "Visual source of the key light L0 by day");
        sun_->explodable = false;
        sun_->obstacle = false;
        place();
    }

    const char* name() const override { return "SKY / TIME OF DAY"; }
    const char* controls() const override { return "W/S scrub the clock  \\ auto / manual  ENTER next phase"; }
    SceneNode* root() override { return root_.get(); }

    void update(const WorldContext& w, const ControlInput* in) override {
        if (in) {
            if (std::fabs(in->forward) > 0.01f) cycle_.scrub(in->forward * cycle_.config().scrubHoursPerSec * w.dt);
            if (in->actionPressed) cycle_.toggleAuto();
            if (in->firePressed) cycle_.jumpToNextPhase();
        }
        place();
    }

    CameraHint camera(bool firstPerson) const override {
        // Look from the camp towards the key light (sun by day, moon by night), the camp in the foreground.
        const Vec3 to = cycle_.keyIsSun() ? cycle_.towardSun() : moonLightDirection() * -1.0f;
        const Vec3 h = normalize(Vec3{to.x, 0.0f, to.z} + Vec3{1e-4f, 0.0f, 0.0f});
        const float elev = clampf(degrees(std::asin(clampf(to.y, -1.0f, 1.0f))), 8.0f, 35.0f);
        CameraHint c;
        c.eye = firstPerson ? Vec3{0.0f, 2.0f, 0.0f} : Vec3{-h.x * 30.0f, 9.0f, -h.z * 30.0f};
        c.target = c.eye + (h * std::cos(radians(elev)) + Vec3{0, std::sin(radians(elev)), 0}) * 50.0f;
        c.fovDeg = 70.0f;
        return c;
    }

    std::string status() const override {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s  SUN %+.1f AZ %03d", cycle_.clockText().c_str(), cycle_.sunElevationDeg(),
                      static_cast<int>(std::lround(cycle_.sunAzimuthDeg())) % 360);
        return buf;
    }
    Vec3 position() const override { return cycle_.towardSun() * kSunDistance; }
    float friendlyRadius() const override { return 0.0f; }   // not a ground object

private:
    void place() {
        const float e = cycle_.sunElevationDeg();
        sun_->local.translation = cycle_.towardSun() * kSunDistance;
        sun_->visible = e > -3.0f;
        // Disc colour follows the air-mass reddening (normalised: the disc stays bright, only its hue changes).
        const Vec3 tr = DayCycle::sunTransmittance(e);
        const float m = std::max({tr.x, tr.y, tr.z, 1e-4f});
        sun_->material.baseColor = Vec3{std::max(tr.x / m, 0.05f), std::max(tr.y / m, 0.05f), std::max(tr.z / m, 0.05f)};
    }

    DayCycle& cycle_;
    std::unique_ptr<SceneNode> root_;
    SceneNode* sun_ = nullptr;
};

}  // namespace

std::unique_ptr<Controllable> makeSkyClock(DayCycle& cycle) { return std::make_unique<SkyClock>(cycle); }

}  // namespace ts
