#include "samurai.hpp"

#include "levels/sun.hpp" // the same cast the battlegrounds' own shadows use

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

// Places a bone that hangs straight down from `restTop` in the rest pose so it
// runs from `top` to `bottom` instead. Built from the joint positions rather
// than from angles because a leg with a pole-picked knee is not confined to
// any one plane — there is no pair of Euler angles to write. `front` only
// picks the roll about the bone, keeping the box's width facing sensibly.
glm::mat4 boneXf(glm::vec3 restTop, glm::vec3 top, glm::vec3 bottom,
                 glm::vec3 front) {
    const glm::vec3 yAxis = glm::normalize(top - bottom);
    glm::vec3 xAxis = front - yAxis * glm::dot(front, yAxis);
    const float len = glm::length(xAxis);
    xAxis = len > 1e-4f ? xAxis / len : glm::vec3{1.0f, 0.0f, 0.0f};
    glm::mat4 r{1.0f};
    r[0] = glm::vec4(xAxis, 0.0f);
    r[1] = glm::vec4(yAxis, 0.0f);
    r[2] = glm::vec4(glm::cross(xAxis, yAxis), 0.0f);
    return glm::translate(glm::mat4(1.0f), top) * r *
           glm::translate(glm::mat4(1.0f), -restTop);
}

bool isSevered(const SamuraiPose& pose, int limb) {
    return pose.severed && pose.severed[limb];
}

// The two-bone solve, given where the hip is and where the foot has to be.
// With both ends fixed the knee is *not* pinned: it can sit anywhere on a
// circle about the hip-to-foot axis, and which point of that circle it takes
// is the whole difference between a leg and a horse's hind leg. A pole vector
// picks it, and the pole is model forward — always — so both knees point the
// way the fighter faces.
//
// Solving inside the vertical plane through hip and foot instead, which is
// what this did first, silently bulges the knee toward whichever way the foot
// is offset. The lead leg looked right by luck, its foot being ahead; the rear
// leg, whose foot is behind, folded backward at the knee.
LegPose solveLeg(glm::vec3 hip, glm::vec3 foot) {
    glm::vec3 toFoot = foot - hip;
    float dist = glm::length(toFoot);
    const float maxReach = kLegLength - 0.002f;
    if (dist > maxReach) { // a leg cannot reach further than it is long
        toFoot *= maxReach / dist;
        dist = maxReach;
        foot = hip + toFoot;
    }
    const glm::vec3 axis = toFoot / dist;
    // Equal-length bones (kThighLen == kShinLen), so the knee stands over the
    // midpoint of the chord, out by whatever Pythagoras leaves.
    const float half = 0.5f * dist;
    const float bulge =
        std::sqrt(std::max(0.0f, kThighLen * kThighLen - half * half));
    glm::vec3 pole{1.0f, 0.0f, 0.0f};
    pole -= axis * glm::dot(pole, axis); // the part of forward across the leg
    const float poleLen = glm::length(pole);

    LegPose out{};
    out.hipAt = hip;
    out.footAt = foot;
    out.kneeAt = hip + axis * half +
                 (poleLen > 1e-4f ? pole / poleLen : glm::vec3{0.0f, 0.0f, 1.0f}) *
                     bulge;
    return out;
}

// The hop, in numbers. The plant is the part of the cycle with the foot on the
// floor and so the part that covers no ground at all; the rest is flight. The
// body sinks a little to push off and rides up over the hop, and the foot
// tucks up past that rise so it genuinely clears the floor rather than
// skimming it. Every one of these is bounded by the leg being a fixed-length
// lever: at full flight the hip is at 0.79 and the foot at 0.30, which is
// 0.49 of a 0.80 m leg — a deep tuck, and nowhere near straightening it.
constexpr float kHopPlant = 0.32f; // fraction of the cycle with the foot down
constexpr float kHopRise = 0.09f;  // m the body lifts at the top of a hop
constexpr float kHopDip = 0.05f;   // m it sinks into the push-off
constexpr float kHopClear = 0.16f; // m the foot tucks up past the body's rise

} // namespace

LegPose shuffleLeg(float phase, float strideBlend, glm::vec2 dir, float crouch,
                   bool grounded, float side) {
    const float pi = glm::pi<float>();
    const float hipY = kStanceHipY - crouch * kCrouchDrop;

    // Where in the cycle, and which half. A cycle is two movements: *widen*,
    // one foot reaching out along the travel, then *close*, the other coming
    // up after it. Both feet's excursions are the same either way round — all
    // that changes with the direction of travel is which of the two is moving,
    // which is what makes the same leg lead all match while advancing and
    // retreating still start on the correct foot.
    const bool closeHalf = phase >= pi;
    const float t = closeHalf ? phase - pi : phase;
    // Fraction of this half's ground already covered. This is the integral of
    // Game's own speed curve (2·sin²) and has to stay exactly that, or the
    // planted foot creeps: it is the one number holding the sim's travel and
    // the model's foot together.
    const float advance = (t - 0.5f * std::sin(2.0f * t)) / pi;
    // The moving foot is under no such constraint — it is in the air — so it
    // just eases across and arcs over.
    const float ts = t / pi;
    const float ease = ts * ts * (3.0f - 2.0f * ts);
    const float step = 0.5f * kShuffleStride * strideBlend;

    // Which foot moves first: the one already furthest along the direction of
    // travel. Its own z offset breaks the tie, so a pure side-step leads with
    // the leg on the side being stepped toward instead of picking arbitrarily.
    const glm::vec2 neutral{side * 0.5f * kStanceSep, side * kLegSide};
    const bool movesOnWiden = neutral.x * dir.x + neutral.y * dir.y > 0.0f;
    const bool moving = grounded && (closeHalf ? !movesOnWiden : movesOnWiden);
    // The excursion is ±half a stride along the travel: the foot that goes
    // first leans out, the other is left behind by the body and catches up.
    const float reachSign = movesOnWiden ? 1.0f : -1.0f;
    const float k = moving ? ease : advance;
    const float f = closeHalf ? 1.0f - k : k;
    const float excursion = reachSign * step * f;

    glm::vec3 foot{neutral.x + dir.x * excursion, kFootY,
                   neutral.y + dir.y * excursion};
    // The lift closes with the step, not just the reach: a fighter who stops
    // mid-cycle freezes the phase wherever it was, and without this they would
    // stand there holding one foot in the air.
    if (moving) {
        foot.y += kFootLift * strideBlend * std::sin(t);
    }
    if (!grounded) {
        // Nothing to stand on, so the stance opens into a tuck — trailing leg
        // folded up hard, leading one reaching.
        foot = {side > 0.0f ? 0.30f : -0.20f,
                kFootY + (side > 0.0f ? 0.26f : 0.44f), side * kLegSide};
    }

    // Solve the two-bone chain for that foot rather than swinging the hip and
    // hoping — a rigid leg has exactly one way to reach the ground, which is
    // not enough freedom to hold a stance *and* put both feet on the floor.
    return solveLeg({0.0f, hipY, side * kLegSide}, foot);
}

HopPose hopCycle(float phase, float strideBlend, float crouch, bool grounded,
                 float side) {
    const float pi = glm::pi<float>();
    HopPose out{};
    if (!grounded) {
        // Already off the floor for some other reason — knocked into the air,
        // or falling. There is nothing to hop off, so the leg just tucks the
        // way it does in any other jump and the travel is ordinary air control.
        out.surge = 1.0f;
        out.leg = shuffleLeg(phase, strideBlend, {1.0f, 0.0f}, crouch, false, side);
        return out;
    }

    const float hipY = kStanceHipY - crouch * kCrouchDrop;
    const float u = std::fmod(phase, pi) / pi; // 0..1 through one hop
    float lift = 0.0f;
    if (u < kHopPlant) {
        // Foot down. No ground is covered at all here, which is exactly what
        // lets the foot stay where it was put — the sim's speed curve is this
        // same surge, so a planted foot and a still body are one statement.
        out.surge = 0.0f;
        out.rise = -kHopDip * std::sin(pi * u / kHopPlant);
    } else {
        const float w = (u - kHopPlant) / (1.0f - kHopPlant);
        // Zero at both ends of the flight, so the body neither jerks off the
        // floor nor lands still travelling; ∫(1−cos 2πw)dw = 1 over the flight,
        // and dividing by its share of the cycle puts the mean back at 1 across
        // the whole of it.
        out.surge = (1.0f - std::cos(2.0f * pi * w)) / (1.0f - kHopPlant);
        out.rise = kHopRise * std::sin(pi * w);
        lift = (kHopRise + kHopClear) * std::sin(pi * w);
    }
    // Amplitude closes with the step, exactly as the shuffle's foot lift does:
    // a fighter standing still on one leg is balancing, not hopping in place.
    out.rise *= strideBlend;
    lift *= strideBlend;

    // The foot stays under the body — that *is* the hop, and it is why nothing
    // here needs the direction of travel: a body carried through the air lands
    // where it is going without the leg having to reach for it. So the leg only
    // ever rises and falls, and the ground it covers is covered airborne.
    return {out.surge, out.rise,
            solveLeg({0.0f, hipY + out.rise, side * kLegSide},
                     {0.0f, kFootY + lift, side * kLegSide})};
}

// Heights here are authored against a fighter standing tall and then dropped
// by kStanceDrop, because that is exactly what drawSamurai does to the body
// they bound. The legs are the exception: theirs move with the shuffle, so
// this is only their *size* — a representative box for debris and a fallback
// extent. Where they actually are comes from shuffleLeg, via game.cpp's
// posedLimbBounds.
LimbBounds samuraiLimbBounds(int limb) {
    switch (limb) {
        case 0: return {{0.0f, 1.06f - kStanceDrop, 0.26f}, {0.06f, 0.27f, 0.06f}};
        case 1: return {{0.0f, 1.06f - kStanceDrop, -0.26f}, {0.06f, 0.27f, 0.06f}};
        case 2: return {{0.09f, 0.38f, kLegSide}, {0.22f, 0.34f, 0.12f}}; // leg + sandal
        case 3: return {{-0.09f, 0.38f, -kLegSide}, {0.22f, 0.34f, 0.12f}};
        default: return {{0.0f, 1.62f - kStanceDrop, 0.0f}, {0.19f, 0.18f, 0.19f}};
    }
}

namespace {

// Local space: origin at the feet, +x forward (facing), +y up, legs/arms
// offset to the sides along z. Proportions add up to ~1.8 units tall,
// matching the Player collision box.
//
// Every box the model is made of, handed to `emit` as a finished world
// transform and the color it wears. drawSamurai draws them; drawSamuraiShadow
// casts them on the floor. One walk of the body with two consumers, for the
// same reason shuffleLeg is one function with two: a second description of the
// fighter would drift from this one, and a shadow is the most literal way that
// drift could ever show.
//
// `detail` marks a box lying wholly inside another's silhouette — a kneecap
// inside its own thigh, the head under its kasa, an obi inside the hip skirt.
// Drawing does not care and never reads it. The shadow skips them: each slab
// it casts costs a slice of the thin band they are all stacked in, and a
// silhouette is made of the outermost boxes and nothing else.
template <typename Emit>
void buildSamurai(const glm::vec3& feet, float yaw, const SamuraiPose& pose,
                  const SamuraiColors& colors, Emit&& emit) {
    glm::mat4 base =
        glm::rotate(glm::translate(glm::mat4(1.0f), feet), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    if (pose.bodyRoll != 0.0f) {
        // Topple: the whole body tips about the feet toward ±z.
        base = glm::rotate(base, pose.bodyRoll, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    if (pose.rollSpin != 0.0f) {
        // Barrel roll, inside the topple: the body turns about its own spine,
        // which by then is lying along the ground, so it rolls rather than
        // tipping again. game.cpp's modelToWorld composes these two in this
        // order — a hurtbox that disagreed would be cut where the body is not.
        base = glm::rotate(base, pose.rollSpin, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    auto part = [&](const glm::mat4& local, glm::vec3 center, glm::vec3 size,
                    const glm::vec4& color, bool detail = false) {
        emit(base * local * boxAt(center, size), color, detail);
    };

    const float pi = glm::pi<float>();

    // The guard settles onto bent knees, and a duck lowers it further. The hip
    // height and kCrouchDrop must match game.cpp so the ducked hurtboxes sit
    // where the body is drawn.
    const float crouchDrop = pose.crouch * kCrouchDrop;
    // A fighter down to one leg hops on it, and the whole body rides that hop —
    // so this is a signed offset rather than the drop it used to be. Its sign
    // is the only difference between ducking and being carried up off the
    // floor, and everything above the hip takes it either way.
    const float hopSide = isSevered(pose, 2) ? -1.0f : 1.0f;
    const float hopRise =
        pose.hopping ? hopCycle(pose.walkPhase, pose.strideBlend, pose.crouch,
                                pose.grounded, hopSide)
                           .rise
                     : 0.0f;
    const float hipY = kStanceHipY - crouchDrop + hopRise;
    // How far the whole body sits below a fighter standing tall, which the
    // upper body rides down by — and which every sim height is authored
    // against too, so the blade cuts where it is drawn.
    const float stanceDrop = kStanceDrop + crouchDrop - hopRise;

    for (float s : {-1.0f, 1.0f}) {
        if (isSevered(pose, s > 0.0f ? 2 : 3)) {
            // Stump peeking out below the hip skirt.
            part(glm::mat4(1.0f), {0.0f, hipY - 0.11f, s * kLegSide},
                 {0.18f, 0.10f, 0.20f}, kStump, true);
            continue;
        }
        // Solved, not posed — and solved by the same function game.cpp bounds
        // the leg with, so the fighter is hit where their legs actually are.
        // On one leg that is the hop's solve instead of the shuffle's: there is
        // no shuffling onto a leg that is not there.
        const LegPose leg =
            pose.hopping
                ? hopCycle(pose.walkPhase, pose.strideBlend, pose.crouch,
                           pose.grounded, s)
                      .leg
                : shuffleLeg(pose.walkPhase, pose.strideBlend, pose.strideDir,
                             pose.crouch, pose.grounded, s);
        // Boxes hang from the rest pose — leg straight down from the hip — and
        // each bone is carried onto the joints the solve produced. The roll
        // reference is model forward, the same vector the knee was poled with,
        // so the kneecap faces the way the knee bends.
        const glm::vec3 front{1.0f, 0.0f, 0.0f};
        const glm::vec3 kneeRest{0.0f, hipY - kThighLen, s * kLegSide};
        const glm::mat4 thigh =
            boneXf(leg.hipAt, leg.hipAt, leg.kneeAt, front);
        const glm::mat4 shin = boneXf(kneeRest, leg.kneeAt, leg.footAt, front);
        part(thigh, {0.0f, hipY - 0.5f * kThighLen, s * kLegSide},
             {0.21f, kThighLen + 0.02f, 0.225f}, colors.hakama);
        // Kneecap, centered on the pivot so it covers the joint at any fold.
        part(thigh, kneeRest, {0.20f, 0.19f, 0.23f}, colors.hakama, true);
        part(shin, {0.0f, hipY - kThighLen - 0.5f * kShinLen, s * kLegSide},
             {0.19f, kShinLen + 0.02f, 0.21f}, colors.hakama);
        part(shin, {0.04f, hipY - kLegLength, s * kLegSide}, {0.26f, 0.10f, 0.16f},
             kLacquer, true);
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
    part(upper, {0.0f, 1.02f, 0.0f}, {0.42f, 0.10f, 0.32f}, colors.accent, true); // obi
    part(upper, {0.0f, 1.22f, 0.0f}, {0.40f, 0.34f, 0.30f}, colors.kimono); // torso
    part(upper, {0.0f, 1.40f, 0.0f}, {0.28f, 0.08f, 0.24f}, colors.hakama, true); // collar

    // Shoulder plates and arms. The +z arm is the sword arm — unless it has
    // been severed, in which case the -z arm takes over the katana. During an
    // attack the sword arm overrides the walk swing and carries the blade.
    const float swordSide = isSevered(pose, 0) ? -1.0f : 1.0f;
    for (float s : {-1.0f, 1.0f}) {
        part(upper, {0.0f, 1.38f, s * 0.24f}, {0.16f, 0.12f, 0.20f}, colors.kimono, true);
        if (isSevered(pose, s > 0.0f ? 0 : 1)) {
            part(upper, {0.0f, 1.28f, s * 0.26f}, {0.10f, 0.10f, 0.10f}, kStump, true);
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
        part(arm, {0.0f, 0.84f, s * 0.26f}, {0.09f, 0.10f, 0.09f}, kSkin, true); // hand
        if (swordArm) {
            // Drawn katana extending past the hand, parallel to the arm. The
            // blade runs from just past the guard down to the reach distance
            // measured from the shoulder pivot (y 1.36), so its tip matches
            // the gameplay sweep segment.
            part(arm, {0.0f, 0.76f, s * 0.26f}, {0.13f, 0.03f, 0.13f}, kTsuba);
            const float bladeTop = kBladeTop;
            const float bladeTip = kShoulderY - pose.reach;
            const float bw = 0.05f * pose.bladeWidth;
            // boxAt takes a *full* size — the cube in cube.vert spans ±0.5 —
            // so the steel is the whole guard-to-tip span, not half of it.
            // Halved, the box kept its center and lost a quarter of the span
            // off each end: a hand's width of nothing where the guard should
            // be, and a tip stopping 25 cm short of the arc game.cpp actually
            // sweeps. The gap was the visible half of that bug and the short
            // tip the expensive one.
            part(arm, {0.0f, (bladeTop + bladeTip) * 0.5f, s * 0.26f},
                 {bw, bladeTop - bladeTip, bw}, kSteel);
        }
    }

    // Head under a wide straw kasa (stacked slabs read as a cone).
    if (isSevered(pose, 4)) {
        part(upper, {0.0f, 1.46f, 0.0f}, {0.14f, 0.08f, 0.14f}, kStump, true);
    } else {
        part(upper, {0.0f, 1.56f, 0.0f}, {0.20f, 0.20f, 0.20f}, kSkin, true);
        // The brim is the widest thing on the fighter, so of the four boxes up
        // here it is the only one with a silhouette of its own — a shadow with
        // a hat on is most of what says which way a head is turned.
        part(upper, {0.0f, 1.70f, 0.0f}, {0.56f, 0.06f, 0.56f}, kStraw);
        part(upper, {0.0f, 1.75f, 0.0f}, {0.36f, 0.06f, 0.36f}, kStraw, true);
        part(upper, {0.0f, 1.80f, 0.0f}, {0.18f, 0.05f, 0.18f}, kStrawDark, true);
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
    part(katana, {-0.30f, 0.0f, 0.0f}, {0.60f, 0.05f, 0.05f}, kLacquer, true); // scabbard
}

// The floor band a fighter's eleven shadow slabs are stacked in, emitted top
// down (two thighs, two shins, hip skirt, torso, two arms, guard, blade, kasa).
// The pipeline writes depth, so wherever two of them overlap the higher one
// wins and the lower is rejected outright — which is exactly what makes a body
// print one flat silhouette instead of a heap of translucent rectangles going
// darker at every joint where they pile up. It is the same depth-write the
// shafts have to build their lanes around, used the other way about.
//
// So the band is a *stack*, and everything about it is ordering. The step has
// to be wide enough that the depth buffer can still tell two slabs apart out at
// the far end of a camera pull-back, and the whole band has to sit clear of the
// blood marks below it (which reach y = 0.022 — Game::addBloodMark's 0.016 of
// jitter plus half their drawn thickness) or a fresh stain would punch holes
// through a shadow crossing it. Eleven slabs at this step reach 0.024, so the
// headroom is 2 mm: another caster wants a look at these three numbers rather
// than just a flag. It costs nothing to be generous otherwise: the camera
// looks level rather than down, so lift is a fraction of a percent of screen
// height and not the slide across the ground it would be in a top-down game.
constexpr float kShadowTop = 0.030f;
constexpr float kShadowStep = 0.0006f;
constexpr float kShadowSlab = 0.0004f; // under the step, so no two slabs meet

} // namespace

void drawSamurai(Renderer& renderer, const glm::vec3& feet, float yaw,
                 const SamuraiPose& pose, const SamuraiColors& colors) {
    buildSamurai(feet, yaw, pose, colors,
                 [&](const glm::mat4& model, const glm::vec4& color, bool) {
                     renderer.drawBox(model, color);
                 });
}

void drawSamuraiShadow(Renderer& renderer, const glm::vec3& feet, float yaw,
                       const SamuraiPose& pose, const SunDef& sun) {
    // Softness and darkness come from how high the *fighter* is off the floor,
    // not from each box's own height the way scenery's does. A samurai is not a
    // tree: the parts are all within arm's length of each other, and grading
    // them separately would blur the kasa to a smudge while the sandals stayed
    // crisp. Taken off the body instead, one number does the job the blob was
    // really there for — a fighter in the air throws a wide faint shadow well
    // off to the side, which is how you read where they will land.
    //
    // A fighter also prints harder than the scenery around them: the level's
    // own `shadow` is the strength of a thing standing *in* the battleground,
    // and the two people fighting in it are what the eye is actually tracking.
    // kShadowWeight puts a grounded fighter back at about the weight of the
    // blob this replaced — under it, a shadow on Hanami's grass was there but
    // not readable, which is the one job it cannot fail at.
    constexpr float kShadowWeight = 1.5f;
    const float height = std::max(feet.y, 0.0f);
    const levels::ShadowSlab slab{kShadowTop, kShadowSlab, 0.035f + height * 0.07f,
                                  kShadowWeight / (1.0f + height * 0.30f)};

    int cast = 0;
    buildSamurai(feet, yaw, pose, SamuraiColors{},
                 [&](const glm::mat4& model, const glm::vec4&, bool detail) {
                     if (detail) {
                         return; // inside another box's silhouette already
                     }
                     levels::ShadowSlab s = slab;
                     s.y -= static_cast<float>(cast++) * kShadowStep;
                     levels::sunShadowBox(renderer, model, sun, s);
                 });
}

float bladeSteelLength(float reachBonus) {
    // Same span the in-hand blade draws, at the baseline reach: guard to tip.
    // A full length, like the size drawSamurai hands boxAt and the one
    // drawDroppedBlade lays along +x — halving it here made a thrown sword
    // half the sword that left the hand.
    return kBladeTop - (kShoulderY - (kNominalReach + reachBonus));
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
