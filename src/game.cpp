#include "game.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kMoveSpeed = 6.0f;      // m/s
constexpr float kGravity = 28.0f;       // m/s^2
constexpr float kJumpVelocity = 10.0f;  // m/s
} // namespace

Game::Game() {
    m_players[0].pos = {-3.0f, Player::kHalfHeight, 0.0f};
    m_players[1].pos = {3.0f, Player::kHalfHeight, 0.0f};
    m_players[0].facing = 1.0f;
    m_players[1].facing = -1.0f;
}

void Game::update(const PlayerInput inputs[2], float dt) {
    for (int i = 0; i < 2; ++i) {
        Player& p = m_players[i];
        const PlayerInput& in = inputs[i];

        glm::vec2 move = in.move;
        float len = glm::length(move);
        if (len > 1.0f) {
            move /= len; // diagonal movement is not faster
        }
        p.pos.x += move.x * kMoveSpeed * dt;
        p.pos.z += move.y * kMoveSpeed * dt;

        p.moveAmount = glm::length(move);
        p.animPhase = std::fmod(p.animPhase + p.moveAmount * 12.0f * dt,
                                2.0f * 3.14159265358979f);

        if (p.grounded && in.jump) {
            p.vy = kJumpVelocity;
            p.grounded = false;
        }
        if (!p.grounded) {
            p.vy -= kGravity * dt;
            p.pos.y += p.vy * dt;
            if (p.pos.y <= Player::kHalfHeight) {
                p.pos.y = Player::kHalfHeight;
                p.vy = 0.0f;
                p.grounded = true;
            }
        }
    }

    // Fighters are solid: push overlapping bodies apart in the ground plane.
    Player& a = m_players[0];
    Player& b = m_players[1];
    glm::vec2 delta{b.pos.x - a.pos.x, b.pos.z - a.pos.z};
    float dist = glm::length(delta);
    float minDist = 2.0f * Player::kHalfWidth;
    float dy = std::abs(b.pos.y - a.pos.y);
    if (dist < minDist && dy < 2.0f * Player::kHalfHeight) {
        glm::vec2 dir = dist > 1e-4f ? delta / dist : glm::vec2{1.0f, 0.0f};
        glm::vec2 push = dir * ((minDist - dist) * 0.5f);
        a.pos.x -= push.x;
        a.pos.z -= push.y;
        b.pos.x += push.x;
        b.pos.z += push.y;
    }

    for (Player& p : m_players) {
        p.pos.x = std::clamp(p.pos.x, -kArenaHalfWidth + Player::kHalfWidth,
                             kArenaHalfWidth - Player::kHalfWidth);
        p.pos.z = std::clamp(p.pos.z, -kArenaHalfDepth + Player::kHalfWidth,
                             kArenaHalfDepth - Player::kHalfWidth);
    }

    a.facing = b.pos.x >= a.pos.x ? 1.0f : -1.0f;
    b.facing = -a.facing;
}
