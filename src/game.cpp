#include "game.hpp"

#include "levels/level.hpp" // static obstacle boxes for the chosen battleground
#include "physics.hpp"
#include "samurai.hpp" // limb bounds double as gameplay hit regions

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
// FNV-1a, for Game::checksum. Floats go in as their bits: a determinism check
// wants bit-exactness, and hashing the value would let 0.0f and -0.0f — or two
// NaNs from different arithmetic — read as agreement.
struct Hasher {
    std::uint32_t h = 2166136261u;
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h = (h ^ b[i]) * 16777619u;
        }
    }
    void f(float v) { bytes(&v, sizeof v); }
    void u(std::uint32_t v) { bytes(&v, sizeof v); }
    void i(int v) { u(static_cast<std::uint32_t>(v)); }
    void b(bool v) { u(v ? 1u : 0u); }
    void v3(const glm::vec3& v) { f(v.x); f(v.y); f(v.z); }
};

// Fighter tuning that used to live here (move/jump speed, swing timings,
// reach, knockback) is now per-character: see character.cpp's roster.
constexpr float kGravity = 28.0f;         // m/s^2
constexpr float kAttackMoveScale = 0.35f; // movement slowdown while swinging
constexpr float kBlockMoveScale = 0.55f;  // movement slowdown while the guard is up

// Crouching: held input ducks the whole upper body kCrouchDrop meters at full
// depth — pose, hurtboxes, and the blade pivot all ride crouchAmount, so a
// crouched fighter is genuinely smaller and their swings sweep a lower lane
// (a leg-hunting stance). Legs fold rather than drop: their hit boxes squash
// toward the floor by the same fraction the model compresses them. The drop
// and the leg pivot height must mirror samurai.cpp's crouch numbers so the
// cut lands where the body is drawn.
constexpr float kCrouchMoveScale = 0.45f; // movement slowdown while crouched
constexpr float kCrouchDrop = 0.45f;      // upper-body y drop at full crouch
constexpr float kCrouchRate = 9.0f;       // crouchAmount approach speed, 1/s
constexpr float kHipHeight = 0.85f;       // leg pivot height (samurai.cpp legs)

// Per-attack tuning, indexed by AttackKind. Each attack scales the wielder's
// resolved stats (character + weapon) rather than replacing them, the same
// contract weapons follow — a fast character jabs faster than a slow one.
// The arc angles are about the shoulder (0 = hanging down, positive =
// forward/up) and must mirror samurai.cpp's swordArmAngle so the cut lands
// where the blade is drawn: light/heavy chop overhead-to-front, the jab
// snaps a short arc to horizontal — a forward thrust.
struct AttackTuning {
    float windupScale, activeScale, recoveryScale; // on the character's phase times
    float damageScale;    // on torso-hit and sever blood costs (never the beheading)
    float knockbackScale; // on shove dealt and upward pop
    float hitstunScale;   // on the victim's control lockout
    float startAngle, endAngle; // rad, Active-phase blade arc
    bool canSever;        // false = a limb/head connect lands as a torso hit
};
constexpr AttackTuning kAttackTuning[kAttackKindCount] = {
    // Light: the baseline — exactly the pre-attack-types swing.
    {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.60f, 0.55f, true},
    // Heavy: wound up well past overhead, slow everywhere, ruinous on connect.
    {1.9f, 1.25f, 1.6f, 2.4f, 1.9f, 1.4f, 2.95f, 0.45f, true},
    // Jab: quick poke, cheap cut, barely a shove; can't dismember.
    {0.45f, 0.55f, 0.55f, 0.5f, 0.35f, 0.6f, 1.20f, 1.55f, false},
};

// Blade sweep, mirroring the model's Active-phase swing (samurai.cpp
// swordArmAngle case 2): the arm pivots at the shoulder along the attack's
// arc (kAttackTuning angles), with the blade a segment along the arm
// direction. The tip's distance from the shoulder is the character's reach.
constexpr float kShoulderHeight = 1.36f;  // above the feet
constexpr float kShoulderSide = 0.26f;    // sword arm's z offset from center
constexpr float kBladeRoot = 0.45f;       // blade segment start, distance from shoulder
constexpr float kHandHeight = 0.84f;      // resting hand (samurai.cpp), where a blade leaves

// Throwing the blade down, and taking one back up. Deliberately generous on
// range and stingy on timing: fumbling for a sword at your feet under a swing
// is the drama, hunting for the exact pixel it landed on is not. The range
// itself is Game::kPickupRange — public, because the bot has to walk to one.
//
// The settle time is what stops the control being a no-op — press it twice in
// a frame or two and you would otherwise still be armed, having only made a
// noise. It also gives the blade a moment to actually leave the hand before
// the fighter who threw it (or the one standing over them) can claim it.
constexpr float kPickupHeight = 1.60f;  // m of vertical slack from the body center
constexpr float kDropSettle = 0.45f;    // s a thrown blade is untouchable
constexpr float kDropToss = 1.8f;       // m/s forward on the throw
constexpr float kDropLift = 1.4f;       // m/s upward on the throw
constexpr float kDropSpin = 6.0f;       // rad/s end over end

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

// Blood costs (the pool is Player::kMaxBlood = 100). Deliberately stingy:
// attrition is a slow background pressure, not the way matches end — the
// head is the execution, and beheading empties the pool outright. Stumps
// bleed continuously, so a limbless fighter still dies eventually, but
// you're meant to finish them, not wait them out.
constexpr float kTorsoHitBlood = 3.0f;   // per clean torso hit (~34 to kill)
constexpr float kSeverBloodCost = 8.0f;  // per limb taken
constexpr float kBleedPerLimb = 0.25f;   // blood/s drained per missing limb

constexpr float kKnockbackPop = 3.0f;   // upward pop on hit (divided by weight)
constexpr float kKnockbackDecay = 6.0f; // 1/s exponential decay
constexpr float kHitstunTime = 0.35f;

// Blocking. A raised guard catches any attack from the front: no blood, no
// cut — just a clang, a brief stagger, and a fraction of the shove leaking
// through. The heavy still moves a blocker plenty via its knockback scale.
constexpr float kBlockKnockbackScale = 0.35f; // on the shove that leaks through
constexpr float kBlockstunTime = 0.12f;       // stagger on catching a blow

// Riposte: catching a blow opens a counter window while the attacker is
// still stuck in their swing — an attack started inside it comes out with a
// fraction of its windup. Heavier caught blows leave longer openings (the
// window scales with the caught attack's hitstunScale), and taking a real
// hit erases any opening earned.
constexpr float kRiposteWindow = 0.45f;       // s, before the hitstunScale factor
constexpr float kRiposteWindupScale = 0.35f;  // on the riposte swing's windup

// An attack press made while the fighter can't yet swing (blockstun after a
// catch, tail of hitstun, recovery) waits this long instead of dropping —
// without it, clicking at the clang would eat the riposte it just earned.
constexpr float kAttackBufferTime = 0.15f;

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

// A limb's model-local bounds with the player's crouch applied: arms and the
// head drop with the upper body, legs squash toward the floor by the same
// fraction the model compresses them.
LimbBounds crouchedLimbBounds(const Player& p, int limb) {
    LimbBounds b = samuraiLimbBounds(limb);
    const float drop = p.crouchAmount * kCrouchDrop;
    if (drop <= 0.0f) {
        return b;
    }
    if (limb == static_cast<int>(Limb::LegFront) ||
        limb == static_cast<int>(Limb::LegBack)) {
        const float squash = (kHipHeight - drop) / kHipHeight;
        b.center.y *= squash;
        b.half.y *= squash;
    } else {
        b.center.y -= drop;
    }
    return b;
}

float attackDuration(const CharacterStats& st, AttackKind kind, AttackState state) {
    const AttackTuning& tun = kAttackTuning[static_cast<int>(kind)];
    switch (state) {
        case AttackState::Windup: return st.windupTime * tun.windupScale;
        case AttackState::Active: return st.activeTime * tun.activeScale;
        case AttackState::Recovery: return st.recoveryTime * tun.recoveryScale;
        default: return 1.0f;
    }
}
} // namespace

Game::Game(int p0Character, int p0Weapon, int p1Character, int p1Weapon, int level)
    : m_level(level) {
    m_defs[0] = &characterDef(p0Character);
    m_defs[1] = &characterDef(p1Character);
    // The weapon each fighter starts the match with. equip bakes it into their
    // stats; the sim (and the bot, via stats()) reads the resolved numbers and
    // never re-applies the weapon itself — which is what lets a blade change
    // hands mid-match without anything downstream noticing.
    equip(0, p0Weapon);
    equip(1, p1Weapon);
    m_players[0].pos = {-3.0f, Player::kHalfHeight, 0.0f};
    m_players[1].pos = {3.0f, Player::kHalfHeight, 0.0f};
    m_players[0].facing = 1.0f;
    m_players[1].facing = -1.0f;
    // Physics characters are positioned by their feet; ground surface is y = 0.
    // The level dictates the arena's width (side-wall placement), its
    // obstacle boxes (stones, trunks, the house) become static colliders
    // alongside the arena's ground and walls, its ground boxes replace the
    // flat slab (Hanami's carved stream channel), and its water volume gives
    // debris buoyancy and a downstream current.
    m_arenaHalfWidth = levelDef(level).arenaHalfWidth;
    std::vector<Physics::StaticBox> statics;
    for (const LevelObstacle& o : levelObstacles(level)) {
        statics.push_back({o.center, o.halfExtent});
    }
    std::vector<Physics::StaticBox> ground;
    for (const LevelObstacle& g : levelGround(level)) {
        ground.push_back({g.center, g.halfExtent});
    }
    Physics::Water water;
    if (const LevelWater* w = levelWater(level)) {
        water = {w->min, w->max, w->current};
        m_hasWater = true;
        m_waterMinXZ = {w->min.x, w->min.z};
        m_waterMaxXZ = {w->max.x, w->max.z};
    }
    m_physics = std::make_unique<Physics>(kGravity, m_arenaHalfWidth,
                                          glm::vec3{-3.0f, 0.0f, 0.0f},
                                          glm::vec3{3.0f, 0.0f, 0.0f}, statics,
                                          ground, water);
}

Game::~Game() = default;

void Game::update(const PlayerInput inputs[2], float dt) {
    // Blades lying in the arena: count down the beat before each can be
    // claimed. Nothing else about them is simulated here — they are debris
    // bodies, and Jolt moves them with everything else in step().
    for (DroppedWeapon& blade : m_dropped) {
        blade.settle = std::max(0.0f, blade.settle - dt);
    }

    for (int i = 0; i < 2; ++i) {
        Player& p = m_players[i];
        // The dead take no input; the body still topples and gets shoved.
        const PlayerInput in = p.dead() ? PlayerInput{} : inputs[i];
        const CharacterStats& st = m_stats[i];

        if (p.hitstun > 0.0f) {
            p.hitstun = std::max(0.0f, p.hitstun - dt);
        }
        if (p.riposteTime > 0.0f) {
            p.riposteTime = std::max(0.0f, p.riposteTime - dt);
        }

        // Open stumps keep bleeding; this can decide a match on its own.
        if (!p.dead()) {
            int lost = 0;
            for (bool s : p.severed) {
                lost += s ? 1 : 0;
            }
            if (lost > 0) {
                p.blood = std::max(0.0f, p.blood - lost * kBleedPerLimb * dt);
                if (p.dead()) {
                    if (m_winner < 0) {
                        m_winner = 1 - i;
                    }
                    collapse(p);
                }
            }
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
                        p.attackTimer = attackDuration(st, p.attackKind, p.attackState);
                        break;
                    case AttackState::Active:
                        p.attackState = AttackState::Recovery;
                        p.attackTimer = attackDuration(st, p.attackKind, p.attackState);
                        break;
                    default:
                        p.attackState = AttackState::None;
                        p.attackTimer = 0.0f;
                        break;
                }
            }
        }
        // Throw the blade down, or take up whatever is lying at the feet. One
        // control for both, and which of the two it means is decided here
        // rather than at the keyboard: nothing outside the sim knows what is
        // in a fighter's hand, and after the first exchange that may not even
        // be the blade they chose. A press that lands mid-swing is dropped
        // rather than buffered — the blade is committed to the cut, and a
        // throw that goes off a moment late is worse than one that never
        // happened.
        if (in.drop && !p.dead() && p.hitstun <= 0.0f &&
            p.attackState == AttackState::None) {
            if (p.armed()) {
                dropWeapon(i);
            } else {
                takeUpWeapon(i);
            }
        }

        // Swinging needs a blade and an arm to hold it with; the model swaps
        // the katana to the off hand when the sword arm is gone. Throwing the
        // sword away is therefore the most expensive thing a fighter can do to
        // themselves — no attack, and no guard either, since it takes crossed
        // steel to catch a cut.
        bool hasArm = !p.severed[static_cast<int>(Limb::ArmFront)] ||
                      !p.severed[static_cast<int>(Limb::ArmBack)];
        bool canSwing = hasArm && p.armed();
        // Guard: held while standing and not mid-swing. It takes an arm to
        // hold the blade up, and it wins over an attack press — the sword is
        // committed to the guard.
        p.blocking = in.block && p.attackState == AttackState::None &&
                     !p.downed() && canSwing;
        // Crouch: held while grounded and upright. The pose (and every
        // crouch-dependent hurtbox) chases the held state instead of
        // snapping so ducking reads as a motion, not a teleport.
        p.crouching = in.crouch && p.grounded && !p.downed();
        const float crouchTarget = p.crouching ? 1.0f : 0.0f;
        p.crouchAmount = p.crouchAmount < crouchTarget
                             ? std::min(crouchTarget, p.crouchAmount + kCrouchRate * dt)
                             : std::max(crouchTarget, p.crouchAmount - kCrouchRate * dt);
        // A fresh press (re)arms the buffer; otherwise it counts down. The
        // swing fires from the buffer the first tick the fighter is able.
        if (in.attack) {
            p.attackBuffer = kAttackBufferTime;
            p.bufferedKind = in.attackKind;
        } else if (p.attackBuffer > 0.0f) {
            p.attackBuffer = std::max(0.0f, p.attackBuffer - dt);
        }
        if (p.attackState == AttackState::None && p.hitstun <= 0.0f &&
            p.attackBuffer > 0.0f && canSwing && !p.blocking) {
            p.attackState = AttackState::Windup;
            p.attackKind = p.bufferedKind;
            p.attackBuffer = 0.0f;
            // Started inside the post-block window this swing is a riposte:
            // it snaps out with a fraction of the windup. Window consumed.
            p.attackWindupScale =
                p.riposteTime > 0.0f ? kRiposteWindupScale : 1.0f;
            p.riposteTime = 0.0f;
            p.attackTimer = attackDuration(st, p.attackKind, p.attackState) *
                            p.attackWindupScale;
            p.attackLanded = false;
            // The whoosh's swell is tuned to peak right as the blade goes
            // active; the jab's is quieter to match the shorter cut.
            m_soundCues.push_back(
                {Sfx::Swing, p.pos.x,
                 p.attackKind == AttackKind::Jab ? 0.7f : 1.0f});
        }
        // Phase progress; the windup's duration carries the riposte scale so
        // a riposte's pose still sweeps 0..1, just faster.
        float phaseLen = attackDuration(st, p.attackKind, p.attackState);
        if (p.attackState == AttackState::Windup) {
            phaseLen *= p.attackWindupScale;
        }
        p.attackT = p.attackState == AttackState::None
                        ? 0.0f
                        : std::clamp(1.0f - p.attackTimer / phaseLen, 0.0f, 1.0f);

        // Movement: locked in hitstun, slowed while swinging.
        glm::vec2 move = p.hitstun > 0.0f ? glm::vec2{0.0f} : in.move;
        float len = glm::length(move);
        if (len > 1.0f) {
            move /= len; // diagonal movement is not faster
        }
        float speedScale = p.attackState != AttackState::None ? kAttackMoveScale : 1.0f;
        if (p.blocking) {
            speedScale *= kBlockMoveScale;
        }
        if (p.crouching) {
            speedScale *= kCrouchMoveScale;
        }
        if (p.downed()) {
            if (p.fallTilt < kMaxTilt) {
                speedScale = 0.0f; // mid-fall: no footing at all
            } else {
                int arms = (p.severed[static_cast<int>(Limb::ArmFront)] ? 0 : 1) +
                           (p.severed[static_cast<int>(Limb::ArmBack)] ? 0 : 1);
                speedScale *= kCrawlSpeed[arms];
            }
        }
        glm::vec2 planarVel = move * (st.moveSpeed * speedScale) + p.kbVel;
        p.kbVel *= std::exp(-kKnockbackDecay * dt);

        p.moveAmount = glm::length(move) * speedScale;
        p.animPhase = std::fmod(p.animPhase + p.moveAmount * 12.0f * dt,
                                2.0f * 3.14159265358979f);

        if (p.grounded) {
            p.vy = 0.0f;
            if (in.jump && p.hitstun <= 0.0f && !p.downed() && !p.crouching) {
                p.vy = st.jumpVelocity;
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
        // A piece hitting the stream bed splashes rather than smears — no
        // mark, no droplet burst (addBloodMark would drop the mark anyway).
        // And only limbs bleed: a thrown-down blade clatters and lies there,
        // which is the whole reason an impact says which body it came from.
        if (impact.pos.y < 0.15f && !inWater(impact.pos.x, impact.pos.z) &&
            isLimbDebris(impact.id)) {
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
            addBloodMark({d.pos.x, 0.0f, d.pos.z}, 0.05f + 0.10f * cfrand());
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
        const CharacterStats& st = m_stats[i];
        const CharacterStats& foeSt = m_stats[1 - i];
        if (p.attackState != AttackState::Active || p.attackLanded) {
            continue;
        }

        // Both the blade arc and the target boxes follow each body's topple
        // roll: a downed attacker's chop becomes a sweep along the ground,
        // and a downed defender is hit where the body actually lies.
        const AttackTuning& tun = kAttackTuning[static_cast<int>(p.attackKind)];
        const float swordSide =
            p.severed[static_cast<int>(Limb::ArmFront)] ? -1.0f : 1.0f;
        // A crouched attacker's shoulder (and so the whole sweep) rides lower.
        const glm::vec3 pivot = modelToWorld(
            p, {0.0f, kShoulderHeight - p.crouchAmount * kCrouchDrop,
                swordSide * kShoulderSide});
        const float tCur = p.attackT;
        const float tPrev =
            std::max(0.0f, tCur - dt / (st.activeTime * tun.activeScale));

        int hitLimb = -1;
        bool hitTorso = false;
        constexpr int kSweepSamples = 4;
        for (int k = 1; k <= kSweepSamples && hitLimb < 0; ++k) {
            float t = glm::mix(tPrev, tCur, static_cast<float>(k) / kSweepSamples);
            float angle = glm::mix(tun.startAngle, tun.endAngle, t);
            glm::vec3 dir = rollLocal(p, {std::sin(angle), -std::cos(angle), 0.0f});
            dir = {p.facing * dir.x, dir.y, p.facing * dir.z};
            glm::vec3 s0 = pivot + dir * kBladeRoot;
            glm::vec3 s1 = pivot + dir * st.reach;

            // Of the limbs this sample crosses, cut the one whose depth lane
            // best matches the blade's.
            float bestZ = 1e9f;
            for (int l = 0; l < kLimbCount; ++l) {
                if (foe.severed[l]) {
                    continue;
                }
                LimbBounds b = crouchedLimbBounds(foe, l);
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
                glm::vec3 torsoCenter = modelToWorld(
                    foe,
                    {0.0f, kTorsoCenterY - foe.crouchAmount * kCrouchDrop, 0.0f});
                hitTorso = segmentHitsBox(s0, s1, torsoCenter,
                                          rollHalfExtent(foe, kTorsoHalf));
            }
        }
        if (hitLimb < 0 && !hitTorso) {
            continue;
        }
        // A thrust pierces instead of cutting: a jab that crosses a limb (or
        // the head) still connects, but lands as a torso-grade hit — no
        // dismemberment, and no cut-rate execution.
        if (!tun.canSever && hitLimb >= 0) {
            hitLimb = -1;
            hitTorso = true;
        }
        p.attackLanded = true;
        glm::vec2 dir{foe.pos.x - p.pos.x, foe.pos.z - p.pos.z};
        float dist = glm::length(dir);
        dir = dist > 1e-4f ? dir / dist : glm::vec2{p.facing, 0.0f};

        // A raised guard catches anything from the front (facing tracks the
        // opponent, so in practice: anything not landed from behind). The
        // blade never touches flesh — no blood, no sever, no execution —
        // but part of the shove still comes through the crossed steel.
        if (foe.blocking && (p.pos.x - foe.pos.x) * foe.facing >= 0.0f) {
            foe.hitstun = kBlockstunTime;
            foe.kbVel = dir * (st.knockback * tun.knockbackScale *
                               kBlockKnockbackScale / foeSt.weight);
            // The catch opens the counter window: the attacker is committed
            // to the rest of their swing, and a heavier caught blow leaves a
            // longer opening.
            foe.riposteTime = kRiposteWindow * tun.hitstunScale;
            m_soundCues.push_back({Sfx::Block, foe.pos.x});
            continue;
        }

        foe.hitstun = kHitstunTime * tun.hitstunScale;
        foe.riposteTime = 0.0f; // a clean hit erases any opening they'd earned
        foe.attackState = AttackState::None; // a clean hit interrupts the foe's swing
        foe.attackTimer = 0.0f;
        // Heavier characters shrug off more of the shove; harder hitters
        // deal more of it. Both read straight from the roster stats, scaled
        // by how much weight the attack itself carries.
        foe.kbVel = dir * (st.knockback * tun.knockbackScale / foeSt.weight);
        foe.vy = std::max(foe.vy, kKnockbackPop * tun.knockbackScale / foeSt.weight);
        foe.grounded = false;
        m_soundCues.push_back({hitLimb >= 0 ? Sfx::Dismember : Sfx::Hit, foe.pos.x});

        // The cut costs blood: a chunk for the torso, more for a limb, the
        // whole pool for the head. The weapon's damage scale and the attack's
        // price the first two, but never the head — beheading executes with
        // any blade. Chopping at a corpse still severs and sprays but can't
        // re-decide the match.
        const bool wasDead = foe.dead();
        // Armed by construction — nothing can start a swing empty-handed, and
        // nothing can disarm mid-swing — but the blade is state now, so read
        // it as state rather than trusting a pointer three screens away.
        const float dmg =
            (m_weapons[i] ? m_weapons[i]->stats.damage : 1.0f) * tun.damageScale;
        const float cost = hitTorso ? kTorsoHitBlood * dmg
                           : hitLimb == static_cast<int>(Limb::Head)
                               ? Player::kMaxBlood
                               : kSeverBloodCost * dmg;
        foe.blood = std::max(0.0f, foe.blood - cost);

        // Blood sprays from the wound, away from the attacker and upward;
        // dismemberment gushes harder than a body hit.
        glm::vec3 wound = modelToWorld(
            foe, {0.0f, kTorsoCenterY - foe.crouchAmount * kCrouchDrop, 0.0f});
        if (hitLimb >= 0) {
            wound = modelToWorld(foe, crouchedLimbBounds(foe, hitLimb).center);
        }
        glm::vec3 spray = glm::normalize(glm::vec3{dir.x, 1.2f, dir.y});
        spawnBlood(wound, spray, hitLimb >= 0 ? kSeverBloodCount : kHitBloodCount,
                   hitLimb >= 0 ? kSeverBloodSpeed : kHitBloodSpeed);

        if (hitLimb >= 0) {
            severLimb(1 - i, static_cast<Limb>(hitLimb), dir);
        }
        if (!wasDead && foe.dead()) {
            if (m_winner < 0) {
                m_winner = i;
            }
            collapse(foe);
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

    if (m_winner >= 0) {
        m_overTime += dt;
    } else {
        // Run the clock last, so a kill landed on this very step is the thing
        // that decided the match rather than the expiry that happened
        // alongside it.
        m_timeLeft = std::max(0.0f, m_timeLeft - dt);
        if (m_timeLeft <= 0.0f) {
            decideOnTime();
        }
    }
}

// Time. Nobody died, so the match goes to whoever came closest: blood first,
// since that is what the whole duel is spent taking, then limbs, since a
// fighter who kept all four while the other lost one was plainly winning.
//
// The last tie-break is the fighter index, which is arbitrary and is meant to
// be — it can only be reached when the two are identical to the bit, which in
// practice means nothing happened for ninety-nine seconds. Something has to be
// returned, and a draw would be a whole outcome for the win overlay, the
// rematch flow and the netplay level ownership to learn for that one case.
void Game::decideOnTime() {
    auto lost = [](const Player& p) {
        int n = 0;
        for (bool s : p.severed) {
            n += s ? 1 : 0;
        }
        return n;
    };
    const Player& a = m_players[0];
    const Player& b = m_players[1];
    if (a.blood != b.blood) {
        m_winner = a.blood > b.blood ? 0 : 1;
    } else if (lost(a) != lost(b)) {
        m_winner = lost(a) < lost(b) ? 0 : 1;
    } else {
        m_winner = 0;
    }
    m_timedOut = true;
    // Nobody is killed by the bell: both fighters are left standing exactly as
    // they were, and only the swing in flight is cut short so the overlay does
    // not come up over a blade frozen mid-arc.
    for (Player& p : m_players) {
        p.attackState = AttackState::None;
        p.attackTimer = 0.0f;
    }
}

// Death drops the fighter where they stand: any swing dies with them, and if
// the body isn't already toppling it starts now (dead legs don't hold) — the
// existing topple integrator carries it the rest of the way to the floor.
void Game::collapse(Player& p) {
    p.attackState = AttackState::None;
    p.attackTimer = 0.0f;
    if (p.fallSide == 0.0f) {
        p.fallSide = frand() < 0.5f ? 1.0f : -1.0f;
        p.fallVel = kToppleKick;
    }
}

// Puts a blade (or nothing) in a fighter's hand and re-resolves their stats
// from it. The only place m_weapons and m_stats are written, so the invariant
// "stats are the character's with the held weapon applied" holds by
// construction however often the blade changes.
void Game::equip(int i, int weapon) {
    m_players[i].weapon = weapon;
    m_weapons[i] = weapon == Player::kUnarmed ? nullptr : &weaponDef(weapon);
    CharacterStats st = m_defs[i]->stats;
    if (m_weapons[i]) {
        const WeaponStats& w = m_weapons[i]->stats;
        st.windupTime *= w.swingScale;
        st.activeTime *= w.swingScale;
        st.recoveryTime *= w.swingScale;
        st.reach += w.reachBonus;
        st.knockback *= w.knockbackScale;
    }
    // Empty-handed leaves the bare character stats standing. Nothing scales
    // them, because nothing uses them: an unarmed fighter cannot swing at all.
    m_stats[i] = st;
}

// Throws the blade out of a fighter's hand and into the world as a debris
// body, tumbling forward. From here it belongs to nobody — either fighter can
// take it up, which is the point of being allowed to throw one away.
void Game::dropWeapon(int i) {
    Player& p = m_players[i];
    if (!p.armed()) {
        return;
    }
    const int weapon = p.weapon;
    equip(i, Player::kUnarmed);

    // Leaves from the hand that was holding it, ducking with the crouch the
    // way the drawn blade does, so the throw starts where the sword was.
    const float swordSide =
        p.severed[static_cast<int>(Limb::ArmFront)] ? -1.0f : 1.0f;
    const glm::vec3 hand = modelToWorld(
        p, {0.0f, kHandHeight - p.crouchAmount * kCrouchDrop,
            swordSide * kShoulderSide});
    const float steel = bladeSteelLength(weaponDef(weapon).stats.reachBonus);
    const glm::vec3 vel{p.facing * kDropToss, kDropLift, 0.0f};
    const glm::vec3 angVel{0.0f, 0.0f, -p.facing * kDropSpin};
    const int id = m_physics->addDebris(hand, p.facing > 0.0f ? 0.0f : 3.14159265358979f,
                                        droppedBladeHalfExtent(steel), vel, angVel);
    m_dropped.push_back({weapon, id, kDropSettle});
}

// Takes up the nearest settled blade within reach. Nearest rather than first
// so two blades at the same feet resolve the way the player expects, and by
// planar distance because depth is where the fight is fought — a blade a
// fighter is standing over is in reach whether or not they are mid-jump above
// it, but one on a bank a metre up is not.
bool Game::takeUpWeapon(int i) {
    Player& p = m_players[i];
    // It takes a hand. A fighter who lost both arms dropped the blade for
    // exactly that reason and is not going to pick it back up.
    if (p.armed() || (p.severed[static_cast<int>(Limb::ArmFront)] &&
                      p.severed[static_cast<int>(Limb::ArmBack)])) {
        return false;
    }
    int best = -1;
    float bestDist = kPickupRange;
    for (std::size_t n = 0; n < m_dropped.size(); ++n) {
        if (m_dropped[n].settle > 0.0f) {
            continue;
        }
        const glm::vec3 at = droppedWeaponPos(m_dropped[n]);
        if (std::abs(at.y - p.pos.y) > kPickupHeight) {
            continue;
        }
        const float dist = glm::length(glm::vec2{at.x - p.pos.x, at.z - p.pos.z});
        if (dist < bestDist) {
            bestDist = dist;
            best = static_cast<int>(n);
        }
    }
    if (best < 0) {
        return false;
    }
    equip(i, m_dropped[best].weapon);
    m_physics->removeDebris(m_dropped[best].debrisId);
    m_dropped.erase(m_dropped.begin() + best);
    // Steel coming off the ground, borrowing the block's clang at half its
    // weight — the same sound a blade makes meeting anything else.
    m_soundCues.push_back({Sfx::Block, p.pos.x, 0.5f});
    return true;
}

bool Game::isLimbDebris(int debrisId) const {
    for (const SeveredPiece& piece : m_pieces) {
        if (piece.debrisId == debrisId) {
            return true;
        }
    }
    return false;
}

glm::mat4 Game::droppedWeaponTransform(const DroppedWeapon& blade) const {
    return m_physics->debrisTransform(blade.debrisId);
}

glm::vec3 Game::droppedWeaponPos(const DroppedWeapon& blade) const {
    return glm::vec3(m_physics->debrisTransform(blade.debrisId)[3]);
}

// Marks the limb lost and launches it as a debris rigid body along the hit
// direction, spinning end over end.
void Game::severLimb(int victim, Limb limb, const glm::vec2& impulseDir) {
    Player& v = m_players[victim];
    v.severed[static_cast<int>(limb)] = true;

    // With both arms gone there is nothing left to hold a sword with, so the
    // blade goes down whether its owner wanted it to or not. Done before the
    // limb is launched so the two pieces do not spawn on top of each other.
    if (v.severed[static_cast<int>(Limb::ArmFront)] &&
        v.severed[static_cast<int>(Limb::ArmBack)]) {
        dropWeapon(victim);
    }

    // Losing a leg starts the topple, toward the lost leg's side. If the
    // other leg goes later the body is already down (or falling) — keep the
    // direction it committed to.
    if ((limb == Limb::LegFront || limb == Limb::LegBack) && v.fallSide == 0.0f) {
        v.fallSide = limb == Limb::LegFront ? 1.0f : -1.0f;
        v.fallVel = kToppleKick;
    }

    // The debris body keeps the limb's full-size half extents (that's the
    // shape drawSeveredLimb draws) but spawns where the crouched limb sits.
    LimbBounds b = samuraiLimbBounds(static_cast<int>(limb));
    glm::vec3 center = modelToWorld(v, crouchedLimbBounds(v, static_cast<int>(limb)).center);
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
        glm::vec3 spread{cfrand() - 0.5f, cfrand() - 0.35f, cfrand() - 0.5f};
        glm::vec3 vel = dir * (speed * (0.4f + 0.8f * cfrand())) + spread * (1.1f * speed);
        m_blood.push_back({pos + spread * 0.2f, vel,
                           kBloodLife * (0.6f + 0.5f * cfrand()),
                           0.04f + 0.05f * cfrand()});
    }
}

void Game::addBloodMark(const glm::vec3& pos, float radius) {
    if (inWater(pos.x, pos.z)) {
        return; // washed away downstream
    }
    // Each mark gets its own tiny y offset so overlapping splats don't
    // z-fight; all stay below the blob shadows (y = 0.03).
    BloodMark mark{{pos.x, 0.006f + 0.010f * cfrand(), pos.z},
                   radius,
                   cfrand() * 6.2831853f,
                   0.55f + 0.35f * cfrand()};
    if (m_bloodMarks.size() < kMaxBloodMarks) {
        m_bloodMarks.push_back(mark);
    } else {
        m_bloodMarks[m_bloodMarkCursor] = mark;
        m_bloodMarkCursor = (m_bloodMarkCursor + 1) % kMaxBloodMarks;
    }
}

std::uint32_t Game::checksum() const {
    Hasher hs;
    for (const Player& p : m_players) {
        hs.v3(p.pos);
        hs.f(p.vy);
        hs.b(p.grounded);
        hs.f(p.facing);
        hs.f(p.animPhase);
        hs.f(p.moveAmount);
        hs.i(static_cast<int>(p.attackState));
        hs.i(static_cast<int>(p.attackKind));
        hs.f(p.attackTimer);
        hs.f(p.attackT);
        hs.b(p.attackLanded);
        hs.f(p.hitstun);
        hs.b(p.blocking);
        hs.b(p.crouching);
        hs.f(p.crouchAmount);
        hs.f(p.riposteTime);
        hs.f(p.attackWindupScale);
        hs.f(p.attackBuffer);
        hs.i(static_cast<int>(p.bufferedKind));
        hs.f(p.kbVel.x);
        hs.f(p.kbVel.y);
        for (bool s : p.severed) {
            hs.b(s);
        }
        // What is in the hand decides every number the next swing uses, so it
        // has to be here — two peers holding different blades would otherwise
        // agree right up until one of them cut.
        hs.i(p.weapon);
        hs.f(p.blood);
        hs.f(p.fallTilt);
        hs.f(p.fallVel);
        hs.f(p.fallSide);
    }
    hs.u(m_rng);
    hs.u(m_cosmeticRng);
    hs.i(m_winner);
    hs.f(m_overTime);
    // The clock decides a match, so two peers disagreeing about it by one step
    // would end the duel differently — it is sim state, not a HUD number.
    hs.f(m_timeLeft);
    hs.b(m_timedOut);
    // Blades on the ground. Their positions ride in the debris states below;
    // what is hashed here is which blade each one *is* and whether it can yet
    // be claimed — a settle timer that disagreed would have one peer take up a
    // sword the other could not.
    hs.i(static_cast<int>(m_dropped.size()));
    for (const DroppedWeapon& blade : m_dropped) {
        hs.i(blade.weapon);
        hs.i(blade.debrisId);
        hs.f(blade.settle);
    }
    // Debris is created only by severLimb, so two runs that agree up to here
    // have the same pieces in the same order and the lists line up index for
    // index. A piece drifting apart shows up here a long time before it can
    // change a blood mark and drag the cosmetic rng with it.
    std::vector<Physics::DebrisState> debris;
    m_physics->debrisStates(debris);
    hs.i(static_cast<int>(debris.size()));
    for (const Physics::DebrisState& d : debris) {
        hs.v3(d.pos);
        hs.f(d.rot.x);
        hs.f(d.rot.y);
        hs.f(d.rot.z);
        hs.f(d.rot.w);
        hs.v3(d.vel);
        hs.v3(d.angVel);
    }
    return hs.h;
}

float Game::frand() {
    m_rng = m_rng * 1664525u + 1013904223u;
    return static_cast<float>(m_rng >> 8) * (1.0f / 16777216.0f);
}

float Game::cfrand() {
    m_cosmeticRng = m_cosmeticRng * 1664525u + 1013904223u;
    return static_cast<float>(m_cosmeticRng >> 8) * (1.0f / 16777216.0f);
}
