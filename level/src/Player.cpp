#include "idk/level/Player.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <algorithm>
#include <cmath>
#include <new>

namespace idk::level
{
    namespace
    {
        class Layers final : public JPH::BroadPhaseLayerInterface
        {
          public:
            JPH::uint GetNumBroadPhaseLayers() const override { return 1; }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override { return JPH::BroadPhaseLayer(0); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "Terrain"; }
#endif
        };

        class BroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
            bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
        };

        class PairFilter final : public JPH::ObjectLayerPairFilter
        {
            bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override { return true; }
        };

        JPH::Vec3 vector(glm::vec3 value)
        {
            return {value.x, value.y, value.z};
        }
    }

    struct Player::Impl
    {
        Layers layers;
        BroadFilter broadFilter;
        PairFilter pairFilter;
        JPH::PhysicsSystem system;
        JPH::TempAllocatorImpl temporary{8 * 1024 * 1024};
        JPH::BodyID terrain;
        JPH::Ref<JPH::CharacterVirtual> character;

        ~Impl()
        {
            character = nullptr;
            if (!terrain.IsInvalid())
            {
                auto& bodies = system.GetBodyInterface();
                bodies.RemoveBody(terrain);
                bodies.DestroyBody(terrain);
            }
        }
    };

    Player::~Player()
    {
        shutdown();
    }

    bool Player::initialize(const Level& level, Error& error)
    {
        if (mImpl || !level.validate(error))
            return false;
        if (JPH::Factory::sInstance)
            return error.set("Another physics world already owns the Jolt factory.");
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        mOwnsFactory = true;
        mImpl = new (std::nothrow) Impl;
        if (!mImpl)
        {
            shutdown();
            return error.set("Could not allocate playtest physics.");
        }
        mImpl->system.Init(64, 0, 64, 64, mImpl->layers, mImpl->broadFilter, mImpl->pairFilter);
        JPH::TriangleList triangles;
        size_t count = 0;
        for (const auto& object : level.terrain)
            count += object.indices.size() / 3;
        triangles.reserve(count);
        for (const auto& object : level.terrain)
            for (size_t i = 0; i < object.indices.size(); i += 3)
                triangles.emplace_back(vector(object.vertices[object.indices[i]].position),
                                       vector(object.vertices[object.indices[i + 1]].position),
                                       vector(object.vertices[object.indices[i + 2]].position));
        auto mesh = JPH::MeshShapeSettings(triangles).Create();
        if (mesh.HasError())
        {
            error.set(mesh.GetError().c_str());
            shutdown();
            return false;
        }
        mImpl->terrain = mImpl->system.GetBodyInterface().CreateAndAddBody(
            JPH::BodyCreationSettings(mesh.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static,
                                      0),
            JPH::EActivation::DontActivate);
        if (mImpl->terrain.IsInvalid())
        {
            shutdown();
            return error.set("Could not create terrain collision body.");
        }
        mImpl->system.OptimizeBroadPhase();
        auto capsule = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0, .9f, 0), JPH::Quat::sIdentity(),
                                                           new JPH::CapsuleShape(.6f, .3f))
                           .Create();
        if (capsule.HasError())
        {
            error.set(capsule.GetError().c_str());
            shutdown();
            return false;
        }
        JPH::CharacterVirtualSettings settings;
        settings.mShape = capsule.Get();
        settings.mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
        settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -.3f);
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        mImpl->character = new JPH::CharacterVirtual(&settings, JPH::RVec3(vector(level.entry.position)),
                                                     JPH::Quat::sIdentity(), 0, &mImpl->system);
        reset(level.entry.position);
        return true;
    }

    void Player::shutdown()
    {
        delete mImpl;
        mImpl = nullptr;
        if (mOwnsFactory)
        {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
            mOwnsFactory = false;
        }
    }

    void Player::reset(glm::vec3 feet)
    {
        if (!mImpl || !mImpl->character)
            return;
        mImpl->character->SetPosition(JPH::RVec3(vector(feet)));
        mImpl->character->SetLinearVelocity(JPH::Vec3::sZero());
        mImpl->character->RefreshContacts(mImpl->system.GetDefaultBroadPhaseLayerFilter(0),
                                          mImpl->system.GetDefaultLayerFilter(0), {}, {}, mImpl->temporary);
    }

    void Player::step(glm::vec3 movement, bool jump, float dt)
    {
        if (!mImpl || !mImpl->character || !std::isfinite(dt) || dt <= 0)
            return;
        dt = std::min(dt, 1.0f / 30);
        auto& character = *mImpl->character;
        float vertical = character.GetLinearVelocity().GetY();
        if (grounded() && vertical <= .1f)
            vertical = jump ? 5.0f : 0;
        character.SetLinearVelocity(JPH::Vec3(movement.x, vertical - 9.81f * dt, movement.z));
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        character.ExtendedUpdate(dt, JPH::Vec3(0, -9.81f, 0), settings,
                                 mImpl->system.GetDefaultBroadPhaseLayerFilter(0),
                                 mImpl->system.GetDefaultLayerFilter(0), {}, {}, mImpl->temporary);
    }

    glm::vec3 Player::position() const
    {
        if (!mImpl || !mImpl->character)
            return {};
        auto p = mImpl->character->GetPosition();
        return {float(p.GetX()), float(p.GetY()), float(p.GetZ())};
    }

    bool Player::grounded() const
    {
        return mImpl && mImpl->character &&
               mImpl->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    }
}
