#include "idk/editor/EditorUI.hpp"
#include "idk/editor/EditorSession.hpp"
#include "idk/editor/EditorViewport.hpp"
#include "idk/gfx/RenderEngine.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>

namespace idk::editor
{
    namespace
    {
        // SDL callbacks may arrive after the application shuts down. Keep the
        // mailbox and its callback userdata alive for the process lifetime.
        ImportFileDialog importFileDialog;
    } // namespace

    void EditorUI::shutdown()
    {
        mImport.awaitingFile = false;
    }

    void EditorUI::chooseImportFile(SDL_Window *parent)
    {
        mImport.awaitingFile = importFileDialog.show(parent, mImport.path);
    }

    void EditorUI::pollFileDialog()
    {
        ImportFileDialog::Result result;
        if (!importFileDialog.poll(result) || !mImport.awaitingFile)
            return;
        mImport.awaitingFile = false;
        switch (result.outcome)
        {
            case ImportFileDialog::Outcome::Selected:
                std::memcpy(mImport.path, result.path, sizeof(mImport.path));
                mImport.error[0] = 0;
                mImport.open = true;
                break;
            case ImportFileDialog::Outcome::Cancelled:
                break;
            case ImportFileDialog::Outcome::Error:
                std::snprintf(mImport.error, sizeof(mImport.error), "File dialog failed: %s", result.error);
                mImport.open = true;
                break;
        }
    }

    void EditorUI::showImportError(const char *error)
    {
        std::snprintf(mImport.error, sizeof(mImport.error), "Import failed: %s", error);
        mImport.open = true;
    }

    void EditorUI::confirmOverwrite(const char *path)
    {
        std::snprintf(mExport.overwritePath, sizeof(mExport.overwritePath), "%s", path);
        mExport.confirmOverwrite = true;
    }

    void EditorUI::drawMenu()
    {
        if (!ImGui::BeginMenuBar())
            return;
        bool clear = ImGui::MenuItem("New model");
        if (ImGui::MenuItem("Import...", nullptr, false, !importFileDialog.busy()))
        {
            mAction.type = EditorAction::Type::ChooseImportFile;
            mImport.error[0] = 0;
        }
        ImGui::EndMenuBar();
        if (clear)
            ImGui::OpenPopup("Clear model?");
    }

    void EditorUI::drawShapes(EditorSession &session)
    {
        ImGui::TextUnformatted("ADD SHAPE");
        for (uint32_t i = 0; i < ShapeDocument::ShapeCount; ++i)
        {
            if (i % 2)
                ImGui::SameLine();
            float width = ImGui::GetContentRegionAvail().x / (i % 2 ? 1 : 2) - 4;
            auto kind = asset::ShapeKind(i);
            if (ImGui::Button(asset::ShapeName(kind), ImVec2(width, 0)))
                session.addShape(kind);
        }
    }

    void EditorUI::drawObjectList(EditorSession &session, EditorViewport &viewport)
    {
        ImGui::Separator();
        ImGui::Text("Objects (%u / %u)", session.document().count(), ShapeDocument::Capacity);
        if (ImGui::BeginChild("Objects", ImVec2(0, 150 * ImGui::GetFontSize() / 13), ImGuiChildFlags_Borders))
        {
            for (uint32_t i = 0; i < ShapeDocument::Capacity; ++i)
            {
                const auto *object = session.document().get(i);
                if (!object)
                    continue;
                ImGui::PushID(int(i));
                if (ImGui::Selectable(object->name, session.selection() == i))
                    session.select(i);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::BeginDisabled(!session.selected());
        if (ImGui::Button("Duplicate"))
            session.duplicateSelection();
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
            session.deleteSelection();
        ImGui::SameLine();
        if (ImGui::Button("Focus"))
            viewport.focusSelection(session);
        ImGui::EndDisabled();
    }

    void EditorUI::drawInspector(ShapeObject &object)
    {
        ImGui::SeparatorText("Selected object");
        ImGui::InputText("Name", object.name, sizeof(object.name));
        ImGui::TextUnformatted(object.isImported() ? "Imported mesh" : asset::ShapeName(object.shape));
        ImGui::DragFloat3("Position", glm::value_ptr(object.position), .025f, -10000, 10000, "%.3f",
                          ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat3("Rotation", glm::value_ptr(object.rotation), .5f, -360, 360, "%.1f",
                          ImGuiSliderFlags_AlwaysClamp);
        glm::vec3 previousScale = object.scale;
        if (ImGui::DragFloat3("Scale", glm::value_ptr(object.scale), .01f, -1000, 1000, "%.3f",
                              ImGuiSliderFlags_AlwaysClamp))
        {
            for (unsigned i = 0; i < 3; ++i)
                if (std::abs(object.scale[i]) < .001f)
                    object.scale[i] = std::copysign(.001f, previousScale[i]);
        }
        ImGui::ColorEdit4("Colour", glm::value_ptr(object.color));
        ImGui::Checkbox("Double sided", &object.material.doubleSided);
        int alpha = int(object.material.alphaMode);
        if (ImGui::Combo("Alpha", &alpha, "Opaque\0Mask\0Blend\0"))
            object.material.alphaMode = asset::AlphaMode(alpha);
        if (object.material.alphaMode == asset::AlphaMode::Mask)
            ImGui::SliderFloat("Cutoff", &object.material.alphaCutoff, 0, 1);
    }

    void EditorUI::drawTransformTools(EditorViewport &viewport)
    {
        auto &tools = viewport.tools();
        ImGui::SeparatorText("Transform gizmo");
        if (ImGui::RadioButton("Move", tools.tool == TransformTool::Move))
            tools.tool = TransformTool::Move;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", tools.tool == TransformTool::Rotate))
            tools.tool = TransformTool::Rotate;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", tools.tool == TransformTool::Scale))
            tools.tool = TransformTool::Scale;
        ImGui::Checkbox("Local axes", &tools.localAxes);
        ImGui::Checkbox("Snap", &tools.snap);
        if (tools.snap)
        {
            ImGui::DragFloat("Move step", &tools.moveStep, .01f, .001f, 100, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Angle step", &tools.rotateStep, 1, 1, 180, "%.0f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Scale step", &tools.scaleStep, .01f, .001f, 10, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }
    }

    void EditorUI::drawExport(const EditorSession &session)
    {
        ImGui::SeparatorText("Export model");
        ImGui::TextWrapped("GLB retains each mesh, transform, name and material.");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ExportPath", mExport.path, sizeof(mExport.path));
        ImGui::TextDisabled("Relative paths use the content directory.");
        ImGui::BeginDisabled(!session.document().count() || !mExport.path[0]);
        if (ImGui::Button("Export GLB", ImVec2(-1, 0)))
        {
            mAction.type = EditorAction::Type::Export;
            std::snprintf(mAction.path, sizeof(mAction.path), "%s", mExport.path);
        }
        ImGui::EndDisabled();
        if (session.status()[0])
            ImGui::TextWrapped("%s", session.status());
    }

    void EditorUI::drawTools(EditorSession &session, EditorViewport &viewport)
    {
        drawShapes(session);
        drawObjectList(session, viewport);
        if (auto *object = session.selected())
            drawInspector(*object);
        drawTransformTools(viewport);
        drawExport(session);
        ImGui::Separator();
        ImGui::TextWrapped("Left click: select\nRight drag: orbit\nMiddle drag: pan\nWheel: zoom\nW / E / R: move / "
                           "rotate / scale\nF: focus   Ctrl+D: duplicate   Delete: remove");
    }

    void EditorUI::drawImportDialog(const EditorSession &session)
    {
        if (mImport.open)
        {
            ImGui::OpenPopup("Import model");
            mImport.open = false;
        }
        ImGui::SetNextWindowSize(ImVec2(560 * ImGui::GetFontSize() / 13, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Import model", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextWrapped(
            "Open a static, untextured GLB or glTF model. Each mesh primitive becomes an editable object.");
        if (mImport.path[0])
            ImGui::TextWrapped("File: %s", mImport.path);
        else
            ImGui::TextUnformatted("No file selected.");
        ImGui::BeginDisabled(importFileDialog.busy());
        if (ImGui::Button("Choose file..."))
        {
            mAction.type = EditorAction::Type::ChooseImportFile;
            mImport.error[0] = 0;
        }
        ImGui::EndDisabled();
        ImGui::Checkbox("Replace current model", &mImport.replace);
        if (mImport.replace && session.document().count())
            ImGui::TextWrapped("A successful import will discard the current %u objects. Export first to keep them.",
                               session.document().count());
        else
            ImGui::TextUnformatted("Imported objects will be added to the current model.");
        if (mImport.error[0])
            ImGui::TextWrapped("%s", mImport.error);
        bool choosingFile = mAction.type == EditorAction::Type::ChooseImportFile;
        ImGui::BeginDisabled(!mImport.path[0] || importFileDialog.busy() || choosingFile);
        if (ImGui::Button("Import"))
        {
            mAction.type = EditorAction::Type::Import;
            mAction.replace = mImport.replace;
            std::memcpy(mAction.path, mImport.path, sizeof(mAction.path));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void EditorUI::drawDialogs(const EditorSession &session)
    {
        if (ImGui::BeginPopupModal("Clear model?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Discard the current objects? Export first to keep your model.");
            if (ImGui::Button("Discard"))
            {
                mAction.type = EditorAction::Type::Clear;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (mExport.confirmOverwrite)
        {
            ImGui::OpenPopup("Replace exported model?");
            mExport.confirmOverwrite = false;
        }
        if (ImGui::BeginPopupModal("Replace exported model?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Replace %s?", mExport.overwritePath);
            if (ImGui::Button("Replace"))
            {
                mAction.type = EditorAction::Type::Export;
                mAction.overwrite = true;
                std::memcpy(mAction.path, mExport.overwritePath, sizeof(mAction.path));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        drawImportDialog(session);
    }

    EditorAction EditorUI::draw(EditorSession &session, EditorViewport &viewport, gfx::RenderEngine &renderer)
    {
        mAction = {};
        pollFileDialog();
        auto *mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("Model editor", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar);
        drawMenu();
        float width = std::min(350 * ImGui::GetFontSize() / 13, ImGui::GetContentRegionAvail().x * .45f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.11f, .12f, .14f, 1));
        if (ImGui::BeginChild("Tools", ImVec2(width, 0), ImGuiChildFlags_Borders))
            drawTools(session, viewport);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::BeginChild("Viewport", ImVec2(0, 0), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse))
            viewport.draw(session, renderer);
        else
            renderer.SetViewport(0, 0, 0, 0);
        ImGui::EndChild();
        drawDialogs(session);
        ImGui::End();
        return mAction;
    }
} // namespace idk::editor
