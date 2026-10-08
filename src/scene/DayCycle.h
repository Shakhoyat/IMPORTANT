// v6 time of day (WP-A). A solar-position model for the camp (latitude 22.8 deg, early October) drives the key
// light (sun by day, the v5 moon by night), the sky / fog / exposure presets, the practical lamps, the UAV
// searchlights and the rule that rover waves only run at night. Pure CPU (no GL): unit-testable and exported.
//
//   sin e = sin(phi) sin(delta) + cos(phi) cos(delta) cos(H),  H = 15 deg x (hour - 12)
//   towards the sun (east x, up y, north z) = (-cos(delta) sin H, sin e, cos(phi) sin(delta) - sin(phi) cos(delta) cos H)
//
// Phases (with 0.25 deg hysteresis): NIGHT below -6 deg, DAY above +4 deg, DAWN / DUSK in between (morning /
// evening). Rover waves are allowed at NIGHT only; the UAV searchlights run while the sun is below -2 deg.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "math/Math3D.h"
#include "rendering/Lighting.h"
#include "scene/GameEvents.h"

namespace ts {

class Controllable;

enum class DayPhase { Night, Dawn, Day, Dusk };

struct DayCycleConfig {
    float latitudeDeg = 22.8f, declinationDeg = -4.6f;   // Khulna, early October
    float daySecPerHour = 10.0f, nightSecPerHour = 13.3f;  // ~120 s of day + ~160 s of night in auto mode
    float twilightSlow = 1.5f;                             // |elevation| < 6 deg plays slower
    float scrubHoursPerSec = 2.0f;                         // manual scrubbing rate
    float shadowQuantDeg = 1.0f;                           // key-light direction quantum (static shadow cache)
};

// Elevation thresholds of the phases and of the light switches (degrees).
constexpr float kNightBelowDeg = -6.0f, kDayAboveDeg = 4.0f, kPhaseHysteresisDeg = 0.25f;
constexpr float kBeamsBelowDeg = -2.0f;
// Sun key light: intensity 3.2 x smoothstep(-1, 10 deg, e) x Kasten-Young transmittance (per channel optical
// depth, normalised to the zenith).
constexpr float kSunIntensity = 3.2f, kSunKeyAboveDeg = -1.0f;
constexpr float kSunOpticalDepth[3] = {0.06f, 0.11f, 0.21f};
// Day preset (linear radiance / irradiance of the HDR view); the night preset is Atmosphere{} (v5 literals).
constexpr float kDayExposureKeyLog2 = -2.8f;   // mean log2 luminance of the reference noon frame

class DayCycle {
public:
    explicit DayCycle(float startHour = 23.0f, bool autoRun = false, DayCycleConfig cfg = {});

    // Advances the clock when auto, emits Dusk / NightFalls / Dawn / DayBreak once per crossing.
    void update(float dt, std::vector<GameEvent>& events, float time);

    void setAuto(bool on);
    void toggleAuto();
    bool autoRun() const;
    void setHour(float h);            // wraps into [0, 24)
    void scrub(float hoursDelta);     // manual time change (wraps)
    void jumpToNextPhase();           // next of dawn / day / dusk / night

    float hour() const;
    DayPhase phase() const;
    const char* phaseName() const;    // "NIGHT", "DAWN", "DAY", "DUSK"
    float sunElevationDeg() const;
    float sunAzimuthDeg() const;      // from north (+Z) towards east (+X)
    Vec3 towardSun() const;           // unit vector to the sun
    Vec3 keyDirection() const;        // direction the key light travels (sun or moon), quantised
    bool keyIsSun() const;            // sun above -1 deg: the key light is the sun
    float daylight() const;           // 0 night .. 1 day (smoothstep of the elevation)
    bool wavesAllowed() const;        // sun below -6 deg
    bool beamsOn() const;             // searchlights allowed (sun below -2 deg)
    float hoursPerSecond() const;     // auto-mode clock rate at the current hour

    // Fills the key light (moon slot), ambient and the atmosphere (sky, fog, exposure, practicals, beams).
    void applyTo(LightRig& lights) const;
    std::string clockText() const;    // e.g. "17:42 DUSK AUTO"
    const DayCycleConfig& config() const { return cfg_; }
    // Replaces the configuration only (rates, quantum...): hour, auto mode and the phase / hysteresis state stay.
    void setConfig(const DayCycleConfig& cfg) { cfg_ = cfg; }

    static Vec3 sunDirection(float hour, const DayCycleConfig& cfg);  // unit vector to the sun (pure)
    static float sunElevationDeg(float hour, const DayCycleConfig& cfg);
    // Clock hour at which the sun passes elevation 'elevDeg' (morning or evening branch); -1 if never.
    static float hourAtElevation(float elevDeg, bool morning, const DayCycleConfig& cfg);
    // Seconds of one full auto cycle (numerical integration of the clock rate over 24 h).
    static float cycleSeconds(const DayCycleConfig& cfg);
    // Sun colour (Kasten-Young air mass reddening, 1 at the zenith) at elevation 'elevDeg' (>= -1).
    static Vec3 sunTransmittance(float elevDeg);
    // Kasten & Young (1989) relative optical air mass.
    static float airMass(float elevDeg);
    // The complete light rig of a given hour (key light, ambient, atmosphere): applyTo of a fresh cycle.
    static LightRig rigAt(float hour, const DayCycleConfig& cfg = {});

private:
    DayPhase bandFor(float hour) const;   // phase after a move to 'hour' (hysteresis around the current one)
    void moveTo(float hour, bool backwards);

    DayCycleConfig cfg_;
    float hour_ = 23.0f;
    bool auto_ = false;
    DayPhase phase_ = DayPhase::Night;
    std::vector<GameEventType> pending_;   // crossings since the last update()
};

// CPU mirror of the shader's thermal solar loading (the shader gets the same constants):
//   heat = uHeat + 0.04 min(irr_night, 1.5) + kSolarGain solar passive(uHeat) (0.3 + 0.7 N.L shadow)
//   passive(u) = 1 - smoothstep(kPassiveLo, kPassiveHi, u)   (engines, exhausts, lamps get no solar term)
// irr_night: the v5 irradiance term without the sun (moon, beacon, searchlight); solar 0..1 (sun height).
constexpr float kSolarGain = 0.12f, kPassiveLo = 0.55f, kPassiveHi = 0.75f;
float apparentHeat(float uHeat, float irradianceNight, float sunNdotL, float shadow, float solar);
float solarWeight(float elevDeg);    // sin(e) / sin(60 deg), clamped to [0, 1]

// Procedural cloud layer shared by the day sky and the cloud shadows on the ground (CPU mirror of the GLSL
// cloudDensity in ShaderSources.cpp: 4-octave value-noise fBm of the drifting layer, soft threshold).
constexpr float kCloudAltitude = 120.0f, kCloudCoverage = 0.45f;
constexpr float kCloudScale = 1.0f / 55.0f;           // noise cycles per metre
constexpr float kCloudWindX = 3.2f, kCloudWindZ = 1.4f;  // drift, m/s
constexpr float kCloudLo = 0.395f, kCloudHi = 0.545f;   // fBm -> density soft threshold (coverage ~0.45)
constexpr float kCloudOpacity = 0.6f;                 // sunlight removed under the thickest cloud
float cloudDensity(float x, float z, float time);           // 0..1 at a point of the cloud layer
float cloudShadow(const Vec3& p, const Vec3& towardSun, float time);  // 1 = sunlit, lower under a cloud

// Possessable "SKY / TIME OF DAY" object: W/S scrub the clock, '\' toggles auto / manual, Enter jumps a phase.
// Its node tree is a group with an emissive "Sun" sphere 300 m out along the sun direction.
std::unique_ptr<Controllable> makeSkyClock(DayCycle& cycle);

}  // namespace ts
