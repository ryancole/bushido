#include "samurai.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
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

// The knee is no longer posed at all — it is solved from where the foot has to
// be (see the leg block in drawSamurai). A leg is a two-bone chain rather than
// one post because a rigid leg has exactly one way to reach the ground, which
// is not enough freedom to hold a stance *and* put both feet on the floor.

// Sword-arm angle about the shoulder (0 = hanging down, positive = forward/up
// — the convention weapons/stance.hpp documents in full). Windup raises the
// blade out of the stance's ready angle, active carries it through the arc,
// recovery settles back to ready.
//
// Every one of those angles comes from the stance table, which is also what
// game.cpp sweeps the hit test along: the cut has to land where the blade is
// drawn, and one table is the only way to keep that true. So a blade in the
// High stance falls from overhead, one in Low rises off the floor, and the
// difference is a swing rather than a skin.

// Blade geometry, shared by the sword in the hand and the one on the ground
// so a thrown-down odachi is still visibly an odachi. The steel runs from
// just past the guard out to the reach measured from the shoulder pivot.
constexpr float kShoulderY = 1.36f;   // arm pivot height (matches the drawn arm)
constexpr float kBladeTop = 0.755f;   // where the steel starts, above the hand
constexpr float kNominalReach = 1.6f; // SamuraiPose's default — the katana baseline
constexpr float kGripLength = 0.26f;  // grip + guard, behind the steel

float swordArmAngle(Stance stance, int attackState, int attackKind, float t) {
    const StanceDef& s = stanceDef(stance);
    const StanceArc& arc = s.arcs[attackKind];
    switch (attackState) {
        case 1: return glm::mix(s.readyAngle, arc.start, t * t * (3.0f - 2.0f * t));
        case 2: return glm::mix(arc.start, arc.end, t);
        case 3: return glm::mix(arc.end, s.readyAngle, t);
        default: return s.readyAngle;
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

    // The guard settles onto bent knees, and a duck lowers it further. The hip
    // height and kCrouchDrop must match game.cpp so the ducked hurtboxes sit
    // where the body is drawn.
    const float crouchDrop = pose.crouch * 0.45f;
    const float hipY = kStanceHipY - crouchDrop;
    // How far the whole body sits below a fighter standing tall, which the
    // upper body rides down by.
    const float stanceDrop = kHipPivotY - hipY;

    // Where in the cycle, and which half of it. A cycle is two movements:
    // *widen*, one foot reaching out, then *close*, the other coming up after
    // it. Both feet's targets are the same either way round — all that changes
    // with the direction of travel is which of the two is the one moving,
    // which is what makes the same leg lead all match while advancing and
    // retreating still start on the correct foot.
    const bool closeHalf = pose.walkPhase >= pi;
    const float t = closeHalf ? pose.walkPhase - pi : pose.walkPhase;
    // Fraction of this half's ground already covered. This is the integral of
    // Game's own speed curve (2·sin²) and has to stay that, or the planted
    // foot creeps: it is the one number keeping the sim's travel and the
    // model's foot in agreement.
    const float advance = (t - 0.5f * std::sin(2.0f * t)) / pi;
    // The moving foot is under no such constraint — it is in the air — so it
    // just eases across and arcs over.
    const float ts = t / pi;
    const float ease = ts * ts * (3.0f - 2.0f * ts);
    const float step = 0.5f * kShuffleStride * pose.strideBlend;

    for (float s : {-1.0f, 1.0f}) {
        if (isSevered(pose, s > 0.0f ? 2 : 3)) {
            // Stump peeking out below the hip skirt.
            part(glm::mat4(1.0f), {0.0f, hipY - 0.11f, s * 0.12f},
                 {0.18f, 0.10f, 0.20f}, kStump);
            continue;
        }
        // The +z leg leads. It is the sword side (kShoulderSide), it is always
        // the front foot, and it stays the front foot — local +x is the way
        // the fighter faces, so this holds through every turn without anything
        // having to flip.
        const bool isLead = s > 0.0f;
        // Advancing, the lead reaches out first and the rear closes; backing
        // off, the rear goes first and the lead follows it. Either way neither
        // foot ever passes the other.
        const bool movesOnWiden = (pose.strideSign > 0.0f) == isLead;
        const bool moving = pose.grounded && (closeHalf ? !movesOnWiden : movesOnWiden);
        const float neutral = (isLead ? 0.5f : -0.5f) * kStanceSep;
        const float out = isLead ? step : -step;
        const float from = closeHalf ? neutral + out : neutral;
        const float to = closeHalf ? neutral : neutral + out;

        float footX = from + (to - from) * (moving ? ease : advance);
        // The lift closes with the step, not just the reach: a fighter who
        // stops mid-cycle freezes the phase wherever it was, and without this
        // they would stand there holding one foot in the air.
        float footY =
            kFootY + (moving ? kFootLift * pose.strideBlend * std::sin(t) : 0.0f);
        if (!pose.grounded) {
            // Airborne: nothing to stand on, so the stance opens into a tuck —
            // trailing leg folded up hard, leading one reaching.
            footX = isLead ? 0.30f : -0.20f;
            footY = kFootY + (isLead ? 0.26f : 0.44f);
        }

        // Solve the two-bone chain for that foot instead of swinging the hip
        // and hoping. The bones are equal length, so the leg is an isosceles
        // triangle: the knee closes by 2·acos(reach / leg) and the thigh sits
        // half that angle off the straight hip-to-foot line. Placing the foot
        // and solving back is what lets the stance be as wide as it likes and
        // still have both feet exactly on the floor.
        const float ry = hipY - footY;
        const float reach =
            std::min(std::sqrt(footX * footX + ry * ry), kLegLength - 0.002f);
        const float knee =
            2.0f * std::acos(std::clamp(reach / kLegLength, 0.0f, 1.0f));
        const float hipAng = std::atan2(footX, ry) + 0.5f * knee;

        // Boxes hang from the rest pose — leg straight down from the hip — and
        // the two rotations carry them onto the solved angles.
        const glm::vec3 kneeRest{0.0f, hipY - kThighLen, s * 0.12f};
        const glm::mat4 thigh = pivotRotZ({0.0f, hipY, s * 0.12f}, hipAng);
        const glm::mat4 shin = thigh * pivotRotZ(kneeRest, -knee);
        part(thigh, {0.0f, hipY - 0.5f * kThighLen, s * 0.12f},
             {0.21f, kThighLen + 0.02f, 0.225f}, colors.hakama);
        // Kneecap, centered on the pivot so it covers the joint at any fold.
        part(thigh, kneeRest, {0.20f, 0.19f, 0.23f}, colors.hakama);
        part(shin, {0.0f, hipY - kThighLen - 0.5f * kShinLen, s * 0.12f},
             {0.19f, kShinLen + 0.02f, 0.21f}, colors.hakama);
        part(shin, {0.04f, hipY - kLegLength, s * 0.12f}, {0.26f, 0.10f, 0.16f},
             kLacquer);
    }

    // Upper body: breathes at rest, rides the hip, leans into motion. There is
    // no stride bounce at all any more — a shuffle holds its height, which is
    // most of why it reads as a fighter moving rather than a person walking.
    float bob = 0.015f * std::sin(pose.time * 2.2f);
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
    glm::mat4 upper =
        glm::translate(glm::mat4(1.0f), {0.0f, bob - stanceDrop, 0.0f}) *
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
        // The sword arm carries the blade whenever there is one to carry —
        // there is no sheathing it mid-duel. At rest it holds the stance's
        // ready angle, which is what makes the stance something the player can
        // read off the fighter standing there rather than a shape that
        // appears for the third of a second a swing takes. It also costs the
        // arm its stride swing, which is right: nobody walks a sword around
        // like an empty hand. Empty-handed, it goes back to being an arm.
        const bool swordArm = s == swordSide && pose.armed;
        float swing;
        if (swordArm) {
            swing = guarding ? stanceDef(pose.stance).guardAngle
                             : swordArmAngle(pose.stance, pose.attackState,
                                             pose.attackKind, pose.attackT);
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

    // The saya worn at the hip, angled slightly downward behind, and always
    // empty: the blade that belongs in it is in the fighter's hand from the
    // first frame of the match to the last. It stays on the belt whatever
    // happens, including after the sword has been thrown away — a sheath is
    // not something you drop with the blade.
    //
    // So it is no longer what says "this fighter has nothing to swing"; the
    // empty *hand* is, which is a far louder thing on screen than a 5 cm slat
    // at the hip ever was.
    glm::mat4 katana = upper * glm::translate(glm::mat4(1.0f), {0.02f, 1.00f, 0.22f}) *
                       glm::rotate(glm::mat4(1.0f), 0.30f, glm::vec3(0.0f, 0.0f, 1.0f));
    part(katana, {-0.30f, 0.0f, 0.0f}, {0.60f, 0.05f, 0.05f}, kLacquer); // scabbard
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
