#ifndef ASSETS_H
#define ASSETS_H

#include "math.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <glad/glad.h>

struct ArenaMesh {
    struct Texture {
        std::filesystem::path image_path;
        GLuint gl_id = 0;
      int embedded_width = 0;
      int embedded_height = 0;
      std::vector<unsigned char> embedded_rgba;
    };

    struct Primitive {
        std::vector<float> vertex_data; // position.xyz + uv.xy
      std::vector<Vec3> positions;
      std::vector<std::array<float, 2>> texcoords;
      std::vector<std::array<int, 4>> bone_ids;
      std::vector<std::array<float, 4>> bone_weights;
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

  struct AnimatedModel {
    struct Node {
      std::string name;
      Mat4 local_transform = identity_mat4();
      std::vector<int> children;
    };

    struct Bone {
      std::string name;
      Mat4 offset = identity_mat4();
      int node_index = -1;
    };

    struct KeyVec3 {
      double time = 0.0;
      Vec3 value;
    };

    struct KeyQuat {
      double time = 0.0;
      std::array<float, 4> value{0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct Channel {
      std::string node_name;
      std::vector<KeyVec3> position_keys;
      std::vector<KeyQuat> rotation_keys;
      std::vector<KeyVec3> scale_keys;
    };

    struct Clip {
      std::string name;
      double duration = 0.0;
      double ticks_per_second = 25.0;
      std::vector<Channel> channels;
    };

    ArenaMesh mesh;
    std::vector<Node> nodes;
    std::unordered_map<std::string, int> node_lookup;
    std::vector<Bone> bones;
    std::unordered_map<std::string, int> bone_lookup;
    std::vector<Clip> clips;
    Mat4 global_inverse = identity_mat4();
    Mat4 normalization = identity_mat4();
    std::vector<Mat4> node_transforms;
    std::vector<Mat4> bone_transforms;
    std::array<float, 3> bounds_center{0.0f, 0.0f, 0.0f};
    float bounds_min_y = 0.0f;
    float bounds_scale = 1.0f;
  };

bool load_arena_mesh(const std::filesystem::path& gltf_path, ArenaMesh& mesh, std::string& error);
bool load_static_model_mesh(const std::filesystem::path& model_path, ArenaMesh& mesh, std::string& error);
  bool load_animated_model(const std::filesystem::path& model_path, AnimatedModel& model, std::string& error);
  bool load_animation_clip(const std::filesystem::path& clip_path, AnimatedModel::Clip& clip, std::string& error);
  void update_animated_model(AnimatedModel& model, int clip_index, float time_seconds);
bool upload_arena_textures(ArenaMesh& mesh, std::string& error);
void free_arena_textures(ArenaMesh& mesh);
  void free_arena_textures(AnimatedModel& model);

std::filesystem::path find_resource(const std::string& relative_path);

#endif // ASSETS_H
