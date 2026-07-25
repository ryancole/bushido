#include "physics.hpp"

#include "game.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <mutex>
#include <vector>

namespace {

namespace Layers {
constexpr JPH::ObjectLayer NON_MOVING = 0;
constexpr JPH::ObjectLayer MOVING = 1;
constexpr JPH::ObjectLayer DEBRIS = 2; // severed limbs: hit the world and fighters
constexpr JPH::ObjectLayer NUM_LAYERS = 3;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer NON_MOVING(0);
constexpr JPH::BroadPhaseLayer MOVING(1);
constexpr JPH::uint NUM_LAYERS = 2;
} // namespace BroadPhaseLayers

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NUM_LAYERS;
    }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::NON_MOVING ? BroadPhaseLayers::NON_MOVING
                                           : BroadPhaseLayers::MOVING;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
    }
#endif
};

class ObjectVsBroadPhaseLayerFilterImpl final
    : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer1,
                       JPH::BroadPhaseLayer layer2) const override {
        switch (layer1) {
            case Layers::NON_MOVING: return layer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING: return true;
            case Layers::DEBRIS: return true;
            default: return false;
        }
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override {
        switch (layer1) {
            case Layers::NON_MOVING: return layer2 != Layers::NON_MOVING;
            // Fighters collide with limbs too: the CharacterVirtual update
            // pushes the (much lighter) debris bodies out of its way.
            case Layers::MOVING: return true;
            case Layers::DEBRIS: return true;
            default: return false;
        }
    }
};

// Records audible debris impacts (limb hits ground/wall/other debris). Only
// rigid bodies raise contacts — the fighters are CharacterVirtuals and never
// appear here. Jolt calls this from its job threads, hence the mutex. The
// speed threshold keeps rolling/settling contacts quiet; with restitution
// 0.25 a landed limb yields one solid thud and at most one soft bounce.
class DebrisContactListener final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
                        const JPH::ContactManifold& manifold,
                        JPH::ContactSettings&) override {
        if (body1.GetObjectLayer() != Layers::DEBRIS &&
            body2.GetObjectLayer() != Layers::DEBRIS) {
            return;
        }
        JPH::Vec3 rel = body1.GetLinearVelocity() - body2.GetLinearVelocity();
        float speed = std::abs(rel.Dot(manifold.mWorldSpaceNormal));
        if (speed < kMinAudibleSpeed) {
            return;
        }
        JPH::RVec3 p = manifold.GetWorldSpaceContactPointOn1(0);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_impacts.push_back({{static_cast<float>(p.GetX()), static_cast<float>(p.GetY()),
                              static_cast<float>(p.GetZ())},
                             speed});
    }

    void clear() { m_impacts.clear(); }
    const std::vector<Physics::DebrisImpact>& impacts() const { return m_impacts; }

private:
    static constexpr float kMinAudibleSpeed = 2.0f; // m/s along the normal

    std::mutex m_mutex;
    std::vector<Physics::DebrisImpact> m_impacts;
};

// Global Jolt state (allocator, RTTI factory, type registry) is process-wide
// and intentionally never torn down.
void ensureJoltInitialized() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    initialized = true;
}

} // namespace

struct Physics::Impl {
    JPH::TempAllocatorImpl tempAllocator{4 * 1024 * 1024};
    std::unique_ptr<JPH::JobSystemSingleThreaded> jobSystem;
    BPLayerInterfaceImpl bpLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objVsBpFilter;
    ObjectLayerPairFilterImpl objPairFilter;
    JPH::PhysicsSystem physicsSystem;
    JPH::CharacterVsCharacterCollisionSimple charVsChar;
    JPH::Ref<JPH::CharacterVirtual> characters[2];
    JPH::RefConst<JPH::Shape> standShape;
    JPH::RefConst<JPH::Shape> proneShape;
    bool prone[2] = {false, false};
    std::vector<JPH::BodyID> debris;
    DebrisContactListener contactListener;
    Physics::Water water;
};

Physics::Physics(float gravity, float arenaHalfWidth, const glm::vec3& spawnA,
                 const glm::vec3& spawnB, const std::vector<StaticBox>& staticBoxes,
                 const std::vector<StaticBox>& groundBoxes, const Water& water)
    : m_gravity(gravity) {
    ensureJoltInitialized();
    m_impl = std::make_unique<Impl>();
    Impl& im = *m_impl;
    im.water = water;

    // Single-threaded on purpose. Jolt only promises determinism for the same
    // binary run with the same thread count, and a pool sized from
    // hardware_concurrency() is exactly the thing that differs between two
    // machines trying to run the same match — different job counts reorder
    // contact resolution, and the divergence works its way back into gameplay
    // through debrisImpacts(). The sim is two capsules and a handful of limb
    // boxes; there is nothing here a pool would win.
    im.jobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);

    constexpr JPH::uint kMaxBodies = 1024;
    constexpr JPH::uint kMaxBodyPairs = 1024;
    constexpr JPH::uint kMaxContactConstraints = 1024;
    im.physicsSystem.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints,
                          im.bpLayerInterface, im.objVsBpFilter, im.objPairFilter);
    im.physicsSystem.SetGravity(JPH::Vec3(0.0f, -gravity, 0.0f));
    im.physicsSystem.SetContactListener(&im.contactListener);

    JPH::BodyInterface& bodies = im.physicsSystem.GetBodyInterface();
    auto addStaticBox = [&bodies](const JPH::Vec3& halfExtent, const JPH::RVec3& pos) {
        JPH::BodyCreationSettings settings(new JPH::BoxShape(halfExtent), pos,
                                           JPH::Quat::sIdentity(),
                                           JPH::EMotionType::Static,
                                           Layers::NON_MOVING);
        bodies.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    };

    // Ground (top surface at y = 0) and four arena walls whose inner faces
    // sit at the arena bounds, replacing the old position clamp. The ground
    // is one flat slab unless the level hands in its own boxes (Hanami's
    // banks + carved stream bed).
    constexpr float kWall = 0.5f; // wall/ground half thickness
    constexpr float kWallHalfHeight = 20.0f;
    const float hw = arenaHalfWidth;
    const float hd = Game::kArenaHalfDepth;
    if (groundBoxes.empty()) {
        addStaticBox({hw + 2.0f * kWall, kWall, hd + 2.0f * kWall},
                     {0.0f, -kWall, 0.0f});
    } else {
        for (const StaticBox& g : groundBoxes) {
            addStaticBox({g.halfExtent.x, g.halfExtent.y, g.halfExtent.z},
                         {g.center.x, g.center.y, g.center.z});
        }
    }
    addStaticBox({kWall, kWallHalfHeight, hd + 2.0f * kWall},
                 {hw + kWall, kWallHalfHeight, 0.0f});
    addStaticBox({kWall, kWallHalfHeight, hd + 2.0f * kWall},
                 {-hw - kWall, kWallHalfHeight, 0.0f});
    addStaticBox({hw + 2.0f * kWall, kWallHalfHeight, kWall},
                 {0.0f, kWallHalfHeight, hd + kWall});
    addStaticBox({hw + 2.0f * kWall, kWallHalfHeight, kWall},
                 {0.0f, kWallHalfHeight, -hd - kWall});

    // Level scenery colliders (e.g. Hanami's bank stones). Same layer as the
    // arena, so fighters and debris both collide with them.
    for (const StaticBox& b : staticBoxes) {
        addStaticBox({b.halfExtent.x, b.halfExtent.y, b.halfExtent.z},
                     {b.center.x, b.center.y, b.center.z});
    }

    // Character capsule matching the fighter's body box, origin at the feet.
    const float radius = Player::kHalfWidth;
    const float halfHeight = Player::kHalfHeight;
    im.standShape =
        JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0.0f, halfHeight, 0.0f), JPH::Quat::sIdentity(),
            new JPH::CapsuleShape(halfHeight - radius, radius))
            .Create()
            .Get();

    // Squat capsule for a toppled fighter: same radius, roughly half the
    // height, so standing opponents can jump clear over a body on the ground.
    const float proneHalf = 0.5f;
    im.proneShape =
        JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0.0f, proneHalf, 0.0f), JPH::Quat::sIdentity(),
            new JPH::CapsuleShape(proneHalf - radius, radius))
            .Create()
            .Get();

    const glm::vec3 spawns[2] = {spawnA, spawnB};
    for (int i = 0; i < 2; ++i) {
        JPH::Ref<JPH::CharacterVirtualSettings> settings =
            new JPH::CharacterVirtualSettings();
        settings->mShape = im.standShape;
        settings->mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
        settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
        im.characters[i] = new JPH::CharacterVirtual(
            settings, JPH::RVec3(spawns[i].x, spawns[i].y, spawns[i].z),
            JPH::Quat::sIdentity(), 0, &im.physicsSystem);
        im.characters[i]->SetCharacterVsCharacterCollision(&im.charVsChar);
        im.charVsChar.Add(im.characters[i]);
    }
}

Physics::~Physics() = default;

Physics::MoveResult Physics::moveCharacter(int i, const glm::vec3& velocity, float dt) {
    Impl& im = *m_impl;
    JPH::CharacterVirtual* ch = im.characters[i];
    ch->SetLinearVelocity({velocity.x, velocity.y, velocity.z});

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    if (velocity.y > 0.0f) {
        // Don't glue a jumping character back to the floor.
        updateSettings.mStickToFloorStepDown = JPH::Vec3::sZero();
    }
    ch->ExtendedUpdate(dt, JPH::Vec3(0.0f, -m_gravity, 0.0f), updateSettings,
                       im.physicsSystem.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                       im.physicsSystem.GetDefaultLayerFilter(Layers::MOVING),
                       {}, {}, im.tempAllocator);

    JPH::RVec3 pos = ch->GetPosition();
    return {{static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()),
             static_cast<float>(pos.GetZ())},
            ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround};
}

void Physics::setCharacterProne(int i, bool prone) {
    Impl& im = *m_impl;
    if (im.prone[i] == prone) {
        return;
    }
    im.prone[i] = prone;
    // FLT_MAX skips the penetration check: the prone shape only ever shrinks
    // the standing one, so the swap can't wedge the character into geometry.
    im.characters[i]->SetShape(
        prone ? im.proneShape : im.standShape, FLT_MAX,
        im.physicsSystem.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        im.physicsSystem.GetDefaultLayerFilter(Layers::MOVING), {}, {},
        im.tempAllocator);
}

void Physics::step(float dt) {
    Impl& im = *m_impl;

    // Water: float any debris that has drifted into the volume. Buoyancy
    // against the surface plane makes a piece bob ~2/3 submerged; the drag
    // terms pull its velocity toward the current's, which is what actually
    // carries it downstream. Pieces are re-activated while in the water so a
    // settled piece can't sleep through the current.
    if (im.water.max.x > im.water.min.x) {
        constexpr float kBuoyancy = 1.6f;    // fluid/body density ratio; >1 floats
        constexpr float kLinearDrag = 0.6f;
        constexpr float kAngularDrag = 0.3f;
        JPH::BodyInterface& bodies = im.physicsSystem.GetBodyInterface();
        for (const JPH::BodyID& id : im.debris) {
            JPH::RVec3 pos = bodies.GetPosition(id);
            if (pos.GetX() < im.water.min.x || pos.GetX() > im.water.max.x ||
                pos.GetZ() < im.water.min.z || pos.GetZ() > im.water.max.z ||
                pos.GetY() > im.water.max.y + 0.3f) {
                continue;
            }
            bodies.ActivateBody(id);
            JPH::BodyLockWrite lock(im.physicsSystem.GetBodyLockInterface(), id);
            if (!lock.Succeeded()) {
                continue;
            }
            lock.GetBody().ApplyBuoyancyImpulse(
                JPH::RVec3(pos.GetX(), im.water.max.y, pos.GetZ()),
                JPH::Vec3::sAxisY(), kBuoyancy, kLinearDrag, kAngularDrag,
                {im.water.current.x, im.water.current.y, im.water.current.z},
                JPH::Vec3(0.0f, -m_gravity, 0.0f), dt);
        }
    }

    im.contactListener.clear();
    im.physicsSystem.Update(dt, 1, &im.tempAllocator, im.jobSystem.get());
}

const std::vector<Physics::DebrisImpact>& Physics::debrisImpacts() const {
    return m_impl->contactListener.impacts();
}

int Physics::addDebris(const glm::vec3& center, float yaw, const glm::vec3& halfExtent,
                       const glm::vec3& velocity, const glm::vec3& angularVelocity) {
    Impl& im = *m_impl;
    // Small convex radius so thin pieces (arms) are still valid boxes.
    JPH::BodyCreationSettings settings(
        new JPH::BoxShape({halfExtent.x, halfExtent.y, halfExtent.z}, 0.02f),
        JPH::RVec3(center.x, center.y, center.z),
        JPH::Quat::sRotation(JPH::Vec3::sAxisY(), yaw), JPH::EMotionType::Dynamic,
        Layers::DEBRIS);
    settings.mLinearVelocity = {velocity.x, velocity.y, velocity.z};
    settings.mAngularVelocity = {angularVelocity.x, angularVelocity.y,
                                 angularVelocity.z};
    settings.mRestitution = 0.25f;
    // Comically nudgeable on purpose: near-zero mass so the 70 kg character
    // launches pieces on the slightest touch, and low friction so they slide
    // well past the point of contact instead of stopping dead.
    settings.mFriction = 0.35f;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = 0.2f;
    JPH::BodyID id = im.physicsSystem.GetBodyInterface().CreateAndAddBody(
        settings, JPH::EActivation::Activate);
    im.debris.push_back(id);
    return static_cast<int>(im.debris.size()) - 1;
}

void Physics::debrisStates(std::vector<DebrisState>& out) const {
    Impl& im = *m_impl;
    const JPH::BodyInterface& bodies = im.physicsSystem.GetBodyInterface();
    out.clear();
    out.reserve(im.debris.size());
    for (const JPH::BodyID& id : im.debris) {
        JPH::RVec3 p = bodies.GetPosition(id);
        JPH::Quat q = bodies.GetRotation(id);
        JPH::Vec3 v = bodies.GetLinearVelocity(id);
        JPH::Vec3 w = bodies.GetAngularVelocity(id);
        out.push_back({{p.GetX(), p.GetY(), p.GetZ()},
                       {q.GetX(), q.GetY(), q.GetZ(), q.GetW()},
                       {v.GetX(), v.GetY(), v.GetZ()},
                       {w.GetX(), w.GetY(), w.GetZ()}});
    }
}

glm::mat4 Physics::debrisTransform(int id) const {
    Impl& im = *m_impl;
    JPH::RMat44 xf =
        im.physicsSystem.GetBodyInterface().GetWorldTransform(im.debris[id]);
    glm::mat4 out;
    for (int c = 0; c < 4; ++c) {
        JPH::Vec4 col = xf.GetColumn4(c);
        out[c] = {col.GetX(), col.GetY(), col.GetZ(), col.GetW()};
    }
    return out;
}
