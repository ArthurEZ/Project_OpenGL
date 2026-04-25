#include "game.h"
#include "math.h"
#include <cstdio>
#include <limits>
#include <algorithm>

void reset_game(GameState& game) {
    game = GameState{};
}

void spawn_enemy(GameState& game, float arena_radius, std::mt19937& rng) {
    std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * kPi);
    std::uniform_real_distribution<float> radial_dist(0.0f, 1.0f);
    const float angle = angle_dist(rng);
    const float min_spawn_radius = 0.0f;
    const float max_spawn_radius = std::max(8.0f, arena_radius * 0.25f);

    // Sample radius with sqrt so points are uniformly distributed across area.
    const float t = radial_dist(rng);
    const float spawn_radius = std::sqrt(
        min_spawn_radius * min_spawn_radius +
        t * (max_spawn_radius * max_spawn_radius - min_spawn_radius * min_spawn_radius)
    );

    Vec3 spawn{
        std::cos(angle) * spawn_radius,
        0.55f,
        std::sin(angle) * spawn_radius
    };

    Enemy enemy;
    enemy.position = spawn;
    enemy.speed = 1.8f + std::min(3.2f, game.survival_time * 0.035f);
    enemy.hp = 60.0f;
    enemy.max_hp = 60.0f;
    game.enemies.push_back(enemy);
}

void shoot_at_nearest(GameState& game, const Vec3& spawn_position) {
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

    const Vec3 dir = normalized(nearest_it->position - spawn_position);
    if (length_sq(dir) < 1e-5f) {
        return;
    }

    Projectile p;
    p.position = spawn_position;
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
