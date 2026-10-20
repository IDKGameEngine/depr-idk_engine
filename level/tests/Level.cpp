#include "idk/level/Build.hpp"
#include "idk/level/Player.hpp"
#include "idk/mapedit/MapSession.hpp"
#include "libidk/dsa/Timer.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unistd.h>

#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Check failed: %s (%d), %s\n", #value, __LINE__, error.message);                      \
            return false;                                                                                              \
        }                                                                                                              \
    } while (false)

namespace
{
    bool interpolationChecks(idk::level::Player &player, idk::level::Error &error)
    {
        const unsigned rates[] = {60, 120, 144, 240};
        const double unevenFrames[] = {1.0 / 144, 1.0 / 75, 1.0 / 240, .12, 1.0 / 60};
        for (unsigned schedule = 0; schedule < 5; ++schedule)
        {
            idk::FixedTimer timer(30);
            player.reset({0, 0, 0});
            // Reach steady speed before checking render cadence independently
            // of the controller's acceleration.
            for (unsigned i = 0; i < 15; ++i)
                player.step({4, 0, 0}, false, 1.0f / 30);
            float start = player.interpolatedPosition(0).x;
            double elapsed = 0;
            glm::vec3 previousRender = player.interpolatedPosition(0);
            for (unsigned frame = 0; elapsed < 1; ++frame)
            {
                double dt = schedule < 4 ? 1.0 / rates[schedule] : unevenFrames[frame % 5];
                dt = std::min(dt, 1 - elapsed);
                elapsed += dt;
                timer.addDeltaTimeSec(dt);
                while (timer.step())
                    player.step({4, 0, 0}, false, float(timer.getStepSec()));
                double alpha = timer.getInterpolationAlpha();
                CHECK(alpha >= 0 && alpha < 1);
                auto rendered = player.interpolatedPosition(float(alpha));
                // A steady 4 m/s walk must move every render frame, including
                // frames without a physics tick.
                float expected = start + float(4 * elapsed);
                CHECK(std::abs(rendered.x - expected) < .001f);
                CHECK(rendered.x >= previousRender.x - .001f);
                CHECK(rendered.x - previousRender.x <= 4 * dt + .001);
                previousRender = rendered;
            }
        }
        // Teleports must not blend from the preceding run, even before another
        // physics tick. This also covers spawn resets used by level transitions.
        glm::vec3 spawn(-2, 0, 1);
        player.reset(spawn);
        for (float alpha : {0.0f, .25f, .5f, .75f, 1.0f})
            CHECK(player.interpolatedPosition(alpha) == spawn);
        player.step({4, 0, 0}, false, 1.0f / 30);
        CHECK(player.interpolatedPosition(0) == spawn);
        return true;
    }

    bool movementChecks(idk::level::Player &player, idk::level::Error &error)
    {
        using idk::level::Player;
        float referenceDistance = 0, referenceHeight = 0;
        for (unsigned rate : {30u, 60u, 120u})
        {
            float dt = 1.0f / rate;
            player.reset({0, 0, 0});
            for (unsigned i = 0; i < rate / 2; ++i)
                player.step({Player::WalkSpeed, 0, 0}, false, dt);
            float distance = player.position().x;
            CHECK(distance > 2.2f && distance < 3.0f);
            if (rate == 30)
                referenceDistance = distance;
            CHECK(std::abs(distance - referenceDistance) < .03f);
            auto previous = player.position();
            player.step({Player::WalkSpeed, 0, 0}, false, dt);
            CHECK(std::abs((player.position().x - previous.x) / dt - 6) < .05f);
            // Releasing movement should brake promptly on the ground.
            previous = player.position();
            for (unsigned i = 0; i < rate / 2; ++i)
                player.step({}, false, dt);
            CHECK(player.position().x - previous.x < .7f);
            previous = player.position();
            player.step({}, false, dt);
            CHECK(glm::distance(player.position(), previous) < .001f);
            for (unsigned i = 0; i < rate / 5; ++i)
                player.step({-Player::WalkSpeed, 0, 0}, false, dt);
            previous = player.position();
            player.step({-Player::WalkSpeed, 0, 0}, false, dt);
            CHECK((player.position().x - previous.x) / dt < -5.9f);
            // Reverse from full speed and reach the opposite target promptly.
            for (unsigned i = 0; i < rate / 5; ++i)
                player.step({Player::WalkSpeed, 0, 0}, false, dt);
            previous = player.position();
            player.step({Player::WalkSpeed, 0, 0}, false, dt);
            CHECK((player.position().x - previous.x) / dt > 5.9f);

            player.reset({0, 0, 0});
            for (unsigned i = 0; i < rate / 2; ++i)
                player.step({Player::RunSpeed, 0, 0}, false, dt);
            previous = player.position();
            player.step({Player::RunSpeed, 0, 0}, false, dt);
            CHECK(std::abs((player.position().x - previous.x) / dt - 10) < .05f);
            // Jump with running momentum, then release all directional input.
            player.step({Player::RunSpeed, 0, 0}, true, dt);
            previous = player.position();
            for (unsigned i = 0; i < rate / 10; ++i)
                player.step({}, false, dt);
            CHECK(std::abs(player.position().x - previous.x - 1) < .03f);
            CHECK(!player.grounded());
            // Air strafing adds lateral control without erasing forward speed.
            previous = player.position();
            for (unsigned i = 0; i < rate / 10; ++i)
                player.step({0, 0, Player::WalkSpeed}, false, dt);
            CHECK(std::abs(player.position().x - previous.x - 1) < .03f);
            CHECK(player.position().z > previous.z + .03f);
            CHECK(player.position().z < previous.z + .1f);

            // Reset clears momentum; measure the full vertical arc separately.
            player.reset({0, 0, 0});
            player.step({}, false, dt);
            CHECK(glm::length(player.position()) < .05f);
            player.step({}, true, dt);
            float maximum = player.position().y;
            unsigned ticks = 1;
            while (!player.grounded() && ticks < rate)
            {
                player.step({}, false, dt);
                maximum = std::max(maximum, player.position().y);
                ++ticks;
            }
            CHECK(player.grounded());
            CHECK(maximum > 1.2f && maximum < 1.4f);
            CHECK(ticks * dt > .6f && ticks * dt < .8f);
            if (rate == 30)
                referenceHeight = maximum;
            CHECK(std::abs(maximum - referenceHeight) < .03f);
        }
        return true;
    }

    bool checks()
    {
        using namespace idk;
        using namespace idk::level;
        Error error;
        alignas(16) static std::byte geometry[64 * 1024], built[4 * 1024 * 1024], loaded[4 * 1024 * 1024];
        Arena shapes(geometry), arena(built), read(loaded);
        mapedit::MapSession session;
        CHECK(session.initialize(shapes));
        Level level;
        CHECK(!Build(session, arena, level, error));
        CHECK(arena.mark() == 0);
        session.addShape(asset::ShapeKind::Box);
        session.selected()->scale = {20, 1, 20};
        session.selected()->position = {0, -.5f, 0};
        session.placeMarker(mapedit::ObjectRole::Entry);
        session.selected()->position = {0, .1f, 0};
        session.selected()->rotation.y = 45;
        CHECK(!Build(session, arena, level, error));
        session.placeMarker(mapedit::ObjectRole::Exit);
        session.selected()->position = {3, .5f, 0};
        session.addShape(asset::ShapeKind::Box);
        session.selected()->position = {0, 1, -3};
        session.selected()->scale = {8, 2, .5f};
        session.addShape(asset::ShapeKind::Sphere);
        session.selected()->position = {6, 1, 3};
        session.selected()->scale = {-1, 2, 1};
        session.selected()->rotation = {20, 45, 0};
        session.addShape(asset::ShapeKind::Cylinder);
        session.selected()->position = {-6, .5f, 3};
        std::snprintf(session.destination().path, sizeof(session.destination().path), "level/next.idklvl");
        CHECK(Build(session, arena, level, error));
        CHECK(level.terrain.size() == 4);
        CHECK(level.entry.rotation.y == 45);
        CHECK(level.terrain[0].vertices[0].position == glm::vec3(10, -1, -10));
        for (const auto &object : level.terrain)
            for (size_t i = 0; i < object.indices.size(); i += 3)
            {
                auto &a = object.vertices[object.indices[i]];
                auto &b = object.vertices[object.indices[i + 1]];
                auto &c = object.vertices[object.indices[i + 2]];
                CHECK(glm::dot(glm::cross(b.position - a.position, c.position - a.position),
                               a.normal + b.normal + c.normal) > 0);
            }
        CHECK(level.atExit({3, 0, 0}));
        CHECK(!level.atExit({0, 0, 0}));
        Level rotated = level;
        rotated.exit.rotation = {0, 45, 25};
        rotated.exit.scale = {-2, 1, 2};
        CHECK(rotated.atExit({3, 0, 0}));
        char directory[] = "/tmp/idk-level-tests-XXXXXX";
        CHECK(mkdtemp(directory));
        char path[128], source[128];
        std::snprintf(path, sizeof(path), "%s/level.idklvl", directory);
        std::snprintf(source, sizeof(source), "%s/source.idkmap", directory);
        CHECK(Save(level, path, error));
        CHECK(!Save(level, path, error));
        Level result;
        CHECK(Load(path, read, result, error));
        CHECK(result.terrain.size() == 4);
        CHECK(result.entry.position == level.entry.position);
        CHECK(!std::strcmp(result.destination.path, "level/next.idklvl"));
        for (size_t i = 0; i < 4; ++i)
        {
            CHECK(result.terrain[i].vertices.size() == level.terrain[i].vertices.size());
            CHECK(result.terrain[i].indices.size() == level.terrain[i].indices.size());
            for (size_t j = 0; j < level.terrain[i].vertices.size(); ++j)
                CHECK(result.terrain[i].vertices[j].position == level.terrain[i].vertices[j].position);
        }
        alignas(16) std::byte small[64];
        Arena tiny(small);
        size_t mark = read.mark();
        auto *old = result.terrain.data();
        CHECK(!Load(path, tiny, result, error));
        CHECK(tiny.mark() == 0 && result.terrain.data() == old);
        Level previous = level;
        CHECK(!Build(session, tiny, previous, error));
        CHECK(tiny.mark() == 0 && previous.terrain.data() == level.terrain.data());
        session.selected()->scale.x = 0;
        CHECK(!Build(session, arena, previous, error));
        CHECK(previous.terrain.data() == level.terrain.data());
        session.selected()->scale.x = 1;
        CHECK(session.save(source) == mapedit::MapSession::SaveResult::Saved);
        CHECK(!BuildFile(source, source, true, error));
        CHECK(BuildFile(source, path, true, error));
        FILE *file = std::fopen(path, "rb");
        CHECK(file);
        static std::array<unsigned char, 4 * 1024 * 1024> bytes;
        size_t size = std::fread(bytes.data(), 1, bytes.size(), file);
        CHECK(!std::ferror(file));
        std::fclose(file);
        // Version 1 had no 256-byte destination field after the two locations.
        file = std::fopen(path, "wb");
        CHECK(file);
        auto version = bytes[8];
        bytes[8] = 1;
        CHECK(std::fwrite(bytes.data(), 1, 88, file) == 88);
        CHECK(std::fwrite(bytes.data() + 344, 1, size - 344, file) == size - 344);
        std::fclose(file);
        bytes[8] = version;
        Level legacy;
        CHECK(Load(path, read, legacy, error));
        CHECK(!legacy.destination.path[0] && legacy.terrain.size() == level.terrain.size());
        read.rewind(mark);
        // Header version/count and terrain flags/counts/first position/first index.
        const size_t corrupt[] = {0, 8, 12, 88, 408, 412, 416, 420, 420 + 24 * 64};
        for (auto offset : corrupt)
        {
            CHECK(offset + 4 < size);
            auto original =
                std::array<unsigned char, 4>{bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]};
            for (unsigned j = 0; j < 4; ++j)
                bytes[offset + j] = 255;
            if (offset == 88)
                bytes[offset] = '/'; // Absolute destination paths are invalid.
            file = std::fopen(path, "wb");
            CHECK(file);
            CHECK(std::fwrite(bytes.data(), 1, size, file) == size);
            std::fclose(file);
            CHECK(!Load(path, read, result, error));
            CHECK(read.mark() == mark && result.terrain.data() == old);
            for (unsigned j = 0; j < 4; ++j)
                bytes[offset + j] = original[j];
        }
        file = std::fopen(path, "wb");
        CHECK(file);
        CHECK(std::fwrite(bytes.data(), 1, size - 1, file) == size - 1);
        std::fclose(file);
        CHECK(!Load(path, read, result, error));
        CHECK(read.mark() == mark);
        CHECK(Save(level, path, error, true));
        auto position = level.terrain[0].vertices[0].position;
        level.terrain[0].vertices[0].position.x = std::numeric_limits<float>::infinity();
        CHECK(!Save(level, path, error, true));
        level.terrain[0].vertices[0].position = position;
        read.reset();
        CHECK(Load(path, read, result, error));
        {
            alignas(16) static std::byte physicsStorage[64 * 1024];
            Arena physics(physicsStorage);
            PhysicsWorld world;
            CHECK(!world.initialize(tiny, error));
            CHECK(tiny.mark() == 0 && !tiny.failed());
            CHECK(world.initialize(physics, error));
            CHECK(world.prepareTerrain(result, error));
            world.commitTerrain();
            Player player;
            CHECK(player.initialize(world, error));
            player.reset(result.entry.position);
            CHECK(glm::distance(player.position(), result.entry.position) < .001f);
            for (int i = 0; i < 120; ++i)
                player.step({}, false, 1.0f / 60);
            CHECK(player.grounded());
            CHECK(std::abs(player.position().y) < .05f);
            CHECK(interpolationChecks(player, error));
            CHECK(movementChecks(player, error));
            player.reset(result.entry.position);
            for (int i = 0; i < 180; ++i)
                player.step({0, 0, -4}, false, 1.0f / 60);
            CHECK(player.position().z > -2.5f && player.position().z < -2.2f);
            CHECK(std::abs(player.position().y) < .05f);
            player.reset({0, 0, 0});
            player.step({}, false, 1.0f / 60);
            player.step({}, true, 1.0f / 60);
            float maximum = 0;
            for (int i = 0; i < 120; ++i)
            {
                player.step({}, false, 1.0f / 60);
                maximum = std::max(maximum, player.position().y);
            }
            CHECK(maximum > 1);
            CHECK(player.grounded());
            player.reset(result.entry.position);
            for (int i = 0; i < 45; ++i)
                player.step({4, 0, 0}, false, 1.0f / 60);
            CHECK(result.atExit(player.position()));
        }
        // Two-sided plane collision and clean physics teardown/reinitialization.
        session.clear();
        session.addShape(asset::ShapeKind::Plane);
        session.selected()->scale = {10, 1, 10};
        session.placeMarker(mapedit::ObjectRole::Entry);
        session.selected()->position = {0, 1, 0};
        session.placeMarker(mapedit::ObjectRole::Exit);
        arena.reset();
        CHECK(Build(session, arena, level, error));
        {
            alignas(16) static std::byte physicsStorage[64 * 1024];
            Arena physics(physicsStorage);
            PhysicsWorld world;
            CHECK(world.initialize(physics, error));
            CHECK(world.prepareTerrain(level, error));
            world.commitTerrain();
            Player player;
            CHECK(player.initialize(world, error));
            player.reset(level.entry.position);
            for (int i = 0; i < 180; ++i)
                player.step({}, false, 1.0f / 60);
            CHECK(player.grounded() && std::abs(player.position().y) < .05f);
        }
        session.clear();
        for (uint32_t i = 0; i < Level::MaxTerrain; ++i)
            session.addShape(asset::ShapeKind::Sphere);
        session.placeMarker(mapedit::ObjectRole::Entry);
        session.placeMarker(mapedit::ObjectRole::Exit);
        arena.reset();
        CHECK(Build(session, arena, level, error));
        CHECK(level.terrain.size() == Level::MaxTerrain);
        CHECK(Save(level, path, error, true));
        read.reset();
        CHECK(Load(path, read, result, error));
        CHECK(result.terrain.size() == Level::MaxTerrain);
        unlink(path);
        unlink(source);
        rmdir(directory);
        return true;
    }
}

int main()
{
    if (!checks())
        return 1;
    std::puts("Level build/load, collision, movement, jumping, render interpolation, reset and exit checks passed");
    return 0;
}
