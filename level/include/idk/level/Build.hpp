#pragma once

#include "Level.hpp"

namespace idk::mapedit
{
    class MapSession;
}

namespace idk::level
{
    // Build directly from a live editor document, including unsaved changes.
    // All returned spans are owned by output. No references to the editor survive.
    bool Build(const mapedit::MapSession &, idk::Arena &output, Level &, Error &);
    bool BuildFile(const char *source, const char *destination, bool overwrite, Error &);
} // namespace idk::level
