#include "bot.hpp"

#include "input.hpp" // quantizeAxis: the bot may only press what a player could

#include <algorithm>
#include <cmath>

namespace {
// Strike band, as fractions of the character's reach: inside the low edge
// the blade arc mostly passes over the foe, past the high edge it whiffs.
constexpr float kBandLo = 0.50f;
constexpr float kBandHi = 0.90f;
constexpr float kDepthAligned = 0.50f; // |dz| where a swing can still connect
// Where the bot stops walking and reaches for the blade underfoot. Inside
// Game::kPickupRange with room to spare, so the press always lands: pressing
// early is harmless (the sim just says no) but it reads as fumbling, and
// stopping short of the range would be a bot standing over a sword forever.
constexpr float kGrabRange = 0.75f;
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

    // Where a fighter's body actually is in depth, which for a toppled one is
    // not where its capsule is: the fall rolls the whole model about the local
    // +x axis at the feet, so the torso swings nearly a meter off `pos.z` and
    // the head half again as far (main stretches the blob shadow the same way,
    // for the same reason). Steering at the capsule leaves the blade lane
    // sweeping the empty floor beside a body — connecting only when the hit
    // pads happen to reach — which is a bot that stands over a downed opponent
    // and cannot finish them. Half height is the torso's own line: aim at the
    // body, not at the feet it fell away from.
    //
    // Applied to both fighters, so it also answers the other half of it: a
    // *downed bot's* own blade sweeps from a shoulder that has swung just as
    // far out of line. Upright, sin(0) is 0 and this is exactly what it was.
    auto lieZ = [](const Player& p) {
        return p.pos.z + p.facing * std::sin(p.bodyRoll()) * Player::kHalfHeight;
    };

    const float dx = foe.pos.x - self.pos.x;
    const float dz = lieZ(foe) - lieZ(self);
    const float adx = std::abs(dx);
    const float toFoe = dx >= 0.0f ? 1.0f : -1.0f;

    m_modeTimer -= dt;
    m_attackDelay -= dt;
    m_blockTimer -= dt;

    // Seeing the foe start a windup in range triggers a defensive read:
    // sometimes raise the guard and catch the blow, sometimes step out of
    // the blade's path — and sometimes just trade.
    const bool foeWinding = foe.attackState == AttackState::Windup;
    if (foeWinding && !m_foeWasWinding && adx < foeReach + 0.7f) {
        float roll = frand();
        if (roll < 0.25f && self.attackState == AttackState::None && !self.downed()) {
            m_blockTimer = 0.30f + 0.30f * frand();
        } else if (roll < 0.55f) {
            m_mode = Mode::Retreat;
            m_modeTimer = 0.20f + 0.20f * frand();
        }
    }
    m_foeWasWinding = foeWinding;

    // Empty-handed, nothing else on this page matters: with no blade there is
    // no attack and no guard, so the only move that changes anything is
    // getting one back in hand — the one the other fighter threw away, or
    // whatever a level has lying around. Without this a disarmed bot walks
    // into sweeps it cannot answer until somebody runs out of blood, which
    // between two disarmed fighters is never.
    //
    // Both guards matter, and the arm one is not theoretical. The usual way a
    // bot ends up empty-handed is losing *both arms*, which is exactly the
    // state that can never take a blade back up (Game::takeUpWeapon refuses) —
    // so without the check it walks to the sword it just dropped and stands
    // over it pressing take-up for the rest of the match, which is worse than
    // the behaviour this branch exists to fix. Armless, or with nothing on the
    // ground to go for, it falls through to the ordinary mode machine and at
    // least stays in the fight while its stumps bleed it out.
    const bool hasArm = !self.severed[static_cast<int>(Limb::ArmFront)] ||
                        !self.severed[static_cast<int>(Limb::ArmBack)];
    if (!self.armed() && hasArm && !game.droppedWeapons().empty()) {
        bool found = false;
        float bestDist = 0.0f;
        glm::vec3 bestAt{0.0f};
        for (const DroppedWeapon& blade : game.droppedWeapons()) {
            const glm::vec3 at = game.droppedWeaponPos(blade);
            const float d = std::sqrt((at.x - self.pos.x) * (at.x - self.pos.x) +
                                      (at.z - self.pos.z) * (at.z - self.pos.z));
            if (!found || d < bestDist) {
                found = true;
                bestDist = d;
                bestAt = at;
            }
        }
        // Straight at it. Quantized like every other steering decision the bot
        // makes — it only gets to press what a player could press — which
        // leaves a small deadzone either side of the blade, well inside the
        // range the grab goes off at.
        PlayerInput grab;
        grab.move.x = quantizeAxis((bestAt.x - self.pos.x) * 2.0f);
        grab.move.y = quantizeAxis((bestAt.z - self.pos.z) * 2.0f);
        grab.drop = bestDist < kGrabRange;
        return grab;
    }

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

    // z: steer into the foe's depth lane; strafing jinks instead. Quantized
    // because the bot only gets to press what a player could press — an analog
    // lean would be an input no recording or netplay peer can carry, and the
    // rounding would land somewhere the bot never chose. The 0.5 threshold
    // quantizeAxis applies leaves a ~0.25 m deadzone in the lane, which is
    // close enough to lined up to swing.
    if (m_mode == Mode::Strafe) {
        in.move.y = m_strafeSign;
    } else {
        in.move.y = quantizeAxis(std::clamp(dz * 2.0f, -1.0f, 1.0f));
    }

    if (m_jumpQueued && self.grounded && !self.downed()) {
        in.jump = true;
        m_jumpQueued = false;
    }

    // A caught blow drops the guard for the counter: the riposte window
    // outranks whatever the block timer had left.
    in.block = m_blockTimer > 0.0f && self.riposteTime <= 0.0f;

    // Finish what is already on the floor. The blade pivots at the shoulder,
    // so a standing swing sweeps a lane a toppled fighter lies clean under —
    // and a bot that keeps swinging over a body it cannot reach is how a match
    // ends on the clock instead of with a kill. Crouching drops the pivot (and
    // with it the whole arc) by kCrouchDrop, which is what puts the cut down
    // where the body actually is.
    //
    // Only in striking range: the duck costs more than half the walk speed,
    // which is a bad way to cross the arena, and the crouch is full inside
    // ~0.1 s — well under any windup — so there is nothing to gain by
    // starting it earlier. The sim ignores the press while the bot is itself
    // downed, so a crawling bot is not asking for anything it cannot have.
    in.crouch = foe.downed() && adx < st.reach + 0.25f;

    // Swing when lined up and off cooldown. The cooldown is what makes the
    // bot beatable: it won't re-swing the instant recovery ends — except a
    // riposte, which it takes the moment it's earned. The guard wins while
    // it's up (the sim would drop the press anyway).
    if ((m_attackDelay <= 0.0f || self.riposteTime > 0.0f) && !in.block &&
        self.attackState == AttackState::None &&
        adx < st.reach + 0.25f && std::abs(dz) < kDepthAligned) {
        in.attack = true;
        // Pick the blow: mostly the standard cut. The heavy's long windup
        // only commits when the foe can't answer (hitstun or downed) — plus
        // the rare greedy read — and the jab is the fast answer in close.
        const bool foeHelpless = foe.hitstun > 0.0f || foe.downed();
        const float roll = frand();
        if (roll < (foeHelpless ? 0.45f : 0.08f)) {
            in.attackKind = AttackKind::Heavy;
        } else if (adx < st.reach * 0.65f && frand() < 0.45f) {
            in.attackKind = AttackKind::Jab;
        }
        m_attackDelay = 0.35f + 0.75f * frand();
    }

    return in;
}
