#pragma once

#include <cstring>

namespace idk::asset
{
    // Empty means completion. Otherwise this is a compiled level relative to
    // the game's level root, independent of the source map's location.
    struct ExitDestination
    {
        static constexpr unsigned Capacity = 256;
        char path[Capacity]{};

        bool valid() const
        {
            if (!std::memchr(path, 0, sizeof(path)))
                return false;
            size_t length = std::strlen(path);
            if (!length)
                return true;
            if (path[0] == '/' || length < 7 || std::strcmp(path + length - 7, ".idklvl"))
                return false;
            const char *segment = path;
            for (const char *cursor = path;; ++cursor)
            {
                unsigned char byte = static_cast<unsigned char>(*cursor);
                if (byte && (byte < 32 || byte == 127 || byte == '\\' || byte == ':'))
                    return false;
                if (!byte || byte == '/')
                {
                    size_t count = size_t(cursor - segment);
                    if (!count || (count == 1 && segment[0] == '.') ||
                        (count == 2 && segment[0] == '.' && segment[1] == '.'))
                        return false;
                    segment = cursor + 1;
                }
                if (!byte)
                    return true;
            }
        }
    };
} // namespace idk::asset
