#pragma once

#include "audio.hpp"     // Sfx ids for sound cues
#include "characters/character.hpp" // per-fighter stats
#include "weapons/weapon.hpp"    // per-fighter sword loadout

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

class Physics;

// A one-shot sound the sim wants played. The sim only raises these; main
// drains them each frame and hands them to Audio (panned by world x).
struct SoundCue {
    Sfx sfx;
    float x;           // world x of the sound's source
    float gain = 1.0f; // 0..1 loudness scale (debris thuds scale with impact speed)
};

// The three attacks. Values double as the pose index handed to the model.
// Light is the baseline swing the character stats describe; Heavy and Jab
// scale those stats via the tuning table in game.cpp. Jab is a thrust, not
// a cut: it can never sever a limb (and so can never behead).
enum class AttackKind { Light = 0, Heavy = 1, Jab = 2 };
inline constexpr int kAttackKindCount = 3;

struct PlayerInput {
    glm::vec2 move{0.0f, 0.0f}; // x: left/right, y: depth (+ toward camera), each -1..1
    bool jump = false;
    // Held: break the stance and run. It buys ground and costs everything the
    // stance was for — no guard, no swing — and while it is held the fighter
    // turns to face where they are going rather than the foe.
    bool sprint = false;
    bool block = false;  // held: keep the guard up (slows the walk, no swinging)
    bool crouch = false; // held: duck low (slows the walk, no jumping)
    bool attack = false; // edge-triggered: true only on the tick(s) after a fresh press
    AttackKind attackKind = AttackKind::Light; // which attack, when attack is set
    // Edge-triggered, and one control for both halves of the same idea: throw
    // the blade down if there is one in hand, otherwise take up whatever is
    // lying at your feet. Which of the two it means is the sim's to decide —
    // the input layer has no business knowing what a fighter is holding.
    bool drop = false;

    // Exact equality, so the replay harness can check that an input survives
    // the wire format unchanged (see quantizeAxis in input.hpp).
    bool operator==(const PlayerInput&) const = default;
};

// Sword swing phases. Values double as the pose index handed to the model.
enum class AttackState { None = 0, Windup = 1, Active = 2, Recovery = 3 };

// Severable body parts. Values index Player::severed and match the limb
// indices the samurai model exposes (samuraiLimbBounds / drawSeveredLimb).
// "Front" is the model's +z side — the sword arm's side.
enum class Limb { ArmFront = 0, ArmBack, LegFront, LegBack, Head };
inline constexpr int kLimbCount = 5;

// A blood droplet in flight. Pure ballistics (no Jolt); a droplet that
// reaches the ground leaves a BloodMark, the rest fade out mid-air.
struct BloodParticle {
    glm::vec3 pos;
    glm::vec3 vel;
    float life; // seconds left before it vanishes mid-air
    float size; // cube edge length
};

// A blood splat on the ground. Marks last the whole match; once the cap is
// hit the oldest are recycled.
struct BloodMark {
    glm::vec3 pos;  // y is pre-jittered slightly above the floor (z-fighting)
    float radius;
    float yaw;   // random orientation so repeats don't look stamped-out
    float alpha;
};

// A limb that has been cut off and now tumbles as a physics debris body.
struct SeveredPiece {
    int victim;   // player index the limb came from (for colors)
    Limb limb;
    int debrisId; // handle into Physics' debris bodies
};

// A blade lying in the arena, thrown down by whoever was carrying it. It is a
// debris rigid body on exactly the same terms as a severed limb, so it
// tumbles, gets kicked around and floats downstream for free — and lands
// inside the determinism checksum for free with it.
//
// Nothing here says who dropped it: a blade on the ground belongs to whoever
// reaches it, which is the whole point of being able to throw one away, and is
// the same shape a blade the *level* placed will need.
struct DroppedWeapon {
    int weapon;   // armory index (weapons/weapon.hpp)
    int debrisId; // handle into Physics' debris bodies
    float settle; // s before it can be taken up, so a drop isn't a no-op
};

// World: x right, y up, z toward the camera; ground surface at y = 0.
// Units are meters-ish. Players move freely in the x/z ground plane.
struct Player {
    glm::vec3 pos{0.0f, 0.0f, 0.0f}; // center of the body box
    float vy = 0.0f;
    bool grounded = true;
    // Which way the body is turned: radians about +Y, 0 facing +x, and exactly
    // what the model is drawn with (drawSamurai's yaw). This was a ±1 mirror
    // for as long as a fighter only ever squared up to their opponent — which
    // is the same thing at 0 and pi and nothing else. A sprinting fighter turns
    // to face where they are running, so those two values stopped being the
    // only two there are. Locked while a swing is in flight.
    float yaw = 0.0f;
    // Running this tick, rather than holding a stance and shuffling it. The
    // trade is the whole of it: more than twice the ground covered, and no
    // guard, no swing, and a body turned away from the foe while it lasts.
    bool sprinting = false;
    float animPhase = 0.0f;  // walk-cycle phase in radians, advances with ground speed
    float moveAmount = 0.0f; // 0..1 fraction of max ground speed this tick
    // 0..1 ease into a full-length stride. Not a fraction of speed: a fighter
    // walking slowly takes full steps less often, not short ones. It exists
    // only so the legs come back together when they stop rather than freezing
    // mid-split, and so a standing fighter isn't holding a bent knee.
    float strideBlend = 0.0f;
    // Direction of travel in model-local space (x forward, y = z), unit
    // length. Decides which foot moves first and which way the step is taken.
    // A vector rather than a sign because a duel is not fought along one axis:
    // the stride used to work only in the facing plane, so stepping in depth
    // slid the fighter sideways on legs doing a forward shuffle.
    glm::vec2 strideDir{1.0f, 0.0f};

    AttackState attackState = AttackState::None;
    AttackKind attackKind = AttackKind::Light; // which attack is in flight
    float attackTimer = 0.0f;  // seconds left in the current attack phase
    float attackT = 0.0f;      // 0..1 progress through the current attack phase
    bool attackLanded = false; // this swing already connected
    float hitstun = 0.0f;      // seconds of control lockout after being hit
    bool blocking = false;     // guard is up this tick; catches attacks from the front
    bool crouching = false;    // ducked this tick; crouchAmount chases it
    float crouchAmount = 0.0f; // 0..1 smoothed crouch depth (pose + hurtboxes)
    float riposteTime = 0.0f;  // s left in the post-block counter window
    float attackWindupScale = 1.0f; // this swing's windup scale (a riposte snaps out)
    float attackBuffer = 0.0f; // s a pressed attack waits for the fighter to be able
    AttackKind bufferedKind = AttackKind::Light; // which attack is waiting
    glm::vec2 kbVel{0.0f};     // knockback velocity in the ground plane
    bool severed[kLimbCount] = {}; // dismembered parts stay lost for the match

    // The blade in hand, as an armory index — or kUnarmed once it has been
    // thrown down (or taken with both arms). It is per-fighter *state* rather
    // than part of the match's setup because it changes mid-match: a fighter
    // can end a duel holding the sword they started it with, none at all, or
    // the one their opponent threw away. Game::stats is re-resolved whenever
    // this moves, so everything outside the sim keeps reading one number.
    static constexpr int kUnarmed = -1;
    int weapon = 0;
    bool armed() const { return weapon != kUnarmed; }

    // Blood is the health pool and the win condition: torso hits cost a
    // chunk, dismemberment a bigger one, and open stumps keep draining it.
    // Empty = dead (beheading empties it instantly); the corpse collapses
    // through the topple path and the other fighter takes the match.
    static constexpr float kMaxBlood = 100.0f;
    float blood = kMaxBlood;

    // Toppling: a fighter missing a leg cannot stay standing. fallSide is the
    // model-space z sign they tip toward (0 until a leg is lost); fallTilt is
    // the roll about the local x axis at the feet, integrated like an inverted
    // pendulum until the body lies on the ground, where it stays.
    float fallTilt = 0.0f; // rad, 0 = upright
    float fallVel = 0.0f;  // rad/s
    float fallSide = 0.0f; // ±1 once toppling, toward the severed leg

    // The model's local axes in the world's ground plane (x, z): +x is the way
    // the fighter faces, +z the sword side. A rotation about +Y matching
    // drawSamurai's base transform — which at yaw 0 and pi is precisely the
    // mirror these replaced. Every hit test, shove and thrown object goes
    // through them, so there is one answer to "which way is this body turned".
    glm::vec2 forward() const { return {std::cos(yaw), -std::sin(yaw)}; }
    glm::vec2 sideAxis() const { return {std::sin(yaw), std::cos(yaw)}; }

    bool dead() const { return blood <= 0.0f; }
    bool downed() const {
        return dead() || severed[static_cast<int>(Limb::LegFront)] ||
               severed[static_cast<int>(Limb::LegBack)];
    }
    // Signed roll about the model's local +x axis (what the renderer applies).
    float bodyRoll() const { return fallSide * fallTilt; }
    // Ground-plane offset from the capsule to where a toppled body actually
    // lies. The roll swings the whole model about its local +x at the feet, so
    // the body travels along the side axis; multiply by a height to ask about a
    // particular part of it. Zero while upright, which is why every reader can
    // apply it unconditionally.
    glm::vec2 lieOffset() const { return sideAxis() * std::sin(bodyRoll()); }

    static constexpr float kHalfWidth = 0.45f;
    static constexpr float kHalfHeight = 0.9f;
};

class Game {
public:
    // A match is fully described by two (character, weapon) roster index
    // pairs plus a level index; that's the whole setup a future netplay
    // layer would agree on. The level picks the scenery and its static
    // obstacle colliders (levelObstacles).
    Game(int p0Character, int p0Weapon, int p1Character, int p1Weapon, int level);
    ~Game();
    void update(const PlayerInput inputs[2], float dt);
    const Player& player(int i) const { return m_players[i]; }
    const CharacterDef& character(int i) const { return *m_defs[i]; }
    // The blade fighter i is holding, or null once they have thrown it down.
    // A pointer rather than a reference because "empty-handed" is a state the
    // match can genuinely be in, and every reader has to say what it draws
    // (or prints) for it.
    const WeaponDef* weapon(int i) const { return m_weapons[i]; }
    // Effective per-fighter stats: the character's, with the weapon's swing
    // scale, reach bonus, and knockback scale already applied — re-resolved
    // whenever the blade in hand changes, so a fighter who picks up an odachi
    // swings like one. Everything outside Game (bot, rendering) should read
    // these, not the raw roster. An empty-handed fighter reads back their bare
    // character stats; they cannot attack at all, so nothing is scaled.
    const CharacterStats& stats(int i) const { return m_stats[i]; }
    // The stance fighter i is holding their blade in — resolved from that
    // blade, and so re-resolved with it whenever one changes hands. It decides
    // the arc of every swing, which the model has to animate along, so this is
    // read outside the sim exactly the way stats() is. An empty-handed fighter
    // reads back Normal and it means nothing: they can neither swing nor
    // guard, which are the only two things a stance describes.
    Stance stance(int i) const { return m_stance[i]; }
    // The battleground this match was built on (roster index into levels/level.hpp);
    // main draws the matching scenery.
    int level() const { return m_level; }

    // Match outcome: winner() is -1 while the duel is live, else the index
    // of the surviving player. overTime() is seconds since it was decided
    // (the UI waits a beat before covering the kill with the overlay).
    int winner() const { return m_winner; }
    bool over() const { return m_winner >= 0; }
    float overTime() const { return m_overTime; }

    // The duel is on a clock. A sword fight that cannot end is not a
    // stalemate, it is a hang: a fighter wedged in the stream, one who threw
    // their blade away and will not go back for it, or simply two people
    // circling. None of those is worth waiting out, and every one of them
    // used to run until somebody closed the window.
    //
    // The clock decides nothing on its own — it only stops the match and
    // hands it to whoever is in better shape. That keeps it a backstop rather
    // than a tactic: running the clock down from ahead is possible, but the
    // limit is long enough that it is a worse plan than fighting.
    static constexpr float kMatchTimeLimit = 99.0f; // s
    float timeLeft() const { return m_timeLeft; }
    // Was the outcome the clock's rather than a kill? The overlay says so —
    // "left standing" is a different thing to have done than "takes the duel".
    bool timedOut() const { return m_timedOut; }

    const std::vector<SeveredPiece>& severedPieces() const { return m_pieces; }
    // World transform of a severed piece's debris body, for rendering.
    glm::mat4 severedPieceTransform(const SeveredPiece& piece) const;

    const std::vector<DroppedWeapon>& droppedWeapons() const { return m_dropped; }
    glm::mat4 droppedWeaponTransform(const DroppedWeapon& blade) const; // rendering
    glm::vec3 droppedWeaponPos(const DroppedWeapon& blade) const;       // gameplay

    // How close a fighter's feet have to be, in the ground plane, to take a
    // blade up. Public because the bot has to walk to one, and a bot working
    // from its own idea of "close enough" would either stop short of every
    // sword forever or stand there pressing at nothing.
    static constexpr float kPickupRange = 1.25f;

    const std::vector<BloodParticle>& bloodParticles() const { return m_blood; }
    const std::vector<BloodMark>& bloodMarks() const { return m_bloodMarks; }

    // Sounds raised by update() since the last clear; main drains these.
    const std::vector<SoundCue>& soundCues() const { return m_soundCues; }
    void clearSoundCues() { m_soundCues.clear(); }

    // FNV-1a over everything the sim decided: both Players field by field,
    // both rng streams, the outcome, and every debris body's state. Two runs
    // fed the same inputs must agree on this every step — it is what a replay
    // checks and what a netplay peer would trade to catch a desync early.
    // Floats are hashed by their bits, not their value: bit-exactness is the
    // whole point, and 0.0f == -0.0f would hide a real divergence.
    // Deliberately not covered: the blood particles and marks themselves.
    // They are a pure function of m_cosmeticRng, which *is* hashed — one word
    // standing in for thousands, and a canary on the debris contact stream
    // that feeds them.
    std::uint32_t checksum() const;

    // Baseline arena bounds. Depth is shared by every level; width is only
    // the default — each level authors its own (LevelDef::arenaHalfWidth),
    // baked into this match as arenaHalfWidth().
    static constexpr float kArenaHalfWidth = 12.0f;
    static constexpr float kArenaHalfDepth = 5.0f;
    float arenaHalfWidth() const { return m_arenaHalfWidth; }

private:
    // Inside the level's water volume (xz)? Blood never marks the floor
    // there — the moving water carries it away.
    bool inWater(float x, float z) const {
        return m_hasWater && x >= m_waterMinXZ.x && x <= m_waterMaxXZ.x &&
               z >= m_waterMinXZ.y && z <= m_waterMaxXZ.y;
    }
    // Puts a blade (or Player::kUnarmed) in fighter i's hand and re-resolves
    // their stats from it. The one place m_weapons/m_stats are written.
    void equip(int i, int weapon);
    // Throws fighter i's blade to the ground in front of them, where either
    // fighter can take it up again. A no-op if they have nothing to throw.
    void dropWeapon(int i);
    // Takes up the nearest blade within reach of fighter i, if there is one
    // and they have a hand to hold it with. Returns whether anything was
    // picked up.
    bool takeUpWeapon(int i);
    // Is this debris body one of the severed limbs? Blades bleed no blood, so
    // the impact stream has to be able to tell them apart.
    bool isLimbDebris(int debrisId) const;
    // The clock ran out: call the match for whoever is in better shape.
    void decideOnTime();
    void severLimb(int victim, Limb limb, const glm::vec2& impulseDir);
    void collapse(Player& p); // death: cancel the swing, start the body's fall
    void spawnBlood(const glm::vec3& pos, const glm::vec3& dir, int count, float speed);
    void addBloodMark(const glm::vec3& pos, float radius);
    float frand();  // 0..1, from the sim stream — anything the fight turns on
    float cfrand(); // 0..1, from the cosmetic stream — blood spray and splats

    Player m_players[2];
    const CharacterDef* m_defs[2];   // roster entries (colors, names)
    // The blade each fighter is currently holding, null when empty-handed.
    // Follows Player::weapon rather than the match's setup — a fighter can
    // put one down and take up another mid-duel.
    const WeaponDef* m_weapons[2];
    CharacterStats m_stats[2];       // character stats with the weapon applied
    // How each fighter carries what they are holding. Derived from the blade
    // rather than stored per fighter, which is why the checksum needn't hash
    // it: Player::weapon is hashed, and this is a pure function of it.
    Stance m_stance[2] = {Stance::Normal, Stance::Normal};
    std::vector<SeveredPiece> m_pieces;
    std::vector<DroppedWeapon> m_dropped; // blades on the ground, free to take
    std::vector<SoundCue> m_soundCues;
    std::vector<BloodParticle> m_blood;
    std::vector<BloodMark> m_bloodMarks;
    std::size_t m_bloodMarkCursor = 0; // next mark to recycle once at the cap
    int m_level = 0;        // battleground roster index (scenery + obstacles)
    float m_arenaHalfWidth = kArenaHalfWidth; // this match's playable half width
    // The level's water volume footprint (xz), mirrored from levels/level.hpp so
    // blood marks can be suppressed in the stream; Physics holds the rest.
    bool m_hasWater = false;
    glm::vec2 m_waterMinXZ{0.0f};
    glm::vec2 m_waterMaxXZ{0.0f};
    int m_winner = -1;      // decided once; a post-match death can't flip it
    float m_overTime = 0.0f;
    float m_timeLeft = kMatchTimeLimit; // counts down only while the duel is live
    bool m_timedOut = false;            // the clock called it, not a killing blow
    // Two streams, deliberately. m_rng decides things the fight turns on (which
    // way a dying body topples); m_cosmeticRng decides only how blood looks.
    // Splitting them keeps the number of droplets a hit happened to spawn from
    // shifting every later gameplay roll — and is what will let a rollback
    // layer leave the cosmetic state out of save/restore. Retrofitting the
    // split later would move every roll in the game; doing it now costs a line.
    std::uint32_t m_rng = 0x51ce00d5u;
    std::uint32_t m_cosmeticRng = 0x9e3779b9u;
    std::unique_ptr<Physics> m_physics; // collision & movement solver (Jolt)
};
