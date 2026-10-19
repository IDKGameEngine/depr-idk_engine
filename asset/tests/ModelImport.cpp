#include "idk/asset/ModelImporter.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include "libidk/dsa/FixedList.hpp"
#include <cstring>

namespace
{
#define CHECK(value, message)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "%s\n", message);                                                                     \
            return false;                                                                                              \
        }                                                                                                              \
    } while (false)

    bool checkModel(const idk::asset::Model &model)
    {
        using namespace idk::asset;
        CHECK(model.primitives.size() == 1, "Mesh instances must share one primitive");
        const auto &primitive = model.primitives[0];
        CHECK(primitive.vertices.size() == 3 && primitive.indices.size() == 3, "Triangle geometry was lost");
        CHECK(primitive.material < model.materials.size(), "Material reference was lost");
        const auto &vertex = primitive.vertices[0];
        CHECK(std::abs(vertex.position.x + .18f) < .0001f && vertex.normal.z == 1 && vertex.uv.y == 0,
              "Vertex coordinates/normals/UV origin are incorrect");
        const auto &material = model.materials[primitive.material];
        CHECK(std::abs(material.baseColor.x - .8f) < .0001f && material.metallic == .25f && material.roughness == .75f,
              "PBR factors were not preserved");
        CHECK(material.baseColorTexture < model.textures.size() && model.images.size() == 1, "Image data was lost");
        const auto &image = model.images[0];
        CHECK(image.width == 2 && image.height == 2 && image.rgba.size() == 16, "Embedded/external PNG decode failed");
        CHECK(image.rgba[0] == 255 && image.rgba[2] == 0 && image.rgba[8] == 0 && image.rgba[10] == 255,
              "Image rows must remain in glTF's top-left convention");
        const auto &texture = model.textures[material.baseColorTexture];
        const auto &sampler = model.samplers[texture.sampler];
        CHECK(sampler.mag == Filter::Nearest && sampler.min == MinFilter::Nearest &&
                  sampler.wrapS == Wrap::ClampToEdge && sampler.wrapT == Wrap::MirroredRepeat,
              "Texture sampler settings were lost");
        size_t instances = 0;
        bool left = false, mirrored = false;
        CHECK(model.nodes.size() <= 64, "Test node capacity exceeded");
        glm::mat4 world[64];
        for (size_t i = 0; i < model.nodes.size(); ++i)
        {
            const auto &node = model.nodes[i];
            CHECK(node.parent == NoIndex || node.parent < i, "Hierarchy is not in parent-before-child order");
            world[i] = node.parent == NoIndex ? node.localTransform : world[node.parent] * node.localTransform;
            if (!node.primitives.empty())
            {
                CHECK(node.primitives.size() == 1 && node.primitives[0] == 0,
                      "Node instance does not reference shared geometry");
                ++instances;
                auto translation = glm::vec3(world[i][3]);
                left |= std::abs(translation.x + .45f) < .0001f && std::abs(translation.y - .05f) < .0001f;
                mirrored |= std::abs(translation.x - .45f) < .0001f && glm::determinant(glm::mat3(world[i])) < 0;
            }
        }
        CHECK(instances == 2 && left && mirrored, "Hierarchy transforms or mirrored instance were lost");
        return true;
    }
}

bool runTests()
{
    alignas(std::max_align_t) static std::byte modelBytes[2 * 1024 * 1024];
    alignas(std::max_align_t) static std::byte scratchBytes[4 * 1024 * 1024];
    idk::Arena output(modelBytes), scratch(scratchBytes);
    idk::asset::Model model;
    idk::asset::ImportError error;
    char path[1024];
    for (const char *filename : {"scene.gltf", "scene.glb"})
    {
        output.reset();
        std::snprintf(path, sizeof(path), "%s/%s", IDK_ASSET_TEST_DATA, filename);
        CHECK(idk::asset::ImportModel(path, output, scratch, model, error), error.message);
        CHECK(checkModel(model), "Invalid imported model");
        CHECK(model.dependencies.size() == (std::strcmp(filename, "scene.gltf") == 0 ? 3u : 1u),
              "Conversion dependencies are incorrect");
        CHECK(scratch.mark() == 0 && output.mark() > 0, "Import must release scratch and retain only output");
    }
    output.reset();
    for (const char *filename : {"required-extension.gltf", "compressed.gltf", "animated.gltf", "skinned.gltf",
                                 "lines.gltf", "uv1.gltf", "missing-image.gltf", "truncated.glb", "missing.glb"})
    {
        std::snprintf(path, sizeof(path), "%s/%s", IDK_ASSET_TEST_DATA, filename);
        CHECK(!idk::asset::ImportModel(path, output, scratch, model, error) && error.message[0],
              "Invalid input must report failure");
        CHECK(output.mark() == 0 && scratch.mark() == 0 && model.primitives.empty(),
              "Failure must restore both arenas and clear output");
    }
    alignas(std::max_align_t) std::byte tinyBytes[64];
    idk::Arena tiny(tinyBytes);
    tiny.alloc(8);
    std::snprintf(path, sizeof(path), "%s/scene.glb", IDK_ASSET_TEST_DATA);
    CHECK(!idk::asset::ImportModel(path, tiny, scratch, model, error), "Output exhaustion must fail");
    CHECK(error.code == idk::asset::ImportErrorCode::OutOfMemory && tiny.mark() == 8 && scratch.mark() == 0,
          "Output exhaustion must preserve caller marks");
    CHECK(!idk::asset::ImportModel(path, output, tiny, model, error), "Scratch exhaustion must fail");
    CHECK(error.code == idk::asset::ImportErrorCode::OutOfMemory && tiny.mark() == 8 && output.mark() == 0,
          "Scratch exhaustion must preserve caller marks");
    CHECK(!idk::asset::ImportModel(path, output, output, model, error), "Aliased arenas must be rejected");
    idk::Arena alias(modelBytes);
    CHECK(!idk::asset::ImportModel(path, output, alias, model, error), "Overlapping arena buffers must be rejected");
    tiny.reset();
    CHECK(tiny.alloc(32, 16) && tiny.mark() == 32, "Aligned arena allocation failed");
    CHECK(!tiny.alloc(64, 16) && tiny.mark() == 32, "Arena overflow must not advance cursor");
    tiny.reset();
    auto retained = tiny.array<uint32_t>(2);
    retained[0] = 42;
    const size_t retainedMark = tiny.mark();
    {
        idk::ArenaScope job(tiny);
        CHECK(tiny.alloc(16), "Temporary job allocation failed");
        const size_t jobMark = tiny.mark();
        {
            idk::ArenaScope nested(tiny);
            CHECK(!tiny.alloc(64) && tiny.failed(), "Nested exhaustion must be reported");
        }
        CHECK(tiny.mark() == jobMark && !tiny.failed(), "Nested rollback must restore usable parent storage");
    }
    CHECK(tiny.mark() == retainedMark && retained[0] == 42, "Job rollback damaged caller-owned storage");
    {
        idk::ArenaScope job(tiny);
        CHECK(tiny.alloc(8), "Committed allocation failed");
        job.commit();
    }
    CHECK(tiny.mark() > retainedMark && retained[0] == 42, "Committed storage was reclaimed");
    idk::FixedList<int, 2> list;
    CHECK(list.push(1) && list.push(2) && !list.push(3) && list.size() == 2, "Bounded list must reject overflow");
    list.clear();
    CHECK(list.empty(), "Bounded list must clear without allocation");
    return true;
}

int main()
{
    bool ok = runTests();
    if (ok)
        std::puts("Model importer checks passed");
    return ok ? 0 : 1;
}
