#include "primitives/GrassBlade.h"

#include <cmath>

namespace ts {

MeshData createGrassBlade() {
    MeshData m;
    m.name = "GrassBlade";
    // Folded blade: the edge normals tilt outwards (a midrib fold), so a blade catches the moon / sun at two angles
    // across its width as the cube's faces did; the strip is 1.25 wide to make up for the cube's thickness.
    const float ts[3] = {0.0f, 0.38f, 0.72f};
    const float fold = 0.55f, k = 1.0f / std::sqrt(1.0f + fold * fold);
    for (float t : ts)
        for (float x : {-0.625f, 0.625f}) m.vertices.push_back({{x, t - 0.5f, 0.0f}, {(x < 0.0f ? -fold : fold) * k, 0.0f, k}});
    m.vertices.push_back({{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}});   // tip
    m.indices = {0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4, 4, 5, 6};
    return m;
}

}  // namespace ts
