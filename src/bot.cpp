#include "bot.hpp"

#include <algorithm>
#include <cmath>

namespace {
// Strike band, as fractions of the character's reach: inside the low edge
// the blade arc mostly passes over the foe, past the high edge it whiffs.
constexpr float kBandLo = 0.50f;
constexpr float kBandHi = 0.90f;
constexpr float kDepthAligned = 0.50f; // |dz| where a swing can still connect
} // namespace

Bot::Bot(int self, std::uint32_t seed) : m_self(self), m_rng(seed | 1u) {}

float Bot::frand() {
    m_rng = m_rng * 1664525u + 1013904223u;
    return static_cast<float>(m_rng >> 8) * (1.0f / 16777216.0f);
}

PlayerInput Bot::think(const Game& game, float dt) {
    const Player& self = game.player(m_self);
    const Player& foe = game.player(1 - m_self);
    // Resolved stats (character + weapon) — the strike band must match the
    // blade actually in hand, not the bare roster reach.
    const CharacterStats& st = game.stats(m_self);
    const float foeReach = game.stats(1 - m_self).reach;

    const float dx = foe.pos.x - self.pos.x;
    const float dz = foe.pos.z - self.pos.z;
    const float adx = std::abs(dx);
    const float toFoe = dx >= 0.0f ? 1.0f : -1.0f;

    m_modeTimer -= dt;
    m_attackDelay -= dt;

    // Seeing the foe start a windup in range sometimes triggers a dodge:
    // step out of the blade's path instead of trading.
    const bool foeWinding = foe.attackState == AttackState::Windup;
    if (foeWinding && !m_foeWasWinding && adx < foeReach + 0.7f && frand() < 0.45f) {
        m_mode = Mode::Retreat;
        m_modeTimer = 0.20f + 0.20f * frand();
    }
    m_foeWasWinding = foeWinding;

    if (m_modeTimer <= 0.0f) {
        float roll = frand();
        m_mode = roll < 0.60f ? Mode::Approach
                              : (roll < 0.85f ? Mode::Strafe : Mode::Retreat);
        m_modeTimer = 0.25f + 0.45f * frand();
        m_strafeSign = frand() < 0.5f ? -1.0f : 1.0f;
        // The occasional closing hop, only from far enough out to land it.
        m_jumpQueued = m_mode == Mode::Approach && adx > 3.0f && frand() < 0.18f;
    }

    PlayerInput in;

    // x: hold the strike band relative to own reach; retreat overrides.
    if (m_mode == Mode::Retreat) {
        in.move.x = -toFoe;
    } else if (adx > st.reach * kBandHi) {
        in.move.x = m_mode == Mode::Approach ? toFoe : 0.0f;
    } else if (adx < st.reach * kBandLo) {
        in.move.x = -toFoe; // too close for the arc — step back out
    }

    // z: steer into the foe's depth lane; strafing jinks instead.
    if (m_mode == Mode::Strafe) {
        in.move.y = m_strafeSign;
    } else {
        in.move.y = std::clamp(dz * 2.0f, -1.0f, 1.0f);
    }

    if (m_jumpQueued && self.grounded && !self.downed()) {
        in.jump = true;
        m_jumpQueued = false;
    }

    // Swing when lined up and off cooldown. The cooldown is what makes the
    // bot beatable: it won't re-swing the instant recovery ends.
    if (m_attackDelay <= 0.0f && self.attackState == AttackState::None &&
        adx < st.reach + 0.25f && std::abs(dz) < kDepthAligned) {
        in.attack = true;
        m_attackDelay = 0.35f + 0.75f * frand();
    }

    return in;
}
