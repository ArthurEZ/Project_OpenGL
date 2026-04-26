#include "math.h"
#include "assets.h"
#include "graphics.h"
#include "game.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <random>
#include <algorithm>
#include <array>

namespace {

struct GroundTriangle;

bool sample_ground_y(const std::vector<GroundTriangle>& triangles, float x, float z, float& out_y);

Mat4 make_translation(float x, float y, float z) {
    Mat4 m = identity_mat4();
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    return m;
}

Mat4 make_scale(float x, float y, float z) {
    Mat4 m = identity_mat4();
    m.m[0] = x;
    m.m[5] = y;
    m.m[10] = z;
    return m;
}

Mat4 make_rotate_y(float yaw_radians) {
    const float c = std::cos(yaw_radians);
    const float s = std::sin(yaw_radians);
    Mat4 m = identity_mat4();
    m.m[0] = c;
    m.m[8] = s;
    m.m[2] = -s;
    m.m[10] = c;
    return m;
}

Mat4 make_rotate_x(float pitch_radians) {
    const float c = std::cos(pitch_radians);
    const float s = std::sin(pitch_radians);
    Mat4 m = identity_mat4();
    m.m[5] = c;
    m.m[9] = -s;
    m.m[6] = s;
    m.m[10] = c;
    return m;
}

Mat4 make_rotate_z(float roll_radians) {
    const float c = std::cos(roll_radians);
    const float s = std::sin(roll_radians);
    Mat4 m = identity_mat4();
    m.m[0] = c;
    m.m[4] = -s;
    m.m[1] = s;
    m.m[5] = c;
    return m;
}

Vec3 cross_vec3(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float wrap_angle_pi(float angle) {
    while (angle > kPi) {
        angle -= 2.0f * kPi;
    }
    while (angle < -kPi) {
        angle += 2.0f * kPi;
    }
    return angle;
}

float smooth_yaw_towards(float current, float target, float max_step) {
    const float delta = clampf(wrap_angle_pi(target - current), -max_step, max_step);
    return wrap_angle_pi(current + delta);
}

constexpr float kPlayerFootOffset = 0.55f;
constexpr float kEnemyRenderScale = 1.0f;
constexpr float kEnemyYawFixRadians = 0.0f;

void place_player_on_ground(GameState& game, const std::vector<GroundTriangle>& ground_triangles) {
    float ground_y = 0.0f;
    if (sample_ground_y(ground_triangles, game.player_position.x, game.player_position.z, ground_y)) {
        game.player_position.y = ground_y + kPlayerFootOffset;
    }
}

void restart_game(GameState& game, const std::vector<GroundTriangle>& ground_triangles) {
    reset_game(game);
    place_player_on_ground(game, ground_triangles);
}

const std::array<unsigned char, 7>& glyph_rows(char c) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(c)))) {
        case 'A': { static const std::array<unsigned char, 7> rows{{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}; return rows; }
        case 'B': { static const std::array<unsigned char, 7> rows{{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}}; return rows; }
        case 'C': { static const std::array<unsigned char, 7> rows{{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}; return rows; }
        case 'D': { static const std::array<unsigned char, 7> rows{{0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}}; return rows; }
        case 'E': { static const std::array<unsigned char, 7> rows{{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}; return rows; }
        case 'F': { static const std::array<unsigned char, 7> rows{{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}}; return rows; }
        case 'G': { static const std::array<unsigned char, 7> rows{{0x0E, 0x11, 0x10, 0x13, 0x11, 0x11, 0x0E}}; return rows; }
        case 'H': { static const std::array<unsigned char, 7> rows{{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}; return rows; }
        case 'I': { static const std::array<unsigned char, 7> rows{{0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}; return rows; }
        case 'J': { static const std::array<unsigned char, 7> rows{{0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}}; return rows; }
        case 'K': { static const std::array<unsigned char, 7> rows{{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}; return rows; }
        case 'L': { static const std::array<unsigned char, 7> rows{{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}}; return rows; }
        case 'M': { static const std::array<unsigned char, 7> rows{{0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11}}; return rows; }
        case 'N': { static const std::array<unsigned char, 7> rows{{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}}; return rows; }
        case 'O': { static const std::array<unsigned char, 7> rows{{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}; return rows; }
        case 'P': { static const std::array<unsigned char, 7> rows{{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}}; return rows; }
        case 'Q': { static const std::array<unsigned char, 7> rows{{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}; return rows; }
        case 'R': { static const std::array<unsigned char, 7> rows{{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}}; return rows; }
        case 'S': { static const std::array<unsigned char, 7> rows{{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}; return rows; }
        case 'T': { static const std::array<unsigned char, 7> rows{{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}}; return rows; }
        case 'U': { static const std::array<unsigned char, 7> rows{{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}; return rows; }
        case 'V': { static const std::array<unsigned char, 7> rows{{0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04}}; return rows; }
        case 'W': { static const std::array<unsigned char, 7> rows{{0x11, 0x11, 0x11, 0x11, 0x15, 0x1B, 0x11}}; return rows; }
        case 'X': { static const std::array<unsigned char, 7> rows{{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}}; return rows; }
        case 'Y': { static const std::array<unsigned char, 7> rows{{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}}; return rows; }
        case 'Z': { static const std::array<unsigned char, 7> rows{{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}}; return rows; }
        case '!': { static const std::array<unsigned char, 7> rows{{0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}}; return rows; }
        case ' ': { static const std::array<unsigned char, 7> rows{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}; return rows; }
        default:  { static const std::array<unsigned char, 7> rows{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}; return rows; }
    }
}

float text_width(const std::string& text, float scale) {
    if (text.empty()) {
        return 0.0f;
    }
    return static_cast<float>(text.size()) * (scale * 6.0f) - scale;
}

void draw_text_screen(float x, float y, float scale, const std::string& text, const std::array<float, 3>& color) {
    glColor4f(color[0], color[1], color[2], 1.0f);
    float cursor_x = x;

    for (char c : text) {
        const auto& rows = glyph_rows(c);
        for (size_t row = 0; row < rows.size(); ++row) {
            for (int col = 0; col < 5; ++col) {
                const unsigned char mask = static_cast<unsigned char>(1u << (4 - col));
                if ((rows[row] & mask) == 0) {
                    continue;
                }

                const float px = cursor_x + static_cast<float>(col) * scale;
                const float py = y + static_cast<float>(row) * scale;
                glBegin(GL_QUADS);
                glVertex2f(px, py);
                glVertex2f(px + scale, py);
                glVertex2f(px + scale, py + scale);
                glVertex2f(px, py + scale);
                glEnd();
            }
        }
        cursor_x += scale * 6.0f;
    }
}

bool point_in_rect(float x, float y, float left, float top, float right, float bottom) {
    return x >= left && x <= right && y >= top && y <= bottom;
}

Mat4 make_rigid_transform(const Mat4& m) {
    Vec3 right = normalized({m.m[0], m.m[1], m.m[2]});
    Vec3 forward = normalized({m.m[8], m.m[9], m.m[10]});
    if (length_sq(right) < 1e-6f || length_sq(forward) < 1e-6f) {
        Mat4 out = identity_mat4();
        out.m[12] = m.m[12];
        out.m[13] = m.m[13];
        out.m[14] = m.m[14];
        return out;
    }

    Vec3 up = normalized(cross_vec3(forward, right));
    if (length_sq(up) < 1e-6f) {
        up = {0.0f, 1.0f, 0.0f};
    }
    right = normalized(cross_vec3(up, forward));

    Mat4 out = identity_mat4();
    out.m[0] = right.x;
    out.m[1] = right.y;
    out.m[2] = right.z;
    out.m[4] = up.x;
    out.m[5] = up.y;
    out.m[6] = up.z;
    out.m[8] = forward.x;
    out.m[9] = forward.y;
    out.m[10] = forward.z;
    out.m[12] = m.m[12];
    out.m[13] = m.m[13];
    out.m[14] = m.m[14];
    return out;
}

Mat4 make_player_world_matrix(const GameState& game) {
    constexpr float kPlayerScale = 3.0f;
    return mul_mat4(
        make_translation(game.player_position.x, game.player_position.y - 0.55f, game.player_position.z),
        mul_mat4(make_rotate_y(game.player_yaw), make_scale(kPlayerScale, kPlayerScale, kPlayerScale))
    );
}

struct StaffAttachSettings {
    const char* hand_bone_name = "mixamorig_RightHand";
    Vec3 local_translation{-0.02f, -0.26f, 0.05f};
    Vec3 local_rotation_radians{0.18f, 0.22f, 1.48f};
    float render_scale = 1.55f;
    float tip_offset = 1.10f; // Unscaled local +Y distance from grip socket to staff tip.
};

StaffAttachSettings make_walk_staff_attach_settings() {
    StaffAttachSettings walk{};
    return walk;
};

StaffAttachSettings make_idle_staff_attach_settings() {
    // Keep idle grip matched to walk so the hand stays on the shaft/bar instead of near the head.
    StaffAttachSettings idle = make_walk_staff_attach_settings();
    return idle;
}

Mat4 make_idle_staff_adjustment() {
    constexpr float kIdlePitchRadians = 0.0f;
    constexpr float kIdleYawRadians = 0.0f;
    constexpr float kIdleRollRadians = kPi * 0.5f; // 90 deg around Z to stand staff up relative to grip.

    const Mat4 idle_rotation = mul_mat4(
        make_rotate_y(kIdleYawRadians),
        mul_mat4(make_rotate_x(kIdlePitchRadians), make_rotate_z(kIdleRollRadians))
    );

    // Idle-only local rotation correction. No translation here to avoid drifting away from hand pivot.
    return idle_rotation;
}

Mat4 make_enemy_local_transform() {
    return mul_mat4(
        make_rotate_y(kEnemyYawFixRadians),
        make_scale(kEnemyRenderScale, kEnemyRenderScale, kEnemyRenderScale)
    );
}

bool get_world_bone_transform(const AnimatedModel& animated_model, const Mat4& model_world, const char* bone_name, Mat4& out_bone_world) {
    int node_index = -1;

    const auto model_bone_it = animated_model.bone_lookup.find(bone_name);
    if (model_bone_it != animated_model.bone_lookup.end()) {
        const int bone_index = model_bone_it->second;
        if (bone_index >= 0 && bone_index < static_cast<int>(animated_model.bones.size())) {
            node_index = animated_model.bones[bone_index].node_index;
        }
    }

    if (node_index < 0) {
        const auto node_it = animated_model.node_lookup.find(bone_name);
        if (node_it != animated_model.node_lookup.end()) {
            node_index = node_it->second;
        }
    }

    if (node_index < 0) {
        const std::array<const char*, 3> hand_aliases = {
            "mixamorig_RightHand",
            "mixamorig:RightHand",
            "RightHand"
        };
        for (const char* alias : hand_aliases) {
            const auto alias_bone_it = animated_model.bone_lookup.find(alias);
            if (alias_bone_it != animated_model.bone_lookup.end()) {
                const int bone_index = alias_bone_it->second;
                if (bone_index >= 0 && bone_index < static_cast<int>(animated_model.bones.size())) {
                    const int mapped_node = animated_model.bones[bone_index].node_index;
                    if (mapped_node >= 0) {
                        node_index = mapped_node;
                        break;
                    }
                }
            }

            const auto alias_node_it = animated_model.node_lookup.find(alias);
            if (alias_node_it != animated_model.node_lookup.end()) {
                node_index = alias_node_it->second;
                break;
            }
        }
    }

    if (node_index < 0 || node_index >= static_cast<int>(animated_model.node_transforms.size())) {
        return false;
    }

    // Keep attachment in the same model space used by skinned vertices:
    // normalized * (global_inverse * node_global).
    const Mat4 bone_model_space = mul_mat4(animated_model.global_inverse, animated_model.node_transforms[node_index]);
    const Mat4 bone_model_normalized = mul_mat4(animated_model.normalization, bone_model_space);
    out_bone_world = mul_mat4(model_world, bone_model_normalized);
    return true;
}

Mat4 make_staff_local_offset(const StaffAttachSettings& settings) {
    const Mat4 local_rotation = mul_mat4(
        make_rotate_y(settings.local_rotation_radians.y),
        mul_mat4(make_rotate_x(settings.local_rotation_radians.x), make_rotate_z(settings.local_rotation_radians.z))
    );
    return mul_mat4(
        make_translation(settings.local_translation.x, settings.local_translation.y, settings.local_translation.z),
        local_rotation
    );
}

bool renderStaff(
    const AnimatedModel& animated_player,
    const Mat4& player_world,
    const ArenaMesh& staff,
    const StaffAttachSettings& settings,
    const Mat4& local_adjustment,
    Vec3& out_tip_world,
    Mat4* out_staff_socket_world = nullptr
) {
    Mat4 hand_world = identity_mat4();
    if (!get_world_bone_transform(animated_player, player_world, settings.hand_bone_name, hand_world)) {
        return false;
    }

    const Mat4 hand_rigid_world = make_rigid_transform(hand_world);
    // Keep hand translation fixed, then rotate locally so the staff pivots in-place at the grip.
    const Mat4 staff_socket_world = mul_mat4(
        mul_mat4(hand_rigid_world, make_staff_local_offset(settings)),
        local_adjustment
    );

    // Tip offset is authored in unscaled mesh space, so scale it to match rendered staff size.
    out_tip_world = transform_point(staff_socket_world, {0.0f, settings.tip_offset * settings.render_scale, 0.0f});

    const Mat4 staff_world = mul_mat4(
        staff_socket_world,
        make_scale(settings.render_scale, settings.render_scale, settings.render_scale)
    );
    render_model(staff, staff_world);

    if (out_staff_socket_world != nullptr) {
        *out_staff_socket_world = staff_socket_world;
    }
    return true;
}

struct GroundTriangle {
    Vec3 a{};
    Vec3 b{};
    Vec3 c{};
};

std::vector<GroundTriangle> build_ground_triangles(const ArenaMesh& arena) {
    std::vector<GroundTriangle> triangles;
    triangles.reserve(arena.primitives.size() * 64);

    constexpr float kWalkableNormalY = 0.45f;
    for (const auto& primitive : arena.primitives) {
        if (primitive.indices.size() < 3 || primitive.vertex_data.empty()) {
            continue;
        }

        const auto read_position = [&](unsigned int index, Vec3& out_pos) -> bool {
            const size_t base = static_cast<size_t>(index) * 5;
            if (base + 2 >= primitive.vertex_data.size()) {
                return false;
            }
            out_pos = {
                primitive.vertex_data[base + 0],
                primitive.vertex_data[base + 1],
                primitive.vertex_data[base + 2]
            };
            return true;
        };

        for (size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
            Vec3 a{};
            Vec3 b{};
            Vec3 c{};
            if (!read_position(primitive.indices[i], a) ||
                !read_position(primitive.indices[i + 1], b) ||
                !read_position(primitive.indices[i + 2], c)) {
                continue;
            }

            const Vec3 normal = normalized(cross_vec3(b - a, c - a));
            if (length_sq(normal) < 1e-8f || normal.y < kWalkableNormalY) {
                continue;
            }

            triangles.push_back({a, b, c});
        }
    }

    return triangles;
}

bool sample_ground_y(const std::vector<GroundTriangle>& triangles, float x, float z, float& out_y) {
    bool found = false;
    float best_y = -1e9f;

    for (const auto& tri : triangles) {
        const float ax = tri.a.x;
        const float az = tri.a.z;
        const float bx = tri.b.x;
        const float bz = tri.b.z;
        const float cx = tri.c.x;
        const float cz = tri.c.z;

        const float denom = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        if (std::fabs(denom) < 1e-6f) {
            continue;
        }

        const float w0 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / denom;
        const float w1 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / denom;
        const float w2 = 1.0f - w0 - w1;
        constexpr float kEpsilon = -1e-4f;
        if (w0 < kEpsilon || w1 < kEpsilon || w2 < kEpsilon) {
            continue;
        }

        const float y = w0 * tri.a.y + w1 * tri.b.y + w2 * tri.c.y;
        if (!found || y > best_y) {
            best_y = y;
            found = true;
        }
    }

    if (!found) {
        return false;
    }

    out_y = best_y;
    return true;
}

void apply_enemy_soft_separation(GameState& game) {
    constexpr float kEnemyRadius = 0.75f;
    constexpr float kMinSeparation = kEnemyRadius * 2.0f;
    constexpr float kMinSeparationSq = kMinSeparation * kMinSeparation;

    for (size_t i = 0; i < game.enemies.size(); ++i) {
        for (size_t j = i + 1; j < game.enemies.size(); ++j) {
            float dx = game.enemies[j].position.x - game.enemies[i].position.x;
            float dz = game.enemies[j].position.z - game.enemies[i].position.z;
            float dist_sq = dx * dx + dz * dz;

            if (dist_sq >= kMinSeparationSq) {
                continue;
            }

            if (dist_sq < 1e-6f) {
                // Deterministic fallback direction when two enemies are at same point.
                const float angle = static_cast<float>((i + 1) * (j + 3));
                dx = std::cos(angle);
                dz = std::sin(angle);
                dist_sq = dx * dx + dz * dz;
            }

            const float dist = std::sqrt(dist_sq);
            if (dist <= 1e-6f) {
                continue;
            }

            const float overlap = kMinSeparation - dist;
            if (overlap <= 0.0f) {
                continue;
            }

            const float nx = dx / dist;
            const float nz = dz / dist;
            const float push = overlap * 0.5f;

            game.enemies[i].position.x -= nx * push;
            game.enemies[i].position.z -= nz * push;
            game.enemies[j].position.x += nx * push;
            game.enemies[j].position.z += nz * push;
        }
    }
}


struct GameContext {
    GLFWwindow* window;
    GameState& game;
    const std::vector<GroundTriangle>& ground_triangles;
    const ArenaMesh& arena;
    const AnimatedModel& vanguard;
    const ArenaMesh& enemy_mesh;
    const ArenaMesh& staff;
    bool have_vanguard;
    bool have_enemy;
    bool have_staff;
    int idle_clip_index;
    int walk_clip_index;
    const StaffAttachSettings& staff_attach_walk_settings;
    const StaffAttachSettings& staff_attach_idle_settings;
    std::mt19937& rng;
    double& animation_clock;
    bool& player_is_moving;
    bool& restart_mouse_down_last_frame;
    bool& levelup_mouse_down_last_frame;
    int width;
    int height;
    float dt;
};

void UpdateInput(GameContext& ctx) {
    if (glfwGetKey(ctx.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(ctx.window, 1);
    }

    if (ctx.game.game_over && glfwGetKey(ctx.window, GLFW_KEY_R) == GLFW_PRESS) {
        restart_game(ctx.game, ctx.ground_triangles);
    }

    if (!ctx.game.game_over) {
        if (ctx.game.pending_levelups > 0) {
            if (ctx.game.levelup_option_count <= 0) {
                roll_level_up_options(ctx.game, ctx.rng);
            }
            ctx.player_is_moving = false;
        } else {
            ctx.levelup_mouse_down_last_frame = false;

            Vec3 move{};
            if (glfwGetKey(ctx.window, GLFW_KEY_W) == GLFW_PRESS) {
                move.z -= 1.0f;
            }
            if (glfwGetKey(ctx.window, GLFW_KEY_S) == GLFW_PRESS) {
                move.z += 1.0f;
            }
            if (glfwGetKey(ctx.window, GLFW_KEY_A) == GLFW_PRESS) {
                move.x -= 1.0f;
            }
            if (glfwGetKey(ctx.window, GLFW_KEY_D) == GLFW_PRESS) {
                move.x += 1.0f;
            }

            ctx.player_is_moving = length_sq(move) > 1e-5f;
            move = normalized(move);

            if (ctx.player_is_moving) {
                const float target_yaw = std::atan2(move.x, move.z);
                constexpr float kPlayerRotationSpeed = 10.0f;
                ctx.game.player_yaw = smooth_yaw_towards(ctx.game.player_yaw, target_yaw, kPlayerRotationSpeed * ctx.dt);
            }

            const Vec3 previous_player_position = ctx.game.player_position;
            const Vec3 proposed_player_position = ctx.game.player_position + move * (ctx.game.player_move_speed * ctx.dt);
            ctx.animation_clock += ctx.dt;

            const float max_player_radius = ctx.arena.radius * 0.5f;
            constexpr float kPlayableCenterX = 0.0f;
            constexpr float kPlayableCenterZ = 0.0f;

            float current_ground_y = 0.0f;
            const bool have_current_ground = sample_ground_y(ctx.ground_triangles, previous_player_position.x, previous_player_position.z, current_ground_y);

            Vec3 next_player_position = proposed_player_position;
            const float to_center_x = next_player_position.x - kPlayableCenterX;
            const float to_center_z = next_player_position.z - kPlayableCenterZ;
            const float r = std::sqrt(to_center_x * to_center_x + to_center_z * to_center_z);
            if (r > max_player_radius) {
                const float inv = max_player_radius / r;
                next_player_position.x = kPlayableCenterX + to_center_x * inv;
                next_player_position.z = kPlayableCenterZ + to_center_z * inv;
            }

            float next_ground_y = 0.0f;
            const bool have_next_ground = sample_ground_y(ctx.ground_triangles, next_player_position.x, next_player_position.z, next_ground_y);

            constexpr float kCharacterHeight = 1.8f;
            constexpr float kMaxStepHeight = kCharacterHeight * 0.5f;
            if (have_current_ground && have_next_ground && (next_ground_y - current_ground_y) > kMaxStepHeight) {
                next_player_position.x = previous_player_position.x;
                next_player_position.z = previous_player_position.z;
                next_ground_y = current_ground_y;
            }

            ctx.game.player_position = next_player_position;

            {
                float ground_y = 0.0f;
                if (have_next_ground) {
                    ctx.game.player_position.y = next_ground_y + kPlayerFootOffset;
                } else if (sample_ground_y(ctx.ground_triangles, ctx.game.player_position.x, ctx.game.player_position.z, ground_y)) {
                    ctx.game.player_position.y = ground_y + kPlayerFootOffset;
                }
            }
        }
    }

    if (ctx.game.game_over) {
        ctx.player_is_moving = false;
        ctx.levelup_mouse_down_last_frame = false;
    } else {
        ctx.restart_mouse_down_last_frame = false;
    }
}

void UpdatePhysics(GameContext& ctx) {
    if (ctx.game.game_over || ctx.game.pending_levelups > 0) return;

    ctx.game.survival_time += ctx.dt;

    const float spawn_interval = std::max(0.18f, 0.55f - ctx.game.survival_time * 0.008f);
    ctx.game.spawn_timer -= ctx.dt;
    if (ctx.game.spawn_timer <= 0.0f) {
        spawn_enemy(ctx.game, ctx.arena.radius, ctx.rng);
        ctx.game.spawn_timer += spawn_interval;
    }

    constexpr float kEnemyFootOffset = 0.45f;
    constexpr float kCharacterHeight = 1.8f;
    constexpr float kMaxStepHeight = kCharacterHeight * 0.5f;

    for (auto& enemy : ctx.game.enemies) {
        const Vec3 previous_enemy_position = enemy.position;
        const Vec3 to_player = ctx.game.player_position - enemy.position;
        const Vec3 dir = normalized(to_player);
        Vec3 next_enemy_position = enemy.position + dir * (enemy.speed * ctx.dt);

        float current_enemy_ground_y = 0.0f;
        float next_enemy_ground_y = 0.0f;
        const bool have_current_enemy_ground = sample_ground_y(ctx.ground_triangles, previous_enemy_position.x, previous_enemy_position.z, current_enemy_ground_y);
        const bool have_next_enemy_ground = sample_ground_y(ctx.ground_triangles, next_enemy_position.x, next_enemy_position.z, next_enemy_ground_y);

        if (have_current_enemy_ground && have_next_enemy_ground && (next_enemy_ground_y - current_enemy_ground_y) > kMaxStepHeight) {
            next_enemy_position.x = previous_enemy_position.x;
            next_enemy_position.z = previous_enemy_position.z;
            next_enemy_ground_y = current_enemy_ground_y;
        }

        enemy.position = next_enemy_position;
        if (have_next_enemy_ground) {
            enemy.position.y = next_enemy_ground_y + kEnemyFootOffset;
        }
    }

    apply_enemy_soft_separation(ctx.game);

    for (auto& p : ctx.game.projectiles) {
        p.position = p.position + p.velocity * ctx.dt;
        p.lifetime -= ctx.dt;
    }

    const float zoom_lerp_speed = 8.0f;
    ctx.game.current_zoom_distance += (ctx.game.target_zoom_distance - ctx.game.current_zoom_distance) * (zoom_lerp_speed * ctx.dt);
}

void UpdateCombat(GameContext& ctx, bool& should_fire_projectile) {
    if (ctx.game.game_over || ctx.game.pending_levelups > 0) return;

    ctx.game.shoot_timer -= ctx.dt;
    if (ctx.game.shoot_timer <= 0.0f) {
        should_fire_projectile = true;
        ctx.game.shoot_timer += 0.18f;
    }

    for (const auto& enemy : ctx.game.enemies) {
        if (distance_xz_sq(enemy.position, ctx.game.player_position) < 0.65f * 0.65f) {
            ctx.game.player_hp -= 28.0f * ctx.dt;
        }
    }

    std::vector<char> enemy_alive(ctx.game.enemies.size(), 1);
    for (auto& p : ctx.game.projectiles) {
        if (p.lifetime <= 0.0f) continue;
        for (size_t i = 0; i < ctx.game.enemies.size(); ++i) {
            if (!enemy_alive[i]) continue;
            if (distance_xz_sq(p.position, ctx.game.enemies[i].position) < 0.75f * 0.75f) {
                ctx.game.enemies[i].hp -= ctx.game.player_attack_damage;
                p.lifetime = 0.0f;
                if (ctx.game.enemies[i].hp <= 0.0f) {
                    enemy_alive[i] = 0;
                    ++ctx.game.kills;
                    add_player_exp(ctx.game, 25);
                }
                break;
            }
        }
    }

    {
        size_t write = 0;
        for (size_t i = 0; i < ctx.game.enemies.size(); ++i) {
            if (enemy_alive[i]) {
                ctx.game.enemies[write++] = ctx.game.enemies[i];
            }
        }
        ctx.game.enemies.resize(write);
    }

    ctx.game.projectiles.erase(
        std::remove_if(
            ctx.game.projectiles.begin(),
            ctx.game.projectiles.end(),
            [&](const Projectile& p) {
                const float r2 = p.position.x * p.position.x + p.position.z * p.position.z;
                return p.lifetime <= 0.0f || r2 > (ctx.arena.radius * ctx.arena.radius * 1.2f);
            }
        ),
        ctx.game.projectiles.end()
    );

    if (ctx.game.player_hp <= 0.0f) {
        ctx.game.player_hp = 0.0f;
        ctx.game.game_over = true;
    }
}

void RenderScene(GameContext& ctx, bool should_fire_projectile) {
    int selected_clip = ctx.idle_clip_index;
    if (ctx.player_is_moving && ctx.walk_clip_index >= 0) {
        selected_clip = ctx.walk_clip_index;
    } else if (selected_clip < 0 && ctx.walk_clip_index >= 0) {
        selected_clip = ctx.walk_clip_index;
    }

    if (ctx.have_vanguard) {
        update_animated_model(const_cast<AnimatedModel&>(ctx.vanguard), selected_clip, static_cast<float>(ctx.animation_clock));
    }

    const Mat4 player_world = make_player_world_matrix(ctx.game);

    glViewport(0, 0, ctx.width, ctx.height);
    setup_camera(ctx.game, ctx.width, ctx.height);

    glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    render_arena(ctx.arena);

    if (ctx.have_vanguard) {
        render_model(ctx.vanguard.mesh, player_world);
    } else {
        draw_box(ctx.game.player_position, {0.9f, 1.0f, 0.9f}, {0.22f, 0.85f, 0.36f});
    }

    Vec3 projectile_spawn = ctx.game.player_position;
    projectile_spawn.y = 0.85f;
    if (ctx.have_staff) {
        const StaffAttachSettings& active_staff_attach_settings = ctx.player_is_moving
            ? ctx.staff_attach_walk_settings
            : ctx.staff_attach_idle_settings;
        const Mat4 staff_pose_adjustment = ctx.player_is_moving
            ? identity_mat4()
            : make_idle_staff_adjustment();

        bool rendered_from_hand_socket = false;
        if (ctx.have_vanguard) {
            Vec3 staff_tip_world{};
            if (renderStaff(ctx.vanguard, player_world, ctx.staff, active_staff_attach_settings, staff_pose_adjustment, staff_tip_world)) {
                projectile_spawn = staff_tip_world;
                rendered_from_hand_socket = true;
            }
        }

        if (!rendered_from_hand_socket) {
            const Mat4 fallback_staff_socket = mul_mat4(player_world, make_translation(0.36f, 0.92f, 0.16f));
            const Mat4 fallback_staff_socket_adjusted = mul_mat4(
                mul_mat4(fallback_staff_socket, make_staff_local_offset(active_staff_attach_settings)),
                staff_pose_adjustment
            );
            projectile_spawn = transform_point(
                fallback_staff_socket_adjusted,
                {0.0f, active_staff_attach_settings.tip_offset * active_staff_attach_settings.render_scale, 0.0f}
            );
            const Mat4 fallback_staff_world = mul_mat4(
                fallback_staff_socket_adjusted,
                make_scale(
                    active_staff_attach_settings.render_scale,
                    active_staff_attach_settings.render_scale,
                    active_staff_attach_settings.render_scale
                )
            );
            render_model(ctx.staff, fallback_staff_world);
        }
    }

    if (!ctx.game.game_over && should_fire_projectile) {
        shoot_at_nearest(ctx.game, projectile_spawn);
    }

    const Mat4 enemy_local = make_enemy_local_transform();
    for (const auto& enemy : ctx.game.enemies) {
        if (ctx.have_enemy) {
            const Mat4 enemy_world = mul_mat4(
                make_translation(enemy.position.x, enemy.position.y, enemy.position.z),
                enemy_local
            );
            render_model(ctx.enemy_mesh, enemy_world);
        } else {
            draw_box(enemy.position, {0.75f, 0.9f, 0.75f}, {0.89f, 0.19f, 0.18f});
        }
    }

    for (const auto& projectile : ctx.game.projectiles) {
        draw_box(projectile.position, {0.25f, 0.25f, 0.25f}, {1.0f, 0.86f, 0.35f});
    }

    for (const auto& enemy : ctx.game.enemies) {
        const float hp_ratio = (enemy.max_hp > 0.0f) ? (enemy.hp / enemy.max_hp) : 0.0f;
        draw_health_bar_world({enemy.position.x, enemy.position.y + 1.25f, enemy.position.z}, 0.95f, 0.12f, hp_ratio);
    }
}

void RenderUI(GameContext& ctx) {
    begin_screen_space(ctx.width, ctx.height);

    if (!ctx.game.game_over) {
        draw_health_bar_screen(18.0f, 18.0f, 240.0f, 18.0f, ctx.game.player_hp / ctx.game.player_max_hp);

        if (ctx.game.pending_levelups > 0) {
            glColor4f(0.0f, 0.0f, 0.0f, 0.50f);
            glBegin(GL_QUADS);
            glVertex2f(0.0f, 0.0f);
            glVertex2f(static_cast<float>(ctx.width), 0.0f);
            glVertex2f(static_cast<float>(ctx.width), static_cast<float>(ctx.height));
            glVertex2f(0.0f, static_cast<float>(ctx.height));
            glEnd();

            const float panel_width = std::min(680.0f, static_cast<float>(ctx.width) - 36.0f);
            const float panel_height = 360.0f;
            const float panel_left = (static_cast<float>(ctx.width) - panel_width) * 0.5f;
            const float panel_top = (static_cast<float>(ctx.height) - panel_height) * 0.5f;
            const float panel_right = panel_left + panel_width;
            const float panel_bottom = panel_top + panel_height;

            glColor4f(0.07f, 0.08f, 0.11f, 0.95f);
            glBegin(GL_QUADS);
            glVertex2f(panel_left, panel_top);
            glVertex2f(panel_right, panel_top);
            glVertex2f(panel_right, panel_bottom);
            glVertex2f(panel_left, panel_bottom);
            glEnd();

            glColor4f(0.92f, 0.95f, 1.0f, 0.35f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(panel_left, panel_top);
            glVertex2f(panel_right, panel_top);
            glVertex2f(panel_right, panel_bottom);
            glVertex2f(panel_left, panel_bottom);
            glEnd();

            const std::string title_text = "CHOOSE UPGRADE";
            const float title_scale = 5.0f;
            draw_text_screen(
                panel_left + (panel_width - text_width(title_text, title_scale)) * 0.5f,
                panel_top + 18.0f,
                title_scale,
                title_text,
                {0.98f, 0.97f, 0.90f}
            );

            const int visible_options = std::max(0, std::min(3, ctx.game.levelup_option_count));
            const float button_width = panel_width - 72.0f;
            const float button_height = 68.0f;
            const float button_gap = 14.0f;
            const float first_button_top = panel_top + 86.0f;

            double cursor_x = 0.0;
            double cursor_y = 0.0;
            glfwGetCursorPos(ctx.window, &cursor_x, &cursor_y);
            const float mx = static_cast<float>(cursor_x);
            const float my = static_cast<float>(cursor_y);
            const bool mouse_down = glfwGetMouseButton(ctx.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            for (int i = 0; i < visible_options; ++i) {
                const float top = first_button_top + static_cast<float>(i) * (button_height + button_gap);
                const float left = panel_left + 36.0f;
                const float right = left + button_width;
                const float bottom = top + button_height;
                const bool hovered = point_in_rect(mx, my, left, top, right, bottom);

                glColor4f(hovered ? 0.27f : 0.16f, hovered ? 0.31f : 0.20f, hovered ? 0.42f : 0.28f, 0.96f);
                glBegin(GL_QUADS);
                glVertex2f(left, top);
                glVertex2f(right, top);
                glVertex2f(right, bottom);
                glVertex2f(left, bottom);
                glEnd();

                glColor4f(0.90f, 0.93f, 1.0f, hovered ? 0.90f : 0.55f);
                glBegin(GL_LINE_LOOP);
                glVertex2f(left, top);
                glVertex2f(right, top);
                glVertex2f(right, bottom);
                glVertex2f(left, bottom);
                glEnd();

                const std::string label = level_up_option_label(ctx.game.levelup_options[i]);
                const float label_scale = 4.0f;
                draw_text_screen(
                    left + (button_width - text_width(label, label_scale)) * 0.5f,
                    top + (button_height - 7.0f * label_scale) * 0.5f,
                    label_scale,
                    label,
                    {0.98f, 0.98f, 0.98f}
                );

                if (hovered && mouse_down && !ctx.levelup_mouse_down_last_frame) {
                    apply_level_up_choice(ctx.game, i);
                    break;
                }
            }

            ctx.levelup_mouse_down_last_frame = mouse_down;
        }
    } else {
        glColor4f(0.0f, 0.0f, 0.0f, 0.60f);
        glBegin(GL_QUADS);
        glVertex2f(0.0f, 0.0f);
        glVertex2f(static_cast<float>(ctx.width), 0.0f);
        glVertex2f(static_cast<float>(ctx.width), static_cast<float>(ctx.height));
        glVertex2f(0.0f, static_cast<float>(ctx.height));
        glEnd();

        const float title_scale = 8.0f;
        const std::string game_over_text = "GAME OVER!";
        draw_text_screen(
            (static_cast<float>(ctx.width) - text_width(game_over_text, title_scale)) * 0.5f,
            static_cast<float>(ctx.height) * 0.30f,
            title_scale,
            game_over_text,
            {1.0f, 0.92f, 0.92f}
        );

        const float button_width = 260.0f;
        const float button_height = 58.0f;
        const float button_left = (static_cast<float>(ctx.width) - button_width) * 0.5f;
        const float button_top = static_cast<float>(ctx.height) * 0.50f;
        const float button_right = button_left + button_width;
        const float button_bottom = button_top + button_height;

        double cursor_x = 0.0;
        double cursor_y = 0.0;
        glfwGetCursorPos(ctx.window, &cursor_x, &cursor_y);
        const bool mouse_down = glfwGetMouseButton(ctx.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool hovered = point_in_rect(static_cast<float>(cursor_x), static_cast<float>(cursor_y), button_left, button_top, button_right, button_bottom);

        glColor4f(0.12f, 0.12f, 0.12f, hovered ? 0.95f : 0.80f);
        glBegin(GL_QUADS);
        glVertex2f(button_left, button_top);
        glVertex2f(button_right, button_top);
        glVertex2f(button_right, button_bottom);
        glVertex2f(button_left, button_bottom);
        glEnd();

        glColor4f(1.0f, 1.0f, 1.0f, 0.65f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(button_left, button_top);
        glVertex2f(button_right, button_top);
        glVertex2f(button_right, button_bottom);
        glVertex2f(button_left, button_bottom);
        glEnd();

        const float button_text_scale = 6.0f;
        const std::string restart_text = "RESTART";
        draw_text_screen(
            button_left + (button_width - text_width(restart_text, button_text_scale)) * 0.5f,
            button_top + (button_height - 7.0f * button_text_scale) * 0.5f - 2.0f,
            button_text_scale,
            restart_text,
            {0.97f, 0.97f, 0.97f}
        );

        if (mouse_down && !ctx.restart_mouse_down_last_frame && hovered) {
            restart_game(ctx.game, ctx.ground_triangles);
        }

        ctx.restart_mouse_down_last_frame = mouse_down;
    }

    end_screen_space();
}

} // namespace

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    GameState* game = static_cast<GameState*>(glfwGetWindowUserPointer(window));
    if (game) {
        // Map scroll to target zoom distance changes.
        game->target_zoom_distance -= static_cast<float>(yoffset) * 1.5f;
        game->target_zoom_distance = clampf(game->target_zoom_distance, 5.0f, 20.0f);
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
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        return EXIT_FAILURE;
    }
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetScrollCallback(window, scroll_callback);

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

    const std::vector<GroundTriangle> ground_triangles = build_ground_triangles(arena);
    if (ground_triangles.empty()) {
        std::cerr << "Ground sampling warning: no walkable triangles detected.\n";
    }

    std::string texture_error;
    if (!upload_arena_textures(arena, texture_error)) {
        std::cerr << "Arena textures unavailable: " << texture_error << '\n';
        std::cerr << "Continuing with untextured fallback rendering.\n";
    }

    std::cout << "Loaded arena map: " << arena_path.string() << '\n';

    const auto vanguard_path = find_resource("object/mixamo/Vanguard By T. Choonyung/Vanguard By T. Choonyung.dae");
    const auto vanguard_idle_path = find_resource("object/mixamo/Great Sword Idle.dae");
    const auto vanguard_walk_path = find_resource("object/mixamo/Standing Walk Forward.dae");
    const auto enemy_path = find_resource("object/enemy_spider_leela/Leela.gltf");
    const auto staff_path = find_resource("object/staff/as-vulcan_scarlet_lance_-_3d_model_stylized.glb");

    AnimatedModel vanguard;
    ArenaMesh enemy_mesh;
    ArenaMesh staff;
    bool have_vanguard = false;
    bool have_enemy = false;
    bool have_staff = false;

    if (!vanguard_path.empty()) {
        std::string model_error;
        have_vanguard = load_animated_model(vanguard_path, vanguard, model_error);
        if (!have_vanguard) {
            std::cerr << "Failed to load Vanguard: " << model_error << '\n';
        }
    } else {
        std::cerr << "Could not find Vanguard model file.\n";
    }

    if (!enemy_path.empty()) {
        std::string model_error;
        have_enemy = load_static_model_mesh(enemy_path, enemy_mesh, model_error);
        if (!have_enemy) {
            std::cerr << "Failed to load Enemy: " << model_error << '\n';
        }
    } else {
        std::cerr << "Could not find Enemy model file.\n";
    }

    if (!staff_path.empty()) {
        std::string model_error;
        have_staff = load_static_model_mesh(staff_path, staff, model_error);
        if (!have_staff) {
            std::cerr << "Failed to load Staff: " << model_error << '\n';
        }
    } else {
        std::cerr << "Could not find Staff model file.\n";
    }

    if (have_vanguard) {
        std::string model_texture_error;
        if (!upload_arena_textures(vanguard.mesh, model_texture_error)) {
            std::cerr << "Vanguard textures unavailable: " << model_texture_error << '\n';
        }
    }

    if (have_enemy && !enemy_mesh.textures.empty()) {
        std::string model_texture_error;
        if (!upload_arena_textures(enemy_mesh, model_texture_error)) {
            std::cerr << "Enemy textures unavailable: " << model_texture_error << '\n';
        }
    }

    if (have_staff) {
        if (!staff.textures.empty()) {
            std::string model_texture_error;
            if (!upload_arena_textures(staff, model_texture_error)) {
                std::cerr << "Staff textures unavailable: " << model_texture_error << '\n';
            }
        }
    }

    int idle_clip_index = -1;
    int walk_clip_index = -1;
    if (have_vanguard && !vanguard_idle_path.empty()) {
        AnimatedModel::Clip idle_clip;
        std::string clip_error;
        if (load_animation_clip(vanguard_idle_path, idle_clip, clip_error)) {
            idle_clip_index = static_cast<int>(vanguard.clips.size());
            vanguard.clips.push_back(std::move(idle_clip));
        } else {
            std::cerr << "Failed to load Vanguard idle animation: " << clip_error << '\n';
        }
    }
    if (have_vanguard && !vanguard_walk_path.empty()) {
        AnimatedModel::Clip walk_clip;
        std::string clip_error;
        if (load_animation_clip(vanguard_walk_path, walk_clip, clip_error)) {
            walk_clip_index = static_cast<int>(vanguard.clips.size());
            vanguard.clips.push_back(std::move(walk_clip));
        } else {
            std::cerr << "Failed to load Vanguard walk animation: " << clip_error << '\n';
        }
    }

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    GameState game;
    {
        float initial_ground_y = 0.0f;
        if (sample_ground_y(ground_triangles, game.player_position.x, game.player_position.z, initial_ground_y)) {
            game.player_position.y = initial_ground_y + kPlayerFootOffset;
        }
    }
    glfwSetWindowUserPointer(window, &game);
    std::mt19937 rng(static_cast<unsigned int>(std::random_device{}()));
    double animation_clock = 0.0;
    bool player_is_moving = false;
    const StaffAttachSettings staff_attach_walk_settings = make_walk_staff_attach_settings();
    const StaffAttachSettings staff_attach_idle_settings = make_idle_staff_attach_settings();

    double prev_time = glfwGetTime();
    double title_timer = 0.0;
    bool restart_mouse_down_last_frame = false;
    bool levelup_mouse_down_last_frame = false;

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        float dt = static_cast<float>(now - prev_time);
        prev_time = now;
        dt = clampf(dt, 0.0f, 0.05f);

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        GameContext ctx = {
            window, game, ground_triangles, arena, vanguard, enemy_mesh, staff,
            have_vanguard, have_enemy, have_staff, idle_clip_index, walk_clip_index,
            staff_attach_walk_settings, staff_attach_idle_settings, rng,
            animation_clock, player_is_moving, restart_mouse_down_last_frame,
            levelup_mouse_down_last_frame, width, height, dt
        };

        UpdateInput(ctx);
        UpdatePhysics(ctx);

        bool should_fire_projectile = false;
        UpdateCombat(ctx, should_fire_projectile);

        RenderScene(ctx, should_fire_projectile);
        RenderUI(ctx);

        glfwSwapBuffers(window);
        glfwPollEvents();

        title_timer += dt;
        if (title_timer >= 0.12) {
            title_timer = 0.0;
            update_window_title(window, game);
        }
    }

    free_arena_textures(arena);
    if (have_vanguard) free_arena_textures(vanguard);
    if (have_staff) free_arena_textures(staff);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
