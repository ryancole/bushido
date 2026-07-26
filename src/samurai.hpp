#pragma once

#include "weapons/stance.hpp" // the arcs the sword arm animates along

#include <glm/glm.hpp>

class Renderer;

struct SamuraiColors {
    glm::vec4 kimono; // torso, arms, shoulder plates
    glm::vec4 hakama; // trousers, hip skirt, collar
    glm::vec4 accent; // obi belt, sword grip wrap
};

// Stride geometry. These are the model's own dimensions, public because the
// sim moves the body against them: a step covers whatever ground the leg
// actually reaches, so Game derives its walk cadence *and* its per-step
// velocity from these numbers rather than authoring a stride length of its
// own. Two copies of that arithmetic would drift, and the symptom would be
// exactly the thing they exist to prevent — feet skating over the floor.
inline constexpr float kHipPivotY = 0.85f;  // hip joint height (drawSamurai)
inline constexpr float kFootY = 0.05f;      // sandal center height
inline constexpr float kLegLength = kHipPivotY - kFootY; // hip → foot lever arm
inline constexpr float kStrideAngle = 0.55f; // max hip swing either side, radians
// One step covers 2·legLength·sin(amp) — the foot swinging from one extreme of
// the hip's arc to the other — and a full cycle is two of those, one per leg.
// Game::update is where that turns into a cadence and a velocity.

struct SamuraiPose {
    float walkPhase = 0.0f;  // radians through the stride cycle
    float moveAmount = 0.0f; // 0..1 fraction of max ground speed; drives bob and lean
    // Hip swing amplitude in radians, 0..kStrideAngle — how long a step this
    // fighter is taking. It comes from the sim (Player::strideBlend) rather
    // than from moveAmount, because the sim moves the body exactly as far as
    // this amplitude carries the foot; the model is not free to reinterpret
    // it. Scaling the stride by moveAmount here, as this used to, meant a
    // fighter walking at half speed took half-length steps while the body
    // still travelled a full stride between footfalls — the glide.
    float strideAmp = 0.0f;
    // +1 walking the way they face, -1 backing up (Player::strideSign). Says
    // which leg is the planted one, and so which knee folds.
    float strideSign = 1.0f;
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
