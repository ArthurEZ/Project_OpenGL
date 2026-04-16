#include "math.h"
#include "assets.h"
#include "graphics.h"
#include "game.h"

#include <iostream>
#include <cstdlib>
#include <cmath>
#include <random>
#include <algorithm>
#include <GL/gl.h>
#include <GLFW/glfw3.h>

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
