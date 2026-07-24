#include "weapon.hpp"

#include <algorithm>

namespace {
// The katana is the neutral loadout (all scales 1, no reach bonus) so the
// character roster's tuning is unchanged when it's equipped. The other two
// trade the swing's speed against its price: the wakizashi lands often and
// cheaply, the odachi rarely and ruinously. Beheading is untouched by the
// damage scale — the execution empties the pool no matter the blade.
const WeaponDef kArmory[kWeaponCount] = {
    {
        "katana",
        "Katana",
        "The soul of the samurai",
        {1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {0.30f, 0.32f, 0.40f, 0.75f},
        3, 3, 3,
    },
    {
        "wakizashi",
        "Wakizashi",
        "A whisper of steel",
        {0.70f, 0.60f, -0.25f, 0.75f, 0.80f},
        {0.62f, 0.63f, 0.60f, 0.75f},
        5, 2, 1,
    },
    {
        "odachi",
        "Odachi",
        "The field-cleaver",
        {1.45f, 2.00f, 0.30f, 1.50f, 1.50f},
        {0.20f, 0.10f, 0.09f, 0.75f},
        1, 5, 5,
    },
};
} // namespace

const WeaponDef& weaponDef(int index) {
    return kArmory[std::clamp(index, 0, kWeaponCount - 1)];
}
