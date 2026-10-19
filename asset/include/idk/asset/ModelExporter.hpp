#pragma once
#include "Model.hpp"
#include "libidk/memory/Arena.hpp"

namespace idk::asset
{
    enum class ExportErrorCode
    {
        None,
        InvalidInput,
        Unsupported,
        OutOfMemory,
        Io,
        AlreadyExists
    };

    struct ExportError
    {
        ExportErrorCode code = ExportErrorCode::None;
        char message[512]{};
    };

    // Exports untextured static models to GLB 2.0. Geometry, materials, names,
    // hierarchy and transforms are retained. Scratch is always rewound.
    // On Linux the result is published atomically; overwrite is opt-in.
    bool ExportModelGlb(const Model &model, const char *path, idk::Arena &scratch, ExportError &error,
                        bool overwrite = false);
}
