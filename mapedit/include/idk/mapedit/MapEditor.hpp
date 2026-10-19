#pragma once

#include "MapSession.hpp"
#include "idk/editor/EditorViewport.hpp"
#include "idk/editor/ScenePreview.hpp"
#include "libidk/Service.hpp"

struct SDL_Process;

namespace idk::mapedit
{
    class MapEditorApplication final : public idk::Service
    {
      public:
        void onInit(idk::EngineAPI&) override;
        void onShutdown(idk::EngineAPI&) override;
        void onPreRender(idk::EngineAPI&) override;
        void onMidRender(idk::EngineAPI&) override;

      private:
        enum class Action
        {
            None,
            New,
            Open,
            Save,
            Replace,
            Build,
            Play,
            Stop
        };

        bool mInitialized = false;
        MapSession mSession;
        editor::ScenePreview mPreview;
        editor::EditorViewport mViewport;
        char mPath[1024] = "level.idkmap";
        char mOverwritePath[1024]{};
        bool mConfirmOverwrite = false;
        char mBuildPath[1024] = "level.idklvl";
        char mPlayDirectory[64]{};
        char mPlayPath[128]{};
        SDL_Process* mPlaytest = nullptr;

        Action draw(gfx::RenderEngine&);
        Action drawFiles();
        Action drawDialogs();
        void drawTools();
        void drawObjects();
        void drawInspector();
        void execute(Action, gfx::RenderEngine&);
        void buildLevel(bool play);
        void pollPlaytest();
        void stopPlaytest();
        void cleanPlaytest();
        static void onGenericEventCallback(idk::EngineAPI&, const void*);
    };
} // namespace idk::mapedit
