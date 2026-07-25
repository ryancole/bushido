#pragma once

#include <glm/glm.hpp>

class Renderer;

struct SamuraiColors {
    glm::vec4 kimono; // torso, arms, shoulder plates
    glm::vec4 hakama; // trousers, hip skirt, collar
    glm::vec4 accent; // obi belt, sword grip wrap
};

struct SamuraiPose {
    float walkPhase = 0.0f;  // radians through the stride cycle
    float moveAmount = 0.0f; // 0..1 fraction of max ground speed; scales the stride
    bool grounded = true;
    float time = 0.0f;       // seconds since start, for idle breathing
    int attackState = 0;     // mirrors game AttackState: 0 none, 1 windup, 2 active, 3 recovery
    int attackKind = 0;      // mirrors game AttackKind: 0 light, 1 heavy, 2 jab
    float attackT = 0.0f;    // 0..1 progress through the current attack phase
    // Shoulder-to-tip length of the drawn katana, matching the fighter's
    // resolved reach (character + weapon) so what connects is what the
    // player sees. bladeWidth scales only the drawn blade's thickness —
    // a visual cue for the equipped weapon, no gameplay effect.
    float reach = 1.6f;
    float bladeWidth = 1.0f;
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

// Draws a samurai assembled procedurally from shaded boxes: hakama legs,
// kimono torso, obi, sode shoulder plates, arms, head, straw kasa, and a
// sheathed katana. `feet` is the ground point under the character; `yaw`
// rotates about +Y (0 faces +x).
void drawSamurai(Renderer& renderer, const glm::vec3& feet, float yaw,
                 const SamuraiPose& pose, const SamuraiColors& colors);
