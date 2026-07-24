#include "samurai.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

#include "renderer.hpp"

namespace {

const glm::vec4 kSkin{0.85f, 0.66f, 0.50f, 1.0f};
const glm::vec4 kStraw{0.62f, 0.52f, 0.30f, 1.0f};
const glm::vec4 kStrawDark{0.50f, 0.41f, 0.23f, 1.0f};
const glm::vec4 kLacquer{0.10f, 0.09f, 0.09f, 1.0f};
const glm::vec4 kTsuba{0.55f, 0.45f, 0.15f, 1.0f};
const glm::vec4 kSteel{0.78f, 0.80f, 0.85f, 1.0f};
const glm::vec4 kStump{0.42f, 0.05f, 0.05f, 1.0f}; // cut-surface blood cap

// Sword-arm angle about the shoulder (0 = hanging down, positive = forward/up).
// Windup raises the blade overhead, active chops it down in front, recovery
// settles back toward rest (stopping short so the blade never pierces the
// ground before it disappears).
float swordArmAngle(int attackState, float t) {
    switch (attackState) {
        case 1: return glm::mix(0.35f, 2.60f, t * t * (3.0f - 2.0f * t));
        case 2: return glm::mix(2.60f, 0.55f, t);
        case 3: return glm::mix(0.55f, 0.35f, t);
        default: return 0.0f;
    }
}

glm::mat4 boxAt(glm::vec3 center, glm::vec3 size) {
    return glm::scale(glm::translate(glm::mat4(1.0f), center), size);
}

glm::mat4 pivotRotZ(glm::vec3 pivot, float angle) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), pivot);
    m = glm::rotate(m, angle, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::translate(m, -pivot);
}

bool isSevered(const SamuraiPose& pose, int limb) {
    return pose.severed && pose.severed[limb];
}

} // namespace

LimbBounds samuraiLimbBounds(int limb) {
    switch (limb) {
        case 0: return {{0.0f, 1.06f, 0.26f}, {0.06f, 0.27f, 0.06f}};  // arm + hand
        case 1: return {{0.0f, 1.06f, -0.26f}, {0.06f, 0.27f, 0.06f}};
        case 2: return {{0.02f, 0.42f, 0.12f}, {0.15f, 0.42f, 0.11f}}; // leg + sandal
        case 3: return {{0.02f, 0.42f, -0.12f}, {0.15f, 0.42f, 0.11f}};
        default: return {{0.0f, 1.62f, 0.0f}, {0.19f, 0.18f, 0.19f}};  // head + neck
    }
}

// Local space: origin at the feet, +x forward (facing), +y up, legs/arms
// offset to the sides along z. Proportions add up to ~1.8 units tall,
// matching the Player collision box.
void drawSamurai(Renderer& renderer, const glm::vec3& feet, float yaw,
                 const SamuraiPose& pose, const SamuraiColors& colors) {
    glm::mat4 base =
        glm::rotate(glm::translate(glm::mat4(1.0f), feet), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    if (pose.bodyRoll != 0.0f) {
        // Topple: the whole body tips about the feet toward ±z.
        base = glm::rotate(base, pose.bodyRoll, glm::vec3(1.0f, 0.0f, 0.0f));
    }

    auto part = [&](const glm::mat4& local, glm::vec3 center, glm::vec3 size,
                    const glm::vec4& color) {
        renderer.drawBox(base * local * boxAt(center, size), color);
    };

    const float pi = glm::pi<float>();

    // Legs: opposite-phase stride while grounded, a fixed tuck in the air.
    for (float s : {-1.0f, 1.0f}) {
        if (isSevered(pose, s > 0.0f ? 2 : 3)) {
            // Stump peeking out below the hip skirt.
            part(glm::mat4(1.0f), {0.0f, 0.74f, s * 0.12f}, {0.18f, 0.10f, 0.20f}, kStump);
            continue;
        }
        float swing;
        if (pose.grounded) {
            swing = std::sin(pose.walkPhase + (s > 0.0f ? 0.0f : pi)) * 0.55f * pose.moveAmount;
        } else {
            swing = s > 0.0f ? 0.55f : -0.35f;
        }
        glm::mat4 leg = pivotRotZ({0.0f, 0.85f, s * 0.12f}, swing);
        part(leg, {0.0f, 0.44f, s * 0.12f}, {0.20f, 0.80f, 0.22f}, colors.hakama);
        part(leg, {0.04f, 0.05f, s * 0.12f}, {0.26f, 0.10f, 0.16f}, kLacquer);
    }

    // Upper body: breathes at rest, bounces with the stride, leans into motion.
    float bob = 0.015f * std::sin(pose.time * 2.2f) +
                0.035f * pose.moveAmount * std::fabs(std::sin(pose.walkPhase));
    float lean = -0.12f * pose.moveAmount;
    if (pose.attackState == 1) {
        lean += 0.12f * pose.attackT; // rear back through the windup
    } else if (pose.attackState == 2) {
        lean -= 0.22f * pose.attackT; // drive into the chop
    }
    glm::mat4 upper = glm::translate(glm::mat4(1.0f), {0.0f, bob, 0.0f}) *
                      pivotRotZ({0.0f, 0.95f, 0.0f}, lean);

    part(upper, {0.0f, 0.86f, 0.0f}, {0.44f, 0.28f, 0.36f}, colors.hakama); // hip skirt
    part(upper, {0.0f, 1.02f, 0.0f}, {0.42f, 0.10f, 0.32f}, colors.accent); // obi
    part(upper, {0.0f, 1.22f, 0.0f}, {0.40f, 0.34f, 0.30f}, colors.kimono); // torso
    part(upper, {0.0f, 1.40f, 0.0f}, {0.28f, 0.08f, 0.24f}, colors.hakama); // collar

    // Shoulder plates and arms. The +z arm is the sword arm — unless it has
    // been severed, in which case the -z arm takes over the katana. During an
    // attack the sword arm overrides the walk swing and carries the blade.
    const float swordSide = isSevered(pose, 0) ? -1.0f : 1.0f;
    for (float s : {-1.0f, 1.0f}) {
        part(upper, {0.0f, 1.38f, s * 0.24f}, {0.16f, 0.12f, 0.20f}, colors.kimono);
        if (isSevered(pose, s > 0.0f ? 0 : 1)) {
            part(upper, {0.0f, 1.28f, s * 0.26f}, {0.10f, 0.10f, 0.10f}, kStump);
            continue;
        }
        bool swordArm = s == swordSide && pose.attackState != 0;
        float swing;
        if (swordArm) {
            swing = swordArmAngle(pose.attackState, pose.attackT);
        } else if (pose.grounded) {
            swing = std::sin(pose.walkPhase + (s > 0.0f ? pi : 0.0f)) * 0.45f * pose.moveAmount;
        } else {
            swing = -0.6f;
        }
        glm::mat4 arm = upper * pivotRotZ({0.0f, 1.36f, s * 0.26f}, swing);
        part(arm, {0.0f, 1.10f, s * 0.26f}, {0.11f, 0.44f, 0.11f}, colors.kimono);
        part(arm, {0.0f, 0.84f, s * 0.26f}, {0.09f, 0.10f, 0.09f}, kSkin); // hand
        if (swordArm) {
            // Drawn katana extending past the hand, parallel to the arm. The
            // blade runs from just past the guard down to the reach distance
            // measured from the shoulder pivot (y 1.36), so its tip matches
            // the gameplay sweep segment.
            part(arm, {0.0f, 0.76f, s * 0.26f}, {0.13f, 0.03f, 0.13f}, kTsuba);
            const float bladeTop = 0.755f;
            const float bladeTip = 1.36f - pose.reach;
            const float bw = 0.05f * pose.bladeWidth;
            part(arm, {0.0f, (bladeTop + bladeTip) * 0.5f, s * 0.26f},
                 {bw, (bladeTop - bladeTip) * 0.5f, bw}, kSteel);
        }
    }

    // Head under a wide straw kasa (stacked slabs read as a cone).
    if (isSevered(pose, 4)) {
        part(upper, {0.0f, 1.46f, 0.0f}, {0.14f, 0.08f, 0.14f}, kStump);
    } else {
        part(upper, {0.0f, 1.56f, 0.0f}, {0.20f, 0.20f, 0.20f}, kSkin);
        part(upper, {0.0f, 1.70f, 0.0f}, {0.56f, 0.06f, 0.56f}, kStraw);
        part(upper, {0.0f, 1.75f, 0.0f}, {0.36f, 0.06f, 0.36f}, kStraw);
        part(upper, {0.0f, 1.80f, 0.0f}, {0.18f, 0.05f, 0.18f}, kStrawDark);
    }

    // Sheathed katana worn at the hip, angled slightly downward behind.
    glm::mat4 katana = upper * glm::translate(glm::mat4(1.0f), {0.02f, 1.00f, 0.22f}) *
                       glm::rotate(glm::mat4(1.0f), 0.30f, glm::vec3(0.0f, 0.0f, 1.0f));
    part(katana, {-0.30f, 0.0f, 0.0f}, {0.60f, 0.05f, 0.05f}, kLacquer);       // scabbard
    part(katana, {0.01f, 0.0f, 0.0f}, {0.03f, 0.10f, 0.10f}, kTsuba);          // guard
    part(katana, {0.15f, 0.0f, 0.0f}, {0.24f, 0.045f, 0.045f}, colors.accent); // grip
}

// Same boxes the attached limb is built from, re-centered on the limb's
// bounds so they track the debris rigid body, plus a stump cap at the cut.
void drawSeveredLimb(Renderer& renderer, const glm::mat4& transform, int limb,
                     const SamuraiColors& colors) {
    const glm::vec3 origin = samuraiLimbBounds(limb).center;
    auto part = [&](glm::vec3 center, glm::vec3 size, const glm::vec4& color) {
        renderer.drawBox(transform * boxAt(center - origin, size), color);
    };
    const float s = (limb == 1 || limb == 3) ? -1.0f : 1.0f;
    switch (limb) {
        case 0:
        case 1:
            part({0.0f, 1.10f, s * 0.26f}, {0.11f, 0.44f, 0.11f}, colors.kimono);
            part({0.0f, 0.84f, s * 0.26f}, {0.09f, 0.10f, 0.09f}, kSkin);
            part({0.0f, 1.30f, s * 0.26f}, {0.10f, 0.06f, 0.10f}, kStump);
            break;
        case 2:
        case 3:
            part({0.0f, 0.44f, s * 0.12f}, {0.20f, 0.80f, 0.22f}, colors.hakama);
            part({0.04f, 0.05f, s * 0.12f}, {0.26f, 0.10f, 0.16f}, kLacquer);
            part({0.0f, 0.82f, s * 0.12f}, {0.18f, 0.06f, 0.20f}, kStump);
            break;
        default:
            part({0.0f, 1.56f, 0.0f}, {0.20f, 0.20f, 0.20f}, kSkin);
            part({0.0f, 1.70f, 0.0f}, {0.56f, 0.06f, 0.56f}, kStraw);
            part({0.0f, 1.75f, 0.0f}, {0.36f, 0.06f, 0.36f}, kStraw);
            part({0.0f, 1.80f, 0.0f}, {0.18f, 0.05f, 0.18f}, kStrawDark);
            part({0.0f, 1.47f, 0.0f}, {0.12f, 0.06f, 0.12f}, kStump);
            break;
    }
}
