#include "characters/registry.hpp"

// Kensei — reach as the whole identity. At 1.95 m it out-ranges everyone by a
// clear margin, which matters twice: the sweep starts connecting sooner, and
// the drawn blade is that long too (SamuraiPose::reach), so the threat is
// visible. Everything else is deliberately unremarkable — baseline weight,
// modest knockback, a swing a touch slower than the Ronin's — so the fighter
// lives or dies on holding the spacing its reach earns.

namespace characters::kensei {

const CharacterDef kDef = {
    .id = "kensei",
    .name = "Kensei",
    .epithet = "Death at a distance",
    .stats =
        {
            .moveSpeed = 5.4f,
            .jumpVelocity = 9.5f,
            .windupTime = 0.16f,
            .activeTime = 0.14f,
            .recoveryTime = 0.33f,
            .reach = 1.95f,
            .knockback = 7.0f,
            .weight = 1.0f,
        },
    // Bleached white over cold blue-grey, deep violet accents.
    .colors =
        {
            .kimono = {0.82f, 0.78f, 0.70f, 1.0f},
            .hakama = {0.22f, 0.24f, 0.30f, 1.0f},
            .accent = {0.42f, 0.16f, 0.45f, 1.0f},
        },
    .rSpeed = 2,
    .rPower = 2,
    .rReach = 5,
    .rWeight = 3,
};

} // namespace characters::kensei
