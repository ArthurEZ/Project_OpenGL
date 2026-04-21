#include "math.h"
#include "assets.h"
#include "graphics.h"
#include "game.h"

#include <iostream>
#include <cstdlib>
#include <cmath>
#include <random>
#include <algorithm>
#include <array>
#ifdef __APPLE__
  #include <OpenGL/gl.h>
#else
  #include <GL/gl.h>
#endif
#include <GLFW/glfw3.h>

namespace {

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

bool compute_staff_socket_world(const AnimatedModel& vanguard, const Mat4& player_world, Mat4& out_socket_world) {
    const auto hand_it = vanguard.node_lookup.find("mixamorig_RightHand");
    if (hand_it == vanguard.node_lookup.end() || hand_it->second < 0 || hand_it->second >= static_cast<int>(vanguard.node_transforms.size())) {
        return false;
    }

    const Mat4 hand_model = mul_mat4(vanguard.normalization, vanguard.node_transforms[hand_it->second]);
    const Mat4 hand_world = mul_mat4(player_world, hand_model);
    const Mat4 hand_rigid_world = make_rigid_transform(hand_world);

    // Keep the staff at the hand socket origin; only apply a fixed local orientation.
    const Mat4 staff_local_rotation = mul_mat4(make_rotate_y(0.0f), mul_mat4(make_rotate_x(-0.6f), make_rotate_z(0.0f)));
    out_socket_world = mul_mat4(hand_rigid_world, staff_local_rotation);
    return true;
}

Vec3 compute_staff_tip_from_socket(const Mat4& staff_socket_world) {
    constexpr float kStaffTipDistance = 1.05f;
    const Vec3 socket_pos{staff_socket_world.m[12], staff_socket_world.m[13], staff_socket_world.m[14]};
    Vec3 forward = normalized({staff_socket_world.m[8], staff_socket_world.m[9], staff_socket_world.m[10]});
    if (length_sq(forward) < 1e-6f) {
        forward = {0.0f, 0.0f, 1.0f};
    }
    return socket_pos + forward * kStaffTipDistance;
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

} // namespace

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

    const auto vanguard_path = find_resource("object/mixamo/Vanguard By T. Choonyung/Vanguard By T. Choonyung.dae");
    const auto vanguard_idle_path = find_resource("object/mixamo/Great Sword Idle.dae");
    const auto vanguard_walk_path = find_resource("object/mixamo/Standing Walk Forward.dae");
    const auto staff_path = find_resource("object/staff/as-vulcan_scarlet_lance_-_3d_model_stylized.glb");

    AnimatedModel vanguard;
    ArenaMesh staff;
    bool have_vanguard = false;
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

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    GameState game;
    std::mt19937 rng(static_cast<unsigned int>(std::random_device{}()));
    double animation_clock = 0.0;
    bool player_is_moving = false;

    double prev_time = glfwGetTime();
    double title_timer = 0.0;

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        float dt = static_cast<float>(now - prev_time);
        prev_time = now;
        dt = clampf(dt, 0.0f, 0.05f);
        bool should_fire_projectile = false;

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

            player_is_moving = length_sq(move) > 1e-5f;
            move = normalized(move);

            if (player_is_moving) {
                const float target_yaw = std::atan2(move.x, move.z);
                constexpr float kPlayerRotationSpeed = 10.0f;
                game.player_yaw = smooth_yaw_towards(game.player_yaw, target_yaw, kPlayerRotationSpeed * dt);
            }

            game.player_position = game.player_position + move * (8.0f * dt);
            animation_clock += dt;

            const float max_player_radius = arena.radius * 0.25f;
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
                should_fire_projectile = true;
                game.shoot_timer += 0.18f;
            }

            for (auto& enemy : game.enemies) {
                const Vec3 to_player = game.player_position - enemy.position;
                const Vec3 dir = normalized(to_player);
                enemy.position = enemy.position + dir * (enemy.speed * dt);
            }

            apply_enemy_soft_separation(game);

            for (const auto& enemy : game.enemies) {
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

        if (game.game_over) {
            player_is_moving = false;
        }

        int selected_clip = idle_clip_index;
        if (player_is_moving && walk_clip_index >= 0) {
            selected_clip = walk_clip_index;
        } else if (selected_clip < 0 && walk_clip_index >= 0) {
            selected_clip = walk_clip_index;
        }

        if (have_vanguard) {
            update_animated_model(vanguard, selected_clip, static_cast<float>(animation_clock));
        }

        const Mat4 player_world = make_player_world_matrix(game);

        Mat4 weapon_socket_world = mul_mat4(player_world, make_translation(0.36f, 0.92f, 0.16f));
        if (have_staff && have_vanguard) {
            Mat4 hand_socket_world;
            if (compute_staff_socket_world(vanguard, player_world, hand_socket_world)) {
                weapon_socket_world = hand_socket_world;
            }
        }

        constexpr float kStaffScale = 0.95f;
        const Mat4 weapon_world = mul_mat4(weapon_socket_world, make_scale(kStaffScale, kStaffScale, kStaffScale));

        Vec3 projectile_spawn = game.player_position;
        projectile_spawn.y = 0.85f;
        if (have_staff) {
            projectile_spawn = compute_staff_tip_from_socket(weapon_socket_world);
        }

        if (!game.game_over && should_fire_projectile) {
            shoot_at_nearest(game, projectile_spawn);
        }

        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        setup_camera(game, width, height);

        glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        render_arena(arena);

        if (have_vanguard) {
            render_model(vanguard.mesh, player_world);
        } else {
            draw_box(game.player_position, {0.9f, 1.0f, 0.9f}, {0.22f, 0.85f, 0.36f});
        }

        if (have_staff) {
            render_model(staff, weapon_world);
        }

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
    free_arena_textures(vanguard);
    free_arena_textures(staff);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
