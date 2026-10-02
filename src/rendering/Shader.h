#pragma once

#include <glad/gl.h>

#include <string>
#include <unordered_map>

#include "math/Math3D.h"

namespace ts {

class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // geometrySrc may be empty. Returns false and prints the GLSL log on failure.
    bool build(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc,
               const std::string& geometrySrc = "");
    void use() const { glUseProgram(program_); }
    GLuint id() const { return program_; }

    void set(const char* name, float v);
    void set(const char* name, int v);
    void set(const char* name, const Vec3& v);
    void set(const char* name, float x, float y);
    void set(const char* name, const Mat4& m);
    void set(const char* name, const Mat3& m);
    void setVec4Array(const char* name, const float* xyzw, int count);
    void setVec3Array(const char* name, const float* xyz, int count);

private:
    GLint location(const char* name);
    GLuint program_ = 0;
    std::string name_;
    std::unordered_map<std::string, GLint> cache_;
};

}  // namespace ts
