#include "idk/editor/ShapeDocument.hpp"
#include "idk/asset/ModelExporter.hpp"
#include "idk/asset/ModelImporter.hpp"
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
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

    struct Temporary
    {
        char directory[64] = "/tmp/idk-shape-tests-XXXXXX";
        char file[96]{};

        bool create()
        {
            if (!mkdtemp(directory))
                return false;
            std::snprintf(file, sizeof(file), "%s/model.glb", directory);
            return true;
        }

        ~Temporary()
        {
            if (file[0])
                unlink(file);
            rmdir(directory);
        }
    };

    uint64_t hashFile(const char *path)
    {
        FILE *file = std::fopen(path, "rb");
        if (!file)
            return 0;
        uint64_t hash = 14695981039346656037ull;
        unsigned char bytes[4096];
        size_t count = 0;
        while ((count = std::fread(bytes, 1, sizeof(bytes), file)))
            for (size_t i = 0; i < count; ++i)
                hash = (hash ^ bytes[i]) * 1099511628211ull;
        if (std::ferror(file))
            hash = 0;
        std::fclose(file);
        return hash;
    }

    bool geometryChecks(const idk::asset::Primitive &p)
    {
        CHECK(!p.vertices.empty() && !p.indices.empty() && p.indices.size() % 3 == 0, "Missing triangles");
        for (const auto &v : p.vertices)
        {
            CHECK(std::abs(glm::length(v.normal) - 1) < .0001f, "Normals must be normalized");
            CHECK(std::abs(glm::length(glm::vec3(v.tangent)) - 1) < .0001f &&
                      std::abs(glm::dot(v.normal, glm::vec3(v.tangent))) < .0001f,
                  "Invalid tangent frame");
            CHECK(v.uv.x >= 0 && v.uv.x <= 1 && v.uv.y >= 0 && v.uv.y <= 1, "UVs exceed unit square");
        }
        for (size_t i = 0; i < p.indices.size(); i += 3)
        {
            CHECK(p.indices[i] < p.vertices.size() && p.indices[i + 1] < p.vertices.size() &&
                      p.indices[i + 2] < p.vertices.size(),
                  "Index out of range");
            const auto &a = p.vertices[p.indices[i]];
            const auto &b = p.vertices[p.indices[i + 1]];
            const auto &c = p.vertices[p.indices[i + 2]];
            glm::vec3 normal = glm::cross(b.position - a.position, c.position - a.position);
            CHECK(glm::length(normal) > .00001f, "Degenerate triangle");
            CHECK(glm::dot(normal, a.normal + b.normal + c.normal) > 0, "Triangle winds inward");
        }
        return true;
    }
}

bool runTests()
{
    using namespace idk::asset;
    using namespace idk::editor;
    alignas(16) static std::byte geometryBytes[64 * 1024], snapshotBytes[64 * 1024], exportBytes[512 * 1024];
    alignas(16) static std::byte importBytes[2 * 1024 * 1024], scratchBytes[4 * 1024 * 1024];
    idk::Arena geometry(geometryBytes), snapshot(snapshotBytes), output(importBytes), scratch(scratchBytes),
        exportScratch(exportBytes);
    ShapeDocument doc;
    CHECK(doc.initialize(geometry), "Document initialization failed");
    ShapeObject transformSource, decomposed;
    transformSource.position = {3, 4, -2};
    transformSource.rotation = {37, -52, 81};
    transformSource.scale = {1, 2, .5f};
    auto matrix = transformSource.transform();
    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(matrix), glm::value_ptr(decomposed.position),
                                          glm::value_ptr(decomposed.rotation), glm::value_ptr(decomposed.scale));
    auto reconstructed = decomposed.transform();
    for (unsigned c = 0; c < 4; ++c)
        for (unsigned r = 0; r < 4; ++r)
            CHECK(std::abs(reconstructed[c][r] - matrix[c][r]) < .00001f,
                  "Document and ImGuizmo must agree on TRS rotation order");
    for (uint32_t i = 0; i < ShapeDocument::ShapeCount; ++i)
    {
        CHECK(geometryChecks(doc.geometry(ShapeKind(i))), "Bad basic shape");
        uint32_t slot = doc.add(ShapeKind(i));
        CHECK(slot == i, "Initial slot allocation failed");
        auto *object = doc.get(slot);
        object->position = {float(i) * 2, 1, -float(i)};
        object->rotation = {15, -25, 38};
        object->scale = {1, 2, .5f};
        object->color = {float(i) / 4, .7f, .3f, 1};
    }
    std::snprintf(doc.get(0)->name, sizeof(doc.get(0)->name), "Box \"quoted\" \\ tab\t");
    uint32_t copy = doc.duplicate(0);
    CHECK(copy == 4 && doc.count() == 5 && doc.get(copy)->color == doc.get(0)->color, "Duplicate failed");
    CHECK(doc.get(copy)->position == doc.get(0)->position + glm::vec3(.25f, 0, .25f), "Duplicate should be offset");
    Model model;
    CHECK(doc.snapshot(snapshot, model), "Snapshot failed");
    CHECK(model.nodes.size() == 5 && model.primitives[0].vertices.data() == model.primitives[4].vertices.data(),
          "Snapshot must share immutable geometry");
    Temporary temp;
    CHECK(temp.create(), "Cannot create temporary directory");
    ExportError error;
    exportScratch.alloc(16);
    CHECK(ExportModelGlb(model, temp.file, exportScratch, error), error.message);
    CHECK(exportScratch.mark() == 16, "Export scratch not restored");
    uint64_t original = hashFile(temp.file);
    CHECK(original != 0, "Export file missing");
    ImportError importError;
    Model loaded;
    CHECK(ImportModel(temp.file, output, scratch, loaded, importError), importError.message);
    CHECK(loaded.nodes.size() == model.nodes.size() && loaded.materials.size() == model.materials.size(),
          "GLB lost objects or materials");
    for (size_t i = 0; i < model.nodes.size(); ++i)
    {
        const auto &expected = model.nodes[i];
        const auto &actual = loaded.nodes[i];
        CHECK(actual.name == expected.name && actual.parent == NoIndex, "GLB names or scene roots differ");
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned r = 0; r < 4; ++r)
                CHECK(std::abs(actual.localTransform[c][r] - expected.localTransform[c][r]) < .00001f,
                      "GLB changed transform");
        CHECK(actual.primitives.size() == 1, "Lost primitive reference");
        const auto &a = loaded.primitives[actual.primitives[0]];
        const auto &e = model.primitives[expected.primitives[0]];
        CHECK(a.vertices.size() == e.vertices.size() && a.indices.size() == e.indices.size(), "Geometry size changed");
        for (size_t k = 0; k < a.vertices.size(); ++k)
        {
            CHECK(glm::length(a.vertices[k].position - e.vertices[k].position) < .00001f, "Position changed");
            CHECK(glm::length(a.vertices[k].normal - e.vertices[k].normal) < .00001f, "Normal changed");
            CHECK(glm::length(a.vertices[k].tangent - e.vertices[k].tangent) < .00001f, "Tangent changed");
            CHECK(glm::length(a.vertices[k].uv - e.vertices[k].uv) < .00001f, "UV changed");
        }
        for (size_t k = 0; k < a.indices.size(); ++k)
            CHECK(a.indices[k] == e.indices[k], "Triangle changed");
        const auto &material = loaded.materials[a.material];
        CHECK(glm::length(material.baseColor - model.materials[e.material].baseColor) < .00001f &&
                  material.metallic == 0,
              "Material changed");
        CHECK(material.doubleSided == model.materials[e.material].doubleSided, "Plane must remain double-sided");
    }
    CHECK(!ExportModelGlb(model, temp.file, exportScratch, error) && error.code == ExportErrorCode::AlreadyExists,
          "Existing files require overwrite");
    CHECK(hashFile(temp.file) == original, "Refused overwrite changed destination");
    alignas(16) std::byte tinyBytes[64];
    idk::Arena tiny(tinyBytes);
    tiny.alloc(8);
    CHECK(!ExportModelGlb(model, temp.file, tiny, error, true) && error.code == ExportErrorCode::OutOfMemory,
          "Export must report arena exhaustion");
    CHECK(tiny.mark() == 8 && hashFile(temp.file) == original, "Failed export changed memory or destination");
    // Enough for metadata, deliberately too little for JSON.
    alignas(16) std::byte jsonBytes[1024];
    idk::Arena tinyJson(jsonBytes);
    CHECK(!ExportModelGlb(model, temp.file, tinyJson, error, true) && error.code == ExportErrorCode::OutOfMemory,
          "JSON overflow must fail");
    model.materials[0].baseColor.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(!ExportModelGlb(model, temp.file, exportScratch, error, true) && error.code == ExportErrorCode::InvalidInput,
          "NaN must be rejected");
    CHECK(hashFile(temp.file) == original, "Validation failure changed existing file");
    model.materials[0].baseColor.x = .9f;
    CHECK(ExportModelGlb(model, temp.file, exportScratch, error, true), error.message);
    CHECK(hashFile(temp.file) != original, "Overwrite did not replace model");
    // Exercise hierarchy, a shared mesh instance, and an empty grouping node.
    model.nodes[1].parent = 0;
    model.nodes[2].primitives = model.nodes[1].primitives;
    model.nodes[3].primitives = {};
    CHECK(ExportModelGlb(model, temp.file, exportScratch, error, true), error.message);
    output.reset();
    CHECK(ImportModel(temp.file, output, scratch, loaded, importError), importError.message);
    bool foundChild = false, foundEmpty = false;
    for (const auto &node : loaded.nodes)
    {
        foundChild |= node.parent != NoIndex;
        foundEmpty |= node.primitives.empty();
    }
    CHECK(foundChild && foundEmpty, "Hierarchy or empty node lost");
    // Snapshot/allocation failures must leave the arena usable.
    CHECK(!doc.snapshot(tiny, loaded) && tiny.mark() == 8 && loaded.nodes.empty(), "Snapshot failure must roll back");
    Primitive failedShape;
    CHECK(!CreateShape(ShapeKind::Sphere, tiny, failedShape) && tiny.mark() == 8 && failedShape.vertices.empty(),
          "Shape failure must roll back");
    ShapeDocument failedDocument;
    CHECK(!failedDocument.initialize(tiny) && tiny.mark() == 8, "Partial shape initialization must roll back");
    alignas(16) static std::byte retryBytes[64 * 1024];
    idk::Arena retry(retryBytes);
    CHECK(failedDocument.initialize(retry), "Initialization retry should succeed");
    CHECK(doc.remove(1) && !doc.remove(1) && doc.get(1) == nullptr && doc.count() == 4, "Delete failed");
    CHECK(doc.add(ShapeKind::Box) == 1, "Deleted slots must be reused");
    while (doc.count() < ShapeDocument::Capacity)
        CHECK(doc.add(ShapeKind::Box) != NoIndex, "Capacity filled prematurely");
    CHECK(doc.add(ShapeKind::Box) == NoIndex && doc.duplicate(0) == NoIndex, "Full documents must report failure");
    snapshot.reset();
    CHECK(doc.snapshot(snapshot, model) && model.nodes.size() == ShapeDocument::Capacity,
          "Full document snapshot failed");
    CHECK(ExportModelGlb(model, temp.file, exportScratch, error, true), error.message);
    output.reset();
    CHECK(ImportModel(temp.file, output, scratch, loaded, importError) &&
              loaded.nodes.size() == ShapeDocument::Capacity,
          "Full model roundtrip failed");
    doc.clear();
    CHECK(doc.count() == 0 && doc.pick({0, 0, 5}, {0, 0, -1}) == NoIndex, "Clear failed");
    uint32_t far = doc.add(ShapeKind::Box), near = doc.add(ShapeKind::Sphere);
    doc.get(near)->position.z = 2;
    CHECK(doc.pick({0, .5f, 5}, {0, 0, -1}) == near, "Picking must find closest triangle");
    doc.remove(near);
    doc.get(far)->rotation = {20, 30, 40};
    doc.get(far)->scale = {1, 2, 3};
    CHECK(doc.pick({0, .5f, 5}, {0, 0, -1}) == far, "Picking transformed shapes failed");
    CHECK(doc.pick({10, 10, 5}, {0, 0, -1}) == NoIndex, "Ray miss must clear selection");
    doc.clear();
    uint32_t plane = doc.add(ShapeKind::Plane);
    CHECK(doc.pick({0, 5, 0}, {0, -1, 0}) == plane && doc.pick({0, -5, 0}, {0, 1, 0}) == plane,
          "Picking double-sided plane failed");
    return true;
}

int main()
{
    if (!runTests())
        return 1;
    std::puts("Shape generation, document, picking and GLB roundtrip checks passed");
    return 0;
}
