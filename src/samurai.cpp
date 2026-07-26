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
// Windup raises the blade, active carries it through the attack's arc,
// recovery settles back toward rest (stopping short so the blade never
// pierces the ground before it disappears). The Active-phase angles must
// match game.cpp's kAttackTuning so the cut lands where the blade is drawn:
// light chops overhead-to-front, heavy from further past overhead, and the
// jab snaps a short arc to horizontal — a forward thrust.
// Guard: the sword arm holds the drawn blade level at the foe, between the
// jab's start and end angles — clearly "up", clearly not a swing.
constexpr float kGuardAngle = 1.30f;

// Blade geometry, shared by the sword in the hand and the one on the ground
// so a thrown-down odachi is still visibly an odachi. The steel runs from
// just past the guard out to the reach measured from the shoulder pivot.
constexpr float kShoulderY = 1.36f;   // arm pivot height (matches the drawn arm)
constexpr float kBladeTop = 0.755f;   // where the steel starts, above the hand
constexpr float kNominalReach = 1.6f; // SamuraiPose's default — the katana baseline
constexpr float kGripLength = 0.26f;  // grip + guard, behind the steel

float swordArmAngle(int attackState, int attackKind, float t) {
    constexpr float kStart[3] = {2.60f, 2.95f, 1.20f}; // light, heavy, jab
    constexpr float kEnd[3] = {0.55f, 0.45f, 1.55f};
    switch (attackState) {
        case 1: return glm::mix(0.35f, kStart[attackKind], t * t * (3.0f - 2.0f * t));
        case 2: return glm::mix(kStart[attackKind], kEnd[attackKind], t);
        case 3: return glm::mix(kEnd[attackKind], 0.35f, t);
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

    // Crouch: the legs fold — squashed toward the floor so the feet stay
    // planted — and everything hip-up rides down with them. The drop and the
    // 0.85 hip height must match game.cpp's kCrouchDrop/kHipHeight so the
    // ducked hurtboxes sit where the body is drawn.
    const float crouchDrop = pose.crouch * 0.45f;
    const float legSquash = (0.85f - crouchDrop) / 0.85f;

    // Legs: opposite-phase stride while grounded, a fixed tuck in the air.
    for (float s : {-1.0f, 1.0f}) {
        if (isSevered(pose, s > 0.0f ? 2 : 3)) {
            // Stump peeking out below the hip skirt (which drops with a crouch).
            part(glm::mat4(1.0f), {0.0f, 0.74f - crouchDrop, s * 0.12f},
                 {0.18f, 0.10f, 0.20f}, kStump);
            continue;
        }
        float swing;
        if (pose.grounded) {
            swing = std::sin(pose.walkPhase + (s > 0.0f ? 0.0f : pi)) * 0.55f * pose.moveAmount;
        } else {
            swing = s > 0.0f ? 0.55f : -0.35f;
        }
        // Crouching splits the stance into a slight lunge so the folded legs
        // read as bent knees rather than shrunken ones.
        swing += pose.crouch * (s > 0.0f ? 0.30f : -0.30f);
        glm::mat4 leg = pivotRotZ({0.0f, 0.85f * legSquash, s * 0.12f}, swing);
        part(leg, {0.0f, 0.44f * legSquash, s * 0.12f},
             {0.20f, 0.80f * legSquash, 0.22f}, colors.hakama);
        part(leg, {0.04f, 0.05f * legSquash, s * 0.12f}, {0.26f, 0.10f, 0.16f}, kLacquer);
    }

    // Upper body: breathes at rest, bounces with the stride, leans into motion.
    float bob = 0.015f * std::sin(pose.time * 2.2f) +
                0.035f * pose.moveAmount * std::fabs(std::sin(pose.walkPhase));
    float lean = -0.12f * pose.moveAmount;
    // Rear back through the windup, drive in through the strike: the heavy
    // exaggerates both, the jab barely coils and lunges into the thrust.
    constexpr float kRearBack[3] = {0.12f, 0.20f, 0.05f}; // light, heavy, jab
    constexpr float kDriveIn[3] = {0.22f, 0.30f, 0.28f};
    if (pose.attackState == 1) {
        lean += kRearBack[pose.attackKind] * pose.attackT;
    } else if (pose.attackState == 2) {
        lean -= kDriveIn[pose.attackKind] * pose.attackT;
    }
    lean -= 0.15f * pose.crouch; // hunch forward over the folded legs
    glm::mat4 upper = glm::translate(glm::mat4(1.0f), {0.0f, bob - crouchDrop, 0.0f}) *
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
        const bool guarding = pose.blocking && pose.attackState == 0;
        bool swordArm = s == swordSide && (pose.attackState != 0 || guarding);
        float swing;
        if (swordArm) {
            swing = guarding ? kGuardAngle
                             : swordArmAngle(pose.attackState, pose.attackKind,
                                             pose.attackT);
        } else if (pose.grounded) {
            swing = std::sin(pose.walkPhase + (s > 0.0f ? pi : 0.0f)) * 0.45f * pose.moveAmount;
        } else {
            swing = -0.6f;
        }
        glm::mat4 arm = upper * pivotRotZ({0.0f, 1.36f, s * 0.26f}, swing);
        part(arm, {0.0f, 1.10f, s * 0.26f}, {0.11f, 0.44f, 0.11f}, colors.kimono);
        part(arm, {0.0f, 0.84f, s * 0.26f}, {0.09f, 0.10f, 0.09f}, kSkin); // hand
        if (swordArm && pose.armed) {
            // Drawn katana extending past the hand, parallel to the arm. The
            // blade runs from just past the guard down to the reach distance
            // measured from the shoulder pivot (y 1.36), so its tip matches
            // the gameplay sweep segment.
            part(arm, {0.0f, 0.76f, s * 0.26f}, {0.13f, 0.03f, 0.13f}, kTsuba);
            const float bladeTop = kBladeTop;
            const float bladeTip = kShoulderY - pose.reach;
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

    // Sheathed katana worn at the hip, angled slightly downward behind. The
    // saya stays on the belt whatever happens — it is what a thrown-down
    // blade leaves behind, and an empty one is the clearest thing on the model
    // saying this fighter has nothing to swing.
    glm::mat4 katana = upper * glm::translate(glm::mat4(1.0f), {0.02f, 1.00f, 0.22f}) *
                       glm::rotate(glm::mat4(1.0f), 0.30f, glm::vec3(0.0f, 0.0f, 1.0f));
    part(katana, {-0.30f, 0.0f, 0.0f}, {0.60f, 0.05f, 0.05f}, kLacquer); // scabbard
    if (pose.armed) {
        part(katana, {0.01f, 0.0f, 0.0f}, {0.03f, 0.10f, 0.10f}, kTsuba);          // guard
        part(katana, {0.15f, 0.0f, 0.0f}, {0.24f, 0.045f, 0.045f}, colors.accent); // grip
    }
}

float bladeSteelLength(float reachBonus) {
    // Same span the in-hand blade draws, at the baseline reach: guard to tip.
    return (kBladeTop - (kShoulderY - (kNominalReach + reachBonus))) * 0.5f;
}

glm::vec3 droppedBladeHalfExtent(float steel) {
    // Deliberately a touch thicker than the drawn steel: Jolt's boxes carry a
    // convex radius, and a blade modelled as thin as it looks would be mostly
    // rounded corner. It is a sword lying on the floor, not a hitbox.
    return {(steel + kGripLength) * 0.5f, 0.045f, 0.045f};
}

void drawDroppedBlade(Renderer& renderer, const glm::mat4& transform, float steel,
                      float width, const glm::vec4& grip) {
    const float total = steel + kGripLength;
    const float butt = -total * 0.5f; // end of the grip, in the object's own space
    const float bw = 0.05f * width;
    auto part = [&](glm::vec3 center, glm::vec3 size, const glm::vec4& color) {
        renderer.drawBox(transform * boxAt(center, size), color);
    };
    part({butt + 0.11f, 0.0f, 0.0f}, {0.22f, 0.045f, 0.045f}, grip);
    part({butt + 0.24f, 0.0f, 0.0f}, {0.03f, 0.10f, 0.10f}, kTsuba);
    part({butt + kGripLength + steel * 0.5f, 0.0f, 0.0f}, {steel, bw, bw}, kSteel);
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
