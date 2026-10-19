#pragma once

#include "idk/gfx/RenderTypes.hpp"

namespace idk::gfx
{
    class RenderEngine;
}

namespace idk::editor
{
    class ShapeSession;
    class ShapeDocument;

    enum class TransformTool
    {
        Move,
        Rotate,
        Scale
    };

    struct TransformSettings
    {
        TransformTool tool = TransformTool::Move;
        bool localAxes = true;
        bool snap = false;
        float moveStep = .25f;
        float rotateStep = 15;
        float scaleStep = .1f;
    };

    // Camera navigation, picking and ImGuizmo interaction in the current ImGui
    // child window. All document changes go through the session.
    class EditorViewport
    {
    public:
        void draw(ShapeSession &, gfx::RenderEngine &, const char *title = "Shape model builder", bool grid = false);
        void resetCamera();
        void focusSelection(const ShapeSession &);
        void frameAll(const ShapeDocument &);

        TransformSettings &tools() { return mTools; }

    private:
        struct Canvas
        {
            glm::vec2 origin;
            glm::vec2 size;
            bool hovered;
        };

        struct OrbitCamera
        {
            glm::vec3 target{0, .5f, 0};
            float yaw = 45;
            float pitch = 25;
            float distance = 6;
            int dragButton = -1;
        };

        OrbitCamera mCamera;
        TransformSettings mTools;

        void focus(const ShapeDocument &, uint32_t slot);
        gfx::Camera navigate(const Canvas &);
        void configureViewport(gfx::RenderEngine &, const Canvas &);
        void handleShortcuts(ShapeSession &);
        void manipulate(ShapeSession &, const gfx::Camera &);
        void pick(ShapeSession &, const Canvas &, const gfx::Camera &);
    };
} // namespace idk::editor
