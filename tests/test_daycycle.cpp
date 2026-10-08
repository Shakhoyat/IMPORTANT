// T190-T199: v6 time of day (solar model, light presets, phases, sky clock, solar loading, clouds).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "TestKit.h"
#include "objects/Environment.h"
#include "scene/Controllable.h"
#include "scene/DayCycle.h"
#include "scene/Scene.h"

using ts::testkit::fmt;
using ts::testkit::record;

namespace {
using namespace ts;

float angleDeg(const Vec3& a, const Vec3& b) {
    return degrees(std::acos(clampf(dot(normalize(a), normalize(b)), -1.0f, 1.0f)));
}
bool same(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
std::string hhmm(float h) {
    const int m = static_cast<int>(std::lround(h * 60.0f));
    return fmt("%02d:%02d", m / 60, m % 60);
}

// Every field of the rig that the v5 night scene used, compared exactly (bit-for-bit floats).
bool rigIsV5Night(const LightRig& r, std::string& diff) {
    const LightRig v5;
    const Atmosphere& a = r.atmo;
    const Atmosphere d;
    diff.clear();
    if (!same(r.moon.direction, moonLightDirection())) diff += "moon.dir ";
    if (!same(r.moon.color, v5.moon.color) || r.moon.intensity != v5.moon.intensity || r.moon.enabled != v5.moon.enabled) diff += "moon ";
    if (!same(r.ambient, v5.ambient)) diff += "ambient ";
    if (r.spot.enabled != v5.spot.enabled || r.beacon.enabled != v5.beacon.enabled) diff += "spot/beacon ";
    if (a.day != d.day || a.night != d.night || a.solar != d.solar || a.practicalScale != d.practicalScale) diff += "weights ";
    if (a.beamsOn != d.beamsOn || a.keyIsSun != d.keyIsSun || a.twilight != d.twilight) diff += "flags ";
    if (!same(a.sunDir, d.sunDir) || !same(a.sunColor, d.sunColor)) diff += "sun ";
    if (!same(a.skyZenith, d.skyZenith) || !same(a.groundBounce, d.groundBounce)) diff += "sky ";
    if (!same(a.fogColor, d.fogColor) || !same(a.fogAmbient, d.fogAmbient)) diff += "fogcol ";
    if (a.fogDensity != d.fogDensity || a.volFogDensity != d.volFogDensity) diff += "fogdens ";
    if (a.exposureKeyLog2 != d.exposureKeyLog2 || a.purkinje != d.purkinje) diff += "exposure ";
    return diff.empty();
}

// Runs the auto clock for 'seconds' (dt 1/30 s), collecting the phase events.
std::vector<GameEvent> runAuto(DayCycle& c, float seconds) {
    std::vector<GameEvent> ev;
    const float dt = 1.0f / 30.0f;
    for (float t = 0.0f; t < seconds; t += dt) c.update(dt, ev, t);
    return ev;
}

const char* eventName(GameEventType t) {
    switch (t) {
        case GameEventType::Dusk: return "Dusk";
        case GameEventType::NightFalls: return "NightFalls";
        case GameEventType::Dawn: return "Dawn";
        case GameEventType::DayBreak: return "DayBreak";
        default: return "other";
    }
}

}  // namespace

TS_TEST_GROUP(daycycle) {
    using namespace ts;
    const DayCycleConfig cfg;

    // T190 solar path
    {
        const float rise = DayCycle::hourAtElevation(0.0f, true, cfg), set = DayCycle::hourAtElevation(0.0f, false, cfg);
        const float noonE = DayCycle::sunElevationDeg(12.0f, cfg);
        DayCycle noon(12.0f), morning(8.0f), evening(16.0f);
        const float azNoon = noon.sunAzimuthDeg(), azAm = morning.sunAzimuthDeg(), azPm = evening.sunAzimuthDeg();
        float maxUnitErr = 0.0f, maxEq = 0.0f;
        for (float h = 0.0f; h < 24.0f; h += 0.25f) {
            const Vec3 s = DayCycle::sunDirection(h, cfg);
            maxUnitErr = std::max(maxUnitErr, std::fabs(length(s) - 1.0f));
            // closed form sin e = sin(phi) sin(delta) + cos(phi) cos(delta) cos(H)
            const float phi = radians(cfg.latitudeDeg), del = radians(cfg.declinationDeg), H = radians(15.0f * (h - 12.0f));
            const float se = std::sin(phi) * std::sin(del) + std::cos(phi) * std::cos(del) * std::cos(H);
            maxEq = std::max(maxEq, std::fabs(s.y - se));
        }
        const bool ok = std::fabs(rise - (6.0f + 8.0f / 60.0f)) < 4.0f / 60.0f && std::fabs(set - (17.0f + 52.0f / 60.0f)) < 4.0f / 60.0f &&
                        std::fabs(noonE - 62.6f) < 0.2f && std::fabs(azNoon - 180.0f) < 0.5f && azAm > 90.0f && azAm < 120.0f &&
                        std::fabs((360.0f - azPm) - azAm) < 0.5f && maxUnitErr < 1e-5f && maxEq < 1e-5f;
        record("T190", "Solar position (lat 22.8 N, decl -4.6): sunrise/sunset, noon elevation, azimuth symmetry",
               "sunrise 06:08 +-4 min, sunset 17:52 +-4 min, noon e 62.6 +-0.2 deg due south, 08:00 / 16:00 mirror",
               fmt("sunrise %s sunset %s noon e %.2f az %.1f; az 08:00 %.1f, 16:00 %.1f; |s|-1 %.1e, sin e err %.1e",
                   hhmm(rise).c_str(), hhmm(set).c_str(), noonE, azNoon, azAm, azPm, maxUnitErr, maxEq),
               ok);
    }

    // T191 gating: waves (night phase), searchlights (sun < -2 deg), practical lamps, rig switches
    {
        int bad = 0, nightSamples = 0;
        for (float h = 0.0f; h < 24.0f; h += 0.05f) {
            DayCycle c(h);
            const float e = c.sunElevationDeg();
            LightRig r;
            c.applyTo(r);
            if (c.wavesAllowed() != (c.phase() == DayPhase::Night)) ++bad;
            if (c.wavesAllowed() && e > kNightBelowDeg + kPhaseHysteresisDeg) ++bad;
            if (c.beamsOn() != (e < kBeamsBelowDeg) || r.spot.enabled != c.beamsOn() || r.atmo.beamsOn != c.beamsOn()) ++bad;
            if (e > 2.0f && (r.atmo.practicalScale != 0.0f || r.beacon.enabled)) ++bad;
            if (e < -4.0f && r.atmo.practicalScale != 1.0f) ++bad;
            nightSamples += c.wavesAllowed();
        }
        DayCycle noon(12.0f), night(23.0f);
        const bool ok = bad == 0 && !noon.wavesAllowed() && night.wavesAllowed() && !noon.beamsOn() && night.beamsOn();
        record("T191", "Day/night gating: waves only in NIGHT (sun < -6 deg), searchlights below -2 deg, lamps off by day",
               "0 violations over 24 h (480 samples); noon: no waves, no beams; 23:00: both",
               fmt("%d violations, %d night samples (%.1f h of waves)", bad, nightSamples, nightSamples * 0.05f), ok);
    }

    // T192 night preset == v5 literals (bit-for-bit)
    {
        std::string diffs;
        int fails = 0;
        for (float h : {23.0f, 0.0f, 2.5f, 4.9f, 19.0f, 21.5f}) {
            std::string d;
            if (!rigIsV5Night(DayCycle::rigAt(h, cfg), d)) {
                ++fails;
                diffs += hhmm(h) + ": " + d + "; ";
            }
        }
        // A scene with the default options is the v5 night scene.
        Scene scene;
        std::string d;
        const bool sceneOk = rigIsV5Night(scene.lights, d);
        if (!sceneOk) diffs += "Scene{}: " + d;
        const bool ok = fails == 0 && sceneOk;
        record("T192", "Night preset equals the v5 literals exactly (key = moon, ambient, sky, fog, exposure, purkinje)",
               "rigAt(23:00, 00:00, 02:30, 04:54, 19:00, 21:30) and Scene{} lights == LightRig{} (moon dir = moonLightDirection())",
               ok ? "all fields identical at 6 night hours and in Scene{}" : diffs, ok);
    }

    // T193 day preset
    {
        const LightRig r = DayCycle::rigAt(12.0f, cfg);
        DayCycle noon(12.0f);
        const float off = angleDeg(r.moon.direction, noon.towardSun() * -1.0f);
        const LightRig dusk = DayCycle::rigAt(17.6f, cfg);
        const bool reddened = dusk.moon.color.x > dusk.moon.color.y && dusk.moon.color.y > dusk.moon.color.z && dusk.moon.color.z < 0.5f;
        const bool ok = r.atmo.keyIsSun && std::fabs(r.moon.intensity - kSunIntensity) < 1e-4f && r.moon.color.z > 0.95f &&
                        off < 1.0f && r.atmo.day == 1.0f && r.atmo.night == 0.0f && r.atmo.purkinje == 0.0f &&
                        std::fabs(r.atmo.exposureKeyLog2 - kDayExposureKeyLog2) < 1e-5f && r.atmo.skyZenith.z > 5.0f * Atmosphere{}.skyZenith.z &&
                        r.atmo.fogDensity < Atmosphere{}.fogDensity && !r.atmo.beamsOn && r.atmo.solar > 0.99f && reddened &&
                        dusk.atmo.twilight > 0.0f;
        record("T193", "Day preset: sun key (3.2, Kasten-Young colour), bright sky, thin haze, day exposure key, no Purkinje",
               "noon: key = sun within 1 deg, I 3.2, colour ~white; 17:36 colour r > g > b (reddened); twilight > 0",
               fmt("noon I %.2f col (%.2f %.2f %.2f) off %.2f deg key %.2f sky.z %.2f; 17:36 col (%.2f %.2f %.2f) e %.1f tw %.2f",
                   r.moon.intensity, r.moon.color.x, r.moon.color.y, r.moon.color.z, off, r.atmo.exposureKeyLog2,
                   r.atmo.skyZenith.z, dusk.moon.color.x, dusk.moon.color.y, dusk.moon.color.z,
                   DayCycle::sunElevationDeg(17.6f, cfg), dusk.atmo.twilight),
               ok);
    }

    // T194 auto period, event sequence, scrub / jump, hysteresis
    {
        const float period = DayCycle::cycleSeconds(cfg);
        DayCycle c(23.0f, true, cfg);
        const std::vector<GameEvent> ev = runAuto(c, period);
        std::string seq;
        for (const GameEvent& e : ev) seq += std::string(eventName(e.type)) + " ";
        const bool order = ev.size() == 4 && ev[0].type == GameEventType::Dawn && ev[1].type == GameEventType::DayBreak &&
                           ev[2].type == GameEventType::Dusk && ev[3].type == GameEventType::NightFalls;
        float dh = std::fabs(c.hour() - 23.0f);
        dh = std::min(dh, 24.0f - dh);
        // Scrub wraps; a fast forward scrub over the morning emits every crossing; a backward one only the entered phase.
        DayCycle s(23.5f, false, cfg);
        s.scrub(1.0f);
        const float wrapped = s.hour();
        std::vector<GameEvent> e1;
        s.scrub(12.0f);   // 00:30 -> 12:30 in one step
        s.update(0.0f, e1, 0.0f);
        std::vector<GameEvent> e2;
        s.setHour(6.1f);  // back into the dawn band
        s.update(0.0f, e2, 0.0f);
        // Hysteresis: dithering +-0.2 min around the -6 deg crossing gives one event, not dozens.
        const float tNight = DayCycle::hourAtElevation(kNightBelowDeg, false, cfg);
        DayCycle hcy(tNight - 0.05f, false, cfg);
        std::vector<GameEvent> e3;
        for (int i = 0; i < 200; ++i) {
            hcy.setHour(tNight + ((i % 2) ? 0.0035f : -0.0035f) + 0.02f * (i > 100));
            hcy.update(0.0f, e3, 0.0f);
        }
        const bool ok = period > 260.0f && period < 320.0f && order && dh < 0.05f && std::fabs(wrapped - 0.5f) < 1e-4f &&
                        e1.size() == 2 && e1[0].type == GameEventType::Dawn && e1[1].type == GameEventType::DayBreak &&
                        e2.size() == 1 && e2[0].type == GameEventType::Dawn && e3.size() == 1;
        record("T194", "Auto cycle period and phase events (Dawn, DayBreak, Dusk, NightFalls once each), scrub, hysteresis",
               "period 260-320 s; 4 events in order; back to 23:00 +-3 min; 23:30+1h = 00:30; jump 00:30->12:30 emits Dawn+DayBreak; back to dawn 1 event; dither 1 event",
               fmt("period %.1f s; events: %s; end %s; wrap %.3f; fast %zu, back %zu, dither %zu", period, seq.c_str(),
                   hhmm(c.hour()).c_str(), wrapped, e1.size(), e2.size(), e3.size()),
               ok);
    }

    // T195 quantised key direction (static shadow cache)
    {
        std::set<std::string> dirs;
        float maxOff = 0.0f;
        int changes = 0;
        Vec3 prev{0, 0, 0};
        for (float h = 9.0f; h < 10.0f; h += 0.001f) {
            DayCycle c(h);
            const Vec3 k = c.keyDirection();
            maxOff = std::max(maxOff, angleDeg(k, c.towardSun() * -1.0f));
            if (!same(k, prev)) ++changes;
            prev = k;
            dirs.insert(fmt("%.6f %.6f %.6f", k.x, k.y, k.z));
        }
        const bool ok = maxOff < 0.75f && changes < 40 && changes >= 8;
        record("T195", "Key-light direction quantised to 1 deg (elevation, azimuth): shadow cache re-rendered per quantum",
               "09:00-10:00 in 3.6 s steps: within 0.75 deg of the sun, 8..40 distinct directions",
               fmt("max offset %.3f deg, %d changes, %zu distinct directions", maxOff, changes, dirs.size()), ok);
    }

    // T196 solar loading never lifts a passive surface to the CV floor (0.80)
    {
        Scene scene;
        float peak = 0.0f, peakHeat = 0.0f;
        std::string peakName;
        int passive = 0;
        for (const SceneNode* n : scene.leaves()) {
            const float u = n->material.heat;
            if (u >= kPassiveHi) continue;
            ++passive;
            const float variation = (n->primitive == PrimitiveType::TerrainDisc ? 0.11f : 0.015f) * 0.75f;
            const float h = apparentHeat(u, 0.0f, 1.0f, 1.0f, 1.0f) + variation;   // full noon sun, no shadow, no cloud
            if (h > peak) {
                peak = h;
                peakHeat = u;
                peakName = n->name;
            }
        }
        float worst = 0.0f;   // worst case over the whole passive heat range
        for (float u = 0.0f; u < kPassiveHi; u += 0.001f) worst = std::max(worst, apparentHeat(u, 0.0f, 1.0f, 1.0f, 1.0f));
        const float nightMatch = std::fabs(apparentHeat(0.3f, 0.8f, 1.0f, 1.0f, 0.0f) - (0.3f + 0.04f * 0.8f));
        const float typical = apparentHeat(0.5f, 0.0f, 1.0f, 1.0f, 1.0f);
        const bool ok = passive > 100 && peak < 0.80f && worst < 0.80f && nightMatch < 1e-6f && typical > 0.6f && typical < 0.64f;
        record("T196", "Solar loading only on passive surfaces (heat < 0.75): noon peak stays below the CV floor 0.80",
               "all passive leaves < 0.80 at full sun (incl. texture variation); passive 0.50 -> ~0.62; night = v5 formula",
               fmt("%d passive leaves, peak %.3f (%s, heat %.2f), range worst %.3f, 0.50 -> %.3f, night err %.1e", passive,
                   peak, peakName.c_str(), peakHeat, worst, typical, nightMatch),
               ok);
    }

    // T197 sky clock controllable
    {
        DayCycle c(12.0f, false, cfg);
        std::unique_ptr<Controllable> clk = makeSkyClock(c);
        bool sunLeaf = false;
        const SceneNode* sun = nullptr;
        if (clk && clk->root()) {
            for (const auto& ch : clk->root()->children)
                if (ch->name == "Sun") {
                    sunLeaf = ch->primitive == PrimitiveType::Sphere;
                    sun = ch.get();
                }
        }
        bool ok = clk && sunLeaf && std::string(clk->name()) == "SKY / TIME OF DAY" && clk->possessable() &&
                  clk->friendlyRadius() == 0.0f;
        float afterScrub = 0.0f, sunErr = 1.0f;
        bool toggled = false, jumped = false;
        if (ok) {
            WorldContext w;
            w.dt = 0.1f;
            ControlInput in;
            in.forward = 1.0f;
            for (int i = 0; i < 10; ++i) clk->update(w, &in);   // 1 s of W
            afterScrub = c.hour();
            ControlInput act;
            act.actionPressed = true;
            clk->update(w, &act);
            toggled = c.autoRun();
            const DayPhase before = c.phase();
            ControlInput fire;
            fire.firePressed = true;
            clk->update(w, &fire);
            jumped = c.phase() != before;
            sunErr = length(sun->local.translation - c.towardSun() * 300.0f);
        }
        ok = ok && std::fabs(afterScrub - 14.0f) < 1e-3f && toggled && jumped && sunErr < 1e-3f;
        record("T197", "SKY / TIME OF DAY controllable: W scrubs 2 h/s, \\ toggles auto, ENTER jumps a phase, sun sphere follows",
               "12:00 + 1 s W = 14:00; auto on; phase changes; Sun sphere at 300 m along the sun; friendly radius 0",
               fmt("hour %.3f, auto %d, jumped %d, sun error %.4f m, sun leaf %d", afterScrub, toggled, jumped, sunErr, sunLeaf), ok);
    }

    // T198 clouds: noon cloud-shadow mean, drift with the wind, no effect at night
    {
        const Vec3 noonSun = DayCycle::sunDirection(12.0f, cfg);
        double sum = 0.0;
        float lo = 1.0f, hi = 0.0f;
        int n = 0;
        for (float x = -40.0f; x <= 40.0f; x += 1.0f)
            for (float z = -40.0f; z <= 40.0f; z += 1.0f)
                for (float t : {0.0f, 60.0f, 120.0f, 180.0f}) {
                    const float s = cloudShadow({x, 0.0f, z}, noonSun, t);
                    sum += s;
                    lo = std::min(lo, s);
                    hi = std::max(hi, s);
                    ++n;
                }
        const float mean = static_cast<float>(sum / n);
        // The field moves with the wind: density at (x, z, t) = density at (x - w dt, z - w dt, t + dt); it changes in place.
        float driftErr = 0.0f, change = 0.0f;
        for (int i = 0; i < 200; ++i) {
            const float x = -50.0f + 0.7f * i, z = 30.0f - 0.45f * i, dt = 25.0f;
            driftErr = std::max(driftErr, std::fabs(cloudDensity(x, z, 10.0f) - cloudDensity(x - kCloudWindX * dt, z - kCloudWindZ * dt, 10.0f + dt)));
            change += std::fabs(cloudDensity(x, z, 10.0f) - cloudDensity(x, z, 10.0f + dt));
        }
        change /= 200.0f;
        float nightMax = 0.0f;
        for (float h : {20.0f, 23.0f, 2.0f})
            for (float x = -30.0f; x <= 30.0f; x += 3.0f)
                nightMax = std::max(nightMax, std::fabs(1.0f - cloudShadow({x, 0.0f, -x}, DayCycle::sunDirection(h, cfg), 50.0f)));
        const bool nightOff = !DayCycle::rigAt(23.0f, cfg).atmo.keyIsSun;   // the renderer shades only a sun key
        const bool ok = mean >= 0.55f && mean <= 0.9f && lo < 0.6f && hi == 1.0f && driftErr < 2e-3f && change > 0.05f &&
                        nightMax == 0.0f && nightOff;
        record("T198", "Cloud layer (120 m, coverage ~0.45) and its shadows: soft, drifting with the wind, none at night",
               "noon mean shadow 0.55-0.9 with fully lit gaps; field translated by the wind (err < 2e-3), changes in place; night 0",
               fmt("mean %.3f (min %.2f max %.2f); drift err %.1e, in-place change %.3f; night |1-s| max %.2f", mean, lo, hi,
                   driftErr, change, nightMax),
               ok);
    }

    // T199 clock wrap and HUD text
    {
        DayCycle c(23.9f, true, cfg);
        std::vector<GameEvent> ev;
        for (int i = 0; i < 120; ++i) c.update(0.1f, ev, 0.0f);   // 12 s at night ~ 0.9 h
        const std::string txt = c.clockText();
        DayCycle d(17.7f, false, cfg);
        const std::string dusk = d.clockText();
        const bool ok = c.hour() >= 0.0f && c.hour() < 1.5f && txt.size() >= 14 && txt.substr(2, 1) == ":" &&
                        txt.find("NIGHT AUTO") != std::string::npos && dusk.find("DUSK MANUAL") != std::string::npos &&
                        ev.empty();
        record("T199", "Auto clock wraps past midnight; HUD clock text 'HH:MM PHASE AUTO|MANUAL'",
               "23:54 + 12 s -> 00:xx NIGHT AUTO, no events; 17:42 -> DUSK MANUAL",
               fmt("hour %.3f '%s', '%s', %zu events", c.hour(), txt.c_str(), dusk.c_str(), ev.size()), ok);
    }
}
