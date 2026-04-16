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
