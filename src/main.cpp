#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

namespace {
void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

std::vector<std::filesystem::path> resource_roots() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    return {
        cwd / "resources",
        cwd.parent_path() / "resources",
        std::filesystem::path(APP_SOURCE_DIR) / "resources"
    };
}

std::filesystem::path find_resource(const std::string& relative_path) {
    for (const auto& root : resource_roots()) {
        const auto candidate = root / relative_path;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "OpenGL Starter", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    const auto model_path = find_resource("cube.obj");
    if (model_path.empty()) {
        std::cerr << "Could not find cube.obj in resources folders:\n";
        for (const auto& root : resource_roots()) {
            std::cerr << "  - " << root.string() << '\n';
        }
    } else {
        std::ifstream model_file(model_path);
        if (!model_file) {
            std::cerr << "Found model but failed to open: " << model_path.string() << '\n';
        } else {
            std::cout << "Loaded model from: " << model_path.string() << '\n';
        }
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, 1);
        }

        const double t = glfwGetTime();
        const float r = static_cast<float>(0.5 + 0.5 * std::sin(t));
        const float g = static_cast<float>(0.5 + 0.5 * std::sin(t + 2.094));
        const float b = static_cast<float>(0.5 + 0.5 * std::sin(t + 4.188));

        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
