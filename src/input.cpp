#include "input.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace {

// The held half of the controls. Everything here is a level rather than an
// edge, so it is read fresh each frame with nothing remembered between them.
PlayerInput readHeld(GLFWwindow* window, const Keybinds& keys) {
    PlayerInput in;
    if (bindHeld(window, keys[Action::MoveLeft])) in.move.x -= 1.0f;
    if (bindHeld(window, keys[Action::MoveRight])) in.move.x += 1.0f;
    if (bindHeld(window, keys[Action::MoveAway])) in.move.y -= 1.0f;   // into the screen (-z)
    if (bindHeld(window, keys[Action::MoveToward])) in.move.y += 1.0f; // toward the camera (+z)
    in.jump = bindHeld(window, keys[Action::Jump]);
    in.sprint = bindHeld(window, keys[Action::Sprint]);
    in.block = bindHeld(window, keys[Action::Block]);
    in.crouch = bindHeld(window, keys[Action::Crouch]);
    return in;
}

} // namespace

float quantizeAxis(float v) {
    return v < -0.5f ? -1.0f : (v > 0.5f ? 1.0f : 0.0f);
}

std::uint16_t packInput(const PlayerInput& in) {
    std::uint16_t bits = 0;
    bits |= static_cast<std::uint16_t>(quantizeAxis(in.move.x) + 1.0f);
    bits |= static_cast<std::uint16_t>((quantizeAxis(in.move.y) + 1.0f)) << 2;
    if (in.jump) bits |= 1u << 4;
    if (in.block) bits |= 1u << 5;
    if (in.crouch) bits |= 1u << 6;
    if (in.attack) bits |= 1u << 7;
    bits |= static_cast<std::uint16_t>((static_cast<int>(in.attackKind) & 3) << 8);
    if (in.drop) bits |= 1u << 10;
    if (in.sprint) bits |= 1u << 11;
    return bits;
}

PlayerInput unpackInput(std::uint16_t bits) {
    PlayerInput in;
    in.move.x = static_cast<float>(static_cast<int>(bits & 3u) - 1);
    in.move.y = static_cast<float>(static_cast<int>((bits >> 2) & 3u) - 1);
    in.jump = (bits & (1u << 4)) != 0;
    in.block = (bits & (1u << 5)) != 0;
    in.crouch = (bits & (1u << 6)) != 0;
    in.attack = (bits & (1u << 7)) != 0;
    int kind = (bits >> 8) & 3;
    // A third bit pattern is unreachable from packInput, but a corrupt or
    // hostile packet is not — fall back rather than index a table out of range.
    in.attackKind = kind < kAttackKindCount ? static_cast<AttackKind>(kind)
                                            : AttackKind::Light;
    in.drop = (bits & (1u << 10)) != 0;
    in.sprint = (bits & (1u << 11)) != 0;
    return in;
}

void LocalInput::beginMatch(GLFWwindow* window, const Keybinds& keys) {
    m_input = PlayerInput{};
    m_attackHeld = bindHeld(window, keys[Action::Attack]);
    m_jabHeld = bindHeld(window, keys[Action::Jab]);
    // Seeded like the others so a control already down at the whistle can't
    // read as a fresh press and throw the blade away on the opening frame.
    m_dropHeld = bindHeld(window, keys[Action::Drop]);
    m_suppressed = m_attackHeld;
    m_holdTime = 0.0f;
    m_pending = false;
    m_pendingKind = AttackKind::Light;
    m_dropPending = false;
}

void LocalInput::poll(GLFWwindow* window, const Keybinds& keys, float frameTime) {
    m_input = readHeld(window, keys);

    // Attack: light on a tap's release, heavy on a long hold's release. Both
    // share one control, so which one it was isn't known until it comes up.
    bool attack = bindHeld(window, keys[Action::Attack]);
    if (attack && !m_attackHeld) {
        m_holdTime = 0.0f; // fresh press: start the charge clock
    }
    if (attack) {
        m_holdTime += frameTime;
    } else if (m_attackHeld) { // release edge
        if (m_suppressed) {
            m_suppressed = false; // the click that started the match
        } else if (!m_pending) {
            m_pendingKind =
                m_holdTime >= kHeavyHoldTime ? AttackKind::Heavy : AttackKind::Light;
            m_pending = true;
        }
    }
    m_attackHeld = attack;

    // Jab on press.
    bool jab = bindHeld(window, keys[Action::Jab]);
    if (jab && !m_jabHeld && !m_pending) {
        m_pendingKind = AttackKind::Jab;
        m_pending = true;
    }
    m_jabHeld = jab;

    // Drop / take up on press. One control for both, because which one the
    // player means is never in doubt from where they are standing — and the
    // sim is the only thing that knows whether there is a blade in hand.
    bool drop = bindHeld(window, keys[Action::Drop]);
    if (drop && !m_dropHeld) {
        m_dropPending = true;
    }
    m_dropHeld = drop;

    m_input.attack = m_pending;
    m_input.attackKind = m_pendingKind;
    m_input.drop = m_dropPending;
}

void LocalInput::consumeEdges() {
    m_pending = false;
    m_input.attack = false;
    m_dropPending = false;
    m_input.drop = false;
}
