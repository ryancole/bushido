#include "character.hpp"

#include <algorithm>

namespace {
// The Ronin is the tuning baseline (the pre-roster constants); everyone else
// trades off around it. Knockback dealt is divided by the victim's weight, so
// e.g. Oni-vs-Oni shoves land at 11.5/1.55 ≈ the baseline 8.
const CharacterDef kRoster[kCharacterCount] = {
    {
        "ronin",
        "Ronin",
        "The wandering blade",
        {6.0f, 10.0f, 0.12f, 0.14f, 0.28f, 1.60f, 8.0f, 1.0f},
        {{0.72f, 0.13f, 0.13f, 1.0f}, {0.17f, 0.15f, 0.17f, 1.0f}, {0.85f, 0.70f, 0.25f, 1.0f}},
        3, 3, 3, 3,
    },
    {
        "shinobi",
        "Shinobi",
        "Strikes before the breath",
        {7.6f, 11.5f, 0.09f, 0.12f, 0.21f, 1.40f, 6.0f, 0.75f},
        {{0.15f, 0.28f, 0.72f, 1.0f}, {0.15f, 0.16f, 0.20f, 1.0f}, {0.80f, 0.78f, 0.70f, 1.0f}},
        5, 2, 2, 2,
    },
    {
        "oni",
        "Oni",
        "The mountain that walks",
        {4.6f, 8.5f, 0.19f, 0.16f, 0.40f, 1.72f, 11.5f, 1.55f},
        {{0.13f, 0.12f, 0.13f, 1.0f}, {0.38f, 0.07f, 0.07f, 1.0f}, {0.86f, 0.55f, 0.15f, 1.0f}},
        1, 5, 4, 5,
    },
    {
        "kensei",
        "Kensei",
        "Death at a distance",
        {5.4f, 9.5f, 0.16f, 0.14f, 0.33f, 1.95f, 7.0f, 1.0f},
        {{0.82f, 0.78f, 0.70f, 1.0f}, {0.22f, 0.24f, 0.30f, 1.0f}, {0.42f, 0.16f, 0.45f, 1.0f}},
        2, 2, 5, 3,
    },
};
} // namespace

const CharacterDef& characterDef(int index) {
    return kRoster[std::clamp(index, 0, kCharacterCount - 1)];
}
