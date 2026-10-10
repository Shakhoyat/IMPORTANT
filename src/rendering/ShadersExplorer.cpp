// v7 WP-I Frame Explorer GLSL: the explorer's own didactic passes over the frozen scene (G-buffer + raster
// + interpolation MRT pass with a geometry shader for barycentrics, Gouraud vs Phong, overdraw, bird's-eye view,
// wireframe / back faces) and the display shader of the overlay (image, wipe, magnifier, scanline reveal, sampling).
namespace ts::glsl {

// ---------------------------------------------------------------- MRT "geometry" pass (VS + GS + FS)
const char* kExplGeoVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel, uViewProj;
uniform mat3 uNormalMat;
out vec3 vsWorld;
out vec3 vsNormal;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vsWorld = w.xyz;
    vsNormal = uNormalMat * aNormal;
    gl_Position = uViewProj * w;
}
)";

// Passes each triangle through unchanged and gives its corners the barycentric "colours" (1,0,0), (0,1,0), (0,0,1),
// once perspective-correct (smooth) and once screen-linear (noperspective), plus the world position both ways.
const char* kExplGeoGeom = R"(#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in vec3 vsWorld[];
in vec3 vsNormal[];
out vec3 gWorld;
noperspective out vec3 gWorldAffine;
out vec3 gNormal;
out vec3 gBary;
noperspective out vec3 gBaryScreen;
void main() {
    for (int i = 0; i < 3; ++i) {
        gl_Position = gl_in[i].gl_Position;
        gWorld = vsWorld[i];
        gWorldAffine = vsWorld[i];
        gNormal = vsNormal[i];
        gBary = vec3(i == 0 ? 1.0 : 0.0, i == 1 ? 1.0 : 0.0, i == 2 ? 1.0 : 0.0);
        gBaryScreen = gBary;
        gl_PrimitiveID = gl_PrimitiveIDIn;
        EmitVertex();
    }
    EndPrimitive();
}
)";

const char* kExplGeoFrag = R"(#version 330 core
in vec3 gWorld;
noperspective in vec3 gWorldAffine;
in vec3 gNormal;
in vec3 gBary;
noperspective in vec3 gBaryScreen;
uniform vec3 uAlbedo;
uniform vec4 uMaterial;       // roughness, metalness, emissive, specular
uniform float uShininess;
uniform float uLeaf;          // leaf index + 1
uniform vec3 uCamPos;
uniform vec3 uLightDir;       // direction the key light travels
uniform int uShadows;
uniform sampler2DArrayShadow uMoonShadow;
uniform mat4 uMoonVP0, uMoonVP1;
uniform vec2 uMoonTexel;
uniform float uCascadeSplit;
layout(location = 0) out vec4 oAlbedo;
layout(location = 1) out vec4 oMaterial;
layout(location = 2) out vec4 oIds;
layout(location = 3) out vec4 oLight;
layout(location = 4) out vec4 oRaster;
layout(location = 5) out vec4 oCheckerAffine;
layout(location = 6) out vec4 oCheckerPersp;
layout(location = 7) out vec4 oBary;

float hash1(float n) { return fract(sin(n) * 43758.5453123); }
vec3 idColour(float leaf, float prim) {
    float h = hash1(leaf * 12.9898 + prim * 78.233);
    vec3 c = 0.5 + 0.5 * cos(6.28318 * (h + vec3(0.0, 0.33, 0.67)));
    return tsMix(c, vec3(tsDot(c, vec3(0.33))), 0.15) * (0.55 + 0.45 * hash1(leaf * 3.1 + prim * 1.7));
}
// 1 on the triangle's edges (about 1.2 px wide), 0 inside.
float edgeLine(vec3 b) {
    vec3 d = fwidth(b);
    vec3 a = tsSmoothstep(vec3(0.0), d * 1.2, b);
    // triangles only a few pixels across would be all edge: fade them out
    return (1.0 - min(min(a.x, a.y), a.z)) * (1.0 - tsSmoothstep(0.12, 0.4, max(d.x, max(d.y, d.z))));
}
float checker(vec3 p, vec3 n) {
    vec3 an = abs(n);
    vec2 uv = an.y > max(an.x, an.z) ? p.xz : (an.x > an.z ? p.zy : p.xy);
    vec2 c = floor(uv);
    float v = mod(c.x + c.y, 2.0);
    vec2 fw = fwidth(uv);
    return tsMix(v, 0.5, tsSmoothstep(0.25, 0.7, max(fw.x, fw.y)));   // too small to show: average grey
}
float cascade(int layer, mat4 vp, vec3 P, vec3 N, float texel) {
    vec3 L = tsNormalize(-uLightDir);
    float slope = 1.0 - max(tsDot(N, L), 0.0);
    vec4 lp = vp * vec4(P + N * texel * (1.0 + 2.0 * slope), 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    vec2 size = vec2(textureSize(uMoonShadow, 0).xy);
    float sum = 0.0;
    for (int y = 0; y < 4; ++y)        // PCF: 4 x 4 depth comparisons, each bilinearly filtered by the hardware
        for (int x = 0; x < 4; ++x) {
            vec2 o = (vec2(x, y) - 1.5) * 0.9 / size;
            sum += texture(uMoonShadow, vec4(p.xy + o, float(layer), p.z - 0.0004));
        }
    return sum / 16.0;
}
float shadowAt(vec3 P, vec3 N) {
    if (uShadows == 0) return 1.0;
    float dist = tsLength(P - uCamPos);
    return dist < uCascadeSplit ? cascade(0, uMoonVP0, P, N, uMoonTexel.x) : cascade(1, uMoonVP1, P, N, uMoonTexel.y);
}
void main() {
    vec3 N = tsNormalize(gNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 L = tsNormalize(-uLightDir);
    vec3 V = tsNormalize(uCamPos - gWorld);
    vec3 H = tsNormalize(L + V);
    float NL = max(tsDot(N, L), 0.0);
    float spec = NL > 0.0 ? pow(max(tsDot(N, H), 0.0), uShininess) : 0.0;
    float s = shadowAt(gWorld, N);
    oAlbedo = vec4(uAlbedo, 1.0);
    oMaterial = vec4(uMaterial.xyz, 1.0);
    oIds = vec4(uLeaf, float(gl_PrimitiveID), gl_FrontFacing ? 1.0 : 0.0, 1.0);
    oLight = vec4(s, NL, spec * uMaterial.w, 1.0);   // b = k_s (N.H)^n
    float shade = 0.55 + 0.45 * max(tsDot(N, V), 0.0);
    vec3 rc = idColour(uLeaf, float(gl_PrimitiveID)) * shade;
    oRaster = vec4(tsMix(rc, vec3(0.015), edgeLine(gBaryScreen) * 0.8), 1.0);
    float lightShade = 0.45 + 0.55 * max(tsDot(N, V), 0.0);
    vec3 dark = vec3(0.10, 0.11, 0.14), bright = vec3(0.92, 0.78, 0.50);
    oCheckerAffine = vec4(tsMix(dark, bright, checker(gWorldAffine, N)) * lightShade, 1.0);
    oCheckerPersp = vec4(tsMix(dark, bright, checker(gWorld, N)) * lightShade, 1.0);
    oBary = vec4(tsMix(gBary * 0.95, vec3(1.0), edgeLine(gBaryScreen) * 0.6), 1.0);
}
)";

// ---------------------------------------------------------------- Gouraud vs Phong (Blinn-Phong, key light)
const char* kExplShadeVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel, uViewProj;
uniform mat3 uNormalMat;
uniform vec3 uAlbedo, uCamPos, uLightDir, uLightColor, uAmbient;
uniform float uSpecular, uShininess;
out vec3 vWorld;
out vec3 vNormal;
out vec3 vGouraud;
vec3 blinnPhong(vec3 P, vec3 N) {
    vec3 L = tsNormalize(-uLightDir), V = tsNormalize(uCamPos - P), H = tsNormalize(L + V);
    float NL = max(tsDot(N, L), 0.0);
    float spec = NL > 0.0 ? pow(max(tsDot(N, H), 0.0), uShininess) : 0.0;
    return uAlbedo * (uAmbient + uLightColor * NL) + uLightColor * uSpecular * spec;
}
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNormal = tsNormalize(uNormalMat * aNormal);
    vGouraud = blinnPhong(vWorld, vNormal);   // lit once per VERTEX, the colour is interpolated
    gl_Position = uViewProj * w;
}
)";

const char* kExplShadeFrag = R"(#version 330 core
in vec3 vWorld;
in vec3 vNormal;
in vec3 vGouraud;
uniform vec3 uAlbedo, uCamPos, uLightDir, uLightColor, uAmbient;
uniform float uSpecular, uShininess, uExposure;
layout(location = 0) out vec4 oGouraud;
layout(location = 1) out vec4 oPhong;
vec3 blinnPhong(vec3 P, vec3 N) {
    vec3 L = tsNormalize(-uLightDir), V = tsNormalize(uCamPos - P), H = tsNormalize(L + V);
    float NL = max(tsDot(N, L), 0.0);
    float spec = NL > 0.0 ? pow(max(tsDot(N, H), 0.0), uShininess) : 0.0;
    return uAlbedo * (uAmbient + uLightColor * NL) + uLightColor * uSpecular * spec;
}
vec3 display(vec3 c) { c *= uExposure; c = c / (1.0 + c); return pow(c, vec3(1.0 / 2.2)); }
void main() {
    vec3 N = tsNormalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    oGouraud = vec4(display(vGouraud), 1.0);
    oPhong = vec4(display(blinnPhong(vWorld, N)), 1.0);   // the NORMAL is interpolated, lit per PIXEL
}
)";

// ---------------------------------------------------------------- overdraw count / bird's-eye view
const char* kExplSimpleVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel, uViewProj;
uniform mat3 uNormalMat;
out vec3 vWorld;
out vec3 vNormal;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNormal = uNormalMat * aNormal;
    gl_Position = uViewProj * w;
}
)";

const char* kExplOverdrawFrag = R"(#version 330 core
layout(location = 0) out vec4 oCount;
void main() { oCount = vec4(1.0, 0.0, 0.0, 1.0); }   // additive blending: one per fragment
)";

// External camera: objects inside the main camera's frustum in full colour, the culled rest dimmed and blue.
const char* kExplBirdFrag = R"(#version 330 core
in vec3 vWorld;
in vec3 vNormal;
uniform vec3 uAlbedo;
uniform mat4 uCamViewProj;   // the frozen camera
uniform vec3 uEye;           // the external camera
layout(location = 0) out vec4 oColor;
void main() {
    vec3 N = tsNormalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 L = tsNormalize(vec3(0.35, 0.85, 0.40));
    vec3 c = pow(uAlbedo, vec3(0.8)) * (0.30 + 0.70 * max(tsDot(N, L), 0.0)) + vec3(0.02);
    vec4 clip = uCamViewProj * vec4(vWorld, 1.0);
    bool inside = clip.w > 0.0 && all(lessThanEqual(abs(clip.xyz), vec3(clip.w)));
    if (!inside) c = tsMix(vec3(tsDot(c, vec3(0.3, 0.59, 0.11))) * vec3(0.55, 0.65, 0.95), vec3(0.03, 0.05, 0.09), 0.55);
    float fog = 1.0 - exp(-tsLength(vWorld - uEye) * 0.006);
    c = tsMix(c, vec3(0.035, 0.05, 0.08), fog);
    oColor = vec4(pow(c, vec3(1.0 / 1.6)), 1.0);
}
)";

// Primitive assembly: front faces as dim solids with cyan edges; back faces (normally culled) as red x-ray.
const char* kExplWireFrag = R"(#version 330 core
in vec3 gWorld;
noperspective in vec3 gWorldAffine;
in vec3 gNormal;
in vec3 gBary;
noperspective in vec3 gBaryScreen;
uniform int uBack;          // 0 front faces (back faces culled), 1 back faces only (x-ray, additive)
uniform vec3 uCamPos;
layout(location = 0) out vec4 oColor;
float edgeLine(vec3 b) {
    vec3 d = fwidth(b);
    vec3 a = tsSmoothstep(vec3(0.0), d * 1.1, b);
    return (1.0 - min(min(a.x, a.y), a.z)) * (1.0 - tsSmoothstep(0.15, 0.5, max(d.x, max(d.y, d.z))));
}
void main() {
    float e = edgeLine(gBaryScreen);
    if (uBack == 1) {
        oColor = vec4(vec3(1.0, 0.16, 0.10) * (0.05 + 0.55 * e), 1.0);
        return;
    }
    vec3 N = tsNormalize(gNormal);
    float nv = abs(tsDot(N, tsNormalize(uCamPos - gWorld)));
    vec3 fill = vec3(0.07, 0.09, 0.12) * (0.4 + 0.8 * nv);
    oColor = vec4(tsMix(fill, vec3(0.25, 0.95, 1.0), e * 0.9), 1.0);
}
)";

// ---------------------------------------------------------------- display (overlay images)
const char* kExplDisplayVert = R"(#version 330 core
uniform vec4 uRect;   // x0, y0, x1, y1 in NDC
uniform vec4 uUv;     // u0, v0, u1, v1
out vec2 vUV;
out vec2 vLocal;
void main() {
    const vec2 k[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));
    vec2 t = k[gl_VertexID];
    vLocal = t;
    vUV = tsMix(uUv.xy, uUv.zw, t);
    gl_Position = vec4(tsMix(uRect.xy, uRect.zw, t), 0.0, 1.0);
}
)";

const char* kExplDisplayFrag = R"(#version 330 core
in vec2 vUV;
in vec2 vLocal;
uniform sampler2D uTexA;
uniform sampler2D uTexB;
uniform int uMode;        // 0 image, 1 split / wipe, 2 magnifier, 3 scanline reveal, 4 sampling demo
uniform float uSplit;     // 1: split position (u); 3: scanline (v)
uniform float uAlpha;
uniform float uDim;       // 3: brightness of the not yet rasterised part
uniform int uMethod;      // 4: 0 nearest, 1 bilinear (level 0), 2 trilinear mipmap
uniform float uFactor;    // 4: minification factor
uniform float uGrid;      // 2: draw the pixel grid
out vec4 Color;
void main() {
    vec3 c;
    if (uMode == 1) {
        c = vUV.x < uSplit ? texture(uTexA, vUV).rgb : texture(uTexB, vUV).rgb;
        float px = fwidth(vUV.x);
        float d = abs(vUV.x - uSplit) / max(px, 1e-6);
        c = tsMix(c, vec3(1.0, 0.78, 0.32), tsClamp(1.5 - d, 0.0, 1.0));
        c += vec3(1.0, 0.6, 0.2) * exp(-d * 0.25) * 0.18;
    } else if (uMode == 2) {
        vec2 size = vec2(textureSize(uTexA, 0));
        vec2 p = vUV * size;
        c = texelFetch(uTexA, tsClamp(ivec2(floor(p)), ivec2(0), ivec2(size) - 1), 0).rgb;
        if (uGrid > 0.5) {
            vec2 f = fract(p);
            vec2 w = fwidth(p);
            float g = max(1.0 - tsSmoothstep(0.0, w.x * 1.2, min(f.x, 1.0 - f.x)), 1.0 - tsSmoothstep(0.0, w.y * 1.2, min(f.y, 1.0 - f.y)));
            c = tsMix(c, vec3(0.0), g * 0.35);
        }
    } else if (uMode == 3) {
        bool done = vUV.y > uSplit;
        c = done ? texture(uTexA, vUV).rgb : texture(uTexB, vUV).rgb * uDim;
        float d = (vUV.y - uSplit) / max(fwidth(vUV.y), 1e-6);
        c += vec3(0.3, 1.0, 0.85) * (exp(-abs(d) * 0.9) * 0.9 + (d > 0.0 ? exp(-d * 0.05) * 0.22 : 0.0));
    } else if (uMode == 4) {
        vec2 size = vec2(textureSize(uTexA, 0));
        vec2 small = max(floor(size / uFactor), vec2(1.0));
        vec2 centre = (floor(vUV * small) + 0.5) / small;
        if (uMethod == 0) c = texelFetch(uTexA, ivec2(centre * size), 0).rgb;
        else if (uMethod == 1) c = textureLod(uTexA, centre, 0.0).rgb;
        else c = textureLod(uTexA, centre, log2(uFactor)).rgb;
    } else {
        c = texture(uTexA, vUV).rgb;
    }
    Color = vec4(c, uAlpha);
}
)";

// ---------------------------------------------------------------- shadow map layer 0 -> small float image (display)
const char* kExplShadowCopyFrag = R"(#version 330 core
in vec2 vUV;
in vec2 vLocal;
uniform sampler2DArray uShadow;   // the renderer's moon map with comparison switched off: raw depths
out vec4 Color;
void main() { Color = vec4(texture(uShadow, vec3(vLocal, 0.0)).r, 0.0, 0.0, 1.0); }
)";

}  // namespace ts::glsl
