#include "idk/editor/EditorGui.hpp"
#include "idk/gfx/RenderEngine.hpp"
#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <SDL3/SDL.h>

namespace idk::editor
{
    bool InitializeGui(gfx::RenderEngine &renderer)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        if (scale <= 0)
            scale = 1;
        ImGui::GetStyle().ScaleAllSizes(scale);
        ImGui::GetStyle().FontScaleDpi = scale;
        ImGui::GetIO().ConfigDpiScaleFonts = true;
        if (!ImGui_ImplSDL3_InitForOpenGL(static_cast<SDL_Window *>(renderer.GetWindowHandle()),
                                          renderer.GetContextHandle()))
        {
            ImGui::DestroyContext();
            return false;
        }
        if (!ImGui_ImplOpenGL3_Init("#version 460 core"))
        {
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return false;
        }
        return true;
    }

    void ShutdownGui()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    void BeginGuiFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }

    void RenderGui()
    {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void ProcessGuiEvent(const void *event)
    {
        if (ImGui::GetCurrentContext())
            ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event *>(event));
    }
}
