// Hand-written vector maths for every shader of the program. No GLSL built-in vector or interpolation function is
// called: dot, length, normalize, cross, reflect, refract, mix, clamp, step, smoothstep and transpose are written out
// below from their definitions (tsDot, tsLength, ...), and the lighting equations are built from them. What stays with
// the hardware: the scalar elementary functions (sin, cos, pow, exp, sqrt, abs, min, max, floor, fract), texture
// fetches, and the fixed stages after the vertex shader (clipping, perspective divide, rasterisation, depth test).
#pragma once

#include <string>

namespace ts {

extern const char* const kManualMath;   // the GLSL source of the ts* functions

// 'src' with kManualMath inserted after its #version line (every program is compiled through this).
std::string withManualMath(const std::string& src);
// Name of the first GLSL built-in vector function that 'src' calls (comments skipped), or "" when it calls none.
// Shader::build and the ray tracer's compute programs refuse a source that still calls one.
std::string builtinVectorCall(const std::string& src);

}  // namespace ts
