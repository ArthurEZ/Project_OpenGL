#include "game.h"
#include "math.h"
#include <cstdio>
#include <limits>
#include <algorithm>

const char* level_up_option_label(int option_index) {
    switch (option_index) {
        case 0:
            return "Max HP";
        case 1:
            return "Heal HP";
        case 2:
            return "Move Speed";
        case 3:
            return "Bullet Speed";
        case 4:
            return "Attack Damage";
        default:
            return "Unknown";
    }
}

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
    p.velocity = dir * game.player_bullet_speed;
    p.lifetime = 1.1f;
    game.projectiles.push_back(p);
}

void add_player_exp(GameState& game, int amount) {
    if (amount <= 0) {
        return;
    }

    game.player_exp += amount;
    while (game.player_exp >= game.exp_to_next_level) {
        game.player_exp -= game.exp_to_next_level;
        ++game.player_level;
        ++game.pending_levelups;
        game.exp_to_next_level += 30;
    }
}

void roll_level_up_options(GameState& game, std::mt19937& rng) {
    if (game.pending_levelups <= 0) {
        game.levelup_option_count = 0;
        return;
    }

    int pool[5] = {0, 1, 2, 3, 4};
    std::shuffle(pool, pool + 5, rng);
    game.levelup_option_count = 3;
    for (int i = 0; i < game.levelup_option_count; ++i) {
        game.levelup_options[i] = pool[i];
    }
}

bool apply_level_up_choice(GameState& game, int option_index) {
    if (game.pending_levelups <= 0 || game.levelup_option_count <= 0) {
        return false;
    }

    if (option_index < 0 || option_index >= game.levelup_option_count) {
        return false;
    }

    const int selected_upgrade = game.levelup_options[option_index];

    switch (selected_upgrade) {
        case 0: {
            ++game.max_hp_upgrade_level;
            game.player_max_hp += 20.0f;
            if (game.player_hp > game.player_max_hp) {
                game.player_hp = game.player_max_hp;
            }
            break;
        }
        case 1: {
            ++game.hp_upgrade_level;
            game.player_hp = std::min(game.player_max_hp, game.player_hp + 35.0f);
            break;
        }
        case 2: {
            ++game.move_speed_upgrade_level;
            game.player_move_speed += 0.8f;
            break;
        }
        case 3: {
            ++game.bullet_speed_upgrade_level;
            game.player_bullet_speed += 1.5f;
            break;
        }
        case 4: {
            ++game.damage_upgrade_level;
            game.player_attack_damage += 8.0f;
            break;
        }
        default:
            return false;
    }

    --game.pending_levelups;
    game.levelup_option_count = 0;
    return true;
}

void update_window_title(GLFWwindow* window, const GameState& game) {
    char title[256];
    if (game.game_over) {
        std::snprintf(
            title,
            sizeof(title),
            "3D Survivor Arena | GAME OVER | Lv:%d | Time: %.1fs | Kills: %d | Press R to Restart",
            game.player_level,
            game.survival_time,
            game.kills
        );
    } else if (game.pending_levelups > 0) {
        const int c = std::max(0, std::min(3, game.levelup_option_count));
        const char* o1 = (c > 0) ? level_up_option_label(game.levelup_options[0]) : "...";
        const char* o2 = (c > 1) ? level_up_option_label(game.levelup_options[1]) : "...";
        const char* o3 = (c > 2) ? level_up_option_label(game.levelup_options[2]) : "...";
        std::snprintf(
            title,
            sizeof(title),
            "LEVEL UP! Choose: [1] %s [2] %s [3] %s | Pending: %d",
            o1,
            o2,
            o3,
            game.pending_levelups
        );
    } else {
        std::snprintf(
            title,
            sizeof(title),
            "3D Survivor Arena | HP: %d/%d | Lv:%d EXP:%d/%d | Time: %.1fs | Kills: %d",
            static_cast<int>(game.player_hp),
            static_cast<int>(game.player_max_hp),
            game.player_level,
            game.player_exp,
            game.exp_to_next_level,
            game.survival_time,
            game.kills
        );
    }
    glfwSetWindowTitle(window, title);
}
