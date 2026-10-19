#include "idk/level/Level.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unistd.h>

namespace idk::level
{
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

    namespace
    {
        constexpr char Magic[8] = {'I', 'D', 'K', 'L', 'V', 'L', '\r', '\n'};

        struct Stream
        {
            FILE *file;
            bool writing, ok = true;

            void bytes(void *data, size_t size)
            {
                if (ok)
                    ok = (writing ? std::fwrite(data, 1, size, file) : std::fread(data, 1, size, file)) == size;
            }

            void integer(uint32_t &value)
            {
                uint8_t data[4]{};
                if (writing)
                    for (unsigned i = 0; i < 4; ++i)
                        data[i] = uint8_t(value >> (8 * i));
                bytes(data, 4);
                if (!writing)
                    value = uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) |
                            (uint32_t(data[3]) << 24);
            }

            void vector(float *values, unsigned count)
            {
                for (unsigned i = 0; i < count; ++i)
                {
                    uint32_t bits = std::bit_cast<uint32_t>(values[i]);
                    integer(bits);
                    if (!writing)
                        values[i] = std::bit_cast<float>(bits);
                }
            }

            void location(Location &value)
            {
                vector(&value.position.x, 3);
                vector(&value.rotation.x, 3);
                vector(&value.scale.x, 3);
            }

            void vertex(asset::Vertex &value)
            {
                vector(&value.position.x, 3);
                vector(&value.normal.x, 3);
                vector(&value.uv.x, 2);
                vector(&value.tangent.x, 4);
                vector(&value.color.x, 4);
            }
        };

        bool finite(const float *values, unsigned count, float bound = 100000)
        {
            for (unsigned i = 0; i < count; ++i)
                if (!std::isfinite(values[i]) || std::abs(values[i]) > bound)
                    return false;
            return true;
        }

        bool colour(glm::vec4 value)
        {
            for (unsigned i = 0; i < 4; ++i)
                if (!std::isfinite(value[i]) || value[i] < 0 || value[i] > 1)
                    return false;
            return true;
        }

        bool locationValid(const Location &value)
        {
            if (!finite(&value.position.x, 3) || !finite(&value.rotation.x, 3) || !finite(&value.scale.x, 3, 1000))
                return false;
            for (unsigned i = 0; i < 3; ++i)
                if (std::abs(value.scale[i]) < .001f)
                    return false;
            return true;
        }

        bool write(FILE *file, const Level &level)
        {
            Stream stream{file, true};
            char magic[8];
            std::memcpy(magic, Magic, 8);
            stream.bytes(magic, 8);
            uint32_t version = 2, count = uint32_t(level.terrain.size());
            stream.integer(version);
            stream.integer(count);
            auto entry = level.entry, exit = level.exit;
            stream.location(entry);
            stream.location(exit);
            auto destination = level.destination;
            stream.bytes(destination.path, sizeof(destination.path));
            for (const auto &object : level.terrain)
            {
                char name[48];
                std::memcpy(name, object.name, 48);
                stream.bytes(name, 48);
                auto color = object.colour;
                stream.vector(&color.x, 4);
                uint32_t flags = object.doubleSided ? 1 : 0, vertices = uint32_t(object.vertices.size()),
                         indices = uint32_t(object.indices.size());
                stream.integer(flags);
                stream.integer(vertices);
                stream.integer(indices);
                for (auto vertex : object.vertices)
                    stream.vertex(vertex);
                for (auto index : object.indices)
                    stream.integer(index);
            }
            return stream.ok;
        }
    }

    bool Error::set(const char *value)
    {
        std::snprintf(message, sizeof(message), "%s", value);
        return false;
    }

    glm::mat4 Location::transform() const
    {
        auto result = glm::translate(glm::mat4(1), position);
        result = glm::rotate(result, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        result = glm::rotate(result, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        result = glm::rotate(result, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        return glm::scale(result, scale);
    }

    bool Level::validate(Error &error) const
    {
        if (!destination.valid())
            return error.set(
                "Exit destination must be a relative .idklvl path without dot segments, or empty for completion.");
        if (terrain.empty() || terrain.size() > MaxTerrain || !locationValid(entry) || !locationValid(exit))
            return error.set("A runtime level requires terrain and valid entry/exit transforms.");
        size_t vertices = 0, indices = 0;
        for (const auto &object : terrain)
        {
            vertices += object.vertices.size();
            indices += object.indices.size();
            if (!std::memchr(object.name, 0, 48) || !colour(object.colour) || object.vertices.empty() ||
                object.indices.empty() || object.indices.size() % 3 || vertices > MaxVertices || indices > MaxIndices)
                return error.set("Invalid terrain name, colour, geometry counts, or level capacity.");
            for (const auto &v : object.vertices)
                if (!finite(&v.position.x, 3) || !finite(&v.normal.x, 3) || !finite(&v.uv.x, 2) ||
                    !finite(&v.tangent.x, 4) || !colour(v.color) || std::abs(glm::length(v.normal) - 1) > .01f ||
                    std::abs(glm::length(glm::vec3(v.tangent)) - 1) > .01f || std::abs(v.tangent.w) != 1)
                    return error.set("Terrain contains invalid or out-of-range vertex data.");
            for (auto index : object.indices)
                if (index >= object.vertices.size())
                    return error.set("Terrain index exceeds its vertex count.");
            for (size_t i = 0; i < object.indices.size(); i += 3)
            {
                auto a = object.vertices[object.indices[i]].position,
                     b = object.vertices[object.indices[i + 1]].position,
                     c = object.vertices[object.indices[i + 2]].position;
                auto cross = glm::cross(b - a, c - a);
                if (!finite(&cross.x, 3, 1e12f) || glm::dot(cross, cross) < 1e-14f)
                    return error.set("Terrain contains a degenerate collision triangle. Increase its scale.");
            }
        }
        return true;
    }

    bool Level::model(idk::Arena &arena, asset::Model &output, Error &error) const
    {
        if (!validate(error))
            return false;
        idk::ArenaScope allocations(arena);
        asset::Model candidate;
        candidate.primitives = arena.array<asset::Primitive>(terrain.size());
        candidate.materials = arena.array<asset::Material>(terrain.size());
        candidate.nodes = arena.array<asset::Node>(terrain.size());
        auto refs = arena.array<uint32_t>(terrain.size());
        if (arena.failed())
            return error.set("Level model arena exhausted.");
        for (size_t i = 0; i < terrain.size(); ++i)
        {
            const auto &object = terrain[i];
            candidate.primitives[i] = {object.name, object.vertices, object.indices, uint32_t(i), true};
            candidate.materials[i].baseColor = object.colour;
            candidate.materials[i].doubleSided = object.doubleSided;
            candidate.materials[i].metallic = 0;
            candidate.nodes[i].name = object.name;
            refs[i] = uint32_t(i);
            candidate.nodes[i].primitives = refs.subspan(i, 1);
        }
        output = candidate;
        allocations.commit();
        return true;
    }

    bool Level::atExit(glm::vec3 feet) const
    {
        auto inverse = glm::inverse(exit.transform());
        glm::vec3 a(inverse * glm::vec4(feet + glm::vec3(0, .3f, 0), 1)),
            b(inverse * glm::vec4(feet + glm::vec3(0, 1.5f, 0), 1));
        auto direction = b - a;
        float near = 0, far = 1;
        for (unsigned i = 0; i < 3; ++i)
        {
            float extent = .5f + .3f * glm::length(glm::vec3(inverse[0][i], inverse[1][i], inverse[2][i]));
            if (std::abs(direction[i]) < 1e-6f)
            {
                if (std::abs(a[i]) > extent)
                    return false;
            }
            else
            {
                float t0 = (-extent - a[i]) / direction[i], t1 = (extent - a[i]) / direction[i];
                near = std::max(near, std::min(t0, t1));
                far = std::min(far, std::max(t0, t1));
                if (near > far)
                    return false;
            }
        }
        return true;
    }

    bool Load(const char *path, idk::Arena &arena, Level &output, Error &error)
    {
        FILE *file = path && *path ? std::fopen(path, "rb") : nullptr;
        if (!file)
            return error.set("Could not open runtime level.");
        idk::ArenaScope allocations(arena);
        Stream stream{file, false};
        char magic[8]{};
        uint32_t version = 0, count = 0;
        stream.bytes(magic, 8);
        stream.integer(version);
        stream.integer(count);
        Level candidate;
        bool ok = stream.ok && !std::memcmp(magic, Magic, 8) && (version == 1 || version == 2) && count > 0 &&
                  count <= Level::MaxTerrain;
        if (ok)
        {
            stream.location(candidate.entry);
            stream.location(candidate.exit);
            if (version == 2)
                stream.bytes(candidate.destination.path, sizeof(candidate.destination.path));
            candidate.terrain = arena.array<Terrain>(count);
            ok = !arena.failed();
        }
        uint32_t totalVertices = 0, totalIndices = 0;
        for (uint32_t i = 0; ok && i < count; ++i)
        {
            auto &object = candidate.terrain[i];
            stream.bytes(object.name, 48);
            stream.vector(&object.colour.x, 4);
            uint32_t flags = 0, vertices = 0, indices = 0;
            stream.integer(flags);
            stream.integer(vertices);
            stream.integer(indices);
            ok = stream.ok && flags <= 1 && vertices > 0 && indices > 0 && indices % 3 == 0 &&
                 vertices <= Level::MaxVertices - totalVertices && indices <= Level::MaxIndices - totalIndices;
            if (!ok)
                break;
            totalVertices += vertices;
            totalIndices += indices;
            object.doubleSided = flags != 0;
            object.vertices = arena.array<asset::Vertex>(vertices);
            object.indices = arena.array<uint32_t>(indices);
            ok = !arena.failed();
            if (!ok)
                break;
            for (auto &vertex : object.vertices)
                stream.vertex(vertex);
            for (auto &index : object.indices)
                stream.integer(index);
            ok = stream.ok;
        }
        ok = ok && std::fgetc(file) == EOF && !std::ferror(file);
        if (std::fclose(file) != 0)
            ok = false;
        if (!ok)
            return error.set("Invalid, truncated, oversized or unsupported runtime level.");
        if (!candidate.validate(error))
            return false;
        output = candidate;
        allocations.commit();
        return true;
    }

    bool Save(const Level &level, const char *path, Error &error, bool overwrite)
    {
        if (!level.validate(error))
            return false;
        if (!path || !*path)
            return error.set("Enter a runtime level destination.");
        char temporary[4096];
        int length = std::snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path);
        if (length < 0 || size_t(length) >= sizeof(temporary))
            return error.set("Runtime level path is too long.");
        int descriptor = mkstemp(temporary);
        FILE *file = descriptor >= 0 ? fdopen(descriptor, "wb") : nullptr;
        bool ok = file != nullptr;
        if (file)
        {
            ok = write(file, level) && std::fflush(file) == 0 && fsync(descriptor) == 0;
            if (std::fclose(file) != 0)
                ok = false;
        }
        else if (descriptor >= 0)
            close(descriptor);
        if (ok)
            ok = (overwrite ? std::rename(temporary, path) : link(temporary, path)) == 0;
        int code = errno;
        if (descriptor >= 0)
            unlink(temporary);
        if (!ok)
        {
            std::snprintf(error.message, sizeof(error.message), "Level write failed: %s%s", std::strerror(code),
                          code == EEXIST && !overwrite ? " (use --force to replace)" : "");
            return false;
        }
        return true;
    }
}
