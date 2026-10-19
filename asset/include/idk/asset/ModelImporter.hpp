#pragma once
#include "Model.hpp"
#include "libidk/memory/Arena.hpp"

namespace idk::asset
{
    enum class ImportErrorCode
    {
        None,
        InvalidInput,
        Unsupported,
        Io,
        OutOfMemory,
        Decode
    };

    struct ImportError
    {
        ImportErrorCode code = ImportErrorCode::None;
        char message[512]{};
    };

    // All returned spans/strings live in output. Parser and decoder use scratch.
    // Both arenas are restored on failure; scratch is restored on success too.
    // Arenas must have non-overlapping storage. No heap fallback and no exception path.
    bool ImportModel(const char *path, idk::Arena &output, idk::Arena &scratch, Model &model, ImportError &error);
}
