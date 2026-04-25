#ifndef GAME_H
#define GAME_H

#include "math.h"
#include "assets.h"
#include <vector>
#include <GLFW/glfw3.h>
#include <random>

struct Enemy {
    Vec3 position;
    float speed = 2.4f;
    float hp = 60.0f;
    float max_hp = 60.0f;
};

struct Projectile {
    Vec3 position;
    Vec3 velocity;
    float lifetime = 1.2f;
};

struct GameState {
    Vec3 player_position{0.0f, 0.55f, 0.0f};
    float player_yaw = 0.0f;
    float player_hp = 100.0f;
    float survival_time = 0.0f;
    int kills = 0;
    float spawn_timer = 0.0f;
    float shoot_timer = 0.0f;
    bool game_over = false;
    std::vector<Enemy> enemies;
    std::vector<Projectile> projectiles;

    float current_zoom_distance = 16.0f;
    float target_zoom_distance = 16.0f;
};

void reset_game(GameState& game);
void spawn_enemy(GameState& game, float arena_radius, std::mt19937& rng);
void shoot_at_nearest(GameState& game, const Vec3& spawn_position);
void update_window_title(GLFWwindow* window, const GameState& game);

#endif // GAME_H
