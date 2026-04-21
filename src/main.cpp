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
#include <limits>
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

bool find_nearest_enemy(const GameState& game, Vec3& nearest_enemy) {
    if (game.enemies.empty()) {
        return false;
    }

    float best_d2 = std::numeric_limits<float>::max();
    size_t best_index = 0;
    for (size_t i = 0; i < game.enemies.size(); ++i) {
        const float d2 = distance_xz_sq(game.player_position, game.enemies[i].position);
        if (d2 < best_d2) {
            best_d2 = d2;
            best_index = i;
        }
    }

    nearest_enemy = game.enemies[best_index].position;
    return true;
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

        // Player world matrix: translate to game position and scale normalized mesh to gameplay size.
        const Mat4 player_world = mul_mat4(
            make_translation(game.player_position.x, game.player_position.y - 0.55f, game.player_position.z),
            make_scale(2.2f, 2.2f, 2.2f)
        );

        if (have_vanguard) {
            int selected_clip = idle_clip_index;
            if (player_is_moving && walk_clip_index >= 0) {
                selected_clip = walk_clip_index;
            } else if (selected_clip < 0 && walk_clip_index >= 0) {
                selected_clip = walk_clip_index;
            }

            update_animated_model(vanguard, selected_clip, static_cast<float>(animation_clock));
            render_model(vanguard.mesh, player_world);
        } else {
            draw_box(game.player_position, {0.9f, 1.0f, 0.9f}, {0.22f, 0.85f, 0.36f});
        }

        if (have_staff) {
            Vec3 nearest_enemy{};
            float aim_yaw = 0.0f;
            if (find_nearest_enemy(game, nearest_enemy)) {
                const Vec3 to_enemy = nearest_enemy - game.player_position;
                if ((to_enemy.x * to_enemy.x + to_enemy.z * to_enemy.z) > 1e-5f) {
                    aim_yaw = std::atan2(to_enemy.x, to_enemy.z);
                }
            }

            Mat4 weapon_world = mul_mat4(
                mul_mat4(player_world, make_translation(0.36f, 0.92f, 0.16f)),
                make_rotate_y(aim_yaw)
            );

            if (have_vanguard) {
                const auto hand_it = vanguard.node_lookup.find("mixamorig_RightHand");
                if (hand_it != vanguard.node_lookup.end() && hand_it->second >= 0 && hand_it->second < static_cast<int>(vanguard.node_transforms.size())) {
                    const Mat4 hand_world = mul_mat4(player_world, mul_mat4(vanguard.normalization, vanguard.node_transforms[hand_it->second]));
                    weapon_world = mul_mat4(hand_world, mul_mat4(make_translation(0.08f, -0.02f, 0.12f), make_rotate_y(aim_yaw)));
                }
            }

            weapon_world = mul_mat4(weapon_world, make_scale(0.95f, 0.95f, 0.95f));
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
