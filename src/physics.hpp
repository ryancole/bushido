#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <vector>

// Thin wrapper around Jolt Physics. Owns the physics world (static ground +
// arena walls) and one virtual character capsule per fighter. Gameplay stays
// authored in Game; this layer only resolves collision for the velocities it
// is handed each fixed step.
class Physics {
public:
    // An extra static collider box beyond the arena statics (level scenery
    // like Hanami's bank stones). Axis-aligned, alive for the whole match.
    struct StaticBox {
        glm::vec3 center;
        glm::vec3 halfExtent;
    };

    // A water volume: debris that drifts inside floats (buoyancy against the
    // surface plane, drag toward the current's velocity) and rides the
    // current downstream. A level may have several (Sorihashi's river shows
    // on both sides of its dry deck); empty list = no water.
    struct Water {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f}; // max.y is the surface height
        glm::vec3 current{0.0f};
    };

    // Spawns are feet positions (the ground surface is y = 0). Gravity is the
    // magnitude of downward acceleration, matching the game's own constant.
    // arenaHalfWidth places the side walls (per-level; depth is fixed).
    // groundBoxes replace the default flat ground slab when non-empty
    // (Hanami's carved stream channel).
    Physics(float gravity, float arenaHalfWidth, const glm::vec3& spawnA,
            const glm::vec3& spawnB, const std::vector<StaticBox>& staticBoxes = {},
            const std::vector<StaticBox>& groundBoxes = {},
            const std::vector<Water>& water = {});
    ~Physics();

    struct MoveResult {
        glm::vec3 feetPos;
        bool onGround;
    };

    // Moves character i for one fixed step with the given desired velocity
    // (gravity already integrated into velocity.y by the caller).
    MoveResult moveCharacter(int i, const glm::vec3& velocity, float dt);

    // Swaps character i between the standing capsule and a squat prone one
    // (a toppled fighter lying on the ground; opponents step over the body).
    // Idempotent — safe to call every step.
    void setCharacterProne(int i, bool prone);

    // Steps the rigid-body world (arena statics + severed-limb debris).
    void step(float dt);

    // A debris body striking something (ground, wall, other debris) during the
    // last step() hard enough to be audible.
    struct DebrisImpact {
        glm::vec3 pos; // world-space contact point
        float speed;   // closing speed along the contact normal, m/s
        // Which debris body it was. The caller knows what each of its handles
        // *is* and this layer does not, which is what lets a severed arm smear
        // blood where it lands while a thrown-down sword only clatters.
        int id;
    };
    const std::vector<DebrisImpact>& debrisImpacts() const;

    // Spawns a dynamic box body (a severed limb, a discarded blade). Debris
    // collides with the arena, other debris, and the fighters (who kick pieces
    // around by walking into them). Returns a handle for debrisTransform.
    // `yaw` rotates about +Y (matches the fighter's facing).
    int addDebris(const glm::vec3& center, float yaw, const glm::vec3& halfExtent,
                  const glm::vec3& velocity, const glm::vec3& angularVelocity);

    // Destroys a debris body — a blade being taken back up off the ground.
    // The handle is *retired*, not reused: every other handle already handed
    // out has to keep meaning the same body, so the slot stays as a hole and
    // debrisStates simply skips it.
    void removeDebris(int id);

    // Current world transform of a debris body, for rendering.
    glm::mat4 debrisTransform(int id) const;

    // Raw state of every live debris body, in creation order (retired handles
    // are skipped), for a determinism checksum. Debris is not merely
    // cosmetic: a piece landing one step late
    // changes the debrisImpacts() stream, which Game turns into blood marks —
    // advancing its rng — so the rigid-body world sits inside the determinism
    // boundary whether or not a limb ever decides a fight.
    struct DebrisState {
        glm::vec3 pos;
        glm::vec4 rot; // orientation quaternion (x, y, z, w)
        glm::vec3 vel;
        glm::vec3 angVel;
    };
    void debrisStates(std::vector<DebrisState>& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    float m_gravity;
};
