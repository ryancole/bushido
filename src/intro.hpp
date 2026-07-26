#pragma once

#include "game.hpp" // PlayerInput, and the fighters the sequences frame

#include <glm/glm.hpp>

// The two arcade intros, and the one thing they have in common: both take the
// camera away from the fighters for a few seconds and hand it back.
//
// Neither owns any state the sim can see. The versus screen does not step the
// sim at all — it holds it, which is the same freeze the menus already use, so
// a netplay peer holding its own copy of the sequence is nothing but a stall
// lockstep already knows how to sit through. The title sequence *does* step,
// but the world it steps is the menu diorama, which is thrown away and rebuilt
// the moment the sequence ends and has never been anybody's match.

// Where the camera is and what it looks at. That is all either sequence gets to
// say: the projection stays the gameplay one throughout (FramingCamera::proj),
// so a cut here is a change of viewpoint and never of lens — an intro that
// quietly widened the fov would hand the match back through a zoom.
struct CameraShot {
    glm::vec3 eye;
    glm::vec3 target;
};
glm::mat4 shotView(const CameraShot& shot);

// The pre-match versus screen: a close-up on each fighter in turn with their
// name slammed in beside them, a pull-back to the shot the match will be played
// from, and FIGHT! over the opening seconds of the duel.
//
// It runs *inside* AppState::Playing rather than as a state of its own. A match
// under an intro is still a match — Esc leaves it, a peer is still connected,
// the win overlay still cannot fire — and every one of those would need saying
// twice if the sequence were its own state. What it actually needs is one
// question answered for the fixed-step loop, which is `holdingSim`.
class VersusIntro {
public:
    void begin();
    void update(float dt);
    // Offer a skip for a player who has seen it — it jumps to the banner, not
    // past it. `anyDown` is whether any control is currently down, and the
    // sequence *arms* on the first frame none is: the click that picked the
    // battleground is still held when the match is built, so a skip taken on
    // the level alone would mean nobody ever saw this screen. (The options
    // screen's rebind capture is armed the same way, for the same reason.)
    // True on the frame it actually skips.
    bool offerSkip(bool anyDown);

    bool active() const { return m_active; }
    // Is the sim still held? False for the last stretch, deliberately: FIGHT!
    // belongs over a fight, so the banner outlives the freeze it announced.
    bool holdingSim() const { return m_active && m_t < kTour; }
    // True once, on the step the stamp lands — the caller turns it into a hit.
    bool takeStampCue();

    // This frame's viewpoint. `gameplay` is where FramingCamera has settled,
    // which the pull-back eases into and the banner leaves alone (bar the
    // shake), so the handover is a movement rather than a cut.
    CameraShot shot(const Game& game, const CameraShot& gameplay) const;
    void draw(const Game& game) const;

private:
    static constexpr float kPortrait = 0.95f; // one fighter's close-up
    static constexpr float kFaceoff = 0.8f;   // pull back to the framing shot
    static constexpr float kTour = 2.0f * kPortrait + kFaceoff;
    static constexpr float kBanner = 1.0f; // FIGHT! lingering into the match

    bool m_active = false;
    bool m_armed = false; // no control has been down since the match was built
    bool m_stampPending = false;
    float m_t = 0.0f;
};

// The attract-mode title sequence: two fighters charge, trade a blow, and the
// logo lands on the clash. Plays once between the loading screen and the menu.
//
// The duel is real rather than posed — the sequence emits ordinary PlayerInputs
// and the diorama simulates them, so the swing has the windup it has in a match
// and the block rings with the same clang. A pose table would have been a
// second, worse copy of the animation the model already does.
class TitleIntro {
public:
    void begin();
    void skip();
    bool offerSkip(bool anyDown); // armed on release, exactly as the versus one
    bool active() const { return m_active; }

    // One fixed step of the sequence: the inputs the script calls for over the
    // interval about to be simulated, and then the clock. Both in one call so
    // the attack *edges* can be exact — an attack is a single-step press, and a
    // caller free to ask for the inputs twice, or to advance without asking,
    // would fire it twice or not at all. The clock advances only here, so the
    // sequence is step-exact and plays out identically every launch rather than
    // drifting with the frame rate.
    void step(float dt, PlayerInput out[2]);

    bool takeSlamCue(); // true once, on the step the logo lands

    // Same contract the versus screen keeps: the sequence eases back into where
    // the menu's own camera is sitting, and the logo and bars fade with it, so
    // the handoff is the end of a shot rather than a cut. The diorama is
    // deliberately *not* rebuilt afterwards — the script settles both fighters
    // back into their guard before it ends, so what the menu comes up over is
    // the aftermath of the duel that was just fought in front of it.
    CameraShot shot(const Game& game, const CameraShot& gameplay) const;
    void draw() const;

private:
    // Beat times, in sequence seconds. The fight ones are what the script is
    // written against; the presentation ones hang off the same clock.
    // The fight beats are timed against the Ronin's heavy — 0.12 s of windup
    // scaled by 1.9, then 0.14 s scaled by 1.25 of active — because the guard
    // has to be up across the *whole* of that active window, not just its
    // start. Releasing it early is not a near miss: the blade lands unblocked,
    // and the first cut of this sequence took the Shinobi's leg off and left
    // him lying behind the main menu.
    static constexpr float kCharge = 0.95f;  // sprinting at each other
    static constexpr float kGuard = 1.20f;   // the defender's guard goes up
    static constexpr float kSwing = 1.30f;   // the heavy that gets caught
    static constexpr float kRiposte = 1.78f; // the counter, once the arc is spent
    static constexpr float kBreak = 2.00f;   // the exchange breaks apart
    static constexpr float kSlam = 2.25f;    // the logo lands
    static constexpr float kSettle = 3.2f;   // inputs stop; both stand in guard
    static constexpr float kPrompt = 3.6f;   // "press any key" fades up
    static constexpr float kEnd = 6.5f;

    bool m_active = false;
    bool m_armed = false;
    bool m_slamPending = false;
    float m_t = 0.0f;
    float m_out = 0.0f; // 0..1 handover to the menu, once skipped or finished
};
