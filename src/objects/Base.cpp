#include "objects/Base.h"

#include <algorithm>
#include <cstdio>

#include "objects/PropKit.h"
#include "objects/Launchers.h"
#include "objects/Structures.h"

namespace ts {

// ---------------------------------------------------------------------------------------------------------
// Wind
// ---------------------------------------------------------------------------------------------------------
float windDirectionDeg(float t) { return 200.0f + 25.0f * std::sin(0.21f * t) + 10.0f * std::sin(0.57f * t + 1.3f); }
float windStrength(float t) {
    return clampf(0.68f + 0.27f * std::sin(0.35f * t + 1.0f) + 0.05f * std::sin(1.7f * t), 0.35f, 1.0f);
}

namespace {

using P = PrimitiveType;
using namespace layout;

constexpr float G = kApronLevel;
constexpr float kTwoPi = 6.28318530718f;

float smooth01(float x) {
    x = clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// Yaw that turns a primitive's local X axis onto the tangent of the circle at polar angle a (from +X to +Z);
// it also turns local +Z onto the inward radial direction.
float tangentYawDeg(float a) { return degrees(std::atan2(-std::cos(a), -std::sin(a))); }

// ----- materials ------------------------------------------------------------------------------------------
Material hescoMat() { return makeMaterial({0.42f, 0.38f, 0.27f}, 0.22f, Surface::Hesco, 0.92f, 0.0f); }
Material asphaltMat() { return makeMaterial({0.10f, 0.10f, 0.105f}, 0.28f, Surface::Asphalt, 0.82f, 0.0f); }
Material paintWhite() { return makeMaterial({0.62f, 0.62f, 0.58f}, 0.26f, Surface::Paint, 0.55f, 0.0f); }
Material paintYellow() { return makeMaterial({0.60f, 0.46f, 0.05f}, 0.26f, Surface::Paint, 0.55f, 0.0f); }
Material paintRed() { return makeMaterial({0.50f, 0.04f, 0.03f}, 0.26f, Surface::Paint, 0.5f, 0.0f); }
Material hazard() { return makeMaterial({0.5f, 0.4f, 0.05f}, 0.26f, Surface::Hazard, 0.55f, 0.0f); }
Material corrugated(const Vec3& c) { return makeMaterial(c, 0.30f, Surface::Corrugated, 0.60f, 0.22f); }
Material oliveCorr() { return corrugated({0.24f, 0.30f, 0.18f}); }
Material tanCorr() { return corrugated({0.42f, 0.35f, 0.22f}); }
Material rustCorr() { return corrugated({0.42f, 0.17f, 0.09f}); }
Material blackRubber() { return makeMaterial({0.025f, 0.025f, 0.025f}, 0.30f, Surface::Rubber, 0.85f, 0.0f); }
Material oliveFlat() { return makeMaterial({0.22f, 0.26f, 0.17f}, 0.35f, Surface::Paint, 0.6f, 0.2f); }
Material darkGlass() { return makeMaterial({0.03f, 0.04f, 0.05f}, 0.20f, Surface::Glass, 0.1f, 0.0f); }
// Light discipline (v5): windows are blacked out, floodlights are dim hooded lamps, the rest are small tactical
// markers (red / blue-green) that only mark doors, steps and the gate.
Material blueEmissive() { return makeMaterial({0.08f, 0.25f, 1.0f}, 0.40f, Surface::Glass, 0.2f, 0.0f, 0.35f); }
Material bluePractical() { return asLight(makeMaterial({0.08f, 0.25f, 1.0f}, 0.40f, Surface::Glass, 0.2f, 0.0f, 1.0f), 0.5f, 4.0f); }
Material hoodedLamp(const Vec3& c, float intensity, float range, float coneDeg) {
    return asLight(makeMaterial(c, 0.45f, Surface::Glass, 0.05f, 0.0f, 1.0f), intensity, range, coneDeg);
}
Material tacRedMark() { return makeMaterial({1.0f, 0.05f, 0.03f}, 0.35f, Surface::Glass, 0.2f, 0.0f, 0.55f); }
Material tacTealMark() { return makeMaterial({0.05f, 0.85f, 0.60f}, 0.35f, Surface::Glass, 0.2f, 0.0f, 0.55f); }
Material fabric(const Vec3& c) { return makeMaterial(c, 0.28f, Surface::Plain, 0.9f, 0.0f); }

// ----- primitive shorthands (positions are relative to the parent group; y is above the apron) ---------------
SceneNode* box(Builder& b, SceneNode* p, const std::string& name, const Vec3& size, const Vec3& pos, float yaw,
               const Material& m, const std::string& purpose) {
    return b.part(p, name, P::Cube, size, {0, yaw, 0}, pos, m, purpose);
}
// Vertical cylinder (axis +Y), base at pos.y.
SceneNode* post(Builder& b, SceneNode* p, const std::string& name, float dia, float h, const Vec3& pos,
                const Material& m, const std::string& purpose) {
    return b.part(p, name, P::Cylinder, {dia, dia, h}, {90, 0, 0}, {pos.x, pos.y + 0.5f * h, pos.z}, m, purpose);
}
SceneNode* flush(Builder& b, SceneNode* p, const std::string& name, const Vec3& size, const Vec3& pos, float yaw,
                 const Material& m, const std::string& purpose) {
    return b.part(p, name, P::Cube, size, {0, yaw, 0}, pos, m, purpose);
}

SceneNode* site(Builder& b, SceneNode* root, const std::string& name, float x, float z, float yawDeg,
                const std::string& purpose) {
    return b.group(root, name, {x, G, z}, {0, yawDeg, 0}, purpose);
}

// Sandbag course along a straight line a -> c (parent space, ground y = 0), 'rows' high, running bond.
void sandbagRun(Builder& b, SceneNode* p, const Vec3& a, const Vec3& c, int rows, const char* name = "Sandbag") {
    const Material sand = mats::sandbag();
    const Vec3 d = c - a;
    const float len = std::sqrt(d.x * d.x + d.z * d.z);
    const int n = std::max(1, static_cast<int>(len / 0.5f + 0.5f));
    const float yaw = degrees(std::atan2(d.x, d.z));
    for (int r = 0; r < rows; ++r)
        for (int k = 0; k < n - (r & 1); ++k) {
            const float u = (static_cast<float>(k) + 0.5f + 0.5f * static_cast<float>(r & 1)) / static_cast<float>(n);
            b.part(p, name, P::Cylinder, {0.26f, 0.17f, 0.5f}, {0, yaw, 0},
                   {a.x + d.x * u, a.y + 0.09f + 0.15f * static_cast<float>(r), a.z + d.z * u}, sand, "Sandbag");
        }
}

// Jersey barrier (two stacked blocks: wide foot + sloped upper body), long axis along local Z.
void jersey(Builder& b, SceneNode* p, const Vec3& pos, float yaw, bool striped = false) {
    const Material c = mats::concrete();
    SceneNode* j = b.group(p, "Jersey barrier", pos, {0, yaw, 0}, "Precast concrete vehicle barrier");
    b.part(j, "Jersey foot", P::Cube, {0.6f, 0.30f, 3.0f}, {0, 0, 0}, {0, 0.15f, 0}, c, "Wide base of the barrier");
    b.part(j, "Jersey body", P::Cube, {0.30f, 0.55f, 2.96f}, {0, 0, 0}, {0, 0.575f, 0}, striped ? hazard() : c,
           "Narrow upper part of the barrier");
}

// ---------------------------------------------------------------------------------------------------------
// Perimeter: HESCO wall, concertina wire
// ---------------------------------------------------------------------------------------------------------
constexpr float kGateHalfGap = 4.9f;            // wall opening |x| < this on the +Z side
constexpr float kCellHeight = 1.4f;
constexpr float kTowerRadius = 32.4f;
const float kTowerAngles[4] = {60.0f, 130.0f, 245.0f, 340.0f};

struct WallPlan {
    int cells = 0;
    std::vector<float> top;  // top height per cell, 0 = no cell (gate opening)
};

WallPlan planWall() {
    WallPlan w;
    w.cells = static_cast<int>(std::lround(kTwoPi * kPerimeterRadius / 1.0f));
    w.top.assign(static_cast<size_t>(w.cells), kCellHeight);
    for (int i = 0; i < w.cells; ++i) {
        const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(w.cells);
        const float x = kPerimeterRadius * std::cos(a), z = kPerimeterRadius * std::sin(a);
        const float arcFromGate = (z > 0.0f) ? std::fabs(x) - kGateHalfGap : 1e9f;
        if (arcFromGate < 0.0f) {
            w.top[static_cast<size_t>(i)] = 0.0f;
            continue;
        }
        bool tall = arcFromGate < 3.5f;  // gate flanks
        for (float t : kTowerAngles) {
            float d = std::fabs(degrees(a) - t);
            d = std::min(d, 360.0f - d);
            if (radians(d) * kPerimeterRadius < 4.0f) tall = true;  // corners of the perimeter = the towers
        }
        if (tall) w.top[static_cast<size_t>(i)] = 2.0f * kCellHeight;
    }
    return w;
}

void addPerimeter(Builder& b, SceneNode* root) {
    SceneNode* wall = b.group(root, "HESCO perimeter", {0, G, 0}, {0, 0, 0}, "Bastion wall and concertina wire");
    const Material hesco = hescoMat(), wire = makeMaterial({0.55f, 0.55f, 0.56f}, 0.15f, Surface::Metal, 0.45f, 0.35f);
    const WallPlan plan = planWall();
    for (int i = 0; i < plan.cells; ++i) {
        const float top = plan.top[static_cast<size_t>(i)];
        if (top <= 0.0f) continue;
        const float a = kTwoPi * (static_cast<float>(i) + 0.5f) / static_cast<float>(plan.cells);
        const Vec3 c{kPerimeterRadius * std::cos(a), 0.0f, kPerimeterRadius * std::sin(a)};
        const float yaw = tangentYawDeg(a);
        for (float y = 0.0f; y < top - 0.01f; y += kCellHeight)
            box(b, wall, y > 0.0f ? "HESCO cell (upper)" : "HESCO cell", {1.03f, kCellHeight, 1.06f},
                {c.x, y + 0.5f * kCellHeight, c.z}, yaw, hesco, "Wire-mesh and geotextile cell filled with sand");
    }
    // Concertina: one coil (6 short struts on a helical hexagon) every 4th cell, joined by a razor strand.
    SceneNode* coils = b.group(root, "Concertina wire", {0, G, 0}, {0, 0, 0}, "Coils of razor wire on the wall");
    Vec3 prev{};
    bool havePrev = false;
    for (int i = 0; i < plan.cells; i += 4) {
        const float top = plan.top[static_cast<size_t>(i)];
        if (top <= 0.0f) {
            havePrev = false;
            continue;
        }
        const float a = kTwoPi * (static_cast<float>(i) + 0.5f) / static_cast<float>(plan.cells);
        const Vec3 n{std::cos(a), 0.0f, std::sin(a)}, t{-std::sin(a), 0.0f, std::cos(a)};
        const Vec3 c = n * kPerimeterRadius + Vec3{0, top + 0.40f, 0};
        auto ringPoint = [&](int k) {
            const float ang = radians(60.0f * static_cast<float>(k));
            return c + n * (0.38f * std::cos(ang)) + Vec3{0, 0.38f * std::sin(ang), 0} + t * (0.14f * static_cast<float>(k) - 0.42f);
        };
        for (int k = 0; k < 6; ++k)
            strut(b, coils, "Wire coil turn", P::Cylinder, ringPoint(k), ringPoint(k + 1), 0.035f, wire,
                  "One turn of a concertina coil");
        const Vec3 top0 = c + Vec3{0, 0.30f, 0};
        if (havePrev)
            strut(b, coils, "Razor strand", P::Cylinder, prev, top0, 0.03f, wire, "Strand tying neighbouring coils");
        prev = top0;
        havePrev = true;
    }
}

// ---------------------------------------------------------------------------------------------------------
// Guard towers
// ---------------------------------------------------------------------------------------------------------
void addTower(Builder& b, SceneNode* parent, float angleDeg, int index) {
    const float a = radians(angleDeg);
    char nm[48];
    std::snprintf(nm, sizeof(nm), "Guard tower %d", index + 1);
    SceneNode* t = b.group(parent, nm, {kTowerRadius * std::cos(a), G, kTowerRadius * std::sin(a)},
                           {0, tangentYawDeg(a), 0}, "Perimeter guard tower; local +Z points into the base");
    const Material steel = mats::towerSteel(), planks = mats::crateWood(), sand = mats::sandbag(),
                   roof = oliveFlat();
    constexpr float kPlat = 4.0f;
    auto leg = [](float sx, float sz, float y) {
        const float off = 1.15f - 0.12f * y / 4.0f;
        return Vec3{sx * off, y, sz * off};
    };
    const float cs[4][2] = {{1, 1}, {1, -1}, {-1, -1}, {-1, 1}};
    for (const auto& c : cs)
        strut(b, t, "Tower leg", P::Cylinder, leg(c[0], c[1], -0.2f), leg(c[0], c[1], kPlat), 0.16f, steel,
              "Steel leg");
    for (int i = 0; i < 4; ++i)
        strut(b, t, "Tower brace", P::Cylinder, leg(cs[i][0], cs[i][1], 0.3f), leg(cs[(i + 1) % 4][0], cs[(i + 1) % 4][1], 3.4f),
              0.07f, steel, "Cross brace");
    box(b, t, "Platform", {2.9f, 0.14f, 2.9f}, {0, kPlat + 0.07f, 0}, 0, planks, "Guard platform");
    // Sandbag parapet: courses on the front and both sides (the back holds the ladder hatch).
    box(b, t, "Parapet front", {2.9f, 0.95f, 0.5f}, {0, kPlat + 0.62f, 1.2f}, 0, sand, "Sandbag parapet, front");
    box(b, t, "Parapet side", {0.5f, 0.95f, 2.0f}, {1.2f, kPlat + 0.62f, 0.0f}, 0, sand, "Sandbag parapet, east");
    box(b, t, "Parapet side", {0.5f, 0.95f, 2.0f}, {-1.2f, kPlat + 0.62f, 0.0f}, 0, sand, "Sandbag parapet, west");
    box(b, t, "Parapet cap", {2.9f, 0.14f, 0.5f}, {0, kPlat + 1.17f, 1.2f}, 0, sand, "Top course");
    // Roof on four posts.
    for (const auto& c : cs)
        post(b, t, "Roof post", 0.10f, 2.1f, {c[0] * 1.35f, kPlat + 0.14f, c[1] * 1.35f}, steel, "Roof support");
    box(b, t, "Roof slab", {3.5f, 0.10f, 3.5f}, {0, kPlat + 2.3f, 0}, 0, roof, "Flat roof of the tower");
    box(b, t, "Roof lip", {3.5f, 0.12f, 0.08f}, {0, kPlat + 2.38f, 1.7f}, 0, roof, "Roof edge");
    // Ladder on the back.
    for (float s : {-0.25f, 0.25f})
        strut(b, t, "Ladder rail", P::Cylinder, {s, -0.2f, -1.9f}, {s, kPlat, -1.45f}, 0.05f, steel, "Ladder stringer");
    for (int i = 0; i < 5; ++i) {
        const float k = (static_cast<float>(i) + 0.7f) / 5.5f;
        b.part(t, "Ladder rung", P::Cylinder, {0.035f, 0.035f, 0.5f}, {0, 90, 0},
               {0, lerpf(-0.2f, kPlat, k), -lerpf(1.9f, 1.45f, k)}, steel, "Rung");
    }
    // Machine gun on the parapet (tripod, receiver, barrel).
    box(b, t, "MG mount", {0.12f, 0.3f, 0.12f}, {0.5f, kPlat + 1.35f, 1.15f}, 0, steel, "Pintle mount");
    box(b, t, "MG receiver", {0.14f, 0.16f, 0.5f}, {0.5f, kPlat + 1.58f, 1.2f}, 0, steel, "Machine gun receiver");
    b.part(t, "MG barrel", P::Cylinder, {0.04f, 0.04f, 0.7f}, {0, 0, 0}, {0.5f, kPlat + 1.58f, 1.72f}, steel, "Barrel");
    // Floodlight: sodium spot on the roof front, pointing at the open ground 12 m inside the wall.
    const Vec3 head{-0.8f, kPlat + 2.05f, 1.62f};
    const Vec3 aim = normalize(Vec3{0.35f, -(head.y + 0.0f), 12.0f});
    strut(b, t, "Floodlight bracket", P::Cylinder, {head.x, kPlat + 2.3f, 1.62f}, head, 0.06f, steel, "Lamp arm");
    b.part(t, "Floodlight housing", P::Cylinder, {0.36f, 0.36f, 0.34f}, alignZ(aim), head, steel, "Lamp body");
    b.part(t, "Floodlight lens", P::Sphere, {0.32f, 0.32f, 0.06f}, alignZ(aim), head + aim * 0.18f, darkGlass(),
           "Floodlight lens, switched off (light discipline)");
}

// ---------------------------------------------------------------------------------------------------------
// Outer gate
// ---------------------------------------------------------------------------------------------------------
void addGate(Builder& b, SceneNode* root) {
    SceneNode* g = site(b, root, "Outer gate", kOuterGate.x, kOuterGate.z, 0, "Checkpoint: posts, booth, barriers, signs");
    const Material conc = mats::concrete(), steel = mats::towerSteel();
    // Gate posts (the HESCO flanks next to them are double height).
    for (float s : {-1.0f, 1.0f}) {
        box(b, g, "Gate post", {0.7f, 3.4f, 0.7f}, {s * (kGateHalfGap + 0.45f), 1.7f, 0.2f}, 0, conc, "Reinforced gate post");
        box(b, g, "Gate post cap", {0.85f, 0.15f, 0.85f}, {s * (kGateHalfGap + 0.45f), 3.45f, 0.2f}, 0, conc, "Post cap");
    }
    // Guard booth east of the road.
    SceneNode* booth = b.group(g, "Guard booth", {5.4f, 0, -2.4f}, {0, 0, 0}, "Corrugated checkpoint booth");
    box(b, booth, "Booth floor", {2.4f, 0.2f, 2.4f}, {0, 0.1f, 0}, 0, conc, "Concrete plinth");
    box(b, booth, "Booth body", {2.2f, 2.3f, 2.2f}, {0, 1.35f, 0}, 0, tanCorr(), "Corrugated booth walls");
    box(b, booth, "Booth roof", {2.7f, 0.12f, 2.7f}, {0, 2.56f, 0}, 0, oliveCorr(), "Overhanging roof");
    box(b, booth, "Booth window", {0.04f, 0.7f, 1.3f}, {-1.12f, 1.65f, 0}, 0, darkGlass(), "Blacked-out window facing the road");
    box(b, booth, "Booth door", {0.9f, 1.8f, 0.04f}, {0.3f, 1.1f, 1.12f}, 0, darkGlass(), "Door facing the outside");
    box(b, booth, "Booth AC unit", {0.7f, 0.5f, 0.35f}, {1.25f, 1.1f, -0.5f}, 90, mats::galvanized(), "Air conditioner");
    post(b, booth, "Booth antenna", 0.03f, 1.4f, {0.8f, 2.6f, -0.8f}, steel, "Radio antenna");
    sandbagRun(b, g, {3.9f, 0, -3.9f}, {7.0f, 0, -3.9f}, 3);  // sandbag wall on the north (outer) side
    sandbagRun(b, g, {7.0f, 0, -3.9f}, {7.0f, 0, -1.0f}, 3);
    // Staggered jersey barriers on the shoulders force a slow approach.
    jersey(b, g, {-2.95f, 0, -5.5f}, 0, true);
    jersey(b, g, {2.95f, 0, -8.5f}, 0, true);
    jersey(b, g, {-2.95f, 0, -11.5f}, 0);
    jersey(b, g, {2.95f, 0, -13.0f}, 0);
    // Signs facing the approach (+Z): STOP, CHECKPOINT, SLOW.
    struct Sign {
        float x, z, w, h;
        Material m;
        const char* nm;
    };
    const Sign signs[] = {{-3.6f, -3.2f, 0.9f, 0.9f, paintRed(), "STOP sign"},
                          {3.7f, -7.0f, 1.2f, 0.8f, paintYellow(), "CHECKPOINT sign"},
                          {-3.6f, -9.9f, 0.8f, 0.8f, hazard(), "SLOW hazard board"}};
    for (const Sign& s : signs) {
        post(b, g, "Sign post", 0.07f, 2.0f, {s.x, 0, s.z}, steel, "Sign pole");
        box(b, g, s.nm, {s.w, s.h, 0.04f}, {s.x, 1.75f, s.z + 0.05f}, 0, s.m, "Warning board facing the approach");
    }
    // Gate lamp.
    post(b, g, "Gate lamp pole", 0.12f, 5.4f, {-3.9f, 0, -2.4f}, steel, "Lamp pole");
    box(b, g, "Gate lamp arm", {1.4f, 0.07f, 0.07f}, {-3.2f, 5.35f, -2.4f}, 0, steel, "Lamp arm");
    box(b, g, "Gate lamp hood", {0.42f, 0.04f, 0.34f}, {-2.65f, 5.36f, -2.4f}, 0, steel, "Hood that keeps the light pointing down");
    b.part(g, "Gate lamp", P::Sphere, {0.20f, 0.20f, 0.08f}, {90, 0, 0}, {-2.65f, 5.22f, -2.4f},
           hoodedLamp({1.0f, 0.22f, 0.08f}, 1.3f, 8.0f, 55.0f), "Dim red hooded checkpoint lamp pointing down (practical light)");
    for (float s : {-1.0f, 1.0f})  // tactical markers on the inner faces of the gate posts
        b.part(g, "Gate post marker", P::Sphere, {0.10f, 0.10f, 0.05f}, {0, 0, 0}, {s * (kGateHalfGap + 0.45f), 1.2f, 0.2f + 0.36f},
               s < 0 ? tacRedMark() : tacTealMark(), "Tactical marker light on the gate post (emissive only)");
    // Czech hedgehogs outside the wall.
    for (float x : {-6.5f, 6.5f}) {
        SceneNode* h = b.group(g, "Tank trap", {x, 0.0f, 0.9f}, {0, x * 11.0f, 0}, "Czech hedgehog anti-tank obstacle");
        const Material rust = mats::rustDrum();
        b.part(h, "Trap beam", P::Cube, {1.5f, 0.12f, 0.12f}, {0, 0, 0}, {0, 0.55f, 0}, rust, "Steel angle");
        b.part(h, "Trap beam", P::Cube, {1.5f, 0.12f, 0.12f}, {0, 0, 55}, {0, 0.55f, 0}, rust, "Steel angle");
        b.part(h, "Trap beam", P::Cube, {1.5f, 0.12f, 0.12f}, {0, 90, 35}, {0, 0.55f, 0}, rust, "Steel angle");
    }
}

// ---------------------------------------------------------------------------------------------------------
// Road
// ---------------------------------------------------------------------------------------------------------
void addRoad(Builder& b, SceneNode* root) {
    SceneNode* r = b.group(root, "Road", {0, G, 0}, {0, 0, 0}, "Asphalt road from the inner gate to the outer gate");
    constexpr float z0 = 19.5f, z1 = 35.0f;
    box(b, r, "Asphalt", {2.0f * kRoadHalfWidth, 0.06f, z1 - z0}, {0, 0.03f, 0.5f * (z0 + z1)}, 0, asphaltMat(),
        "Dark tarmac, 4.4 m wide");
    const Material white = paintWhite(), yel = paintYellow();
    for (float s : {-1.0f, 1.0f})
        flush(b, r, "Edge line", {0.12f, 0.012f, z1 - z0 - 0.4f}, {s * (kRoadHalfWidth - 0.25f), 0.066f, 0.5f * (z0 + z1)}, 0,
              white, "Painted road edge");
    for (float z = 20.8f; z < 31.0f; z += 4.0f)
        flush(b, r, "Centre dash", {0.12f, 0.012f, 2.0f}, {0, 0.066f, z + 1.0f}, 0, yel, "Painted centre line dash");
    flush(b, r, "Stop line", {kRoadHalfWidth * 2.0f - 0.5f, 0.012f, 0.4f}, {0, 0.066f, 30.0f}, 0, white,
          "Stop line before the boom barrier");
}

// ---------------------------------------------------------------------------------------------------------
// Helipad
// ---------------------------------------------------------------------------------------------------------
void addHelipad(Builder& b, SceneNode* root) {
    SceneNode* h = site(b, root, "Helipad", kHelipad.x, kHelipad.z, 0, "Concrete landing pad with markings and lights");
    b.part(h, "Helipad disc", P::Cylinder, {2 * kHelipadRadius, 2 * kHelipadRadius, 0.08f}, {90, 0, 0}, {0, 0.04f, 0},
           mats::concrete(), "Flattened cylinder, 10 m diameter");
    const Material white = paintWhite(), yel = paintYellow();
    const float y = 0.086f;
    flush(b, h, "H bar", {0.5f, 0.012f, 3.2f}, {-0.95f, y, 0}, 0, white, "Left bar of the H");
    flush(b, h, "H bar", {0.5f, 0.012f, 3.2f}, {0.95f, y, 0}, 0, white, "Right bar of the H");
    flush(b, h, "H crossbar", {1.5f, 0.012f, 0.5f}, {0, y, 0}, 0, white, "Crossbar of the H");
    for (int i = 0; i < 24; ++i) {  // touchdown circle: 24 short bars
        const float a = kTwoPi * static_cast<float>(i) / 24.0f;
        flush(b, h, "Circle mark", {0.7f, 0.012f, 0.2f}, {3.9f * std::cos(a), y, 3.9f * std::sin(a)},
              degrees(std::atan2(-std::sin(a), std::cos(a)) ), white, "Painted touchdown circle");
    }
    for (int i = 0; i < 4; ++i)  // yellow edge ticks at the compass points
        flush(b, h, "Edge mark", {0.35f, 0.012f, 1.0f},
              {4.6f * std::cos(1.5707963f * static_cast<float>(i)), y, 4.6f * std::sin(1.5707963f * static_cast<float>(i))},
              90.0f * static_cast<float>(i) + 90.0f, yel, "Pad edge marker");
    // 16 blue perimeter lights; three of them are practical lights.
    for (int i = 0; i < 16; ++i) {
        const float a = kTwoPi * (static_cast<float>(i) + 0.5f) / 16.0f;
        const bool practical = (i == 7);
        b.part(h, "Perimeter light", P::Sphere, {0.2f, 0.07f, 0.2f}, {0, 0, 0},
               {4.85f * std::cos(a), 0.07f, 4.85f * std::sin(a)}, practical ? bluePractical() : blueEmissive(),
               practical ? "Blue edge light (practical)" : "Blue edge light (emissive only)");
    }
    // Floodlight on the north-west edge (the side the base looks at the pad from), clear of the rotor disc:
    // lights the parked helicopter's flank instead of leaving it a silhouette against the moon.
    {
        const Vec3 pp{-6.2f, 0.0f, 4.0f};
        const Vec3 head{pp.x, 4.2f, pp.z};
        const Vec3 aim = normalize(Vec3{0.0f, 1.0f, 0.0f} - head);
        post(b, h, "Pad floodlight pole", 0.14f, 4.0f, pp, mats::towerSteel(), "Mast of the helipad floodlight");
        b.part(h, "Pad floodlight housing", P::Cylinder, {0.36f, 0.36f, 0.3f}, alignZ(aim), head, mats::towerSteel(), "Lamp body");
        b.part(h, "Pad floodlight lens", P::Sphere, {0.32f, 0.32f, 0.05f}, alignZ(aim), head + aim * 0.17f,
               hoodedLamp({1.0f, 0.46f, 0.20f}, 2.0f, 13.0f, 38.0f),
               "Dim hooded lamp over the helipad edge (practical spot light)");
    }
}

// ---------------------------------------------------------------------------------------------------------
// Motor pool, depot, light towers
// ---------------------------------------------------------------------------------------------------------
void container(Builder& b, SceneNode* p, const Vec3& pos, float yaw, const Material& m, const char* nm) {
    SceneNode* c = b.group(p, nm, pos, {0, yaw, 0}, "ISO shipping container (20 ft)");
    b.part(c, "Container body", P::Cube, {2.44f, 2.59f, 6.06f}, {0, 0, 0}, {0, 1.295f, 0}, m, "Corrugated steel box");
    for (float s : {-0.55f, 0.55f})
        b.part(c, "Door bar", P::Cylinder, {0.045f, 0.045f, 2.4f}, {90, 0, 0}, {s, 1.3f, 3.06f}, mats::galvanized(),
               "Locking bar of the doors");
    b.part(c, "Door seam", P::Cube, {0.03f, 2.4f, 0.03f}, {0, 0, 0}, {0, 1.3f, 3.06f}, mats::towerSteel(), "Gap between the doors");
    b.part(c, "Corner casting", P::Cube, {0.18f, 0.18f, 0.18f}, {0, 0, 0}, {1.15f, 2.55f, 2.95f}, mats::towerSteel(),
           "Lifting fitting");
}

void truck(Builder& b, SceneNode* p, const Vec3& pos, float yaw) {
    SceneNode* t = b.group(p, "Cargo truck", pos, {0, yaw, 0}, "6x6 tactical truck, parked (local +Z = front)");
    const Material od = oliveFlat(), canv = fabric({0.17f, 0.20f, 0.12f}), rub = blackRubber();
    box(b, t, "Chassis", {1.0f, 0.3f, 7.0f}, {0, 0.75f, -0.3f}, 0, mats::towerSteel(), "Ladder frame");
    box(b, t, "Hood", {2.0f, 0.95f, 1.6f}, {0, 1.45f, 2.9f}, 0, od, "Engine hood");
    box(b, t, "Cab", {2.35f, 1.55f, 1.5f}, {0, 1.85f, 1.35f}, 0, od, "Crew cab");
    box(b, t, "Windscreen", {2.0f, 0.7f, 0.04f}, {0, 2.1f, 2.12f}, 0, darkGlass(), "Windscreen");
    box(b, t, "Bumper", {2.3f, 0.25f, 0.3f}, {0, 0.85f, 3.8f}, 0, mats::towerSteel(), "Front bumper");
    box(b, t, "Cargo bed", {2.4f, 0.2f, 4.2f}, {0, 1.25f, -1.65f}, 0, mats::crateWood(), "Wooden load bed");
    box(b, t, "Bed side", {0.08f, 0.55f, 4.2f}, {-1.2f, 1.6f, -1.65f}, 0, od, "Drop side");
    box(b, t, "Bed side", {0.08f, 0.55f, 4.2f}, {1.2f, 1.6f, -1.65f}, 0, od, "Drop side");
    b.part(t, "Canvas cover", P::Cylinder, {2.3f, 1.9f, 4.0f}, {0, 0, 0}, {0, 1.95f, -1.65f}, canv,
           "Canvas tilt: half-buried cylinder over the bed");
    for (float s : {-1.0f, 1.0f})  // 6 wheels: front axle + two rear axles
        b.part(t, "Wheel", P::Cylinder, {1.05f, 1.05f, 0.4f}, {0, 90, 0}, {s * 1.1f, 0.53f, 2.75f}, rub, "Tyre");
    for (float s : {-1.0f, 1.0f}) {
        b.part(t, "Wheel", P::Cylinder, {1.05f, 1.05f, 0.4f}, {0, 90, 0}, {s * 1.1f, 0.53f, -1.2f}, rub, "Tyre");
        b.part(t, "Wheel", P::Cylinder, {1.05f, 1.05f, 0.4f}, {0, 90, 0}, {s * 1.1f, 0.53f, -2.7f}, rub, "Tyre");
        b.part(t, "Fender", P::Cube, {0.25f, 0.1f, 1.3f}, {0, 0, 0}, {s * 1.1f, 1.12f, 2.75f}, od, "Front mudguard");
    }
    for (float s : {-0.7f, 0.7f})
        b.part(t, "Headlight", P::Sphere, {0.22f, 0.22f, 0.1f}, {0, 0, 0}, {s, 1.55f, 3.72f}, darkGlass(), "Headlamp (off)");
}

void addMotorPool(Builder& b, SceneNode* root) {
    const float yaw = kTankParkYawDeg;
    const float sy = std::sin(radians(yaw)), cy = std::cos(radians(yaw));
    const Vec3 f{sy, 0, cy}, s{cy, 0, -sy};  // forward and right of the parking spot
    SceneNode* mp = b.group(root, "Motor pool", {kMotorPool.x, G, kMotorPool.z}, {0, 0, 0}, "Vehicle park around the tank spot");
    auto at = [&](float so, float fo) { return s * so + f * fo; };  // local offset in the spot frame
    b.setShading(ShadingModel::Flat);
    // Containers (west side, one stacked).
    // (moved west and thinned out in v5 to make room for the tank revetment and the camp bays)
    container(b, mp, at(-7.6f, -1.0f), yaw, oliveCorr(), "Container A");
    container(b, mp, at(-7.6f, -1.0f) + Vec3{0, 2.59f, 0}, yaw, rustCorr(), "Container C (stacked)");
    b.setShading(ShadingModel::Gouraud);
    // Two trucks under the camouflage net.
    truck(b, mp, at(7.6f, 1.4f), yaw);
    truck(b, mp, at(10.6f, 1.4f), yaw + 180.0f);
    // Camouflage net over the two trucks: sagging between poles (>= 3.2 m above the ground), with gaps.
    {
        const float so0 = 5.9f, so1 = 12.3f, fo0 = -3.0f, fo1 = 6.0f;
        const Vec3 c = at(0.5f * (so0 + so1), 0.5f * (fo0 + fo1));
        addCamoNet(b, mp, {c.x, 0.0f, c.z}, yaw, so1 - so0, fo1 - fo0, 3.9f, 0.32f, 0x4e7b01u);
    }
    // Floodlight pole behind the tank spot, aimed at the parking box.
    {
        const Vec3 pp = at(-5.6f, 5.6f);  // front-left corner of the park (north-west of the tank), clear of the 6 x 9 m box
        const Vec3 head{pp.x, 5.4f, pp.z};
        const Vec3 target = Vec3{0.0f, 1.0f, 0.0f};
        const Vec3 aim = normalize(target - head);
        post(b, mp, "Floodlight pole", 0.16f, 5.2f, pp, mats::towerSteel(), "Mast of the motor-pool floodlight");
        box(b, mp, "Floodlight arm", {0.1f, 0.1f, 0.5f}, {pp.x, 5.3f, pp.z}, yaw, mats::towerSteel(), "Lamp bracket");
        b.part(mp, "Floodlight housing", P::Cylinder, {0.42f, 0.42f, 0.36f}, alignZ(aim), head, mats::towerSteel(), "Lamp body");
        b.part(mp, "Floodlight lens", P::Sphere, {0.38f, 0.38f, 0.06f}, alignZ(aim), head + aim * 0.2f,
               hoodedLamp({1.0f, 0.50f, 0.22f}, 2.4f, 13.0f, 42.0f),
               "Dim hooded lamp over the motor pool (practical spot light)");
    }
    // Parking spot marking: four flush yellow bars around the 9 x 6 m tank spot.
    SceneNode* mk = b.group(root, "Tank spot marking", {kMotorPool.x, G, kMotorPool.z}, {0, yaw, 0}, "Painted parking box");
    flush(b, mk, "Spot line", {6.0f, 0.012f, 0.12f}, {0, 0.012f, 4.5f}, 0, paintYellow(), "Front line");
    flush(b, mk, "Spot line", {6.0f, 0.012f, 0.12f}, {0, 0.012f, -4.5f}, 0, paintYellow(), "Rear line");
    flush(b, mk, "Spot line", {0.12f, 0.012f, 9.0f}, {3.0f, 0.012f, 0}, 0, paintYellow(), "Right line");
    flush(b, mk, "Spot line", {0.12f, 0.012f, 9.0f}, {-3.0f, 0.012f, 0}, 0, paintYellow(), "Left line");
}

[[maybe_unused]] void lightTower(Builder& b, SceneNode* p, float x, float z, float yaw, bool /*practical: the tower stays dark*/) {
    SceneNode* t = b.group(p, "Light tower", {x, 0, z}, {0, yaw, 0}, "Trailer-mounted mast with four lamp heads");
    const Material steel = mats::towerSteel(), od = oliveFlat();
    box(b, t, "Trailer", {1.5f, 0.5f, 2.4f}, {0, 0.55f, 0}, 0, od, "Generator trailer");
    for (float s : {-1.0f, 1.0f}) b.part(t, "Trailer wheel", P::Cylinder, {0.7f, 0.7f, 0.2f}, {0, 90, 0}, {s * 0.85f, 0.35f, 0}, blackRubber(), "Tyre");
    for (const Vec3 c : {Vec3{1, 0, 1}, Vec3{-1, 0, 1}, Vec3{1, 0, -1}, Vec3{-1, 0, -1}})
        strut(b, t, "Stabiliser", P::Cylinder, {c.x * 0.7f, 0.5f, c.z * 1.0f}, {c.x * 1.3f, 0.0f, c.z * 1.4f}, 0.07f, steel, "Outrigger");
    post(b, t, "Mast", 0.14f, 6.5f, {0, 0.8f, 0}, steel, "Telescopic mast");
    box(b, t, "Lamp bar", {1.5f, 0.1f, 0.12f}, {0, 7.35f, 0}, 0, steel, "Cross bar of the lamp heads");
    for (int i = 0; i < 4; ++i) {
        const float x0 = -0.55f + 0.37f * static_cast<float>(i);
        box(b, t, "Lamp head", {0.3f, 0.22f, 0.2f}, {x0, 7.3f, 0.12f}, 0, steel, "Lamp housing");
        b.part(t, "Lamp lens", P::Cube, {0.26f, 0.18f, 0.03f}, {25, 0, 0}, {x0, 7.28f, 0.23f}, darkGlass(),
               "Flood lens, switched off (light discipline)");
    }
}

void addDepot(Builder& b, SceneNode* root) {
    SceneNode* d = site(b, root, "Fuel depot", kFuelDepot.x, kFuelDepot.z, 0, "Bladders in a sandbag dike, pump, generator");
    const Material rub = blackRubber(), sand = mats::sandbag(), od = oliveFlat();
    // Dike: four low earth/sand walls 8 x 5 m.
    box(b, d, "Dike wall", {8.4f, 0.55f, 0.5f}, {0, 0.275f, 2.5f}, 0, sand, "Containment dike (north)");
    box(b, d, "Dike wall", {8.4f, 0.55f, 0.5f}, {0, 0.275f, -2.5f}, 0, sand, "Containment dike (south)");
    box(b, d, "Dike wall", {0.5f, 0.55f, 5.0f}, {4.0f, 0.275f, 0}, 0, sand, "Containment dike (east)");
    box(b, d, "Dike wall", {0.5f, 0.55f, 5.0f}, {-4.0f, 0.275f, 0}, 0, sand, "Containment dike (west)");
    for (float x : {-2.0f, 2.0f}) {
        b.part(d, "Fuel bladder", P::Sphere, {3.4f, 1.1f, 2.1f}, {0, 0, 0}, {x, 0.5f, 0.3f}, rub, "Collapsible 10,000 l fuel bladder");
        b.part(d, "Bladder fill cap", P::Cylinder, {0.25f, 0.25f, 0.1f}, {90, 0, 0}, {x, 1.02f, 0.3f}, mats::galvanized(), "Fill port");
    }
    // Pump skid and hoses.
    box(b, d, "Pump skid", {0.9f, 0.4f, 0.6f}, {0, 0.2f, -1.7f}, 0, od, "Transfer pump");
    strut(b, d, "Fuel hose", P::Cylinder, {-0.4f, 0.4f, -1.7f}, {-2.0f, 0.5f, -0.7f}, 0.08f, rub, "Suction hose");
    strut(b, d, "Fuel hose", P::Cylinder, {0.4f, 0.4f, -1.7f}, {2.0f, 0.5f, -0.7f}, 0.08f, rub, "Delivery hose");
    for (int i = 0; i < 4; ++i)
        b.part(d, "Fuel drum", P::Cylinder, {0.55f, 0.55f, 0.85f}, {90, 0, 0}, {-3.2f + 0.6f * static_cast<float>(i), 0.43f, -3.6f},
               i % 2 ? mats::rustDrum() : mats::oliveDrum(), "Drum of lubricant");
    // Generator (east of the dike) with exhaust, panel, cables.
    SceneNode* g = b.group(d, "Generator", {7.2f, 0, -0.6f}, {0, 90, 0}, "Diesel generator set, 60 kW");
    box(b, g, "Generator housing", {1.0f, 1.1f, 2.0f}, {0, 0.7f, 0}, 0, od, "Sound-proof canopy");
    box(b, g, "Generator skid", {1.2f, 0.15f, 2.2f}, {0, 0.07f, 0}, 0, mats::towerSteel(), "Base frame");
    post(b, g, "Exhaust stack", 0.1f, 0.7f, {0.3f, 1.25f, 0.8f}, mats::towerSteel(), "Exhaust");
    box(b, g, "Control panel", {0.5f, 0.4f, 0.04f}, {0, 0.85f, 1.03f}, 0, makeMaterial({0.05f, 0.07f, 0.06f}, 0.3f, Surface::Screen, 0.4f, 0.0f, 0.4f), "Panel with display");
    strut(b, g, "Power cable", P::Cylinder, {0.0f, 0.2f, 1.1f}, {0.0f, 0.05f, 3.4f}, 0.06f, rub, "Cable to the light tower");
    // (the dark light tower of v4 was removed in v5: light discipline, and it blocked the south tank bays)
}

// ---------------------------------------------------------------------------------------------------------
// Barracks
// ---------------------------------------------------------------------------------------------------------
void addBarracks(Builder& b, SceneNode* root) {
    SceneNode* bar = b.group(root, "Barracks", {0, G, 0}, {0, 0, 0}, "Three Quonset huts with sandbag blast walls");
    const float a0 = std::atan2(kBarracks.z, kBarracks.x);
    constexpr float kHutR = 27.0f, kRad = 2.2f, kLen = 8.5f, kSpacing = 6.2f;
    const Material tin = corrugated({0.30f, 0.34f, 0.24f}), ac = mats::galvanized(), steel = mats::towerSteel();
    for (int k = -1; k <= 1; ++k) {
        const float a = a0 + static_cast<float>(k) * kSpacing / kHutR;
        const Vec3 ctr{kHutR * std::cos(a), 0.0f, kHutR * std::sin(a)};
        const float yaw = degrees(std::atan2(-std::cos(a), -std::sin(a)));  // local +Z = inward (towards the compound)
        char nm[32];
        std::snprintf(nm, sizeof(nm), "Quonset hut %d", k + 2);
        SceneNode* h = b.group(bar, nm, ctr, {0, yaw, 0}, "Half-buried cylinder hut; local +Z faces the compound");
        b.part(h, "Hut shell", P::Cylinder, {2 * kRad, 2 * kRad, kLen}, {0, 0, 0}, {0, 0.15f, 0}, tin, "Corrugated half-cylinder roof, lower half buried");
        const float zEnd = 0.5f * kLen + 0.03f;
        b.part(h, "Hut door", P::Cube, {0.95f, 1.9f, 0.08f}, {0, 0, 0}, {-0.5f, 0.95f, zEnd}, makeMaterial({0.07f, 0.09f, 0.07f}, 0.3f, Surface::Paint, 0.6f, 0.2f), "Entrance door in the end wall");
        b.part(h, "Hut door step", P::Cube, {1.2f, 0.18f, 0.6f}, {0, 0, 0}, {-0.5f, 0.09f, zEnd + 0.3f}, mats::concrete(), "Concrete step");
        b.part(h, "Hut window", P::Cube, {0.7f, 0.5f, 0.08f}, {0, 0, 0}, {0.95f, 1.35f, zEnd}, darkGlass(), "Blacked-out window");
        b.part(h, "Hut AC unit", P::Cube, {0.9f, 0.6f, 0.4f}, {0, 0, 0}, {0.9f, 0.75f, -zEnd - 0.2f}, ac, "Air conditioner on the rear wall");
        b.part(h, "Hut AC fan", P::Cylinder, {0.45f, 0.45f, 0.05f}, {0, 0, 0}, {0.9f, 0.75f, -zEnd - 0.43f}, steel, "Condenser fan");
        // Windows along both flanks (lit in the two outer huts) and a practical door lamp above the entrance.
        for (float side : {-1.0f, 1.0f})
            for (float wz : {-2.4f, 0.0f, 2.4f}) {
                const float th = radians(32.0f);
                b.part(h, "Hut flank window", P::Cube, {0.7f, 0.03f, 0.5f}, {0, 0, side * (degrees(th) - 90.0f)},
                       {side * (kRad + 0.01f) * std::cos(th), 0.15f + (kRad + 0.01f) * std::sin(th), wz}, darkGlass(),
                       "Blacked-out window in the hut flank");
            }
        box(b, h, "Hut door lamp hood", {0.3f, 0.04f, 0.26f}, {-0.5f, 2.2f, zEnd + 0.16f}, 0, steel, "Hood over the door lamp");
        b.part(h, "Hut door lamp", P::Sphere, {0.14f, 0.14f, 0.06f}, {90, 0, 0}, {-0.5f, 2.13f, zEnd + 0.16f},
               k == 0 ? hoodedLamp({1.0f, 0.10f, 0.05f}, 0.8f, 5.0f, 60.0f) : tacRedMark(),
               k == 0 ? "Dim red hooded lamp above the door (practical light)" : "Dim red door marker (emissive only)");
        b.part(h, "Door step marker", P::Sphere, {0.09f, 0.04f, 0.09f}, {0, 0, 0}, {-0.1f, 0.2f, zEnd + 0.55f}, tacTealMark(),
               "Blue-green tactical marker on the step (emissive only)");
        post(b, h, "Hut stove pipe", 0.12f, 0.8f, {-0.6f, 2.0f, -1.5f}, steel, "Chimney");
        // Blast wall: three sandbag courses, 1.8 m in front of the door.
        const float wz = zEnd + 2.0f;
        for (int r = 0; r < 3; ++r)
            box(b, h, "Blast wall course", {3.0f - 0.3f * static_cast<float>(r), 0.4f, 0.6f}, {0, 0.2f + 0.4f * static_cast<float>(r), wz}, 0, mats::sandbag(), "Sandbag blast wall in front of the door");
    }
}

// Tyre tracks: pairs of dark flush strips along the service ring from the road to the motor pool and the helipad.
void addTyreTracks(Builder& b, SceneNode* root) {
    SceneNode* t = b.group(root, "Tyre tracks", {0, G, 0}, {0, 0, 0}, "Worn dirt tracks of vehicles driving the service ring");
    const Material mud = makeMaterial({0.035f, 0.03f, 0.024f}, 0.2f, Surface::Plain, 0.95f, 0.0f);
    auto arc = [&](float a0, float a1, float rad, float w) {
        const int n = std::max(1, static_cast<int>(std::fabs(a1 - a0) / 6.0f));
        for (int i = 0; i < n; ++i) {
            const float a = radians(a0 + (a1 - a0) * (static_cast<float>(i) + 0.5f) / static_cast<float>(n));
            const float len = radians(std::fabs(a1 - a0)) / static_cast<float>(n) * rad + 0.15f;
            const float yaw = degrees(std::atan2(-std::sin(a), std::cos(a)));
            for (float off : {-w, w})
                box(b, t, "Tyre track", {0.24f, 0.012f, len}, {(rad + off) * std::cos(a), 0.006f, (rad + off) * std::sin(a)}, yaw, mud,
                    "Rut of a vehicle tyre in the packed dirt");
        }
    };
    arc(92.0f, 215.0f, 21.4f, 0.6f);  // road -> west -> motor pool
    arc(88.0f, 58.0f, 21.4f, 0.6f);   // road -> east towards the helipad
}

// ---------------------------------------------------------------------------------------------------------
// Sandbag ring around the AA pit, fenced radar, pads
// ---------------------------------------------------------------------------------------------------------
void addAaSurround(Builder& b, SceneNode* root) {
    SceneNode* s = site(b, root, "AA gun surround", kAaGunSite.x, kAaGunSite.z, 0, "Concrete pad and sandbag ring (the gun is built separately)");
    b.part(s, "AA pad", P::Cylinder, {7.0f, 7.0f, 0.04f}, {90, 0, 0}, {0, 0.02f, 0}, mats::concrete(), "Flush concrete pad under the gun");
    const float inward = std::atan2(-kAaGunSite.z, -kAaGunSite.x);  // opening faces the base centre
    const int n = 22;
    for (int r = 0; r < 2; ++r)
        for (int i = 0; i < n; ++i) {
            const float a = kTwoPi * (static_cast<float>(i) + 0.5f * static_cast<float>(r)) / static_cast<float>(n);
            float d = std::fabs(a - inward);
            d = std::min(d, kTwoPi - d);
            if (d < 0.45f) continue;  // entrance
            b.part(s, "Sandbag", P::Cylinder, {0.26f, 0.17f, 0.52f}, {0, degrees(std::atan2(-std::sin(a), std::cos(a))), 0},
                   {3.7f * std::cos(a), 0.09f + 0.15f * static_cast<float>(r), 3.7f * std::sin(a)}, mats::sandbag(), "Ring wall");
        }
    for (int i = 0; i < 3; ++i) {
        const Vec3 p{4.9f * std::cos(inward + 0.5f + 0.45f * static_cast<float>(i)), 0, 4.9f * std::sin(inward + 0.5f + 0.45f * static_cast<float>(i))};
        box(b, s, "Ammo crate", {0.7f, 0.4f, 0.4f}, {p.x, 0.2f, p.z}, 20.0f * static_cast<float>(i), mats::crateWood(), "Stack of shell crates");
    }
}

void addRadarSurround(Builder& b, SceneNode* root) {
    SceneNode* s = site(b, root, "Radar surround", kRadarSite.x, kRadarSite.z, 0, "Concrete pad and chain-link fence (the radar is built separately)");
    box(b, s, "Radar pad", {5.4f, 0.04f, 5.4f}, {0, 0.02f, 0}, 0, mats::concrete(), "Flush concrete pad under the radar");
    const float h = 3.3f;
    const Material steel = mats::galvanized(), wire = mats::wire();
    const Vec3 corner[4] = {{h, 0, h}, {h, 0, -h}, {-h, 0, -h}, {-h, 0, h}};
    for (int i = 0; i < 4; ++i) {
        const Vec3 a = corner[i], c = corner[(i + 1) % 4];
        const int bays = 3;
        for (int k = 0; k <= bays; ++k) {
            const Vec3 p = a + (c - a) * (static_cast<float>(k) / bays);
            post(b, s, "Fence post", 0.08f, 2.0f, p, steel, "Fence post");
            if (k == bays) continue;
            const Vec3 q = a + (c - a) * (static_cast<float>(k + 1) / bays);
            if (i == 2 && k == 1) continue;  // gate opening on the west side
            for (float y : {0.4f, 1.0f, 1.7f})
                strut(b, s, "Fence wire", P::Cylinder, p + Vec3{0, y, 0}, q + Vec3{0, y, 0}, 0.012f, wire, "Chain-link strand");
        }
    }
    box(b, s, "Radar warning sign", {0.6f, 0.4f, 0.03f}, {-h - 0.05f, 1.6f, -1.1f}, 90, mats::warningSign(), "Radiation hazard sign");
}

// Compound-style obstacle flags: thin parts are not UAV obstacles, nothing outside r = 20 is an occluder.
// ---------------------------------------------------------------------------------------------------------
// v7 detail pass (WP-M): appended after every other part so all existing node ids stay stable. The parts are
// attached to the groups built above (found by name) and are surface detail only (finalizeFlags: no obstacles).
// ---------------------------------------------------------------------------------------------------------
void findGroups(SceneNode& n, const std::string& prefix, std::vector<SceneNode*>& out) {
    if (!n.isLeaf() && n.name.rfind(prefix, 0) == 0) out.push_back(&n);
    for (auto& c : n.children) findGroups(*c, prefix, out);
}

void addDetailPass(Builder& b, SceneNode* root) {
    const Material steel = mats::towerSteel(), worn = reworked(shade(oliveFlat(), 1.45f), 0.45f, 0.35f);
    std::vector<SceneNode*> g;
    findGroups(*root, "Cargo truck", g);
    for (SceneNode* t : g) {
        for (float z : {2.75f, -1.2f, -2.7f})
            for (float s : {-1.0f, 1.0f})
                b.part(t, "Wheel hub", P::Cylinder, {0.45f, 0.45f, 0.42f}, {0, 90, 0}, {s * 1.1f, 0.53f, z}, steel, "Steel wheel hub");
        for (float s : {-1.0f, 1.0f}) {
            b.part(t, "Cab side window", P::Cube, {0.02f, 0.5f, 0.7f}, {0, 0, 0}, {s * 1.18f, 2.2f, 1.4f}, darkGlass(), "Door window");
            b.part(t, "Mirror", P::Cube, {0.05f, 0.3f, 0.18f}, {0, 0, 0}, {s * 1.32f, 2.2f, 1.95f}, steel, "Wing mirror");
        }
        b.part(t, "Radiator grille", P::Cube, {1.0f, 0.6f, 0.02f}, {0, 0, 0}, {0, 1.45f, 3.71f}, shade(steel, 0.5f), "Radiator grille");
        b.part(t, "Exhaust stack", P::Cylinder, {0.12f, 0.12f, 1.0f}, {90, 0, 0}, {1.05f, 2.5f, 0.5f}, shade(steel, 0.6f),
               "Vertical exhaust behind the cab");
    }
    g.clear();
    findGroups(*root, "Container ", g);
    for (SceneNode* c : g) {
        const Vec3 corners[5] = {{-1.15f, 2.55f, 2.95f}, {1.15f, 2.55f, -2.95f}, {-1.15f, 2.55f, -2.95f}, {1.15f, 0.09f, 2.95f},
                                 {-1.15f, 0.09f, 2.95f}};
        for (const Vec3& p : corners)
            b.part(c, "Corner casting", P::Cube, {0.18f, 0.18f, 0.18f}, {0, 0, 0}, p, steel, "Lifting / stacking fitting");
        b.part(c, "Container ID", P::Cube, {0.01f, 0.3f, 1.4f}, {0, 0, 0}, {1.226f, 2.2f, 1.6f}, decal({0.80f, 0.80f, 0.76f}, 0.30f),
               "Painted owner code and number");
    }
    g.clear();
    findGroups(*root, "Guard tower ", g);
    constexpr float kPlat = 4.0f;
    for (SceneNode* t : g) {
        for (float y : {0.36f, 0.76f})
            b.part(t, "Sandbag course", P::Cylinder, {0.1f, 0.1f, 2.8f}, {0, 90, 0}, {0, kPlat + y, 1.45f}, mats::sandbag(),
                   "Row of filled bags (bulging course line)");
        b.part(t, "MG ammo box", P::Cube, {0.26f, 0.18f, 0.12f}, {0, 0, 0}, {0.8f, kPlat + 1.33f, 1.15f}, oliveFlat(), "Ammunition can");
        b.part(t, "Field radio", P::Cube, {0.3f, 0.35f, 0.15f}, {0, 0, 0}, {-0.85f, kPlat + 0.32f, 0.85f}, shade(oliveFlat(), 0.6f),
               "Tower field radio");
        b.part(t, "Radio LED", P::Sphere, {0.035f, 0.035f, 0.02f}, {0, 0, 0}, {-0.85f, kPlat + 0.42f, 0.772f},
               indicator({0.2f, 1.0f, 0.3f}, 0.8f), "Radio power LED (emissive only)");
    }
    g.clear();
    findGroups(*root, "Guard booth", g);
    for (SceneNode* bo : g) {
        b.part(bo, "Door handle", P::Cube, {0.15f, 0.04f, 0.05f}, {0, 0, 0}, {0.65f, 1.1f, 1.16f}, steel, "Door handle");
        for (float z : {-1.35f, 1.35f})
            bevel(b, bo, "Booth roof edge", {-1.35f, 2.56f, z}, {1.35f, 2.56f, z}, 0.12f, worn, "Rounded roof edge");
        b.part(bo, "Checkpoint placard", P::Cube, {0.01f, 0.3f, 0.8f}, {0, 0, 0}, {-1.115f, 0.85f, 0.0f}, paintYellow(),
               "Checkpoint instructions placard");
    }
}

void finalizeFlags(SceneNode& n) {
    if (n.isLeaf()) {
        float s[3] = {std::fabs(n.local.scale.x), std::fabs(n.local.scale.y), std::fabs(n.local.scale.z)};
        std::sort(s, s + 3);
        n.obstacle = s[2] >= 0.6f && s[1] >= 0.18f;
        n.occluder = false;
    }
    for (auto& c : n.children) finalizeFlags(*c);
}

}  // namespace

std::vector<ClearZone> baseClearZones() {
    return {{"helipad", kHelipad.x, kHelipad.z, kHelipadRadius, kHelipadRadius, 0.0f, true},
            {"radar site", kRadarSite.x, kRadarSite.z, 2.5f, 2.5f, 0.0f, false},
            {"AA gun pit", kAaGunSite.x, kAaGunSite.z, 3.0f, 3.0f, 0.0f, true},
            {"tank spot", kMotorPool.x, kMotorPool.z, 3.0f, 4.5f, kTankParkYawDeg, false},
            {"road", 0.0f, 25.0f, kRoadHalfWidth, 5.5f, 0.0f, false},
            {"trench strip", 0.0f, 8.5f, 2.5f, 5.0f, 0.0f, false}};
}

std::vector<ClearZone> campClearZones() {
    std::vector<ClearZone> z = baseClearZones();
    for (const camp::Site& s : camp::allSites()) {
        const camp::PlanRect p = camp::planRect(s);
        z.push_back({s.name, p.cx, p.cz, p.halfX, p.halfZ, p.yawDeg, false});
    }
    const camp::PlanRect pool = camp::poolPlanRect();
    z.push_back({"tank park revetment", pool.cx, pool.cz, pool.halfX, pool.halfZ, pool.yawDeg, false});
    return z;
}

// ---------------------------------------------------------------------------------------------------------
// buildBase
// ---------------------------------------------------------------------------------------------------------
BaseRig buildBase() {
    BaseRig rig;
    Builder b("Base", "BASE", ShadingModel::Gouraud);
    rig.root = b.root("Military base", {0, 0, 0}, "Outer apron of the forward operating base");
    SceneNode* r = rig.root.get();
    b.setShading(ShadingModel::Flat);
    addPerimeter(b, r);
    b.setShading(ShadingModel::Gouraud);
    for (int i = 0; i < 4; ++i) addTower(b, r, kTowerAngles[i], i);
    addGate(b, r);
    addRoad(b, r);
    addHelipad(b, r);
    addMotorPool(b, r);
    addDepot(b, r);
    addBarracks(b, r);
    addTyreTracks(b, r);
    addAaSurround(b, r);
    addRadarSurround(b, r);
    addDetailPass(b, r);   // v7 (WP-M), last: the ids above stay stable
    // Static parts of the poles of the animated objects are inside their own roots.
    finalizeFlags(*r);

    rig.objects.push_back(std::make_unique<GateBarrier>());
    rig.objects.push_back(std::make_unique<Windsock>());
    rig.objects.push_back(std::make_unique<WavingFlag>());
    return rig;
}


// ---------------------------------------------------------------------------------------------------------
// GateBarrier
// ---------------------------------------------------------------------------------------------------------
namespace {
constexpr float kPivotX = 2.9f, kPivotZ = 31.6f, kPivotH = 1.05f, kArmLen = 5.3f;
}

Vec3 GateBarrier::pivotWorld() const { return {kPivotX, G + kPivotH, kPivotZ}; }

GateBarrier::GateBarrier() {
    Builder b("Boom barrier", "GATEB", ShadingModel::Gouraud);
    root_ = b.root("Boom barrier", {kPivotX, G, kPivotZ}, "Counterweighted boom at the outer gate (user controlled)");
    SceneNode* r = root_.get();
    const Material steel = mats::towerSteel(), conc = mats::concrete();
    box(b, r, "Barrier foundation", {0.9f, 0.12f, 0.9f}, {0, 0.06f, 0}, 0, conc, "Concrete footing");
    box(b, r, "Barrier housing", {0.5f, 1.05f, 0.5f}, {0, 0.64f, 0}, 0, paintYellow(), "Drive housing of the boom");
    box(b, r, "Housing door", {0.3f, 0.5f, 0.02f}, {0, 0.5f, 0.26f}, 0, steel, "Service hatch");
    // Receiving post on the far side of the road.
    box(b, r, "Boom receiver", {0.3f, 0.95f, 0.3f}, {-kArmLen - 0.3f + 0.15f, 0.475f, 0}, 0, paintYellow(), "Rest for the lowered boom");
    box(b, r, "Boom cradle", {0.36f, 0.08f, 0.2f}, {-kArmLen - 0.3f + 0.15f, 0.98f, 0}, 0, steel, "V cradle");

    arm_ = b.group(r, "Boom arm", {0, kPivotH, 0}, {0, 0, 0}, "Arm pivot (horizontal axis along Z)");
    arm_->animation = "Rotates 0..85 deg about the pivot (roll), 2.5 s, smoothstep easing; lamp blinks 3 Hz while moving";
    for (int i = 0; i < 10; ++i)
        box(b, arm_, i % 2 ? "Boom stripe (white)" : "Boom stripe (red)", {0.5f, 0.12f, 0.10f},
            {-0.3f - 0.25f - 0.5f * static_cast<float>(i), 0, 0}, 0, i % 2 ? paintWhite() : paintRed(), "Red / white boom segment");
    box(b, arm_, "Counterweight arm", {0.9f, 0.08f, 0.08f}, {0.35f, 0, 0}, 0, steel, "Tail of the arm");
    box(b, arm_, "Counterweight", {0.3f, 0.34f, 0.26f}, {0.62f, -0.12f, 0}, 0, mats::concrete(), "Concrete counterweight");
    b.part(arm_, "Pivot bolt", P::Cylinder, {0.12f, 0.12f, 0.6f}, {0, 0, 0}, {0, 0, 0}, steel, "Pivot axle along Z");
    armLamp_ = b.part(arm_, "Boom tip lamp", P::Sphere, {0.12f, 0.12f, 0.12f}, {0, 0, 0}, {-kArmLen, 0.12f, 0}, makeMaterial({1.0f, 0.3f, 0.05f}, 0.4f, Surface::Glass, 0.1f, 0.0f, 0.1f), "Amber lamp at the tip");
    Material lamp = asLight(makeMaterial({1.0f, 0.15f, 0.03f}, 0.45f, Surface::Glass, 0.1f, 0.0f, 0.1f), 1.0f, 4.5f);
    lamp_ = b.part(r, "Warning lamp", P::Sphere, {0.22f, 0.22f, 0.22f}, {0, 0, 0}, {0, 1.3f, 0}, lamp, "Amber warning lamp (practical light), blinks while moving");
    lamp_->animation = "Blinks 3 Hz while the boom moves";
    finalizeFlags(*r);
    arm_->local.rotationDeg = {0, 0, 0};
}

void GateBarrier::update(const WorldContext& w, const ControlInput* in) {
    clock_ += w.dt;
    if (in && (in->actionPressed || in->firePressed)) target_ = target_ > 0.5f ? 0.0f : 1.0f;
    const float step = w.dt / kTravelSeconds;
    if (t_ < target_) t_ = std::min(t_ + step, target_);
    else if (t_ > target_) t_ = std::max(t_ - step, target_);
    angle_ = kMaxAngleDeg * smooth01(t_);
    arm_->local.rotationDeg = {0, 0, -angle_};  // negative roll lifts the arm that extends along -X
    const bool blink = moving() && std::fmod(clock_ * 3.0f, 1.0f) < 0.5f;
    lamp_->material.emissive = blink ? 1.0f : (moving() ? 0.0f : 0.12f);
    armLamp_->material.emissive = blink ? 1.0f : 0.1f;
}

CameraHint GateBarrier::camera(bool) const {
    CameraHint h;
    h.eye = {kPivotX + 4.5f, G + 1.9f, kPivotZ - 6.0f};
    h.target = {-0.5f, G + 1.4f, kPivotZ};
    h.fovDeg = 55.0f;
    return h;
}

std::string GateBarrier::status() const {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "BOOM %s  ANGLE %02.0f DEG", moving() ? (wantsOpen() ? "RAISING" : "LOWERING") : (wantsOpen() ? "OPEN" : "CLOSED"), angle_);
    return buf;
}

std::vector<Box> GateBarrier::dynamicObstacles() const {
    if (angle_ >= kBlockingAngleDeg) return {};
    const float rise = kArmLen * std::sin(radians(angle_));
    return {Box{{kPivotX - kArmLen - 0.3f, G + kPivotH - 0.1f, kPivotZ - 0.1f},
                {kPivotX + 0.3f, G + kPivotH + 0.1f + rise, kPivotZ + 0.1f}}};
}

// ---------------------------------------------------------------------------------------------------------
// Windsock
// ---------------------------------------------------------------------------------------------------------
Windsock::Windsock() {
    Builder b("Windsock", "WSOCK", ShadingModel::Gouraud);
    root_ = b.root("Windsock", {12.5f, G, 22.5f}, "Wind indicator beside the road (animated scenery)");
    SceneNode* r = root_.get();
    const Material steel = mats::galvanized();
    box(b, r, "Windsock base", {0.5f, 0.08f, 0.5f}, {0, 0.04f, 0}, 0, mats::concrete(), "Footing");
    post(b, r, "Windsock pole", 0.14f, 3.0f, {0, 0, 0}, steel, "Lower pole");
    post(b, r, "Windsock pole", 0.10f, 3.1f, {0, 3.0f, 0}, steel, "Upper pole");
    swivel_ = b.group(r, "Sock swivel", {0, 6.0f, 0}, {0, 0, 0}, "Turns the sock downwind");
    swivel_->animation = "Yaw follows the wind direction (200 deg +-35, slow drift)";
    for (int k = 0; k < 6; ++k) {  // mouth hoop: hexagon of struts
        const float a0 = radians(60.0f * static_cast<float>(k)), a1 = radians(60.0f * static_cast<float>(k + 1));
        strut(b, swivel_, "Mouth hoop", P::Cylinder, {0.36f * std::cos(a0), 0.36f * std::sin(a0), 0}, {0.36f * std::cos(a1), 0.36f * std::sin(a1), 0}, 0.025f, steel, "Hoop holding the sock open");
    }
    strut(b, swivel_, "Hoop spoke", P::Cylinder, {0, -0.36f, 0}, {0, -0.05f, 0}, 0.025f, steel, "Hoop to pole bracket");
    const float dia[5] = {0.72f, 0.60f, 0.48f, 0.38f, 0.28f};
    const float len = 0.55f;
    SceneNode* parent = swivel_;
    for (int k = 0; k < 5; ++k) {
        SceneNode* seg = b.group(parent, "Sock segment", {0, 0, k == 0 ? 0.0f : len}, {0, 0, 0}, "Cone segment; child of the previous one");
        seg->animation = "Droops and flutters with a phase lag down the chain";
        b.part(seg, "Sock cone", P::Cylinder, {dia[k], dia[k], len}, {0, 0, 0}, {0, 0, 0.5f * len},
               fabric(k % 2 == 0 ? Vec3{0.62f, 0.16f, 0.03f} : Vec3{0.62f, 0.60f, 0.52f}), "Fabric band of the windsock");
        segments_.push_back(seg);
        parent = seg;
    }
    finalizeFlags(*r);
    update(WorldContext{}, nullptr);
}

void Windsock::update(const WorldContext& w, const ControlInput*) {
    const float t = w.time, wind = windStrength(t);
    swivel_->local.rotationDeg = {0, windDirectionDeg(t), 0};
    for (size_t k = 0; k < segments_.size(); ++k) {
        const float kf = static_cast<float>(k);
        const float pitch = 20.0f * (1.0f - wind) + 2.5f + 2.0f * wind * std::sin(2.3f * t - 0.9f * kf);
        const float yaw = 4.0f * wind * std::sin(3.1f * t - 0.8f * kf);
        segments_[k]->local.rotationDeg = {pitch, yaw, 0};
    }
}

CameraHint Windsock::camera(bool) const {
    CameraHint h;
    h.eye = {9.0f, G + 3.0f, 17.0f};
    h.target = {12.5f, G + 5.5f, 22.5f};
    return h;
}
std::string Windsock::status() const { return "WINDSOCK"; }
Vec3 Windsock::position() const { return {12.5f, G + 6.0f, 22.5f}; }

// ---------------------------------------------------------------------------------------------------------
// Flag
// ---------------------------------------------------------------------------------------------------------
WavingFlag::WavingFlag() {
    Builder b("Flag", "FLAG", ShadingModel::Gouraud);
    root_ = b.root("Flagpole and flag", {kFlagpole.x, G, kFlagpole.z}, "Flag on the parade-ground pole (animated scenery)");
    SceneNode* r = root_.get();
    const Material steel = mats::galvanized();
    box(b, r, "Flagpole base", {0.7f, 0.2f, 0.7f}, {0, 0.1f, 0}, 0, mats::concrete(), "Concrete footing");
    post(b, r, "Flagpole lower", 0.16f, 4.5f, {0, 0.2f, 0}, steel, "Lower pole section");
    post(b, r, "Flagpole upper", 0.11f, 4.5f, {0, 4.7f, 0}, steel, "Upper pole section");
    b.part(r, "Flagpole finial", P::Sphere, {0.24f, 0.24f, 0.24f}, {0, 0, 0}, {0, 9.32f, 0}, makeMaterial({0.6f, 0.45f, 0.12f}, 0.3f, Surface::Metal, 0.3f, 0.9f), "Gilded ball");
    for (float s : {-1.0f, 1.0f}) {
        post(b, r, "Halyard", 0.015f, 7.4f, {0.1f * s, 1.5f, 0.12f}, mats::wire(), "Rope run along the pole");
        box(b, r, "Cleat", {0.08f, 0.05f, 0.05f}, {0.1f * s, 1.6f, 0.15f}, 0, steel, "Rope cleat");
    }
    // Spotlight at the base, aimed at the flag (practical spot).
    const Vec3 head{0.0f, 0.3f, 1.9f};
    const Vec3 aim = normalize(Vec3{0.0f, 8.0f - head.y, -1.2f - head.z});
    b.part(r, "Flag spot housing", P::Cylinder, {0.34f, 0.34f, 0.3f}, alignZ(aim), head, mats::towerSteel(), "Ground spotlight body");
    b.part(r, "Flag spot lens", P::Sphere, {0.3f, 0.3f, 0.05f}, alignZ(aim), head + aim * 0.16f,
           darkGlass(), "Flag spotlight lens, switched off (light discipline)");
    box(b, r, "Flag spot stand", {0.2f, 0.12f, 0.2f}, {0, 0.06f, 1.9f}, 0, mats::concrete(), "Spot footing");

    hoist_ = b.group(r, "Flag hoist", {0.13f, 8.95f, 0.0f}, {0, 0, 0}, "Attachment to the halyard; turns downwind");
    hoist_->animation = "Yaw follows the wind; roll droops the flag when the wind is weak";
    const Material bands[3] = {fabric({0.65f, 0.05f, 0.05f}), fabric({0.75f, 0.74f, 0.66f}), fabric({0.06f, 0.12f, 0.40f})};
    SceneNode* parent = hoist_;
    for (int i = 0; i < kStrips; ++i) {
        SceneNode* g = b.group(parent, "Flag strip", {i == 0 ? 0.0f : kStripWidth, 0, 0}, {0, 0, 0}, "One 0.2 m strip of cloth, child of the previous strip");
        g->animation = "Yaw = A_i sin(0.7 i - w t): travelling wave, amplitude grows towards the fly";
        for (int j = 0; j < 3; ++j)
            b.part(g, "Flag cloth", P::Cube, {kStripWidth, kFlagHeight / 3.0f, 0.02f}, {0, 0, 0},
                   {0.5f * kStripWidth, -(0.25f + 0.5f * static_cast<float>(j)), 0}, bands[j], "Cloth band of the flag");
        strips_.push_back(g);
        parent = g;
    }
    finalizeFlags(*r);
    update(WorldContext{}, nullptr);
}

void WavingFlag::update(const WorldContext& w, const ControlInput*) {
    const float t = w.time, wind = windStrength(t);
    hoist_->local.rotationDeg = {0, windDirectionDeg(t), -(1.0f - wind) * 25.0f};
    const float omega = kTwoPi * (0.6f + 1.4f * wind);
    for (int i = 0; i < kStrips; ++i) {
        const float k = static_cast<float>(i) / static_cast<float>(kStrips - 1);
        const float amp = (2.0f + 4.0f * wind) * (0.3f + 0.7f * k);
        strips_[static_cast<size_t>(i)]->local.rotationDeg = {1.5f * std::sin(0.9f * static_cast<float>(i) - 1.3f * omega * t),
                                                               amp * std::sin(0.7f * static_cast<float>(i) - omega * t), 0};
    }
}

CameraHint WavingFlag::camera(bool) const {
    CameraHint h;
    h.eye = {kFlagpole.x + 4.0f, G + 2.0f, kFlagpole.z - 7.0f};
    h.target = {kFlagpole.x, G + 8.0f, kFlagpole.z};
    return h;
}
std::string WavingFlag::status() const { return "FLAG"; }
Vec3 WavingFlag::position() const { return {kFlagpole.x, G + 8.9f, kFlagpole.z}; }

}  // namespace ts
