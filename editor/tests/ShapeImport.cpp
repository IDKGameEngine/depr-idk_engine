#include "idk/editor/ShapeDocument.hpp"
#include "idk/asset/ModelExporter.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
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

    bool sameMatrix(const glm::mat4 &a, const glm::mat4 &b)
    {
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned r = 0; r < 4; ++r)
                if (std::abs(a[c][r] - b[c][r]) > .00005f * std::max(1.0f, std::abs(a[c][r])))
                    return false;
        return true;
    }

    bool sameModel(const idk::asset::Model &a, const idk::asset::Model &b)
    {
        CHECK(a.nodes.size() == b.nodes.size(), "Object count changed");
        for (size_t i = 0; i < a.nodes.size(); ++i)
        {
            const auto &an = a.nodes[i];
            const auto &bn = b.nodes[i];
            CHECK(an.name == bn.name && sameMatrix(an.localTransform, bn.localTransform), "Name or transform changed");
            CHECK(an.primitives.size() == 1 && bn.primitives.size() == 1, "Primitive missing");
            const auto &ap = a.primitives[an.primitives[0]];
            const auto &bp = b.primitives[bn.primitives[0]];
            CHECK(ap.vertices.size() == bp.vertices.size() && ap.indices.size() == bp.indices.size(),
                  "Geometry counts changed");
            for (size_t k = 0; k < ap.vertices.size(); ++k)
            {
                const auto &av = ap.vertices[k];
                const auto &bv = bp.vertices[k];
                CHECK(av.position == bv.position && av.normal == bv.normal && av.uv == bv.uv &&
                          av.tangent == bv.tangent && av.color == bv.color,
                      "Vertex stream changed");
            }
            CHECK(std::equal(ap.indices.begin(), ap.indices.end(), bp.indices.begin()), "Triangle list changed");
            const auto &am = a.materials[ap.material];
            const auto &bm = b.materials[bp.material];
            CHECK(am.baseColor == bm.baseColor && am.metallic == bm.metallic && am.roughness == bm.roughness &&
                      am.emissive == bm.emissive && am.alphaMode == bm.alphaMode && am.alphaCutoff == bm.alphaCutoff &&
                      am.doubleSided == bm.doubleSided,
                  "Material factors changed");
        }
        return true;
    }

    struct Temporary
    {
        char directory[64] = "/tmp/idk-editor-import-XXXXXX", path[96]{};

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
}

bool checks()
{
    using namespace idk::asset;
    using namespace idk::editor;
    alignas(16) static std::byte geometryBytes[64 * 1024], sourceBytes[64 * 1024], snapshotBytes[64 * 1024];
    alignas(16) static std::byte parseBytes[2 * 1024 * 1024], scratchBytes[4 * 1024 * 1024];
    alignas(16) static std::byte bankBytes[2][2 * 1024 * 1024];
    idk::Arena geometry(geometryBytes), sourceArena(sourceBytes), snapshot(snapshotBytes), parsed(parseBytes),
        scratch(scratchBytes);
    idk::Arena bank0(bankBytes[0]), bank1(bankBytes[1]);
    ShapeDocument source;
    CHECK(source.initialize(geometry), "Geometry initialization failed");
    uint32_t box = source.add(ShapeKind::Box), sphere = source.add(ShapeKind::Sphere);
    auto *object = source.get(box);
    object->position = {3, -2, 1};
    object->rotation = {31, -57, 16};
    object->scale = {-1, 2, .5f};
    object->color = {.1f, .3f, .7f, .4f};
    object->material.metallic = .35f;
    object->material.roughness = .45f;
    object->material.emissive = {.1f, .2f, .3f};
    object->material.alphaMode = AlphaMode::Blend;
    object->material.doubleSided = true;
    std::snprintf(object->name, sizeof(object->name), "Mesh \"name\" \\ UTF-8: \xc3\xa9");
    source.get(sphere)->rotation = {23, 90, -42};
    uint32_t duplicate = source.duplicate(box);
    CHECK(duplicate != NoIndex, "Duplicate failed");
    Model original;
    CHECK(source.snapshot(sourceArena, original), "Source snapshot failed");
    Temporary temp;
    CHECK(temp.create(), "Cannot create temporary directory");
    ExportError exportError;
    CHECK(ExportModelGlb(original, temp.path, scratch, exportError), exportError.message);
    Model input;
    ImportError error;
    CHECK(ImportModel(temp.path, parsed, scratch, input, error), error.message);
    ShapeDocument doc = source;
    CHECK(doc.importModel(input, bank0, error, true), error.message);
    CHECK(doc.count() == 3 && doc.get(0)->shape == ShapeKind::Count && doc.get(0)->scale.x < 0,
          "Replace or mirrored transform failed");
    CHECK(doc.get(0)->mesh.vertices.data() != input.primitives[0].vertices.data(),
          "Import must copy parser-owned geometry");
    Model actual;
    CHECK(doc.snapshot(snapshot, actual) && sameModel(original, actual), "Import changed editor-exported model");
    CHECK(doc.get(0)->mesh.vertices.data() == doc.get(2)->mesh.vertices.data(),
          "Instances must share document geometry");
    // Reclaim every byte owned by the file importer before editing and exporting.
    parsed.reset();
    std::memset(parseBytes, 0xcc, sizeof(parseBytes));
    uint32_t copied = doc.duplicate(0);
    CHECK(copied == 3 && doc.get(copied)->mesh.vertices.data() == doc.get(0)->mesh.vertices.data(),
          "Imported duplicate failed");
    CHECK(doc.remove(copied), "Imported delete failed");
    glm::vec3 minimum, maximum;
    CHECK(doc.bounds(minimum, maximum) && maximum.x > minimum.x, "Imported scene bounds missing");
    snapshot.reset();
    CHECK(doc.snapshot(snapshot, actual) && ExportModelGlb(actual, temp.path, scratch, exportError, true),
          exportError.message);
    CHECK(ImportModel(temp.path, parsed, scratch, input, error) && sameModel(original, input),
          "Edited model cannot roundtrip");
    // Append compacts old meshes and adds new objects, without retaining the old bank.
    size_t oldUsage = bank0.mark();
    CHECK(doc.importModel(input, bank1, error) && doc.count() == 6, error.message);
    CHECK(bank1.mark() <= oldUsage * 2, "Append copied shared instances more than once");
    bank0.reset();
    std::memset(bankBytes[0], 0xdd, sizeof(bankBytes[0]));
    CHECK(sameMatrix(doc.get(0)->transform(), original.nodes[0].localTransform),
          "Compaction changed retained transforms");
    CHECK(doc.pick({3, -2, 10}, {0, 0, -1}) != NoIndex, "Picking imported geometry failed");
    CHECK(doc.get(0)->mesh.vertices[0].normal == original.primitives[0].vertices[0].normal,
          "Compaction retained old arena data");
    // Replace works even when the old document is full.
    while (doc.count() < ShapeDocument::Capacity)
        CHECK(doc.add(ShapeKind::Plane) != NoIndex, "Failed to fill document");
    CHECK(!doc.importModel(input, bank0, error) && error.code == ImportErrorCode::Unsupported &&
              doc.count() == ShapeDocument::Capacity && bank0.mark() == 0,
          "Overflow must leave document and arena intact");
    CHECK(doc.importModel(input, bank0, error, true) && doc.count() == 3, error.message);
    bank1.reset();
    ShapeDocument before = doc;
    alignas(16) std::byte tinyBytes[64];
    idk::Arena tiny(tinyBytes);
    tiny.alloc(8);
    CHECK(!doc.importModel(input, tiny, error, true) && error.code == ImportErrorCode::OutOfMemory && tiny.mark() == 8,
          "Memory exhaustion must roll back");
    CHECK(doc.count() == before.count() && doc.get(0)->mesh.vertices.data() == before.get(0)->mesh.vertices.data(),
          "Failed replacement damaged document");
    // Alias protection includes replace mode, where old data is still needed on failure.
    CHECK(!doc.importModel(input, bank0, error, true) && error.code == ImportErrorCode::InvalidInput,
          "Overlapping destination must be rejected");
    Model invalid = input;
    Material textured = input.materials[0];
    textured.baseColorTexture = 0;
    invalid.materials = {&textured, 1};
    CHECK(!doc.importModel(invalid, bank1, error, true) && error.code == ImportErrorCode::Unsupported,
          "Texture loss must not be silent");
    // Reject bad transforms after copying some geometry; retain all old resources.
    glm::mat4 oldTransform = input.nodes[1].localTransform;
    input.nodes[1].localTransform[1][0] += .75f;
    CHECK(!doc.importModel(input, bank1, error, true) && error.code == ImportErrorCode::Unsupported &&
              bank1.mark() == 0,
          "Partial import must roll back on shear");
    input.nodes[1].localTransform = oldTransform;
    input.nodes[1].localTransform[0] = glm::vec4(0);
    CHECK(!doc.importModel(input, bank1, error, true) && bank1.mark() == 0, "Zero scale must be rejected atomically");
    input.nodes[1].localTransform = oldTransform;
    input.nodes[1].parent = 2;
    CHECK(!doc.importModel(input, bank1, error, true) && error.code == ImportErrorCode::InvalidInput,
          "Forward parents must be rejected");
    input.nodes[1].parent = NoIndex;
    CHECK(doc.count() == before.count() &&
              doc.get(0)->mesh.vertices[0].position == before.get(0)->mesh.vertices[0].position,
          "Rejected inputs changed the scene");
    // Group nodes and multi-primitive nodes flatten to editable world-space objects.
    Node nodes[3];
    uint32_t refs[] = {0, 1};
    nodes[0].localTransform = glm::translate(glm::mat4(1), glm::vec3(5, 0, 0));
    nodes[1].name = "Grouped meshes";
    nodes[1].parent = 0;
    nodes[1].primitives = refs;
    nodes[1].localTransform = glm::translate(glm::mat4(1), glm::vec3(0, 2, 0));
    nodes[2].name = "Instance";
    nodes[2].parent = 0;
    nodes[2].primitives = {refs, 1};
    Model grouped = input;
    grouped.nodes = nodes;
    CHECK(doc.importModel(grouped, bank1, error, true) && doc.count() == 3, error.message);
    CHECK(doc.get(0)->position == glm::vec3(5, 2, 0) && doc.get(2)->position == glm::vec3(5, 0, 0),
          "Hierarchy world transform lost");
    CHECK(doc.get(0)->mesh.vertices.data() == doc.get(2)->mesh.vertices.data(), "Hierarchy instancing lost");
    bank0.reset();
    snapshot.reset();
    CHECK(doc.snapshot(snapshot, actual), "Grouped snapshot failed");
    for (const auto &node : actual.nodes)
        CHECK(node.parent == NoIndex, "Document should export flattened objects");
    // Imported nonindexed triangles must remain pickable and exportable.
    Vertex triangle[3];
    triangle[0].position = {-1, -1, 0};
    triangle[1].position = {1, -1, 0};
    triangle[2].position = {0, 1, 0};
    triangle[0].color = {.2f, .3f, .4f, .5f};
    Primitive primitive;
    primitive.vertices = triangle;
    Node node;
    uint32_t reference = 0;
    node.primitives = {&reference, 1};
    Model nonindexed;
    nonindexed.nodes = {&node, 1};
    nonindexed.primitives = {&primitive, 1};
    CHECK(doc.importModel(nonindexed, bank0, error, true) && doc.pick({0, 0, 5}, {0, 0, -1}) == 0,
          "Nonindexed import/picking failed");
    CHECK(doc.get(0)->mesh.vertices[0].color == triangle[0].color, "Vertex colours lost");
    snapshot.reset();
    CHECK(doc.snapshot(snapshot, actual) && ExportModelGlb(actual, temp.path, scratch, exportError, true),
          exportError.message);
    // Exercise signed TRS and both gimbal-lock branches independently of file parsing.
    for (float y : {-90.0f, -89.999f, 0.0f, 89.999f, 90.0f})
        for (float xScale : {-2.0f, 2.0f})
        {
            ShapeObject sourceObject, restored;
            sourceObject.position = {1, -2, 3};
            sourceObject.rotation = {31, y, 54};
            sourceObject.scale = {xScale, 3, .25f};
            CHECK(restored.setTransform(sourceObject.transform()) &&
                      sameMatrix(restored.transform(), sourceObject.transform()),
                  "Signed TRS or gimbal-lock decomposition failed");
        }
    CHECK(!doc.importModel({}, bank1, error), "Empty model must be rejected");
    // Exercise the JSON glTF file path as well as the editor's GLB output.
    parsed.reset();
    bank1.reset();
    CHECK(ImportModel(IDK_EDITOR_TEST_DATA "/coloured-triangles.gltf", parsed, scratch, input, error), error.message);
    CHECK(doc.importModel(input, bank1, error, true) && doc.count() == 2, error.message);
    CHECK(doc.get(0)->scale.x < 0 && doc.get(1)->position.x == 1 && doc.get(0)->material.alphaMode == AlphaMode::Mask,
          "glTF hierarchy, reflection or alpha mode lost");
    CHECK(doc.get(0)->mesh.vertices.data() == doc.get(1)->mesh.vertices.data() &&
              doc.get(0)->mesh.vertices[0].color.r == 1,
          "glTF geometry sharing/vertex colours lost");
    CHECK(std::abs(doc.get(0)->material.metallic - .25f) < .0001f && doc.get(0)->material.doubleSided,
          "glTF material factors lost");
    doc.clear();
    CHECK(doc.count() == 0 && doc.add(ShapeKind::Box) != NoIndex, "Clear must allow subsequent shape creation");
    return true;
}

int main()
{
    if (!checks())
        return 1;
    std::puts("Editor import, compaction, rollback, signed transforms and GLB roundtrip checks passed");
    return 0;
}
