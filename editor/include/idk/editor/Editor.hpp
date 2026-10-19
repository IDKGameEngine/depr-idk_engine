#pragma once

#include "libidk/Service.hpp"
#include "EditorSession.hpp"
#include "EditorUI.hpp"
#include "EditorViewport.hpp"
#include "ScenePreview.hpp"

namespace idk::editor
{
    // Engine lifecycle and command coordination. Components own their own state.
    class EditorApplication final: public idk::Service
    {
    public:
        void onInit(idk::EngineAPI &) override;
        void onShutdown(idk::EngineAPI &) override;
        void onPreRender(idk::EngineAPI &) override;
        void onMidRender(idk::EngineAPI &) override;

    private:
        bool mInitialized = false;
        EditorSession mSession;
        ScenePreview mPreview;
        EditorViewport mViewport;
        EditorUI mUI;

        void execute(const EditorAction &, gfx::RenderEngine &);
        void importModel(const EditorAction &, gfx::RenderEngine &);
        static void onGenericEventCallback(idk::EngineAPI &, const void *);
    };
} // namespace idk::editor
