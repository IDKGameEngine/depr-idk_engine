#include "idk/level/LevelRunner.hpp"
#include "idk/gfx/RenderEngine.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace idk::level
{
    LevelRunner::~LevelRunner()
    {
        shutdown();
    }

    bool LevelRunner::initialize(Storage &storage, PhysicsWorld &world, Player &player, gfx::RenderEngine &renderer,
                                 const char *root, Error &error)
    {
        if (mStorage || !renderer.IsInitialized())
            return error.set("Level runner requires an initialized renderer.");
        struct stat info{};
        if (!realpath(root && *root ? root : ".", mRoot) || stat(mRoot, &info) != 0 || !S_ISDIR(info.st_mode))
            return error.set("Level root must be an existing directory.");
        mStorage = &storage;
        mWorld = &world;
        mPlayer = &player;
        mRenderer = &renderer;
        return true;
    }

    bool LevelRunner::loadInitial(const char *path, Error &error)
    {
        if (!mStorage || mGeneration)
            return error.set("Initial level can only be loaded once.");
        char absolute[4096];
        if (!path || !realpath(path, absolute))
            return error.set("Could not resolve initial level path.");
        return loadAndCommit(absolute, error);
    }

    bool LevelRunner::requestTransition(const asset::ExitDestination &destination, Error &error)
    {
        if (!mStorage || !mGeneration)
            return error.set("No active level.");
        if (pending())
            return error.set("A level transition is already queued.");
        if (!destination.valid() || !destination.path[0])
            return error.set("Transition requires a relative .idklvl destination.");
        mPending = destination;
        return true;
    }

    LevelRunner::Transition LevelRunner::processPending(Error &error)
    {
        if (!pending())
            return Transition::None;
        auto destination = mPending;
        mPending = {};
        char path[4096], absolute[4096];
        int length = std::snprintf(path, sizeof(path), "%s/%s", mRoot, destination.path);
        if (length < 0 || size_t(length) >= sizeof(path) || !realpath(path, absolute))
        {
            error.set("Could not resolve destination level. The current level was retained.");
            return Transition::Failed;
        }
        size_t rootLength = std::strlen(mRoot);
        if (std::strcmp(mRoot, "/") && (std::strncmp(absolute, mRoot, rootLength) || absolute[rootLength] != '/'))
        {
            error.set("Destination resolves outside the level root. The current level was retained.");
            return Transition::Failed;
        }
        return loadAndCommit(absolute, error) ? Transition::Changed : Transition::Failed;
    }

    bool LevelRunner::loadAndCommit(const char *path, Error &error)
    {
        unsigned bank = mGeneration ? 1 - mBank : 0;
        idk::Arena output(mStorage->banks[bank]), scratch(mStorage->scratch);
        Level candidate;
        asset::Model model;
        if (!Load(path, output, candidate, error) || !candidate.model(scratch, model, error))
            return false;
        auto uploaded = mRenderer->UploadModel(model);
        if (!uploaded.isValid())
            return error.set("Could not upload destination level. The current level was retained.");
        if (!mWorld->prepareTerrain(candidate, error))
        {
            mRenderer->DestroyModel(uploaded);
            return false;
        }
        // All fallible work is done. No fixed update or draw submission occurs
        // between replacing terrain, resetting contacts and releasing old data.
        auto previous = mModel;
        mWorld->commitTerrain();
        mLevel = candidate;
        mModel = uploaded;
        mBank = bank;
        ++mGeneration;
        std::snprintf(mCurrentPath, sizeof(mCurrentPath), "%s", path);
        mPlayer->reset(mLevel.entry.position);
        mRenderer->DestroyModel(previous);
        return true;
    }

    void LevelRunner::shutdown()
    {
        if (mRenderer)
            mRenderer->DestroyModel(mModel);
        if (mWorld)
            mWorld->clearTerrain();
        mStorage = nullptr;
        mWorld = nullptr;
        mPlayer = nullptr;
        mRenderer = nullptr;
        mLevel = {};
        mModel = {};
        mPending = {};
        mGeneration = 0;
        mCurrentPath[0] = mRoot[0] = 0;
    }
}
