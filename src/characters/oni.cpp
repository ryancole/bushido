#include "characters/registry.hpp"

// Oni — the mass end of the roster. Slowest walk, lowest jump, and a swing
// that telegraphs badly (0.19 windup, 0.40 recovery): whiffing is genuinely
// punishing. What it buys is 11.5 knockback and a 1.55 weight, and since
// knockback dealt is divided by the victim's weight, the two numbers are
// tuned against each other — an Oni shoving another Oni lands 11.5/1.55 ≈ the
// baseline 8, while an Oni shoving a Shinobi lands 11.5/0.75 ≈ 15.

namespace characters::oni {

const CharacterDef kDef = {
    .id = "oni",
    .name = "Oni",
    .epithet = "The mountain that walks",
    .stats =
        {
            .moveSpeed = 4.6f,
            .jumpVelocity = 8.5f,
            .windupTime = 0.19f,
            .activeTime = 0.16f,
            .recoveryTime = 0.40f,
            .reach = 1.72f,
            .knockback = 11.5f,
            .weight = 1.55f,
        },
    // Near-black kimono, dried-blood hakama, ember accents.
    .colors =
        {
            .kimono = {0.13f, 0.12f, 0.13f, 1.0f},
            .hakama = {0.38f, 0.07f, 0.07f, 1.0f},
            .accent = {0.86f, 0.55f, 0.15f, 1.0f},
        },
    .rSpeed = 1,
    .rPower = 5,
    .rReach = 4,
    .rWeight = 5,
};

} // namespace characters::oni
