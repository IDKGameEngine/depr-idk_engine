#include "idk/editor/ShapeDocument.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace idk::editor
{
    namespace
    {
        bool fail(asset::ImportError &error, asset::ImportErrorCode code, const char *message)
        {
            error.code = code;
            std::snprintf(error.message, sizeof(error.message), "%s", message);
            return false;
        }

        bool equalGeometry(const asset::Primitive &a, const asset::Primitive &b)
        {
            if (a.vertices.size() != b.vertices.size() || a.indices.size() != b.indices.size())
                return false;
            if (a.vertices.data() == b.vertices.data() && a.indices.data() == b.indices.data())
                return true;
            // Separate glTF primitives/materials may reuse accessors but arrive in
            // separate importer buffers. Compare fields, avoiding struct padding.
            if (!std::equal(a.indices.begin(), a.indices.end(), b.indices.begin()))
                return false;
            for (size_t i = 0; i < a.vertices.size(); ++i)
            {
                const auto &av = a.vertices[i];
                const auto &bv = b.vertices[i];
                if (av.position != bv.position || av.normal != bv.normal || av.uv != bv.uv ||
                    av.tangent != bv.tangent || av.color != bv.color)
                    return false;
            }
            return true;
        }

        template <class T>
        bool overlaps(idk::Arena &arena, std::span<T> span)
        {
            idk::Arena view(std::as_writable_bytes(span));
            return arena.overlaps(view);
        }

        bool overlaps(idk::Arena &arena, std::string_view text)
        {
            idk::Arena view({reinterpret_cast<std::byte *>(const_cast<char *>(text.data())), text.size()});
            return arena.overlaps(view);
        }

        struct GeometryCopier
        {
            idk::Arena &output;
            asset::ImportError &error;

            struct Entry
            {
                asset::Primitive source;
                asset::Primitive copy;
            };

            std::array<Entry, ShapeDocument::Capacity> entries{};
            size_t count = 0;

            bool copy(const asset::Primitive &source, asset::Primitive &result)
            {
                for (size_t i = 0; i < count; ++i)
                    if (equalGeometry(source, entries[i].source))
                    {
                        result = entries[i].copy;
                        result.hasTangents = source.hasTangents;
                        return true;
                    }
                size_t indices = source.indices.empty() ? source.vertices.size() : source.indices.size();
                if (source.vertices.empty() || !indices || indices % 3)
                    return fail(error, asset::ImportErrorCode::InvalidInput, "Imported meshes must contain triangles");
                for (uint32_t index : source.indices)
                    if (index >= source.vertices.size())
                        return fail(error, asset::ImportErrorCode::InvalidInput, "Imported mesh index is out of range");
                if (count == entries.size())
                    return fail(error, asset::ImportErrorCode::Unsupported, "Too many unique meshes");
                result = {};
                result.vertices = output.array<asset::Vertex>(source.vertices.size());
                result.indices = output.array<uint32_t>(source.indices.size());
                if (output.failed())
                    return fail(error, asset::ImportErrorCode::OutOfMemory,
                                "Imported geometry exceeds the document memory budget");
                std::copy(source.vertices.begin(), source.vertices.end(), result.vertices.begin());
                std::copy(source.indices.begin(), source.indices.end(), result.indices.begin());
                result.hasTangents = source.hasTangents;
                entries[count++] = {source, result};
                return true;
            }
        };

        bool untextured(const asset::Material &material)
        {
            return material.baseColorTexture == asset::NoIndex && material.normalTexture == asset::NoIndex &&
                   material.metallicRoughnessTexture == asset::NoIndex && material.occlusionTexture == asset::NoIndex &&
                   material.emissiveTexture == asset::NoIndex;
        }

        bool validateProfile(const asset::Model &model, asset::ImportError &error)
        {
            using asset::ImportErrorCode;
            if (model.nodes.empty() || model.primitives.empty())
                return fail(error, ImportErrorCode::InvalidInput, "Import requires a nonempty static model");
            if (model.nodes.size() > ShapeDocument::MaxImportNodes)
                return fail(error, ImportErrorCode::Unsupported, "Import is limited to 512 source nodes");
            if (!model.textures.empty() || !model.images.empty())
                return fail(error, ImportErrorCode::Unsupported,
                            "The model editor currently imports untextured models only");
            for (const auto &material : model.materials)
                if (!untextured(material))
                    return fail(error, ImportErrorCode::Unsupported,
                                "The model editor currently imports untextured materials only");
            return true;
        }

        bool validateStorage(const asset::Model &model, const ShapeDocument &document, idk::Arena &destination,
                             asset::ImportError &error)
        {
            using asset::ImportErrorCode;
            if (overlaps(destination, model.nodes) || overlaps(destination, model.primitives) ||
                overlaps(destination, model.materials))
                return fail(error, ImportErrorCode::InvalidInput, "Import destination must use separate storage");
            for (const auto &p : model.primitives)
                if (overlaps(destination, p.vertices) || overlaps(destination, p.indices) ||
                    overlaps(destination, p.name))
                    return fail(error, ImportErrorCode::InvalidInput, "Import destination overlaps source geometry");
            for (const auto &node : model.nodes)
                if (overlaps(destination, node.primitives) || overlaps(destination, node.name))
                    return fail(error, ImportErrorCode::InvalidInput, "Import destination overlaps source nodes");
            for (const auto &material : model.materials)
                if (overlaps(destination, material.name))
                    return fail(error, ImportErrorCode::InvalidInput, "Import destination overlaps source materials");
            // Also protect data retained by the old document, even in replace mode.
            for (uint32_t i = 0; i < ShapeDocument::Capacity; ++i)
                if (const auto *object = document.get(i))
                {
                    const auto &mesh = document.geometry(*object);
                    if (overlaps(destination, mesh.vertices) || overlaps(destination, mesh.indices))
                        return fail(error, ImportErrorCode::InvalidInput,
                                    "Import destination overlaps the current document");
                }
            return true;
        }

        bool validateInstances(const asset::Model &model, uint32_t availableSlots, asset::ImportError &error)
        {
            using asset::ImportErrorCode;
            size_t instances = 0;
            for (size_t i = 0; i < model.nodes.size(); ++i)
            {
                const auto &node = model.nodes[i];
                if (node.parent != asset::NoIndex && node.parent >= i)
                    return fail(error, ImportErrorCode::InvalidInput, "Imported parents must precede their children");
                if (node.primitives.size() > ShapeDocument::Capacity - instances)
                    return fail(error, ImportErrorCode::Unsupported, "Import would exceed the 64 object limit");
                instances += node.primitives.size();
                for (auto primitive : node.primitives)
                    if (primitive >= model.primitives.size() ||
                        (model.primitives[primitive].material != asset::NoIndex &&
                         model.primitives[primitive].material >= model.materials.size()))
                        return fail(error, ImportErrorCode::InvalidInput,
                                    "Invalid imported primitive/material reference");
            }
            if (!instances)
                return fail(error, ImportErrorCode::InvalidInput, "Imported scene has no mesh instances");
            if (instances > availableSlots)
                return fail(error, ImportErrorCode::Unsupported, "Import would exceed the 64 object limit");
            return true;
        }
    } // namespace

    bool ShapeDocument::importModel(const asset::Model &model, idk::Arena &destination, asset::ImportError &error,
                                    bool replace)
    {
        using asset::ImportErrorCode;
        error = {};
        if (!mReady)
            return fail(error, ImportErrorCode::InvalidInput, "Import requires an initialized document");
        uint32_t availableSlots = Capacity - (replace ? 0 : count());
        if (!validateProfile(model, error) || !validateStorage(model, *this, destination, error) ||
            !validateInstances(model, availableSlots, error))
            return false;

        ShapeDocument next = *this;
        if (replace)
            next.clear();
        idk::ArenaScope allocations(destination);
        GeometryCopier copier{destination, error};
        for (auto &object : next.mObjects)
            if (object.active && object.isImported())
            {
                asset::Primitive copy;
                if (!copier.copy(object.mesh, copy))
                    return false;
                object.mesh = copy;
            }
        std::array<glm::mat4, MaxImportNodes> world;
        for (size_t i = 0; i < model.nodes.size(); ++i)
        {
            const auto &node = model.nodes[i];
            world[i] = node.parent == asset::NoIndex ? node.localTransform : world[node.parent] * node.localTransform;
            for (uint32_t reference : node.primitives)
            {
                uint32_t slot = 0;
                while (next.mObjects[slot].active)
                    ++slot; // Capacity was checked above.
                auto &object = next.mObjects[slot];
                object = {};
                object.active = true;
                object.shape = asset::ShapeKind::Count;
                if (!object.setTransform(world[i]))
                {
                    return fail(error, ImportErrorCode::Unsupported,
                                "Imported world transforms must be finite TRS without shear or zero scale");
                }
                const auto &primitive = model.primitives[reference];
                if (!copier.copy(primitive, object.mesh))
                    return false;
                if (primitive.material != asset::NoIndex)
                    object.material = model.materials[primitive.material];
                object.material.name = {}; // Names belong to object slots, never the parser arena.
                object.color = object.material.baseColor;
                std::string_view name = node.name.empty() ? primitive.name : node.name;
                if (name.empty())
                    std::snprintf(object.name, sizeof(object.name), "Imported mesh %u", ++next.mSerial);
                else
                    std::snprintf(object.name, sizeof(object.name), "%.*s",
                                  int(std::min(name.size(), sizeof(object.name) - 1)), name.data());
            }
        }
        *this = next;
        allocations.commit();
        return true;
    }
} // namespace idk::editor
