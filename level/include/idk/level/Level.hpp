#pragma once

#include "idk/asset/Model.hpp"
#include "idk/asset/ExitDestination.hpp"
#include "libidk/memory/Arena.hpp"

namespace idk::level
{
    struct Error
    {
        char message[640]{};
        bool set(const char *);
    };

    struct Location
    {
        glm::vec3 position{0}, rotation{0}, scale{1};
        glm::mat4 transform() const;
    };

    struct Terrain
    {
        char name[48]{};
        glm::vec4 colour{1};
        bool doubleSided = false;
        // Baked world-space geometry; collision uses the same triangles.
        std::span<asset::Vertex> vertices;
        std::span<uint32_t> indices;
    };

    struct Level
    {
        static constexpr uint32_t MaxTerrain = 62;
        static constexpr uint32_t MaxVertices = 32768;
        static constexpr uint32_t MaxIndices = 196608;
        Location entry, exit;
        asset::ExitDestination destination;
        std::span<Terrain> terrain;

        bool validate(Error &) const;
        bool model(idk::Arena &, asset::Model &, Error &) const;
        bool atExit(glm::vec3 feet) const;
    };

    // No native struct dumps: explicit little-endian IEEE float32 fields.
    // Failed loads rewind the arena and leave the caller's Level unchanged.
    bool Load(const char *path, idk::Arena &, Level &, Error &);
    bool Save(const Level &, const char *path, Error &, bool overwrite = false);
} // namespace idk::level
