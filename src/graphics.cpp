#include "graphics.h"
#include "math.h"
#include "game.h"
#include <GL/gl.h>

constexpr float kCameraPitchDegrees = 79.0f;
constexpr float kArenaRenderYOffset = -80.0f;

void draw_box(const Vec3& center, const Vec3& size, const std::array<float, 3>& color) {
    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float hz = size.z * 0.5f;

    glColor3f(color[0], color[1], color[2]);
    glBegin(GL_QUADS);
    glVertex3f(center.x - hx, center.y - hy, center.z + hz);
    glVertex3f(center.x + hx, center.y - hy, center.z + hz);
    glVertex3f(center.x + hx, center.y + hy, center.z + hz);
    glVertex3f(center.x - hx, center.y + hy, center.z + hz);

    glVertex3f(center.x + hx, center.y - hy, center.z - hz);
    glVertex3f(center.x - hx, center.y - hy, center.z - hz);
    glVertex3f(center.x - hx, center.y + hy, center.z - hz);
    glVertex3f(center.x + hx, center.y + hy, center.z - hz);

    glVertex3f(center.x - hx, center.y - hy, center.z - hz);
    glVertex3f(center.x - hx, center.y - hy, center.z + hz);
    glVertex3f(center.x - hx, center.y + hy, center.z + hz);
    glVertex3f(center.x - hx, center.y + hy, center.z - hz);

    glVertex3f(center.x + hx, center.y - hy, center.z + hz);
    glVertex3f(center.x + hx, center.y - hy, center.z - hz);
    glVertex3f(center.x + hx, center.y + hy, center.z - hz);
    glVertex3f(center.x + hx, center.y + hy, center.z + hz);

    glVertex3f(center.x - hx, center.y + hy, center.z + hz);
    glVertex3f(center.x + hx, center.y + hy, center.z + hz);
    glVertex3f(center.x + hx, center.y + hy, center.z - hz);
    glVertex3f(center.x - hx, center.y + hy, center.z - hz);

    glVertex3f(center.x - hx, center.y - hy, center.z - hz);
    glVertex3f(center.x + hx, center.y - hy, center.z - hz);
    glVertex3f(center.x + hx, center.y - hy, center.z + hz);
    glVertex3f(center.x - hx, center.y - hy, center.z + hz);
    glEnd();
}

void setup_camera(const GameState& game, int width, int height) {
    const float aspect = (height == 0) ? 1.0f : static_cast<float>(width) / static_cast<float>(height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float ortho_height = 16.0f;
    glOrtho(-ortho_height * aspect, ortho_height * aspect, -ortho_height, ortho_height, -120.0, 200.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -10.0f, -28.0f);
    glRotatef(kCameraPitchDegrees, 1.0f, 0.0f, 0.0f);
    glTranslatef(-game.player_position.x, -0.2f, -game.player_position.z);
}

void render_arena(const ArenaMesh& arena) {
    glPushMatrix();
    glTranslatef(0.0f, kArenaRenderYOffset, 0.0f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_TEXTURE_2D);

    for (const auto& primitive : arena.primitives) {
        if (primitive.vertex_data.empty() || primitive.indices.empty()) {
            continue;
        }

        const GLsizei stride = static_cast<GLsizei>(sizeof(float) * 5);
        glVertexPointer(3, GL_FLOAT, stride, primitive.vertex_data.data());
        glTexCoordPointer(2, GL_FLOAT, stride, primitive.vertex_data.data() + 3);

        if (primitive.alpha_blend || primitive.base_color_factor[3] < 0.999f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }

        bool base_textured = false;
        if (primitive.base_texture_slot >= 0 && primitive.base_texture_slot < static_cast<int>(arena.textures.size())) {
            const GLuint tex_id = arena.textures[primitive.base_texture_slot].gl_id;
            if (tex_id != 0) {
                glBindTexture(GL_TEXTURE_2D, tex_id);
                base_textured = true;
            }
        }
        if (!base_textured) {
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        glColor4f(
            primitive.base_color_factor[0],
            primitive.base_color_factor[1],
            primitive.base_color_factor[2],
            primitive.base_color_factor[3]
        );
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(primitive.indices.size()), GL_UNSIGNED_INT, primitive.indices.data());

        if (primitive.emissive_texture_slot >= 0 && primitive.emissive_texture_slot < static_cast<int>(arena.textures.size())) {
            const GLuint emissive_tex = arena.textures[primitive.emissive_texture_slot].gl_id;
            if (emissive_tex != 0) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                glDepthMask(GL_FALSE);
                glBindTexture(GL_TEXTURE_2D, emissive_tex);
                glColor4f(
                    primitive.emissive_factor[0],
                    primitive.emissive_factor[1],
                    primitive.emissive_factor[2],
                    1.0f
                );
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(primitive.indices.size()), GL_UNSIGNED_INT, primitive.indices.data());
                glDepthMask(GL_TRUE);
            }
        }
    }

    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    glPopMatrix();
}

void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}
