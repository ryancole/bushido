#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>

#include "renderer.hpp"

// Coordinates are Vulkan NDC for now: x right, y down, both in [-1, 1].
namespace world {
constexpr float kGroundTop = 0.7f;      // y of the ground surface
constexpr float kGravity = 6.0f;        // units/s^2 (downward = +y)
constexpr float kMoveSpeed = 1.2f;      // units/s
constexpr float kJumpVelocity = -2.4f;  // units/s (upward = -y)
constexpr float kArenaHalfWidth = 0.95f;
} // namespace world

struct Player {
    float x = 0.0f;
    float y = 0.0f; // y of the player's center
    float vy = 0.0f;
    bool grounded = true;
    static constexpr float kHalfWidth = 0.05f;
    static constexpr float kHalfHeight = 0.12f;
};

static void fixedUpdate(Player& player, GLFWwindow* window, float dt) {
    float move = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        move -= 1.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        move += 1.0f;
    }
    player.x += move * world::kMoveSpeed * dt;
    player.x = std::clamp(player.x, -world::kArenaHalfWidth + Player::kHalfWidth,
                          world::kArenaHalfWidth - Player::kHalfWidth);

    bool jumpHeld = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS;
    if (player.grounded && jumpHeld) {
        player.vy = world::kJumpVelocity;
        player.grounded = false;
    }

    if (!player.grounded) {
        player.vy += world::kGravity * dt;
        player.y += player.vy * dt;
        float standingY = world::kGroundTop - Player::kHalfHeight;
        if (player.y >= standingY) {
            player.y = standingY;
            player.vy = 0.0f;
            player.grounded = true;
        }
    }
}

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

    Player player;
    player.y = world::kGroundTop - Player::kHalfHeight;

    constexpr float kFixedDt = 1.0f / 120.0f;
    float accumulator = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        auto now = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        accumulator += std::min(frameTime, 0.25f); // avoid spiral of death on stalls

        while (accumulator >= kFixedDt) {
            fixedUpdate(player, window, kFixedDt);
            accumulator -= kFixedDt;
        }

        if (renderer.beginFrame()) {
            // Ground.
            renderer.drawQuad({{0.0f, (world::kGroundTop + 1.0f) * 0.5f},
                               {1.0f, (1.0f - world::kGroundTop) * 0.5f},
                               {0.18f, 0.16f, 0.14f, 1.0f}});
            // Player.
            renderer.drawQuad({{player.x, player.y},
                               {Player::kHalfWidth, Player::kHalfHeight},
                               {0.78f, 0.15f, 0.15f, 1.0f}});
            renderer.endFrame();
        }
    }

    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
