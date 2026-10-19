#pragma once

#include "ShapeSession.hpp"

namespace idk::editor
{
    // CPU document, selection and file I/O. Storage is supplied by the caller
    // and must outlive the session. No ImGui, SDL or graphics API dependencies.
    class EditorSession: public ShapeSession
    {
    public:
        struct Storage
        {
            alignas(16) std::byte shapes[64 * 1024];
            alignas(16) std::byte imported[2][8 * 1024 * 1024];
            alignas(16) std::byte parsed[8 * 1024 * 1024];
            alignas(16) std::byte scratch[16 * 1024 * 1024];
            alignas(16) std::byte exported[512 * 1024];
        };

        struct ImportCandidate
        {
            ShapeDocument document;
            uint32_t bank = 0;
            uint32_t selection = asset::NoIndex;
        };
        enum class ExportResult
        {
            Saved,
            AlreadyExists,
            Failed
        };

        bool initialize(Storage &);

        // Preparing never changes the live document. Commit only after the
        // candidate's GPU upload succeeds. Valid until the next prepare call.
        bool prepareImport(const char *path, bool replace, ImportCandidate &);
        void commitImport(const ImportCandidate &, const char *path);
        ExportResult exportModel(const char *path, bool overwrite = false);

    private:
        Storage *mStorage = nullptr;
        uint32_t mImportBank = 0;
    };
} // namespace idk::editor
