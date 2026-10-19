#pragma once

#include "idk/level/PhysicsWorld.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace idk::level
{
    namespace detail
    {
        class Layers final: public JPH::BroadPhaseLayerInterface
        {
        public:
            JPH::uint GetNumBroadPhaseLayers() const override { return 1; }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override { return JPH::BroadPhaseLayer(0); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "Terrain"; }
#endif
        };

        class BroadFilter final: public JPH::ObjectVsBroadPhaseLayerFilter
        {
            bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
        };

        class PairFilter final: public JPH::ObjectLayerPairFilter
        {
            bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override { return true; }
        };

        inline JPH::Vec3 ToJolt(glm::vec3 value)
        {
            return {value.x, value.y, value.z};
        }
    }

    struct PhysicsWorld::Impl
    {
        JPH::Factory *factory;
        detail::Layers layers;
        detail::BroadFilter broadFilter;
        detail::PairFilter pairFilter;
        JPH::PhysicsSystem system;
        JPH::TempAllocatorImpl temporary{8 * 1024 * 1024};
        JPH::BodyID terrain;
        JPH::BodyID candidate;

        explicit Impl(JPH::Factory *owner) : factory(owner) {}

        ~Impl()
        {
            auto &bodies = system.GetBodyInterface();
            if (!candidate.IsInvalid())
                bodies.DestroyBody(candidate);
            if (!terrain.IsInvalid())
            {
                bodies.RemoveBody(terrain);
                bodies.DestroyBody(terrain);
            }
        }
    };

}
