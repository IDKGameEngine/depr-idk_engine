#pragma once

#include "ShapeDocument.hpp"
#include "idk/gfx/RenderTypes.hpp"
#include "libidk/Types.hpp"

namespace idk::gfx
{
    class RenderEngine;
}

namespace idk::editor
{
    // Owns all editor GPU resources. Document geometry is borrowed for cache
    // lookup only; GPU buffers consume the vertex/index data synchronously.
    class ScenePreview: public idk::Immobile
    {
    public:
        bool initialize(gfx::RenderEngine &, const ShapeDocument &);
        void shutdown(gfx::RenderEngine &);
        void clear(gfx::RenderEngine &);
        // Builds all replacement resources before releasing the current scene.
        bool rebuild(gfx::RenderEngine &, const ShapeDocument &, const char *&error);
        bool submit(gfx::RenderEngine &, const ShapeDocument &, const char *&error);

    private:
        struct ImportedMesh
        {
            asset::Primitive geometry;
            gfx::MeshHandle handle;
        };

        struct Material
        {
            gfx::MaterialHandle handle;
            gfx::MaterialDesc description;
        };

        struct Resources
        {
            std::array<ImportedMesh, ShapeDocument::Capacity> meshes{};
            std::array<Material, ShapeDocument::Capacity> materials{};
        };

        gfx::ShaderHandle mShader;
        std::array<gfx::MeshHandle, ShapeDocument::ShapeCount> mShapes{};
        Resources mScene;

        static void release(gfx::RenderEngine &, Resources &);
        static gfx::MeshHandle findMesh(const Resources &, const asset::Primitive &);
        void releaseUnusedMeshes(gfx::RenderEngine &, const ShapeDocument &);
        gfx::MaterialDesc describeMaterial(const ShapeObject &) const;
    };
} // namespace idk::editor
