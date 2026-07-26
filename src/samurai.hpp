#pragma once

#include "weapons/stance.hpp" // the arcs the sword arm animates along

#include <glm/glm.hpp>

class Renderer;

struct SamuraiColors {
    glm::vec4 kimono; // torso, arms, shoulder plates
    glm::vec4 hakama; // trousers, hip skirt, collar
    glm::vec4 accent; // obi belt, sword grip wrap
};

// Stance and stride geometry. The model's own dimensions, public because the
// sim moves the body against them and both ends have to read one copy — two
// would drift, and the symptom would be exactly what they exist to prevent:
// feet skating over the floor.
inline constexpr float kHipPivotY = 0.85f;  // hip joint height, standing tall
inline constexpr float kFootY = 0.05f;      // sandal center height
inline constexpr float kKneePivotY = 0.45f; // knee joint height, standing tall
inline constexpr float kThighLen = kHipPivotY - kKneePivotY;
inline constexpr float kShinLen = kKneePivotY - kFootY;
inline constexpr float kLegLength = kThighLen + kShinLen;

// A fighter with a sword in their hands does not walk. They hold a stance and
// shuffle it along: one foot always leads, the feet never cross, and a step is
// the lead reaching out and the rear closing up after it. So the legs are
// placed by where the *feet* need to be and solved back to joint angles,
// rather than swung from the hip and hoped over — which also means the stance
// is what it is because a leg is a fixed-length lever. A hip at full standing
// height can only reach the floor straight down, so holding the feet any
// distance apart at all requires settling onto bent knees. These three numbers
// are locked together by that: widen the stance or lengthen the step and the
// hip has to come down to keep both feet on the ground.
inline constexpr float kStanceHipY = 0.70f;    // hip height in the guard
inline constexpr float kStanceSep = 0.34f;     // lead foot ahead of rear
inline constexpr float kShuffleStride = 0.55f; // ground covered per cycle
inline constexpr float kFootLift = 0.07f;      // peak height of a moving foot
inline constexpr float kLegSide = 0.12f;       // a leg's z offset from center
// How far the whole body sits below a fighter standing tall. Everything from
// the hip up rides down by this, so every *sim* height authored against the
// old upright model — shoulder, hand, torso, arm and head hurtboxes — is that
// value minus this. Without it the drawn blade sweeps 15 cm under the arc it
// actually cuts along, and the head you aim at is not the head you must hit.
inline constexpr float kStanceDrop = kHipPivotY - kStanceHipY;
inline constexpr float kCrouchDrop = 0.45f; // further drop at full crouch

// One leg, solved. The feet are placed by the shuffle and the joints fall out
// of them, so this is the single source both ends read: samurai.cpp draws from
// it and game.cpp builds the leg hurtbox from it. Two copies of this would
// drift, and the fighter would be hit where their legs are not.
struct LegPose {
    glm::vec3 hipAt, kneeAt, footAt; // model-local joint centers
};

// `side` is the leg's z sign: +1 is the lead (sword-side) leg, which is the
// front foot and stays the front foot. `dir` is the direction of travel in
// model-local space (x forward, y = z), and decides which foot moves first —
// never which one leads.
LegPose shuffleLeg(float phase, float strideBlend, glm::vec2 dir, float crouch,
                   bool grounded, float side);

struct SamuraiPose {
    float walkPhase = 0.0f;  // radians through the stride cycle
    float moveAmount = 0.0f; // 0..1 fraction of max ground speed; drives bob and lean
    // 0..1 ease into a full-length step (Player::strideBlend). Not a fraction
    // of speed: a slow fighter takes full steps less often, not short ones. It
    // only closes the *stepping* — the stance separation is held either way,
    // since a guard is a guard whether or not you are moving in it.
    float strideBlend = 0.0f;
    // Direction of travel in model-local space, x forward (Player::strideDir).
    // Says which foot goes first and which way the step is taken — never which
    // foot *leads*, which is the same one all match. A vector rather than a
    // sign because a duel is not fought along one axis: stepping in depth used
    // to slide the fighter sideways on legs doing a forward shuffle.
    glm::vec2 strideDir{1.0f, 0.0f};
    bool grounded = true;
    float time = 0.0f;       // seconds since start, for idle breathing
    int attackState = 0;     // mirrors game AttackState: 0 none, 1 windup, 2 active, 3 recovery
    int attackKind = 0;      // mirrors game AttackKind: 0 light, 1 heavy, 2 jab
    float attackT = 0.0f;    // 0..1 progress through the current attack phase
    bool blocking = false;   // guard up: the sword arm holds the blade at the foe
    float crouch = 0.0f;     // 0..1 duck depth: legs fold, upper body drops
    // Shoulder-to-tip length of the drawn katana, matching the fighter's
    // resolved reach (character + weapon) so what connects is what the
    // player sees. bladeWidth scales only the drawn blade's thickness —
    // a visual cue for the equipped weapon, no gameplay effect.
    float reach = 1.6f;
    float bladeWidth = 1.0f;
    // How the blade is carried: where the guard holds it, where a swing
    // settles back to, and the arc of the swing itself. Comes from the weapon
    // (via Game::stance), and is the one thing that makes an odachi's cleave
    // and a wakizashi's rising cut look like different swings — the sim sweeps
    // the very same angles, so this is not a rendering choice.
    Stance stance = Stance::Normal;
    // Is there a blade in hand at all? An armed fighter carries theirs drawn
    // for the whole match — held at the stance's ready angle when they are
    // doing nothing else with it — so this is what puts a sword on screen or
    // takes it off. A fighter who has thrown theirs down (or lost both arms)
    // gets an empty hand and an arm that swings with the stride again, which
    // is what makes "I am unarmed" readable at a glance. The saya at the hip
    // is empty either way and says nothing about it.
    bool armed = true;
    // Signed topple roll about the local +x axis at the feet: a fighter who
    // has lost a leg tips toward ±z and ends up lying on the ground.
    float bodyRoll = 0.0f;
    // Optional [5] dismemberment flags, indexed like samuraiLimbBounds. Severed
    // parts draw as stumps; if the +z (sword) arm is gone, the -z arm swings.
    const bool* severed = nullptr;
};

// Severable body parts are indexed 0..4: 0 arm +z (sword side), 1 arm -z,
// 2 leg +z, 3 leg -z, 4 head. Order must match game.hpp's Limb enum.
// Bounds are the limb's tight AABB in model-local space (feet origin,
// +x facing) — used for gameplay hit regions and debris collision shapes.
struct LimbBounds {
    glm::vec3 center;
    glm::vec3 half;
};
LimbBounds samuraiLimbBounds(int limb);

// Draws limb `limb` as a free-flying piece (with a blood-red cut cap), with
// its boxes centered on samuraiLimbBounds(limb).center so `transform` can be
// a rigid-body transform for a box of those half extents.
void drawSeveredLimb(Renderer& renderer, const glm::mat4& transform, int limb,
                     const SamuraiColors& colors);

// A blade as a loose object rather than something in a hand — thrown down, or
// waiting on the ground to be taken up.
//
// Its length comes from the weapon's reach *bonus* alone, never from a
// fighter's reach: a blade on the ground is the same object whoever picks it
// up, and one that grew or shrank as it changed hands would be a lie about
// what the next swing covers. The baseline is what drawSamurai puts in the
// hand of a 1.6 m-reach fighter carrying a katana.
float bladeSteelLength(float reachBonus);
// Half extents of the physics box for a blade of that steel length — grip
// included, laid along local +x with the tip at +x.
glm::vec3 droppedBladeHalfExtent(float steel);
// Draws that blade centered on its own origin, so `transform` can be a rigid
// body transform for a box of those half extents (the same contract
// drawSeveredLimb keeps). `width` is the weapon's visual thickness cue.
void drawDroppedBlade(Renderer& renderer, const glm::mat4& transform, float steel,
                      float width, const glm::vec4& grip);

// Draws a samurai assembled procedurally from shaded boxes: hakama legs,
// kimono torso, obi, sode shoulder plates, arms, head, straw kasa, an empty
// saya at the hip, and — while armed — the drawn blade itself. `feet` is the ground point under the character; `yaw`
// rotates about +Y (0 faces +x).
void drawSamurai(Renderer& renderer, const glm::vec3& feet, float yaw,
                 const SamuraiPose& pose, const SamuraiColors& colors);
