#pragma once

#include "characters/character.hpp"

// Internal wiring between the roster dispatch (character.cpp) and the
// per-fighter files. Every fighter owns exactly one .cpp holding its whole
// definition — stats, palette, select-screen ratings — plus the design notes
// explaining what it trades away, which is the part that doesn't survive being
// one row in a shared table.
//
// Adding a fighter: write characters/<name>.cpp, declare its namespace below,
// add a row to character.cpp's table, and bump kCharacterCount in
// character.hpp.
//
// Unlike a level, a character carries no code of its own today — a namespace
// here holds nothing but `kDef`. If that changes (per-fighter move sets,
// bespoke model tweaks), the hooks belong beside kDef the way
// levels/registry.hpp carries draw/obstacles/water.

namespace characters {

namespace ronin {
extern const CharacterDef kDef;
} // namespace ronin

namespace shinobi {
extern const CharacterDef kDef;
} // namespace shinobi

namespace oni {
extern const CharacterDef kDef;
} // namespace oni

namespace kensei {
extern const CharacterDef kDef;
} // namespace kensei

} // namespace characters
