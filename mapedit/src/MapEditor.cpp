#include "idk/mapedit/MapEditor.hpp"
#include "idk/gfx/RenderEngine.hpp"
#include "idk/EngineAPI.hpp"
#include "idk/level/Build.hpp"
#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <SDL3/SDL.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace idk::mapedit
{
    namespace
    {
        bool initializeGui(gfx::RenderEngine &renderer)
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
    } // namespace

    void MapEditorApplication::onInit(EngineAPI &api)
    {
        auto *renderer = api.getService<gfx::RenderEngine>();
        if (!renderer || !renderer->IsInitialized() || !initializeGui(*renderer))
        {
            api.quit();
            return;
        }
        mInitialized = true;
        api.mEvent->addGenericCallback(onGenericEventCallback);
        alignas(16) static std::byte geometry[64 * 1024];
        idk::Arena arena(geometry);
        if (!mSession.initialize(arena) || !mPreview.initialize(*renderer, mSession.document()))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not initialize map editor: %s", mSession.status());
            api.quit();
            return;
        }
        renderer->SetClearColor({.075f, .085f, .105f, 1});
        SDL_SetWindowTitle(static_cast<SDL_Window *>(renderer->GetWindowHandle()), "idk - Level editor (mapedit)");
        mSession.setStatus("Add terrain, then place a spawn / entry marker and an exit marker.");
    }

    void MapEditorApplication::onShutdown(EngineAPI &api)
    {
        stopPlaytest();
        if (!mInitialized)
            return;
        if (auto *renderer = api.getService<gfx::RenderEngine>())
            mPreview.shutdown(*renderer);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        mInitialized = false;
    }

    void MapEditorApplication::drawObjects()
    {
        ImGui::SeparatorText("Level objects");
        ImGui::Text("%u / %u objects", mSession.document().count(), editor::ShapeDocument::Capacity);
        if (ImGui::BeginChild("Objects", ImVec2(0, 135 * ImGui::GetFontSize() / 13), ImGuiChildFlags_Borders))
        {
            for (uint32_t i = 0; i < editor::ShapeDocument::Capacity; ++i)
            {
                const auto *object = mSession.document().get(i);
                if (!object)
                    continue;
                ImGui::PushID(int(i));
                char label[96];
                std::snprintf(label, sizeof(label), "[%s] %s", RoleName(mSession.role(i)), object->name);
                if (ImGui::Selectable(label, mSession.selection() == i))
                    mSession.select(i);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::BeginDisabled(!mSession.selected());
        ImGui::BeginDisabled(mSession.role(mSession.selection()) != ObjectRole::Terrain);
        if (ImGui::Button("Duplicate"))
            mSession.duplicateSelection();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
            mSession.deleteSelection();
        ImGui::SameLine();
        if (ImGui::Button("Focus"))
            mViewport.focusSelection(mSession);
        ImGui::EndDisabled();
    }

    void MapEditorApplication::drawInspector()
    {
        auto *object = mSession.selected();
        if (!object)
            return;
        ImGui::SeparatorText(RoleName(mSession.role(mSession.selection())));
        ImGui::InputText("Name", object->name, sizeof(object->name));
        if (mSession.role(mSession.selection()) == ObjectRole::Exit)
        {
            auto &destination = mSession.destination();
            ImGui::InputText("Next level", destination.path, sizeof(destination.path));
            ImGui::TextWrapped("Relative to the game's level root, e.g. level/next.idklvl. Leave empty to finish.");
            if (!destination.valid())
                ImGui::TextWrapped("Use a relative .idklvl path without dot segments.");
        }
        ImGui::DragFloat3("Position", glm::value_ptr(object->position), .05f, -10000, 10000, "%.3f",
                          ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat3("Rotation", glm::value_ptr(object->rotation), .5f, -360, 360, "%.1f",
                          ImGuiSliderFlags_AlwaysClamp);
        glm::vec3 previous = object->scale;
        if (ImGui::DragFloat3("Scale", glm::value_ptr(object->scale), .05f, -1000, 1000, "%.3f",
                              ImGuiSliderFlags_AlwaysClamp))
        {
            for (unsigned c = 0; c < 3; ++c)
                if (std::abs(object->scale[c]) < .001f)
                    object->scale[c] = std::copysign(.001f, previous[c]);
        }
        if (mSession.role(mSession.selection()) == ObjectRole::Terrain)
            ImGui::ColorEdit4("Colour", glm::value_ptr(object->color));
        else
            ImGui::TextWrapped("This coloured mesh marks a location; its centre is the saved marker position.");
    }

    void MapEditorApplication::drawTools()
    {
        ImGui::SeparatorText("Add terrain / environment");
        for (uint32_t i = 0; i < editor::ShapeDocument::ShapeCount; ++i)
        {
            if (i % 2)
                ImGui::SameLine();
            float width = ImGui::GetContentRegionAvail().x / (i % 2 ? 1 : 2) - 4;
            auto kind = asset::ShapeKind(i);
            if (ImGui::Button(asset::ShapeName(kind), ImVec2(width, 0)))
                mSession.addShape(kind);
        }
        ImGui::SeparatorText("Level locations");
        if (ImGui::Button("Spawn / entry (green)", ImVec2(-1, 0)))
            mSession.placeMarker(ObjectRole::Entry);
        if (ImGui::Button("Exit (orange)", ImVec2(-1, 0)))
            mSession.placeMarker(ObjectRole::Exit);
        ImGui::Text("Entry: %s   Exit: %s", mSession.marker(ObjectRole::Entry) == asset::NoIndex ? "missing" : "set",
                    mSession.marker(ObjectRole::Exit) == asset::NoIndex ? "missing" : "set");
        drawObjects();
        drawInspector();
        auto &tools = mViewport.tools();
        ImGui::SeparatorText("Transform gizmo");
        if (ImGui::RadioButton("Move", tools.tool == editor::TransformTool::Move))
            tools.tool = editor::TransformTool::Move;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", tools.tool == editor::TransformTool::Rotate))
            tools.tool = editor::TransformTool::Rotate;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", tools.tool == editor::TransformTool::Scale))
            tools.tool = editor::TransformTool::Scale;
        ImGui::Checkbox("Local axes", &tools.localAxes);
        ImGui::Checkbox("Snap", &tools.snap);
        if (tools.snap)
        {
            ImGui::DragFloat("Move step", &tools.moveStep, .05f, .001f, 100, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Angle step", &tools.rotateStep, 1, 1, 180, "%.0f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Scale step", &tools.scaleStep, .01f, .001f, 10, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }
        if (ImGui::Button("Frame level", ImVec2(-1, 0)))
            mViewport.frameAll(mSession.document());
        ImGui::Separator();
        ImGui::TextWrapped("Left click: select\nRight drag: orbit\nMiddle drag: pan\nWheel: zoom\n"
                           "W / E / R: move / rotate / scale\nF: focus   Ctrl+D: duplicate terrain   Delete: remove");
    }

    MapEditorApplication::Action MapEditorApplication::drawFiles()
    {
        Action action = Action::None;
        ImGui::SeparatorText("Map file");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##MapPath", mPath, sizeof(mPath));
        ImGui::TextDisabled("Relative paths use the content directory.");
        if (ImGui::Button("New level"))
            ImGui::OpenPopup("Discard level?");
        ImGui::SameLine();
        ImGui::BeginDisabled(!mPath[0]);
        if (ImGui::Button("Open"))
        {
            if (mSession.document().count())
                ImGui::OpenPopup("Open another level?");
            else
                action = Action::Open;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
            action = Action::Save;
        ImGui::EndDisabled();
        ImGui::SeparatorText("Build / playtest");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##RuntimePath", mBuildPath, sizeof(mBuildPath));
        ImGui::TextDisabled("Build replaces this .idklvl output file.");
        ImGui::BeginDisabled(!mBuildPath[0]);
        if (ImGui::Button("Build level"))
            action = Action::Build;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (mPlaytest)
        {
            if (ImGui::Button("Stop playtest"))
                action = Action::Stop;
        }
        else if (ImGui::Button("Build & Play"))
            action = Action::Play;
        ImGui::TextWrapped("Build uses the current level, including unsaved changes.");
        if (mSession.status()[0])
            ImGui::TextWrapped("%s", mSession.status());
        return action;
    }

    MapEditorApplication::Action MapEditorApplication::drawDialogs()
    {
        Action action = Action::None;
        if (ImGui::BeginPopupModal("Discard level?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Discard this level and start empty? Save first to keep your work.");
            if (ImGui::Button("Discard"))
            {
                action = Action::New;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Open another level?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Replace this level with the saved map? Save first to keep your work.");
            if (ImGui::Button("Open map"))
            {
                action = Action::Open;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (mConfirmOverwrite)
        {
            ImGui::OpenPopup("Replace map file?");
            mConfirmOverwrite = false;
        }
        if (ImGui::BeginPopupModal("Replace map file?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Replace %s?", mOverwritePath);
            if (ImGui::Button("Replace"))
            {
                action = Action::Replace;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return action;
    }

    MapEditorApplication::Action MapEditorApplication::draw(gfx::RenderEngine &renderer)
    {
        auto *mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("Level editor", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking);
        float width = std::min(365 * ImGui::GetFontSize() / 13, ImGui::GetContentRegionAvail().x * .45f);
        Action action = Action::None;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.11f, .12f, .14f, 1));
        if (ImGui::BeginChild("Tools", ImVec2(width, 0), ImGuiChildFlags_Borders))
        {
            action = drawFiles();
            drawTools();
            Action dialog = drawDialogs();
            if (dialog != Action::None)
                action = dialog;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::BeginChild("Viewport", ImVec2(0, 0), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse))
            mViewport.draw(mSession, renderer, "Level editor - 1 unit = 1 metre", true);
        else
            renderer.SetViewport(0, 0, 0, 0);
        ImGui::EndChild();
        ImGui::End();
        return action;
    }

    void MapEditorApplication::execute(Action action, gfx::RenderEngine &renderer)
    {
        switch (action)
        {
            case Action::None:
                break;
            case Action::New:
                mPreview.clear(renderer);
                mSession.clear();
                mViewport.resetCamera();
                break;
            case Action::Open: {
                MapSession candidate = mSession;
                if (!candidate.load(mPath))
                {
                    mSession.setStatus(candidate.status());
                    break;
                }
                const char *error = nullptr;
                if (!mPreview.rebuild(renderer, candidate.document(), error))
                {
                    mSession.setStatus(error);
                    break;
                }
                mSession = candidate;
                mViewport.frameAll(mSession.document());
                break;
            }
            case Action::Save:
                if (mSession.save(mPath) == MapSession::SaveResult::AlreadyExists)
                {
                    std::memcpy(mOverwritePath, mPath, sizeof(mPath));
                    mConfirmOverwrite = true;
                }
                break;
            case Action::Replace:
                mSession.save(mOverwritePath, true);
                break;
            case Action::Build:
                buildLevel(false);
                break;
            case Action::Play:
                buildLevel(true);
                break;
            case Action::Stop:
                stopPlaytest();
                mSession.setStatus("Playtest stopped.");
                break;
        }
    }

    void MapEditorApplication::cleanPlaytest()
    {
        if (mPlayPath[0])
            unlink(mPlayPath);
        if (mPlayDirectory[0])
            rmdir(mPlayDirectory);
        mPlayPath[0] = mPlayDirectory[0] = 0;
    }

    void MapEditorApplication::stopPlaytest()
    {
        if (mPlaytest)
        {
            SDL_KillProcess(mPlaytest, true);
            SDL_WaitProcess(mPlaytest, true, nullptr);
            SDL_DestroyProcess(mPlaytest);
            mPlaytest = nullptr;
        }
        cleanPlaytest();
    }

    void MapEditorApplication::pollPlaytest()
    {
        int code = 0;
        if (!mPlaytest || !SDL_WaitProcess(mPlaytest, false, &code))
            return;
        SDL_DestroyProcess(mPlaytest);
        mPlaytest = nullptr;
        cleanPlaytest();
        char message[128];
        std::snprintf(message, sizeof(message), "Playtest finished (exit code %d).", code);
        mSession.setStatus(message);
    }

    void MapEditorApplication::buildLevel(bool play)
    {
        alignas(16) static std::byte storage[4 * 1024 * 1024];
        idk::Arena arena(storage);
        level::Level result;
        level::Error error;
        if (!level::Build(mSession, arena, result, error))
        {
            mSession.setStatus(error.message);
            return;
        }
        if (!play)
        {
            size_t length = std::strlen(mBuildPath);
            struct stat source{}, destination{};
            bool same = !std::strcmp(mPath, mBuildPath) ||
                        (stat(mPath, &source) == 0 && stat(mBuildPath, &destination) == 0 &&
                         source.st_dev == destination.st_dev && source.st_ino == destination.st_ino);
            if (same || length < 7 || std::strcmp(mBuildPath + length - 7, ".idklvl"))
            {
                mSession.setStatus("Choose a separate .idklvl runtime output; the source map is preserved.");
                return;
            }
            if (!level::Save(result, mBuildPath, error, true))
                mSession.setStatus(error.message);
            else
                mSession.setStatus("Runtime level built successfully.");
            return;
        }
        if (mPlaytest)
            return;
        std::snprintf(mPlayDirectory, sizeof(mPlayDirectory), "/tmp/idk-playtest-XXXXXX");
        if (!mkdtemp(mPlayDirectory))
        {
            mPlayDirectory[0] = 0;
            mSession.setStatus("Could not create a temporary playtest directory.");
            return;
        }
        std::snprintf(mPlayPath, sizeof(mPlayPath), "%s/level.idklvl", mPlayDirectory);
        if (!level::Save(result, mPlayPath, error))
        {
            mSession.setStatus(error.message);
            cleanPlaytest();
            return;
        }
        char executable[4096];
        const char *base = SDL_GetBasePath();
        if (!base)
        {
            mSession.setStatus("Could not locate the playtest executable directory.");
            cleanPlaytest();
            return;
        }
        int length = std::snprintf(executable, sizeof(executable), "%s%s", base, IDK_PLAYTEST_EXECUTABLE);
        if (length < 0 || size_t(length) >= sizeof(executable))
        {
            mSession.setStatus("Playtest executable path is too long.");
            cleanPlaytest();
            return;
        }
        const char *arguments[] = {executable, "--level", mPlayPath, nullptr};
        mPlaytest = SDL_CreateProcess(arguments, false);
        if (!mPlaytest)
        {
            char message[640];
            std::snprintf(message, sizeof(message),
                          "Playtest launch failed: %s. Build and install the game alongside mapedit.", SDL_GetError());
            mSession.setStatus(message);
            cleanPlaytest();
            return;
        }
        mSession.setStatus("Playtest running. Esc in the game returns here; Stop playtest closes it.");
    }

    void MapEditorApplication::onPreRender(EngineAPI &api)
    {
        if (!mInitialized)
            return;
        auto &renderer = *api.getService<gfx::RenderEngine>();
        pollPlaytest();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        execute(draw(renderer), renderer);
        const char *error = nullptr;
        if (!mPreview.submit(renderer, mSession.document(), error))
            mSession.setStatus(error);
    }

    void MapEditorApplication::onMidRender(EngineAPI &)
    {
        if (!mInitialized)
            return;
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void MapEditorApplication::onGenericEventCallback(EngineAPI &, const void *event)
    {
        if (ImGui::GetCurrentContext())
            ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event *>(event));
    }
} // namespace idk::mapedit
