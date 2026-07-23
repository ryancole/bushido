#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>

#include "camera.hpp"
#include "game.hpp"
#include "renderer.hpp"
#include "samurai.hpp"

namespace {

void drawBox(Renderer& renderer, glm::vec3 center, glm::vec3 size, glm::vec4 color) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::scale(model, size);
    renderer.drawBox(model, color);
}

PlayerInput readInput(GLFWwindow* window, int left, int right, int away, int toward,
                      int jump) {
    PlayerInput in;
    if (glfwGetKey(window, left) == GLFW_PRESS) in.move.x -= 1.0f;
    if (glfwGetKey(window, right) == GLFW_PRESS) in.move.x += 1.0f;
    if (glfwGetKey(window, away) == GLFW_PRESS) in.move.y -= 1.0f;   // into the screen (-z)
    if (glfwGetKey(window, toward) == GLFW_PRESS) in.move.y += 1.0f; // toward the camera (+z)
    in.jump = glfwGetKey(window, jump) == GLFW_PRESS;
    return in;
}

void drawScene(Renderer& renderer, const Game& game, float time) {
    // Arena floor: covers the playable x/z area with some visual overhang.
    drawBox(renderer, {0.0f, -0.5f, 0.0f},
            {Game::kArenaHalfWidth * 2.0f + 8.0f, 1.0f, Game::kArenaHalfDepth * 2.0f + 4.0f},
            {0.16f, 0.15f, 0.13f, 1.0f});

    // Background pillars for depth reference, behind the playable area.
    const float pillarX[] = {-14.0f, -7.0f, 0.0f, 7.0f, 14.0f};
    const float pillarH[] = {4.5f, 3.2f, 5.5f, 3.8f, 4.8f};
    for (int i = 0; i < 5; ++i) {
        drawBox(renderer, {pillarX[i], pillarH[i] * 0.5f, -Game::kArenaHalfDepth - 1.5f},
                {1.2f, pillarH[i], 1.2f}, {0.10f, 0.10f, 0.14f, 1.0f});
    }

    // Blob shadows: without these, depth position is unreadable while airborne.
    for (int i = 0; i < 2; ++i) {
        const Player& p = game.player(i);
        drawBox(renderer, {p.pos.x, 0.03f, p.pos.z},
                {Player::kHalfWidth * 2.2f, 0.02f, Player::kHalfWidth * 1.6f},
                {0.0f, 0.0f, 0.0f, 0.45f});
    }

    // Fighters: crimson vs indigo samurai.
    const SamuraiColors colors[2] = {
        {{0.72f, 0.13f, 0.13f, 1.0f}, {0.17f, 0.15f, 0.17f, 1.0f}, {0.85f, 0.70f, 0.25f, 1.0f}},
        {{0.15f, 0.28f, 0.72f, 1.0f}, {0.15f, 0.16f, 0.20f, 1.0f}, {0.80f, 0.78f, 0.70f, 1.0f}},
    };
    for (int i = 0; i < 2; ++i) {
        const Player& p = game.player(i);
        float yaw = p.facing > 0.0f ? 0.0f : 3.14159265358979f;
        glm::vec3 feet{p.pos.x, p.pos.y - Player::kHalfHeight, p.pos.z};
        drawSamurai(renderer, feet, yaw, {p.animPhase, p.moveAmount, p.grounded, time},
                    colors[i]);
    }
}

} // namespace

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "bushido", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    Renderer renderer;
    try {
        renderer.init(window);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "renderer init failed: %s\n", e.what());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    glfwSetWindowUserPointer(window, &renderer);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) {
        static_cast<Renderer*>(glfwGetWindowUserPointer(w))->onResize();
    });

    Game game;
    FramingCamera camera;

    constexpr float kFixedDt = 1.0f / 120.0f;
    float accumulator = 0.0f;
    float elapsed = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        PlayerInput inputs[2] = {
            readInput(window, GLFW_KEY_A, GLFW_KEY_D, GLFW_KEY_W, GLFW_KEY_S, GLFW_KEY_SPACE),
            readInput(window, GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN,
                      GLFW_KEY_RIGHT_CONTROL),
        };

        auto now = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        elapsed += frameTime;
        accumulator += std::min(frameTime, 0.25f); // avoid spiral of death on stalls

        while (accumulator >= kFixedDt) {
            game.update(inputs, kFixedDt);
            accumulator -= kFixedDt;
        }

        camera.update(game.player(0).pos, game.player(1).pos, renderer.aspect(), frameTime);

        if (renderer.beginFrame()) {
            renderer.setViewProj(camera.proj(renderer.aspect()) * camera.view());
            drawScene(renderer, game, elapsed);
            renderer.endFrame();
        }
    }

    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
