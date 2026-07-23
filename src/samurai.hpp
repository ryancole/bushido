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
};

// Draws a samurai assembled procedurally from shaded boxes: hakama legs,
// kimono torso, obi, sode shoulder plates, arms, head, straw kasa, and a
// sheathed katana. `feet` is the ground point under the character; `yaw`
// rotates about +Y (0 faces +x).
void drawSamurai(Renderer& renderer, const glm::vec3& feet, float yaw,
                 const SamuraiPose& pose, const SamuraiColors& colors);
