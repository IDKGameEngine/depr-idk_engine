#include "PhysicsWorldInternal.hpp"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <new>

namespace idk::level
{
    PhysicsWorld::~PhysicsWorld()
    {
        shutdown();
    }

    bool PhysicsWorld::initialize(idk::Arena &storage, Error &error)
    {
        if (mImpl)
            return error.set("Physics world is already initialized.");
        if (JPH::Factory::sInstance)
            return error.set("Another physics world already owns the Jolt factory.");
        idk::ArenaScope allocations(storage);
        void *factoryStorage = storage.alloc(sizeof(JPH::Factory), alignof(JPH::Factory));
        void *worldStorage = storage.alloc(sizeof(Impl), alignof(Impl));
        if (!factoryStorage || !worldStorage)
            return error.set("Physics world arena exhausted.");
        JPH::RegisterDefaultAllocator();
        auto *factory = new (factoryStorage) JPH::Factory();
        JPH::Factory::sInstance = factory;
        JPH::RegisterTypes();
        mImpl = new (worldStorage) Impl(factory);
        mImpl->system.Init(64, 0, 64, 64, mImpl->layers, mImpl->broadFilter, mImpl->pairFilter);
        allocations.commit();
        return true;
    }

    bool PhysicsWorld::prepareTerrain(const Level &level, Error &error)
    {
        if (!mImpl)
            return error.set("Physics world is not initialized.");
        if (!mImpl->candidate.IsInvalid())
            return error.set("A terrain candidate is already prepared.");
        if (!level.validate(error))
            return false;
        JPH::TriangleList triangles;
        size_t count = 0;
        for (const auto &object : level.terrain)
            count += object.indices.size() / 3;
        triangles.reserve(count);
        for (const auto &object : level.terrain)
            for (size_t i = 0; i < object.indices.size(); i += 3)
                triangles.emplace_back(detail::ToJolt(object.vertices[object.indices[i]].position),
                                       detail::ToJolt(object.vertices[object.indices[i + 1]].position),
                                       detail::ToJolt(object.vertices[object.indices[i + 2]].position));
        auto mesh = JPH::MeshShapeSettings(triangles).Create();
        if (mesh.HasError())
            return error.set(mesh.GetError().c_str());
        auto *body = mImpl->system.GetBodyInterface().CreateBody(JPH::BodyCreationSettings(
            mesh.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, 0));
        if (!body)
            return error.set("Could not prepare terrain collision body.");
        mImpl->candidate = body->GetID();
        return true;
    }

    void PhysicsWorld::discardTerrain()
    {
        if (mImpl && !mImpl->candidate.IsInvalid())
        {
            mImpl->system.GetBodyInterface().DestroyBody(mImpl->candidate);
            mImpl->candidate = JPH::BodyID();
        }
    }

    void PhysicsWorld::commitTerrain()
    {
        if (!mImpl || mImpl->candidate.IsInvalid())
            return;
        auto &bodies = mImpl->system.GetBodyInterface();
        if (!mImpl->terrain.IsInvalid())
        {
            bodies.RemoveBody(mImpl->terrain);
            bodies.DestroyBody(mImpl->terrain);
        }
        mImpl->terrain = mImpl->candidate;
        mImpl->candidate = JPH::BodyID();
        bodies.AddBody(mImpl->terrain, JPH::EActivation::DontActivate);
        mImpl->system.OptimizeBroadPhase();
    }

    void PhysicsWorld::clearTerrain()
    {
        discardTerrain();
        if (mImpl && !mImpl->terrain.IsInvalid())
        {
            auto &bodies = mImpl->system.GetBodyInterface();
            bodies.RemoveBody(mImpl->terrain);
            bodies.DestroyBody(mImpl->terrain);
            mImpl->terrain = JPH::BodyID();
        }
    }

    void PhysicsWorld::shutdown()
    {
        if (!mImpl)
            return;
        auto *factory = mImpl->factory;
        mImpl->~Impl();
        mImpl = nullptr;
        JPH::UnregisterTypes();
        factory->~Factory();
        JPH::Factory::sInstance = nullptr;
    }

}
