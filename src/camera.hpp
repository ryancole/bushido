#pragma once

#include <glm/glm.hpp>

// Fighting-game camera: follows the midpoint of the two fighters and pulls
// back along +Z just far enough that both stay on screen with a margin —
// accounting for each fighter's depth (a fighter nearer the camera needs more
// pull-back). Smoothed exponentially so it never snaps.
class FramingCamera {
public:
    void update(const glm::vec3& p1, const glm::vec3& p2, float aspect, float dt);
    glm::mat4 view() const;
    glm::mat4 proj(float aspect) const;

private:
    static constexpr float kPi = 3.14159265358979f;
    static constexpr float kFovY = 50.0f * kPi / 180.0f;
    static constexpr float kMargin = 2.5f;      // world units kept visible around the pair
    static constexpr float kMinDistance = 9.0f; // never zoom in closer than this
    static constexpr float kHeightBias = 1.2f;  // aim a bit above the pair's midpoint
    static constexpr float kSmoothing = 5.0f;   // exponential follow rate (1/s)

    glm::vec3 m_position{0.0f, 3.0f, 14.0f};
    glm::vec3 m_target{0.0f, 2.0f, 0.0f};
};
