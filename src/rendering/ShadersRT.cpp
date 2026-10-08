// GLSL 4.30 compute shaders of the GPU ray tracer (rendering/RayTracerGL). The scene lives in shader storage
// buffers with the std430 records of raytrace/GpuLayout.h; every function below mirrors its CPU reference in
// src/raytrace (Intersect.cpp, Heightfield.cpp, RtScene.cpp, RayTracer.cpp) so the two tracers agree.
#include "rendering/RayTracerGL.h"

namespace ts::glslrt {

// ---------------------------------------------------------------------------------------------------- common
const char* kCommon = R"(#version 430
#define PI 3.14159265358979
struct Prim { vec4 r0; vec4 r1; vec4 r2; ivec4 info; };          // world -> local rows; shape, flags, node
struct Mat { vec4 a; vec4 e; vec4 t; };                           // albedo+rough, emission+metal, transmit+ior
struct Node { vec3 lo; int leftFirst; vec3 hi; int count; };
struct Light { vec3 position; int type; vec3 direction; float radius; vec3 color; float range; vec4 att; };
layout(std430, binding = 0) readonly buffer PrimBuf { Prim prims[]; };
layout(std430, binding = 1) readonly buffer MatBuf { Mat mats[]; };
layout(std430, binding = 2) readonly buffer NodeBuf { Node nodes[]; };
layout(std430, binding = 3) readonly buffer LightBuf { Light lights[]; };

uniform int uNodeCount, uLightCount, uHasTerrain;
uniform sampler2D uHf;          // n x n: height, normal.x, normal.z, hole flag of the cell
uniform sampler2D uHfBlocks;    // blocks x blocks: min, max height
uniform int uHfN, uHfBlocksN;
uniform float uHfRadius, uHfCell;
uniform vec2 uHfY;              // height range of the whole field
uniform vec4 uTerrA, uTerrE, uTerrT;
uniform vec3 uZenith, uBounce, uFogColor;
uniform float uFogDensity;
uniform vec3 uCamPos, uCamFwd, uCamRight, uCamUp;
uniform float uTanHalfY, uAspect;
uniform uint uFrame;

const int TERRAIN = 1073741824;
const uint F_NOSHADOW = 1u, F_PROXY = 2u, F_HIDDEN = 8u, F_SKY = 16u, F_HOLDER = 32u, F_FOLIAGE = 64u;
const float EPS = 2e-3;

// ---- random numbers (PCG hash)
uint gSeed;
uint pcg(uint v) { uint s = v * 747796405u + 2891336453u; uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }
float rnd() { gSeed = pcg(gSeed); return float(gSeed >> 8) * (1.0 / 16777216.0); }

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
float maxc(vec3 c) { return max(c.r, max(c.g, c.b)); }

// ---- unit shapes (local space, d unnormalised; quadratics solved from the closest approach)
bool roots(float a, float b, float c, out float t0, out float t1) {
    t0 = t1 = 0.0;
    if (abs(a) < 1e-12) { if (abs(b) < 1e-12) return false; t0 = t1 = -c / (2.0 * b); return true; }
    float disc = b * b - a * c;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    float q = -(b + (b >= 0.0 ? s : -s));
    float r0 = q / a, r1 = abs(q) > 1e-20 ? c / q : r0;
    t0 = min(r0, r1); t1 = max(r0, r1);
    return true;
}
bool hitSphere(vec3 o0, vec3 d, float tmin, float tmax, out float t, out vec3 n) {
    t = 0.0; n = vec3(0.0);
    float ts = -dot(o0, d) / max(dot(d, d), 1e-30);
    vec3 o = o0 + d * ts;
    float t0, t1;
    if (!roots(dot(d, d), dot(o, d), dot(o, o) - 0.25, t0, t1)) return false;
    t0 += ts; t1 += ts;
    float r = t0 > tmin ? t0 : t1;
    if (r <= tmin || r >= tmax) return false;
    t = r; n = normalize(o0 + d * r);
    return true;
}
bool hitBox(vec3 o, vec3 d, float tmin, float tmax, out float t, out vec3 n) {
    t = 0.0; n = vec3(0.0);
    vec3 inv = 1.0 / mix(d, vec3(1e-12), lessThan(abs(d), vec3(1e-12)));
    vec3 ta = (-0.5 - o) * inv, tb = (0.5 - o) * inv;
    vec3 tn = min(ta, tb), tf = max(ta, tb);
    float tNear = max(tn.x, max(tn.y, tn.z)), tFar = min(tf.x, min(tf.y, tf.z));
    if (tNear > tFar) return false;
    float r; vec3 sel;
    if (tNear > tmin) { r = tNear; sel = vec3(equal(tn, vec3(tNear))) * -sign(d); }
    else { r = tFar; sel = vec3(equal(tf, vec3(tFar))) * sign(d); }
    if (r <= tmin || r >= tmax) return false;
    t = r;
    n = sel.x != 0.0 ? vec3(sel.x, 0, 0) : (sel.y != 0.0 ? vec3(0, sel.y, 0) : vec3(0, 0, sel.z));
    return true;
}
bool hitCylinder(vec3 o, vec3 d, float tmin, float tmax, out float t, out vec3 n) {
    bool found = false; float best = tmax; t = 0.0; n = vec3(0.0);
    float a = d.x * d.x + d.y * d.y;
    float ts = a > 1e-30 ? -(o.x * d.x + o.y * d.y) / a : 0.0;
    vec2 s = o.xy + d.xy * ts;
    float t0, t1;
    if (roots(a, dot(s, d.xy), dot(s, s) - 0.25, t0, t1)) {
        t0 += ts; t1 += ts;
        for (int k = 0; k < 2; ++k) {
            float r = k == 0 ? t0 : t1;
            if (r <= tmin || r >= best) continue;
            float z = o.z + d.z * r;
            if (z < -0.5 || z > 0.5) continue;
            best = r; vec3 p = o + d * r; n = normalize(vec3(p.xy, 0.0)); found = true; break;
        }
    }
    if (abs(d.z) > 1e-12) {
        for (int k = 0; k < 2; ++k) {
            float zc = k == 0 ? -0.5 : 0.5;
            float r = (zc - o.z) / d.z;
            if (r <= tmin || r >= best) continue;
            vec2 q = o.xy + d.xy * r;
            if (dot(q, q) > 0.25) continue;
            best = r; n = vec3(0, 0, sign(zc)); found = true;
        }
    }
    t = best;
    return found;
}
bool hitCone(vec3 o, vec3 d, float tmin, float tmax, out float t, out vec3 n) {
    bool found = false; float best = tmax; t = 0.0; n = vec3(0.0);
    float ts = -dot(o, d) / max(dot(d, d), 1e-30);
    vec3 s = o + d * ts;
    float k0 = 0.25 - 0.5 * s.z;
    float t0, t1;
    if (roots(d.x * d.x + d.y * d.y - 0.25 * d.z * d.z, s.x * d.x + s.y * d.y + 0.5 * k0 * d.z, s.x * s.x + s.y * s.y - k0 * k0, t0, t1)) {
        t0 += ts; t1 += ts;
        for (int k = 0; k < 2; ++k) {
            float r = k == 0 ? t0 : t1;
            if (r <= tmin || r >= best) continue;
            float z = o.z + d.z * r;
            if (z < -0.5 || z > 0.5) continue;
            best = r; vec3 p = o + d * r; n = normalize(vec3(p.xy, 0.5 * (0.25 - 0.5 * p.z))); found = true; break;
        }
    }
    if (abs(d.z) > 1e-12) {
        float r = (-0.5 - o.z) / d.z;
        vec2 q = o.xy + d.xy * r;
        if (r > tmin && r < best && dot(q, q) <= 0.25) { best = r; n = vec3(0, 0, -1); found = true; }
    }
    t = best;
    return found;
}
bool hitShape(int shape, vec3 o, vec3 d, float tmin, float tmax, out float t, out vec3 nl) {
    if (shape == 0) return hitSphere(o, d, tmin, tmax, t, nl);
    if (shape == 1) return hitBox(o, d, tmin, tmax, t, nl);
    if (shape == 2) return hitCylinder(o, d, tmin, tmax, t, nl);
    return hitCone(o, d, tmin, tmax, t, nl);
}
// Foliage stand-in (tree crowns, pine cones, bushes, camouflage nets): the surface is "alpha tested" by smooth 3D value
// noise in local space (11 lattice cells per unit, ~62 % covered), so crowns and nets get organic gaps, dappled shadows
// and a ragged silhouette; a ray through a gap continues to the far side of the same primitive. Same function as
// rt::leafCovered (raytrace/RtTypes.h).
float latticeHash(ivec3 c) { return float(pcg(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z) * 83492791u)) * (1.0 / 4294967296.0); }
float valueNoise3(vec3 p) {
    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float x00 = mix(latticeHash(i), latticeHash(i + ivec3(1, 0, 0)), f.x);
    float x10 = mix(latticeHash(i + ivec3(0, 1, 0)), latticeHash(i + ivec3(1, 1, 0)), f.x);
    float x01 = mix(latticeHash(i + ivec3(0, 0, 1)), latticeHash(i + ivec3(1, 0, 1)), f.x);
    float x11 = mix(latticeHash(i + ivec3(0, 1, 1)), latticeHash(i + ivec3(1, 1, 1)), f.x);
    return mix(mix(x00, x10, f.y), mix(x01, x11, f.y), f.z);
}
bool leafCovered(vec3 pl) { return valueNoise3(pl * 11.0) > 0.40; }
bool hitPrim(int i, vec3 ro, vec3 rd, float tmin, float tmax, out float t, out vec3 nw) {
    t = 0.0; nw = vec3(0.0);
    Prim p = prims[i];
    uint fl = uint(p.info.y);
    if ((fl & F_HIDDEN) != 0u) return false;
    vec3 o = vec3(dot(p.r0.xyz, ro) + p.r0.w, dot(p.r1.xyz, ro) + p.r1.w, dot(p.r2.xyz, ro) + p.r2.w);
    vec3 d = vec3(dot(p.r0.xyz, rd), dot(p.r1.xyz, rd), dot(p.r2.xyz, rd));
    vec3 nl;
    bool h = hitShape(p.info.x, o, d, tmin, tmax, t, nl);
    if (h && (fl & F_FOLIAGE) != 0u && !leafCovered(o + d * t)) {   // a gap: try the far side once
        h = hitShape(p.info.x, o, d, t + 1e-4, tmax, t, nl) && leafCovered(o + d * t);
    }
    if (h) nw = normalize(nl.x * p.r0.xyz + nl.y * p.r1.xyz + nl.z * p.r2.xyz);   // inverse transpose
    return h;
}

// ---- terrain height field: 2-level DDA (Heightfield.cpp)
bool hitTriangle(vec3 o, vec3 d, vec3 a, vec3 b, vec3 c, out float t, out float u, out float v) {
    t = u = v = 0.0;
    vec3 e1 = b - a, e2 = c - a, p = cross(d, e2);
    float det = dot(e1, p);
    if (abs(det) < 1e-12) return false;
    float inv = 1.0 / det;
    vec3 s = o - a;
    u = dot(s, p) * inv;
    if (u < 0.0 || u > 1.0) return false;
    vec3 q = cross(s, e1);
    v = dot(d, q) * inv;
    if (v < 0.0 || u + v > 1.0) return false;
    t = dot(e2, q) * inv;
    return true;
}
vec3 hfVertex(int i, int j) { return vec3(-uHfRadius + uHfCell * float(i), texelFetch(uHf, ivec2(i, j), 0).r, -uHfRadius + uHfCell * float(j)); }
vec3 hfNormal(int i, int j) { vec4 x = texelFetch(uHf, ivec2(i, j), 0); return vec3(x.g, sqrt(max(0.0, 1.0 - x.g * x.g - x.b * x.b)), x.b); }
bool hfCell(int ci, int cj, vec3 o, vec3 d, float tmin, float tmax, vec2 yRange, inout float t, inout vec3 n) {
    if (texelFetch(uHf, ivec2(ci, cj), 0).a > 0.5) return false;
    vec3 v00 = hfVertex(ci, cj), v10 = hfVertex(ci + 1, cj), v01 = hfVertex(ci, cj + 1), v11 = hfVertex(ci + 1, cj + 1);
    // the ray's height over this cell vs the cell's height range: no triangle test when it passes above or below
    float hiH = max(max(v00.y, v10.y), max(v01.y, v11.y)), loH = min(min(v00.y, v10.y), min(v01.y, v11.y));
    if (yRange.x > hiH + 1e-3 || yRange.y < loH - 1e-3) return false;
    float best = tmax, tt, u, v; bool found = false;
    if (hitTriangle(o, d, v00, v10, v11, tt, u, v) && tt > tmin && tt < best) {
        best = tt; found = true;
        n = normalize(hfNormal(ci, cj) * (1.0 - u - v) + hfNormal(ci + 1, cj) * u + hfNormal(ci + 1, cj + 1) * v);
    }
    if (hitTriangle(o, d, v00, v11, v01, tt, u, v) && tt > tmin && tt < best) {
        best = tt; found = true;
        n = normalize(hfNormal(ci, cj) * (1.0 - u - v) + hfNormal(ci + 1, cj + 1) * u + hfNormal(ci, cj + 1) * v);
    }
    if (found) t = best;
    return found;
}
bool hitTerrain(vec3 o, vec3 d, float tmin, float tmax, out float t, out vec3 n) {
    t = 0.0; n = vec3(0, 1, 0);
    if (uHasTerrain == 0) return false;
    vec3 invD = 1.0 / mix(d, vec3(1e-12), lessThan(abs(d), vec3(1e-12)));
    vec3 lo = vec3(-uHfRadius, uHfY.x, -uHfRadius), hi = vec3(uHfRadius, uHfY.y, uHfRadius);
    vec3 t0 = (lo - o) * invD, t1 = (hi - o) * invD;
    vec3 tn = min(t0, t1), tf = max(t0, t1);
    float ta = max(max(tn.x, tn.y), max(tn.z, tmin)), tb = min(min(tf.x, tf.y), min(tf.z, tmax));
    if (ta > tb) return false;
    int cells = uHfN - 1;
    int K = 8;
    float bsize = uHfCell * float(K);
    vec3 p0 = o + d * ta;
    int bi = clamp(int(floor((p0.x + uHfRadius) / bsize)), 0, uHfBlocksN - 1);
    int bj = clamp(int(floor((p0.z + uHfRadius) / bsize)), 0, uHfBlocksN - 1);
    int sx = d.x > 0.0 ? 1 : -1, sz = d.z > 0.0 ? 1 : -1;
    float dtx = abs(bsize * invD.x), dtz = abs(bsize * invD.z);
    float nextX = (-uHfRadius + bsize * float(bi + (sx > 0 ? 1 : 0)) - o.x) * invD.x;
    float nextZ = (-uHfRadius + bsize * float(bj + (sz > 0 ? 1 : 0)) - o.z) * invD.z;
    float tcur = ta;
    for (int guard = 0; guard < 256; ++guard) {
        if (tcur > tb || bi < 0 || bj < 0 || bi >= uHfBlocksN || bj >= uHfBlocksN) break;
        float tnext = min(min(nextX, nextZ), tb);
        float y0 = o.y + d.y * tcur, y1 = o.y + d.y * tnext;
        vec2 b = texelFetch(uHfBlocks, ivec2(bi, bj), 0).rg;
        if (min(y0, y1) <= b.y + 1e-3 && max(y0, y1) >= b.x - 1e-3) {
            int i0 = bi * K, i1 = min(i0 + K, cells) - 1, j0 = bj * K, j1 = min(j0 + K, cells) - 1;
            vec3 q = o + d * tcur;
            int ci = clamp(int(floor((q.x + uHfRadius) / uHfCell)), i0, i1);
            int cj = clamp(int(floor((q.z + uHfRadius) / uHfCell)), j0, j1);
            float ctx = abs(uHfCell * invD.x), ctz = abs(uHfCell * invD.z);
            float cx = (-uHfRadius + uHfCell * float(ci + (sx > 0 ? 1 : 0)) - o.x) * invD.x;
            float cz = (-uHfRadius + uHfCell * float(cj + (sz > 0 ? 1 : 0)) - o.z) * invD.z;
            float tc = tcur;
            for (int g2 = 0; g2 < 24; ++g2) {
                if (ci < i0 || ci > i1 || cj < j0 || cj > j1) break;
                float tout = min(min(cx, cz), tnext) + 1e-4;
                float ya = o.y + d.y * (tc - 1e-4), yb = o.y + d.y * tout;
                if (hfCell(ci, cj, o, d, tmin, tmax, vec2(min(ya, yb), max(ya, yb)), t, n)) return true;
                if (cx < cz) { if (cx > tnext + 1e-4) break; tc = cx; ci += sx; cx += ctx; }
                else { if (cz > tnext + 1e-4) break; tc = cz; cj += sz; cz += ctz; }
            }
        }
        if (nextX < nextZ) { tcur = nextX; bi += sx; nextX += dtx; }
        else { tcur = nextZ; bj += sz; nextZ += dtz; }
    }
    return false;
}

// ---- BVH
bool boxHit(vec3 lo, vec3 hi, vec3 o, vec3 invD, float tmin, float tmax, out float te) {
    vec3 t0 = (lo - o) * invD, t1 = (hi - o) * invD;
    vec3 tn = min(t0, t1), tf = max(t0, t1);
    te = max(max(tn.x, tn.y), max(tn.z, tmin));
    return te <= min(min(tf.x, tf.y), min(tf.z, tmax));
}
struct Hit { float t; int prim; vec3 ng; };    // ng: outward geometric normal
bool trace(vec3 ro, vec3 rd, float tmin, float tmax, out Hit h) {
    h.t = tmax; h.prim = -1; h.ng = vec3(0, 1, 0);
    float t; vec3 n;
    if (hitTerrain(ro, rd, tmin, h.t, t, n)) { h.t = t; h.prim = TERRAIN; h.ng = n; }
    if (uNodeCount == 0) return h.prim >= 0;
    vec3 invD = 1.0 / mix(rd, vec3(1e-12), lessThan(abs(rd), vec3(1e-12)));
    // Stack of (node, entry distance): a node is only visited if it still starts before the closest hit. Children
    // are tested once (when their parent is visited); the nearer one is descended into directly.
    int stack[24];
    float stackT[24];
    int sp = 0;
    float te;
    if (!boxHit(nodes[0].lo, nodes[0].hi, ro, invD, tmin, h.t, te)) return h.prim >= 0;
    int node = 0;
    for (int guard = 0; guard < 4096; ++guard) {
        Node nd = nodes[node];
        bool descend = false;
        if (nd.count > 0) {
            for (int k = nd.leftFirst; k < nd.leftFirst + nd.count; ++k)
                if (hitPrim(k, ro, rd, tmin, h.t, t, n)) { h.t = t; h.prim = k; h.ng = n; }
        } else {
            float ta, tb;
            bool ha = boxHit(nodes[nd.leftFirst].lo, nodes[nd.leftFirst].hi, ro, invD, tmin, h.t, ta);
            bool hb = boxHit(nodes[nd.leftFirst + 1].lo, nodes[nd.leftFirst + 1].hi, ro, invD, tmin, h.t, tb);
            if (ha && hb) {
                bool aFirst = ta <= tb;
                if (sp < 24) { stack[sp] = aFirst ? nd.leftFirst + 1 : nd.leftFirst; stackT[sp] = aFirst ? tb : ta; ++sp; }
                node = aFirst ? nd.leftFirst : nd.leftFirst + 1;
                descend = true;
            } else if (ha || hb) {
                node = ha ? nd.leftFirst : nd.leftFirst + 1;
                descend = true;
            }
        }
        if (descend) continue;
        bool found = false;
        while (sp > 0) {
            --sp;
            if (stackT[sp] <= h.t) { node = stack[sp]; found = true; break; }
        }
        if (!found) break;
    }
    return h.prim >= 0;
}
// Glass-tinted visibility (RtScene::transmittance); the BVH is walked first (an opaque blocker ends the query
// early), the terrain last.
vec3 transmittance(vec3 ro, vec3 rd, float tmax, uint skip) {
    float t; vec3 n;
    vec3 T = vec3(1.0);
    if (uNodeCount > 0) {
        vec3 invD = 1.0 / mix(rd, vec3(1e-12), lessThan(abs(rd), vec3(1e-12)));
        int stack[24];
        int sp = 0;
        float te;
        int node = boxHit(nodes[0].lo, nodes[0].hi, ro, invD, 1e-4, tmax, te) ? 0 : -1;
        for (int guard = 0; guard < 4096 && node >= 0; ++guard) {
            Node nd = nodes[node];
            node = -1;
            if (nd.count > 0) {
                for (int k = nd.leftFirst; k < nd.leftFirst + nd.count; ++k) {
                    if ((uint(prims[k].info.y) & skip) != 0u) continue;
                    if (!hitPrim(k, ro, rd, 1e-4, tmax, t, n)) continue;
                    vec4 mt = mats[k].t;
                    if (mt.w < 1.0) return vec3(0.0);
                    T *= mt.rgb;
                }
            } else {
                float ta, tb;
                bool ha = boxHit(nodes[nd.leftFirst].lo, nodes[nd.leftFirst].hi, ro, invD, 1e-4, tmax, ta);
                bool hb = boxHit(nodes[nd.leftFirst + 1].lo, nodes[nd.leftFirst + 1].hi, ro, invD, 1e-4, tmax, tb);
                if (ha && hb && sp < 24) { stack[sp++] = nd.leftFirst + 1; node = nd.leftFirst; }
                else if (ha) node = nd.leftFirst;
                else if (hb) node = nd.leftFirst + 1;
            }
            if (node < 0 && sp > 0) node = stack[--sp];
        }
    }
    if (hitTerrain(ro, rd, 1e-4, tmax, t, n)) return vec3(0.0);
    return T;
}

// ---- materials
struct Surf { vec3 albedo; float rough; vec3 emission; float metal; vec3 transmit; float ior; uint flags; };
uniform sampler2D uGround;     // the raster's ground map (R wet, G track, B forest, A meadow)
uniform float uGroundExtent;
uniform int uHasGround;
// Terrain albedo from the ground map: the raster terrain shader's materials without its procedural detail.
float valueNoise2(vec2 p) {   // smooth value noise (bilinear hash lattice)
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = float(pcg(uint(int(i.x)) * 1597334677u ^ uint(int(i.y)) * 3812015801u)) / 4294967296.0;
    float b = float(pcg(uint(int(i.x) + 1) * 1597334677u ^ uint(int(i.y)) * 3812015801u)) / 4294967296.0;
    float c = float(pcg(uint(int(i.x)) * 1597334677u ^ uint(int(i.y) + 1) * 3812015801u)) / 4294967296.0;
    float e = float(pcg(uint(int(i.x) + 1) * 1597334677u ^ uint(int(i.y) + 1) * 3812015801u)) / 4294967296.0;
    return mix(mix(a, b, f.x), mix(c, e, f.x), f.y);
}
// Grass stand-in: the meadow channel of the ground map turns the soil into a turf of blade colours (olive / straw
// tufts like the raster blades) darkened by the blades' mutual occlusion (no blade geometry is traced).
float meadowAt(vec3 P) { return uHasGround == 0 ? 0.0 : texture(uGround, P.xz / (2.0 * uGroundExtent) + 0.5).a; }
vec3 terrainAlbedo(vec3 P) {
    if (uHasGround == 0) return uTerrA.rgb;
    vec4 m = texture(uGround, P.xz / (2.0 * uGroundExtent) + 0.5);
    float tuft = valueNoise2(P.xz * 1.7), fine = valueNoise2(P.xz * 9.0);
    vec3 grass = mix(vec3(0.075, 0.105, 0.030), vec3(0.150, 0.130, 0.062), smoothstep(0.35, 0.8, tuft)) * (0.75 + 0.5 * fine);
    grass *= 0.8;                                                                       // blade self-occlusion
    vec3 a = mix(vec3(0.066, 0.055, 0.039), grass, m.a);                                // soil -> meadow
    a = mix(a, vec3(0.065, 0.045, 0.026), m.b);                                         // needle litter
    a = mix(a, vec3(0.064, 0.056, 0.044), smoothstep(0.1, 0.9, m.g));                   // vehicle track
    a = mix(a, vec3(0.035, 0.030, 0.024), smoothstep(0.3, 0.7, m.r));                   // creek mud
    return a;
}
// Shading normal of the stand-ins: leaves and grass blades scatter the normal in small cells.
vec3 standInNormal(vec3 n, vec3 p, int prim, uint flags) {
    float k = 0.0, freq = 6.0;
    if ((flags & F_FOLIAGE) != 0u) k = 0.7;
    else if (prim == TERRAIN) { k = 0.3 * meadowAt(p); freq = 9.0; }
    if (k <= 0.0) return n;
    vec3 q = p * freq;
    vec3 j = vec3(valueNoise3(q), valueNoise3(q + 17.3), valueNoise3(q + 41.7)) * 2.0 - 1.0;
    vec3 r = normalize(n + k * j);
    return dot(r, n) > 0.1 ? r : n;
}

Surf surfaceOf(int prim, vec3 P) {
    Surf s;
    vec4 a, e, t;
    if (prim == TERRAIN) { a = vec4(terrainAlbedo(P), uTerrA.w); e = uTerrE; t = uTerrT; s.flags = 0u; }
    else { Mat m = mats[prim]; a = m.a; e = m.e; t = m.t; s.flags = uint(prims[prim].info.y); }
    s.albedo = a.rgb; s.rough = a.w; s.emission = e.rgb; s.metal = e.w; s.transmit = t.rgb; s.ior = t.w;
    return s;
}
bool isGlass(Surf s) { return s.ior >= 1.0; }
vec3 F0of(Surf s) { return mix(vec3(s.ior), s.albedo, s.metal); }   // opaque: ior slot = dielectric F0

vec3 fresnelSchlick(float c, vec3 f0) { float m = clamp(1.0 - c, 0.0, 1.0); float m2 = m * m; return f0 + (1.0 - f0) * (m2 * m2 * m); }
float fresnelDielectric(float cosi, float eta) {
    float sin2t = eta * eta * (1.0 - cosi * cosi);
    if (sin2t >= 1.0) return 1.0;
    float cost = sqrt(1.0 - sin2t);
    float rs = (eta * cosi - cost) / (eta * cosi + cost), rp = (cosi - eta * cost) / (cosi + eta * cost);
    return 0.5 * (rs * rs + rp * rp);
}
float ggxD(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1.0) + 1.0; return a2 / (PI * d * d); }
float smithG1(float nx, float a) { float a2 = a * a; return 2.0 * nx / (nx + sqrt(a2 + (1.0 - a2) * nx * nx)); }
vec3 evalBsdf(Surf s, vec3 n, vec3 wo, vec3 wi) {
    float nl = dot(n, wi), nv = dot(n, wo);
    if (nl <= 0.0 || nv <= 0.0) return vec3(0.0);
    if (s.ior <= 0.0 && s.metal <= 0.0) return s.albedo / PI;
    vec3 h = normalize(wo + wi);
    float nh = max(dot(n, h), 0.0), vh = max(dot(wo, h), 0.0);
    float a = max(s.rough * s.rough, 2e-3);
    vec3 F = fresnelSchlick(vh, F0of(s));
    vec3 spec = F * (ggxD(nh, a) * smithG1(nv, a) * smithG1(nl, a) / (4.0 * nv * nl));
    return (1.0 - F) * s.albedo * ((1.0 - s.metal) / PI) + spec;
}
void basis(vec3 n, out vec3 t, out vec3 b) {
    float sg = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sg + n.z), c = n.x * n.y * a;
    t = vec3(1.0 + sg * n.x * n.x * a, sg * c, -sg * n.x);
    b = vec3(c, sg + n.y * n.y * a, -n.y);
}
vec3 toWorld(vec3 l, vec3 n) { vec3 t, b; basis(n, t, b); return t * l.x + b * l.y + n * l.z; }
vec3 sampleCosine(vec3 n, float u1, float u2) { float r = sqrt(u1), ph = 2.0 * PI * u2; return toWorld(vec3(r * cos(ph), r * sin(ph), sqrt(max(0.0, 1.0 - u1))), n); }
vec3 sampleCone(vec3 axis, float cosMax, float u1, float u2) {
    float c = 1.0 - u1 * (1.0 - cosMax), s = sqrt(max(0.0, 1.0 - c * c)), ph = 2.0 * PI * u2;
    return toWorld(vec3(s * cos(ph), s * sin(ph), c), axis);
}
vec3 sampleSphere(float u1, float u2) { float z = 1.0 - 2.0 * u1, r = sqrt(max(0.0, 1.0 - z * z)), ph = 2.0 * PI * u2; return vec3(r * cos(ph), r * sin(ph), z); }
float specProb(Surf s, vec3 n, vec3 wo) {
    if (s.ior <= 0.0 && s.metal <= 0.0) return 0.0;
    float fs = luminance(fresnelSchlick(max(dot(n, wo), 0.0), F0of(s)));
    float fd = luminance(s.albedo) * (1.0 - s.metal) * (1.0 - fs);
    return clamp(fs / max(fs + fd, 1e-4), 0.1, 0.9);
}
// BSDF sample: returns false if absorbed; weight = f cos / pdf; spec = delta-like lobe
bool sampleBsdf(Surf s, vec3 n, bool front, vec3 wo, out vec3 wi, out vec3 weight, out bool spec) {
    wi = vec3(0.0); weight = vec3(0.0); spec = false;
    if (isGlass(s)) {
        float cosi = max(dot(wo, n), 0.0), eta = front ? 1.0 / s.ior : s.ior;
        float F = fresnelDielectric(cosi, eta);
        spec = true;
        vec3 t = refract(-wo, n, eta);
        if (rnd() < F || dot(t, t) == 0.0) { wi = reflect(-wo, n); weight = vec3(1.0); }
        else { wi = normalize(t); weight = s.transmit; }
        return true;
    }
    float ps = specProb(s, n, wo);
    float a = max(s.rough * s.rough, 2e-3);
    float u1 = rnd(), u2 = rnd();
    if (rnd() < ps) {
        float a2 = a * a, c2 = (1.0 - u1) / (1.0 + (a2 - 1.0) * u1), c = sqrt(c2), sn = sqrt(max(0.0, 1.0 - c2)), ph = 2.0 * PI * u2;
        vec3 hv = toWorld(vec3(sn * cos(ph), sn * sin(ph), c), n);
        wi = reflect(-wo, hv);
        spec = a < 0.01;
    } else {
        wi = sampleCosine(n, u1, u2);
    }
    float nl = dot(n, wi);
    if (nl <= 0.0) return false;
    vec3 hv = normalize(wo + wi);
    float nh = max(dot(n, hv), 1e-6), vh = max(dot(wo, hv), 1e-6);
    float pdf = ps * ggxD(nh, a) * nh / (4.0 * vh) + (1.0 - ps) * nl / PI;
    if (pdf <= 1e-8) return false;
    weight = evalBsdf(s, n, wo, wi) * (nl / pdf);
    return true;
}

// ---- lights (RayTracer.cpp sampleLight)
bool sampleLight(int i, vec3 p, bool soft, out vec3 wi, out vec3 E, out float dist) {
    Light l = lights[i];
    wi = vec3(0, 1, 0); E = vec3(0.0); dist = 1e30;
    if (l.type == 0) {
        vec3 axis = -l.direction;
        wi = soft && l.radius > 0.0 ? sampleCone(axis, cos(l.radius), rnd(), rnd()) : axis;
        E = l.color * PI;
        return maxc(E) > 0.0;
    }
    vec3 pos = l.position;
    if (soft && l.radius > 0.0) pos += sampleSphere(rnd(), rnd()) * l.radius;
    vec3 d = pos - p;
    dist = max(length(d), 1e-3);
    wi = d / dist;
    float att = 0.0;
    if (l.type == 1) att = 1.0 / (1.0 + l.att.x * dist + l.att.y * dist * dist);
    else if (l.type == 2) {
        float cone = clamp((dot(-wi, l.direction) - l.att.w) / max(l.att.z - l.att.w, 1e-4), 0.0, 1.0);
        att = cone * cone / (1.0 + l.att.x * dist + l.att.y * dist * dist);
    } else {
        float x = dist / max(l.range, 1e-3);
        float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
        att = window * window / (1.0 + dist * dist);
        if (l.att.w > -1.5) att *= smoothstep(l.att.w, l.att.w + 0.1, dot(-wi, l.direction));
    }
    E = l.color * (PI * att);
    return att > 0.0;
}
vec3 directLight(vec3 p, vec3 n, Surf s, vec3 wo, bool soft) {
    vec3 L = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        vec3 wi, E; float dist;
        if (!sampleLight(i, p, soft, wi, E, dist)) continue;
        float nl = dot(n, wi);
        bool leaf = (s.flags & F_FOLIAGE) != 0u;
        if (leaf) nl = (nl + 0.5) / 1.5;              // leaves: wrapped Lambert (light through thin leaves)
        if (nl <= 0.0) continue;
        vec3 f = leaf ? s.albedo / PI : evalBsdf(s, n, wo, wi);
        if (maxc(f * E) <= 1e-7) continue;
        uint skip = F_NOSHADOW | F_PROXY | (lights[i].type == 2 ? F_HOLDER : 0u);
        vec3 T = transmittance(p + n * EPS, wi, dist >= 1e29 ? 1e30 : dist * 0.999 - 0.02, skip);
        L += f * E * T * nl;
    }
    return L;
}
vec3 envRadiance(vec3 d) { return max(uBounce + (uZenith - uBounce) * (0.5 + 0.75 * d.y), vec3(0.0)); }
// What a mirror sees of the sky when its reflected ray escapes: the raster's sky-in-water gradient (fog colour at the
// horizon to 1.2 x zenith overhead), not the brighter hemispherical-integral model used for diffuse light.
vec3 skyDome(vec3 d) { return d.y >= 0.0 ? mix(uFogColor, uZenith * 1.2, clamp(d.y * 1.5, 0.0, 1.0)) : mix(uFogColor, uBounce, clamp(-d.y * 3.0, 0.0, 1.0)); }
vec3 ambientOf(vec3 n) { return uBounce + (uZenith - uBounce) * (0.5 + 0.5 * n.y); }
float fogOf(float t) { float x = uFogDensity * t; return 1.0 - exp(-x * x); }

vec3 primaryDir(vec2 uv) {   // uv in [0,1], v = 0 at the TOP row
    float x = (2.0 * uv.x - 1.0) * uTanHalfY * uAspect, y = (1.0 - 2.0 * uv.y) * uTanHalfY;
    return normalize(uCamFwd + uCamRight * x + uCamUp * y);
}
)";

// ---------------------------------------------------------------------------------------------------- Whitted / path
// One sample per pixel at the internal resolution, blended into the running average (uBlend = 1 / n, capped while
// the scene animates); the path tracer also writes the denoiser's guide buffers (albedo, normal + depth).
const char* kTrace = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba32f, binding = 0) uniform image2D uAccum;
layout(rgba32f, binding = 1) uniform image2D uAlbedo;
layout(rgba32f, binding = 2) uniform image2D uNormalDepth;
uniform sampler2D uSky;      // raster sky (+ moon / stars) at full resolution: primary misses
uniform int uMode;           // 2 Whitted, 3 path
uniform int uMaxDepth, uMaxBounces, uRrStart;
uniform float uBlend;
uniform vec2 uJitter;        // sub-pixel offset of this frame's sample (pixels)
uniform ivec2 uRes;

struct Task { vec3 o; vec3 d; vec3 w; int depth; };

vec3 whitted(vec3 ro, vec3 rd, vec2 uv) {
    // The primary hit is traced first for the fog weight; the tree is then walked with an explicit stack
    // (GLSL has no recursion): glass pushes its reflection and refraction, smooth surfaces their mirror ray.
    Hit h0;
    if (!trace(ro, rd, 1e-4, 1e30, h0)) return texture(uSky, uv).rgb;
    float fog = (h0.prim == TERRAIN || (uint(prims[h0.prim].info.y) & F_SKY) == 0u) ? fogOf(h0.t) : 0.0;
    vec3 L = uFogColor * fog;
    Task st[6];
    int sp = 0;
    st[sp++] = Task(ro, rd, vec3(1.0 - fog), 0);
    while (sp > 0) {
        Task k = st[--sp];
        Hit h;
        if (!trace(k.o, k.d, 1e-4, 1e30, h)) {
            L += k.w * (k.depth == 0 ? texture(uSky, uv).rgb : skyDome(k.d));
            continue;
        }
        vec3 p = k.o + k.d * h.t;
        Surf s = surfaceOf(h.prim, p);
        bool front = dot(k.d, h.ng) < 0.0;
        vec3 n = standInNormal(front ? h.ng : -h.ng, p, h.prim, s.flags);
        vec3 wo = -k.d;
        L += k.w * s.emission;
        if (isGlass(s)) {
            float cosi = max(dot(wo, n), 0.0), eta = front ? 1.0 / s.ior : s.ior;
            float F = fresnelDielectric(cosi, eta);
            if (k.depth < uMaxDepth) {
                vec3 t = refract(k.d, n, eta);
                if (F < 1.0 && dot(t, t) > 0.0 && sp < 6) st[sp++] = Task(p - n * EPS, normalize(t), k.w * s.transmit * (1.0 - F), k.depth + 1);
                if (sp < 6) st[sp++] = Task(p + n * EPS, reflect(k.d, n), k.w * F, k.depth + 1);
            }
            continue;
        }
        L += k.w * directLight(p, n, s, wo, true);
        // ambient sky with one short occlusion ray (accumulates into soft AO while the view is still)
        vec3 ad = sampleCosine(n, rnd(), rnd());
        float ao = maxc(transmittance(p + n * EPS, ad, 1.5, F_NOSHADOW | F_PROXY)) > 0.0 ? 1.0 : 0.25;
        L += k.w * s.albedo * ambientOf(n) * ((1.0 - s.metal) * ao);
        if (s.rough < 0.4 && k.depth < uMaxDepth && sp < 6) {
            float gloss = (0.4 - s.rough) / 0.4;
            vec3 kr = fresnelSchlick(max(dot(wo, n), 0.0), F0of(s)) * (gloss * gloss);
            if (maxc(kr) > 0.01) st[sp++] = Task(p + n * EPS, reflect(k.d, n), k.w * kr, k.depth + 1);
        }
    }
    return L;
}

vec3 pathTrace(vec3 ro, vec3 rd, vec2 uv, out vec3 aovAlbedo, out vec4 aovND) {
    vec3 L = vec3(0.0), beta = vec3(1.0);
    bool spec = true;
    float primaryT = -1.0;
    bool primarySky = false;
    aovAlbedo = vec3(1.0);
    aovND = vec4(0.0, 1.0, 0.0, 1e4);
    for (int bounce = 0; bounce <= uMaxBounces; ++bounce) {
        Hit h;
        if (!trace(ro, rd, 1e-4, 1e30, h)) {
            L += beta * (bounce == 0 ? texture(uSky, uv).rgb : envRadiance(rd));
            break;
        }
        vec3 p = ro + rd * h.t;
        Surf s = surfaceOf(h.prim, p);
        bool front = dot(rd, h.ng) < 0.0;
        vec3 n = standInNormal(front ? h.ng : -h.ng, p, h.prim, s.flags);
        if (bounce == 0) {
            primaryT = h.t;
            primarySky = h.prim != TERRAIN && (s.flags & F_SKY) != 0u;
            aovAlbedo = isGlass(s) ? s.transmit : max(s.albedo, vec3(0.02));
            aovND = vec4(n, h.t);
        }
        if ((s.flags & F_PROXY) == 0u || spec) L += beta * s.emission;
        vec3 wo = -rd;
        if (!isGlass(s)) L += beta * directLight(p, n, s, wo, true);
        vec3 wi, w; bool sp;
        if (!sampleBsdf(s, n, front, wo, wi, w, sp)) break;
        beta *= w;
        spec = sp;
        ro = p + n * (dot(wi, n) >= 0.0 ? EPS : -EPS);
        rd = wi;
        if (bounce >= uRrStart) {
            float q = max(0.05, 1.0 - maxc(beta));
            if (rnd() < q) break;
            beta /= 1.0 - q;
        }
    }
    if (primaryT > 0.0 && !primarySky) L = mix(L, uFogColor, fogOf(primaryT));
    return L;
}

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= uRes.x || px.y >= uRes.y) return;
    gSeed = pcg(uint(px.x) + uint(px.y) * 9781u + uFrame * 26699u);
    vec2 uv = (vec2(px) + uJitter) / vec2(uRes);   // GL convention: row 0 at the bottom (also for uSky)
    vec3 rd = primaryDir(vec2(uv.x, 1.0 - uv.y));
    vec3 c;
    vec3 alb = vec3(1.0);
    vec4 nd = vec4(0.0, 1.0, 0.0, 1e4);
    if (uMode == 2) c = whitted(uCamPos, rd, uv);
    else c = pathTrace(uCamPos, rd, uv, alb, nd);
    if (any(isnan(c)) || any(isinf(c))) c = vec3(0.0);
    c = min(c, vec3(64.0));            // firefly clamp (night key ~ 0.03: far above any real surface)
    vec4 prev = imageLoad(uAccum, px);
    imageStore(uAccum, px, vec4(mix(prev.rgb, c, uBlend), 1.0));
    if (uMode == 3) {
        imageStore(uAlbedo, px, vec4(mix(imageLoad(uAlbedo, px).rgb, alb, uBlend), 1.0));
        imageStore(uNormalDepth, px, nd);
    }
}
)";

// ---------------------------------------------------------------------------------------------------- hybrid
// Raster G-buffer (depth, world normal + reflectivity) + ray-traced soft shadows for the key light and the
// searchlight (the raster pass ran without shadow maps: the unoccluded direct term the ray finds blocked is taken
// back out) and ray-traced mirror reflections in place of SSR (a reflection hit that is on screen and matches the
// depth buffer reuses the raster colour, anything else is shaded by the tracer). Then the raster fog, as in combine.
const char* kHybrid = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba16f, binding = 3) uniform writeonly image2D uOut;
uniform sampler2D uScene, uDepth, uSurface, uFog;
uniform int uUseFog, uShadows, uReflections, uKeyLight, uSpotLight;
uniform mat4 uInvViewProj, uViewProj;
uniform ivec2 uRes;

vec3 worldAt(vec2 uv, float d) { vec4 p = uInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0); return p.xyz / p.w; }

vec3 shadeHit(vec3 p, vec3 n, Surf s, vec3 wo) {
    return s.emission + directLight(p, n, s, wo, true) + s.albedo * ambientOf(n) * (1.0 - s.metal);
}

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= uRes.x || px.y >= uRes.y) return;
    gSeed = pcg(uint(px.x) + uint(px.y) * 9781u + uFrame * 26699u);
    vec2 uv = (vec2(px) + 0.5) / vec2(uRes);
    vec3 c = texture(uScene, uv).rgb;
    float d = texelFetch(uDepth, px, 0).r;
    vec4 surf = texture(uSurface, uv);
    if (d < 1.0) {
        vec3 P = worldAt(uv, d);
        vec3 N = normalize(surf.rgb * 2.0 - 1.0);
        vec3 V = normalize(P - uCamPos);
        if (dot(N, V) > 0.0) N = -N;
        float dist = length(P - uCamPos);
        // Which primitive is it? (albedo for the shadow term; grass / foliage fall back to a mid albedo)
        Hit h;
        vec3 albedo = vec3(0.18);
        float metal = 0.0;
        if (trace(uCamPos, V, 1e-3, dist * 1.05 + 0.1, h) && abs(h.t - dist) < 0.05 * dist + 0.05) {
            Surf s = surfaceOf(h.prim, P);
            albedo = s.albedo;
            metal = s.metal;
        }
        if (uShadows == 1) {
            // The albedo can be no larger than what the raster pixel shows under the unoccluded light: c / irradiance
            // (dark wet ground, puddles and grass have darker raster materials than their BVH primitive).
            vec3 irr = ambientOf(N);
            for (int i = 0; i < uLightCount; ++i) {
                if (!(i == uKeyLight || i == uSpotLight)) continue;
                vec3 wi, E; float ld;
                if (sampleLight(i, P, false, wi, E, ld)) irr += E / PI * max(dot(N, wi), 0.0);
            }
            vec3 albedoS = min(albedo, c / max(irr, vec3(1e-4)));
            vec3 lost = vec3(0.0);
            for (int i = 0; i < uLightCount; ++i) {
                if (!(i == uKeyLight || i == uSpotLight)) continue;
                vec3 wi, E; float ld;
                if (!sampleLight(i, P, true, wi, E, ld)) continue;
                float nl = dot(N, wi);
                if (nl <= 0.0) continue;
                uint skip = F_NOSHADOW | F_PROXY | (lights[i].type == 2 ? F_HOLDER : 0u);
                vec3 T = transmittance(P + N * 0.02, wi, ld >= 1e29 ? 1e30 : ld * 0.999 - 0.02, skip);
                lost += (1.0 - T) * albedoS * (1.0 - metal) * (E / PI) * nl;   // raster units: albedo * colour * N.L
            }
            c = max(c - lost, c * 0.06);
        }
        if (uReflections == 1 && surf.a > 0.02) {
            vec3 R = reflect(V, N);
            vec3 refl = vec3(0.0);
            Hit rh;
            vec3 o = P + N * 0.02;
            bool hitR = (R.y > -0.2 || metal > 0.5) && trace(o, R, 1e-4, 1e30, rh);
            if (!hitR && metal > 0.5) refl = skyDome(R);
            if (hitR) {
                vec3 q = o + R * rh.t;
                vec4 cp = uViewProj * vec4(q, 1.0);
                vec2 quv = cp.xy / cp.w * 0.5 + 0.5;
                bool onScreen = cp.w > 0.05 && all(greaterThan(quv, vec2(0.0))) && all(lessThan(quv, vec2(1.0)));
                bool reuse = false;
                if (onScreen) {
                    vec3 seen = worldAt(quv, texture(uDepth, quv).r);
                    reuse = length(seen - q) < 0.05 + 0.01 * rh.t;
                }
                if (reuse) refl = texture(uScene, quv).rgb;
                else {
                    Surf s = surfaceOf(rh.prim, q);
                    bool fr = dot(R, rh.ng) < 0.0;
                    refl = shadeHit(q, standInNormal(fr ? rh.ng : -rh.ng, q, rh.prim, s.flags), s, -R);
                    refl = mix(refl, uFogColor, fogOf(rh.t));   // the raster fogs what it shows: so must the mirror
                }
            }
            float F = 0.02 + 0.98 * pow(1.0 - clamp(dot(N, -V), 0.0, 1.0), 5.0);
            if (metal > 0.5) {
                // Polished metal: the raster colour is mostly its sky-specular approximation; the traced mirror image
                // (tinted by F0 = albedo, Schlick towards white at grazing angles) replaces it.
                vec3 Fm = albedo + (1.0 - albedo) * pow(1.0 - clamp(dot(N, -V), 0.0, 1.0), 5.0);
                c = mix(c, refl * Fm, clamp(surf.a / 0.5, 0.0, 1.0));
            } else {
                c += refl * mix(0.35, 1.0, F) * surf.a;   // water film / wet surfaces: added like SSR
            }
        }
    }
    if (uUseFog == 1) {
        vec4 f = texture(uFog, uv);
        c = c * f.a + f.rgb;
    }
    imageStore(uOut, px, vec4(c, 1.0));
}
)";

// ---------------------------------------------------------------------------------------------------- light tracing
// Reverse ray tracing: one light path per invocation, emitted by a light (or the sky) picked by power, every
// non-specular vertex connected to the pinhole camera and splatted with atomics (fixed point, 3 uints per pixel).
const char* kLight = R"(
layout(local_size_x = 64) in;
layout(std430, binding = 4) buffer SplatBuf { uint splat[]; };
uniform int uEmitters;          // lights + 1 (sky)
uniform float uEmitCdf[24];     // cumulative probabilities
uniform float uEmitPdf[24];
uniform float uEmitRadius;
uniform vec3 uEmitCentre;
uniform vec3 uFocus;            // where the camera looks (sun / moon / sky photons are concentrated around it)
uniform float uFocusRadius;
uniform ivec2 uRes;
uniform float uSplatScale;      // fixed-point scale
uniform int uMaxBounces, uRrStart;
uniform uint uPathsPerFrame;

void splatAt(vec3 p, vec3 val) {
    vec3 d = p - uCamPos;
    float z = dot(d, uCamFwd);
    if (z <= 1e-4) return;
    float x = dot(d, uCamRight) / z, y = dot(d, uCamUp) / z;
    vec2 uv = vec2(0.5 + 0.5 * x / (uTanHalfY * uAspect), 0.5 + 0.5 * y / uTanHalfY);   // GL rows (y up)
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) return;
    ivec2 px = ivec2(uv * vec2(uRes));
    uint base = 3u * uint(px.y * uRes.x + px.x);
    uvec3 q = uvec3(min(val * uSplatScale, vec3(4.0e9)) + 0.5);
    if (q.r > 0u) atomicAdd(splat[base], q.r);
    if (q.g > 0u) atomicAdd(splat[base + 1u], q.g);
    if (q.b > 0u) atomicAdd(splat[base + 2u], q.b);
}

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= uPathsPerFrame) return;
    gSeed = pcg(id * 7919u + uFrame * 104729u + 17u);
    float u = rnd();
    int li = 0;
    for (int i = 0; i < 24; ++i) { if (i >= uEmitters) break; li = i; if (u < uEmitCdf[i]) break; }
    float pl = uEmitPdf[li];
    if (pl <= 0.0) return;
    vec3 ro, rd, beta;
    int attType = -1;
    float u1 = rnd(), u2 = rnd(), u3 = rnd(), u4 = rnd();
    if (li >= uLightCount || lights[li].type == 0) {
        vec3 travel, irr;
        float pdfDir = 1.0;
        if (li < uLightCount) { travel = lights[li].direction; irr = lights[li].color * PI; }
        else { vec3 d = sampleSphere(u3, u4); d.y = abs(d.y); travel = -d; irr = envRadiance(d); pdfDir = 1.0 / (2.0 * PI); }
        // Start points on a plane across the beam: a mixture of a small disc around the view's focus (70 %) and the
        // whole camp (30 %), weighted by the mixture density, so the photons land where the camera looks.
        vec3 t, b;
        basis(travel, t, b);
        vec2 f2 = vec2(dot(uFocus - uEmitCentre, t), dot(uFocus - uEmitCentre, b));
        float r = sqrt(u1), ph = 2.0 * PI * u2;
        vec2 disc = vec2(r * cos(ph), r * sin(ph));
        vec2 q = rnd() < 0.7 ? f2 + disc * uFocusRadius : disc * uEmitRadius;
        float pdfA = (length(q - f2) < uFocusRadius ? 0.7 / (PI * uFocusRadius * uFocusRadius) : 0.0) +
                     (length(q) < uEmitRadius ? 0.3 / (PI * uEmitRadius * uEmitRadius) : 0.0);
        ro = uEmitCentre - travel * 250.0 + t * q.x + b * q.y;
        rd = travel;
        beta = irr / (pdfA * pdfDir * pl);
    } else {
        Light l = lights[li];
        if (l.type == 2) {
            rd = sampleCone(l.direction, l.att.w, u1, u2);
            float pdfDir = 1.0 / (2.0 * PI * (1.0 - l.att.w));
            float cone = clamp((dot(rd, l.direction) - l.att.w) / max(l.att.z - l.att.w, 1e-4), 0.0, 1.0);
            beta = l.color * (PI * cone * cone / (pdfDir * pl));
        } else {
            rd = sampleSphere(u1, u2);
            float k = 1.0;
            if (l.type == 3 && l.att.w > -1.5) k = smoothstep(l.att.w, l.att.w + 0.1, dot(rd, l.direction));
            beta = l.color * (PI * k * 4.0 * PI / pl);
        }
        attType = l.type;
        ro = l.position + rd * 0.03;
    }
    float tanX = uTanHalfY * uAspect;
    float area = 4.0 * tanX * uTanHalfY;
    float npix = float(uRes.x) * float(uRes.y);
    for (int bounce = 0; bounce <= uMaxBounces; ++bounce) {
        Hit h;
        if (maxc(beta) <= 0.0 || !trace(ro, rd, 1e-4, 1e30, h)) return;
        if (bounce == 0 && attType >= 0) {
            Light l = lights[li];
            float d = h.t + 0.03, att;
            if (attType == 3) { float x = d / max(l.range, 1e-3); float wdw = clamp(1.0 - x * x * x * x, 0.0, 1.0); att = wdw * wdw / (1.0 + d * d); }
            else att = 1.0 / (1.0 + l.att.x * d + l.att.y * d * d);
            beta *= att * d * d;
        }
        vec3 p = ro + rd * h.t;
        Surf s = surfaceOf(h.prim, p);
        bool front = dot(rd, h.ng) < 0.0;
        vec3 n = standInNormal(front ? h.ng : -h.ng, p, h.prim, s.flags);
        vec3 wi = -rd;
        if (!isGlass(s) && !(s.metal > 0.5 && s.rough < 0.1)) {   // (near-)mirrors are delta surfaces for the camera
            vec3 toCam = uCamPos - p;
            float dist = length(toCam);
            vec3 wo = toCam / max(dist, 1e-6);
            float cosCam = dot(-wo, uCamFwd);
            if (cosCam > 0.0 && dot(wo, n) > 0.0) {
                vec3 f = evalBsdf(s, n, wo, wi);
                if (maxc(f * beta) > 0.0) {
                    vec3 T = transmittance(p + n * EPS, wo, dist * 0.999, F_NOSHADOW | F_PROXY);
                    if (maxc(T) > 0.0) {
                        float c2 = cosCam * cosCam;
                        float We = npix / (area * c2 * c2);
                        // (distance clamped at 0.5 m: a surface touching the lens would splat unbounded values)
                        splatAt(p, beta * f * T * (dot(wo, n) * cosCam / max(dist * dist, 0.25) * We));
                    }
                }
            }
        }
        vec3 nwi, w; bool sp;
        if (!sampleBsdf(s, n, front, wi, nwi, w, sp)) return;
        beta *= w;
        ro = p + n * (dot(nwi, n) >= 0.0 ? EPS : -EPS);
        rd = nwi;
        if (bounce >= uRrStart) {
            float q = max(0.05, 1.0 - maxc(w));
            if (rnd() < q) return;
            beta /= 1.0 - q;
        }
    }
}
)";

// Light-tracing resolve: splats of this frame -> radiance (divide by the path count), plus what light paths cannot
// reach the pinhole with (directly seen emitters and the sky: one primary ray), fog on the primary distance; blended
// into the running average; the splat buffer is cleared for the next frame.
const char* kLightResolve = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 4) buffer SplatBuf { uint splat[]; };
layout(rgba32f, binding = 0) uniform image2D uAccum;
layout(rgba32f, binding = 1) uniform image2D uAlbedo;
layout(rgba32f, binding = 2) uniform image2D uNormalDepth;
uniform sampler2D uSky;
uniform ivec2 uRes;
uniform float uInvScalePaths;   // 1 / (splat scale * paths this frame)
uniform float uBlend;

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= uRes.x || px.y >= uRes.y) return;
    uint base = 3u * uint(px.y * uRes.x + px.x);
    vec3 c = vec3(float(splat[base]), float(splat[base + 1u]), float(splat[base + 2u])) * uInvScalePaths;
    splat[base] = 0u; splat[base + 1u] = 0u; splat[base + 2u] = 0u;
    vec2 uv = (vec2(px) + 0.5) / vec2(uRes);
    vec3 rd = primaryDir(vec2(uv.x, 1.0 - uv.y));
    Hit h;
    vec3 alb = vec3(1.0);
    vec4 nd = vec4(0.0, 1.0, 0.0, 1e4);
    if (!trace(uCamPos, rd, 1e-4, 1e30, h)) c = texture(uSky, uv).rgb;
    else {
        Surf s = surfaceOf(h.prim, uCamPos + rd * h.t);
        c += s.emission;
        if (h.prim == TERRAIN || (s.flags & F_SKY) == 0u) c = mix(c, uFogColor, fogOf(h.t));
        alb = isGlass(s) ? s.transmit : max(s.albedo, vec3(0.02));
        nd = vec4(dot(rd, h.ng) < 0.0 ? h.ng : -h.ng, h.t);
    }
    vec4 prev = imageLoad(uAccum, px);
    imageStore(uAccum, px, vec4(mix(prev.rgb, c, uBlend), 1.0));
    imageStore(uAlbedo, px, vec4(alb, 1.0));
    imageStore(uNormalDepth, px, nd);
}
)";

// ---------------------------------------------------------------------------------------------------- denoise + output
// A-trous wavelet pass (5x5 B3 spline with holes) on albedo-demodulated radiance, edge-stopping on normal, depth
// and luminance (sigma shrinks as samples accumulate). uFirst demodulates on read, the output pass remodulates.
const char* kDenoise = R"(#version 430
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba32f, binding = 0) uniform readonly image2D uSrc;
layout(rgba32f, binding = 1) uniform readonly image2D uAlbedo;
layout(rgba32f, binding = 2) uniform readonly image2D uNormalDepth;
layout(rgba32f, binding = 5) uniform writeonly image2D uDst;
uniform int uStep, uFirst;
uniform float uSigma;
uniform ivec2 uRes;

vec3 fetch(ivec2 p) {
    vec3 c = imageLoad(uSrc, p).rgb;
    return uFirst == 1 ? c / max(imageLoad(uAlbedo, p).rgb, vec3(0.02)) : c;
}
void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= uRes.x || px.y >= uRes.y) return;
    const float k[5] = float[](1.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0);
    vec3 c0 = fetch(px);
    vec4 nd0 = imageLoad(uNormalDepth, px);
    float l0 = dot(c0, vec3(0.2126, 0.7152, 0.0722));
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            ivec2 q = px + ivec2(dx, dy) * uStep;
            if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, uRes))) continue;
            vec3 c = fetch(q);
            vec4 nd = imageLoad(uNormalDepth, q);
            float wn = pow(max(0.0, dot(nd0.xyz, nd.xyz)), 32.0);
            float wz = exp(-abs(nd0.w - nd.w) / (0.02 * max(nd0.w, 0.1) * float(uStep) + 1e-3));
            float wl = exp(-abs(l0 - dot(c, vec3(0.2126, 0.7152, 0.0722))) / (uSigma * (l0 + 0.002) + 1e-5));
            float w = k[dx + 2] * k[dy + 2] * wn * wz * wl;
            sum += c * w;
            wsum += w;
        }
    imageStore(uDst, px, vec4(wsum > 0.0 ? sum / wsum : c0, 1.0));
}
)";

// Output: internal resolution -> full resolution (bilinear), remodulated by the albedo when the source is the
// demodulated denoiser output.
const char* kOutput = R"(#version 430
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba16f, binding = 3) uniform writeonly image2D uOut;
uniform sampler2D uSrc;
uniform sampler2D uAlbedoTex;
uniform int uRemodulate;
uniform ivec2 uRes;
void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= uRes.x || px.y >= uRes.y) return;
    vec2 uv = (vec2(px) + 0.5) / vec2(uRes);
    vec3 c = texture(uSrc, uv).rgb;
    if (uRemodulate == 1) c *= max(texture(uAlbedoTex, uv).rgb, vec3(0.02));
    imageStore(uOut, px, vec4(max(c, vec3(0.0)), 1.0));
}
)";

}  // namespace ts::glslrt
