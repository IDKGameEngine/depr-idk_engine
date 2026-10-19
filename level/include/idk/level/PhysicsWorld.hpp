#pragma once

#include "Level.hpp"
#include "libidk/Types.hpp"

namespace idk::level
{
    // Persistent across transitions. Prepared terrain is not added to the
    // collision world until commit; discard leaves active terrain untouched.
    // Players must be shut down before their world.
    class PhysicsWorld final: public idk::Immobile
    {
    public:
        ~PhysicsWorld();
        // Storage must outlive the world. Reclaim it after shutdown.
        bool initialize(idk::Arena &storage, Error &);
        void shutdown();
        bool prepareTerrain(const Level &, Error &);
        void discardTerrain();
        void commitTerrain();
        void clearTerrain();

    private:
        friend class Player;
        struct Impl;
        Impl *mImpl = nullptr;
    };
}
