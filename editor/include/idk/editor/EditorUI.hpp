#pragma once

#include "ImportFileDialog.hpp"

namespace idk::gfx
{
    class RenderEngine;
}

namespace idk::editor
{
    class EditorSession;
    class EditorViewport;
    struct ShapeObject;

    // File I/O and whole-scene changes are deferred until drawing has finished.
    struct EditorAction
    {
        enum class Type
        {
            None,
            Clear,
            ChooseImportFile,
            Import,
            Export
        };
        Type type = Type::None;
        char path[ImportFileDialog::PathCapacity]{};
        bool replace = false;
        bool overwrite = false;
    };

    class EditorUI
    {
    public:
        EditorAction draw(EditorSession &, EditorViewport &, gfx::RenderEngine &);
        void chooseImportFile(SDL_Window *);
        void showImportError(const char *);
        void confirmOverwrite(const char *path);
        void shutdown();

    private:
        struct ImportOptions
        {
            char path[ImportFileDialog::PathCapacity]{};
            char error[640]{};
            bool replace = false;
            bool open = false;
            bool awaitingFile = false;
        };

        struct ExportOptions
        {
            char path[512] = "model/shape-model.glb";
            char overwritePath[512]{};
            bool confirmOverwrite = false;
        };

        ImportOptions mImport;
        ExportOptions mExport;
        EditorAction mAction;

        void pollFileDialog();
        void drawMenu();
        void drawTools(EditorSession &, EditorViewport &);
        void drawShapes(EditorSession &);
        void drawObjectList(EditorSession &, EditorViewport &);
        void drawInspector(ShapeObject &);
        void drawTransformTools(EditorViewport &);
        void drawExport(const EditorSession &);
        void drawDialogs(const EditorSession &);
        void drawImportDialog(const EditorSession &);
    };
} // namespace idk::editor
