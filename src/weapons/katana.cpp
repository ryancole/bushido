#include "weapons/registry.hpp"

// Katana — the neutral loadout, and the reason character tuning can be read
// on its own. Every scale is 1 and the reach bonus is 0, so equipping it
// leaves the wielder's CharacterStats exactly as authored; the other two
// blades are trades measured against this one.

namespace weapons::katana {

const WeaponDef kDef = {
    .id = "katana",
    .name = "Katana",
    .epithet = "The soul of the samurai",
    .stats =
        {
            .swingScale = 1.0f,
            .damage = 1.0f,
            .reachBonus = 0.0f,
            .knockbackScale = 1.0f,
            .bladeWidth = 1.0f,
        },
    .tileColor = {0.30f, 0.32f, 0.40f, 0.75f}, // steel
    .rSpeed = 3,
    .rDamage = 3,
    .rReach = 3,
};

} // namespace weapons::katana
