#pragma once

#include "renderer.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>

// Small drawing helpers shared by the battleground implementations. Nothing
// here touches the sim — level scenery is drawn from pure functions of time.

namespace levels {

// An axis-aligned box given as center + full size (the renderer takes a model
// matrix over a unit cube).
inline void box(Renderer& r, glm::vec3 center, glm::vec3 size, glm::vec4 color) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::scale(model, size);
    r.drawBox(model, color);
}

// Cheap deterministic 0..1 hash for scattering scenery; keeps every frame
// identical without any stored state.
inline float hash01(int i) {
    std::uint32_t h = static_cast<std::uint32_t>(i) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

} // namespace levels
