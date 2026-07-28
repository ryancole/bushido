#pragma once

#include "weapons/stance.hpp" // the arcs the sword arm animates along

#include <glm/glm.hpp>

#include <functional>

class Renderer;
struct SunDef; // levels/level.hpp — one sun over every battleground

struct SamuraiColors {
    glm::vec4 kimono; // torso, arms, shoulder plates
    glm::vec4 hakama; // trousers, hip skirt, collar
    glm::vec4 accent; // obi belt, sword grip wrap
};

// What a fighter wears on their head. An enum the shared builder interprets,
// rather than geometry authored through the adorn hook, for a blunt reason:
// the head comes off. drawSeveredLimb draws the severed head from this same
// value, so whatever is worn travels with the piece — a hat authored through
// the hook would stay hanging over the stump. Every value must be handled in
// both buildSamurai's head block and drawSeveredLimb's head case; variants are
// added here as characters need them.
enum class Headgear {
    Kasa,   // wide straw hat — the roster's original look
    Bare,   // nothing: the head itself becomes the topmost silhouette
    Hood,   // close cowl over the head, a mask across the face below the eyes
    Horns,  // a dark mane with a pair of bone horns rising off it
    Eboshi, // tall lacquered court cap, narrower than the head it sits on
};

// Stance and stride geometry. The model's own dimensions, public because the
// sim moves the body against them and both ends have to read one copy — two
// would drift, and the symptom would be exactly what they exist to prevent:
// feet skating over the floor.
inline constexpr float kHipPivotY = 0.85f;  // hip joint height, standing tall
inline constexpr float kFootY = 0.05f;      // sandal center height
inline constexpr float kKneePivotY = 0.45f; // knee joint height, standing tall
inline constexpr float kThighLen = kHipPivotY - kKneePivotY;
inline constexpr float kShinLen = kKneePivotY - kFootY;
inline constexpr float kLegLength = kThighLen + kShinLen;

// A fighter with a sword in their hands does not walk. They hold a stance and
// shuffle it along: one foot always leads, the feet never cross, and a step is
// the lead reaching out and the rear closing up after it. So the legs are
// placed by where the *feet* need to be and solved back to joint angles,
// rather than swung from the hip and hoped over — which also means the stance
// is what it is because a leg is a fixed-length lever. A hip at full standing
// height can only reach the floor straight down, so holding the feet any
// distance apart at all requires settling onto bent knees. These three numbers
// are locked together by that: widen the stance or lengthen the step and the
// hip has to come down to keep both feet on the ground.
inline constexpr float kStanceHipY = 0.70f;    // hip height in the guard
inline constexpr float kStanceSep = 0.34f;     // lead foot ahead of rear
inline constexpr float kShuffleStride = 0.55f; // ground covered per cycle
inline constexpr float kFootLift = 0.07f;      // peak height of a moving foot
inline constexpr float kLegSide = 0.12f;       // a leg's z offset from center
// How far the whole body sits below a fighter standing tall. Everything from
// the hip up rides down by this, so every *sim* height authored against the
// old upright model — shoulder, hand, torso, arm and head hurtboxes — is that
// value minus this. Without it the drawn blade sweeps 15 cm under the arc it
// actually cuts along, and the head you aim at is not the head you must hit.
inline constexpr float kStanceDrop = kHipPivotY - kStanceHipY;
inline constexpr float kCrouchDrop = 0.45f; // further drop at full crouch

// One leg, solved. The feet are placed by the shuffle and the joints fall out
// of them, so this is the single source both ends read: samurai.cpp draws from
// it and game.cpp builds the leg hurtbox from it. Two copies of this would
// drift, and the fighter would be hit where their legs are not.
struct LegPose {
    glm::vec3 hipAt, kneeAt, footAt; // model-local joint centers
};

// `side` is the leg's z sign: +1 is the lead (sword-side) leg, which is the
// front foot and stays the front foot. `dir` is the direction of travel in
// model-local space (x forward, y = z), and decides which foot moves first —
// never which one leads.
LegPose shuffleLeg(float phase, float strideBlend, glm::vec2 dir, float crouch,
                   bool grounded, float side);

// One hop, solved. A fighter who has lost a leg does not go down — there is
// still a leg to stand on, and standing is what a duel is — so they hop on it.
//
// The hop keeps the shuffle's rule, and keeps it for the same reason: the foot
// on the floor must not slide. All the ground is covered in the air, so
// `surge` is flat zero through the plant and a bell through the flight, which
// is what lets the foot sit still under a body that is not moving. It averages
// to exactly 1 across the cycle the way the shuffle's 2·sin² does, so the mean
// speed is still precisely the speed stat (times game.cpp's kHopMoveScale) and
// cadence remains the only thing speed buys.
//
// Both ends read this one function — game.cpp for the surge and the leg's
// hurtbox, samurai.cpp for the drawing — since two copies would disagree about
// when the foot is down, and the fighter would be hit where their leg is not.
struct HopPose {
    float surge;   // × speed this instant; 0 while the foot is planted
    float rise;    // m the hip and everything above it ride above the guard
    LegPose leg;   // the leg that is left, solved for where its foot has to be
};
// One hop per π of phase, matching a footfall of the shuffle. `side` is the
// remaining leg's z sign (+1 = the lead, sword-side leg).
HopPose hopCycle(float phase, float strideBlend, float crouch, bool grounded,
                 float side);

struct SamuraiPose {
    float walkPhase = 0.0f;  // radians through the stride cycle
    float moveAmount = 0.0f; // 0..1 fraction of max ground speed; drives bob and lean
    // 0..1 ease into a full-length step (Player::strideBlend). Not a fraction
    // of speed: a slow fighter takes full steps less often, not short ones. It
    // only closes the *stepping* — the stance separation is held either way,
    // since a guard is a guard whether or not you are moving in it.
    float strideBlend = 0.0f;
    // Direction of travel in model-local space, x forward (Player::strideDir).
    // Says which foot goes first and which way the step is taken — never which
    // foot *leads*, which is the same one all match. A vector rather than a
    // sign because a duel is not fought along one axis: stepping in depth used
    // to slide the fighter sideways on legs doing a forward shuffle.
    glm::vec2 strideDir{1.0f, 0.0f};
    bool grounded = true;
    float time = 0.0f;       // seconds since start, for idle breathing
    int attackState = 0;     // mirrors game AttackState: 0 none, 1 windup, 2 active, 3 recovery
    int attackKind = 0;      // mirrors game AttackKind: 0 light, 1 heavy, 2 jab
    float attackT = 0.0f;    // 0..1 progress through the current attack phase
    bool blocking = false;   // guard up: the sword arm holds the blade at the foe
    float crouch = 0.0f;     // 0..1 duck depth: legs fold, upper body drops
    // One leg gone and still standing on the other: the surviving leg is
    // placed by hopCycle rather than the shuffle, and the whole body rides the
    // hop up and down. A flag rather than something read off `severed`,
    // because a corpse with one leg is toppling, not hopping.
    bool hopping = false;
    // Shoulder-to-tip length of the drawn katana, matching the fighter's
    // resolved reach (character + weapon) so what connects is what the
    // player sees. bladeWidth scales only the drawn blade's thickness —
    // a visual cue for the equipped weapon, no gameplay effect.
    float reach = 1.6f;
    float bladeWidth = 1.0f;
    // How the blade is carried: where the guard holds it, where a swing
    // settles back to, and the arc of the swing itself. Comes from the weapon
    // (via Game::stance), and is the one thing that makes an odachi's cleave
    // and a wakizashi's rising cut look like different swings — the sim sweeps
    // the very same angles, so this is not a rendering choice.
    Stance stance = Stance::Normal;
    // Is there a blade in hand at all? An armed fighter carries theirs drawn
    // for the whole match — held at the stance's ready angle when they are
    // doing nothing else with it — so this is what puts a sword on screen or
    // takes it off. A fighter who has thrown theirs down (or lost both arms)
    // gets an empty hand and an arm that swings with the stride again, which
    // is what makes "I am unarmed" readable at a glance. The saya at the hip
    // is empty either way and says nothing about it.
    bool armed = true;
    // Signed topple roll about the local +x axis at the feet: a fighter who
    // has lost a leg tips toward ±z and ends up lying on the ground.
    float bodyRoll = 0.0f;
    // Barrel roll about the model's own +y — the spine, which is the long axis
    // a body lying on the ground turns over about. Applied *inside* bodyRoll
    // (spin the model, then tip it), which is what makes it a roll along the
    // ground rather than a second topple. game.cpp's spinLocal is the same
    // rotation, and every hurtbox on a rolling fighter goes through it.
    float rollSpin = 0.0f;
    // Optional [5] dismemberment flags, indexed like samuraiLimbBounds. Severed
    // parts draw as stumps; if the +z (sword) arm is gone, the -z arm swings.
    const bool* severed = nullptr;
};

// The bespoke-geometry seam: a character's own dressing — armor plates, a
// mask, a back banner — authored in that character's own file
// (characters/<name>.cpp) and emitted into the same part walk drawSamurai and
// drawSamuraiShadow both consume, so it is drawn, shaded and shadowed exactly
// like the shared body. The sink is the builder's own `part`: a local
// transform (composed under the body's base, so a topple or a roll carries
// adornments with the body), a box center and full size, a color, and the
// `detail` flag. Three rules keep a hook honest:
//  - Hang everything off `upper`, or a pivot built on it — never on a
//    severable limb. Severed pieces are drawn by drawSeveredLimb from the
//    shared list, so hook geometry on an arm or a leg would not travel with
//    the cut (headgear is an enum for exactly that reason).
//  - `detail = true` unless the piece genuinely widens the silhouette. Every
//    non-detail box is one more shadow caster, and the shadow band is a
//    budgeted stack — see the numbers over kShadowTop in samurai.cpp before
//    spending one.
//  - The sim cannot see any of it. Dressing that reads as body — bulk, broad
//    pauldrons — wants to stay near the shared hurtboxes (samuraiLimbBounds),
//    or the fighter will be missed where they visibly are.
struct AdornContext {
    const SamuraiPose& pose;
    // Upper-body transform: bob, stance drop and attack lean already folded
    // in. The torso, collar, shoulders and head all hang off this one matrix.
    glm::mat4 upper;
};
using AdornPart = std::function<void(const glm::mat4& local, glm::vec3 center,
                                     glm::vec3 size, const glm::vec4& color, bool detail)>;
using AdornFn = void (*)(const AdornContext&, const AdornPart&);

// Everything that makes one fighter look like themselves: the palette the
// shared body wears, what sits on their head, and the bespoke hook. A
// character authors one of these beside its stats (CharacterDef::look) and the
// one buildSamurai consumes it — the body, its gaits and its hurtboxes stay
// shared, because those are exactly the parts that must not drift between
// what is drawn, what is cast on the floor, and what can be hit.
struct SamuraiLook {
    SamuraiColors colors;
    Headgear headgear = Headgear::Kasa;
    AdornFn adorn = nullptr; // optional bespoke geometry; null = nothing extra
};

// Severable body parts are indexed 0..4: 0 arm +z (sword side), 1 arm -z,
// 2 leg +z, 3 leg -z, 4 head. Order must match game.hpp's Limb enum.
// Bounds are the limb's tight AABB in model-local space (feet origin,
// +x facing) — used for gameplay hit regions and debris collision shapes.
struct LimbBounds {
    glm::vec3 center;
    glm::vec3 half;
};
LimbBounds samuraiLimbBounds(int limb);

// Draws limb `limb` as a free-flying piece (with a blood-red cut cap), with
// its boxes centered on samuraiLimbBounds(limb).center so `transform` can be
// a rigid-body transform for a box of those half extents. Takes the whole
// look, not just the palette: a severed head leaves wearing what the fighter
// wore.
void drawSeveredLimb(Renderer& renderer, const glm::mat4& transform, int limb,
                     const SamuraiLook& look);

// A blade as a loose object rather than something in a hand — thrown down, or
// waiting on the ground to be taken up.
//
// Its length comes from the weapon's reach *bonus* alone, never from a
// fighter's reach: a blade on the ground is the same object whoever picks it
// up, and one that grew or shrank as it changed hands would be a lie about
// what the next swing covers. The baseline is what drawSamurai puts in the
// hand of a 1.6 m-reach fighter carrying a katana.
float bladeSteelLength(float reachBonus);
// Half extents of the physics box for a blade of that steel length — grip
// included, laid along local +x with the tip at +x.
glm::vec3 droppedBladeHalfExtent(float steel);
// Draws that blade centered on its own origin, so `transform` can be a rigid
// body transform for a box of those half extents (the same contract
// drawSeveredLimb keeps). `width` is the weapon's visual thickness cue.
void drawDroppedBlade(Renderer& renderer, const glm::mat4& transform, float steel,
                      float width, const glm::vec4& grip);

// Draws a samurai assembled procedurally from shaded boxes: hakama legs,
// kimono torso, obi, sode shoulder plates, arms, head, headgear, an empty
// saya at the hip, and — while armed — the drawn blade itself, dressed by the
// character's `look` (palette, headgear, adorn hook). `feet` is the ground
// point under the character; `yaw` rotates about +Y (0 faces +x).
void drawSamurai(Renderer& renderer, const glm::vec3& feet, float yaw,
                 const SamuraiPose& pose, const SamuraiLook& look);

// The same fighter, printed on the floor by the level's sun. Every box the
// model is built from is cast down the sun's own line, so the shadow holds a
// stance, takes a step, ducks, swings its blade and lies down with the body —
// none of which a disc at the feet can do.
//
// It walks the *same* part list drawSamurai draws, which is the point rather
// than an economy: a shadow built from a second description of the fighter
// would be the shadow of a fighter who is not there, and would drift from this
// one exactly the way a second copy of shuffleLeg would drift from the legs.
//
// `feet`, `yaw`, `pose` and `look` are the very arguments drawSamurai was
// given, so the two calls are read as one thing — the look matters here too,
// since what a fighter wears changes their outline and the outline is all a
// shadow is. A level whose sun casts nothing (Dojo's night) draws nothing here
// and wants main's blob instead — the blob was never lighting, it is what
// makes depth readable in the air, and an unlit stage still owes the player
// that.
void drawSamuraiShadow(Renderer& renderer, const glm::vec3& feet, float yaw,
                       const SamuraiPose& pose, const SamuraiLook& look,
                       const SunDef& sun);
