#include "idk/editor/Editor.hpp"
#include "idk/gfx/RenderEngine.hpp"
#include "idk/editor/EditorGui.hpp"
#include "idk/EngineAPI.hpp"
#include <imgui.h>
#include <SDL3/SDL.h>

namespace idk::editor
{
    void EditorApplication::onInit(EngineAPI &api)
    {
        auto *renderer = api.getService<gfx::RenderEngine>();
        if (!renderer || !renderer->IsInitialized() || !InitializeGui(*renderer))
        {
            api.quit();
            return;
        }
        mInitialized = true;
        api.mEvent->addGenericCallback(onGenericEventCallback);

        // Fixed process storage backs the session's borrowed geometry spans.
        static EditorSession::Storage storage;
        if (!mSession.initialize(storage))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not initialize editor: %s", mSession.status());
            api.quit();
            return;
        }
        if (!mPreview.initialize(*renderer, mSession.document()))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not initialize editor preview resources");
            api.quit();
            return;
        }
        renderer->SetClearColor({.075f, .085f, .105f, 1});
        SDL_SetWindowTitle(static_cast<SDL_Window *>(renderer->GetWindowHandle()), "idk - Shape model editor");
        mSession.addShape(asset::ShapeKind::Box);
    }

    void EditorApplication::onShutdown(EngineAPI &api)
    {
        mUI.shutdown();
        if (!mInitialized)
            return;
        if (auto *renderer = api.getService<gfx::RenderEngine>())
            mPreview.shutdown(*renderer);
        ShutdownGui();
        mInitialized = false;
    }

    void EditorApplication::importModel(const EditorAction &action, gfx::RenderEngine &renderer)
    {
        EditorSession::ImportCandidate candidate;
        if (!mSession.prepareImport(action.path, action.replace, candidate))
        {
            mUI.showImportError(mSession.status());
            return;
        }
        const char *error = nullptr;
        if (!mPreview.rebuild(renderer, candidate.document, error))
        {
            mUI.showImportError(error);
            return;
        }
        mSession.commitImport(candidate, action.path);
        mViewport.frameAll(mSession.document());
    }

    void EditorApplication::execute(const EditorAction &action, gfx::RenderEngine &renderer)
    {
        switch (action.type)
        {
            case EditorAction::Type::None:
                break;
            case EditorAction::Type::Clear:
                mPreview.clear(renderer);
                mSession.clear();
                mViewport.resetCamera();
                break;
            case EditorAction::Type::ChooseImportFile:
                mUI.chooseImportFile(static_cast<SDL_Window *>(renderer.GetWindowHandle()));
                break;
            case EditorAction::Type::Import:
                importModel(action, renderer);
                break;
            case EditorAction::Type::Export:
                if (mSession.exportModel(action.path, action.overwrite) == EditorSession::ExportResult::AlreadyExists)
                    mUI.confirmOverwrite(action.path);
                break;
        }
    }

    void EditorApplication::onPreRender(EngineAPI &api)
    {
        if (!mInitialized)
            return;
        auto &renderer = *api.getService<gfx::RenderEngine>();
        BeginGuiFrame();
        auto action = mUI.draw(mSession, mViewport, renderer);
        execute(action, renderer);
        const char *error = nullptr;
        if (!mPreview.submit(renderer, mSession.document(), error))
            mSession.setStatus(error);
    }

    void EditorApplication::onMidRender(EngineAPI &)
    {
        if (!mInitialized)
            return;
        RenderGui();
    }

    void EditorApplication::onGenericEventCallback(EngineAPI &, const void *event)
    {
        ProcessGuiEvent(event);
    }
} // namespace idk::editor
