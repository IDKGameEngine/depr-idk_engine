#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace idk::asset
{
    inline constexpr uint32_t NoIndex = std::numeric_limits<uint32_t>::max();

    // Caller-arena-owned CPU data. No Assimp, graphics API, or on-disk ABI dependencies.
    // Right-handed, +Y up, metres; UV (0,0) and image row zero are top-left.
    struct Vertex
    {
        glm::vec3 position{0};
        glm::vec3 normal{0, 0, 1};
        glm::vec2 uv{0};
        glm::vec4 tangent{1, 0, 0, 1};
        glm::vec4 color{1};
    };

    struct Primitive
    {
        std::string_view name;
        std::span<Vertex> vertices;
        std::span<uint32_t> indices;
        uint32_t material = NoIndex;
        bool hasTangents = false;
    };

    struct Image
    {
        std::string_view source;
        uint32_t width = 0, height = 0;
        std::span<uint8_t> rgba;
    };

    enum class Wrap
    {
        Repeat,
        ClampToEdge,
        MirroredRepeat
    };
    enum class Filter
    {
        Nearest,
        Linear
    };
    enum class MinFilter
    {
        Nearest,
        Linear,
        NearestMipmapNearest,
        LinearMipmapNearest,
        NearestMipmapLinear,
        LinearMipmapLinear
    };

    struct Sampler
    {
        Wrap wrapS = Wrap::Repeat, wrapT = Wrap::Repeat;
        Filter mag = Filter::Linear;
        MinFilter min = MinFilter::LinearMipmapLinear;
        bool operator==(const Sampler &) const = default;
    };

    struct Texture
    {
        uint32_t image = NoIndex;
        uint32_t sampler = NoIndex;
        bool operator==(const Texture &) const = default;
    };

    enum class AlphaMode
    {
        Opaque,
        Mask,
        Blend
    };

    struct Material
    {
        std::string_view name;
        glm::vec4 baseColor{1};
        float metallic = 1.0f, roughness = 1.0f;
        glm::vec3 emissive{0};
        float normalScale = 1.0f, occlusionStrength = 1.0f;
        uint32_t baseColorTexture = NoIndex;
        // Metallic = B, roughness = G; occlusion = R in the respective maps.
        uint32_t metallicRoughnessTexture = NoIndex;
        uint32_t normalTexture = NoIndex, occlusionTexture = NoIndex, emissiveTexture = NoIndex;
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
    };

    struct Node
    {
        std::string_view name;
        // Parents precede children. NoIndex marks a scene root.
        uint32_t parent = NoIndex;
        glm::mat4 localTransform{1};
        std::span<uint32_t> primitives;
    };

    struct Model
    {
        std::span<Primitive> primitives;
        std::span<Node> nodes;
        std::span<Material> materials;
        std::span<Image> images;
        std::span<Texture> textures;
        std::span<Sampler> samplers;
        std::span<std::string_view> dependencies;
        std::span<std::string_view> warnings;
    };
}
