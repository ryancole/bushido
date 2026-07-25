#include "characters/registry.hpp"

// Ronin — the tuning baseline. Every number here is the old pre-roster global
// constant, and the rest of the cast is authored as trades against it, so a
// change to this file is a change to what "average" means. Ratings sit at 3
// across the board for the same reason.

namespace characters::ronin {

const CharacterDef kDef = {
    .id = "ronin",
    .name = "Ronin",
    .epithet = "The wandering blade",
    .stats =
        {
            .moveSpeed = 6.0f,
            .jumpVelocity = 10.0f,
            .windupTime = 0.12f,
            .activeTime = 0.14f,
            .recoveryTime = 0.28f,
            .reach = 1.60f,
            .knockback = 8.0f,
            .weight = 1.0f,
        },
    // Deep red kimono over near-black hakama, gold obi and grip wrap.
    .colors =
        {
            .kimono = {0.72f, 0.13f, 0.13f, 1.0f},
            .hakama = {0.17f, 0.15f, 0.17f, 1.0f},
            .accent = {0.85f, 0.70f, 0.25f, 1.0f},
        },
    .rSpeed = 3,
    .rPower = 3,
    .rReach = 3,
    .rWeight = 3,
};

} // namespace characters::ronin
