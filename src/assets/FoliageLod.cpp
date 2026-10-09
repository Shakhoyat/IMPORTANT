#include "assets/FoliageLod.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "meshoptimizer/meshoptimizer.h"

namespace ts::asset {

namespace {

uint32_t findRoot(std::vector<uint32_t>& parent, uint32_t x) {
    uint32_t r = x;
    while (parent[r] != r) r = parent[r];
    while (parent[x] != r) {
        const uint32_t n = parent[x];
        parent[x] = r;
        x = n;
    }
    return r;
}

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Per component: the anchor vertex and the component's triangles.
struct Cards {
    std::vector<uint32_t> triComp;   // component per triangle
    uint32_t count = 0;
    std::vector<uint32_t> anchor;    // anchor vertex per component
};

Cards cardsOf(const MeshPart& p, Anchor mode) {
    Cards c;
    c.triComp = triangleComponents(p.idx, p.vertexCount(), &c.count);
    c.anchor.assign(c.count, 0xFFFFFFFFu);
    std::vector<float> best(c.count, 1e30f);
    for (size_t t = 0; t < c.triComp.size(); ++t) {
        const uint32_t comp = c.triComp[t];
        for (int k = 0; k < 3; ++k) {
            const uint32_t v = p.idx[3 * t + k];
            const float x = p.pos[3 * v], y = p.pos[3 * v + 1], z = p.pos[3 * v + 2];
            const float key = mode == Anchor::TrunkAxis ? x * x + z * z : y;
            if (key < best[comp]) {
                best[comp] = key;
                c.anchor[comp] = v;
            }
        }
    }
    return c;
}

}  // namespace

bool isFoliageMaterial(const MaterialInfo& m) { return m.alphaMode != AlphaMode::Opaque; }

std::vector<uint32_t> triangleComponents(const std::vector<uint32_t>& idx, size_t vertexCount, uint32_t* count) {
    std::vector<uint32_t> parent(vertexCount);
    std::iota(parent.begin(), parent.end(), 0u);
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const uint32_t a = findRoot(parent, idx[t]), b = findRoot(parent, idx[t + 1]), c = findRoot(parent, idx[t + 2]);
        parent[b] = a;
        parent[findRoot(parent, c)] = a;
    }
    std::vector<uint32_t> label(vertexCount, 0xFFFFFFFFu);
    uint32_t n = 0;
    std::vector<uint32_t> out(idx.size() / 3);
    for (size_t t = 0; t < out.size(); ++t) {
        const uint32_t r = findRoot(parent, idx[3 * t]);
        if (label[r] == 0xFFFFFFFFu) label[r] = n++;
        out[t] = label[r];
    }
    if (count) *count = n;
    return out;
}

MeshPart thinCards(const MeshPart& part, float keep, float exponent, uint32_t seed, Anchor anchor, ThinStats* stats) {
    const Cards cards = cardsOf(part, anchor);
    keep = std::clamp(keep, 0.0f, 1.0f);
    const float grow = keep < 1.0f && keep > 0.0f ? std::pow(keep, -exponent) : 1.0f;
    // A card survives when its hash falls below 'keep': deterministic, and any lower level's cards are a subset of a
    // higher level's (the same hash threshold), so LOD transitions only remove cards.
    std::vector<uint8_t> kept(cards.count, 0);
    size_t nKept = 0;
    for (uint32_t c = 0; c < cards.count; ++c) {
        const float u = static_cast<float>(hash32(c * 2654435761u ^ seed) & 0xFFFFFF) / static_cast<float>(0x1000000);
        kept[c] = u < keep ? 1 : 0;
        nKept += kept[c];
    }
    MeshPart out;
    out.material = part.material;
    std::vector<uint32_t> remap(part.vertexCount(), 0xFFFFFFFFu);
    const bool hasColor = part.color.size() == part.vertexCount() * 4;
    for (size_t t = 0; t < cards.triComp.size(); ++t) {
        const uint32_t comp = cards.triComp[t];
        if (!kept[comp]) continue;
        const uint32_t a = cards.anchor[comp];
        for (int k = 0; k < 3; ++k) {
            const uint32_t v = part.idx[3 * t + k];
            if (remap[v] == 0xFFFFFFFFu) {
                remap[v] = static_cast<uint32_t>(out.vertexCount());
                for (int c = 0; c < 3; ++c)
                    out.pos.push_back(part.pos[3 * a + c] + (part.pos[3 * v + c] - part.pos[3 * a + c]) * grow);
                out.nrm.insert(out.nrm.end(), &part.nrm[3 * v], &part.nrm[3 * v] + 3);
                out.uv.insert(out.uv.end(), &part.uv[2 * v], &part.uv[2 * v] + 2);
                if (hasColor) out.color.insert(out.color.end(), &part.color[4 * v], &part.color[4 * v] + 4);
            }
            out.idx.push_back(remap[v]);
        }
    }
    if (stats) {
        stats->cardsIn = cards.count;
        stats->cardsKept = nKept;
        stats->scale = grow;
    }
    return out;
}

void compactPart(MeshPart& p) {
    if (p.idx.empty()) {
        p.pos.clear();
        p.nrm.clear();
        p.uv.clear();
        p.color.clear();
        return;
    }
    const size_t nv = p.vertexCount();
    std::vector<uint32_t> cache(p.idx.size());
    meshopt_optimizeVertexCache(cache.data(), p.idx.data(), p.idx.size(), nv);
    std::vector<uint32_t> remap(nv, 0xFFFFFFFFu);
    MeshPart o;
    o.material = p.material;
    const bool hasColor = p.color.size() == nv * 4;
    for (uint32_t& i : cache) {
        if (remap[i] == 0xFFFFFFFFu) {
            remap[i] = static_cast<uint32_t>(o.vertexCount());
            o.pos.insert(o.pos.end(), &p.pos[3 * i], &p.pos[3 * i] + 3);
            o.nrm.insert(o.nrm.end(), &p.nrm[3 * i], &p.nrm[3 * i] + 3);
            o.uv.insert(o.uv.end(), &p.uv[2 * i], &p.uv[2 * i] + 2);
            if (hasColor) o.color.insert(o.color.end(), &p.color[4 * i], &p.color[4 * i] + 4);
        }
        i = remap[i];
    }
    o.idx = std::move(cache);
    p = std::move(o);
}

MeshPart simplifyPart(const MeshPart& part, float ratio, float maxError, float* errorMetres, bool prune) {
    MeshPart out = part;
    if (errorMetres) *errorMetres = 0.0f;
    if (ratio >= 1.0f || part.idx.size() < 36) return out;
    const size_t nv = part.vertexCount();
    std::vector<float> attrs(nv * 5);
    for (size_t v = 0; v < nv; ++v) {
        attrs[5 * v + 0] = part.uv[2 * v];
        attrs[5 * v + 1] = part.uv[2 * v + 1];
        for (int c = 0; c < 3; ++c) attrs[5 * v + 2 + c] = part.nrm[3 * v + c];
    }
    const float weights[5] = {0.5f, 0.5f, 0.15f, 0.15f, 0.15f};
    const size_t target = std::max<size_t>(3, static_cast<size_t>(static_cast<float>(part.idx.size() / 3) * ratio) * 3);
    std::vector<uint32_t> dst(part.idx.size());
    float err = 0.0f;
    const size_t n = meshopt_simplifyWithAttributes(dst.data(), part.idx.data(), part.idx.size(), part.pos.data(), nv,
                                                    3 * sizeof(float), attrs.data(), 5 * sizeof(float), weights, 5, nullptr,
                                                    target, maxError, prune ? static_cast<unsigned>(meshopt_SimplifyPrune) : 0u, &err);
    dst.resize(n);
    out.idx = std::move(dst);
    if (errorMetres) *errorMetres = err * meshopt_simplifyScale(part.pos.data(), nv, 3 * sizeof(float));
    compactPart(out);
    return out;
}

void computeWindColors(Variant& v, const std::vector<bool>& foliageMaterial, Anchor anchor) {
    v.computeBounds();
    const float H = std::max(v.maxY - v.minY, 1e-3f), R = std::max(v.radius, 1e-3f);
    // Crown envelope: the largest distance from the trunk axis in each of 16 height bands (smoothed).
    constexpr int kBands = 16;
    float env[kBands] = {};
    for (const MeshPart& p : v.parts)
        for (size_t i = 0; i < p.vertexCount(); ++i) {
            const float y = (p.pos[3 * i + 1] - v.minY) / H;
            const int b = std::clamp(static_cast<int>(y * kBands), 0, kBands - 1);
            env[b] = std::max(env[b], std::sqrt(p.pos[3 * i] * p.pos[3 * i] + p.pos[3 * i + 2] * p.pos[3 * i + 2]));
        }
    float envS[kBands];
    for (int b = 0; b < kBands; ++b) envS[b] = std::max({env[b], b > 0 ? env[b - 1] : 0.0f, b + 1 < kBands ? env[b + 1] : 0.0f, 0.15f * R});
    for (MeshPart& p : v.parts) {
        const bool foliage = p.material >= 0 && p.material < static_cast<int>(foliageMaterial.size()) && foliageMaterial[p.material];
        const size_t nv = p.vertexCount();
        p.color.assign(nv * 4, 0);
        std::vector<float> flutter(nv, 0.0f);
        std::vector<uint8_t> phase(nv, 0);
        if (foliage && !p.idx.empty()) {
            const Cards cards = cardsOf(p, anchor);
            std::vector<float> size(cards.count, 1e-4f);
            for (size_t t = 0; t < cards.triComp.size(); ++t) {
                const uint32_t comp = cards.triComp[t], a = cards.anchor[comp];
                for (int k = 0; k < 3; ++k) {
                    const uint32_t i = p.idx[3 * t + k];
                    const float dx = p.pos[3 * i] - p.pos[3 * a], dy = p.pos[3 * i + 1] - p.pos[3 * a + 1], dz = p.pos[3 * i + 2] - p.pos[3 * a + 2];
                    flutter[i] = std::sqrt(dx * dx + dy * dy + dz * dz);
                    size[comp] = std::max(size[comp], flutter[i]);
                    phase[i] = static_cast<uint8_t>(hash32(comp + 977u) & 0xFF);
                }
            }
            for (size_t t = 0; t < cards.triComp.size(); ++t)
                for (int k = 0; k < 3; ++k) {
                    const uint32_t i = p.idx[3 * t + k];
                    flutter[i] = std::min(flutter[i] / size[cards.triComp[t]], 1.0f);
                }
        }
        for (size_t i = 0; i < nv; ++i) {
            const float x = p.pos[3 * i], y = (p.pos[3 * i + 1] - v.minY) / H, z = p.pos[3 * i + 2];
            const float r = std::sqrt(x * x + z * z);
            const int b = std::clamp(static_cast<int>(y * kBands), 0, kBands - 1);
            const float branch = std::clamp(r / R, 0.0f, 1.0f);
            const float depth = std::clamp(r / envS[b], 0.0f, 1.0f);   // 1 at the crown surface
            const float ao = std::clamp((0.35f + 0.65f * std::pow(depth, 0.8f)) * (0.72f + 0.28f * y), 0.0f, 1.0f);
            uint8_t* c = &p.color[4 * i];
            c[0] = static_cast<uint8_t>(branch * 255.0f + 0.5f);
            c[1] = static_cast<uint8_t>(flutter[i] * 255.0f + 0.5f);
            c[2] = phase[i];
            c[3] = static_cast<uint8_t>(ao * 255.0f + 0.5f);
        }
    }
}

std::vector<Variant> buildLodChain(const Variant& lod0, const std::vector<MaterialInfo>& materials, const LodSettings& s,
                                   std::vector<LodInfo>* info) {
    std::vector<Variant> chain;
    if (info) info->clear();
    for (int level = 0; level < 3; ++level) {
        Variant v;
        v.name = lod0.name + "_LOD" + std::to_string(level);
        LodInfo li;
        for (size_t pi = 0; pi < lod0.parts.size(); ++pi) {
            const MeshPart& p = lod0.parts[pi];
            const bool foliage = p.material >= 0 && p.material < static_cast<int>(materials.size()) && isFoliageMaterial(materials[p.material]);
            MeshPart q;
            if (level == 0) {
                q = p;
                compactPart(q);
            } else if (foliage) {
                ThinStats st;
                q = thinCards(p, s.keep[level] / std::max(s.keep[0], 1e-3f), s.thinExponent, 0x51EDu + static_cast<uint32_t>(pi) * 7919u,
                              s.anchor, &st);
                compactPart(q);
                li.cardScale = std::max(li.cardScale, st.scale);
            } else {
                float err = 0.0f;
                q = simplifyPart(p, s.solidRatio[level], s.solidError[level], &err, true);
                li.solidErrorMetres = std::max(li.solidErrorMetres, err);
            }
            if (!q.idx.empty()) v.parts.push_back(std::move(q));
        }
        v.computeBounds();
        li.triangles = v.triangles();
        if (info) info->push_back(li);
        chain.push_back(std::move(v));
    }
    return chain;
}

}  // namespace ts::asset
