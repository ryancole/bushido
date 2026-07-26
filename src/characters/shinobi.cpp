#include "characters/registry.hpp"

// Shinobi — speed bought with everything else. Fastest on the ground and the
// highest jump, and the whole swing (windup 0.09 / active 0.12 / recovery
// 0.21) undercuts the Ronin's, so it wins exchanges by striking first rather
// than by striking hard. The price is the shortest reach in the cast and a
// 0.75 weight: it has to get inside to work, and every hit it takes shoves it
// a third further than it would shove anyone else.

namespace characters::shinobi {

const CharacterDef kDef = {
    .id = "shinobi",
    .name = "Shinobi",
    .epithet = "Strikes before the breath",
    .stats =
        {
            .moveSpeed = 1.90f,
            .jumpVelocity = 11.5f,
            .windupTime = 0.09f,
            .activeTime = 0.12f,
            .recoveryTime = 0.21f,
            .reach = 1.40f,
            .knockback = 6.0f,
            .weight = 0.75f,
        },
    // Indigo over slate, bone-pale accents.
    .colors =
        {
            .kimono = {0.15f, 0.28f, 0.72f, 1.0f},
            .hakama = {0.15f, 0.16f, 0.20f, 1.0f},
            .accent = {0.80f, 0.78f, 0.70f, 1.0f},
        },
    .rSpeed = 5,
    .rPower = 2,
    .rReach = 2,
    .rWeight = 2,
};

} // namespace characters::shinobi
