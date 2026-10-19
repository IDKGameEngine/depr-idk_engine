#include "idk/level/Playtest.hpp"
#include "idk/asset/PrimitiveShape.hpp"
#include "idk/gfx/RenderEngine.hpp"
#include "idk/EngineAPI.hpp"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace idk::level
{
    Playtest::Playtest(const char* path, uint32_t frameLimit) : mPath(path), mFrameLimit(frameLimit)
    {
    }

    void Playtest::onInit(EngineAPI& api)
    {
        auto* renderer = api.getService<gfx::RenderEngine>();
        if (!renderer || !renderer->IsInitialized())
        {
            mFailed = true;
            api.quit();
            return;
        }
        alignas(16) static std::byte data[4 * 1024 * 1024], scratch[64 * 1024];
        idk::Arena arena(data), temporary(scratch);
        Error error;
        asset::Model model;
        if (!Load(mPath, arena, mLevel, error) || !mLevel.model(temporary, model, error) ||
            !mPlayer.initialize(mLevel, error))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Playtest: %s", error.message);
            mFailed = true;
            api.quit();
            return;
        }
        mModel = renderer->UploadModel(model);
        asset::Primitive box;
        if (!asset::CreateShape(asset::ShapeKind::Box, temporary, box))
        {
            mFailed = true;
            api.quit();
            return;
        }
        const gfx::VertexAttribute attributes[] = {
            {0, gfx::VertexAttribType::Float3, offsetof(asset::Vertex, position)},
            {1, gfx::VertexAttribType::Float2, offsetof(asset::Vertex, uv)},
            {4, gfx::VertexAttribType::Float4, offsetof(asset::Vertex, color)}};
        mExitMesh = renderer->CreateMesh({box.vertices.data(), box.vertices.size(), sizeof(asset::Vertex),
                                          box.indices.data(), box.indices.size(), attributes});
        gfx::MaterialDesc beacon;
        beacon.shader = renderer->GetDefaultShader();
        beacon.baseColor = {1, .45f, .1f, .35f};
        beacon.blend = gfx::BlendMode::Alpha;
        beacon.doubleSided = true;
        beacon.depthWrite = false;
        mExitMaterial = renderer->CreateMaterial(beacon);
        if (!mModel.isValid() || !mExitMesh.isValid() || !mExitMaterial.isValid())
        {
            mFailed = true;
            api.quit();
            return;
        }
        float minimum = mLevel.entry.position.y;
        for (const auto& object : mLevel.terrain)
            for (const auto& v : object.vertices)
                minimum = std::min(minimum, v.position.y);
        mFallLimit = minimum - 50;
        renderer->SetClearColor({.075f, .085f, .105f, 1});
        restart();
        auto* window = static_cast<SDL_Window*>(renderer->GetWindowHandle());
        SDL_SetWindowTitle(window,
                           "Playtest - click to look | WASD move | Space jump | R restart | Tab cursor | Esc close");
        SDL_Log("Playtest loaded %zu terrain objects at entry %.3f %.3f %.3f", mLevel.terrain.size(),
                mLevel.entry.position.x, mLevel.entry.position.y, mLevel.entry.position.z);
    }

    void Playtest::onShutdown(EngineAPI& api)
    {
        mPlayer.shutdown();
        if (auto* renderer = api.getService<gfx::RenderEngine>())
        {
            SDL_SetWindowRelativeMouseMode(static_cast<SDL_Window*>(renderer->GetWindowHandle()), false);
            renderer->DestroyModel(mModel);
            renderer->DestroyMesh(mExitMesh);
            renderer->DestroyMaterial(mExitMaterial);
        }
    }

    void Playtest::restart()
    {
        mPlayer.reset(mLevel.entry.position);
        mYaw = mLevel.entry.rotation.y;
        mPitch = std::clamp(mLevel.entry.rotation.x, -85.0f, 85.0f);
        mReached = false;
        mJump = false;
    }

    void Playtest::onUpdate(EngineAPI& api)
    {
        if (mFailed)
            return;
        auto& renderer = *api.getService<gfx::RenderEngine>();
        auto* window = static_cast<SDL_Window*>(renderer.GetWindowHandle());
        const bool* keys = SDL_GetKeyboardState(nullptr);
        bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        if (focused && keys[SDL_SCANCODE_ESCAPE])
            api.quit();
        bool tab = focused && keys[SDL_SCANCODE_TAB];
        if (tab && !mTabHeld)
            mCaptured = !mCaptured;
        mTabHeld = tab;
        if (!focused)
            mCaptured = false;
        if (focused && !mCaptured && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK))
            mCaptured = true;
        SDL_SetWindowRelativeMouseMode(window, mCaptured);
        float mouseX = 0, mouseY = 0;
        SDL_GetRelativeMouseState(&mouseX, &mouseY);
        if (mCaptured)
        {
            mYaw -= mouseX * .12f;
            mPitch = std::clamp(mPitch - mouseY * .12f, -85.0f, 85.0f);
        }
        bool restartKey = focused && keys[SDL_SCANCODE_R];
        if (restartKey && !mRestartHeld)
        {
            restart();
            SDL_SetWindowTitle(window, "Playtest restarted - WASD move | Space jump | R restart | Esc close");
        }
        mRestartHeld = restartKey;
        mMovement = {0, 0, 0};
        bool jump = focused && mCaptured && keys[SDL_SCANCODE_SPACE];
        if (jump && !mJumpHeld)
            mJump = true;
        mJumpHeld = jump;
        if (focused && mCaptured)
        {
            glm::vec3 forward(-std::sin(glm::radians(mYaw)), 0, -std::cos(glm::radians(mYaw)));
            glm::vec3 right(-forward.z, 0, forward.x);
            mMovement = forward * float(int(keys[SDL_SCANCODE_W]) - int(keys[SDL_SCANCODE_S])) +
                        right * float(int(keys[SDL_SCANCODE_D]) - int(keys[SDL_SCANCODE_A]));
            if (glm::length(mMovement) > 0)
                mMovement = glm::normalize(mMovement) * (keys[SDL_SCANCODE_LSHIFT] ? 7.0f : 4.0f);
        }
    }

    void Playtest::onFixedUpdate(EngineAPI& api)
    {
        if (mFailed)
            return;
        mPlayer.step(mMovement, mJump, float(api.getFixedDeltaTimeSec()));
        mJump = false;
        if (mPlayer.position().y < mFallLimit)
            restart();
        if (!mReached && mLevel.atExit(mPlayer.position()))
        {
            mReached = true;
            auto* window = static_cast<SDL_Window*>(api.getService<gfx::RenderEngine>()->GetWindowHandle());
            SDL_SetWindowTitle(window, "Exit reached! - R restart | Esc return to editor");
            SDL_Log("Playtest exit reached");
        }
    }

    void Playtest::onPreRender(EngineAPI& api)
    {
        if (mFailed)
            return;
        auto& renderer = *api.getService<gfx::RenderEngine>();
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(renderer.GetWindowHandle()), &width, &height);
        if (width > 0 && height > 0)
        {
            float yaw = glm::radians(mYaw), pitch = glm::radians(mPitch);
            auto eye = mPlayer.position() + glm::vec3(0, 1.6f, 0);
            glm::vec3 forward(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
            gfx::Camera camera;
            camera.view = glm::lookAt(eye, eye + forward, glm::vec3(0, 1, 0));
            camera.proj = glm::perspectiveRH_NO(glm::radians(70.0f), float(width) / height, .05f, 20000.0f);
            renderer.SetCamera(camera);
            if (!renderer.SubmitModel(mModel) || !renderer.Submit({mExitMesh, mExitMaterial, mLevel.exit.transform()}))
            {
                mFailed = true;
                api.quit();
            }
        }
        if (mFrameLimit && ++mFrames >= mFrameLimit)
            api.quit();
    }
}
