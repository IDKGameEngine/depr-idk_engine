#include "idk/mapedit/MapSession.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unistd.h>

namespace
{
#define CHECK(value, message)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "%s (line %d)\n", message, __LINE__);                                                 \
            return false;                                                                                              \
        }                                                                                                              \
    } while (false)

    using namespace idk::mapedit;
    using idk::asset::NoIndex;
    using idk::asset::ShapeKind;

    struct Temporary
    {
        char directory[64] = "/tmp/idk-mapedit-XXXXXX";
        char map[96]{};
        char malformed[96]{};

        bool create()
        {
            if (!mkdtemp(directory))
                return false;
            std::snprintf(map, sizeof(map), "%s/level.idkmap", directory);
            std::snprintf(malformed, sizeof(malformed), "%s/bad.idkmap", directory);
            return true;
        }

        ~Temporary()
        {
            if (map[0])
                unlink(map);
            if (malformed[0])
                unlink(malformed);
            rmdir(directory);
        }
    };

    bool writeText(const char *path, const char *text)
    {
        FILE *file = std::fopen(path, "wb");
        if (!file)
            return false;
        bool ok = std::fputs(text, file) >= 0;
        return std::fclose(file) == 0 && ok;
    }

    uint64_t hashFile(const char *path)
    {
        FILE *file = std::fopen(path, "rb");
        if (!file)
            return 0;
        uint64_t hash = 14695981039346656037ull;
        int c = 0;
        while ((c = std::fgetc(file)) != EOF)
            hash = (hash ^ uint64_t(c)) * 1099511628211ull;
        bool ok = !std::ferror(file);
        std::fclose(file);
        return ok ? hash : 0;
    }

    bool checks()
    {
        alignas(16) std::byte storage[64 * 1024];
        idk::Arena arena(storage);
        MapSession session;
        CHECK(session.initialize(arena), session.status());
        CHECK(session.document().count() == 0 && session.marker(ObjectRole::Entry) == NoIndex &&
                  session.marker(ObjectRole::Exit) == NoIndex,
              "A new level should be empty");

        session.addShape(ShapeKind::Box);
        session.selected()->scale = {12, .5f, 12};
        session.selected()->position.y = -.25f;
        session.addShape(ShapeKind::Sphere);
        session.selected()->position = {2.125f, 3.25f, -4.375f};
        session.selected()->rotation = {13, -47, 150};
        session.selected()->scale = {-2, .25f, 3};
        session.selected()->color = {.2f, .4f, .6f, .8f};
        std::snprintf(session.selected()->name, sizeof(session.selected()->name), "Rock \"one\" \\ terrain");
        session.duplicateSelection();
        CHECK(session.document().count() == 3 && session.role(session.selection()) == ObjectRole::Terrain,
              "Terrain duplication failed");
        session.deleteSelection();
        session.addShape(ShapeKind::Plane);
        session.addShape(ShapeKind::Cylinder);
        session.selected()->position.x = std::numeric_limits<float>::denorm_min();
        CHECK(session.document().count() == 4, "Not all basic shapes can be placed");

        session.placeMarker(ObjectRole::Entry);
        uint32_t entry = session.selection();
        session.selected()->position = {1, 2, 3};
        session.selected()->rotation.y = 90;
        std::snprintf(session.selected()->name, sizeof(session.selected()->name), "Player start");
        session.placeMarker(ObjectRole::Exit);
        uint32_t exit = session.selection();
        session.selected()->position = {-10, .5f, 6};
        session.placeMarker(ObjectRole::Entry);
        CHECK(session.selection() == entry && session.document().count() == 6,
              "Placing an existing marker must select it");
        CHECK(session.selected()->position == glm::vec3(1, 2, 3), "Reselecting moved the entry");
        session.duplicateSelection();
        CHECK(session.selection() == entry && session.document().count() == 6, "Entry marker was duplicated");
        session.select(exit);
        session.duplicateSelection();
        CHECK(session.document().count() == 6, "Exit marker was duplicated");
        CHECK(session.document().pick({-10, .5f, 10}, {0, 0, -1}) == exit, "Exit marker cannot be picked");

        Temporary files;
        std::snprintf(session.destination().path, sizeof(session.destination().path), "level/next room.idklvl");
        CHECK(files.create(), "Could not create test directory");
        CHECK(session.save(files.map) == MapSession::SaveResult::Saved, session.status());
        uint64_t original = hashFile(files.map);
        CHECK(original, "Saved map is empty");
        MapSession loaded = session;
        loaded.clear();
        CHECK(loaded.load(files.map), loaded.status());
        CHECK(!std::strcmp(loaded.destination().path, session.destination().path),
              "Exit destination did not roundtrip");
        const char *invalidDestinations[] = {"/absolute.idklvl",   "../outside.idklvl",  "level/../outside.idklvl",
                                             "./room.idklvl",      "level//room.idklvl", "level/room.idkmap",
                                             "level\\room.idklvl", "room\n.idklvl"};
        for (const char *destination : invalidDestinations)
        {
            std::snprintf(loaded.destination().path, sizeof(loaded.destination().path), "%s", destination);
            CHECK(loaded.save(files.map, true) == MapSession::SaveResult::Failed, "Invalid exit destination was saved");
            CHECK(hashFile(files.map) == original, "Invalid destination damaged the saved map");
        }
        loaded.destination() = session.destination();
        CHECK(loaded.document().count() == 6 && loaded.marker(ObjectRole::Entry) == entry &&
                  loaded.marker(ObjectRole::Exit) == exit,
              "Map roles or object count did not roundtrip");
        for (uint32_t i = 0; i < 6; ++i)
        {
            const auto *before = session.document().get(i);
            const auto *after = loaded.document().get(i);
            CHECK(before && after && before->shape == after->shape && before->position == after->position &&
                      before->rotation == after->rotation && before->scale == after->scale &&
                      before->color == after->color && !std::strcmp(before->name, after->name) &&
                      before->material.doubleSided == after->material.doubleSided,
                  "Shape data changed during save/open");
        }
        CHECK(loaded.selection() == NoIndex, "Loading must clear stale selection");
        loaded.select(entry);
        loaded.deleteSelection();
        CHECK(loaded.marker(ObjectRole::Entry) == NoIndex && loaded.marker(ObjectRole::Exit) == exit,
              "Deleting entry corrupted marker roles");
        loaded.addShape(ShapeKind::Box);
        CHECK(loaded.selection() == entry && loaded.role(entry) == ObjectRole::Terrain,
              "A reused terrain slot inherited the removed marker's role");
        loaded.placeMarker(ObjectRole::Entry);
        CHECK(loaded.marker(ObjectRole::Entry) != entry && loaded.document().count() == 7,
              "Deleted entry cannot be placed again");

        CHECK(loaded.save(files.map) == MapSession::SaveResult::AlreadyExists && hashFile(files.map) == original,
              "Saving bypassed overwrite confirmation");
        loaded.select(0);
        loaded.selected()->scale.x = 0;
        CHECK(loaded.save(files.map, true) == MapSession::SaveResult::Failed && hashFile(files.map) == original,
              "Invalid save damaged the existing map");
        loaded.selected()->scale.x = 1;
        loaded.selected()->position.x = std::numeric_limits<float>::infinity();
        CHECK(loaded.save(files.map, true) == MapSession::SaveResult::Failed && hashFile(files.map) == original,
              "Nonfinite transform was saved");
        loaded.selected()->position.x = 0;
        CHECK(loaded.save("/nonexistent-mapedit-directory/level.idkmap") == MapSession::SaveResult::Failed,
              "Invalid destination succeeded");
        CHECK(loaded.save(files.map, true) == MapSession::SaveResult::Saved && hashFile(files.map) != original,
              "Confirmed overwrite failed");

        // A valid first record makes later failures exercise partial-load rollback.
        constexpr const char *box = "object 0 0\nname Floor\nposition 0 0 0\nrotation 0 0 0\n"
                                    "scale 1 1 1\ncolour 1 1 1 1\n";
        const char *invalid[] = {
            "",
            "IDKMAP 3\nobjects 0\n",
            "IDKMAP 2\nobjects 0\n",
            "IDKMAP 2\ndestination ../outside.idklvl\nobjects 0\n",
            "IDKMAP 2\ndestination wrong.idkmap\nobjects 0\n",
            "IDKMAP 1\nobjects 65\n",
            "IDKMAP 1\nobjects 1\n",
            "IDKMAP 1\nobjects 0\nextra",
            "IDKMAP 1\nobjects 1\nobject 3 0\n",
            "IDKMAP 1\nobjects 1\nobject 0 4\n",
            "IDKMAP 1\nobjects 4294967296\n",
            "IDKMAP 1\nobjects -1\n",
            "IDKMAP 1\nobjects 1\nobject 4294967296 0\n",
            "IDKMAP 1\nobjects 1\nobject 0 4294967296\n",
            "IDKMAP 1\nobjects 1\nobject 1 3\n",
            "IDKMAP 1\nobjects 1\nobject 0 0\nname Bad\nposition nan 0 0\n",
            "IDKMAP 1\nobjects 1\nobject 0 0\nname Bad\nposition 0 0 0\nrotation 0 0 0\n"
            "scale 0 1 1\ncolour 1 1 1 1\n",
            "IDKMAP 1\nobjects 1\nobject 0 0\nname Bad\nposition 0 0 0\nrotation 0 0 0\n"
            "scale 1 1 1\ncolour 2 1 1 1\n",
            "IDKMAP 1\nobjects 2\nobject 1 2\nname Entry\nposition 0 0 0\nrotation 0 0 0\n"
            "scale 1 1 1\ncolour 1 1 1 1\nobject 1 2\n",
        };
        for (const char *text : invalid)
        {
            CHECK(writeText(files.malformed, text), "Could not write malformed fixture");
            uint32_t selection = loaded.selection();
            CHECK(!loaded.load(files.malformed) && loaded.selection() == selection && loaded.document().count() == 7 &&
                      loaded.marker(ObjectRole::Exit) == exit &&
                      !std::strcmp(loaded.destination().path, session.destination().path) &&
                      loaded.document().get(0)->scale == glm::vec3(1, .5f, 12),
                  "Failed load changed the level or selection");
        }
        char partial[512];
        std::snprintf(partial, sizeof(partial), "IDKMAP 1\nobjects 2\n%sobject 0 0\n", box);
        CHECK(writeText(files.malformed, partial) && !loaded.load(files.malformed) && loaded.document().count() == 7,
              "Partial import changed the live scene");
        CHECK(!loaded.load("/nonexistent-mapedit-directory/level.idkmap") && loaded.document().count() == 7,
              "Missing file changed the live scene");

        MapSession legacy = loaded;
        CHECK(writeText(files.malformed, "IDKMAP 1\nobjects 1\nobject 0 0\nname Old map\nposition 0 0 0\nrotation 0 0 "
                                         "0\nscale 1 1 1\ncolour 1 1 1 1\n"),
              "Cannot write legacy map");
        CHECK(legacy.load(files.malformed) && !legacy.destination().path[0],
              "Version 1 maps must load with no destination");
        legacy = loaded;
        legacy.select(exit);
        legacy.deleteSelection();
        CHECK(!legacy.destination().path[0], "Deleting exit must clear its destination");
        legacy = loaded;
        legacy.clear();
        CHECK(!legacy.destination().path[0], "New map retained an exit destination");

        MapSession bounded = loaded;
        std::memset(bounded.destination().path, 'a', sizeof(bounded.destination().path));
        std::memcpy(bounded.destination().path + 248, ".idklvl", 8);
        CHECK(bounded.save(files.malformed, true) == MapSession::SaveResult::Saved && bounded.load(files.malformed) &&
                  std::strlen(bounded.destination().path) == 255,
              "Maximum-length destination did not roundtrip");
        bounded.destination().path[255] = 'a';
        CHECK(bounded.save(files.malformed, true) == MapSession::SaveResult::Failed,
              "Unterminated destination was accepted");

        loaded.select(1);
        loaded.deleteSelection();
        CHECK(loaded.save(files.map, true) == MapSession::SaveResult::Saved && loaded.load(files.map) &&
                  loaded.document().count() == 6 &&
                  loaded.document().get(loaded.marker(ObjectRole::Entry))->position == glm::vec3(-1.5f, .5f, 0) &&
                  loaded.document().get(loaded.marker(ObjectRole::Exit))->position == glm::vec3(-10, .5f, 6),
              "Sparse map compaction changed marker identity or location");

        loaded.clear();
        CHECK(loaded.marker(ObjectRole::Entry) == NoIndex && loaded.marker(ObjectRole::Exit) == NoIndex &&
                  loaded.selection() == NoIndex,
              "New level retained marker state");
        CHECK(loaded.save(files.map, true) == MapSession::SaveResult::Saved && loaded.load(files.map) &&
                  !loaded.document().count(),
              "An empty work-in-progress map must roundtrip");
        for (uint32_t i = 0; i < idk::editor::ShapeDocument::Capacity; ++i)
            loaded.addShape(ShapeKind::Box);
        loaded.placeMarker(ObjectRole::Entry);
        CHECK(loaded.document().count() == idk::editor::ShapeDocument::Capacity &&
                  loaded.marker(ObjectRole::Entry) == NoIndex,
              "Full scene corrupted the entry marker");
        loaded.duplicateSelection();
        CHECK(loaded.document().count() == idk::editor::ShapeDocument::Capacity, "Full scene duplicate overflow");
        loaded.deleteSelection();
        loaded.placeMarker(ObjectRole::Entry);
        CHECK(loaded.role(loaded.selection()) == ObjectRole::Entry, "Marker cannot use a freed slot");
        CHECK(loaded.save(files.map, true) == MapSession::SaveResult::Saved && loaded.load(files.map) &&
                  loaded.document().count() == idk::editor::ShapeDocument::Capacity,
              "Full map roundtrip failed");
        return true;
    }
} // namespace

int main()
{
    if (!checks())
        return 1;
    std::puts("Map editing and persistence checks passed");
    return 0;
}
