// v7 WP-Q: per-instance data of the scene parts for instanced draws.
// The solids, the depth pre-pass and the shadow maps used to issue one draw per part (~4500 draws with ~20 uniform
// updates each). On Mesa d3d12 (WSL2) every draw re-uploads the whole default uniform block (~3 KB for the lighting
// shaders), about 20 us of GPU time per draw; natively it costs CPU time in the driver. Now each pass packs the
// per-part values (model matrix, normal matrix, material) into a buffer texture (core since GL 3.1, so the native
// GL 3.3 context works too) and issues one glDrawElementsInstanced per (program, primitive mesh); the shaders fetch
// their part with texelFetch(uInstances, (uInstanceBase + gl_InstanceID) * stride + k).
#pragma once

#include <glad/gl.h>

#include <vector>

namespace ts {

class InstanceStream {
public:
    InstanceStream() = default;
    ~InstanceStream();
    InstanceStream(const InstanceStream&) = delete;
    InstanceStream& operator=(const InstanceStream&) = delete;

    bool init(int ringSize = 16);
    bool ready() const { return !buffers_.empty(); }
    // Uploads 'texels' (RGBA32F, 4 floats each) into the next buffer of the ring and binds it as the buffer texture
    // of texture unit 'unit'. False (nothing bound) if the data exceeds GL_MAX_TEXTURE_BUFFER_SIZE.
    bool upload(const std::vector<float>& texels, int unit);
    long long maxTexels() const { return maxTexels_; }

private:
    std::vector<GLuint> buffers_, textures_;
    int next_ = 0;
    long long maxTexels_ = 0;
};

}  // namespace ts
