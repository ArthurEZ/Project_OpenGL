#ifndef GAME_H
#define GAME_H

#include "math.h"
#include "assets.h"
#include <vector>
#include <GLFW/glfw3.h>
#include <random>

struct GameConfig {
    struct Player {
        float base_hp = 100.0f;
        float base_move_speed = 8.0f;
        float base_bullet_speed = 14.0f;
        float base_attack_damage = 30.0f;
        float fire_rate = 0.18f;
        int exp_base = 100;
        int exp_increment = 30;
    } player;

    struct Enemy {
        float base_hp = 60.0f;
        float base_speed = 1.8f;
        float speed_scaling = 0.035f;
        float max_speed_cap = 5.0f;
        float base_damage = 28.0f;
        float spawn_rate_start = 0.55f;
        float spawn_rate_min = 0.18f;
        float spawn_rate_scaling = 0.008f;
        int exp_reward = 25;
    } enemy;

    struct Upgrades {
        float hp_boost = 20.0f;
        float heal_amount = 35.0f;
        float speed_boost = 0.8f;
        float bullet_speed_boost = 1.5f;
        float damage_boost = 8.0f;
    } upgrades;
};

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
    float player_max_hp = 100.0f;
    float player_move_speed = 8.0f;
    float player_bullet_speed = 14.0f;
    float player_attack_damage = 30.0f;
    int player_level = 1;
    int player_exp = 0;
    int exp_to_next_level = 100;
    int pending_levelups = 0;
    int levelup_option_count = 0;
    int levelup_options[3] = {0, 1, 2};
    int max_hp_upgrade_level = 0;
    int hp_upgrade_level = 0;
    int move_speed_upgrade_level = 0;
    int bullet_speed_upgrade_level = 0;
    int damage_upgrade_level = 0;
    float survival_time = 0.0f;
    int kills = 0;
    float spawn_timer = 0.0f;
    float shoot_timer = 0.0f;
    bool game_over = false;
    std::vector<Enemy> enemies;
    std::vector<Projectile> projectiles;

    float current_zoom_distance = 16.0f;
    float target_zoom_distance = 16.0f;

    GameConfig config;
};

GameConfig load_game_config(const std::string& filepath);
void reset_game_with_config(GameState& game, const GameConfig& config);
void reset_game(GameState& game);

void spawn_enemy_with_config(GameState& game, float arena_radius, std::mt19937& rng);
void spawn_enemy(GameState& game, float arena_radius, std::mt19937& rng);

void shoot_at_nearest_with_config(GameState& game, const Vec3& spawn_position);
void shoot_at_nearest(GameState& game, const Vec3& spawn_position);

void add_player_exp_with_config(GameState& game, int amount);
void add_player_exp(GameState& game, int amount);

void roll_level_up_options(GameState& game, std::mt19937& rng);

bool apply_level_up_choice_with_config(GameState& game, int option_index);
bool apply_level_up_choice(GameState& game, int option_index);

const char* level_up_option_label(int option_index);
void update_window_title(GLFWwindow* window, const GameState& game);

#endif // GAME_H
