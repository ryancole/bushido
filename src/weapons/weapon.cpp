#include "weapons/weapon.hpp"

#include "weapons/registry.hpp"

#include <algorithm>

// Nothing but the index → definition dispatch. Each blade's numbers and the
// reasoning behind them live in its own file; this table is the only place
// that knows the armory order — which is also the select-screen order and the
// netplay index, so rows are appended, never reordered.

namespace {

const WeaponDef* const kArmory[kWeaponCount] = {
    &weapons::katana::kDef,
    &weapons::wakizashi::kDef,
    &weapons::odachi::kDef,
};

} // namespace

const WeaponDef& weaponDef(int index) {
    return *kArmory[std::clamp(index, 0, kWeaponCount - 1)];
}
