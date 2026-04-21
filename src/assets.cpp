#include "assets.h"
#include "math.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstring>
#include <limits>
#include <algorithm>
#include <functional>
#include <cctype>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef _WIN32
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#endif

using json = nlohmann::json;

constexpr float kArenaWorldRadius = 150.0f;

#ifdef _WIN32
struct GdiPlusContext {
    ULONG_PTR token = 0;
    bool initialized = false;

    bool startup() {
        if (initialized) {
            return true;
        }
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
            return false;
        }
        initialized = true;
        return true;
    }

    void shutdown() {
        if (initialized) {
            Gdiplus::GdiplusShutdown(token);
            token = 0;
            initialized = false;
        }
    }
};
#endif

bool contains_insensitive(const std::string& text, const std::string& needle) {
    if (needle.empty() || text.size() < needle.size()) {
        return false;
    }

    for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + j])));
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[j])));
            if (a != b) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }

    return false;
}

std::vector<unsigned char> read_binary_file(const std::filesystem::path& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        return {};
    }
    file.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(size);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return data;
}

template <typename T>
T read_scalar(const std::vector<unsigned char>& data, size_t offset) {
    T value{};
    if (offset + sizeof(T) > data.size()) {
        return value;
    }
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

Mat4 mat4_from_ai(const aiMatrix4x4& in) {
    Mat4 out = {};
    out.m[0] = in.a1; out.m[4] = in.a2; out.m[8] = in.a3; out.m[12] = in.a4;
    out.m[1] = in.b1; out.m[5] = in.b2; out.m[9] = in.b3; out.m[13] = in.b4;
    out.m[2] = in.c1; out.m[6] = in.c2; out.m[10] = in.c3; out.m[14] = in.c4;
    out.m[3] = in.d1; out.m[7] = in.d2; out.m[11] = in.d3; out.m[15] = in.d4;
    return out;
}

Mat4 ai_inverse_to_mat4(aiMatrix4x4 matrix) {
    matrix.Inverse();
    return mat4_from_ai(matrix);
}

std::array<float, 4> normalize_quat(const std::array<float, 4>& q) {
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len < 1e-6f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    return {q[0] / len, q[1] / len, q[2] / len, q[3] / len};
}

std::array<float, 4> slerp_quat(const std::array<float, 4>& a, const std::array<float, 4>& b, float t) {
    std::array<float, 4> end = b;
    float dot = a[0] * end[0] + a[1] * end[1] + a[2] * end[2] + a[3] * end[3];
    if (dot < 0.0f) {
        dot = -dot;
        end = {-end[0], -end[1], -end[2], -end[3]};
    }

    if (dot > 0.9995f) {
        std::array<float, 4> out{
            a[0] + t * (end[0] - a[0]),
            a[1] + t * (end[1] - a[1]),
            a[2] + t * (end[2] - a[2]),
            a[3] + t * (end[3] - a[3])
        };
        return normalize_quat(out);
    }

    const float theta_0 = std::acos(clampf(dot, -1.0f, 1.0f));
    const float theta = theta_0 * t;
    const float sin_theta = std::sin(theta);
    const float sin_theta_0 = std::sin(theta_0);
    const float s0 = std::cos(theta) - dot * sin_theta / sin_theta_0;
    const float s1 = sin_theta / sin_theta_0;
    return {
        s0 * a[0] + s1 * end[0],
        s0 * a[1] + s1 * end[1],
        s0 * a[2] + s1 * end[2],
        s0 * a[3] + s1 * end[3]
    };
}

Vec3 lerp_vec3(const Vec3& a, const Vec3& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

Vec3 sample_vec3_keys(const std::vector<AnimatedModel::KeyVec3>& keys, double time_seconds) {
    if (keys.empty()) {
        return {};
    }
    if (keys.size() == 1) {
        return keys.front().value;
    }

    if (time_seconds <= keys.front().time) {
        return keys.front().value;
    }
    if (time_seconds >= keys.back().time) {
        return keys.back().value;
    }

    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time_seconds < keys[i + 1].time) {
            const double span = std::max(1e-6, keys[i + 1].time - keys[i].time);
            const float t = static_cast<float>((time_seconds - keys[i].time) / span);
            return lerp_vec3(keys[i].value, keys[i + 1].value, t);
        }
    }

    return keys.back().value;
}

std::array<float, 4> sample_quat_keys(const std::vector<AnimatedModel::KeyQuat>& keys, double time_seconds) {
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (keys.size() == 1) {
        return normalize_quat(keys.front().value);
    }

    if (time_seconds <= keys.front().time) {
        return normalize_quat(keys.front().value);
    }
    if (time_seconds >= keys.back().time) {
        return normalize_quat(keys.back().value);
    }

    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time_seconds < keys[i + 1].time) {
            const double span = std::max(1e-6, keys[i + 1].time - keys[i].time);
            const float t = static_cast<float>((time_seconds - keys[i].time) / span);
            return normalize_quat(slerp_quat(keys[i].value, keys[i + 1].value, t));
        }
    }

    return normalize_quat(keys.back().value);
}

Mat4 read_node_local_matrix(const json& node) {
    if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16) {
        Mat4 out = {};
        for (int i = 0; i < 16; ++i) {
            out.m[i] = node["matrix"][i].get<float>();
        }
        return out;
    }

    std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};

    if (node.contains("translation") && node["translation"].is_array() && node["translation"].size() >= 3) {
        translation[0] = node["translation"][0].get<float>();
        translation[1] = node["translation"][1].get<float>();
        translation[2] = node["translation"][2].get<float>();
    }

    if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() >= 4) {
        rotation[0] = node["rotation"][0].get<float>();
        rotation[1] = node["rotation"][1].get<float>();
        rotation[2] = node["rotation"][2].get<float>();
        rotation[3] = node["rotation"][3].get<float>();
    }

    if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() >= 3) {
        scale[0] = node["scale"][0].get<float>();
        scale[1] = node["scale"][1].get<float>();
        scale[2] = node["scale"][2].get<float>();
    }

    return compose_trs(translation, rotation, scale);
}

std::vector<std::filesystem::path> resource_roots() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    return {
        cwd / "resources",
        cwd.parent_path() / "resources",
        std::filesystem::path(APP_SOURCE_DIR) / "resources"
    };
}

std::filesystem::path find_resource(const std::string& relative_path) {
    for (const auto& root : resource_roots()) {
        const auto candidate = root / relative_path;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool load_arena_mesh(const std::filesystem::path& gltf_path, ArenaMesh& mesh, std::string& error) {
    std::ifstream gltf_file(gltf_path);
    if (!gltf_file) {
        error = "Failed to open glTF file: " + gltf_path.string();
        return false;
    }

    json gltf;
    try {
        gltf_file >> gltf;
    } catch (const std::exception& ex) {
        error = "Failed to parse glTF JSON: " + std::string(ex.what());
        return false;
    }

    if (!gltf.contains("buffers") || gltf["buffers"].empty()) {
        error = "glTF has no buffers";
        return false;
    }

    const auto bin_uri = gltf["buffers"][0].value("uri", "");
    const auto bin_path = gltf_path.parent_path() / bin_uri;
    const auto buffer_data = read_binary_file(bin_path);
    if (buffer_data.empty()) {
        error = "Failed to read buffer: " + bin_path.string();
        return false;
    }

    struct BufferView {
        size_t byte_offset = 0;
        size_t byte_length = 0;
        size_t byte_stride = 0;
    };

    std::vector<BufferView> views;
    if (!gltf.contains("bufferViews")) {
        error = "glTF has no bufferViews";
        return false;
    }

    for (const auto& view_json : gltf["bufferViews"]) {
        BufferView view;
        view.byte_offset = view_json.value("byteOffset", 0);
        view.byte_length = view_json.value("byteLength", 0);
        view.byte_stride = view_json.value("byteStride", 0);
        views.push_back(view);
    }

    if (!gltf.contains("accessors")) {
        error = "glTF has no accessors";
        return false;
    }

    std::vector<Vec3> all_positions;

    struct TempPrimitive {
        std::vector<Vec3> positions;
        std::vector<std::array<float, 2>> texcoords;
        std::vector<unsigned int> indices;
        int base_texture_slot = -1;
        int emissive_texture_slot = -1;
        bool alpha_blend = false;
        std::array<float, 4> base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 3> emissive_factor{1.0f, 1.0f, 1.0f};
    };
    std::vector<TempPrimitive> temp_primitives;

    auto read_positions = [&](int accessor_index, std::vector<Vec3>& out_positions) -> bool {
        if (accessor_index < 0 || accessor_index >= static_cast<int>(gltf["accessors"].size())) {
            return false;
        }

        const auto& accessor = gltf["accessors"][accessor_index];
        if (accessor.value("type", "") != "VEC3" || accessor.value("componentType", 0) != 5126) {
            return false;
        }

        const int view_index = accessor.value("bufferView", -1);
        if (view_index < 0 || view_index >= static_cast<int>(views.size())) {
            return false;
        }

        const auto& view = views[view_index];
        const size_t accessor_offset = accessor.value("byteOffset", 0);
        const size_t count = accessor.value("count", 0);
        const size_t stride = (view.byte_stride == 0) ? sizeof(float) * 3 : view.byte_stride;
        const size_t base = view.byte_offset + accessor_offset;

        out_positions.clear();
        out_positions.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            const size_t offset = base + i * stride;
            const float x = read_scalar<float>(buffer_data, offset + 0);
            const float y = read_scalar<float>(buffer_data, offset + 4);
            const float z = read_scalar<float>(buffer_data, offset + 8);
            out_positions.push_back({x, y, z});
        }

        return true;
    };

    auto read_texcoords = [&](int accessor_index, std::vector<std::array<float, 2>>& out_texcoords) -> bool {
        if (accessor_index < 0 || accessor_index >= static_cast<int>(gltf["accessors"].size())) {
            return false;
        }

        const auto& accessor = gltf["accessors"][accessor_index];
        if (accessor.value("type", "") != "VEC2" || accessor.value("componentType", 0) != 5126) {
            return false;
        }

        const int view_index = accessor.value("bufferView", -1);
        if (view_index < 0 || view_index >= static_cast<int>(views.size())) {
            return false;
        }

        const auto& view = views[view_index];
        const size_t accessor_offset = accessor.value("byteOffset", 0);
        const size_t count = accessor.value("count", 0);
        const size_t stride = (view.byte_stride == 0) ? sizeof(float) * 2 : view.byte_stride;
        const size_t base = view.byte_offset + accessor_offset;

        out_texcoords.clear();
        out_texcoords.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            const size_t offset = base + i * stride;
            const float u = read_scalar<float>(buffer_data, offset + 0);
            const float v = read_scalar<float>(buffer_data, offset + 4);
            out_texcoords.push_back({u, v});
        }

        return true;
    };

    auto read_indices = [&](int accessor_index, std::vector<unsigned int>& out_indices) -> bool {
        if (accessor_index < 0 || accessor_index >= static_cast<int>(gltf["accessors"].size())) {
            return false;
        }

        const auto& accessor = gltf["accessors"][accessor_index];
        const int view_index = accessor.value("bufferView", -1);
        if (view_index < 0 || view_index >= static_cast<int>(views.size())) {
            return false;
        }

        const auto& view = views[view_index];
        const size_t accessor_offset = accessor.value("byteOffset", 0);
        const size_t count = accessor.value("count", 0);
        const int component_type = accessor.value("componentType", 0);

        size_t component_size = 0;
        switch (component_type) {
            case 5121: component_size = 1; break; // uint8
            case 5123: component_size = 2; break; // uint16
            case 5125: component_size = 4; break; // uint32
            default: return false;
        }

        const size_t stride = (view.byte_stride == 0) ? component_size : view.byte_stride;
        const size_t base = view.byte_offset + accessor_offset;

        out_indices.clear();
        out_indices.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            const size_t offset = base + i * stride;
            unsigned int value = 0;
            if (component_type == 5121) {
                value = read_scalar<unsigned char>(buffer_data, offset);
            } else if (component_type == 5123) {
                value = read_scalar<unsigned short>(buffer_data, offset);
            } else {
                value = read_scalar<unsigned int>(buffer_data, offset);
            }
            out_indices.push_back(value);
        }

        return true;
    };

    struct MaterialInfo {
        int base_texture_slot = -1;
        int emissive_texture_slot = -1;
        bool alpha_blend = false;
        std::array<float, 4> base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 3> emissive_factor{1.0f, 1.0f, 1.0f};
    };
    std::vector<MaterialInfo> materials;

    auto texture_to_image_slot = [&](int texture_index) -> int {
        if (!gltf.contains("textures") || texture_index < 0 || texture_index >= static_cast<int>(gltf["textures"].size())) {
            return -1;
        }
        const int image_source = gltf["textures"][texture_index].value("source", -1);
        if (image_source < 0 || image_source >= static_cast<int>(mesh.textures.size())) {
            return -1;
        }
        return image_source;
    };

    if (gltf.contains("images") && gltf["images"].is_array()) {
        mesh.textures.resize(gltf["images"].size());
        for (size_t i = 0; i < gltf["images"].size(); ++i) {
            const std::string uri = gltf["images"][i].value("uri", "");
            if (!uri.empty()) {
                mesh.textures[i].image_path = gltf_path.parent_path() / uri;
            }
        }
    }

    if (gltf.contains("materials") && gltf["materials"].is_array()) {
        materials.resize(gltf["materials"].size());
        for (size_t m = 0; m < gltf["materials"].size(); ++m) {
            const auto& material = gltf["materials"][m];
            auto& mat = materials[m];

            if (material.value("alphaMode", std::string("OPAQUE")) == "BLEND") {
                mat.alpha_blend = true;
            }

            if (material.contains("emissiveFactor") && material["emissiveFactor"].is_array() && material["emissiveFactor"].size() >= 3) {
                mat.emissive_factor[0] = material["emissiveFactor"][0].get<float>();
                mat.emissive_factor[1] = material["emissiveFactor"][1].get<float>();
                mat.emissive_factor[2] = material["emissiveFactor"][2].get<float>();
            }

            if (material.contains("emissiveTexture")) {
                const int emissive_texture = material["emissiveTexture"].value("index", -1);
                mat.emissive_texture_slot = texture_to_image_slot(emissive_texture);
            }

            if (!material.contains("pbrMetallicRoughness")) {
                continue;
            }
            const auto& pbr = material["pbrMetallicRoughness"];

            if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array() && pbr["baseColorFactor"].size() >= 4) {
                mat.base_color_factor[0] = pbr["baseColorFactor"][0].get<float>();
                mat.base_color_factor[1] = pbr["baseColorFactor"][1].get<float>();
                mat.base_color_factor[2] = pbr["baseColorFactor"][2].get<float>();
                mat.base_color_factor[3] = pbr["baseColorFactor"][3].get<float>();
            }

            if (!pbr.contains("baseColorTexture")) {
                continue;
            }

            const int tex_index = pbr["baseColorTexture"].value("index", -1);
            mat.base_texture_slot = texture_to_image_slot(tex_index);
        }
    }

    if (!gltf.contains("meshes") || gltf["meshes"].empty()) {
        error = "glTF has no meshes";
        return false;
    }

    auto process_mesh = [&](int mesh_index, const Mat4& world_matrix) {
        if (mesh_index < 0 || mesh_index >= static_cast<int>(gltf["meshes"].size())) {
            return;
        }

        const auto& gltf_mesh = gltf["meshes"][mesh_index];
        const std::string mesh_name = gltf_mesh.value("name", "");
        // Imported scenes commonly include a giant sky sphere that should not be part of gameplay bounds.
        if (contains_insensitive(mesh_name, "sky") || contains_insensitive(mesh_name, "sphere_sky")) {
            return;
        }

        if (!gltf_mesh.contains("primitives")) {
            return;
        }

        for (const auto& primitive : gltf_mesh["primitives"]) {
            if (!primitive.contains("attributes") || !primitive["attributes"].contains("POSITION")) {
                continue;
            }

            const int position_accessor = primitive["attributes"].value("POSITION", -1);
            std::vector<Vec3> primitive_vertices;
            if (!read_positions(position_accessor, primitive_vertices) || primitive_vertices.empty()) {
                continue;
            }

            for (auto& vertex : primitive_vertices) {
                vertex = transform_point(world_matrix, vertex);
            }

            TempPrimitive temp;
            temp.positions = std::move(primitive_vertices);

            if (primitive.contains("attributes") && primitive["attributes"].contains("TEXCOORD_0")) {
                const int uv_accessor = primitive["attributes"].value("TEXCOORD_0", -1);
                std::vector<std::array<float, 2>> uvs;
                if (read_texcoords(uv_accessor, uvs) && uvs.size() == temp.positions.size()) {
                    temp.texcoords = std::move(uvs);
                }
            }

            std::vector<unsigned int> primitive_indices;
            if (primitive.contains("indices")) {
                const int index_accessor = primitive.value("indices", -1);
                if (!read_indices(index_accessor, primitive_indices)) {
                    continue;
                }
            } else {
                primitive_indices.resize(primitive_vertices.size());
                for (unsigned int i = 0; i < static_cast<unsigned int>(primitive_vertices.size()); ++i) {
                    primitive_indices[i] = i;
                }
            }

            if (primitive.contains("material") && !materials.empty()) {
                const int material_index = primitive.value("material", -1);
                if (material_index >= 0 && material_index < static_cast<int>(materials.size())) {
                    const auto& mat = materials[material_index];
                    temp.base_texture_slot = mat.base_texture_slot;
                    temp.emissive_texture_slot = mat.emissive_texture_slot;
                    temp.alpha_blend = mat.alpha_blend;
                    temp.base_color_factor = mat.base_color_factor;
                    temp.emissive_factor = mat.emissive_factor;
                }
            }

            temp.indices = std::move(primitive_indices);
            all_positions.insert(all_positions.end(), temp.positions.begin(), temp.positions.end());
            temp_primitives.push_back(std::move(temp));
        }
    };

    if (gltf.contains("nodes") && gltf["nodes"].is_array() && !gltf["nodes"].empty()) {
        std::function<void(int, const Mat4&)> visit_node;
        visit_node = [&](int node_index, const Mat4& parent_matrix) {
            if (node_index < 0 || node_index >= static_cast<int>(gltf["nodes"].size())) {
                return;
            }

            const auto& node = gltf["nodes"][node_index];
            const Mat4 local = read_node_local_matrix(node);
            const Mat4 world = mul_mat4(parent_matrix, local);

            if (node.contains("mesh")) {
                process_mesh(node.value("mesh", -1), world);
            }

            if (node.contains("children") && node["children"].is_array()) {
                for (const auto& child : node["children"]) {
                    visit_node(child.get<int>(), world);
                }
            }
        };

        bool traversed_scene_nodes = false;
        if (gltf.contains("scenes") && gltf["scenes"].is_array() && !gltf["scenes"].empty()) {
            int scene_index = gltf.value("scene", 0);
            if (scene_index < 0 || scene_index >= static_cast<int>(gltf["scenes"].size())) {
                scene_index = 0;
            }
            const auto& scene = gltf["scenes"][scene_index];
            if (scene.contains("nodes") && scene["nodes"].is_array()) {
                const Mat4 identity = identity_mat4();
                for (const auto& root_node : scene["nodes"]) {
                    visit_node(root_node.get<int>(), identity);
                    traversed_scene_nodes = true;
                }
            }
        }

        if (!traversed_scene_nodes) {
            const Mat4 identity = identity_mat4();
            for (int i = 0; i < static_cast<int>(gltf["nodes"].size()); ++i) {
                visit_node(i, identity);
            }
        }
    } else {
        const Mat4 identity = identity_mat4();
        for (int mesh_index = 0; mesh_index < static_cast<int>(gltf["meshes"].size()); ++mesh_index) {
            process_mesh(mesh_index, identity);
        }
    }

    if (all_positions.empty() || temp_primitives.empty()) {
        error = "No renderable geometry found in arena glTF";
        return false;
    }

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();

    for (const auto& v : all_positions) {
        min_x = std::min(min_x, v.x);
        min_y = std::min(min_y, v.y);
        min_z = std::min(min_z, v.z);
        max_x = std::max(max_x, v.x);
        max_z = std::max(max_z, v.z);
    }

    const float center_x = 0.5f * (min_x + max_x);
    const float center_z = 0.5f * (min_z + max_z);
    const float half_x = 0.5f * (max_x - min_x);
    const float half_z = 0.5f * (max_z - min_z);
    const float raw_radius = std::max(half_x, half_z);
    const float scale = (raw_radius > 1e-5f) ? (kArenaWorldRadius / raw_radius) : 1.0f;

    mesh.primitives.clear();
    mesh.primitives.reserve(temp_primitives.size());

    for (auto& temp : temp_primitives) {
        ArenaMesh::Primitive primitive;
        primitive.base_texture_slot = temp.base_texture_slot;
        primitive.emissive_texture_slot = temp.emissive_texture_slot;
        primitive.alpha_blend = temp.alpha_blend;
        primitive.base_color_factor = temp.base_color_factor;
        primitive.emissive_factor = temp.emissive_factor;
        primitive.indices = std::move(temp.indices);

        primitive.vertex_data.reserve(temp.positions.size() * 5);
        for (size_t i = 0; i < temp.positions.size(); ++i) {
            const auto& p = temp.positions[i];
            const float px = (p.x - center_x) * scale;
            const float py = (p.y - min_y) * scale;
            const float pz = (p.z - center_z) * scale;

            float u = 0.0f;
            float v = 0.0f;
            if (!temp.texcoords.empty() && i < temp.texcoords.size()) {
                u = temp.texcoords[i][0];
                v = 1.0f - temp.texcoords[i][1];
            }

            primitive.vertex_data.push_back(px);
            primitive.vertex_data.push_back(py);
            primitive.vertex_data.push_back(pz);
            primitive.vertex_data.push_back(u);
            primitive.vertex_data.push_back(v);
        }

        mesh.primitives.push_back(std::move(primitive));
    }

    mesh.radius = raw_radius * scale;
    return true;
}

bool upload_arena_textures(ArenaMesh& mesh, std::string& error) {
    bool any_loaded = false;

    stbi_set_flip_vertically_on_load(1);

    for (auto& texture : mesh.textures) {
        int width = 0;
        int height = 0;
        unsigned char* decoded_pixels = nullptr;
        const unsigned char* pixels = nullptr;

        if (!texture.embedded_rgba.empty() && texture.embedded_width > 0 && texture.embedded_height > 0) {
            width = texture.embedded_width;
            height = texture.embedded_height;
            pixels = texture.embedded_rgba.data();
        } else if (!texture.image_path.empty()) {
            int channels = 0;
            decoded_pixels = stbi_load(texture.image_path.string().c_str(), &width, &height, &channels, 4);
            if (decoded_pixels == nullptr || width <= 0 || height <= 0) {
                std::cerr << "Texture load warning: " << texture.image_path.string() << " (stb_image failed)\n";
                if (decoded_pixels != nullptr) {
                    stbi_image_free(decoded_pixels);
                }
                continue;
            }
            pixels = decoded_pixels;
        } else {
            continue;
        }

        glGenTextures(1, &texture.gl_id);
        glBindTexture(GL_TEXTURE_2D, texture.gl_id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        if (decoded_pixels != nullptr) {
            stbi_image_free(decoded_pixels);
        }

        any_loaded = true;
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    if (!any_loaded) {
        error = "No texture files loaded successfully";
    }
    return any_loaded;
}

void free_arena_textures(ArenaMesh& mesh) {
    for (auto& texture : mesh.textures) {
        if (texture.gl_id != 0) {
            glDeleteTextures(1, &texture.gl_id);
            texture.gl_id = 0;
        }
    }
}

bool load_static_model_mesh(const std::filesystem::path& model_path, ArenaMesh& mesh, std::string& error) {
    Assimp::Importer importer;
    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs;

    const aiScene* scene = importer.ReadFile(model_path.string(), flags);
    if (scene == nullptr || scene->mRootNode == nullptr) {
        error = "Failed to import model: " + model_path.string();
        const char* assimp_error = importer.GetErrorString();
        if (assimp_error != nullptr && assimp_error[0] != '\0') {
            error += " (" + std::string(assimp_error) + ")";
        }
        return false;
    }

    mesh.primitives.clear();
    mesh.textures.clear();

    struct TempPrimitive {
        std::vector<Vec3> positions;
        std::vector<std::array<float, 2>> texcoords;
        std::vector<unsigned int> indices;
        int base_texture_slot = -1;
        int emissive_texture_slot = -1;
        bool alpha_blend = false;
        std::array<float, 4> base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 3> emissive_factor{0.0f, 0.0f, 0.0f};
    };

    std::vector<TempPrimitive> temp_primitives;
    std::vector<Vec3> all_positions;
    std::unordered_map<std::string, int> texture_slot_by_key;

    auto as_mat4 = [](const aiMatrix4x4& in) {
        Mat4 out = {};
        out.m[0] = in.a1; out.m[4] = in.a2; out.m[8] = in.a3; out.m[12] = in.a4;
        out.m[1] = in.b1; out.m[5] = in.b2; out.m[9] = in.b3; out.m[13] = in.b4;
        out.m[2] = in.c1; out.m[6] = in.c2; out.m[10] = in.c3; out.m[14] = in.c4;
        out.m[3] = in.d1; out.m[7] = in.d2; out.m[11] = in.d3; out.m[15] = in.d4;
        return out;
    };

    auto get_material_texture = [&](unsigned int material_index, aiTextureType texture_type) -> int {
        if (material_index >= scene->mNumMaterials) {
            return -1;
        }

        const aiMaterial* material = scene->mMaterials[material_index];
        aiString rel_path;
        if (material->GetTextureCount(texture_type) == 0 || material->GetTexture(texture_type, 0, &rel_path) != aiReturn_SUCCESS) {
            return -1;
        }

        const std::string rel = rel_path.C_Str();
        if (rel.empty()) {
            return -1;
        }

        const std::string key = (rel[0] == '*')
            ? rel
            : (model_path.parent_path() / rel).lexically_normal().string();

        const auto existing_it = texture_slot_by_key.find(key);
        if (existing_it != texture_slot_by_key.end()) {
            return existing_it->second;
        }

        ArenaMesh::Texture texture;
        if (rel[0] == '*') {
            const aiTexture* embedded = scene->GetEmbeddedTexture(rel.c_str());
            if (embedded == nullptr || embedded->pcData == nullptr) {
                return -1;
            }

            if (embedded->mHeight == 0) {
                int width = 0;
                int height = 0;
                int channels = 0;
                const unsigned char* encoded_data = reinterpret_cast<const unsigned char*>(embedded->pcData);
                unsigned char* decoded_pixels = stbi_load_from_memory(
                    encoded_data,
                    static_cast<int>(embedded->mWidth),
                    &width,
                    &height,
                    &channels,
                    4
                );
                if (decoded_pixels == nullptr || width <= 0 || height <= 0) {
                    if (decoded_pixels != nullptr) {
                        stbi_image_free(decoded_pixels);
                    }
                    return -1;
                }

                texture.embedded_width = width;
                texture.embedded_height = height;
                texture.embedded_rgba.assign(
                    decoded_pixels,
                    decoded_pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4
                );
                stbi_image_free(decoded_pixels);
            } else {
                const int width = static_cast<int>(embedded->mWidth);
                const int height = static_cast<int>(embedded->mHeight);
                if (width <= 0 || height <= 0) {
                    return -1;
                }

                texture.embedded_width = width;
                texture.embedded_height = height;
                texture.embedded_rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
                for (int i = 0; i < width * height; ++i) {
                    const aiTexel& texel = embedded->pcData[i];
                    texture.embedded_rgba[static_cast<size_t>(i) * 4 + 0] = texel.r;
                    texture.embedded_rgba[static_cast<size_t>(i) * 4 + 1] = texel.g;
                    texture.embedded_rgba[static_cast<size_t>(i) * 4 + 2] = texel.b;
                    texture.embedded_rgba[static_cast<size_t>(i) * 4 + 3] = texel.a;
                }
            }
        } else {
            texture.image_path = (model_path.parent_path() / rel).lexically_normal();
        }

        mesh.textures.push_back(std::move(texture));
        const int slot = static_cast<int>(mesh.textures.size() - 1);
        texture_slot_by_key.emplace(key, slot);
        return slot;
    };

    std::function<void(const aiNode*, const aiMatrix4x4&)> visit_node;
    visit_node = [&](const aiNode* node, const aiMatrix4x4& parent_transform) {
        if (node == nullptr) {
            return;
        }

        const aiMatrix4x4 world_transform = parent_transform * node->mTransformation;
        const Mat4 world = as_mat4(world_transform);

        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            const aiMesh* src_mesh = scene->mMeshes[node->mMeshes[i]];
            if (src_mesh == nullptr || src_mesh->mNumVertices == 0) {
                continue;
            }

            TempPrimitive temp;
            temp.positions.reserve(src_mesh->mNumVertices);
            temp.texcoords.reserve(src_mesh->mNumVertices);

            for (unsigned int v = 0; v < src_mesh->mNumVertices; ++v) {
                const aiVector3D& p = src_mesh->mVertices[v];
                const Vec3 world_pos = transform_point(world, {p.x, p.y, p.z});
                temp.positions.push_back(world_pos);
                all_positions.push_back(world_pos);

                if (src_mesh->HasTextureCoords(0)) {
                    const aiVector3D& uv = src_mesh->mTextureCoords[0][v];
                    temp.texcoords.push_back({uv.x, uv.y});
                }
            }

            for (unsigned int f = 0; f < src_mesh->mNumFaces; ++f) {
                const aiFace& face = src_mesh->mFaces[f];
                if (face.mNumIndices != 3) {
                    continue;
                }
                temp.indices.push_back(face.mIndices[0]);
                temp.indices.push_back(face.mIndices[1]);
                temp.indices.push_back(face.mIndices[2]);
            }

            if (src_mesh->mMaterialIndex < scene->mNumMaterials) {
                const aiMaterial* material = scene->mMaterials[src_mesh->mMaterialIndex];
                if (material != nullptr) {
                    aiColor4D diffuse(1.0f, 1.0f, 1.0f, 1.0f);
                    if (aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse) == aiReturn_SUCCESS) {
                        temp.base_color_factor = {diffuse.r, diffuse.g, diffuse.b, diffuse.a};
                    }

                    aiColor3D emissive(0.0f, 0.0f, 0.0f);
                    if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == aiReturn_SUCCESS) {
                        temp.emissive_factor = {emissive.r, emissive.g, emissive.b};
                    }

                    float opacity = 1.0f;
                    if (material->Get(AI_MATKEY_OPACITY, opacity) == aiReturn_SUCCESS && opacity < 0.999f) {
                        temp.alpha_blend = true;
                        temp.base_color_factor[3] = std::min(temp.base_color_factor[3], opacity);
                    }

                    temp.base_texture_slot = get_material_texture(src_mesh->mMaterialIndex, aiTextureType_DIFFUSE);
                    temp.emissive_texture_slot = get_material_texture(src_mesh->mMaterialIndex, aiTextureType_EMISSIVE);
                }
            }

            if (!temp.positions.empty() && !temp.indices.empty()) {
                temp_primitives.push_back(std::move(temp));
            }
        }

        for (unsigned int c = 0; c < node->mNumChildren; ++c) {
            visit_node(node->mChildren[c], world_transform);
        }
    };

    const aiMatrix4x4 root_identity(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
    visit_node(scene->mRootNode, root_identity);

    if (temp_primitives.empty() || all_positions.empty()) {
        error = "No renderable geometry found in model: " + model_path.string();
        return false;
    }

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();

    for (const auto& p : all_positions) {
        min_x = std::min(min_x, p.x);
        min_y = std::min(min_y, p.y);
        min_z = std::min(min_z, p.z);
        max_x = std::max(max_x, p.x);
        max_z = std::max(max_z, p.z);
    }

    const float center_x = 0.5f * (min_x + max_x);
    const float center_z = 0.5f * (min_z + max_z);
    const float half_x = 0.5f * (max_x - min_x);
    const float half_z = 0.5f * (max_z - min_z);
    const float raw_radius = std::max(half_x, half_z);
    const float scale = (raw_radius > 1e-5f) ? (1.0f / raw_radius) : 1.0f;

    mesh.primitives.reserve(temp_primitives.size());
    for (auto& temp : temp_primitives) {
        ArenaMesh::Primitive primitive;
        primitive.base_texture_slot = temp.base_texture_slot;
        primitive.emissive_texture_slot = temp.emissive_texture_slot;
        primitive.alpha_blend = temp.alpha_blend;
        primitive.base_color_factor = temp.base_color_factor;
        primitive.emissive_factor = temp.emissive_factor;
        primitive.indices = std::move(temp.indices);
        primitive.vertex_data.reserve(temp.positions.size() * 5);

        for (size_t i = 0; i < temp.positions.size(); ++i) {
            const Vec3& p = temp.positions[i];
            const float x = (p.x - center_x) * scale;
            const float y = (p.y - min_y) * scale;
            const float z = (p.z - center_z) * scale;

            float u = 0.0f;
            float v = 0.0f;
            if (!temp.texcoords.empty() && i < temp.texcoords.size()) {
                u = temp.texcoords[i][0];
                v = 1.0f - temp.texcoords[i][1];
            }

            primitive.vertex_data.push_back(x);
            primitive.vertex_data.push_back(y);
            primitive.vertex_data.push_back(z);
            primitive.vertex_data.push_back(u);
            primitive.vertex_data.push_back(v);
        }

        mesh.primitives.push_back(std::move(primitive));
    }

    mesh.radius = 1.0f;
    return true;
}

namespace {

void add_bone_influence(std::array<int, 4>& ids, std::array<float, 4>& weights, int bone_index, float weight) {
    for (int i = 0; i < 4; ++i) {
        if (weights[i] == 0.0f) {
            ids[i] = bone_index;
            weights[i] = weight;
            return;
        }
    }

    int smallest_index = 0;
    for (int i = 1; i < 4; ++i) {
        if (weights[i] < weights[smallest_index]) {
            smallest_index = i;
        }
    }
    if (weight > weights[smallest_index]) {
        ids[smallest_index] = bone_index;
        weights[smallest_index] = weight;
    }
}

int copy_nodes_recursive(const aiNode* node, AnimatedModel& model) {
    if (node == nullptr) {
        return -1;
    }

    AnimatedModel::Node current;
    current.name = node->mName.C_Str();
    current.local_transform = mat4_from_ai(node->mTransformation);

    const int current_index = static_cast<int>(model.nodes.size());
    model.node_lookup[current.name] = current_index;
    model.nodes.push_back(current);

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        const int child_index = copy_nodes_recursive(node->mChildren[i], model);
        if (child_index >= 0) {
            model.nodes[current_index].children.push_back(child_index);
        }
    }

    return current_index;
}

void extract_track_keys(
    const aiNodeAnim* channel,
    std::vector<AnimatedModel::KeyVec3>& positions,
    std::vector<AnimatedModel::KeyQuat>& rotations,
    std::vector<AnimatedModel::KeyVec3>& scales
) {
    if (channel == nullptr) {
        return;
    }

    positions.reserve(channel->mNumPositionKeys);
    for (unsigned int i = 0; i < channel->mNumPositionKeys; ++i) {
        const auto& key = channel->mPositionKeys[i];
        positions.push_back({static_cast<double>(key.mTime), {key.mValue.x, key.mValue.y, key.mValue.z}});
    }

    rotations.reserve(channel->mNumRotationKeys);
    for (unsigned int i = 0; i < channel->mNumRotationKeys; ++i) {
        const auto& key = channel->mRotationKeys[i];
        rotations.push_back({static_cast<double>(key.mTime), {key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
    }

    scales.reserve(channel->mNumScalingKeys);
    for (unsigned int i = 0; i < channel->mNumScalingKeys; ++i) {
        const auto& key = channel->mScalingKeys[i];
        scales.push_back({static_cast<double>(key.mTime), {key.mValue.x, key.mValue.y, key.mValue.z}});
    }

}

} // namespace

bool load_animated_model(const std::filesystem::path& model_path, AnimatedModel& model, std::string& error) {
    Assimp::Importer importer;
    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs;

    const aiScene* scene = importer.ReadFile(model_path.string(), flags);
    if (scene == nullptr || scene->mRootNode == nullptr) {
        error = "Failed to import animated model: " + model_path.string();
        const char* assimp_error = importer.GetErrorString();
        if (assimp_error != nullptr && assimp_error[0] != '\0') {
            error += " (" + std::string(assimp_error) + ")";
        }
        return false;
    }

    model = AnimatedModel{};
    model.global_inverse = ai_inverse_to_mat4(scene->mRootNode->mTransformation);

    if (scene->mAnimations != nullptr && scene->mNumAnimations > 0) {
        for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
            const aiAnimation* anim = scene->mAnimations[i];
            if (anim == nullptr) {
                continue;
            }

            AnimatedModel::Clip clip;
            clip.name = anim->mName.C_Str();
            clip.duration = static_cast<double>(anim->mDuration);
            clip.ticks_per_second = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;

            for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
                const aiNodeAnim* channel = anim->mChannels[c];
                if (channel == nullptr) {
                    continue;
                }

                AnimatedModel::Channel out_channel;
                out_channel.node_name = channel->mNodeName.C_Str();
                extract_track_keys(channel, out_channel.position_keys, out_channel.rotation_keys, out_channel.scale_keys);
                clip.channels.push_back(std::move(out_channel));
            }

            model.clips.push_back(std::move(clip));
        }
    }

    std::vector<Vec3> all_positions;

    struct TempPrimitive {
        std::vector<Vec3> positions;
        std::vector<std::array<float, 2>> texcoords;
        std::vector<std::array<int, 4>> bone_ids;
        std::vector<std::array<float, 4>> bone_weights;
        std::vector<unsigned int> indices;
        int base_texture_slot = -1;
        std::array<float, 4> base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
    };
    std::vector<TempPrimitive> temp_primitives;

    auto get_or_add_bone = [&](const std::string& bone_name, const aiMatrix4x4& offset_matrix) -> int {
        const auto found = model.bone_lookup.find(bone_name);
        if (found != model.bone_lookup.end()) {
            return found->second;
        }

        AnimatedModel::Bone bone;
        bone.name = bone_name;
        bone.offset = mat4_from_ai(offset_matrix);
        const auto node_it = model.node_lookup.find(bone_name);
        if (node_it != model.node_lookup.end()) {
            bone.node_index = node_it->second;
        }

        const int bone_index = static_cast<int>(model.bones.size());
        model.bones.push_back(bone);
        model.bone_lookup[bone_name] = bone_index;
        return bone_index;
    };

    if (scene->mRootNode != nullptr) {
        copy_nodes_recursive(scene->mRootNode, model);
    }

    if (scene->mMeshes == nullptr || scene->mNumMeshes == 0) {
        error = "Animated model has no meshes: " + model_path.string();
        return false;
    }

    for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index) {
        const aiMesh* src_mesh = scene->mMeshes[mesh_index];
        if (src_mesh == nullptr || src_mesh->mNumVertices == 0) {
            continue;
        }

        TempPrimitive temp;
        temp.positions.reserve(src_mesh->mNumVertices);
        temp.texcoords.reserve(src_mesh->mNumVertices);
        temp.bone_ids.resize(src_mesh->mNumVertices, {-1, -1, -1, -1});
        temp.bone_weights.resize(src_mesh->mNumVertices, {0.0f, 0.0f, 0.0f, 0.0f});

        for (unsigned int v = 0; v < src_mesh->mNumVertices; ++v) {
            const aiVector3D& p = src_mesh->mVertices[v];
            temp.positions.push_back({p.x, p.y, p.z});
            all_positions.push_back({p.x, p.y, p.z});

            if (src_mesh->HasTextureCoords(0)) {
                const aiVector3D& uv = src_mesh->mTextureCoords[0][v];
                temp.texcoords.push_back({uv.x, uv.y});
            }
        }

        for (unsigned int f = 0; f < src_mesh->mNumFaces; ++f) {
            const aiFace& face = src_mesh->mFaces[f];
            if (face.mNumIndices != 3) {
                continue;
            }
            temp.indices.push_back(face.mIndices[0]);
            temp.indices.push_back(face.mIndices[1]);
            temp.indices.push_back(face.mIndices[2]);
        }

        for (unsigned int b = 0; b < src_mesh->mNumBones; ++b) {
            const aiBone* src_bone = src_mesh->mBones[b];
            if (src_bone == nullptr) {
                continue;
            }

            const int bone_index = get_or_add_bone(src_bone->mName.C_Str(), src_bone->mOffsetMatrix);
            for (unsigned int w = 0; w < src_bone->mNumWeights; ++w) {
                const aiVertexWeight& weight = src_bone->mWeights[w];
                if (weight.mVertexId >= temp.bone_ids.size()) {
                    continue;
                }
                add_bone_influence(temp.bone_ids[weight.mVertexId], temp.bone_weights[weight.mVertexId], bone_index, weight.mWeight);
            }
        }

        if (src_mesh->mMaterialIndex < scene->mNumMaterials) {
            const aiMaterial* material = scene->mMaterials[src_mesh->mMaterialIndex];
            if (material != nullptr) {
                aiColor4D diffuse(1.0f, 1.0f, 1.0f, 1.0f);
                if (aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse) == aiReturn_SUCCESS) {
                    temp.base_color_factor = {diffuse.r, diffuse.g, diffuse.b, diffuse.a};
                }
                if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
                    aiString rel_path;
                    if (material->GetTexture(aiTextureType_DIFFUSE, 0, &rel_path) == aiReturn_SUCCESS && rel_path.length > 0 && rel_path.C_Str()[0] != '*') {
                        ArenaMesh::Texture texture;
                        texture.image_path = model_path.parent_path() / rel_path.C_Str();
                        model.mesh.textures.push_back(texture);
                        temp.base_texture_slot = static_cast<int>(model.mesh.textures.size() - 1);
                    }
                }
            }
        }

        temp_primitives.push_back(std::move(temp));
    }

    if (all_positions.empty() || temp_primitives.empty()) {
        error = "No renderable geometry found in animated model: " + model_path.string();
        return false;
    }

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();

    for (const auto& v : all_positions) {
        min_x = std::min(min_x, v.x);
        min_y = std::min(min_y, v.y);
        min_z = std::min(min_z, v.z);
        max_x = std::max(max_x, v.x);
        max_z = std::max(max_z, v.z);
    }

    model.bounds_center = {0.5f * (min_x + max_x), 0.0f, 0.5f * (min_z + max_z)};
    model.bounds_min_y = min_y;
    const float half_x = 0.5f * (max_x - min_x);
    const float half_z = 0.5f * (max_z - min_z);
    const float raw_radius = std::max(half_x, half_z);
    model.bounds_scale = (raw_radius > 1e-5f) ? (1.0f / raw_radius) : 1.0f;
    model.normalization = compose_trs(
        {-model.bounds_center[0], -model.bounds_min_y, -model.bounds_center[2]},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {model.bounds_scale, model.bounds_scale, model.bounds_scale}
    );

    model.mesh.primitives.clear();
    model.mesh.primitives.reserve(temp_primitives.size());

    for (auto& temp : temp_primitives) {
        ArenaMesh::Primitive primitive;
        primitive.base_texture_slot = temp.base_texture_slot;
        primitive.base_color_factor = temp.base_color_factor;
        primitive.indices = std::move(temp.indices);
        primitive.positions = std::move(temp.positions);
        primitive.texcoords = std::move(temp.texcoords);
        primitive.bone_ids = std::move(temp.bone_ids);
        primitive.bone_weights = std::move(temp.bone_weights);

        primitive.vertex_data.reserve(primitive.positions.size() * 5);
        for (size_t i = 0; i < primitive.positions.size(); ++i) {
            const Vec3& p = primitive.positions[i];
            const float x = (p.x - model.bounds_center[0]) * model.bounds_scale;
            const float y = (p.y - model.bounds_min_y) * model.bounds_scale;
            const float z = (p.z - model.bounds_center[2]) * model.bounds_scale;

            float u = 0.0f;
            float v = 0.0f;
            if (i < primitive.texcoords.size()) {
                u = primitive.texcoords[i][0];
                v = 1.0f - primitive.texcoords[i][1];
            }

            primitive.vertex_data.push_back(x);
            primitive.vertex_data.push_back(y);
            primitive.vertex_data.push_back(z);
            primitive.vertex_data.push_back(u);
            primitive.vertex_data.push_back(v);
        }

        model.mesh.primitives.push_back(std::move(primitive));
    }

    if (!model.clips.empty()) {
        update_animated_model(model, 0, 0.0f);
    }

    return true;
}

bool load_animation_clip(const std::filesystem::path& clip_path, AnimatedModel::Clip& clip, std::string& error) {
    Assimp::Importer importer;
    const unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices;
    const aiScene* scene = importer.ReadFile(clip_path.string(), flags);
    if (scene == nullptr || scene->mNumAnimations == 0) {
        error = "Failed to import animation clip: " + clip_path.string();
        const char* assimp_error = importer.GetErrorString();
        if (assimp_error != nullptr && assimp_error[0] != '\0') {
            error += " (" + std::string(assimp_error) + ")";
        }
        return false;
    }

    const aiAnimation* anim = scene->mAnimations[0];
    clip = AnimatedModel::Clip{};
    clip.name = anim->mName.C_Str();
    clip.duration = static_cast<double>(anim->mDuration);
    clip.ticks_per_second = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;

    for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
        const aiNodeAnim* channel = anim->mChannels[c];
        if (channel == nullptr) {
            continue;
        }

        AnimatedModel::Channel out_channel;
        out_channel.node_name = channel->mNodeName.C_Str();
        extract_track_keys(channel, out_channel.position_keys, out_channel.rotation_keys, out_channel.scale_keys);
        clip.channels.push_back(std::move(out_channel));
    }

    return true;
}

void update_animated_model(AnimatedModel& model, int clip_index, float time_seconds) {
    if (model.mesh.primitives.empty()) {
        return;
    }

    if (clip_index < 0 || clip_index >= static_cast<int>(model.clips.size()) || model.bones.empty()) {
        for (auto& primitive : model.mesh.primitives) {
            primitive.vertex_data.clear();
            primitive.vertex_data.reserve(primitive.positions.size() * 5);
            for (size_t i = 0; i < primitive.positions.size(); ++i) {
                const Vec3& p = primitive.positions[i];
                const float x = (p.x - model.bounds_center[0]) * model.bounds_scale;
                const float y = (p.y - model.bounds_min_y) * model.bounds_scale;
                const float z = (p.z - model.bounds_center[2]) * model.bounds_scale;
                const float u = (i < primitive.texcoords.size()) ? primitive.texcoords[i][0] : 0.0f;
                const float v = (i < primitive.texcoords.size()) ? (1.0f - primitive.texcoords[i][1]) : 0.0f;
                primitive.vertex_data.push_back(x);
                primitive.vertex_data.push_back(y);
                primitive.vertex_data.push_back(z);
                primitive.vertex_data.push_back(u);
                primitive.vertex_data.push_back(v);
            }
        }
        return;
    }

    const AnimatedModel::Clip& clip = model.clips[clip_index];
    double clip_time = time_seconds;
    if (clip.duration > 0.0) {
        clip_time = std::fmod(time_seconds * clip.ticks_per_second, clip.duration);
    }

    std::unordered_map<std::string, const AnimatedModel::Channel*> channel_lookup;
    for (const auto& channel : clip.channels) {
        channel_lookup[channel.node_name] = &channel;
    }

    std::vector<Mat4> global_transforms(model.nodes.size(), identity_mat4());

    std::function<void(int, const Mat4&)> recurse = [&](int node_index, const Mat4& parent_transform) {
        const auto& node = model.nodes[node_index];
        Mat4 local = node.local_transform;

        const auto channel_it = channel_lookup.find(node.name);
        if (channel_it != channel_lookup.end() && channel_it->second != nullptr) {
            const auto& channel = *channel_it->second;
            const Vec3 translation = channel.position_keys.empty()
                ? Vec3{0.0f, 0.0f, 0.0f}
                : sample_vec3_keys(channel.position_keys, clip_time);
            const Vec3 scale = channel.scale_keys.empty()
                ? Vec3{1.0f, 1.0f, 1.0f}
                : sample_vec3_keys(channel.scale_keys, clip_time);
            const std::array<float, 4> rotation = channel.rotation_keys.empty()
                ? std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}
                : sample_quat_keys(channel.rotation_keys, clip_time);
            local = compose_trs({translation.x, translation.y, translation.z}, rotation, {scale.x, scale.y, scale.z});
        }

        global_transforms[node_index] = mul_mat4(parent_transform, local);
        for (int child_index : node.children) {
            recurse(child_index, global_transforms[node_index]);
        }
    };

    if (!model.nodes.empty()) {
        recurse(0, identity_mat4());
    }

    std::vector<Mat4> bone_matrices(model.bones.size(), identity_mat4());
    for (size_t i = 0; i < model.bones.size(); ++i) {
        const auto& bone = model.bones[i];
        if (bone.node_index >= 0 && bone.node_index < static_cast<int>(global_transforms.size())) {
            bone_matrices[i] = mul_mat4(model.global_inverse, mul_mat4(global_transforms[bone.node_index], bone.offset));
        }
    }

    for (auto& primitive : model.mesh.primitives) {
        primitive.vertex_data.clear();
        primitive.vertex_data.reserve(primitive.positions.size() * 5);

        for (size_t i = 0; i < primitive.positions.size(); ++i) {
            Vec3 skinned{};
            bool has_influence = false;

            const auto& ids = primitive.bone_ids[i];
            const auto& weights = primitive.bone_weights[i];
            for (int j = 0; j < 4; ++j) {
                if (ids[j] < 0 || weights[j] <= 0.0f || ids[j] >= static_cast<int>(bone_matrices.size())) {
                    continue;
                }
                const Vec3 transformed = transform_point(bone_matrices[ids[j]], primitive.positions[i]);
                skinned = skinned + transformed * weights[j];
                has_influence = true;
            }

            if (!has_influence) {
                skinned = primitive.positions[i];
            }

            const float x = (skinned.x - model.bounds_center[0]) * model.bounds_scale;
            const float y = (skinned.y - model.bounds_min_y) * model.bounds_scale;
            const float z = (skinned.z - model.bounds_center[2]) * model.bounds_scale;
            const float u = (i < primitive.texcoords.size()) ? primitive.texcoords[i][0] : 0.0f;
            const float v = (i < primitive.texcoords.size()) ? (1.0f - primitive.texcoords[i][1]) : 0.0f;

            primitive.vertex_data.push_back(x);
            primitive.vertex_data.push_back(y);
            primitive.vertex_data.push_back(z);
            primitive.vertex_data.push_back(u);
            primitive.vertex_data.push_back(v);
        }
    }

    model.node_transforms = global_transforms;
    model.bone_transforms = bone_matrices;
}

void free_arena_textures(AnimatedModel& model) {
    free_arena_textures(model.mesh);
}
