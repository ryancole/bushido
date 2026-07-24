#pragma once

#include <glm/glm.hpp>

#include <memory>

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

    // Steps the rigid-body world. Static-only today; kept in the loop so
    // future dynamic bodies (ragdolls, debris) just work.
    void step(float dt);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    float m_gravity;
};
