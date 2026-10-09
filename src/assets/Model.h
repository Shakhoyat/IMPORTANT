// v9 M6: CPU-side 3D models loaded from glTF 2.0 files (cgltf, MIT, include/cgltf.h) -- the CC0 vegetation and rocks
// of assets/vegetation. Pure CPU (no OpenGL): the loader, the LOD chain (FoliageLod.h) and the offline baker
// (tools/bake_assets.cpp) share it, and the unit tests load a tiny embedded glTF through the same code.
//
// A model is a list of materials and a list of variants. Every top-level glTF node that carries a mesh becomes one
// variant (Poly Haven ships three trees side by side as three nodes): its node transform is applied and the variant is
// moved so its root sits at the origin (x = z = 0). Each glTF primitive becomes one MeshPart (one material).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ts::asset {

enum class AlphaMode { Opaque = 0, Mask = 1, Blend = 2 };

struct MaterialInfo {
    std::string name;
    std::string baseColor, normal, arm;   // texture files relative to the model's directory ("" = none)
    std::string alpha;                    // separate cut-out mask (Poly Haven ships it beside the JPG albedo); baked away
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    float baseColorFactor[4] = {1, 1, 1, 1};
};

struct MeshPart {
    int material = 0;
    std::vector<float> pos;        // x y z
    std::vector<float> nrm;        // nx ny nz
    std::vector<float> uv;         // u v
    std::vector<uint8_t> color;    // r g b a per vertex (wind weights, see FoliageLod.h); empty = none
    std::vector<uint32_t> idx;     // triangle list
    size_t vertexCount() const { return pos.size() / 3; }
    size_t triangleCount() const { return idx.size() / 3; }
};

struct Variant {
    std::string name;
    std::vector<MeshPart> parts;
    float minY = 0.0f, maxY = 0.0f, radius = 0.0f;   // bounds after recentring (radius: max distance from the y axis)
    size_t triangles() const;
    void computeBounds();
};

struct Model {
    std::string dir;                        // directory of the .gltf (texture paths are relative to it)
    std::vector<MaterialInfo> materials;
    std::vector<Variant> variants;
    size_t triangles() const;
};

// Loads a .gltf (+ .bin) file. Positions, normals, UV0 and COLOR_0 (if present) are read; other attributes are ignored.
bool loadGltf(const std::string& path, Model& out, std::string* error = nullptr);
// Same from memory (buffers must be embedded as data: URIs). Used by the unit tests.
bool loadGltfMemory(const std::string& json, Model& out, std::string* error = nullptr);
// Writes the model as <path> (.gltf JSON) + <path minus .gltf>.bin. Textures are referenced, not copied.
bool writeGltf(const std::string& path, const Model& model, std::string* error = nullptr);

}  // namespace ts::asset
