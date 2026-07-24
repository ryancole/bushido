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

// Blood. Droplets are lighter than fighters so they hang a touch longer;
// most land (and splat) well before the mid-air fade kicks in.
constexpr float kBloodGravity = 20.0f;    // m/s^2
constexpr float kBloodLife = 1.1f;        // s, mid-air fade for strays
constexpr int kHitBloodCount = 12;        // droplets per torso/glancing hit
constexpr int kSeverBloodCount = 26;      // droplets when a limb comes off
constexpr float kHitBloodSpeed = 3.2f;    // m/s burst speed
constexpr float kSeverBloodSpeed = 4.6f;
constexpr std::size_t kMaxBloodParticles = 600;
constexpr std::size_t kMaxBloodMarks = 400;

// Toppling. Losing a leg loses the footing: the body tips about the feet
// toward the missing leg's side like an inverted pendulum (gravity torque
// grows with the lean), stopping just shy of flat so the model's boxes rest
// on the floor instead of clipping through it. Once down, movement is a
// crawl paced by how many arms are left; with none it's a wriggle.
constexpr float kToppleAccel = 16.0f;    // rad/s^2 scale on sin(tilt)
constexpr float kToppleBias = 0.15f;     // rad added inside sin() so 0 isn't stable
constexpr float kToppleKick = 1.2f;      // rad/s initial push when the leg goes
constexpr float kMaxTilt = 1.35f;        // rad (~77°): lying on the ground
constexpr float kProneShapeTilt = 0.7f;  // past this the capsule swaps to prone
constexpr float kCrawlSpeed[3] = {0.10f, 0.20f, 0.30f}; // by remaining arms

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

// Rotates a model-local vector by the player's topple roll (about local +x).
glm::vec3 rollLocal(const Player& p, const glm::vec3& v) {
    float roll = p.bodyRoll();
    if (roll == 0.0f) {
        return v;
    }
    float c = std::cos(roll), s = std::sin(roll);
    return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
}

// Model-local point (feet origin, +x forward) to world space, applying the
// topple roll and the facing mirror — must match drawSamurai's base transform.
glm::vec3 modelToWorld(const Player& p, const glm::vec3& local) {
    glm::vec3 v = rollLocal(p, local);
    return {p.pos.x + p.facing * v.x, p.pos.y - Player::kHalfHeight + v.y,
            p.pos.z + p.facing * v.z};
}

// AABB half extents of a model-local box after the topple roll (a lying
// limb is long in z, not y).
glm::vec3 rollHalfExtent(const Player& p, const glm::vec3& half) {
    float roll = p.bodyRoll();
    float c = std::abs(std::cos(roll)), s = std::abs(std::sin(roll));
    return {half.x, c * half.y + s * half.z, s * half.y + c * half.z};
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

        // Toppling: with a leg gone, gravity wins. Integrate the fall and,
        // once the body is leaning far enough, swap the collision capsule to
        // the squat prone shape so the opponent can step over the body.
        if (p.downed()) {
            if (p.fallTilt < kMaxTilt) {
                p.fallVel += kToppleAccel * std::sin(p.fallTilt + kToppleBias) * dt;
                p.fallTilt += p.fallVel * dt;
                if (p.fallTilt >= kMaxTilt) {
                    p.fallTilt = kMaxTilt;
                    p.fallVel = 0.0f;
                    // The body slaps the floor: thud + a smear under the chest.
                    glm::vec3 chest = modelToWorld(p, {0.0f, kTorsoCenterY, 0.0f});
                    m_soundCues.push_back({Sfx::Thud, chest.x, 0.9f});
                    addBloodMark({chest.x, 0.0f, chest.z}, 0.30f);
                    spawnBlood({chest.x, 0.15f, chest.z}, {0.0f, 1.0f, 0.0f}, 6, 2.0f);
                }
            }
            m_physics->setCharacterProne(i, p.fallTilt > kProneShapeTilt);
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
        if (p.downed()) {
            if (p.fallTilt < kMaxTilt) {
                speedScale = 0.0f; // mid-fall: no footing at all
            } else {
                int arms = (p.severed[static_cast<int>(Limb::ArmFront)] ? 0 : 1) +
                           (p.severed[static_cast<int>(Limb::ArmBack)] ? 0 : 1);
                speedScale *= kCrawlSpeed[arms];
            }
        }
        glm::vec2 planarVel = move * (kMoveSpeed * speedScale) + p.kbVel;
        p.kbVel *= std::exp(-kKnockbackDecay * dt);

        p.moveAmount = glm::length(move) * speedScale;
        p.animPhase = std::fmod(p.animPhase + p.moveAmount * 12.0f * dt,
                                2.0f * 3.14159265358979f);

        if (p.grounded) {
            p.vy = 0.0f;
            if (in.jump && p.hitstun <= 0.0f && !p.downed()) {
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

    // Severed limbs thudding on the ground (or walls/each other), loudness
    // scaled by how hard they hit. ~9 m/s is a limb's first landing; the
    // 0.25-restitution bounce comes back much softer. Ground contacts also
    // smear blood where the limb bounced, sized by how hard it landed.
    for (const Physics::DebrisImpact& impact : m_physics->debrisImpacts()) {
        m_soundCues.push_back(
            {Sfx::Thud, impact.pos.x, std::clamp(impact.speed / 9.0f, 0.3f, 1.0f)});
        if (impact.pos.y < 0.15f) {
            addBloodMark(impact.pos,
                         std::clamp(0.08f + impact.speed * 0.03f, 0.10f, 0.34f));
            spawnBlood({impact.pos.x, 0.08f, impact.pos.z}, {0.0f, 1.0f, 0.0f}, 3,
                       std::min(2.5f, 0.3f * impact.speed));
        }
    }

    // Blood droplets: straight ballistics against the bare ground plane —
    // debris and fighters are too small/fast-moving to be worth testing.
    for (std::size_t n = 0; n < m_blood.size();) {
        BloodParticle& d = m_blood[n];
        d.vel.y -= kBloodGravity * dt;
        d.pos += d.vel * dt;
        d.life -= dt;
        if (d.pos.y <= d.size * 0.5f && d.vel.y < 0.0f) {
            addBloodMark({d.pos.x, 0.0f, d.pos.z}, 0.05f + 0.10f * frand());
            d = m_blood.back();
            m_blood.pop_back();
        } else if (d.life <= 0.0f) {
            d = m_blood.back();
            m_blood.pop_back();
        } else {
            ++n;
        }
    }

    // Resolve sword hits after both players have moved. The blade is swept as
    // a segment along the same arc the model animates; the first body part it
    // crosses is where the cut lands, and severable parts come off there.
    for (int i = 0; i < 2; ++i) {
        Player& p = m_players[i];
        Player& foe = m_players[1 - i];
        if (p.attackState != AttackState::Active || p.attackLanded) {
            continue;
        }

        // Both the blade arc and the target boxes follow each body's topple
        // roll: a downed attacker's chop becomes a sweep along the ground,
        // and a downed defender is hit where the body actually lies.
        const float swordSide =
            p.severed[static_cast<int>(Limb::ArmFront)] ? -1.0f : 1.0f;
        const glm::vec3 pivot =
            modelToWorld(p, {0.0f, kShoulderHeight, swordSide * kShoulderSide});
        const float tCur = p.attackT;
        const float tPrev = std::max(0.0f, tCur - dt / kActiveTime);

        int hitLimb = -1;
        bool hitTorso = false;
        constexpr int kSweepSamples = 4;
        for (int k = 1; k <= kSweepSamples && hitLimb < 0; ++k) {
            float t = glm::mix(tPrev, tCur, static_cast<float>(k) / kSweepSamples);
            float angle = glm::mix(kSwingStartAngle, kSwingEndAngle, t);
            glm::vec3 dir = rollLocal(p, {std::sin(angle), -std::cos(angle), 0.0f});
            dir = {p.facing * dir.x, dir.y, p.facing * dir.z};
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
                glm::vec3 center = modelToWorld(foe, b.center);
                if (!segmentHitsBox(s0, s1, center,
                                    rollHalfExtent(foe, b.half) + kLimbHitPad[l])) {
                    continue;
                }
                float dz = std::abs(center.z - s0.z);
                if (dz < bestZ) {
                    bestZ = dz;
                    hitLimb = l;
                }
            }
            if (hitLimb < 0 && !hitTorso) {
                glm::vec3 torsoCenter = modelToWorld(foe, {0.0f, kTorsoCenterY, 0.0f});
                hitTorso = segmentHitsBox(s0, s1, torsoCenter,
                                          rollHalfExtent(foe, kTorsoHalf));
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

        // Blood sprays from the wound, away from the attacker and upward;
        // dismemberment gushes harder than a body hit.
        glm::vec3 wound = modelToWorld(foe, {0.0f, kTorsoCenterY, 0.0f});
        if (hitLimb >= 0) {
            wound = modelToWorld(foe, samuraiLimbBounds(hitLimb).center);
        }
        glm::vec3 spray = glm::normalize(glm::vec3{dir.x, 1.2f, dir.y});
        spawnBlood(wound, spray, hitLimb >= 0 ? kSeverBloodCount : kHitBloodCount,
                   hitLimb >= 0 ? kSeverBloodSpeed : kHitBloodSpeed);

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
    // A toppled body must not swing around when facing flips: negating
    // fallSide together with facing keeps the world-space lie direction
    // (facing · fallSide) unchanged, so only the sword side mirrors.
    auto faceToward = [](Player& self, const Player& other) {
        if (self.attackState != AttackState::None) {
            return;
        }
        float facing = other.pos.x >= self.pos.x ? 1.0f : -1.0f;
        if (facing != self.facing && self.downed()) {
            self.fallSide = -self.fallSide;
        }
        self.facing = facing;
    };
    faceToward(a, b);
    faceToward(b, a);
}

// Marks the limb lost and launches it as a debris rigid body along the hit
// direction, spinning end over end.
void Game::severLimb(int victim, Limb limb, const glm::vec2& impulseDir) {
    Player& v = m_players[victim];
    v.severed[static_cast<int>(limb)] = true;

    // Losing a leg starts the topple, toward the lost leg's side. If the
    // other leg goes later the body is already down (or falling) — keep the
    // direction it committed to.
    if ((limb == Limb::LegFront || limb == Limb::LegBack) && v.fallSide == 0.0f) {
        v.fallSide = limb == Limb::LegFront ? 1.0f : -1.0f;
        v.fallVel = kToppleKick;
    }

    LimbBounds b = samuraiLimbBounds(static_cast<int>(limb));
    glm::vec3 center = modelToWorld(v, b.center);
    float yaw = v.facing > 0.0f ? 0.0f : 3.14159265358979f;
    glm::vec3 vel{impulseDir.x * 4.0f, 4.5f, impulseDir.y * 4.0f};
    glm::vec3 angVel{0.0f, 3.0f, -impulseDir.x * 12.0f};
    int id = m_physics->addDebris(center, yaw, b.half, vel, angVel);
    m_pieces.push_back({victim, limb, id});
}

glm::mat4 Game::severedPieceTransform(const SeveredPiece& piece) const {
    return m_physics->debrisTransform(piece.debrisId);
}

// Bursts `count` droplets from `pos`, biased along `dir` with random spread.
void Game::spawnBlood(const glm::vec3& pos, const glm::vec3& dir, int count,
                      float speed) {
    for (int n = 0; n < count && m_blood.size() < kMaxBloodParticles; ++n) {
        glm::vec3 spread{frand() - 0.5f, frand() - 0.35f, frand() - 0.5f};
        glm::vec3 vel = dir * (speed * (0.4f + 0.8f * frand())) + spread * (1.1f * speed);
        m_blood.push_back({pos + spread * 0.2f, vel,
                           kBloodLife * (0.6f + 0.5f * frand()),
                           0.04f + 0.05f * frand()});
    }
}

void Game::addBloodMark(const glm::vec3& pos, float radius) {
    // Each mark gets its own tiny y offset so overlapping splats don't
    // z-fight; all stay below the blob shadows (y = 0.03).
    BloodMark mark{{pos.x, 0.006f + 0.010f * frand(), pos.z},
                   radius,
                   frand() * 6.2831853f,
                   0.55f + 0.35f * frand()};
    if (m_bloodMarks.size() < kMaxBloodMarks) {
        m_bloodMarks.push_back(mark);
    } else {
        m_bloodMarks[m_bloodMarkCursor] = mark;
        m_bloodMarkCursor = (m_bloodMarkCursor + 1) % kMaxBloodMarks;
    }
}

float Game::frand() {
    m_rng = m_rng * 1664525u + 1013904223u;
    return static_cast<float>(m_rng >> 8) * (1.0f / 16777216.0f);
}
