// T70-T79: military base apron (objects/Base.*), compound edits and backdrop placement.
#include <algorithm>
#include <cmath>
#include <set>

#include "TestKit.h"
#include "ai/Occlusion.h"
#include "objects/Backdrop.h"
#include "objects/Base.h"
#include "objects/Environment.h"
#include "objects/Terrain.h"

using ts::testkit::fmt;
using ts::testkit::record;

namespace {

using namespace ts;

void updateWorldRec(SceneNode& n, const Mat4& parent) {
    n.world = parent * n.local.matrix();
    for (auto& c : n.children) updateWorldRec(*c, n.world);
}

template <typename F>
void forLeaves(SceneNode& n, F&& f) {
    if (n.isLeaf()) f(n);
    for (auto& c : n.children) forLeaves(*c, f);
}

// 2D separating-axis test: axis-aligned box (x0..x1, z0..z1) against the zone shape.
bool zoneHits(const ClearZone& z, const Box& b) {
    if (z.circle) {
        const float dx = std::max({b.min.x - z.cx, 0.0f, z.cx - b.max.x});
        const float dz = std::max({b.min.z - z.cz, 0.0f, z.cz - b.max.z});
        return dx * dx + dz * dz < z.halfX * z.halfX;
    }
    const float yaw = radians(z.yawDeg), s = std::sin(yaw), c = std::cos(yaw);
    const Vec3 fw{s, 0, c}, sd{c, 0, -s};
    const float cxb = 0.5f * (b.min.x + b.max.x), czb = 0.5f * (b.min.z + b.max.z);
    const float ex = 0.5f * (b.max.x - b.min.x), ez = 0.5f * (b.max.z - b.min.z);
    const float dx = cxb - z.cx, dz = czb - z.cz;
    // axes of the zone
    const float rS = z.halfX + ex * std::fabs(sd.x) + ez * std::fabs(sd.z);
    if (std::fabs(dx * sd.x + dz * sd.z) >= rS) return false;
    const float rF = z.halfZ + ex * std::fabs(fw.x) + ez * std::fabs(fw.z);
    if (std::fabs(dx * fw.x + dz * fw.z) >= rF) return false;
    // world axes
    const float zx = z.halfX * std::fabs(sd.x) + z.halfZ * std::fabs(fw.x);
    const float zz = z.halfX * std::fabs(sd.z) + z.halfZ * std::fabs(fw.z);
    return std::fabs(dx) < ex + zx && std::fabs(dz) < ez + zz;
}

// Exact test: sample the zone on a 0.2 m grid and ask whether a sample lies inside the part's (unit-cube)
// bounds after the world transform. Pre-filtered with the conservative AABB test.
bool partHits(const ClearZone& z, const SceneNode& n) {
    const Box b = worldBounds(n.world);
    if (!zoneHits(z, b)) return false;
    const Mat4 inv = inverse(n.world);
    const float yc = 0.5f * (b.min.y + b.max.y);
    const float s = std::sin(radians(z.yawDeg)), c = std::cos(radians(z.yawDeg));
    const float hx = z.halfX, hz = z.circle ? z.halfX : z.halfZ;
    for (float u = -hx; u <= hx; u += 0.2f)
        for (float v = -hz; v <= hz; v += 0.2f) {
            if (z.circle && u * u + v * v > hx * hx) continue;
            const Vec3 p{z.cx + u * c + v * s, yc, z.cz - u * s + v * c};
            const Vec3 l = transformPoint(inv, p);
            if (std::fabs(l.x) <= 0.5f && std::fabs(l.y) <= 0.5f && std::fabs(l.z) <= 0.5f) return true;
        }
    return false;
}

int countLeaves(SceneNode& n) {
    int k = 0;
    forLeaves(n, [&](SceneNode&) { ++k; });
    return k;
}

BaseRig& rig() {
    static BaseRig r = [] {
        BaseRig x = buildBase();
        updateWorldRec(*x.root, Mat4::identity());
        for (auto& o : x.objects) updateWorldRec(*o->root(), Mat4::identity());
        return x;
    }();
    return r;
}

template <typename T>
T* find() {
    for (auto& o : rig().objects)
        if (T* p = dynamic_cast<T*>(o.get())) return p;
    return nullptr;
}

}  // namespace

TS_TEST_GROUP(Base) {
    BaseRig& r = rig();

    // T70: lab primitives only, named and documented.
    {
        int bad = 0, total = 0;
        auto check = [&](SceneNode& n) {
            ++total;
            const bool ok = (n.primitive == PrimitiveType::Cube || n.primitive == PrimitiveType::Sphere ||
                             n.primitive == PrimitiveType::Cylinder || n.primitive == PrimitiveType::Cone ||
                             n.primitive == PrimitiveType::BeamCone) && !n.name.empty() && !n.purpose.empty() && !n.id.empty();
            if (!ok) ++bad;
        };
        forLeaves(*r.root, check);
        for (auto& o : r.objects) forLeaves(*o->root(), check);
        record("T70", "every Base part is a lab primitive with a name and purpose", "0 violations",
               fmt("%d parts, %d violations", total, bad), bad == 0 && total > 0);
    }

    // T71: nothing solid inside the clear zones (flush markings and pads are allowed).
    {
        const float groundTop = layout::kApronLevel + kFlushHeight;
        std::string worst;
        int hits = 0;
        forLeaves(*r.root, [&](SceneNode& n) {
            const Box b = worldBounds(n.world);
            if (b.max.y <= groundTop) return;
            for (const ClearZone& z : baseClearZones())
                if (partHits(z, n)) {
                    ++hits;
                    if (worst.empty()) worst = n.name + " in " + z.name;
                }
        });
        record("T71", "no Base part intersects the helipad, radar, AA pit, tank spot, road or trench strip",
               "0 solid parts inside the zones", fmt("%d hits %s", hits, worst.c_str()), hits == 0);
    }

    // T72: HESCO wall closed except for the gate.
    {
        std::vector<float> ang;
        forLeaves(*r.root, [&](SceneNode& n) {
            if (n.name == "HESCO cell") ang.push_back(std::atan2(n.local.translation.z, n.local.translation.x));
        });
        std::sort(ang.begin(), ang.end());
        float maxGap = 0.0f, gapAt = 0.0f, secondGap = 0.0f;
        std::vector<std::pair<float, float>> gaps;
        for (size_t i = 0; i < ang.size(); ++i) {
            const float nxt = (i + 1 < ang.size()) ? ang[i + 1] : ang[0] + 2.0f * PI;
            const float gap = (nxt - ang[i]) * layout::kPerimeterRadius;
            gaps.push_back({gap, 0.5f * (nxt + ang[i])});
        }
        std::sort(gaps.begin(), gaps.end(), [](auto& a, auto& b) { return a.first > b.first; });
        maxGap = gaps[0].first;
        gapAt = degrees(gaps[0].second);
        secondGap = gaps[1].first;
        record("T72", "perimeter wall is closed except one gate opening centred on +Z (90 deg), 9..11 m wide",
               "1 gap of 9..11 m at 90 deg, every other joint < 1.3 m",
               fmt("%zu cells; largest gap %.1f m at %.1f deg, next %.2f m", ang.size(), maxGap, gapAt, secondGap),
               maxGap > 9.0f && maxGap < 11.0f && std::fabs(gapAt - 90.0f) < 2.0f && secondGap < 1.3f);
    }

    // T73: boom barrier timing, easing and obstacle.
    {
        GateBarrier* g = find<GateBarrier>();
        bool ok = g != nullptr;
        std::string obs;
        if (g) {
            WorldContext w;
            w.dt = 0.01f;
            const bool closedBlocks = !g->dynamicObstacles().empty() && g->angleDeg() == 0.0f;
            ControlInput in;
            in.actionPressed = true;
            g->update(w, &in);
            float time = 0.01f, prev = g->angleDeg(), half = -1.0f;
            bool monotone = true;
            while (g->moving() && time < 4.0f) {
                w.time += w.dt;
                g->update(w, nullptr);
                time += 0.01f;
                if (g->angleDeg() + 1e-4f < prev) monotone = false;
                prev = g->angleDeg();
                if (half < 0 && g->angleDeg() >= 0.5f * GateBarrier::kMaxAngleDeg) half = time;
            }
            const bool openFree = g->dynamicObstacles().empty();
            ok = closedBlocks && openFree && monotone && std::fabs(time - 2.5f) < 0.08f && std::fabs(g->angleDeg() - 85.0f) < 0.01f &&
                 std::fabs(half - 1.25f) < 0.1f;
            // Arm tip really rises (world transform) and falls again.
            updateWorldRec(*g->root(), Mat4::identity());
            float tipY = -1;
            forLeaves(*g->root(), [&](SceneNode& n) {
                if (n.name == "Boom tip lamp") tipY = transformPoint(n.world, {0, 0, 0}).y;
            });
            ok = ok && tipY > layout::kApronLevel + 4.0f;
            in.firePressed = true;
            in.actionPressed = false;
            g->update(w, &in);
            for (int i = 0; i < 300; ++i) g->update(w, nullptr);
            ok = ok && g->angleDeg() == 0.0f && !g->dynamicObstacles().empty();
            obs = fmt("travel %.2f s, half-way at %.2f s, final %.1f deg, tip y %.2f, closed again", time, half, 85.0f, tipY);
        }
        record("T73", "boom barrier: 0 to 85 deg in 2.5 s with smoothstep easing, arm box only when lowered, input toggles",
               "2.50 s +-0.08, half way 1.25 s, obstacle iff lowered", obs, ok);
    }

    // T74: flag animation bounded and genuinely moving.
    {
        WavingFlag* f = find<WavingFlag>();
        bool ok = f != nullptr;
        std::string obs;
        if (f) {
            float maxYaw = 0, maxPitch = 0, tipMin = 1e9f, tipMax = -1e9f, motion = 0;
            Vec3 prevTip{};
            WorldContext w;
            for (int i = 0; i < 1200; ++i) {
                w.time = 0.05f * static_cast<float>(i);
                f->update(w, nullptr);
                for (SceneNode* s : f->strips()) {
                    maxYaw = std::max(maxYaw, std::fabs(s->local.rotationDeg.y));
                    maxPitch = std::max(maxPitch, std::fabs(s->local.rotationDeg.x));
                }
                updateWorldRec(*f->root(), Mat4::identity());
                const Vec3 tip = transformPoint(f->strips().back()->world, {WavingFlag::kStripWidth, -0.75f, 0});
                const Vec3 hoist = transformPoint(f->hoist()->world, {0, 0, 0});
                const float d = length(tip - hoist);
                tipMin = std::min(tipMin, d);
                tipMax = std::max(tipMax, d);
                if (i > 0) motion += length(tip - prevTip);
                prevTip = tip;
            }
            const float flagLen = WavingFlag::kStrips * WavingFlag::kStripWidth;
            ok = f->strips().size() == 12 && maxYaw <= 6.1f && maxPitch <= 1.6f && tipMax <= flagLen + 0.8f && tipMin > 1.5f && motion > 5.0f;
            obs = fmt("12 strips, max strip yaw %.1f deg, pitch %.1f deg, tip distance %.2f..%.2f m, path %.1f m", maxYaw, maxPitch,
                      tipMin, tipMax, motion);
        }
        record("T74", "waving flag: travelling wave, strip rotations bounded, flag never stretches or collapses",
               "yaw <= 6.1 deg, tip 1.5..3.2 m from the hoist, tip moves", obs, ok);
    }

    // T75: windsock chain.
    {
        Windsock* s = find<Windsock>();
        bool ok = s != nullptr;
        std::string obs;
        if (s) {
            float calm = 0, gale = 0;
            WorldContext w;
            float minW = 9, maxW = 0;
            for (int i = 0; i < 2000; ++i) {
                w.time = 0.1f * static_cast<float>(i);
                const float wind = windStrength(w.time);
                s->update(w, nullptr);
                if (wind < minW) { minW = wind; calm = s->segments()[2]->local.rotationDeg.x; }
                if (wind > maxW) { maxW = wind; gale = s->segments()[2]->local.rotationDeg.x; }
            }
            bool chain = s->segmentCount() == 5;
            for (int k = 1; k < 5; ++k) chain = chain && s->segments()[static_cast<size_t>(k)]->parent == s->segments()[static_cast<size_t>(k - 1)];
            ok = chain && calm > gale + 5.0f && calm < 30.0f;
            obs = fmt("5 chained segments, segment pitch %.1f deg at wind %.2f vs %.1f deg at wind %.2f", calm, minW, gale, maxW);
        }
        record("T75", "windsock: 5 hierarchical cone segments that droop more in weak wind", "chain of 5, droop(calm) > droop(gale)", obs, ok);
    }

    // T76: part counts.
    {
        const int base = countLeaves(*r.root);
        int ctl = 0;
        for (auto& o : r.objects) ctl += countLeaves(*o->root());
        record("T76", "Base part counts (static apron + controllables)", "900..1500 parts in total",
               fmt("static %d + controllable %d = %d, %zu controllables", base, ctl, base + ctl, r.objects.size()),
               base + ctl >= 900 && base + ctl <= 1500 && r.objects.size() == 3);
    }

    // T77: nothing of the base is an occluder; everything inside the wall + wire radius or an outer-gate item.
    {
        int occ = 0, outside = 0;
        float worst = 0;
        forLeaves(*r.root, [&](SceneNode& n) {
            if (n.occluder) ++occ;
            const Box b = worldBounds(n.world);
            const float cx = 0.5f * (b.min.x + b.max.x), cz = 0.5f * (b.min.z + b.max.z);
            const float rr = std::sqrt(cx * cx + cz * cz);
            worst = std::max(worst, rr);
            if (rr > layout::kBaseRadius) ++outside;
        });
        record("T77", "Base parts are never occluders and stay on the terrain mesh", "occluders 0, centres r <= 36 m",
               fmt("occluders %d, parts beyond r = 36: %d, farthest centre %.1f m", occ, outside, worst), occ == 0 && outside == 0);
    }

    // T78: compound edits: trench strip free, rover cover kept.
    {
        auto env = buildEnvironment();
        updateWorldRec(*env, Mat4::identity());
        int inStrip = 0, occluders = 0;
        ClearZone strip{"trench", 0, 8.5f, 2.5f, 5.0f, 0, false};
        const ClearZone foot{"bunker footprint", 0.0f, -2.25f, 5.5f, 6.25f, 0, false};
        int inFoot = 0;
        std::string hitName;
        forLeaves(*env, [&](SceneNode& n) {
            if (n.occluder) ++occluders;
            if (n.name == "Moon") return;
            const Box b = worldBounds(n.world);
            if (b.max.y <= 0.0f) return;
            if (partHits(strip, n)) ++inStrip;
            if (b.min.y < 1.2f && partHits(foot, n)) { ++inFoot; if (hitName.empty()) hitName = n.name + fmt(" at %.1f,%.1f", n.world.at(0, 3), n.world.at(2, 3)); }
        });
        constexpr int kOriginalOccluders = 477;  // v3 environment
        record("T78", "compound: trench strip and bunker footprint free of props; >= 70% of the v3 occluders kept",
               "strip 0, footprint 0, occluders >= 334", fmt("strip %d, footprint %d %s, occluders %d (v3: %d)", inStrip, inFoot, hitName.c_str(), occluders, kOriginalOccluders),
               inStrip == 0 && inFoot == 0 && occluders >= static_cast<int>(0.7f * kOriginalOccluders));
    }

    // T79: backdrop beyond the terrain mesh.
    {
        auto bk = buildBackdrop();
        updateWorldRec(*bk, Mat4::identity());
        float nearest = 1e9f, groundTop = 1e9f;
        int obstacles = 0;
        forLeaves(*bk, [&](SceneNode& n) {
            if (n.obstacle || n.occluder) ++obstacles;
            const Vec3 c = transformPoint(n.world, {0, 0, 0});
            if (n.name == "Far ground") {
                groundTop = transformPoint(n.world, {0, 0, 0.5f}).y;  // cylinder axis after the 90 deg rotation
                groundTop = std::max(groundTop, transformPoint(n.world, {0, 0, -0.5f}).y);
                return;
            }
            nearest = std::min(nearest, std::sqrt(c.x * c.x + c.z * c.z));
        });
        record("T79", "backdrop starts beyond the terrain mesh (r >= 36) and its ground stays just below the apron level",
               "nearest backdrop centre >= 36 m, far ground top in (apron - 0.2, apron)",
               fmt("nearest %.1f m, far ground top %.2f, solid flags %d", nearest, groundTop, obstacles),
               nearest >= layout::kBaseRadius && groundTop < layout::kApronLevel && groundTop > layout::kApronLevel - 0.2f && obstacles == 0);
    }
}
