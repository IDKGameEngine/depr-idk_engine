#include "idk/editor/EditorSession.hpp"
#include "idk/editor/ScenePreview.hpp"
#include "idk/gfx/RenderEngine.hpp"
#include "idk/EngineAPI.hpp"
#include "libidk/gpu/gl.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cstdio>
#include <cstdlib>
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

    struct Temporary
    {
        char directory[64] = "/tmp/idk-editor-preview-XXXXXX";
        char path[96]{};

        bool create()
        {
            if (!mkdtemp(directory))
                return false;
            std::snprintf(path, sizeof(path), "%s/model.glb", directory);
            return true;
        }

        ~Temporary()
        {
            if (path[0])
                unlink(path);
            rmdir(directory);
        }
    };

    std::array<uint8_t, 4> pixel()
    {
        std::array<uint8_t, 4> color{};
        glReadPixels(64, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
        return color;
    }

    int glErrors = 0;

    void APIENTRY debugCallback(GLenum, GLenum type, GLuint, GLenum, GLsizei, const GLchar *, const void *)
    {
        if (type == GL_DEBUG_TYPE_ERROR)
            ++glErrors;
    }
} // namespace

bool checks(idk::gfx::RenderEngine &renderer, idk::EngineAPI &api)
{
    using namespace idk::editor;
    glDebugMessageCallback(debugCallback, nullptr);
    static EditorSession::Storage storage;
    EditorSession session;
    CHECK(session.initialize(storage), session.status());
    ScenePreview preview;
    CHECK(preview.initialize(renderer, session.document()), "Preview initialization failed");
    renderer.SetCamera({});
    renderer.SetClearColor({0, 0, 0, 1});
    const char *error = nullptr;

    session.addShape(idk::asset::ShapeKind::Box);
    session.selected()->position = {0, 0, 0};
    session.selected()->color = {1, 0, 0, 1};
    renderer.onPreRender(api);
    CHECK(preview.submit(renderer, session.document(), error), error);
    renderer.onMidRender(api);
    CHECK(pixel()[0] > 80 && pixel()[1] < 10, "Native shape preview did not draw");

    session.selected()->color = {0, 1, 0, 1};
    renderer.onPreRender(api);
    CHECK(preview.submit(renderer, session.document(), error), error);
    renderer.onMidRender(api);
    CHECK(pixel()[1] > 80 && pixel()[0] < 10, "Material editing did not update the preview");
    session.addShape(idk::asset::ShapeKind::Sphere);
    session.selected()->position = {3, 0, 0};
    Temporary file;
    CHECK(file.create(), "Cannot create temporary directory");
    CHECK(session.exportModel(file.path) == EditorSession::ExportResult::Saved, session.status());
    CHECK(session.exportModel(file.path) == EditorSession::ExportResult::AlreadyExists,
          "Overwrite confirmation was bypassed");

    EditorSession::ImportCandidate candidate;
    CHECK(session.prepareImport(file.path, true, candidate), session.status());
    CHECK(!session.document().get(0)->isImported() && candidate.document.get(0)->isImported(),
          "Preparation mutated the live document");
    CHECK(preview.rebuild(renderer, candidate.document, error), error);
    session.commitImport(candidate, file.path);
    renderer.onPreRender(api);
    CHECK(preview.submit(renderer, session.document(), error), error);
    renderer.onMidRender(api);
    CHECK(pixel()[1] > 80 && pixel()[0] < 10, "Imported preview changed pixels");
    CHECK(session.selection() == 0 && session.document().count() == 2, "Committed import lost selection or objects");

    // Fail on a second mesh, after candidate GPU resources have been allocated.
    CHECK(session.prepareImport(file.path, true, candidate), session.status());
    uint32_t invalidIndices[] = {0, 1, UINT32_MAX};
    candidate.document.get(1)->mesh.indices = invalidIndices;
    CHECK(!preview.rebuild(renderer, candidate.document, error) && error, "Invalid candidate upload should fail");
    renderer.onPreRender(api);
    CHECK(preview.submit(renderer, session.document(), error), "Mesh upload failure damaged the old GPU scene");
    renderer.onMidRender(api);
    CHECK(pixel()[1] > 80 && session.document().get(0)->color.g == 1, "Mesh failure damaged the live scene");

    CHECK(session.prepareImport(file.path, true, candidate), session.status());
    // Exhaust the material pool to exercise a real GPU-resource failure.
    std::array<idk::gfx::MaterialHandle, 2048> heldMaterials{};
    idk::gfx::MaterialDesc description;
    description.shader = renderer.GetDefaultShader();
    size_t heldCount = 0;
    for (; heldCount < heldMaterials.size(); ++heldCount)
    {
        heldMaterials[heldCount] = renderer.CreateMaterial(description);
        if (!heldMaterials[heldCount].isValid())
            break;
    }
    CHECK(heldCount > 0 && heldCount < heldMaterials.size(), "Could not exhaust the bounded material pool");
    CHECK(!preview.rebuild(renderer, candidate.document, error), "Exhausted material pool should reject candidate");
    renderer.onPreRender(api);
    CHECK(preview.submit(renderer, session.document(), error), "Material failure damaged the old GPU scene");
    renderer.onMidRender(api);
    CHECK(pixel()[1] > 80, "Material failure changed the old scene");
    for (auto material : heldMaterials)
        renderer.DestroyMaterial(material);
    CHECK(!session.prepareImport("/missing/idk-editor-model.glb", true, candidate), "Missing input should fail");
    CHECK(session.document().count() == 2 && session.document().get(0)->color.g == 1,
          "Parse failure damaged the live session");

    session.select(0);
    session.duplicateSelection();
    CHECK(session.document().count() == 3 && session.selected()->isImported(),
          "Duplicate no longer selects the imported copy");
    session.deleteSelection();
    CHECK(session.document().count() == 2 && session.selection() == idk::asset::NoIndex,
          "Delete no longer clears selection");
    preview.clear(renderer);
    session.clear();
    renderer.onPreRender(api);
    CHECK(preview.submit(renderer, session.document(), error), error);
    renderer.onMidRender(api);
    CHECK(pixel()[0] == 0 && pixel()[1] == 0 && pixel()[2] == 0, "Clear left stale GPU draws");

    session.addShape(idk::asset::ShapeKind::Box);
    session.selected()->position = {0, 0, 0};
    session.selected()->color = {0, 0, 1, 1};
    preview.shutdown(renderer);
    CHECK(preview.initialize(renderer, session.document()), "Preview reinitialization failed after cleanup");
    renderer.onPreRender(api);
    CHECK(preview.submit(renderer, session.document(), error), error);
    renderer.onMidRender(api);
    CHECK(pixel()[2] > 80 && pixel()[0] < 10, "Resource reuse retained stale meshes or materials");
    preview.shutdown(renderer);
    CHECK(glErrors == 0 && glGetError() == GL_NO_ERROR, "Preview operations generated GL errors");
    return true;
}

int main()
{
    idk::ServiceManager services;
    idk::VideoManager video("Editor preview test", 128, 128);
    idk::gfx::RenderEngine renderer;
    services.addService(&video);
    services.addService(&renderer);
    idk::EngineAPI api(&services);
    video.onInit(api);
    video.setWindowVisibility(false);
    renderer.onInit(api);
    bool ok = renderer.IsInitialized() && checks(renderer, api);
    if (renderer.GetContextHandle())
        glDebugMessageCallback(nullptr, nullptr);
    renderer.onShutdown(api);
    video.onShutdown(api);
    SDL_Quit();
    if (ok)
        std::puts("Editor session transactions and preview resource checks passed");
    return ok ? 0 : 1;
}
