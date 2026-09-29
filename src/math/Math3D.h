// Minimal 3D math used by ThermalSense.
// Written by hand (no GLM) so that every matrix used in the report is visible in code.
// Matrices are column-major (OpenGL convention): element (row r, column c) is m[c * 4 + r].
#pragma once

#include <cmath>

namespace ts {

constexpr float PI = 3.14159265358979323846f;

inline float radians(float degrees) { return degrees * PI / 180.0f; }
inline float degrees(float radiansValue) { return radiansValue * 180.0f / PI; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3() = default;
    constexpr Vec3(float xv, float yv, float zv) : x(xv), y(yv), z(zv) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
};

inline Vec3 mul(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    const float len = length(v);
    return len > 1e-8f ? v / len : Vec3(0.0f, 0.0f, 0.0f);
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

// 3x3 matrix, column-major; used for the normal matrix (inverse-transpose of the model 3x3).
struct Mat3 {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float at(int row, int col) const { return m[col * 4 + row]; }
    float& at(int row, int col) { return m[col * 4 + row]; }
    static Mat4 identity() { return Mat4{}; }
};

Mat4 operator*(const Mat4& a, const Mat4& b);
Vec4 operator*(const Mat4& a, const Vec4& v);

Vec3 transformPoint(const Mat4& m, const Vec3& p);      // w = 1
Vec3 transformDirection(const Mat4& m, const Vec3& d);  // w = 0

Mat4 translate(const Vec3& t);
Mat4 scale(const Vec3& s);
Mat4 rotateX(float angleRadians);
Mat4 rotateY(float angleRadians);
Mat4 rotateZ(float angleRadians);
// Euler rotation used throughout the project: R = Ry(yaw) * Rx(pitch) * Rz(roll) (angles in degrees).
Mat4 rotateEulerYXZ(const Vec3& eulerDegrees);

Mat4 perspective(float fovyRadians, float aspect, float zNear, float zFar);
Mat4 ortho(float left, float right, float bottom, float top, float zNear, float zFar);
Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up);

Mat4 inverse(const Mat4& m);  // general 4x4 inverse (identity if singular)
Mat3 normalMatrix(const Mat4& model);

}  // namespace ts
