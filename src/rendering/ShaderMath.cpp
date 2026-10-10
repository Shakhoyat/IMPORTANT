#include "rendering/ShaderMath.h"

#include <algorithm>
#include <cctype>

namespace ts {

const char* const kManualMath = R"(
// ---- manual vector maths: every shader uses these instead of the GLSL built-ins ----
// dot product: a . b = sum of a_i b_i
float tsDot(float a, float b) { return a * b; }
float tsDot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
float tsDot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float tsDot(vec4 a, vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
// length: |a| = sqrt(a . a)
float tsLength(float a) { return abs(a); }
float tsLength(vec2 a) { return sqrt(tsDot(a, a)); }
float tsLength(vec3 a) { return sqrt(tsDot(a, a)); }
float tsLength(vec4 a) { return sqrt(tsDot(a, a)); }
float tsDistance(vec2 a, vec2 b) { return tsLength(a - b); }
float tsDistance(vec3 a, vec3 b) { return tsLength(a - b); }
// unit vector: a / |a|
vec2 tsNormalize(vec2 a) { return a / tsLength(a); }
vec3 tsNormalize(vec3 a) { return a / tsLength(a); }
vec4 tsNormalize(vec4 a) { return a / tsLength(a); }
// cross product: a x b
vec3 tsCross(vec3 a, vec3 b) { return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
// mirror direction: r = d - 2 (n . d) n
vec2 tsReflect(vec2 d, vec2 n) { return d - 2.0 * tsDot(n, d) * n; }
vec3 tsReflect(vec3 d, vec3 n) { return d - 2.0 * tsDot(n, d) * n; }
// Snell refraction: t = eta d - (eta (n . d) + sqrt(k)) n, k = 1 - eta^2 (1 - (n . d)^2); k < 0: total internal reflection
vec3 tsRefract(vec3 d, vec3 n, float eta) {
    float c = tsDot(n, d);
    float k = 1.0 - eta * eta * (1.0 - c * c);
    if (k < 0.0) return vec3(0.0);
    return eta * d - (eta * c + sqrt(k)) * n;
}
// clamp: min(max(x, lo), hi)
float tsClamp(float x, float lo, float hi) { return min(max(x, lo), hi); }
vec2 tsClamp(vec2 x, float lo, float hi) { return min(max(x, vec2(lo)), vec2(hi)); }
vec3 tsClamp(vec3 x, float lo, float hi) { return min(max(x, vec3(lo)), vec3(hi)); }
vec4 tsClamp(vec4 x, float lo, float hi) { return min(max(x, vec4(lo)), vec4(hi)); }
vec2 tsClamp(vec2 x, vec2 lo, vec2 hi) { return min(max(x, lo), hi); }
vec3 tsClamp(vec3 x, vec3 lo, vec3 hi) { return min(max(x, lo), hi); }
vec4 tsClamp(vec4 x, vec4 lo, vec4 hi) { return min(max(x, lo), hi); }
int tsClamp(int x, int lo, int hi) { return min(max(x, lo), hi); }
ivec2 tsClamp(ivec2 x, ivec2 lo, ivec2 hi) { return min(max(x, lo), hi); }
ivec3 tsClamp(ivec3 x, ivec3 lo, ivec3 hi) { return min(max(x, lo), hi); }
// linear interpolation: (1 - t) a + t b
float tsMix(float a, float b, float t) { return a * (1.0 - t) + b * t; }
vec2 tsMix(vec2 a, vec2 b, float t) { return a * (1.0 - t) + b * t; }
vec3 tsMix(vec3 a, vec3 b, float t) { return a * (1.0 - t) + b * t; }
vec4 tsMix(vec4 a, vec4 b, float t) { return a * (1.0 - t) + b * t; }
vec2 tsMix(vec2 a, vec2 b, vec2 t) { return a * (vec2(1.0) - t) + b * t; }
vec3 tsMix(vec3 a, vec3 b, vec3 t) { return a * (vec3(1.0) - t) + b * t; }
vec4 tsMix(vec4 a, vec4 b, vec4 t) { return a * (vec4(1.0) - t) + b * t; }
// selection: b where t is true, a elsewhere
float tsMix(float a, float b, bool t) { return t ? b : a; }
vec2 tsMix(vec2 a, vec2 b, bvec2 t) { return vec2(t.x ? b.x : a.x, t.y ? b.y : a.y); }
vec3 tsMix(vec3 a, vec3 b, bvec3 t) { return vec3(t.x ? b.x : a.x, t.y ? b.y : a.y, t.z ? b.z : a.z); }
vec4 tsMix(vec4 a, vec4 b, bvec4 t) { return vec4(t.x ? b.x : a.x, t.y ? b.y : a.y, t.z ? b.z : a.z, t.w ? b.w : a.w); }
// step: 0 below the edge, 1 from the edge on
float tsStep(float edge, float x) { return x < edge ? 0.0 : 1.0; }
vec2 tsStep(float edge, vec2 x) { return vec2(tsStep(edge, x.x), tsStep(edge, x.y)); }
vec3 tsStep(float edge, vec3 x) { return vec3(tsStep(edge, x.x), tsStep(edge, x.y), tsStep(edge, x.z)); }
vec4 tsStep(float edge, vec4 x) { return vec4(tsStep(edge, x.x), tsStep(edge, x.y), tsStep(edge, x.z), tsStep(edge, x.w)); }
vec2 tsStep(vec2 edge, vec2 x) { return vec2(tsStep(edge.x, x.x), tsStep(edge.y, x.y)); }
vec3 tsStep(vec3 edge, vec3 x) { return vec3(tsStep(edge.x, x.x), tsStep(edge.y, x.y), tsStep(edge.z, x.z)); }
vec4 tsStep(vec4 edge, vec4 x) { return vec4(tsStep(edge.x, x.x), tsStep(edge.y, x.y), tsStep(edge.z, x.z), tsStep(edge.w, x.w)); }
// smooth step (cubic Hermite): t = clamp((x - e0) / (e1 - e0), 0, 1), then t^2 (3 - 2 t)
float tsSmoothstep(float e0, float e1, float x) { float t = tsClamp((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }
vec2 tsSmoothstep(float e0, float e1, vec2 x) { return vec2(tsSmoothstep(e0, e1, x.x), tsSmoothstep(e0, e1, x.y)); }
vec3 tsSmoothstep(float e0, float e1, vec3 x) { return vec3(tsSmoothstep(e0, e1, x.x), tsSmoothstep(e0, e1, x.y), tsSmoothstep(e0, e1, x.z)); }
vec4 tsSmoothstep(float e0, float e1, vec4 x) { return vec4(tsSmoothstep(e0, e1, x.x), tsSmoothstep(e0, e1, x.y), tsSmoothstep(e0, e1, x.z), tsSmoothstep(e0, e1, x.w)); }
vec2 tsSmoothstep(vec2 e0, vec2 e1, vec2 x) { return vec2(tsSmoothstep(e0.x, e1.x, x.x), tsSmoothstep(e0.y, e1.y, x.y)); }
vec3 tsSmoothstep(vec3 e0, vec3 e1, vec3 x) { return vec3(tsSmoothstep(e0.x, e1.x, x.x), tsSmoothstep(e0.y, e1.y, x.y), tsSmoothstep(e0.z, e1.z, x.z)); }
// transpose: rows become columns
mat3 tsTranspose(mat3 m) { return mat3(m[0][0], m[1][0], m[2][0], m[0][1], m[1][1], m[2][1], m[0][2], m[1][2], m[2][2]); }
mat4 tsTranspose(mat4 m) {
    return mat4(m[0][0], m[1][0], m[2][0], m[3][0], m[0][1], m[1][1], m[2][1], m[3][1],
                m[0][2], m[1][2], m[2][2], m[3][2], m[0][3], m[1][3], m[2][3], m[3][3]);
}
// ---- end of manual vector maths ----
#line 2
)";

std::string withManualMath(const std::string& src) {
    const size_t v = src.find("#version");
    if (v == std::string::npos) return std::string(kManualMath) + src;
    size_t eol = src.find('\n', v);
    if (eol == std::string::npos) eol = src.size();
    return src.substr(0, eol) + kManualMath + src.substr(std::min(eol + 1, src.size()));
}

std::string builtinVectorCall(const std::string& src) {
    static const char* const kBanned[] = {"dot", "length", "distance", "normalize", "cross", "reflect", "refract", "faceforward",
                                          "mix", "clamp", "step", "smoothstep", "transpose", "inverse"};
    auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    const size_t n = src.size();
    for (size_t i = 0; i < n;) {
        if (src[i] == '/' && i + 1 < n && src[i + 1] == '/') {   // line comment
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (src[i] == '/' && i + 1 < n && src[i + 1] == '*') {   // block comment
            i += 2;
            while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) ++i;
            i += 2;
            continue;
        }
        if (!ident(src[i]) || std::isdigit(static_cast<unsigned char>(src[i]))) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && ident(src[j])) ++j;
        size_t k = j;
        while (k < n && (src[k] == ' ' || src[k] == '\t')) ++k;
        const bool call = k < n && src[k] == '(' && (i == 0 || src[i - 1] != '.');
        if (call) {
            const std::string word = src.substr(i, j - i);
            for (const char* b : kBanned)
                if (word == b) return word;
        }
        i = j;
    }
    return {};
}

}  // namespace ts
