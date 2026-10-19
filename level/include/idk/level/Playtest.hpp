#pragma once

#include "LevelRunner.hpp"
#include "libidk/Service.hpp"
#include "idk/gfx/RenderTypes.hpp"

namespace idk::level
{
    class Playtest final: public idk::Service
    {
    public:
        // Path is resolved by the launcher before the engine changes directory.
        Playtest(const char *path, uint32_t frameLimit = 0, const char *levelRoot = nullptr);
        void onInit(idk::EngineAPI &) override;
        void onShutdown(idk::EngineAPI &) override;
        void onUpdate(idk::EngineAPI &) override;
        void onFixedUpdate(idk::EngineAPI &) override;
        void onPreRender(idk::EngineAPI &) override;

        bool failed() const { return mFailed; }

    private:
        const char *mPath;
        const char *mRoot;
        uint32_t mFrameLimit, mFrames = 0;
        PhysicsWorld mPhysics;
        Player mPlayer;
        LevelRunner mRunner;
        gfx::MeshHandle mExitMesh;
        gfx::MaterialHandle mExitMaterial;
        float mYaw = 0, mPitch = 0, mFallLimit = -100;
        glm::vec3 mMovement{0};
        bool mJump = false, mJumpHeld = false, mRestartHeld = false;
        bool mTabHeld = false, mCaptured = false, mReached = false, mFailed = false;
        void restart();
    };
} // namespace idk::level
