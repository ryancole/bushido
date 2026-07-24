#include "physics.hpp"

#include "game.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <thread>

namespace {

namespace Layers {
constexpr JPH::ObjectLayer NON_MOVING = 0;
constexpr JPH::ObjectLayer MOVING = 1;
constexpr JPH::ObjectLayer NUM_LAYERS = 2;
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
            default: return false;
        }
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override {
        switch (layer1) {
            case Layers::NON_MOVING: return layer2 == Layers::MOVING;
            case Layers::MOVING: return true;
            default: return false;
        }
    }
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
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    BPLayerInterfaceImpl bpLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objVsBpFilter;
    ObjectLayerPairFilterImpl objPairFilter;
    JPH::PhysicsSystem physicsSystem;
    JPH::CharacterVsCharacterCollisionSimple charVsChar;
    JPH::Ref<JPH::CharacterVirtual> characters[2];
};

Physics::Physics(float gravity, const glm::vec3& spawnA, const glm::vec3& spawnB)
    : m_gravity(gravity) {
    ensureJoltInitialized();
    m_impl = std::make_unique<Impl>();
    Impl& im = *m_impl;

    im.jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1));

    constexpr JPH::uint kMaxBodies = 1024;
    constexpr JPH::uint kMaxBodyPairs = 1024;
    constexpr JPH::uint kMaxContactConstraints = 1024;
    im.physicsSystem.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints,
                          im.bpLayerInterface, im.objVsBpFilter, im.objPairFilter);
    im.physicsSystem.SetGravity(JPH::Vec3(0.0f, -gravity, 0.0f));

    JPH::BodyInterface& bodies = im.physicsSystem.GetBodyInterface();
    auto addStaticBox = [&bodies](const JPH::Vec3& halfExtent, const JPH::RVec3& pos) {
        JPH::BodyCreationSettings settings(new JPH::BoxShape(halfExtent), pos,
                                           JPH::Quat::sIdentity(),
                                           JPH::EMotionType::Static,
                                           Layers::NON_MOVING);
        bodies.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    };

    // Ground slab (top surface at y = 0) and four arena walls whose inner
    // faces sit at the arena bounds, replacing the old position clamp.
    constexpr float kWall = 0.5f; // wall/ground half thickness
    constexpr float kWallHalfHeight = 20.0f;
    const float hw = Game::kArenaHalfWidth;
    const float hd = Game::kArenaHalfDepth;
    addStaticBox({hw + 2.0f * kWall, kWall, hd + 2.0f * kWall}, {0.0f, -kWall, 0.0f});
    addStaticBox({kWall, kWallHalfHeight, hd + 2.0f * kWall},
                 {hw + kWall, kWallHalfHeight, 0.0f});
    addStaticBox({kWall, kWallHalfHeight, hd + 2.0f * kWall},
                 {-hw - kWall, kWallHalfHeight, 0.0f});
    addStaticBox({hw + 2.0f * kWall, kWallHalfHeight, kWall},
                 {0.0f, kWallHalfHeight, hd + kWall});
    addStaticBox({hw + 2.0f * kWall, kWallHalfHeight, kWall},
                 {0.0f, kWallHalfHeight, -hd - kWall});

    // Character capsule matching the fighter's body box, origin at the feet.
    const float radius = Player::kHalfWidth;
    const float halfHeight = Player::kHalfHeight;
    JPH::RefConst<JPH::Shape> capsule =
        JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0.0f, halfHeight, 0.0f), JPH::Quat::sIdentity(),
            new JPH::CapsuleShape(halfHeight - radius, radius))
            .Create()
            .Get();

    const glm::vec3 spawns[2] = {spawnA, spawnB};
    for (int i = 0; i < 2; ++i) {
        JPH::Ref<JPH::CharacterVirtualSettings> settings =
            new JPH::CharacterVirtualSettings();
        settings->mShape = capsule;
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

void Physics::step(float dt) {
    m_impl->physicsSystem.Update(dt, 1, &m_impl->tempAllocator,
                                 m_impl->jobSystem.get());
}
