#pragma once

#include "idk/asset/PrimitiveShape.hpp"
#include "idk/asset/ModelImporter.hpp"
#include <array>

namespace idk::editor
{
    struct ShapeObject
    {
        bool active = false;
        asset::ShapeKind shape = asset::ShapeKind::Box;
        // Count identifies an imported mesh, rather than a generated shape.
        asset::Primitive mesh;
        asset::Material material;
        char name[48]{};
        glm::vec3 position{0}, rotation{0}, scale{1}; // Rotation in degrees, XYZ.
        glm::vec4 color{.65f, .72f, .9f, 1};

        bool isImported() const { return shape == asset::ShapeKind::Count; }

        glm::mat4 transform() const;
        // Accepts affine TRS matrices, including reflections; rejects shear/zero scale.
        bool setTransform(const glm::mat4 &);
    };

    // Stable slots, bounded geometry, no owning heap storage. Geometry lives in
    // the caller's arena; snapshots also borrow names/geometry from the document.
    class ShapeDocument
    {
    public:
        static constexpr uint32_t Capacity = 64;
        static constexpr uint32_t ShapeCount = uint32_t(asset::ShapeKind::Count);
        static constexpr uint32_t MaxImportNodes = 512;
        bool initialize(idk::Arena &geometry);
        uint32_t add(asset::ShapeKind);
        uint32_t duplicate(uint32_t);
        bool remove(uint32_t);
        void clear();
        uint32_t count() const;
        ShapeObject *get(uint32_t);
        const ShapeObject *get(uint32_t) const;
        const asset::Primitive &geometry(asset::ShapeKind kind) const;
        const asset::Primitive &geometry(const ShapeObject &) const;
        // Transactional deep copy into a separate arena. Retained imported meshes
        // are compacted too; successful replacement releases all old references.
        // The destination must not overlap the source or existing document data.
        bool importModel(const asset::Model &, idk::Arena &destination, asset::ImportError &, bool replace = false);
        bool snapshot(idk::Arena &, asset::Model &) const;
        uint32_t pick(glm::vec3 origin, glm::vec3 direction) const;
        bool bounds(glm::vec3 &minimum, glm::vec3 &maximum, uint32_t slot = asset::NoIndex) const;

    private:
        std::array<ShapeObject, Capacity> mObjects{};
        std::array<asset::Primitive, ShapeCount> mGeometry{};
        uint32_t mSerial = 0;
        bool mReady = false;
    };
} // namespace idk::editor
