#include "game.hpp"

#include "physics.hpp"
#include "samurai.hpp" // limb bounds double as gameplay hit regions

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr float kMoveSpeed = 6.0f;      // m/s
constexpr float kGravity = 28.0f;       // m/s^2
constexpr float kJumpVelocity = 10.0f;  // m/s

constexpr float kWindupTime = 0.12f;
constexpr float kActiveTime = 0.14f;
constexpr float kRecoveryTime = 0.28f;
constexpr float kAttackMoveScale = 0.35f; // movement slowdown while swinging

// Blade sweep, mirroring the model's Active-phase swing (samurai.cpp
// swordArmAngle case 2): the arm pivots at the shoulder from overhead down to
// in front, with the blade a segment along the arm direction.
constexpr float kSwingStartAngle = 2.60f; // rad about the shoulder, 0 = hanging down
constexpr float kSwingEndAngle = 0.55f;
constexpr float kShoulderHeight = 1.36f;  // above the feet
constexpr float kShoulderSide = 0.26f;    // sword arm's z offset from center
constexpr float kBladeRoot = 0.45f;       // blade segment span, distance from shoulder
constexpr float kBladeTip = 1.60f;

// Forgiveness padding added around each limb's tight bounds for the hit test.
// The head's z pad is deliberately small: the blade travels in a lane
// kShoulderSide off the attacker's center, so beheading takes stepping in
// depth to line that lane up with the neck instead of the near arm.
const glm::vec3 kLimbHitPad[kLimbCount] = {
    {0.25f, 0.18f, 0.20f}, // arm front
    {0.25f, 0.18f, 0.20f}, // arm back
    {0.22f, 0.15f, 0.20f}, // leg front
    {0.22f, 0.15f, 0.20f}, // leg back
    {0.20f, 0.15f, 0.04f}, // head
};

// Torso region (padded): a blade pass through here that misses every limb
// still lands as a normal, non-severing hit.
constexpr float kTorsoCenterY = 1.05f;
const glm::vec3 kTorsoHalf{0.30f, 0.48f, 0.30f};

constexpr float kKnockbackSpeed = 8.0f; // m/s impulse on hit
constexpr float kKnockbackPop = 3.0f;   // upward pop on hit
constexpr float kKnockbackDecay = 6.0f; // 1/s exponential decay
constexpr float kHitstunTime = 0.35f;

// Segment-vs-AABB slab test.
bool segmentHitsBox(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& center,
                    const glm::vec3& half) {
    glm::vec3 d = p1 - p0;
    float tmin = 0.0f, tmax = 1.0f;
    for (int a = 0; a < 3; ++a) {
        float lo = center[a] - half[a];
        float hi = center[a] + half[a];
        if (std::abs(d[a]) < 1e-6f) {
            if (p0[a] < lo || p0[a] > hi) return false;
            continue;
        }
        float t0 = (lo - p0[a]) / d[a];
        float t1 = (hi - p0[a]) / d[a];
        if (t0 > t1) std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        if (tmin > tmax) return false;
    }
    return true;
}

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
    // Physics characters are positioned by their feet; ground surface is y = 0.
    m_physics = std::make_unique<Physics>(kGravity, glm::vec3{-3.0f, 0.0f, 0.0f},
                                          glm::vec3{3.0f, 0.0f, 0.0f});
}

Game::~Game() = default;

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
        // Swinging needs at least one arm; the model swaps the katana to the
        // off hand when the sword arm is gone.
        bool canSwing = !p.severed[static_cast<int>(Limb::ArmFront)] ||
                        !p.severed[static_cast<int>(Limb::ArmBack)];
        if (p.attackState == AttackState::None && p.hitstun <= 0.0f && in.attack &&
            canSwing) {
            p.attackState = AttackState::Windup;
            p.attackTimer = kWindupTime;
            p.attackLanded = false;
            // The whoosh's swell is tuned to peak right as the blade goes active.
            m_soundCues.push_back({Sfx::Swing, p.pos.x});
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
        glm::vec2 planarVel = move * (kMoveSpeed * speedScale) + p.kbVel;
        p.kbVel *= std::exp(-kKnockbackDecay * dt);

        p.moveAmount = glm::length(move) * speedScale;
        p.animPhase = std::fmod(p.animPhase + p.moveAmount * 12.0f * dt,
                                2.0f * 3.14159265358979f);

        if (p.grounded) {
            p.vy = 0.0f;
            if (in.jump && p.hitstun <= 0.0f) {
                p.vy = kJumpVelocity;
                p.grounded = false;
            }
        } else {
            p.vy -= kGravity * dt;
        }

        // Jolt resolves the move against the arena and the other fighter.
        Physics::MoveResult res = m_physics->moveCharacter(
            i, {planarVel.x, p.vy, planarVel.y}, dt);
        p.pos = {res.feetPos.x, res.feetPos.y + Player::kHalfHeight, res.feetPos.z};
        p.grounded = res.onGround;
    }
    m_physics->step(dt);

    // Resolve sword hits after both players have moved. The blade is swept as
    // a segment along the same arc the model animates; the first body part it
    // crosses is where the cut lands, and severable parts come off there.
    for (int i = 0; i < 2; ++i) {
        Player& p = m_players[i];
        Player& foe = m_players[1 - i];
        if (p.attackState != AttackState::Active || p.attackLanded) {
            continue;
        }

        const float swordSide =
            p.severed[static_cast<int>(Limb::ArmFront)] ? -1.0f : 1.0f;
        const glm::vec3 pivot{p.pos.x,
                              p.pos.y - Player::kHalfHeight + kShoulderHeight,
                              p.pos.z + p.facing * swordSide * kShoulderSide};
        const float tCur = p.attackT;
        const float tPrev = std::max(0.0f, tCur - dt / kActiveTime);
        const float foeFeetY = foe.pos.y - Player::kHalfHeight;

        int hitLimb = -1;
        bool hitTorso = false;
        constexpr int kSweepSamples = 4;
        for (int k = 1; k <= kSweepSamples && hitLimb < 0; ++k) {
            float t = glm::mix(tPrev, tCur, static_cast<float>(k) / kSweepSamples);
            float angle = glm::mix(kSwingStartAngle, kSwingEndAngle, t);
            glm::vec3 dir{p.facing * std::sin(angle), -std::cos(angle), 0.0f};
            glm::vec3 s0 = pivot + dir * kBladeRoot;
            glm::vec3 s1 = pivot + dir * kBladeTip;

            // Of the limbs this sample crosses, cut the one whose depth lane
            // best matches the blade's.
            float bestZ = 1e9f;
            for (int l = 0; l < kLimbCount; ++l) {
                if (foe.severed[l]) {
                    continue;
                }
                LimbBounds b = samuraiLimbBounds(l);
                glm::vec3 center{foe.pos.x + foe.facing * b.center.x,
                                 foeFeetY + b.center.y,
                                 foe.pos.z + foe.facing * b.center.z};
                if (!segmentHitsBox(s0, s1, center, b.half + kLimbHitPad[l])) {
                    continue;
                }
                float dz = std::abs(center.z - s0.z);
                if (dz < bestZ) {
                    bestZ = dz;
                    hitLimb = l;
                }
            }
            if (hitLimb < 0 && !hitTorso) {
                glm::vec3 torsoCenter{foe.pos.x, foeFeetY + kTorsoCenterY, foe.pos.z};
                hitTorso = segmentHitsBox(s0, s1, torsoCenter, kTorsoHalf);
            }
        }
        if (hitLimb < 0 && !hitTorso) {
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
        m_soundCues.push_back({hitLimb >= 0 ? Sfx::Dismember : Sfx::Hit, foe.pos.x});
        if (hitLimb >= 0) {
            severLimb(1 - i, static_cast<Limb>(hitLimb), dir);
        }
    }

    // Body solidity and arena bounds are handled by Jolt: the characters
    // collide with each other (CharacterVsCharacterCollision) and with the
    // static arena walls.
    Player& a = m_players[0];
    Player& b = m_players[1];

    // Facing tracks the opponent, but locks while a swing is in progress.
    if (a.attackState == AttackState::None) {
        a.facing = b.pos.x >= a.pos.x ? 1.0f : -1.0f;
    }
    if (b.attackState == AttackState::None) {
        b.facing = a.pos.x >= b.pos.x ? 1.0f : -1.0f;
    }
}

// Marks the limb lost and launches it as a debris rigid body along the hit
// direction, spinning end over end.
void Game::severLimb(int victim, Limb limb, const glm::vec2& impulseDir) {
    Player& v = m_players[victim];
    v.severed[static_cast<int>(limb)] = true;

    LimbBounds b = samuraiLimbBounds(static_cast<int>(limb));
    glm::vec3 center{v.pos.x + v.facing * b.center.x,
                     v.pos.y - Player::kHalfHeight + b.center.y,
                     v.pos.z + v.facing * b.center.z};
    float yaw = v.facing > 0.0f ? 0.0f : 3.14159265358979f;
    glm::vec3 vel{impulseDir.x * 4.0f, 4.5f, impulseDir.y * 4.0f};
    glm::vec3 angVel{0.0f, 3.0f, -impulseDir.x * 12.0f};
    int id = m_physics->addDebris(center, yaw, b.half, vel, angVel);
    m_pieces.push_back({victim, limb, id});
}

glm::mat4 Game::severedPieceTransform(const SeveredPiece& piece) const {
    return m_physics->debrisTransform(piece.debrisId);
}
