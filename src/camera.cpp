#include "camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

void FramingCamera::update(const glm::vec3& p1, const glm::vec3& p2, float aspect,
                           float dt) {
    glm::vec3 mid = 0.5f * (p1 + p2);
    float tanHalfFov = std::tan(kFovY * 0.5f);

    // For a fighter at depth offset dz from the midpoint, the camera plane
    // distance to them is (D - dz), so their lateral offset fits when
    // |offset| + margin <= tanHalfFov * (D - dz)  (times aspect horizontally).
    // Solve for the minimum D per fighter per axis and take the worst.
    float distance = kMinDistance;
    for (const glm::vec3& p : {p1, p2}) {
        float dz = p.z - mid.z;
        float spanX = std::abs(p.x - mid.x) + kMargin;
        float spanY = std::abs(p.y - mid.y) + kMargin;
        distance = std::max(distance, spanX / (tanHalfFov * aspect) + dz);
        distance = std::max(distance, spanY / tanHalfFov + dz);
    }

    glm::vec3 desiredTarget{mid.x, mid.y + kHeightBias, mid.z};
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
