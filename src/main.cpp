#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#endif

namespace {
using json = nlohmann::json;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kArenaWorldRadius = 150.0f;
constexpr float kArenaRenderYOffset = -80.0f;
constexpr float kCameraPitchDegrees = 79.0f;

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

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat4 {
    std::array<float, 16> m{};
};

Mat4 identity_mat4() {
    return {{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    }};
}

Mat4 mul_mat4(const Mat4& a, const Mat4& b) {
    Mat4 out = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return out;
}

Vec3 transform_point(const Mat4& m, const Vec3& v) {
    return {
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12],
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13],
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14]
    };
}

Mat4 compose_trs(
    const std::array<float, 3>& t,
    const std::array<float, 4>& q,
    const std::array<float, 3>& s
) {
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];

    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    Mat4 m = identity_mat4();

    m.m[0] = (1.0f - 2.0f * (yy + zz)) * s[0];
    m.m[1] = (2.0f * (xy + wz)) * s[0];
    m.m[2] = (2.0f * (xz - wy)) * s[0];

    m.m[4] = (2.0f * (xy - wz)) * s[1];
    m.m[5] = (1.0f - 2.0f * (xx + zz)) * s[1];
    m.m[6] = (2.0f * (yz + wx)) * s[1];

    m.m[8] = (2.0f * (xz + wy)) * s[2];
    m.m[9] = (2.0f * (yz - wx)) * s[2];
    m.m[10] = (1.0f - 2.0f * (xx + yy)) * s[2];

    m.m[12] = t[0];
    m.m[13] = t[1];
    m.m[14] = t[2];
    return m;
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

Vec3 operator+(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(const Vec3& v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

float length_sq(const Vec3& v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

float length(const Vec3& v) {
    return std::sqrt(length_sq(v));
}

Vec3 normalized(const Vec3& v) {
    const float len = length(v);
    if (len < 1e-6f) {
        return {};
    }
    return v * (1.0f / len);
}

float clampf(float value, float min_value, float max_value) {
    return std::max(min_value, std::min(max_value, value));
}

float distance_xz_sq(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
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

struct ArenaMesh {
    struct Texture {
        std::filesystem::path image_path;
        GLuint gl_id = 0;
    };

    struct Primitive {
        std::vector<float> vertex_data; // position.xyz + uv.xy
        std::vector<unsigned int> indices;
        int base_texture_slot = -1;
        int emissive_texture_slot = -1;
        bool alpha_blend = false;
        std::array<float, 4> base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 3> emissive_factor{1.0f, 1.0f, 1.0f};
    };

    std::vector<Texture> textures;
    std::vector<Primitive> primitives;
    float radius = 20.0f;
};

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
#ifdef _WIN32
    GdiPlusContext gdiplus;
    if (!gdiplus.startup()) {
        error = "Failed to initialize GDI+";
        return false;
    }

    bool any_loaded = false;

    for (auto& texture : mesh.textures) {
        if (texture.image_path.empty()) {
            continue;
        }

        const std::wstring path = texture.image_path.wstring();
        Gdiplus::Bitmap bitmap(path.c_str());
        if (bitmap.GetLastStatus() != Gdiplus::Ok) {
            std::cerr << "Texture load warning: " << texture.image_path.string() << " (GDI+ failed)\n";
            continue;
        }

        const int width = static_cast<int>(bitmap.GetWidth());
        const int height = static_cast<int>(bitmap.GetHeight());
        if (width <= 0 || height <= 0) {
            continue;
        }

        Gdiplus::Rect rect(0, 0, width, height);
        Gdiplus::BitmapData data;
        if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
            std::cerr << "Texture lock warning: " << texture.image_path.string() << "\n";
            continue;
        }

        std::vector<unsigned char> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        const int abs_stride = (data.Stride >= 0) ? data.Stride : -data.Stride;

        for (int y = 0; y < height; ++y) {
            const int src_row = (data.Stride >= 0) ? y : (height - 1 - y);
            const auto* src = reinterpret_cast<const unsigned char*>(data.Scan0) + src_row * abs_stride;
            auto* dst = rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;

            for (int x = 0; x < width; ++x) {
                const unsigned char b = src[x * 4 + 0];
                const unsigned char g = src[x * 4 + 1];
                const unsigned char r = src[x * 4 + 2];
                const unsigned char a = src[x * 4 + 3];
                dst[x * 4 + 0] = r;
                dst[x * 4 + 1] = g;
                dst[x * 4 + 2] = b;
                dst[x * 4 + 3] = a;
            }
        }

        bitmap.UnlockBits(&data);

        glGenTextures(1, &texture.gl_id);
        glBindTexture(GL_TEXTURE_2D, texture.gl_id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

        any_loaded = true;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    gdiplus.shutdown();

    if (!any_loaded) {
        error = "No texture files loaded successfully";
    }
    return any_loaded;
#else
    (void)mesh;
    error = "Texture loading is currently implemented for Windows only";
    return false;
#endif
}

void free_arena_textures(ArenaMesh& mesh) {
    for (auto& texture : mesh.textures) {
        if (texture.gl_id != 0) {
            glDeleteTextures(1, &texture.gl_id);
            texture.gl_id = 0;
        }
    }
}

struct Enemy {
    Vec3 position;
    float speed = 2.4f;
};

struct Projectile {
    Vec3 position;
    Vec3 velocity;
    float lifetime = 1.2f;
};

struct GameState {
    Vec3 player_position{0.0f, 0.55f, 0.0f};
    float player_hp = 100.0f;
    float survival_time = 0.0f;
    int kills = 0;
    float spawn_timer = 0.0f;
    float shoot_timer = 0.0f;
    bool game_over = false;
    std::vector<Enemy> enemies;
    std::vector<Projectile> projectiles;
};

void draw_box(const Vec3& center, const Vec3& size, const std::array<float, 3>& color) {
    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float hz = size.z * 0.5f;

    glColor3f(color[0], color[1], color[2]);
    glBegin(GL_QUADS);
    glVertex3f(center.x - hx, center.y - hy, center.z + hz);
    glVertex3f(center.x + hx, center.y - hy, center.z + hz);
    glVertex3f(center.x + hx, center.y + hy, center.z + hz);
    glVertex3f(center.x - hx, center.y + hy, center.z + hz);

    glVertex3f(center.x + hx, center.y - hy, center.z - hz);
    glVertex3f(center.x - hx, center.y - hy, center.z - hz);
    glVertex3f(center.x - hx, center.y + hy, center.z - hz);
    glVertex3f(center.x + hx, center.y + hy, center.z - hz);

    glVertex3f(center.x - hx, center.y - hy, center.z - hz);
    glVertex3f(center.x - hx, center.y - hy, center.z + hz);
    glVertex3f(center.x - hx, center.y + hy, center.z + hz);
    glVertex3f(center.x - hx, center.y + hy, center.z - hz);

    glVertex3f(center.x + hx, center.y - hy, center.z + hz);
    glVertex3f(center.x + hx, center.y - hy, center.z - hz);
    glVertex3f(center.x + hx, center.y + hy, center.z - hz);
    glVertex3f(center.x + hx, center.y + hy, center.z + hz);

    glVertex3f(center.x - hx, center.y + hy, center.z + hz);
    glVertex3f(center.x + hx, center.y + hy, center.z + hz);
    glVertex3f(center.x + hx, center.y + hy, center.z - hz);
    glVertex3f(center.x - hx, center.y + hy, center.z - hz);

    glVertex3f(center.x - hx, center.y - hy, center.z - hz);
    glVertex3f(center.x + hx, center.y - hy, center.z - hz);
    glVertex3f(center.x + hx, center.y - hy, center.z + hz);
    glVertex3f(center.x - hx, center.y - hy, center.z + hz);
    glEnd();
}

void setup_camera(const GameState& game, int width, int height) {
    const float aspect = (height == 0) ? 1.0f : static_cast<float>(width) / static_cast<float>(height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float ortho_height = 16.0f;
    glOrtho(-ortho_height * aspect, ortho_height * aspect, -ortho_height, ortho_height, -120.0, 200.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -10.0f, -28.0f);
    glRotatef(kCameraPitchDegrees, 1.0f, 0.0f, 0.0f);
    glTranslatef(-game.player_position.x, -0.2f, -game.player_position.z);
}

void render_arena(const ArenaMesh& arena) {
    glPushMatrix();
    glTranslatef(0.0f, kArenaRenderYOffset, 0.0f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_TEXTURE_2D);

    for (const auto& primitive : arena.primitives) {
        if (primitive.vertex_data.empty() || primitive.indices.empty()) {
            continue;
        }

        const GLsizei stride = static_cast<GLsizei>(sizeof(float) * 5);
        glVertexPointer(3, GL_FLOAT, stride, primitive.vertex_data.data());
        glTexCoordPointer(2, GL_FLOAT, stride, primitive.vertex_data.data() + 3);

        if (primitive.alpha_blend || primitive.base_color_factor[3] < 0.999f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }

        bool base_textured = false;
        if (primitive.base_texture_slot >= 0 && primitive.base_texture_slot < static_cast<int>(arena.textures.size())) {
            const GLuint tex_id = arena.textures[primitive.base_texture_slot].gl_id;
            if (tex_id != 0) {
                glBindTexture(GL_TEXTURE_2D, tex_id);
                base_textured = true;
            }
        }
        if (!base_textured) {
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        glColor4f(
            primitive.base_color_factor[0],
            primitive.base_color_factor[1],
            primitive.base_color_factor[2],
            primitive.base_color_factor[3]
        );
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(primitive.indices.size()), GL_UNSIGNED_INT, primitive.indices.data());

        if (primitive.emissive_texture_slot >= 0 && primitive.emissive_texture_slot < static_cast<int>(arena.textures.size())) {
            const GLuint emissive_tex = arena.textures[primitive.emissive_texture_slot].gl_id;
            if (emissive_tex != 0) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                glDepthMask(GL_FALSE);
                glBindTexture(GL_TEXTURE_2D, emissive_tex);
                glColor4f(
                    primitive.emissive_factor[0],
                    primitive.emissive_factor[1],
                    primitive.emissive_factor[2],
                    1.0f
                );
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(primitive.indices.size()), GL_UNSIGNED_INT, primitive.indices.data());
                glDepthMask(GL_TRUE);
            }
        }
    }

    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    glPopMatrix();
}

void reset_game(GameState& game) {
    game = GameState{};
}

void spawn_enemy(GameState& game, float arena_radius, std::mt19937& rng) {
    std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * kPi);
    const float angle = angle_dist(rng);
    const float spawn_radius = std::max(6.0f, arena_radius * 0.9f);
    Vec3 spawn{
        std::cos(angle) * spawn_radius,
        0.55f,
        std::sin(angle) * spawn_radius
    };

    Enemy enemy;
    enemy.position = spawn;
    enemy.speed = 1.8f + std::min(3.2f, game.survival_time * 0.035f);
    game.enemies.push_back(enemy);
}

void shoot_at_nearest(GameState& game) {
    if (game.enemies.empty()) {
        return;
    }

    auto nearest_it = game.enemies.begin();
    float nearest_dist = std::numeric_limits<float>::max();

    for (auto it = game.enemies.begin(); it != game.enemies.end(); ++it) {
        const float d2 = distance_xz_sq(it->position, game.player_position);
        if (d2 < nearest_dist) {
            nearest_dist = d2;
            nearest_it = it;
        }
    }

    const Vec3 dir = normalized(nearest_it->position - game.player_position);
    if (length_sq(dir) < 1e-5f) {
        return;
    }

    Projectile p;
    p.position = game.player_position;
    p.position.y = 0.85f;
    p.velocity = dir * 14.0f;
    p.lifetime = 1.1f;
    game.projectiles.push_back(p);
}

void update_window_title(GLFWwindow* window, const GameState& game) {
    char title[256];
    if (game.game_over) {
        std::snprintf(
            title,
            sizeof(title),
            "3D Survivor Arena | GAME OVER | Time: %.1fs | Kills: %d | Press R to Restart",
            game.survival_time,
            game.kills
        );
    } else {
        std::snprintf(
            title,
            sizeof(title),
            "3D Survivor Arena | HP: %d | Time: %.1fs | Kills: %d",
            static_cast<int>(game.player_hp),
            game.survival_time,
            game.kills
        );
    }
    glfwSetWindowTitle(window, title);
}
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return EXIT_FAILURE;
    }

    // Compatibility profile keeps fixed-function calls available for this prototype.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "3D Survivor Arena", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    const auto arena_path = find_resource("object/arena/scene.gltf");
    if (arena_path.empty()) {
        std::cerr << "Could not find arena map: resources/object/arena/scene.gltf\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    ArenaMesh arena;
    std::string load_error;
    if (!load_arena_mesh(arena_path, arena, load_error)) {
        std::cerr << "Failed to load arena map: " << load_error << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::string texture_error;
    if (!upload_arena_textures(arena, texture_error)) {
        std::cerr << "Arena textures unavailable: " << texture_error << '\n';
        std::cerr << "Continuing with untextured fallback rendering.\n";
    }

    std::cout << "Loaded arena map: " << arena_path.string() << '\n';

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    GameState game;
    std::mt19937 rng(static_cast<unsigned int>(std::random_device{}()));

    double prev_time = glfwGetTime();
    double title_timer = 0.0;

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        float dt = static_cast<float>(now - prev_time);
        prev_time = now;
        dt = clampf(dt, 0.0f, 0.05f);

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, 1);
        }

        if (game.game_over && glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            reset_game(game);
        }

        if (!game.game_over) {
            Vec3 move{};
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
                move.z -= 1.0f;
            }
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
                move.z += 1.0f;
            }
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
                move.x -= 1.0f;
            }
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
                move.x += 1.0f;
            }

            move = normalized(move);
            game.player_position = game.player_position + move * (8.0f * dt);

            const float max_player_radius = std::max(4.0f, arena.radius * 0.9f);
            const float r = std::sqrt(game.player_position.x * game.player_position.x + game.player_position.z * game.player_position.z);
            if (r > max_player_radius) {
                const float inv = max_player_radius / r;
                game.player_position.x *= inv;
                game.player_position.z *= inv;
            }

            game.survival_time += dt;

            const float spawn_interval = std::max(0.28f, 0.85f - game.survival_time * 0.006f);
            game.spawn_timer -= dt;
            if (game.spawn_timer <= 0.0f) {
                spawn_enemy(game, arena.radius, rng);
                game.spawn_timer += spawn_interval;
            }

            game.shoot_timer -= dt;
            if (game.shoot_timer <= 0.0f) {
                shoot_at_nearest(game);
                game.shoot_timer += 0.18f;
            }

            for (auto& enemy : game.enemies) {
                const Vec3 to_player = game.player_position - enemy.position;
                const Vec3 dir = normalized(to_player);
                enemy.position = enemy.position + dir * (enemy.speed * dt);

                if (distance_xz_sq(enemy.position, game.player_position) < 0.65f * 0.65f) {
                    game.player_hp -= 28.0f * dt;
                }
            }

            for (auto& p : game.projectiles) {
                p.position = p.position + p.velocity * dt;
                p.lifetime -= dt;
            }

            std::vector<char> enemy_alive(game.enemies.size(), 1);
            for (auto& p : game.projectiles) {
                if (p.lifetime <= 0.0f) {
                    continue;
                }
                for (size_t i = 0; i < game.enemies.size(); ++i) {
                    if (!enemy_alive[i]) {
                        continue;
                    }
                    if (distance_xz_sq(p.position, game.enemies[i].position) < 0.75f * 0.75f) {
                        enemy_alive[i] = 0;
                        p.lifetime = 0.0f;
                        ++game.kills;
                        break;
                    }
                }
            }

            {
                size_t write = 0;
                for (size_t i = 0; i < game.enemies.size(); ++i) {
                    if (enemy_alive[i]) {
                        game.enemies[write++] = game.enemies[i];
                    }
                }
                game.enemies.resize(write);
            }

            game.projectiles.erase(
                std::remove_if(
                    game.projectiles.begin(),
                    game.projectiles.end(),
                    [&](const Projectile& p) {
                        const float r2 = p.position.x * p.position.x + p.position.z * p.position.z;
                        return p.lifetime <= 0.0f || r2 > (arena.radius * arena.radius * 1.2f);
                    }
                ),
                game.projectiles.end()
            );

            if (game.player_hp <= 0.0f) {
                game.player_hp = 0.0f;
                game.game_over = true;
            }
        }

        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        setup_camera(game, width, height);

        glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        render_arena(arena);

        draw_box(game.player_position, {0.9f, 1.0f, 0.9f}, {0.22f, 0.85f, 0.36f});

        for (const auto& enemy : game.enemies) {
            draw_box(enemy.position, {0.75f, 0.9f, 0.75f}, {0.89f, 0.19f, 0.18f});
        }

        for (const auto& projectile : game.projectiles) {
            draw_box(projectile.position, {0.25f, 0.25f, 0.25f}, {1.0f, 0.86f, 0.35f});
        }

        glfwSwapBuffers(window);
        glfwPollEvents();

        title_timer += dt;
        if (title_timer >= 0.12) {
            title_timer = 0.0;
            update_window_title(window, game);
        }
    }

    free_arena_textures(arena);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
