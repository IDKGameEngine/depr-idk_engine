#include "idk/editor/EditorViewport.hpp"
#include "idk/editor/ShapeSession.hpp"
#include "idk/gfx/RenderEngine.hpp"
#include <imgui.h>
#include <ImGuizmo.h>
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

namespace idk::editor
{
    void EditorViewport::resetCamera()
    {
        mCamera.target = {0, .5f, 0};
        mCamera.distance = 6;
        mCamera.dragButton = -1;
    }

    void EditorViewport::focus(const ShapeDocument &document, uint32_t slot)
    {
        glm::vec3 minimum, maximum;
        if (document.bounds(minimum, maximum, slot))
        {
            mCamera.target = (minimum + maximum) * .5f;
            mCamera.distance = std::clamp(glm::length(maximum - minimum) * 1.8f, .1f, 5000.0f);
        }
        else
            resetCamera();
    }

    void EditorViewport::focusSelection(const ShapeSession &session)
    {
        focus(session.document(), session.selection());
    }

    void EditorViewport::frameAll(const ShapeDocument &document)
    {
        focus(document, asset::NoIndex);
        mCamera.dragButton = -1;
    }

    gfx::Camera EditorViewport::navigate(const Canvas &canvas)
    {
        const auto &io = ImGui::GetIO();
        if (mCamera.dragButton >= 0 && !ImGui::IsMouseDown(mCamera.dragButton))
            mCamera.dragButton = -1;
        if (canvas.hovered && !ImGuizmo::IsUsing())
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                mCamera.dragButton = ImGuiMouseButton_Right;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                mCamera.dragButton = ImGuiMouseButton_Middle;
        }
        if (!ImGuizmo::IsUsing())
        {
            if (mCamera.dragButton == ImGuiMouseButton_Right && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            {
                mCamera.yaw -= io.MouseDelta.x * .35f;
                mCamera.pitch = std::clamp(mCamera.pitch + io.MouseDelta.y * .35f, -85.0f, 85.0f);
            }
            if (canvas.hovered)
                mCamera.distance = std::clamp(mCamera.distance * std::exp(-io.MouseWheel * .15f), .1f, 5000.0f);
        }

        glm::vec3 offset(std::cos(glm::radians(mCamera.pitch)) * std::sin(glm::radians(mCamera.yaw)),
                         std::sin(glm::radians(mCamera.pitch)),
                         std::cos(glm::radians(mCamera.pitch)) * std::cos(glm::radians(mCamera.yaw)));
        gfx::Camera camera;
        camera.view = glm::lookAt(mCamera.target + offset * mCamera.distance, mCamera.target, glm::vec3(0, 1, 0));
        if (mCamera.dragButton == ImGuiMouseButton_Middle && !ImGuizmo::IsUsing() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            auto inverse = glm::inverse(camera.view);
            float units = 2 * mCamera.distance * std::tan(glm::radians(45.0f) / 2) / canvas.size.y;
            mCamera.target +=
                (glm::vec3(inverse[1]) * io.MouseDelta.y - glm::vec3(inverse[0]) * io.MouseDelta.x) * units;
            camera.view = glm::lookAt(mCamera.target + offset * mCamera.distance, mCamera.target, glm::vec3(0, 1, 0));
        }
        camera.proj = glm::perspectiveRH_NO(glm::radians(45.0f), canvas.size.x / canvas.size.y, .01f, 20000.0f);
        return camera;
    }

    void EditorViewport::configureViewport(gfx::RenderEngine &renderer, const Canvas &canvas)
    {
        const auto &io = ImGui::GetIO();
        const ImVec2 display = ImGui::GetMainViewport()->Pos;
        int drawableHeight = 0;
        SDL_GetWindowSizeInPixels(static_cast<SDL_Window *>(renderer.GetWindowHandle()), nullptr, &drawableHeight);
        renderer.SetViewport(
            int((canvas.origin.x - display.x) * io.DisplayFramebufferScale.x),
            drawableHeight - int((canvas.origin.y - display.y + canvas.size.y) * io.DisplayFramebufferScale.y),
            int(canvas.size.x * io.DisplayFramebufferScale.x), int(canvas.size.y * io.DisplayFramebufferScale.y));
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(canvas.origin.x, canvas.origin.y, canvas.size.x, canvas.size.y);
        ImGuizmo::SetOrthographic(false);
    }

    void EditorViewport::handleShortcuts(ShapeSession &session)
    {
        const auto &io = ImGui::GetIO();
        if (io.WantTextInput || ImGui::IsAnyItemActive())
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_W))
            mTools.tool = TransformTool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_E))
            mTools.tool = TransformTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R))
            mTools.tool = TransformTool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_F))
            focusSelection(session);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
            session.duplicateSelection();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            session.deleteSelection();
    }

    void EditorViewport::manipulate(ShapeSession &session, const gfx::Camera &camera)
    {
        auto *object = session.selected();
        if (!object)
            return;
        ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
        float step = mTools.moveStep;
        switch (mTools.tool)
        {
            case TransformTool::Move:
                break;
            case TransformTool::Rotate:
                operation = ImGuizmo::ROTATE;
                step = mTools.rotateStep;
                break;
            case TransformTool::Scale:
                operation = ImGuizmo::SCALE;
                step = mTools.scaleStep;
                break;
        }
        float snap[] = {step, step, step};
        // Local-axis scaling keeps the matrix decomposable for GLB export.
        auto mode = mTools.localAxes || mTools.tool == TransformTool::Scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        glm::mat4 matrix = object->transform();
        if (ImGuizmo::Manipulate(glm::value_ptr(camera.view), glm::value_ptr(camera.proj), operation, mode,
                                 glm::value_ptr(matrix), nullptr, mTools.snap ? snap : nullptr) &&
            !object->setTransform(matrix))
            session.setStatus("Transform must have nonzero scale and no shear.");
    }

    void EditorViewport::pick(ShapeSession &session, const Canvas &canvas, const gfx::Camera &camera)
    {
        if (!canvas.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGuizmo::IsOver() ||
            ImGuizmo::IsUsing())
            return;
        const auto &io = ImGui::GetIO();
        float x = 2 * (io.MousePos.x - canvas.origin.x) / canvas.size.x - 1;
        float y = 1 - 2 * (io.MousePos.y - canvas.origin.y) / canvas.size.y;
        auto inverse = glm::inverse(camera.proj * camera.view);
        auto nearPoint = inverse * glm::vec4(x, y, -1, 1);
        auto farPoint = inverse * glm::vec4(x, y, 1, 1);
        glm::vec3 start = glm::vec3(nearPoint) / nearPoint.w;
        glm::vec3 end = glm::vec3(farPoint) / farPoint.w;
        session.select(session.document().pick(start, glm::normalize(end - start)));
    }

    void EditorViewport::draw(ShapeSession &session, gfx::RenderEngine &renderer, const char *title, bool grid)
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x < 1 || size.y < 1)
        {
            renderer.SetViewport(0, 0, 0, 0);
            return;
        }
        // An interactive item here would capture clicks before ImGuizmo.
        ImGui::Dummy(size);
        const auto &io = ImGui::GetIO();
        Canvas canvas{{origin.x, origin.y},
                      {size.x, size.y},
                      ImGui::IsWindowHovered() && io.MousePos.x >= origin.x && io.MousePos.y >= origin.y &&
                          io.MousePos.x < origin.x + size.x && io.MousePos.y < origin.y + size.y};
        auto camera = navigate(canvas);
        renderer.SetCamera(camera);
        configureViewport(renderer, canvas);
        if (grid)
        {
            const glm::mat4 ground(1);
            ImGuizmo::DrawGridCustomColor(glm::value_ptr(camera.view), glm::value_ptr(camera.proj),
                                          glm::value_ptr(ground), 20, 1, 1, IM_COL32(100, 110, 130, 80),
                                          IM_COL32(100, 110, 130, 40), IM_COL32(120, 150, 200, 130));
        }
        if (canvas.hovered)
            handleShortcuts(session);
        manipulate(session, camera);
        pick(session, canvas, camera);

        auto *draw = ImGui::GetWindowDrawList();
        draw->AddText(ImVec2(origin.x + 12, origin.y + 10), IM_COL32(190, 200, 220, 255), title);
        if (const auto *object = session.selected())
            draw->AddText(ImVec2(origin.x + 12, origin.y + 12 + ImGui::GetTextLineHeight()),
                          IM_COL32(255, 210, 90, 255), object->name);
    }
} // namespace idk::editor
