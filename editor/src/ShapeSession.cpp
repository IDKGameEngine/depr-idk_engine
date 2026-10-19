#include "idk/editor/ShapeSession.hpp"
#include <cstdio>

namespace idk::editor
{
    void ShapeSession::select(uint32_t slot)
    {
        mSelection = mDocument.get(slot) ? slot : asset::NoIndex;
    }

    void ShapeSession::addShape(asset::ShapeKind kind)
    {
        uint32_t slot = mDocument.add(kind);
        if (slot == asset::NoIndex)
        {
            std::snprintf(mStatus, sizeof(mStatus), "The scene is limited to %u objects.", ShapeDocument::Capacity);
            return;
        }
        mSelection = slot;
        mStatus[0] = 0;
    }

    void ShapeSession::duplicateSelection()
    {
        if (!selected())
            return;
        uint32_t slot = mDocument.duplicate(mSelection);
        if (slot != asset::NoIndex)
            mSelection = slot;
        else
            std::snprintf(mStatus, sizeof(mStatus), "The scene is limited to %u objects.", ShapeDocument::Capacity);
    }

    void ShapeSession::deleteSelection()
    {
        mDocument.remove(mSelection);
        mSelection = asset::NoIndex;
    }

    void ShapeSession::clear()
    {
        mDocument.clear();
        mSelection = asset::NoIndex;
        mStatus[0] = 0;
    }

    void ShapeSession::setStatus(const char *message)
    {
        std::snprintf(mStatus, sizeof(mStatus), "%s", message);
    }
} // namespace idk::editor
