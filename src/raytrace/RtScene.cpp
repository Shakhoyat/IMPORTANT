#include "raytrace/RtScene.h"

#include <algorithm>
#include <cmath>

namespace ts::rt {

void RtScene::clear() {
    prims.clear();
    materials.clear();
    lights.clear();
    bounds_.clear();
    bvh_ = Bvh{};
    terrain_ = nullptr;
}

int RtScene::addPrim(Shape s, const Mat4& world, const RtMaterial& m, uint32_t flags, int node, const char* name) {
    RtPrim p;
    p.shape = s;
    p.world = world;
    p.inv = inverse(world);
    p.flags = flags;
    p.node = node;
    p.name = name;
    prims.push_back(p);
    materials.push_back(m);
    return static_cast<int>(prims.size()) - 1;
}

void RtScene::build() {
    std::vector<Aabb> b(prims.size());
    for (size_t i = 0; i < prims.size(); ++i) {
        prims[i].inv = inverse(prims[i].world);
        if (!(prims[i].flags & kHidden)) b[i] = shapeBounds(prims[i].shape, prims[i].world);
    }
    bvh_.build(b);
    std::vector<RtPrim> p2(prims.size());
    std::vector<RtMaterial> m2(prims.size());
    bounds_.resize(prims.size());
    for (size_t k = 0; k < prims.size(); ++k) {
        const int src = bvh_.order()[k];
        p2[k] = std::move(prims[src]);
        m2[k] = materials[src];
        bounds_[k] = b[src];
    }
    prims.swap(p2);
    materials.swap(m2);
    buildCost_ = std::max(bvh_.sahCost(), 1e-6f);
}

float RtScene::refit() {
    if (bvh_.empty()) return 1.0f;
    for (size_t i = 0; i < prims.size(); ++i) {
        prims[i].inv = inverse(prims[i].world);
        bounds_[i] = (prims[i].flags & kHidden) ? Aabb{} : shapeBounds(prims[i].shape, prims[i].world);
    }
    bvh_.refit(bounds_);
    return bvh_.sahCost() / buildCost_;
}

const char* RtScene::name(const Hit& h) const {
    if (h.prim == kTerrainPrim) return "Terrain";
    if (h.prim < 0 || h.prim >= static_cast<int>(prims.size())) return "sky";
    return prims[h.prim].name.c_str();
}

bool RtScene::primHit(int i, const Ray& r, float tmax, float& t, Vec3& nLocal) const {
    const RtPrim& p = prims[i];
    if (p.flags & kHidden) return false;
    const Vec3 o = transformPoint(p.inv, r.o), d = transformDirection(p.inv, r.d);
    if (!intersectUnitShape(p.shape, o, d, r.tmin, tmax, t, nLocal)) return false;
    if (!(p.flags & kFoliage) || leafCovered(o + d * t)) return true;
    // a gap in the leaves: the far side of the same primitive, once
    return intersectUnitShape(p.shape, o, d, t + 1e-4f, tmax, t, nLocal) && leafCovered(o + d * t);
}

void RtScene::finishHit(const Ray& r, Hit& h, const Vec3& nWorld) const {
    h.p = r.o + r.d * h.t;
    h.ng = nWorld;
    h.front = dot(r.d, nWorld) < 0.0f;
    h.n = h.front ? nWorld : -nWorld;
}

namespace {

// World normal from a local one: inverse transpose of the 3x3 part.
Vec3 normalToWorld(const Mat4& inv, const Vec3& n) {
    return normalize(Vec3{inv.at(0, 0) * n.x + inv.at(1, 0) * n.y + inv.at(2, 0) * n.z,
                          inv.at(0, 1) * n.x + inv.at(1, 1) * n.y + inv.at(2, 1) * n.z,
                          inv.at(0, 2) * n.x + inv.at(1, 2) * n.y + inv.at(2, 2) * n.z});
}

Vec3 safeInv(const Vec3& d) {
    auto f = [](float v) { return 1.0f / (std::fabs(v) > 1e-12f ? v : (v < 0 ? -1e-12f : 1e-12f)); };
    return {f(d.x), f(d.y), f(d.z)};
}

}  // namespace

bool RtScene::intersect(const Ray& r, Hit& h, TraceStats* st) const {
    h = Hit{};
    float tmax = r.tmax;
    Vec3 nBest;
    int best = -1;
    if (st) ++st->rays;
    if (terrain_) {
        float t;
        Vec3 n;
        if (terrain_->intersect(r.o, r.d, r.tmin, tmax, t, n)) {
            tmax = t;
            best = kTerrainPrim;
            nBest = n;
        }
    }
    if (!bvh_.empty()) {
        const Vec3 invD = safeInv(r.d);
        int stack[64];
        int sp = 0;
        stack[sp++] = 0;
        const std::vector<BvhNode>& nodes = bvh_.nodes();
        while (sp > 0) {
            const BvhNode& n = nodes[stack[--sp]];
            float te;
            if (st) ++st->nodeVisits;
            if (!n.box.hit(r.o, invD, r.tmin, tmax, te)) continue;
            if (n.leaf()) {
                for (int k = n.leftFirst; k < n.leftFirst + n.count; ++k) {
                    float t;
                    Vec3 nl;
                    if (st) ++st->primTests;
                    if (primHit(k, r, tmax, t, nl)) {
                        tmax = t;
                        best = k;
                        nBest = nl;
                    }
                }
            } else {
                // Visit the nearer child first (pushed last).
                float ta = 1e30f, tb = 1e30f;
                const bool ha = nodes[n.leftFirst].box.hit(r.o, invD, r.tmin, tmax, ta);
                const bool hb = nodes[n.leftFirst + 1].box.hit(r.o, invD, r.tmin, tmax, tb);
                if (ha && hb) {
                    if (ta < tb) {
                        stack[sp++] = n.leftFirst + 1;
                        stack[sp++] = n.leftFirst;
                    } else {
                        stack[sp++] = n.leftFirst;
                        stack[sp++] = n.leftFirst + 1;
                    }
                } else if (ha) {
                    stack[sp++] = n.leftFirst;
                } else if (hb) {
                    stack[sp++] = n.leftFirst + 1;
                }
                if (sp > 62) sp = 62;   // never on a sane tree (depth < 60)
            }
        }
    }
    if (best < 0) return false;
    h.t = tmax;
    h.prim = best;
    finishHit(r, h, best == kTerrainPrim ? nBest : normalToWorld(prims[best].inv, nBest));
    return true;
}

bool RtScene::intersectBrute(const Ray& r, Hit& h) const {
    h = Hit{};
    float tmax = r.tmax;
    Vec3 nBest;
    int best = -1;
    if (terrain_) {
        float t;
        Vec3 n;
        if (terrain_->intersect(r.o, r.d, r.tmin, tmax, t, n)) {
            tmax = t;
            best = kTerrainPrim;
            nBest = n;
        }
    }
    for (int k = 0; k < static_cast<int>(prims.size()); ++k) {
        float t;
        Vec3 nl;
        if (primHit(k, r, tmax, t, nl)) {
            tmax = t;
            best = k;
            nBest = nl;
        }
    }
    if (best < 0) return false;
    h.t = tmax;
    h.prim = best;
    finishHit(r, h, best == kTerrainPrim ? nBest : normalToWorld(prims[best].inv, nBest));
    return true;
}

Vec3 RtScene::transmittance(const Ray& r, uint32_t skip, TraceStats* st) const {
    if (st) ++st->rays;
    if (terrain_) {
        float t;
        Vec3 n;
        if (terrain_->intersect(r.o, r.d, r.tmin, r.tmax, t, n)) return {0, 0, 0};
    }
    Vec3 T{1, 1, 1};
    if (bvh_.empty()) return T;
    const Vec3 invD = safeInv(r.d);
    int stack[64];
    int sp = 0;
    stack[sp++] = 0;
    const std::vector<BvhNode>& nodes = bvh_.nodes();
    while (sp > 0) {
        const BvhNode& n = nodes[stack[--sp]];
        float te;
        if (st) ++st->nodeVisits;
        if (!n.box.hit(r.o, invD, r.tmin, r.tmax, te)) continue;
        if (n.leaf()) {
            for (int k = n.leftFirst; k < n.leftFirst + n.count; ++k) {
                if (prims[k].flags & skip) continue;
                float t;
                Vec3 nl;
                if (st) ++st->primTests;
                if (!primHit(k, r, r.tmax, t, nl)) continue;
                const RtMaterial& m = materials[k];
                if (m.transmission <= 0.0f) return {0, 0, 0};
                T = mul(T, m.tint * m.transmission);   // one interface crossing of clear glass
            }
        } else if (sp < 62) {
            stack[sp++] = n.leftFirst;
            stack[sp++] = n.leftFirst + 1;
        }
    }
    return T;
}

}  // namespace ts::rt
