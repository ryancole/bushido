#include "game.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kMoveSpeed = 6.0f;      // m/s
constexpr float kGravity = 28.0f;       // m/s^2
constexpr float kJumpVelocity = 10.0f;  // m/s

constexpr float kWindupTime = 0.12f;
constexpr float kActiveTime = 0.14f;
constexpr float kRecoveryTime = 0.28f;
constexpr float kAttackMoveScale = 0.35f; // movement slowdown while swinging

constexpr float kAttackReach = 1.2f;      // hitbox length in front of the body
constexpr float kAttackHalfHeight = 0.8f;
constexpr float kAttackHalfDepth = 0.55f;

constexpr float kKnockbackSpeed = 8.0f; // m/s impulse on hit
constexpr float kKnockbackPop = 3.0f;   // upward pop on hit
constexpr float kKnockbackDecay = 6.0f; // 1/s exponential decay
constexpr float kHitstunTime = 0.35f;

float attackDuration(AttackState state) {
    switch (state) {
        case AttackState::Windup: return kWindupTime;
        case AttackState::Active: return kActiveTime;
        case AttackState::Recovery: return kRecoveryTime;
        default: return 1.0f;
    }
}
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

        if (p.hitstun > 0.0f) {
            p.hitstun = std::max(0.0f, p.hitstun - dt);
        }

        // Attack phase machine.
        if (p.attackState != AttackState::None) {
            p.attackTimer -= dt;
            if (p.attackTimer <= 0.0f) {
                switch (p.attackState) {
                    case AttackState::Windup:
                        p.attackState = AttackState::Active;
                        p.attackTimer = kActiveTime;
                        break;
                    case AttackState::Active:
                        p.attackState = AttackState::Recovery;
                        p.attackTimer = kRecoveryTime;
                        break;
                    default:
                        p.attackState = AttackState::None;
                        p.attackTimer = 0.0f;
                        break;
                }
            }
        }
        if (p.attackState == AttackState::None && p.hitstun <= 0.0f && in.attack) {
            p.attackState = AttackState::Windup;
            p.attackTimer = kWindupTime;
            p.attackLanded = false;
        }
        p.attackT = p.attackState == AttackState::None
                        ? 0.0f
                        : std::clamp(1.0f - p.attackTimer / attackDuration(p.attackState),
                                     0.0f, 1.0f);

        // Movement: locked in hitstun, slowed while swinging.
        glm::vec2 move = p.hitstun > 0.0f ? glm::vec2{0.0f} : in.move;
        float len = glm::length(move);
        if (len > 1.0f) {
            move /= len; // diagonal movement is not faster
        }
        float speedScale = p.attackState != AttackState::None ? kAttackMoveScale : 1.0f;
        p.pos.x += move.x * kMoveSpeed * speedScale * dt;
        p.pos.z += move.y * kMoveSpeed * speedScale * dt;

        p.moveAmount = glm::length(move) * speedScale;
        p.animPhase = std::fmod(p.animPhase + p.moveAmount * 12.0f * dt,
                                2.0f * 3.14159265358979f);

        // Knockback decays exponentially.
        p.pos.x += p.kbVel.x * dt;
        p.pos.z += p.kbVel.y * dt;
        p.kbVel *= std::exp(-kKnockbackDecay * dt);

        if (p.grounded && in.jump && p.hitstun <= 0.0f) {
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

    // Resolve sword hits after both players have moved.
    for (int i = 0; i < 2; ++i) {
        Player& p = m_players[i];
        Player& foe = m_players[1 - i];
        if (p.attackState != AttackState::Active || p.attackLanded) {
            continue;
        }
        glm::vec3 hitCenter{p.pos.x + p.facing * (Player::kHalfWidth + kAttackReach * 0.5f),
                            p.pos.y + 0.1f, p.pos.z};
        glm::vec3 hitHalf{kAttackReach * 0.5f, kAttackHalfHeight, kAttackHalfDepth};
        glm::vec3 foeHalf{Player::kHalfWidth, Player::kHalfHeight, Player::kHalfWidth};
        bool overlap = std::abs(foe.pos.x - hitCenter.x) < hitHalf.x + foeHalf.x &&
                       std::abs(foe.pos.y - hitCenter.y) < hitHalf.y + foeHalf.y &&
                       std::abs(foe.pos.z - hitCenter.z) < hitHalf.z + foeHalf.z;
        if (!overlap) {
            continue;
        }
        p.attackLanded = true;
        foe.hitstun = kHitstunTime;
        foe.attackState = AttackState::None; // a clean hit interrupts the foe's swing
        foe.attackTimer = 0.0f;
        glm::vec2 dir{foe.pos.x - p.pos.x, foe.pos.z - p.pos.z};
        float dist = glm::length(dir);
        dir = dist > 1e-4f ? dir / dist : glm::vec2{p.facing, 0.0f};
        foe.kbVel = dir * kKnockbackSpeed;
        foe.vy = std::max(foe.vy, kKnockbackPop);
        foe.grounded = false;
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

    // Facing tracks the opponent, but locks while a swing is in progress.
    if (a.attackState == AttackState::None) {
        a.facing = b.pos.x >= a.pos.x ? 1.0f : -1.0f;
    }
    if (b.attackState == AttackState::None) {
        b.facing = a.pos.x >= b.pos.x ? 1.0f : -1.0f;
    }
}
