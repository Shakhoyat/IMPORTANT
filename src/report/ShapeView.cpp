#include "report/ShapeView.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <tuple>

#include "primitives/Library.h"
#include "report/PrimitiveColors.h"
#include "scene/PartOrder.h"

namespace ts::shape {

namespace {

Mat4 restWorld(const SceneNode& n) {
    const Vec3 s = restScaleOf(n);
    const Mat4 local = translate(n.local.translation) * rotateEulerYXZ(n.local.rotationDeg) * scale(s);
    return n.parent ? n.parent->world * local : local;
}

bool underCut(const SceneNode& n, const SceneNode* root, const std::vector<std::string>& cut) {
    for (const SceneNode* p = &n; p; p = p->parent) {
        for (const std::string& c : cut)
            if (nameMatches(p->name, c)) return true;
        if (p == root) break;
    }
    return false;
}

Vec3 mul3(const Mat3& m, const Vec3& v) {
    return {m.m[0] * v.x + m.m[3] * v.y + m.m[6] * v.z, m.m[1] * v.x + m.m[4] * v.y + m.m[7] * v.z,
            m.m[2] * v.x + m.m[5] * v.y + m.m[8] * v.z};
}

struct Sv {
    float x, y, iw;
    bool ok;
};

void toScreen(const Mat4& mvp, const Vec3& p, int W, int H, bool ortho, Sv& s) {
    float clip[4];
    for (int r = 0; r < 4; ++r) clip[r] = mvp.at(r, 0) * p.x + mvp.at(r, 1) * p.y + mvp.at(r, 2) * p.z + mvp.at(r, 3);
    s.ok = clip[3] > 1e-5f;
    const float iw = s.ok ? 1.0f / clip[3] : 0.0f;
    s.x = (clip[0] * iw * 0.5f + 0.5f) * static_cast<float>(W);
    s.y = (1.0f - (clip[1] * iw * 0.5f + 0.5f)) * static_cast<float>(H);
    // Orthographic: w = 1 everywhere; nearer = smaller NDC z, stored as 2 - z so larger is nearer.
    s.iw = ortho ? 2.0f - clip[2] : iw;
}

}  // namespace

Spec parseSpec(const std::string& text) {
    Spec s;
    size_t p = text.find('|');
    s.design = text.substr(0, p);
    while (p != std::string::npos) {
        const size_t e = text.find('|', p + 1);
        const std::string kv = text.substr(p + 1, e == std::string::npos ? std::string::npos : e - p - 1);
        const size_t eq = kv.find('=');
        const std::string k = kv.substr(0, eq), v = eq == std::string::npos ? "" : kv.substr(eq + 1);
        if (k == "az") s.az = std::stof(v);
        else if (k == "el") s.el = std::stof(v);
        else if (k == "ksub") s.ksub = std::stof(v);
        else if (k == "kpart") s.kpart = std::stof(v);
        else if (k == "inst") s.inst = std::stoi(v);
        else if (k == "w") s.w = std::stoi(v);
        else if (k == "h") s.h = std::stoi(v);
        else if (k == "fill") s.fill = std::stof(v);
        else if (k == "stage") s.stage = std::stoi(v);
        else if (k == "cut") {
            size_t a = 0;
            while (a <= v.size()) {
                const size_t b = v.find(';', a);
                const std::string t = v.substr(a, b == std::string::npos ? std::string::npos : b - a);
                if (!t.empty()) s.cut.push_back(t);
                if (b == std::string::npos) break;
                a = b + 1;
            }
        }
        p = e;
    }
    return s;
}

bool nameMatches(const std::string& name, const std::string& pat) {
    if (!pat.empty() && pat.back() == '*') return name.rfind(pat.substr(0, pat.size() - 1), 0) == 0;
    return name == pat;
}

// ------------------------------------------------------------------------------------------------ parts of a view
std::vector<DrawPart> gatherParts(const designs::Catalogue& cat, int inst, const std::vector<std::string>& cut, bool cutaway) {
    const designs::Instance& in = cat.instances[static_cast<size_t>(inst)];
    std::vector<DrawPart> out;
    std::function<void(const SceneNode&)> walk = [&](const SceneNode& n) {
        if (n.isLeaf()) {
            DrawPart d;
            d.node = &n;
            d.prim = n.primitive;
            d.cuboid = n.primitive == PrimitiveType::Cube && isCuboidScale(restScaleOf(n));
            const auto it = cat.instanceOf.find(&n);
            const int owner = it == cat.instanceOf.end() ? -1 : it->second;
            d.nested = owner != inst ? owner : -1;
            d.hidden = hiddenAtRest(n);
            d.world = d.hidden ? restWorld(n) : n.world;
            // Parked = stored below the ground while unused (ejected casings). A part of the design itself is drawn
            // wherever it is (the view is fitted to it); a parked part of a nested design is left out of the parent.
            d.parked = d.hidden && d.nested >= 0 && d.world.at(1, 3) < -6.0f;
            d.cut = cutaway && underCut(n, in.root, cut);
            out.push_back(d);
        }
        for (const auto& ch : n.children) walk(*ch);
    };
    walk(*in.root);
    return out;
}

// ------------------------------------------------------------------------------------------------ geometry helpers
void cornersOf(const Mat4& w, std::vector<Vec3>& out) {
    for (int k = 0; k < 8; ++k)
        out.push_back(transformPoint(w, {(k & 1) ? 0.5f : -0.5f, (k & 2) ? 0.5f : -0.5f, (k & 4) ? 0.5f : -0.5f}));
}

void meshBoxCorners(const MeshData& m, const Mat4& w, std::vector<Vec3>& out) {
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (const Vertex& v : m.vertices) {
        lo = {std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)};
        hi = {std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
    }
    for (int k = 0; k < 8; ++k) out.push_back(transformPoint(w, {(k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z}));
}

Vec3 boxCentre(const std::vector<Vec3>& pts) {
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (const Vec3& p : pts) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    return (lo + hi) * 0.5f;
}

// Pull-apart offsets. Every group gets ksub * (its centre - its parent's centre) on top of its parent's offset (halved
// for each deeper level that branches), every part kpart * (its centre - its group's centre): sub-assemblies move as
// blocks first, then the parts inside them. A group with a single child group (Hull, Pelvis, Body) is not a level.
std::map<const SceneNode*, GroupInfo> explode(std::vector<DrawPart>& parts, const SceneNode* root, float ksub, float kpart) {
    std::map<const SceneNode*, std::vector<Vec3>> pts;
    std::map<const SceneNode*, int> count;
    for (const DrawPart& d : parts) {
        // The light-beam frustum reaches the ground: it would drag every centre down, so it does not count.
        if (!drawn(d) || d.prim == PrimitiveType::BeamCone) continue;
        std::vector<Vec3> c;
        cornersOf(d.world, c);
        for (const SceneNode* p = d.node->parent; p; p = p->parent) {
            auto& v = pts[p];
            v.insert(v.end(), c.begin(), c.end());
            ++count[p];
            if (p == root) break;
        }
        if (d.node == root) {   // one-part design
            auto& v = pts[root];
            v.insert(v.end(), c.begin(), c.end());
            ++count[root];
        }
    }
    std::map<const SceneNode*, GroupInfo> g;
    for (auto& kv : pts) {
        GroupInfo gi;
        gi.node = kv.first;
        gi.centre = boxCentre(kv.second);
        gi.parts = count[kv.first];
        g[kv.first] = gi;
    }
    std::function<void(const SceneNode&, int)> walk = [&](const SceneNode& n, int level) {
        auto it = g.find(&n);
        if (it == g.end()) return;
        int groupKids = 0, leafKids = 0;
        for (const auto& ch : n.children) (ch->isLeaf() ? leafKids : groupKids) += g.count(ch.get()) || ch->isLeaf();
        const bool branches = groupKids + leafKids > 1;
        for (const auto& ch : n.children) {
            if (ch->isLeaf()) continue;
            auto c = g.find(ch.get());
            if (c == g.end()) continue;
            const int lv = level + (branches ? 1 : 0);
            const float k = lv <= 0 ? 0.0f : ksub * std::pow(0.5f, static_cast<float>(std::max(0, lv - 1)));
            c->second.offset = it->second.offset + (c->second.centre - it->second.centre) * k;
            c->second.level = lv;
            walk(*ch, lv);
        }
    };
    walk(*root, 0);
    for (DrawPart& d : parts) {
        if (!drawn(d) || d.node == root) continue;
        const auto it = g.find(d.node->parent);
        if (it == g.end()) continue;
        const Vec3 c = transformPoint(d.world, {0, 0, 0});
        d.offset = it->second.offset + (c - it->second.centre) * kpart;
    }
    return g;
}

// ------------------------------------------------------------------------------------------------ camera
Cam perspectiveFit(const Vec3& centre, float radius, const Vec3& dir, int w, int h, float fill) {
    Cam c;
    c.w = w;
    c.h = h;
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    const float th = std::tan(radians(kFov) * 0.5f);
    const float dist = radius / (fill * std::min(th, th * aspect));
    c.eye = centre + dir * dist;
    c.view = lookAt(c.eye, centre, {0, 1, 0});
    c.nearZ = std::max(0.01f, dist - 2.5f * radius);
    c.farZ = dist + 3.0f * radius;
    c.proj = perspective(radians(kFov), aspect, c.nearZ, c.farZ);
    c.fwd = dir * -1.0f;
    c.right = normalize(cross(c.fwd, Vec3{0, 1, 0}));
    c.up = cross(c.right, c.fwd);
    return c;
}

Cam fitPoints(const std::vector<Vec3>& pts, const Vec3& dir, int w, int h, float fill) {
    const float aspect = static_cast<float>(w) / static_cast<float>(h), th = std::tan(radians(kFov) * 0.5f);
    const Vec3 fwd = dir * -1.0f, right = normalize(cross(fwd, Vec3{0, 1, 0})), up = cross(right, fwd);
    Vec3 target = boxCentre(pts);
    float radius = 1e-4f;
    for (const Vec3& p : pts) radius = std::max(radius, length(p - target));
    float cx = 0, cy = 0;
    auto span = [&](float d) {
        const Vec3 eye = target + dir * d;
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (const Vec3& p : pts) {
            const Vec3 v = p - eye;
            const float z = std::max(dot(v, fwd), 1e-3f);
            const float x = dot(v, right) / (z * th * aspect), y = dot(v, up) / (z * th);
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
        cx = 0.5f * (x0 + x1);
        cy = 0.5f * (y0 + y1);
        return std::max(0.5f * (x1 - x0), 0.5f * (y1 - y0));
    };
    float lo = 1.01f * radius, hi = 200.0f * radius;
    for (int it = 0; it < 50; ++it) {
        const float mid = 0.5f * (lo + hi);
        (span(mid) > fill ? lo : hi) = mid;
    }
    span(hi);
    target = target + right * (cx * hi * th * aspect) + up * (cy * hi * th);
    Cam c = perspectiveFit(target, radius, dir, w, h, fill);
    c.eye = target + dir * hi;
    c.view = lookAt(c.eye, target, {0, 1, 0});
    c.nearZ = std::max(0.01f, hi - 2.5f * radius);
    c.farZ = hi + 3.0f * radius;
    c.proj = perspective(radians(kFov), aspect, c.nearZ, c.farZ);
    return c;
}

bool project(const Cam& c, const Vec3& p, float& x, float& y) {
    const Mat4 vp = c.proj * c.view;
    float clip[4];
    for (int r = 0; r < 4; ++r) clip[r] = vp.at(r, 0) * p.x + vp.at(r, 1) * p.y + vp.at(r, 2) * p.z + vp.at(r, 3);
    if (clip[3] <= 1e-6f) return false;
    x = (clip[0] / clip[3] * 0.5f + 0.5f) * static_cast<float>(c.w);
    y = (1.0f - (clip[1] / clip[3] * 0.5f + 0.5f)) * static_cast<float>(c.h);
    return true;
}

// ------------------------------------------------------------------------------------------------ CPU raster
void rasterise(Raster& R, const Cam& cam, const std::vector<Shaded>& items, const Vec3& lightDir) {
    const Mat4 vp = cam.proj * cam.view;
    for (const Shaded& it : items) {
        const MeshData& mesh = *it.mesh;
        const Mat4 mvp = vp * it.world;
        const Mat3 nm = normalMatrix(it.world);
        std::vector<Sv> sv(mesh.vertices.size());
        std::vector<Vec3> nw(mesh.vertices.size());
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            toScreen(mvp, mesh.vertices[i].position, R.W, R.H, cam.ortho, sv[i]);
            nw[i] = normalize(mul3(nm, mesh.vertices[i].normal));
        }
        for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const uint32_t ia = mesh.indices[t], ib = mesh.indices[t + 1], ic = mesh.indices[t + 2];
            const Sv &a = sv[ia], &b = sv[ib], &d = sv[ic];
            if (!a.ok || !b.ok || !d.ok) continue;
            const float area = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
            if (std::fabs(area) < 1e-9f) continue;
            const int bx0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, d.x}))));
            const int bx1 = std::min(R.W - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, d.x}))));
            const int by0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, d.y}))));
            const int by1 = std::min(R.H - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, d.y}))));
            if (bx0 > bx1 || by0 > by1) continue;
            // flat face normal (world) for two-sided lighting of open shells
            for (int y = by0; y <= by1; ++y)
                for (int x = bx0; x <= bx1; ++x) {
                    const float px = x + 0.5f, py = y + 0.5f;
                    const float w0 = ((b.x - px) * (d.y - py) - (b.y - py) * (d.x - px)) / area;
                    const float w1 = ((d.x - px) * (a.y - py) - (d.y - py) * (a.x - px)) / area;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    const float iw = w0 * a.iw + w1 * b.iw + w2 * d.iw;
                    const size_t k = static_cast<size_t>(y) * R.W + x;
                    if (iw <= R.depth[k]) continue;
                    R.depth[k] = iw;
                    // perspective-correct normal
                    Vec3 n;
                    if (cam.ortho) {
                        n = nw[ia] * w0 + nw[ib] * w1 + nw[ic] * w2;
                    } else {
                        const float q0 = w0 * a.iw, q1 = w1 * b.iw, q2 = w2 * d.iw, qs = q0 + q1 + q2;
                        n = (nw[ia] * q0 + nw[ib] * q1 + nw[ic] * q2) * (1.0f / qs);
                    }
                    n = normalize(n);
                    if (dot(n, cam.fwd) > 0.0f) n = n * -1.0f;   // back face of an open shell: light its inside
                    const float lam = std::max(0.0f, dot(n, lightDir));
                    const float k2 = 0.40f + 0.60f * lam;
                    R.rgb[k * 3 + 0] = std::min(1.0f, it.color.x * k2);
                    R.rgb[k * 3 + 1] = std::min(1.0f, it.color.y * k2);
                    R.rgb[k * 3 + 2] = std::min(1.0f, it.color.z * k2);
                    R.id[k] = it.id;
                    if (!it.fit) continue;
                    R.x0 = std::min(R.x0, x);
                    R.x1 = std::max(R.x1, x);
                    R.y0 = std::min(R.y0, y);
                    R.y1 = std::max(R.y1, y);
                }
        }
    }
}

// Triangle edges (visible ones only, against the depth buffer), faded out when shorter than a few final pixels so dense
// tessellations stay readable, then a dark outline wherever the part id changes.
void drawEdges(Raster& R, const Cam& cam, const std::vector<Shaded>& items) {
    const Mat4 vp = cam.proj * cam.view;
    std::vector<float> cover(R.depth.size(), 0.0f);
    for (const Shaded& it : items) {
        if (!it.edges) continue;
        const MeshData& mesh = *it.mesh;
        const Mat4 mvp = vp * it.world;
        std::vector<Sv> sv(mesh.vertices.size());
        for (size_t i = 0; i < mesh.vertices.size(); ++i) toScreen(mvp, mesh.vertices[i].position, R.W, R.H, cam.ortho, sv[i]);
        auto q = [](const Vec3& p) { return std::make_tuple(std::lround(p.x * 1e4f), std::lround(p.y * 1e4f), std::lround(p.z * 1e4f)); };
        std::set<std::pair<decltype(q(Vec3{})), decltype(q(Vec3{}))>> seen;
        for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
            for (int e = 0; e < 3; ++e) {
                uint32_t i = mesh.indices[t + e], j = mesh.indices[t + (e + 1) % 3];
                auto ki = q(mesh.vertices[i].position), kj = q(mesh.vertices[j].position);
                if (kj < ki) {
                    std::swap(ki, kj);
                    std::swap(i, j);
                }
                if (ki == kj || !seen.insert({ki, kj}).second) continue;
                const Sv &a = sv[i], &b = sv[j];
                if (!a.ok || !b.ok) continue;
                const float len = std::hypot(b.x - a.x, b.y - a.y);
                const float alpha = std::clamp((len / kSS - 3.0f) / 9.0f, 0.0f, 1.0f) * 0.55f;
                if (alpha <= 0.01f) continue;
                const int n = std::max(2, static_cast<int>(len * 1.5f));
                for (int s = 0; s <= n; ++s) {
                    const float u = static_cast<float>(s) / static_cast<float>(n);
                    const float x = a.x + (b.x - a.x) * u, y = a.y + (b.y - a.y) * u, iw = a.iw + (b.iw - a.iw) * u;
                    const int cx = static_cast<int>(x), cy = static_cast<int>(y);
                    if (cx < 0 || cy < 0 || cx >= R.W || cy >= R.H) continue;
                    const float zb = R.depth[static_cast<size_t>(cy) * R.W + cx];
                    if (zb > 0.0f && iw < zb * (cam.ortho ? 0.9995f : 0.997f)) continue;   // behind a surface
                    for (int yy = cy - 1; yy <= cy + 1; ++yy)
                        for (int xx = cx - 1; xx <= cx + 1; ++xx) {
                            if (xx < 0 || yy < 0 || xx >= R.W || yy >= R.H) continue;
                            const float dd = std::hypot(xx + 0.5f - x, yy + 0.5f - y);
                            const float a2 = std::clamp(1.1f - dd, 0.0f, 1.0f) * alpha;
                            float& cv = cover[static_cast<size_t>(yy) * R.W + xx];
                            cv = std::max(cv, a2);
                        }
                }
            }
    }
    // outline at part boundaries (2 supersampled pixels)
    for (int y = 0; y < R.H; ++y)
        for (int x = 0; x < R.W; ++x) {
            const size_t k = static_cast<size_t>(y) * R.W + x;
            const int me = R.id[k];
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy)
                for (int dx = -1; dx <= 1 && !edge; ++dx) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= R.W || yy >= R.H) continue;
                    const int o = R.id[static_cast<size_t>(yy) * R.W + xx];
                    if (o != me && (me == 0 || o == 0 || R.depth[k] >= R.depth[static_cast<size_t>(yy) * R.W + xx])) edge = true;
                }
            if (edge) cover[k] = std::max(cover[k], 0.85f);
        }
    const float line[3] = {0.08f, 0.09f, 0.12f};
    for (size_t k = 0; k < cover.size(); ++k) {
        const float a = cover[k];
        if (a <= 0.0f) continue;
        for (int c = 0; c < 3; ++c) R.rgb[k * 3 + c] = R.rgb[k * 3 + c] * (1.0f - a) + line[c] * a;
    }
}

Image resolve(const Raster& R, const Vec3& bgTop, const Vec3& bgBottom) {
    Image img;
    img.w = R.W / kSS;
    img.h = R.H / kSS;
    img.px.resize(static_cast<size_t>(img.w) * img.h * 3);
    for (int y = 0; y < img.h; ++y) {
        const float t = static_cast<float>(y) / static_cast<float>(std::max(1, img.h - 1));
        const Vec3 bg = lerp(bgTop, bgBottom, t);
        for (int x = 0; x < img.w; ++x) {
            float acc[3] = {0, 0, 0};
            for (int sy = 0; sy < kSS; ++sy)
                for (int sx = 0; sx < kSS; ++sx) {
                    const size_t k = static_cast<size_t>(y * kSS + sy) * R.W + (x * kSS + sx);
                    const bool covered = R.id[k] != 0 || R.rgb[k * 3] + R.rgb[k * 3 + 1] + R.rgb[k * 3 + 2] > 0.0f;
                    acc[0] += covered ? R.rgb[k * 3 + 0] : bg.x;
                    acc[1] += covered ? R.rgb[k * 3 + 1] : bg.y;
                    acc[2] += covered ? R.rgb[k * 3 + 2] : bg.z;
                }
            for (int c = 0; c < 3; ++c)
                img.px[(static_cast<size_t>(y) * img.w + x) * 3 + c] =
                    static_cast<uint8_t>(std::lround(std::clamp(acc[c] / (kSS * kSS), 0.0f, 1.0f) * 255.0f));
        }
    }
    return img;
}

Raster makeRaster(int w, int h) {
    Raster R;
    R.W = w * kSS;
    R.H = h * kSS;
    R.depth.assign(static_cast<size_t>(R.W) * R.H, 0.0f);
    R.rgb.assign(static_cast<size_t>(R.W) * R.H * 3, 0.0f);
    R.id.assign(static_cast<size_t>(R.W) * R.H, 0);
    return R;
}

// Light from the upper left, a little behind the viewer.
Vec3 lightFor(const Cam& c) { return normalize(c.right * -0.45f + c.up * 0.75f + c.fwd * -0.5f); }

Vec3 partColour(const DrawPart& d) {
    const Vec3 c = primkey::srgb(primkey::colorOf(d.prim, d.cuboid));
    if (d.nested < 0) return c;
    const Vec3 grey{primkey::kContextGrey[0] / 255.0f, primkey::kContextGrey[1] / 255.0f, primkey::kContextGrey[2] / 255.0f};
    return lerp(c, grey, kPale);
}

const MeshData& meshFor(PrimitiveType t) {
    static std::map<PrimitiveType, MeshData> cache;
    auto it = cache.find(t);
    if (it == cache.end()) it = cache.emplace(t, sceneMesh(t)).first;
    return it->second;
}

}  // namespace ts::shape
