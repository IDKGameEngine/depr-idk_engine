#pragma once

#include "ShapeDocument.hpp"

namespace idk::editor
{
    // Shared editing commands used by the model and level editor viewports.
    class ShapeSession
    {
    public:
        virtual ~ShapeSession() = default;

        ShapeDocument &document() { return mDocument; }

        const ShapeDocument &document() const { return mDocument; }

        uint32_t selection() const { return mSelection; }

        ShapeObject *selected() { return mDocument.get(mSelection); }

        void select(uint32_t slot);
        void addShape(asset::ShapeKind);
        virtual void duplicateSelection();
        virtual void deleteSelection();
        virtual void clear();

        const char *status() const { return mStatus; }

        void setStatus(const char *message);

    protected:
        ShapeDocument mDocument;
        uint32_t mSelection = asset::NoIndex;
        char mStatus[640]{};
    };
} // namespace idk::editor
