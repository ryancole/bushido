#pragma once

#include "characters/character.hpp"

// Internal wiring between the roster dispatch (character.cpp) and the
// per-fighter files. Every fighter owns exactly one .cpp holding its whole
// definition — stats, look, select-screen ratings — plus the design notes
// explaining what it trades away, which is the part that doesn't survive being
// one row in a shared table.
//
// Adding a fighter: write characters/<name>.cpp, declare its namespace below,
// add a row to character.cpp's table, and bump kCharacterCount in
// character.hpp.
//
// A character's file may carry code as well as data: an adorn hook
// (SamuraiLook::adorn) is bespoke model geometry authored beside kDef, the
// way levels/registry.hpp carries draw/obstacles/water — a file-local
// function wired into the def, no declaration needed here. The shared body,
// its gaits and its hurtboxes stay in samurai.cpp; what belongs in a
// character's file is only what makes that fighter look like themselves.

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
