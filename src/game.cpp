#include "game.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kMoveSpeed = 6.0f;      // m/s
constexpr float kGravity = 28.0f;       // m/s^2
constexpr float kJumpVelocity = 10.0f;  // m/s
} // namespace

Game::Game() {
    m_players[0].pos = {-3.0f, Player::kHalfHeight};
    m_players[1].pos = {3.0f, Player::kHalfHeight};
    m_players[0].facing = 1.0f;
    m_players[1].facing = -1.0f;
}

void Game::update(const PlayerInput inputs[2], float dt) {
    for (int i = 0; i < 2; ++i) {
        Player& p = m_players[i];
        const PlayerInput& in = inputs[i];

        p.pos.x += in.move * kMoveSpeed * dt;

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

    // Fighters are solid: push overlapping bodies apart horizontally.
    Player& a = m_players[0];
    Player& b = m_players[1];
    float dx = b.pos.x - a.pos.x;
    float dy = std::abs(b.pos.y - a.pos.y);
    if (std::abs(dx) < 2.0f * Player::kHalfWidth && dy < 2.0f * Player::kHalfHeight) {
        float push = (2.0f * Player::kHalfWidth - std::abs(dx)) * 0.5f;
        float dir = dx >= 0.0f ? 1.0f : -1.0f;
        a.pos.x -= push * dir;
        b.pos.x += push * dir;
    }

    for (Player& p : m_players) {
        p.pos.x = std::clamp(p.pos.x, -kArenaHalfWidth + Player::kHalfWidth,
                             kArenaHalfWidth - Player::kHalfWidth);
    }

    a.facing = dx >= 0.0f ? 1.0f : -1.0f;
    b.facing = -a.facing;
}
