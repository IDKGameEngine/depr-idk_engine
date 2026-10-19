#include "idk/editor/ScenePreview.hpp"
#include "idk/gfx/RenderEngine.hpp"
#include "PreviewShaders.hpp"

namespace idk::editor
{
    namespace
    {
        bool sameGeometry(const asset::Primitive &a, const asset::Primitive &b)
        {
            return a.vertices.data() == b.vertices.data() && a.vertices.size() == b.vertices.size() &&
                   a.indices.data() == b.indices.data() && a.indices.size() == b.indices.size();
        }

        gfx::MeshHandle createMesh(gfx::RenderEngine &renderer, const asset::Primitive &mesh)
        {
            const gfx::VertexAttribute attributes[] = {
                {0, gfx::VertexAttribType::Float3, offsetof(asset::Vertex, position)},
                {1, gfx::VertexAttribType::Float2, offsetof(asset::Vertex, uv)},
                {2, gfx::VertexAttribType::Float3, offsetof(asset::Vertex, normal)},
                {3, gfx::VertexAttribType::Float4, offsetof(asset::Vertex, tangent)},
                {4, gfx::VertexAttribType::Float4, offsetof(asset::Vertex, color)}};
            return renderer.CreateMesh({mesh.vertices.data(), mesh.vertices.size(), sizeof(asset::Vertex),
                                        mesh.indices.data(), mesh.indices.size(), attributes});
        }

        bool sameMaterial(const gfx::MaterialDesc &a, const gfx::MaterialDesc &b)
        {
            return a.shader == b.shader && a.baseColor == b.baseColor && a.blend == b.blend &&
                   a.alphaCutoff == b.alphaCutoff && a.doubleSided == b.doubleSided;
        }
    } // namespace

    bool ScenePreview::initialize(gfx::RenderEngine &renderer, const ShapeDocument &document)
    {
        mShader = renderer.CreateShader({preview::DefaultVertex, preview::DefaultFragment, "Shape preview"});
        if (!mShader.isValid())
            return false;
        for (uint32_t i = 0; i < ShapeDocument::ShapeCount; ++i)
        {
            mShapes[i] = createMesh(renderer, document.geometry(asset::ShapeKind(i)));
            if (!mShapes[i].isValid())
            {
                shutdown(renderer);
                return false;
            }
        }
        return true;
    }

    void ScenePreview::release(gfx::RenderEngine &renderer, Resources &resources)
    {
        for (const auto &mesh : resources.meshes)
            renderer.DestroyMesh(mesh.handle);
        for (const auto &material : resources.materials)
            renderer.DestroyMaterial(material.handle);
        resources = {};
    }

    void ScenePreview::clear(gfx::RenderEngine &renderer)
    {
        release(renderer, mScene);
    }

    void ScenePreview::shutdown(gfx::RenderEngine &renderer)
    {
        clear(renderer);
        for (auto mesh : mShapes)
            renderer.DestroyMesh(mesh);
        renderer.DestroyShader(mShader);
        mShapes = {};
        mShader = {};
    }

    gfx::MeshHandle ScenePreview::findMesh(const Resources &resources, const asset::Primitive &geometry)
    {
        for (const auto &mesh : resources.meshes)
            if (mesh.handle.isValid() && sameGeometry(mesh.geometry, geometry))
                return mesh.handle;
        return {};
    }

    gfx::MaterialDesc ScenePreview::describeMaterial(const ShapeObject &object) const
    {
        gfx::MaterialDesc description;
        description.shader = mShader;
        description.baseColor = object.color;
        description.doubleSided = object.material.doubleSided;
        description.alphaCutoff = object.material.alphaCutoff;
        switch (object.material.alphaMode)
        {
            case asset::AlphaMode::Opaque:
                description.blend = gfx::BlendMode::Opaque;
                break;
            case asset::AlphaMode::Mask:
                description.blend = gfx::BlendMode::Mask;
                break;
            case asset::AlphaMode::Blend:
                description.blend = gfx::BlendMode::Alpha;
                break;
        }
        return description;
    }

    bool ScenePreview::rebuild(gfx::RenderEngine &renderer, const ShapeDocument &document, const char *&error)
    {
        Resources candidate;
        size_t meshCount = 0;
        for (uint32_t i = 0; i < ShapeDocument::Capacity; ++i)
        {
            const auto *object = document.get(i);
            if (!object)
                continue;
            if (object->isImported() && !findMesh(candidate, object->mesh).isValid())
            {
                auto mesh = createMesh(renderer, object->mesh);
                if (!mesh.isValid())
                {
                    release(renderer, candidate);
                    error = "Could not upload mesh geometry. The current model was retained.";
                    return false;
                }
                candidate.meshes[meshCount++] = {object->mesh, mesh};
            }
            auto &material = candidate.materials[i];
            material.description = describeMaterial(*object);
            material.handle = renderer.CreateMaterial(material.description);
            if (!material.handle.isValid())
            {
                release(renderer, candidate);
                error = "Could not upload materials. The current model was retained.";
                return false;
            }
        }
        clear(renderer);
        mScene = candidate;
        return true;
    }

    void ScenePreview::releaseUnusedMeshes(gfx::RenderEngine &renderer, const ShapeDocument &document)
    {
        for (auto &mesh : mScene.meshes)
        {
            if (!mesh.handle.isValid())
                continue;
            bool used = false;
            for (uint32_t i = 0; i < ShapeDocument::Capacity; ++i)
            {
                const auto *object = document.get(i);
                if (object && object->isImported() && sameGeometry(mesh.geometry, object->mesh))
                {
                    used = true;
                    break;
                }
            }
            if (!used)
            {
                renderer.DestroyMesh(mesh.handle);
                mesh = {};
            }
        }
    }

    bool ScenePreview::submit(gfx::RenderEngine &renderer, const ShapeDocument &document, const char *&error)
    {
        releaseUnusedMeshes(renderer, document);
        bool success = true;
        for (uint32_t i = 0; i < ShapeDocument::Capacity; ++i)
        {
            const auto *object = document.get(i);
            auto &material = mScene.materials[i];
            if (!object)
            {
                renderer.DestroyMaterial(material.handle);
                material = {};
                continue;
            }
            auto description = describeMaterial(*object);
            if (!material.handle.isValid() || !sameMaterial(material.description, description))
            {
                auto replacement = renderer.CreateMaterial(description);
                if (!replacement.isValid())
                {
                    error = "Could not create the shape material.";
                    success = false;
                }
                else
                {
                    renderer.DestroyMaterial(material.handle);
                    material = {replacement, description};
                }
            }
            auto mesh = object->isImported() ? findMesh(mScene, object->mesh) : mShapes[uint32_t(object->shape)];
            if (!renderer.Submit({mesh, material.handle, object->transform()}))
            {
                error = "Could not submit the shape for rendering.";
                success = false;
            }
        }
        return success;
    }
} // namespace idk::editor
