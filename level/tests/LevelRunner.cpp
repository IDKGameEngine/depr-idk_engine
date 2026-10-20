#include "idk/level/LevelRunner.hpp"
#include "idk/level/Build.hpp"
#include "idk/mapedit/MapSession.hpp"
#include "idk/gfx/RenderEngine.hpp"
#include "idk/EngineAPI.hpp"
#include "libidk/gpu/gl.hpp"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Check failed: %s (%d): %s\n", #value, __LINE__, error.message);                      \
            return false;                                                                                              \
        }                                                                                                              \
    } while (false)

namespace
{
    struct Temporary
    {
        char base[64] = "/tmp/idk-transitions-XXXXXX", root[96]{}, a[128]{}, b[128]{}, bad[128]{}, outside[128]{},
             link[128]{};

        bool create()
        {
            if (!mkdtemp(base))
                return false;
            std::snprintf(root, sizeof(root), "%s/levels", base);
            if (mkdir(root, 0700) != 0)
                return false;
            std::snprintf(a, sizeof(a), "%s/a.idklvl", root);
            std::snprintf(b, sizeof(b), "%s/b.idklvl", root);
            std::snprintf(bad, sizeof(bad), "%s/bad.idklvl", root);
            std::snprintf(outside, sizeof(outside), "%s/outside.idklvl", base);
            std::snprintf(link, sizeof(link), "%s/escape.idklvl", root);
            return true;
        }

        ~Temporary()
        {
            unlink(a);
            unlink(b);
            unlink(bad);
            unlink(link);
            unlink(outside);
            rmdir(root);
            rmdir(base);
        }
    };

    bool checks(idk::gfx::RenderEngine &renderer, idk::EngineAPI &api)
    {
        using namespace idk;
        using namespace idk::level;
        Error error;
        Temporary files;
        CHECK(files.create());
        alignas(16) static std::byte shapes[64 * 1024], bytes[4 * 1024 * 1024];
        Arena geometry(shapes), built(bytes);
        mapedit::MapSession session;
        CHECK(session.initialize(geometry));
        session.addShape(asset::ShapeKind::Box);
        session.selected()->position = {0, -.5f, 0};
        session.selected()->scale = {20, 1, 20};
        session.selected()->color = {1, 0, 0, 1};
        session.placeMarker(mapedit::ObjectRole::Entry);
        session.selected()->position = {0, .1f, 0};
        session.placeMarker(mapedit::ObjectRole::Exit);
        session.selected()->position = {3, .5f, 0};
        std::snprintf(session.destination().path, sizeof(session.destination().path), "b.idklvl");
        Level level;
        CHECK(Build(session, built, level, error));
        CHECK(Save(level, files.a, error));
        session.document().get(0)->position.y = 4.5f;
        session.document().get(0)->color = {0, 0, 1, 1};
        session.document().get(session.marker(mapedit::ObjectRole::Entry))->position.y = 5.1f;
        session.document().get(session.marker(mapedit::ObjectRole::Exit))->position.y = 5.5f;
        std::snprintf(session.destination().path, sizeof(session.destination().path), "a.idklvl");
        built.reset();
        CHECK(Build(session, built, level, error));
        CHECK(Save(level, files.b, error));
        CHECK(Save(level, files.outside, error));
        CHECK(symlink("../outside.idklvl", files.link) == 0);
        FILE *file = std::fopen(files.bad, "wb");
        CHECK(file);
        std::fputs("bad level", file);
        std::fclose(file);

        alignas(16) static std::byte physicsStorage[64 * 1024];
        Arena physics(physicsStorage);
        PhysicsWorld world;
        CHECK(world.initialize(physics, error));
        Player player;
        CHECK(player.initialize(world, error));
        static LevelRunner::Storage storage;
        LevelRunner runner;
        CHECK(runner.initialize(storage, world, player, renderer, files.root, error));
        CHECK(runner.loadInitial(files.a, error));
        CHECK(runner.generation() == 1);
        CHECK(player.interpolatedPosition(0) == player.position());
        CHECK(!runner.loadInitial(files.b, error));
        auto initialModel = runner.model();
        auto *initialData = runner.current().terrain.data();
        CHECK(world.prepareTerrain(level, error));
        player.reset({0, 7, 0});
        for (int i = 0; i < 240; ++i)
            player.step({}, false, 1.0f / 60);
        CHECK(std::abs(player.position().y) < .05f); // Prepared upper floor must not collide yet.
        world.discardTerrain();

        asset::ExitDestination next;
        std::snprintf(next.path, sizeof(next.path), "b.idklvl");
        auto position = player.position();
        // A collision preparation failure must also release the candidate GPU
        // model and leave the active level and external staged body untouched.
        CHECK(world.prepareTerrain(level, error));
        CHECK(runner.requestTransition(next, error));
        renderer.onPreRender(api);
        CHECK(runner.processPending(error) == LevelRunner::Transition::Failed);
        CHECK(runner.generation() == 1 && runner.model() == initialModel &&
              runner.current().terrain.data() == initialData && player.position() == position);
        CHECK(!world.prepareTerrain(level, error));
        world.discardTerrain();
        CHECK(runner.requestTransition(next, error));
        CHECK(runner.model() == initialModel && runner.current().terrain.data() == initialData &&
              player.position() == position);
        CHECK(!runner.requestTransition(next, error));
        CHECK(runner.pending());
        renderer.onPreRender(api);
        CHECK(runner.processPending(error) == LevelRunner::Transition::Changed);
        CHECK(runner.generation() == 2 && runner.model() != initialModel &&
              runner.current().terrain.data() != initialData);
        CHECK(player.position() == glm::vec3(0, 5.1f, 0));
        CHECK(player.interpolatedPosition(0) == player.position());
        CHECK(player.interpolatedPosition(.5f) == player.position());
        for (int i = 0; i < 120; ++i)
            player.step({}, false, 1.0f / 60);
        CHECK(player.grounded() && std::abs(player.position().y - 5) < .05f);
        auto retained = runner.model();
        auto *retainedData = runner.current().terrain.data();
        position = player.position();
        for (const char *path : {"missing.idklvl", "bad.idklvl", "escape.idklvl"})
        {
            std::snprintf(next.path, sizeof(next.path), "%s", path);
            CHECK(runner.requestTransition(next, error));
            renderer.onPreRender(api);
            CHECK(runner.processPending(error) == LevelRunner::Transition::Failed);
            CHECK(!runner.pending() && runner.generation() == 2 && runner.model() == retained &&
                  runner.current().terrain.data() == retainedData && player.position() == position);
            CHECK(runner.processPending(error) == LevelRunner::Transition::None);
        }
        for (const char *path : {"/absolute.idklvl", "../outside.idklvl", "a/../outside.idklvl"})
        {
            std::snprintf(next.path, sizeof(next.path), "%s", path);
            CHECK(!runner.requestTransition(next, error));
            CHECK(!runner.pending());
        }
        std::array<gfx::MaterialHandle, 2048> held{};
        gfx::MaterialDesc material;
        material.shader = renderer.GetDefaultShader();
        size_t count = 0;
        for (; count < held.size(); ++count)
        {
            held[count] = renderer.CreateMaterial(material);
            if (!held[count].isValid())
                break;
        }
        CHECK(count > 0 && count < held.size());
        std::snprintf(next.path, sizeof(next.path), "a.idklvl");
        CHECK(runner.requestTransition(next, error));
        renderer.onPreRender(api);
        CHECK(runner.processPending(error) == LevelRunner::Transition::Failed);
        CHECK(runner.model() == retained && runner.generation() == 2 && player.position() == position);
        for (auto handle : held)
            renderer.DestroyMaterial(handle);
        // Repeated swaps exceed the model pool capacity if old models leak.
        for (unsigned i = 0; i < 40; ++i)
        {
            bool upper = i % 2 != 0;
            std::snprintf(next.path, sizeof(next.path), "%s", upper ? "b.idklvl" : "a.idklvl");
            uint64_t generation = runner.generation();
            CHECK(runner.requestTransition(next, error));
            renderer.onPreRender(api);
            CHECK(runner.processPending(error) == LevelRunner::Transition::Changed &&
                  runner.generation() == generation + 1);
            CHECK(player.interpolatedPosition(0) == player.position());
            player.reset({0, 7, 0});
            for (int tick = 0; tick < 240; ++tick)
                player.step({}, false, 1.0f / 60);
            CHECK(player.grounded() && std::abs(player.position().y - (upper ? 5 : 0)) < .05f);
            gfx::Camera camera;
            auto target = glm::vec3(0, upper ? 5 : 0, 0);
            camera.view = glm::lookAt(target + glm::vec3(0, 3, 5), target, glm::vec3(0, 1, 0));
            camera.proj = glm::perspectiveRH_NO(glm::radians(60.0f), 1.0f, .1f, 100.0f);
            renderer.SetCamera(camera);
            CHECK(renderer.SubmitModel(runner.model()));
            renderer.onMidRender(api);
            std::array<unsigned char, 4> pixel{};
            glReadPixels(64, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
            CHECK(pixel[upper ? 2 : 0] > 100 && pixel[upper ? 0 : 2] < 10);
            CHECK(glGetError() == GL_NO_ERROR);
        }
        runner.shutdown();
        player.reset({0, 1, 0});
        for (int i = 0; i < 120; ++i)
            player.step({}, false, 1.0f / 60);
        CHECK(player.position().y < -1); // Shutdown removes level collision without destroying the player/world.
        CHECK(runner.initialize(storage, world, player, renderer, files.root, error));
        CHECK(runner.loadInitial(files.a, error));
        CHECK(runner.generation() == 1 && player.position() == glm::vec3(0, .1f, 0));
        player.shutdown();
        runner.shutdown();
        world.shutdown();
        return true;
    }
}

int main()
{
    idk::ServiceManager services;
    idk::VideoManager video("Level transitions", 128, 128);
    idk::gfx::RenderEngine renderer;
    services.addService(&video);
    services.addService(&renderer);
    idk::EngineAPI api(&services);
    video.onInit(api);
    video.setWindowVisibility(false);
    renderer.onInit(api);
    bool ok = renderer.IsInitialized() && checks(renderer, api);
    renderer.onShutdown(api);
    video.onShutdown(api);
    SDL_Quit();
    if (ok)
        std::puts("Queued transitions, collision staging, failure rollback, GPU reuse and cleanup checks passed");
    return ok ? 0 : 1;
}
