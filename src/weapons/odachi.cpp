#include "weapons/registry.hpp"

// Odachi — rare and ruinous. Doubled damage and half again the knockback on
// top of 0.30 m of extra reach, which on a Kensei stretches the sweep past
// 2.2 m. The 1.45 swing scale is the whole price: it stretches windup,
// active, and recovery together, so a miss leaves the wielder committed long
// enough to be dismembered for it. Pairs with the fighters who can survive
// the recovery frames, not the ones who need tempo.

namespace weapons::odachi {

const WeaponDef kDef = {
    .id = "odachi",
    .name = "Odachi",
    .epithet = "The field-cleaver",
    .stats =
        {
            .swingScale = 1.45f,
            .damage = 2.00f,
            .reachBonus = 0.30f,
            .knockbackScale = 1.50f,
            .bladeWidth = 1.50f,
        },
    .tileColor = {0.20f, 0.10f, 0.09f, 0.75f}, // dark iron
    .rSpeed = 1,
    .rDamage = 5,
    .rReach = 5,
};

} // namespace weapons::odachi
