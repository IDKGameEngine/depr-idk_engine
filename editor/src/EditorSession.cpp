#include "idk/editor/EditorSession.hpp"
#include "idk/asset/ModelExporter.hpp"
#include <cstdio>

namespace idk::editor
{
    bool EditorSession::initialize(Storage &storage)
    {
        idk::Arena shapes(storage.shapes);
        if (!mDocument.initialize(shapes))
        {
            setStatus("Shape geometry arena exhausted");
            return false;
        }
        mStorage = &storage;
        return true;
    }

    bool EditorSession::prepareImport(const char *path, bool replace, ImportCandidate &candidate)
    {
        if (!mStorage)
            return false;
        idk::Arena parsed(mStorage->parsed);
        idk::Arena scratch(mStorage->scratch);
        candidate.document = mDocument;
        candidate.bank = 1 - mImportBank;
        candidate.selection = 0;
        if (!replace)
        {
            while (candidate.selection < ShapeDocument::Capacity && mDocument.get(candidate.selection))
                ++candidate.selection;
        }

        asset::Model source;
        asset::ImportError error;
        idk::Arena destination(mStorage->imported[candidate.bank]);
        if (!asset::ImportModel(path, parsed, scratch, source, error) ||
            !candidate.document.importModel(source, destination, error, replace))
        {
            setStatus(error.message);
            return false;
        }
        return true;
    }

    void EditorSession::commitImport(const ImportCandidate &candidate, const char *path)
    {
        mDocument = candidate.document;
        mImportBank = candidate.bank;
        mSelection = candidate.selection;
        std::snprintf(mStatus, sizeof(mStatus), "Imported %s (%u objects in model)", path, mDocument.count());
    }

    EditorSession::ExportResult EditorSession::exportModel(const char *path, bool overwrite)
    {
        if (!mStorage)
            return ExportResult::Failed;
        idk::Arena scratch(mStorage->exported);
        asset::Model model;
        if (!mDocument.snapshot(scratch, model))
        {
            setStatus("Add a shape before exporting.");
            return ExportResult::Failed;
        }

        asset::ExportError error;
        if (asset::ExportModelGlb(model, path, scratch, error, overwrite))
        {
            std::snprintf(mStatus, sizeof(mStatus), "Exported %u objects to %s", mDocument.count(), path);
            return ExportResult::Saved;
        }
        if (error.code == asset::ExportErrorCode::AlreadyExists && !overwrite)
            return ExportResult::AlreadyExists;

        std::snprintf(mStatus, sizeof(mStatus), "Export failed: %s", error.message);
        return ExportResult::Failed;
    }
} // namespace idk::editor
