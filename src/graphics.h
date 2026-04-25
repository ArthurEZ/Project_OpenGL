#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "math.h"
#include "assets.h"
#include <array>
#include <GLFW/glfw3.h>

struct GameState;

void draw_box(const Vec3& center, const Vec3& size, const std::array<float, 3>& color);
void begin_screen_space(int width, int height);
void end_screen_space();
void draw_health_bar_world(const Vec3& center, float width, float height, float t);
void draw_health_bar_screen(float x, float y, float width, float height, float t);
void setup_camera(const GameState& game, int width, int height);
void render_arena(const ArenaMesh& arena);
void render_model(const ArenaMesh& model, const Mat4& world_matrix);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

#endif // GRAPHICS_H
