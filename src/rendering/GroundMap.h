// Ground data map: a 1024 x 1024 RGBA texture over the terrain, computed once on the CPU from the scene layout
// (no image files). The terrain shader uses it to decide which ground material is where, and the grass
// placement uses the same densities, so blades grow exactly where the ground is shaded as meadow.
//   R  wetness     1 in the creek bed, fading up the banks
//   G  track       soft mask of the vehicle track (ruts are placed analytically in the shader)
//   B  forest      1 under tree crowns (needle / leaf litter)
//   A  meadow      grass density 0..1
#pragma once

#include <glad/gl.h>

#include <vector>

#include "scene/Scene.h"

namespace ts {

class GroundMap {
public:
    ~GroundMap();
    void build(const Scene& scene);
    GLuint texture() const { return tex_; }
    static float extent();                    // half size of the covered square, metres
    float meadow(float x, float z) const;     // bilinear sample of the A channel (CPU side, for grass)
    float forest(float x, float z) const;

private:
    float sample(int channel, float x, float z) const;
    static constexpr int kSize = 1024;
    std::vector<unsigned char> texels_;
    GLuint tex_ = 0;
};

}  // namespace ts
