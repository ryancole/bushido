#include "characters/character.hpp"

#include "characters/registry.hpp"

#include <algorithm>

// Nothing but the index → definition dispatch. Each fighter's numbers and the
// reasoning behind them live in its own file; this table is the only place
// that knows the roster order — which is also the select-screen order and the
// netplay index, so rows are appended, never reordered.

namespace {

const CharacterDef* const kRoster[kCharacterCount] = {
    &characters::ronin::kDef,
    &characters::shinobi::kDef,
    &characters::oni::kDef,
    &characters::kensei::kDef,
};

} // namespace

const CharacterDef& characterDef(int index) {
    return *kRoster[std::clamp(index, 0, kCharacterCount - 1)];
}
