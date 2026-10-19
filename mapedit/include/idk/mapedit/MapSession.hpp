#pragma once

#include "idk/editor/ShapeSession.hpp"
#include "idk/asset/ExitDestination.hpp"

namespace idk::mapedit
{
    enum class ObjectRole : uint32_t
    {
        Terrain,
        Entry,
        Exit
    };

    // The marker meshes are editing aids. Their roles and transforms, rather
    // than mesh names or colours, identify entry and exit in the saved map.
    class MapSession final: public editor::ShapeSession
    {
    public:
        enum class SaveResult
        {
            Saved,
            AlreadyExists,
            Failed
        };

        bool initialize(idk::Arena &geometry);
        ObjectRole role(uint32_t slot) const;
        uint32_t marker(ObjectRole) const;
        void placeMarker(ObjectRole);
        void duplicateSelection() override;
        void deleteSelection() override;
        void clear() override;

        asset::ExitDestination &destination() { return mDestination; }

        const asset::ExitDestination &destination() const { return mDestination; }

        // Loading validates a complete candidate before replacing the scene.
        // Saving publishes a temporary file atomically in the same directory.
        bool load(const char *path);
        SaveResult save(const char *path, bool overwrite = false);

    private:
        uint32_t mEntry = asset::NoIndex;
        uint32_t mExit = asset::NoIndex;
        asset::ExitDestination mDestination;
        bool validate() const;
    };

    const char *RoleName(ObjectRole);
} // namespace idk::mapedit
