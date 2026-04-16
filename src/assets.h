#ifndef ASSETS_H
#define ASSETS_H

#include "math.h"
#include <filesystem>
#include <string>
#include <vector>
#include <GL/gl.h>

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

bool load_arena_mesh(const std::filesystem::path& gltf_path, ArenaMesh& mesh, std::string& error);
bool upload_arena_textures(ArenaMesh& mesh, std::string& error);
void free_arena_textures(ArenaMesh& mesh);

std::filesystem::path find_resource(const std::string& relative_path);

#endif // ASSETS_H
