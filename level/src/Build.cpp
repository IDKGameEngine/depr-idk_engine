#include "idk/level/Build.hpp"
#include "idk/mapedit/MapSession.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace idk::level
{
    bool Build(const mapedit::MapSession &session, idk::Arena &arena, Level &output, Error &error)
    {
        const auto *entry = session.document().get(session.marker(mapedit::ObjectRole::Entry));
        const auto *exit = session.document().get(session.marker(mapedit::ObjectRole::Exit));
        if (!entry || !exit)
            return error.set("Build requires one spawn / entry marker and one exit marker.");
        uint32_t count = session.document().count();
        if (count < 3)
            return error.set("Build requires at least one terrain object.");
        idk::ArenaScope allocations(arena);
        Level candidate;
        candidate.entry = {entry->position, entry->rotation, entry->scale};
        candidate.exit = {exit->position, exit->rotation, exit->scale};
        candidate.destination = session.destination();
        candidate.terrain = arena.array<Terrain>(count - 2);
        if (arena.failed())
            return error.set("Level build arena exhausted.");
        uint32_t next = 0;
        for (uint32_t slot = 0; slot < editor::ShapeDocument::Capacity; ++slot)
        {
            const auto *source = session.document().get(slot);
            if (!source || session.role(slot) != mapedit::ObjectRole::Terrain)
                continue;
            if (uint32_t(source->shape) >= editor::ShapeDocument::ShapeCount)
                return error.set("Level builder currently accepts primitive terrain only.");
            for (unsigned i = 0; i < 3; ++i)
                if (!std::isfinite(source->scale[i]) || std::abs(source->scale[i]) < .001f)
                    return error.set("Terrain scales must have magnitude at least 0.001.");
            const auto &geometry = session.document().geometry(*source);
            auto &destination = candidate.terrain[next++];
            std::memcpy(destination.name, source->name, 48);
            destination.colour = source->color;
            destination.doubleSided = source->material.doubleSided;
            destination.vertices = arena.array<asset::Vertex>(geometry.vertices.size());
            destination.indices = arena.array<uint32_t>(geometry.indices.size());
            if (arena.failed())
                return error.set("Level build arena exhausted.");
            auto transform = source->transform();
            auto normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
            bool mirrored = glm::determinant(glm::mat3(transform)) < 0;
            for (size_t i = 0; i < geometry.vertices.size(); ++i)
            {
                auto vertex = geometry.vertices[i];
                vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1));
                vertex.normal = glm::normalize(normalMatrix * vertex.normal);
                auto tangent = glm::mat3(transform) * glm::vec3(vertex.tangent);
                tangent = glm::normalize(tangent - vertex.normal * glm::dot(vertex.normal, tangent));
                vertex.tangent = glm::vec4(tangent, vertex.tangent.w * (mirrored ? -1 : 1));
                destination.vertices[i] = vertex;
            }
            std::copy(geometry.indices.begin(), geometry.indices.end(), destination.indices.begin());
            if (mirrored)
                for (size_t i = 0; i < destination.indices.size(); i += 3)
                    std::swap(destination.indices[i + 1], destination.indices[i + 2]);
        }
        if (!candidate.validate(error))
            return false;
        output = candidate;
        allocations.commit();
        return true;
    }

    bool BuildFile(const char *source, const char *destination, bool overwrite, Error &error)
    {
        struct stat input{}, output{};
        if (!source || !destination ||
            (stat(source, &input) == 0 && stat(destination, &output) == 0 && input.st_dev == output.st_dev &&
             input.st_ino == output.st_ino))
            return error.set("Source and runtime output must be different files.");
        alignas(16) static std::byte geometry[64 * 1024], built[4 * 1024 * 1024];
        idk::Arena shapes(geometry), arena(built);
        mapedit::MapSession session;
        if (!session.initialize(shapes) || !session.load(source))
            return error.set(session.status());
        Level level;
        return Build(session, arena, level, error) && Save(level, destination, error, overwrite);
    }
}
