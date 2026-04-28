#include "game.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>

namespace {

constexpr float kDefaultPlayerY = 0.55f;
constexpr float kDefaultCameraZoom = 16.0f;
constexpr float kEnemySpawnY = 0.45f;
constexpr int kMaxActiveEnemies = 150;
constexpr std::array<const char*, 5> kLevelUpLabels = {
	"MAX HP",
	"HEAL",
	"MOVE SPEED",
	"BULLET SPEED",
	"ATTACK DAMAGE"
};

template <typename T>
void read_value(const nlohmann::json& object, const char* key, T& out_value) {
	const auto it = object.find(key);
	if (it != object.end() && !it->is_null()) {
		out_value = it->get<T>();
	}
}

GameState make_default_game_state(const GameConfig& config) {
	GameState game{};
	game.config = config;
	game.player_position = {0.0f, kDefaultPlayerY, 0.0f};
	game.player_yaw = 0.0f;
	game.player_hp = config.player.base_hp;
	game.player_max_hp = config.player.base_hp;
	game.player_move_speed = config.player.base_move_speed;
	game.player_bullet_speed = config.player.base_bullet_speed;
	game.player_attack_damage = config.player.base_attack_damage;
	game.player_level = 1;
	game.player_exp = 0;
	game.exp_to_next_level = std::max(1, config.player.exp_base);
	game.pending_levelups = 0;
	game.levelup_option_count = 0;
	game.levelup_options[0] = 0;
	game.levelup_options[1] = 1;
	game.levelup_options[2] = 2;
	game.max_hp_upgrade_level = 0;
	game.hp_upgrade_level = 0;
	game.move_speed_upgrade_level = 0;
	game.bullet_speed_upgrade_level = 0;
	game.damage_upgrade_level = 0;
	game.survival_time = 0.0f;
	game.kills = 0;
	game.spawn_timer = 0.0f;
	game.shoot_timer = 0.0f;
	game.game_over = false;
	game.enemies.clear();
	game.projectiles.clear();
	game.current_zoom_distance = kDefaultCameraZoom;
	game.target_zoom_distance = kDefaultCameraZoom;
	return game;
}

Vec3 choose_enemy_spawn_position(float arena_radius, std::mt19937& rng) {
	std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * kPi);
	std::uniform_real_distribution<float> radius_factor_dist(0.10f, 0.18f);
	const float angle = angle_dist(rng);
	const float visible_spawn_radius = std::max(12.0f, arena_radius * radius_factor_dist(rng));
	const float spawn_radius = std::min(arena_radius * 0.98f, visible_spawn_radius);
	return {
		std::cos(angle) * spawn_radius,
		kEnemySpawnY,
		std::sin(angle) * spawn_radius
	};
}

void apply_level_up(GameState& game, int option_kind) {
	switch (option_kind) {
		case 0:
			game.max_hp_upgrade_level += 1;
			game.player_max_hp += 20.0f;
			game.player_hp = std::min(game.player_hp + 20.0f, game.player_max_hp);
			break;
		case 1:
			game.hp_upgrade_level += 1;
			game.player_hp = std::min(game.player_hp + 35.0f, game.player_max_hp);
			break;
		case 2:
			game.move_speed_upgrade_level += 1;
			game.player_move_speed += 0.8f;
			break;
		case 3:
			game.bullet_speed_upgrade_level += 1;
			game.player_bullet_speed += 1.5f;
			break;
		case 4:
			game.damage_upgrade_level += 1;
			game.player_attack_damage += 8.0f;
			break;
		default:
			break;
	}
}

} // namespace

GameConfig load_game_config(const std::string& filepath) {
	GameConfig config{};

	std::ifstream input(filepath);
	if (!input.is_open()) {
		return config;
	}

	try {
		nlohmann::json root;
		input >> root;

		if (root.contains("player") && root["player"].is_object()) {
			const auto& player = root["player"];
			read_value(player, "base_hp", config.player.base_hp);
			read_value(player, "base_move_speed", config.player.base_move_speed);
			read_value(player, "base_bullet_speed", config.player.base_bullet_speed);
			read_value(player, "base_attack_damage", config.player.base_attack_damage);
			read_value(player, "fire_rate", config.player.fire_rate);
			read_value(player, "exp_base", config.player.exp_base);
			read_value(player, "exp_increment", config.player.exp_increment);
		}

		if (root.contains("enemy") && root["enemy"].is_object()) {
			const auto& enemy = root["enemy"];
			read_value(enemy, "base_hp", config.enemy.base_hp);
			read_value(enemy, "base_speed", config.enemy.base_speed);
			read_value(enemy, "speed_scaling", config.enemy.speed_scaling);
			read_value(enemy, "max_speed_cap", config.enemy.max_speed_cap);
			read_value(enemy, "base_damage", config.enemy.base_damage);
			read_value(enemy, "spawn_rate_start", config.enemy.spawn_rate_start);
			read_value(enemy, "spawn_rate_min", config.enemy.spawn_rate_min);
			read_value(enemy, "spawn_rate_scaling", config.enemy.spawn_rate_scaling);
			read_value(enemy, "exp_reward", config.enemy.exp_reward);
		}

		if (root.contains("upgrades") && root["upgrades"].is_object()) {
			const auto& upgrades = root["upgrades"];
			read_value(upgrades, "hp_boost", config.upgrades.hp_boost);
			read_value(upgrades, "heal_amount", config.upgrades.heal_amount);
			read_value(upgrades, "speed_boost", config.upgrades.speed_boost);
			read_value(upgrades, "bullet_speed_boost", config.upgrades.bullet_speed_boost);
			read_value(upgrades, "damage_boost", config.upgrades.damage_boost);
		}
	} catch (...) {
		return GameConfig{};
	}

	return config;
}

void reset_game_with_config(GameState& game, const GameConfig& config) {
	game = make_default_game_state(config);
}

void reset_game(GameState& game) {
	reset_game_with_config(game, game.config);
}

void spawn_enemy_with_config(GameState& game, float arena_radius, std::mt19937& rng) {
	if (static_cast<int>(game.enemies.size()) >= kMaxActiveEnemies) {
		return;
	}

	Enemy enemy{};
	enemy.position = choose_enemy_spawn_position(arena_radius, rng);

	const float survival_time = std::max(0.0f, game.survival_time);
	enemy.speed = std::min(game.config.enemy.max_speed_cap, game.config.enemy.base_speed + survival_time * game.config.enemy.speed_scaling);
	enemy.hp = game.config.enemy.base_hp + survival_time * 0.4f;
	enemy.max_hp = enemy.hp;

	game.enemies.push_back(enemy);
}

void spawn_enemy(GameState& game, float arena_radius, std::mt19937& rng) {
	spawn_enemy_with_config(game, arena_radius, rng);
}

void shoot_at_nearest_with_config(GameState& game, const Vec3& spawn_position) {
	if (game.enemies.empty()) {
		return;
	}

	const Enemy* nearest_enemy = nullptr;
	float nearest_distance_sq = 0.0f;
	for (const auto& enemy : game.enemies) {
		const float candidate_distance_sq = distance_xz_sq(spawn_position, enemy.position);
		if (nearest_enemy == nullptr || candidate_distance_sq < nearest_distance_sq) {
			nearest_enemy = &enemy;
			nearest_distance_sq = candidate_distance_sq;
		}
	}

	if (nearest_enemy == nullptr) {
		return;
	}

	Vec3 direction = nearest_enemy->position - spawn_position;
	direction = normalized(direction);
	if (length_sq(direction) < 1e-6f) {
		direction = {0.0f, 0.0f, 1.0f};
	}

	Projectile projectile{};
	projectile.position = spawn_position;
	projectile.velocity = direction * game.player_bullet_speed;
	projectile.lifetime = 1.2f;
	game.projectiles.push_back(projectile);
}

void shoot_at_nearest(GameState& game, const Vec3& spawn_position) {
	shoot_at_nearest_with_config(game, spawn_position);
}

void add_player_exp_with_config(GameState& game, int amount) {
	if (amount <= 0) {
		return;
	}

	game.player_exp += amount;

	const int exp_increment = std::max(1, game.config.player.exp_increment);
	while (game.player_exp >= game.exp_to_next_level) {
		game.player_exp -= game.exp_to_next_level;
		game.player_level += 1;
		game.pending_levelups += 1;
		game.exp_to_next_level += exp_increment;
	}
}

void add_player_exp(GameState& game, int amount) {
	add_player_exp_with_config(game, amount);
}

void roll_level_up_options(GameState& game, std::mt19937& rng) {
	std::array<int, kLevelUpLabels.size()> options{};
	for (size_t i = 0; i < options.size(); ++i) {
		options[i] = static_cast<int>(i);
	}

	std::shuffle(options.begin(), options.end(), rng);

	game.levelup_option_count = std::min<int>(3, static_cast<int>(options.size()));
	for (int i = 0; i < game.levelup_option_count; ++i) {
		game.levelup_options[i] = options[static_cast<size_t>(i)];
	}
}

bool apply_level_up_choice_with_config(GameState& game, int option_index) {
	if (option_index < 0 || option_index >= game.levelup_option_count) {
		return false;
	}

	const int option_kind = game.levelup_options[option_index];
	apply_level_up(game, option_kind);

	if (game.pending_levelups > 0) {
		game.pending_levelups -= 1;
	}
	game.levelup_option_count = 0;
	return true;
}

bool apply_level_up_choice(GameState& game, int option_index) {
	return apply_level_up_choice_with_config(game, option_index);
}

const char* level_up_option_label(int option_index) {
	if (option_index < 0 || option_index >= static_cast<int>(kLevelUpLabels.size())) {
		return "UNKNOWN";
	}
	return kLevelUpLabels[static_cast<size_t>(option_index)];
}

void update_window_title(GLFWwindow* window, const GameState& game) {
	if (window == nullptr) {
		return;
	}

	std::ostringstream title;
	title.setf(std::ios::fixed);
	title.precision(1);
	title << "OpenGL Starter | Time " << game.survival_time << "s | Kills " << game.kills << " | Level " << game.player_level;
	if (game.game_over) {
		title << " | GAME OVER";
	} else if (game.pending_levelups > 0) {
		title << " | LEVEL UP";
	}

	glfwSetWindowTitle(window, title.str().c_str());
}
