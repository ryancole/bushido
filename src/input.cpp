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
    in.block = bindHeld(window, keys[Action::Block]);
    in.crouch = bindHeld(window, keys[Action::Crouch]);
    return in;
}

} // namespace

void LocalInput::beginMatch(GLFWwindow* window, const Keybinds& keys) {
    m_input = PlayerInput{};
    m_attackHeld = bindHeld(window, keys[Action::Attack]);
    m_jabHeld = bindHeld(window, keys[Action::Jab]);
    m_suppressed = m_attackHeld;
    m_holdTime = 0.0f;
    m_pending = false;
    m_pendingKind = AttackKind::Light;
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

    m_input.attack = m_pending;
    m_input.attackKind = m_pendingKind;
}

void LocalInput::consumeAttack() {
    m_pending = false;
    m_input.attack = false;
}
