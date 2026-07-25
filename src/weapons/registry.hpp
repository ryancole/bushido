#pragma once

#include "weapons/weapon.hpp"

// Internal wiring between the armory dispatch (weapon.cpp) and the per-blade
// files. Every weapon owns exactly one .cpp holding its whole definition —
// the scales it applies to the wielder, its tile color, its select-screen
// ratings — plus the note on what equipping it costs, which is the part that
// doesn't survive being one row in a shared table.
//
// Adding a blade: write weapons/<name>.cpp, declare its namespace below, add a
// row to weapon.cpp's table, and bump kWeaponCount in weapon.hpp.
//
// Like a character and unlike a level, a weapon carries no code of its own — a
// namespace here holds nothing but `kDef`. Per-blade hooks (a bespoke arc, a
// special) would go beside it, the way levels/registry.hpp carries
// draw/obstacles/water.

namespace weapons {

namespace katana {
extern const WeaponDef kDef;
} // namespace katana

namespace wakizashi {
extern const WeaponDef kDef;
} // namespace wakizashi

namespace odachi {
extern const WeaponDef kDef;
} // namespace odachi

} // namespace weapons
