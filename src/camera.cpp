#include "camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

void FramingCamera::update(const glm::vec2& p1, const glm::vec2& p2, float aspect,
                           float dt) {
    glm::vec2 mid = 0.5f * (p1 + p2);
    float halfSpanX = 0.5f * std::abs(p1.x - p2.x) + kMargin;
    float halfSpanY = 0.5f * std::abs(p1.y - p2.y) + kMargin;

    // Distance at which a span just fits the frustum, per axis; take the worst.
    float tanHalfFov = std::tan(kFovY * 0.5f);
    float distance = std::max(kMinDistance,
                              std::max(halfSpanX / (tanHalfFov * aspect),
                                       halfSpanY / tanHalfFov));

    glm::vec3 desiredTarget{mid.x, mid.y + kHeightBias, 0.0f};
    glm::vec3 desiredPosition = desiredTarget + glm::vec3(0.0f, 0.0f, distance);

    float blend = 1.0f - std::exp(-kSmoothing * dt);
    m_target += (desiredTarget - m_target) * blend;
    m_position += (desiredPosition - m_position) * blend;
}

glm::mat4 FramingCamera::view() const {
    return glm::lookAt(m_position, m_target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 FramingCamera::proj(float aspect) const {
    glm::mat4 p = glm::perspective(kFovY, aspect, 0.1f, 200.0f);
    p[1][1] *= -1.0f; // GLM builds GL-style clip space; Vulkan's Y points down
    return p;
}
