#include "characters/registry.hpp"

// Oni — the mass end of the roster. Slowest walk, lowest jump, and a swing
// that telegraphs badly (0.19 windup, 0.40 recovery): whiffing is genuinely
// punishing. What it buys is 11.5 knockback and a 1.55 weight, and since
// knockback dealt is divided by the victim's weight, the two numbers are
// tuned against each other — an Oni shoving another Oni lands 11.5/1.55 ≈ the
// baseline 8, while an Oni shoving a Shinobi lands 11.5/0.75 ≈ 15.
//
// The look is the weight stat made visible: horns instead of a hat, iron
// piled on both shoulders, and a shrine rope for a belt — the demon the name
// claims. All of it reads as mass, which is the honest cue, since mass is
// what the sim actually gives this fighter.

namespace characters::oni {

namespace {

const glm::vec4 kIron{0.18f, 0.17f, 0.18f, 1.0f};  // hammered plate
const glm::vec4 kEmber{0.86f, 0.55f, 0.15f, 1.0f}; // the accent, as plate rim
const glm::vec4 kRope{0.70f, 0.60f, 0.40f, 1.0f};  // braided shimenawa straw

// Heavy sode: three slabs stepping down and outward off each shoulder, the
// top rim in ember. All detail — the slabs hug the shoulder-and-arm
// silhouette, so the shadow already tells their story and they spend nothing
// from its budget. Torso only, per the AdornFn rules: the arms under these
// plates come off, and armor authored here would not leave with them.
void adorn(const AdornContext& ctx, const AdornPart& part) {
    for (float s : {-1.0f, 1.0f}) {
        part(ctx.upper, {0.0f, 1.46f, s * 0.26f}, {0.22f, 0.055f, 0.24f}, kEmber, true);
        part(ctx.upper, {0.0f, 1.40f, s * 0.29f}, {0.20f, 0.06f, 0.24f}, kIron, true);
        part(ctx.upper, {0.0f, 1.34f, s * 0.32f}, {0.18f, 0.06f, 0.22f}, kIron, true);
    }
    // The shimenawa riding above the obi — a rope thick enough to read at
    // fighting distance, in straw against the near-black kimono.
    part(ctx.upper, {0.0f, 1.07f, 0.0f}, {0.46f, 0.075f, 0.36f}, kRope, true);
}

} // namespace

const CharacterDef kDef = {
    .id = "oni",
    .name = "Oni",
    .epithet = "The mountain that walks",
    .stats =
        {
            .moveSpeed = 0.65f,
            .jumpVelocity = 8.5f,
            .windupTime = 0.19f,
            .activeTime = 0.16f,
            .recoveryTime = 0.40f,
            .reach = 1.72f,
            .knockback = 11.5f,
            .weight = 1.55f,
        },
    // Near-black kimono, dried-blood hakama, ember accents; horned, ironed
    // and roped by the adorn hook above.
    .look =
        {
            .colors =
                {
                    .kimono = {0.13f, 0.12f, 0.13f, 1.0f},
                    .hakama = {0.38f, 0.07f, 0.07f, 1.0f},
                    .accent = {0.86f, 0.55f, 0.15f, 1.0f},
                },
            .headgear = Headgear::Horns,
            .adorn = adorn,
        },
    .rSpeed = 1,
    .rPower = 5,
    .rReach = 4,
    .rWeight = 5,
};

} // namespace characters::oni
