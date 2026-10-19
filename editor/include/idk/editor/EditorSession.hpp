#pragma once

#include "ShapeDocument.hpp"

namespace idk::editor
{
    // CPU document, selection and file I/O. Storage is supplied by the caller
    // and must outlive the session. No ImGui, SDL or graphics API dependencies.
    class EditorSession
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

        bool initialize(Storage&);

        ShapeDocument& document() { return mDocument; }

        const ShapeDocument& document() const { return mDocument; }

        uint32_t selection() const { return mSelection; }

        ShapeObject* selected() { return mDocument.get(mSelection); }

        void select(uint32_t slot);
        void addShape(asset::ShapeKind);
        void duplicateSelection();
        void deleteSelection();
        void clear();

        // Preparing never changes the live document. Commit only after the
        // candidate's GPU upload succeeds. Valid until the next prepare call.
        bool prepareImport(const char* path, bool replace, ImportCandidate&);
        void commitImport(const ImportCandidate&, const char* path);
        ExportResult exportModel(const char* path, bool overwrite = false);

        const char* status() const { return mStatus; }

        void setStatus(const char* message);

      private:
        ShapeDocument mDocument;
        Storage* mStorage = nullptr;
        uint32_t mImportBank = 0;
        uint32_t mSelection = asset::NoIndex;
        char mStatus[640]{};
    };
} // namespace idk::editor
