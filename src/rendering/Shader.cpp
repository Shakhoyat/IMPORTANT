#include "rendering/Shader.h"

#include "rendering/ShaderMath.h"

#include <cstdio>
#include <utility>
#include <vector>

namespace ts {

namespace {

GLuint compile(GLenum type, const std::string& source, const std::string& name) {
    // Every program gets the hand-written vector maths (ShaderMath.h); a source that still calls a GLSL built-in vector
    // function is refused.
    if (const std::string b = builtinVectorCall(source); !b.empty()) {
        std::fprintf(stderr, "[shader %s] calls the GLSL built-in %s(): use the ts* function of ShaderMath.h\n", name.c_str(), b.c_str());
        return 0;
    }
    const std::string src = withManualMath(source);
    GLuint s = glCreateShader(type);
    const char* text = src.c_str();
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::fprintf(stderr, "[shader %s] compile error:\n%s\n", name.c_str(), log.data());
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}  // namespace

Shader::~Shader() {
    if (program_) glDeleteProgram(program_);
}

bool Shader::build(const std::string& name, const std::string& vs, const std::string& fs, const std::string& gs) {
    name_ = name;
    GLuint v = compile(GL_VERTEX_SHADER, vs, name + ".vert");
    GLuint f = compile(GL_FRAGMENT_SHADER, fs, name + ".frag");
    GLuint g = gs.empty() ? 0 : compile(GL_GEOMETRY_SHADER, gs, name + ".geom");
    if (!v || !f || (!gs.empty() && !g)) return false;

    program_ = glCreateProgram();
    glAttachShader(program_, v);
    glAttachShader(program_, f);
    if (g) glAttachShader(program_, g);
    glLinkProgram(program_);
    glDeleteShader(v);
    glDeleteShader(f);
    if (g) glDeleteShader(g);

    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[shader %s] link error:\n%s\n", name.c_str(), log);
        return false;
    }
    // v9 M8: the shared lighting samplers sit on fixed texture units (Renderer::kUnit*). A sampler left at its default
    // unit 0 would share it with a sampler of another type in programs that never set it (grass, foliage, effects...),
    // which GL rejects at draw time; so every program that declares them gets the units here.
    const std::pair<const char*, int> units[] = {{"uMoonShadow", 5}, {"uSpotShadow", 6}, {"uAO", 7}, {"uMoonDepth", 12},
                                                 {"uContactShadow", 13}};
    glUseProgram(program_);
    for (const auto& [sampler, unit] : units) {
        const GLint loc = glGetUniformLocation(program_, sampler);
        if (loc >= 0) glUniform1i(loc, unit);
    }
    glUseProgram(0);
    return true;
}

GLint Shader::location(const char* name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second;
    const GLint loc = glGetUniformLocation(program_, name);
    cache_.emplace(name, loc);
    return loc;
}

void Shader::set(const char* name, float v) { glUniform1f(location(name), v); }
void Shader::set(const char* name, int v) { glUniform1i(location(name), v); }
void Shader::set(const char* name, const Vec3& v) { glUniform3f(location(name), v.x, v.y, v.z); }
void Shader::set(const char* name, float x, float y) { glUniform2f(location(name), x, y); }
void Shader::set(const char* name, const Mat4& m) { glUniformMatrix4fv(location(name), 1, GL_FALSE, m.m); }
void Shader::set(const char* name, const Mat3& m) { glUniformMatrix3fv(location(name), 1, GL_FALSE, m.m); }
void Shader::setVec4Array(const char* name, const float* v, int count) { glUniform4fv(location(name), count, v); }
void Shader::setVec3Array(const char* name, const float* v, int count) { glUniform3fv(location(name), count, v); }
void Shader::setVec2Array(const char* name, const float* v, int count) { glUniform2fv(location(name), count, v); }

}  // namespace ts
