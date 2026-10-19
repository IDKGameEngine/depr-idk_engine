#include "idk/mapedit/MapSession.hpp"
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace idk::mapedit
{
    namespace
    {
        // Each record has fixed fields and bounded lines; no parser allocation.
        bool readLine(FILE *file, char (&line)[512])
        {
            if (!std::fgets(line, sizeof(line), file))
                return false;
            size_t size = std::strlen(line);
            if (!size || line[size - 1] != '\n')
                return false;
            line[--size] = 0;
            if (size && line[size - 1] == '\r')
                line[size - 1] = 0;
            return true;
        }

        bool readVector(FILE *file, const char *field, float *values, unsigned count)
        {
            char line[512];
            if (!readLine(file, line))
                return false;
            size_t length = std::strlen(field);
            if (std::strncmp(line, field, length) || line[length] != ' ')
                return false;
            const char *cursor = line + length + 1;
            const char *limit = line + std::strlen(line);
            for (unsigned i = 0; i < count; ++i)
            {
                auto result = std::from_chars(cursor, limit, values[i]);
                if (result.ec != std::errc{} || !std::isfinite(values[i]))
                    return false;
                cursor = result.ptr;
                if (i + 1 < count && *cursor++ != ' ')
                    return false;
            }
            return cursor == limit;
        }

        bool readUnsignedFields(FILE *file, const char *field, std::span<uint32_t> values)
        {
            char line[512];
            if (!readLine(file, line))
                return false;
            size_t length = std::strlen(field);
            if (std::strncmp(line, field, length) || line[length] != ' ')
                return false;
            const char *cursor = line + length + 1;
            const char *limit = line + std::strlen(line);
            for (size_t i = 0; i < values.size(); ++i)
            {
                auto result = std::from_chars(cursor, limit, values[i]);
                if (result.ec != std::errc{})
                    return false;
                cursor = result.ptr;
                if (i + 1 < values.size() && *cursor++ != ' ')
                    return false;
            }
            return cursor == limit;
        }

        bool readMap(FILE *file, MapSession &candidate)
        {
            char line[512];
            if (!readLine(file, line))
                return false;
            bool version2 = !std::strcmp(line, "IDKMAP 2");
            if (!version2 && std::strcmp(line, "IDKMAP 1"))
                return false;
            candidate.clear();
            if (version2)
            {
                if (!readLine(file, line))
                    return false;
                if (std::strcmp(line, "destination"))
                {
                    if (std::strncmp(line, "destination ", 12) ||
                        std::strlen(line + 12) >= asset::ExitDestination::Capacity)
                        return false;
                    std::memcpy(candidate.destination().path, line + 12, std::strlen(line + 12) + 1);
                }
                if (!candidate.destination().valid())
                    return false;
            }
            uint32_t count = 0;
            if (!readUnsignedFields(file, "objects", {&count, 1}) || count > editor::ShapeDocument::Capacity)
                return false;
            for (unsigned i = 0; i < count; ++i)
            {
                uint32_t fields[2]{};
                if (!readUnsignedFields(file, "object", fields) || fields[0] > unsigned(ObjectRole::Exit) ||
                    fields[1] >= editor::ShapeDocument::ShapeCount)
                    return false;
                auto type = ObjectRole(fields[0]);
                auto shape = asset::ShapeKind(fields[1]);
                if (type == ObjectRole::Terrain)
                    candidate.addShape(shape);
                else
                {
                    if (candidate.marker(type) != asset::NoIndex)
                        return false;
                    candidate.placeMarker(type);
                }
                auto *object = candidate.selected();
                if (!object || object->shape != shape)
                    return false;
                if (!readLine(file, line) || std::strncmp(line, "name ", 5) ||
                    std::strlen(line + 5) >= sizeof(object->name))
                    return false;
                std::snprintf(object->name, sizeof(object->name), "%s", line + 5);
                if (!readVector(file, "position", &object->position.x, 3) ||
                    !readVector(file, "rotation", &object->rotation.x, 3) ||
                    !readVector(file, "scale", &object->scale.x, 3) || !readVector(file, "colour", &object->color.x, 4))
                    return false;
            }
            return std::fgetc(file) == EOF && !std::ferror(file);
        }

        bool writeMap(FILE *file, const MapSession &session)
        {
            if (std::fprintf(file, "IDKMAP 2\ndestination%s%s\nobjects %u\n", session.destination().path[0] ? " " : "",
                             session.destination().path, session.document().count()) < 0)
                return false;
            for (uint32_t i = 0; i < editor::ShapeDocument::Capacity; ++i)
            {
                const auto *object = session.document().get(i);
                if (!object)
                    continue;
                if (std::fprintf(file,
                                 "object %u %u\nname %s\n"
                                 "position %.9g %.9g %.9g\nrotation %.9g %.9g %.9g\n"
                                 "scale %.9g %.9g %.9g\ncolour %.9g %.9g %.9g %.9g\n",
                                 unsigned(session.role(i)), unsigned(object->shape), object->name,
                                 double(object->position.x), double(object->position.y), double(object->position.z),
                                 double(object->rotation.x), double(object->rotation.y), double(object->rotation.z),
                                 double(object->scale.x), double(object->scale.y), double(object->scale.z),
                                 double(object->color.r), double(object->color.g), double(object->color.b),
                                 double(object->color.a)) < 0)
                    return false;
            }
            return true;
        }
    } // namespace

    const char *RoleName(ObjectRole role)
    {
        switch (role)
        {
            case ObjectRole::Terrain:
                return "Terrain";
            case ObjectRole::Entry:
                return "Spawn / entry";
            case ObjectRole::Exit:
                return "Exit";
        }
        return "Unknown";
    }

    bool MapSession::initialize(idk::Arena &geometry)
    {
        if (!mDocument.initialize(geometry))
        {
            setStatus("Could not initialize map shape geometry.");
            return false;
        }
        return true;
    }

    ObjectRole MapSession::role(uint32_t slot) const
    {
        if (slot != asset::NoIndex && slot == mEntry)
            return ObjectRole::Entry;
        if (slot != asset::NoIndex && slot == mExit)
            return ObjectRole::Exit;
        return ObjectRole::Terrain;
    }

    uint32_t MapSession::marker(ObjectRole type) const
    {
        if (type == ObjectRole::Entry)
            return mEntry;
        if (type == ObjectRole::Exit)
            return mExit;
        return asset::NoIndex;
    }

    void MapSession::placeMarker(ObjectRole type)
    {
        if (type != ObjectRole::Entry && type != ObjectRole::Exit)
            return;
        if (marker(type) != asset::NoIndex)
        {
            select(marker(type));
            return;
        }
        uint32_t slot = mDocument.add(type == ObjectRole::Entry ? asset::ShapeKind::Cylinder : asset::ShapeKind::Box);
        if (slot == asset::NoIndex)
        {
            setStatus("The map is full. Delete an object to place a marker.");
            return;
        }
        auto &object = *mDocument.get(slot);
        object.position.x = type == ObjectRole::Entry ? -1.5f : 1.5f;
        object.scale = {.5f, 1, .5f};
        object.color = type == ObjectRole::Entry ? glm::vec4(.15f, .9f, .4f, 1) : glm::vec4(1, .45f, .1f, 1);
        std::snprintf(object.name, sizeof(object.name), "%s", RoleName(type));
        (type == ObjectRole::Entry ? mEntry : mExit) = slot;
        select(slot);
        mStatus[0] = 0;
    }

    void MapSession::duplicateSelection()
    {
        if (role(mSelection) != ObjectRole::Terrain)
        {
            setStatus("Entry and exit are unique. Move the existing marker to change its location.");
            return;
        }
        ShapeSession::duplicateSelection();
    }

    void MapSession::deleteSelection()
    {
        if (mSelection == mEntry)
            mEntry = asset::NoIndex;
        if (mSelection == mExit)
        {
            mExit = asset::NoIndex;
            mDestination = {};
        }
        ShapeSession::deleteSelection();
    }

    void MapSession::clear()
    {
        ShapeSession::clear();
        mEntry = mExit = asset::NoIndex;
        mDestination = {};
    }

    bool MapSession::validate() const
    {
        if (!mDestination.valid())
            return false;
        for (uint32_t i = 0; i < editor::ShapeDocument::Capacity; ++i)
        {
            const auto *object = mDocument.get(i);
            if (!object)
                continue;
            if (uint32_t(object->shape) >= editor::ShapeDocument::ShapeCount ||
                !std::memchr(object->name, 0, sizeof(object->name)) || std::strpbrk(object->name, "\r\n"))
                return false;
            for (unsigned c = 0; c < 3; ++c)
                if (!std::isfinite(object->position[c]) || !std::isfinite(object->rotation[c]) ||
                    !std::isfinite(object->scale[c]) || std::abs(object->scale[c]) < 1e-8f)
                    return false;
            for (unsigned c = 0; c < 4; ++c)
                if (!std::isfinite(object->color[c]) || object->color[c] < 0 || object->color[c] > 1)
                    return false;
        }
        return true;
    }

    bool MapSession::load(const char *path)
    {
        if (!path || !*path)
        {
            setStatus("Enter a map path to open.");
            return false;
        }
        FILE *file = std::fopen(path, "rb");
        if (!file)
        {
            std::snprintf(mStatus, sizeof(mStatus), "Open failed: %s", std::strerror(errno));
            return false;
        }
        MapSession candidate = *this;
        bool ok = readMap(file, candidate) && candidate.validate();
        if (std::fclose(file) != 0)
            ok = false;
        if (!ok)
        {
            setStatus("Invalid or unsupported map. The current level was retained.");
            return false;
        }
        *this = candidate;
        select(asset::NoIndex);
        std::snprintf(mStatus, sizeof(mStatus), "Opened %s", path);
        return true;
    }

    MapSession::SaveResult MapSession::save(const char *path, bool overwrite)
    {
        if (!path || !*path || !validate())
        {
            setStatus(
                "Save requires a path, finite transforms, nonzero scales, and valid colours, names and destination.");
            return SaveResult::Failed;
        }
        char temporary[1200];
        int length = std::snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path);
        if (length < 0 || size_t(length) >= sizeof(temporary))
        {
            setStatus("Map path is too long.");
            return SaveResult::Failed;
        }
        int descriptor = mkstemp(temporary);
        FILE *file = descriptor >= 0 ? fdopen(descriptor, "wb") : nullptr;
        bool ok = file != nullptr;
        if (file)
        {
            ok = writeMap(file, *this) && std::fflush(file) == 0 && fsync(descriptor) == 0;
            if (std::fclose(file) != 0)
                ok = false;
        }
        else if (descriptor >= 0)
            close(descriptor);
        if (ok)
            ok = (overwrite ? std::rename(temporary, path) : link(temporary, path)) == 0;
        int error = errno;
        if (descriptor >= 0)
            unlink(temporary);
        if (!ok)
        {
            if (!overwrite && error == EEXIST)
                return SaveResult::AlreadyExists;
            std::snprintf(mStatus, sizeof(mStatus), "Save failed: %s", std::strerror(error));
            return SaveResult::Failed;
        }
        std::snprintf(mStatus, sizeof(mStatus), "Saved %s", path);
        return SaveResult::Saved;
    }
} // namespace idk::mapedit
