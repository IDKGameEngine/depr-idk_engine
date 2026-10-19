#pragma once

#include "Player.hpp"
#include "idk/gfx/RenderTypes.hpp"

namespace idk::gfx
{
    class RenderEngine;
}

namespace idk::level
{
    // Persistent owner of per-level resources. Call processPending at the
    // start of application onPreRender, before submitting this frame's draws.
    class LevelRunner final: public idk::Immobile
    {
    public:
        ~LevelRunner();

        struct Storage
        {
            alignas(16) std::byte banks[2][4 * 1024 * 1024];
            alignas(16) std::byte scratch[64 * 1024];
        };
        enum class Transition
        {
            None,
            Changed,
            Failed
        };

        bool initialize(Storage &, PhysicsWorld &, Player &, gfx::RenderEngine &, const char *root, Error &);
        bool loadInitial(const char *path, Error &);
        bool requestTransition(const asset::ExitDestination &, Error &);
        Transition processPending(Error &);
        void shutdown();

        // References into current() expire on the next successful commit.
        const Level &current() const { return mLevel; }

        gfx::ModelHandle model() const { return mModel; }

        const char *currentPath() const { return mCurrentPath; }

        bool pending() const { return mPending.path[0] != 0; }

        uint64_t generation() const { return mGeneration; }

    private:
        Storage *mStorage = nullptr;
        PhysicsWorld *mWorld = nullptr;
        Player *mPlayer = nullptr;
        gfx::RenderEngine *mRenderer = nullptr;
        Level mLevel;
        gfx::ModelHandle mModel;
        asset::ExitDestination mPending;
        char mRoot[4096]{}, mCurrentPath[4096]{};
        unsigned mBank = 0;
        uint64_t mGeneration = 0;
        bool loadAndCommit(const char *, Error &);
    };
}
