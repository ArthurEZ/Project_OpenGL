#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "math.h"
#include "assets.h"
#include <array>
#include <GLFW/glfw3.h>

struct GameState;

void draw_box(const Vec3& center, const Vec3& size, const std::array<float, 3>& color);
void setup_camera(const GameState& game, int width, int height);
void render_arena(const ArenaMesh& arena);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);

#endif // GRAPHICS_H
