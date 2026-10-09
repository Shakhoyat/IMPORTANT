#include "assets/Model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ts::asset {

size_t Variant::triangles() const {
    size_t n = 0;
    for (const MeshPart& p : parts) n += p.triangleCount();
    return n;
}

void Variant::computeBounds() {
    minY = 1e30f;
    maxY = -1e30f;
    radius = 0.0f;
    for (const MeshPart& p : parts)
        for (size_t i = 0; i < p.vertexCount(); ++i) {
            const float x = p.pos[3 * i], y = p.pos[3 * i + 1], z = p.pos[3 * i + 2];
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            radius = std::max(radius, std::sqrt(x * x + z * z));
        }
    if (minY > maxY) minY = maxY = 0.0f;
}

size_t Model::triangles() const {
    size_t n = 0;
    for (const Variant& v : variants) n += v.triangles();
    return n;
}

namespace {

std::string dirOf(const std::string& path) {
    const size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? std::string(".") : path.substr(0, s);
}

std::string imageUri(const cgltf_texture_view& v) {
    if (!v.texture || !v.texture->image || !v.texture->image->uri) return "";
    std::string uri = v.texture->image->uri;
    // percent-decoding of spaces only (Poly Haven / our baker never use other escapes)
    for (size_t p = uri.find("%20"); p != std::string::npos; p = uri.find("%20")) uri.replace(p, 3, " ");
    return uri;
}

// Column-major 4x4 times point / direction.
void xformPoint(const float* m, const float* p, float* o) {
    for (int r = 0; r < 3; ++r) o[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
}
void xformDir(const float* m, const float* d, float* o) {
    for (int r = 0; r < 3; ++r) o[r] = m[r] * d[0] + m[4 + r] * d[1] + m[8 + r] * d[2];
    const float l = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
    if (l > 0.0f)
        for (int r = 0; r < 3; ++r) o[r] /= l;
}

bool convert(cgltf_data* data, Model& out, std::string* error) {
    out.materials.clear();
    out.variants.clear();
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        MaterialInfo mi;
        mi.name = m.name ? m.name : ("material" + std::to_string(i));
        if (m.has_pbr_metallic_roughness) {
            mi.baseColor = imageUri(m.pbr_metallic_roughness.base_color_texture);
            mi.arm = imageUri(m.pbr_metallic_roughness.metallic_roughness_texture);
            for (int k = 0; k < 4; ++k) mi.baseColorFactor[k] = m.pbr_metallic_roughness.base_color_factor[k];
        }
        mi.normal = imageUri(m.normal_texture);
        mi.alphaMode = m.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask
                       : m.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend : AlphaMode::Opaque;
        mi.alphaCutoff = m.alpha_mode == cgltf_alpha_mode_mask ? m.alpha_cutoff : 0.5f;
        mi.doubleSided = m.double_sided;
        out.materials.push_back(mi);
    }
    if (out.materials.empty()) {
        MaterialInfo def;
        def.name = "default";
        out.materials.push_back(def);
    }

    // Variants: the scene's root nodes (or every root node when there is no scene) with a mesh somewhere below them.
    std::vector<cgltf_node*> roots;
    if (data->scene) {
        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) roots.push_back(data->scene->nodes[i]);
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
            if (!data->nodes[i].parent) roots.push_back(&data->nodes[i]);
    }
    for (cgltf_node* root : roots) {
        Variant v;
        v.name = root->name ? root->name : ("variant" + std::to_string(out.variants.size()));
        float rootWorld[16];
        cgltf_node_transform_world(root, rootWorld);
        std::vector<cgltf_node*> stack{root};
        while (!stack.empty()) {
            cgltf_node* n = stack.back();
            stack.pop_back();
            for (cgltf_size c = 0; c < n->children_count; ++c) stack.push_back(n->children[c]);
            if (!n->mesh) continue;
            float world[16];
            cgltf_node_transform_world(n, world);
            for (cgltf_size pi = 0; pi < n->mesh->primitives_count; ++pi) {
                const cgltf_primitive& prim = n->mesh->primitives[pi];
                if (prim.type != cgltf_primitive_type_triangles) continue;
                const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *col = nullptr;
                for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                    const cgltf_attribute& at = prim.attributes[a];
                    if (at.type == cgltf_attribute_type_position) pos = at.data;
                    else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                    else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
                    else if (at.type == cgltf_attribute_type_color && at.index == 0) col = at.data;
                }
                if (!pos) continue;
                MeshPart part;
                part.material = prim.material ? static_cast<int>(prim.material - data->materials) : 0;
                const size_t nv = pos->count;
                std::vector<float> tmp(nv * 4);
                cgltf_accessor_unpack_floats(pos, tmp.data(), nv * 3);
                part.pos.resize(nv * 3);
                for (size_t k = 0; k < nv; ++k) xformPoint(world, &tmp[3 * k], &part.pos[3 * k]);
                part.nrm.assign(nv * 3, 0.0f);
                if (nrm && nrm->count == nv) {
                    cgltf_accessor_unpack_floats(nrm, tmp.data(), nv * 3);
                    for (size_t k = 0; k < nv; ++k) xformDir(world, &tmp[3 * k], &part.nrm[3 * k]);
                } else {
                    for (size_t k = 0; k < nv; ++k) part.nrm[3 * k + 1] = 1.0f;
                }
                part.uv.assign(nv * 2, 0.0f);
                if (uv && uv->count == nv) cgltf_accessor_unpack_floats(uv, part.uv.data(), nv * 2);
                if (col && col->count == nv) {
                    const cgltf_size comps = cgltf_num_components(col->type);
                    cgltf_accessor_unpack_floats(col, tmp.data(), nv * comps);
                    part.color.resize(nv * 4);
                    for (size_t k = 0; k < nv; ++k)
                        for (size_t c = 0; c < 4; ++c) {
                            const float f = c < comps ? tmp[k * comps + c] : 1.0f;
                            part.color[4 * k + c] = static_cast<uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
                        }
                }
                if (prim.indices) {
                    part.idx.resize(prim.indices->count);
                    for (size_t k = 0; k < prim.indices->count; ++k)
                        part.idx[k] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, k));
                } else {
                    part.idx.resize(nv);
                    for (size_t k = 0; k < nv; ++k) part.idx[k] = static_cast<uint32_t>(k);
                }
                part.idx.resize(part.idx.size() / 3 * 3);
                v.parts.push_back(std::move(part));
            }
        }
        if (v.parts.empty()) continue;
        // Root at the origin: the root node's horizontal offset (Poly Haven lines variants up along x) is removed.
        for (MeshPart& p : v.parts)
            for (size_t k = 0; k < p.vertexCount(); ++k) {
                p.pos[3 * k] -= rootWorld[12];
                p.pos[3 * k + 2] -= rootWorld[14];
            }
        v.computeBounds();
        out.variants.push_back(std::move(v));
    }
    if (out.variants.empty()) {
        if (error) *error = "no triangle meshes";
        return false;
    }
    return true;
}

const char* resultName(cgltf_result r) {
    switch (r) {
        case cgltf_result_success: return "ok";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid json";
        case cgltf_result_invalid_gltf: return "invalid gltf";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "io error";
        case cgltf_result_out_of_memory: return "out of memory";
        default: return "error";
    }
}

}  // namespace

bool loadGltf(const std::string& path, Model& out, std::string* error) {
    cgltf_options opt;
    std::memset(&opt, 0, sizeof(opt));
    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse_file(&opt, path.c_str(), &data);
    if (r == cgltf_result_success) r = cgltf_load_buffers(&opt, data, path.c_str());
    if (r == cgltf_result_success) r = cgltf_validate(data);
    if (r != cgltf_result_success) {
        if (error) *error = std::string("cgltf: ") + resultName(r) + " (" + path + ")";
        if (data) cgltf_free(data);
        return false;
    }
    out.dir = dirOf(path);
    const bool ok = convert(data, out, error);
    cgltf_free(data);
    return ok;
}

bool loadGltfMemory(const std::string& json, Model& out, std::string* error) {
    cgltf_options opt;
    std::memset(&opt, 0, sizeof(opt));
    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse(&opt, json.data(), json.size(), &data);
    if (r == cgltf_result_success) r = cgltf_load_buffers(&opt, data, nullptr);
    if (r == cgltf_result_success) r = cgltf_validate(data);
    if (r != cgltf_result_success) {
        if (error) *error = std::string("cgltf: ") + resultName(r);
        if (data) cgltf_free(data);
        return false;
    }
    out.dir = ".";
    const bool ok = convert(data, out, error);
    cgltf_free(data);
    return ok;
}

// ---------------------------------------------------------------- writer (offline baker)

namespace {

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

std::string num(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.7g", static_cast<double>(v));
    return b;
}

}  // namespace

bool writeGltf(const std::string& path, const Model& model, std::string* error) {
    std::string binPath = path;
    if (binPath.size() > 5 && binPath.substr(binPath.size() - 5) == ".gltf") binPath = binPath.substr(0, binPath.size() - 5);
    binPath += ".bin";
    const std::string binName = binPath.substr(binPath.find_last_of("/\\") == std::string::npos ? 0 : binPath.find_last_of("/\\") + 1);

    std::vector<uint8_t> bin;
    std::ostringstream views, accs;
    int viewCount = 0, accCount = 0;
    bool quantized = false;
    auto addView = [&](const void* data, size_t bytes, int target, int stride = 0) {
        while (bin.size() % 4) bin.push_back(0);
        const size_t off = bin.size();
        bin.insert(bin.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes);
        views << (viewCount ? ",\n    " : "    ") << "{\"buffer\": 0, \"byteOffset\": " << off << ", \"byteLength\": " << bytes
              << (stride ? ", \"byteStride\": " + std::to_string(stride) : std::string()) << ", \"target\": " << target << "}";
        return viewCount++;
    };
    auto addAccessor = [&](int view, int compType, size_t count, const char* type, bool normalized, const std::string& minMax) {
        accs << (accCount ? ",\n    " : "    ") << "{\"bufferView\": " << view << ", \"componentType\": " << compType
             << ", \"count\": " << count << ", \"type\": \"" << type << "\"" << (normalized ? ", \"normalized\": true" : "") << minMax
             << "}";
        return accCount++;
    };

    std::ostringstream meshes, nodes;
    int meshCount = 0;
    for (const Variant& v : model.variants) {
        std::ostringstream prims;
        int primCount = 0;
        for (const MeshPart& p : v.parts) {
            if (p.idx.empty()) continue;
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (size_t k = 0; k < p.vertexCount(); ++k)
                for (int c = 0; c < 3; ++c) {
                    lo[c] = std::min(lo[c], p.pos[3 * k + c]);
                    hi[c] = std::max(hi[c], p.pos[3 * k + c]);
                }
            const std::string mm = ", \"min\": [" + num(lo[0]) + ", " + num(lo[1]) + ", " + num(lo[2]) + "], \"max\": [" + num(hi[0]) +
                                   ", " + num(hi[1]) + ", " + num(hi[2]) + "]";
            const int aPos = addAccessor(addView(p.pos.data(), p.pos.size() * 4, 34962), 5126, p.vertexCount(), "VEC3", false, mm);
            // KHR_mesh_quantization: normals as normalised bytes (4-byte stride), UVs in [0, 1] as normalised shorts.
            std::vector<int8_t> qn(p.vertexCount() * 4, 0);
            for (size_t k = 0; k < p.vertexCount(); ++k)
                for (int c = 0; c < 3; ++c)
                    qn[4 * k + c] = static_cast<int8_t>(std::lround(std::clamp(p.nrm[3 * k + c], -1.0f, 1.0f) * 127.0f));
            const int aNrm = addAccessor(addView(qn.data(), qn.size(), 34962, 4), 5120, p.vertexCount(), "VEC3", true, "");
            quantized = true;
            bool unitUv = true;
            for (float u : p.uv) unitUv = unitUv && u >= 0.0f && u <= 1.0f;
            int aUv;
            if (unitUv) {
                std::vector<uint16_t> qu(p.uv.size());
                for (size_t k = 0; k < p.uv.size(); ++k) qu[k] = static_cast<uint16_t>(std::lround(p.uv[k] * 65535.0f));
                aUv = addAccessor(addView(qu.data(), qu.size() * 2, 34962), 5123, p.vertexCount(), "VEC2", true, "");
            } else {
                aUv = addAccessor(addView(p.uv.data(), p.uv.size() * 4, 34962), 5126, p.vertexCount(), "VEC2", false, "");
            }
            int aCol = -1;
            if (p.color.size() == p.vertexCount() * 4)
                aCol = addAccessor(addView(p.color.data(), p.color.size(), 34962), 5121, p.vertexCount(), "VEC4", true, "");
            const int aIdx = addAccessor(addView(p.idx.data(), p.idx.size() * 4, 34963), 5125, p.idx.size(), "SCALAR", false, "");
            prims << (primCount++ ? ", " : "") << "{\"attributes\": {\"POSITION\": " << aPos << ", \"NORMAL\": " << aNrm
                  << ", \"TEXCOORD_0\": " << aUv << (aCol >= 0 ? ", \"COLOR_0\": " + std::to_string(aCol) : std::string())
                  << "}, \"indices\": " << aIdx << ", \"material\": " << p.material << "}";
        }
        meshes << (meshCount ? ",\n    " : "    ") << "{\"name\": \"" << esc(v.name) << "\", \"primitives\": [" << prims.str() << "]}";
        nodes << (meshCount ? ",\n    " : "    ") << "{\"name\": \"" << esc(v.name) << "\", \"mesh\": " << meshCount << "}";
        ++meshCount;
    }

    std::ostringstream images, textures, mats;
    int imageCount = 0;
    auto tex = [&](const std::string& uri) {
        images << (imageCount ? ",\n    " : "    ") << "{\"uri\": \"" << esc(uri) << "\"}";
        textures << (imageCount ? ",\n    " : "    ") << "{\"sampler\": 0, \"source\": " << imageCount << "}";
        return imageCount++;
    };
    for (size_t i = 0; i < model.materials.size(); ++i) {
        const MaterialInfo& m = model.materials[i];
        mats << (i ? ",\n    " : "    ") << "{\"name\": \"" << esc(m.name) << "\", \"doubleSided\": " << (m.doubleSided ? "true" : "false");
        if (m.alphaMode == AlphaMode::Mask) mats << ", \"alphaMode\": \"MASK\", \"alphaCutoff\": " << num(m.alphaCutoff);
        else if (m.alphaMode == AlphaMode::Blend) mats << ", \"alphaMode\": \"BLEND\"";
        mats << ", \"pbrMetallicRoughness\": {\"metallicFactor\": 0";
        if (!m.baseColor.empty()) mats << ", \"baseColorTexture\": {\"index\": " << tex(m.baseColor) << "}";
        if (!m.arm.empty()) mats << ", \"metallicRoughnessTexture\": {\"index\": " << tex(m.arm) << "}";
        mats << "}";
        if (!m.normal.empty()) mats << ", \"normalTexture\": {\"index\": " << tex(m.normal) << "}";
        mats << "}";
    }

    std::ofstream j(path);
    std::ofstream b(binPath, std::ios::binary);
    if (!j || !b) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    b.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    j << "{\n  \"asset\": {\"version\": \"2.0\", \"generator\": \"ThermalSense bake_assets (v9 M6)\"},\n  \"scene\": 0,\n"
      << (quantized ? "  \"extensionsUsed\": [\"KHR_mesh_quantization\"],\n  \"extensionsRequired\": [\"KHR_mesh_quantization\"],\n" : "")
      << "  \"scenes\": [{\"nodes\": [";
    for (int i = 0; i < meshCount; ++i) j << (i ? ", " : "") << i;
    j << "]}],\n  \"nodes\": [\n" << nodes.str() << "\n  ],\n  \"meshes\": [\n" << meshes.str() << "\n  ],\n  \"materials\": [\n"
      << mats.str() << "\n  ],\n";
    if (imageCount) {
        j << "  \"samplers\": [{\"magFilter\": 9729, \"minFilter\": 9987}],\n  \"images\": [\n" << images.str()
          << "\n  ],\n  \"textures\": [\n" << textures.str() << "\n  ],\n";
    }
    j << "  \"accessors\": [\n" << accs.str() << "\n  ],\n  \"bufferViews\": [\n" << views.str() << "\n  ],\n"
      << "  \"buffers\": [{\"uri\": \"" << esc(binName) << "\", \"byteLength\": " << bin.size() << "}]\n}\n";
    return true;
}

}  // namespace ts::asset
