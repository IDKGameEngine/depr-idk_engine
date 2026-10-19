#pragma once

namespace idk::gfx
{
    class RenderEngine;
}

namespace idk::editor
{
    // Shared ImGui backends and frame setup for the model and map editors.
    // Shutdown must precede destruction of the renderer's OpenGL context.
    bool InitializeGui(gfx::RenderEngine &);
    void ShutdownGui();
    void BeginGuiFrame();
    void RenderGui();
    void ProcessGuiEvent(const void *event);
}
