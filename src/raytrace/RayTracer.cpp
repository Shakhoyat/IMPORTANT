#include "raytrace/RayTracer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

#include "raytrace/Optics.h"

namespace ts::rt {

namespace {

constexpr float kEps = 2e-3f;   // offset of secondary ray origins along the normal (metres)

float smoothstepf(float a, float b, float x) {
    const float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec3 offsetPoint(const Hit& h, const Vec3& dir) { return h.p + h.n * (dot(dir, h.n) >= 0.0f ? kEps : -kEps); }

int threadCount(int requested) {
    if (requested > 0) return requested;
    const unsigned hc = std::thread::hardware_concurrency();
    return hc > 0 ? static_cast<int>(hc) : 4;
}

// Runs fn(row) for every row on 'threads' workers (rows handed out dynamically).
template <typename F>
void parallelRows(int rows, int threads, F fn) {
    threads = std::min(threadCount(threads), std::max(rows, 1));
    if (threads <= 1) {
        for (int y = 0; y < rows; ++y) fn(y);
        return;
    }
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&]() {
            for (int y = next++; y < rows; y = next++) fn(y);
        });
    for (std::thread& t : pool) t.join();
}

}  // namespace

const char* rtModeName(RtMode m) {
    switch (m) {
        case RtMode::Raster: return "RASTER";
        case RtMode::Hybrid: return "HYBRID RT";
        case RtMode::Whitted: return "WHITTED RT";
        case RtMode::PathTrace: return "PATH TRACING";
        case RtMode::LightTrace: return "LIGHT TRACING";
    }
    return "?";
}

const char* rtModeCaption(RtMode m) {
    switch (m) {
        case RtMode::Raster: return "rasterised (shadow maps, SSR)";
        case RtMode::Hybrid: return "raster G-buffer + ray-traced soft shadows and reflections";
        case RtMode::Whitted: return "recursive: reflection, refraction, Fresnel, shadow rays";
        case RtMode::PathTrace: return "progressive Monte Carlo: NEE, importance sampling, Russian roulette, denoised";
        case RtMode::LightTrace: return "reverse: light paths from the sun / moon / lamps splatted to the camera";
    }
    return "";
}

const char* rayKindName(RayKind k) {
    switch (k) {
        case RayKind::Primary: return "primary";
        case RayKind::Shadow: return "shadow";
        case RayKind::Reflection: return "reflection";
        case RayKind::Refraction: return "refraction";
        case RayKind::Bounce: return "bounce";
        case RayKind::Light: return "light";
        case RayKind::Connect: return "connect";
    }
    return "?";
}

// ------------------------------------------------------------------------------------------------ camera
Ray RtCamera::primary(float u, float v) const {
    const float x = (2.0f * u - 1.0f) * tanHalfY * aspect, y = (1.0f - 2.0f * v) * tanHalfY;
    Ray r;
    r.o = position;
    r.d = normalize(forward + right * x + up * y);
    return r;
}

bool RtCamera::project(const Vec3& p, float& u, float& v) const {
    const Vec3 d = p - position;
    const float z = dot(d, forward);
    if (z <= 1e-4f) return false;
    const float x = dot(d, right) / z, y = dot(d, up) / z;
    u = 0.5f + 0.5f * x / (tanHalfY * aspect);
    v = 0.5f - 0.5f * y / tanHalfY;
    return u >= 0.0f && u < 1.0f && v >= 0.0f && v < 1.0f;
}

RtCamera RtCamera::fromView(const Mat4& view, const Mat4& proj) {
    RtCamera c;
    const Mat4 inv = inverse(view);
    c.position = transformPoint(inv, {0, 0, 0});
    c.right = normalize(transformDirection(inv, {1, 0, 0}));
    c.up = normalize(transformDirection(inv, {0, 1, 0}));
    c.forward = normalize(transformDirection(inv, {0, 0, -1}));
    c.tanHalfY = 1.0f / proj.at(1, 1);
    c.aspect = proj.at(1, 1) / proj.at(0, 0);
    return c;
}

// ------------------------------------------------------------------------------------------------ lights
bool sampleLight(const RtLight& l, const Vec3& p, float u1, float u2, bool soft, LightSample& s) {
    if (l.type == LightType::Directional) {
        const Vec3 axis = -l.direction;
        s.wi = soft && l.radius > 0.0f ? sampleCone(axis, std::cos(l.radius), u1, u2) : axis;
        s.E = l.color * PI;
        s.dist = 1e30f;
        return luminance(s.E) > 0.0f;
    }
    Vec3 pos = l.position;
    if (soft && l.radius > 0.0f) pos = pos + sampleSphere(u1, u2) * l.radius;
    const Vec3 d = pos - p;
    const float dist = std::max(length(d), 1e-3f);
    s.wi = d / dist;
    s.dist = dist;
    float att = 0.0f;
    switch (l.type) {
        case LightType::Point: att = 1.0f / (1.0f + l.linear * dist + l.quadratic * dist * dist); break;
        case LightType::Spot: {
            const float cone = clampf((dot(-s.wi, l.direction) - l.cosOuter) / std::max(l.cosInner - l.cosOuter, 1e-4f), 0.0f, 1.0f);
            att = cone * cone / (1.0f + l.linear * dist + l.quadratic * dist * dist);
            break;
        }
        case LightType::Practical: {
            const float x = dist / std::max(l.range, 1e-3f);
            const float window = clampf(1.0f - x * x * x * x, 0.0f, 1.0f);
            att = window * window / (1.0f + dist * dist);
            if (l.cosOuter > -1.5f) att *= smoothstepf(l.cosOuter, l.cosOuter + 0.1f, dot(-s.wi, l.direction));
            break;
        }
        default: break;
    }
    s.E = l.color * (PI * att);
    return att > 0.0f;
}

// ------------------------------------------------------------------------------------------------ BSDF
Vec3 evalBsdf(const RtMaterial& m, const Vec3& n, const Vec3& wo, const Vec3& wi) {
    const float nl = dot(n, wi), nv = dot(n, wo);
    if (nl <= 0.0f || nv <= 0.0f) return {0, 0, 0};
    if (m.specular <= 0.0f && m.metalness <= 0.0f) return m.albedo / PI;   // pure Lambert (no Fresnel lobe at all)
    const Vec3 h = normalize(wo + wi);
    const float nh = std::max(dot(n, h), 0.0f), vh = std::max(dot(wo, h), 0.0f);
    const float alpha = std::max(m.roughness * m.roughness, 2e-3f);
    const Vec3 f0 = lerp(Vec3{m.specular, m.specular, m.specular}, m.albedo, m.metalness);
    const Vec3 F = fresnelSchlick(vh, f0);
    const Vec3 spec = F * (ggxD(nh, alpha) * smithG1(nv, alpha) * smithG1(nl, alpha) / (4.0f * nv * nl));
    const Vec3 diff = mul(Vec3{1, 1, 1} - F, m.albedo) * ((1.0f - m.metalness) / PI);
    return diff + spec;
}

float specularProbability(const RtMaterial& m, const Vec3& n, const Vec3& wo) {
    if (m.specular <= 0.0f && m.metalness <= 0.0f) return 0.0f;
    const Vec3 f0 = lerp(Vec3{m.specular, m.specular, m.specular}, m.albedo, m.metalness);
    const float fs = luminance(fresnelSchlick(std::max(dot(n, wo), 0.0f), f0));
    const float fd = luminance(m.albedo) * (1.0f - m.metalness) * (1.0f - fs);
    return clampf(fs / std::max(fs + fd, 1e-4f), 0.1f, 0.9f);
}

bool sampleBsdf(const RtMaterial& m, const Hit& h, const Vec3& wo, Rng& rng, BsdfSample& s) {
    const Vec3& n = h.n;
    if (m.transmission > 0.0f) {   // smooth dielectric: reflect with probability F, refract otherwise
        const float cosi = std::max(dot(wo, n), 0.0f);
        const float eta = h.front ? 1.0f / m.ior : m.ior;
        const float F = fresnelDielectric(cosi, eta);
        s.specular = true;
        Vec3 t;
        if (rng.uniform() < F || !refractDir(-wo, n, eta, t)) {
            s.wi = reflectDir(-wo, n);
            s.weight = {1, 1, 1};
        } else {
            s.wi = t;
            s.weight = m.tint * m.transmission;
        }
        return true;
    }
    const float ps = specularProbability(m, n, wo);
    const float alpha = std::max(m.roughness * m.roughness, 2e-3f);
    const float u1 = rng.uniform(), u2 = rng.uniform();
    if (rng.uniform() < ps) {
        const Vec3 hv = sampleGgxHalf(n, alpha, u1, u2);
        s.wi = reflectDir(-wo, hv);
        s.specular = alpha < 0.01f;
    } else {
        s.wi = sampleCosine(n, u1, u2);
        s.specular = false;
    }
    const float nl = dot(n, s.wi);
    if (nl <= 0.0f) return false;
    const Vec3 hv = normalize(wo + s.wi);
    const float nh = std::max(dot(n, hv), 1e-6f), vh = std::max(dot(wo, hv), 1e-6f);
    const float pdf = ps * ggxD(nh, alpha) * nh / (4.0f * vh) + (1.0f - ps) * nl / PI;
    if (pdf <= 1e-8f) return false;
    s.weight = evalBsdf(m, n, wo, s.wi) * (nl / pdf);
    return true;
}

// ------------------------------------------------------------------------------------------------ tracer
Vec3 Tracer::sky(const Vec3& d) const {
    Vec3 L = s_.env.radiance(d);
    if (dot(d, s_.env.discDir) > s_.env.discCos) L = L + s_.env.discRadiance;
    return L;
}

Vec3 Tracer::ambient(const Vec3& n) const {
    const RtEnv& e = s_.env;
    return e.bounce + (e.zenith - e.bounce) * (0.5f + 0.5f * n.y);
}

Vec3 Tracer::directLight(const Hit& h, const RtMaterial& m, const Vec3& wo, Rng* rng, RayTree* tree, int parent,
                         const Vec3& weight, int depth) const {
    Vec3 L;
    for (size_t i = 0; i < s_.lights.size(); ++i) {
        const RtLight& l = s_.lights[i];
        LightSample ls;
        const bool soft = rng && ts_.softShadows;
        const float u1 = soft ? rng->uniform() : 0.5f, u2 = soft ? rng->uniform() : 0.5f;
        if (!sampleLight(l, h.p, u1, u2, soft, ls)) continue;
        const bool leaf = m.surface == 5;   // Surface::Foliage: wrapped Lambert (light through thin leaves), as on the GPU
        const float nl = leaf ? (dot(h.n, ls.wi) + 0.5f) / 1.5f : dot(h.n, ls.wi);
        if (nl <= 0.0f) continue;
        const Vec3 f = leaf ? m.albedo / PI : evalBsdf(m, h.n, wo, ls.wi);
        if (maxComponent(f) <= 0.0f) continue;
        Ray sr;
        sr.o = offsetPoint(h, ls.wi);
        sr.d = ls.wi;
        sr.tmax = ls.dist >= 1e29f ? 1e30f : ls.dist * 0.999f - 0.02f;
        const uint32_t skip = kNoShadow | kLightProxy | (l.type == LightType::Spot ? kLightHolder : 0u);
        const Vec3 T = s_.transmittance(sr, skip, &stats);
        const Vec3 c = mul(mul(f, ls.E), T) * nl;
        L = L + c;
        if (tree) {
            RayNode node;
            node.kind = RayKind::Shadow;
            node.parent = parent;
            node.depth = depth + 1;
            node.from = sr.o;
            node.to = ls.dist >= 1e29f ? sr.o + ls.wi * 25.0f : sr.o + ls.wi * ls.dist;
            node.t = ls.dist >= 1e29f ? 1e30f : ls.dist;
            node.hit = maxComponent(T) > 0.0f;
            node.weight = weight;
            node.contribution = mul(weight, c);
            static const char* names[] = {"key light", "point light", "searchlight", "lamp"};
            node.object = names[static_cast<int>(l.type)];
            tree->nodes.push_back(node);
        }
    }
    return L;
}

Vec3 Tracer::whitted(const Ray& r, Rng* rng, RayTree* tree) const {
    return whittedRec(r, 0, rng, tree, -1, {1, 1, 1}, RayKind::Primary);
}

Vec3 Tracer::whittedRec(const Ray& r, int depth, Rng* rng, RayTree* tree, int parent, const Vec3& weight, RayKind kind) const {
    Hit h;
    const bool hit = s_.intersect(r, h, &stats);
    int self = -1;
    if (tree) {
        RayNode node;
        node.kind = kind;
        node.parent = parent;
        node.depth = depth;
        node.from = r.o;
        node.to = hit ? h.p : r.o + r.d * 40.0f;
        node.t = hit ? h.t : 1e30f;
        node.hit = hit;
        node.weight = weight;
        node.object = hit ? s_.name(h) : "sky";
        self = static_cast<int>(tree->nodes.size());
        tree->nodes.push_back(node);
    }
    Vec3 L;
    if (!hit) {
        L = depth == 0 || kind == RayKind::Reflection || kind == RayKind::Refraction ? sky(r.d) : s_.env.radiance(r.d);
    } else {
        const RtMaterial& m = s_.material(h);
        const Vec3 wo = -r.d;
        L = m.emission;
        if (m.transmission > 0.0f) {
            const float cosi = std::max(dot(wo, h.n), 0.0f);
            const float eta = h.front ? 1.0f / m.ior : m.ior;
            const float F = fresnelDielectric(cosi, eta);
            if (depth < ts_.whittedDepth) {
                Ray rr;
                rr.d = reflectDir(r.d, h.n);
                rr.o = offsetPoint(h, rr.d);
                L = L + whittedRec(rr, depth + 1, rng, tree, self, weight * F, RayKind::Reflection) * F;
                Vec3 td;
                if (F < 1.0f && refractDir(r.d, h.n, eta, td)) {
                    Ray tr;
                    tr.d = td;
                    tr.o = offsetPoint(h, td);
                    const Vec3 k = m.tint * (m.transmission * (1.0f - F));
                    L = L + mul(whittedRec(tr, depth + 1, rng, tree, self, mul(weight, k), RayKind::Refraction), k);
                }
            }
        } else {
            L = L + directLight(h, m, wo, rng, tree, self, weight, depth);
            float ao = 1.0f;
            if (ts_.ambientOcclusion && rng) {
                Ray ar;
                ar.d = sampleCosine(h.n, rng->uniform(), rng->uniform());
                ar.o = offsetPoint(h, ar.d);
                ar.tmax = 1.5f;
                ao = maxComponent(s_.transmittance(ar, kNoShadow | kLightProxy, &stats)) > 0.0f ? 1.0f : 0.25f;
            }
            L = L + mul(m.albedo, ambient(h.n)) * ((1.0f - m.metalness) * ao);
            // Mirror term for smooth surfaces: Schlick Fresnel with F0 = mix(0.04, albedo, metal), fading with roughness.
            if (m.roughness < 0.4f && depth < ts_.whittedDepth) {
                const Vec3 f0 = lerp(Vec3{m.specular, m.specular, m.specular}, m.albedo, m.metalness);
                const float gloss = (0.4f - m.roughness) / 0.4f;
                const Vec3 kr = fresnelSchlick(std::max(dot(wo, h.n), 0.0f), f0) * (gloss * gloss);
                if (maxComponent(kr) > 0.01f) {
                    Ray rr;
                    rr.d = reflectDir(r.d, h.n);
                    rr.o = offsetPoint(h, rr.d);
                    L = L + mul(whittedRec(rr, depth + 1, rng, tree, self, mul(weight, kr), RayKind::Reflection), kr);
                }
            }
        }
        if (depth == 0 && s_.env.fogDensity > 0.0f) {
            const float x = s_.env.fogDensity * h.t;
            L = lerp(L, s_.env.fogColor, 1.0f - std::exp(-x * x));
        }
    }
    if (tree) tree->nodes[self].contribution = mul(weight, L);
    return L;
}

Vec3 Tracer::path(const Ray& r0, Rng& rng, PixelAov* aov) const {
    Vec3 L, beta{1, 1, 1};
    Ray r = r0;
    bool specular = true;
    float primaryT = -1.0f;
    for (int bounce = 0; bounce <= ts_.maxBounces; ++bounce) {
        Hit h;
        if (!s_.intersect(r, h, &stats)) {
            L = L + mul(beta, bounce == 0 || specular ? sky(r.d) : s_.env.radiance(r.d));
            break;
        }
        const RtMaterial& m = s_.material(h);
        const uint32_t fl = s_.flags(h);
        if (bounce == 0) {
            primaryT = h.t;
            if (aov) {
                aov->albedo = m.transmission > 0.0f ? m.tint : m.albedo;
                aov->normal = h.n;
                aov->depth = h.t;
            }
        }
        // Lamp bulbs are lights through NEE: their emission only counts when seen directly or in a mirror.
        if (!(fl & kLightProxy) || specular) L = L + mul(beta, m.emission);
        const Vec3 wo = -r.d;
        if (m.transmission <= 0.0f) L = L + mul(beta, directLight(h, m, wo, &rng));
        BsdfSample bs;
        if (!sampleBsdf(m, h, wo, rng, bs)) break;
        beta = mul(beta, bs.weight);
        specular = bs.specular;
        r.o = offsetPoint(h, bs.wi);
        r.d = bs.wi;
        r.tmin = 1e-4f;
        r.tmax = 1e30f;
        if (bounce >= ts_.rrStart) {
            const float q = std::max(0.05f, 1.0f - maxComponent(beta));
            if (rng.uniform() < q) break;
            beta = beta / (1.0f - q);
        }
    }
    if (primaryT > 0.0f && s_.env.fogDensity > 0.0f) {
        const float x = s_.env.fogDensity * primaryT;
        L = lerp(L, s_.env.fogColor, 1.0f - std::exp(-x * x));
    }
    return L;
}

std::vector<float> Tracer::emitterPower() const {
    const float R = ts_.emitRadius;
    std::vector<float> p;
    for (const RtLight& l : s_.lights) {
        const float e = PI * luminance(l.color);
        switch (l.type) {
            case LightType::Directional: p.push_back(e * PI * R * R); break;
            case LightType::Point: p.push_back(e * 4.0f * PI); break;
            case LightType::Spot: p.push_back(e * 2.0f * PI * (1.0f - l.cosOuter)); break;
            case LightType::Practical: p.push_back(e * 4.0f * PI); break;
        }
    }
    const float skyL = 0.5f * (luminance(s_.env.zenith) + luminance(s_.env.bounce));
    p.push_back(skyL * PI * PI * R * R);
    return p;
}

int Tracer::pickEmitter(float u, float& pdf) const {
    const std::vector<float> p = emitterPower();
    float total = 0.0f;
    for (float v : p) total += v;
    if (total <= 0.0f) {
        pdf = 0.0f;
        return -1;
    }
    float acc = 0.0f;
    for (size_t i = 0; i < p.size(); ++i) {
        acc += p[i];
        if (u * total < acc || i + 1 == p.size()) {
            pdf = p[i] / total;
            return pdf > 0.0f ? static_cast<int>(i) : -1;
        }
    }
    pdf = 0.0f;
    return -1;
}

void Tracer::lightPath(const RtCamera& cam, int w, int h, Rng& rng, const std::function<void(float, float, const Vec3&)>& splat) const {
    float pl;
    const int li = pickEmitter(rng.uniform(), pl);
    if (li < 0) return;
    const float R = ts_.emitRadius;
    Ray r;
    Vec3 beta;
    int attType = -1;   // first segment: replace the 1/d^2 of the photon density by the light's attenuation law
    const RtLight* light = li < static_cast<int>(s_.lights.size()) ? &s_.lights[li] : nullptr;
    const float u1 = rng.uniform(), u2 = rng.uniform(), u3 = rng.uniform(), u4 = rng.uniform();
    if (!light || light->type == LightType::Directional) {
        Vec3 travel, irr;
        float pdfDir = 1.0f;
        if (light) {
            travel = light->direction;
            irr = light->color * PI;
        } else {   // sky: a direction in the upper hemisphere (uniform), its radiance arrives along -d
            Vec3 d = sampleSphere(u3, u4);
            d.y = std::fabs(d.y);
            travel = -d;
            irr = s_.env.radiance(d);
            pdfDir = 1.0f / (2.0f * PI);
        }
        float dx, dy;
        sampleDisc(u1, u2, dx, dy);
        Vec3 t, b;
        basis(travel, t, b);
        r.o = ts_.emitCentre - travel * 250.0f + (t * dx + b * dy) * R;
        r.d = travel;
        beta = irr * (PI * R * R / (pdfDir * pl));
    } else {
        r.o = light->position;
        float pdfDir;
        if (light->type == LightType::Spot) {
            r.d = sampleCone(light->direction, light->cosOuter, u1, u2);
            pdfDir = 1.0f / (2.0f * PI * (1.0f - light->cosOuter));
            const float cone = clampf((dot(r.d, light->direction) - light->cosOuter) / std::max(light->cosInner - light->cosOuter, 1e-4f), 0.0f, 1.0f);
            beta = light->color * (PI * cone * cone / (pdfDir * pl));
        } else {
            r.d = sampleSphere(u1, u2);
            pdfDir = 1.0f / (4.0f * PI);
            float k = 1.0f;
            if (light->type == LightType::Practical && light->cosOuter > -1.5f)
                k = smoothstepf(light->cosOuter, light->cosOuter + 0.1f, dot(r.d, light->direction));
            beta = light->color * (PI * k / (pdfDir * pl));
        }
        attType = static_cast<int>(light->type);
        r.o = r.o + r.d * 0.03f;   // leave the bulb
    }
    const float tanX = cam.tanHalfY * cam.aspect;
    const float area = 4.0f * tanX * cam.tanHalfY;   // image plane at distance 1
    const float npix = static_cast<float>(w) * static_cast<float>(h);
    for (int bounce = 0; bounce <= ts_.maxBounces; ++bounce) {
        Hit hh;
        if (maxComponent(beta) <= 0.0f || !s_.intersect(r, hh, &stats)) return;
        if (bounce == 0 && attType >= 0) {
            const float d = hh.t + 0.03f;
            float att;
            if (attType == static_cast<int>(LightType::Practical)) {
                const float x = d / std::max(light->range, 1e-3f);
                const float window = clampf(1.0f - x * x * x * x, 0.0f, 1.0f);
                att = window * window / (1.0f + d * d);
            } else {
                att = 1.0f / (1.0f + light->linear * d + light->quadratic * d * d);
            }
            beta = beta * (att * d * d);
        }
        const RtMaterial& m = s_.material(hh);
        const Vec3 wi = -r.d;   // towards where the light came from
        if (m.transmission <= 0.0f && !(m.metalness > 0.5f && m.roughness < 0.1f)) {   // (near-)mirrors: delta for the camera
            // Connect to the camera (next-event estimation towards the sensor).
            const Vec3 toCam = cam.position - hh.p;
            const float dist = length(toCam);
            const Vec3 wo = toCam / std::max(dist, 1e-6f);
            float u, v;
            const float cosCam = dot(-wo, cam.forward);
            if (cosCam > 0.0f && cam.project(hh.p, u, v) && dot(wo, hh.n) > 0.0f) {
                const Vec3 f = evalBsdf(m, hh.n, wo, wi);
                if (maxComponent(f) > 0.0f) {
                    Ray cr;
                    cr.o = offsetPoint(hh, wo);
                    cr.d = wo;
                    cr.tmax = dist * 0.999f;
                    const Vec3 T = s_.transmittance(cr, kNoShadow | kLightProxy, &stats);
                    if (maxComponent(T) > 0.0f) {
                        const float c2 = cosCam * cosCam;
                        const float We = npix / (area * c2 * c2);
                        splat(u, v, mul(mul(beta, f), T) * (dot(wo, hh.n) * cosCam / (dist * dist) * We));
                    }
                }
            }
        }
        BsdfSample bs;
        if (!sampleBsdf(m, hh, wi, rng, bs)) return;
        beta = mul(beta, bs.weight);
        r.o = offsetPoint(hh, bs.wi);
        r.d = bs.wi;
        r.tmin = 1e-4f;
        r.tmax = 1e30f;
        if (bounce >= ts_.rrStart) {
            const float q = std::max(0.05f, 1.0f - maxComponent(bs.weight));
            if (rng.uniform() < q) return;
            beta = beta / (1.0f - q);
        }
    }
}

// ------------------------------------------------------------------------------------------------ images
Vec3 Image::mean() const {
    double s[3] = {0, 0, 0};
    for (const Vec3& p : px) {
        s[0] += p.x;
        s[1] += p.y;
        s[2] += p.z;
    }
    const double n = px.empty() ? 1.0 : static_cast<double>(px.size());
    return {static_cast<float>(s[0] / n), static_cast<float>(s[1] / n), static_cast<float>(s[2] / n)};
}

void renderWhitted(const RtScene& s, const RtCamera& cam, Image& out, int spp, uint32_t seed, const TraceSettings& ts, int threads) {
    const Tracer tr(s, ts);
    parallelRows(out.h, threads, [&](int y) {
        for (int x = 0; x < out.w; ++x) {
            Rng rng(seed, static_cast<uint64_t>(y) * out.w + x);
            Vec3 c;
            for (int k = 0; k < spp; ++k) {
                const float jx = spp > 1 ? rng.uniform() : 0.5f, jy = spp > 1 ? rng.uniform() : 0.5f;
                c = c + tr.whitted(cam.primary((x + jx) / out.w, (y + jy) / out.h), spp > 1 ? &rng : nullptr);
            }
            out.at(x, y) = c / static_cast<float>(spp);
        }
    });
}

void renderPath(const RtScene& s, const RtCamera& cam, Image& out, int spp, uint32_t seed, const TraceSettings& ts, Image* albedo,
                Image* normal, std::vector<float>* depth, int threads) {
    const Tracer tr(s, ts);
    if (albedo) *albedo = Image(out.w, out.h);
    if (normal) *normal = Image(out.w, out.h);
    if (depth) depth->assign(static_cast<size_t>(out.w) * out.h, 1e30f);
    parallelRows(out.h, threads, [&](int y) {
        for (int x = 0; x < out.w; ++x) {
            Rng rng(seed, static_cast<uint64_t>(y) * out.w + x);
            Vec3 c;
            PixelAov aov;
            for (int k = 0; k < spp; ++k) {
                const float jx = rng.uniform(), jy = rng.uniform();
                c = c + tr.path(cam.primary((x + jx) / out.w, (y + jy) / out.h), rng, k == 0 ? &aov : nullptr);
            }
            out.at(x, y) = c / static_cast<float>(spp);
            if (albedo) albedo->at(x, y) = aov.albedo;
            if (normal) normal->at(x, y) = aov.normal;
            if (depth) (*depth)[static_cast<size_t>(y) * out.w + x] = aov.depth;
        }
    });
}

void renderLight(const RtScene& s, const RtCamera& cam, Image& out, long long paths, uint32_t seed, const TraceSettings& ts, int threads) {
    const Tracer tr(s, ts);
    const int chunks = 256;
    std::vector<Image> partial(static_cast<size_t>(threadCount(threads)), Image(out.w, out.h));
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    auto work = [&](int worker) {
        Image& img = partial[worker];
        auto splat = [&](float u, float v, const Vec3& val) {
            const int x = std::min(static_cast<int>(u * img.w), img.w - 1), y = std::min(static_cast<int>(v * img.h), img.h - 1);
            img.at(x, y) = img.at(x, y) + val;
        };
        for (int c = next++; c < chunks; c = next++) {
            Rng rng(seed, 0x9E3779B9u + static_cast<uint64_t>(c));
            const long long n = paths / chunks + (c < paths % chunks ? 1 : 0);
            for (long long k = 0; k < n; ++k) tr.lightPath(cam, img.w, img.h, rng, splat);
        }
    };
    const int nt = static_cast<int>(partial.size());
    if (nt <= 1) work(0);
    else {
        for (int t = 0; t < nt; ++t) pool.emplace_back(work, t);
        for (std::thread& t : pool) t.join();
    }
    // Chunk -> worker assignment is dynamic, but addition order inside a pixel only changes rounding.
    out = Image(out.w, out.h);
    const float inv = 1.0f / static_cast<float>(std::max(paths, 1LL));
    for (const Image& p : partial)
        for (size_t i = 0; i < out.px.size(); ++i) out.px[i] = out.px[i] + p.px[i] * inv;
}

void denoiseAtrous(Image& color, const Image& albedo, const Image& normal, const std::vector<float>& depth, int iterations, float sigmaColor) {
    const int w = color.w, h = color.h;
    auto demod = [](const Vec3& c, const Vec3& a) {
        return Vec3{c.x / std::max(a.x, 0.02f), c.y / std::max(a.y, 0.02f), c.z / std::max(a.z, 0.02f)};
    };
    Image cur(w, h), nxt(w, h);
    for (int i = 0; i < w * h; ++i) cur.px[i] = demod(color.px[i], albedo.px[i]);
    const float kernel[5] = {1.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 4, 1.0f / 16};
    for (int it = 0; it < iterations; ++it) {
        const int step = 1 << it;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t ci = static_cast<size_t>(y) * w + x;
                const Vec3 c0 = cur.px[ci], n0 = normal.px[ci];
                const float z0 = depth[ci], l0 = luminance(c0);
                Vec3 sum;
                float wsum = 0.0f;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int xx = x + dx * step, yy = y + dy * step;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        const size_t j = static_cast<size_t>(yy) * w + xx;
                        const float wn = std::pow(std::max(0.0f, dot(n0, normal.px[j])), 32.0f);
                        const float wz = std::exp(-std::fabs(z0 - depth[j]) / (0.02f * std::max(z0, 0.1f) * step + 1e-3f));
                        const float wl = std::exp(-std::fabs(l0 - luminance(cur.px[j])) / (sigmaColor * (l0 + 0.02f) + 1e-4f));
                        const float wgt = kernel[dx + 2] * kernel[dy + 2] * wn * wz * wl;
                        sum = sum + cur.px[j] * wgt;
                        wsum += wgt;
                    }
                nxt.px[ci] = wsum > 0.0f ? sum / wsum : c0;
            }
        std::swap(cur, nxt);
    }
    for (int i = 0; i < w * h; ++i) color.px[i] = mul(cur.px[i], Vec3{std::max(albedo.px[i].x, 0.02f), std::max(albedo.px[i].y, 0.02f),
                                                                       std::max(albedo.px[i].z, 0.02f)});
}

RayTree debugTrace(const RtScene& s, const RtCamera& cam, float u, float v, const TraceSettings& ts) {
    TraceSettings t = ts;
    t.softShadows = false;
    t.ambientOcclusion = false;
    const Tracer tr(s, t);
    RayTree tree;
    tree.u = u;
    tree.v = v;
    const Ray r = cam.primary(u, v);
    tree.radiance = tr.whitted(r, nullptr, &tree);
    // One sampled diffuse bounce from the primary hit (what the path tracer would do next), for information.
    Hit h;
    if (s.intersect(r, h) && s.material(h).transmission <= 0.0f) {
        Rng rng(7, 11);
        const Vec3 d = sampleCosine(h.n, rng.uniform(), rng.uniform());
        Ray br;
        br.d = d;
        br.o = h.p + h.n * kEps;
        Hit bh;
        const bool hit = s.intersect(br, bh);
        RayNode node;
        node.kind = RayKind::Bounce;
        node.parent = 0;
        node.depth = 1;
        node.from = br.o;
        node.to = hit ? bh.p : br.o + d * 8.0f;
        node.t = hit ? bh.t : 1e30f;
        node.hit = hit;
        node.weight = s.material(h).albedo;
        node.object = hit ? s.name(bh) : "sky";
        node.informational = true;
        node.contribution = mul(node.weight, hit ? s.material(bh).emission : s.env.radiance(d));
        tree.nodes.push_back(node);
    }
    return tree;
}

ReflectionSample traceReflection(const RtScene& s, const RtCamera& cam, float u, float v, const TraceSettings& ts) {
    ReflectionSample r;
    const Ray pr = cam.primary(u, v);
    r.view = pr.d;
    Hit h;
    if (!s.intersect(pr, h)) return r;
    r.hit = true;
    r.point = h.p;
    r.normal = h.n;
    r.surface = s.name(h);
    const RtMaterial& m = s.material(h);
    const Vec3 f0 = lerp(Vec3{m.specular, m.specular, m.specular}, m.albedo, m.metalness);
    r.fresnel = luminance(fresnelSchlick(std::max(dot(-pr.d, h.n), 0.0f), f0));
    r.reflectDir = reflectDir(pr.d, h.n);
    Ray rr;
    rr.d = r.reflectDir;
    rr.o = h.p + h.n * kEps;
    Hit rh;
    r.reflectedHit = s.intersect(rr, rh);
    r.reflectedPoint = r.reflectedHit ? rh.p : rr.o + rr.d * 40.0f;
    r.reflectedObject = r.reflectedHit ? s.name(rh) : "sky";
    TraceSettings t = ts;
    t.softShadows = false;
    t.ambientOcclusion = false;
    r.radiance = Tracer(s, t).whitted(rr);
    return r;
}

void tracePhotons(const RtScene& s, int count, uint32_t seed, std::vector<Photon>& out, const TraceSettings& ts) {
    const Tracer tr(s, ts);
    out.clear();
    Rng rng(seed, 0xC0FFEE);
    for (int i = 0; i < count; ++i) {
        float pl;
        const int li = tr.pickEmitter(rng.uniform(), pl);
        if (li < 0 || li >= static_cast<int>(s.lights.size())) continue;   // sky photons are not stored
        const RtLight& l = s.lights[li];
        Ray r;
        Vec3 beta;
        const float u1 = rng.uniform(), u2 = rng.uniform();
        if (l.type == LightType::Directional) {
            float dx, dy;
            sampleDisc(u1, u2, dx, dy);
            Vec3 t, b;
            basis(l.direction, t, b);
            r.o = ts.emitCentre - l.direction * 250.0f + (t * dx + b * dy) * ts.emitRadius;
            r.d = l.direction;
            beta = l.color * (PI * PI * ts.emitRadius * ts.emitRadius / pl);
        } else {
            r.o = l.position;
            r.d = l.type == LightType::Spot ? sampleCone(l.direction, l.cosOuter, u1, u2) : sampleSphere(u1, u2);
            r.o = r.o + r.d * 0.03f;
            beta = l.color * (PI * 4.0f * PI / pl);
        }
        bool anySpecular = false, allSpecular = true;   // caustic = L S+ D
        for (int bounce = 0; bounce <= ts.maxBounces; ++bounce) {
            Hit h;
            if (!s.intersect(r, h)) break;
            const RtMaterial& m = s.material(h);
            const bool delta = m.transmission > 0.0f || (m.metalness > 0.5f && m.roughness < 0.15f);
            if (!delta) out.push_back({h.p, beta / static_cast<float>(count), r.d, bounce, anySpecular && allSpecular});
            BsdfSample bs;
            if (!sampleBsdf(m, h, -r.d, rng, bs)) break;
            anySpecular = anySpecular || delta;
            allSpecular = allSpecular && delta;
            beta = mul(beta, bs.weight);
            r.o = offsetPoint(h, bs.wi);
            r.d = bs.wi;
            if (bounce >= 2) {
                const float q = std::max(0.05f, 1.0f - maxComponent(bs.weight));
                if (rng.uniform() < q) break;
                beta = beta / (1.0f - q);
            }
        }
    }
}

}  // namespace ts::rt
