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
    // Spawns are feet positions (the ground surface is y = 0). Gravity is the
    // magnitude of downward acceleration, matching the game's own constant.
    Physics(float gravity, const glm::vec3& spawnA, const glm::vec3& spawnB);
    ~Physics();

    struct MoveResult {
        glm::vec3 feetPos;
        bool onGround;
    };

    // Moves character i for one fixed step with the given desired velocity
    // (gravity already integrated into velocity.y by the caller).
    MoveResult moveCharacter(int i, const glm::vec3& velocity, float dt);

    // Steps the rigid-body world (arena statics + severed-limb debris).
    void step(float dt);

    // A debris body striking something (ground, wall, other debris) during the
    // last step() hard enough to be audible.
    struct DebrisImpact {
        float x;     // world x of the contact point
        float speed; // closing speed along the contact normal, m/s
    };
    const std::vector<DebrisImpact>& debrisImpacts() const;

    // Spawns a dynamic box body for a severed limb. Debris collides with the
    // arena, other debris, and the fighters (who kick pieces around by walking
    // into them). Returns a handle for debrisTransform. `yaw` rotates about +Y
    // (matches the fighter's facing).
    int addDebris(const glm::vec3& center, float yaw, const glm::vec3& halfExtent,
                  const glm::vec3& velocity, const glm::vec3& angularVelocity);

    // Current world transform of a debris body, for rendering.
    glm::mat4 debrisTransform(int id) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    float m_gravity;
};
